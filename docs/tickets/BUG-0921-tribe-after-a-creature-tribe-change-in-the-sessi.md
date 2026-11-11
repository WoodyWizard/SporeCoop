---
id: BUG-0921
title: Tribe: after a Creature -> Tribe change in the session the guests' roster shows white squares for every member, the host's roster has the portraits (campaign, campaign_ct)
status: open
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0643, BUG-0424, BUG-0431
---

## Report
Key-frame review of the final check out/lab/20261111-044257 (rel/0.7.12 2f8f6ba4), 4-player campaigns.
Sheets: `out/lab/20261111-044257/frames/sheets/015_campaign_ct_05_play-tribe.jpg` and `out/lab/20261111-044257/frames/sheets/014_campaign_ct_04_advance-creature.jpg`; the same in
`out/lab/20261111-044257/frames/sheets/004_campaign_04_advance-creature.jpg` and `out/lab/20261111-044257/frames/sheets/005_campaign_05_play-tribe.jpg`.
Frames: `out/lab/20261111-044257/frames/campaign_ct/05_play-tribe_guest-Bob.jpg` (14 white squares), `..._host-Alice.jpg`,
`04_advance-creature_guest-Carol.jpg` (2 white squares), `04_advance-creature_host-Alice.jpg` (2 portraits).

* Host (Alice): the tribe roster on the right edge shows the members' portraits (the purple creature on a grey card).
* Every guest (Bob, Carol, Dave): each member slot of the roster is a plain white square; the chief's icon above
  them and the slots with a state icon (the red cross of a hurt member) are drawn normally. Right after the
  Creature -> Tribe change there are 2 white squares, at the end of the Tribe play 8 to 14 (every member).
* Not in campaign_tribe (sheets 023, 024: it starts from the Creature fixture, the red species): portraits on all
  four screens. The white squares appear only where the species was made in the session (Cell -> Creature -> Tribe,
  the purple creature).
* The same in the earlier runs that have these frames: lab 20261111-000117 (rel/0.7.12) and 20261111-002733
  (fix/w47-lab-memory): white on every guest, none on the host (counted: near-white pixels in the roster column, 0 on
  the host, 380 to 2700 on the guests).

## Root cause
Not analysed. Inferred: BUG-0643 again by another way. The roster portrait is the species' png
(docs/re/tribe.md#tribe-portraits); `creations.png` (0.7.10) sends the pngs a creation has in the host's packages.
A species the campaign makes in the session (the stage change's creature, not saved through an editor into My
Creations) may have no png record to send, while the host's game draws its own thumbnail; the guests then show the
failed-image square. BUG-0643's Verification already says the portrait on the screen was never checked.
Older than 0.7.12 by this reading (no key frames exist from before 2026-11-10 to prove it).

## Fix
Open.

## Verification
Open. How to test: campaign_ct up to the Tribe play, then on a guest `coop.creations` for the tribe's species
(is there a png record?) and a screenshot of the roster column. The check the campaign lacks: after the
Creature -> Tribe change no member slot of a guest's roster is a white square (the roster column of the key frame:
near-white pixels 0 on the host).

## History
- 2026-11-11: opened.
