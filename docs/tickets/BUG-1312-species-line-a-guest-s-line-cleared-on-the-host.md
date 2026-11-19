---
id: BUG-1312
title: species line: a guest's line cleared on the host stays on the other games (the host never sends an emptied row)
status: open
kind: desync
stage: Tribe
reported: 2026-11-19
source: lab
role: guest
versions: 0.7.16 (unreleased)
fixed_in:
branch: fix/w57-tribe-keys
commits:
tests: late_species (step 3), late_species_rowclear_off (twin)
signature: late_species: "without a record of this world the guest's old choice does not hold" {'host': 0, 'guest': <0x7C key>}
docs: docs/01-architecture.md#arch-species-line
related: BUG-1270, BUG-1289, BUG-0638
---

## Report
Lab 20261119-104136, late_species step 3: after the guest's world record was forgotten (BUG-0638: it plays the
tribe's species again) the host answered no Tribe line for the guest and drew its members in the tribe's species, the
guest's own game still answered its old creature's key 7CBC3253 and drew its members in it.

## Root cause
✔ (code, species_line.cpp) The guest's own_tick sends its row with creature 0 (empty). The host's take_row erases an
empty row, and host_tick broadcasts only the rows it still has: an erased row was never sent, so every other game (the
owner's own, which resolves its line from the host's echo, and every other guest) kept the old row.

## Fix
`species.row_cleared`: host_tick sends an empty row once for a player still in the session whose last sent row was not
empty and who has no row any more; the receivers erase it (take_row). Off: not sent (0.7.16 before the fix).

## Verification
RUNIDS

## History
- 2026-11-19: opened (fix/w57-tribe-keys, first 0.7.16 lab run).
- 2026-11-19: fixed on fix/w57-tribe-keys.
