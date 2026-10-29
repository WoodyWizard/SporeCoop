---
id: BUG-0082
title: the guest keeps creature pointers while the host is away and destroys them minutes later
status: fixed
kind: crash
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#60
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-creature
commits: 543edf6, d624824
tests: host_away_kill
signature:
docs: docs/re/creature.md#creature-kill-list
related: #60
switch: npc.kill
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/60 (full text, discussion).

On a guest, the update filter collects the game's own NPCs into `g_kill` during the game's update, and `guest_tick` destroys them in the same frame. On the frame the host goes away (to the main menu), `guest_tick` is skipped and `g_kill` is not cleared. Those pointers are only used when the host comes back to the same world, possibly minutes later, by which time the objects may have been freed or reused. Severity: possible crash (a freed object used later, the most common crash class in CLAUDE.md section 2). The window is one frame, so it is rare.

### Where
- Filter fills `g_kill`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L93-L106
- `g_kill` is used and cleared only in `guest_tick`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L380-L391
- The away branch skips `guest_tick`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L583-L601
- `g_host_away` flips in `host_away_tick`, later in the frame than the filter: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L369-L382

### How to reproduce
1. Two instances, Creature stage, NPC sync on.
2. Make the guest's game have local NPCs updating on the flip frame: for example, start a cinematic on the guest (deferred destroys are re-pushed every frame), or stand near a nest that refills.
3. The host goes to the main menu (pause menu, Quit to galaxy) and stays there for more than 1.5 s. Check `coop.npc` on the guest: `host_away=1`.
4. On the guest, kill and eat the NPCs that were near; wait until their corpses are gone.
5. The host loads the same save again (back to the same world). The guest's first `guest_tick` uses the old pointers. A debug log of `g_kill.size()` at that moment shows entries from before the away time.

### Expected
`g_kill` holds only the pointers of the current frame; nothing collected before the host went away is used after it is back.

## Root cause
**What happens**

1. Frame N: `host_away_tick` (coop.cpp) sets `g_host_away = true` after the host has been in the main menu for 1.5 s.
2. Frame N+1, in the game's update: npc.cpp's own `g_host_away` is still false, so `update_filter` pushes every local NPC that tries to `Update` into `g_kill`. Creatures whose destroy waits for a cinematic (`destroy_pending`) are pushed every frame too.
3. Frame N+1, `npc::tick`: `away` is true, so the proxies are destroyed, `g_host_away = true`, and `guest_tick` (the only place that reads and clears `g_kill`) is not called:
   ```cpp
   g_host_away = away;
   if (!away)
       guest_tick(now);
   ```
4. While the host is away the guest plays on with its own NPCs. They can die, be eaten and be removed, and pooled creature objects get reused.
5. When the host is back in the same world (`host_here`; a world change would clear `g_kill` through `on_world_change`), `guest_tick` reads the stale entries:
   ```cpp
   for (void* c : g_kill)
       if (c != av && !game::is_puppet(c) && !game::is_destroyed(c) && !game::destroy_pending(c)) {
           game::destroy_creature(c);
   ```
   `is_destroyed` reads `+0x21` of an object that may be freed, and `DestroyInstance` runs on it, or on another object that now uses the same memory.

### Confidence
PLAUSIBLE. The code path is CONFIRMED by reading. Not verified: how many entries the flip frame usually holds (normally few, since native NPCs are removed the frame they first update) and whether a freed creature object keeps `+0x21` set in the pool, which would make the stale entries harmless.

## Fix
Fixed (fix/w1-creature): `npc::tick` now ends every frame with `g_kill.clear()` (guest_tick
empties the list itself, anything left is from a frame that skipped it: the host away, NPC sync off,
another stage). `coop.npc killfix off` keeps the old behaviour (tests).

## Verification
Test `tools/tests/coop_host_away_kill.py` (suite `host_away_kill`), a test command that forces the
condition: the host leaves for the galaxy menu (`coop.menu`), the guest sees `host_away=1`, then puts
its avatar into the kill list five times (`coop.npc killhold`: what the update filter does on the
flip frame; the avatar is a stand-in guest_tick never destroys) and reads `kill_held=` in
`coop.npc`. Then the host loads its world again, the guest mirrors its NPCs, no faults.

* FAIL without the fix (`--without-fix` = `coop.npc killfix off`, lane 4, 2026-10-27): kill_held
  [1, 2, 3, 4, 5]: every pointer stays until the host is back (direct run, no lab directory).
* PASS with the fix twice: lab run out/lab/20261027-185409 (`host_away_kill#1`, `#2`): [0, 0, 0, 0, 0].
* Not shown: a crash. The stale entries were used only when the host came back to the same world;
  whether a pooled creature keeps `+0x21` set is still unknown (the ticket's "PLAUSIBLE").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #60.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-creature (kill list cleared every frame, `coop.npc killfix`), test host_away_kill FAIL without / PASS with.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: npc.kill (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
