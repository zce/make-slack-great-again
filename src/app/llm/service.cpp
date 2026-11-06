#include "app/llm/service.h"
#include "app/model/timers.h"

#include "base/i18n.h"
#include "plat/plat.h"

namespace llm {

using i18n::arg;
using i18n::tr;

Service::Service(plat::App &app) : _app(app), _net(app) {}

Service::~Service() = default;

void Service::setProviders(std::vector<Provider> providers, std::string defaultId) {
    _providers = std::move(providers);
    _defaultId = std::move(defaultId);
}

const Provider *Service::active() const {
    for (const Provider &p : _providers)
        if (p.id == _defaultId && p.connected())
            return &p;
    for (const Provider &p : _providers)
        if (p.connected())
            return &p;
    return _standInAnswer ? &_standIn : nullptr;
}

void Service::setStandIn(Provider p, std::function<std::string(std::string_view)> answer) {
    _standInAnswer = std::move(answer);
    _standIn       = std::move(p);
}

net::RequestId Service::chat(Request req, ChatDone done) {
    if (const Provider *p = active())
        return chat(*p, std::move(req), std::move(done));
    failChat(
        std::move(done),
        tr("No AI provider connected \xE2\x80\x94 connect one in Settings \xE2\x86\x92 AI "
           "assistance")
    );
    return 0;
}

net::RequestId Service::chat(const Provider &p, Request req, ChatDone done) {
    if (!p.connected()) {
        failChat(std::move(done), arg(tr("%1 is not connected"), p.name));
        return 0;
    }
    if (req.model.empty())
        req.model = p.model;
    if (_standInAnswer && p.id == _standIn.id) {
        std::string text = req.system;
        for (const Message &m : req.messages)
            text += "\n" + m.text;
        // Later, as every answer: callers rely on never being re-entered.
        model::postWhileAlive(_app, _alive, [this, text = std::move(text), done = std::move(done)] {
            if (!done)
                return;
            ChatResult r;
            r.ok             = true;
            r.response.text  = _standInAnswer(text);
            r.response.model = _standIn.model;
            done(std::move(r));
        });
        return 0;
    }
    return _net.send(
        buildChat(p.endpoint(req.model), req),
        [format = p.format, done = std::move(done)](net::Response r) {
            if (done)
                done(parseChat(format, r));
        }
    );
}

net::RequestId Service::listModels(const Provider &p, ModelsDone done) {
    return _net.send(buildListModels(p.endpoint()), [done = std::move(done)](net::Response r) {
        if (done)
            done(parseModels(r));
    });
}

void Service::failChat(ChatDone done, std::string error) {
    model::postWhileAlive(_app, _alive, [error = std::move(error), done = std::move(done)] {
        if (!done)
            return;
        ChatResult r;
        r.error = error;
        done(std::move(r));
    });
}

} // namespace llm
