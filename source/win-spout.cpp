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

static void spout_obs_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		if (!win_spout_out) {
			return;
		}

		obs_output_stop(win_spout_out);
		obs_output_release(win_spout_out);
		win_spout_out = nullptr;
	}
}

bool obs_module_load(void)
{
	// load spout - source
	spout_source_info = create_spout_source_info();
	obs_register_source(&spout_source_info);

	// load spout output
	QMainWindow *main_window = (QMainWindow *)obs_frontend_get_main_window();

	if (!main_window) {
		blog(LOG_ERROR, "Can't get main window!");
		return false;
	}

	win_spout_config *config = win_spout_config::get();
	config->load();

	spout_output_info = create_spout_output_info();
	obs_register_output(&spout_output_info);

	obs_data_t *settings = obs_data_create();
	win_spout_out = obs_output_create("spout_output", "OBS Spout Output", settings, NULL);
	obs_data_release(settings);

	QAction *menu_action = (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("toolslabel"));

	obs_frontend_push_ui_translation(obs_module_get_string);
	spout_output_settings = new win_spout_output_settings(main_window);
	obs_frontend_pop_ui_translation();

	auto menu_cb = [] {
		spout_output_settings->toggle_show_hide();
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

void spout_output_start(const char *SpoutName)
{
	obs_data_t *settings = obs_output_get_settings(win_spout_out);
	obs_data_set_string(settings, "senderName", SpoutName);
	obs_output_update(win_spout_out, settings);
	obs_data_release(settings);
	obs_output_start(win_spout_out);
}

void spout_output_stop()
{
	obs_output_stop(win_spout_out);
}
