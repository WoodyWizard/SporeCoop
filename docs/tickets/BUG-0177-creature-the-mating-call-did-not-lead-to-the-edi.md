---
id: BUG-0177
title: Creature: the mating call did not lead to the editor in the bot
status: fixed
kind: test
stage: Creature
reported: 2026-10-24
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in: unreleased
branch: fix/w46-g4-tests
commits: 0929b2d8
tests: creature_mating (tools/tests/coop_creature_mating.py)
signature:
docs: docs/re/creature.md#creature-mating-call
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* Backspace (Mating Call) at the nest with DNA shows "Mate: Locate your Mate!" and a thought bubble,
  but clicking the family members around did not open the editor within 30 s. Not a confirmed bug:
  how a player answers the call is not explored yet (docs/re/creature.md 5). The evolution vote is
  covered by `coop_vote.py` through `coop.vote editor`. The Cell stage's mating call is driven for
  real since 2026-10-24 (`coop_editor_desync.py`, docs/re/cell.md 12), and the Creature stage's first
  editor through the stage switch, not the creature mating call.

## Root cause
Observed (Ghidra static, docs/re/creature.md#creature-mating-call, and this branch's runs): the mate is not chosen by
the call. The game's drama manager picks one family member within 15 m of the nest every 5 s (mgr +0x38, marked +0xB58 &
0x10); the mating call (Backspace) only plays animations and hearts, on the mate and on up to 3 other members, so the
old bot clicked hearted members that were not the mate and nothing happened. A left click on the marked mate (cursor
kind 9) makes the avatar walk to it and mate (BT node 02D852F0), the CRG_LaidEgg cinematic plays, and the game's mating
editor request (0xD3C650, tag 0x12191CA) reaches the co-op vote about 60 s after the click. Not a bug: the bot's input.

## Fix
The missing test, tools/tests/coop_creature_mating.py (suite creature_mating): `coop.drama mate` (new read-only line of
coop.drama: the drama manager's mate, its flags and behaviour tree node, the avatar's node) names the mate, the host
presses Backspace and left-clicks the mate where its game draws it; checks: the avatar goes to mate (node 02D852F0), the
guest's screen shows the vote "wants to evolve (Creature)", the guest is ready, the host's editor and the guest's own
open, both leave with "done" and are back in the Creature stage. MATING_CLICK=other (another member) must FAIL.

## Verification
FAIL (the twin, a click on another family member: no mating, no vote, no editor): out/lab/20261108-065608.
PASS 4 of 4 on lanes 3 and 2: out/lab/20261108-054311 (lane 3), 20261108-065028 #1-#3 (lane 2).

## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached: how a player answers the Creature mating call (which object to click after Backspace) is unexplored reverse engineering. Left open.
- 2026-11-08 (fix/w46-g4-tests): how a player answers the call (Ghidra), the test written; 4 of 4 PASS, the twin FAILS; fixed.
