---
id: BUG-0347
title: the watchdog reports a minimized game as a hang (no frames, main thread in Canvas::Tick) and dumps stacks into the player's log
status: open
kind: bug
stage: All
reported: 2026-10-31
source: player reports 2026-10-31 (host and guest logs)
role: 
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0050
---

## Report
Both players' logs (2026-10-31, Windows) hold `WATCHDOG (no frames, stage 'SporeApp::Init done'):
thread ... (main)` with a full stack dump into sporekit.log, right after `frames: Present has not
reached SporeKit for 2 s` and before a "longest frame 23-61 s" perf line. The main thread is in
`App::Canvas::Tick+0x34` -> kernel32 Sleep: the game window was minimized (or the player had
switched away), which docs/06-testing.md describes (Canvas::Tick sleeps 200 ms per tick while the
canvas is minimized).

## Root cause
Observed: the watchdog treats "no frames" as a hang without asking whether the window is minimized.
Harmless for the game, but every alt-tab puts an ERROR line and a stack dump into the player's log
and report, which costs triage time and looks like a freeze.

## Fix
Not done. Proposal: when the frame stall starts while `IsIconic` of the game window (or the
main thread is in Canvas::Tick's minimized sleep), log one INFO line "game minimized, no frames"
instead of the WATCHDOG dump; keep the dump for real stalls.

## Verification
Test to write: minimize a lane's game for 30 s (background mode off) and check the log has no
WATCHDOG line but the INFO line; a real stall (`sk` test hook that blocks the main thread) still
writes the WATCHDOG dump.

## History
- 2026-10-31: opened from the host's and the guest's report zips.
- 2026-11-03: seen in a player report (0.7.3.6 guest, report 20261104-012758): WATCHDOG 'no frames' during 17-38 s gaps with a device Reset after each (minimized / alt-tab). The host meanwhile showed the guest sunk into the ground: BUG-0513; a crash right after such a gap: BUG-0507.
