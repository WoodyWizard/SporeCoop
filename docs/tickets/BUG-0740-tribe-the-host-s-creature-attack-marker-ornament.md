---
id: BUG-0740
title: Tribe: the host's creature attack marker (ornament model 0x356EB8A, minimap blip) is not shown on the guests
status: open
kind: bug
stage: Tribe
reported: 2026-11-08
source: BUG-0528 triage (fix/w46-n2-desyncs, lab 20261108-041234, 20261108-050749)
role: guest
versions: 
fixed_in:
branch: fix/w46-n2-desyncs (triage only)
commits:
tests: campaign_tribe (SPORE_DETECTOR=20, the row Tribe:unmatched:ornament)
signature:
docs: docs/re/tribe.md#tribe-attack-marker
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

## Fix
None yet. A fix gives the guests the marker: the host relays the attack's place when 0xD73930 starts (or its marker's position and its end), the guest calls 0xC911C0 on its own global (the minimap blip comes with it) and removes it when the host's attack ends. Its removal path in the game is not known yet.

## Verification
A reproduction: campaign_tribe with SPORE_DETECTOR=20 (about 1 in 5 runs). A fixture test needs the condition above.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-n2-desyncs): the object and its maker found (BUG-0528 triage); left open for a fix.
