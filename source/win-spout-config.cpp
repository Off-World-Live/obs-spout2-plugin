/**
 * Copyright Off World Live Ltd (https://offworld.live), 2019-2021
 *
 * and licenced under the GPL v2 (https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
 *
 * Many thanks to authors of https://github.com/baffler/OBS-OpenVR-Input-Plugin which
 * was used as guidance to working with the OBS Studio APIs
 */

#include "win-spout-config.h"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#define SECTION_NAME "win_spout"
#define PARAM_AUTO_START "auto_start"
#define PARAM_SPOUT_OUTPUT_NAME "spout_output_name"

win_spout_config* win_spout_config::_instance = nullptr;

win_spout_config::win_spout_config()
	: auto_start(false), spout_output_name("OBS_Spout")
{
	config_t* obs_config = obs_frontend_get_global_config();

	if (obs_config) {
		config_set_default_bool(obs_config, SECTION_NAME,
					PARAM_AUTO_START, auto_start);
		config_set_default_string(obs_config, SECTION_NAME,
					  PARAM_SPOUT_OUTPUT_NAME,
					  spout_output_name.toUtf8().constData());
	}
}

void win_spout_config::load()
{
	config_t* obs_config = obs_frontend_get_global_config();
	if (obs_config) {
		auto_start = config_get_bool(obs_config, SECTION_NAME,
						PARAM_AUTO_START);
		spout_output_name = config_get_string(obs_config, SECTION_NAME,
					       PARAM_SPOUT_OUTPUT_NAME);
	}
}

void win_spout_config::save()
{
	config_t* obs_config = obs_frontend_get_global_config();
	if (obs_config) {
		config_set_bool(obs_config, SECTION_NAME,
				PARAM_AUTO_START, auto_start);
		config_set_string(obs_config, SECTION_NAME,
				  PARAM_SPOUT_OUTPUT_NAME,
				  spout_output_name.toUtf8().constData());
		config_save(obs_config);
	}
}

win_spout_config* win_spout_config::get()
{
	if (!_instance) {
		_instance = new win_spout_config();
	}
	return _instance;
}
