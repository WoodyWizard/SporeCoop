---
id: BUG-1178
title: Tests: nest_follow fails since the lab's condition start-up waits (A9): the guest's game gets the brain upgrade's DNA 6 s after entering and plans no migration
status: fixed
kind: test
stage: Creature
reported: 2026-11-15
source: feat/w51-npc lab 20261115-165219
role: 
versions: rel/0.7.15 since ec51786b (the lab start of A9)
fixed_in: unreleased
branch: feat/w51-npc
commits: ef715c49, 7bfcaa33, d8b058e1
tests: nest_follow, nest_follow_early, nest_follow_swap
signature:
docs: docs/06-testing.md#testing-w51-npc, docs/06-testing.md#testing-w51-nest, docs/re/creature.md#creature-nest-early-dna
related: BUG-1112, BUG-0640, BUG-1173, BUG-1176, BUG-1181
---

## Report
nest_follow failed in feat/w51-npc's stage run (lab 20261115-165219 lane 3) and again with and without npc.registry
(20261115-192704 lane 3 off, 20261115-192709 lane 2 on): "the guest's own game claims the host's new nest", "the guest's
creature lives in the host's new nest's herd", "the guest's new home nest heals its creature".

## Root cause
Bisect on lane 3 (a worktree of the tag): v0.7.14.1 PASS (20261115-193814), d0cdc8ea PASS (20261115-194716),
b298b893 PASS (20261115-195218), ec51786b FAIL (20261115-195713). Between them only the rel/0.7.15 merge b20b30e9
with A9 (BUG-1112, d1fe9727: the lab's start-up condition waits instead of the fixed 4 s / 3 s sleeps) and BUG-1134's
call guards. The final tree with `SPORE_START_FIXED=1` (the old sleeps) PASS (20261115-200355). Observed over nine
runs: the guest's game got the brain upgrade's 250 DNA at session s 6.5 / 7.9 / 7.9 / 10.0 in the four FAILs and at
s 12.2 / 12.8 / 14.4 / 16.4 / 25.0 in the PASSes (guest log `dna: 0 -> 250`): with the DNA that early the guest's
game planned no migration (its next nest stayed its home nest, wsync's creature_nest row found nothing to follow;
`guest migration state 0, its next nest` = its home). Inferred, not verified: the guest's game ignores the brain
threshold's migration planning in its first ~11 s in the world (after the join's hatch). A player whose host crosses a
brain threshold within ~10 s of the join would not follow the next migration either (not seen in reports).

## Root cause (fix/w51-nest)
CORRECTION (2026-11-15, fix/w51-nest): the inference above ("the guest's game ignores the brain threshold's migration
planning in its first ~11 s") is disproved, the cause is BUG-1173. Observed:
* Early DNA alone is fine: with the world's species (a lane without a worlds.ini record) the guest got the DNA at s 6.3,
  earlier than in any failed run, and followed the migration (claim, home, heal; lab 20261115-214800,
  NEST_FOLLOW_NO_PLAY=1).
* In all four failed runs the guest played its own creature from a worlds.ini record of the lane (BUG-1176) and its
  game swapped it in 0.3-0.8 s after the DNA (s 6.5 -> 7.4, 7.9 -> 8.3, 7.9 -> 8.6); in the passing runs of the same
  lanes the swap came before the DNA. That is BUG-1173: the swap inside the brain upgrade's cinematic CRG_BrainUpgrade1
  leaves the cinematic playing for good (the guest's trace `input_lock=1` from then on, its position never changed, so
  the test's `gopos` to the new nest did nothing and its game never claimed).
* Reproduced on purpose with BUG-1173's switch off (nest_follow_early: the guest's own creature recorded for the world,
  a rejoin, the DNA 0.3 s after it is in, the swap 1.35 s after the DNA): the same three FAILs, the brain level stayed
  0 (1 after a normal upgrade), input locked (lab 20261115-215620). With the switch on: the swap waits for the
  cinematic and the guest follows.
* The moment between the DNA and the cinematic's start: a swap ordered at once after the DNA (nest_follow_swap) found
  the cinematic already playing 0.12 s after the host's `coop.dna add` and waited (lab 20261115-220240); a swap in
  that moment was not reached from the console (not reproduced; inferred: under a frame or two).
A player hits it as BUG-1173 says (a guest on its own creature, a brain level crossed in the seconds before its own
creature comes after a join); fixed there (`creations.swap_after_cinematic`, unreleased).

## Fix
coop_nest_follow.py: the guest's game plays its world 8 s before the test gives the DNA (`GUEST_PLAY_S`, the old
start's margin); `NEST_FOLLOW_NO_PLAY=1` gives it at once. Test-only.

## Verification
FAIL: 20261115-165219, 20261115-192704, 20261115-192709, 20261115-195713 (A9's start, DNA at s 6.5-10.0).
PASS: 20261115-201309 (2 of 2 with the fix). The twin NEST_FOLLOW_NO_PLAY=1 passed once (20261115-200855) because that
cold start was slow (DNA at s 16.4): the timing decides, not the switch of a test.

## Verification (fix/w51-nest)
FAIL: 20261115-215620 (nest_follow_early, `SPORE_LAB_FIXOFF=nest_follow_early=creations.swap_after_cinematic`, lane 2).
PASS: 20261115-215204, 20261115-220653, 20261115-222140 (nest_follow_early, lane 2); 20261115-214800 (nest_follow with the DNA at
s 6.3 and the world's species); 20261115-220240, 20261115-222140 (nest_follow_swap).

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-npc): bisected to the lab's start-up of A9, test fixed.
- 2026-11-15 (fix/w51-nest): the early-DNA inference disproved; the failures were BUG-1173 (own creature swapped in under the brain upgrade); nest_follow_early and nest_follow_swap in the suite.
