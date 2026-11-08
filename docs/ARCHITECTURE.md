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
- QuickSwitcher keeps each workspace's next cursor, failed cursor, error and
  pagination flag together in `SearchPageState`. A query generation rejects
  stale responses; starting a new query resets all workspace page states.
- Before closing a popup, navigation copies the selected result, workspace
  key and outgoing callbacks. Closing can destroy the popup or change the
  active workspace; no reference into its tab/result storage may survive it.
- A person pick delegates DM creation to Shell's `Context::messageUser` hook.
  The request outlives the picker, not Shell; its result may navigate only
  while the originating Store is still active.
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
- Lifetime flags used by `postWhileAlive` are UI-thread state, not cross-thread
  synchronization. Background workers return owned results to the UI thread
  before a callback reads an owner's flag.
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
- TokenDeriver has one request at a time. A replacement run cancels delivery
  of the previous request and resets its candidates, results and error. Its
  completion reports the last error only when no workspace was validated.
- Shell exclusively owns the Context navigation hooks, Window event callback
  and filters, native hit-test callback and its installed shortcuts. Teardown
  invalidates its callback token first, then unregisters those hooks before
  destroying the view tree. The caller's close handler and unrelated shortcut
  registrations remain intact. Replacing Shell-owned hooks while it is active
  is not a supported ownership model.

## Fork feature boundary

Removing the old Claude Code integration and voice dictation did not mean
removing Slack-native functionality with similar terminology:

- Slack's **Agents & apps** section and app-provided slash commands remain.
- Slack audio attachments remain playable. Slack-provided transcript/VTT tracks
  can be displayed; this is not microphone capture or speech recognition.
- `plat::audio::Recorder` remains a tested API of the standalone platform layer,
  not a client feature: no application code invokes it, and playback already
  needs the shared audio dependencies. The client bundle declares no microphone
  usage or audio-input entitlement; adding capture would require an explicit
  product and permission change, not simply wiring this platform API.
- The retained AI feature is discussion summarization through Anthropic Messages
  and OpenAI-compatible provider wires, not a Claude Code agent runtime.
- Older non-Slack/Claude workspace records are ignored by Accounts and preserved
  in the workspace store. This is data compatibility, not an active integration.
- The upstream self-updater remains optional for compatibility, but
  `MSGA_SELF_UPDATE` is OFF by default and in CI/release/Nix builds.
- Some generic UI/model extension points still exist: composer history and
  suggestions, read-only reasons, and elapsed typing status. No production
  Slack backend supplies the former agent runtime. Removing these interfaces
  is a separate contract reduction, not required to remove its processes,
  credentials, assets or dependencies; they must not be mistaken for a working
  Claude Code integration. The obsolete Claude cache-import declaration is gone.

Regression coverage lives in `tests/app/core/test_slack*`,
`tests/app/core/test_audio.cpp`, `tests/app/llm/test_llm.cpp` and
`tests/app/screens/settings/test_settings.cpp`.

## Implementation seams

- `slack_backend_core.cpp` owns shared request dispatch, token-refresh routing
  and deferred auth-loss delivery. Read, write and realtime code use the same
  dispatch path.
- `shell_notify.cpp` owns notification policy, deduplication, click routes and
  their persistence. Its numbered writes are serialized so older background
  work cannot overwrite a newer notification snapshot.
- `llm/wire.{h,cpp}` shapes protocol requests and parses provider responses;
  `llm/server_url.{h,cpp}` owns custom-server URL normalization and the
  cleartext-key warning policy. Neither changes summary instructions.
- `llm/discussion_summary.cpp` bounds and formats the transcript separately
  from prompt assembly. Prompt wording and transcript truncation are part of
  the existing behavior, not cleanup opportunities to change implicitly.
- `tests/support/fake_slack_server.{h,cpp}` owns the fake-server controls and
  request-log snapshots shared by the Slack suites. Returned `json::Value`s
  borrow their snapshot's document. Asynchronous sign-in regressions live in
  `test_slack_auth.cpp`, separate from signed-in backend feature tests.

## Memory validation

- Pull requests run `scripts/run-asan.sh 10` under Xvfb, covering the test
  suite and a 10-round demo soak with AddressSanitizer and LeakSanitizer.
- A daily scheduled run and manual dispatch execute `scripts/soak.sh` under
  Xvfb. Manual runs accept a round count (1–100; default 20).
- Workflow logs are uploaded as artifacts for inspection and trend comparison.
  Memory measurements are observational; there is intentionally no hard RSS
  threshold until a stable runner baseline has been established.
- `MSGA_ASAN` selects the allocator, leak-check and graceful-shutdown diagnostic
  paths for both GCC and Clang; GCC-specific compiler macros must not gate them.
- Idle-worker tests track the actual worker thread IDs, not equality with a
  process-wide count: Linux can briefly list a joined thread in `/proc/self/task`.
  An unrelated thread retiring must neither cause a false failure nor hide a
  worker that is still alive. Production Worker Pool retirement is unchanged.

## Audit decisions

- No broad Store rewrite was justified: Store is already the single workspace
  data owner, and BackendProxy is an explicit routing boundary.
- Shell is a large UI orchestrator, but major responsibilities already have
  separate translation units and Accounts owns workspace lifecycle. Extracting
  classes without moving a coherent lifecycle would add indirection.
- Read-side Slack state remains one private `Read` implementation. The retry
  queue, roster loads, polling and thread baselines deliberately share its
  cancellation scope. Splitting that state into independently owned backends
  would weaken the workspace invariant; a future translation-unit split should
  keep this owner and avoid exposing its state outside the Slack implementation.
- Workspace cache write queue and disk mutex are intentional: they prevent a
  cache wipe racing a worker write and recreating signed-out data.
- Legacy importer remains because it migrates user settings and caches.
- No dependency was removed solely because its name looked related to a removed
  feature; audio, spell, text and TLS libraries still back retained behavior.

## Build and release integrity

- Normal Linux CI runs independent GCC and Clang build trees; Windows builds
  with MSYS2/MinGW. Test processes isolate HOME/XDG state, and the fake Slack
  and LLM servers bind process-specific loopback ports. Sanitizer validation
  may limit CTest parallelism without changing ordinary CI's four workers.
- Release first checks out the dispatch commit, then validates a push-to-master
  CI run's workflow path, repository, branch and exact commit SHA. A display
  name alone is not identity. Only a completed successful run supplies the named
  production artifacts; waiting, download or checksum failures block publication.
- Release dispatches share a non-cancelling concurrency group. Preview artifacts
  are never used as production artifacts. Upstream synchronization pushes only
  a proposal branch with a lease, opens a PR, and explicitly dispatches CI;
  it never merges into master or publishes a release.
