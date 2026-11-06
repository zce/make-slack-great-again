// Wire codecs for the two chat formats msga speaks — pure
// functions, no network, so request shaping and response parsing are tested
// against captured payloads.
//
//   OpenAiChat         POST <base>/chat/completions, GET <base>/models,
//                      "Authorization: Bearer". OpenAI itself and every
//                      OpenAI-compatible server (vLLM, Ollama, LM Studio,
//                      LiteLLM, OpenRouter, …); <base> ends in "/v1" by
//                      convention (OpenRouter: "/api/v1").
//   AnthropicMessages  POST <base>/v1/messages, GET <base>/v1/models,
//                      "x-api-key" + "anthropic-version". Anthropic's native
//                      API — its OpenAI-compatibility layer is documented as
//                      not production-ready.
//
#pragma once

#include "app/llm/types.h"
#include "net/net.h"

#include <string>
#include <string_view>
#include <vector>

namespace llm {

enum class Format : uint8_t { OpenAiChat, AnthropicMessages };

struct Endpoint {
    Format      format = Format::OpenAiChat;
    std::string baseUrl;
    std::string apiKey; // "" → no auth header (local servers without a key)
    // OpenAI proper wants max_completion_tokens (its reasoning models reject
    // max_tokens); compat servers all take max_tokens. Never both.
    bool        maxCompletionTokens = false;
    // Sent as reasoning_effort when set (the OpenAI preset only: compat
    // servers may reject unknown fields). With reasoning on, temperature is
    // dropped — OpenAI's reasoning models reject it.
    std::string reasoningEffort;
};

// Slow local models: a 512-token summary on a CPU-hosted 7B takes a minute.
constexpr int kTimeoutMs = 120000;

net::Request buildChat(const Endpoint &ep, const Request &req);
net::Request buildListModels(const Endpoint &ep);

// ── Custom servers ──────────────────────────────────────────────────────────

// What the user typed as an OpenAI-compatible server → the base URL to use:
// http:// added when no scheme is given, trailing '/' and a pasted
// "/chat/completions" dropped, "/v1" appended to a bare host. "" when the
// result is no usable http(s) URL.
std::string normalizeOpenAiBaseUrl(std::string_view raw);
// Plain http to a host that is not this machine or the local network
// (loopback, *.localhost, *.local, link-local, RFC 1918, fc00::/7 are
// local): the API key would cross the internet unencrypted.
bool        isCleartextRemote(std::string_view url);
// A sentence for net's "no HTTP answer" reasons ("dns", "timeout: …");
// "network error" for anything else.
std::string transportFailure(std::string_view netError);

// A finished net::Response (status 0: no HTTP answer, see its error).
ChatResult   parseChat(Format format, const net::Response &r);
ModelsResult parseModels(const net::Response &r);

} // namespace llm
