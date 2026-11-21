---
id: BUG-1375
title: Tribe: a guest's copy of a host NPC tribe whose species it lacked waits 5 s after the species came (campaign_civ_jump sync-check t missing)
status: open
kind: desync
stage: Tribe
reported: 2026-11-20
source: lab
role: guest
versions: rel/0.7.16
fixed_in:
branch: fix/w61-civ
commits:
tests: tribe_npc_form, tribe_npc_form_late, tribe_npc_form_late_off (twin), campaign_civ_jump
signature: sync-check: MISMATCH with the host (Tribe ...): t: 5 missing on the guest
docs: docs/re/tribe.md#tribe-copy-species-retry
related: BUG-0381, BUG-0685, BUG-0724
switch: tribe.copy_species_retry
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, campaign_civ_jump, 4 players, lane 2): the lab's sync-check gate
`MISMATCH with the host (Tribe 68AC222A, 73 objects compared): t: 5 missing on the guest (t34 t35 t36 t37 t38)` on
Carol (extra1) at s 824, one mismatch of two checks 3 s apart. The five were the members of an NPC village the host's
game had formed 4.5 s before (owner 254, the leader t34). (The same run's other gate, "2 NPC proxies over 3 m off the
host's NPC (Creature)" on Bob at s 260, is a Creature-stage finding: see BUG-1388.)

## Root cause
Observed (Carol's log): at 13:50:59.221, 40 ms after the host's game formed the village (host: ApplyStamp at
13:50:59.183), Carol's game had no copy: `creations: species 2929D89A of the host's world is unknown here: asked the
host`, `mirror #34..#38 is not made: its tribe has no tribe of this game`; the species came 22 ms later (`registered (3
records)`), but the copy was made only at 13:51:04.333 (`the host's NPC tribe 3 ... made it`, then `mirror #34..#38 is
made again`), 5.1 s after the first try. The village before it (tribe 2, species 2929D89C) took the same 5.1 s
(13:50:50.22 asked, 13:50:55.32 made); tribe 4, whose species Carol had, was copied 50 ms after the host formed it.
In the code (tribe.cpp make_npc_tribes, BUG-0381): one try per host tribe every 5 s; a try that finds the species
missing asks the host (want_species) and waits the full 5 s, though the answer comes in a round trip. The sync-check
compares twice 3 s apart, so a 5 s gap is a mismatch. Players saw a new village's people appear 5 s late on a guest.

## Fix
`coop.fix tribe.copy_species_retry` (tribe.cpp make_npc_tribes): a try that found the species missing is made again
0.5 s later instead of 5 s (want_species asks at most once per 30 s, so this sends nothing more). Off: 5 s (0.7.15).
Test knob `coop.tribe species_miss <n>` (guest): its next n tries read their species as missing (the answer is then
there at once). Test coop_tribe_npc_form.py --species-late: the copy with every member within 3 s of the host's
formation.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL (before, the lab gate): 20261120-133703:campaign_civ_jump (sync-check on extra1 at s 824).
- Open, waiting for the lab run: tribe_npc_form and tribe_npc_form_late must PASS (the copy within about 0.6 s),
  tribe_npc_form_late_off must FAIL (about 5 s); campaign_civ_jump without the Tribe sync-check gate.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-civ): cause from the logs, fix, knob, test and twin; no lab run (owner rule).
