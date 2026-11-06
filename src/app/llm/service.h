// The app's LLM facade: the configured providers, the
// one requests go to, and the calls themselves over net::Client.
//
//   service.chat(req, [](llm::ChatResult r) { … r.ok ? r.response.text : r.error … });
//   //
// The shell owns the configuration (Settings → AI assistance, persisted in
// shell::Settings) and pushes it in with setProviders / setLanguage after
// every change; callers only ask available(), active() and chat().
//
// Threading: UI thread only. Every `done` runs later on the UI thread —
// never inside the call that started it, also when it fails at once (no
// provider) — and never after the Service is destroyed.
#pragma once

#include "app/llm/provider.h"
#include "base/observers.h"
#include "net/net.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace plat {
class App;
}

namespace llm {


class Service {
public:
    explicit Service(plat::App &app);
    ~Service();
    Service(const Service &)            = delete;
    Service &operator=(const Service &) = delete;

    // Presets first, then custom servers; defaultId = the user's pick.
    void setProviders(std::vector<Provider> providers, std::string defaultId);
    const std::vector<Provider> &providers() const { return _providers; }
    // The language AI features address the user in (ISO 639-1, "en").
    void                         setLanguage(std::string code) { _language = std::move(code); }
    const std::string           &language() const { return _language; }

    // What chat() routes to: the default while connected, else the first
    // connected provider, else null.
    const Provider *active() const;
    bool            available() const { return active() != nullptr; }

    using ChatDone       = std::function<void(ChatResult)>;
    using ModelsDone     = std::function<void(ModelsResult)>;
    // A completion on active() (req.model "" → its model).
    net::RequestId chat(Request req, ChatDone done);
    // …on this provider.
    net::RequestId chat(const Provider &p, Request req, ChatDone done);
    // GET /models, which doubles as the connection and key test (the
    // settings editor's "Test connection" / "Fetch models", for a provider
    // that need not be saved yet).
    net::RequestId listModels(const Provider &p, ModelsDone done);
    void           cancel(net::RequestId id) { _net.cancel(id); }


    // A provider answered in-process by `answer` (given the request's text)
    // instead of a server; active() falls back to it while no real provider
    // is connected. It never transcribes.
    void setStandIn(Provider p, std::function<std::string(std::string_view request)> answer);
    bool isStandIn(const Provider *p) const { return _standInAnswer && p == &_standIn; }

private:
    // `done` with `error`, later (never re-entered), unless the Service is gone.
    void failChat(ChatDone done, std::string error);

    plat::App                                   &_app;
    net::Client                                  _net;
    std::vector<Provider>                        _providers;
    std::string                                  _defaultId, _language = "en";
    Provider                                     _standIn;
    std::function<std::string(std::string_view)> _standInAnswer;
    std::shared_ptr<char>                        _alive = std::make_shared<char>(0);
};

} // namespace llm
