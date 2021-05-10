/**
 * copyright Off World Live Ltd (https://offworld.live), 2019-2021
 *
 * and licenced under the GPL v2 (https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
 *
 * Many thanks to authors of https://github.com/baffler/OBS-OpenVR-Input-Plugin which
 * was used as guidance to working with the OBS Studio APIs
 */

#include <obs-module.h>
#include "win-spout.h"

#define debug(message, ...)                                                    \
	blog(LOG_DEBUG, "[%s] " message, obs_source_get_name(context->source), \
	     ##__VA_ARGS__)
#define info(message, ...)                                                    \
	blog(LOG_INFO, "[%s] " message, obs_source_get_name(context->source), \
	     ##__VA_ARGS__)
#define warn(message, ...)                 \
	blog(LOG_WARNING, "[%s] " message, \
	     obs_source_get_name(context->source), ##__VA_ARGS__)

#define SPOUT_SENDER_LIST "spoutsenders"
#define USE_FIRST_AVAILABLE_SENDER "usefirstavailablesender"
#define SPOUT_TICK_SPEED_LIMIT "tickspeedlimit"
#define SPOUT_COMPOSITE_MODE "compositemode"

#define COMPOSITE_MODE_OPAQUE 1
#define COMPOSITE_MODE_ALPHA 2
#define COMPOSITE_MODE_DEFAULT 3

struct spout_source {
	obs_source_t *source;
	char senderName[256];
	bool useFirstSender;
	gs_texture_t *texture;
	HANDLE dxHandle;
	DWORD dxFormat;
	ULONGLONG lastCheckTick;
	int width;
	int height;
	bool initialized;
	bool active;
	ULONGLONG tick_speed_limit;
	ULONGLONG composite_mode;
	int spout_status;
	int render_status;
	int tick_status;
	//bool should_release;
};

/**
 * Writes sender texture details (width & height) to the context
 * @return bool success
 */
static bool win_spout_source_store_sender_info(spout_source *context)
{
	unsigned int width, height;
	// get info about this active sender:
	if (!spoutptr->GetSenderInfo(context->senderName, width,
					height, context->dxHandle,
					context->dxFormat)) {
		return false;
	}

	context->width = width;
	context->height = height;
	return true;
}

static void win_spout_source_init(void *data, bool forced = false)
{
	struct spout_source *context = (spout_source *)data;
	if (context->initialized) {
		context->spout_status = 0;
		return;
	}

	ULONG64 tickDelta = GetTickCount64() - context->lastCheckTick;

	if (tickDelta < context->tick_speed_limit && !forced) {
		return;
	}
	context->lastCheckTick = GetTickCount64();

	if (spoutptr == NULL) {
		if (context->spout_status != -1) {
			warn("Spout pointer didn't exist");
			context->spout_status = -1;
		}
		return;
	}

	int totalSenders = spoutptr->GetSenderCount();

	if (totalSenders == 0) {
		if (context->spout_status != -2) {
			info("No active Spout cameras");
			context->spout_status = -2;
		}
		return;
	}

	if (context->useFirstSender) {
		if (spoutptr->GetSender(0, context->senderName)) {
			if (!spoutptr->SetActiveSender(
				    context->senderName)) {
				if (context->spout_status != -4) {
					info("WoW , i can't set active sender as %s", context->senderName);
					context->spout_status = -4;
				}
				return;
			}
		} else {
			if (context->spout_status != -3) {
				info("Strange , there is a sender without name ?");
				context->spout_status = -3;
			}
			return;
		}
	} else {
		int index;
		char senderName[256];
		bool exists = false;
		// then get the name of each sender from SPOUT
		for (index = 0; index < totalSenders; index++) {
			spoutptr->GetSender(index, senderName);
			if (strcmp(senderName, context->senderName) == 0) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			if (context->spout_status != -5) {
				info("Sorry, Sender Name %s not found", context->senderName);
				context->spout_status = -5;
			}
			return;
		} else {
			context->spout_status = 0;
		}
	}

	info("Getting info for sender %s", context->senderName);
	if (!win_spout_source_store_sender_info(context)) {
		warn("Named %s sender not found", context->senderName);
	} else {
		info("Sender %s is of dimensions %d x %d",
		     context->senderName,
		     context->width, context->height);
	};

	obs_enter_graphics();
	gs_texture_destroy(context->texture);
	//context->should_release = true;
	context->texture = gs_texture_open_shared((uint32_t)context->dxHandle);
	obs_leave_graphics();

	context->initialized = true;
}

static void win_spout_source_deinit(void *data)
{
	struct spout_source *context = (spout_source *)data;
	context->initialized = false;
	if (context->texture) {
		obs_enter_graphics();
		gs_texture_destroy(context->texture);
		obs_leave_graphics();
		context->texture = NULL;
	}
	//// cleanup spout
	//if (context->should_release) {
	//	spoutptr->ReleaseReceiver();
	//	context->should_release = false;
	//}
}

static void win_spout_source_update(void *data, obs_data_t *settings)
{
	struct spout_source *context = (spout_source *)data;

	auto selectedSender = obs_data_get_string(settings, SPOUT_SENDER_LIST);

	if (strcmp(selectedSender, USE_FIRST_AVAILABLE_SENDER) == 0) {
		context->useFirstSender = true;
	} else {
		context->useFirstSender = false;
		memset(context->senderName, 0, 256);
		strcpy(context->senderName, selectedSender);
	}

	auto selectedSpeed = obs_data_get_int(settings, SPOUT_TICK_SPEED_LIMIT);
	context->tick_speed_limit = selectedSpeed;

	auto compositeMode = obs_data_get_int(settings, SPOUT_COMPOSITE_MODE);
	context->composite_mode = compositeMode;

	if (context->initialized) {
		win_spout_source_deinit(data);
		win_spout_source_init(data);
	}
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
	info("initialising spout source");
	context->source = source;
	context->useFirstSender = true;
	context->initialized = false;
	context->tick_speed_limit = 0;
	context->texture = NULL;
	context->dxHandle = NULL;
	context->active = false;
	context->initialized = false;

	// set the initial size as 100x100 until we
	// have the actual dimensions from SPOUT
	context->width = context->height = 100;

	win_spout_source_update(context, settings);
	return context;
}

static void win_spout_source_destroy(void *data)
{
	struct spout_source *context = (spout_source *)data;

	win_spout_source_deinit(data);

	bfree(context);
}

static void win_spout_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SPOUT_SENDER_LIST,
				    USE_FIRST_AVAILABLE_SENDER);
	obs_data_set_default_int(settings, "tickspeedlimit", 100);
}

static void win_spout_source_show(void *data)
{
	win_spout_source_init(data, true); // When showing do forced init without delay
}

static void win_spout_source_hide(void *data)
{
	win_spout_source_deinit(data);
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

static void win_spout_source_render(void *data, gs_effect_t *effect)
{
	struct spout_source *context = (spout_source *)data;

	if (!context->active) {
		if (context->render_status != -1) {
			debug("inactive");
			context->render_status = -1;
		}
		return;
	}

	// tried to initialise again
	// but failed, so we exit
	if (!context->initialized) {
		if (context->render_status != -2) {
			debug("uninit'd");
			context->render_status = -2;
		}
		return;
	}

	if (!context->texture) {
		if (context->render_status != -3) {
			debug("no texture");
			context->render_status = -3;
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
		effect = obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA);
		break;
	case COMPOSITE_MODE_DEFAULT:
		effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		break;
	default:
		effect = obs_get_base_effect(OBS_EFFECT_OPAQUE);
		break;
	}

	while (gs_effect_loop(effect, "Draw")) {
		bool linearRGB = gs_get_linear_srgb();
		gs_set_linear_srgb(false);
		obs_source_draw(context->texture, 0, 0, 0, 0, false);
		gs_set_linear_srgb(linearRGB);
	}
}

/**
 * Updates sender texture details on the context
 * and works out whether any of this data has changed
 *
 * @return bool sender data has changed
 */
static bool win_spout_sender_has_changed(spout_source *context)
{
	DWORD oldFormat = context->dxFormat;
	auto oldWidth = context->width;
	auto oldHeight = context->height;

	if (!win_spout_source_store_sender_info(context)) {
		// assume that if it fails, it has changed
		// ie sender no longer exists
		return true;
	}
	if (context->width != oldWidth || context->height != oldHeight ||
	    oldFormat != context->dxFormat) {
		return true;
	}
	return false;
}

static void win_spout_source_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);

	struct spout_source *context = (spout_source *)data;

	context->active = obs_source_active(context->source);
	if (win_spout_sender_has_changed(context)) {
		if (context->tick_status != -1) {
			info("Sender %s has changed / gone away. Resetting ",
			     context->senderName);
			context->tick_status = -1;
		}
		context->initialized = false;
		win_spout_source_deinit(data);
		win_spout_source_init(data);
		return;
	}
	if (!context->initialized) {
		if (context->tick_status != -2) {
			context->tick_status = -2;
		}
		win_spout_source_init(data);
	}
	if (context->tick_status != 0) {
		context->tick_status = 0;
	}
}

