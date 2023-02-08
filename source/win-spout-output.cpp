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

struct spout_output
{
	spoutDX* sender;
	obs_output_t* output;
	const char* senderName;
	bool output_started;
	// mutex guards accesses to rest of context variables,
	// and any methods on spoutDX* sender.
	// Calling obs methods on obs_output_t* output seems thread-safe.
	// trying to avoid calling obs methods while holding our own mutex.
	pthread_mutex_t mutex;
};

// Forward decls
void win_spout_output_destroy(void *data);

bool init_spout(void* data)
{
	spout_output* context = (spout_output*)data;
	// Enable for debugging spout:
	// spoututils::SetSpoutLogLevel(spoututils::SPOUT_LOG_VERBOSE);
	// spoututils::EnableSpoutLog();
	context->sender->SetMaxSenders(255);

	if (!context->sender->OpenDirectX11()) {
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

static void* win_spout_output_create(obs_data_t* settings, obs_output_t* output)
{
	spout_output* context = (spout_output*)bzalloc(sizeof(spout_output));
	context->output = output;
	context->senderName = obs_data_get_string(settings, "senderName");
	context->output_started = false;
	context->sender = new spoutDX;

	pthread_mutex_init_value(&context->mutex);
	if (pthread_mutex_init(&context->mutex, NULL) != 0) {
		blog(LOG_ERROR, "Failed to create mutex for spout output!");
		win_spout_output_destroy(context);
		return nullptr;
	}

	if (!init_spout(context))
	{
		blog(LOG_ERROR, "Failed to create spout output!");
		win_spout_output_destroy(context);
		return nullptr;	
	}

	win_spout_output_update(context, settings);

	// from this point, need to lock mutex to access context safely
	return context;
}

static void win_spout_output_destroy(void *data)
{
	spout_output *context = (spout_output *)data;

	if (!context) {
		return;
	}

	if (context->sender) {
		context->sender->CloseDirectX11();
		delete context->sender;
		context->sender = nullptr;
	}

	pthread_mutex_destroy(&context->mutex);
	bfree(context);
}

bool win_spout_output_start(void* data)
{
	spout_output* context = (spout_output*)data;

	if (!context->output)
	{
		blog(LOG_ERROR, "Trying to start with no output!");
		return false;
	}

	pthread_mutex_lock(&context->mutex);

	const char *senderName = context->senderName;
	context->sender->SetSenderName(context->senderName);

	obs_output_t *output = context->output;

	pthread_mutex_unlock(&context->mutex);

	int32_t width = (int32_t)obs_output_get_width(output);
	int32_t height = (int32_t)obs_output_get_height(output);

	video_t* video = obs_output_video(output);
	if (!video)
	{
		blog(LOG_ERROR, "Trying to start with no video!");
		return false;
	}

	if (!obs_output_can_begin_data_capture(output, 0))
	{
		blog(LOG_ERROR, "Unable to begin data capture!");
		return false;
	}

	video_scale_info info{ };
	// we enforce BGRA format as it works well with spout
	info.format = VIDEO_FORMAT_BGRA;
	info.width = width;
	info.height = height;

	obs_output_set_video_conversion(output, &info);

	bool started = obs_output_begin_data_capture(output, 0);

	pthread_mutex_lock(&context->mutex);

	context->output_started = started;

	pthread_mutex_unlock(&context->mutex);

	if (!started) {
		blog(LOG_ERROR, "Unable to start capture!");
	} else {
		blog(LOG_INFO,
		     "Creating capture with name: %s, width: %i, height: %i",
		     context->senderName, width, height);
	}

	return started;
}

void win_spout_output_stop(void* data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);

	spout_output* context = (spout_output*)data;

	pthread_mutex_lock(&context->mutex);
	bool started = context->output_started;
	obs_output_t *output = context->output;
	pthread_mutex_unlock(&context->mutex);

	if (started)
	{
		obs_output_end_data_capture(output);

		pthread_mutex_lock(&context->mutex);

		context->sender->ReleaseSender();
		context->output_started = false;

		pthread_mutex_unlock(&context->mutex);
	}
}

void win_spout_output_rawvideo(void* data, struct video_data* frame)
{
	spout_output* context = (spout_output*)data;

	pthread_mutex_lock(&context->mutex);

	bool started = context->output_started;
	obs_output_t *output = context->output;

	pthread_mutex_unlock(&context->mutex);

	if (!started)
	{
		return;
	}

	int32_t width = (int32_t)obs_output_get_width(output);
	int32_t height = (int32_t)obs_output_get_height(output);

	pthread_mutex_lock(&context->mutex);

	context->sender->SendImage(frame->data[0], width, height);

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

