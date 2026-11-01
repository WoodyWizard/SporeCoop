---
id: BUG-0414
title: Creature: the partner's creature pops ~1 m away and back for 0.13 s while the partner stands and uses an ability (guest at 30 fps, player network)
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: puppet_fight_player (w33)
role: guest
versions: 0.7.3.3+ (feat/w33-tests-creature de258df)
fixed_in: unreleased
branch: fix/w34-ability-pop
commits:
tests: tools/tests/coop_puppet_fight.py (suite puppet_fight_player)
signature:
docs: docs/re/creature.md#creature-out-of-view-ground
related: BUG-0360, BUG-0394, BUG-0434, BUG-0435
---

## Report
puppet_fight_player (guest at 30 fps, SPORE_NET=player), the guest watching the host fight: lab 20261101-151044,
t 37.31 s: the host stood (its own speed 0, the guest's interpolated target still, `tp` unchanged), its own animation
02481DE5 (an ability), and the guest's game drew the host's creature 0.97 m off the target for 4 frames (0.13 s),
then back in one frame: two steps of ~0.95 m in 27-32 ms. The first _player run (20261101-142305) had three such
steps of 0.77-1.0 m at the end of the guest's fight. Without the player network and the 30 fps cap the fight
showed no step in 4 directions (20261101-145803, 20261101-141938 guest side).

## Root cause
Observed (fix/w34-ability-pop, lane 4, `coop.track` with two new columns: the partner's animations played on the
puppet so far and the last one; docs/re/creature.md#creature-out-of-view-ground):
* Not an ability and not the animation relay: no MSG_ANIM came within 0.6 s before any of the steps (they are rare
  in a fight, 7 in 50 s); 02481DE5 is the animation of a stop (speed state 0). The driver did nothing at the step
  (standing branch, error 0.06 m: no walk, no nudge, no teleport; `cCreatureAnimal::Update` does not move a puppet).
* Every step of the two failing runs (0.77-1.0 m) was radial (down onto the plain terrain, `hd` 0.95 -> 0.00, and
  back) and happened while the puppet was **out of the observer's view** (vis 0 or screen x -800..4700): the test's
  `Watch` turns the camera, the fighter stood or walked on the raised ground of the fixture's nest mound (its own
  height 0.97 / 1.03 m over `GetHeightAt`). The game puts a creature outside the view on the plain terrain and
  lifts it onto the mound again when it is in view. Reproduced at will: the host standing on the mound, the guest
  turning its camera: 1.03 m steps every time the view swings over the puppet, at 7-40 m (72 in 70 s), none with
  the camera still.
* The observer never sees these steps. What it does see: the first frame back in view is still drawn low (the
  game lifts it one update later): BUG-0434, not fixed (writes at the mod's frame cannot keep the creature up
  out of view, docs/re/creature.md#creature-out-of-view-ground).

## Fix
The test judged what the player cannot see. coop_puppet_fight.py counts a step as a drawn jump only when the
puppet is on the observer's screen in both frames (`coop.track` vis / sx / sy); steps out of view are printed
(`off_view`), and a radial step from the first frame back in view is printed as `entry` (BUG-0434), not judged.
It prints the screen share and, per step, the partner's animations played on the puppet before it. No change
of the mod's driving (none would change what is drawn: see the root cause). The test also sets the fighter's
health to its maximum (`coop.team hp 1000` gave it 1000 of 10, which the invariant monitor's puppet_hp rule
failed every run on since the monitor was merged).

## Verification
<!-- filled below -->

## History
- 2026-11-01: opened.
- 2026-11-01: root cause found, the test fixed (fix/w34-ability-pop).
