# Maintaining the zce fork

This repository follows [punarinta/make-slack-great-again](https://github.com/punarinta/make-slack-great-again).
The goal is a small Slack-focused client with local UI customizations, not a rewrite.
Keep changes isolated so that upstream can continue to move.

## Branch and update policy

- `master` contains only reviewed changes and produces releases. Do not commit directly to it.
- `feat/*` and `fix/*` are short-lived branches, merged through PRs.
- `.github/workflows/upstream-sync.yml` checks upstream daily and proposes a merge PR on `automation/upstream-sync`. It never modifies `master`.
- **Merge** upstream-sync PRs with a merge commit; never squash or rebase them. This preserves the common upstream history and avoids recurring giant diffs.
- A conflicted upstream merge fails visibly and requires a human to resolve it. Never force-update `master`.

The initial fork shares its OS app identity, config paths and URL scheme (`msga://`) with upstream. Do not run both variants against the same profile at once. Revisit application identity deliberately if side-by-side installation becomes a requirement.

The repository setting **Actions → General → Workflow permissions → Allow GitHub Actions to create and approve pull requests** must be enabled for the scheduled sync workflow to create PRs using `GITHUB_TOKEN`. Without that setting, the workflow will fail at PR creation.

## CI and releases

`ci.yml` compiles on **Ubuntu 24.04/GCC and Ubuntu 26.04/Clang**. Every PR runs the full test suite with GCC and a Clang compile check. Pushes to `master` (and manual CI runs) run the full suite on both toolchains. CTest runs with two workers initially; tests sharing global resources must use `RESOURCE_LOCK` or `RUN_SERIAL`. The GCC PR job uploads a test-enabled executable as an Actions artifact for manual testing (14-day retention); it is **not** a production binary. Actions are pinned to reviewed commits.

The `i18n_up_to_date` check is intentionally enforced: when translatable UI strings change, run `python3 src/tools/i18n.py update` and commit both the updated `.po` catalogs and generated `languages_generated.cpp` file. Do not disable this check to unblock CI.

On successful pushes to `master`, the GCC job also builds a separate production executable with tests, demo and upstream self-update disabled, then uploads `msga-linux-x86_64-release` with checksums and the dynamic dependency list (30-day retention). This artifact belongs to the exact CI run/commit and is **not** itself a public Release.

**Official fork releases are manually triggered**, not automatically created for every merge. From Actions → Release → Run workflow on **master**, enter the next fork version without the leading `v` (e.g. `0.1.0` or `0.2.0-rc.1`); optionally supply a specific master CI run ID. Example:

```sh
gh workflow run release.yml --ref master -f version=0.1.0
```

The release workflow waits up to **20 minutes** for the `master` push CI run for the exact commit selected by the dispatch to appear and complete. This means you can trigger Release immediately after pushing/merging without waiting for CI yourself. It only accepts a successful CI run for that exact commit, downloads that run's production artifact, verifies its SHA256 checksum, and publishes it without rebuilding. If CI fails or does not finish within the wait window, the release stops without publishing. The optional `ci_run_id` input is an advanced override; it is still validated against the exact commit, branch and successful result. Existing tags/releases are never silently overwritten.

The fork uses an independent, monotonically increasing **SemVer** release sequence: `v0.1.0`, `v0.1.1`, `v0.2.0`, etc. `v0.2.0-rc.1` is supported for pre-releases (not marked Latest). Stable releases are explicitly marked **Latest** on GitHub. The upstream integer `MSGA_VERSION` in `version.cmake` is retained for upstream runtime compatibility and is **not** the fork release number. Do not change it just to create a new fork release. Existing historical `v44-zce.*` tags remain intact. GitHub's Release page ordering is managed by GitHub and may depend on the tagged commit's date; the explicit Latest marker and release naming avoid relying on chronological ordering of historical tags.

The first release pipeline produces **Linux x86_64 only**. It uses Ubuntu 24.04's shared libraries, **not** upstream's static-musl release build. Verify runtime dependencies on the target distribution before replacing a daily driver. macOS and Windows packages listed by upstream are not fork builds.

The fork **must never use the upstream in-app updater**: it downloads a binary from `msga.app`, which would silently replace our customized build. `MSGA_SELF_UPDATE` defaults to OFF and is OFF explicitly in CI and release builds. Install new fork versions from this repository's GitHub Releases. The release workflow requires `actions: read` to access validated CI artifacts and `contents: write` to publish tags/releases. Upstream-sync PRs never publish automatically.

## Customization boundaries

The fork removes Claude-specific backend contracts, session/agent UI, thread watchers, speech-to-text, source, tests and assets rather than carrying unused code. Upstream syncs may need manual conflict resolution for these deletions. The **optional \`Summarize down\` feature remains supported** by \`src/app/llm/\` and the AI Provider settings; it is independent of Claude Code agent sessions and voice dictation.

Prefer existing Settings and palette controls before adding new UI systems. Keep new UI changes inside the corresponding screen or view module rather than broad changes in shared primitives. Remove unwanted integration code in a focused PR after identifying all references; merely hiding an entry does not make the binary smaller. Preserve Slack functionality, credentials, notifications and message routing.

Planned follow-on work (not part of this infrastructure PR):

1. **Removed from the fork build:** Claude Code workspace, roles, process handling, agent UI and linked threads; ordinary Slack bot messages remain.
2. Route external message permalinks to the existing message/thread jump logic with multi-workspace handling.
3. Probe Slack Lists APIs under both session and OAuth credentials before implementing a native Lists UI.
4. Iterate on visual customizations as independent, reviewable changes.

## Search entry points

The title-bar Search control, `Ctrl+K` and `Ctrl+F` (use `Cmd` on
macOS) open the same message-first popup. There is no separate message-area
search overlay. The list groups message-text matches, channel/DM navigation
and people; message hits show the conversation, timestamp and preview.
Results come from Slack `search.messages` across all connected workspaces
and jump directly to the selected message (including thread replies).
Workspace tabs scope conversation/people quick navigation only.

This is a compact search-results preview, not a paginated Slack search page:
the existing backend requests the newest 20 message hits per workspace.
Slack's message-search operators are passed through to the API, but this
does **not** include full Slack file-content search.

On Linux and Windows, the workspace switcher, conversation heading,
star and huddle actions belong to the integrated title bar. The old
sidebar-bottom switcher and standalone member-count button are removed.
macOS retains the upstream unified-title-bar layout for now.

## Slack deep links (Linux)

The fork understands both the upstream MSGA message links and Slack's documented native
deep links. Parsing URLs does **not** change the OS default handler:

- `msga://<workspace>.slack.com/archives/<channel>/p<timestamp>?thread_ts=<root>&cid=<channel>`:
  open a Slack message in the appropriate workspace and thread (when present).
- `msga://thread/<channel>/<root-ts>`: open a thread in the currently selected Slack workspace.
- `slack://open[?team=T...]`, `slack://channel?team=T...&id=C...`,
  `slack://user?team=T...&id=U...`: match Slack's official native URI formats.
  The same `open`, `channel`, and `user` paths also work under `msga://`.
- `msga 'https://<workspace>.slack.com/archives/...'`: open an unmodified
  official Slack permalink from the command line; the fork does **not** intercept all HTTPS links.

`slack://app`, `slack://file` and `slack://share-file` are not implemented
because the corresponding MSGA UI actions are not available yet.

The generated desktop launcher advertises support for `slack://`, but the app
registers and selects **only** `msga://` automatically. To explicitly choose MSGA
as the `slack://` handler after running the fork binary once:

```sh
xdg-mime query default x-scheme-handler/slack
xdg-mime default msga.desktop x-scheme-handler/slack
xdg-mime query default x-scheme-handler/slack
```

Keep the original handler name from the first command to restore the default later.
Closing the official/older MSGA process is necessary when testing a different build
because the application is single-instance. The old and forked builds still share
an application identity.

The `msga://` message link and `slack://` routing may open the target workspace,
channel or thread before its full history arrives; the existing message-list jump
is bounded and may not find distant older messages. Do not claim universal
message-position accuracy until verified against the real Slack history.
