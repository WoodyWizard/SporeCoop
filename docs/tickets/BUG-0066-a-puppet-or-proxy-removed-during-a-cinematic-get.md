---
id: BUG-0066
title: a puppet or proxy removed during a cinematic gets its AI back, and the host sends it to guests as an NPC
status: fixed
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#44
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-creature
commits: c068f08
tests: cinematic_puppet
signature:
docs: docs/re/creature.md#creature-pool-ids
related: #44
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/44 (full text, discussion).

`game::destroy_creature` takes the creature out of the puppet list before it decides to keep it until the running cinematic is over. While it waits, the game's own AI runs it again (NPCTickAI is no longer skipped). On the host, `npc::host_tick` then treats it as an NPC, gives it a net id and spawns it to the guests, so a guest can see an AI-driven copy of its own creature during the cinematic. Severity: wrong behaviour (minor, limited to the cinematic's length). It also contradicts the docs/known-issues.md entry that says these creatures "stand still".

### Where
- `destroy_creature` unregisters before deferring: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L436-L451
- The AI skip depends on the puppet list: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L243-L253
- The host tracks every updated creature that is not a puppet: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L231-L244
- Callers: puppet despawn https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L78 and proxy removal https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L365-L378
- The known-issues statement: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L29-L31

### How to reproduce
1. Two instances (host + guest), Creature stage.
2. Start a cinematic on the host (`call Simulator::cCinematicManager::PlayCinematic thiscall <cm> s:CRG_ClaimNest 1 1 0 0 0`, as in docs/re/creature.md 16), or evolve into a brain upgrade.
3. While it plays, make the guest change its creation (`coop.creations next creature`), so the host rebuilds the puppet. Or on the host run `coop.fix` / a test command that despawns the puppet.
4. On the host, `coop.npc list` shows a new `#id` for the old puppet's pointer; on the guest, `coop.npc list` shows a proxy of its own species, and it moves (position changes) while the cinematic plays.

### Expected
A creature that waits for its deferred destroy keeps the puppet treatment: no local AI, not tracked as a host NPC, not mirrored to guests.

## Root cause
**What happens**

1. A puppet (the host's copy of a guest's creature) or a proxy (the guest's copy of a host NPC) is removed: the guest leaves, its creation changes (puppet rebuilt), the host despawns an NPC.
2. `destroy_creature` runs:
   ```cpp
   puppet_unregister(c); // also used for UFOs and other cGameData puppets
   if (g_destroy_defer && noun_id(c) == 0x18EB45Eu && cinematic_running()) {
       ...
       g_destroy_later.push_back({c, world_token()});
       return;
   }
   ```
   It is no longer a puppet, but it stays in the world until `destroy_deferred_tick` runs after the cinematic.
3. `h_NPCTickAI` only skips registered puppets, so the creature's AI ticks again. A guest's puppet has no archetype tuning (+0xE84) and is in a herd of ours: the same kind of creature as the save strays of docs/re/creature.md 12. A proxy in a nest herd behaves like a normal NPC of that nest (it can wander or attack the local avatar; its hits are not filtered as proxy hits any more because it is out of `g_proxy_of`).
4. On the host, `host_tick` only skips `c == av || is_puppet(c) || is_destroyed(c)`. The deferred puppet gets a new id and `send_spawn` goes to every guest in range. The guest whose creature it was sees a second, AI-driven copy of itself until the cinematic ends and the despawn follows.
5. Guest-side NPC removal through the update filter is not affected: `destroy_pending` is checked there. `remove_saved_strays` skips it too.

### Confidence
CONFIRMED by reading the full path (`destroy_creature`, `h_NPCTickAI`, `host_tick`). Not verified at runtime: what the AI of an untuned puppet does during those seconds.

## Fix
Fixed (fix/w2-creature): `game::destroy_creature` unregisters the puppet only on the immediate
path; a deferred one stays in the puppet list (no `NPCTickAI`, skipped by `host_tick` and the
stray removals) until `destroy_deferred_tick` destroys it. Entries queued in another world are
dropped and unregistered at once, also while a cinematic of the new world plays; `puppet_register`
of an object that is still queued takes it off the queue. Local NPCs the guest removes during a
cinematic were never puppets and still run their AI until it is over. `coop.npc deferpuppet off`
keeps the old order (tests).

## Verification
Test `tools/tests/coop_cinematic_puppet.py` (suite `cinematic_puppet`): the host plays
CRG_ClaimNest, the guest switches its creation (`coop.creations next creature`), the host builds a
new puppet during the cinematic; the old one must have no host NPC id (none is sent to the guest).

* FAIL without the fix (`--without-fix` = `coop.npc deferpuppet off`, lane 5, 2026-10-28, direct
  run): the old puppet 2003BFD8 had host NPC id 23 in 12 of 12 reads (so it went to the guest).
* PASS with the fix: direct run (lane 5) and lab run out/lab/20261027-232407 (`cinematic_puppet`).
* Position is not a usable sign of AI here: the old puppet moved 0.4 m (without) and 1.4 m (with
  the fix) on its last velocity; the planned "does not move" check was dropped.

### Related
- docs/re/creature.md 16 (migration crash 0xC6AE60, why destroys are deferred), docs/re/creature.md 12 (untuned creatures with AI on).
- docs/known-issues.md "While a cinematic plays the creatures the mod removes on a guest ... stand still": CORRECTION and FIXED note added (2026-10-28).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #44.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: confirmed in the game (host NPC id 23 for the old puppet) and fixed on fix/w2-creature; test cinematic_puppet.
- 2026-10-28: released in 0.7.3.0.
