---
id: BUG-0820
title: Space finale: the galactic core waits forever for a partner who already entered and flew away
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0198
---

## Report
A player reaching the core is told it 'waits for your partners: A' although A went in earlier; the wait never ends, only F9 'Enter now' gets it in. Found by reading the code.

## Root cause
Observed in the code, confidence high. space_finale.cpp:268-279 missing() lists every partner in Space whose zone is not Z_GATE; it ignores F_ENTERED and g_reached_by. h_Enter (:294-309) blocks while the list is not empty. BUG-0198 lists 'a partner who never comes', not this. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
In missing() skip the partners with F_ENTERED or an entry in g_reached_by.

## How to test
Host `coop.space finale goto core enter`, `coop.space finale alone`, `coop.space finale enter`, then fly away; guest `coop.space finale goto core enter`, `coop.space finale enter`: FAIL = status waiting=1 entered=0 for 60 s; PASS = the guest enters.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
