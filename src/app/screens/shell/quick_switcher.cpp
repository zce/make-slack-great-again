#include "screens/shell/quick_switcher.h"

#include "base/i18n.h"
#include "base/str.h"
#include "base/utf8.h"
#include "gfx/icons_generated.h"
#include "screens/common/avatar_initial.h"
#include "app/screens/common/message_text.h"
#include "base/time.h"
#include "screens/shell/shell_text.h"
#include "screens/shell/sidebar.h"

#include <algorithm>
#include <optional>
#include <utility>

using namespace ui;
using gfx::Icon;
using i18n::tr;
using model::ConvRef;

namespace shell {

namespace {

constexpr float kListH = 380; // compact, scrollable search results
constexpr float kMessageRowH = 60;
constexpr size_t kPreviewChars = 180;

// Issue #60: fuzzy, not substring ("xdg" lands on #xd-general). None
// for an id we can't name yet: not something to offer.
std::optional<double> scoreOf(const QuickSwitchName &n, const std::vector<uint32_t> &q) {
    if (n.text.folded.empty())
        return std::nullopt;
    std::optional<double> s = fuzzyScore(q, n.text);
    if (!n.alt.folded.empty())
        if (const std::optional<double> a = fuzzyScore(q, n.alt); a && (!s || *a > *s))
            s = a;
    if (!s)
        return std::nullopt;
    // Group DMs are named after their members, so a person matches every
    // group they are in as well as their DM — and the groups, often more
    // recent, buried it (issue #61): half a consecutive match.
    return *s + (n.group ? -0.5 : 0.0);
}

// A workspace tab's bubble: the rail's 40 px bubble in a slot that
// keeps room for the selection ring (accent around the current one, a
// divider ring on hover), so picking one moves nothing; dimmed when its
// workspace has nothing for the query.
class TabBubble final : public Clickable {
public:
    static constexpr float kBubble = 40, kRadius = 10, kRing = 2, kRingGap = 2;
    static constexpr float kSlot = kBubble + 2 * (kRing + kRingGap);

    TabBubble(std::string name, std::shared_ptr<const gfx::Bitmap> icon)
        : _name(std::move(name)), _icon(std::move(icon)),
          _letter(screens::avatarInitial(_name.empty() ? std::string_view("?") : _name)) {
        style().size(kSlot, kSlot).noShrink();
        setLook({C::None, C::None, C::None, C::None, 0});
        setHoverRepaint(true);
        setCursor(plat::Cursor::Hand);
        setTooltip(_name);
    }
    void set(bool current, bool dimmed) {
        if (current != _current || dimmed != _dimmed) {
            _current = current;
            _dimmed  = dimmed;
            update();
        }
    }
    bool showsBitmap(const gfx::Bitmap *b) const override { return _icon.get() == b; }
    void paint(gfx::Painter &p) override {
        const float in = kRing + kRingGap;
        const RectF r{in, in, kBubble, kBubble};
        p.save();
        if (_dimmed && !_current)
            p.setOpacity(0.35f);
        if (_icon && !_icon->empty()) {
            p.save();
            p.clipRoundRect(r, kRadius);
            p.drawBitmap(_icon->view(), r, gfx::Sampling::Smooth);
            p.restore();
        } else {
            p.fillRoundRect(r, kRadius, color(C::Accent));
            screens::paintInitial(p, *this, r, kRadius, 0, _letter, _layout, 17);
        }
        p.restore();
        if (_current || hovered()) {
            const float out = kRingGap + kRing / 2;
            p.strokeRoundRect(
                {r.x - out, r.y - out, r.w + 2 * out, r.h + 2 * out},
                kRadius + out,
                kRing,
                color(_current ? C::Accent : C::FormDividerStrong)
            );
        }
    }

private:
    std::string                        _name;
    std::shared_ptr<const gfx::Bitmap> _icon;
    std::string                        _letter;
    std::unique_ptr<text::Layout>      _layout;
    bool                               _current = false, _dimmed = false;
};

} // namespace

namespace {

// Seconds of a message timestamp (epoch micros).
int64_t secs(model::Ts ts) {
    return ts / 1000000;
}

} // namespace

std::vector<ConvRef> quickSwitchOrder(
    const model::Store &store, const std::unordered_map<std::string, int64_t> &visited
) {
    struct Entry {
        ConvRef     c;
        int64_t     activity;
        std::string key; // the name, case-folded
    };
    std::vector<Entry> list;
    for (ConvRef c = 0; c < store.conversationCount(); ++c) {
        const model::Conversation &cv = store.conversation(c);
        if (!cv.member)
            continue;
        if (deadDm(store, cv)) // a DM whose peer is gone: nothing to jump to
            continue;
        const auto v = visited.find(cv.id);
        list.push_back(
            {c,
             std::max(
                 {v == visited.end() ? int64_t(0) : v->second, secs(cv.latest), secs(cv.lastRead)}
             ),
             utf8::foldCase(store.displayName(c))}
        );
    }
    std::sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) {
        if (a.activity != b.activity)
            return a.activity > b.activity; // most recent first
        return a.key < b.key;
    });
    std::vector<ConvRef> out;
    out.reserve(list.size());
    for (const Entry &e : list)
        out.push_back(e.c);
    return out;
}

