// The fake Slack Web API (fake_slack.py) shared by the Slack test suites:
// started once per process, every SlackBackend's calls pointed at it
// (MSGA_SLACK_API_BASE), one plat::App to pump. POSIX only.
#pragma once

#include "base/json.h"
#include "net/net.h"
#include "plat/plat.h"

#include <functional>
#include <string>
#include <string_view>

namespace fakeslack {

plat::App         &app();
// Pumps the loop until done() or the time runs out; false on timeout.
bool               pumpUntil(const std::function<bool()> &done, int timeoutMs = 10000);
void               pumpFor(int ms);
// "http://127.0.0.1:port" ("" when python3 is missing). The first call
// starts the server and sets MSGA_SLACK_API_BASE to its /api/.
const std::string &server();
// A control request: /_ctl/reset, /_ctl/script, /_ctl/set, /_ctl/log.
net::Response      ctl(const char *method, const std::string &path, std::string body = {});

// The fake's Slack file hosts over HTTPS, behind a CONNECT proxy: a
// certificate for files.slack.com and files.slack-edge.com, the same request
// log, and a TLS port for direct external-host URLs. Empty when python3 or
// openssl cannot provide it (set `ca` in SSL_CERT_FILE before the first TLS
// request, and HTTPS_PROXY to `proxy`).
struct TlsProxy {
    std::string proxy;    // "http://127.0.0.1:port"; "" when unavailable
    std::string ca;       // a PEM bundle for SSL_CERT_FILE
    std::string error;    // why it is unavailable
    int         port = 0; // the HTTPS server itself, for https://localhost URLs
};
const TlsProxy &tlsProxy();

// Standing answers and one method's sequence of answers (raw JSON).
void set(const std::string &answers);
void script(const char *method, const std::string &responses);

// A snapshot of requests and WebSocket frames seen by the fake. Returned
// Values borrow this snapshot's document; keep it alive while using them.
class Log {
public:
    Log();
    int count(std::string_view method) const;
    int count(std::string_view method, std::string_view field, std::string_view value) const;
    json::Value get(std::string_view method, int index = 0) const;
    // Method names in request order, separated by commas.
    std::string order() const;
    bool        frame(std::string_view method, std::string_view needle) const;
    json::Value entries() const { return _doc.root(); }

private:
    json::Document _doc;
};

// A fresh snapshot for polling a single method's request count.
int count(std::string_view method);

} // namespace fakeslack
