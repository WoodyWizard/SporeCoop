# SporeCoop 0.7.10 plan (2026-11-06, starts right after 0.7.9 is released)

**Scope (owner, 2026-11-06):** 0.7.10 is a short release: the player reports of 2026-11-06 (P0, P0b), the address-space
drain behind the Civ crash (M1) and the rest of the remote-body desyncs (K3). The other packages (P4, V2, K2a, K2b, N1, N2,
E1, G4) moved to docs/plan-0.7.11.md. The text below the packages table is kept from the first draft.

After 0.7.9 about 78 bug, desync, crash and test tickets stay open or not reproduced (the Space feature notes BUG-0191..0198
belong to 0.8.0, Rivals, and are not in this release). This release takes the rest of the per-stage bugs, empties the class
umbrellas (BUG-0418..0432) by turning what is left in each into concrete fixes, gives every not-reproduced ticket one more
serious attempt on the current build, and makes the remaining test tickets reliable.

Rules as in 0.7.9 (out/RULES-w40.md, docs/plan-0.7.9.md): one mechanism per class, every fix behind a COOP_FIX switch with
twin FAIL/PASS runs, check what the other player's game shows, the detector rows of a class gone on every stage, GOG 2024
only. **New rule (owner, 2026-11-06): every ticket of the plan closes in this release**, fixed or (after a serious, dedicated
attempt) with evidence that it is not a bug or cannot be fixed. "Not reached" is not an outcome: a ticket an agent leaves
gets its own agent before the final check, and the release waits for it. Tickets that need unknown reverse engineering get
their own agent from the start instead of the end of a package.

## Step 0: intake (orchestrator, before the agents)

* New player reports since 0.7.9 (GitHub issues, the Discord bug-reports threads): a ticket each, put into the packages below.
* What 0.7.9 leaves open, with its evidence, goes into the package of its stage.

## Work packages (one agent, one branch, one lane pair each)

| Package | Mechanism | Tickets |
|---|---|---|
| **P0 Discord 2026-11-06** (first, wave 1) | The player reports of 2026-11-06 (0.7.8, two players Cell to Civ), by importance: the evolution bar lowered by a partner's editor spend (players: important), the guest's own creature from another world after the host's Cell -> Creature, the migration that never reaches the guest, the host's Civ crash with the address space full, then the Cell double events (a kill applied twice, a meal eaten twice), the Cell level-up churn, the part token, the attackable evolving nest, the Tribe roster portraits, village colours, member cap, dance music, the guest's Civ attacks, the host's name tag | BUG-0630, 0638, 0640, 0631, 0632, 0636, 0635, 0633, 0639, 0634, 0637, 0643, 0644, 0645, 0646, 0648, 0649, and the children of the class umbrellas from these reports: BUG-0641, 0642 (of 0418), BUG-0647 (of 0424) |
| **M1 Address space** (dedicated, wave 1) | The game's 2 GB address space runs out in long sessions (host crash in Civ after 50 min of Creature, 740 MB gone; a Cell guest 3502 -> 1427 MB in 6 min): find what grows (a memory census per stage over a long-session bot), fix the leak; whether the large-address flag can be set safely | BUG-0631, 0621 |
| **K3 Remote bodies, the rest** (after the 0.7.9 drive.lead merge) | Bodies placed behind or stepping on the other screen that 0.7.9 did not close: a guest's mirror held back by its own game, the player tribe's citizens p90 1.9-2.0 m, Civ vehicles under loss bursts, walked animal proxies stepping, a hit NPC's proxy held off its path, the chaos_tribe proxies gate | BUG-0386, 0410, 0438, 0445, 0409, 0440 |
| **P0b Creature nest** | The guest gets no heal at its home nest (with BUG-0640: the guest's respawn nest does not follow the host's) | BUG-0496 |

A class umbrella closes when its children are fixed or closed and the detector shows no row of the class on any stage;
what is left becomes a concrete ticket in the same package.

## Order

1. Step 0, then wave 1 of at most 3 lab agents at once (a fourth lab agent made the lab unreliable in 0.7.9):
   P0-Cell (BUG-0632..0637), P0-Creature (BUG-0638, 0640, 0639, 0641, 0642, 0496), M1 (BUG-0631, 0621).
   BUG-0630 went into 0.7.9 (its cause was found at once); if 0.7.9 does not close it, it leads P0-Creature.
2. Wave 2 as lane pairs free up: P0-Tribe/Civ (BUG-0643..0649, 0647), K3; after each report, unreached tickets go to a
   dedicated agent at once.
3. Integration run on rel/0.7.10 after every two merges.
4. Final check on at most 4 lanes: smoke, every test added in 0.7.10, play_* of all stages, campaign_*,
   detect_*, the long-session bot on Creature and Tribe. Then the release procedure.
