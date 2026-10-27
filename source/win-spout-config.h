/**
 * Copyright Off World Live Ltd (https://offworld.live), 2019-2021
 *
 * and licenced under the GPL v2 (https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
 *
 * Many thanks to authors of https://github.com/baffler/OBS-OpenVR-Input-Plugin which
 * was used as guidance to working with the OBS Studio APIs
 */

#ifndef WINSPOUTCONFIG_H
#define WINSPOUTCONFIG_H

#include <atomic>

#include <QString>
#include <obs-module.h>

class win_spout_config {
public:
	win_spout_config();
	static win_spout_config *get();
	void load();
	void save();

	bool auto_start;
	QString spout_output_name;
	// Continuous broadcast: when enabled, Spout output filters ignore whether their
	// source is in the active scene, registering the sender and broadcasting from a
	// cold start. When disabled the previous behaviour is kept (send only while the
	// source is being rendered). Default off. Atomic because the render thread reads
	// it every frame while the settings dialog writes it from the UI thread.
	std::atomic<bool> continuous_broadcast;
	// Key bindings of the output's Start/Stop hotkeys (#54), as obs_hotkey_pair_save() JSON.
	// Frontend hotkeys are not saved by OBS, so they live here with the other output settings.
	QString output_hotkeys;

private:
	static win_spout_config *_instance;
};

#endif // WINSPOUTCONFIG_H
