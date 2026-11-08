# Make Slack Great Again (zce fork)

> **Slack-focused fork.** Claude Code workspaces and agent sessions are not included in this build. This fork follows [upstream](https://github.com/punarinta/make-slack-great-again), with changes isolated for easier synchronization. This README is the canonical guide to the fork's behavior, setup, build, maintenance and releases.

A fast native Slack client built in C++, on its own platform, network and graphics layers: no Electron, no web view, no GUI toolkit.

## About

msga is a Slack client first and foremost. Channels, DMs, threads, reactions, files and notifications all work the way you expect from Slack, in a window that starts instantly and needs a fraction of the memory of the official app. This fork excludes upstream's Claude Code workspace integration.

Everything below the UI is msga's own code: a thin OS layer for windows, input and desktop services (Wayland and X11 on Linux, AppKit on macOS, Win32 on Windows), an HTTP and WebSocket client (WinHTTP on Windows, NSURLSession on macOS, msga's own client over Mbed TLS on Linux), and a CPU renderer with FreeType and HarfBuzz for text. The first goal is a small binary that uses few resources.

## What this fork changes

This fork keeps the upstream native C++ Slack client and concentrates its changes on a Slack-first workflow, more dependable message handling, a cleaner desktop experience and independently maintained releases. The highlights below describe changes made in this fork rather than features inherited unchanged from upstream.

### Slack-focused product scope

- Removed the Claude Code workspace and agent-session integration, including its agent UI, process/worktree handling, approvals, linked agent threads and related voice-dictation code, assets and tests.
- Kept Slack-native messaging features and the independent **Summarize down** capability; removing the agent integration does not remove ordinary Slack bot messages.
- Kept upstream synchronization reviewable: daily sync proposals are opened as PRs rather than applied directly to `master`.

