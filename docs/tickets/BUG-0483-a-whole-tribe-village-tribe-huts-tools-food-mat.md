---
id: BUG-0483
title: a whole tribe village (tribe, huts, tools, food mat, members) exists in the guest's game only (Tribe and Civ)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in: 0.7.5
branch: fix/w41-tribe-world
commits: 7b92a15b
tests: tribe_guest_tribes
signature:
docs: docs/re/tribe.md#tribe-guest-world
related: BUG-0381, BUG-0523
---

## Report
Detector: `Tribe:guest_only:tribe`, `tribe_hut`, `tribe_tool`, `tribe_food_mat`, `citizen` together (1% of Tribe comparisons, e.g. at (83 -423 -268)), and `Civilization:guest_only:tribe` / `tribe_hut` / `tribe_tool` / `tribe_food_mat` (2%, at (-249 -33 475)). A tribe the host's game no longer has (conquered, allied, removed) or one the guest's game placed itself is still on the guest. Class G4 / M6 (the guest's own NPC tribes).
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (lab 20261103-234937, `tribe_guest_tribes` with SPORE_FIX_OFF=tribe.guest_tribes, tribe fixture):
  the guest's own NPC tribe controller formed tribes the host's game did not have: one `coop.tribe form` (the
  controller's own forming, NpcTribeController_FormTribe 0xCC6440) and within 10 s the guest's game had 3 tribes
  with 5 members each, the host 2 tribes; 8 s later 4 guest-only tribes with 7-9 members. The host's game forms
  tribes at the same markers later (the save_strip run: (446 -150 185), (438 181 188) on the host after 1-2 min), so
  a guest-only village stays until the host forms its own there, or for good when it does not.
* Observed (code): the end of a complete cycle removes only the unknown members of tribes the host's tribes map to
  (40 m); a local NPC tribe no host tribe maps to kept its members for good.
* Inferred: the detector's campaign_ct rows (a village on the guest only after the live Creature -> Tribe change)
  are such a tribe of the guest's own controller (the guests load the host's world and form at their own pace).
  The Civ rows (Civilization:guest_only:tribe) are not looked at here (Civ guests, C1's area).

## Fix
`tribe.guest_tribes` (tribe.cpp): a hook on SporeCoop::NpcTribeController_FormTribe (its only function, vtable slot
0x24): on a guest in a session in Tribe its game does not form NPC tribes (the host's come through tribe.npc_tribes);
and a local NPC tribe no host tribe maps to for 5 s of complete cycles loses its members (RemoveMember with destroy).
Its hut, tools and food mat stay (no game function to remove a whole tribe is known: docs/known-issues.md#issue-tribe-tools).

## Verification
* FAIL (`SPORE_FIX_OFF=tribe.guest_tribes`): out/lab/20261103-234937: the guest's game formed 3 tribes of its own,
  their members stayed (4 guest-only tribes with 7-9 members at the end).
* PASS: out/lab/20261103-234822: the forming did not run on the guest (forms_blocked=1), no tribe of its own; with
  the fix off for a moment 3 tribes were formed and their 15 members went once it was on again.
* Detector: the rows came only from campaign_ct (out/detector/20261103-final.md: Tribe:guest_only:tribe 3 of 218
  Tribe comparisons, campaign_ct#1 / #2); campaign_ct was not run on this branch (time box). detect_tribe has no
  Tribe row of this ticket before or after.

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w41-tribe-world (tribe.guest_tribes): the guest's own forming; the Civ rows not looked at.
- 2026-11-04: released in 0.7.5.
