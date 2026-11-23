---
id: BUG-1449
title: tribe_held_item: the grip check counted the copy's hold while the host's chief held too (the mirrored hold, tribe.mirror_hold up to 8 s)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w65-night
commits: fb569645
tests: tools/tests/coop_tribe_looks.py (tribe_held_item)
signature:
docs:
related: BUG-1450, BUG-0906, BUG-1347, BUG-1349
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): tribe_held_item "FAIL rejoin: the copy keeps the staff in the grip (not held as in the middle of taking it for 4 s)".
The same check failed at "start" in 20261117-154911 (rel/0.7.15) and 20261119-214823 (rel/0.7.16); 1 of 11 on rel/main.

## Root cause
Test (one part): the 8 s loop counted only the guest's `hold`; the mod keeps a mirror's hold bound to the host member's
hold for up to 8 s on purpose (tribe.mirror_hold, kHoldBoundMax 8.0, tribe.cpp), so a correct copy of a host chief in
its own swing counted as stuck (both holding at once: 20261117-213354 "host ... hold=1 ... guest ... hold=1"); the loop
never read the host's hold. In this run the other cause is BUG-1450: after the rejoin #1 was the guest's member (frames
04-06 "Bob's chief", no owner=0 leader row), so on the guest it was no copy at all.

## Fix
coop_tribe_looks.py check_item: samples in which the host's member holds too are not counted; the loop waits (up to
20 s) for 8 samples without the host's hold, fewer than 4 is a FAIL; plus a new check that the host's leader is still
the host's member (owner 0), which catches BUG-1450. Nothing loosened: a copy that holds while the host's member does
not still fails.

## Verification
Was waiting for the lab run: PASS `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_held_item --lane N --retry 0
--no-build` (it can FAIL on the new owner check while BUG-1450 is open: that is the mod bug); FAIL
`SPORE_LAB_FIXOFF="tribe_held_item=tribe.held_item" SPORE_DETECTOR=20 python3 tools/lab.py run tribe_held_item --lane M
--retry 0 --no-build` (the mirrors' hands empty). no-fail-run: a test fix; FAIL before 20261122-194210.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tribe_held_item PASS in 20261123-023643; the twin SPORE_FIX_OFF=tribe.held_item FAIL in 20261123-021322 ("what the members hold has the host's size" differs); its FAIL in 20261123-015558 is the new owner check ("the host's chief #1 is still the host's member"), the mod bug BUG-1450. Closed.

## History
- 2026-11-23: opened; fix/w65-night: the grip check compares with the host's hold, the leader's owner checked.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
