---
id: BUG-1405
title: tests: an extra guest (Carol, mp1/mp2 profile) keeps its hold records and settings from an earlier test (party_space_names: 6 charges, not 3)
status: fixed
kind: test
stage: Space
reported: 2026-11-21
source: final check 20261121-003202 (party_space_names)
role: guest
versions: unreleased
fixed_in: unreleased
branch: fix/w62-flake-tcs
commits: d3303a4d, 84c8f9e6
tests: party_space_names (tools/tests/coop_party_space.py)
signature:
docs:
related: BUG-0679, BUG-1034, BUG-0762, BUG-1149
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 6, cold): party_space_names "[gift] the second Bob's game has the
tool with 3 charges (6)". History: 1 of 4 failed on rel/main, 3 of 11 on all branches since 2026-10-30; the earlier
failures (20261030, fix/w9-tests) were other ones (None: the gift never arrived). Not a 0.7.16 or w61 regression: the
mod's behaviour is as designed, the test's start is not clean.

## Root cause
* Observed (party_space_names.extra1.game.log, the second Bob = Carol's game, profile mp1 of lane 6): s 24.0 "trade: a
  gift from Bob waits for the own hold's restore (BUG-1034)", s 25.5 "own hold back: 12 items", "own hold from this
  game's record of Alice|madas.spo (BUG-0679)", then "Bob gave you 3 x Anti-Matter Bomb": 3 charges given, 6 in the
  hold. The previous run on that lane (out/lab/20261120-133703, lane 6, PASS) restored 8 items from the record and ended
  with 3 bombs; the record file (lane 6 bottle, SporeKit-mp1/Spore/SporeCoop/holds/4E816FAF.txt, "world
  Alice|madas.spo") now reads `item 2 144DF50A ... 1 6` after this run. So the restore gave back 3 bombs of the last
  run and the gift added 3.
* Cause (tests): twoinst's start clears the host's and the first guest's hold records, badges, settings and the
  per-player store (forget_holds, forget_settings, forget_store, choices.ini / worlds.ini) before each test; the extra
  guests of coop_multi.start_extra (Party and every 3-4 player test) only lost their saves (`Spore/Games`). Every Space
  party test that ends with something in an extra's hold changes the next one on that lane.

## Fix
Test harness (tools/tests/coop_multi.py `forget_extra_records`): an extra guest's profile also loses SporeCoop/holds,
badges, settings, choices.ini, worlds.ini, Games-sporecoop-store and -backup before it starts, as the first guest's.
`SPORE_EXTRA_RESET_FIX=0`: only the saves go (the old start). party_space_names itself is unchanged (it asserts 3).

## Verification
FAIL (no change): final check out/lab/20261121-003202 (party_space_names on lane 6: 6 charges).
Was waiting for the lab run (lane 6 first: its mp1 record holds 6 bombs now):
* FAIL twin: `SPORE_EXTRA_RESET_FIX=0 SPORE_DETECTOR=20 python3 tools/lab.py run party_space_names --lane 6 --retry 0
  --no-build` (9 charges: the record's 6 + 3).
* PASS: then `SPORE_DETECTOR=20 python3 tools/lab.py run party_space_names --lane 6 --retry 0 --no-build` twice (the
  second run starts after a record of 3 bombs).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): party_space_names: the twin SPORE_EXTRA_RESET_FIX=0 FAIL in 20261122-193040 (the second Bob's tool with 6 charges), then PASS in 20261122-193139, 20261122-194210, 20261123-065537. Closed.

## History
- 2026-11-21: opened; fixed on fix/w62-flake-tcs (the extras' profile records cleared), waiting for the lab run.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
