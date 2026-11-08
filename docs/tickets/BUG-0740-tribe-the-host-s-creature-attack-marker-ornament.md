---
id: BUG-0740
title: Tribe: the host's creature attack marker (ornament model 0x356EB8A, minimap blip) is not shown on the guests
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-08
source: BUG-0528 triage (fix/w46-n2-desyncs, lab 20261108-041234, 20261108-050749)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-attack-marker
commits: 1a39ebc8 c9bb2c7a
tests: tribe_attack_marker, tribe_attack_marker_rejoin; campaign_tribe (SPORE_DETECTOR=20, the row Tribe:unmatched:ornament)
signature:
docs: docs/re/tribe.md#tribe-attack-marker, docs/re/tribe.md#tribe-spot-markers, docs/06-testing.md#testing-attack-marker
related: BUG-0528, BUG-0582, BUG-0465, BUG-0486
---

## Report
The divergence detector's `Tribe:unmatched:ornament` / `host_only:ornament` (BUG-0528): one ornament of noun
0x18C88E4 in the host's game that no guest has. Seen in 3 of 16 four-player campaigns into Tribe on 2026-11-08 (lab
20261108-040233 campaign_tribe_share#2, 20261108-041234 campaign_tribe_share#2, 20261108-050749 campaign_tribe#3), always
the same object: model 0x356EB8A, a new serial, at the same place of the campaign planet (-177 -445 -143), ~60 m from the
player village. What a guest misses: the host's marker of a creature attack on the village and its minimap blip.

## Root cause
Observed (lab 20261108-050749 campaign_tribe#3, `probe 0x00C911C0` / `probe 0x00C9B140` on the host from the Tribe
arrival, `CAMPAIGN_HOST_CMDS_TRIBE`): the one call of 0xC911C0 during the play came with the ornament row, from
`SporeCoop::SpotRecord_CellId` 0xC9B140 (+0x66F), called with make = 0 from the creature attack behaviour's start
0xD73930 (return 0xD739E0; the Drama Manager's kind 7, docs/re/tribe.md#tribe-attack-spot). 0xC911C0 (Ghidra) runs in
Tribe only, on the global 0x157ADB0: it makes the ornament (+0x28, once, then reuses it) with model 0x356EB8A at the
attacked place, a noun 0x2E72CAE object (+0x24), and adds a minimap blip (cTribeUI vt 0x6C, 0xCC98C0). The creature
attack runs in the host's game only (its animals reach the guests as proxies), so no guest game makes the marker.
Not reproduced on the tribe fixture: `coop.tribe form` (held by the pace rule) + `coop.drama start 7` started and ended
an attack (active 1 -> 0 in 150 s) without a 0xC911C0 call or a new / moved marker (lab 20261108-052511, -052727; the
draft test was dropped). The condition under which the attack start reaches the marker path is not known.

CORRECTION (2026-11-08, fix/w46-attack-marker; Ghidra + lab lane 9, docs/re/tribe.md#tribe-spot-markers): 0x157ADB0 is
not a global: it is a record of the spot record table `SporeCoop::SpotRecord_Table` 0x157ABB0 (a fixed vector with inline
storage). Every spot record has its own marker: SpotRecord_CellId (which does not read `make`) returns the record found
within +-4 cells of the asked place, or makes one at the first shore cell (terrain kind 3 of 0xB83110, shallow water)
and places its marker on it. The villages' records are made by both games; a record for a creature attack's place
without one near is made by the attack's start, in the host's game only. Why the fixture's attack made none: it hits
the player village, whose record exists; a village formed during play lies inland (no shore cell near: no record at
all, also BUG-0582's missing record). Records (and markers) go only with the whole table (0xC9A210 at the stage modes'
enter / exit, SpotRecord_Release 0xC8F150 each): the game has no removal "when the attack ends".

## Fix
`wsync.tribe_marker` (mods/coop/wsync.cpp, row 15 `tribe_marker`): the host's row lists its spot records that have a
marker (cell id, the record's position, the place SpotRecord_CellId was asked for: a hook on it remembers that per new
record). A guest without the cell's record asks its own game for the same place outside the frame (the scan depends
only on the planet's terrain: the same cell, record, marker ornament of model 0x356EB8A and minimap blip, 0.00 m apart),
or places the marker on its record of that cell (TribeMarker_Place); a marker it made for a host record that is gone is
released (SpotRecord_Release). A joining guest gets the row at once. `coop.fix wsync.tribe_marker off`: as before.

## Verification
`tribe_attack_marker` (coop_tribe_attack_marker.py, lane 9): the host's game asked for places round the village until
it makes a record (121 m away, a shore cell), then the guest's game must have the cell's record with the marker at the
same place and an ornament there. FAIL with `SPORE_FIX_OFF=wsync.tribe_marker`: out/lab/20261108-061201 (the guest has
no record after 20 s, no ornament). PASS: out/lab/20261108-061051, 20261108-061349. `tribe_attack_marker_rejoin`
(the guest rejoins after the marker is made): PASS out/lab/20261108-062026. play_tribe PASS 20261108-062153,
detect_tribe PASS 20261108-063308.
Not reproduced through the game's own attack on the fixture (`coop.tribe form` with the pace rule off + `coop.drama
start 7`: no new record in 151 s, lab 20261108-061613): see the root cause.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-n2-desyncs): the object and its maker found (BUG-0528 triage); left open for a fix.
- 2026-11-08 (fix/w46-attack-marker): the marker is per spot record; the wsync row tribe_marker gives the guests the host's records and markers; tests tribe_attack_marker(_rejoin).
