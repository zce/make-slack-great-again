# Make Slack Great Again (zce fork)

> **Slack-focused fork.** Claude Code workspaces and agent sessions are not included in this build. This fork follows [upstream](https://github.com/punarinta/make-slack-great-again), with changes isolated for easier synchronization. See [fork maintenance](docs/FORK.md).

A fast native Slack client built in C++, on its own platform, network and graphics layers: no Electron, no web view, no GUI toolkit.

## About

msga is a Slack client first and foremost. Channels, DMs, threads, reactions, files and notifications all work the way you expect from Slack, in a window that starts instantly and needs a fraction of the memory of the official app. This fork excludes upstream's Claude Code workspace integration.

Everything below the UI is msga's own code: a thin OS layer for windows, input and desktop services (Wayland and X11 on Linux, AppKit on macOS, Win32 on Windows), an HTTP and WebSocket client (WinHTTP on Windows, NSURLSession on macOS, msga's own client over Mbed TLS on Linux), and a CPU renderer with FreeType and HarfBuzz for text. The first goal is a small binary that uses few resources.

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)
![C++](https://img.shields.io/badge/language-C%2B%2B20-00599C?logo=cplusplus&logoColor=white)
![Release](https://img.shields.io/github/v/release/zce/make-slack-great-again)

## Demo

![msga in action](gfx/screenshots/recording-1.webp)

## Download

Prebuilt fork versions are published at [GitHub Releases](https://github.com/zce/make-slack-great-again/releases). The current fork release workflow builds a Linux x86_64 **dynamically linked** binary; it is not the upstream static-musl binary. Windows and macOS packages listed by upstream are **not** fork builds.

The upstream in-app updater is disabled so it cannot replace this customized build. Install fork updates from GitHub Releases.

## Connecting to Slack

Grab a [fork release](https://github.com/zce/make-slack-great-again/releases) and you can connect Slack straight away. Click **+** (Add workspace) in the rail to start Slack sign-in. There are two ways to sign in:

- **Slack session (recommended).** Click *Sign in with Chrome* (or Chromium, Brave, Edge or Vivaldi, whichever you have) and log in to Slack the normal way. msga picks the session up from a private, throwaway browser profile and closes the window. There is nothing to register, nothing to build and no tokens to copy, and everything runs on your own account's limits. New messages arrive by polling, as there is no live push in this mode. (Already have the Slack desktop app on Linux? *Import from local Slack* takes its session in one click. You can also paste the session cookie by hand.)
- **Your own Slack app.** Register a free Slack app for live message push (Socket Mode), then paste its client ID, client secret and app-level token into **Settings → System → Slack connection**.

Both are covered step by step in the **[Slack setup guide](https://github.com/punarinta/make-slack-great-again/blob/master/docs/SETUP_SLACK.md)**. Most people want session sign-in, because it needs no setup at all.

## Network proxy on Linux

MSGA automatically uses proxy settings without a separate settings page:

1. Environment variables take precedence: `https_proxy`/`HTTPS_PROXY` for HTTPS and WSS, `http_proxy`/`HTTP_PROXY` for HTTP and WS, then `all_proxy`/`ALL_PROXY` as a fallback. Lowercase takes precedence over uppercase.
2. `no_proxy`/`NO_PROXY` bypasses the proxy for matching hosts, domain suffixes, IP addresses, IP CIDR ranges, and optional ports.
3. If no matching environment proxy is set, MSGA reads the current user's GNOME GSettings manual proxy configuration, including its ignored hosts. If neither source specifies a proxy, the connection is direct.

HTTP proxies (`http://127.0.0.1:7890`) and unauthenticated SOCKS5 proxies (`socks5://127.0.0.1:1080`, `socks5h://...`) are supported for HTTP, HTTPS, WS and WSS. SOCKS5 resolves the destination hostname through the proxy. HTTPS and WSS connections always retain the original destination's TLS certificate validation. Proxy failures **do not** silently fall back to direct connections.

Example using a local Mihomo mixed port:

```sh
ALL_PROXY=http://127.0.0.1:7890 msga
```

When started from a desktop launcher, MSGA inherits the desktop session's environment, which may differ from a terminal shell's environment. GNOME GSettings is used when no proxy environment variable applies. Restart MSGA after changing GNOME proxy settings. Automatic PAC/WPAD and proxy authentication are not implemented. Other platforms retain their native OS network proxy handling.

## Coming from slk?

[slk](https://github.com/gammons/slk) is a great Slack client if you live in a terminal. If you want the same speed in a desktop window, msga is the closest thing:

- **No Slack desktop app needed.** msga signs in through your browser, so the Electron app doesn't have to be installed to hold a session.
- **Images where you expect them.** Images, animated GIFs and custom emoji are drawn in the window on every platform, whatever your terminal supports.
- **Even smaller.** 4.3 MB on Linux, 3.8 MB on Windows, a 2.2 MB DMG on macOS. One file each.

The full side-by-side is at **[slk vs msga](https://msga.app/blog/slk-vs-msga/)**.

## Or build your own version

You can build msga yourself, for example to bake your own Slack app keys into the binary. Note that **Slack session sign-in needs no app keys**: it works with a plain build.

### Step 1. Add Slack app keys (optional)

To build in your own Slack app's keys, follow the **[Slack setup guide](https://github.com/punarinta/make-slack-great-again/blob/master/docs/SETUP_SLACK.md)**: create a Slack app with its scopes, Socket Mode and events. The guide ends by writing the keys into `credentials.cmake` (copy `credentials.cmake.example`; the file is gitignored). The same file takes an optional GIPHY key for the GIF picker. Keys pasted in **Settings → System** take precedence over the built-in ones.

The upstream app version lives in `version.cmake` (tracked in git). Fork GitHub Release tags append a fork build number automatically; see [fork maintenance](docs/FORK.md).

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

## License

msga is free software under the [GNU General Public License v3.0 or later](https://github.com/punarinta/make-slack-great-again/blob/master/LICENSE). The third-party code it uses is listed in [THIRD_PARTY_LICENSES](https://github.com/punarinta/make-slack-great-again/blob/master/THIRD_PARTY_LICENSES).
