/**
 * This Plugin is written by Campbell Morgan,
 * copyright Off World Live Ltd (https://offworld.live), 2019
 *
 * and licenced under the GPL v2 (https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
 *
 * Many thanks to authors of https://github.com/baffler/OBS-OpenVR-Input-Plugin which
 * was used as guidance to working with the OBS Studio APIs
 */
#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <sys/stat.h>
#include <string.h>

#include "Include/SpoutLibrary.h"
#ifdef _WIN64
#pragma comment(lib, "Binaries/x64/SpoutLibrary.lib")
#else
#pragma comment(lib, "Binaries/Win32/SpoutLibrary.lib")
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-spout", "en-US")

#define blog(log_level, message, ...) \
	blog(log_level, "[win_spout] " message, ##__VA_ARGS__)

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

struct win_spout {
	obs_source_t *source;

	char senderName[256];

	bool useFirstSender;

	gs_texture_t *texture;

	HANDLE dxHandle;
	DWORD dxFormat;

	SPOUTHANDLE spoutptr;

	ULONGLONG lastCheckTick;

	int width;
	int height;

	bool initialized;
	bool active;

	int tickspeedlimit;

	int spout_status;
	int render_status;
	int tick_status;
	bool should_release;
};

/**
 * Writes sender texture details (width & height) to the context
 * @return bool success
 */
static bool win_spout_store_sender_info(win_spout *context)
{
	unsigned int width, height;
	// get info about this active sender:
	if (!context->spoutptr->GetSenderInfo(context->senderName, width,
					      height, context->dxHandle,
					      context->dxFormat)) {
		return false;
	}

	context->width = width;
	context->height = height;
	return true;
}

/**
 * Updates sender texture details on the context
 * and works out whether any of this data has changed
 *
 * @return bool sender data has changed
 */
static bool win_spout_sender_has_changed(win_spout *context)
{
	DWORD oldFormat = context->dxFormat;
	auto oldWidth = context->width;
	auto oldHeight = context->height;

	if (!win_spout_store_sender_info(context)) {
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

static void win_spout_init(void *data, bool forced = false)
{
	struct win_spout *context = (win_spout *)data;
	if (context->initialized) {
		context->spout_status = 0;
		return;
	}

	if (GetTickCount64() - context->lastCheckTick <
		    context->tickspeedlimit &&
	    !forced) {
		return;
	}
	context->lastCheckTick = GetTickCount64();

	if (context->spoutptr == NULL) {
		if (context->spout_status != -1) {
			warn("Spout pointer didn't exist");
			context->spout_status = -1;
		}
		return;
	}
	int totalSenders = context->spoutptr->GetSenderCount();
	if (totalSenders == 0) {
		if (context->spout_status != -2) {
			info("No active Spout cameras");
			context->spout_status = -2;
		}
		return;
	}

	if (context->useFirstSender) {
		if (context->spoutptr->GetSenderName(0, context->senderName)) {
			if (!context->spoutptr->SetActiveSender(
				    context->senderName)) {
				if (context->spout_status != -4) {
					info("WoW , i can't set active sender as %s",
					     context->senderName);
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
			context->spoutptr->GetSenderName(index, senderName);
			if (strcmp(senderName, context->senderName) == 0) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			if (context->spout_status != -5) {
				info("Sorry, Sender Name %s not found",
				     context->senderName);
				context->spout_status = -5;
			}
			return;
		} else {
			context->spout_status = 0;
		}
	}

	info("Getting info for sender %s", context->senderName);
	if (!win_spout_store_sender_info(context)) {
		warn("Named %s sender not found", context->senderName);
	} else {
		info("Sender %s is of dimensions %d x %d", context->senderName,
		     context->width, context->height);
	};

	obs_enter_graphics();
	gs_texture_destroy(context->texture);
	context->should_release = true;
	context->texture = gs_texture_open_shared((uint32_t)context->dxHandle);
	obs_leave_graphics();

	context->initialized = true;
}

static void win_spout_deinit(void *data)
{
	struct win_spout *context = (win_spout *)data;
	context->initialized = false;
	if (context->texture) {
		obs_enter_graphics();
		gs_texture_destroy(context->texture);
		obs_leave_graphics();
		context->texture = NULL;
	}
	// cleanup spout
	if (context->should_release) {
		context->spoutptr->ReleaseReceiver();
		context->should_release = false;
	}
}

static const char *win_spout_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("sourcename");
}

static void win_spout_update(void *data, obs_data_t *settings)
{
	struct win_spout *context = (win_spout *)data;

	auto selectedSender = obs_data_get_string(settings, SPOUT_SENDER_LIST);

	if (strcmp(selectedSender, USE_FIRST_AVAILABLE_SENDER) == 0) {
		context->useFirstSender = true;
	} else {
		context->useFirstSender = false;
		memset(context->senderName, 0, 256);
		strcpy(context->senderName, selectedSender);
	}

	auto selectedSpeed = obs_data_get_int(settings, "tickspeedlimit");
	context->tickspeedlimit = selectedSpeed;

	if (context->initialized) {
		win_spout_deinit(data);
		win_spout_init(data);
	}
}

static void win_spout_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SPOUT_SENDER_LIST,
				    USE_FIRST_AVAILABLE_SENDER);
	obs_data_set_default_int(settings, "tickspeedlimit", 100);
}

static uint32_t win_spout_getwidth(void *data)
{
	struct win_spout *context = (win_spout *)data;
	return context->width;
}

static uint32_t win_spout_getheight(void *data)
{
	struct win_spout *context = (win_spout *)data;
	return context->height;
}

static void win_spout_show(void *data)
{
	win_spout_init(data, true); // When showing do forced init without delay
}

static void win_spout_hide(void *data)
{
	win_spout_deinit(data);
}

// Create our context struct which will be passed to each
// of the plugin functions as void *data
static void *win_spout_create(obs_data_t *settings, obs_source_t *source)
{
	struct win_spout *context = (win_spout *)bzalloc(sizeof(win_spout));
	info("initialising spout");
	context->spoutptr = GetSpout();
	context->source = source;
	context->useFirstSender = true;

	context->initialized = false;
	context->tickspeedlimit = 0;
	context->texture = NULL;
	context->dxHandle = NULL;
	context->active = false;
	context->initialized = false;

	// set the initial size as 100x100 until we
	// have the actual dimensions from SPOUT
	context->width = context->height = 100;

	win_spout_update(context, settings);
	return context;
}

static void win_spout_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);

	struct win_spout *context = (win_spout *)data;

	context->active = obs_source_active(context->source);
	if (win_spout_sender_has_changed(context)) {
		if (context->tick_status != -1) {
			info("Sender %s has changed / gone away. Resetting ",
			     context->senderName);
			context->tick_status = -1;
		}
		context->initialized = false;
		win_spout_deinit(data);
		win_spout_init(data);
		return;
	}
	if (!context->initialized) {
		if (context->tick_status != -2) {
			context->tick_status = -2;
		}
		win_spout_init(data);
	}
	if (context->tick_status != 0) {
		context->tick_status = 0;
	}
}

