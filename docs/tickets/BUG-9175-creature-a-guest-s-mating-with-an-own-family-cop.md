---
id: BUG-9175
title: Creature: a guest's mating with an own-family copy stalls after the click (no animal target)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits: a8a3ceca
tests: tools/qa_win (TITAN, host 9 + guest 8 over 127.0.0.1, 2026-11-14)
signature:
docs: docs/re/creature.md#creature-mate-keeper
related: BUG-9174 BUG-9160 BUG-9161 BUG-0177
switch: creature.mating_direct creature.nest_family_back
---

## Report
Found while testing BUG-9174 (2026-11-13, the MSI as a guest of the TITAN host, Samuth): a click on the own-family copy offered as mate starts the mating (hearts over the player's creature and the family) and nothing follows: the creature stands in the mating action for minutes, no partner comes, no cocoon. The owner saw the same on 2026-11-12 ("getting stuck on the center of the nest"); clicking the nestmate again made them mate then.

## Root cause
Observed: `coop.evolve mates` after the click: action 02D852F0 (the mating approach), target 00000000, the native mating step (0xD6D560) called every frame with state 0 -1 -256 0, reached avatar 1 mate -1. Inferred: the mating needs the mate's own AI to answer and walk in (0xD6D360, the answer that also starts the hearts); a copy's AI is the host's (npc.cpp moves it), so the guest's game never gets an answer. Ideas: the game's own spawned mate on a guest (0xD52B86 spawns one of the avatar's species near the nest when none is eligible: decision 2E "else one is spawned"), kept by the guest's NPC filter; or the copy's AI handed over for the mating.

## Fix
Review first (owner, decision 2S: "Investigate if we're over engineering ... prevent spaghetti coding"): a guest's own mating never worked (BUG-9160: "Not seen yet: a guest with its own creature evolving"); what worked with real players was the evolution vote that decision 2E replaced. The game's pairing cannot work with a copy as mate, so the guest does not run it:
* creature.mating_direct (on), evolve.cpp mating_tick: the own creature's mating action (controller +0x1D8 02D852F0 / D23FE7CA, started by the game's own click on the mate) ends in the cocoon and submit_evolution_request (the request the join editor and the migration already use): on a guest after 2 s of hearts; on the host only when the game's pairing has not reached its egg scene after 15 s. Once per mating action (not again after the editor). The cocoon steps are shared with egg_tick (own_wrap / own_unwrap).
* npc.cpp own_family_tick: the copies of the player's own family also join the player's own nest herd (Creature_SetHerd). After the host gave the nest another key (BUG-9151, BUG-9173) the guest's copy of the world had no nest herd of that key: the copy stood at the nest in a herd of its own, never offered.
* creature.nest_family_back (on), home_nests.cpp family_back (decision 2E, "else one is spawned"): an online player's nest with no living member gets the game's respawn in 2 s and its eggs hatch in 2 s, then the game's own times again. This replaces the new nest's own "first member in 2 s" code (ShareDue.rate, rate_back: removed).
* coop.cpp mate_keeper offers no copy while the game's own gates are closed (the avatar not grown, +0xB5E, +0x1678 = eggs to lay): an offer to a hatchling was cleared every frame (29 offers in 15 s).
* Tests: `coop.nest kill <h> <n> raw` (past the damage filter: the host cannot hurt a party species), `coop.evolve` prints "mating: direct stage finished_by_mod".

Second fix (2026-11-14, a8a3ceca): the paired approach is restarted every 2.5 s, 5 times (was every 4 s, twice), each restart logged with the state words, both spots and the distance; the direct fallback to the editor comes after 18 s (was 30 s). Switch: `coop.fix creature.mate_retries off`.

## Verification
no-fail-run: manual on two PCs (no Windows lab lane). FAIL (08:17, before): the MSI's click: action 02D852F0, target 00000000, the mating step state 0 -1 -256 0 for minutes, no editor. PASS (08:44 and 08:58, the MSI as a guest): the mate clicked: hearts, the cocoon (screenshot), "MSIGuest evolves its own creature", Editor 4 s after the click; Done: the hatch at its own nest, "mating: direct=1 stage=0 finished_by_mod=1", action 9413EDEB (no second editor). The host's own mating after it (TITAN, a real click): the game's pairing (target 2083A150, state 2 1 -> 2 2), Editor after 12 s, finished_by_mod=0. The own nest herd: FAIL "#94 species=7CD76455 ... herd=38C84698" at the nest, the slot empty; PASS the copy in the avatar's herd, class 9, hearts. The family back: FAIL (08:50, the rate only): both members killed, 2 min later no living member (an egg with 129 s); PASS (08:55): 10 s after the kill the egg hatched, a living member, the rate 120000 again. 0 faults on the three games. Not seen: the host's 15 s fallback (the pairing did not stall); the hover card's icon.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on feat/lineages-47 (decision 2S A), live on two PCs.
- 2026-11-13 (later): the copies join the nest herd the host told us as home (nest::home_herd), not the avatar's herd: before the avatar had joined its own nest that was the host's nest, the copies went there and the own nest stayed empty ("no members here yet", joins=0 fails=8 on the TITAN guest); after: joins=1.
- 2026-11-14 (evening): owner play test: a guest still had to click its mate many times. Guest log: `restarting the paired mating approach (retry 1)` at 18:48:16, then the mate copy `moved 0.0 m` for 20 s, sitting on the avatar at the nest centre; the egg scene came only after more clicks. Inferred: the retry only clears `state[1]`, weaker than a click, which re-issues the mate's answer and a fresh walk goal. Fix idea: the retry repeats what a click does, sooner and more than 2 times.
- 2026-11-14 (night): retries and the 18 s fallback built (decision 3V A, a8a3ceca); not tested live: the stall did not happen on demand (it needs a guest mating at its nest). Left open for the owner's next play test; the new log line says what the pair was doing.
