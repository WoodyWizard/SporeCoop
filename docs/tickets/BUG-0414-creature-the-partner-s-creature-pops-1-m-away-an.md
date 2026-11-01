---
id: BUG-0414
title: Creature: the partner's creature pops ~1 m away and back for 0.13 s while the partner stands and uses an ability (guest at 30 fps, player network)
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: puppet_fight_player (w33)
role: guest
versions: 0.7.3.3+ (feat/w33-tests-creature de258df)
fixed_in:
branch: feat/w33-tests-creature
commits:
tests: tools/tests/coop_puppet_fight.py (suite puppet_fight_player)
signature:
docs:
related: BUG-0360, BUG-0394
---

## Report
puppet_fight_player (guest at 30 fps, SPORE_NET=player), the guest watching the host fight: lab 20261101-151044,
t 37.31 s: the host stood (its own speed 0, the guest's interpolated target still, `tp` unchanged), its own animation
02481DE5 (an ability), and the guest's game drew the host's creature 0.97 m off the target for 4 frames (0.13 s),
then back in one frame: two steps of ~0.95 m in 27-32 ms. The first _player run (20261101-142305) had three such
steps of 0.77-1.0 m at the end of the guest's fight. Without the player network and the 30 fps cap the fight
showed no step in 4 directions (20261101-145803, 20261101-141938 guest side).

## Root cause
Not analysed. Inferred: the ability played on the puppet (the mod replays the partner's animations) moves it by
its own root motion or action in the guest's game; the driving pulls it back to the target a few frames later in
one step. Not checked: which abilities, whether the 30 fps cap or the network is needed.

## Fix
Not fixed.

## Verification
puppet_fight_player fails on it (`drawn jumps within the bound`), 2 of 2 runs: 20261101-142305, 20261101-151044.
The per-frame rows are saved in out/lane5/track/puppet_fight_*.json of the worktree.

## History
- 2026-11-01: opened.
