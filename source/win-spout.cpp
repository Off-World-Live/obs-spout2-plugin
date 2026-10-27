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
#include <QCoreApplication>
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

// Start/Stop hotkeys for the output, listed in Settings > Hotkeys (#54).
static obs_hotkey_pair_id output_hotkeys = OBS_INVALID_HOTKEY_PAIR_ID;

// Hotkeys fire on the hotkey thread; start and stop on the UI thread like the Tools dialog
// and the profile-change handling do.
static bool output_hotkey_start(void *, obs_hotkey_pair_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || spout_output_active()) {
		return false;
	}
	QMetaObject::invokeMethod(
		QCoreApplication::instance(),
		[] { spout_output_start(win_spout_config::get()->spout_output_name.toUtf8().constData()); },
		Qt::QueuedConnection);
	return true;
}

static bool output_hotkey_stop(void *, obs_hotkey_pair_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !spout_output_active()) {
		return false;
	}
	QMetaObject::invokeMethod(
		QCoreApplication::instance(), [] { spout_output_stop(); }, Qt::QueuedConnection);
	return true;
}

static void load_output_hotkeys()
{
	const QByteArray json = win_spout_config::get()->output_hotkeys.toUtf8();
	if (json.isEmpty()) {
		return;
	}
	obs_data_t *data = obs_data_create_from_json(json.constData());
	if (!data) {
		return;
	}
	obs_data_array_t *start = obs_data_get_array(data, "start");
	obs_data_array_t *stop = obs_data_get_array(data, "stop");
	obs_hotkey_pair_load(output_hotkeys, start, stop);
	obs_data_array_release(start);
	obs_data_array_release(stop);
	obs_data_release(data);
}

static void save_output_hotkeys()
{
	if (output_hotkeys == OBS_INVALID_HOTKEY_PAIR_ID) {
		return;
	}
	obs_data_array_t *start = nullptr;
	obs_data_array_t *stop = nullptr;
	obs_hotkey_pair_save(output_hotkeys, &start, &stop);
	obs_data_t *data = obs_data_create();
	obs_data_set_array(data, "start", start);
	obs_data_set_array(data, "stop", stop);

	win_spout_config *config = win_spout_config::get();
	config->output_hotkeys = obs_data_get_json(data);
	config->save();

	obs_data_release(data);
	obs_data_array_release(start);
	obs_data_array_release(stop);
}

static void spout_save_event(obs_data_t *, bool saving, void *)
{
	if (saving) {
		save_output_hotkeys();
	}
}

// The output can now be started and stopped by hotkey or obs-websocket, so keep an open
// Tools dialog's Start/Stop buttons in step with it.
static void output_activity_changed(void *data, calldata_t *)
{
	const bool running = data != nullptr;
	QMetaObject::invokeMethod(
		QCoreApplication::instance(),
		[running] {
			if (spout_output_settings) {
				spout_output_settings->set_started_button_state(!running);
			}
		},
		Qt::QueuedConnection);
}

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
		save_output_hotkeys();
		obs_hotkey_pair_unregister(output_hotkeys);
		output_hotkeys = OBS_INVALID_HOTKEY_PAIR_ID;
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
	// First line of every support log: both versions at a glance.
	blog(LOG_INFO, "win-spout %s (built against libobs %d.%d.%d) loading on OBS %s", PLUGIN_VERSION,
	     LIBOBS_API_MAJOR_VER, LIBOBS_API_MINOR_VER, LIBOBS_API_PATCH_VER, obs_get_version_string());

	// The source and filter share textures with OBS's D3D11 device, so they need the Direct3D 11
	// renderer. The output sends CPU frames from its own D3D11 device and works on any renderer (#64).
	obs_enter_graphics();
	const bool d3d11 = gs_get_device_type() == GS_DEVICE_DIRECT3D_11;
	obs_leave_graphics();
	if (!d3d11) {
		blog(LOG_WARNING, "OBS is running with the OpenGL renderer (--allow-opengl): the Spout2 Capture "
				  "source and the Spout Filter need Direct3D 11 and are not available. The Spout "
				  "Output (Tools menu) still works");
	}

	// load spout - source
	if (d3d11) {
		spout_source_info = create_spout_source_info();
		if (obs_source_get_display_name(spout_source_info.id)) {
			// libobs keeps the first registration, so ours would silently not appear (#56).
			blog(LOG_ERROR,
			     "another plugin already registered the source id '%s' (PRPR Live's prpr-library.dll is a known case); "
			     "'Spout2 Capture' will be missing from the Sources list until that plugin is removed",
			     spout_source_info.id);
		}
		obs_register_source(&spout_source_info);
	}

	// load spout output
	win_spout_config *config = win_spout_config::get();
	config->load();

	spout_output_info = create_spout_output_info();
	obs_register_output(&spout_output_info);

	obs_data_t *settings = obs_data_create();
	win_spout_out = obs_output_create("spout_output", "OBS Spout Output", settings, NULL);
	obs_data_release(settings);

	if (win_spout_out) {
		signal_handler_t *handler = obs_output_get_signal_handler(win_spout_out);
		signal_handler_connect(handler, "activate", output_activity_changed, (void *)1);
		signal_handler_connect(handler, "deactivate", output_activity_changed, nullptr);

		output_hotkeys = obs_hotkey_pair_register_frontend(
			"SpoutOutput.Start", obs_module_text("hotkeystartoutput"), "SpoutOutput.Stop",
			obs_module_text("hotkeystopoutput"), output_hotkey_start, output_hotkey_stop, nullptr, nullptr);
		load_output_hotkeys();
		obs_frontend_add_save_callback(spout_save_event, nullptr);
	}

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
	if (d3d11) {
		spout_filter_info = create_spout_filter_info();
		obs_register_source(&spout_filter_info);
	}

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
