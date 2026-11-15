---
id: BUG-0665
title: Tribe: a guest that joins while a dead member's body lies on the host shows no body for it
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-07
source: fix/w46-tribe-missing (BUG-0664 analysis)
role: guest
versions: 0.7.9-dev
fixed_in: 0.7.11
branch: fix/w46-k8-open
commits: 4278b714
tests: tribe_join_body, tribe_dead_rejoin, tribe_dead_player, tribe_body_teardown, tribe_dead_member
signature:
docs: docs/known-issues.md#issue-tribe-join-body, docs/06-testing.md#testing-tribe-join-body
related: BUG-0620, BUG-0664, BUG-0667
---

## Report
Found while fixing BUG-0664: a guest spawns no mirror for a body entry of the host's snapshot (tribe.member_body,
BUG-0620), so a guest that joins or reloads the world while a dead member's body lies on the host (10-20 s, at most
60 s) has no copy of it. Reproduce: tribe_dead_rejoin (coop_tribe_dead_member.py --rejoin): the host sends the body
(bodies_sent grows) for 24 s after the join, the guest's `coop.sync` never lists its key (lab 20261107-014244).
Not checked: whether the guest's loaded world holds a body of its own there (the shared save). See
docs/known-issues.md#issue-tribe-join-body.

## Root cause
Observed (code + lab 20261108-003733): the guest's snapshot apply (tribe.cpp, guest chunk loop) skipped every entry
marked as a body (`if (c || m.body) continue`: "no new mirror for a body"), so a body entry without a mirror never got
one. The guest's own loaded member of it (alive in the shared save, written before the death) was then removed at the
end of the cycle as a member the host does not know. In the FAIL run the host's body had no citizen within 3 m on
the guest's game in all 6 samples (0.1-20.7 s after the join), while the host kept sending it (bodies_sent grew).

## Fix
`tribe.join_body`: a body entry without a mirror binds the nearest unbound local member of its tribe (the loaded one)
or spawns one, as a live entry does; guest_tick then plays its death (tribe.ko, health 0) and takes it out of its
tribe (detach_body, BUG-0620), as for a member that died while the guest was there; it goes with the host's body
(the snapshot drops it). `coop.tribe` (guest) counts them as `join_bodies=`. Off: no mirror for a body (0.7.10).

## Verification
tribe_join_body (`coop_tribe_dead_member.py --join-body`: the --rejoin flow, and while the host's body lies the
guest's game has a citizen within 3 m of it), lanes 4 / 5:
- FAIL with SPORE_FIX_OFF=tribe.join_body: out/lab/20261108-003733 (0 of 6 samples with a partner).
- PASS: out/lab/20261108-003738 (5 of 5), out/lab/20261108-003944 (with tribe_dead_rejoin, tribe_dead_player,
  tribe_body_teardown, tribe_dead_member: 5/5 passed).

## History
- 2026-11-07: opened.
- 2026-11-08: fixed on fix/w46-k8-open (tribe.join_body).
- 2026-11-08: released in 0.7.11.
