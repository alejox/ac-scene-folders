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

/* Minimal HTTPS GET with the system network stack (NSURLSession on macOS,
 * WinHTTP on Windows). OBS does not ship Qt's TLS plugins, so QtNetwork can't
 * do HTTPS inside OBS. Blocking: call it only from a worker thread. */

#pragma once

#include <string>

namespace acsg {

struct HttpResponse {
	int status = 0; /* 0 = no response, see error */
	std::string body;
	std::string error;
};

HttpResponse https_get(const std::string &url, const std::string &userAgent, int timeoutMs);

} // namespace acsg
