---
id: BUG-0972
title: Tribe: same_view_tribe view 1: the host's avatar (Alice) crouches at the fire on the host while the guest's puppet of it stands about 1 m off in a stepping pose and Bob's chief differs in pose
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-last same_view_tribe lab 20261112-172240 (#1)
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-mirror
commits: 093807dc
tests: tribe_mirror_stale, same_view_tribe
signature:
docs: docs/re/tribe.md#tribe-mirror-pose-phase, docs/known-issues.md#issue-tribe-same-view-avatar-pose, docs/06-testing.md#testing-t2-mirror
related: BUG-0936, BUG-0992, BUG-0988
switch: tribe.mirror_stale_anim
---

## Report
Key frame 01_view1 of same_view_tribe-1 in out/lab/20261112-172240 (fix/w48-last, a54a52bf): on the host, the host's
own creature "Alice" crouches next to the fire and Bob's chief stands bent forward; on the guest, its puppet of Alice
stands about 1 m to the left in a stepping pose and Bob's chief is mid-step with a leg raised. Views 1 of the other
two runs (-2, -3) agree. The test's creature match passed (positions within its tolerance). Seen once; it may be an
animation phase at the moment of the still (the avatar's puppet is driven by the creature sync, not the member relay).
Reproduce: `lab.py run same_view_tribe --repeat 3` and compare 01_view1 pairs.
Seen again (fix/w48-ftribe review of the 0.7.13 final check, out/lab/20261112-175447 sheet 054_same_view_tribe_01_view1):
the same view, the other way round: the host's avatar "Alice" stands at the fire on the host and crouches on the guest
(its puppet), Bob's chief stands on the host and is in another pose on the guest. Same class (the avatar's puppet is
driven by the creature sync, its pose by the relayed animation at the moment of the still): folded in here.

## Root cause
Observed (probe, both games' `coop.tribe anims` at 5 Hz): "Alice" in Tribe is the host's chief, member #1, shown on
the guest by a member mirror (not the creature puppet). Its mirror played the host's animation in 59 % of the samples
(other members 89-98 %); 257 of the rest were the host's chief standing (0x02481DE5) and the mirror in the gesture
0x04C6CB02. The guest's hold trace showed how: a relayed restart of the gesture waited behind the running one as a
pending track (BUG-0992), the next relayed start (the change 0x02C39200) played, and after it the waiting gesture came
back and ran for 37 s. The idle fix (kind 10, BUG-0936) left it: the host sent "idle" and the mirror's animation was
not the last relayed one, which the fix took for the mirror's own idle or walk. The rest of the pose difference in
the stills is the animation's moment (BUG-0988).

## Fix
A mirror in an animation that is not its locomotion's idle / walk, while the host's member is in its idle or walk, has
its pending tracks ended (AnimatedCreature_ClearPending) and its idle refreshed. Switch `coop.fix tribe.mirror_stale_anim off`.

## Verification
Test `tribe_mirror_stale` (coop_tribe_mirror_stale.py: while the host's chief stands, the guest's mirror plays the
gesture as a returned start, `coop.tribe mplay`; back in its idle / walk within 4.5 s).
FAIL (SPORE_FIX_OFF=tribe.mirror_stale_anim): out/lab/20261113-045727 (two rounds never back in 9 s). PASS:
out/lab/20261113-045727-2 (0.7-2.1 s), and the final check runs (see History).

## History
- 2026-11-12: opened.
- 2026-11-12: fix/w48-ftribe: a second frame (final check 054) folded in; still open.
- 2026-11-13: cause found and fixed on fix/w49-mirror (package T2 of 0.7.14).
- 2026-11-13: fix/w49-mirror final check: one round of 12 (the first of a session) not cut in 9 s: BUG-1071.
- 2026-11-13: fix/w49-mirror final check: tribe_mirror_stale PASS 20261113-052132 (lane 7), 20261113-054031 and 20261113-054834-2 (lane 6); same_view_tribe PASS 20261113-051820-2, 20261113-054834, 20261113-054834-2 (key frames looked at: poses agree; the staff grip differed in one still, BUG-1069).
- 2026-11-14: released in 0.7.14.
