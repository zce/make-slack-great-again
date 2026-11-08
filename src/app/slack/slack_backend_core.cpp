// What SlackBackend's read, write and realtime paths share: construction,
// the tracked Web API call (with the token refresh it may wait for),
// conversation ids and the auth-loss signal.
#include "app/slack/slack_backend.h"

#include "app/model/timers.h"
#include "base/log.h"
#include "plat/plat.h"

#include <algorithm>
#include <utility>

namespace slack {

SlackBackend::SlackBackend(
    model::Store &store, plat::App &app, net::Client &client, Credentials creds
)
    : Backend(store), _app(app), _client(client), _creds(std::move(creds)),
      _auth{_creds.token, _creds.cookie}, _alive(std::make_shared<bool>(true)) {
    _auth.orgToken  = _creds.orgToken;
    _auth.orgBase   = hostApiBase(_creds.orgUrl);
    _auth.orgTeamId = _creds.teamId;
    _read           = newRead(*this);
    _write          = newWrite(*this);
    _live           = newLive(*this);
}

SlackBackend::~SlackBackend() {
    *_alive = false;
    _transfers.reset(); // no transfer callback runs past here
    for (auto &d : _downloads)
        if (Done fn = std::exchange(*d, nullptr))
            _app.post([fn] { fn(false, "cancelled"); });
    for (net::RequestId id : _inflight)
        _client.cancel(id);
    _inflight.clear();
    deleteLive(_live);
    deleteWrite(_write);
    deleteRead(_read);
}

void SlackBackend::reconcileDirectConversation(
    const std::string &id, const model::Conversation &info
) {
    const model::ConvRef ref = _store.findConversation(id);
    if (ref == model::kNoConv)
        return;
    const model::Conversation &current = _store.conversation(ref);
    if (info.lastRead <= current.lastRead && info.latest <= current.latest &&
        info.unread <= current.unread && info.unread <= current.mentions)
        return;

    const bool readAdvanced = info.lastRead > current.lastRead;
    uint32_t   newerUnread = 0;
    if (readAdvanced) {
        // The snapshot is authoritative through info.latest. Preserve live
        // arrivals received after that snapshot while clearing older unread.
        for (const model::Message &m : current.messages)
            if (m.ts > info.latest && m.ts > info.lastRead && m.user != _store.me && !m.pending)
                ++newerUnread;
    }
    _store.updateConversation(ref, [&](model::Conversation &x) {
        x.lastRead = std::max(x.lastRead, info.lastRead);
        x.latest   = std::max(x.latest, info.latest);
        if (readAdvanced) {
            x.unread   = info.unread + newerUnread;
            x.mentions = info.unread + newerUnread;
        } else {
            x.unread   = std::max(x.unread, info.unread);
            x.mentions = std::max(x.mentions, info.unread);
        }
    });
}

net::Client &SlackBackend::transfers() {
    if (!_transfers)
        _transfers = std::make_unique<net::Client>(_app);
    return *_transfers;
}

bool SlackBackend::authError(const std::string &e) {
    return isAuthError(e);
}

void SlackBackend::api(std::string_view method, std::string form, ApiDone done) {
    issueApi(std::string(method), std::move(form), std::move(done), false);
}

// The answer-or-error call most writes are: a cancel is silent (the backend
// is going), a failure is logged and `done` hears it; on ok, onOk(doc) runs
// first, then done(true).
void SlackBackend::api(
    std::string_view                            method,
    std::string                                 form,
    std::function<void(const json::Document &)> onOk,
    Done                                        done
) {
    api(method,
        std::move(form),
        [m    = std::string(method),
         onOk = std::move(onOk),
         done = std::move(done)](const json::Document &doc, const std::string &err) {
            if (err == "cancelled")
                return;
            if (!err.empty())
                LOG_WARN("slack", "%s: %s", m.c_str(), err.c_str());
            else if (onOk)
                onOk(doc);
            if (done)
                done(err.empty(), err);
        });
}

// A rejected token (token_expired, or invalid_auth: revoked, or superseded by
// a refresh made on another device) is refreshed and the call re-issued —
// once: invalid_auth can outlive a "successful" refresh, and looping would
// hammer oauth.v2.access. Without a refresh
// token (session auth, a non-rotating OAuth token) it is fatal, as before.
//
// While a refresh is out nothing else goes out:
// a call sent on the dying token would come back invalid_auth and spend the
// next single-use refresh token. A call that was already out and comes back
// rejected after a refresh landed is re-sent on the new token, unrefreshed.
void SlackBackend::issueApi(
    std::string method, std::string form, ApiDone done, bool refreshed, bool quiet
) {
    if (refreshInFlight()) {
        refreshToken([this,
                      m    = std::move(method),
                      f    = std::move(form),
                      done = std::move(done),
                      refreshed,
                      quiet](bool ok) mutable {
            if (ok) {
                issueApi(std::move(m), std::move(f), std::move(done), refreshed, quiet);
                return;
            }
            json::Document none;
            if (done)
                done(none, _authLost ? std::string("invalid_auth") : "token_refresh_failed");
        });
        return;
    }
    // The id is only known after apiCall returns; the callback never runs
    // before that (net delivers later on the UI thread).
    auto        slot     = std::make_shared<net::RequestId>(0);
    std::string body     = form;
    std::string sentWith = _auth.token;
    *slot                = apiCall(
        _client,
        _auth,
        method,
        std::move(body),
        [this,
         slot,
         m = method,
         f = std::move(form),
         refreshed,
         quiet,
         sentWith = std::move(sentWith),
         done     = std::move(done)](const json::Document &doc, const std::string &err) mutable {
            _inflight.erase(
                std::remove(_inflight.begin(), _inflight.end(), *slot), _inflight.end()
            );
            const bool rejected = err == "token_expired" || err == "invalid_auth";
            if (rejected && !refreshed && (sentWith != _auth.token || refreshInFlight())) {
                // Sent on a token that has since been (or is being) replaced.
                issueApi(m, f, std::move(done), true, quiet);
                return;
            }
            if (rejected && !refreshed && canRefresh()) {
                LOG_INFO("slack", "%s: %s — refreshing the token", m.c_str(), err.c_str());
                refreshToken(
                    [this, m, f = std::move(f), err, quiet, done = std::move(done)](bool ok) {
                        if (ok) {
                            issueApi(m, f, done, true, quiet);
                            return;
                        }
                        // Dead credentials are reported by the refresh; a
                        // transient failure leaves the workspace signed in (the
                        // 60 s check tries again) and only this call fails —
                        // with a reason no caller takes for a dead sign-in.
                        json::Document none;
                        if (done)
                            done(none, _authLost ? err : std::string("token_refresh_failed"));
                    }
                );
                return;
            }
            if (!err.empty() && authError(err))
                loseAuth(err);
            if (err == "ratelimited" && !quiet)
                noteRateLimited(m, std::max<int64_t>(doc.root()["retry_after"].integer(1), 1));
            if (done)
                done(doc, err);
        }
    );
    _inflight.push_back(*slot);
}

void SlackBackend::loseAuth(const std::string &err) {
    if (_authLost)
        return;
    _authLost = true;
    LOG_WARN("slack", "%s — credentials no longer valid", err.c_str());
    if (onAuthLost) {
        auto fn = onAuthLost;
        model::postWhileAlive(_app, _alive, [fn, err] { fn(err); });
    }
}

const std::string &SlackBackend::convId(model::ConvRef conv) const {
    static const std::string kNone;
    return conv < _store.conversationCount() ? _store.conversation(conv).id : kNone;
}

} // namespace slack
