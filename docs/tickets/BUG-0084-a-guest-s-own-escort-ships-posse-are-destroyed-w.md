---
id: BUG-0084
title: a guest's own escort ships (posse) are destroyed while it is with the host
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#62
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-space
commits: c5fe0b8
tests: space_fleet
signature:
docs: docs/re/space.md#space-ship-state-chunks
related: #62
switch: fleet.posse
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/62 (full text, discussion).

While a guest is in the host's place, the squadron code destroys every local UFO that is not the player's ship, not a mirror and not a Defender (type 6). That includes type 3, the player's posse (allied escort ships). The game's posse code keeps its own pointers to these ships. A guest who recruited allies loses them as soon as it meets the host, and the game may later use the destroyed ships. Severity: wrong behaviour (loss of the player's allies), possible crash.

### Where
- The local NPC removal in `guest_state`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L319-L326
- `destroy_mirror(u, false)` -> `game::destroy_creature` -> `DestroyInstance`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L249-L261 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L436-L451

### How to reproduce
1. Guest in Space with an ally escort (recruit ships from an allied empire through the comm screen; if the fixture has no ally, a test can add a posse member with the game's own `cPosseSimulator::AddMember`).
2. Check the guest's UFO list (`coop.fleet list`): the type 3 ships are there.
3. The guest flies to the planet or system where the host is.
4. Within 0.1 s of the first host state, `coop.fleet list` on the guest no longer shows the type 3 ships (`local_removed` grows). They do not come back after leaving the host.

### Expected
The guest keeps its own posse. Only the NPC ships that the host simulates (pirates, empire ships of that place) are replaced by mirrors.

## Root cause
**What happens**

1. After every complete ship-state cycle from the host (10 Hz), the guest runs:
   ```cpp
   for (void* u : all_ufos()) {
       if (u == me || F<int>(u, kType) == 0 || g_mirror_of.count(u) || F<int>(u, kType) == 6)
           continue; // type 6: the mod's player puppets (Defender)
       destroy_mirror(u, false);
   ```
2. The only exceptions are the player (type 0), mirrors and type 6. docs/re/space.md section 0 lists the UFO types. Section 2.1 step 4 calls type 3 "posse/ally" (an attacker of type 3 gets the player empire's political id). Section 1 notes that 0x1000C60 kills all type-3 UFOs of the player empire.
3. So a guest's posse ships are destroyed by `DestroyInstance` the moment the guest is in the host's place. Their owner was the guest's empire, not the host's, so the host's mirrors do not replace them. The host's own posse is mirrored instead.
4. The game's posse simulator (`cPosseSimulator`, symbols in the .sym files) and `cSimulatorSpaceGame` +0x78 keep references to the ships. CLAUDE.md section 2 lists "the pack" and AI targets among the places the game keeps pointers. Whether the posse code then touches a destroyed ship is not verified.

### Confidence
PLAUSIBLE. The removal loop is confirmed by reading. That type 3 is the posse comes from docs/re/space.md (2.1 step 4 and the 0x1000C60 note), not from a runtime check in co-op. What the posse simulator does with a destroyed member is not verified.

## Fix
Fixed on fix/w2-space (c5fe0b8). The local NPC removal in `guest_state` skips every type 3 ship. Type 3 is the posse: `cSimulatorSpaceGame::DestroyNPCUFOs` 0x1009310 (GOG 2024, same address as GOG 2016) skips exactly `mUFOType == 3` when keepPosse is set (and type 8 for keepAmbush), checked in the disassembly. No political id check: a posse recruited from an allied empire may carry that empire's id, and the host's own type 3 ships arrive as mirrors anyway. `coop.fleet keepposse 0` restores the old loop.

Suggested in the review:

- Skip type 3 ships of the player's own empire in the removal loop: `if (F<int>(u, kType) == 3 && F<uint32_t>(u, kPol) == GetPlayerEmpireID()) continue;`.
- Better, keep any ship that is in the posse simulator's member list.
- Also check the host side: `host_tick` mirrors the host's posse (type 3) to the guests, which is fine, but the guest's posse is then not visible on the host. Showing it is a separate feature.
- Keep the old loop behind `coop.fix keepposse off`.

## Verification
Done (2026-10-28, lane 6): `coop_space_fleet.py` step 2: the guest makes a type 3 ship (`coop.fleet spawn 3`) while it is with the host and it must still be in the game's UFO list 2 s later. `--without-fix` (`keepposse 0`) FAILS (the ship is gone), with the fix PASS twice. Not covered: a posse recruited through the comm screen (no recipe yet); a type 3 ship made by SpawnUFO with the own empire's political id is freed by the game at once (outside the real posse), so the test uses pol -1.

Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_posse.py` on `twoinst.start('space')`:
1. On the guest, give the ship one posse member (comm screen with an ally, or the game's `AddMember` through a test command).
2. Assert in the guest's UFO noun list (`GetAllUFOs`) that a type 3 ship with the guest's political id exists, and read the posse member count from the game's posse simulator.
3. The guest flies to the host's planet. Wait 5 s.
4. Assert the same ship pointer is still listed and the posse member count is unchanged.
5. Fails on main (the ship is gone), passes with the fix. No crash report on either game.

### Related
- docs/re/space.md sections 0, 1 and 2.1.
- CLAUDE.md section 2 (object lifetime).

Run evidence (2026-10-28, BUG-0215, found afterwards in the lab folders and agent logs): FAIL with the fix off: `fleet2--without-fix.log`, `fleet_nofix.log` (fix/w2-space, keepposse 0), a standalone test run whose log stayed in the agent's scratch folder (not kept in out/lab). PASS: lab run out/lab/20261027-233002 on fix/w2-space (space_fleet ok); later full runs on main pass too.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #62.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-space (c5fe0b8): type 3 (posse, per DestroyNPCUFOs) is never removed as a host NPC; test space_fleet FAIL without / PASS with.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: Verification names the FAIL log and the PASS lab run (BUG-0215).
- 2026-10-28: switch: fleet.posse (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
