# SporeCoop 0.7.10 plan (2026-11-06, draft: starts right after 0.7.9 is released)

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
| **P4 Creature** | A guest's avatar found ~14 m from where it joined, the host's creations on the guest, the previous creature version after the host's editor, the DNA budget message of a held editor request, the guest's editor showing the host's new parts, a chat line lost in play_creature | BUG-0333, 0629, 0200, 0457, 0581, 0283 |
| **V2 Civ and Space** | A guest's vehicle gift lost, vehicles of owner 0 after the chunk budget drop, the Space sync mismatch during a rescue, the zoom after a load, the ships' p90 near its limit | BUG-0325, 0206, 0547, 0182, 0190 |
| **K2a Classes: state** | State sent but not applied, hits and world state without a relation / world filter, per-player state under a reused id, credits and effects dropped or undone; a relayed effect at the sender's ground height | classes BUG-0419, 0420, 0422, 0423; BUG-0608 |
| **K2b Classes: content and limits** | Content the other game lacks, game rules copied by hand, mod input read globally, requests the mod builds incompletely, fixed-size game pools overrun, avatar-only rules a guest misses | classes BUG-0424, 0428, 0429, 0431, 0432, 0418 |
| **N1 Not reproduced, crashes and freezes** (dedicated) | One more serious attempt each on the current build (targeted tests, 4 players where the report had them, low fps / bad net): fix, or close with the runs as evidence | BUG-0026, 0118, 0167, 0170, 0171, 0172, 0184, 0578 |
| **N2 Not reproduced, desyncs and bugs** | As N1 | BUG-0011, 0013, 0178, 0221, 0237, 0282, 0328, 0402, 0359 (if 0.7.9 leaves it) |
| **E1 Features without Rivals** | The Cell -> Creature switch vote, the host's editor opening at once after the evolution vote | BUG-0315, 0516 |
| **G4 Tests** | Flaky and missing tests: each test made reliable (the cause found, not a looser limit) or the missing test written; the harness that hides failures | BUG-0021, 0022, 0168, 0177, 0189, 0181, 0193, 0293, 0300, 0311, 0363, 0450, 0489, 0430, 0199 |

A class umbrella closes when its children are fixed or closed and the detector shows no row of the class on any stage;
what is left becomes a concrete ticket in the same package.

## Order

1. Step 0, then wave 1 of 4 agents: P4, V2, K2a, N1 (lane pairs 4/5, 6/7, 9/10, 2/3 as they free up; lane 3 stays the
   orchestrator's integration lane when an agent does not need it).
2. Wave 2: K2b, N2, E1, G4 as lanes free up; after each report, unreached tickets go to a dedicated agent at once.
3. Integration run on rel/0.7.10 after every two merges.
4. Final check on at most 4 lanes: smoke, every test added in 0.7.9 and 0.7.10, play_* of all stages, campaign_*,
   detect_*, the long-session bot on Creature and Tribe. Then the release procedure.
