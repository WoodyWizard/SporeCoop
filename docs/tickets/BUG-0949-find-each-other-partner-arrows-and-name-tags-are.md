---
id: BUG-0949
title: Find each other: partner arrows and name tags are drawn over each other, over notification cards and over the minimap and the bottom bar (unreadable labels)
status: open
kind: bug
stage: Core
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-151535
role: host,guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/08-ui.md#ui-find-each-other
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
Inferred from the frames, the code not read: each arrow sits where its direction meets the screen edge and each tag
at the projected point; there is no pass that moves labels apart, keeps them out of the HUD rectangles (the minimap,
the bottom bar, the notification column) or hides a tag whose point is under the HUD.

## Fix
Open.

## Verification
Open. How to reproduce: 3-4 players, two partners in nearly the same direction off screen (Tribe: their chiefs
60-120 m away the same way); a partner whose arrow lands in the top right corner while a notification is up; Civ:
a partner's vehicles at the bottom left of the view. A test: `coop.find` rows' screen boxes must not intersect each
other or the HUD boxes (ui_golden could carry a frame with two partners in one direction).

## History
- 2026-11-11: opened.
