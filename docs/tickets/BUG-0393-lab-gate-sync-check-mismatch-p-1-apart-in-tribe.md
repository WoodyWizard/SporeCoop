---
id: BUG-0393
title: Lab gate: sync-check MISMATCH 'p: 1 apart' in Tribe while a player's view jumps (the RTS player point, 20-70 m)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-01
source: lab gates (feat/w30-lab-gates) on fix/w30-tribe-walk and fix/w31-* branches
role: both
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w31-civ-gate (the fix), fix/w32-tribe-sync (the Tribe verification)
commits: e052b04
tests: chaos_tribe, tribe_walk_mobile
signature:
docs:
related: BUG-0380, BUG-0381
---

## Report
With the log gates on, `chaos_tribe` (out/lab/20261101-104002, lane 3) and `tribe_walk_mobile`
(out/lab/20261101-101600 #1, lane 3) failed on `gate sync-check`: "sync-check: MISMATCH with the host
(Tribe A3930F03, 38 objects compared): p: 1 apart (p0 69.9)" on both games, the 3 s recheck too. Every
tribe member (t) and animal (n) matched. The same line is in other branches' Tribe runs today
(fix/w31-* lab dirs, p0 27.7 / p1 19.7 / p0 40.8, also 02:08 before these branches).

## Root cause
Decided with runs on lane 7 (fix/w32-tribe-sync, 2026-11-01): the check, not the sync. `p` of a Tribe player
is its RTS pointer (modes.cpp rts_fill: the planet point under its mouse, moved by the mouse and by every
view change, the minimap jumps included); the other game's copy is the pointer a state and the
interpolation delay behind, and the two digests are read half a round trip (more with resends) apart: a
pointer that moves (or whose view jumps) is tens of metres "apart" without any desync. BUG-0380
(fix/w31-civ-gate, `diag.rts_cursor`) leaves a Tribe / Civ pointer that moved within the last second out
of the compare on both games; all the recorded runs of this ticket were made without it (their branches
had not merged it yet). With it the Tribe runs had no `p apart` (below); with it off the same test had it
again. At rest the copy matches (Civ: 0 m, coop_civ_cursor.py, BUG-0380), so the guests do show the
partner's pointer where it is. Before (kept for the record): in Tribe a player's `p` entry is its RTS point (the view / cursor marker, rts_drive), which jumps
tens of metres when the player moves the view by the minimap (both tests do, every second or so); the
other game's copy is a state behind (over a mobile network 120-180 ms more). Not checked in diag_sync.cpp
which point `p` is in Tribe.

## Fix
No new code: `diag.rts_cursor` (BUG-0380, commit e052b04, on main since 85675b9) is the fix for Tribe too.
A pointer that stands still for a second is still compared, so a real pointer desync would show.

## Verification
Log gates on, lane 7:
* FAIL with `SPORE_FIX_OFF=diag.rts_cursor`: out/lab/20261101-130015 tribe_walk_mobile#2 ("p: 1 apart (p1 37.8)"
  on both games while the guest walked its members; 1 of 3 runs).
* PASS: out/lab/20261101-124441 (tribe_walk 3/3, tribe_walk_mobile 3/3), out/lab/20261101-115510 (chaos_tribe 2,
  tribe_coop 2, tribe_play 2, play_tribe 2: no sync-check MISMATCH at all); before, on fix/w30-tribe-walk
  without the switch, tribe_walk_mobile had `p apart` in most runs.

## History
- 2026-11-01: fix/w30-npc-walk: the same gate also on animals in Tribe: out/lab/20261101-125335 tribe_walk "n: 3 apart
  (n51 9.1 n53 8.5 n50 8.3)" at s 19 after the join, both checks 3 s apart; the same signature (n 8.2-11 m) in 11 main-checkout
  runs before (not from this branch). Not analysed.
- 2026-11-01: opened.
- 2026-11-01 (fix/w32-tribe-sync): decided: the check (moving pointer), fixed by BUG-0380's diag.rts_cursor; FAIL / PASS runs on lane 7.
- 2026-11-02: released in 0.7.3.4.
