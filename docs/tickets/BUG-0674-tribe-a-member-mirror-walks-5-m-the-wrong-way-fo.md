---
id: BUG-0674
title: Tribe: a member mirror walks 5 m the wrong way for 0.5 s at a walk start on the guest (body_trail_tribe_player, 8.9 m off)
status: open
kind: desync
stage: Tribe
reported: 2026-11-07
source: fix/w46-k3-bodies lab 20261107-102038
role: guest
versions: unreleased (rel/0.7.10 + fix/w46-k3-bodies)
fixed_in:
branch:
commits:
tests: body_trail_tribe_player
signature:
docs:
related: BUG-0410, BUG-0425
---

## Report
body_trail_tribe_player#1 of lab 20261107-102038 (fix/w46-k3-bodies 6baee26a, lane 7, load ~15-25 on 6 cores) failed `no copy over 2 m off for more than 1.0 s` by 0.01 s (longest 1.01 s, t2 at 4.1 s; members p90 1.81 m passed). In the track (out/lane7/track/body_trail_tribe_*.json of that run, coop.bodyrec): at 3.85 s the guest's mirror of member 2 stepped 2.97 m in one 75 ms frame (a 151 ms guest frame follows), then walked 5 m in the opposite direction from the host's member (y -5.5 -> +1.3 while the host's went -4.0 -> -6.3) for 0.45 s, 8.9 m off at 4.6 s, and closed it in ~1 s. The host's rows show no jump of the member.

How to reproduce: tools/lab.py run body_trail_tribe_player --repeat 3 --retry 0 with SPORE_BODYREC_DIAG=1 (the aim / raw lead rows show whether the target went the wrong way); seen once in 9 runs of this branch.

## Root cause
Not analysed. Inferred: the lead target went the wrong way: M1's lead_velocity (net/interp.h) takes the state's own velocity, capped in speed by the path between the two newest states but not in direction; a state whose game velocity points against its path (a member turning at a walk start, or the guest's frame hitch) would be extrapolated the wrong way for up to kMaxLead. Not confirmed (that run had no diag rows).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
- 2026-11-07: opened (fix/w46-k3-bodies, K3), not fixed there.
