/**
 * Copyright Off World Live Ltd (https://offworld.live), 2019-2021
 *
 * and licenced under the GPL v2 (https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
 *
 * Many thanks to authors of https://github.com/baffler/OBS-OpenVR-Input-Plugin which
 * was used as guidance to working with the OBS Studio APIs
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <sys/stat.h>
#include <util/platform.h>
#include <QAction>
#include <QMainWindow>

#include "win-spout.h"
#include "ui/win-spout-output-settings.h"
#include "win-spout-config.h"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Off World Live")
OBS_MODULE_USE_DEFAULT_LOCALE("win-spout", "en-US")

extern struct obs_source_info create_spout_source_info();
struct obs_source_info spout_source_info;

extern struct obs_output_info create_spout_output_info();
struct obs_output_info spout_output_info;

extern struct obs_source_info create_spout_filter_info();
struct obs_source_info spout_filter_info;

win_spout_output_settings *spout_output_settings;
obs_output_t *win_spout_out;

// Set while a profile switch is in flight so the output can be restarted afterwards (#66).
static bool restart_output_after_profile_change = false;

static void spout_obs_event(enum obs_frontend_event event, void *)
{
	if (!win_spout_out) {
		return;
	}

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
		// AutoStart has to happen here, not when the Tools dialog is opened (#80, #92).
		win_spout_config *config = win_spout_config::get();
		blog(LOG_INFO, "auto_start=%s (user.ini [win_spout])", config->auto_start ? "true" : "false");
		if (config->auto_start) {
			spout_output_start(config->spout_output_name.toUtf8().constData());
		}
		break;
	}
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		// A running raw output blocks obs_reset_video(), which left the canvas at the
		// previous profile's size until OBS was restarted (#66). Stop it for the switch.
		restart_output_after_profile_change = obs_output_active(win_spout_out);
		if (restart_output_after_profile_change) {
			blog(LOG_INFO, "stopping Spout output for profile change");
			spout_output_stop();
			// Deactivation of a raw output finishes on a helper thread; give it a moment
			// so the profile's video reset is not refused as "currently active".
			for (int i = 0; i < 100 && obs_video_active(); i++) {
				os_sleep_ms(10);
			}
		}
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		if (restart_output_after_profile_change) {
			restart_output_after_profile_change = false;
			blog(LOG_INFO, "restarting Spout output after profile change");
			spout_output_start(win_spout_config::get()->spout_output_name.toUtf8().constData());
		}
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		obs_output_stop(win_spout_out);
		obs_output_release(win_spout_out);
		win_spout_out = nullptr;
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	// load spout - source
	spout_source_info = create_spout_source_info();
	obs_register_source(&spout_source_info);

	// load spout output
	win_spout_config *config = win_spout_config::get();
	config->load();

	spout_output_info = create_spout_output_info();
	obs_register_output(&spout_output_info);

	obs_data_t *settings = obs_data_create();
	win_spout_out = obs_output_create("spout_output", "OBS Spout Output", settings, NULL);
	obs_data_release(settings);

	QAction *menu_action = (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("toolslabel"));

	obs_frontend_push_ui_translation(obs_module_get_string);
	obs_frontend_pop_ui_translation();

	auto menu_cb = [] {
		if (!spout_output_settings) {
			QMainWindow *main_window = (QMainWindow *)obs_frontend_get_main_window();
			if (!main_window) {
				blog(LOG_ERROR, "Can't get main window!");
				return;
			}
			spout_output_settings = new win_spout_output_settings(main_window);
			spout_output_settings->show();
		} else {
			spout_output_settings->show();
			spout_output_settings->raise();
			spout_output_settings->activateWindow();
		}
	};
	menu_action->connect(menu_action, &QAction::triggered, menu_cb);

	obs_frontend_add_event_callback(spout_obs_event, nullptr);

	// load spout filter
	spout_filter_info = create_spout_filter_info();
	obs_register_source(&spout_filter_info);

	blog(LOG_INFO, "win-spout loaded!");

	return true;
}

void obs_module_unload()
{
	if (spout_output_settings) {
		delete spout_output_settings;
	}
	blog(LOG_INFO, "win-spout unloaded!");
}

const char *obs_module_name()
{
	return "win-spout";
}

const char *obs_module_description()
{
	return "Spout input/output for OBS Studio";
}

bool spout_output_start(const char *SpoutName)
{
	if (!win_spout_out) {
		return false;
	}
	if (obs_output_active(win_spout_out)) {
		blog(LOG_INFO, "Spout output already running");
		return true;
	}

	obs_data_t *settings = obs_output_get_settings(win_spout_out);
	obs_data_set_string(settings, "senderName", SpoutName);
	obs_output_update(win_spout_out, settings);
	obs_data_release(settings);

	// obs_output_create() captured the video/audio handles that existed at module load.
	// Every obs_reset_video() (profile switch, Settings > Video) replaces them, so point the
	// output at the current ones before starting or we hand libobs a dangling video_t.
	obs_output_set_media(win_spout_out, obs_get_video(), obs_get_audio());

	if (!obs_output_start(win_spout_out)) {
		blog(LOG_ERROR, "Failed to start Spout output '%s'", SpoutName);
		return false;
	}
	return true;
}

void spout_output_stop()
{
	if (win_spout_out) {
		obs_output_stop(win_spout_out);
	}
}

bool spout_output_active()
{
	return win_spout_out && obs_output_active(win_spout_out);
}
