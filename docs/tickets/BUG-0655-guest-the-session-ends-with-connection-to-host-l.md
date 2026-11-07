---
id: BUG-0655
title: Guest: the session ends with 'connection to host lost (timeout)' after 29.5 s silence while the host keeps running
status: duplicate
kind: bug
stage: All
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: 
branch: fix/w46-s1-sweep
commits: 
tests: 
signature:
docs: docs/06-testing.md#testing-log-signals
related: BUG-0584, BUG-0585, BUG-0430, BUG-0308
---

## Report
5 runs on 4 branches: `net: the session with the host ended: connection to host lost (timeout)` 29.5 s after the host was last heard while the host keeps running; `no answer from host (timeout)` with rtt -1 on host_bundle. Examples: rel/0.7.8 out/lab/20261105-123757/long_creature_short.guest.game.log:139; fix/w45-world-objects out/lab/20261106-042445/host_bundle.guest.game.log:83; fix/w45-crashes out/lab/20261105-225531/cell_pause_rejoin.guest.game.log. Not yet checked whether the host stalled. Related: BUG-0030, BUG-0104, BUG-0138.

## Root cause
Observed: every case (8 guest logs 2026-11-04..06) had the host's game crashed or stopped first; the host's log ends 29-30 s before
the guest's timeout, so "the host keeps running" in the report was not so:
* rel/0.7.8 20261105-123757 long_creature_short: the host's log ends 12:46:08.8 (right after sending the world), the guest timed
  out 12:46:40 (BUG-0430 notes the same run's console timeouts); not seen again.
* fix/w45-world-objects 20261106-042445 host_bundle (+retry): the host's game faulted at its start (Renderer_SetVideoMode), the
  guest's join got no answer ("no answer from host", rtt -1).
* fix/w45-crashes 20261105-225531 cell_pause_rejoin: the host's game hung at the share's saved box (its console timed out).
* fix/w45-creature-reports 20261105-234209 creature_carry: the host faulted (0xC000001D in a hook trampoline after
  "hook_create 00B22560 failed: MH_ERROR_ALREADY_CREATED", a dev branch; 0 such lines in later runs).
* fix/w44-crashes 20261105-100942 crash_fruit_eat and fix/w43-transitions 20261104-224915 tribe_civ (SPORE_FIX_OFF=tribe.food_fx_mat):
  crash reproductions with the fix off (BUG-0584, BUG-0585).
The guest's session code (net/session.cpp: 30 s without a packet after the welcome, 10 s for the join) did the right thing.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
None needed in the guest. docs/06-testing.md#testing-log-signals: look at the host's log end before suspecting the guest.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not a guest bug; every case was a host that had crashed or hung first (runs above). 0 such lines in the rel/0.7.10
and fix/w46-* runs.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
- 2026-11-07 (fix/w46-s1-sweep): duplicate; see Root cause / Verification.