std::vector<QuickSwitchName>
quickSwitchNames(const model::Store &store, const std::vector<ConvRef> &order) {
    std::vector<QuickSwitchName> out;
    out.reserve(order.size());
    for (ConvRef c : order) {
        const model::Conversation &cv = store.conversation(c);
        const std::string          nm = store.displayName(c);
        // A DM also by its peer's other names: the full or display name
        // that doesn't show (Settings → Names), and the handle.
        std::string                alt;
        if (cv.kind == model::ConvKind::Dm && cv.localName.empty() && cv.dmUser != model::kNoUser) {
            const model::User &u = store.user(cv.dmUser);
            for (const std::string *n : {&u.realName, &u.profileName, &u.name})
                if (!n->empty() && *n != nm)
                    alt = alt.empty() ? *n : str::concat({alt, " ", *n});
        }
        out.push_back(
            {FuzzyText(nm),
             cv.kind == model::ConvKind::Group,
             alt.empty() ? FuzzyText() : FuzzyText(alt)}
        );
    }
    return out;
}

namespace {

// What matches, with its score, in `order`.
QuickSwitcher::Scored scoreAll(
    const std::vector<uint32_t>        &q,
    const std::vector<ConvRef>         &order,
    const std::vector<QuickSwitchName> &names
) {
    QuickSwitcher::Scored scored;
    for (size_t i = 0; i < order.size() && i < names.size(); ++i)
        if (const std::optional<double> sc = scoreOf(names[i], q))
            scored.emplace_back(*sc, order[i]);
    return scored;
}

// Best first; equal scores keep `order`.
std::vector<ConvRef> ranked(QuickSwitcher::Scored scored) {
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    std::vector<ConvRef> out;
    out.reserve(scored.size());
    for (const auto &s : scored)
        out.push_back(s.second);
    return out;
}

std::optional<double> bestOf(const QuickSwitcher::Scored &scored) {
    std::optional<double> best;
    for (const auto &s : scored)
        if (!best || s.first > *best)
            best = s.first;
    return best;
}

} // namespace

std::vector<ConvRef> quickSwitchFilter(
    std::string_view                    query,
    const std::vector<ConvRef>         &order,
    const std::vector<QuickSwitchName> &names
) {
    return ranked(scoreAll(fuzzyQuery(str::trim(query)), order, names));
}

std::vector<ConvRef> quickSwitchFilter(
    const model::Store &store, std::string_view query, const std::vector<ConvRef> &order
) {
    return quickSwitchFilter(query, order, quickSwitchNames(store, order));
}

// Search text formatting shared by the popup and its tests.
std::string searchConvLabel(const model::Store &store, ConvRef conv) {
    const std::string name = conv < store.conversationCount() ? store.displayName(conv) : "";
    if (name.empty())
        return tr("Unknown channel");
    return store.conversation(conv).isDirect() ? name : str::concat({"#", name});
}

namespace {
constexpr std::string_view kHighlightStart = "\xEE\x80\x80";
constexpr std::string_view kHighlightStop = "\xEE\x80\x81";
std::string withoutHighlights(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    while (!s.empty()) {
        if (s.starts_with(kHighlightStart)) s.remove_prefix(kHighlightStart.size());
        else if (s.starts_with(kHighlightStop)) s.remove_prefix(kHighlightStop.size());
        else { out += s.front(); s.remove_prefix(1); }
    }
    return out;
}
} // namespace

std::string searchPreview(const model::Store &store, std::string_view value) {
    std::string out = screens::plainText(store, withoutHighlights(value));
    out.resize(utf8::prefixBytes(out, kPreviewChars));
    std::replace(out.begin(), out.end(), '\n', ' ');
    return out;
}

text::AttributedText searchHighlightedPreview(const model::Store &store, std::string_view value) {
    const std::string plain = screens::plainText(store, value);
    text::AttributedText result;
    text::Style normal = pxFont(13, text::Weight::Regular, themed(C::TextMuted));
    text::Style marked = normal;
    marked.weight = text::Weight::Semibold;
    marked.color = themed(C::Text);
    marked.background = themed(C::FormHighlight);
    bool active = false;
    size_t pos = 0, count = 0;
    while (pos < plain.size() && count < kPreviewChars) {
        if (plain.compare(pos, kHighlightStart.size(), kHighlightStart) == 0) {
            active = true; pos += kHighlightStart.size(); continue;
        }
        if (plain.compare(pos, kHighlightStop.size(), kHighlightStop) == 0) {
            active = false; pos += kHighlightStop.size(); continue;
        }
        size_t bytes = utf8::prefixBytes(std::string_view(plain).substr(pos), 1);
        if (!bytes) break;
        std::string_view part(plain.data() + pos, bytes);
        if (part == "\n") part = " ";
        result.append(part, active ? marked : normal);
        pos += bytes; ++count;
    }
    return result;
}

