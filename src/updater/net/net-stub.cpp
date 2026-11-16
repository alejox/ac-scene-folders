// Linux builds don't update themselves (releases ship a .deb there), so the network layer is
// a stub that always fails cleanly.
#include "net/net.hpp"

namespace acsg::net {

const char *const kUserAgent = "Mozilla/5.0 (X11; Linux x86_64)";

namespace {
Response unsupported()
{
	Response res;
	res.error = "network not available on this platform";
	return res;
}
} // namespace

Response get(const std::string &, const Headers &, int, CancelToken *)
{
	return unsupported();
}

Response post_form(const std::string &, const std::string &, const Headers &, int, CancelToken *)
{
	return unsupported();
}

Response post(const std::string &, const std::string &, const std::string &, const Headers &, int, CancelToken *)
{
	return unsupported();
}

std::shared_ptr<WebSocket> connect_websocket(const std::string &, const Headers &, std::string &error)
{
	error = "network not available on this platform";
	return nullptr;
}

} // namespace acsg::net
