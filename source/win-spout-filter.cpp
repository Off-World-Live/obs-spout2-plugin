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
	// mutex guards accesses to fields in SHARED section
	// and any methods on spoutDX* filter_sender.
	// Calling obs methods on obs types seems thread-safe.
	// trying to avoid calling obs methods while holding our own mutex.
	pthread_mutex_t mutex;

	// [SHARED]
	spoutDX* filter_sender; // owned by the filter
	obs_source_t* source_context;
	const char* sender_name; // owned by obs

	// [RENDER] After creation, only accessed on render thread
	gs_texrender_t* texrender_curr;         // owned by filter
	gs_texrender_t* texrender_prev;         // "
	gs_texrender_t* texrender_intermediate; // "
	gs_stagesurf_t* stagesurface;           // "

	// set after we successfully init on render thread
	bool is_initialised;
	// detect that source is still active by setting in _videorender() and clearing in _offscreen_render()
	bool is_active;
};

// forward decls
void win_spout_filter_update(void *data, obs_data_t *settings);
void win_spout_filter_destroy(void *data);

bool init_on_render_thread(struct win_spout_filter *context)
{
	if (context->is_initialised) { return true; }

	// Create textures
	// Use a Spout-compatible texture format
	context->texrender_curr =
		gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
	context->texrender_prev =
		gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
	context->texrender_intermediate =
		gs_texrender_create(GS_BGRA, GS_ZS_NONE);

	// Init Spout
	context->filter_sender->SetMaxSenders(255);

	// Get the OBS D3D11 device, rather than creating a new one for each filter.
	// If this ends up causing deadlocks or perf issues, can revisit.
	ID3D11Device *const d3d_device = (ID3D11Device *)gs_get_device_obj();

	if (!d3d_device) {
		blog(LOG_ERROR, "Failed to retrieve OBS d3d11 device");
		return false;
	}

	if (!context->filter_sender->OpenDirectX11(d3d_device))
	{
		blog(LOG_ERROR, "Failed to Open DX11");
		return false;
	}

	blog(LOG_INFO, "Opened DX11");

	context->is_initialised = true;

	return true;
}

const char* win_spout_filter_getname(void* unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("filtername");
}

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

	// We check if video_render has been called since the last offscreen_render
	if (!context->is_active) { return; }
	context->is_active = false;

	if (!init_on_render_thread(context)) {
		blog(LOG_ERROR,
		     "Failed to create DX11 context for spout filter!");
		win_spout_filter_destroy(context);
		return;
	}

	pthread_mutex_lock(&context->mutex);
	obs_source_t* source_context = context->source_context;
	gs_texrender_t* texrender_intermediate = context->texrender_intermediate;
	gs_texrender_t* texrender_curr = context->texrender_curr;
	gs_texrender_t* texrender_prev = context->texrender_prev;
	pthread_mutex_unlock(&context->mutex);

	obs_source_t* target = obs_filter_get_parent(source_context);
	if (!target) return;

	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);

	// Render the target to an intemediate format in sRGB-aware format
	gs_texrender_reset(texrender_intermediate);
	if (gs_texrender_begin(texrender_intermediate, width, height)) {
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f,
			 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		obs_source_video_render(target);

		gs_blend_state_pop();
		gs_texrender_end(texrender_intermediate);
	}

	// Use the default effect to render it back into a format Spout accepts
	gs_texrender_reset(texrender_curr);
	if (gs_texrender_begin(texrender_curr, width, height))
	{
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		// To get sRGB handling, render with the default effect
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_texture_t *tex = gs_texrender_get_texture(texrender_intermediate);
		if (tex) {
			const bool linear_srgb = gs_get_linear_srgb();

			const bool previous = gs_framebuffer_srgb_enabled();
			gs_enable_framebuffer_srgb(linear_srgb);

			gs_eparam_t *image =
				gs_effect_get_param_by_name(effect, "image");
			if (linear_srgb)
				gs_effect_set_texture_srgb(image, tex);
			else
				gs_effect_set_texture(image, tex);

			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(tex, 0, width, height);

			gs_enable_framebuffer_srgb(previous);
		}

		gs_blend_state_pop();
		gs_texrender_end(texrender_curr);

		bool ok = false;

		gs_texture_t *prev_tex =
			gs_texrender_get_texture(texrender_prev);
		ID3D11Texture2D *prev_tex_d3d11 = nullptr;
		if (prev_tex) {
			prev_tex_d3d11 = (ID3D11Texture2D *)gs_texture_get_obj(prev_tex);
		}
		pthread_mutex_lock(&context->mutex);

		if (prev_tex) {
			ok = context->filter_sender->SendTexture(prev_tex_d3d11);
		}

		// Swap the buffers
		// Double-buffering avoids the need for a flush, and also fixes
		// some issues related to G-Sync.
		
		context->texrender_curr = texrender_prev;
		context->texrender_prev = texrender_curr;

		pthread_mutex_unlock(&context->mutex);

		if (!ok) {
			blog(LOG_ERROR, "Error calling SendTexture()!");
		}
	}
}

