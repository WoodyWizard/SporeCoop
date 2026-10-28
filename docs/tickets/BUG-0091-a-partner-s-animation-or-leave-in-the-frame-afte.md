---
id: BUG-0091
title: a partner's animation or leave in the frame after a world teardown can touch a freed puppet
status: fixed
kind: crash
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#69
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-creature
commits: d624824, 28cc753
tests: anim_teardown
signature:
docs: docs/re/creature.md#creature-kill-list
related: #69
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/69 (full text, discussion).

`on_frame` handles all network messages (`g_sess.update()`) at its very start. Only later in the same frame does it notice that the local avatar changed and forget the puppets (`forget_puppets`). On the first frame after the game tore down the creature world (leaving for the galaxy menu, loading a received world, a load from the pause menu), messages that arrived during the teardown are handled with the old puppet pointers still in `g_remotes`. `MSG_ANIM` calls `play_animation` on the freed puppet, and a `player_leave` or disconnect in that frame calls `destroy_creature` on it. Severity: likely crash (use after free), timing dependent.

### Where
- Message handling before the avatar check: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1089-L1096 versus https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1133-L1148
- `on_remote_anim` uses `r.puppet` without any check: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1474-L1484
- `player_leave` / disconnected despawn the stored pointers: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1491-L1507 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1707-L1717
- The creature, tribe and civ adapters have no `alive()`, so `drive_puppet` never notices a puppet the game removed: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L667-L674
- `play_animation` calls the game directly: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L227-L232

### How to reproduce
1. Two instances, `creature` fixture. The host fights an NPC continuously (many attack animations; `coop.overlay anims` on the host logs them).
2. On the host: `coop.share` repeatedly (the guest loads the host's world each time), or on the guest `coop.menu` / `coop.load` while the host fights.
3. Over 20-50 rounds, watch the guest for an access violation in or below `PlayAnimation` (SporeKit fault log, crash report). A debug log line in `on_remote_anim` printing `game::creature_listed(puppet)` shows `0` on the faulting frame.

### Expected
No remote message can reach a puppet that is not alive in the current world.

## Root cause
**What happens**

1. Frame N: the guest plays the creature stage; `g_remotes[0].puppet = P` (the host's puppet), `g_last_avatar = avatar`.
2. Between frame N and N+1 the game leaves the stage, e.g. the guest's received world is loaded (world.cpp posts the quit message and the load). The planet's creatures, including P, are freed. That step can take a second or more without frames, while the host's messages pile up in the UDP socket.
3. Frame N+1: `on_frame` calls `g_sess.update()` first and delivers every queued message:
   - `MSG_ANIM` from the host (sent reliably for every animation of the host's avatar: attacks, gestures, eating, social) goes to `on_remote_anim`, which calls `game::play_animation(P, ...)` on freed memory;
   - a `MSG_PLAYER_LEAVE` in the same batch goes to `despawn(r)`, then `creature_despawn(P)`, then `destroy_creature(P)`: `DestroyInstance` on freed memory.
4. Only after that does `g_local = g_adapter->local()` (null now) differ from `g_last_avatar`, and `forget_puppets()` drop P.

`h_StageMsg` walks `game::puppets()` with `noun_id(c)` (a virtual call) and writes `+0xE84`. It is therefore exposed the same way to any puppet the game freed while it was still registered.

### Confidence
PLAUSIBLE: the ordering is CONFIRMED by reading. Not verified: that the game frees the creature objects before the next Present when it leaves the stage (very likely for a load; less clear for the editor, where the world stays), and how often an animation message falls into that frame.

## Fix
Fixed (fix/w1-creature), behind `coop.framefix 0|1` (0 = the old order and unchecked calls):

* `on_frame` runs the avatar check (`avatar_change_check`: the local entity of the current mode
  differs from `g_last_avatar` -> `forget_puppets`, `npc::on_world_change`, `tribe::on_world_change`)
  before `g_sess.update()` as well as at its old place, so the messages of a frame never see the
  puppets of a torn-down world.
* `on_remote_anim` and `despawn(Remote&)` touch a creature (or adventure) puppet only while the game
  lists it (`game::creature_listed`); otherwise the animation is dropped and the puppet is only
  unregistered. Counters in `coop.framefix` (anims / despawns dropped and made on an unlisted puppet).
* Not done: an `alive()` for the creature adapter (drive_puppet every frame): `creature_listed`
  walks the whole object list, too costly per remote per frame; the teardown case is covered by the
  ordering.

## Verification
Test `tools/tests/coop_anim_teardown.py` (suite `anim_teardown`): the host sends its avatar's
animation messages 10 times a second (`coop.overlay animsend F37DB906`: the same MSG_ANIM a real
animation sends; ability keys without a target play none) while the guest leaves for the galaxy
menu (`coop.menu`) and gets the host's world back, 6 rounds (~11 s each). The guest counts the
animation calls on a puppet the game no longer lists.

* FAIL without the fix (`--without-fix` = `coop.framefix 0`, lane 4, 2026-10-27): 15 animations
  were played on freed puppets in 6 rounds (3, 5, 8, 9, 12, 15 after each round); no fault (the
  freed memory was still mapped), which is why players saw it only sometimes. Direct run, no lab
  directory.
* PASS with the fix twice: lab run out/lab/20261027-185409 (`anim_teardown#1`, `#2`): 0 made, and
  0 dropped: the ordering alone already forgot the puppet; the list checks are the second layer.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #69.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-creature (avatar check before the messages, list checks, `coop.framefix`), test anim_teardown FAIL without (15 anims on freed puppets) / PASS with.
- 2026-10-28: released in 0.7.3.0.
