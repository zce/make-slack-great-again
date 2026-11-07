# Refresh, connectivity and deep-link smoke test

Automated coverage is limited to parser tests, fake backend/page tests, and local transport tests. It **does not** simulate a real browser's external protocol handler or authenticate to Slack.

## Desktop / live workspace

Use two signed-in workspaces A and B with different hostnames. In each, select a channel with a root and a reply and note the exact IDs and timestamps.

1. From a browser, open a Slack HTTPS permalink to a root in A. Confirm the app activates A and scrolls to the root.
2. Open a permalink to a reply containing `thread_ts=<root>` and optionally `cid=<channel>`. Confirm the thread opens with the **reply** selected, not only the root.
3. Use `./msga 'https://<workspace>.slack.com/archives/<channel>/p<timestamp>?thread_ts=<root>&cid=<channel>'` to isolate URL routing from the browser launcher.
4. Open a matching `slack://channel?team=<team>&id=<channel>&message=<reply>&thread_ts=<root>` URL and an `msga://channel?... ` alias; verify the same target.
5. While A is displayed, open a link targeting B and confirm workspace switches before resolving the message. Repeat from B back to A.
6. Check a nonmember private channel, a deleted/missing message, an unknown workspace host, a bogus timestamp, and a malformed link. Confirm no cross-workspace misrouting or unexpected navigation. Missing messages may only be diagnosed after the server answers.
7. Close and relaunch the app and repeat the HTTPS-to-browser-to-desktop hand-off. Confirm desktop registration and single-instance forwarding under the actual Linux desktop environment.

## Overview refresh and connectivity

1. Open Activity, Threads, Saved messages and Scheduled messages. Click the titlebar refresh; confirm new content appears without page layout changes.
2. Throttle the network; double-click Refresh. Confirm one request in flight and the icon returning to normal after success/failure.
3. Populate Activity/Threads, then disconnect. Refresh and confirm existing cards remain, an in-page error plus Retry is visible, and Retry succeeds once connectivity returns.
4. Switch overview tabs while a request is in flight. Confirm the prior response cannot overwrite the current tab. Switch workspaces while a request is in flight and confirm old workspace data never appears.
5. Disconnect the OS network and verify Offline; restore and verify it disappears. For Socket Mode, drop the WebSocket, verify Reconnecting after an established connection, then verify recovery. A single failed HTTP API request must not show Offline.
6. Confirm drafts remain editable while offline, no new recurring network requests are introduced, and socket reconnection still follows the existing backoff logic.

CI jobs GCC / Clang / CTest and Linux Preview are independent of these live checks. Do not interpret parser or headless tests as browser/desktop E2E coverage.
