---
id: BUG-0761
title: tribe_conquer_orders: the raid took the hut to health 0 but the host's game played no conquest cinematic in 60 s (1 of 5 runs)
status: open
kind: test
stage: Tribe
reported: 2026-11-08
source: lab 20261108-114435 tribe_conquer_orders (rel/0.7.11 75365a87, lane 4)
role: host
versions: 0.7.11
fixed_in:
branch:
commits:
tests: tribe_conquer_orders
signature: tribe_conquer_orders: `ok the raid ended the hut (29 s)` then `FAIL the host's game played the conquest cinematic (None)`; host log without `tribe: the village cinematic 5418B038 plays on the host`
docs:
related: BUG-0531
---

## Report
0.7.11 final check (lab 20261108-114435, tribe_conquer_orders, lane 4, warm reset): the NPC tribe formed with its hut,
the raiders (the guest's #15, the host's #16) took the hut from the cut 40 to `health 0/400` in 29 s, both games were
not busy (`busy: -`, no input lock, no modal), and for 60 s the host's game played no cinematic (no `village cinematic
5418B038` line in tribe_conquer_orders.host.game.log; the conquest TRG_TotemPole_Conquer). Everything else passed
(every raider walks on a new order after the fall).

## Root cause
Not found. Observed: the same build passed 4 of 4 on lane 2 right after (lab 20261108-140527-rel-0.7.11, 1 cold and 3
warm resets; the cinematic 46-58 s into each test), and the test passed 2 of its last 4 runs on its own branch
(fix/w46-member-stall 20261108-101943 .. -102543). The failing run's "after the fall" rows (both raiders at the hut,
rec=15, the hut alive and listed) have the same shape as the passing ones; NPC tribe members were killed during the raid
in passing runs too; a Drama creature attack started 18 s before the fall here and 1 s before the cinematic in a passing
run (#1), so neither explains it. Inferred: the game's conquest has a condition the test does not control (the hut's
last blow by a member of the player tribe, or the tribe's state when health 0 came from the cut plus few blows);
the test stops polling at the first `health 0`, so whether the game destroyed the hut is not known.
Not a 0.7.11 regression of the mod: the test is new in 0.7.11 (BUG-0531) and tribe.conquer_caption only ends a cinematic
that plays (8 s with guests waiting); it cannot keep one from starting.

## Fix
Not done. Next: log on the host what the game does at the hut's 0 (the hut's destroyed flag, the tribe's state, who hit
last) and let the test wait for `no hut` before it waits for the cinematic, or report "precondition not met" instead
of a FAIL when the hut stands at 0.

## Verification
Open. 1 failure in 5 runs of rel/0.7.11 75365a87 (20261108-114435 FAIL, 20261108-140527-rel-0.7.11 4 x ok).

## History
- 2026-11-08: opened.
