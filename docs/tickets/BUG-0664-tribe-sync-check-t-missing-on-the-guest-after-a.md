---
id: BUG-0664
title: Tribe: sync-check t missing on the guest after a join while a member's body lies on the host
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-07
source: lab long_tribe 20261107-004042 (rel/0.7.9)
role: guest
versions: 0.7.9-dev
fixed_in: unreleased
branch: fix/w46-tribe-missing
commits: f165530b, 8ec44b21
tests: tribe_dead_rejoin (coop_tribe_dead_member.py --rejoin)
signature: sync-check: MISMATCH with the host (Tribe ...): t: 1 missing on the guest (t<id>); t<id> here: not listed (on the host too)
docs: docs/re/tribe.md#tribe-sync-bodies, docs/06-testing.md#testing-tribe-dead-rejoin, docs/known-issues.md#issue-tribe-join-body
related: BUG-0620, BUG-0381, BUG-0665
---

## Report
The 0.7.9 final check's rerun of long_tribe on a quiet lab (rel/0.7.9 d7fa8529, out/lab/20261107-004042 of that
worktree, lane 6) failed the sync-check gate twice: s 1865 "MISMATCH with the host (Tribe A3930F03, 66 objects
compared): t: 1 missing on the guest (t10)" (12 s after the guest's restart event had loaded the host's world) and
s 2315 "(82 objects compared): t: 1 missing on the guest (t8)" (6 s after its rejoin event had); the host: "2 checks 3 s
apart", and `t10 here: not listed` on the host too. long_tribe on rel/0.7.8 (20261105-155518) and 0.7.7
(20261105-015320): 0 MISMATCH lines.

## Root cause
A 0.7.9 regression of tribe.member_body (BUG-0620). Observed: the key was a member's body: on the host in no tribe's
member list (`not listed` in coop.tribe), and the host's bodies_sent grew across both joins (1778 -> 1979 around
s 1830-1900, 1979 -> 2212 around s 2300-2330, flat in between). Code: the host's `tribe::sync_entries` listed every
live object of g_hm, which since BUG-0620 holds bodies while the host's game keeps them; a guest spawns no mirror for a
body entry ("no new mirror for a body", it only lets a mirror it already had lie there), so a guest that joined or
reloaded the world while a body lay had no copy and the check said missing. With tribe.member_body off (0.7.8) the body
leaves g_hm at once: no key, no mismatch (tribe_dead_rejoin with SPORE_FIX_OFF=tribe.member_body,tribe.sync_bodies:
0 mismatches, lab 20261107-014701; its "body lay" precondition fails there as expected: no body sent).

## Fix
`tribe.sync_bodies` (tribe.cpp, BUG-0664): the sync-check compares members, not bodies; the host leaves g_hm entries
with body_since != 0 out of `sync_entries`, the guest its mirrors with the body bit. `coop.fix tribe.sync_bodies off`:
the host lists bodies again (0.7.9 before the fix). That a joining guest shows no body at all is BUG-0665 (0.7.10).

## Verification
tribe_dead_rejoin: FAIL with `SPORE_FIX_OFF=tribe.sync_bodies` out/lab/20261107-013812 (2 of 2, "t: 1 missing on the
guest (t3)") and out/lab/20261107-014224 (2 of 2: 5 and 6 mismatches of 6 checks); PASS with the fix
out/lab/20261107-014244 (2 of 2: 7 checks ok, the body sent in 6 of 6 samples). A/B with tribe.member_body off too
(0.7.8 behaviour): 0 mismatches, out/lab/20261107-014701.
Regression with the fix: play_tribe out/lab/20261107-014721 (OK), tribe_coop + tribe_dead_player
out/lab/20261107-015219 (OK 2/2); make -j8 and make test pass.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-tribe-missing): a 0.7.9 regression of tribe.member_body; fixed by tribe.sync_bodies; test tribe_dead_rejoin.
