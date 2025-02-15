/*
 * Copyright (C) 2021 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#if RESHADE_ADDON

#include "version.h"
#include "reshade.hpp"
#include "addon_manager.hpp"
#include "dll_log.hpp"
#include "ini_file.hpp"

extern HMODULE g_module_handle;

extern std::filesystem::path get_module_path(HMODULE module);

bool reshade::addon::enabled = true;
std::vector<void *> reshade::addon::event_list[static_cast<uint32_t>(reshade::addon_event::max)];
std::vector<reshade::addon::info> reshade::addon::loaded_info;
#if RESHADE_GUI
std::vector<std::pair<std::string, void(*)(reshade::api::effect_runtime *, void *)>> reshade::addon::overlay_list;
#endif
static unsigned long s_reference_count = 0;

#ifndef RESHADE_TEST_APPLICATION
#pragma comment(lib, "GenericDepth.lib")
extern void register_addon_depth();
extern void unregister_addon_depth();
#endif

void reshade::load_addons()
{
	// Only load add-ons the first time a reference is added
	if (s_reference_count++ != 0)
		return;

#ifndef RESHADE_TEST_APPLICATION
#if RESHADE_VERBOSE_LOG
	LOG(INFO) << "Loading built-in add-ons ...";
#endif

	{	addon::info &info = addon::loaded_info.emplace_back();
		info.name = "Generic Depth";
		info.description = "Automatic depth buffer detection that works in the majority of games.";
		info.file = g_reshade_dll_path.u8string();
		info.author = "crosire";
		info.version = VERSION_STRING_FILE;

		register_addon_depth();
	}
#endif

#if RESHADE_ADDON_LOAD
	// Get directory from where to load add-ons from
	std::filesystem::path addon_search_path = g_reshade_base_path;
	if (global_config().get("INSTALL", "AddonPath", addon_search_path))
		addon_search_path = g_reshade_base_path / addon_search_path;

	LOG(INFO) << "Searching for add-ons (*.addon) in " << addon_search_path << " ...";

	std::error_code ec;
	for (std::filesystem::path path : std::filesystem::directory_iterator(addon_search_path, std::filesystem::directory_options::skip_permission_denied, ec))
	{
		if (path.extension() != L".addon")
			continue;

		LOG(INFO) << "Loading add-on from " << path << " ...";

		// Use 'LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR' to temporarily add add-on search path to the list of directories "LoadLibraryEx" will use to resolve DLL dependencies
		const HMODULE handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (handle == nullptr)
		{
			LOG(WARN) << "Failed to load add-on from " << path << " with error code " << GetLastError() << '.';
			continue;
		}
	}
#endif
}
void reshade::unload_addons()
{
	// Only unload add-ons after the last reference to the manager was released
	if (--s_reference_count != 0)
		return;

#if RESHADE_ADDON_LOAD
	// Create copy of add-on list before unloading, since add-ons call 'ReShadeUnregisterAddon' during 'FreeLibrary', which modifies the list
	const std::vector<reshade::addon::info> loaded_info_copy = addon::loaded_info;
	for (const reshade::addon::info &info : loaded_info_copy)
	{
		if (info.handle == nullptr)
			continue; // Skip built-in add-ons

		assert(info.handle != g_module_handle);

		LOG(INFO) << "Unloading add-on \"" << info.name << "\" ...";

		FreeLibrary(static_cast<HMODULE>(info.handle));
	}
#endif

#ifndef RESHADE_TEST_APPLICATION
#if RESHADE_VERBOSE_LOG
	LOG(INFO) << "Unloading built-in add-ons ...";
#endif

	unregister_addon_depth();
#endif

	addon::loaded_info.clear();
}

#if RESHADE_VERBOSE_LOG
static const char *addon_event_to_string(reshade::addon_event ev)
{
#define CASE(name) case reshade::addon_event::name: return #name
	switch (ev)
	{
		CASE(init_device);
		CASE(destroy_device);
		CASE(init_command_list);
		CASE(destroy_command_list);
		CASE(init_command_queue);
		CASE(destroy_command_queue);
		CASE(init_swapchain);
		CASE(create_swapchain);
		CASE(destroy_swapchain);
		CASE(init_effect_runtime);
		CASE(destroy_effect_runtime);
		CASE(init_sampler);
		CASE(create_sampler);
		CASE(destroy_sampler);
		CASE(init_resource);
		CASE(create_resource);
		CASE(destroy_resource);
		CASE(init_resource_view);
		CASE(create_resource_view);
		CASE(destroy_resource_view);
		CASE(init_pipeline);
		CASE(create_pipeline);
		CASE(destroy_pipeline);
		CASE(init_render_pass);
		CASE(create_render_pass);
		CASE(destroy_render_pass);
		CASE(init_framebuffer);
		CASE(create_framebuffer);
		CASE(destroy_framebuffer);
		CASE(update_buffer_region);
		CASE(update_texture_region);
		CASE(update_descriptor_sets);
		CASE(barrier);
		CASE(begin_render_pass);
		CASE(finish_render_pass);
		CASE(bind_render_targets_and_depth_stencil);
		CASE(bind_pipeline);
		CASE(bind_pipeline_states);
		CASE(bind_viewports);
		CASE(bind_scissor_rects);
		CASE(push_constants);
		CASE(push_descriptors);
		CASE(bind_descriptor_sets);
		CASE(bind_index_buffer);
		CASE(bind_vertex_buffers);
		CASE(draw);
		CASE(draw_indexed);
		CASE(dispatch);
		CASE(draw_or_dispatch_indirect);
		CASE(copy_resource);
		CASE(copy_buffer_region);
		CASE(copy_buffer_to_texture);
		CASE(copy_texture_region);
		CASE(copy_texture_to_buffer);
		CASE(resolve_texture_region);
		CASE(clear_attachments);
		CASE(clear_depth_stencil_view);
		CASE(clear_render_target_view);
		CASE(clear_unordered_access_view_uint);
		CASE(clear_unordered_access_view_float);
		CASE(generate_mipmaps);
		CASE(reset_command_list);
		CASE(execute_command_list);
		CASE(execute_secondary_command_list);
		CASE(present);
		CASE(reshade_begin_effects);
		CASE(reshade_finish_effects);
	}
#undef  CASE
	return "unknown";
}
#endif

reshade::addon::info *find_addon(HMODULE module)
{
	for (auto it = reshade::addon::loaded_info.rbegin(); it != reshade::addon::loaded_info.rend(); ++it)
		if (it->handle == module || (module == g_module_handle && it->handle == nullptr))
			return &(*it);
	return nullptr;
}

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(HMODULE module, uint32_t api_version);
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(HMODULE module);

extern "C" __declspec(dllexport) void ReShadeRegisterEvent(reshade::addon_event ev, void *callback);
extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(reshade::addon_event ev, void *callback);

extern "C" __declspec(dllexport) void ReShadeRegisterOverlay(const char *title, void(*callback)(reshade::api::effect_runtime *runtime, void *imgui_context));
extern "C" __declspec(dllexport) void ReShadeUnregisterOverlay(const char *title);

bool ReShadeRegisterAddon(HMODULE module, uint32_t api_version)
{
	// Can only register an add-on module once
	if (module == nullptr || module == g_module_handle || find_addon(module) != nullptr)
		return false;

	// Check that the requested API version is supported
	if (api_version > RESHADE_API_VERSION)
		return false;

	const std::filesystem::path path = get_module_path(module);

	reshade::addon::info info;
	info.name = path.stem().u8string();
	info.file = path.u8string();
	info.handle = module;

	DWORD version_dummy, version_size = GetFileVersionInfoSizeW(path.c_str(), &version_dummy);
	std::vector<uint8_t> version_data(version_size);
	if (GetFileVersionInfoW(path.c_str(), version_dummy, version_size, version_data.data()))
	{
		if (char *product_name = nullptr;
			VerQueryValueA(version_data.data(), "\\StringFileInfo\\040004b0\\ProductName", reinterpret_cast<LPVOID *>(&product_name), nullptr))
		{
			info.name = product_name;
		}
		if (char *company_name = nullptr;
			VerQueryValueA(version_data.data(), "\\StringFileInfo\\040004b0\\CompanyName", reinterpret_cast<LPVOID *>(&company_name), nullptr))
		{
			info.author = company_name;
		}

		if (char *file_version = nullptr;
			VerQueryValueA(version_data.data(), "\\StringFileInfo\\040004b0\\FileVersion", reinterpret_cast<LPVOID *>(&file_version), nullptr))
		{
			info.version = file_version;
		}

		if (char *file_description = nullptr;
			VerQueryValueA(version_data.data(), "\\StringFileInfo\\040004b0\\FileDescription", reinterpret_cast<LPVOID *>(&file_description), nullptr))
		{
			info.description = file_description;
		}
	}

	if (const char *const *name = reinterpret_cast<const char *const *>(GetProcAddress(module, "NAME"));
		name != nullptr)
		info.name = *name;
	if (const char *const *description = reinterpret_cast<const char *const *>(GetProcAddress(module, "DESCRIPTION"));
		description != nullptr)
		info.description = *description;

	if (info.version.empty())
		info.version = "1.0.0.0";

	LOG(INFO) << "Registered add-on \"" << info.name << "\" v" << info.version << '.';

	reshade::addon::loaded_info.push_back(std::move(info));

	return true;
}
void ReShadeUnregisterAddon(HMODULE module)
{
	if (module == nullptr || module == g_module_handle)
		return;

	reshade::addon::info *const info = find_addon(module);
	if (info == nullptr)
		return;

#if RESHADE_GUI
	// Unregister all overlays associated with this add-on
	while (!info->overlay_titles.empty())
	{
		ReShadeUnregisterOverlay(info->overlay_titles.back().c_str());
	}
#endif

	// Unregister all event callbacks registered by this add-on
	while (!info->event_callbacks.empty())
	{
		const auto &last_event_callback = info->event_callbacks.back();
		ReShadeUnregisterEvent(static_cast<reshade::addon_event>(last_event_callback.first), last_event_callback.second);
	}

	LOG(INFO) << "Unregistered add-on \"" << info->name << "\".";

	reshade::addon::loaded_info.erase(reshade::addon::loaded_info.begin() + (info - reshade::addon::loaded_info.data()));
}

void ReShadeRegisterEvent(reshade::addon_event ev, void *callback)
{
	if (ev >= reshade::addon_event::max || callback == nullptr)
		return;

	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(callback), &module))
		return;

	reshade::addon::info *const info = find_addon(module);
	if (info != nullptr)
		info->event_callbacks.emplace_back(static_cast<uint32_t>(ev), callback);

	auto &event_list = reshade::addon::event_list[static_cast<uint32_t>(ev)];
	event_list.push_back(callback);

#if RESHADE_VERBOSE_LOG
	LOG(DEBUG) << "Registered event callback " << callback << " for event " << addon_event_to_string(ev) << '.';
#endif
}
void ReShadeUnregisterEvent(reshade::addon_event ev, void *callback)
{
	if (ev >= reshade::addon_event::max || callback == nullptr)
		return;

	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(callback), &module))
		return;

	reshade::addon::info *const info = find_addon(module);
	if (info != nullptr)
		info->event_callbacks.erase(std::remove(info->event_callbacks.begin(), info->event_callbacks.end(), std::make_pair(static_cast<uint32_t>(ev), callback)), info->event_callbacks.end());

	auto &event_list = reshade::addon::event_list[static_cast<uint32_t>(ev)];
	event_list.erase(std::remove(event_list.begin(), event_list.end(), callback), event_list.end());

#if RESHADE_VERBOSE_LOG
	LOG(DEBUG) << "Unregistered event callback " << callback << " for event " << addon_event_to_string(ev) << '.';
#endif
}

#if RESHADE_GUI
void ReShadeRegisterOverlay(const char *title, void(*callback)(reshade::api::effect_runtime *runtime, void *imgui_context))
{
	if (callback == nullptr)
		return;

	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(callback), &module))
		return;

	reshade::addon::info *const info = find_addon(module);
	if (title == nullptr)
	{
		if (info != nullptr)
			info->settings_overlay_callback = callback;
		return;
	}
	else
	{
		if (info != nullptr)
			info->overlay_titles.push_back(title);
	}

	auto &overlay_list = reshade::addon::overlay_list;
	overlay_list.emplace_back(title, callback);

#if RESHADE_VERBOSE_LOG
	LOG(DEBUG) << "Registered overlay callback " << callback << " with title \"" << title << "\".";
#endif
}
void ReShadeUnregisterOverlay(const char *title)
{
	if (title == nullptr)
		return; // Cannot unregister settings overlay

	for (auto &info : reshade::addon::loaded_info)
		info.overlay_titles.erase(std::remove(info.overlay_titles.begin(), info.overlay_titles.end(), title), info.overlay_titles.end());

	auto &overlay_list = reshade::addon::overlay_list;

	// Need to use a functor instead of a lambda here, since lambdas don't compile in functions declared 'extern "C"' on MSVC ...
	struct predicate {
		const char *title;
		bool operator()(const std::pair<std::string, void(*)(reshade::api::effect_runtime *, void *)> &it) const { return it.first == title; }
	};

	const auto callback_it = std::find_if(overlay_list.begin(), overlay_list.end(), predicate { title });
	if (callback_it == overlay_list.end())
		return;

#if RESHADE_VERBOSE_LOG
	LOG(DEBUG) << "Unregistered overlay callback " << callback_it->second << " with title \"" << title << "\".";
#endif

	overlay_list.erase(callback_it);
}
#endif

#endif
