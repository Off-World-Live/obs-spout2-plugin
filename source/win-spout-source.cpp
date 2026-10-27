/**
 * Copyright Off World Live Ltd (https://offworld.live), 2019-2021
 *
 * and licenced under the GPL v2 (https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
 *
 * Many thanks to authors of https://github.com/baffler/OBS-OpenVR-Input-Plugin which
 * was used as guidance to working with the OBS Studio APIs
 */

#include <obs-module.h>
#include <util/threading.h>
#include "win-spout.h"

#include "SpoutDX.h"

#define debug(message, ...) blog(LOG_DEBUG, "[%s] " message, obs_source_get_name(context->source), ##__VA_ARGS__)
#define info(message, ...) blog(LOG_INFO, "[%s] " message, obs_source_get_name(context->source), ##__VA_ARGS__)
#define warn(message, ...) blog(LOG_WARNING, "[%s] " message, obs_source_get_name(context->source), ##__VA_ARGS__)

#define SPOUT_SENDER_LIST "spoutsenders"
#define USE_FIRST_AVAILABLE_SENDER "usefirstavailablesender"
#define SPOUT_TICK_SPEED_LIMIT "tickspeedlimit"
#define SPOUT_COMPOSITE_MODE "compositemode"

#define COMPOSITE_MODE_OPAQUE 1
#define COMPOSITE_MODE_ALPHA 2
#define COMPOSITE_MODE_DEFAULT 3
#define COMPOSITE_MODE_PREMULTIPLIED 4

// Placeholder size reported until a sender is connected.
#define PLACEHOLDER_SIZE 100

/*
 * Receiving is done with a spoutDX receiver on OBS's own D3D11 device, the same way the
 * filter sends. spoutDX::ReceiveTexture() copies the sender's shared texture into a texture
 * we own, under Spout's access mutex (keyed mutex or named mutex, whichever the sender uses)
 * and only when the sender has produced a new frame. Reading the shared texture directly, as
 * earlier versions did, was unsynchronised and could tear or read half-rendered frames (#86).
 *
 * All Spout and texture work happens on the graphics thread in video_tick; update/show/hide
 * only set flags that the next tick acts on.
 */
struct spout_source {
	obs_source_t *source;
	spoutDX *receiver;

	// Settings (written by update() on the UI thread, read in tick)
	char senderName[256];
	bool useFirstSender;
	ULONGLONG tick_speed_limit;
	ULONGLONG composite_mode;
	volatile bool pending_reconnect; // settings changed: release and reconnect

	// Graphics-thread state
	bool dx_initialised;
	gs_texture_t *texture; // our copy of the sender texture, same DXGI format family
	gs_texrender_t *linear_texrender;
	int width;
	int height;
	ULONGLONG lastCheckTick;
	int spout_status;
	int render_status;
};

static gs_color_format gs_format_for_dxgi(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		return GS_BGRA; // typeless + sRGB view, CopyResource-compatible with the UNORM sender
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		return GS_RGBA;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		return GS_RGBA16F;
	case DXGI_FORMAT_R16G16B16A16_UNORM:
		return GS_RGBA16;
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		return GS_R10G10B10A2;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		return GS_RGBA32F;
	default:
		return GS_UNKNOWN;
	}
}

// Names for the DXGI formats Spout senders commonly use. DirectX 9 senders
// report 0, and their texture is always D3DFMT_A8R8G8B8.
static const char *win_spout_format_name(DWORD format)
{
	switch (format) {
	case 0:
		return "DirectX 9 sender (8-bit BGRA)";
	case 10:
		return "R16G16B16A16_FLOAT (16-bit float)";
	case 11:
		return "R16G16B16A16_UNORM (16-bit)";
	case 24:
		return "R10G10B10A2_UNORM (10-bit)";
	case 28:
		return "R8G8B8A8_UNORM (8-bit)";
	case 87:
		return "B8G8R8A8_UNORM (8-bit)";
	case 91:
		return "B8G8R8A8_UNORM_SRGB (8-bit)";
	default:
		return "unrecognised";
	}
}

