---
id: BUG-1173
title: evolve_world: the guest's own editor never opens after the vote (its game's input stays locked by a cinematic since its own creature was applied at the join)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-15
source: lab 20261115-164240 (feat/w51-core2), baseline 20261115-180220
role: guest
versions: 0.7.14.1 (the swap never waited for a cinematic; older versions inferred)
fixed_in: 0.7.15
branch: fix/w51-evolve
commits: bfa52150, 2c625750
tests: evolve_world
signature: guest busy=cinematic,input_lock for good, cinematic F6A4B1E1 (CRG_BrainUpgrade1) with no pause, "own editor not opened: the stage did not settle"
docs: docs/re/creations.md#creations-swap-cinematic, docs/06-testing.md#testing-evolve-own-creature
related: BUG-0019, BUG-0404, BUG-0689, BUG-1141, BUG-0318, BUG-0345, BUG-1112, BUG-1176
switch: creations.swap_after_cinematic
---

## Report
evolve_world FAILS "both players are in their editors" on feat/w51-core2 (lab 20261115-164240, rerun 20261115-174822)
and the same on rel/0.7.15 9362bcdd without that branch (baseline worktree, lab 20261115-180220): the guest's game
says "own editor not opened: the stage did not settle (or was left)" 90 s after the vote. The rest of the test (the
host's migration, the guest's load of the host's world, the guest growing up and migrating) passes.

## Root cause
Observed (guest game log of 20261115-164240): from the join on, after its own creature was applied (`body: own avatar
species ... from this world's record (worlds.ini)`, BUG-0689), the guest's game shows `busy=cinematic,input_lock` in
every trace line until after the host's evolution; the own editor waits for the stage to settle and gives up. The test
confirms the captions it sees only once before the vote. Inferred: a cinematic of the own-creature swap (or a caption
under it) holds the guest's game; not looked into further (not in the C2 / C3 package).

## Root cause (fix/w51-evolve)
Observed (lane 10 probe with the test's start, 2026-11-15): the DNA reached the guest at s 6.9 of its game and its
own game started its brain upgrade CRG_BrainUpgrade1 (F6A4B1E1) 0.6 s later; the own creature swap (SetSpecies,
creations.cpp apply_own_avatar, 3 s after the avatar appeared) ran at s 7.9, inside it. That cinematic then played on
for good: no pause (`coop.pauses` paused=0 for 40 s; a normal brain upgrade pauses the game with reason 0600C4AE when
its caption comes, ~14 s in), so no caption, and 8 Enters did nothing; the input lock held until the next world load
(the guest could not click, its own editor after the vote waited for the cinematic and gave up after 90 s). The same
probe with the DNA 6 s later (after the swap): the caption at ~14 s, gone on the first Enter.
Why 0.7.15 showed it and 0.7.14.1 did not: the race needs the DNA within the 3 s before the swap. v0.7.14.1 on lane 10
(20261115-191956) passed with the swap at s 20.8 and the DNA at s 22.4 (the fixed 4 s settle of the start); since A9
(BUG-1112, the start's condition waits, settle 1.4-2.9 s) the test adds the DNA first. The mod bug is older (the swap
never waited for a cinematic); a player meets it when its game loads the host's world (join, rejoin, a world sent
after an evolution) and a brain level is crossed (or any cinematic plays) in the 3 s before its own creature comes.
Inferred: the cinematic waits for something of the avatar that SetSpecies rebuilt (its body is built over the next
frames, BUG-0318); not looked into further.

## Fix
creations.cpp `apply_own_avatar`: the swap waits while a cinematic of the game plays (`world::cinematic_playing()`,
cCinematicManager +0x2C), logs "own avatar <id> waits: cinematic <id> plays (BUG-1173)" once per wait, and swaps when
it is over (lab: the brain upgrade's caption came, the test's Enter closed it, the swap followed 19 s after the DNA).
`coop.status`: "swap waits for a cinematic: N ticks". Switch `coop.fix creations.swap_after_cinematic off`: the swap
at once (0.7.14).
Test: `coop_evolve_world.py` checks that the guest's brain upgrade caption closes with Enter and its input is free;
when the start took longer than the 3 s window (the swap came first) it sends the host's world once more and adds the
DNA as soon as the guest's avatar is in it, so the twin reproduces the race; it prints whether the guest played its
own creature and whether it was applied before the DNA.

## Verification
* FAIL before: rel/0.7.15 9362bcdd out/lab/20261115-180220 (feat/w51-core2-base, lane 10, "both players are in their
  editors"). v0.7.14.1 passed (fix/w51-evolve-v07141/out/lab/20261115-191956, lane 10: the swap came before the DNA).
* FAIL twin (`SPORE_LAB_FIXOFF=evolve_world=creations.swap_after_cinematic`, lane 9): out/lab/20261115-194402 ("the
  guest's brain upgrade caption closed, its input is free (cinematic F6A4B1E1, input lock 1)", then "both players are
  in their editors"). (20261115-193249-2 with the switch off passed: the swap came before the DNA, the reason for the
  reshare step of the test.)
* PASS: out/lab/20261115-193249 (lane 10, "own avatar 290225CD waits: cinematic F6A4B1E1 plays"), 20261115-194127,
  20261115-194407 (lane 10, own creature True, applied before the DNA False); final runs out/lab/20261115-195303 and
  20261115-195524 (lane 10, the race set up both times); play_creature --quick out/lab/20261115-195748 (lane 10) PASS.

## History
- 2026-11-15: opened.
- 2026-11-15: a mod bug (kind bug): fixed on fix/w51-evolve (creations.swap_after_cinematic; FAIL twin and PASS runs above).
- 2026-11-18: released in 0.7.15.
