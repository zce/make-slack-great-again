# Runtime architecture and ownership

This document records the current ownership model and the event paths that are
easy to break when changing the Slack client. It describes the implementation,
not an aspirational rewrite.

## Module map

The CMake dependency direction is intentionally narrow:

- `prim/`: small string, UTF-8 and hash primitives shared by platform and app.
- `plat/`: OS boundary — windows, input, timers, event loop and platform services.
- `base/`: files, JSON, logging, time, crypto, secrets and common utilities.
- `net/`: cancellable HTTP requests and WebSockets.
- `gfx/` and `text/`: CPU drawing, image decoding, font discovery and text layout.
- `ui/`: retained view tree, layout, input routing, scrolling and text editing.
- `app/`: workspace model, Slack backend, cache, media, AI summaries and screens.

Application dependencies flow down through `app → ui → text → gfx → base` and
`app → net → base`. Platform access is explicit through `plat`.

## Runtime composition

`src/app/screens/shell/main.cpp` composes the process: platform and UI apps,
network client, remote-image and image caches, context, AI summary service,
shared audio player, window and `Shell`. Normal mode then creates `Accounts`,
wires workspace actions and starts saved workspaces.

### Authoritative state

| Concern | Owner | Notes |
| --- | --- | --- |
| Saved credentials, order and active key | `auth::WorkspaceStore` | Account records, not conversation data. |
| Running workspace | `Accounts::Running` | Owns its `model::Store` and `model::Backend`; Store outlives backend. |
| Conversations, users, messages, unread counts, drafts and thread state | `model::Store` | One per workspace; mutations and observers run on UI thread. |
| Slack API and live events | `slack::SlackBackend` | Read, write and live parts mutate Store; no parallel UI copy. |
| Active workspace routing | `BackendProxy` + `StoreSlot` | Proxy targets active backend; slot targets active Store. |
| Cross-workspace attention and notifications | `Shell` | Accounts attaches all running Stores, including background workspaces. |
| Pages, selection, threads, navigation and notification policy | `Shell` | Owns view state, not message data. |
| Back/forward history | `NavHistory` | Bounded locations, validated on replay and purged on workspace removal. |
| Quick-switch recency | `Settings::visitedAt` + `QuickSwitcher` | Local ranking input, not a second conversation store. |
| Preferences | `shell::Settings` | settings.json; secrets delegated to secret store. |
| Workspace cache | `cache::WorkspaceCache` | Roster, users, metadata and conversation records. |
| Discussion summaries | `llm::Service` + `DiscussionSummary` | Provider transport and prompt construction are separate from Slack model. |

## Important flows

### Workspace startup and switching

1. Accounts reads WorkspaceStore and chooses a Slack workspace.
2. `Accounts::ensure` creates a Running record with its own Store and
   SlackBackend, attaches Store to Shell, opens its cache and connects.
3. Switching changes BackendProxy and StoreSlot targets before screens rebuild.
   The previous workspace remains connected in the background; it is not copied
   into the active Store.
4. Sign-out detaches Store before destroying backend. Cache removal is explicit.

### Slack data and messages

- SlackBackend separates read-side roster/history/polling, write-side mutations
  and live events. Socket Mode is shared across app-key workspaces.
- Backend parsers normalize Slack responses into model types. Store is the
  single source of truth for conversations and messages.
- History/cache restoration uses page/restore mutations. Live events use
  `Store::addMessage`; `Change::isArrival()` distinguishes new arrivals from
  history, cache restoration and backfills.

### Notifications

- Live Store changes reach Shell through attached workspace observers.
- Shell applies workspace, conversation, thread, mute, read-cursor and user
  preferences before notifying.
- Notification deduplication and persisted notification state belong to Shell.
  Opening a permalink or loading old history is not itself a notification.

### Search and navigation

- Conversation quick switcher ranks names locally from active Store using fuzzy
  matching and visit recency.
