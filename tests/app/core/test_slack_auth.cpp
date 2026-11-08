// Asynchronous sign-in flows against the local fake Slack, never a real account.
#include "app/slack/browser_login.h"
#include "app/slack/oauth.h"
#include "app/slack/session.h"
#include "support/fake_slack_server.h"
#include "support/test.h"
#include "plat/testing.h"

#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32

namespace {

using fakeslack::app;
using fakeslack::count;
using fakeslack::ctl;
using fakeslack::pumpFor;
using fakeslack::pumpUntil;
using fakeslack::set;

bool haveServer() {
    if (!fakeslack::server().empty())
        return true;
    std::fprintf(stderr, "  skip: no fake Slack (python3 missing)\n");
    return false;
}

slack::TeamSession candidate(const char *token, const char *teamId) {
    slack::TeamSession team;
    team.token   = token;
    team.teamId  = teamId;
    team.iconUrl = "https://example.test/icon.png";
    return team;
}

} // namespace

TEST("slack OAuth: a callback delivered twice exchanges the code once") {
    if (!haveServer())
        return;
    plat::TestHooks *hooks = app().testHooks();
    if (!hooks)
        return;
    ctl("POST", "/_ctl/reset");
    // The first exchange answers late; a second one (the same code again)
    // would fail at once, end the flow — and the late answer would then land
    // on a destroyed flow (issue #96).
    set(R"({"oauth.v2.access": [
              {"ok": true, "authed_user": {"access_token": "xoxp-new"},
               "team": {"id": "T9", "name": "Nine"}, "__delay": 0.3},
              {"ok": false, "error": "invalid_code"}]})");
    net::Client client{app()};
    auto        flow = std::make_unique<slack::OAuthFlow>(
        app(), client, slack::AppConfig{"cid", "secret", "xapp-1"}
    );
    int         called = 0;
    std::string token, error;
    flow->start([&](slack::Credentials c, std::string err) {
        ++called;
        token = c.token;
        error = std::move(err);
        app().post([&] { flow.reset(); }); // as Accounts does (releaseLater)
    });
    std::string opened;
    REQUIRE(hooks->lastOpenedUrl(&opened));
    const std::string state = net::queryValue(opened.substr(opened.find('?')), "state");
    REQUIRE(!state.empty());
    const std::string url = "msga://oauth/callback?code=c1&state=" + state;
    CHECK(flow->handleCallback(url));
    CHECK_FALSE(flow->handleCallback(url)); // a second launch's copy
    REQUIRE(pumpUntil([&] { return called > 0; }));
    pumpFor(500);
    CHECK(called == 1);
    CHECK_STR(error, "");
    CHECK_STR(token, "xoxp-new");
    CHECK(count("oauth.v2.access") == 1);
}

TEST("slack OAuth: a deferred setup error is dropped after flow destruction") {
    net::Client client{app()};
    int         called = 0;
    {
        slack::OAuthFlow flow(app(), client, {});
        flow.start([&](slack::Credentials, std::string) { ++called; });
    }
    pumpFor(30);
    CHECK(called == 0);
}

TEST("slack browser login: a deferred startup error is dropped after destruction") {
    base::test::setEnv("MSGA_BROWSER_LOGIN", "0");
    net::Client client{app()};
    int         called = 0;
    {
        slack::BrowserLogin login(app(), client);
        login.start({}, [&](std::string, std::vector<slack::TeamSession>, std::string) {
            ++called;
        });
    }
    pumpFor(30);
    base::test::unsetEnv("MSGA_BROWSER_LOGIN");
    CHECK(called == 0);
}

