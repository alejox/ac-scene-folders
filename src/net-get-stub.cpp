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

#include "net-get.hpp"

namespace acsg {

/* Linux builds don't check for updates (no system HTTPS API without extra dependencies). */
HttpResponse https_get(const std::string &, const std::string &, int)
{
	HttpResponse result;
	result.error = "update check not available on this platform";
	return result;
}

} // namespace acsg
