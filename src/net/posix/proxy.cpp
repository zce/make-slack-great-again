// Environment / GNOME proxy selection and HTTP CONNECT / SOCKS5 tunneling.
// Linux's lightweight native socket transport does not use GIO or libcurl.
// GSettings is queried once with the standard gsettings CLI, only when no
// proxy environment variable applies; no background service is introduced.
#include "net/posix/proxy.h"

#include "base/str.h"
#include "net/posix/posix.h"

#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string_view>

namespace net::detail {
namespace {

const char *env(const char *lower, const char *upper) {
    const char *value = std::getenv(lower);
    if (!value || !*value)
        value = std::getenv(upper);
    return value && *value ? value : nullptr;
}

std::string_view unquote(std::string_view value) {
    value = str::trim(value);
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
        return value.substr(1, value.size() - 2);
    return value;
}

struct Gnome {
    std::string mode;
    bool same = false;
    std::string httpHost, httpsHost, socksHost;
    int httpPort = 0, httpsPort = 0, socksPort = 0;
    std::string ignore;
};

// Reading once keeps the network workers independent of GIO/DBus and avoids
// starting a command for each request. Like environment changes, GNOME proxy
// changes take effect after restarting MSGA.
const Gnome &gnome() {
    static const Gnome value = [] {
        Gnome g;
        FILE *p = popen("gsettings list-recursively org.gnome.system.proxy 2>/dev/null", "r");
        if (!p)
            return g;
        char line[4096];
        while (std::fgets(line, sizeof line, p)) {
            const std::string_view s = str::trim(line);
            const size_t a = s.find(' '), b = a == s.npos ? s.npos : s.find(' ', a + 1);
            if (b == s.npos)
                continue;
            const std::string_view schema = s.substr(0, a);
            const std::string_view key = s.substr(a + 1, b - a - 1);
            const std::string_view raw = str::trim(s.substr(b + 1));
            const std::string value(unquote(raw));
            if (schema == "org.gnome.system.proxy") {
                if (key == "mode") g.mode = value;
                else if (key == "use-same-proxy") g.same = raw == "true";
                else if (key == "ignore-hosts") g.ignore = value;
            } else {
                std::string *host = nullptr;
                int *port = nullptr;
                if (schema == "org.gnome.system.proxy.http") {
                    host = &g.httpHost; port = &g.httpPort;
                } else if (schema == "org.gnome.system.proxy.https") {
                    host = &g.httpsHost; port = &g.httpsPort;
                } else if (schema == "org.gnome.system.proxy.socks") {
                    host = &g.socksHost; port = &g.socksPort;
                }
                if (!host)
                    continue;
                if (key == "host") *host = value;
                else if (key == "port") *port = std::atoi(value.c_str());
            }
        }
        pclose(p);
        return g;
    }();
    return value;
}

bool cidrMatches(std::string_view host, std::string_view pattern) {
    const size_t slash = pattern.find('/');
    if (slash == pattern.npos)
        return false;
    const std::string address(pattern.substr(0, slash));
    const std::string bits(pattern.substr(slash + 1));
    if (bits.empty() || !std::all_of(bits.begin(), bits.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    const bool v6 = address.find(':') != std::string::npos;
    unsigned char ip[16]{}, network[16]{};
    const int family = v6 ? AF_INET6 : AF_INET;
    const size_t bytes = v6 ? 16 : 4;
    if (inet_pton(family, std::string(host).c_str(), ip) != 1 ||
        inet_pton(family, address.c_str(), network) != 1)
        return false;
    if (bits.size() > 3)
        return false;
    const int prefix = std::atoi(bits.c_str());
    if (prefix < 0 || prefix > int(bytes * 8))
        return false;
    const int whole = prefix / 8, extra = prefix % 8;
    if (std::memcmp(ip, network, whole) != 0)
        return false;
    return !extra || (ip[whole] & (0xff << (8 - extra))) ==
                         (network[whole] & (0xff << (8 - extra)));
}

bool hostMatches(const Url &target, std::string_view item) {
    item = str::trim(item);
    if (item.empty())
        return false;
    if (item == "*")
        return true;
    // GNOME ignore-hosts uses '*.example.com'; no_proxy often uses '.example.com'.
    if (item.starts_with("*.")) item.remove_prefix(1);
    if (item.find('/') != item.npos)
        return cidrMatches(target.host, item);
    std::string rule = str::asciiLower(item);
    if (rule.starts_with("[") && rule.find(']') != rule.npos) {
        const size_t end = rule.find(']');
        if (rule.size() > end + 1 && rule[end + 1] == ':' &&
            std::atoi(rule.c_str() + end + 2) != target.port)
            return false;
        rule = rule.substr(1, end - 1);
    } else if (const size_t colon = rule.rfind(':'); colon != rule.npos &&
               rule.find(':') == colon) {
        const std::string port = rule.substr(colon + 1);
        if (!port.empty() && std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            if (std::atoi(port.c_str()) != target.port)
                return false;
            rule.resize(colon);
        }
    }
    if (rule.starts_with(".")) {
        const std::string_view suffix(rule);
        return target.host == suffix.substr(1) ||
               (target.host.size() > suffix.size() && target.host.ends_with(suffix));
    }
    // Like curl, a bare no_proxy domain also matches its subdomains, but
    // never a lookalike suffix without the dot boundary.
    return target.host == rule ||
           (target.host.size() > rule.size() &&
            target.host[target.host.size() - rule.size() - 1] == '.' &&
            target.host.ends_with(rule));
}

bool bypasses(const Url &target, std::string_view rules) {
    // Accept both comma-separated NO_PROXY values and GVariant's quoted list.
    size_t start = 0;
    while (start < rules.size()) {
        while (start < rules.size() &&
               (rules[start] == ',' || rules[start] == '[' || rules[start] == ']' ||
                rules[start] == '\'' || rules[start] == '"' ||
                std::isspace(static_cast<unsigned char>(rules[start]))))
            ++start;
        size_t end = start;
        while (end < rules.size() &&
               rules[end] != ',' && rules[end] != '\'' && rules[end] != '"' &&
               rules[end] != ']' && !std::isspace(static_cast<unsigned char>(rules[end])))
            ++end;
        if (end > start && hostMatches(target, rules.substr(start, end - start)))
            return true;
        start = end < rules.size() ? end + 1 : end;
    }
    return false;
}

Proxy parseProxy(std::string_view raw) {
    Proxy p;
    const size_t schemeEnd = raw.find("://");
    std::string_view scheme = "http", authority = raw;
    if (schemeEnd != raw.npos) {
        scheme = raw.substr(0, schemeEnd);
        authority = raw.substr(schemeEnd + 3);
    }
    scheme = str::trim(scheme);
    if (scheme == "http")
        p.kind = Proxy::Kind::Http;
    else if (scheme == "socks5" || scheme == "socks5h")
        p.kind = Proxy::Kind::Socks5;
    else {
        p.kind = Proxy::Kind::Invalid;
        p.error = "proxy: unsupported scheme";
        return p;
    }
    // Deliberately do not drop credentials and accidentally connect without
    // authentication. Local Mihomo HTTP/SOCKS5 endpoints need no credentials.
    if (authority.find('@') != authority.npos || authority.find('/') != authority.npos ||
        authority.find('?') != authority.npos || authority.find('#') != authority.npos) {
        p.kind = Proxy::Kind::Invalid;
        p.error = "proxy: credentials or URL path not supported";
        return p;
    }
    Url u;
    if (!u.parse(std::string("http://") + std::string(authority)) ||
        u.host.empty() || (u.port == 80 && authority.find(':') == authority.npos)) {
        p.kind = Proxy::Kind::Invalid;
        p.error = "proxy: invalid host or port";
        return p;
    }
    for (unsigned char c : u.host) {
        if (c <= 0x20 || c == 0x7f) {
            p.kind = Proxy::Kind::Invalid;
            p.error = "proxy: invalid hostname";
            return p;
        }
    }
    p.host = u.host;
    p.port = u.port;
    return p;
}

Proxy gnomeProxy(const Url &target) {
    const Gnome &g = gnome();
    if (g.mode != "manual" || bypasses(target, g.ignore))
        return {};
    const bool secure = target.secure();
    std::string host;
    int port = 0;
    if (secure && !g.same && !g.httpsHost.empty() && g.httpsPort > 0) {
        host = g.httpsHost; port = g.httpsPort;
    } else if (!g.httpHost.empty() && g.httpPort > 0) {
        host = g.httpHost; port = g.httpPort;
    }
    bool socks = false;
    if (host.empty() && !g.socksHost.empty() && g.socksPort > 0) {
        host = g.socksHost;
        port = g.socksPort;
        socks = true;
    }
    if (host.empty())
        return {};
    Proxy p = parseProxy(std::string(socks ? "socks5://" : "http://") + host + ":" +
                         std::to_string(port));
    return p;
}

bool sendAll(int fd, std::string_view data, const Waiter &w, std::string *error) {
    while (!data.empty()) {
        if ((w.cancel && w.cancel->load()) || (w.deadline && nowMs() >= w.deadline)) {
            *error = w.cancel && w.cancel->load() ? "cancelled" : "timeout";
            return false;
        }
        int err = 0;
        const long n = sockSend(fd, data.data(), data.size(), &err);
        if (n > 0) {
            data.remove_prefix(size_t(n));
        } else if (n == Stream::Fail || n == 0) {
            *error = n == 0 ? "proxy: closed" : "proxy: send failed";
            return false;
        } else {
            const Wait wr = waitFd(fd, POLLOUT, w);
            if (wr != Wait::Ready) {
                *error = wr == Wait::Cancelled ? "cancelled" : "timeout";
                return false;
            }
        }
    }
    return true;
}

bool receive(int fd, void *out, size_t length, const Waiter &w, std::string *error) {
    char *dst = static_cast<char *>(out);
    while (length) {
        if ((w.cancel && w.cancel->load()) || (w.deadline && nowMs() >= w.deadline)) {
            *error = w.cancel && w.cancel->load() ? "cancelled" : "timeout";
            return false;
        }
        int err = 0;
        const long n = sockRecv(fd, dst, length, &err);
        if (n > 0) {
            dst += n;
            length -= size_t(n);
        } else if (n == Stream::Fail || n == 0) {
            *error = n == 0 ? "proxy: closed" : "proxy: receive failed";
            return false;
        } else {
            const Wait wr = waitFd(fd, POLLIN, w);
            if (wr != Wait::Ready) {
                *error = wr == Wait::Cancelled ? "cancelled" : "timeout";
                return false;
            }
        }
    }
    return true;
}

std::string endpoint(const Url &url) {
    const std::string host = url.host.find(':') != std::string::npos
                                 ? "[" + url.host + "]" : url.host;
    return host + ":" + std::to_string(url.port);
}

bool httpTunnel(int fd, const Url &url, const Waiter &w, std::string *error) {
    const std::string dest = endpoint(url);
    const std::string req = "CONNECT " + dest + " HTTP/1.1\r\nHost: " + dest +
                            "\r\nProxy-Connection: keep-alive\r\n\r\n";
    if (!sendAll(fd, req, w, error))
        return false;
    std::string header;
    while (header.size() < 8192 && !header.ends_with("\r\n\r\n")) {
        char c;
        if (!receive(fd, &c, 1, w, error))
            return false;
        header.push_back(c);
    }
    if (!header.ends_with("\r\n\r\n")) {
        *error = "proxy: response headers too large";
        return false;
    }
    if (!header.starts_with("HTTP/1.") || header.size() < 12 ||
        header.substr(9, 3) != "200") {
        *error = "proxy: CONNECT rejected";
        if (header.size() >= 12)
            *error += " (" + header.substr(9, 3) + ")";
        return false;
    }
    return true;
}

bool socksTunnel(int fd, const Url &url, const Waiter &w, std::string *error) {
    // SOCKS5 with no authentication. Remote name resolution is intentional:
    // it avoids leaking proxied hostnames to the local DNS resolver.
    if (!sendAll(fd, std::string("\x05\x01\x00", 3), w, error))
        return false;
    unsigned char reply[4]{};
    if (!receive(fd, reply, 2, w, error))
        return false;
    if (reply[0] != 5 || reply[1] != 0) {
        *error = "proxy: SOCKS5 authentication required or rejected";
        return false;
    }
    std::string req("\x05\x01\x00", 3);
    unsigned char addr[16]{};
    if (inet_pton(AF_INET, url.host.c_str(), addr) == 1) {
        req += char(1);
        req.append(reinterpret_cast<const char *>(addr), 4);
    } else if (inet_pton(AF_INET6, url.host.c_str(), addr) == 1) {
        req += char(4);
        req.append(reinterpret_cast<const char *>(addr), 16);
    } else {
        if (url.host.size() > 255) {
            *error = "proxy: SOCKS5 hostname too long";
            return false;
        }
        req += char(3);
        req += char(url.host.size());
        req += url.host;
    }
    req += char(url.port >> 8);
    req += char(url.port & 0xff);
    if (!sendAll(fd, req, w, error) || !receive(fd, reply, 4, w, error))
        return false;
    if (reply[0] != 5 || reply[1] != 0) {
        *error = "proxy: SOCKS5 connect rejected";
        return false;
    }
    size_t len = reply[3] == 1 ? 4 : reply[3] == 4 ? 16 : 0;
    if (reply[3] == 3) {
        unsigned char n;
        if (!receive(fd, &n, 1, w, error)) return false;
        len = n;
    } else if (!len) {
        *error = "proxy: SOCKS5 invalid response";
        return false;
    }
    char rest[257];
    return receive(fd, rest, len + 2, w, error);
}

} // namespace

std::string Proxy::key() const {
    if (kind == Kind::Direct) return "direct";
    return std::to_string(int(kind)) + "://" + host + ":" + std::to_string(port);
}

Proxy proxyFor(const Url &target) {
    const char *rules = env("no_proxy", "NO_PROXY");
    if (rules && bypasses(target, rules))
        return {};
    const bool secure = target.secure();
    const char *value = secure ? env("https_proxy", "HTTPS_PROXY")
                               : env("http_proxy", "HTTP_PROXY");
    if (!value)
        value = env("all_proxy", "ALL_PROXY");
    if (value)
        return parseProxy(value);
    return gnomeProxy(target);
}

bool proxyTunnel(
    int fd, const Url &target, const Proxy &proxy, const Waiter &wait, std::string *error
) {
    if (proxy.kind == Proxy::Kind::Http)
        return httpTunnel(fd, target, wait, error);
    if (proxy.kind == Proxy::Kind::Socks5)
        return socksTunnel(fd, target, wait, error);
    *error = proxy.error.empty() ? "proxy: invalid configuration" : proxy.error;
    return false;
}

} // namespace net::detail
