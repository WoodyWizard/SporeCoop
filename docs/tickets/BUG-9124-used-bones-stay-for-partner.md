---
id: BUG-9124
title: Creature: bones a partner used stay visible here and cannot be used
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev
role: host
versions: 0.7.4 to 0.7.8-lineages49-dev
fixed_in: 0.7.8-lineages53-dev
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9 + guest 8: a real dig (click the skeleton: `coop.wobj screen <ornament>` + `click x y`) on the host with the guest near; `coop.wobj list interactive_ornament` on both; guest log "removed here"
signature: -
docs: docs/re/creature.md#creature-world-objects
related: BUG-0459 BUG-0473
switch: wobj.ornament_model wobj.ornament_gone
---

## Report
"When bones are picked up by the connected player, the host still sees the bones, but can no longer pick them up."

## Findings
* The "cannot pick them up" part is BUG-0459's fix working: a partner's use marks the reward used here (no second
  part).
* The interact node also calls cInteractiveOrnament_NextModel 0xC40360 (the next model in the ornament's list);
  worldobj's orn_use did not. Added (switch wobj.ornament_model).
* Not confirmed: Samuth.spo's bones (4750 and its neighbours) have no model list (+0x248 = -1, list empty), so
  NextModel changes nothing there, and the reward has no effect (+0x1C = 0). What makes a used bone look
  different on the picker's screen is still unknown; the QA screenshots near bone 4750 were not conclusive.
* Next: the owner's save and the bone where it was seen.

## Cause (2026-11-11, a real dig in QA)
`coop.wobj use` (OrnamentUsed + the reward message) changes only +0xA3 and the reward; the bones stay. A real dig
(the player clicks the skeleton; the interact node walks there and digs) on QA bone 9188 (no model list): the
skeleton disappeared on the digger's screen, its model links +0x0C..+0x14 were cleared and the object left the
object list a few seconds later. The partner's game applied the use (reward used, +0xA3 = 0, NextModel since
lineages50, which returns 0 here) and kept the bones on screen, looking diggable ("bones still don't clear").

## Fix
orn_use: when NextModel returns 0 (no model left) the partner's copy is removed (game::destroy_instance, the noun
manager's DestroyInstance). A bone with a next model keeps switching to it. Switch wobj.ornament_gone.

## Verification
Host digs bone 26129 with the guest 15 m away (both connected): host list st=1, then gone; the guest's log "wobj:
bones 2E0C61D8 used by a partner have no model left: removed here (1)" within 5 s, gone from its list. 0 faults on both.
Not run: the guest digging (the hostile creatures at the QA bones knocked the guest out every time); the host applies
a guest's dig through the same orn_use.
