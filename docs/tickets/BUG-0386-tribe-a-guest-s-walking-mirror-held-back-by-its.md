---
id: BUG-0386
title: Tribe: a guest's walking mirror held back by its own game (an obstacle?) falls 3.6 m behind and lurches 1 m per frame to catch up
status: not-reproduced
kind: desync
stage: Tribe
reported: 2026-11-01
source: found by tribe_walk_mobile (fix/w30-tribe-walk)
role: guest
versions: unreleased
fixed_in:
branch: fix/w45-remote-bodies-2
commits:
tests: tribe_walk_mobile (tools/tests/coop_tribe_walk.py)
signature:
docs:
related: BUG-0365
---

## Report
tribe_walk_mobile (out/lab/20261101-091623, tribe_walk_mobile#2, lane 3, both games at 30 fps, mobile
network, with BUG-0365's tribe.mirror_walk on): the guest's mirror of its own member #7, walking at 9 m/s
in a group, fell behind its target from 0.1 m to 3.6 m within 0.8 s while its drawn step shrank to
0.11-0.22 m per 32 ms frame (the placing allows 0.38 m) and its game speed rose to 20 m/s; then it was
released and moved 1.09, 0.92 and 0.88 m in three frames (27-34 m/s) and caught up within 0.25 s. The
other mirrors were 9 m away. The per-frame track: out/lane3/track/tribe_walk_guest_walks_091855.json
(not in git). Seen once in 14 direction-views with the fix.

## Root cause
Not found (2026-11-06, fix/w45-remote-bodies-2). Static RE (GOG 2024): creature-vs-creature contact pairs are skipped by the creature stage's gameplay contact handler (0xD3A450: type 2 pairs of two creatures, noun 0x18EB45E, are not handled; creature-vs-object goes to 0xD3A0D0, the bump of rocks and ornaments): what pushes a placed mirror back is not in the gameplay code (inferred: Havok's own solver or the locomotion's avoidance). No per-creature flag that takes one creature out of collision was found (candidates: the loco force list at creature+0xC0, add 0xC44140, clear 0xC441A0; a phantom registration like the eggs' 0xB48870). Observed: the same kind of hold on NPC proxies (BUG-0440's single audit lines: place lost while a herd mate is 3.3 m away).

## Fix
No fix: not reproduced on 0.7.9.

## Verification
no-fail-run: tribe_walk_mobile (30 fps, mobile network) passed with drive.lead on out/lab/20261106-053105 and the runs of this branch's final code (see the report), and with it off out/lab/20261106-062826 (2 runs); M1: no stretch over 2 m longer than 0.79 s in 9 body_trail Tribe runs. One FAIL (out/lab/20261106-063850: member jumps and stop-and-go with 369 target corrections) was with a tried lead smoothing (BUG-0445, removed). Needed for a fix: the function that separates creatures or a collision flag (above).

## History
- 2026-11-01: opened.
- 2026-11-06 (fix/w45-remote-bodies, M1): a different mechanism from the class's lag (BUG-0425): the guest's own game
  holds the placed mirror back (an obstacle or collision only there). With drive.lead a mirror's error is closed in
  ~0.25 s, at most at twice its speed + 3 m/s (catch_prop): a release after a hold is a faster catch-up, not slower. Not
  seen in 9 body_trail Tribe runs (longest stretch over 2 m 0.13-0.79 s); the blocker not found; open.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): static RE of the contact handling (creature pairs skipped, the separating function not found); not reproduced in the runs; closed not-reproduced with the blocker named.
