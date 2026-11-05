---
id: BUG-0313
title: GOG 2024 guest: game time paused for ~7 s after its world load in Tribe (black screen)
status: game-bug
kind: bug
stage: Tribe
reported: 2026-10-30
source: BUG-0175 split (fix/w13-net)
role: guest
versions: 0.7.2.4 and later (GOG 2024 build)
fixed_in:
branch: fix/w43-transitions
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-game-time-pauses
related: BUG-0175, BUG-0444, BUG-0576
---

## Report
Split from BUG-0175 (2026-10-30, fix/w13-net), first seen 2026-10-24 (fix/gog2024-sync): after its
world load in the Tribe stage the GOG 2024 guest keeps its game time paused (cGameTimeManager
+0x48 = 3, black screen, ~23 fps) for ~7 s; the GOG 2016 guest is already running when the
test's start() returns. Citizens get no `Update` meanwhile (`coop.tribe` on the guest:
`ai_skipped=0/0`). Lab 20261030-133141 (lane 10, GOG 2024): "game time running 20.1 s after the
start" in Tribe (3.1 s in Civ). Players see a black screen then, so no mirror error is visible;
what keeps the 2024 game paused (planet still loading?) is not known.
Reproduce: `SPORE_LANE=10 python3 tools/tests/coop_sync.py tribe` prints the seconds until both
games run.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (lane 5, GOG 2024, 2 runs, `coop.pauses` every 0.3 s from the end of twoinst.start): the guest's game
  stands with the single pause reason **0x4BF38A7** for 7.6 s and 8.7 s, its screen black (only the mod's labels and
  notices), the host's game has no pause. Then the reason is gone and the guest plays.
* Observed (Ghidra, GOG 2024): it is the Tribe stage's own entry. The mode-change handler 0xCDEE00 (vtable
  0x1477578) pauses 0x4BF38A7 on entering Tribe; the entry state machine 0xCDE7E0 (cTribeGame +0x22C) resumes it in
  its state 7, after state 6 waited with the renderer's layer 0x16 off until `IBakeManager` has nothing queued and
  the model world nothing pending: every creature model must be baked first.
* Inferred: a guest that just received the host's world bakes the host's creations then (the host's own game had
  them baked long before); the GOG 2024 lanes bake slower than the GOG 2016 ones. No code of the mod pauses here.

## Fix
None: the game's own loading wait. The guest's mirrors are kept on the ground meanwhile by `tribe.mirror_ground`
(BUG-0444); tests that need a running guest wait for it (`coop.pauses` shows the reason). `coop.pauses` (new, tests)
lists every pause reason the game counts.

## Verification
no-fail-run: the game's own loading wait, no fix (seen on lane 5 with `coop.pauses`, scratch runs at 22:37 and 22:40).

## History
- 2026-10-30: opened.
- 2026-11-04 (fix/w43-transitions, T1): the pause reason found (0x4BF38A7, the Tribe entry waiting for its bakes): game-bug.
