/**
 * copyright Off World Live Ltd (https://offworld.live), 2019-2021
 *
 * and licenced under the GPL v2 (https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
 *
 * Many thanks to authors of https://github.com/baffler/OBS-OpenVR-Input-Plugin which
 * was used as guidance to working with the OBS Studio APIs
 */

#ifndef WINSPOUT_H
#define WINSPOUT_H

#include "Include/SpoutLibrary.h"
#ifdef _WIN64
#pragma comment(lib, "Binaries/x64/SpoutLibrary.lib")
#else
#pragma comment(lib, "Binaries/Win32/SpoutLibrary.lib")
#endif

#define blog(log_level, message, ...) \
	blog(log_level, "[win_spout] " message, ##__VA_ARGS__)

extern SPOUTHANDLE spoutptr;

#endif // WINSPOUT_H