static void fill_senders(obs_property_t *list)
{
	// clear the list first
	obs_property_list_clear(list);

	// first option in the list should be "Take whatever is available"
	obs_property_list_add_string(list,
				     obs_module_text("usefirstavailablesender"),
				     USE_FIRST_AVAILABLE_SENDER);
	int totalSenders = spoutptr->GetSenderCount();
	if (totalSenders == 0) {
		return;
	}
	int index;
	char senderName[256];
	// then get the name of each sender from SPOUT
	for (index = 0; index < totalSenders; index++) {
		spoutptr->GetSender(index, senderName);
		obs_property_list_add_string(list, senderName, senderName);
	}
}

// initialise the gui fields
static obs_properties_t *win_spout_properties(void *data)
{
	struct spout_source *context = (spout_source *)data;

	obs_properties_t *props = obs_properties_create();

	obs_property_t *sender_list = obs_properties_add_list(
		props, SPOUT_SENDER_LIST, obs_module_text("SpoutSenders"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	fill_senders(sender_list);

	obs_property_t *composite_mode_list = obs_properties_add_list(
		props, SPOUT_COMPOSITE_MODE, obs_module_text("compositemode"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(composite_mode_list,
				  obs_module_text("compositemodeopaque"),
				  COMPOSITE_MODE_OPAQUE);
	obs_property_list_add_int(composite_mode_list,
				  obs_module_text("compositemodealpha"),
				  COMPOSITE_MODE_ALPHA);
	obs_property_list_add_int(composite_mode_list,
				  obs_module_text("compositemodedefault"),
				  COMPOSITE_MODE_DEFAULT);

	obs_property_t *tick_speed_limit_list = obs_properties_add_list(
		props, SPOUT_TICK_SPEED_LIMIT,
		obs_module_text("tickspeedlimit"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(tick_speed_limit_list,
				  obs_module_text("tickspeedcrazy"), 1);
	obs_property_list_add_int(tick_speed_limit_list,
				  obs_module_text("tickspeedfast"), 100);
	obs_property_list_add_int(tick_speed_limit_list,
				  obs_module_text("tickspeednormal"), 500);
	obs_property_list_add_int(tick_speed_limit_list,
				  obs_module_text("tickspeedslow"), 1000);

	return props;
}

struct obs_source_info create_spout_source_info()
{
	struct obs_source_info spout_source_info = {};
	spout_source_info.id = "spout_capture";
	spout_source_info.type = OBS_SOURCE_TYPE_INPUT;
	spout_source_info.output_flags = OBS_SOURCE_VIDEO |
					 OBS_SOURCE_CUSTOM_DRAW;
	spout_source_info.get_name = win_spout_source_get_name;
	spout_source_info.create = win_spout_source_create;
	spout_source_info.destroy = win_spout_source_destroy;
	spout_source_info.update = win_spout_source_update;
	spout_source_info.get_defaults = win_spout_source_defaults;
	spout_source_info.show = win_spout_source_show;
	spout_source_info.hide = win_spout_source_hide;
	spout_source_info.get_width = win_spout_source_getwidth;
	spout_source_info.get_height = win_spout_source_getheight;

	spout_source_info.video_render = win_spout_source_render;
	spout_source_info.video_tick = win_spout_source_tick;
	spout_source_info.get_properties = win_spout_properties;

	return spout_source_info;
}
