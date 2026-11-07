#include "app/screens/messages/thread_panel.h"

#include "app/screens/messages/thread_export.h"
#include "base/str.h"
#include "base/time.h"
#include "gfx/icons_generated.h"
#include "screens/common/file_dialogs.h"
#include "screens/common/icon_button.h"

#include "base/i18n.h"

#include <algorithm>

using namespace ui;
using i18n::arg;
using i18n::tr;

namespace screens {

namespace {

// A round icon button: 32 px, an 18 px icon.def glyph, surface.highlight
// on hover.
class RoundButton final : public IconButton {
public:
    RoundButton(gfx::Icon icon, std::string tip) : IconButton(icon, 18, C::FormIcon) {
        style().size(32, 32).noShrink();
        setLook({C::None, C::FormHighlight, C::FormHighlight, C::None, 16});
        setTooltip(std::move(tip));
        setRole(Role::Button);
    }
};

} // namespace

ThreadPanel::ThreadPanel(Context &ctx) : _ctx(ctx) {
    setBackground(C::Surface);
    setRole(Role::Group);
    auto *header = add<View>();
    header->style().row().height(48).padding(16, 0, 8, 0).spacing(8).items(Align::Center);
    header->style().noShrink();
    // Over the messages (a narrow window, or dragged full): back to them.
    _back = header->add<RoundButton>(gfx::Icon::ChevronLeft, tr("Close thread"));
    _back->setVisible(false);
    _back->onClick = [this] {
        if (_ctx.closeThread)
            _ctx.closeThread();
    };
    header->add<Label>(tr("Thread"), Font::BodyBold)->style().noShrink();
    header->add<View>()->style().flex(1);
    _mute          = header->add<RoundButton>(gfx::Icon::Bell, tr("Mute thread"));
    _mute->onClick = [this] { toggleMuted(); };
    _download      = header->add<RoundButton>(gfx::Icon::Download, tr("Download thread as text"));
    _download->onClick = [this] { downloadThread(); };
    _close          = header->add<RoundButton>(gfx::Icon::X, tr("Close thread"));
    _close->onClick = [this] {
        if (_ctx.closeThread)
            _ctx.closeThread();
    };
    _list = add<MessageList>(ctx);
    _list->style().flex(1);
    _list->setTypingRow(false); // the shell's indicator sits above the composer
    _typingSlot = add<View>();
    _slot       = add<View>();
    // The composer brings its own margins, as in the channel view (none added here).
    _slot->style().padding(0);
    // In line with the composer's contents (its margin plus the box's inner
    // padding), clear of the bottom edge; the composer's margin is the gap above.
    _broadcastRow = add<View>();
    _broadcastRow->style().row().padding(12 + 8, 0, 12, 12);
    _broadcast = _broadcastRow->add<CheckBox>(tr("Also send to channel"));
    _broadcast->setLabelFont(Font::Tiny, C::FormTextMuted);
    _broadcast->onChange = [this](bool on) { _wanted = on; };
    _broadcastRow->setVisible(false);
}

void ThreadPanel::show(ConvRef conv, Ts root, Ts jumpTarget) {
    // Another thread: the tick was for a reply there.
    if (conv != conversation() || root != this->root())
        _wanted = false;
    _list->showThread(conv, root, jumpTarget);
    refreshMute();
    refreshBroadcast();
}

ui::View *ThreadPanel::setComposer(std::unique_ptr<ui::View> composer) {
    _slot->clearChildren();
    _composer = composer ? _slot->adopt(std::move(composer)) : nullptr;
    return _composer;
}

ui::View *ThreadPanel::setTyping(std::unique_ptr<ui::View> typing) {
    _typingSlot->clearChildren();
    return typing ? _typingSlot->adopt(std::move(typing)) : nullptr;
}

void ThreadPanel::setBackShown(bool on) {
    _back->setVisible(on);
    _close->setVisible(!on);
}

void ThreadPanel::toggleMuted() {
    const ConvRef c = conversation();
    if (c == model::kNoConv || !root())
        return;
    _ctx.store().setThreadMuted(c, root(), !_ctx.store().threadMuted(c, root()));
    refreshMute();
}

void ThreadPanel::refreshMute() {
    const bool muted = conversation() != model::kNoConv && root() &&
                       _ctx.store().threadMuted(conversation(), root());
    auto      *b     = static_cast<RoundButton *>(_mute);
    b->setTooltip(muted ? tr("Unmute thread") : tr("Mute thread"));
    b->setIcon(muted ? gfx::Icon::BellOff : gfx::Icon::Bell);
}

bool ThreadPanel::broadcastShown() const {
    return _broadcastRow->visible();
}

bool ThreadPanel::broadcastWanted() const {
    return _wanted && !_blocked && broadcastShown();
}

void ThreadPanel::setBroadcastWanted(bool on) {
    _wanted = on;
    refreshBroadcast();
}

void ThreadPanel::setBroadcastBlocked(bool blocked) {
    _blocked = blocked;
    refreshBroadcast();
}

void ThreadPanel::refreshBroadcast() {
    // Only a channel thread has a channel to also send the reply to.
    const ConvRef c    = conversation();
    const bool    show = c != model::kNoConv && _ctx.backend.capabilities().replyBroadcast &&
                         !_ctx.store().conversation(c).isDirect();
    _broadcastRow->setVisible(show);
    _broadcast->setEnabled(show && !_blocked);
    _broadcast->setChecked(show && !_blocked && _wanted);
}

// Where to, then exportThread pages
// through the whole thread and writes its transcript in the background.
void ThreadPanel::downloadThread() {
    const ConvRef conv = conversation();
    const Ts      rt   = root();
    if (conv == model::kNoConv || !rt)
        return;
    // Taken now: the export may outlive this panel and its thread.
    std::string          title = threadExportTitle(_ctx.store, conv);
    plat::FileDialogDesc d;
    d.mode          = plat::FileDialogDesc::Mode::Save;
    d.title         = tr("Save thread");
    d.initialDir    = _ctx.app.platform().standardDir(plat::StandardDir::Home);
    d.suggestedName = str::concat({"thread-", base::isoDate(model::tsSecs(rt)), ".txt"});
    Context &ctx    = _ctx; // owned by main: outlives the panel
    fileDialog(
        ctx,
        std::move(d),
        [&ctx, conv, rt, title = std::move(title)](std::vector<std::string> paths) mutable {
            if (!paths.empty() && !paths[0].empty())
                exportThread(ctx, conv, rt, std::move(title), std::move(paths[0]));
        }
    );
}

} // namespace screens
