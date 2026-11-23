#include "app/model/store.h"
#include "screens/shell/typing_indicator.h"
#include "support/test.h"

using namespace model;
using namespace shell;

namespace {

ConvRef addChannel(Store &s) {
    Conversation c;
    c.id     = "C1";
    c.kind   = ConvKind::Channel;
    c.member = true;
    return s.addConversation(std::move(c));
}

} // namespace

TEST("typing_indicator: typingStateOf -- nobody typing") {
    Store             s;
    ConvRef           c     = addChannel(s);
    const TypingState state = typingStateOf(s, c, 0);
    CHECK(state.who.empty());
}

TEST("typing_indicator: typingStateOf -- a person typing is listed") {
    Store   s;
    ConvRef c = addChannel(s);
    UserRef u = s.addUser({.id = "U1"});
    s.setTyping(c, u, 0, true);
    const TypingState state = typingStateOf(s, c, 0);
    CHECK(state.who.size() == 1);
    CHECK(state.who[0] == u);
}

TEST("typing_indicator: typingStateOf -- only this thread's typers count") {
    Store   s;
    ConvRef c  = addChannel(s);
    UserRef u1 = s.addUser({.id = "U1"});
    UserRef u2 = s.addUser({.id = "U2"});
    s.setTyping(c, u1, 0, true); // top-level
    s.setTyping(c, u2, 7, true); // a reply thread
    CHECK(typingStateOf(s, c, 0).who.size() == 1);
    CHECK(typingStateOf(s, c, 7).who.size() == 1);
    CHECK(typingStateOf(s, c, 99).who.empty());
}
