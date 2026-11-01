---
id: BUG-0190
title: coop_sync.py space: the ships' p90 sits near its 10 m limit
status: open
kind: test
stage: Space
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* 2026-10-23, 4 runs: ships p90 7.4-10.6 m (one run over the limit), single samples up to 690 m.
  Ships fly fast and the test reads host and guest one after the other, so most of it is the time
  between the two reads plus the interpolation delay; the 690 m sample is the first sample after the start:
  the host's new ship still stands at its default spot (0, 0, 699) for that moment (play_space.py
  saw one 412 m sample in 2 h). The p90 of real play is
  1.2-8.3 m (docs/stability/space.md). Since then the test compares the guest with the host's path between
  its two reads (the guest is read in between); if it still fails, look at the samples, not at the limit.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached: not measured with --repeat. Left open.
- 2026-11-01: fix/w31-space-gate: coop_sync.py reads `coop.sync` after the mod's write (the mirror's target, not what is drawn). What is drawn, measured with `coop.fleet track` (coop_space_drawn.py): mirrors of the host's NPC ships 0-2.5 m from the ship's path over 0.5 s (p90) in the start system and on the planet, single rows up to 16-28 m at 23-37 m/s (lab 20261101-093823, -094339, -094711). A mirror now also skips the game's controller update and is pinned minus its offset (`coop.fix fleet.mirror_pin`): in the galactic core the controller would draw it 20-30 m off as for the puppets (BUG-0378; mirrors in the core not measured). sync in lab 20261101-103510: ships p90 1.44 m (1 run). Left open: not measured with --repeat, no FAIL run of fleet.mirror_pin (no host NPC ship in the core in any test).
