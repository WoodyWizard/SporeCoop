---
id: BUG-9271
title: Galaxy Tribe planet: more than about 21 villages (16 players and natives) and guests get no Tribe state
status: fixed
kind: bug
stage: tribe
reported: 2026-11-19
source: alpha review 2026-11-18 W4
role: guest
versions: 0.7.14.1-lineages64-dev
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 8cb56580
tests: manual on TITAN QA 8 + 9 (coop.tribe villages_apart on: the table always on its own)
signature: "tribe state: N villages do not fit one packet ... NOT SENT"
docs: docs/13-alpha-review-2026-11-18.md#review-scaling
related: BUG-9266
---

## Report
2026-11-18 review (W4): MSG_TRIBE_STATE carried the whole village table in every chunk. About 21 villages (the galaxy's
default 16 players and 5 native tribes) filled a packet: above that the host sent no Tribe state at all (guarded since
the review with an error line), so guests on a full Tribe planet saw nothing move.

## Root cause
✔ The table (13 bytes a village, 8 more with a food store, 13 species info, 2 native chief) is repeated in each chunk
beside the members (45 bytes each) under one 1200-byte packet.

## Fix
When the table would leave room for fewer than 4 members, the host sends it on its own (MSG_TRIBE_VILLAGES = 98, in
parts of up to about 30 villages, each cycle) and the state's chunks say 0xFF where the village count was; the guest
keeps the last complete table (at most 20 cycles old) and reads the chunks against it (the same per-village food,
species info and native chiefs). The 32-village limit of the table's byte indices stays. Switch
`coop.fix tribe.villages_apart`; `coop.tribe villages_apart on` forces it for tests.

## Verification
no-fail-run: 22 or more villages need more players than this rig runs (3 games). Manual, TITAN, the galaxy Tribe
planet Clyth (7 villages), QA 8 host, QA 9 guest, `coop.tribe villages_apart on` on the host: 105 table parts sent in
20 s, the guest got 107 and read 420 chunks against the table (7 villages), it kept its 41 mirrored members and the
buildings' health kept coming; no error in either log.

## History
- 2026-11-19: opened and fixed.
