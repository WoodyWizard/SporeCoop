---
id: BUG-0685
title: Tribe: in the 4-player campaign one guest's mirror of the host member #4 (the leader the guest's game took by tribe.leader_sync) is gone for 33 s+ (sync-check t: 1 missing on the guest)
status: open
kind: desync
stage: Tribe
reported: 2026-11-07
source: lab 20261107-141346 campaign (rel/0.7.10 e7d1734c, lane 2)
role: guest
versions: 0.7.10
fixed_in:
branch:
commits:
tests:
signature: sync-check: MISMATCH with the host (Tribe 9652F8AD, 33 objects compared): t: 1 missing on the guest (t4); guest `t4 here: not listed`, host `t4 here: #4 ... dead=0 ... tool=11 leader=1`
docs:
related: BUG-0652, BUG-0664, BUG-0645
---

## Report
The 4-player campaign on rel/0.7.10 (lab 20261107-141346, lane 2, 98 min) failed the sync-check gate in Tribe
(world 9652F8AD), besides its stuck findings. Two kinds:
* extra1 (Carol) at s 2973.9 and s 3007.0, the host the same at s 2973.9 ("2 checks 3 s apart"): `t: 1 missing on the
  guest (t4)`; Carol `t4 here: not listed`, the host `#4 266877C8 owner=0 ... hp=150.0 pos=(144.4 -470.7 -106.4) dead=0
  bt=04601D70 tool=11 leader=1` (campaign.extra1.game.log 2767-2785, campaign.host.game.log 2960).
* guest (Bob) and the host at s 2056: `t: 1 apart (t29 9.4)`, once (campaign.guest.game.log 2262).

## Root cause
Not found. Observed:
* #4 is the host's member that leads its tribe (staff, tool 11). On every guest tribe.leader_sync (BUG-0652, new in
  0.7.10) made its mirror the guest tribe's leader again and again: `member #4 leads its tribe here too ... (the leader
  was none)` at s 1635, 1644 (Carol's rejoin), 1996 and 2349 on Carol (the log stops after 5 lines), the same on the
  other two guests: the guest tribe kept losing its leader.
* From s 2960 Carol's game was paused (`busy=input_lock,paused`, only on Carol); its mirrors went 3 -> 2 between s 2950
  and s 2980 (removed 68 -> 69) and #4 was not made again while the host kept sending it (no body bit: the host lists it).
* On the guest a mirror whose object is gone is made again on the next snapshot unless it is a body, its tribe is
  not mapped (`g_local_tribe_of[m.tribe]` null) or `spawn_member` fails (tribe.cpp ~2470-2500); no log line says which.
Inferred candidates, none checked: the guest's game removes or replaces the leader mirror (its own chief rule after
SetChieftain) and spawn_member fails while the game is paused or the tribe is at its population cap
(wsync.tribe_level, BUG-0645, new in 0.7.10); or a lost mapping of that tribe. The t29 "apart" is the BUG-0410/0674
class. No 0.7.9 full campaign log exists to compare (rel/0.7.9 has none); tribe t-missing lines before were only after
joins (BUG-0664) and the tests that hold states.

## Fix
<!-- needs a dedicated agent: a removal/spawn-failure log line per mirror, then a repro (tribe_leader_sync with a pause
     and a full tribe) and `SPORE_FIX_OFF=tribe.leader_sync` / `wsync.tribe_level` A/B -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
