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

struct spout_output {
	spoutDX *sender;
	obs_output_t *output;
	// Owned copy: the obs_data string handed to update() is freed when the settings change.
	char senderName[256];
	bool output_started;
	// Set once a lost device has been reported, so a failing recreate is only logged once.
	bool device_lost;
	// GPU path (#84): after creation, only accessed on the graphics thread.
	gs_texrender_t *texrender_srgb;
	gs_texrender_t *texrender_curr;
	gs_texrender_t *texrender_prev;
	// mutex guards accesses to rest of context variables,
	// and any methods on spoutDX* sender.
	// Calling obs methods on obs_output_t* output seems thread-safe.
	// trying to avoid calling obs methods while holding our own mutex.
	pthread_mutex_t mutex;
};

// With Direct3D 11 the output sends OBS's main canvas texture from OBS's own device, before
// the colour format conversion and without a CPU readback (#84). Under OpenGL it falls back to
// raw CPU frames sent from a device of its own (#64).
static bool use_gpu = false;

// Forward decls
void win_spout_output_destroy(void *data);

bool init_spout(void *data)
{
	spout_output *context = (spout_output *)data;
	// Enable for debugging spout:
	// spoututils::SetSpoutLogLevel(spoututils::SPOUT_LOG_VERBOSE);
	// spoututils::EnableSpoutLog();
	context->sender->SetMaxSenders(255);

	if (use_gpu) {
		// Opened with OBS's device on the graphics thread, see attach_obs_device().
		blog(LOG_INFO, "Spout output sends the canvas from OBS's D3D11 device");
		return true;
	}

	if (!context->sender->OpenDirectX11()) {
		blog(LOG_ERROR, "Failed to Open DX11");
		return false;
	}
	blog(LOG_INFO, "Opened DX11");

	return true;
}

// The output sends from its own D3D11 device. Sleep/resume or a driver reset can remove
// that device; spoutDX::SendImage() still returns true on a removed device and never
// recreates it, so the output sent black until OBS was restarted (#108). OBS rebuilds its
// own device, so do the same for ours. Call with the mutex held.
static bool ensure_device(spout_output *context)
{
	ID3D11Device *device = context->sender->GetDX11Device();
	const HRESULT reason = device ? device->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
	if (reason == S_OK) {
		return true;
	}

	if (!context->device_lost) {
		blog(LOG_WARNING, "Spout output lost its D3D11 device (0x%08lX), recreating it", (unsigned long)reason);
	}

	context->sender->ReleaseSender();
	context->sender->CloseDirectX11();
	context->sender->OpenDirectX11();
	if (!context->sender->GetDX11Device()) {
		if (!context->device_lost) {
			blog(LOG_ERROR, "Failed to recreate the Spout output D3D11 device, will retry");
		}
		context->device_lost = true;
		return false;
	}

	// ReleaseSender() forgets the name; the sender is re-created on the next frame.
	context->sender->SetSenderName(context->senderName);
	context->device_lost = false;
	blog(LOG_INFO, "Recreated the Spout output D3D11 device");
	return true;
}

// GPU path: send from OBS's device. OBS replaces its device after a device loss (sleep/resume,
// driver reset), so follow it when the pointer changes. Call on the graphics thread with the
// mutex held.
static void attach_obs_device(spout_output *context, ID3D11Device *device)
{
	if (context->sender->GetDX11Device() == device) {
		return;
	}
	if (context->sender->GetDX11Device()) {
		blog(LOG_INFO, "OBS recreated its D3D11 device, moving the Spout output to it");
		context->sender->ReleaseSender();
		context->sender->CloseDirectX11();
	}
	context->sender->OpenDirectX11(device);
	// ReleaseSender() forgets the name; the sender is re-created on the next send.
	context->sender->SetSenderName(context->senderName);
}

// Draw tex with blending off. With srgb, an 8-bit texture is read through its sRGB view (10-bit
// and HDR canvases are already linear) and sRGB-encoded on write; without, the bytes are copied
// as they are.
static void draw_texture(gs_texture_t *tex, bool srgb, uint32_t cx, uint32_t cy)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(srgb);

	if (srgb) {
		gs_effect_set_texture_srgb(image, tex);
	} else {
		gs_effect_set_texture(image, tex);
	}
	while (gs_effect_loop(effect, "Draw")) {
		gs_draw_sprite(tex, 0, cx, cy);
	}

	gs_enable_framebuffer_srgb(previous);
	gs_blend_state_pop();
}

