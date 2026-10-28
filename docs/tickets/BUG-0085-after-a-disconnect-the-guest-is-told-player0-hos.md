---
id: BUG-0085
title: after a disconnect the guest is told "player0 (host) is back: your N DNA go to the pack"
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#63
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: aa6d418, 1e905bb, 423392b
tests: coop_ui_status.py (suite ui_status)
signature:
docs: docs/06-testing.md#testing-net-w3
related: #63
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/63 (full text, discussion).

While the host is in the main menu, a guest plays on alone and keeps the DNA it earns ("host away"). If the session then ends (the host quits the game, a timeout, the guest's Leave), the away state is not reset. One second later the guest gets a notification saying the host is back and that its DNA goes to the pack. The host is not back, the name shown is "player0", and no DNA is sent. Severity: minor (a wrong message; the DNA itself stays in the guest's own game).

### Where
- `host_away_tick`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L369-L397
- The session-end paths do not reset `g_host_away` / `g_solo_dna`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1491-L1507 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1761-L1771
- `name_of(0)` without a session is "player0": https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L129-L143

### How to reproduce
1. Two instances, `creature` fixture, host plus guest together.
2. Host: `coop.menu` (leave for the galaxy menu). The guest gets the "left for the main menu" notice.
3. Guest: earn DNA (`coop.dna add 50`).
4. Host: `coop.leave` (or close the game).
5. The guest's notifications show "disconnected: ..." and then "player0 (host) is back: your 50 DNA go to the pack".

### Expected
After a disconnect the guest is told only that the session ended. The away state is cleared without a "back" message.

## Root cause
**What happens**

1. The host goes to the main menu. After 1.5 s the guest sets `g_host_away = true` and feeds "... left for the main menu: you play on alone ...". The guest earns DNA: `g_solo_dna += delta`.
2. The session ends. The disconnected callback clears `g_remotes`, but not `g_host_away`.
3. `host_away_tick` still runs every frame: `change = host_here() || !g_sess.connected() || ...` is true, so after 1 s `away` becomes false.
4. `if (g_solo_dna > 0 && g_sess.connected())` is false (not connected), so nothing is sent, but `feed(host + " (host) is back" + ": your N DNA go to the pack")` still runs, with `host = name_of(0) = "player0"`, and `g_solo_dna = 0`.

### Confidence
CONFIRMED by reading the full path. Not run in game.

## Fix
coop.cpp: `end_session_modules` (every session end) resets `g_host_away`, `g_host_away_since` and
`g_solo_dna`; `host_away_tick` clears the away state silently when the session is gone instead of
taking the "host is back" branch. The DNA earned alone stays in the guest's own game (as before).
Switch: `coop.corefix host_away_reset 0`.

## Verification
`coop_ui_status.py` part 4: host `coop.menu`, guest `coop.dna add 50`, host `coop.leave`; the
guest's log after `disconnected:` must have no "(host) is back".
- FAIL: `coop_ui_status.py --without-fix` on lane 2 (switches off), logs `out/lab/manual-w3-net/ui_status_nofix.log` and `ui_status_nofix2.log` (2026-10-28): "player0 (host) is back: your 50 DNA go to the pack".
- PASS: lab runs `out/lab/20261028-005022` and `out/lab/20261028-011423` (lane 2, both PASS): no such line, `host_away=0`.

### Related
- Umbrella #11.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #63.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
