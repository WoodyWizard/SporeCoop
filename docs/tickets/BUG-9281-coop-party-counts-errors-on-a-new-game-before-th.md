---
id: BUG-9281
title: coop.party counts errors on a new game before the planet has a file
status: fixed
kind: bug
stage: Cell
reported: 2026-11-20
source: lane D integration QA (test/integration)
role: host
versions: 0.7.15.94
fixed_in: unreleased
branch: fix/party-new-planet
commits: bcd0eaf7
tests: manual two-instance run (lane B, below)
signature:
docs:
related: BUG-9260
switch: party.wait_planet_file
---

## Report
On a new game the host's `coop.party` showed `errors=3` (and the log "party: not saved: a saved planet and a valid
party are required") although the party was saved a moment later. Harmless, but it reads like a failure.

## Root cause
Observed: party.cpp saves the party (outside the frame) as soon as a seat changes; on a new game the planet has no
.spo file until the game's first save of it, founders::record_party refuses a planet without a file and every such try
counted as an error.

## Fix
party.cpp save_outside: while the planet has no file (founders::planet_saved) the party stays dirty and the next tick
tries again; the waits are counted apart (`file_waits=` in coop.party), not as errors. Switch: `coop.fix
party.wait_planet_file off` = each try counts as an error (0.7.15.94).

## Verification
Lane B, the integration build, a new Cell game from the galaxy menu, the guest joins:
- FAIL with `coop.fix party.wait_planet_file off` (planet Homan): `party: ... saves=4 errors=4`.
- PASS with the fix (planet Orley): `party: ... saves=1 errors=0 dirty=0 file_waits=4`, then saves=4 errors=0 in Creature.
- no-fail-run: the lab's party test uses a saved planet (fixture creature), which never has the gap; FAIL and PASS were
  run by hand on lane B as above.

## History
- 2026-11-20: opened.
- 2026-11-20: fixed on fix/party-new-planet (from feat/lineages-party).
