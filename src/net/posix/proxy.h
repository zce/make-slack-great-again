// Linux proxy selection shared by HTTP requests and WebSocket connections.
// No new library dependency: proxy environment variables first, GNOME
// GSettings (when installed) second. Only the transport consumes the result.
#pragma once

#include "net/net.h"

#include <cstdint>
#include <string>

namespace net::detail {

struct Waiter;

struct Proxy {
    enum class Kind : uint8_t { Direct, Http, Socks5, Invalid };
    Kind kind = Kind::Direct;
    std::string host;
    int port = 0;
    std::string error;

    // Included in the keep-alive pool key: a connection must never be reused
    // after the effective proxy changes.
    std::string key() const;
};

Proxy proxyFor(const Url &target);

// Once TCP is connected to the proxy, establish a transparent tunnel to the
// original destination. The caller continues to do TLS and hostname checks
// against the original target (never against the proxy).
bool proxyTunnel(
    int fd, const Url &target, const Proxy &proxy, const Waiter &wait, std::string *error
);

} // namespace net::detail
