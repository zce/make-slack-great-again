#include "app/llm/server_url.h"

#include "base/str.h"
#include "net/net.h"

#include <cstdint>

namespace llm {

std::string normalizeOpenAiBaseUrl(std::string_view raw) {
    std::string s(str::trim(raw));
    if (s.empty())
        return {};
    if (s.find("://") == std::string::npos)
        s.insert(0, "http://");
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    if (str::endsWith(s, "/chat/completions"))
        s.resize(s.size() - 17);
    net::Url url;
    if (!url.parse(s) || url.host.empty() || (url.scheme != "http" && url.scheme != "https"))
        return {};
    if (url.target == "/")
        s += "/v1";
    return s;
}

namespace {

// Dotted-quad IPv4 → its 32 bits; false when `h` is not one.
bool parseIpv4(std::string_view h, uint32_t &out) {
    uint32_t v     = 0;
    int      parts = 0;
    size_t   i     = 0;
    while (parts < 4) {
        uint32_t n      = 0;
        size_t   digits = 0;
        while (i < h.size() && h[i] >= '0' && h[i] <= '9' && digits < 4)
            n = n * 10 + uint32_t(h[i++] - '0'), ++digits;
        if (digits == 0 || n > 255)
            return false;
        v = v << 8 | n;
        if (++parts < 4 && (i >= h.size() || h[i++] != '.'))
            return false;
    }
    out = v;
    return i == h.size();
}

// An IPv6 literal → its eight groups; false when `h` is not one. An
// embedded IPv4 tail ("::ffff:10.0.0.1") fills the last two.
bool parseIpv6(std::string_view h, uint16_t out[8]) {
    uint16_t head[8], tail[8];
    int      nh = 0, nt = 0;
    bool     gap = false;
    if (str::startsWith(h, "::")) {
        gap = true;
        h.remove_prefix(2);
    }
    while (!h.empty()) {
        const size_t           colon = h.find(':');
        const std::string_view g     = h.substr(0, colon);
        uint16_t              *dst   = gap ? tail : head;
        int                   &n     = gap ? nt : nh;
        uint32_t               v4    = 0;
        if (colon == std::string_view::npos && g.find('.') != std::string_view::npos) {
            if (n > 6 || !parseIpv4(g, v4))
                return false;
            dst[n++] = uint16_t(v4 >> 16);
            dst[n++] = uint16_t(v4);
            h        = {};
            break;
        }
        if (g.empty() || g.size() > 4 || n >= 8)
            return false;
        uint16_t v = 0;
        for (char c : g) {
            const int d = str::hexDigit(c);
            if (d < 0)
                return false;
            v = uint16_t(v << 4 | d);
        }
        dst[n++] = v;
        if (colon == std::string_view::npos)
            break;
        h.remove_prefix(colon + 1);
        if (str::startsWith(h, ":")) {
            if (gap)
                return false;
            gap = true;
            h.remove_prefix(1);
        } else if (h.empty()) {
            return false; // a trailing single ':'
        }
    }
    if (gap ? nh + nt > 7 : nh != 8)
        return false;
    for (int i = 0; i < 8; ++i)
        out[i] = 0;
    for (int i = 0; i < nh; ++i)
        out[i] = head[i];
    for (int i = 0; i < nt; ++i)
        out[8 - nt + i] = tail[i];
    return true;
}

bool localIpv4(uint32_t v) {
    return (v >> 24) == 127 || (v >> 16) == 0xA9FE                          // loopback, link-local
           || (v >> 24) == 10 || (v >> 20) == 0xAC1 || (v >> 16) == 0xC0A8; // RFC 1918
}

} // namespace

bool isCleartextRemote(std::string_view urlStr) {
    net::Url url;
    if (!url.parse(urlStr) || url.scheme != "http")
        return false;
    const std::string &host = url.host; // lower case, no brackets
    if (host == "localhost" || str::endsWith(host, ".localhost") || str::endsWith(host, ".local"))
        return false;
    uint32_t v4 = 0;
    if (parseIpv4(host, v4))
        return !localIpv4(v4);
    uint16_t v6[8];
    if (!parseIpv6(host, v6))
        return true; // a DNS name over plain http: assume remote
    bool loopback = v6[7] == 1;
    for (int i = 0; i < 7; ++i)
        loopback &= v6[i] == 0;
    const bool mapped =
        v6[0] == 0 && v6[1] == 0 && v6[2] == 0 && v6[3] == 0 && v6[4] == 0 && v6[5] == 0xFFFF;
    if (mapped)
        return !localIpv4(uint32_t(v6[6]) << 16 | v6[7]);
    return !(loopback || (v6[0] & 0xFFC0) == 0xFE80 || (v6[0] & 0xFE00) == 0xFC00);
}

} // namespace llm