static void win_spout_source_log_once(spout_source *context, int status, int level, const char *message)
{
	if (context->spout_status == status) {
		return;
	}
	context->spout_status = status;
	blog(level, "[%s] %s", obs_source_get_name(context->source), message);
}

static void win_spout_source_release_texture(spout_source *context)
{
	if (context->texture || context->linear_texrender) {
		obs_enter_graphics();
		gs_texture_destroy(context->texture);
		gs_texrender_destroy(context->linear_texrender);
		obs_leave_graphics();
		context->texture = NULL;
		context->linear_texrender = NULL;
	}
	context->width = context->height = PLACEHOLDER_SIZE;
}

// Graphics thread only.
static void win_spout_source_disconnect(spout_source *context)
{
	if (context->receiver) {
		context->receiver->ReleaseReceiver();
	}
	win_spout_source_release_texture(context);
}

// Graphics thread only: share OBS's D3D11 device with the receiver.
static bool win_spout_source_init_dx(spout_source *context)
{
	if (context->dx_initialised) {
		return true;
	}

	ID3D11Device *const d3d_device = (ID3D11Device *)gs_get_device_obj();
	if (!d3d_device) {
		win_spout_source_log_once(context, -1, LOG_ERROR, "Failed to retrieve the OBS D3D11 device");
		return false;
	}

	context->receiver->SetMaxSenders(255);
	if (!context->receiver->OpenDirectX11(d3d_device)) {
		win_spout_source_log_once(context, -2, LOG_ERROR, "Failed to open DX11 for the Spout receiver");
		return false;
	}

	context->dx_initialised = true;
	return true;
}

// Graphics thread only: (re)create our texture to match the connected sender.
static bool win_spout_source_create_texture(spout_source *context)
{
	spoutDX *receiver = context->receiver;
	const unsigned int width = receiver->GetSenderWidth();
	const unsigned int height = receiver->GetSenderHeight();
	const DXGI_FORMAT dxgi_format = receiver->GetSenderFormat();

	win_spout_source_release_texture(context);

	if (width == 0 || height == 0) {
		return false;
	}

	gs_color_format format = gs_format_for_dxgi(dxgi_format);
	if (format == GS_UNKNOWN) {
		if (context->spout_status != -9) {
			warn("Sender %s uses an unsupported texture format (DXGI format %d)", receiver->GetSenderName(),
			     (int)dxgi_format);
			context->spout_status = -9;
		}
		return false;
	}

	obs_enter_graphics();
	context->texture = gs_texture_create(width, height, format, 1, NULL, 0);
	obs_leave_graphics();

	if (!context->texture) {
		if (context->spout_status != -10) {
			warn("Failed to create a %ux%u receiving texture for sender %s", width, height,
			     receiver->GetSenderName());
			context->spout_status = -10;
		}
		return false;
	}

	context->width = (int)width;
	context->height = (int)height;
	info("Sender %s is of dimensions %u x %u, format %lu: %s", receiver->GetSenderName(), width, height,
	     (unsigned long)dxgi_format, win_spout_format_name((DWORD)dxgi_format));
	context->spout_status = 0;
	return true;
}

