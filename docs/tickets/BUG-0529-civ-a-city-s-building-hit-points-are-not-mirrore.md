---
id: BUG-0529
title: civ: a city's building hit points are not mirrored: a city the host's vehicles hit stands at full hit points on the guest
status: fixed
kind: desync
stage: Civ
reported: 2026-11-04
source: lab (fix/w41-civ-combat, coop_civ_shots.py)
role: guest
versions: 
fixed_in: 0.7.6
branch: fix/w42-civ-left
commits: d8ea4474
tests: coop_civ_shots.py (civ_shots)
signature:
docs: docs/re/civ.md#civ-hp-shots-turrets
related:
---

## Report
Found while testing BUG-0477 (coop_civ_shots.py, civ_npc): the host's vehicles attack an NPC city for 60 s; the
sum of the city's building hit points (`coop.civ buildings <city>`) falls 5200 -> about 4450 on the host and
stays 5200 on the guest (lab 20261104-015632). A guest gets only the host's ruins (a destroyed building,
reconcile_buildings), not the hit points of a building that still stands. Players see a damaged city whole
in the guest's game until a building falls. How to reproduce: `tools/lab.py run civ_shots`, the line `city hp`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (code + lab 20261104-095130): the MSG_CIV_STATE building rows carry noun, position and a ruined flag, no
hit points (civ.cpp host_tick), and the guest's own game never hurts its buildings (its copies of the host's shots
are cosmetic since BUG-0477): guest 5200, host 4462 after 60 s of fire.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`civ.building_hp`: the host sends the hit points of one city's buildings per snapshot cycle (MSG_CIV_STATE chunk
0xFD, chunks 0; when they changed and every 3 s); the guest lowers its building's hit points with the game's
combatant damage (no attacker) or raises them by setting the field. Ruins stay with reconcile_buildings.
`coop.fix civ.building_hp off`: 0.7.5 behaviour.

## Verification
coop_civ_shots.py (new checks: the guest's city hit points are the host's at 30 s and at the end):
FAIL with SPORE_FIX_OFF=civ.building_hp (host 4462, guest 5200): out/lab/20261104-095130;
PASS (host 4972 / guest 4976 at 30 s, 4462 / 4470 at the end): out/lab/20261104-094742.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): fixed (civ.building_hp).
- 2026-11-04: released in 0.7.6.