std::string searchScopeQuery(const model::Store &store, ConvRef conv) {
    if (conv == model::kNoConv || conv >= store.conversationCount()) return {};
    const auto &c = store.conversation(conv);
    if (c.kind == model::ConvKind::Dm && c.dmUser != model::kNoUser &&
        c.dmUser < store.userCount())
        return "in:<@" + store.user(c.dmUser).id + ">";
    if ((c.kind == model::ConvKind::Channel || c.kind == model::ConvKind::Private) &&
        !c.name.empty()) return "in:" + c.name;
    return c.id.empty() ? std::string() : "in:<#" + c.id + ">";
}

// The list rows, virtual (a large workspace has thousands of channels):
// a DM's photo, a group DM's initial disc, a
// channel's # or lock, then the name.
class QuickSwitcher::Rows final : public VirtualList::Adapter {
public:
    enum class Type : uint8_t { Header, Conversation, Person, Message };
    enum class Section : uint8_t { Conversations, People, Messages };
    struct Entry {
        Type type = Type::Header;
        Section section = Section::Conversations;
        int selectionIndex = -1;
        size_t dataIndex = 0;
    };

    class Row final : public Clickable {
    public:
        Row() {
            setLook({C::None, C::Hover, C::Pressed, C::Selection, 6});
            style().row().height(36).padding(10, 4).spacing(10).items(Align::Center);
            avatar = add<Avatar>();
            avatar->style().size(20, 20);
            avatar->setRadius(5);
            icon = add<IconView>(Icon::Hash, 16, C::TextMuted);
            icon->style().size(20, 20);
            auto *body = add<View>();
            body->style().column().flex(1).spacing(2);
            name = body->add<Label>("", Font::Body);
            name->setMaxLines(1);
            metaRow = body->add<View>();
            metaRow->style().row().spacing(8).items(Align::Center);
            metaRow->setVisible(false);
            meta = metaRow->add<Label>("", Font::Body);
            meta->style().flex(1);
            meta->setMaxLines(1);
            timestamp = metaRow->add<Label>("", Font::Small, C::TextMuted);
            timestamp->setMaxLines(1);
            timestamp->style().noShrink();
            snippet = body->add<Label>("", Font::Small, C::TextMuted);
            snippet->setMaxLines(1);
            snippet->setVisible(false);
        }
        Avatar   *avatar = nullptr;
        IconView *icon = nullptr;
        Label    *name = nullptr;
        View     *metaRow = nullptr;
        Label    *meta = nullptr;
        Label    *timestamp = nullptr;
        Label    *snippet = nullptr;
        int       selectionIndex = -1;
    };

    explicit Rows(QuickSwitcher &q) : _q(q) {}

    int selectableCount() const {
        return int(_q._results.size() + _q._people.size() + _q._hits.size());
    }

    int count() const override {
        return selectableCount() + int(!_q._results.empty()) +
               int(!_q._people.empty()) + int(!_q._hits.empty());
    }

    int kind(int index) const override {
        return entry(index).type == Type::Header ? 1 : 0;
    }

    std::unique_ptr<View> create(int kind) override {
        if (kind == 1) {
            auto header = std::make_unique<Label>("", Font::Small, C::TextMuted);
            header->style().height(24).padding(0, 5, 8, 1);
            header->setMaxLines(1);
            return header;
        }
        auto row = std::make_unique<Row>();
        Row *raw = row.get();
        row->onClick = [this, raw] {
            if (raw->selectionIndex >= 0) _q.choose(raw->selectionIndex);
        };
        return row;
    }

    // The selected index is dense; section headings are visible but not selectable.
    int visualIndex(int selectionIndex) const {
        if (selectionIndex < 0 || selectionIndex >= selectableCount()) return -1;
        int row = 0, remaining = selectionIndex;
        const int sections[] = {
            int(_q._results.size()), int(_q._people.size()), int(_q._hits.size())
        };
        for (int count : sections) {
            if (!count) continue;
            if (remaining < count) return row + 1 + remaining;
            row += count + 1;
            remaining -= count;
        }
        return -1;
    }

    int selectionIndex(int visualIndex) const {
        if (visualIndex < 0) return -1;
        int row = 0, selection = 0;
        const int sections[] = {
            int(_q._results.size()), int(_q._people.size()), int(_q._hits.size())
        };
        for (int count : sections) {
            if (!count) continue;
            if (visualIndex == row) return -1;
            ++row;
            if (visualIndex < row + count) return selection + visualIndex - row;
            row += count;
            selection += count;
        }
        return -1;
    }

