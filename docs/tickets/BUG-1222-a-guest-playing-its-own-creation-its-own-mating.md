---
id: BUG-1222
title: a guest playing its own creation: its own mating-call evolution opens the game's evolution editor, whose done is blocked by the part-list error (parts not in the stage's list) and which has no red cross: the guest cannot leave it
status: fixed
kind: bug
stage: Creature
reported: 2026-11-16
source: fix/w51-t2 (editor_after_swap)
role: guest
versions: 0.7.14.1 and older (inferred: the guest's own-evolution path of BUG-0516 is unchanged)
fixed_in: 0.7.15
branch: fix/w51-t2
commits: 5db495f3
tests: editor_after_swap
signature:
docs: docs/re/creations.md#creations-own-mating-editor, docs/06-testing.md#testing-creature-after-worlds-reset
related: BUG-0516, BUG-0756, BUG-0720, BUG-0368, BUG-0318, BUG-1199
switch: creature.own_mating_editor
---

## Report
Found while fixing editor_after_swap of the 0.7.15 final check (BUG-1199): the guest plays its own creation 'Kirgle'
(0x290225CD, chosen), leaves and joins again, and asks for its editor as its mating call does (`coop.vote evolve`:
the creature stage's evolution request, tag 0x12191CA). The editor opens and draws, but its green check does nothing
(three clicks 20 s apart; at 6.7 fps and at normal fps) and the screen has no red cross: the guest cannot leave the
editor. A player: a guest that chose its own creature in the co-op window and calls a mate in the Creature stage.

## Root cause
Observed (lab lane 10, runs 20261116-094534 / 20261116-095058, shots out/lane10/shots/editor_after_swap_exit.png):
`h_Submit` sends a guest's own in-stage request on as the game's (BUG-0516: "evolves its own creature"); that is the
evolution editor (+0x39 / +0x3B = 0, +0x3C = 1: no red cross). For a creation with Creature Creator parts the editor's
blocking set has the part-list error (bit 5, docs/re/creations.md section 7: "done only shows the error and stays"),
and `h_BlockingErrors` waives it only while the own editor (`open_own_editor`, `g_own_editor`) is open. The world's
species (editor_after_load) has no such error: its check works (20261116-090728).

## Fix
vote.cpp `h_Submit`: in the host's world, a guest playing one of its own creations (`creations::mine(kCreature)` is in
`options(kCreature)`) has its in-stage evolution request refused (`g_evo_flag_refused`, BUG-0920) and its own editor
scheduled as after a vote (`g_own_editor_due`, opened by `own_editor_tick` when the stage settled and the avatar's
body is built, BUG-0318): the red cross, the part-list waiver, no evolution tag (no evolution of the world's species
in the guest's game; the own editor's price, BUG-0466). Log "evolution: this guest plays its own creation: its mating
call opens its own editor <id> (BUG-1222)". Switch `coop.fix creature.own_mating_editor off`: the game's editor
(0.7.14). Test: editor_after_swap (the guest's own creature after a rejoin, the mating call's request, left with the
red cross).
Not changed: a guest in its own world (its request goes on as the game's), the guest playing the world's species.

## Verification
* FAIL twin (`SPORE_LAB_FIXOFF=editor_after_swap=creature.own_mating_editor`): out/lab/20261116-100006 (lane 10,
  "the red cross brings the guest back to the Creature stage"); before the fix: out/lab/20261116-094534 (6.7 fps) and
  20261116-095058 (normal fps), the green check blocked.
* PASS: out/lab/20261116-095548 (lane 10, "its mating call opens its own editor 465C50BA (BUG-1222)", the red cross
  took click 1); final out/lab/20261116-100349 (lane 10, --repeat 2: both copies PASS) (with the host-world condition, commit 5db495f3).

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t2 (runs above).
- 2026-11-18: released in 0.7.15.
