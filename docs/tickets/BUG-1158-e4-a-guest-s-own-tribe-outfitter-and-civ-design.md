---
id: BUG-1158
title: E4: a guest's own Tribe outfitter and Civ design editors change only its own game and are lost
status: fixed
kind: bug
stage: Tribe, Civ
reported: 2026-11-15
source: plan
role: guest
versions: 0.7.14
fixed_in: 0.7.15
branch: fix/w51-editors
commits: cebe2441, ea6cf28b
tests: tribe_outfit, civ_designs
signature:
docs: docs/re/creations.md#creations-stage-designs
related: BUG-1156, BUG-1157, BUG-0516
---

## Report
Playability audit of wave E (item 2), confirmed in the lab: a guest's own Tribal Outfitter (and a design editor in
Civ) opened at once ("Bob evolves its own creature"); its result changed only the guest's game (its tribe profile
2923BFD1 while the host's stayed 2923BFD6) and was lost at the next world from the host. With BUG-1156 / BUG-1157 it
would be taken back at once.

## Root cause
Observed (lane 4, base build c90f2ed9, by hand): vote.cpp h_Submit lets a guest's own editor request of its stage open
at once (BUG-0516). The game keeps one species profile per tribe and one culture set per civilization: there is no
outfit or design of one player only, and the shared one is the host's world's.

## Fix
Decision: the host's world is the truth. In the host's world a guest's own outfitter (Tribe, editor 0x156276D1) and
Civ design editors (any but the spaceship editor 0x96B24187) are refused in the Submit hook with "the tribe's outfit
is shared: only <host> changes it" / "the civilization's designs are shared: only <host> makes new ones" (the planner
stays usable, no input lock left); a guest's own design pick goes back to the host's with a line. A guest in a world
of its own keeps its editors. Switch: `coop.fix editors.guest_designs off`. (Sending a guest's design to the host for
everybody would put a creation of the guest's My Creations into the host's save: a feature for later.)

## Verification
tribe_outfit (the guest's own outfitter by real input: stays in Tribe, refused, no input lock) and civ_designs (the
guest's own pick goes back, its editor request is refused):
FAIL with SPORE_FIX_OFF=tribe.outfit_sync,editors.guest_designs: out/lab/20261115-131200; with
SPORE_FIX_OFF=civ.design_sync,editors.guest_designs: out/lab/20261115-131006 (the editors opened);
PASS (after the merge of rel/0.7.15 5901c362): out/lab/20261115-133408 (two runs each, play_tribe and play_civ --quick 2x PASS in the same run); before it: out/lab/20261115-131752 (two runs each; civ_host_building, civ_host_design, tribe_editor_world 2x PASS in the same run). Earlier PASS runs: out/lab/20261115-130639 (tribe_outfit), out/lab/20261115-130858 (civ_designs).

## History
- 2026-11-15: opened.
- 2026-11-15: reproduced and fixed in fix/w51-editors.
- 2026-11-18: released in 0.7.15.