// Graphics thread only: connect/poll the sender and copy the latest frame into our texture.
static void win_spout_source_receive(spout_source *context)
{
	spoutDX *receiver = context->receiver;

	if (!win_spout_source_init_dx(context)) {
		return;
	}

	if (context->pending_reconnect) {
		os_atomic_set_bool(&context->pending_reconnect, false);
		win_spout_source_disconnect(context);
		// An empty name makes spoutDX follow the active sender ("first available").
		receiver->SetReceiverName(context->useFirstSender ? nullptr : context->senderName);
		context->lastCheckTick = 0;
	}

	// While not connected, only look for senders at the configured poll interval.
	if (!receiver->IsConnected()) {
		const ULONGLONG now = GetTickCount64();
		if (now - context->lastCheckTick < context->tick_speed_limit) {
			return;
		}
		context->lastCheckTick = now;

		if (receiver->GetSenderCount() == 0) {
			win_spout_source_disconnect(context);
			win_spout_source_log_once(context, -3, LOG_INFO, "No active Spout senders");
			return;
		}
	}

	ID3D11Texture2D *dst = context->texture ? (ID3D11Texture2D *)gs_texture_get_obj(context->texture) : nullptr;
	const bool connected = receiver->ReceiveTexture(&dst);

	if (!connected) {
		const bool had_texture = context->texture != NULL;
		win_spout_source_disconnect(context);

		if (receiver->GetSenderHandle() && !receiver->GetSenderTexture()) {
			// spoutDX found the sender but could not open its share handle on our device.
			if (context->spout_status != -8) {
				warn("Could not open the shared texture of sender %s. This usually means the sender is "
				     "running on a different GPU than OBS: in Windows Settings > System > Display > "
				     "Graphics set both OBS and the sender to the same GPU, then restart the sender.",
				     context->useFirstSender ? "(first available)" : context->senderName);
				context->spout_status = -8;
			}
		} else if (had_texture) {
			info("Sender %s has gone away",
			     context->useFirstSender ? "(first available)" : context->senderName);
			context->spout_status = -4;
		} else if (!context->useFirstSender) {
			if (context->spout_status != -5) {
				info("Sender %s not found", context->senderName);
				context->spout_status = -5;
			}
		}
		return;
	}

	// New sender, or the sender's size/format changed: rebuild our texture, the next
	// ReceiveTexture() call copies into it.
	if (receiver->IsUpdated()) {
		if (!receiver->GetSenderTexture()) {
			// spoutDX reports the sender but has no texture for it: the share handle could
			// not be opened on our device (sender on another GPU). Don't build a texture we
			// can never fill; wait for the sender to change.
			if (context->spout_status != -8) {
				warn("Could not open the shared texture of sender %s. This usually means the sender is "
				     "running on a different GPU than OBS: in Windows Settings > System > Display > "
				     "Graphics set both OBS and the sender to the same GPU, then restart the sender.",
				     receiver->GetSenderName());
				context->spout_status = -8;
			}
			return;
		}
		win_spout_source_create_texture(context);
	}
}

static void win_spout_source_update(void *data, obs_data_t *settings)
{
	struct spout_source *context = (spout_source *)data;

	auto selectedSender = obs_data_get_string(settings, SPOUT_SENDER_LIST);

	if (strcmp(selectedSender, USE_FIRST_AVAILABLE_SENDER) == 0) {
		context->useFirstSender = true;
	} else {
		context->useFirstSender = false;
		memset(context->senderName, 0, sizeof(context->senderName));
		strncpy(context->senderName, selectedSender, sizeof(context->senderName) - 1);
	}

	context->tick_speed_limit = obs_data_get_int(settings, SPOUT_TICK_SPEED_LIMIT);
	context->composite_mode = obs_data_get_int(settings, SPOUT_COMPOSITE_MODE);

	os_atomic_set_bool(&context->pending_reconnect, true);
}

static const char *win_spout_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("sourcename");
}

// Create our context struct which will be passed to each
// of the plugin functions as void *data
static void *win_spout_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct spout_source *context = (spout_source *)bzalloc(sizeof(spout_source));
	context->source = source;
	info("initialising spout source");
	context->receiver = new spoutDX;
	context->useFirstSender = true;
	context->width = context->height = PLACEHOLDER_SIZE;

	win_spout_source_update(context, settings);
	return context;
}

static void win_spout_source_destroy(void *data)
{
	struct spout_source *context = (spout_source *)data;

	if (context->receiver) {
		context->receiver->ReleaseReceiver();
		context->receiver->CloseDirectX11();
		delete context->receiver;
		context->receiver = nullptr;
	}
	win_spout_source_release_texture(context);

	bfree(context);
}