static void win_spout_destroy(void *data)
{
	struct win_spout *context = (win_spout *)data;

	win_spout_deinit(data);

	if (context->spoutptr != NULL) {
		context->spoutptr->Release();
	}

	bfree(context);
}

static void win_spout_render(void *data, gs_effect_t *effect)
{
	struct win_spout *context = (win_spout *)data;

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

	effect = obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA);

	while (gs_effect_loop(effect, "Draw")) {
		obs_source_draw(context->texture, 0, 0, 0, 0, false);
	}
}

static void fill_senders(SPOUTHANDLE spoutptr, obs_property_t *list)
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
		spoutptr->GetSenderName(index, senderName);
		obs_property_list_add_string(list, senderName, senderName);
	}
}

// initialise the gui fields
static obs_properties_t *win_spout_properties(void *data)
{
	struct win_spout *context = (win_spout *)data;

	obs_properties_t *props = obs_properties_create();

	obs_property_t *sender_list = obs_properties_add_list(
		props, SPOUT_SENDER_LIST, obs_module_text("SpoutSenders"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	fill_senders(context->spoutptr, sender_list);

	obs_property_t *tick_speed_limit_list = obs_properties_add_list(
		props, "tickspeedlimit", obs_module_text("tickspeedlimit"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
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

bool obs_module_load(void)
{
	obs_source_info info = {};
	info.id = "spout_capture";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = win_spout_get_name;
	info.create = win_spout_create;
	info.destroy = win_spout_destroy;
	info.update = win_spout_update;
	info.get_defaults = win_spout_defaults;
	info.show = win_spout_show;
	info.hide = win_spout_hide;
	info.get_width = win_spout_getwidth;
	info.get_height = win_spout_getheight;
	info.video_render = win_spout_render;
	info.video_tick = win_spout_tick;
	info.get_properties = win_spout_properties;
	obs_register_source(&info);
	return true;
}
