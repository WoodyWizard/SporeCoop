---
id: BUG-9367
title: A Creature game stays on a black screen when another era's model is baked (the bake's go-on message dropped)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-24
source: deep playtest 2026-11-24
role: host
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: e91040a7
tests: tools/qa_win (manual, the MSI, TITAN's galaxy server): a Creature era entered beside the planet's Civ era; `coop.visit status` bake queue
signature:
docs: docs/re/creations.md#creations-bake-go-on
related: BUG-9341 BUG-9290 BUG-0313
---

## Report
Deep playtest 2026-11-24: a player entering a galaxy planet's stored Creature era (as its host) while the planet's
Civ era had players stayed on a black screen for good: no HUD but the Social button, the game's pause reason
0x4BF38A7 never lifted (images/bug9367_black_off.png). The same entry an hour before, when the planet had no Civ
era, was fine.

## Root cause
Observed (runtime, the MSI) and read (code of the Steam game, tools/qa_win/dis.py):
- the Creature stage's entry (CreatureGame_ReturnStateMachine 0xD43DE0; the pause at 0xD45575, the wait at 0xD450BC)
  resumes only when IBakeManager vt 0x28 (the bake queue) is under 1, the model world has nothing pending and the
  player species is baked;
- era visitors asked the bake of the Civ era's building and vehicle (8 levels) in the first second of the load;
- the first bake's picture posts message 0x3FC3F13; BUG-9341's guard left the editor's handler out as a whole (an
  editor without its save area), but that handler ends by posting 0x29D57F4 (0x5921BA), the bake's "go on" (the
  bake's render posts it itself when no editor picture is wanted). Without it the bake stood still with the other 7
  behind it: `coop.visit status` "bake queue: 8 queued" for minutes, the props never made.
So since BUG-9341's fix no other era's building or vehicle was drawn in a game whose editor has no save area, and a
stage entry during such a bake never ended.

## Fix
era_visitors.cpp h_EdMsg: a skipped snapshot message posts 0x29D57F4 in the handler's place (IMessageManager vt
0x14(0x29D57F4, 0, 0)) and returns 1 as the handler does. Switch `coop.fix galaxy.bake_go_on off`: BUG-9341's guard
as it was. `coop.visit status` shows the bake queue, each waiting object's baked levels and the go-on count.

## Verification
- FAIL (the build before, the MSI's second game, 16:30): "bake queue (BUG-9367): 8 queued", props 0 / waiting 2
  and "pauses: paused=1 reasons: 04BF38A7=1" in four reads over 30 s; the black screen (the picture above).
- PASS (the fix, 16:35, the same entry): the queue 8 -> 6 -> 4 -> 2 -> 0 in 40 s, "8 posted for a skipped
  snapshot", props 2 made, no pause, the Civ era's City Hall wall in the Creature game
  (images/bug9367_creature_sees_city_on.png).
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL is the run of the build before.

## History
- 2026-11-24: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the deep playtest).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