TEST("slack session: valid workspaces do not inherit an earlier validation error") {
    if (!haveServer())
        return;
    ctl("POST", "/_ctl/reset");
    fakeslack::script("auth.test", R"([
        {"ok":false,"error":"invalid_auth"},
        {"ok":true,"team_id":"T2","team":"Two","url":"https://two.slack.com/"}])");

    net::Client        client{app()};
    slack::TeamSession invalid, validCandidate;
    invalid.token           = "xoxc-invalid";
    invalid.teamId          = "T1";
    invalid.teamName        = "One";
    invalid.iconUrl         = "https://example.test/one.png";
    validCandidate.token    = "xoxc-valid";
    validCandidate.teamId   = "T2";
    validCandidate.teamName = "Two";
    validCandidate.iconUrl  = "https://example.test/two.png";

    std::vector<slack::Credentials> valid;
    std::string                     error;
    bool                            called = false;
    slack::TokenDeriver             deriver(client);
    deriver.run(
        "xoxd-test",
        {std::move(invalid), std::move(validCandidate)},
        [&](std::vector<slack::Credentials> creds, std::string err) {
            valid  = std::move(creds);
            error  = std::move(err);
            called = true;
        }
    );
    REQUIRE(pumpUntil([&] { return called; }));
    REQUIRE(valid.size() == 1);
    CHECK_STR(valid.front().teamId, "T2");
    CHECK(error.empty());
}

TEST("slack session: a new run clears the previous run's error") {
    if (!haveServer())
        return;
    ctl("POST", "/_ctl/reset");
    fakeslack::script("auth.test", R"([
        {"ok":false,"error":"invalid_auth"},
        {"ok":true,"team_id":"T2","team":"Two"}])");
    net::Client                     client{app()};
    slack::TokenDeriver             deriver(client);
    int                             called = 0;
    std::string                     error;
    std::vector<slack::Credentials> valid;
    const auto done = [&](std::vector<slack::Credentials> creds, std::string err) {
        valid = std::move(creds);
        error = std::move(err);
        ++called;
    };
    deriver.run("xoxd-test", {candidate("xoxc-invalid", "T1")}, done);
    REQUIRE(pumpUntil([&] { return called == 1; }));
    CHECK(valid.empty());
    CHECK_STR(error, "invalid_auth");
    deriver.run("xoxd-test", {candidate("xoxc-valid", "T2")}, done);
    REQUIRE(pumpUntil([&] { return called == 2; }));
    REQUIRE(valid.size() == 1);
    CHECK_STR(valid.front().teamId, "T2");
    CHECK(error.empty());
}

TEST("slack session: replacing or destroying a run suppresses its outstanding result") {
    if (!haveServer())
        return;
    ctl("POST", "/_ctl/reset");
    fakeslack::script("auth.test", R"([
        {"ok":true,"team_id":"T1","__delay":0.2},
        {"ok":true,"team_id":"T2"},
        {"ok":true,"team_id":"T3","__delay":0.2}])");
    net::Client client{app()};
    int         replaced = 0, completed = 0, destroyed = 0;
    {
        slack::TokenDeriver deriver(client);
        deriver.run(
            "xoxd-old",
            {candidate("xoxc-old", "T1")},
            [&](std::vector<slack::Credentials>, std::string) { ++replaced; }
        );
        REQUIRE(pumpUntil([&] { return count("auth.test") == 1; }));
        deriver.run(
            "xoxd-new",
            {candidate("xoxc-new", "T2")},
            [&](std::vector<slack::Credentials> valid, std::string error) {
                REQUIRE(valid.size() == 1);
                CHECK_STR(valid.front().teamId, "T2");
                CHECK_STR(valid.front().cookie, "xoxd-new");
                CHECK(error.empty());
                ++completed;
            }
        );
        REQUIRE(pumpUntil([&] { return completed == 1; }));
        deriver.run(
            "xoxd-third",
            {candidate("xoxc-third", "T3")},
            [&](std::vector<slack::Credentials>, std::string) { ++destroyed; }
        );
        REQUIRE(pumpUntil([&] { return count("auth.test") == 3; }));
    }
    pumpFor(300);
    CHECK(replaced == 0);
    CHECK(completed == 1);
    CHECK(destroyed == 0);
}

#endif // !_WIN32
