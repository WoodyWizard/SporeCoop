---
id: BUG-0125
title: a guest that rejoins while the host is paused sees no animals near it
status: fixed
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#104
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-creature
commits: 7f36e32, 9c3aa4b
tests: npc_rejoin_paused
signature:
docs:
related: #104
switch: npc.known
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/104 (full text, discussion).

The host keeps, per guest id, the set of NPC net ids that guest already mirrors (`g_known`). The set is only cleaned inside `npc.cpp host_tick`, and `host_tick` returns early while the host's simulation is frozen: pause menu, a caption waiting for OK, a cinematic. No player leave or join hook touches it, and player ids are reused (#42, #54).

Suppose a guest leaves, rejoins with the same id and has finished loading the world, all while the host stays frozen. When the host goes on, it still thinks the new guest knows those NPCs. It sends only their states, which the guest ignores because it has no proxy for them, and never sends their spawns. The guest sees no NPCs within the 110 m relevance range: its own game's NPCs are removed by the update filter, and the host's never arrive. Each one appears only after it walks more than 130 m away and comes back.

### Where
- Declaration: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L45
- Early return while frozen: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L215-L224
- The only cleanups, by position and for guests no longer present: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L261-L274 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L305-L311
- Spawns are sent only for ids not already in the set: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L275-L289
- Player leave and join have no npc call: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1707-L1717 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1508-L1512

### How to reproduce
1. Two lanes, `creature` fixture, with the guest next to a few NPCs.
2. On the host, open the pause menu (`key escape`).
3. On the guest, `coop.leave`, then `coop.join` again. Wait until its world is loaded and its avatar is in the world.
4. On the host, close the pause menu.
5. On the guest, `coop.npc list` shows no proxies near it, while on the host `coop.npc list` shows the NPCs next to the guest's puppet.

### Expected
A rejoined guest gets the spawns of every NPC around it within a second of being in the world, whatever the host was doing during the rejoin.

## Root cause
**What happens**

1. Host and guest play in the creature stage, with NPCs mirrored on the guest.
2. The host opens the pause menu, or waits on a caption or cinematic. No creature updates run, so `host_tick` returns at the frozen check.
3. The guest leaves (or crashes) and rejoins. It gets the same id, receives the world and loads it while the host is still paused.
4. The host goes on. `player_pos` is valid for the guest, `known` still holds the old ids, and no MSG_NPC_SPAWN is sent for NPCs within range.

The bug is narrower than it first looks. If the host unfreezes while the rejoining guest is still loading (no position yet), lines 270-273 clear the set and everything works. It only bites when the host stays frozen through the whole leave, rejoin and world load.

### Confidence
- The stale set is CONFIRMED by reading.
- The full effect is PLAUSIBLE: it needs the host to stay frozen through the whole leave, rejoin and load. Not run.
- A second reviewer verified it and rated it low.

## Fix
`npc::on_player_leave(id)` erases what the host remembers the player mirrors (`g_known[id]`); it is
called from `cb.player_leave` and, as a safety for a reused id, from `cb.player_join`. The prune in
`host_tick` stays. `coop.npc knownfix off` keeps the old behaviour (tests).

## Verification
Test `tools/tests/coop_npc_rejoin_paused.py` (suite `npc_rejoin_paused`), as in the report: the host
opens the pause menu (Esc), the guest leaves, joins again under id 1 and loads the world while the
menu stays up, then the host closes it. Checked on the guest's game: spawned proxies within 100 m.

* FAIL without the fix (`--without-fix` = `coop.npc knownfix off`, lane 5, 2026-10-28, direct run):
  no mirrored NPC near the guest 15 s after the host went on, the host counted 21 as mirrored.
* PASS with the fix (lane 5, direct run; lab run out/lab/20261028-010504, `npc_rejoin_paused`): 19 mirrored NPCs near the guest.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #104.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: reproduced and fixed on fix/w3-creature; test npc_rejoin_paused (FAIL with knownfix off, PASS with it).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: npc.known (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
