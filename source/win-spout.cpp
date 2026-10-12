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
#include <QTimer>

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

static bool loading_done = false;
static bool start_pending = false;
static QByteArray pending_name;

bool spout_output_is_active(void)
{
	return win_spout_out && obs_output_active(win_spout_out);
}

static void spout_output_start_now(const char *SpoutName)
{
	if (!SpoutName || !*SpoutName) {
		return;
	}

	if (!win_spout_out) {
		obs_data_t *settings = obs_data_create();
		win_spout_out = obs_output_create("spout_output", "OBS Spout Output", settings, NULL);
		obs_data_release(settings);
	}

	if (!win_spout_out || obs_output_active(win_spout_out)) {
		return;
	}

	obs_data_t *settings = obs_output_get_settings(win_spout_out);
	obs_data_set_string(settings, "senderName", SpoutName);
	obs_output_update(win_spout_out, settings);
	obs_data_release(settings);

	obs_output_start(win_spout_out);
}

static void spout_run_pending_start()
{
	win_spout_config *config = win_spout_config::get();

	if (start_pending) {
		start_pending = false;
		spout_output_start_now(pending_name.constData());
		return;
	}

	if (!config->auto_start) {
		return;
	}

	QByteArray name = config->spout_output_name.toUtf8();
	spout_output_start_now(name.constData());
}

static void spout_obs_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		loading_done = true;
		QMainWindow *main_window = (QMainWindow *)obs_frontend_get_main_window();
		if (!main_window) {
			spout_run_pending_start();
			return;
		}

		QTimer::singleShot(0, main_window, []() { spout_run_pending_start(); });
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
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
	win_spout_config *config = win_spout_config::get();
	config->load();

	spout_output_info = create_spout_output_info();
	obs_register_output(&spout_output_info);

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

void spout_output_start(const char *SpoutName)
{
	if (!SpoutName || !*SpoutName) {
		return;
	}

	if (!loading_done) {
		pending_name = SpoutName;
		start_pending = true;
		return;
	}

	spout_output_start_now(SpoutName);
}

void spout_output_stop()
{
	obs_output_stop(win_spout_out);
}
