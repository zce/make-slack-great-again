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

`ci.yml` builds and runs tests for Linux PRs. `release.yml` builds and **publishes a GitHub Release automatically on every push to `master`**, after a successful CI run on `master` and its own release build succeeds. The release tag is `v<upstream MSGA_VERSION>-zce.<release workflow run number>`. The upstream version remains an integer in `version.cmake`; do not invent a second CMake version source.

The first release pipeline produces **Linux x86_64 only**. It uses Ubuntu 24.04's shared libraries, **not** upstream's static-musl release build, so the asset should not be claimed to work without dependencies on every Linux distro. Verify its runtime dependencies on the target distribution before replacing a daily driver. macOS and Windows packaging can be added later using their respective platform runners.

The fork **must never use the upstream in-app updater**: it downloads a binary from `msga.app`, which would silently replace our customized build. `MSGA_SELF_UPDATE` therefore defaults to OFF in the fork; CI and release also set it OFF explicitly. Install future releases from this repository's GitHub Releases page instead.

The release workflow needs `contents: write` access to create tags/releases. Releases are not triggered by upstream changes until the upstream-sync PR is reviewed and merged.

## Customization boundaries

The source tree retains upstream's uncompiled `src/app/claude/` and agent-oriented fixtures to avoid pervasive delete/modify conflicts when synchronizing. The fork does not build or link that module. `src/app/llm/` remains for independent AI assistance.

Prefer existing Settings and palette controls before adding new UI systems. Keep new UI changes inside the corresponding screen or view module rather than broad changes in shared primitives. Remove unwanted integration code in a focused PR after identifying all references; merely hiding an entry does not make the binary smaller. Preserve Slack functionality, credentials, notifications and message routing.

Planned follow-on work (not part of this infrastructure PR):

1. **Removed from the fork build:** Claude Code workspace, roles, process handling, agent UI and linked threads; ordinary Slack bot messages remain.
2. Route external message permalinks to the existing message/thread jump logic with multi-workspace handling.
3. Probe Slack Lists APIs under both session and OAuth credentials before implementing a native Lists UI.
4. Iterate on visual customizations as independent, reviewable changes.