// GPU path: called on the graphics thread after each canvas has rendered.
static void win_spout_output_rendered(void *data)
{
	spout_output *context = (spout_output *)data;

	// Every canvas (mix) triggers this; only send while the main canvas is the render target.
	gs_texture_t *main_tex = obs_get_main_texture();
	if (!main_tex || gs_get_render_target() != main_tex) {
		return;
	}

	ID3D11Device *device = (ID3D11Device *)gs_get_device_obj();
	video_t *video = obs_get_video();
	if (!device || !video) {
		return;
	}

	const uint32_t base_width = gs_texture_get_width(main_tex);
	const uint32_t base_height = gs_texture_get_height(main_tex);
	const uint32_t width = video_output_get_width(video);
	const uint32_t height = video_output_get_height(video);
	if (!base_width || !base_height || !width || !height) {
		return;
	}

	if (!context->texrender_srgb) {
		context->texrender_srgb = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
		context->texrender_curr = gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
		context->texrender_prev = gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
	}

	struct vec4 clear_color;
	vec4_zero(&clear_color);

	// Canvas to sRGB, scaled to the output resolution like the old raw frames were. Unlike the
	// preview, HDR canvases are not tonemapped: SDR content keeps its levels and highlights
	// above SDR white clip.
	gs_texrender_reset(context->texrender_srgb);
	if (!gs_texrender_begin(context->texrender_srgb, width, height)) {
		return;
	}
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, (float)base_width, 0.0f, (float)base_height, -100.0f, 100.0f);
	draw_texture(main_tex, true, base_width, base_height);
	gs_texrender_end(context->texrender_srgb);

	// Copy the sRGB-encoded bytes as they are into a UNORM texture: Spout receivers expect a
	// plain BGRA format, not the typeless one OBS renders sRGB into.
	gs_texrender_reset(context->texrender_curr);
	if (!gs_texrender_begin(context->texrender_curr, width, height)) {
		return;
	}
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	draw_texture(gs_texrender_get_texture(context->texrender_srgb), false, width, height);
	gs_texrender_end(context->texrender_curr);

	pthread_mutex_lock(&context->mutex);

	if (context->output_started) {
		attach_obs_device(context, device);
		// Send last frame's texture: double-buffering avoids waiting on this frame's
		// rendering, as in the filter.
		gs_texture_t *prev_tex = gs_texrender_get_texture(context->texrender_prev);
		if (prev_tex) {
			context->sender->SendTexture((ID3D11Texture2D *)gs_texture_get_obj(prev_tex));
		}
	}

	gs_texrender_t *swap = context->texrender_curr;
	context->texrender_curr = context->texrender_prev;
	context->texrender_prev = swap;

	pthread_mutex_unlock(&context->mutex);
}

static const char *win_spout_output_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("outputname");
}

static void win_spout_output_update(void *data, obs_data_t *settings)
{
	spout_output *context = (spout_output *)data;
	pthread_mutex_lock(&context->mutex);
	strncpy(context->senderName, obs_data_get_string(settings, "senderName"), sizeof(context->senderName) - 1);
	context->senderName[sizeof(context->senderName) - 1] = '\0';
	pthread_mutex_unlock(&context->mutex);
}

static void *win_spout_output_create(obs_data_t *settings, obs_output_t *output)
{
	spout_output *context = (spout_output *)bzalloc(sizeof(spout_output));
	context->output = output;
	context->output_started = false;
	context->sender = new spoutDX;

	pthread_mutex_init_value(&context->mutex);
	if (pthread_mutex_init(&context->mutex, NULL) != 0) {
		blog(LOG_ERROR, "Failed to create mutex for spout output!");
		win_spout_output_destroy(context);
		return nullptr;
	}

	if (!init_spout(context)) {
		blog(LOG_ERROR, "Failed to create spout output!");
		win_spout_output_destroy(context);
		return nullptr;
	}

	win_spout_output_update(context, settings);

	// from this point, need to lock mutex to access context safely
	return context;
}

static void win_spout_output_destroy(void *data)
{
	spout_output *context = (spout_output *)data;

	if (!context) {
		return;
	}

	if (use_gpu) {
		obs_remove_main_rendered_callback(win_spout_output_rendered, context);
	}

	// The GPU path's sender and textures live on OBS's device.
	obs_enter_graphics();
	if (context->sender) {
		context->sender->CloseDirectX11();
		delete context->sender;
		context->sender = nullptr;
	}
	gs_texrender_destroy(context->texrender_srgb);
	gs_texrender_destroy(context->texrender_curr);
	gs_texrender_destroy(context->texrender_prev);
	obs_leave_graphics();

	pthread_mutex_destroy(&context->mutex);
	bfree(context);
}

static bool win_spout_output_start_gpu(spout_output *context)
{
	obs_output_t *output = context->output;
	video_t *video = obs_get_video();
	if (!video) {
		blog(LOG_ERROR, "Trying to start with no video!");
		return false;
	}

	// No audio or video flags: libobs hooks nothing, frames come from the rendered callback.
	if (!obs_output_can_begin_data_capture(output, 0)) {
		blog(LOG_ERROR, "Unable to begin data capture!");
		return false;
	}

	pthread_mutex_lock(&context->mutex);
	context->sender->SetSenderName(context->senderName);
	context->output_started = true;
	pthread_mutex_unlock(&context->mutex);

	obs_add_main_rendered_callback(win_spout_output_rendered, context);

	if (!obs_output_begin_data_capture(output, 0)) {
		obs_remove_main_rendered_callback(win_spout_output_rendered, context);
		pthread_mutex_lock(&context->mutex);
		context->output_started = false;
		pthread_mutex_unlock(&context->mutex);
		blog(LOG_ERROR, "Unable to start capture!");
		return false;
	}

	blog(LOG_INFO, "Creating capture with name: %s, width: %i, height: %i", context->senderName,
	     (int)video_output_get_width(video), (int)video_output_get_height(video));
	return true;
}

