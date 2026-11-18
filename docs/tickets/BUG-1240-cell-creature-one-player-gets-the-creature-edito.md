---
id: BUG-1240
title: Cell -> Creature: one player gets the Creature editor, the other a black screen, and both play the same creature (each should make its own)
status: fixed
kind: bug
stage: Cell, Creature
reported: 2026-11-18
source: owner: players' video
role: guest
versions: <0.7.15
fixed_in: unreleased
branch: fix/w52-evoeditor
commits: 1dc8adbf
tests: evo_screens_switch_slow, evo_screens_switch, evo_screens_switch_noready, evo_screens_switch_lowfps
signature: the guest arrives in Creature after the landfall vote ended (log "vote: ended by the host ... editor=FD4902BD" while the guest is still in Cell / receiving the world), no "own editor opened"
docs: docs/re/creations.md#creations-own-editor-arrival
related: BUG-0315, BUG-0508, BUG-1239
---

## Report
A players' video (players on a version before 0.7.15), seen by the owner: at the change from the Cell to the Creature
stage one player had the Creature editor, the other a black screen; afterwards both played the same creature. The
owner's requirement: each player makes its own creature at that change (its own Creature editor) and then plays it,
seen as such by the other.

## Root cause
Observed (lab 20261118-094043, `evo_screens_switch_slow`: the guest's link capped at 512 kbit/s, base code of
v0.7.15): the host's History check, the switch vote, the guest's I'm ready; the host's game enters Creature and at once
asks for its first Creature editor (the landfall editor FD4902BD), which waits for BUG-0315's arrival vote: it counts a
guest as ready once it plays the host's Creature world, else it runs out after 60 s. The host's Creature world
(3.2 MB) needs about a minute to reach the guest over that link: the vote ended "time is up" (guest log 09:44:23
"vote: ended by the host ... editor=FD4902BD") while the guest was still in Cell receiving the world. The guest takes
its own editor from a vote's end only when it plays Creature at that moment (session_glue.cpp, the vote handler), and
`own_editor_tick` drops a due editor in any other stage. So the guest got no own editor: it sat in the galaxy menu and
the load (the "black screen") while the host was in its editor, arrived playing the world's species (the host's
creature, 29278AA2) and followed the host's evolved one in place (BUG-0508, 29278AA6): both played 29278AA6. On a
clean link (lab 20261118-095451) the guest arrives within 10 s and the vote counts it: its own editor opens.
Inferred: the players' link or PC was slow enough (a world transfer of a few MB, a slow load) for the 60 s to run out.


## Fix
`core.own_editor_arrival` (vote.cpp, session_glue.cpp): when the landfall editor's vote ends while a guest is still on
its way to the host's Creature world (Cell, the galaxy menu, the load; not another world, not creature.mode host), its
own editor stays due ("own editor: the first Creature editor's vote ended before this guest arrived"); `own_editor_tick`
waits for its arrival in Creature, then counts the usual 90 s and opens the own editor once the stage has settled
(at most 300 s in all, else "evolve at your nest later"). `coop.vote` prints "own editors kept for an arrival N".
`coop.fix core.own_editor_arrival off`: as 0.7.15.


## Verification
Test `evo_screens_switch_slow` (tools/tests/coop_evo_screens.py --part switch under SPORE_NET=lowband, cell fixture,
a guest without creations; real clicks on History's check, I'm ready, the legs dragged in both editors, Save New; both
screens shot every 2 s; then the avatar keys and what each game draws for the partner).
* FAIL: out/lab/20261118-095457 (`SPORE_FIX_OFF=core.own_editor_arrival`, lane 5): "the guest's OWN first Creature
  editor opened" and "each player plays its OWN new creature" fail: host 29278DFC, guest 29278DFC, the host draws the
  guest as 29278DFC. Also out/lab/20261118-094043 (the base code of v0.7.15, before the switch existed): the same
  (both 29278AA6).
* PASS: out/lab/20261118-095454 (lane 4) and out/lab/20261118-100546 (lane 5): the guest's own editor 25 s after the
  host's, two species (host 29278DF9, guest 29278DF6), the host draws the guest with the guest's creation, still so
  45 s later.
* The same flow on a clean link, with the switch vote run out and with the guest at 10 fps (the arrival path not
  taken there): out/lab/20261118-095451 evo_screens_switch, out/lab/20261118-100540 evo_screens_switch_noready,
  out/lab/20261118-100543 evo_screens_switch_lowfps, all OK.


## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w52-evoeditor): reproduced over a slow link (the landfall vote runs out before the guest arrives), fixed with core.own_editor_arrival (FAIL/PASS runs above).
