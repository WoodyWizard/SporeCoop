---
id: BUG-1479
title: Civ: a city's specialty answer is lost on the guest when the first city listener clears the open question (CIV-10)
status: open
kind: desync
stage: Civ
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w78-civ
commits: 9b9fdf4a
tests: civ_specialty_once, civ_specialty_once_off (tools/tests/coop_civ_specialty_once.py); unit tests/civ_tests.cpp test_audit_v1
signature:
docs: docs/re/civ.md#civ-audit-v1-0717, docs/re/civ.md#civ-specialty-buyer
related: BUG-0982, BUG-1175
switch: civ.specialty_once
---

## Report
External static audit of 0.7.15.5, item CIV-10 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-V1 (fix/w78-civ). A behaviour change gets a switch and a twin.

## Root cause
* Code (rel/0.7.17, civ.cpp `h_CityListener`): the guest's hook of the city listener takes the answer message
  0x40FAFDF at the first city listener and sets `g_spec_open = -1`; the guard `data == g_spec_answer_data` sat inside
  `if (... g_spec_open >= 0 ...)`, so for the same message at every later city listener it never ran and the message
  went to the game's listener (`o_CityListener`).
* The game's listener (cCity_MessageListener 0xBE75C0, docs/re/civ.md#civ-specialty-buyer) acts only for the player
  civ's last captured city (+0x44C): button "take" = `SetSpecialty(city, +0x29C, 1)`, then Resume 0x4BF38A7. On a guest
  +0x44C is whatever the loaded save named (the guest's own captures are refused), so that city may get its stale
  capture type as its specialty on the guest only (a desync of one city's specialty) and the box's pause one Resume
  too many. Static reading (audit + this branch); "not synced to the other guests" is not shown by the code.

## Fix
civ.cpp: the decision is `civlogic::spec_answer(fix, open, same_data)` (civ_logic.h): the answer taken is dropped at
every later listener of the same dispatch, whether the question is still open or not; the taken data pointer is
cleared at the next `civ::tick` (after the dispatch), so a later box of the game's own that reuses the same data
pointer is still the game's. The city whose listener took the answer is printed as `took_at=` on the `specialty:`
line of `coop.civ tables` (tests). Switch `coop.fix civ.specialty_once off`: 0.7.16.1.

## Verification
open, waiting for the lab run. pending night run: civ_specialty_once, civ_specialty_once_off (civ_npc, warm, about
3 min each, one lane). `coop_civ_specialty_once.py`: on the guest the player civ's +0x44C is set to a city X whose
listener is not the first of `coop.msglisteners 40FAFDF`, with X's +0x29C other than its specialty; the guest buys an
NPC city and answers Economic (a click). PASS: X's specialty (`spec=`, the game's +0x540) on the guest equals the
host's, the bought city is Economic in both, the guest's game time runs. Twin `civ_specialty_once_off`
(SPORE_FIX_OFF=civ.specialty_once) must FAIL at "city X keeps its specialty"; INCONCLUSIVE when X's own listener took
the answer.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-V1), branch fix/w78-civ.
- 2026-11-23: fixed on fix/w78-civ (9b9fdf4a), switch civ.specialty_once; lab test and twin written, night run pending.
