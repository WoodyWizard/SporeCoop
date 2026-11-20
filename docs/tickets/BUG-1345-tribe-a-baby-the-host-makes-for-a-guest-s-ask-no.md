---
id: BUG-1345
title: Tribe: a baby the host makes for a guest's ask (no relayed hut effect) is born in the tribe's species and keeps it until grown up
status: open
kind: bug
stage: Tribe
reported: 2026-11-20
source: lab
role: guest
versions: rel/0.7.16
fixed_in:
branch: fix/w60-tribe
commits:
tests: tribe_birth_ask, tribe_birth_ask_off (twin), tribe_kid_species
signature:
docs:
related: BUG-1299, BUG-1313, BUG-0671, BUG-0824
---

## Report
The 0.7.16 final check (lab 20261120-041304, rel/0.7.16 f6c16890), tribe_kid_species on lane 6: "host: the new member is
of the guest's species when it is first seen (067B3E4A, the guest's 7CBC3253, the tribe's 067B3E4A)" and later "host:
the kid keeps its species while it grows up (067B3E4A, the old 7CBC3253, the new 7C382858)". On the guest's game the
same kid was the guest's creature (7CBC3253) from its first sight: the two games showed another creature for the
guest's new member until it would grow up. Both games first saw it 9.11 / 9.12 s after the guest's click on its hut;
every PASS run of tribe_birth_species / tribe_kid_species saw it after 5.0-7.1 s.

## Root cause
* ✔ (code + timing) The guest's purchase reaches the host two ways (tribe.cpp, BUG-0671 / BUG-0824): the relayed hut
  effect makes the host's game send its own baby message 5.2 s after the click (h_BabyBorn: `bought`, the buyer known,
  `baby_call(stage, p, buyer)`), and the guest's game asks with CMD_BABY. When the host's game made no baby for the
  ask within kBabyPair 4 s (no relayed hut effect reached it, or its message did not come), `baby_tick` makes it with
  `baby_call(stage, k.pos)`: owner 0xFF. birth_begin (tribe.birth_species, BUG-1299) gives the line species only for a
  known owner, so the baby was born in the tribe's slot species and baby_give named the guest its owner right after.
  5.2 s + 4 s = the 9.1 s of the FAIL run.
* ✔ (code) tribe.kid_species (BUG-1313) never changes a kid (SetSpecies of a kid faults on every game), so that kid
  stayed in the tribe's species on the host until grown up; the guest's mirror of it is born in the line species (its
  owner comes with the host's member), hence one creature on the guest and another on the host.
* Before BUG-1313 the pass changed such a kid a moment later (and faulted); with both fixes the late-named kid is the
  case left.

## Fix
`tribe.ask_baby_species` (tribe.cpp baby_tick): the baby made for a guest's unanswered ask is born for that guest
(`baby_call(stage, pos, k.from)`): birth_begin answers the guest's line profile while the game makes it, as for the
relayed purchase. Off: owner named after the birth (the FAIL above).
Test: `coop_own_editors_robust.py birthask` (suite tribe_birth_ask): the world effects relay off on both games, so the
host's game never sees the guest's hut effect and the baby always comes through baby_tick; the birth checks (the new
member first seen in the guest's species on both games). Twin tribe_birth_ask_off (SPORE_FIX_OFF=tribe.ask_baby_species)
must FAIL at "host: the new member is of the guest's species when it is first seen". The test prints the host's
`baby ... made=` counter (baby_tick's babies) so the run shows the path.

## Verification
FAIL (before the fix, the natural path): rel/0.7.16 out/lab/20261120-041304 tribe_kid_species.
Open, waiting for the lab run (2026-11-20 night): tribe_birth_ask must PASS (host `made=1`), tribe_birth_ask_off must
FAIL, tribe_kid_species and tribe_birth_species must PASS.

## History
- 2026-11-20: opened.
