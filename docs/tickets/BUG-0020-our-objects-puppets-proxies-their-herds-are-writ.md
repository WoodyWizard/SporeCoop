---
id: BUG-0020
title: Our objects (puppets, proxies, their herds) are written into saves
status: fixed
kind: bug
stage: Creature
reported: 2026-10-25
source: analysis (BUG-0008)
role: host
versions: all
fixed_in: 0.7.3.0
branch: fix/w3-cell
commits: 45acba3, 0d79bae, 2be1123, 3699255, 0a6d447
tests: save_hygiene (creature_e84 makes its save with coop.savestrip 0)
signature: (fix side effect seen and handled) dc04aee5:0xB0EC8C read 0 (cGameBehaviorManager update, a null entry)
docs: docs/re/creature.md#creature-save-strip, docs/known-issues.md
related:
---

## Report
Every host save keeps the partners' puppets and the herds made for them; they are only removed after a load (BUG-0008).

## Root cause
Nothing keeps our objects out of the serializer (COMSerializer::SaveClassObjects 0x69FB90).

## Fix
Reverse engineered on 2026-10-28 (docs/re/creature.md#creature-save-strip): SaveGame 0xB29000 is
synchronous; the noun manager writes every noun of its list, and every reference goes through
`COMSerializer::Write` 0x6A05F0, which queues the object for its record. A hook there
(`mods/coop/savestrip.cpp`) writes a null reference for our objects, so they are never queued or
written and the world is not touched: herds without an archetype, a nest or the avatar's flag (made
by CreateHerd for puppets and proxies) and every creature in them. The set is built once per save
from the live noun list (also each object's Cast(0x179C807) pointer), main thread only. Null entries
load as null; the noun list skips them, but cGameBehaviorManager keeps them in its update vector
(+0x20) and its update faulted at 0xB0EC8C every frame: a hook on its Read 0xB0E570 drops them.
`coop.savestrip 0|1` keeps the old behaviour for tests. Not stripped (a null member of a game herd
was not tried): a puppet in the avatar's herd (the fallback for an unknown creation) and a guest's
proxies in the game's nest herds in a guest's own save.

## Verification
Two-game test `tools/tests/coop_save_hygiene.py` (suite `save_hygiene`): the host saves with the
guest's puppet, both games restart, the host loads alone with the stray removal off and counts in
its game: creatures without tuning, herds without an archetype, faults.
- FAIL with `SPORE_SAVE_FIX=0` (`coop.savestrip 0`): out/lab/20261028-005259 (1 creature without
  tuning, 1 herd without an archetype).
- With the first version (no behavior manager hook): out/lab/20261028-005430 and -005551 counted 0
  and 0 but the host faulted 155 times at 0xB0EC8C after the load.
- PASS with the fix: out/lab/20261028-005950, out/lab/20261028-011940 (in the regression run) and
  out/lab/20261028-015652 (final code; `last_ours=2 nulled=9`, `behavior_nulls_dropped=1`, no fault).
- creature_e84 now makes its save with `coop.savestrip 0` (it tests saves made before the fix):
  FAIL in out/lab/20261028-011940 before that change, OK in out/lab/20261028-015652.

## History
- 2026-10-25: reported (analysis (BUG-0008)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: fix/w2-world (wave 2) read the stopped WIP branch fix/save-hygiene (bea0f3e): it is only
  the interface `mods/coop/savestrip.h` (init, per-mode filters, a switch, a "saving" probe), no
  implementation, no hook, no filter, no test. Its design (hook the serializer's reference writer and
  write a null reference for our objects, so the world is never touched) is plausible but unverified:
  which function writes an object reference is not known (`Simulator::COMSerializer::Write`
  0x6A05F0 in march2017_dc04aee5.sym is a name only, signature and role not checked), and a null
  reference where the game expects an object may fault on load. Needs reverse engineering of
  SaveClassObjects 0x69FB90 first, then `coop_save_hygiene.py` (host saves with a guest puppet,
  restart, load: no creature of ours in the world). Not reached in wave 2 (budget went to the data
  loss tickets); left open, the WIP branch is not merged.
- 2026-10-28: fix/w3-cell (wave 3): reverse engineered the save (synchronous; the closure of references
  from the noun list), fixed with a null-reference hook on COMSerializer::Write and a cleanup of the
  behavior manager's list after a load; test FAIL without the fix, PASS with it.
- 2026-10-28 (merge of wave 3, main): the first version stripped every creature of a herd without an archetype. `creature_desync` recruits a console-spawned NPC into the host's pack; it was stripped, the pack kept a null member and the loaded game faulted thousands of times (lab 20261028-021701, 20261028-023605: 0/2). Now only puppets and proxies (never pack members) are stripped, and their herd only when every creature in it is stripped (`coop.savestrip owner 0` restores the first version). creature_desync, save_hygiene, creature_e84, creature: OK (lab 20261028-024059).
- 2026-10-28: released in 0.7.3.0.
