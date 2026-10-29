---
id: BUG-0104
title: the co-op window keeps showing "Connecting to ..." for the whole session and hides the update line
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#83
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: aa6d418, 1e905bb, 423392b
tests: coop_ui_status.py (suite ui_status)
signature:
docs: docs/06-testing.md#testing-net-w3
related: #83
switch: core.ui_status
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/83 (full text, discussion).

The co-op window has one status string (`g_status`). It is set by the buttons (Join, Send my world, Leave) and is never cleared when the action finishes or fails. The old text then sticks around:

- In the lobby it is shown as the "world line" every time there is no transfer or editor hint. So a guest playing normally reads "Connecting to 1.2.3.4:47810..." under the player list for the entire session.
- On the connect page it wins over the update line. After any join attempt, "SporeCoop X is available", the download percentage and "Update failed: <why>" are no longer shown. The Update / Try again button stays visible next to the unrelated text.

### Where
- Status set, never cleared on success/failure: [coop_ui.cpp#L305](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L305) ("Connecting to ..."), [#L495](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L495) ("Sending the world to everyone..."), [#L499](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L499) ("Left the session")
- Lobby world line falls back to the status: [coop_ui.cpp#L622-L623](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L622-L623)
- Connect page: the status hides the update line, the button stays: [coop_ui.cpp#L589-L594](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L589-L594)
- `ui_world_line` returns "" in normal play: [coop.cpp#L2248-L2276](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2248-L2276)
- Disconnect callback (feed + chat log, not the status): [coop.cpp#L1490-L1493](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1490-L1493)

### How to reproduce
- Join any host (or one that does not answer). Play; open F9: the lobby says "Connecting to <address>...". `coop.uistate` prints `world 'Connecting to ...'`.
- With an update available (`[update] url` pointing at a newer test release), press Join once on an unreachable address and wait for the timeout: the connect page shows "Connecting to ..." and the Update button, but not the "SporeCoop X is available" line.

### Expected
The status describes the current state: cleared (or "Connected to X") once welcomed, replaced by the disconnect or reject reason when the session ends. The update line is never hidden by an old action message.

## Root cause
**What happens**

1. A guest types an address and presses Join. The status becomes "Connecting to 1.2.3.4:47810..." (yellow).
2. The join succeeds and the world loads. `ui_world_line()` is empty while nobody is in an editor, so the lobby shows "Connecting to 1.2.3.4:47810..." under the player list for as long as the session lasts.
3. If the join fails (reject, "no answer from host (timeout)") or the connection is lost later, the window falls back to the connect page and still says "Connecting to ...". The real reason is shown for a few seconds in a notification and goes into the chat log (coop.cpp:1492-1493). The window itself never shows it.
4. From now on the connect page's status line never shows the update line. A player who opens the window to update sees "Connecting to ..." next to an "Update" or "Try again" button and cannot read the download progress or why the update failed.

The host has the same problem: after "Send my world", "Sending the world to everyone..." stays on the lobby world line. After "Leave", "Left the session" is printed over a later update line.

### Confidence
CONFIRMED by reading, low severity. A second reviewer verified it, with one correction: the failure reason is not lost entirely, it reaches a notification and the chat log.

## Fix
coop_ui.cpp: `set_status` takes a lifetime; `coop_ui::session_event(connected, why)` is called by
the session callbacks in coop.cpp: the connected callback clears the status ("Connecting to ..."
ends there), the disconnected callback shows "Disconnected: <why>" (red, 30 s). "Sending the world
to everyone..." and "Left the session" go after 5 s, a "Cannot join" error after 30 s; `tick()`
clears an expired status, so the update line of the connect page comes back. The Update button is
shown only together with the update line, never next to an unrelated message. The update line
does not get its own row (no room in the 600 px window). Switch: `coop.corefix ui_status 0`.

## Verification
`coop_ui_status.py` parts 2 and 4 (read `coop.uistate`: the window's status and lobby world line).
- FAIL: `coop_ui_status.py --without-fix` on lane 2 (switches off), logs `out/lab/manual-w3-net/ui_status_nofix.log` and `ui_status_nofix2.log` (2026-10-28): the status stayed "Connecting to 127.0.0.1:107812..." after the guest was in
  (first log) or kept the old "Cannot join" text (second log), and after the session ended the
  window still showed it instead of the reason.
- PASS: lab runs `out/lab/20261028-005022` and `out/lab/20261028-011423` (lane 2, both PASS): status '' once in, "Disconnected: host closed the session" at the end.
Not run: the update line itself (needs a test release server); covered by the same expiry.

### Related
#8 (join timeout after a lost first packet: the player then sees this stale text), #11 (cleanup on leave).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #83.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: core.ui_status (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
