// Shared virtualized picker used by Slack channel and people dialogs.
#pragma once

#include "screens/shell/avatars.h"
#include "ui/ui.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace shell {

class BrowseList;

// The list's rows, in a base so they exist before the VirtualList asks the
// adapter for its count. Only the rows on screen are views: a workspace's
// thousands of people cost a screenful of avatars, not all of them.
struct BrowseListData {
    struct Item {
        std::string id, title, subtitle, avatar, badge;
        std::string searchKey;                              // lower-case, what the filter matches
        bool        badgeStrong                    = false; // bold, primary text ("2 new")
        bool        badgeCheck                     = true; // a check mark before it ("In the list")
        // A channel's row: no picture, this icon (Hash / Lock) before the title.
        uint16_t    titleIcon                      = 0xffff;
        bool        operator==(const Item &) const = default;
    };
    struct Rows final : ui::VirtualList::Adapter {
        BrowseListData           *data = nullptr;
        int                       count() const override { return int(data->shown.size()); }
        std::unique_ptr<ui::View> create(int kind) override;
        void                      bind(ui::View &row, int index) override;
        float                     estimateHeight(int index) const override;
    };
    BrowseListData() { rows.data = this; }
    Rows                rows;
    std::vector<Item>   items;
    std::vector<size_t> shown; // indices into items passing the filter
};

class BrowseList : private BrowseListData, public ui::VirtualList {
public:
    using Item                   = BrowseListData::Item;
    static constexpr float kRowH = 60; // every row's height
    // What the members popup does differently from the dialogs.
    struct Options {
        float rowInset = 24;   // the rows' side padding
        bool  wrap     = true; // Up / Down wrap around the ends (else stop there)
        // A row activates on press; else on click, its press washed as
        // strongly as the selection.
        bool  onPress  = true;
        bool  fill     = true; // paints its own background (else its parent's shows)
        bool  initials = true; // the first letter on a picture still missing
    };
    explicit BrowseList(Avatars &avatars);
    ~BrowseList() override;

    void setItems(std::vector<Item> items);
    // The same list with what changed since (an unfiltered one, as the
    // teammate page's): nothing happens when nothing did, and the list stays
    // scrolled where it was.
    void refreshItems(std::vector<Item> items);
    // Rows whose searchKey contains the (trimmed, lower-cased) query.
    void applyFilter(std::string_view query);
    // On the content surface (the teammate page) instead of a dialog's card.
    void setOnContentSurface(bool on) { _onContent = on; }
    void setAvatarRadius(float r) { _radius = r; } // < 0: round (the default)
    void setOptions(const Options &o) { _opt = o; }

    size_t      count() const { return items.size(); }
    size_t      visibleCount() const { return shown.size(); }
    const Item &visibleItem(size_t row) const { return items[shown[row]]; }
    int         selectedRow() const { return _selected; }
    void        setSelectedRow(int row); // -1: none
    void        moveSelection(int delta);
    void        activateSelected();
    // A search field's keys for the list: Down / Up move the selection,
    // Enter opens it (true); any other key is the field's (false).
    bool        navKey(const ui::Event &e);
    std::function<void(const std::string &id)> onActivated;

    void paint(gfx::Painter &p) override;

private:
    friend struct BrowseListData;
    void bindRow(ui::View &row, int index);

    Avatars &_avatars;
    Options  _opt;
    int      _selected  = -1;
    float    _radius    = -1;
    bool     _onContent = false;
};

// The finders' close button (32 px, a 14 px cross).
ui::Clickable *addDialogCloseButton(ui::View *parent);


} // namespace shell
