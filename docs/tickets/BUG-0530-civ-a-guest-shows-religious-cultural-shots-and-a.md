---
id: BUG-0530
title: civ: a guest shows religious (cultural) shots and AoE areas the host's game does not make (noun_only_guest after BUG-0477)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-04
source: divergence detector, play_civ (fix/w41-civ-combat, out/lab/20261104-024331)
role: guest
versions: 
fixed_in: 0.7.6
branch: fix/w42-civ-left
commits: b4a680fc d8ea4474 b174d8f7 8c7a9834 dc00c3e4 fe6facb2 4bd2a742 cfd1283f 4ffaa3a7 5e1be237
tests: coop_civ_religious.py (civ_religious), coop_civ_aoe.py (civ_aoe)
signature:
docs: docs/re/civ.md#civ-hp-shots-turrets
related: BUG-0477, BUG-0538
---

## Report
After BUG-0477's shot relay (civ.shots) the detector in play_civ (out/lab/20261104-024331) has
`Civilization:noun_only_guest:cultural_projectile` and `noun_only_guest:default_ao_e_area` in 29 % of the
comparisons (the host's whole list has none, the guest's one or more), and in the regression run before the last
change 16 % (out/lab/20261104-020931). The military shots match (civ_shots: 98 guest / 102 host projectiles).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab, fix/w42-civ-left): not extra shots, but late ones.
* `cToolManager::FireTool` returns a bool (al): true only when the refire time passed and WhileFiring made the shot
  (0x0105030B). The host relayed every call (35 of 3474 made a shot in civ_shots), yet the guest's weapon refused
  the extra ones on its own refire time: a religious attack gave 126 / 126 shots with the every-call relay
  (out/lab/20261104-095320).
* A guest's game fires no tool of its own in Civ (a hook on FireTool calls outside the relay logged none in play_civ),
  and the copies made as many shots of the same weapon as the host's (play_civ: 397 / 397 by weapon at 600 s,
  out/lab/20261104-102449).
* Religious projectiles and AoE areas live about a second; the relay showed them 0.3-0.4 s late on the guest (the
  host sent its shot list every 0.2 s, the guest fired at most every 0.1 s): a detector sample saw them in one game
  only. After the count relay the rows came both ways (noun_only_host too, out/lab/20261104-101322).

## Fix
* `civ.shot_release` (the guest-only AoE areas): an AoE weapon (cDefaultAoETool) makes its area once when it starts
  firing (tool +0x120, strategy vt 0x48) and keeps it until `cToolManager::OnMouseUp` 0x0104FAA0 stops the tool
  (strategy vt 0x38 releases the area; cCombatSimulator::Update calls it when a combatant's target is gone). The
  area sends cultural projectiles (vtable slot of 0x1475150 -> 0xCBEBA0, three CreateInstance(0x4F76F0D)). A guest's
  copy of the weapon was never stopped: its area and the area's projectiles stayed (civ_aoe with the switch off: the
  guest's area lived 30.3 s, the host's 5.3 s; cultural projectiles 21.25 s mean life on the guest, 2.34 s on the
  host). Now the host's OnMouseUp hook marks a listed shooter's entry stopped (a flag byte after the count in chunk
  0xFC) and the guest calls OnMouseUp for its copy (and at the latest when the shooter's entry ends).
* `civ.shot_now`: a shot that left goes to the guests at once (at most every 0.05 s, not with the next 0.2 s list),
  and the guest fires a new count on the frame it arrives (delay 0.10-0.20 s instead of 0.30-0.40 s).
* `civ.shot_fired`: the host counts the FireTool calls that made a shot and the guest fires that many (no more, at
  most 3 behind); MSG_CIV_STATE chunk 0xFC (the 0.7.5 list 0xFE is still read). A guard: the old relay was not seen
  to over-fire.
* The shot lists are split by packet size (BUG-0538).

## Verification
coop_civ_aoe.py (a religious attack, then the vehicles go home; AoE areas by object on both games):
FAIL with SPORE_FIX_OFF=civ.shot_release (guest's last area 25 s after the host's, guest-only areas in 124 of 371
samples): out/lab/20261104-124527; PASS (0.2 s, 1 of 378): out/lab/20261104-124742.
coop_civ_religious.py (a religious attack on an NPC city; the guest's religious-shot count series against the host's):
FAIL with SPORE_FIX_OFF=civ.shot_now (delay 0.30 s): out/lab/20261104-105005 (and 0.30 s in out/lab/20261104-104530,
0.40 s in out/lab/20261104-103929 before the check);
PASS (0.20 / 0.10 / 0.15 s): out/lab/20261104-103723, out/lab/20261104-104140 (x2), out/lab/20261104-104800.
SPORE_FIX_OFF=civ.shot_fired did not fail (out/lab/20261104-095320: 126 / 126).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): analysed (late, not extra shots) and fixed (civ.shot_now, civ.shot_fired).
- 2026-11-04 (fix/w42-civ-left): still open. After civ.shot_now the detector's rows remain in some play_civ runs:
  out/lab/20261104-110843 cultural 6 % / AoE 3 %, but out/lab/20261104-113023 AoE areas 26 %, cultural 13 % (guest only,
  no noun_only_host row). The delay part is fixed (civ_religious twin above); the rest is not explained. Inferred
  next step: the AoE area (cDefaultAoETool, noun 0x4167186) and the cultural projectile live longer on the guest
  (a guest copy that misses its target: the host's target found by noun + place within 4 m may be another object,
  or the mirror stands elsewhere); measure each object's lifetime on both games in civ_religious with a
  religious attack on a city (the test has no AoE area at all so far).
- 2026-11-04 (fix/w42-civ-left, round 2): the guest-only areas are copies of AoE weapons never stopped on the guest; fixed (civ.shot_release).
- 2026-11-04: released in 0.7.6.
