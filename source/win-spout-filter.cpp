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



struct win_spout_filter
{
	spoutDX* filter_sender;
	obs_source_t* source_context;
	const char* sender_name;
	uint32_t width;
	uint32_t height;
	gs_texrender_t* texrender_curr;
	gs_texrender_t* texrender_prev;
	gs_stagesurf_t* stagesurface;
	video_t* video_output;
	uint8_t* video_data;
	uint32_t video_linesize;
	obs_video_info video_info;
};

bool openDX11(void* data)
{
	struct win_spout_filter* context = (win_spout_filter*)data;
	context->filter_sender->SetMaxSenders(255);
	if (!context->filter_sender->OpenDirectX11())
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

void win_spout_offscreen_render(void* data, uint32_t cx, uint32_t cy)
{
	
	UNUSED_PARAMETER(cx);
	UNUSED_PARAMETER(cy);
	struct win_spout_filter* context = (win_spout_filter*)data;

	obs_source_t* target = obs_filter_get_parent(context->source_context);
	if (!target) return;

	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);

	gs_texrender_reset(context->texrender_curr);

	if (gs_texrender_begin(context->texrender_curr, width, height))
	{
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		// Colours are wrong with this set to either value...
		// const bool previous = gs_set_linear_srgb(false);

		obs_source_video_render(target);

		// gs_set_linear_srgb(previous);

		gs_blend_state_pop();
		gs_texrender_end(context->texrender_curr);

		gs_texture_t *prev_tex =
			gs_texrender_get_texture(context->texrender_prev);
		if (prev_tex) {
			context->filter_sender->SendTexture((
				ID3D11Texture2D *)gs_texture_get_obj(prev_tex));
		}

		// Swap the buffers
		// Double-buffering avoids the need for a flush, and also fixes
		// some issues related to G-Sync.
		gs_texrender_t *tmp = context->texrender_curr;
		context->texrender_curr = context->texrender_prev;
		context->texrender_prev = tmp;
	}
}

void win_spout_filter_update(void* data, obs_data_t* settings)
{
	UNUSED_PARAMETER(settings);
	struct win_spout_filter* context = (win_spout_filter*)data;

	obs_remove_main_render_callback(win_spout_offscreen_render, context);
	context->filter_sender->ReleaseSender();
	context->sender_name = obs_data_get_string(settings, FILTER_PROP_NAME);
	context->filter_sender->SetSenderName(context->sender_name);
	obs_add_main_render_callback(win_spout_offscreen_render, context);
}

void* win_spout_filter_create(obs_data_t* settings, obs_source_t* source)
{
	struct win_spout_filter* context = (win_spout_filter*)bzalloc(sizeof(win_spout_filter));

	context->source_context = source;
	// Use a Spout-compatible texture format
	context->texrender_curr = gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
	context->texrender_prev = gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
	context->sender_name = obs_data_get_string(settings, FILTER_PROP_NAME);
	context->video_data = nullptr;

	context->filter_sender = new spoutDX;

	obs_get_video_info(&context->video_info);
	win_spout_filter_update(context, settings);

	

	if (openDX11(context))
	{
		return context;
	}

	blog(LOG_ERROR, "Failed to create spout output!");
	context->filter_sender->CloseDirectX11();
	delete context->filter_sender;
	return context;
}

void win_spout_filter_destroy(void* data)
{
	struct win_spout_filter* context = (win_spout_filter*)data;

	context->filter_sender->ReleaseSender();
	context->filter_sender->CloseDirectX11();
	delete context->filter_sender;

	if (context)
	{
		obs_remove_main_render_callback(win_spout_offscreen_render, context);
		video_output_close(context->video_output);
		gs_stagesurface_unmap(context->stagesurface);
		gs_stagesurface_destroy(context->stagesurface);
		gs_texrender_destroy(context->texrender_prev);
		gs_texrender_destroy(context->texrender_curr);
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
	win_spout_filter_info.output_flags = OBS_SOURCE_VIDEO;// | OBS_SOURCE_SRGB;
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
