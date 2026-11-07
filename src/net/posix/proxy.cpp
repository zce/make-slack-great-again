// Environment / GNOME proxy selection and HTTP CONNECT / SOCKS5 tunneling.
// Linux's lightweight native socket transport does not use GIO or libcurl.
// GSettings is queried once with the standard gsettings CLI, only when no
// proxy environment variable applies; no background service is introduced.
#include "net/posix/proxy.h"

#include "base/process.h"
#include "base/str.h"
#include "net/posix/posix.h"

#include <arpa/inet.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string_view>
#include <utility>

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

Proxy invalid(std::string message) {
    Proxy p;
    p.kind = Proxy::Kind::Invalid;
    p.error = std::move(message);
    return p;
}

bool isGnomeSession() {
    for (const char *name : {"XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP",
                             "DESKTOP_SESSION", "GDMSESSION"}) {
        const std::string value = str::asciiLower(base::env(name));
        size_t start = 0;
        while (start < value.size()) {
            const size_t end = value.find(':', start);
            const std::string_view item(
                value.data() + start,
                (end == std::string::npos ? value.size() : end) - start
            );
            if (item == "gnome" || item.starts_with("gnome-"))
                return true;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    return false;
}

// Read GNOME settings once without a shell or an unbounded popen(). Environment
// variables remain dynamic per request; changing GSettings requires a restart.
const GnomeSettings &gnome() {
    static const GnomeSettings value = [] {
        GnomeSettings settings;
        if (base::testProcess() || !isGnomeSession()) {
            settings.mode = "none";
            return settings;
        }
        const std::string executable = base::findExecutable("gsettings");
        if (executable.empty())
            return parseGnomeSettings({}, false, false, -1);
        base::RunOptions options;
        options.timeoutMs = 1500;
        options.mergeStderr = false;
        const base::RunResult result = base::run(
            executable, {"list-recursively", "org.gnome.system.proxy"}, options
        );
        return parseGnomeSettings(result.output, result.started, result.timedOut, result.code);
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
    raw = str::trim(raw);
    const size_t schemeEnd = raw.find("://");
    std::string scheme = "http";
    std::string authority(raw);
    if (schemeEnd != raw.npos) {
        scheme = str::asciiLower(raw.substr(0, schemeEnd));
        authority = std::string(raw.substr(schemeEnd + 3));
    }
    if (scheme != "http" && scheme != "socks5" && scheme != "socks5h")
        return invalid("proxy: unsupported scheme");
    const Proxy::Kind kind =
        scheme == "http" ? Proxy::Kind::Http : Proxy::Kind::Socks5;

    // A trailing slash is the ordinary empty path of a proxy URI.
    if (authority.ends_with('/'))
        authority.pop_back();
    if (authority.empty() || authority.find('@') != std::string::npos ||
        authority.find('/') != std::string::npos || authority.find('?') != std::string::npos ||
        authority.find('#') != std::string::npos)
        return invalid("proxy: credentials or URL path not supported");

    bool explicitPort = false;
    if (authority.front() == '[') {
        const size_t close = authority.find(']');
        if (close == std::string::npos)
            return invalid("proxy: invalid host or port");
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':')
                return invalid("proxy: invalid host or port");
            explicitPort = true;
        }
    } else {
        const size_t first = authority.find(':'), last = authority.rfind(':');
        if (first != std::string::npos && first != last)
            return invalid("proxy: IPv6 proxy addresses must be bracketed");
        explicitPort = first != std::string::npos;
    }
    if (explicitPort && authority.back() == ':')
        return invalid("proxy: invalid host or port");

    Url url;
    if (!url.parse("http://" + authority) || url.host.empty())
        return invalid("proxy: invalid host or port");
    for (unsigned char c : url.host)
        if (c <= 0x20 || c == 0x7f)
            return invalid("proxy: invalid hostname");

    Proxy proxy;
    proxy.kind = kind;
    proxy.host = url.host;
    proxy.port = explicitPort ? url.port : (kind == Proxy::Kind::Http ? 80 : 1080);
    if (proxy.port < 1 || proxy.port > 65535)
        return invalid("proxy: invalid host or port");
    return proxy;
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

GnomeSettings parseGnomeSettings(
    std::string_view output, bool started, bool timedOut, int exitCode
) {
    GnomeSettings settings;
    if (!started) {
        settings.error = "proxy: GNOME gsettings is unavailable";
        return settings;
    }
    if (timedOut) {
        settings.error = "proxy: GNOME gsettings query timed out";
        return settings;
    }
    if (exitCode != 0) {
        settings.error = "proxy: GNOME gsettings query failed (exit " +
                         std::to_string(exitCode) + ")";
        return settings;
    }

    auto parsePort = [&](std::string_view raw, int *port) {
        raw = unquote(raw);
        int value = 0;
        const auto [end, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
        if (raw.empty() || ec != std::errc{} || end != raw.data() + raw.size() ||
            value < 0 || value > 65535) {
            settings.error = "proxy: invalid GNOME proxy port";
            return false;
        }
        *port = value;
        return true;
    };

    size_t start = 0;
    while (start < output.size()) {
        const size_t end = output.find('\n', start);
        const std::string_view line = str::trim(output.substr(
            start, (end == std::string_view::npos ? output.size() : end) - start
        ));
        if (!line.empty()) {
            const size_t a = line.find(' ');
            const size_t b = a == line.npos ? line.npos : line.find(' ', a + 1);
            if (b != line.npos) {
                const std::string_view schema = line.substr(0, a);
                const std::string_view key = line.substr(a + 1, b - a - 1);
                const std::string_view raw = str::trim(line.substr(b + 1));
                const std::string value(unquote(raw));
                if (schema == "org.gnome.system.proxy") {
                    if (key == "mode")
                        settings.mode = value;
                    else if (key == "use-same-proxy") {
                        if (raw == "true")
                            settings.same = true;
                        else if (raw == "false")
                            settings.same = false;
                        else {
                            settings.error = "proxy: invalid GNOME use-same-proxy setting";
                            return settings;
                        }
                    } else if (key == "ignore-hosts") {
                        settings.ignore = value;
                    }
                } else {
                    std::string *host = nullptr;
                    int *port = nullptr;
                    if (schema == "org.gnome.system.proxy.http") {
                        host = &settings.httpHost;
                        port = &settings.httpPort;
                    } else if (schema == "org.gnome.system.proxy.https") {
                        host = &settings.httpsHost;
                        port = &settings.httpsPort;
                    } else if (schema == "org.gnome.system.proxy.socks") {
                        host = &settings.socksHost;
                        port = &settings.socksPort;
                    }
                    if (host) {
                        if (key == "host")
                            *host = value;
                        else if (key == "port" && !parsePort(raw, port))
                            return settings;
                    }
                }
            }
        }
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    if (settings.mode.empty())
        settings.error = "proxy: GNOME proxy mode unavailable";
    return settings;
}

Proxy gnomeProxyFor(const Url &target, const GnomeSettings &settings) {
    if (!settings.error.empty())
        return invalid(settings.error);
    if (settings.mode == "none")
        return {};
    if (settings.mode == "auto")
        return invalid("proxy: GNOME automatic proxy mode (PAC/WPAD) is not supported");
    if (settings.mode != "manual")
        return invalid("proxy: unsupported GNOME proxy mode");
    if (bypasses(target, settings.ignore))
        return {};

    std::string host;
    int port = 0;
    bool socks = false;
    if (target.secure() && !settings.same && !settings.httpsHost.empty()) {
        host = settings.httpsHost;
        port = settings.httpsPort;
    } else if (!settings.httpHost.empty()) {
        host = settings.httpHost;
        port = settings.httpPort;
    }
    if (host.empty() && !settings.socksHost.empty()) {
        host = settings.socksHost;
        port = settings.socksPort;
        socks = true;
    }
    if (host.empty())
        return invalid("proxy: GNOME manual mode has no endpoint for this protocol");
    if (port < 1 || port > 65535)
        return invalid("proxy: invalid GNOME proxy port");
    if (host.find(':') != std::string::npos && !host.starts_with("["))
        host = "[" + host + "]";
    return parseProxy(std::string(socks ? "socks5h://" : "http://") + host + ":" +
                      std::to_string(port));
}

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
    return gnomeProxyFor(target, gnome());
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