    void bind(View &v, int i) override {
        const Entry e = entry(i);
        if (e.type == Type::Header) {
            auto &header = static_cast<Label &>(v);
            header.style().padding(0, 5, 8, 1);
            header.setText(e.section == Section::Conversations ? tr("Conversations")
                             : e.section == Section::People ? tr("People") : tr("Messages"));
            return;
        }

        Row &r = static_cast<Row &>(v);
        r.selectionIndex = e.selectionIndex;
        r.name->setVisible(true);
        r.metaRow->setVisible(false);
        r.snippet->setVisible(false);

        if (e.type == Type::Conversation) {
            const ConvRef c = _q._results[e.dataIndex];
            const auto &st = *_q._tabs[size_t(_q._tab)].store;
            const auto &cv = st.conversation(c);
            const std::string nm = st.displayName(c);
            const bool person = cv.isDirect();
            r.style().height(36).padding(10, 4).spacing(10).items(Align::Center);
            r.avatar->style().size(20, 20);
            r.avatar->setVisible(person);
            r.icon->setVisible(!person);
            if (person) {
                r.avatar->setInitial(nm);
                r.avatar->setBitmap(
                    cv.kind == model::ConvKind::Dm ? _q._avatars.get(st.user(cv.dmUser).avatar, 40)
                                                   : nullptr);
            } else {
                r.icon->setIcon(convIcon(cv));
            }
            r.name->setText(nm);
        } else if (e.type == Type::Person) {
            const auto &st = *_q._tabs[size_t(_q._tab)].store;
            const auto &u = st.user(_q._people[e.dataIndex]);
            r.style().height(36).padding(10, 4).spacing(10).items(Align::Center);
            r.avatar->style().size(20, 20);
            r.avatar->setVisible(true);
            r.icon->setVisible(false);
            r.avatar->setInitial(u.label());
            r.avatar->setBitmap(_q._avatars.get(u.avatar, 40));
            r.name->setText(std::string(u.label()));
        } else {
            const auto &match = _q._hits[e.dataIndex];
            const auto &tab = _q._tabs[match.workspace];
            const auto &hit = match.hit;
            const model::User *user = hit.user != model::kNoUser &&
                hit.user < tab.store->userCount() ? &tab.store->user(hit.user) : nullptr;
            const std::string sender = user ? std::string(user->label()) : hit.sender;

            r.style().height(kMessageRowH).padding(10, 6).spacing(10).items(Align::Center);
            r.avatar->style().size(28, 28);
            r.avatar->setVisible(!sender.empty());
            r.icon->setVisible(sender.empty());
            if (!sender.empty()) {
                r.avatar->setInitial(sender);
                r.avatar->setBitmap(user && !user->avatar.empty()
                                        ? _q._avatars.get(user->avatar, 56) : nullptr);
            } else {
                r.icon->setIcon(Icon::Search);
            }
            r.name->setVisible(false);
            r.metaRow->setVisible(true);
            r.snippet->setVisible(true);

            text::AttributedText metadata;
            const text::Style senderStyle = pxFont(14, text::Weight::Semibold, themed(C::Text));
            const text::Style contextStyle = pxFont(13, text::Weight::Regular, themed(C::TextMuted));
            metadata.append(sender.empty() ? tr("Unknown sender") : sender, senderStyle);
            metadata.append("  ", contextStyle);
            metadata.append(searchConvLabel(*tab.store, hit.conv), contextStyle);
            if (_q._tabs.size() > 1 && !tab.name.empty()) {
                metadata.append("  ·  ", contextStyle);
                metadata.append(tab.name, contextStyle);
            }
            r.meta->setRichText(std::move(metadata));
            r.timestamp->setText(base::formatDateTime(model::tsSecs(hit.ts)));
            r.snippet->setRichText(searchHighlightedPreview(*tab.store, hit.text));
        }
        r.setChecked(e.selectionIndex == _q._current);
    }

    float estimateHeight(int i) const override {
        switch (entry(i).type) {
        case Type::Header: return 24;
        case Type::Message: return kMessageRowH;
        default: return 36;
        }
    }

private:
    Entry entry(int index) const {
        int row = 0, selection = 0;
        const int conversations = int(_q._results.size());
        const int people = int(_q._people.size());
        const int messages = int(_q._hits.size());

        if (conversations) {
            if (index == row) return {Type::Header, Section::Conversations, -1, 0};
            ++row;
            if (index < row + conversations)
                return {Type::Conversation, Section::Conversations,
                        selection + index - row, size_t(index - row)};
            row += conversations;
            selection += conversations;
        }
        if (people) {
            if (index == row) return {Type::Header, Section::People, -1, 0};
            ++row;
            if (index < row + people)
                return {Type::Person, Section::People,
                        selection + index - row, size_t(index - row)};
            row += people;
            selection += people;
        }
        if (messages) {
            if (index == row) return {Type::Header, Section::Messages, -1, 0};
            ++row;
            if (index < row + messages)
                return {Type::Message, Section::Messages,
                        selection + index - row, size_t(index - row)};
        }
        return {};
    }

