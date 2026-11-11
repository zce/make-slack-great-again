// Slack conversation, section and workspace context menus.
// Actions go through the Backend; menus are data-first for easy testing.
#pragma once

#include "screens/common/context.h"
#include "screens/shell/sidebar.h"
#include "ui/ui.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace shell {

class Shell;

class Menus {
public:
    // A workspace on the rail. Its menu acts on it by key; `active` is the
    // one the Store holds (its name and mute state are read from there).
    // `store`: its Store while it runs (active or not), else null.
    struct Workspace {
        std::string         key, name;
        bool                muted  = false;
        bool                active = true;
        const model::Store *store  = nullptr;
    };
    // What the accounts controller does (unset: the items show disabled).
    // `key` is the workspace's; "" = the active one.
    struct Hooks {
        std::function<void(const std::string &key)>             changeWorkspaceIcon;
        std::function<void(const std::string &key)>             signOut;
        std::function<void(const std::string &key, bool muted)> muteWorkspace; // saved
    };

    enum Id : int {
        // Conversations (target: ConvRef)
        kStar = 20,
        kUnstar,
        kNotifyAll,
        kNotifyMentions,
        kNotifyMute, // "Mute and hide"
        kMute,       // DMs
        kUnmute,
        kRename, // group DMs
        kLeave,
        kNewSection,        // "Move to section" → "New section…"
        kRemoveFromSection, // "Remove from <section>"
        // The workspace (no target)
        kWorkspaceAdmin = 40,
        kChangeIcon,
        kMuteWorkspace,
        kUnmuteWorkspace,
        kSignOut,
        // A section header in the sidebar (target: the section's id, "" for
        // one of the built-in sections)
        kCreateSection = 80,
        kRenameSection,
        kDeleteSection,
        kSectionUp,
        kSectionDown,
        kSortDefault, // the section's sort (SectionSort)
        kSortAlpha,
        kSortRecent,
        kUnreadsFirst,
        kMentionsFirst,
        kShowUnreadWhenCollapsed,
        kResetSectionOrder,
        // "Move to section" → the n-th section (target: ConvRef)
        kMoveToSection = 200,
    };

    Menus(Shell &shell, screens::Context &ctx, ui::Window &win);
    Hooks hooks;

    std::vector<ui::MenuItem> chatItems(model::ConvRef c) const;
    // The active workspace's menu, or `w`'s (a rail tile's).
    std::vector<ui::MenuItem> workspaceItems() const;
    std::vector<ui::MenuItem> workspaceItems(const Workspace &w) const;
    // A sidebar section header's menu (its More actions): `section` is a
    // custom section's id, "" a built-in section (Channels, Direct
    // messages…); `sort` is the section's, ticked in the menu.
    std::vector<ui::MenuItem> sectionItems(
        const std::string &section, SectionSort sort,
        std::optional<std::pair<bool, bool>> localMoves = {},
        std::optional<bool> showUnreadWhenCollapsed = {},
        bool                canResetOrder = false
    ) const;
    void                      runSection(int id, const std::string &section);
    // The sort a sort item picks (`sort` unchanged for any other item).
    static SectionSort        sortAfter(int id, SectionSort sort);
    // `target` is the ConvRef the item's menu was built for (0: workspace).
    void                      run(int id, uint32_t target);

    void showChat(model::ConvRef c, ui::PointF at);
    void showWorkspace(ui::PointF at);
    void showWorkspace(const Workspace &w, ui::PointF at);
    // Local ordering is separate from Slack's own custom-section order.
    void showSection(
        const std::string               &section,
        SectionSort                      sort,
        ui::PointF                       at,
        std::function<void(SectionSort)> onSort,
        std::pair<bool, bool>            canMove,
        std::function<void(int)>         onMove,
        bool                             showUnreadWhenCollapsed,
        std::function<void()>            onToggleCollapsedUnreads,
        bool                             canResetOrder,
        std::function<void()>            onResetOrder
    );

private:
    ui::Menu *show(std::vector<ui::MenuItem> items, uint32_t target, ui::PointF at);
    void      sectionMoves(std::vector<ui::MenuItem> &items, const model::Conversation &cv) const;

    Shell            &_shell;
    screens::Context &_ctx;
    ui::Window       &_win;
    Workspace         _ws; // the workspace menu's (key "" = the active one)
};

} // namespace shell
