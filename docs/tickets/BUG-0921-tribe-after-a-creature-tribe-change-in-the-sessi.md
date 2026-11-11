---
id: BUG-0921
title: Tribe: after a Creature -> Tribe change in the session the guests' roster shows white squares for every member, the host's roster has the portraits (campaign, campaign_ct)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: guest
versions: 0.7.10, 0.7.11 (and before: BUG-0643)
fixed_in: unreleased
branch: fix/w47-roster-portraits
commits: 5c49c9f0
tests: roster_portraits
signature:
docs: docs/re/tribe.md#tribe-portraits-session-species
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
Observed (lane 7, Cell -> Creature -> Tribe with 2 players, the campaign's own stage changes): the guest's player tribe
has the host's species key (291E2509 on both games). The roster draws the png {0x2F7D0004, 0x40626200, species}: the
host's game answers it (`coop.creations png 291E2509` known=1), the guest's does not (known=0), and every member slot
of the guest is a plain white square. The guest has the picture only under the content key of the host's own species
(7D573565).

Observed (code): the creature the host plays reaches a guest twice. Its creation bundle (share_own, make_bundle:
"player 0 creature = ... (2 records)", bem and metadata, no png) is registered under the host's own key at priority
2000. The same creation as an own species of the host's world (host_species_out, 4 records with the png, the
BUG-0643 fix `creations.png`) is registered under a content key, and under the host's key only when nothing answers
for that key: the bundle's package already does. So under the key the roster asks, the guest's game had the model
and no picture. BUG-0643's test set another creation of the host on the tribe (never sent as a bundle) and so passed;
the players' case (the tribe is of the creature the host plays) was never fixed. Not specific to a species "made in
the session": any host creature of the host's My Creations; the Creature fixture's species is a premade every game
has, which is why campaign_tribe showed portraits.

## Fix
`creations.bundle_png` (mods/coop/creations.cpp make_bundle): a player's creation bundle carries the creation's
pictures (png of the model group and of the thumbnail group +1, up to 1 MB each; never pictures without a model). The
guest's package of the host's key then answers the picture, for every version of the host's creature. Off: the model
and the metadata only (0.7.11).

## Verification
Test roster_portraits (coop_roster_portraits.py, 6 min, cell fixture): Cell -> Creature -> Tribe by the campaign's
stage changes with 2 players, then both games are shot full size; the roster's slots are found by their frame and a
slot whose inside is plain white is a white square. Checks: the guest's game answers the png of its tribe's species,
the guest's screen shows the roster, no slot is white (host too).
- FAIL (SPORE_FIX_OFF=creations.bundle_png): out/lab/20261111-065944 (the picture unknown on the guest, 2 of 3 slots
  white: the chief's icon and two white members), out/lab/20261111-064706 (half-size shots: 1 of 2 found slots white).
- PASS: out/lab/20261111-065948, out/lab/20261111-070559, out/lab/20261111-065323 (half-size shots).
- Four players: campaign_ct out/lab/20261111-070555 (PASS): the key frames `05_play-tribe_*` show 14 portraits on each
  of the three guests and on the host (by eye), `04_advance-creature_*` too.
- Other tests: tribe_portraits, evolve_in_place_creature (out/lab/20261111-070559), `make test`.
- By eye: the key frames `01_roster_guest.jpg` of the FAIL runs show the white squares, of the PASS runs the purple
  creature on its card as on the host.
- Not verified: a guest of 0.7.11 or older with a 0.7.12 host (it registers every record of a bundle, so it should get
  the picture too); the picture after the host edits its creature again in Tribe or later (the bundle of the new
  version carries its png; whether a roster on the screen takes a new picture without a load was not tried).

## History
- 2026-11-11: opened.
- 2026-11-11: fixed in fix/w47-roster-portraits (creations.bundle_png).
