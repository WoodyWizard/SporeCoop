---
id: BUG-0761
title: tribe_conquer_orders: the raid took the hut to health 0 but the host's game played no conquest cinematic in 60 s (1 of 5 runs)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-08
source: lab 20261108-114435 tribe_conquer_orders (rel/0.7.11 75365a87, lane 4)
role: host
versions: 0.7.11
fixed_in: unreleased
branch: fix/w46-b1-tribe
commits: 555c2ae4
tests: tribe_conquer_orders
signature: tribe_conquer_orders: `ok the raid ended the hut (29 s)` then `FAIL the host's game played the conquest cinematic (None)`; host log without `tribe: the village cinematic 5418B038 plays on the host`
docs: docs/re/tribe.md#tribe-hut-fall
related: BUG-0531
---

## Report
0.7.11 final check (lab 20261108-114435, tribe_conquer_orders, lane 4, warm reset): the NPC tribe formed with its hut,
the raiders (the guest's #15, the host's #16) took the hut from the cut 40 to `health 0/400` in 29 s, both games were
not busy (`busy: -`, no input lock, no modal), and for 60 s the host's game played no cinematic (no `village cinematic
5418B038` line in tribe_conquer_orders.host.game.log; the conquest TRG_TotemPole_Conquer). Everything else passed
(every raider walks on a new order after the fall).

## Root cause
A test problem, not the mod's. Observed (runtime, lane 7, `coop.tribe huthp` now prints `exact=` the unrounded health,
`state=` cCombatant +0x34 and `last=` +0x28):
* The hut falls when the game says so: TakeDamage 0xBFCF10 sets state 2 (and posts the death message) when the health
  goes under 1.5e-5 (Ghidra). Blows of 3.75-4 after the test's cut to 40 leave 0.25-0.5: the test's `health 0/` (the
  rounded print) took the hut as fallen while it stood at state 1 (lab 20261108-153928: "health 0/400 exact=0.50000
  state=1"); the next blow ends it (state 2) and the conquest cinematic follows 0-4 s later.
* The raiders sometimes leave a hut that still stands: on the unchanged test (rel/0.7.12 05f6aa24) lab
  20261108-155202 #1 the hut stood at 26/400 for 60 s with no raider near it ("the raid ended the hut (None)"). With
  the raid ending at a rounded 0 and no raider left to strike the last blow, the hut never reaches state 2 and the game
  plays no conquest: the 0.7.11 failure (20261108-114435: "health 0/400" read after 29 s, no cinematic in 60 s;
  inferred, that run printed no exact health).
* An extra 30 s poll of the hut after the fall (fix/w46-b1-tribe's first diagnostics) made the test miss the cinematic,
  which plays and is closed (8 s, tribe.conquer_caption) during it: those runs (20261108-144555, -153928) are not
  evidence; their hut lines are.

## Fix
tools/tests/coop_tribe_conquer_orders.py: the raid has ended the hut when the host's game has it at state 2 (or gone),
not at a printed health 0; raiders that stop hitting a hut still standing (its health unchanged 16 s after the cut)
are given the raid order again; the wait for the cinematic also prints when the hut goes. mods/coop/tribe.cpp:
`coop.tribe huthp` prints `exact=`, `state=`, `last=`. No mod change (the mod's tribe.conquer_caption only ends a
cinematic that plays).

## Verification
* FAIL (the test before, rel/0.7.12 05f6aa24): out/lab/20261108-155202-tmp-b1tribe-base #1 (lane 7, the raid stalled
  with the hut standing); 0.7.11's out/lab/20261108-114435 (lane 4). (#2 of 20261108-155202 passed.)
* PASS (the test now): out/lab/20261108-155747-fix-w46-b1-tribe #1-#4 (lane 7, 1 cold + 3 warm): state 2 after 35-40 s
  (#2 at exact=0.25 state=1 first), the cinematic 5418B038 0-4 s after it, every raider walks afterwards.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-b1-tribe): a test problem (a hut at 0.25-0.5 taken as fallen, raiders leaving the raid); the test waits for the game's state 2 and sends stalled raiders again; 4 PASS.
