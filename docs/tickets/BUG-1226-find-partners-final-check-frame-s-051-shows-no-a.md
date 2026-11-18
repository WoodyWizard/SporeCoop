---
id: BUG-1226
title: find_partners: final-check frame S/051 shows no arrow and no Bob tag on the host while Bob is 79 m away
status: fixed
kind: test
stage: Creature
reported: 2026-11-17
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-space3
commits: 21ddaa71
tests: find_partners
signature:
docs: docs/08-ui.md#ui-tag-edge
related: BUG-0949, BUG-0764, BUG-1204
---

## Report
0.7.15 final check rel/0.7.15 20261116-025154, find_partners frame S/051 (frames/find_partners/01_creature_tag_host.jpg):
Bob is 79 m from the host, and the host's screen shows neither an arrow to Bob nor a "Bob" tag, although the frame's
note says the host's screen shows the guest. (That run's FAIL itself, "a place 80 m from the host where the host is
off the guest's screen", was BUG-1204's sea monster: every place after dir 0 had the same guest view; fixed since by
`coop.drama hold 40`.)

## Root cause
Observed (lab 20261117-125031, lane 6, the first version of the switch which moved only tags): at find_partners' first
three places the host's game had an arrow to the guest on the bottom edge (y 1034 of 1080, `coop.find` drawn=1) whose
label box the layout hid (`coop.find boxes`: hidden=1): the game's bottom bar is a HUD window across the screen and a
bottom arrow slides only sideways (BUG-0949's layout). Also (frames of fix/w51-t2 20261116-090728): a partner at the
screen's left edge is in the view, so no arrow, and its centred tag reached past the edge; a tag slides only up, so it
was hidden as well. The partner had neither.

## Fix
find.cpp: an arrow with no free place along its edge tries above / below it (up to 10 steps, off the HUD and the
placed labels); a tag is moved sideways into the screen before the layout. Switch `find.edge_labels`. find_partners
checks at every place that the host's game shows the guest after the layout (an arrow or a tag not hidden; a tag
under the HUD counts).

## Verification
* FAIL: 20261117-125031 (lane 6, `SPORE_LAB_FIXOFF=find_partners=find.tag_edge`, the arrow part not yet written):
  dir 0..2 `arrow at (315, 1034) box ... hidden=1`.
* PASS: 20261117-125525-2 and 20261117-125949 (lane 6): dir 0..2 the arrow placed above the bar (`box 313,936..406,984
  hidden=0`); frame find_partners/01_creature_tag_host.jpg of 20261117-125525-2 shows "Bob 79 m" above the bottom bar
  where S/051 showed nothing.

## History
- 2026-11-17: opened.
- 2026-11-17: opened and fixed on fix/w51-space3.
- 2026-11-18: released in 0.7.15.