Implementation: [#3](https://github.com/zce/make-slack-great-again/pull/3), [#8](https://github.com/zce/make-slack-great-again/pull/8), [#1](https://github.com/zce/make-slack-great-again/pull/1).

### Navigation and conversation workflow

- Reworked the shell around a more compact single-sidebar layout and integrated navigation/profile actions into the desktop chrome.
- Added dedicated Sidebar handling for custom Sections and Slack Connect **Externals**, with workspace-specific section ordering and collapsed-unread preferences.
- Improved message hover actions and context menus, including clearer **Reply in thread** behavior and returning focus to the existing thread composer when replying inside a thread.
- Added keyboard navigation between unread conversations and a shortcut to jump to the first unread message. A selected conversation remains visible in a collapsed group after it becomes read, until navigation moves elsewhere.
- Fixed followed Threads feed actions by keeping feed-only roots and replies in the canonical Store, without polluting channel history or channel unread/latest state; composer visibility now follows the active-conversation state.
- Reconciled DM unread state after remote reads so badges and unread navigation stay consistent across clients.

Implementation: [#4](https://github.com/zce/make-slack-great-again/pull/4), [#11](https://github.com/zce/make-slack-great-again/pull/11), [#12](https://github.com/zce/make-slack-great-again/pull/12), [#13](https://github.com/zce/make-slack-great-again/pull/13), [#30](https://github.com/zce/make-slack-great-again/pull/30), [#36](https://github.com/zce/make-slack-great-again/pull/36).

### Unified message search

- The title-bar search, `Ctrl+K` and `Ctrl+F` (use `Cmd` on macOS) share one message-first popup. `Ctrl+K` searches globally; `Ctrl+F` starts scoped to the current conversation.
- Added cursor-based Slack message search with independent pagination per workspace, automatic loading as results scroll, retry of the exact failed cursor and support for Slack search operators passed through to the API.
- Kept the quick-search popup compact: conversation and people suggestions appear before message results, with readable context, highlighted matches and one-line previews.
- Search failures are represented as errors rather than being mistaken for an empty result set; asynchronous requests are guarded so stale responses do not replace the active query.

This remains a compact search popup, not a full Slack search center or file-content search interface.

Implementation: [#6](https://github.com/zce/make-slack-great-again/pull/6), [#14](https://github.com/zce/make-slack-great-again/pull/14), [#31](https://github.com/zce/make-slack-great-again/pull/31).

### Message delivery and live-state reliability

- Failed outgoing messages remain visible with their content and recovery actions instead of disappearing when a send fails.
- Definitive failures can be retried; unconfirmed deliveries are marked **Uncertain** and are not automatically resent. This avoids turning a lost response into a duplicate message. Unconfirmed outgoing messages are also preserved across application restarts.
- Improved Activity and Threads refresh behavior with in-context retry feedback and single-flight refresh handling.
- Added explicit **Offline** and **Reconnecting** feedback for the live connection.
- Distinguished genuine live arrivals from history, cache and backfill updates so loading older content or opening an old link does not replay desktop notifications.
- Added RTM typing indicators and richer presence/DND status. In session-sign-in mode, message delivery continues to use polling; the RTM connection is used for presence/typing behavior, not as a replacement message-delivery transport.

Implementation: [#15](https://github.com/zce/make-slack-great-again/pull/15), [#16](https://github.com/zce/make-slack-great-again/pull/16), [#19](https://github.com/zce/make-slack-great-again/pull/19), [#21](https://github.com/zce/make-slack-great-again/pull/21).

### Slack links and message navigation

- Added routing for Slack's native `slack://` channel/user/open links alongside MSGA's `msga://` links.
- Improved permalink parsing and navigation for older messages, thread roots and exact replies, including workspace/channel context, ordered history loading and cancellation of stale navigation requests.
- The command line can open an unmodified Slack HTTPS permalink. MSGA does not take over every HTTPS link in the browser by default.

Implementation: [#2](https://github.com/zce/make-slack-great-again/pull/2), [#9](https://github.com/zce/make-slack-great-again/pull/9), [#33](https://github.com/zce/make-slack-great-again/pull/33).

### Linux desktop integration

- Added Wayland window rounding and themed shadows.
- Added auto-hiding overlay scrollbars.
- Added a selectable UI font family.
- Corrected Linux XDG data/cache directory handling.
- Hardened Linux proxy transport and GNOME settings behavior: HTTP/WS forward-proxy requests, CONNECT for HTTPS/WSS, SOCKS5 tunneling, explicit Manual/None handling, and visible errors for unsupported PAC/WPAD modes. See [Network proxy on Linux](#network-proxy-on-linux) for precedence, supported schemes and limitations.
- Hardened desktop-entry executable-path escaping and XDG runtime-directory validation.

Implementation: [#5](https://github.com/zce/make-slack-great-again/pull/5), [#7](https://github.com/zce/make-slack-great-again/pull/7), [#18](https://github.com/zce/make-slack-great-again/pull/18), [#20](https://github.com/zce/make-slack-great-again/pull/20), [#22](https://github.com/zce/make-slack-great-again/pull/22), [#35](https://github.com/zce/make-slack-great-again/pull/35).

### Independent CI and releases

- Maintains a fork-specific SemVer release sequence independently of upstream's application version.
- CI builds Linux with GCC and Clang and builds the Windows x86_64 production executable with MSYS2/MinGW. The Linux jobs run the test suite; the Windows job validates the portable release build and checks that it has no MinGW runtime DLL dependency.
- Public releases are manually triggered and publish the Linux and Windows production artifacts from a successful CI run for the exact selected `master` commit; checksums are verified before publishing.
- The upstream in-app updater is disabled so it cannot replace the fork build.

Fork releases provide **Linux x86_64** and **Windows x86_64** binaries. macOS packages shown by upstream are not fork releases.

Implementation: [#1](https://github.com/zce/make-slack-great-again/pull/1), [#17](https://github.com/zce/make-slack-great-again/pull/17), [#23](https://github.com/zce/make-slack-great-again/pull/23), [#32](https://github.com/zce/make-slack-great-again/pull/32), [#39](https://github.com/zce/make-slack-great-again/pull/39).

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)
![C++](https://img.shields.io/badge/language-C%2B%2B20-00599C?logo=cplusplus&logoColor=white)
![Release](https://img.shields.io/github/v/release/zce/make-slack-great-again)

## Demo

![msga light](gfx/screenshots/zce-light.png)

![msga dark](gfx/screenshots/zce-dark.png)

![msga in action](gfx/screenshots/recording-1.webp)

## Download

Stable fork versions are published intentionally at [GitHub Releases](https://github.com/zce/make-slack-great-again/releases), using independent `vMAJOR.MINOR.PATCH` version tags. Pull-request builds are available as downloadable **Actions artifacts** on the associated PR's CI run, not as formal releases: Linux provides a test-enabled executable, while Windows provides the portable MinGW executable and its checksum for manual testing. Both PR artifacts are retained for 14 days. See [Maintaining this fork](#maintaining-this-fork) for the release process.

The existing `v0.1.0` release predates Windows support and remains Linux-only. Subsequent releases from this pipeline provide:

- **Linux x86_64:** `msga-linux-x86_64`, dynamically linked against Ubuntu 24.04 libraries (not a static-musl build).
- **Windows x86_64:** `msga-windows-x86_64.exe`, a portable single-file executable built with MinGW and statically linked to its GCC runtime. No installer is provided.

`SHA256SUMS` covers both binaries; `DEPENDENCIES.txt` lists the Linux shared-library requirements. macOS packages shown by upstream are not fork builds. The upstream in-app updater is disabled so it cannot replace our custom version; install new fork builds from this repository's GitHub Releases.

## Connecting to Slack

Grab a [fork release](https://github.com/zce/make-slack-great-again/releases) and you can connect Slack straight away. Click **+** (Add workspace) in the rail to start Slack sign-in. There are two ways to sign in:

- **Slack session (recommended).** Click *Sign in with Chrome* (or Chromium, Brave, Edge or Vivaldi, whichever you have) and log in to Slack the normal way. msga picks the session up from a private, throwaway browser profile and closes the window. There is nothing to register, nothing to build and no tokens to copy, and everything runs on your own account's limits. New messages arrive by polling, as there is no live push in this mode. (Already have the Slack desktop app on Linux? *Import from local Slack* takes its session in one click. You can also paste the session cookie by hand.)
- **Your own Slack app.** Register a free Slack app for live message push (Socket Mode), then paste its client ID, client secret and app-level token into **Settings → System → Slack connection**.

Both are covered step by step in the **[Slack setup guide](https://github.com/punarinta/make-slack-great-again/blob/master/docs/SETUP_SLACK.md)**. Most people want session sign-in, because it needs no setup at all.

## Slack deep links

MSGA accepts Slack message permalinks and the native Slack URI routes for opening workspaces, channels and users.

- **Slack HTTPS permalink:** `https://<workspace>.slack.com/archives/<channel>/p<timestamp>?thread_ts=<root-ts>&cid=<channel>`
- **MSGA message permalink:** the same path under `msga://`, for example `msga://<workspace>.slack.com/archives/<channel>/p<timestamp>?thread_ts=<root-ts>&cid=<channel>`.
- **MSGA thread link:** `msga://thread/<channel>/<root-ts>`.
- **Slack native routes:** `slack://open?team=<team-id>`, `slack://channel?team=<team-id>&id=<channel-id>`, and `slack://user?team=<team-id>&id=<user-id>`. A channel route may also include `message=<timestamp>` and `thread_ts=<root-ts>` to open a specific reply. The same `open`, `channel` and `user` routes are accepted under `msga://`.

A specific reply is identified by its message timestamp; `thread_ts` identifies its parent thread. When a target is not cached, MSGA loads older channel history or the thread before applying the jump. It stops when the exact target is found or the available history is exhausted, and ignores stale navigation requests.

To open an official HTTPS permalink from a terminal, pass it directly to MSGA:

```sh
msga 'https://<workspace>.slack.com/archives/<channel>/p<timestamp>?thread_ts=<root-ts>&cid=<channel>'
```

MSGA does not intercept all HTTPS links in the browser. On Linux, its desktop entry advertises both `msga://` and `slack://`; the OS's default `slack://` handler is not changed automatically. To select MSGA explicitly, first record the current handler, then run:

```sh
xdg-mime query default x-scheme-handler/slack
xdg-mime default msga.desktop x-scheme-handler/slack
xdg-mime query default x-scheme-handler/slack
```

Use the original value from the first command to restore the previous handler. Slack's `slack://app`, `slack://file` and `slack://share-file` routes are not implemented.

## Network proxy on Linux

MSGA automatically uses proxy settings without a separate settings page:

1. Environment variables take precedence: `https_proxy`/`HTTPS_PROXY` for HTTPS and WSS, `http_proxy`/`HTTP_PROXY` for HTTP and WS, then `all_proxy`/`ALL_PROXY` as a fallback. Lowercase takes precedence over uppercase.
2. `no_proxy`/`NO_PROXY` bypasses the proxy for matching hosts, domain suffixes, IP addresses, IP CIDR ranges, and optional ports.
3. If no matching environment proxy is set, GNOME sessions use the current user's GSettings proxy configuration. Manual mode uses scheme-specific endpoints and ignored hosts; **None** is explicitly direct. Automatic/PAC mode and failed settings discovery return an observable proxy error instead of silently bypassing the selected configuration. Non-GNOME sessions use direct connections when no environment proxy applies.

HTTP proxies (`http://127.0.0.1:7890`) and unauthenticated SOCKS5 proxies (`socks5://127.0.0.1:1080`, `socks5h://...`) are supported for HTTP, HTTPS, WS and WSS. Plain HTTP/WS uses the HTTP forward-proxy request form; HTTPS/WSS uses CONNECT. SOCKS5 resolves destination hostnames through the proxy. HTTPS/WSS retain the original destination's TLS certificate validation. Omitted ports default to 80 for HTTP and 1080 for SOCKS5. Proxy failures **do not** silently fall back to direct connections.

Example using a local Mihomo mixed port:

```sh
ALL_PROXY=http://127.0.0.1:7890 msga
```

When started from a desktop launcher, MSGA inherits the desktop session's environment, which may differ from a terminal shell's environment. Environment variables are read per request; GNOME GSettings is read once per process, so restart MSGA after changing GNOME proxy settings. Automatic PAC/WPAD and proxy authentication are not implemented. Other platforms retain their native OS network proxy handling.

## Coming from slk?

[slk](https://github.com/gammons/slk) is a great Slack client if you live in a terminal. If you want the same speed in a desktop window, msga is the closest thing:

- **No Slack desktop app needed.** msga signs in through your browser, so the Electron app doesn't have to be installed to hold a session.
- **Images where you expect them.** Images, animated GIFs and custom emoji are drawn in the window on every platform, whatever your terminal supports.
- **Compact native builds.** The fork publishes a native Linux executable and a single-file Windows executable without bundling an Electron runtime.

The full side-by-side is at **[slk vs msga](https://msga.app/blog/slk-vs-msga/)**.

## Or build your own version

You can build msga yourself, for example to bake your own Slack app keys into the binary. Note that **Slack session sign-in needs no app keys**: it works with a plain build.

### Step 1. Add Slack app keys (optional)

To build in your own Slack app's keys, follow the **[Slack setup guide](https://github.com/punarinta/make-slack-great-again/blob/master/docs/SETUP_SLACK.md)**: create a Slack app with its scopes, Socket Mode and events. The guide ends by writing the keys into `credentials.cmake` (copy `credentials.cmake.example`; the file is gitignored). The same file takes an optional GIPHY key for the GIF picker. Keys pasted in **Settings → System** take precedence over the built-in ones.

The upstream app version lives in `version.cmake` (tracked in git). Fork GitHub Release tags use an independent SemVer sequence; see [Maintaining this fork](#maintaining-this-fork).

### Step 2. Configure build environment

> **Prerequisites:** CMake ≥ 3.21, Ninja, a C++20 compiler (Clang with lld preferred, GCC works too), pkg-config and Python 3. On Windows the compiler is MSYS2's mingw64 GCC; MSVC is not supported.

The configure scripts install and check everything a build needs:

```sh
./scripts/configure-linux.sh    # for Linux (Debian/Ubuntu, apt)
./scripts/configure-mac.sh      # for macOS (Xcode command line tools + Homebrew)
```

- **Linux:** installs the compiler and build tools plus the development packages of FreeType, HarfBuzz, xkbcommon, Wayland, XCB and D-Bus. On other distributions, install the equivalents (the script lists them when apt isn't there). Add `--release` to also install what the release scripts use: Docker (checked, not installed), Xvfb and Wine.
- **macOS:** only cmake, ninja and clang-format come from Homebrew. The app links nothing but the system frameworks, and msga's CMake builds FreeType and HarfBuzz itself.

On Windows, run the PowerShell script from an **elevated** (Administrator) terminal. It uses winget to install Git and LLVM (for clang-format), and installs MSYS2 to `C:\msys64` (or `$env:MSYS2_ROOT`) with its mingw64 GCC, CMake, Ninja and Python:

```powershell
scripts\configure-windows.ps1
```

If you see _"running scripts is disabled"_, enable local scripts first (one-time, machine-wide):

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope LocalMachine
```

Or bypass the policy for a single run without changing the system setting:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\configure-windows.ps1
```

### Step 3. Build and run

```sh
./scripts/build.sh
./build/msga                            # for Linux
./build/msga.app/Contents/MacOS/msga    # for macOS
```

On Windows (needs MSYS2 at `C:\msys64`, or set `MSYS2_ROOT`):

```powershell
scripts\build.ps1
build\msga.exe
```

`build.ps1` copies the GCC runtime DLLs next to `msga.exe`, so it also starts from Explorer. If you see _"running scripts is disabled"_, see the execution policy note in Step 2, or run directly:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

Both build scripts take the same options:

| Option    | What it does                                                                 |
|-----------|------------------------------------------------------------------------------|
| (none)    | Size-optimised (MinSizeRel) build into `build/`                              |
| `--debug` | Debug build into `build-debug/`                                              |
| `--test`  | Also builds and runs the test suites (combines with `--debug`)               |

`--test` stays on in that build directory once you've used it.

### Step 4. Release builds (optional)

`scripts/release.sh` (Linux, macOS) and `scripts\release.ps1` (Windows) make a stripped release-flags build for the machine you're on, always from a clean configure with no tests. They write to `build-release/` and print the per-module size report. `release.ps1` links the exe fully static, like the shipped one.

For the source layout and the size rules, see [src/README.md](https://github.com/punarinta/make-slack-great-again/blob/master/src/README.md).

## Maintaining this fork

This section is the source of truth for fork-specific development, upstream synchronization, CI and release operations. Keep these instructions here rather than maintaining a second fork guide.

### Branches and upstream synchronization

- `master` is the reviewed branch used for releases. Do not commit directly to it; use short-lived `feat/*`, `fix/*` or `docs/*` branches and pull requests.
- `.github/workflows/upstream-sync.yml` checks upstream daily and can also be run manually. It merges `punarinta/make-slack-great-again:master` into `automation/upstream-sync` and opens or updates a reviewable PR. It never updates `master` automatically.
- Merge upstream-sync PRs with a **merge commit**. Do not squash or rebase them: preserving the common upstream ancestry keeps future syncs manageable.
- A merge conflict intentionally fails the workflow and must be resolved by a maintainer. Never force-update `master`.
- The repository setting **Settings → Actions → General → Workflow permissions → Allow GitHub Actions to create and approve pull requests** must be enabled for the scheduled workflow to open PRs.

### CI

The `.github/workflows/ci.yml` workflow runs on pull requests, pushes to `master` and manual dispatches:

- **Pull requests:** Ubuntu 24.04/GCC builds and runs the full test suite; Ubuntu 24.04/Clang performs a compile check. The GCC job uploads a test-enabled Linux executable. The Windows x86_64/MinGW job builds the portable executable, verifies that it has no MinGW runtime DLL dependencies, and uploads it with its checksum for manual testing. Both PR artifacts have 14-day retention; Windows CTest is not run.
- **Master pushes and manual runs:** both Linux compiler jobs run the full test suite. CTest currently starts with two workers; tests sharing global resources must declare `RESOURCE_LOCK` or `RUN_SERIAL`. The Windows job also builds and validates the portable executable.
- **Successful master pushes:** the GCC job makes a separate production build with tests, demo and upstream self-update disabled, then uploads the dynamically linked Linux x86_64 executable, SHA256 checksum and `DEPENDENCIES.txt`. The Windows job uploads `msga-windows-x86_64.exe` with its SHA256 checksum. Both production artifacts have 30-day retention.
- Actions are pinned to reviewed commit SHAs. Keep PR test artifacts and production release artifacts distinct.

### Releases

Public fork releases are **manual**, not an automatic side effect of merging a PR. From Actions → Release → Run workflow on `master`, enter the next version without the leading `v`. The same operation can be started with GitHub CLI:

```sh
gh workflow run release.yml --ref master -f version=0.1.0
```

The optional `ci_run_id` input is an advanced override. The release workflow waits up to **20 minutes** for the successful push-triggered CI run for the exact selected `master` commit. It validates the run's branch, repository and commit, downloads both platform production artifacts from that run, verifies their SHA256 checksums, combines them into one release-level `SHA256SUMS`, and publishes them **without rebuilding**. If CI fails or does not finish within the wait window, no release is published. Existing tags and releases are never silently overwritten.

Fork release versions use an independent, monotonically increasing **SemVer** sequence (`v0.1.0`, `v0.1.1`, `v0.2.0`, etc.). A version such as `v0.2.0-rc.1` is published as a pre-release; stable releases are marked **Latest**. The upstream integer `MSGA_VERSION` in `version.cmake` is retained for runtime compatibility and is **not** the fork release number. Do not change it just to publish a fork release; historical `v44-zce.*` tags remain intact.

The fork release workflow publishes:

- **Linux x86_64:** `msga-linux-x86_64`, dynamically linked against Ubuntu 24.04 system libraries rather than built as a static-musl executable. Check the attached `DEPENDENCIES.txt` when evaluating runtime compatibility on another distribution.
- **Windows x86_64:** `msga-windows-x86_64.exe`, a portable single-file executable built with MSYS2/MinGW and statically linked to its GCC runtime. The release build is checked for MinGW runtime DLL dependencies. No installer or code signing is produced.
- **macOS:** not currently published by this fork's release workflow; upstream macOS packages are not fork releases.

The existing `v0.1.0` release predates Windows support and remains Linux-only. Subsequent releases from the updated pipeline include Windows.

The upstream in-app updater must remain disabled: it downloads from `msga.app` and could replace the customized fork build. CI and release builds explicitly set `MSGA_SELF_UPDATE=OFF`. Install fork updates from this repository's [GitHub Releases](https://github.com/zce/make-slack-great-again/releases). The release workflow requires `actions: read` to download the validated CI artifacts and `contents: write` to publish tags and releases.

### Contribution notes

- Keep fork-specific changes focused and preserve Slack messaging, credentials, notifications and message routing.
- Remove unwanted integrations end-to-end after tracing their contracts, UI, background work, tests and assets; hiding an entry point alone does not remove the integration from the binary.
- The optional **Summarize down** feature is independent of Claude Code agent sessions and voice dictation, and remains supported.

## License

msga is free software under the [GNU General Public License v3.0 or later](https://github.com/punarinta/make-slack-great-again/blob/master/LICENSE). The third-party code it uses is listed in [THIRD_PARTY_LICENSES](https://github.com/punarinta/make-slack-great-again/blob/master/THIRD_PARTY_LICENSES).
