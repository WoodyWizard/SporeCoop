---
id: BUG-0653
title: Creature: Present does not reach SporeKit for 2 s after a 2.5 s hitch at the game saved box confirmation
status: fixed
kind: bug
stage: Creature
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: unreleased
branch: fix/w46-s1-sweep
commits: 915c0e22
tests: kit_present_hitch (kit_present_hitch.py)
signature:
docs: docs/02-modkit.md#modkit-present-hitch
related: BUG-0347
---

## Report
32 runs on 16 branches, tests chat_pause, team, select, crash_fruit_eat, frames_loop, body_trail_civ, cell_gone_play (not the hooksplit/freeze/pause tests that cause it on purpose): `Present has not reached SporeKit for 2 s (another overlay took it over?)` right after a 2.5 s Creature hitch while the game saved box is confirmed. Example: rel/0.7.8 worktree out/lab/20261105-111032/team.host.game.log:135. Related: BUG-0347.

## Root cause
Observed (code + lab): sporekit/d3d9hook.cpp frame_from_game_loop took the game loop as the frame source when the time since
the last Present was over 2 s. The game saved box hitch is one 2.5 s frame: the first loop pass after it came 2.5 s after that
frame's Present, the fallback ran one frame from the loop and the next Present (1 ms later) switched back
(rel/0.7.8 20261105-111032 team.host.game.log:135-137). `kit.test.hang 2600` gives the same: 3 WARN lines for 3 hitches.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`kit.fix presentlost` (SporeKit fixes.cpp): Present lost also needs 10 game loop passes since the last Present (an overlay that
took Present over leaves 30-60 a second). frames.source loop is not gated. Off: the time alone (SporeKit 0.3.11).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
kit_present_hitch: FAIL with SPORE_FIX_OFF=kit.presentlost out/lab/20261107-114245 (3 "Present has not reached SporeKit" lines after
three 2.6 s frames); PASS out/lab/20261107-114514 (none; frames stay on Present). (20261107-114357: the test passed, the lab's
hitch gate failed on the test's own 2.6 s hitch: the gate is off for this test now.)
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
- 2026-11-07 (fix/w46-s1-sweep): fixed; see Root cause / Verification.
