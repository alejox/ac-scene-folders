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

/* Shares the scene groups with other plugins (AC Global Overlay) through OBS:
 *   proc   "void ac_scene_groups_get(out string json)" on obs_get_proc_handler()
 *   signal "void ac_scene_groups_changed()"           on obs_get_signal_handler()
 * JSON: {"version":1,"groups":[{"id","name","color","scenes":[scene uuids]}]}
 * "scenes" lists every scene of the group, including those inside folders. */

#pragma once

#include <string>

void groups_api_register();
/* Stores the current groups and emits the signal when they changed. UI thread. */
void groups_api_publish(const std::string &json);
