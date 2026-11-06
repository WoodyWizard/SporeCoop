---
id: BUG-0410
title: tribe: the player tribe's citizens on the guest 1.9-2.0 m (p90) from the host's in play_tribe (limit 1.5 m)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-01
source: play_tribe, labs 20261101-134020 and 20261101-145528 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in: unreleased
branch: fix/w45-remote-bodies
commits: de9c4d8d, 261e990c, a71e319b, 440a5c2c, 391562e1, 29330917, 391562e1, 29330917
tests: body_trail_tribe, body_trail_tribe_player, play_tribe
signature:
docs: docs/re/tribe.md#tribe-remote-lead
related: BUG-0425
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
Observed (2026-11-06, fix/w45-remote-bodies, body_trail_tribe): the guest's mirrors of the player tribe's members are
0.21-0.22 s (p50) behind the host's members, p90 2.04 m while they walk (out/lab/20261106-013022): tribe.mirror_time's
interpolation delay (kMirrorDelay 0.2 s) plus the one-way latency. The first candidate of this ticket. Members walk at
up to 9 m/s, so their p90 is the largest of all kinds.

## Fix
drive.lead (BUG-0425): the mirrors are shown where the host has its members now (net::SnapshotBuffer lead).

## Verification
FAIL out/lab/20261106-013022 (body_trail_creature p90 0.78 m, body_trail_tribe animals 1.17 m / members 2.04 m), out/lab/20261106-014253 (body_trail_tribe_player 1.73 / 2.71 m, stretches 1.75 / 3.55 s), out/lab/20261106-020354 (final code: all four FAIL: creature 0.71, tribe 1.20 / 2.07, tribe_player 1.69 / 2.76, player 1.13 m); PASS out/lab/20261106-012156 (creature 0.12 m, tribe 0.37 / 0.64 m, civ 0.93 m), out/lab/20261106-014554 (player 0.33 m, tribe 0.43 / 0.66 m), out/lab/20261106-021152 (final lead code: creature 0.12-0.4, player, tribe_player), out/lab/20261106-035710 (body_trail_tribe, body_trail_creature on the final code) (members p90 0.64-0.66 m clean, 1.28-1.45 m over the player network). play_tribe (out/lab/20261106-021152): `sync citizens: raw (the host now)` p90 0.64 m (0.7.8 rel runs: 1.89-1.94 m).

## History
- 2026-11-01: opened.
- 2026-11-06 (fix/w45-remote-bodies, M1): root cause observed (the 0.2 s delay), fixed by drive.lead.
