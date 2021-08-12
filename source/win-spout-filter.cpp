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
#include <util/threading.h>
#include <media-io/video-frame.h>

#include "SpoutDX.h"

#define FILTER_PROP_NAME "spout_filter_name"

spoutDX filter_sender;

struct win_spout_filter
{
	obs_source_t* source_context;
	const char* sender_name;
	pthread_mutex_t mutex;
	uint32_t width;
	uint32_t height;
	gs_texrender_t* texrender;
	gs_stagesurf_t* stagesurface;
	video_t* video_output;
	uint8_t* video_data;
	uint32_t video_linesize;
	obs_video_info video_info;
};

bool openDX11()
{
	filter_sender.SetMaxSenders(255);
	if (!filter_sender.OpenDirectX11())
	{
		blog(LOG_ERROR, "Failed to Open DX11");
		return false;
	}
	blog(LOG_INFO, "Opened DX11");
	return true;
}

const char* win_spout_filter_getname(void* unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("filtername");
}

void win_spout_filter_update(void* data, obs_data_t* settings);

bool win_spout_filter_change_name(obs_properties_t*, obs_property_t*, void* data)
{
	struct win_spout_filter* context = (win_spout_filter*)data;
	obs_data_t* settings = obs_source_get_settings(context->source_context);
	win_spout_filter_update(context, settings);
	obs_data_release(settings);
	return true;
}

obs_properties_t* win_spout_filter_getproperties(void* unused)
{
	UNUSED_PARAMETER(unused);
	obs_properties_t* props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);
	obs_properties_add_text(props, FILTER_PROP_NAME, obs_module_text("spoutname"), OBS_TEXT_DEFAULT);
	obs_properties_add_button(props, "win_spout_apply", obs_module_text("changename"), win_spout_filter_change_name);
	return props;
}

void win_spout_filter_getdefaults(obs_data_t* defaults)
{
	obs_data_set_default_string(defaults, FILTER_PROP_NAME,
		obs_module_text("defaultfiltername"));
}

void win_spout_filter_raw_video(void* data, video_data* frame)
{
	struct win_spout_filter* context = (win_spout_filter*)data;

	if (!frame|| !frame->data[0]) return;

	pthread_mutex_lock(&context->mutex);
	filter_sender.SendImage(frame->data[0], context->width, context->height);
	pthread_mutex_unlock(&context->mutex);
}

void win_spout_offscreen_render(void* data, uint32_t cx, uint32_t cy)
{
	
	UNUSED_PARAMETER(cx);
	UNUSED_PARAMETER(cy);
	struct win_spout_filter* context = (win_spout_filter*)data;

	obs_source_t* target = obs_filter_get_parent(context->source_context);
	if (!target) return;

	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);

	gs_texrender_reset(context->texrender);

	if (gs_texrender_begin(context->texrender, width, height))
	{
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		obs_source_video_render(target);

		gs_blend_state_pop();
		gs_texrender_end(context->texrender);

		if (context->width != width || context->height != height)
		{

			gs_stagesurface_destroy(context->stagesurface);
			context->stagesurface = gs_stagesurface_create(width, height, GS_BGRA);

			video_output_info video_out = { 0 };
			video_out.format = VIDEO_FORMAT_BGRA;
			video_out.width = width;
			video_out.height = height;
			video_out.fps_den = context->video_info.fps_den;
			video_out.fps_num = context->video_info.fps_num;
			video_out.cache_size = 16;
			video_out.colorspace = VIDEO_CS_DEFAULT;
			video_out.range = VIDEO_RANGE_DEFAULT;
			video_out.name = obs_source_get_name(context->source_context);

			video_output_close(context->video_output);

			context->width = width;
			context->height = height;
			video_output_open(&context->video_output, &video_out);
			video_output_connect(context->video_output, nullptr, win_spout_filter_raw_video, context);


		}

		struct video_frame output_frame;
		if (video_output_lock_frame(context->video_output,
			&output_frame, 1, obs_get_video_frame_time()))
		{
			if (context->video_data) {
				gs_stagesurface_unmap(context->stagesurface);
				context->video_data = nullptr;
			}

			gs_stage_texture(context->stagesurface,
				gs_texrender_get_texture(context->texrender));
			gs_stagesurface_map(context->stagesurface,
				&context->video_data, &context->video_linesize);

			uint32_t linesize = output_frame.linesize[0];
			for (uint32_t i = 0; i < context->height; ++i) {
				uint32_t dst_offset = linesize * i;
				uint32_t src_offset = context->video_linesize * i;
				memcpy(output_frame.data[0] + dst_offset,
					context->video_data + src_offset,
					linesize);
			}

			video_output_unlock_frame(context->video_output);
		}
	}
}

