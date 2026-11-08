// Custom provider URL normalization and the cleartext API-key warning policy.
#pragma once

#include <string>
#include <string_view>

namespace llm {

// What the user typed as an OpenAI-compatible server → the base URL to use:
// http:// added when no scheme is given, trailing '/' and a pasted
// "/chat/completions" dropped, "/v1" appended to a bare host. "" when the
// result is no usable http(s) URL.
std::string normalizeOpenAiBaseUrl(std::string_view raw);
// Plain http to a host that is not this machine or the local network
// (loopback, *.localhost, *.local, link-local, RFC 1918, fc00::/7 are
// local): the API key would cross the internet unencrypted.
bool        isCleartextRemote(std::string_view url);

} // namespace llm
