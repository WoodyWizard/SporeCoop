# SporeCoop 0.7.5 plan (2026-11-03)

0.7.4 made Cell and Creature sync layered (replicated fields, host-owned world objects, NPC animation relay,
evolve in place, DNA pace). 0.7.5 carries the same mechanisms to **Tribe and Civilization** (the scope decided in
docs/sync-architecture-plan.md: one stage pair per release, through the same stage adapters), and closes what is
left of Cell and Creature. Space stays 0.7.6.

Rule from 0.7.4: every fix behind a COOP_FIX switch, a test that fails with it off and passes with it on, checking
what the other player's game shows; the divergence detector before and after (tools/detector_report.py); a hook on
a game predicate that many callers share is scoped to the callers that need it (BUG-0462's lesson).

## Work packages (one agent, one branch, one lane pair each)

| Package | Tickets | What "done" means |
|---|---|---|
| **T1 Tribe actions** | BUG-0514 (guest cannot take tools from the host's buildings, the players' main Tribe complaint), BUG-0505 (guest orders the host takes but its members do not act), BUG-0515 (giants deal 8 damage), BUG-0340 (chiefs spawn on each other) | a guest's tribe plays like the host's: picks up tools, its orders run, epic damage right, chiefs apart |
| **T2 Tribe world** | BUG-0483 (a whole village in the guest's game only), BUG-0488 (tools / members / nests elsewhere), BUG-0332 (village ground height), BUG-0503 (animal proxy off the host's NPC), BUG-0495 (Tribe and Space mirrors in the guest's saves), BUG-0328 (rejoin misses NPCs) | the detector's Tribe rows map to no open ticket; a guest's save has none of our objects |
| **C1 Civ citizens and roads** | BUG-0476 (city citizens walk elsewhere; then BUG-0475's Civ half: citizen size), BUG-0494 (roads), BUG-0319 (late NPC city missing), BUG-0325 (gift / rejected command) | citizens mirrored with the fields engine, roads on both games |
| **C2 Civ combat** | BUG-0477 (shots and projectiles not on the guest), BUG-0484 (cultural targets, bundles, turrets), BUG-0438 (vehicle 4-7 m off under loss bursts) | a fight looks the same on both screens |
| **E1 Effects in Tribe / Civ** | BUG-0451 other stages: the skin painter faulted on a relayed world effect (Skinner::cSkinnerTexturePainter::StartRender, docs/known-issues.md#issue-effects-civ-space) | `effects.other_stages` on by default for Tribe and Civ with no fault in effects_party_tribe / _civ |
| **K1 Creature leftovers** | BUG-0179 (the guest's social game: prompts, meter), BUG-0501 (roar), BUG-0458 (stick), BUG-0496 (home nest heal), BUG-0504 (creatures under the ground in the brain caption), BUG-0517 (host species after its editor without a save), BUG-0482 (chat under the "game saved" box) | each with its twin test |
| **K2 Puppet look** | BUG-0376 (jumps not shown), BUG-0408 (slide after a stop), BUG-0434 / BUG-0435 (nest mound), BUG-0436 (standing animation while walking), BUG-0383 (bodies of killed NPCs) | the partner's creature moves like a creature, measured frame by frame |
| **L1 Cell leftovers** | BUG-0343 (decentered view), BUG-0345 (a guest's own creature from another world), BUG-0446 (Cell parts to Creature twins), BUG-0519 (partner's cell in a level change), BUG-0480 (rare stale mirrors) | |
| **G1 Infrastructure** | BUG-0347 (minimized game reported as a hang), BUG-0405 (world transfer resends 50-90 %), BUG-0465 / BUG-0486 (kraken and UFO events on one game only: the host-owned event path) | |

## Order

1. Wave 1 (4 agents, lanes 2/3, 4/5, 6/7, 9/10): T1, T2, C1, K1 (the players' biggest open complaints).
2. Integration run on rel/0.7.5 after every two merges (0.7.4 showed late merges break each other).
3. Wave 2: C2, E1, K2, L1, G1 as lanes free up.
4. Nightly detector (tools/nightly_detect.sh) on rel/0.7.5 before the final check; every new row gets a ticket.
5. Final check on at most 4 lanes: smoke, every test added in 0.7.5, play_tribe, play_civ, play_creature, play_cell,
   campaign_short, campaign_tribe, detect_* of all stages. Then the release procedure.