    QuickSwitcher &_q;
};
QuickSwitcher::QuickSwitcher(
    screens::Context           &ctx,
    Avatars                    &avatars,
    std::vector<ConvRef>        order,
    std::vector<QuickSwitchTab> tabs,
    ConvRef scope
)
    : _ctx(ctx), _avatars(avatars), _tabs(std::move(tabs)), _scope(scope) {
    // Open on the workspace on screen: with no query the list is its
    // conversations, most recent first, exactly what one workspace shows.
    _tab = -1;
    for (size_t i = 0; i < _tabs.size(); ++i)
        if (_tabs[i].store == &ctx.store()) {
            _tabs[i].order = order;
            _tab           = int(i);
        }
    if (_tab < 0) { // the demo, tests: the open workspace alone
        _tabs = {QuickSwitchTab{{}, {}, {}, &ctx.store(), std::move(order)}};
        _tab  = 0;
    }
    const bool multi = _tabs.size() > 1;
    if (_scope != model::kNoConv && searchScopeQuery(ctx.store(), _scope).empty())
        _scope = model::kNoConv;
    _searchPages.assign(_tabs.size(), SearchPageState{});
    style().width(620).padding(10).spacing(8);
    // The tabs first — the card's header when there is a choice to make.
    if (multi) {
        _strip = add<View>();
        _strip->style().row().spacing(6).items(Align::Center);
        for (size_t i = 0; i < _tabs.size(); ++i) {
            const QuickSwitchTab &t = _tabs[i];
            auto                 *b =
                _strip->add<TabBubble>(t.name, t.icon.empty() ? nullptr : avatars.get(t.icon, 80));
            b->onClick = [this, i] { setTab(int(i)); };
        }
        // The current workspace's name, so the letter bubbles don't have to
        // be decoded.
        _tabName = _strip->add<Label>("", Font::Title);
        _tabName->setMaxLines(1);
        _tabName->style().flex(1).margins(4, 0, 0, 0);
    }
    _scopeChip = add<Button>("", Button::Kind::Secondary, Button::Form::Small);
    _scopeChip->style().alignSelf(Align::Start);
    _scopeChip->onClick = [this] { clearScope(); };
    _scopeChip->setVisible(_scope != model::kNoConv);
    if (_scope != model::kNoConv)
        _scopeChip->setLabel("In " + searchConvLabel(_ctx.store(), _scope) + "  x");
    _field = add<TextEdit>();
    _field->setPlaceholder(tr("Search messages, channels or people\xE2\x80\xA6"));
    _field->setMaxLines(1);
    _field->setBackground(C::InputBg, 6);
    _field->setBorder(C::InputBorderFocus);
    _field->style().padding(10, 8);
    _field->onChange = [this] { applyFilter(); };
    // The switcher's keys, whatever the modifiers: Up/Down move (wrapping),
    // Enter opens; with several workspaces ←/→ and Tab/Shift+Tab switch
    // between them instead of moving the caret.
    _field->onKey    = [this, multi](const Event &e) {
        if (e.type != EventType::KeyDown)
            return false;
        if (e.key == plat::Key::Down || e.key == plat::Key::Up) {
            const int n = _rows->selectableCount();
            if (n)
                highlight((_current + (e.key == plat::Key::Down ? 1 : n - 1)) % n);
            return true;
        }
        if (e.key == plat::Key::Enter || e.key == plat::Key::KpEnter) {
            choose(_current);
            return true;
        }
        if (multi && (e.key == plat::Key::Left || e.key == plat::Key::Right)) {
            stepTab(e.key == plat::Key::Left ? -1 : 1);
            return true;
        }
        if (multi && _scope == model::kNoConv && e.key == plat::Key::Tab) {
            stepTab(e.mods & plat::ModShift ? -1 : 1);
            return true;
        }
        return false;
    };
    // The list: conversation suggestions followed by the scrollable message results.
    _rows = std::make_unique<Rows>(*this);
    _list = add<VirtualList>(_rows.get());
    _list->setGap(1);
    _list->style().height(kListH);
    // Pagination belongs to the message stream, which is the final section.
    _list->onScroll = [this] {
        if (!_searching && _searchError.empty() && _list->atEnd() && hasMore())
            loadMore();
    };
    // In the list's place when nothing matches.
    _empty = add<Label>(tr("No results found."), Font::Body, C::TextMuted);
    _empty->setAlign(text::LayoutOptions::Align::Center);
    _empty->style().height(kListH);
    _empty->setVisible(false);
    _status = add<Label>("", Font::Small, C::TextMuted);
    _status->setAlign(text::LayoutOptions::Align::Center);
    _status->setVisible(false);
    _moreButton = add<Button>("Retry search", Button::Kind::Secondary, Button::Form::Small);
    _moreButton->style().alignSelf(Align::Center);
    _moreButton->setVisible(false);
    _moreButton->onClick = [this] { loadMore(); };
    refreshStrip();
    refilter();
}

