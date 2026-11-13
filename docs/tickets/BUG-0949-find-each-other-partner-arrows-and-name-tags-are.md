---
id: BUG-0949
title: Find each other: partner arrows and name tags are drawn over each other, over notification cards and over the minimap and the bottom bar (unreadable labels)
status: fixed
kind: bug
stage: Core
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-151535
role: host,guest
versions: 0.7.12 (unreleased)
fixed_in: 0.7.13
branch: fix/w48-labels
commits: a98f2cb7 79858616
tests: find_layout
signature:
docs: docs/08-ui.md#ui-label-layout
related: BUG-0155, BUG-0929
---

## Report
Key-frame review of the third final check out/lab/20261111-151535 (rel/0.7.12 8bdf305a). The edge arrows, their
labels and the name tags are placed by the target's projection only; nothing keeps them apart or off the HUD.

* Two arrow labels on top of each other: `out/lab/20261111-151535/frames/campaign_ct/05_play-tribe_guest-Bob.jpg` (sheet
  `out/lab/20261111-151535/frames/sheets/016_campaign_ct_05_play-tribe.jpg`), bottom edge: "Carol (chief) 68 m" and "Alice (chief) 115 m" are
  written over each other, only "115 m" of the second can be read; the two arrows touch.
  The same in a lighter form: `out/lab/20261111-151535/frames/campaign_short/03_play-creature_host-Alice.jpg` (sheet 019), left edge: "Carol 12 m"
  and "Dave 7 m" overlap by half a line.
* An arrow over a notification card: `out/lab/20261111-151535/frames/campaign_short/01_play-cell_guest-Bob.jpg` (sheet 017): the arrow
  "Carol 79 m" is drawn inside the card "+6 food (DNA) from your partners' meals", over the end of its text.
* Name tags over the HUD: `out/lab/20261111-151535/frames/play_civ/03_end_host.jpg` (sheet 036): two tags "Alice" stand on the minimap panel's
  top right corner and two tags "Bob" on the bottom bar next to the money (units that project to the screen's
  bottom left corner).

Severity: low (cosmetic, the labels are hard to read for as long as the targets stay in line). Confidence: high (four
frames). Regression of 0.7.12: the arrows are from 0.7.10; the chief arrows in Tribe, the Civ unit tags and the
players' colours are 0.7.12, so with 3-4 players there are more labels than before. Not compared with 0.7.11 frames.

## Root cause
Confirmed in the code (2026-11-12): every tag and arrow was drawn at once where its projection fell
(coop.cpp on_overlay, draw_owner_tags, draw_civ_tags, find.cpp draw_arrows); nothing knew the other labels or the
game's UI windows.

## Fix
Fixed 2026-11-12 (fix/w48-labels), switch `find.layout`: the labels of an overlay pass are queued and placed together
(findx::layout, docs/08-ui.md#ui-label-layout): apart, off the game's UI windows (read outside the frame twice a second)
and the co-op status block; a tag whose point is under the HUD is hidden. Off: where each falls (0.7.12).

## Verification
`find_layout` (`coop_find_layout.py`, creature): made-up partners (`coop.find ghost`) at the places of the report on
the guest's screen; the labels as drawn (`coop.find boxes`) must touch neither each other nor a UI window, the tags
under the minimap and a message card hidden, the one over the bottom bar moved, no tag colour in the minimap in the shot.
* FAIL (`SPORE_FIX_OFF=find.layout`): out/lab/20261112-123659 (the two arrow labels over each other, a tag on the
  minimap: 95 px of its colour there, a tag on a card, one on the bar).
* PASS: out/lab/20261112-122824, out/lab/20261112-123359-2.
* Not tested in a game: Civ unit tags and Tribe member tags (they go through the same `pfind::tag`).

## History
- 2026-11-11: opened.
- 2026-11-12: fixed on fix/w48-labels (find.layout), find_layout FAIL 20261112-123659, PASS 20261112-122824 and 20261112-123359-2.
- 2026-11-12: released in 0.7.13.
