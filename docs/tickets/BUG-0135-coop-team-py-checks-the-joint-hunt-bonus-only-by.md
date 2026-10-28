---
id: BUG-0135
title: coop_team.py checks the joint-hunt bonus only by a mod counter, never the extra damage on the NPC
status: fixed
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#114
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tests
commits: d0b3f81
tests: team
signature:
docs: docs/06-testing.md#testing-w4-tests
related: #114
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/114 (full text, discussion).

The README advertises "Hunt the same animals for +25 % damage" in the Creature stage. The test that covers it, `coop_team.py` (smoke suite), only checks that the mod's `joint hits` counter went up by one. It never reads the NPC's health in the game, so the test cannot tell whether the 1.25x damage reached the creature. CLAUDE.md section 3 asks for the game's state, not mod bookkeeping.

Next to it, the rescue part asserts two more mod-side values: the `rescues` counter and the chat notice. Those sit beside a valid game-state check (the guest's health after the lethal hit, line 83), so the rescue itself is tested. This issue is about the joint-hunt bonus.

### Where
- The joint-hunt step: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_team.py#L49-L56
- The counter parsed from `coop.team`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_team.py#L12-L15
- Rescue counter and notice checks (bookkeeping, beside the valid health check at L83): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_team.py#L83-L86
- The bonus in the mod: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/teamplay.cpp#L36-L59 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/teamplay.h#L34
- Where it is applied (guest hit on the host, host avatar hit): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L709-L730 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L777-L781
- README claim: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/README.md#L67

### How to reproduce
**How to reproduce / check**

Change `return damage * kHuntBonus;` in teamplay.cpp to `return damage;`. coop_team.py still passes the "joint hunt bonus" check, because only the counter is asserted.

### Expected
The test measures the bonus in the game: the host NPC's health drop from the second hit is about 1.25x that of a single player's hit of the same size. It also checks that this shows on the guest's mirror.

## Root cause
**What happens**

```python
j0, _ = team(host)
guest.call(f'coop.npc hit {nid} 0.5')
time.sleep(0.5)
host.call(f'coop.npc hosthit {nid} 0.5')
time.sleep(0.5)
j1, _ = team(host)
ck(j1 is not None and j0 is not None and j1 == j0 + 1, f'joint hunt bonus on the host: joint hits {j0} -> {j1}')
```

`g_joint_hits++` runs inside `on_player_hit` before the multiplied damage is returned. The counter goes up whether or not the returned damage is used:
- a later change that drops the return value;
- a path that applies `q.damage` instead of `dmg`;
- a TakeDamage that clamps or ignores it.

All of these pass this check. The NPC's health is available on the host (`coop.npc list` prints `hp=`, the game's `health(c)`) but is not read.

### Confidence
Confirmed by reading. A second reviewer verified it, and corrected an earlier draft: the rescue health check at line 83 is valid (`coop.npc hurt` does damage through the filter and `MSG_DAMAGE`; the rescue leaves 40 %), so only the counter-based checks are in question.

## Fix
Fixed on fix/w4-tests (test only). `coop_team.py` reads the NPC's health in the host's game
(`hp=` of `coop.npc list`, now printed with two decimals) around a lone hit of the guest of size d
(d = 30 % of its health) and the host's hit of the same size right after: the lone hit must take
d (10 %), the joint one 1.15-1.35x the lone one; the guest's mirror must show the same health
(0.1). The `joint hits` counter is only printed. `--seed-nobonus` turns teamplay off on the host.
The rescue checks stay as they were (the health check after the lethal hit is the game's state).

## Verification
- FAIL seeded (`coop_team.py --seed-nobonus`, lane 5, twice): "joint hunt bonus in the host's game:
  the second player's hit takes 1.0x a lone hit (1.25)" (hp 6.0 -> 4.2 -> 2.4).
- PASS: out/lab/20261028-025535 (hp 6.0 -> 4.2 -> 1.95 for a hit of 1.8: 1.25x; mirror 1.95).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #114.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fix/w4-tests: the bonus is measured on the NPC's health on both machines; seeded FAIL with teamplay off, PASS in the lab.
- 2026-10-28: released in 0.7.3.0.