- Message search is a backend capability (`Backend::search`) and returns
  server results; it is not a second local message database.
- NavHistory validates stale locations on back/forward. Workspace removal purges
  that workspace's locations.

## Asynchronous lifetime contracts

- Store mutations and observers are UI-thread operations. Observers unregister
  before the Store or observer owner goes away.
- `App::post` is not canceled when its owner dies. A posted callback that
  dereferences an owner must carry a lifetime token; use `model::postWhileAlive`
  for this next-turn handoff. Timers and requests instead use owner cancellation.
- `net::Client` owns request workers. Completion/progress callbacks are posted
  to UI thread; cancellation removes pending delivery; destruction stops workers.
- `net::WebSocket` owns its reader thread. Events use a weak implementation
  reference and open epoch; destruction clears owner, aborts transport and joins.
- `OneShotTimers` tracks timer IDs and cancels outstanding timers on destruction.
- Background work uses `model::runInBackground`. Worker closures own their data
  and must not dereference UI/workspace objects. `stopBackground()` suppresses
  result delivery at process shutdown. `waitBackground()` is required when a
  `plat::App` is destroyed while the process continues (as test suites do),
  not as a reason to block normal process exit.
- ImageCache, RemoteImages and Avatars invalidate owners before stopping workers
  or allowing posted results to run. WorkspaceCache::Disk is shared with queued
  writes; a process-wide disk mutex serializes writes with cache wipes.
- Accounts uses per-workspace serials to reject callbacks from dropped workspaces.
  Objects destroyed from their own callbacks are released on a later loop turn.
- Shell owns the Context navigation hooks, Window filters, native hit-test callback
  and installed shortcuts. Teardown invalidates its callback token first, then
  unregisters those hooks before destroying the view tree.

## Fork feature boundary

Removing the old Claude Code integration and voice dictation did not mean
removing Slack-native functionality with similar terminology:

- Slack's **Agents & apps** section and app-provided slash commands remain.
- Slack audio attachments remain playable. Slack-provided transcript/VTT tracks
  can be displayed; this is not microphone capture or speech recognition.
- The retained AI feature is discussion summarization through Anthropic Messages
  and OpenAI-compatible provider wires, not a Claude Code agent runtime.
- Older non-Slack/Claude workspace records are ignored by Accounts and preserved
  in the workspace store. This is data compatibility, not an active integration.
- The upstream self-updater remains optional for compatibility, but
  `MSGA_SELF_UPDATE` is OFF by default and in CI/release/Nix builds.

Regression coverage lives in `tests/app/core/test_slack*`,
`tests/app/core/test_audio.cpp`, `tests/app/llm/test_llm.cpp` and
`tests/app/screens/settings/test_settings.cpp`.

## Memory validation

- Pull requests run `scripts/run-asan.sh 10` under Xvfb, covering the test
  suite and a 10-round demo soak with AddressSanitizer and LeakSanitizer.
- A daily scheduled run and manual dispatch execute `scripts/soak.sh` under
  Xvfb. Manual runs accept a round count (1–100; default 20).
- Workflow logs are uploaded as artifacts for inspection and trend comparison.
  Memory measurements are observational; there is intentionally no hard RSS
  threshold until a stable runner baseline has been established.

## Audit decisions

- No broad Store rewrite was justified: Store is already the single workspace
  data owner, and BackendProxy is an explicit routing boundary.
- Shell is a large UI orchestrator, but major responsibilities already have
  separate translation units and Accounts owns workspace lifecycle. Extracting
  classes without moving a coherent lifecycle would add indirection.
- Workspace cache write queue and disk mutex are intentional: they prevent a
  cache wipe racing a worker write and recreating signed-out data.
- Legacy importer remains because it migrates user settings and caches.
- No dependency was removed solely because its name looked related to a removed
  feature; audio, spell, text and TLS libraries still back retained behavior.