// Names change with Meta and Users changes only (renames, a member's new
// label): rebuilt then, not per keystroke.
const std::vector<QuickSwitchName> &QuickSwitcher::names(QuickSwitchTab &t) {
    if (t.namesRev != t.store->metaRevision()) {
        t.names    = quickSwitchNames(*t.store, t.order);
        t.namesRev = t.store->metaRevision();
    }
    return t.names;
}

void QuickSwitcher::applyFilter() {
    const std::string query(str::trim(_field->text()));
    if (_scope != model::kNoConv) {
        refreshStrip();
        refilter();
        queueSearch();
        return;
    }
    const int         n = int(_tabs.size());
    _dimmed.clear();
    if (query.empty()) {
        _manualTab = false; // a fresh query starts with a fresh mind
    } else if (n > 1) {
        // Each tab scored once: its best picks the tab, and the one picked
        // is the list (refilter doesn't score it again).
        const std::vector<uint32_t>        q = fuzzyQuery(query);
        std::vector<Scored>                all(static_cast<size_t>(n));
        std::vector<std::optional<double>> scores;
        int                                best = -1;
        for (int i = 0; i < n; ++i) {
            QuickSwitchTab &t = _tabs[size_t(i)];
            all[size_t(i)]    = scoreAll(q, t.order, names(t));
            scores.push_back(bestOf(all[size_t(i)]));
            _dimmed.push_back(!scores.back());
            if (scores.back() && (best < 0 || *scores.back() > *scores[size_t(best)]))
                best = i;
        }
        // Ties keep the tab that is showing; a tab picked by hand holds while
        // it still has something.
        const std::optional<double> &cur = scores[size_t(_tab)];
        if (best >= 0 && cur && *cur == *scores[size_t(best)])
            best = _tab;
        if (best >= 0 && best != _tab && (!_manualTab || !cur))
            _tab = best;
        refreshStrip();
        refilter(&all[size_t(_tab)]);
        queueSearch();
        return;
    }
    refreshStrip();
    refilter();
    queueSearch();
}

void QuickSwitcher::setTab(int index) {
    if (index < 0 || index >= int(_tabs.size()) || index == _tab)
        return;
    _tab       = index;
    if (_scope != model::kNoConv) {
        _scope = model::kNoConv;
        _scopeChip->setVisible(false);
    }
    queueSearch();
    // Picked while a query is up: the user overruling the re-aim. Over an
    // empty field it is just browsing.
    _manualTab = !str::trim(_field->text()).empty();
    refreshStrip();
    refilter(); // changing tab scopes quick navigation, not message results
}

void QuickSwitcher::stepTab(int delta) {
    const int n = int(_tabs.size());
    if (n > 1)
        setTab(((_tab + delta) % n + n) % n);
}

void QuickSwitcher::refreshStrip() {
    if (!_strip)
        return;
    for (size_t i = 0; i < _tabs.size(); ++i)
        static_cast<TabBubble *>(_strip->child(i))
            ->set(int(i) == _tab, i < _dimmed.size() && _dimmed[i]);
    _tabName->setText(_tabs[size_t(_tab)].name);
}

std::string QuickSwitcher::emptyText() const {
    return _empty->visible() ? std::string(_empty->text()) : std::string();
}

QuickSwitcher::~QuickSwitcher() {
    _alive.reset();
    if (_searchTimer) _ctx.app.cancelTimer(_searchTimer);
}


void QuickSwitcher::refilter(Scored *scored) {
    if (_scope != model::kNoConv) {
        _results.clear();
        _people.clear();
        _current = 0;
        _preserveScroll = false;
        _keyboardSelection = false;
        refreshResults();
        return;
    }
    QuickSwitchTab &tab = _tabs[size_t(_tab)];
    _results = scored ? ranked(std::move(*scored))
                      : quickSwitchFilter(_field->text(), tab.order, names(tab));
    const std::string query = utf8::foldCase(str::trim(_field->text()));
    // The popup is a quick picker, not a full conversation directory.
    if (!query.empty() && _results.size() > 4) _results.resize(4);
    _people.clear();
    if (!query.empty()) {
        const model::Store &store = *tab.store;
        // A DM already found above represents its peer. Everyone else is
        // searchable here, including teammates with no existing DM.
        for (model::UserRef u = 0; u < store.userCount(); ++u) {
            const auto &person = store.user(u);
            if (person.deleted || person.label().empty()) continue;
            bool represented = false;
            for (ConvRef conv : _results) {
                const auto &c = store.conversation(conv);
                if (c.kind == model::ConvKind::Dm && c.dmUser == u) {
                    represented = true;
                    break;
                }
            }
            if (represented) continue;
            const std::string names = utf8::foldCase(
                str::concat({person.label(), " ", person.name, " ", person.realName}));
            if (names.find(query) != std::string::npos) {
                _people.push_back(u);
                if (_people.size() >= 3) break; // keep suggestions compact
            }
        }
    }
    _current = 0;
    _preserveScroll = false;
    _keyboardSelection = false;
    refreshResults();
}

bool QuickSwitcher::hasMore() const {
    for (const SearchPageState &page : _searchPages)
        if (page.hasMore || !page.failedCursor.empty())
            return true;
    return false;
}

