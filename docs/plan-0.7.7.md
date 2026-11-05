# SporeCoop 0.7.7 plan (2026-11-04)

0.7.4-0.7.6 made objects and their fields follow the host (worldobj, fields, effects relay, host-owned events) on
every stage. The 26 player reports of 2026-11-04 (BUG-0554..BUG-0579, 0.7.5) show what that left out, and why the
tests did not see it:

1. **World state is not shared and not compared**: the tribe's food, the villages' relations, the current goal, the
   time of day, DNA, the stages done. The detector compares objects within 250 m of the guest only.
2. **No re-alignment after a transition**: after a death, a restart, a rejoin or a load the guest's world keeps
   villages that are gone, broken buildings, empty plazas, members elsewhere.
3. **The guest is not a full player in the game's rules**: no stun from a charge, hit while flying, the host's
   flock not allied, bodies it cannot eat, no target circle, no ally icon of a nest (and the roar, the nest heal).
4. **Content one game lacks**: parts of a pack drawn as blocks, colours of another shade.

0.7.7 fixes classes, not single reports: one mechanism per class, a test per class that runs on every stage, and the
detector extended so that a class shows as rows before and none after.

Rules as in 0.7.6 (out/RULES-w40.md): every fix behind a COOP_FIX switch, twin FAIL/PASS runs, check what the other
player's game shows, a hook on a shared game predicate scoped to its callers, a relay branch runs play_<stage> with
0 faults, a default change runs every test that relies on it. New in 0.7.7:

- **Detector first**: a package is done when the detector rows of its class are gone on every stage (long session
  bot), not when one test passes.
- **A test per class**, e.g. "die, restart, rejoin, then compare the whole planet" on each stage.
- **Report tests use strict thresholds** (BUG-0553: a check of "> 0" passed on the bug); every twin FAIL run is
  looked at, not only its verdict.

## Step 0

Replay the 2026-11-04 scenarios on 0.7.6 (main 6e6128ce): some Tribe reports may be fixed already (tool places
BUG-0523, a guest's purchase BUG-0524, raided villages BUG-0539, social prompts BUG-0525). A ticket fixed by 0.7.6 is
closed with the run that shows it.

## Work packages (one agent, one branch, one lane pair each)

| Package | Mechanism | Tickets |
|---|---|---|
| **D0 Detector 2.0** (first) | World-state rows per stage (DNA, food, relations per tribe / species / empire, the current goal, time of day, stage progress, member counts) and whole-planet object lists (villages, buildings, nests, cities) in worlddiff.py; a long-session bot (60+ min, deaths, a restart, a rejoin, an editor) on every stage | BUG-0430; gives the row list of W1 / W2 / P1 |
| **W1 World state** | One table of host-owned scalars: the host sends, a guest applies through the game's own setters, both ends checked; resent on every transition | BUG-0572, 0565, 0564 (goal), 0579, 0560, 0577; class BUG-0419 |
| **W2 Re-alignment after transitions** | After a death, load, restart, rejoin or editor return a guest compares its planet with the host's whole list: removes what the host has not, makes what it lacks, fixes object state | BUG-0564, 0566, 0567, 0569, 0574, 0563, 0571, 0488, 0328; classes BUG-0421, BUG-0427 |
| **P1 The guest as a player** | Find the game's "is the player / the player's species" checks (stun, flight immunity, flock alliance, eating, target circle, nest icon, roar, nest heal) and cover them in one place for the guest and its puppet, on every stage | BUG-0554, 0555, 0556, 0557, 0561, 0570, 0501, 0496; classes BUG-0418, BUG-0420 |
| **C1 Content the other game lacks** | A missing part replaced by a similar one (same slot, size, function); colours through a shared palette | BUG-0559, 0452; class BUG-0424 |
| **T1 Transitions and crashes** | Tribe -> Civ black screen, the UFO at the end of Creature, the two new crash signatures | BUG-0576, 0558, 0575, 0578, 0313 |
| **G1 Tests** | Tests and harness that hide failures, flaky Space / Cell tests | BUG-0430, 0193, 0547, 0293, 0356, 0550, 0551 |

## Order

1. D0 and step 0 (one agent each), then a long-session detector run on 0.7.6: every new row gets a ticket.
2. Wave 1 (4 agents, lanes 2/3, 4/5, 6/7, 9/10): W1, W2, P1, T1.
3. Integration run on rel/0.7.7 after every two merges (smoke, the merged branches' tests, play_<stage>, the
   long-session bot on the stages they touch).
4. Wave 2: C1, G1 as lanes free up.
5. Final check on at most 4 lanes: smoke, every test added in 0.7.7, play_* of all stages, campaign_*, detect_*,
   the long-session bot on Creature and Tribe. Then the release procedure.
