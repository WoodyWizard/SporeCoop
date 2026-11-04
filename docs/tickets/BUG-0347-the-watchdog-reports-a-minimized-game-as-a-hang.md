---
id: BUG-0347
title: the watchdog reports a minimized game as a hang (no frames, main thread in Canvas::Tick) and dumps stacks into the player's log
status: fixed
kind: bug
stage: All
reported: 2026-10-31
source: player reports 2026-10-31 (host and guest logs)
role: 
versions: 0.7.3.1
fixed_in: 0.7.5
branch: fix/w41-infra
commits: 57aa4198, d7168d32
tests: kit_watchdog
signature:
docs: docs/02-modkit.md (Diagnostics, watchdog)
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

Observed in the lab (lane 6, kit_watchdog with the switch off, out/lab/20261104-030945): the window minimized
(`kit.test.window minimize`, IsIconic 1) stops SporeKit's frames (66 -> 66 over 4 s) while App::Canvas::Tick goes on,
and 30 s later the watchdog wrote `WATCHDOG (no frames, stage 'SporeApp::Init done'): thread ... (main)` with the
stack. The hang report itself was already held back by its IsIconic / focus check (diag_hang_report); the dump
was not. A hidden (SW_HIDE) Wine window is no reproduction: the game goes on presenting there (~80 000 frames/s
without the frame cap, see docs/06-testing.md#testing-g1-infra).

## Fix
SporeKit (sporekit/watchdog.cpp, gamehooks.cpp): the App::Canvas::Tick hook counts the game loop's passes
(`watchdog_loop_tick`). While they go on, no frames is a pause (minimized, unfocused without background mode, a
lost device after an alt-tab): one INFO line `watchdog: no frames for N s, the game loop runs (window minimized
1, visible 1, foreground 1): the game is paused or not drawing, not a hang`, no stack dump, no hang report, and
`watchdog: frames again after N s` when they come back. A stall is counted from the later of the last frame and
the last loop pass, so a blocked main thread (no frames, no loop) still dumps and writes the hang report after
30 s. Switch: `kit.fix hangloop 0` (the old watchdog). Test commands: `kit.test.window
minimize|restore|hide|show`, `kit.test.hang [ms]` (blocks the main thread on its next frame).

## Verification
`kit_watchdog` (tools/tests/kit_watchdog.py, one game): minimize 40 s -> no WATCHDOG 'no frames' dump, the INFO
line, no hang report, frames again after the restore; `kit.test.hang 40000` -> the WATCHDOG dump and the hang
report, frames again afterwards.
- FAIL with `SPORE_FIX_OFF=hangloop`: out/lab/20261104-030945 (minimize: the WATCHDOG dump, no INFO line; hang PASS)
- PASS: out/lab/20261104-031138 (minimize and hang)

## History
- 2026-10-31: opened from the host's and the guest's report zips.
- 2026-11-03: seen in a player report (0.7.3.6 guest, report 20261104-012758): WATCHDOG 'no frames' during 17-38 s gaps with a device Reset after each (minimized / alt-tab). The host meanwhile showed the guest sunk into the ground: BUG-0513; a crash right after such a gap: BUG-0507.
- 2026-11-04: fixed on fix/w41-infra (kit.fix hangloop); SporeKit changed, no version bump (the orchestrator).
- 2026-11-04: released in 0.7.5.
