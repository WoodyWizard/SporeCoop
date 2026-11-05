# SporeCoop 0.7.8 plan (2026-11-05)

0.7.7 shared the world state, re-aligned a guest's planet after transitions and made the guest a player in the
game's rules on Creature and Tribe. What is left: the player reports 0.7.7 did not close, the height of remote
bodies (in the air or under the ground on one game only), the new detector rows of the 0.7.7 final check, and a
co-op window that does not explain itself (a player asked where to pick its own creation, the owner why solo does
not start).

Rules as in 0.7.7 (out/RULES-w40.md, docs/plan-0.7.7.md): one mechanism per class, every fix behind a COOP_FIX
switch with twin FAIL/PASS runs, check what the other player's game shows, the detector rows of a class gone on
every stage. **GOG 2016 is frozen** (owner, 2026-11-05): new symbols, tests and fixes for GOG 2024 (`dc04aee5`)
only; the GOG 2016 files are not touched.

## Work packages (one agent, one branch, one lane pair each)

| Package | Mechanism | Tickets |
|---|---|---|
| **X2 Crashes** (first) | The guest's crash on a double click of the home icon in Tribe; the host's call through null after 44 min of a long Creature session | BUG-0575, 0584 |
| **U1 Co-op window** (first) | Buttons that say why they do nothing (Send my world with nobody joined or no world), the creation row explains how to get a choice, the lobby says a host can start alone and friends join later; chat typed during the share-save pause | BUG-0597, 0551, 0516 |
| **P2 The guest as a player, part 2** | Eating bodies, the roar, the home nest heal, the UFO at the end of Creature, an NPC stuck in its nest, carried / thrown objects, the kraken | BUG-0557, 0501, 0496, 0558, 0562, 0458, 0465; class BUG-0418 |
| **A1 Tribe on the guest** | Music and animations on the guest, the village founding pace owned by the host | BUG-0568, 0569, 0571 |
| **V1 Height over the ground** | One rule for every remote body (puppet, proxy, mirror): the host's height over its terrain is kept, in the air and under the ground; own creatures after transitions | BUG-0550, 0402, 0592, 0504, 0408, 0434, 0440, 0445, 0409; class BUG-0425 |
| **W3 World state, part 2** | The new detector rows: city owners, relations and money in Civ, money in Space, species relations, the totem pole, wsync rows that stop | BUG-0591, 0593, 0594, 0595, 0596, 0588 |
| **G2 Tests** | Flaky gates and tests that hide failures | BUG-0587, 0589, 0437; class BUG-0430 |

## Order

1. X2 (lanes 4/5) and U1 (lanes 6/7).
2. Wave of 4 agents: P2, A1, V1, W3 (lane pairs 2/3, 4/5, 6/7, 9/10 as they free up).
3. Integration run on rel/0.7.8 after every two merges; then G2.
4. Final check on at most 4 lanes: smoke, every test added in 0.7.8, play_* of all stages, campaign_*, detect_*,
   the long-session bot on Creature and Tribe. Then the release procedure.
