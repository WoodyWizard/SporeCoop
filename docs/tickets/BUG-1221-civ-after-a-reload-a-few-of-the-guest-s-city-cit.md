---
id: BUG-1221
title: Civ: after a reload a few of the guest's city citizens never bind to the host's (5 of 19 walk on their own; civ_citizens warm runs, also v0.7.14.1)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-16
source: final check 0.7.15 (lab 20261116-025154 civ_citizens)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in: 0.7.15
branch: fix/w51-citizens
commits: bc15e41b, 09baa3e9, b9219d1c
tests: civ_citizens_reload (coop_civ_citizens.py --reload --reloads 2 --plant 4), civ_citizens (warm runs)
signature: FAIL the guest's citizens are where the host's are: 40 of 152 without a partner (26%, at most 10 %)
docs: docs/re/civ.md#civ-citizens-reload, docs/06-testing.md#testing-civ-citizens-reload, docs/06-testing.md#testing-final-triage-tribe-civ-0715
related: BUG-0476, BUG-0553, BUG-0475, BUG-1227, BUG-1210
---

## Report
Final check 0.7.15 (lab 20261116-025154, rel/0.7.15, lane 5, the 5th warm run of those games): `FAIL the guest's
citizens are where the host's are: 40 of 152 without a partner (26%, at most 10 %)`: in all 8 rounds 5 of the host's 19
citizens near the centre had no guest citizen within 15 m (the guest's 19 within 160 m). v0.7.14.1 (a detached
worktree of the tag, 0.7.15 tools) 20261116-093217 civ_citizens#4 (warm run 3): exactly the same line. Other runs of the
same commits: fix/w51-t4 20261116-082817-2 (2 cold PASS), -091255 (cold PASS, warm FAIL on the sizes: 4 of 27 pairs,
15%), -092515 (cold + 3 warm PASS); v0.7.14.1 -093217 (cold + 2 warm PASS). So a warm run (the host reloads the world
in the running games, the guest follows) now and then leaves a few citizens of each game without a partner for the
whole test. Players: after the host loads a save in Civ.

## Root cause
Observed (the guest's `coop.wobj` line at the end): failed runs have `kind citizen binds=15 own=4` (final check) and
`binds=17 own=2` (v0.7.14.1): 2-4 of the guest's citizens stay unbound ("own") and 2-5 of the host's citizens are
`missing` at every snapshot (missing 1300-1635 against 0-470 in passed runs).
Observed (fix/w51-citizens, coop_civ_citizens.py now prints the guest's binds for every miss; lab 20261117-121708 #3 and
20261117-123109, the switch below off): after a load the two games' 19 citizens near the capital start at other spots;
the guest's first snapshot binds them first come in the host's id order within the kind's 30 m (`tol`), and the kind
had no relocation, so 2-5 host citizens were left with their free guest citizen 32-76 m away (`after load 1: binds=14
own=5`; `unbound host citizens [2, 9]: the nearest free guest citizen 71 m, 37 m`). Some came within 30 m by
themselves after 20-40 s, others not for the whole test. 1 bad load in about 4 (3 runs of 2 loads: one with 2 and 1
unbound). The citizen serials restart at 2..20 after a load (no id collisions in the fields kind).
Observed: the size failures of the triage's relocation try were no wrong sizes. Every "another size" pair is two
walking host citizens whose guest copies trail them by 3-6 m and cross (`size pair host 12 1.23 / guest 6 1.45: the
guest's is bound to host 16 5.4 m away, size 1.45; the host's copy 2 5.7 m away, size 1.23`), with or without the fix;
0 of 172 standing pairs had another size and `fields civ_citizen differ=0`. The lag is BUG-1227 (open). One pair was the
test's own mistake: it took a guest citizen 0.3 m from another host citizen as the pair of a host citizen 2.5 m away
(the "each the other's nearest" rule checked only the distances, not which host citizen was nearest).

## Fix
* worldobj.cpp, kind `citizen` (switch `coop.fix civ.citizen_rebind`, on): a snapshot's new binds go by nearest pairs
  over all its host citizens without a copy (Kind::pair_nearest), then a host citizen still without one takes the
  guest's nearest free citizen within 120 m (kCitizenRelocate; also by nearest pairs), which is stopped, put at the host's
  place (`citizen_place`: game::stop + game::place) and bound; its size follows from the fields kind civ_citizen
  within 0.25 s. 120 m: a city's citizens stand within ~80 m, the cities hundreds of metres apart (civ_npc 855 m), so
  the guest's other city (BUG-0553) keeps its citizens. Off: first come within 30 m, no relocation (0.7.14). Guest side
  only, no wire change (a 0.7.14 host works).
* Test command `coop.wobj loose <kind> <n> <m>`: the guest drops the binds of its n copies nearest the centre and puts
  them m metres out (the state a bad load leaves).
* coop_civ_citizens.py: `--reload --reloads N` (loads in the running games, the switch off before them when
  SPORE_FIX_OFF names it), `--plant N`, a check per load, miss lines; the size pairs must be each other's nearest.
  Suite entry civ_citizens_reload.

## Verification
* FAIL (switch off, final code 5db61d41): 20261117-132335:civ_citizens_reload (`39 of 304 without a partner (13%)`,
  `after every load: at most 33 of 152 (22%)`, relocs=0).
* PASS (final code 5db61d41): 20261117-131240:civ_citizens_reload (`2 of 304`, `binds=19 own=0` after each load, 34
  relocations); civ_citizens 4 of 4 (1 cold + 3 warm) 20261117-130515 (d23a0407, the same mod); civ_citizen_lod
  (BUG-0553, the guest's other city keeps its citizens) and play_civ --quick PASS 20261117-131240.
* FAIL (switch off, earlier test version): 20261117-123109:civ_citizens_reload (`58 of 304 without a partner (19%)`, `after every load: at
  most 49 of 152 (32%)`; the natural load 1 alone `binds=14 own=5`); natural, no plant: 20261117-121708:civ_citizens_reload#3
  (2 and 1 host citizens unbound for 30-40 s; FAIL on the size check, crossings).
* PASS: 20261117-123516:civ_citizens_reload #1 #2 (`binds=19 own=0` after every load, 8-18 relocations per load).
* Earlier FAIL (the report): 20261116-025154:civ_citizens (rel/0.7.15), 20261116-093217:civ_citizens#4 (v0.7.14.1).
* civ_citizens 4 warm: 20261117-120957 (before the test's pairing fix, 4 of 4 PASS, relocations 5 / 17 / 8 per warm reset).

## History
- 2026-11-16: opened (fix/w51-t4, final-check triage 0.7.15): old (v0.7.14.1 the same), warm runs only; a relocation fix tried and dropped (sizes).
- 2026-11-17: fixed (fix/w51-citizens): nearest-pair binds and a relocation within 120 m (civ.citizen_rebind); the size failures of the first try were walking copies crossing (BUG-1227); civ_citizens_reload with a planted bad load.
- 2026-11-18: released in 0.7.15.
