---
id: BUG-0386
title: Tribe: a guest's walking mirror held back by its own game (an obstacle?) falls 3.6 m behind and lurches 1 m per frame to catch up
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-01
source: found by tribe_walk_mobile (fix/w30-tribe-walk)
role: guest
versions: unreleased
fixed_in: unreleased
branch: fix/w46-k3-bodies
commits: 0f0f32f1, 665d2c4a, e266ef4e
tests: body_trail_tribe_hold (tools/tests/coop_body_trail.py --stage tribe --hold), tribe_walk_mobile
signature: a walked member mirror at a game speed of 11-12 m/s moves 0.03-0.04 m a frame while every placing of ~0.4 m is undone; the error grows to 8 m, then a teleport
docs: docs/re/tribe.md#tribe-lead-keep
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
Observed (2026-11-07, fix/w46-k3-bodies, the per-frame track of fix/w45-remote-bodies-2 lab 20261106-172428 #2, out/lane9/track/tribe_walk_guest_walks_173129.json in that worktree, the guest's own member 2 on the guest at 22.3-23.7 s): while the game's speed of the mirror was 0 it was moved by the placing alone (0.3 m a frame, error 0.2-0.3 m); from the frame the game's walk took over (game speed 3 -> 11-12 m/s) every placing of 0.38-0.40 m was undone and the drawn position moved 0.03-0.04 m a frame for 0.9 s while the target walked on at 9 m/s; the error grew to 8 m and drive::step teleported it. walk_place never counted the placing as lost (its lost test needs over 0.5 m in one frame) and keep_lost (BUG-0361's rule for NPC proxies: moved by teleports with the mark while the game does not keep the place) was off for member mirrors. ? What holds the body (the game's own walk standing at 11 m/s) is still not known: no object within 3 m in the probe lines; static RE (2026-11-06, fix/w45-remote-bodies-2): creature-vs-creature contact pairs are skipped by the creature stage's gameplay contact handler (0xD3A450), no per-creature collision flag found (candidates: the loco force list at creature+0xC0, add 0xC44140, clear 0xC441A0).

## Fix
`coop.fix tribe.mirror_keep` (tribe.cpp mirror_drive): WalkPlace::keep_lost on for walked member mirrors: an error over 0.75 m that grows for 0.15 s (or a placing lost outright) moves the mirror by teleports with the mark at the catch-up speed until it is within 0.2 m of its path, the rule the NPC proxies have had since BUG-0361. Off: placed without the mark always (0.7.9). Test hook `coop.tribe undo <id> <s>` (guest): the mirror's walk and every unmarked placing undone (0.04 m a frame towards the aim), marked teleports kept: the hold as it was seen.

## Verification
FAIL (SPORE_FIX_OFF=tribe.mirror_keep): body_trail_tribe_hold out/lab/20261107-083228-2 (0 of 3: held copies 7.7 m and 19.9 m off, over 2 m up to 0.99 s), out/lab/20261107-081649-2 #2 (6.5 m, over 2 m 1.88 s; #1 there ran an earlier hook that undid the placing only).
PASS, final code: body_trail_tribe_hold out/lab/20261107-120427 (3 of 3); earlier code out/lab/20261107-101948 (3 of 3: held copies at most 2.9-3.4 m off, over 2 m at most 0.12 s), out/lab/20261107-082140 #1 (3.8 m, 0.21 s; #2 there 6.0 m for under 0.32 s failed the first 4 m gate, now 7 m). tribe_walk_mobile with the fix: out/lab/20261107-100645-2 3 of 3.

## History
- 2026-11-01: opened.
- 2026-11-06 (fix/w45-remote-bodies, M1): a different mechanism from the class's lag (BUG-0425): the guest's own game
  holds the placed mirror back (an obstacle or collision only there). With drive.lead a mirror's error is closed in
  ~0.25 s, at most at twice its speed + 3 m/s (catch_prop): a release after a hold is a faster catch-up, not slower. Not
  seen in 9 body_trail Tribe runs (longest stretch over 2 m 0.13-0.79 s); the blocker not found; open.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): static RE of the contact handling (creature pairs skipped, the separating function not found); not reproduced in the runs; closed not-reproduced with the blocker named.
- 2026-11-06 (fix/w45-remote-bodies-2, M2, later the same day): seen again by the new detector trail rows (BUG-0609) in play_tribe of the regression (out/lab/20261106-071756): `Tribe:trail:member`, member t13 over 2 m off the host's member for 2.34 s while the other members' copies were within p90 0.56 m (lag p50 0.012 s): a single mirror held back, this ticket's case, not the class's lag. Status back to open, blocked: what holds it is not found (see Root cause); the next step is a probe at that moment (`SPORE_DETECTOR=20 SPORE_DETECTOR_DIAG='coop.npctrack objects 5'` style dump of the objects within 3 m of the held mirror on both games, and the mirror's driver state), or the creature separation function.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): the probe is in (commit 0b19dfc4): a walked member mirror more than 2 m off its aim for 1 s logs `tribe: mirror #N held ... objects within 3 m: <noun@dist ...> (BUG-0386)`, and the proxies' audit line (BUG-0440) lists the objects within 3 m too (game::objects_near). First lines (out/lab/20261106-113612, with a tried BUG-0619 catch-up cap that itself slowed the mirrors): 26 holds, `place lost 0` and no object within 3 m in most (2 with noun 018C8F0C / 018C6D19 / 018C431C at 2.8-2.9 m): those holds were the catch-up cap, not an obstacle. A hold with the M1 driving and an object list was not caught yet.
- 2026-11-06 (fix/w45-remote-bodies-2, M3, BUG-0619 runs): reproduced on rel/0.7.9 + drive.lead with and without
  drive.lead_smooth, always the guest's own member 2 seen by the guest: its game walks the mirror (gv 11-12 m/s) but it
  moves 0.03-0.04 m per 33 ms frame for 0.9 s while its target walks on at 9 m/s, the error grows to 8.1 m, then the
  driver teleports it (tp 0 -> 1): out/lab/20261106-172428 tribe_walk_mobile#2 (t 23.07-24.00 s), -170614 #1 (8.13 m at
  62.15 s and 8.11 m at 1.22 s), -130123 #1 (8.31 / 9.39 m, drive.lead_smooth off); body_trail_tribe -164638 ("guest:
  Tribe member steps over 5 m: t2 8.0 m at 3.5 s"). The BUG-0386 probe did not log (it needs over 2 m for 1 s in
  walk_place). Track files: out/lane9/track/tribe_walk_guest_walks_*.json of those runs.
- 2026-11-07 (fix/w46-k3-bodies, K3): cause observed in the recorded track (the placing undone, never counted as lost, keep_lost off for mirrors); fixed by tribe.mirror_keep, test body_trail_tribe_hold with a planted hold. Status fixed, fixed_in unreleased.
