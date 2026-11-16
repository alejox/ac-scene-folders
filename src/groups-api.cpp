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

#include "groups-api.hpp"

#include <obs.h>

#include <mutex>

static std::mutex cacheMutex;
static std::string cache = "{\"version\":1,\"groups\":[]}";

static void get_groups(void *, calldata_t *cd)
{
	std::lock_guard<std::mutex> lock(cacheMutex);
	calldata_set_string(cd, "json", cache.c_str());
}

void groups_api_register()
{
	proc_handler_add(obs_get_proc_handler(), "void ac_scene_groups_get(out string json)", get_groups, nullptr);
	signal_handler_add(obs_get_signal_handler(), "void ac_scene_groups_changed()");
}

void groups_api_publish(const std::string &json)
{
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		if (json == cache)
			return;
		cache = json;
	}
	calldata_t cd = {};
	signal_handler_signal(obs_get_signal_handler(), "ac_scene_groups_changed", &cd);
	calldata_free(&cd);
}
