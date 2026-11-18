---
id: BUG-1258
title: Creature: a guest in its own first Creature editor is told the host evolved the species and everybody gets the new body
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: lab evo_screens_switch_slow (coordinator)
role: guest
versions: 0.7.15.1
fixed_in: 0.7.15.2
branch: fix/w53-cellvote
commits: 37042f38
tests: evo_screens_switch
signature: the guest's log "<host> evolved the species: everybody gets the new body" while its own Creature editor is due or open
docs: docs/re/creations.md#creations-own-editor-world-species
related: BUG-1240, BUG-0516, BUG-0315
switch: core.evolve_notice
---

## Report
Seen by the coordinator in evo_screens_switch_slow (rel/0.7.15.1, out/lab/20261118-121937, frame
03_guest_editor_guest.jpg): the guest, in its OWN first Creature editor (the BUG-1240 arrival path), gets the notice
"Alice evolved the species: everybody gets the new body". Wrong for a player making its own creature.

## Root cause
Observed (session_glue.cpp, the host's phase notice): the host's return from any evolution editor was told to every
player as "evolved the species: everybody gets the new body", a text from before the own editors. In Creature every
player makes its own creature (the landfall editor: own editors, BUG-0315 / BUG-1240; the mating evolution: offers,
BUG-0516) unless the session plays one creature (creature.mode host). Lab 20261118-124934 (`core.evolve_notice` off):
the guest's log had the notice once while it made its own creature. The co-op window's status line said the same
("is evolving the species in the editor: everybody gets the new body") while the host was in an editor.

## Fix
`core.evolve_notice` (session_glue.cpp evolve_own_text, coop.cpp status line): in Creature, unless creature.mode host,
the notice is "<host> made its creature: every player makes its own" (the landfall editor FD4902BD) or "<host>
evolved its creature", and the status line "<host> is in the editor". Off: the old texts (0.7.15).

## Verification
`evo_screens_switch` checks the guest's log for "everybody gets the new body" from the switch to the end of both
editors.
* FAIL: out/lab/20261118-124934 (`SPORE_FIX_OFF=creations.own_editor_no_replace,core.evolve_notice`, lane 2): 1 notice.
* PASS: out/lab/20261118-124955 (lane 3): none.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellvote): fixed with core.evolve_notice.
- 2026-11-18: released in 0.7.15.2.