static void win_spout_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SPOUT_SENDER_LIST, USE_FIRST_AVAILABLE_SENDER);
	obs_data_set_default_int(settings, SPOUT_TICK_SPEED_LIMIT, 100);
	// Without a default the combo box showed nothing selected while rendering opaque
	// (the switch's fallback); make that explicit so the UI matches the behaviour.
	obs_data_set_default_int(settings, SPOUT_COMPOSITE_MODE, COMPOSITE_MODE_OPAQUE);
}

static void win_spout_source_show(void *data)
{
	struct spout_source *context = (spout_source *)data;
	context->lastCheckTick = 0; // look for the sender immediately
}

static uint32_t win_spout_source_getwidth(void *data)
{
	struct spout_source *context = (spout_source *)data;
	return context->width;
}

static uint32_t win_spout_source_getheight(void *data)
{
	struct spout_source *context = (spout_source *)data;
	return context->height;
}

/**
 * On 10-bit and HDR canvases OBS blends in linear space, but the received texture
 * holds sRGB values, so decode it into a 16F texture first. premultiplied_alpha.effect
 * has no DrawSrgbDecompress technique, which is why this isn't done in the draw call
 * itself.
 */
static gs_texture_t *win_spout_source_linearize(spout_source *context)
{
	const enum gs_color_space space = gs_get_color_space();
	if (space == GS_CS_SRGB)
		return context->texture; // sRGB canvas, no decode needed

	const enum gs_color_format format = gs_texture_get_color_format(context->texture);
	if (format == GS_RGBA16F || format == GS_RGBA32F)
		return context->texture; // float senders are already linear

	const char *tech_name = "DrawSrgbDecompress";
	float multiplier = 1.0f;
	if (space == GS_CS_709_SCRGB) {
		tech_name = "DrawSrgbDecompressMultiply";
		multiplier = obs_get_video_sdr_white_level() / 80.0f;
	}

	if (!context->linear_texrender)
		context->linear_texrender = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	if (!context->linear_texrender)
		return context->texture;

	const uint32_t cx = gs_texture_get_width(context->texture);
	const uint32_t cy = gs_texture_get_height(context->texture);

	gs_texrender_reset(context->linear_texrender);
	if (!gs_texrender_begin(context->linear_texrender, cx, cy))
		return context->texture;

	gs_effect_t *const conv = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_enable_blending(false);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
	gs_effect_set_texture(gs_effect_get_param_by_name(conv, "image"), context->texture);
	gs_effect_set_float(gs_effect_get_param_by_name(conv, "multiplier"), multiplier);
	while (gs_effect_loop(conv, tech_name))
		gs_draw_sprite(context->texture, 0, 0, 0);
	gs_enable_blending(true);
	gs_enable_framebuffer_srgb(previous_srgb);
	gs_texrender_end(context->linear_texrender);

	return gs_texrender_get_texture(context->linear_texrender);
}

static void win_spout_source_render(void *data, gs_effect_t *effect)
{
	struct spout_source *context = (spout_source *)data;

	if (!context->texture) {
		if (context->render_status != -2) {
			debug("no texture");
			context->render_status = -2;
		}
		return;
	}

	if (context->render_status != 0) {
		info("rendering context->texture");
		context->render_status = 0;
	}

	switch (context->composite_mode) {
	case COMPOSITE_MODE_OPAQUE:
		effect = obs_get_base_effect(OBS_EFFECT_OPAQUE);
		break;
	case COMPOSITE_MODE_ALPHA:
		effect = obs_get_base_effect(
			OBS_EFFECT_PREMULTIPLIED_ALPHA); // Converts premultiplied to regular alpha before blending it as regular transparency.
		break;
	case COMPOSITE_MODE_PREMULTIPLIED:
		effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		// Proper blending of premultiplied alpha needs a modified blend function and then works with the default blending effect.
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		break;
	case COMPOSITE_MODE_DEFAULT:
		effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		break;
	default:
		effect = obs_get_base_effect(OBS_EFFECT_OPAQUE);
		break;
	}

	gs_texture_t *const texture = win_spout_source_linearize(context);

	while (gs_effect_loop(effect, "Draw")) {
		obs_source_draw(texture, 0, 0, 0, 0, false);
	}

	if (context->composite_mode == COMPOSITE_MODE_PREMULTIPLIED) {
		gs_blend_state_pop();
	}
}