bool win_spout_output_start(void *data)
{
	spout_output *context = (spout_output *)data;

	if (!context->output) {
		blog(LOG_ERROR, "Trying to start with no output!");
		return false;
	}

	if (use_gpu) {
		return win_spout_output_start_gpu(context);
	}

	pthread_mutex_lock(&context->mutex);

	ensure_device(context);
	context->sender->SetSenderName(context->senderName);

	obs_output_t *output = context->output;

	pthread_mutex_unlock(&context->mutex);

	// obs_output_create() captured the video/audio handles that existed at module load.
	// Every obs_reset_video() (profile switch, Settings > Video) replaces them, so point the
	// output at the current ones here, where obs-websocket's StartOutput also arrives, or we
	// hand libobs a dangling video_t.
	obs_output_set_media(output, obs_get_video(), obs_get_audio());

	int32_t width = (int32_t)obs_output_get_width(output);
	int32_t height = (int32_t)obs_output_get_height(output);

	video_t *video = obs_output_video(output);
	if (!video) {
		blog(LOG_ERROR, "Trying to start with no video!");
		return false;
	}

	if (!obs_output_can_begin_data_capture(output, 0)) {
		blog(LOG_ERROR, "Unable to begin data capture!");
		return false;
	}

	video_scale_info info{};
	// we enforce BGRA format as it works well with spout
	info.format = VIDEO_FORMAT_BGRA;
	info.width = width;
	info.height = height;

	obs_output_set_video_conversion(output, &info);

	bool started = obs_output_begin_data_capture(output, 0);

	pthread_mutex_lock(&context->mutex);

	context->output_started = started;

	pthread_mutex_unlock(&context->mutex);

	if (!started) {
		blog(LOG_ERROR, "Unable to start capture!");
	} else {
		blog(LOG_INFO, "Creating capture with name: %s, width: %i, height: %i", context->senderName, width,
		     height);
	}

	return started;
}

void win_spout_output_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);

	spout_output *context = (spout_output *)data;

	pthread_mutex_lock(&context->mutex);
	bool started = context->output_started;
	obs_output_t *output = context->output;
	pthread_mutex_unlock(&context->mutex);

	if (started) {
		if (use_gpu) {
			// Returns once a running callback has finished; none is called after this.
			obs_remove_main_rendered_callback(win_spout_output_rendered, context);
		}

		obs_output_end_data_capture(output);

		// The GPU path's sender lives on OBS's device; release it under the graphics lock.
		if (use_gpu) {
			obs_enter_graphics();
		}
		pthread_mutex_lock(&context->mutex);

		context->sender->ReleaseSender();
		context->output_started = false;

		pthread_mutex_unlock(&context->mutex);
		if (use_gpu) {
			obs_leave_graphics();
		}
	}
}

void win_spout_output_rawvideo(void *data, struct video_data *frame)
{
	spout_output *context = (spout_output *)data;

	pthread_mutex_lock(&context->mutex);

	bool started = context->output_started;
	obs_output_t *output = context->output;

	pthread_mutex_unlock(&context->mutex);

	if (!started) {
		return;
	}

	int32_t width = (int32_t)obs_output_get_width(output);
	int32_t height = (int32_t)obs_output_get_height(output);

	pthread_mutex_lock(&context->mutex);

	if (ensure_device(context)) {
		context->sender->SendImage(frame->data[0], width, height);
	}

	pthread_mutex_unlock(&context->mutex);
}

obs_properties_t *win_spout_output_getproperties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_properties_add_text(props, "spout_output_name", obs_module_text("outputname"), OBS_TEXT_DEFAULT);

	return props;
}

struct obs_output_info create_spout_output_info(bool gpu)
{
	struct obs_output_info spout_output_info = {};

	use_gpu = gpu;

	spout_output_info.id = "spout_output";
	spout_output_info.flags = gpu ? 0 : OBS_OUTPUT_VIDEO;
	spout_output_info.get_name = win_spout_output_get_name;
	spout_output_info.create = win_spout_output_create;
	spout_output_info.destroy = win_spout_output_destroy;
	spout_output_info.start = win_spout_output_start;
	spout_output_info.update = win_spout_output_update;
	spout_output_info.stop = win_spout_output_stop;
	spout_output_info.raw_video = win_spout_output_rawvideo;
	spout_output_info.get_properties = win_spout_output_getproperties;

	return spout_output_info;
}
