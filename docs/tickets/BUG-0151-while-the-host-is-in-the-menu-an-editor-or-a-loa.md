---
id: BUG-0151
title: while the host is in the menu, an editor or a load, Tribe/Civ guests freeze and guest DNA is lost
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#130
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-civ
commits: 22b7839
tests: tribe_away, civ_away, dna_away
signature:
docs: docs/re/civ.md#civ-wave3
related: #130
switch: civ.away, core.away_dna, tribe.away
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/130 (full text, discussion).

"Host away" exists only for the creature NPCs, and only while the host is in the galaxy menu.
- While the host is in the menu, Tribe and Civ guests are told "you play on alone", but every citizen or vehicle on their screen stands still and their orders go nowhere.
- While the host is in an editor or a loading screen (no "away" at all), Creature proxies freeze, and DNA a guest earns is sent to a host that drops it.
- Goal-card DNA a guest earns is lost even in the menu case.

Severity: medium-low (no crash; progress lost and a frozen game for minutes).

### Where
- "Away" means only `mode == kMenu`: [coop.cpp#L352-L358](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L352-L358). The message: [coop.cpp#L369-L397](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L369-L397).
- Only npc reads `host_away`: [npc.cpp#L93-L106](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L93-L106), [npc.cpp#L584-L600](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L584-L600).
- Tribe:
  - every citizen's behaviour tree is skipped on a guest: [tribe.cpp#L201-L209](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L201-L209);
  - orders go to the host: [tribe.cpp#L300-L350](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L300-L350);
  - the host handles them only while it is in Tribe: [tribe.cpp#L1110-L1113](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1110-L1113).
- Civ:
  - vehicle behaviour trees are skipped: [civ.cpp#L200-L206](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L200-L206);
  - orders are forwarded: [civ.cpp#L274-L298](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L274-L298);
  - the host drops them outside Civ (`active()`): [civ.cpp#L1615-L1625](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1615-L1625).
- DNA:
  - the guest sends what it earns to the host and does not apply it unless "away": [coop.cpp#L92-L108](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L92-L108);
  - the host applies `MSG_DNA_ADD` only with an avatar, and `game::avatar()` is null in the menu, editors and loads: [coop.cpp#L1553-L1560](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1553-L1560), [game.cpp#L338-L344](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L338-L344).
- Goal cards: `social::goal_dna` runs before the away check, sends `K_GOAL` and drops the local reward: [social.cpp#L250-L272](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L250-L272). The host pays it only with an avatar: [social.cpp#L574-L585](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L574-L585).

### How to reproduce
Tribe (2 players, Tribe fixture):
1. Host and guest play together.
2. Host: pause menu, exit to the main menu.
3. Guest: after the "you play on alone" notice, order own members to gather or walk. Nothing moves; `coop.tribe` on the guest shows the behaviour-tree skip counter growing.

Civ: the same with vehicles.

Creature DNA (2 players):
1. Host opens the creature editor while the guest stays in the stage (guest not ready, the vote times out, or the guest's own editor did not open).
2. Guest: eat or kill to earn DNA; `coop.status` shows `dna_fwd` growing.
3. Host returns: the guest's DNA total equals the host's, and the earned DNA is missing.

The same for a goal card completed while the host is in the menu.

### Expected
- While the host is not playing the shared world (menu, editor, load, other world), every guest module either plays on locally or holds what the guest earns until the host is back. Tribe and Civ AI and orders run locally.
- DNA and goal rewards earned meanwhile reach the pack when the host returns, as `g_solo_dna` already does for the menu case.

## Root cause
**What happens**

1. Host in the galaxy menu for more than 1.5 s: `host_in_menu()` is true. The guest gets "<host> (host) left for the main menu: you play on alone until <host> is back", and npc stops removing the guest's own NPCs.
2. tribe and civ have no such state. On the guest, `h_BTTick` / `h_BT` keep returning 0 for every citizen or vehicle, because they are all mirrors. `h_DoActionForObjects`, `h_WalkSelected` and `h_SetOrder` keep sending `MSG_TRIBE_CMD` / `MSG_CIV_CMD`, and the host drops them because it is not in the stage. Nothing on the guest's planet moves, and the player cannot do anything until the host is back.
3. Host in an editor (an evolution or design change in any stage, or one the host opens while the guest is not in its own) or on a loading screen: not "away".
   - Creature: the guest's NPCs are still removed by the update filter, while the host sends no NPC states (its `npc_stage()` is false), so the proxies stand frozen.
   - Cell: the mirrors stay AI-locked.
4. DNA in that time (Creature): `on_dna` sends `MSG_DNA_ADD` and returns false, so the game does not add it. The host is in its editor or loading, so `game::avatar()` is null and the DNA is dropped.
5. Goal cards: `social::goal_dna` reports the reward as `K_GOAL` and drops it locally, also while the host is "away" (it runs before the away branch). The host drops it without an avatar. These rewards are lost in all three cases.

### Confidence
CONFIRMED by reading. A second reviewer verified it independently. Not run in a game: the frozen tribe and civ follow directly from the hooks; the DNA loss from `game::avatar()` being null outside Creature and Adventure.

## Fix
Fixed on fix/w3-civ (22b7839) for Tribe, Civ and DNA; the rest is BUG-0205:
- Tribe and Civ get the host's "away" state (`Ctx::host_away`, the same `g_host_away` as npc: the
  host in the main menu). While it is set, a guest runs the behaviour trees of its members /
  vehicles, its orders go to its own game (`DoActionForObjects`, `WalkSelectedTo`, `SetOrder`,
  `SetMoveOrder`), and `guest_tick` no longer holds them at the last states (the vehicles got
  StopMovement every frame for 3 s). When the host is back its states take over again.
  `coop.tribe fix away 0` / `coop.civ fix away 0`: the old frozen game (tests).
- DNA: a guest's gain while the host is away is kept (`g_solo_dna`) before a goal card's reward is
  sent as K_GOAL; the host keeps a guest's MSG_DNA_ADD that arrives while it has no creature
  (`g_dna_owed`) and pays it when it has one again in a stage. `coop.corefix away_dna 0`.
Not done (BUG-0205): "away" still means only the main menu (an editor or a loading screen of the
host freezes the Creature proxies and Cell mirrors), and a K_GOAL that reaches a host without a
creature is still dropped.

## Verification
- `coop_tribe_away.py` (suite `tribe_away`): host in the menu, the guest orders a member to walk.
  FAIL with `--nofix` (0.0 m, `ai_skipped` = every tick), PASS with the fix (88.7 m, 86.5 m), lane 7.
- `coop_civ_away.py` (suite `civ_away`): the guest's bought vehicle, host in the menu, a move order.
  FAIL with `--nofix` (0.0 m), PASS with the fix (39.7 m), lane 7.
- `coop_dna_away.py` (suite `dna_away`, creature fixture): 7 DNA sent while the host has no creature,
  11 while it is away; the host's game total after its reload. FAIL with `--nofix` (0 -> 11), PASS
  with the fix (0 -> 18, the guest the same), lane 7.

### Related
#63 (the "host is back" message after a disconnect), #60 (creature pointers kept while the host is away), #27 (offline changes to shared resources), #21 (votes after the stage is gone).

Run evidence (2026-10-28, BUG-0215, found afterwards in the lab folders and agent logs): FAIL with the fix off: `away_nofix.log`, `civaway_nofix.log`, `dna_nofix.log` (fix/w3-civ, away off), a standalone test run whose log stayed in the agent's scratch folder (not kept in out/lab). PASS: lab run out/lab/20261028-014643 on fix/w3-civ (tribe_away, civ_away, dna_away ok); later full runs on main pass too.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #130.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-civ for Tribe, Civ and DNA (22b7839), tests tribe_away, civ_away, dna_away (each FAIL with --nofix, PASS with the fix); the editor/load part and K_GOAL without a host creature moved to BUG-0205.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: Verification names the FAIL log and the PASS lab run (BUG-0215).
- 2026-10-28: switch: civ.away, core.away_dna, tribe.away (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