void win_spout_filter_update(void* data, obs_data_t* settings)
{
	UNUSED_PARAMETER(settings);
	struct win_spout_filter* context = (win_spout_filter*)data;

	obs_remove_main_render_callback(win_spout_offscreen_render, context);

	const char *sender_name = obs_data_get_string(settings, FILTER_PROP_NAME);

	pthread_mutex_lock(&context->mutex);

	context->filter_sender->ReleaseSender();
	context->sender_name = sender_name;
	context->filter_sender->SetSenderName(sender_name);

	pthread_mutex_unlock(&context->mutex);

	obs_add_main_render_callback(win_spout_offscreen_render, context);
}

void* win_spout_filter_create(obs_data_t* settings, obs_source_t* source)
{
	struct win_spout_filter* context = (win_spout_filter*)bzalloc(sizeof(win_spout_filter));
	// Despite bzalloc I still want to at least initialise pointer fields
	context->filter_sender = nullptr;
	context->source_context = nullptr;
	context->sender_name = nullptr;
	context->texrender_curr = nullptr;
	context->texrender_prev = nullptr;
	context->texrender_intermediate = nullptr;
	context->stagesurface = nullptr;
	context->is_initialised = false;
	context->is_active = false;

	pthread_mutex_init_value(&context->mutex);
	if (pthread_mutex_init(&context->mutex, NULL) != 0) {
		blog(LOG_ERROR, "Failed to create mutex for spout filter!");
		win_spout_filter_destroy(context);
		return nullptr;
	}

	context->source_context = source;

	context->sender_name = obs_data_get_string(settings, FILTER_PROP_NAME);

	context->filter_sender = new spoutDX;

	win_spout_filter_update(context, settings);

	// from this point, need to lock mutex to access context safely
	return context;
}

void win_spout_filter_destroy(void* data)
{
	struct win_spout_filter* context = (win_spout_filter*)data;

	if (!context) {
		return;
	}

	obs_remove_main_render_callback(win_spout_offscreen_render, context);

	if (context->filter_sender) {
		context->filter_sender->ReleaseSender();
		context->filter_sender->CloseDirectX11();
		delete context->filter_sender;
		context->filter_sender = nullptr;
	}
	
	if (context->stagesurface) {
		gs_stagesurface_unmap(context->stagesurface);
		gs_stagesurface_destroy(context->stagesurface);
		context->stagesurface = nullptr;
	}
	
	if (context->texrender_intermediate) {
		gs_texrender_destroy(context->texrender_intermediate);
		context->texrender_intermediate = nullptr;
	}
	
	if (context->texrender_prev) {
		gs_texrender_destroy(context->texrender_prev);
		context->texrender_prev = nullptr;
	}

	if (context->texrender_curr) {
		gs_texrender_destroy(context->texrender_curr);
		context->texrender_curr = nullptr;
	}

	pthread_mutex_destroy(&context->mutex);
	bfree(context);
}

void win_spout_filter_tick(void* data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct win_spout_filter* context = (win_spout_filter*)data;
}

void win_spout_filter_videorender(void* data, gs_effect_t* effect)
{
	UNUSED_PARAMETER(effect);
	struct win_spout_filter* context = (win_spout_filter*)data;

	pthread_mutex_lock(&context->mutex);

	context->is_active = true;

	pthread_mutex_unlock(&context->mutex);

	obs_source_skip_video_filter(context->source_context);
}

struct obs_source_info create_spout_filter_info()
{
	struct obs_source_info win_spout_filter_info = {};
	win_spout_filter_info.id = "win_spout_filter";
	win_spout_filter_info.type = OBS_SOURCE_TYPE_FILTER;
	win_spout_filter_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
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
