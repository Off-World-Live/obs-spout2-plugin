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

spoutDX sender;

struct spout_output {

	obs_output_t* output;
	const char* senderName;
	bool output_started;
	pthread_mutex_t mutex;
};

bool init_spout()
{
	sender.SetMaxSenders(255);

	if (!sender.OpenDirectX11()) {
		blog(LOG_ERROR, "Failed to Open DX11");
		return false;
	}
	blog(LOG_INFO, "Opened DX11");

	return true;
}

static const char* win_spout_output_get_name(void* unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("outputname");
}

static void win_spout_output_update(void* data, obs_data_t* settings)
{
	spout_output* context = (spout_output*)data;
	context->senderName = obs_data_get_string(settings, "senderName");
}

static void win_spout_output_destroy(void* data)
{
	spout_output* context = (spout_output*)data;

	sender.CloseDirectX11();

	if (context)
	{
		pthread_mutex_destroy(&context->mutex);
		bfree(context);
	}
}

static void* win_spout_output_create(obs_data_t* settings, obs_output_t* output)
{
	spout_output* context = (spout_output*)bzalloc(sizeof(spout_output));
	context->output = output;
	context->senderName = obs_data_get_string(settings, "senderName");
	context->output_started = false;

	win_spout_output_update(context, settings);

	if (init_spout())
	{
		pthread_mutex_init_value(&context->mutex);
		if (pthread_mutex_init(&context->mutex, NULL) == 0) {
			UNUSED_PARAMETER(settings);
			return context;
		}
	}

	blog(LOG_ERROR, "Failed to create spout output!");
	sender.CloseDirectX11();
	win_spout_output_destroy(context);

	return NULL;
}

bool win_spout_output_start(void* data)
{
	spout_output* context = (spout_output*)data;

	if (!context->output)
	{
		blog(LOG_ERROR, "Trying to start with no output!");
		return false;
	}

	sender.SetSenderName(context->senderName);

	int32_t width = (int32_t)obs_output_get_width(context->output);
	int32_t height = (int32_t)obs_output_get_height(context->output);

	video_t* video = obs_output_video(context->output);
	if (!video)
	{
		blog(LOG_ERROR, "Trying to start with no video!");
		return false;
	}

	if (!obs_output_can_begin_data_capture(context->output, 0))
	{
		blog(LOG_ERROR, "Unable to begin data capture!");
		return false;
	}

	video_scale_info info{ };
	// we enforce BGRA format as it works well with spout
	info.format = VIDEO_FORMAT_BGRA;
	info.width = width;
	info.height = height;

	obs_output_set_video_conversion(context->output, &info);

	context->output_started = obs_output_begin_data_capture(context->output, 0);

	if (!context->output_started)
	{
		blog(LOG_ERROR, "Unable to start capture!");
	}
	else
		blog(LOG_INFO, "Creating capture with name: %s, width: %i, height: %i", context->senderName, width, height);


	return context->output_started;
}

void win_spout_output_stop(void* data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);

	spout_output* context = (spout_output*)data;

	if (context->output_started)
	{
		context->output_started = false;

		obs_output_end_data_capture(context->output);
		sender.ReleaseSender();
	}
}

void win_spout_output_rawvideo(void* data, struct video_data* frame)
{
	spout_output* context = (spout_output*)data;

	if (!context->output_started)
	{
		return;
	}

	int32_t width = (int32_t)obs_output_get_width(context->output);
	int32_t height = (int32_t)obs_output_get_height(context->output);

	pthread_mutex_lock(&context->mutex);
	sender.SendImage(frame->data[0], width, height);
	pthread_mutex_unlock(&context->mutex);
}

obs_properties_t* win_spout_output_getproperties(void* data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t* props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_properties_add_text(
		props, "spout_output_name",
		obs_module_text("outputname"),
		OBS_TEXT_DEFAULT);

	return props;
}

struct obs_output_info create_spout_output_info()
{
	struct obs_output_info spout_output_info = {};

	spout_output_info.id = "spout_output";
	spout_output_info.flags = OBS_OUTPUT_VIDEO;
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

