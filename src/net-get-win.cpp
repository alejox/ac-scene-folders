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

#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace acsg {

namespace {

std::wstring widen(const std::string &text)
{
	if (text.empty())
		return std::wstring();
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
	std::wstring out(size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), size);
	return out;
}

struct Handle {
	HINTERNET h = nullptr;
	explicit Handle(HINTERNET handle) : h(handle) {}
	~Handle()
	{
		if (h)
			WinHttpCloseHandle(h);
	}
	Handle(const Handle &) = delete;
	Handle &operator=(const Handle &) = delete;
	operator HINTERNET() const { return h; }
};

std::string failure(const char *step)
{
	return std::string(step) + " failed (" + std::to_string(GetLastError()) + ")";
}

} // namespace

HttpResponse https_get(const std::string &url, const std::string &userAgent, int timeoutMs)
{
	HttpResponse result;

	std::wstring wideUrl = widen(url);
	URL_COMPONENTS parts = {};
	parts.dwStructSize = sizeof(parts);
	wchar_t host[256] = {};
	wchar_t path[2048] = {};
	parts.lpszHostName = host;
	parts.dwHostNameLength = 256;
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = 2048;
	if (!WinHttpCrackUrl(wideUrl.c_str(), (DWORD)wideUrl.size(), 0, &parts)) {
		result.error = failure("WinHttpCrackUrl");
		return result;
	}

	Handle session(WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
				   WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session.h)
		session.h = WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session.h) {
		result.error = failure("WinHttpOpen");
		return result;
	}
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
	WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
	WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

	Handle connection(WinHttpConnect(session, host, parts.nPort, 0));
	if (!connection.h) {
		result.error = failure("WinHttpConnect");
		return result;
	}

	const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
	Handle request(WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
					  WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
	if (!request.h) {
		result.error = failure("WinHttpOpenRequest");
		return result;
	}

	const wchar_t *headers = L"Accept: application/vnd.github+json\r\n";
	if (!WinHttpSendRequest(request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(request, nullptr)) {
		result.error = failure("WinHttpSendRequest");
		return result;
	}

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
	result.status = (int)status;

	std::vector<char> buffer(16384);
	for (;;) {
		DWORD read = 0;
		if (!WinHttpReadData(request, buffer.data(), (DWORD)buffer.size(), &read)) {
			result.error = failure("WinHttpReadData");
			break;
		}
		if (read == 0)
			break;
		result.body.append(buffer.data(), read);
		if (result.body.size() > 4 * 1024 * 1024) {
			result.error = "response too large";
			break;
		}
	}
	return result;
}

} // namespace acsg
