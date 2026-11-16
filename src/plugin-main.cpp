/*
AC Scene Groups
Copyright (C) 2026 alejoxcode

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>

#include "scene-groups-dock.hpp"
#include "groups-api.hpp"
#include "ui/update_controller.hpp"

#include <filesystem>
#include <memory>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static QPointer<SceneGroupsDock> dock;
static bool callbacksRegistered = false;

/* Lets OBS finish starting, and the user see the main window, before the network check. */
static const int UPDATE_CHECK_DELAY_MS = 8000;
static std::unique_ptr<acsg::ui::UpdateController> updates;

/* In UTF-8, as libobs returns it. Convert with u8path: on Windows a path built from a plain
 * string assumes the system code page instead. */
static std::string module_config_path(const char *file)
{
	char *path = obs_module_config_path(file);
	const std::string result = path != nullptr ? path : "";
	bfree(path);
	return result;
}

static std::unique_ptr<acsg::ui::UpdateController> make_update_controller()
{
	acsg::update::UpdateServiceConfig config;
	config.current_version = PLUGIN_VERSION;
	config.staging_dir = std::filesystem::u8path(module_config_path("update"));
	config.result_file = std::filesystem::u8path(module_config_path("update-result.txt"));
	config.platform = acsg::update::current_update_platform();
	const char *binary = obs_get_module_binary_path(obs_current_module());
	if (binary != nullptr && config.platform) {
		if (const auto root = acsg::update::install_root_from_binary(binary, *config.platform))
			config.install_root = std::filesystem::u8path(*root);
	}
	if (!config.install_root)
		obs_log(LOG_INFO, "updates can only be installed manually from %s", binary ? binary : "(unknown)");
	return std::make_unique<acsg::ui::UpdateController>(std::move(config),
							    std::filesystem::u8path(module_config_path("update.json")));
}

static void on_save(obs_data_t *save_data, bool saving, void *)
{
	if (!dock)
		return;
	if (saving)
		dock->saveTo(save_data);
	else
		dock->loadFrom(save_data);
}

static void on_event(enum obs_frontend_event event, void *);

static void remove_callbacks()
{
	if (!callbacksRegistered)
		return;
	obs_frontend_remove_event_callback(on_event, nullptr);
	obs_frontend_remove_save_callback(on_save, nullptr);
	callbacksRegistered = false;
}

static void on_event(enum obs_frontend_event event, void *)
{
	if (dock)
		dock->handleFrontendEvent(event);

	if (event == OBS_FRONTEND_EVENT_EXIT) {
		/* The staged update installs from here rather than from obs_module_unload: Qt and the
		 * main loop still exist, so the installer can be launched, and OBS is certain to be
		 * closing. The installer only starts once this process is gone. */
		if (updates) {
			updates->launch_staged_install();
			updates.reset();
		}
		if (dock)
			dock->shutdown();
		remove_callbacks();
	}
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "AC Scene Groups";
}

bool obs_module_load(void)
{
	groups_api_register();

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_WARNING, "no main window, dock not created");
		return true;
	}

	dock = new SceneGroupsDock(mainWindow);
	if (!obs_frontend_add_dock_by_id("ac-scene-folders-dock", obs_module_text("DockTitle"), dock)) {
		obs_log(LOG_ERROR, "could not register the dock");
		delete dock;
		return false;
	}

	obs_frontend_add_event_callback(on_event, nullptr);
	obs_frontend_add_save_callback(on_save, nullptr);
	callbacksRegistered = true;

	updates = make_update_controller();
	updates->start(UPDATE_CHECK_DELAY_MS);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	remove_callbacks();
	updates.reset();
	obs_log(LOG_INFO, "plugin unloaded");
}
