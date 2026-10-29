---
id: BUG-0119
title: after a load the guest may keep the host's species, or get SetSpecies on a world still loading (avatar pointer used as world identity)
status: fixed
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#98
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-creature
commits: 7f36e32
tests: world_reload
signature:
docs: docs/re/creature.md#creature-world-epoch
related: #98
switch: npc.world
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/98 (full text, discussion).

Three places tell "a new world" from "the same world" only by comparing the avatar's address. Their remembered pointer is never reset when the creature stage is left within a session: a load, a world received from the host, or the menu.

The game pools creature objects (`coop.npc creatures` counts creature objects "without a species: not in the world (pool)"). So the avatar of the next world can have the old address. Each check then takes the new world for the old one.

`coop.cpp` already does it right for its own editor timer: `g_settle_avatar` goes back to null in every frame outside the creature stage.

### Where
- Own avatar swap: `g_seen_avatar` (3 s settle guard) and `g_swapped_avatar` (swap done) are compared by pointer and only updated in the creature stage. `g_swapped_avatar` is reset on session end only, and `g_seen_avatar` never.
  - Store: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L776-L780
  - Use: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L828-L863
  - Session-end reset: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L1004
- Guest proxies: `static s_av`, the "first 1 s of a world" wait. It is not updated while there is no avatar (the return comes first): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L390-L403
- Deferred destroys: the world token is the noun manager's avatar pointer. A queued creature is destroyed in a later world if the avatar address matches and the queued address is listed again: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L414-L468

### How to reproduce
1. A guest with its own creature in the host's creature world, then any reload within the session: the host's editor sends a new world, or `coop.load`.
2. Log `game::avatar()` before and after the load, on the guest.
3. If the address repeats, `coop.status` (creations swaps counter) shows no new swap, and the guest's avatar species stays the host's (`species_key` of the avatar).

To see the effect without relying on the allocator: in a test build, keep `g_seen_avatar` and `g_swapped_avatar` pointed at a fixed address and reuse it.

### Expected
After every load the own creature is applied again, and only once the new world has played for 3 s.

## Root cause
**What happens (if the new avatar has the old address)**

1. `apply_own_avatar` sees `av == g_seen_avatar` and skips the 3 s wait. `SetSpecies` then runs on a world that is still loading. The comment there says that crashed ("a null call inside the game").
2. Or `av == g_swapped_avatar && k == g_swapped_key` returns early. The own-creature swap is never applied in the reloaded world, so the guest keeps playing as the creature the save gives it, typically the host's species.
3. npc's `s_av` skips its 1 s wait. Proxies are created before the update filter has learned the nest herds (`g_local_herds`), so they go into new herds of ours instead of the species' nest herds: no archetype tuning (+0xE84), and more herds of ours in later saves.
4. A creature queued in `g_destroy_later` during a cinematic just before a load would pass the token check if its address is listed again in the new world. A random creature of the new world would then be destroyed. The avatar itself is not excluded.

### Confidence
- PLAUSIBLE: the code paths are confirmed by reading, but whether the game really gives the avatar of a reloaded world the old address was not checked at runtime.
- A second reviewer verified the reading and rated it low.

## Fix
A world counter, `game::world_epoch()` (game.cpp): it goes up every time the creature stage is
entered (a load or a received world passes the galaxy menu and the load mode; the editors keep
it) and when another avatar replaces the avatar inside the stage. `coop.cpp on_frame` reads it
every frame, so it sees the frames outside the stage. The own-creature swap (`creations.cpp`:
3 s settle wait and "swap done"), the guest's first-second proxy wait (`npc.cpp guest_tick`) and
the deferred destroys (`game.cpp`, token = avatar + counter) compare the counter with the avatar.
`coop.npc worldfix off` makes the counter 0 (the old checks by address only).

## Verification
Test `tools/tests/coop_world_reload.py` (suite `world_reload`), a forced condition: whether the
allocator gives the old address back cannot be forced, so the guest's `coop.creations sameaddr`
makes the next new avatar look like the old one (the state a reused address leaves). The host saves
and shares its world twice; checked: the guest's avatar species key (`coop.avatar`, the game's
creature) is the guest's own creation after each load.

* FAIL without the fix (`--without-fix` = `coop.npc worldfix off`, lane 5, 2026-10-28, direct run):
  round 2: the guest kept the host's species 065CBE70 for 30 s, swaps 2 -> 2. (Round 1 passed: the
  simulated reuse was taken by an earlier avatar change.)
* PASS with the fix (lane 5, direct run; lab run out/lab/20261028-010504, `world_reload`): both rounds swap 3.1 s after the guest is in the world
  (the 3 s settle wait holds).
* Not shown: that the game really reuses the avatar's address after a load.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #98.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-creature (world counter instead of the avatar address); test world_reload (FAIL with worldfix off, PASS with it).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: npc.world (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
