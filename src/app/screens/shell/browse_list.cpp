// Shared virtualized picker used by Slack's channel and people dialogs.
// Extracted from session_dialogs.cpp so Claude Code does not enter the build.
#include "screens/shell/session_dialogs.h"

#include "base/str.h"
#include "base/utf8.h"
#include "gfx/icons_generated.h"
#include "ui/controls.h"

#include <algorithm>
#include <memory>

using namespace ui;
using gfx::Icon;

namespace shell {
namespace {
constexpr float kRowH = BrowseList::kRowH, kAvatar = 36;

// One row: the picture (or a channel's icon), the title over its subtitle,
// the badge. Recycled: bind() refills it for another item.
class BrowseRow final : public Clickable {
public:
    BrowseRow() {
        setRole(Role::ListItem);
        style().row().height(kRowH).items(Align::Center).noShrink();
    }
    void bind(
        Avatars                   &avatars,
        const BrowseList::Item    &it,
        const BrowseList::Options &opt,
        bool                       onContent,
        float                      radius
    ) {
        // Keyboard selection reads stronger than hover; so does a press
        // where the row waits for the click.
        const C pressed = opt.onPress ? C::FormHighlight : C::FormHighlightStrong;
        setLook({C::None, C::FormHighlight, pressed, C::FormHighlightStrong, 0});
        setFocusable(!opt.onPress, false);
        style().padding(opt.rowInset, 0);
        _onPress = opt.onPress;
        clearChildren();
        const bool channel = it.titleIcon != 0xffff;
        if (!channel) {
            auto *av = add<Avatar>();
            av->style().size(kAvatar, kAvatar).margins(0, 0, 12, 0).noShrink();
            if (radius < 0)
                av->setCircle(true);
            else
                av->setRadius(radius);
            av->setPlaceholder(C::PresenceAway);
            if (opt.initials)
                av->setInitial(it.title);
            av->setBitmap(avatars.get(it.avatar, int(kAvatar * 2)));
        }
        const C primary = onContent ? C::Text : C::FormText;
        const C muted   = onContent ? C::TextMuted : C::FormTextMuted;
        auto   *text    = add<View>();
        text->style().flex(1).spacing(1);
        text->style().shrink = 1;
        View *titleLine      = text;
        if (channel) { // the hash / lock 6 px before the title
            titleLine = text->add<View>();
            titleLine->style().row().items(Align::Center).spacing(6);
            titleLine->add<IconView>(Icon(it.titleIcon), 14, muted)->style().size(14, 14);
        }
        auto *title = titleLine->add<Label>(it.title, Font::BodyBold, primary);
        title->setMaxLines(1);
        title->style().shrink = 1;
        if (!it.subtitle.empty())
            styledLabel(
                text, it.subtitle, pxFont(15 * 0.88f, text::Weight::Regular, themed(muted)), 1
            );
        if (!it.badge.empty()) {
            auto *badge = add<View>();
            badge->style().row().items(Align::Center).spacing(4).margins(12, 0, 0, 0).noShrink();
            if (it.badgeCheck)
                badge->add<IconView>(Icon::Check, 13, muted)->style().size(13, 13);
            styledLabel(
                badge,
                it.badge,
                pxFont(
                    15 * 0.88f,
                    it.badgeStrong ? text::Weight::Bold : text::Weight::Regular,
                    themed(it.badgeStrong ? primary : muted)
                ),
                1
            );
        }
    }
    bool onEvent(Event &e) override {
        // The list activates on press (unless it waits for the click).
        if (_onPress && e.type == EventType::PointerDown && e.button == plat::Button::Left) {
            activate();
            return true;
        }
        return Clickable::onEvent(e);
    }

private:
    bool _onPress = true;
};

// A 32 px close button with a 14 px cross: flat, a round hover wash.
class CloseX final : public Clickable {
public:
    CloseX() {
        style().size(32, 32).noShrink();
        setFocusable(false);
        setLook({C::None, C::None, C::None, C::None, 0});
    }
    void paint(gfx::Painter &p) override {
        if (hovered())
            p.fillCircle({16, 16}, 16, color(C::FormHighlight));
        gfx::drawIcon(p, Icon::X, {9, 9, 14, 14}, color(C::FormIcon));
    }
};

} // namespace

Clickable *addDialogCloseButton(View *parent) {
    return parent->add<CloseX>();
}

// ── BrowseList ──────────────────────────────────────────────────────────────

std::unique_ptr<View> BrowseListData::Rows::create(int) {
    return std::make_unique<BrowseRow>();
}

void BrowseListData::Rows::bind(View &row, int index) {
    static_cast<BrowseList *>(data)->bindRow(row, index);
}

float BrowseListData::Rows::estimateHeight(int) const {
    return kRowH;
}

BrowseList::BrowseList(Avatars &avatars) : VirtualList(&rows), _avatars(avatars) {
    style().flex(1);
}

BrowseList::~BrowseList() = default;

void BrowseList::setItems(std::vector<Item> list) {
    items = std::move(list);
    applyFilter({});
}

void BrowseList::refreshItems(std::vector<Item> list) {
    if (list == items)
        return;
    const Anchor at = anchor();
    items           = std::move(list);
    shown.clear();
    for (size_t i = 0; i < items.size(); ++i)
        shown.push_back(i);
    _selected = -1;
    reset();
    scrollToAnchor(at);
}

void BrowseList::applyFilter(std::string_view query) {
    const std::string q = utf8::foldCase(str::trim(query));
    shown.clear();
    for (size_t i = 0; i < items.size(); ++i)
        if (utf8::containsPrefolded(items[i].searchKey, q))
            shown.push_back(i);
    _selected = -1; // a re-filtered list has a different nth row
    reset();
    scrollTo(0);
}

void BrowseList::bindRow(View &v, int index) {
    auto &row = static_cast<BrowseRow &>(v);
    row.bind(_avatars, items[shown[size_t(index)]], _opt, _onContent, _radius);
    row.setChecked(index == _selected);
    row.onClick = [this, id = items[shown[size_t(index)]].id] {
        if (onActivated)
            onActivated(id);
    };
}

void BrowseList::setSelectedRow(int row) {
    const int n = int(shown.size());
    row         = row < 0 || n == 0 ? -1 : std::min(row, n - 1);
    if (row == _selected)
        return;
    if (View *old = _selected >= 0 ? viewFor(_selected) : nullptr)
        static_cast<Clickable *>(old)->setChecked(false);
    _selected = row;
    if (row < 0)
        return;
    if (View *now = viewFor(row))
        static_cast<Clickable *>(now)->setChecked(true);
    scrollToItem(row, ItemAlign::Nearest, false);
}

void BrowseList::moveSelection(int delta) {
    const int n = int(shown.size());
    if (n == 0 || delta == 0)
        return;
    if (!_opt.wrap) { // none selected: either key picks the first
        setSelectedRow(std::clamp(_selected + delta, 0, n - 1));
        return;
    }
    // Wrap, so Up from the top lands on the last match.
    const int from = _selected < 0 ? (delta > 0 ? -1 : 0) : _selected;
    setSelectedRow(((from + delta) % n + n) % n);
}

bool BrowseList::navKey(const Event &e) {
    if (e.type != EventType::KeyDown)
        return false;
    switch (e.key) {
    case plat::Key::Down:
        moveSelection(1);
        return true;
    case plat::Key::Up:
        moveSelection(-1);
        return true;
    case plat::Key::Enter:
    case plat::Key::KpEnter:
        activateSelected();
        return true;
    default:
        return false;
    }
}

void BrowseList::activateSelected() {
    if (_selected < 0 || _selected >= int(shown.size()) || !onActivated)
        return;
    onActivated(items[shown[size_t(_selected)]].id);
}

void BrowseList::paint(gfx::Painter &p) {
    if (_opt.fill)
        p.fillRect(bounds(), color(_onContent ? C::Surface : C::FormBg));
    VirtualList::paint(p);
}


} // namespace shell
