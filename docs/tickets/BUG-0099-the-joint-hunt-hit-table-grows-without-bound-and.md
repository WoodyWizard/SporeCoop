---
id: BUG-0099
title: the joint-hunt hit table grows without bound and keys hits by reusable creature addresses
status: fixed
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#77
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-creature
commits: 8de7ca7
tests: unit
signature:
docs: 
related: #77
switch: team.hunt_hits
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/77 (full text, discussion).

teamplay keeps recent player hits per target creature in a `std::map<void*, std::vector<Hit>>`. Old hits are pruned only for the target being hit right now. The size cleanup only removes entries that are already empty, which never happens for targets nobody hits again. In a long creature-stage session, every creature ever hit stays in the map. After 256 entries, every hit walks the whole map and removes nothing. Keys are raw creature addresses, so a new creature allocated where a dead one was inherits its recent hits. Severity: minor (slow growth; a rare wrong joint-hunt bonus).

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/teamplay.cpp#L35-L58

```cpp
auto& v = g_hits[target];
for (...) if (now - v[i].t > kHuntWindow) v.erase(...);   // only this target's hits
v.push_back({player, now});
if (g_hits.size() > 256)
    for (...) it = it->second.empty() ? g_hits.erase(it) : std::next(it);  // others are never empty
```

### How to reproduce
Creature stage, two players hunting for an hour (or a test bot hitting many NPCs: `coop.npc hosthit` over many ids). There is no status output for the map size; a debug print of `g_hits.size()` keeps rising past 256.

### Expected
The table holds only hits within `kHuntWindow`, and is cleared on a world change and at session end.

## Root cause
**What happens**

1. Each first hit on a creature adds an entry with one hit.
2. That entry's vector is only pruned when the same creature is hit again, so for a killed creature it keeps its last hit forever.
3. Past 256 entries the cleanup loop runs on every hit, finds no empty vector, and the map keeps growing (one entry per creature ever hit, for the whole session: no session end, stage change or `on_world_change` clears it).
4. A creature freed by the game and a new one allocated at the same address within `kHuntWindow` gets a "joint" bonus from the other player's hit on the dead one.

### Confidence
CONFIRMED by reading.

## Fix
Fixed (fix/w2-creature): the table is `teamplay::HitTable` (mods/coop/hunt_hits.h). Every hit
prunes the hits older than `kHuntWindow` of every target and drops empty targets, so it holds only
targets hit in the last 6 s. `teamplay::forget_target` drops a target whose object the host
retires (npc.cpp `retire`: forgotten after 2 s without an update, or reused, BUG-0044);
`teamplay::reset` runs on `npc::on_world_change` and `npc::on_session_end`. `hunt_targets=` in
`coop.npc` (host). `HitTable::legacy` keeps the old pruning (unit test).

## Verification
Unit test `tests/npc_tests.cpp` (`make test`, ASan): 1000 targets hit once a second; with
`legacy` the table holds all 1000 (the bug, checked), with the fix at most 7. Joint detection,
the window, `forget` (a new creature at a freed address starts clean) and `clear` are checked too.
Not run in the game on its own (the change is in portable code; the two-game tests that hit NPCs,
creature_kills and team, ran with it).

### Related
- Umbrella #11 (module state that no session end clears).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #77.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-creature (hit table pruned every hit, cleared on world change and session end); unit test.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: team.hunt_hits (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