void QuickSwitcher::refreshResults() {
    const int total = _rows->selectableCount();
    const int rowCount = _rows->count();
    if (_current >= total) _current = 0;
    const float oldOffset = _preserveScroll ? _list->scrollOffset() : 0;
    _list->reset();
    _list->scrollTo(oldOffset);
    _list->setVisible(rowCount > 0);
    _empty->setVisible(total == 0);
    if (total == 0)
        _empty->setText(_searching ? tr("Searching\xE2\x80\xA6") :
                        !_searchError.empty() ? "Search failed. Try again."
                                              : tr("No results found."));
    _status->setVisible(!_searchError.empty() || (_searching && total > 0));
    _status->setText(!_searchError.empty() ? "Search error: " + _searchError
                                        : tr("Searching\xE2\x80\xA6"));
    // Scroll to fetch more when possible. Keep a fallback action when the
    // current result set is too short to scroll, and for failed-page retries.
    const bool retry = !_searchError.empty();
    _moreButton->setVisible(!_searching && hasMore() && (retry || !_list->canScroll()));
    _moreButton->setLabel(retry ? "Retry search" : "Load more results");
}

std::string QuickSwitcher::searchQuery() const {
    const std::string query(str::trim(_field->text()));
    if (_scope == model::kNoConv) return query;
    const std::string scoped = searchScopeQuery(_ctx.store(), _scope);
    return query.empty() ? scoped : scoped + " " + query;
}

void QuickSwitcher::clearScope() {
    if (_scope == model::kNoConv) return;
    _scope = model::kNoConv;
    _scopeChip->setVisible(false);
    applyFilter();
    _field->focus();
}

void QuickSwitcher::queueSearch() {
    ++_searchGeneration;
    if (_searchTimer) {
        _ctx.app.cancelTimer(_searchTimer);
        _searchTimer = 0;
    }
    _hits.clear();
    _pendingSearches = 0;
    _searchPages.assign(_tabs.size(), SearchPageState{});
    _searchError.clear();
    const std::string query(searchQuery());
    _searching = !str::trim(_field->text()).empty();
    _preserveScroll = false;
    _keyboardSelection = false;
    refreshResults();
    if (!_searching) return;
    const uint32_t generation = _searchGeneration;
    std::weak_ptr<int> alive = _alive;
    _searchTimer = _ctx.app.addTimer(250, false, [this, generation, query, alive] {
        if (alive.expired() || generation != _searchGeneration) return;
        _searchTimer = 0;
        std::vector<size_t> targets;
        for (size_t i = 0; i < _tabs.size(); ++i) {
            if (_scope != model::kNoConv && int(i) != _tab) continue;
            const auto &tab = _tabs[i];
            if (tab.backend || tab.store == &_ctx.store()) targets.push_back(i);
        }
        _pendingSearches = targets.size();
        if (!_pendingSearches) {
            _searching = false;
            refreshResults();
            return;
        }
        for (size_t origin : targets) requestPage(origin, "*", generation, query);
    });
}

void QuickSwitcher::requestPage(size_t origin, std::string cursor, uint32_t generation,
                                const std::string &query) {
    model::Backend *backend = _tabs[origin].backend;
    if (!backend && _tabs[origin].store == &_ctx.store()) backend = &_ctx.backend;
    if (!backend) return;
    std::weak_ptr<int> alive = _alive;
    // Copy the retry cursor: moving it into the callback in this call can
    // empty the request argument before searchPage receives it.
    backend->searchPage(query, cursor,
        [this, generation, origin, cursor, alive](
            model::Backend::SearchPage result
        ) {
            if (alive.expired() || generation != _searchGeneration) return;
            receivePage(origin, cursor, generation, std::move(result));
        });
}

