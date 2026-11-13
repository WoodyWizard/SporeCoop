---
id: BUG-0981
title: key frames: civ and tribe capture tests shoot the home city, not the city or village they check
status: fixed
kind: test
stage: civ
reported: 2026-11-12
source: lab-final-0.7.13
role: both
versions: 0.7.13
fixed_in: 0.7.13
branch: fix/w48-fmisc
commits: ea7552b3
tests: civ_away_capture, civ_buy_city, civ_capture_credit, civ_capture_keeps, civ_owners_reload, tribe_away_conquest, tribe_conquest_guest
signature:
docs: docs/06-testing.md#testing-keyframe-framing
related: BUG-0982, BUG-0983, BUG-0924
---

## Report
Key frames 025, 026, 028, 029, 030 (civ capture / buy / reload tests) and 058, 059 (tribe conquests) of the 0.7.13
final check, lab 20261112-175447, show the capital or the home village, not the city or village the test is about:
the player colours and the fallen hut cannot be checked by eye. 026 / 028 / 029 / 030: the host's specialty question
("You've bought out this ... city") covers the screen and pauses the host (030: still up after the load). 027: a
unit tagged "Alice" stands in the guest's captured city.

## Root cause
Test artefact: the tests never moved the cameras; each game kept its start view on the capital / home village. The
specialty question is the game's own for a bought or captured city (for the guest's purchase it comes up on the
host: BUG-0982, open). 027 is no bug: the unit is the host's vehicle, which civ_capture_credit puts by the city hall
on purpose (`coop.civ vput`, "a host vehicle nearer"); the arrow "Alice 398 m" points at the host's view point.

## Fix
Test only: `play_civ.frame_city` (answers a specialty question still up, then pans each game's camera onto the city
with W/A/S/D) and `play_tribe.frame_point` (centres each view through the minimap) before those key frames.
civ_owners_reload frames the guest's bought city (the two are ~600 m apart). civ_buy_city prints which game showed
the specialty question (evidence for BUG-0982).

## Verification
no-fail-run: key frame framing, not a check. Before: out/lab/20261112-175447 sheets 025-030, 058, 059. After:
out/lab/20261112-212739 (5 civ tests OK, every camera reached the city, e.g. civ_buy_city / civ_capture_credit /
civ_owners_reload frames show the city with the [Bob] tag on both screens; civ_buy_city printed `specialty question:
host Military | guest None`), out/lab/20261112-212603 and 20261112-213006 (tribe conquests OK, the fallen hut in
view on both).

## History
- 2026-11-12: opened from the 0.7.13 final check key frames; framing helpers (fix/w48-fmisc).
- 2026-11-12: re-check (out/lab/20261112-225741, sheet 032): tribe_conquest_guest frames the fallen hut on the guest; the host's camera still stays far from the village (only the smoke over the burnt spot shows). Test framing on the host's side, follow-up for 0.7.14.
- 2026-11-12: released in 0.7.13.
