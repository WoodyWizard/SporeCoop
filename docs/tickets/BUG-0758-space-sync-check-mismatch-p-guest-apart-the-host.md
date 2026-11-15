---
id: BUG-0758
title: Space: sync-check MISMATCH p<guest> apart: the host's copy of a guest's own ship lags on its path (sync.own_path covers only the host's ship)
status: not-reproduced
kind: test
stage: Space
reported: 2026-11-08
source: lab 20261108-114435 campaign (rel/0.7.11 75365a87, lane 2)
role: host
versions: 0.7.3.1, 0.7.3.3, 0.7.10.1, 0.7.11
fixed_in:
branch: fix/w46-b1-space
commits: 7e49ddba
tests: space_fast_sync_guest
signature: sync-check: MISMATCH with player N ... (Space 40AE3044, 2 checks 3 s apart, 5 objects compared): p: 1 apart (pN 30-80 m), N = that guest's own id
docs: docs/re/space.md#space-guest-ship-path, docs/known-issues.md#issue-space-guest-ship-path
related: BUG-0547, BUG-0378
---

## Report
The 0.7.11 final check (lab 20261108-114435, campaign, lane 2, 4 players, Space world 40AE3044) failed the sync-check
gate twice in Space, each time on the guest's OWN ship (key p<its id>), not the host's (p0):
* s 4158.7: host and guest (Bob, id 1) `p: 1 apart (p1 79.8)` (campaign.host.game.log 4057, campaign.guest.game.log 4257);
  the host's trace drew Bob's ship at drawn_v 89.7 then 1.6 then 39.6 m/s in the seconds around it (fast key pulses).
* s 4308.4: host and extra2 (Dave, id 3) `p: 1 apart (p3 30.2)` (campaign.host.game.log 4156, campaign.extra2.game.log 4397);
  Dave's copy drawn_v 70.7 then 1.5 then 0.8 m/s (a ship that braked within the moment).
Both came during the campaign's rescue rounds ("X's ship is disabled / repaired" every 20-40 s).
The same signature on guests' ships before: campaign_space 20261031-205302 (0.7.3.1), 20261101-105523 (0.7.3.3),
play_space rel/0.7.3.6 20261102-204139 (p1 13.3, p1 20.6, p2 26.1/48.7/63.0). Not a 0.7.11 regression.

## Root cause
Observed (code): BUG-0547's `sync.own_path` (mods/coop/diag_sync.cpp own_path_check, ~line 352) takes back only a
difference of the host's own key `make_key('p', hd.self)` (p0): the host records its own ship's path every frame
(record_own_path) and nothing else. A guest's own ship is compared the other way round: the host's digest holds its
puppet (drawn a state late, interpolated) and the guest's digest the real ship; only BUG-0378's speed slack (speed at
the read x (0.2 s + rtt), add_speed_slack) covers it. A ship flying key pulses (accelerate, brake, turn within the
moment) is further back on its path than that speed explains: the same mechanism as BUG-0547, mirrored.
Inferred (no per-check detail line exists for p<guest>): the copies lay on the guests' paths, as BUG-0547 measured for p0.

Observed (fix/w46-b1-space, lane 6, 4 runs of `coop_space_fast_sync_guest.py`): the premise does not hold for a guest's
own key. The host compares its copy when the guest's digest arrives, and the states that put the copy there travelled
the same link at the same time, so the copy is only the interpolation delay and a send interval behind (unlike the
guest's copy of p0, a round trip more). 0 differences of p1 in 79, 104, 93 and 246 checks (lab 20261108-142942,
-143539, -144036, -144435): pulses at 350-840 m/s, full reversals with a check asked for 0.05-0.6 s into the turn,
300 ms spikes on the guest's link, the host at 12 fps, the guest disabled at full speed with checks right after.
The campaign's two cases persisted over the recheck 3 s later (the second with the guest at 1.5 m/s) and both came
while the host waited to share its world with a player who had just joined (`share_wait=planet`): not a path lag.
Cause not found; docs/re/space.md#space-guest-ship-path.

## Fix
Not reproduced, so no fix is claimed. Kept as diagnostics plus a guard (commit 7e49ddba, switch
`sync.own_path_guests`): the host keeps each partner's path from the received states and takes a p<guest> difference
back only when its copy lies on that path and the guest's ship within the limit plus speed slack; every p<guest>
difference now logs a detail line (copy and ship against the path), so the next sighting shows what it was.
The original option: the host keeps each guest ship's path from the states it received (the puppet's own snapshot
buffer, the last 2.5 s) and takes back a p<guest> difference whose guest position lies within the player limit of
that path's last kPathWindow + rtt, as own_path does for p0; log the same detail line for guests' keys.

## Verification
no-fail-run: not reproducible in two-player runs (no p1 difference at all in about 520 checks, the runs above).
`coop_space_fast_sync_guest.py` (suite `space_fast_sync_guest`) passes as a guard (no p1 MISMATCH, a forced 100 m
shift on the guest's digest is still a MISMATCH, any on-path difference is none): lab 20261108-145259 and
20261108-150234 (with space_fast_sync for the host's ship: OK in 20261108-145259).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-b1-space): 4 reproduction runs, 0 differences of a guest's own ship; closed not reproduced, diagnostics and guard sync.own_path_guests kept.
