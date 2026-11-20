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
branch:
commits:
tests:
signature:
docs:
related: BUG-9262
---

## Report
Lane A 2026-11-20 (feat/lineages-nests): a guest with its own creature joins a planet where it has no nest yet. The
host's share hold (BUG-9262, world::set_share_hold) let the first world go after 0.4 s: `home nests: player 1's world
share may go (the host's species, 344 ms)`. The nest was made 7 s later and the guest loaded the world a second time
once the nest had members ("Bringing your nest" twice, what Lineages BUG-9210 wanted to avoid).

## Root cause
Observed: at the join the guest reports the world's species (the host's) first: in the galaxy menu its game is in no
world, so its own creation's per-world record (worlds.ini, BUG-0345) does not apply until the host's world is loaded;
party.cpp takes that as the seat's species and nest_due sees "the host's species". Inferred: the same on every first
join of a planet; a returning player whose nest is in the save loads once (seen: "its nest is there").

## Fix
Not fixed. Ideas: a joining guest uses its record for the host's world id (known from the host's state) before its
world is loaded, or the party keeps a seat's own species while its player is not in the world yet.

## Verification
<!-- the test: FAIL without the fix, PASS with it -->

## History
- 2026-11-20: opened (found in the Lineages port test, lane A).
