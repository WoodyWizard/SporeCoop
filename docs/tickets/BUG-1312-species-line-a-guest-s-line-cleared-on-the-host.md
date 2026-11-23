---
id: BUG-1312
title: species line: a guest's line cleared on the host stays on the other games (the host never sends an emptied row)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-19
source: lab
role: guest
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w57-tribe-keys
commits: 4ce6f9de
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
FAIL (twin, SPORE_FIX_OFF=species.row_cleared): late_species_rowclear_off 20261119-112300 and 20261119-113824 (step 3:
host 0, guest 7CBC3253: the guest's own game keeps its members in its old creature). Night run before the fix:
late_species 20261119-104136 (same check). PASS: late_species 20261119-112300, on the tree merged with rel/0.7.16
20261119-115724.

## History
- 2026-11-19: opened (fix/w57-tribe-keys, first 0.7.16 lab run).
- 2026-11-19: fixed on fix/w57-tribe-keys.
- 2026-11-23: released in 0.7.16.
