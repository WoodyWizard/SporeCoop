---
id: BUG-0410
title: tribe: the player tribe's citizens on the guest 1.9-2.0 m (p90) from the host's in play_tribe (limit 1.5 m)
status: open
kind: desync
stage: Tribe
reported: 2026-11-01
source: play_tribe, labs 20261101-134020 and 20261101-145528 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`play_tribe` (10 min of two bots playing Tribe by real clicks, tribe fixture) failed `sync t*: 90% within 1.5 m`
in both of its runs on main a05d1f6 (lane 10): `sync citizens: samples 435 pos p50 0.16 p90 1.89 max 7.15 m`
(lab 20261101-134020, seed 490568738) and `samples 343 pos p50 0.30 p90 1.98 max 4.97 m` (lab 20261101-145528,
seed 789470738). Animals in the same runs: p90 1.04 / 0.98 m. Each sample is the guest's position of a citizen
against the host's positions read right before and right after it (the nearer one counts), so a plain
interpolation delay is mostly inside the bracket.

How to reproduce: `SPORE_LANE=N python3 tools/tests/play_tribe.py --minutes 10 --seed 789470738`.

## Root cause
Not analysed. Candidates (inferred): the mirrors' interpolation since BUG-0365 (tribe.mirror_time: the guest
follows the host's states by the host's clock, adding the buffer delay to the trail), or members walking on
orders (the bots give many) whose path the guest's mirror cuts. A run with `SPORE_FIX_OFF=tribe.mirror_time`
would separate the two. play_tribe's two earlier FAIL runs (fix/w32-tribe-sync, 2026-11-01) are not on disk.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
