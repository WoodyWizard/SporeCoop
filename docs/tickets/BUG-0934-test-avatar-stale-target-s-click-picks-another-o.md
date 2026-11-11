---
id: BUG-0934
title: Test: avatar_stale_target's click picks another object than the puppet for rounds on end (the test cannot set up its state)
status: fixed
kind: test
stage: Creature
reported: 2026-11-11
source: final check 2 of 0.7.12, avatar_stale_target (lab 20261111-090547)
role: 
versions: 
fixed_in: unreleased
branch: fix/w47-final12b
commits: 340bd402
tests: avatar_stale_target
signature:
docs: docs/06-testing.md#testing-stale-target-setup
related: BUG-0917, BUG-0918
---

## Report
The second final check of 0.7.12 (rel/0.7.12 lab 20261111-090547, lane 3): avatar_stale_target failed on its own
precondition, "the puppet was the avatar's target at its removal 2 times (>= 4)". Rounds 0 and 1 ran (the fix held:
the target field named the removed puppet for 20 and 10 ms); from round 2 on, six rounds in a row,
"the click did not make the puppet 1F192540 the target (+0xE7C = 108EF4B8)".

## Root cause
A test problem. Observed: the test clicked at the projected position of the puppet (`coop.targets`: its feet), four
times at the same point, and after the puppet's third spawn every click took the same other object (the avatar's
target stayed 108EF4B8, no creature's combatant by its address; the host stood at its nest with a carcass 2 m away,
`npcs simulated: 8 within 60 m (1 dead), nearest 2 m`). The test never cleared the wrong target, never clicked
another point and gave the round up. Inferred: which object 108EF4B8 was (the run printed only the pointer; the new
setup prints the object's noun). Not reproduced on demand: 16 of 16 rounds took the click on lanes 3 and 5 afterwards.

## Fix
tools/tests/coop_avatar_stale_target.py `click_puppet`: the click goes at the puppet's body (22 px above the feet),
then five other points; a click that picked another object is printed with what the game says the target is
(`coop.abilities reach`), the target is cleared with the game's own SetTarget and the next point is clicked; when no
click takes the puppet, the game's SetTarget names it (new test command `coop.abilities target none|<puppet address>`,
creature vt 0x84 with the puppet's combatant, as the click does) and the round runs. The test now asks for 6 of 8
rounds and at least 2 of them by a real click. `STALE_TARGET_MISS=1` makes every click miss (the fallback alone);
`OLD_STALE_TARGET_SETUP=1` is the old setup.

## Verification
* FAIL (the old setup): rel/0.7.12 out/lab/20261111-090547 (2 of 8 rounds).
* PASS: lane 3 out/lab/20261111-110131, out/lab/20261111-110530, out/lab/20261111-110704; lane 5 out/lab/20261111-110133, out/lab/20261111-110529, out/lab/20261111-110703 (8 of 8 rounds each, all by a real click).
* The fallback: out/lab/20261111-111126 (`STALE_TARGET_MISS=1`: every click goes to a corner of the view): 8 of 8 rounds ran with
  the target from the game's SetTarget, the fix held in each, and the test FAILS only on "0 of these rounds had the
  target from a real click". (out/lab/20261111-110935: a click 400 px over the puppet's feet still took the puppet.)
* The test still catches BUG-0918: out/lab/20261111-110356-2 (`SPORE_FIX_OFF=game.avatar_stale_target`): FAIL.

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-final12b (the test's setup).
