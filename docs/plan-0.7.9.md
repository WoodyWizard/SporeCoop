# SporeCoop 0.7.9 plan (2026-11-05, draft: starts when the owner says so)

After 0.7.8 about 47 bugs, desyncs and crashes are open (plus class umbrellas, test and Space feature notes). This
release takes the rest of the player reports and the crashes first, then the classes behind most of the desyncs:
remote bodies that trail the host's, and objects that exist in one game only.

Rules as in 0.7.7 / 0.7.8 (out/RULES-w40.md, docs/plan-0.7.8.md): one mechanism per class, every fix behind a
COOP_FIX switch with twin FAIL/PASS runs, check what the other player's game shows, the detector rows of a class gone
on every stage. GOG 2016 is not supported (CLAUDE.md): GOG 2024 only.

## Work packages (one agent, one branch, one lane pair each)

| Package | Mechanism | Tickets |
|---|---|---|
| **X3 Crashes** (first) | Heap corruption with 4 players (a rejoined guest's Cell pool freed for the reload; a plant's spatial-grid node on Creature -> Tribe); a SporeKit hook made at run time | BUG-0308, 0359, 0403 |
| **P3 Player reports, Creature** (first) | The UFO at the end of Creature (the guest sees the host's ship and is taken), the sea monster on the partner's screen (an actor of the death cinematic), carrying and throwing, an NPC stuck in its nest, killed bodies and eating them, the home nest heal | BUG-0558, 0465, 0458, 0562, 0557, 0383, 0496 |
| **T2 Tribe** | Chiefs on one spot, the hit impact effects of the members' attacks on the guest, a member stalled in the gather node, a rejoin missing NPCs, the ground of a village made live | BUG-0340, 0603, 0531, 0328, 0332 |
| **M1 Remote bodies that trail** | Why placed proxies and mirrors fall 2-7 m behind the host's (Tribe animals and members, Creature hit NPCs, Civ vehicles under loss), slides and lurches; one rule for every remote body | BUG-0440, 0445, 0409, 0386, 0408, 0410, 0438, 0434; class BUG-0425 |
| **L1 Objects in one game only** | Stale and missing mirrors (Cell), a planet without creatures after a rejoin during the host's editor, NPCs lost during the brain caption, NPCs at the planet's centre, a late NPC city, fruit, ornaments and buildings elsewhere, wrong scales | BUG-0480, 0412, 0413, 0339, 0385, 0319, 0497, 0502, 0528, 0488, 0540, 0541, 0602, 0548, 0549, 0485; classes BUG-0421, 0426, 0427 |
| **C2 Cell** | The guest's screen shows only water after its level-up, a 4-player guest cell going NaN, the mating call after a part token | BUG-0604, 0307, 0316 |
| **G3 Tests** (optional) | What is not covered by tests yet (the test tickets) | the open `kind: test` tickets |

## Order

1. X3 and P3.
2. Wave of 4 agents: T2, M1, L1, C2 (lane pairs 2/3, 4/5, 6/7, 9/10 as they free up).
3. Integration run on rel/0.7.9 after every two merges; G3 if time allows.
4. Final check on at most 4 lanes: smoke, every test added in 0.7.9, play_* of all stages, campaign_*, detect_*,
   the long-session bot on Creature and Tribe. Then the release procedure.
