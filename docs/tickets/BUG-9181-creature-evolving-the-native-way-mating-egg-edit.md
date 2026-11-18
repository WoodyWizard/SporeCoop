---
id: BUG-9181
title: Creature: evolving the native way (mating, egg, editor), the family in cocoons, the player hatching from the egg
status: open
kind: feature
stage: creature
reported: 2026-11-13
source: owner
role: all
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win/msi_input.ps1 (the MSI real mouse) and console, TITAN host + guest + the MSI, 2026-11-13
signature:
docs: docs/ROADMAP.md (2E, the owner sequence)
related: BUG-9160 BUG-9161 BUG-9174 BUG-9175 BUG-9180
switch: creature.cocoon creature.egg_watch creature.mating_direct creature.guest_mate_offer
---

## Report
The owner tested (2026-11-13, the MSI as a guest, TITAN watching): a click on the mate went straight into a cocoon instead of the mating; only the player was in a cocoon, its body poking through; back from the editor it started in its egg, then turned into a cocoon; TITAN saw the guest's mating call waves the whole time. The owner's sequence, closest to the game: 1. the player clicks the mate and walks to it; 2. the game's scene, the dance; 3. the player lays an egg in the middle of the nest; 4. the editor. Then (the mod): 5. every living family member (not the egg) spins into the game's cocoon; 6. they stay in it while the player edits; 7. back: the family comes out first (the game's spin, animation and sounds); 8. the player hatches from its egg with the game's own scene, native from there.

## Root cause
Observed: on a guest the mate is a copy of the host's member (npc.cpp: a puppet, NPCTickAI skipped), so the game's pairing never got its answer (BUG-9175) and the mod went to the cocoon after 2 s of hearts; the own egg scene was replaced by a cocoon (BUG-9161); the own creature played an emerge after the editor (own_tick) on top of the game's hatch.

## Fix
npc.cpp lend_local_ai: a proxy runs its own AI on the guest for a while (puppet unregistered, the sync leaves it alone, back to the sync after). coop.cpp mate_keeper: the mate offered and the one the player mates with are lent, 2 s at a time while they are. evolve.cpp: the egg scene is the game's (no cocoon of the own body; Escape after 12 s, BUG-9161); when the editor opens the own family (its species within 50 m, never the egg) goes into the cocoon, on partners' games with the partner's cocoon (puppet_editing); back, the family comes out first (family_out); the player has no emerge of its own (it hatches from its egg); a partner's puppet leaves its cocoon without an emerge. The direct editor only after a 30 s stalled pairing (was 2 s on a guest).

## Verification
Live 2026-11-13 (the MSI as a guest, the owner's report first): FAIL (the owner): the click did nothing, the mod's cocoon after a while, the body through the cocoon, no family cocoons, the rings for good, creatures standing about after it that TITAN never saw. Causes found on the way: the guest's update filter destroyed the lent mate every frame (a lent proxy is no puppet: taken for a local NPC; it was made again at once), and the console's virtual cursor never reaches the game's hover in a background window (every creature class 0). PASS (msi_input.ps1, the real mouse on the MSI): the mate offered (class 9, hearts, lent, no puppet); the click: action 02D852F0, target the mate, the game's mating step state 2 2, the egg scene 12 s, "3 of our family into the cocoon" on the MSI and "3 of a partner's family into the cocoon" on the host, the editor; Done: the family out (own_emerges 1), the game's hatch at the nest; finished_by_mod 0; the rings on the host ended after 6 s ("a partner's looping one-shot effect ended"); 0 faults on three games. Not seen in a picture: the family's cocoons on the host's screen (the camera); the larger shell on a partner (kShellFits 0.7).

## History
- 2026-11-13: opened.
- 2026-11-13 (later): the update filter keeps lent proxies; family_tick (BUG-9161) leaves proxies alone (it gave copies the host's species after a respawn); the shell sized 1/0.7 of the radius (at least 1); a partner's pool 3 effect ends after 6 s (the mating call rings, E1B24DDB, the sender sends no stop for pool 3). Live on two PCs.
- 2026-11-13 (later, TITAN's real mouse with the owner's OK): the host's view of the MSI in its editor: the family's cocoons (4 wrapped), much larger shells; back: the cocoons open and the family stands at the nest. Seen: one family member's head through the side of its shell (the host's members run their own AI and walked): wrapped members are now held where they went in (teleported back); a small head at a shell's front stays unexplained (likely a hatchling born after the wrap). A click beside the moving mate started a mating without a target: the 30 s stall path took over (finished_by_mod 1). The test games run at 10 FPS now.
- 2026-11-13 (later): the owner: "I think growing the cocoon isn't the way. Maybe shrink the creature till they're really small? Then grow them back to their stored size when coming out?" Done: the shell back to its size (kShellFits 0.9, sized by the radius the creature had before), a creature inside is 0.12 of its stored size (kept every tick: a sync may reset it), and gets its stored size back as it comes out (family_out, a partner's puppet in puppet_back; a new body keeps its own size). Seen on the host (TITAN, the MSI in its editor): three normal shells, nothing through them; after Done the family at the nest at full size; 0 faults on three games (out/shots/shrink-before-after.png).

