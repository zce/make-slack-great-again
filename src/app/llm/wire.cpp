#include "app/llm/wire.h"

#include "base/i18n.h"
#include "base/json.h"
#include "base/str.h"
#include "base/utf8.h"

#include <algorithm>

namespace llm {

using i18n::arg;
using i18n::tr;

namespace {

constexpr const char *kAnthropicVersion = "2023-06-01";

std::string joinUrl(std::string_view base, std::string_view path) {
    while (!base.empty() && base.back() == '/')
        base.remove_suffix(1);
    return str::concat({base, path});
}

void writeMessages(json::Writer &w, const Request &req, bool systemAsMessage) {
    w.key("messages").beginArray();
    if (systemAsMessage && !req.system.empty())
        w.beginObject().key("role").value("system").key("content").value(req.system).endObject();
    for (const Message &m : req.messages)
        w.beginObject()
            .key("role")
            .value(m.role == Message::Role::User ? "user" : "assistant")
            .key("content")
            .value(m.text)
            .endObject();
    w.endArray();
}

void auth(net::Request &r, const Endpoint &ep) {
    if (ep.format == Format::AnthropicMessages) {
        r.headers.push_back({"anthropic-version", kAnthropicVersion});
        r.headers.push_back({"x-api-key", ep.apiKey});
    } else if (!ep.apiKey.empty()) {
        r.headers.push_back({"Authorization", "Bearer " + ep.apiKey});
    }
}

// Text of a `content` that is a string or an array of {type:"text", text}
// parts (some compat servers send the latter).
std::string contentText(json::Value content) {
    if (content.isString())
        return std::string(content.str());
    std::string out;
    for (json::Value part : content)
        if (part["type"].str() == "text" || part.has("text"))
            out += part["text"].str();
    return out;
}

// The error a JSON body carries, in the shapes seen in the wild:
//   {"error":{"message":…}}          OpenAI, Anthropic, Ollama, newer vLLM
//   {"error":"…"}                    some gateways
//   {"object":"error","message":…}   older vLLM
//   {"message":…} on a 4xx/5xx       generic
std::string jsonError(json::Value o, int status) {
    const json::Value err = o["error"];
    if (err.isObject()) {
        const std::string_view m = err["message"].str();
        return m.empty() ? std::string(tr("unknown error")) : std::string(m);
    }
    if (err.isString())
        return std::string(err.str());
    if (o["object"].str() == "error" || status >= 400)
        return std::string(o["message"].str());
    return {};
}

std::string transportFailure(std::string_view netError);

std::string httpFailure(const net::Response &r) {
    if (r.status == 0)
        return transportFailure(r.error);
    if (r.status == 404)
        return tr(
            "No chat endpoint at this URL (HTTP 404) \xE2\x80\x94 most servers expect it to "
            "end in /v1"
        );
    const std::string snippet = utf8::ellipsize(str::simplified(r.body), 200);
    const std::string status  = str::number(r.status);
    return snippet.empty() ? arg(tr("HTTP %1"), status) : arg(tr("HTTP %1: %2"), status, snippet);
}

// Transport, HTTP and JSON-carried errors; true with `doc` parsed otherwise.
bool preflight(
    const net::Response &r, json::Document &doc, std::string &error
) {
    if (r.status == 0) {
        error = httpFailure(r);
        return false;
    }
    if (!doc.parse(std::string_view(r.body), nullptr) || !doc.root().isObject()) {
        error = r.status >= 400 ? httpFailure(r)
                                : std::string(tr("Unexpected response from server (not JSON)"));
        return false;
    }
    error = jsonError(doc.root(), r.status);
    if (!error.empty())
        return false;
    if (r.status >= 400) {
        error = httpFailure(r);
        return false;
    }
    return true;
}

} // namespace

net::Request buildChat(const Endpoint &ep, const Request &req) {
    net::Request out;
    out.method    = "POST";
    out.timeoutMs = kTimeoutMs;
    out.headers.push_back({"Content-Type", "application/json"});
    auth(out, ep);
    json::Writer w;
    w.beginObject().key("model").value(req.model);
    writeMessages(w, req, ep.format == Format::OpenAiChat);
    if (ep.format == Format::AnthropicMessages) {
        out.url = joinUrl(ep.baseUrl, "/v1/messages");
        w.key("max_tokens").value(req.maxTokens);
        if (!req.system.empty())
            w.key("system").value(req.system);
        if (req.temperature)
            w.key("temperature").value(std::clamp(*req.temperature, 0.0, 1.0));
    } else {
        out.url = joinUrl(ep.baseUrl, "/chat/completions");
        w.key(ep.maxCompletionTokens ? "max_completion_tokens" : "max_tokens").value(req.maxTokens);
        const bool reasoning = !ep.reasoningEffort.empty() && ep.reasoningEffort != "none";
        if (!ep.reasoningEffort.empty())
            w.key("reasoning_effort").value(ep.reasoningEffort);
        if (req.temperature && !reasoning)
            w.key("temperature").value(*req.temperature);
    }
    w.endObject();
    out.body = w.take();
    return out;
}

net::Request buildListModels(const Endpoint &ep) {
    net::Request out;
    out.timeoutMs = kTimeoutMs;
    out.url =
        joinUrl(ep.baseUrl, ep.format == Format::AnthropicMessages ? "/v1/models" : "/models");
    auth(out, ep);
    return out;
}

ChatResult parseChat(Format format, const net::Response &r) {
    ChatResult     out;
    json::Document doc;
    if (!preflight(r, doc, out.error))
        return out;
    const json::Value o = doc.root();
    out.response.model  = std::string(o["model"].str());
    if (format == Format::AnthropicMessages) {
        out.response.stopReason = std::string(o["stop_reason"].str());
        // A safety refusal can come with an empty content array: an error,
        // not an empty completion.
        if (out.response.stopReason == "refusal") {
            out.error = "refusal";
            return out;
        }
        for (json::Value block : o["content"])
            if (block["type"].str() == "text")
                out.response.text += block["text"].str();
    } else {
        const json::Value choices = o["choices"];
        if (choices.size() == 0) {
            out.error = tr("Unexpected response from server (no choices)");
            return out;
        }
        out.response.stopReason = std::string(choices[0]["finish_reason"].str());
        out.response.text       = contentText(choices[0]["message"]["content"]);
    }
    out.ok = true;
    return out;
}

ModelsResult parseModels(const net::Response &r) {
    ModelsResult   out;
    json::Document doc;
    if (!preflight(r, doc, out.error))
        return out;
    // OpenAI and Anthropic agree on {"data":[{"id":…},…]}.
    for (json::Value m : doc.root()["data"])
        if (!m["id"].str().empty())
            out.models.emplace_back(m["id"].str());
    out.ok = true;
    return out;
}

namespace {

// A sentence for net's "no HTTP answer" reasons; unknown reasons stay generic.
std::string transportFailure(std::string_view netError) {
    const std::string_view head = netError.substr(0, netError.find(':'));
    if (head == "dns")
        return tr("Server not found");
    if (head == "connect")
        return tr("Couldn't connect to the server");
    if (head == "tls")
        return tr("The secure connection to the server failed");
    if (head == "timeout")
        return tr("The server didn't answer in time");
    if (head == "protocol")
        return tr("The server's answer couldn't be read");
    if (head == "too_many_redirects")
        return tr("Too many redirects");
    if (head == "url")
        return tr("Invalid server URL");
    return tr("network error");
}

} // namespace

} // namespace llm
