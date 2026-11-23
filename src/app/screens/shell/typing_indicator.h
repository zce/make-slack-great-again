// The typing indicator, between the message list and the composer:
// "<b>Mira</b> is typing…", "<b>Mira</b>, <b>Jonas</b> are typing…", or
// "<b>You</b> are typing on another device…" — hidden (no height) while
// nobody types. Follows Store typing for one conversation (and thread).
#pragma once

#include "screens/common/context.h"
#include "ui/ui.h"

namespace shell {

// Who's typing in `conv`'s `thread` right now. The agent "thinking"
// state and its clock were deliberately removed with agent sessions.
struct TypingState {
    std::vector<model::UserRef> who;
};
TypingState typingStateOf(const model::Store &store, model::ConvRef conv, model::Ts thread);

class TypingIndicator : public ui::View {
public:
    explicit TypingIndicator(screens::Context &ctx);
    ~TypingIndicator() override;

    void               setTarget(model::ConvRef conv, model::Ts thread);
    const std::string &text() const { return _label->text(); }

private:
    void refresh();

    screens::Context        &_ctx;
    ui::Label               *_label;
    model::ConvRef           _conv     = model::kNoConv;
    model::Ts                _thread   = 0;
    model::Store::ObserverId _observer = 0;
    text::Style              _shownStyle;   // the plain style of the text shown
};

} // namespace shell