void QuickSwitcher::receivePage(size_t origin, std::string cursor, uint32_t generation,
                                model::Backend::SearchPage result) {
    if (generation != _searchGeneration || origin >= _tabs.size()) return;
    const size_t oldMessageStart = _results.size() + _people.size();
    const int previous = _current;
    std::optional<MessageMatch> selected;
    if (_keyboardSelection && previous >= int(oldMessageStart))
        selected = _hits[size_t(previous) - oldMessageStart];
    const bool keepViewport = _list->scrollOffset() > 1;
    const VirtualList::Anchor previousAnchor = _list->anchor();
    std::optional<MessageMatch> visible;
    const int anchorSelection = _rows->selectionIndex(previousAnchor.index);
    if (keepViewport && anchorSelection >= int(oldMessageStart))
        visible = _hits[size_t(anchorSelection) - oldMessageStart];

    SearchPageState &page = _searchPages[origin];
    if (!result.error.empty()) {
        page.failedCursor = cursor;
        page.error        = result.error;
    } else {
        page.failedCursor.clear();
        page.error.clear();
        page.nextCursor           = result.nextCursor;
        page.hasMore              = !result.nextCursor.empty();
        const model::Store &store = *_tabs[origin].store;
        for (auto &hit : result.hits) {
            if (hit.conv >= store.conversationCount()) continue;
            const auto duplicate = std::find_if(_hits.begin(), _hits.end(), [&](const MessageMatch &m) {
                return m.workspace == origin && m.hit.conv == hit.conv && m.hit.ts == hit.ts;
            });
            if (duplicate == _hits.end()) _hits.push_back({origin, std::move(hit)});
        }
        std::stable_sort(_hits.begin(), _hits.end(), [](const auto &a, const auto &b) {
            return a.hit.ts > b.hit.ts;
        });
    }
    _searchError.clear();
    for (const SearchPageState &workspacePage : _searchPages)
        if (!workspacePage.error.empty()) {
            _searchError = workspacePage.error;
            break;
        }
    const auto findMessage = [this](const MessageMatch &saved) {
        for (size_t i = 0; i < _hits.size(); ++i) {
            const auto &now = _hits[i];
            if (now.workspace == saved.workspace && now.hit.conv == saved.hit.conv &&
                now.hit.ts == saved.hit.ts)
                return int(_results.size() + _people.size() + i);
        }
        return -1;
    };
    if (_keyboardSelection && selected) {
        const int newIndex = findMessage(*selected);
        if (newIndex >= 0) _current = newIndex;
    }
    if (_pendingSearches) --_pendingSearches;
    _searching = _pendingSearches != 0;
    _preserveScroll = true;
    refreshResults();
    if (_keyboardSelection) {
        const int visual = _rows->visualIndex(_current);
        if (_list->visible() && visual >= 0)
            _list->scrollToItem(visual, VirtualList::ItemAlign::Nearest, false);
    } else if (keepViewport) {
        int newIndex = previousAnchor.index;
        if (visible) {
            const int relocated = findMessage(*visible);
            if (relocated >= 0) newIndex = _rows->visualIndex(relocated);
        }
        _list->scrollToAnchor({newIndex, previousAnchor.offset});
    }
}

void QuickSwitcher::loadMore() {
    if (_searching || !hasMore()) return;
    const uint32_t generation = _searchGeneration;
    const std::string query(searchQuery());
    std::vector<std::pair<size_t, std::string>> targets;
    for (size_t i = 0; i < _tabs.size(); ++i) {
        if (_scope != model::kNoConv && int(i) != _tab) continue;
        const SearchPageState &page = _searchPages[i];
        if (!page.failedCursor.empty())
            targets.emplace_back(i, page.failedCursor);
        else if (page.hasMore)
            targets.emplace_back(i, page.nextCursor);
    }
    if (targets.empty()) return;
    _pendingSearches = targets.size();
    _searching = true;
    _searchError.clear();
    refreshResults();
    for (const auto &[origin, cursor] : targets)
        requestPage(origin, cursor, generation, query);
}

void QuickSwitcher::highlight(int index) {
    _keyboardSelection = true;
    const int previous = _current;
    _current = index;
    const int previousVisual = _rows->visualIndex(previous);
    const int currentVisual = _rows->visualIndex(index);
    if (previousVisual >= 0) _list->itemsChanged(previousVisual, 1);
    if (currentVisual >= 0) {
        _list->itemsChanged(currentVisual, 1);
        _list->scrollToItem(currentVisual, VirtualList::ItemAlign::Nearest, false);
    }
}


void QuickSwitcher::choose(int index) {
    const size_t conversations = _results.size(), people = _people.size();
    const size_t messageStart = conversations + people;
    if (index < 0 || size_t(index) >= messageStart + _hits.size()) return;
    const QuickSwitchTab &tab = _tabs[size_t(_tab)];
    const std::string key = tab.key;
    const bool here = tab.store == &_ctx.store();
    if (size_t(index) < conversations) {
        const ConvRef c = _results[size_t(index)];
        const auto open = here ? _ctx.openConversation : std::function<void(ConvRef)>();
        const auto in = onChooseIn;
        close();
        if (open) open(c);
        else if (!here && in) in(key, c);
    } else if (size_t(index) < messageStart) {
        const model::UserRef user = _people[size_t(index) - conversations];
        const auto open = _ctx.messageUser;
        const auto in = onChoosePersonIn;
        close();
        if (here && open) open(user);
        else if (!here && in) in(key, user);
    } else {
        const MessageMatch match = _hits[size_t(index) - messageStart];
        const QuickSwitchTab &messageTab = _tabs[match.workspace];
        const std::string     messageKey  = messageTab.key;
        const bool messageHere = messageTab.store == &_ctx.store();
        const auto openMessage = _ctx.openMessage;
        const auto openIn = onChooseMessageIn;
        close();
        if (messageHere && openMessage)
            openMessage(match.hit.conv, match.hit.ts, match.hit.thread);
        else if (!messageHere && openIn)
            openIn(messageKey, match.hit.conv, match.hit.ts, match.hit.thread);
    }
}

} // namespace shell
