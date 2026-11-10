---
id: BUG-0849
title: Tribe -> Civ: the host's share save after the City Hall editor can run while the village is dismantled for graduation (suspected)
status: open
kind: crash
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9050
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: fork: serializer fault runtime 0xEBAB07 (canonical 0xBCAB07) during an automatic world save
docs:
related: BUG-0588, BUG-0576, BUG-0585
---

## Report
The fork's host crashed in the game's serializer during an automatic world save right after the City Hall editor:
Editor -> Tribe was taken as an evolution and scheduled a share; the save went out 36 ms after the game asked for the
vehicle editor, while the intermediate village was being dismantled for graduation.

## Root cause
* Observed (code): coop.cpp:2406-2427 on_mode_change: Editor -> Tribe after the City Hall editor sets evolved, every
  remote gets needs_world and g_share_due_at = now + 3 s; BUG-0588 changed only the notice ("the in-place path itself
  stays").
* Observed: save_blocker (coop.cpp:1873 ff.) blocks an open editor, g_submit_queued and 5 s after a submit, not a held
  or voted vehicle editor request; follow_phase waits only for the cinematic.
* Inferred: the share save can be posted between the cinematic's end and the vehicle editor. Our tribe_civ lab test
  passed with 0 faults, so the timing may differ: suspected, confidence low-medium.

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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