static void win_spout_source_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);

	struct spout_source *context = (spout_source *)data;

	// Receive regardless of visibility (earlier versions re-initialised on the next tick
	// after hide() anyway), so switching to a scene with this source never shows a black
	// frame while it reconnects. video_tick runs on the graphics thread but outside the
	// graphics context, where gs_get_device_obj() returns NULL, so enter it here.
	obs_enter_graphics();
	win_spout_source_receive(context);
	obs_leave_graphics();
}

static void fill_senders(spoutDX *receiver, obs_property_t *list)
{
	// clear the list first
	obs_property_list_clear(list);

	// first option in the list should be "Take whatever is available"
	obs_property_list_add_string(list, obs_module_text("usefirstavailablesender"), USE_FIRST_AVAILABLE_SENDER);
	int totalSenders = receiver->GetSenderCount();
	if (totalSenders == 0) {
		return;
	}
	int index;
	char senderName[256];
	// then get the name of each sender from SPOUT
	for (index = 0; index < totalSenders; index++) {
		if (receiver->GetSender(index, senderName, (int)sizeof(senderName))) {
			obs_property_list_add_string(list, senderName, senderName);
		}
	}
}

// initialise the gui fields
static obs_properties_t *win_spout_properties(void *data)
{
	struct spout_source *context = (spout_source *)data;

	obs_properties_t *props = obs_properties_create();

	obs_property_t *sender_list = obs_properties_add_list(props, SPOUT_SENDER_LIST, obs_module_text("spoutsenders"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	fill_senders(context->receiver, sender_list);

	obs_property_t *composite_mode_list = obs_properties_add_list(props, SPOUT_COMPOSITE_MODE,
								      obs_module_text("compositemode"),
								      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(composite_mode_list, obs_module_text("compositemodeopaque"), COMPOSITE_MODE_OPAQUE);
	obs_property_list_add_int(composite_mode_list, obs_module_text("compositemodealpha"), COMPOSITE_MODE_ALPHA);
	obs_property_list_add_int(composite_mode_list, obs_module_text("compositemodedefault"), COMPOSITE_MODE_DEFAULT);
	obs_property_list_add_int(composite_mode_list, obs_module_text("compositemodepremultiplied"),
				  COMPOSITE_MODE_PREMULTIPLIED);

	obs_property_t *tick_speed_limit_list = obs_properties_add_list(props, SPOUT_TICK_SPEED_LIMIT,
									obs_module_text("tickspeedlimit"),
									OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(tick_speed_limit_list, obs_module_text("tickspeedcrazy"), 1);
	obs_property_list_add_int(tick_speed_limit_list, obs_module_text("tickspeedfast"), 100);
	obs_property_list_add_int(tick_speed_limit_list, obs_module_text("tickspeednormal"), 500);
	obs_property_list_add_int(tick_speed_limit_list, obs_module_text("tickspeedslow"), 1000);

	return props;
}

struct obs_source_info create_spout_source_info()
{
	struct obs_source_info spout_source_info = {};
	spout_source_info.id = "spout_capture";
	spout_source_info.type = OBS_SOURCE_TYPE_INPUT;
	spout_source_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	spout_source_info.get_name = win_spout_source_get_name;
	spout_source_info.create = win_spout_source_create;
	spout_source_info.destroy = win_spout_source_destroy;
	spout_source_info.update = win_spout_source_update;
	spout_source_info.get_defaults = win_spout_source_defaults;
	spout_source_info.show = win_spout_source_show;
	spout_source_info.get_width = win_spout_source_getwidth;
	spout_source_info.get_height = win_spout_source_getheight;

	spout_source_info.video_render = win_spout_source_render;
	spout_source_info.video_tick = win_spout_source_tick;
	spout_source_info.get_properties = win_spout_properties;

	return spout_source_info;
}
