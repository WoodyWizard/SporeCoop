---
id: BUG-9267
title: A guest's first join of a planet loads the world twice: the share hold sees the host's species before the guest's own creature
status: open
kind: bug
stage: Creature
reported: 2026-11-20
source: Lineages port test (lane A)
role: 
versions: 
fixed_in:
branch: feat/lineages-nests
commits:
tests: tools/tests/coop_home_nests_join.py (suite home_nests_join)
signature:
docs:
related: BUG-9262, BUG-0345
---

## Report
Lane A 2026-11-20 (feat/lineages-nests): a guest with its own creature joins a planet where it has no nest yet. The
host's share hold (BUG-9262, world::set_share_hold) let the first world go after 0.4 s: `home nests: player 1's world
share may go (the host's species, 344 ms)`. The nest was made 7 s later and the guest loaded the world a second time
once the nest had members ("Bringing your nest" twice, what Lineages BUG-9210 wanted to avoid).

## Root cause
Observed (guest log): right after the join the guest sent `sending our creature 065CBE70: 0 record(s)` (the world's
species), its own creature 292846CB only 2 s later. In the galaxy menu its game is in no world, so its creation's
per-world record (worlds.ini, BUG-0345) for the host's world did not apply until that world was loaded; party.cpp took
the world's species as the seat's species and nest_due saw "the host's species".

## Fix
`creations.join_world_record` (guest side): a guest outside a world (no avatar) uses the host's world (the host's latest
state, coop.cpp `host_world`) for its record and choice of this world, and sends no creation until it knows that world.
Its first creature is the one it will play there; the host's hold waits for its nest.

## Verification
Windows lane A, a fresh Boheus (no party file, no nest), the guest with its record for Boheus, driver nests/s5.py
(counts `world: loading Boheus.spo` in the guest's log):
* `coop.fix creations.join_world_record off` on the guest: 2 loads (`may go (the host's species, 250 ms)`, a second
  share 9 s later).
* on: 1 load (`waits (no species yet)`, `waits (its nest is about to be made)`, `waits (made, waits for a member)`,
  `may go (its nest is there, 5391 ms)`, the guest's first send its own creature).
* Lab test coop_home_nests_join.py (new evening, the guest joins from the menu) written, not run on the Linux lab.
  no-fail-run: lab not run; the manual off/on pair above is the fail/pass evidence.

## History
- 2026-11-20: opened (found in the Lineages port test, lane A); fixed on feat/lineages-nests.
