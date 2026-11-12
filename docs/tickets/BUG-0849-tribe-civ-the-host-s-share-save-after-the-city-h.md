---
id: BUG-0849
title: Tribe -> Civ: the host's share save after the City Hall editor can run while the village is dismantled for graduation (suspected)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9050
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: unreleased
branch: fix/w48-saves
commits: f4b30adf, 138a239e
tests: tribe_civ_rejoin, tribe_civ
signature: 0xC0000005 at 0x00BCAB07 data 0x60C (GOG 2024) during a share save in the end of Tribe; nearest symbol BT_GoalSlotStart+0x187 is not the function
docs: docs/re/tribe.md#tribe-graduation-save, docs/06-testing.md#testing-saves-w48
related: BUG-0588, BUG-0576, BUG-0585
---

## Report
The fork's host crashed in the game's serializer during an automatic world save right after the City Hall editor:
Editor -> Tribe was taken as an evolution and scheduled a share; the save went out 36 ms after the game asked for the
vehicle editor, while the intermediate village was being dismantled for graduation.

## Root cause
Observed (lab, switch off, 2 of 2 runs: 20261112-021437, 20261112-023150): back from the City Hall editor (the
end of Tribe: the player tribe has no food mat, the game takes the village down for Civ) a guest that joins makes the
host post a share save; ~1.2 s later the host's game faults at 0x00BCAB07 (data 0x60C), the address the fork saw.
The game's handler keeps it running, no saved box comes, the mod shares the half-written slot after 10 s ("sharing
anyway", one file more than the slot had), the guest stays in its Tribe world while the host goes on to Civ (92-97 s
stage_apart) and in Civ the host's share saves write nothing four times in a row.
Without a join our code made no share save in that time: since BUG-0508 the return from the City Hall editor goes
"in place" for a guest in the same world (it would still save for a guest in another world or stage).
Also observed with the first fix: a share waiting for its refused save held the vehicle editor request 15 s
(share_save_due).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9050, switch tribe.graduation_save); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* coop.cpp h_Submit: a native graduation request is recognised by its EditorRequest fields: editor +0xC = 0x99E92F05
  (City Hall), model +0x10 = 0, return mode +0x1C = kTribe, context +0x90 = 0x0116DD1B (community). It records the
  current world as g_tribal_graduation_world.
* tribal_graduation_pending() is true while that world is current. While pending: on_mode_change returns before
  scheduling any share for the return to Tribe; save_blocker refuses ("Finish Civilization graduation before sharing
  the campaign"); submit_tick does not wait for save_pending.
* tribal_graduation_tick clears the state when the world changes or the mode is not Tribe / editor, and also after
  5 s of playable Tribe with no busy flag, vote or held submit (a native cancel), not during the short gap before the
  cinematic. The real Civ stage is shared as a new stage.
* Their test: a full economic and a military graduation replay (City Hall and vehicle editors with real UI input,
  a 90 s hold in the vehicle editor), both games reach Civ with matching designs and zero faults.

## How to test
Lab: tribe_civ with a guest, with a trace line for every share save and every EditorRequest Submit. Assert that no
share save is posted between the City Hall editor's end and the vehicle editor (and none while the vehicle editor
vote runs); to make it fail reliably, hold the vehicle vote for 20 s. FAIL on main if a save appears in that window
(and a fault if the suspicion holds), PASS with the fix.

## Fix
coop.cpp `tribe_graduating()`: in Tribe while `tribe::stage_end()` or the last editor request was one of the end of
Tribe (City Hall 0x99E92F05, vehicles 0xF670AA43 / 0x9AD7D4AA / 0x7CB21BAD; forgotten on a load, the menu or another
stage). Then save_blocker refuses share saves ("the Tribe stage is ending ... the world is sent in the Civilization
stage"), the return from those editors schedules no share (it is no evolution), and share_save_due() does not hold
the vehicle editor for a save that cannot be made. The Civ stage is shared as a new stage, also to a guest that
joined in the end of Tribe. Switch `coop.fix tribe.graduation_save off` = 0.7.12. (The fork's EditorRequest field
matching was not needed: the game's own stage_end sign and the request id are enough.)

## Verification
`tribe_civ_rejoin` (coop_tribe_civ.py --rejoin, tribe, ~3-8 min): the guest leaves and joins right after the City
Hall editor; the host's slot files are sampled until the vehicle editor opens; faults; the guest must reach Civ.
* FAIL: out/lab/20261112-021437 and out/lab/20261112-023150 (`SPORE_FIX_OFF=tribe.graduation_save`, lane 3): the
  fault at 0xBCAB07, a write into the slot 6 s after the City Hall editor, the guest left in Tribe.
* PASS: out/lab/20261112-022822 (lane 3): no write, no fault, the vehicle editor 0.4 s after its vote, the guest in
  Civ. (Run 20261112-022253 with the first version of the fix: no fault, but the vehicle editor held 15 s: fixed in
  138a239e.)

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12: reproduced with a guest joining in the end of Tribe (fault 0xBCAB07) and fixed on fix/w48-saves (tribe.graduation_save), FAIL 20261112-021437 / 023150, PASS 20261112-022822.
