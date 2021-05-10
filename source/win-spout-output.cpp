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
#include "Spout.h"

struct spout_output {
	obs_output_t* output;
	const char* senderName;
	bool output_started;
	bool spout_created;
	int width;
	int height;
	HANDLE shared_sending_handle = NULL;
	ID3D11Texture2D* sending_texture = nullptr;
	pthread_mutex_t mutex;
	ID3D11Device* DX11_Device = nullptr;
	ID3D11DeviceContext* DX11_ImmediateContext = nullptr;
	// Spout Lib Interface
	spoutSenderNames* senderNames = nullptr;
	spoutDirectX sdx;

public:
	void reset_info()
	{
		width = 0;
		height = 0;
	}

	void reset_spout_resource()
	{
		if (sending_texture) {
			sending_texture->Release();
			sending_texture = nullptr;
			shared_sending_handle = NULL;
			spout_created = false;
		}
	}
};

bool openDX11(void* data)
{
	spout_output* context = (spout_output*)data;
	if (!context->DX11_Device) {
		blog(LOG_INFO, "OpenDX11");
		// Create a DirectX 11 device if not already
		context->DX11_Device = context->sdx.CreateDX11device();
		if (!context->DX11_Device) {
			blog(LOG_ERROR, "Failed to Open DX11");
			return false;
		}
		context->DX11_Device->GetImmediateContext(&context->DX11_ImmediateContext);
		blog(LOG_INFO, "created device (0x%.7X)", PtrToUint(context->DX11_Device));
	}
	return true;
}

void closeDX11(void* data)
{
	spout_output* context = (spout_output*)data;
	if (context->DX11_Device) {
		context->DX11_Device->Release();
		context->DX11_ImmediateContext->Release();
	}

	context->DX11_Device = nullptr;
	context->DX11_ImmediateContext = nullptr;
}

bool init_spout(void* data)
{
	spout_output* context = (spout_output*)data;
	context->senderNames = new spoutSenderNames;
	return openDX11(context);
}

void deinit_spout(void* data)
{
	spout_output* context = (spout_output*)data;
	context->reset_spout_resource();
	closeDX11(context);
	if (context && context->senderNames)
	{
		delete context->senderNames;
		context->senderNames = nullptr;
	}
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

	deinit_spout(context);

	if (context)
	{
		context->reset_info();
		context->reset_spout_resource();
		pthread_mutex_destroy(&context->mutex);
		bfree(context);
	}
}

static void* win_spout_output_create(obs_data_t* settings, obs_output_t* output)
{
	spout_output* context = (spout_output*)bzalloc(sizeof(spout_output));
	context->output = output;
	context->senderName = "";
	context->output_started = false;
	context->reset_info();

	win_spout_output_update(context, settings);

	if (init_spout(context))
	{
		pthread_mutex_init_value(&context->mutex);
		if (pthread_mutex_init(&context->mutex, NULL) == 0) {
			UNUSED_PARAMETER(settings);
			return context;
		}
	}

	blog(LOG_ERROR, "Failed to create spout output!");
	deinit_spout(context);
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

	context->width = (int32_t)obs_output_get_width(context->output);
	context->height = (int32_t)obs_output_get_height(context->output);

	video_t* video = obs_output_video(context->output);
	if (!video)
	{
		blog(LOG_ERROR, "Trying to start with no video!");
		context->reset_info();
		return false;
	}

	if (!obs_output_can_begin_data_capture(context->output, 0))
	{
		blog(LOG_ERROR, "Unable to begin data capture!");
		context->reset_info();
		return false;
	}

	video_scale_info info { };
	// we enforce BGRA format as it works well with spout
	info.format = VIDEO_FORMAT_BGRA;
	info.width = context->width;
	info.height = context->height;

	obs_output_set_video_conversion(context->output, &info);

	context->output_started = obs_output_begin_data_capture(context->output, 0);

	if (!context->output_started)
	{
		context->reset_info();
		blog(LOG_ERROR, "Unable to start capture!");
	}
	else
		blog(LOG_INFO, "Creating capture with name: %s, width: %i, height: %i", context->senderName, context->width, context->height);


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
		context->senderNames->ReleaseSenderName(context->senderName);
		context->reset_info();
		context->reset_spout_resource();
	}
}

bool create_spout_resource(spout_output* context)
{
	if (!context->sdx.CreateSharedDX11Texture(context->DX11_Device,
		context->width,
		context->height,
		DXGI_FORMAT_B8G8R8A8_UNORM,
		&context->sending_texture,
		context->shared_sending_handle))
	{
		blog(LOG_ERROR, "Failed to create shared texture");
		return false;
	}

	// we don't need to create it again if we are only updating texture size
	if (!context->spout_created)
	{
		if (!context->senderNames->CreateSender(context->senderName,
			context->width,
			context->height,
			context->shared_sending_handle,
			DXGI_FORMAT_B8G8R8A8_UNORM))
		{
			blog(LOG_ERROR, "Failed while creating sender");
			return false;
		}
	}

	blog(LOG_INFO, "Created sender DX11 with sender name %s, Width: %i, Height: %i", context->senderName, context->width, context->height);
	return true;
}

void win_spout_output_rawvideo(void* data, struct video_data* frame)
{
	spout_output* context = (spout_output*)data;

	if (!context->output_started)
	{
		return;
	}

	if (!context->spout_created)
	{
		if (!create_spout_resource(context))
		{
			context->reset_info();
			context->reset_spout_resource();
			blog(LOG_ERROR, "Unable to create spout resource!");
			return;
		}
		context->spout_created = true;
	}

	int32_t currentWidth = (int32_t)obs_output_get_width(context->output);
	int32_t currentHeight = (int32_t)obs_output_get_height(context->output);

	if (context->width != currentWidth || context->height != currentHeight)
	{
		context->width = currentWidth;
		context->height = currentHeight;
		context->reset_spout_resource();
		create_spout_resource(context);
	}

	pthread_mutex_lock(&context->mutex);
	context->DX11_ImmediateContext->UpdateSubresource(context->sending_texture, 0, NULL, frame->data[0], context->width * 4, 0);
	context->DX11_ImmediateContext->Flush();
	pthread_mutex_unlock(&context->mutex);
	context->senderNames->UpdateSender(context->senderName, context->width, context->height, context->shared_sending_handle);
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