void win_spout_filter_update(void* data, obs_data_t* settings)
{
	UNUSED_PARAMETER(settings);
	struct win_spout_filter* context = (win_spout_filter*)data;

	obs_remove_main_render_callback(win_spout_offscreen_render, context);
	filter_sender.ReleaseSender();
	context->sender_name = obs_data_get_string(settings, FILTER_PROP_NAME);
	filter_sender.SetSenderName(context->sender_name);
	obs_add_main_render_callback(win_spout_offscreen_render, context);
}

void* win_spout_filter_create(obs_data_t* settings, obs_source_t* source)
{
	struct win_spout_filter* context = (win_spout_filter*)bzalloc(sizeof(win_spout_filter));

	context->source_context = source;
	context->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	context->sender_name = obs_data_get_string(settings, FILTER_PROP_NAME);
	context->video_data = nullptr;

	obs_get_video_info(&context->video_info);
	win_spout_filter_update(context, settings);

	if (openDX11())
	{
		pthread_mutex_init_value(&context->mutex);
		if (pthread_mutex_init(&context->mutex, NULL) == 0)
		{
			return context;
		}
	}

	blog(LOG_ERROR, "Failed to create spout output!");
	filter_sender.CloseDirectX11();
	return context;
}

void win_spout_filter_destroy(void* data)
{
	struct win_spout_filter* context = (win_spout_filter*)data;

	filter_sender.ReleaseSender();
	filter_sender.CloseDirectX11();

	if (context)
	{
		obs_remove_main_render_callback(win_spout_offscreen_render, context);
		video_output_close(context->video_output);
		gs_stagesurface_unmap(context->stagesurface);
		gs_stagesurface_destroy(context->stagesurface);
		gs_texrender_destroy(context->texrender);
		pthread_mutex_destroy(&context->mutex);
		bfree(context);
	}
}

void win_spout_filter_tick(void* data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct win_spout_filter* context = (win_spout_filter*)data;
	obs_get_video_info(&context->video_info);
}

void win_spout_filter_videorender(void* data, gs_effect_t* effect)
{
	UNUSED_PARAMETER(effect);
	struct win_spout_filter* context = (win_spout_filter*)data;
	obs_source_skip_video_filter(context->source_context);
}

struct obs_source_info create_spout_filter_info()
{
	struct obs_source_info win_spout_filter_info = {};
	win_spout_filter_info.id = "win_spout_filter";
	win_spout_filter_info.type = OBS_SOURCE_TYPE_FILTER;
	win_spout_filter_info.output_flags = OBS_SOURCE_VIDEO;
	win_spout_filter_info.get_name = win_spout_filter_getname;
	win_spout_filter_info.get_properties = win_spout_filter_getproperties;
	win_spout_filter_info.get_defaults = win_spout_filter_getdefaults;
	win_spout_filter_info.create = win_spout_filter_create;
	win_spout_filter_info.destroy = win_spout_filter_destroy;
	win_spout_filter_info.update = win_spout_filter_update;
	win_spout_filter_info.video_tick = win_spout_filter_tick;
	win_spout_filter_info.video_render = win_spout_filter_videorender;

	return win_spout_filter_info;
}
