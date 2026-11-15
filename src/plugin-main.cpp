/*
AC Scene Folders
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

#include "scene-tree-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static QPointer<SceneTreeDock> dock;
static bool callbacksRegistered = false;

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
	return "AC Scene Folders";
}

bool obs_module_load(void)
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_WARNING, "no main window, dock not created");
		return true;
	}

	dock = new SceneTreeDock(mainWindow);
	if (!obs_frontend_add_dock_by_id("ac-scene-folders-dock", obs_module_text("DockTitle"), dock)) {
		obs_log(LOG_ERROR, "could not register the dock");
		delete dock;
		return false;
	}

	obs_frontend_add_event_callback(on_event, nullptr);
	obs_frontend_add_save_callback(on_save, nullptr);
	callbacksRegistered = true;

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	remove_callbacks();
	obs_log(LOG_INFO, "plugin unloaded");
}
