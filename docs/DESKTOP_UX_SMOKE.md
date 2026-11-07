# Desktop UX regression matrix

Use this as the short manual complement to the automated shell, Store and Slack-backend tests. Run it against a real desktop build and a real workspace; a headless test cannot validate compositor clipping, native window decorations or actual font availability.

## Navigation and page shell

- Open a channel, a DM, Activity, Threads, Saved messages and Scheduled messages. Each page should have one title in the titlebar, its page actions in the intended titlebar area, and no duplicate content header.
- Use Back/Forward buttons, keyboard shortcuts and mouse side buttons across conversations and overview pages. Repeat across two workspaces, including a workspace switch while a page is open.
- Test profile menu, workspace picker, quick links, Sections and Externals. Verify selection and unread markers after each transition.
- Repeat with the sidebar expanded and collapsed. Use long lists, a selected unread row, collapsed sections with unread items, and scroll to the final item. Check symmetric hover insets and bottom padding.

## Message interactions

- Test a top-level channel message, a DM message, a thread root and a thread reply from both the conversation view and Threads page.
- For each, use hover toolbar, right-click and the keyboard context-menu key. Verify hit targets, focus, dismissal with Escape and that no thread reply offers a nested Reply in Thread action.
- Check ownership-dependent actions with your own message and another user's message. Exercise reaction, copy link/text, save, pin and forward where available; unavailable actions must not appear or must be clearly disabled.
- Switch pages or workspaces while a menu or picker is open. No stale popup, selection or focus should remain.

## Window, scaling and typography

- On Wayland, test normal windowed, resize, maximize, restore and fullscreen. Check rounded frame, outer shadow, transparent corners, child clipping and popup/drop-shadow layering. Fullscreen/maximized states should remove only the outer frame rounding; the content surface keeps its own radius and inset.
- Repeat on each supported native window backend available (Windows/macOS) and record platform-specific differences rather than assuming Wayland behavior applies.
- Check 100%, 150% and 200% UI scaling where available, including a runtime monitor/scale change. Inspect titlebar, sidebar, message rows, menus, composer and popups for clipping, uneven density or layout jumps.
- Select an installed UI font, restart and verify persistence. Then select an unavailable font (or simulate its removal) and verify a safe fallback without blank text or startup failure.
- Repeat in light and dark themes.

## Scrollbars and long content

- Leave each scroll area idle until its overlay scrollbar disappears; scroll by wheel/touchpad and verify it reappears without changing layout.
- Hover the scrollbar, drag its thumb, click its track, then move across adjacent rows. The bar must remain usable without covering row actions or intercepting nearby content clicks.
- Test the sidebar, long channel history, thread panel and long overview pages at the bottom and near the right edge.
