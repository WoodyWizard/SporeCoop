---
id: BUG-1268
title: Lab: the effect workload coop.effects load never stops its effects; a looping one stays as a white disc over the host's creature in the next tests of a warm game
status: fixed
kind: test
stage: Creature
reported: 2026-11-18
source: key-frame review 0.7.15.2
role: host
versions: 
fixed_in: unreleased
branch: rel/0.7.15.2
commits: c9983d95
tests: effects_live, creature_brain_hp
signature:
docs:
related: BUG-0451, BUG-1252
---

## Report
Key-frame review of the 0.7.15.2 final check (lab 20261118-144132, sheets 025/026 of creature_brain_hp): on the
host's screen a huge white disc with a hole hides the host's own creature in both frames; the guest's screen is
clean, the test passes. Reproduced only when effects_live ran before on the same warm games (lab 20261118-165321-2
and 20261118-165900-2; creature_eat, creature_kills, creature_aggression before it: no disc; creature_brain_hp
alone 4 times: no disc). Not a player problem: `coop.effects load` is a test and perf command.

## Root cause
`load_tick` (mods/coop/effects.cpp) creates `n` effects at the local player every period through
`game::create_local_effect` and kept the pointers nowhere: never stopped, never released. effects_live loads the
looping id E1B24DDB (20/s for 30 s) and only checked the guest's relayed copies, which the live list stops after 8 s.
The host's own ~600 copies stayed active for the session and a warm reset does not tear the creature world down.

## Fix
The load's effects go into a list of their own (`lload_add`, the same rules as the relay's live list): stopped and
released after 8 s or at the cap of 64, dropped without a call on a stage or world change. Kept apart from the relay's
live list so their upkeep counts as the workload's cost: in the relay's list effects_party_civ measured the relay at
1.1 % (lab 20261118-170746, limit 1 %). `coop.effects` prints `effects load live: live= stopped=`. Switch `effects.load_stop` (off: the old behaviour).
effects_live checks the host side too: its live list reaches the cap during the load and is back at 0 after.

## Verification
* FAIL with `SPORE_LAB_FIXOFF=effects_live=effects.load_stop`: 20261118-171400 ("the host's own load effects are
  listed and stopped (max 0, now (0, 0))").
* PASS with the fix: 20261118-171400-3 (load list max 64, after the load (0, 620); creature_brain_hp after it on the
  same warm games: no disc, the white share of the centre 0.0 against 0.89 before). The effects tests twice each
  (party in all stages, release, validate, creature_effects): 16 of 16, 20261118-171400-2.

## History
- 2026-11-18: opened from the key-frame review of the 0.7.15.2 final check; fixed the same day.
