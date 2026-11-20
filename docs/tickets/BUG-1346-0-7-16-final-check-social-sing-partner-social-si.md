---
id: BUG-1346
title: 0.7.16 final check: social_sing_partner / social_sing_own_lowfps never judged, social_sing_own fails on the walk start clip (test bugs)
status: open
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: host
versions: 0.7.16 (unreleased)
fixed_in:
branch: fix/w60-dna
commits: 48dfc9fa
tests: social_sing_own, social_sing_own_lowfps, social_sing_partner (coop_social_sing.py; --old: the test before)
signature:
docs:
related: BUG-1241, BUG-1250, BUG-1244
---

## Report
rel/0.7.16 worktree out/lab/20261120-041304: social_sing_own FAIL "while the guest runs the host's game shows it with its own animations only
(['04866D8E'])"; social_sing_own_lowfps and social_sing_partner "INCONCLUSIVE the host's creature never sang in 40 s
(the social game did not start)", lowfps INCONCLUSIVE in its last 4 runs, partner in every run since 0.7.15.1.

## Root cause
Test bugs, the mod behaves as designed:
* social_sing_partner: unreachable precondition. The game starts no social game with a puppet (it runs no AI to answer
  the click's stimulus, BUG-1241) and since 0.7.15.2 the click in the social stance does not pick the partner at all
  (creature.partner_social_hover, BUG-1250, puppets.cpp): the host never sings (rel/0.7.15.1 -110806, rel/0.7.15.2
  -134852 / -144132, rel/0.7.16 20261119-214823 and 20261120-041304; fix/w52-evoeditor -100537 / -101233 / -102747).
* social_sing_own_lowfps: at 10 fps a camera drag of `aim` takes ~17 game frames (1.7 s) and the test read the screen
  point 0.5 s after it while the camera still turned; the click hit the ground and the host walked off (host log: no
  "social: relationship event" for the Kook, the host 1.5 to 30 m from its start; key frame 01_sings_host: no game).
* social_sing_own: the walk start clip 04866D8E (the start of idle_walk_lookat 04866DAB, docs/re/creature.md) on the
  puppet counted as foreign: it is shorter than one sample round and fell into the 2 s after W went down that the test
  did not sample on the own avatar (the same flake failed social_sing_own_walk on rel/0.7.15.1 -110806, 0.7.15.2 -144132).

## Fix
coop_social_sing.py: the aim waits for the game's frames after each drag and for the target's screen point to stand
still (4 frames, 12 px), the click's own frames, and the host clicks again after 8 and 16 sing presses without a song;
the run's first 2 s of the own avatar count as its own animations and a start clip whose loop the own game played
counts as played (social animations stay foreign always); --to-partner judges the guest's side (no song, no social
pose, no pool effect on either game) when no song came, and says so in the log (the variant's point: the partner's
creature takes up nothing). `--old` keeps the test as it was (a manual twin: INCONCLUSIVE for lowfps).

## Verification
Open, waiting for the lab run (no lab today). PASS expected:
  SPORE_DETECTOR=20 python3 tools/lab.py run social_sing_own social_sing_own_lowfps social_sing_partner social_sing_own_walk social_sing social_sing_own_flock social_sing_reverse --lane 2 --lane 3 --retry 0 --no-build
(social_sing_own three times: it failed about 1 run in 3 before). Manual twin: `SPORE_FPS=10 python3 tools/tests/coop_social_sing.py --own --walk --old` INCONCLUSIVE.

## History
- 2026-11-20: opened.
