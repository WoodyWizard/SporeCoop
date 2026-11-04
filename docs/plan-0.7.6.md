# SporeCoop 0.7.6 plan (2026-11-04)

0.7.4 made Cell and Creature sync layered, 0.7.5 carried the mechanisms (fields engine, world-object table, effects
relay, host-owned events) to Tribe and Civilization. 0.7.6 does the same for **Space** (the last stage, with its own
galaxy model: docs/sync-architecture-plan.md) and closes what 0.7.5 left open in Tribe, Civ and Creature.

Rules as in 0.7.5: every fix behind a COOP_FIX switch, a test that fails with it off and passes with it on and checks
what the other player's game shows; the divergence detector before and after (tools/detector_report.py); a hook on a
game predicate that many callers share is scoped to the callers that need it. New in 0.7.6 (lessons of the 0.7.5
final check): a branch that adds a relay of game actions (shots, tools, effects) runs the stage's play_<stage> bot
and shows 0 faults on both games; a branch that changes a default (what a player plays, what is sent) runs every
test that relies on the old default.

## Work packages (one agent, one branch, one lane pair each)

| Package | Tickets | What "done" means |
|---|---|---|
| **S1 Space planets** | BUG-0490 (colony citizens and vehicles differ on a planet), the detector's `Space:*:planetary_artifact` rows (artifacts on the host only; new ticket), `Space:scale:ufo` remainder (BUG-0479 follow-up) | the detector's Space rows map to no open ticket after space_play and detect_space |
| **S2 Space lifecycle** | BUG-0522 (our UFO puppets in the guest's saves), BUG-0195 (a guest's colonies last only until it rejoins), BUG-0388 (a fast partner ship drawn 30 m off its path) | a guest's save holds none of our objects; colonies survive a rejoin; the ship drawn within the limit |
| **S3 Space play** | BUG-0196 (trade open ends), BUG-0197 (allies' mirrored colonies), BUG-0194 (diplomacy drift), BUG-0190 / BUG-0193 / BUG-0257 (flaky Space tests) | each with its twin test; the Space tests pass 3 of 3 |
| **R1 Tribe leftovers** | BUG-0523 / BUG-0524 (tools a tribe buys placed in each game on its own; a guest's palette purchase only in its game), BUG-0531 (member stalled in the gather node), BUG-0340 (chiefs on each other), BUG-0488 (tools / members / nests elsewhere) | a guest's tribe buys, places and uses tools like the host's |
| **R2 Civ leftovers** | BUG-0529 (building hit points not mirrored), BUG-0530 (extra religious shots on the guest), BUG-0484 (cultural targets, bundles, turrets), BUG-0319 (late NPC city), BUG-0526 (citizens only near one camera) | a fight and a conversion look the same on both screens |
| **R3 Creature leftovers** | BUG-0525 (the social game's prompts on the guest), BUG-0501 (roar), BUG-0458 (stick), BUG-0496 (home nest heal), BUG-0532 (puppet in the air drops for a frame), BUG-0536 (no hatch jump with the world's species) | each with its twin test |
| **G1 Infrastructure** | BUG-0527 (effects cost check on a loaded machine), BUG-0353 / BUG-0362 (two lab runs in one second share a directory), BUG-0533 (legless_guest's second editor), BUG-0439 (monitor link rule) | no test fails for a harness reason in the final check |

Ids reserved so parallel branches do not collide: fields kinds K_SPACE_CITIZEN = 6 and K_SPACE_VEHICLE = 7 (S1);
world-object stage bit kStSpace = 8 (S1); the next free MSG_* ids are taken in order of merge (check net/proto.h of
rel/0.7.6 before adding one).

## Order

1. Wave 1 (4 agents, lanes 2/3, 4/5, 6/7, 9/10): S1, S2, R1, R2.
2. Integration run on rel/0.7.6 after every two merges (smoke, the merged branches' tests, play_<stage> of them).
3. Wave 2: S3, R3, G1 as lanes free up.
4. Nightly detector on rel/0.7.6 before the final check; every new row gets a ticket.
5. Final check on at most 4 lanes: smoke, every test added in 0.7.6, play_space, play_tribe, play_civ, play_creature,
   play_cell, campaign_short, campaign_tribe, campaign_space, detect_* of all stages. Then the release procedure.
