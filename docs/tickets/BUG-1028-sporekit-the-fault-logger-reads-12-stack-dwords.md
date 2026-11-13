---
id: BUG-1028
title: SporeKit: the fault logger reads 12 stack dwords past the end of the stack's region and loses a fault that faults again inside it
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-6
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-kit
commits: 438e1cdb, 4ebce297
tests: kit_vehedge
signature:
docs: docs/02-modkit.md#modkit-report-safety
related:
---

## Report
Audit 0.7.14, KIT-6. Not seen at runtime, found by code reading. The fault logger (the vectored handler) copies 12
dwords from the faulting thread's esp after checking only that esp's page is committed; when esp is near the end of a
committed region (BUG-0956's off-stack faults put esp into heap memory) the copy reads past the region and faults
inside the handler. That second fault is ignored (SK_TF_IN_EXC) and goes on to the game's handlers: the first fault
is never recorded (no g_last_*, no log line, no report of it) and the second may end the game without our report.
Also: the checks take MEM_COMMIT as readable without looking at PAGE_NOACCESS / PAGE_GUARD (sporekit/probe.cpp
stack walks, debug tools). A player can hit it only after another fault with a broken esp: rare, Low.

## Root cause
Observed in code (main efd2e0ba):
* sporekit/exceptions.cpp:233-238: `VirtualQuery(sp)` and `mbi.State == MEM_COMMIT`, then `r.stack[i] = sp[i]` for
  i < 12 without `sp + 12 <= end` (the scan below it is bounded by `end`, the copy is not). No Protect check.
* exceptions.cpp:218-221: a fault while SK_TF_IN_EXC is set returns EXCEPTION_CONTINUE_SEARCH; g_last_* are written
  only later (250-253), so the first fault's record is lost.
* probe.cpp:49 and 153: per-dword VirtualQuery with MEM_COMMIT only (probe and ftrace, debug commands).
Audit detail wrong: exe_return_address's `v < lo + 6` (exceptions.cpp:52) lets c[-7] read one byte below the first
section, which is the PE header page: mapped and readable, harmless. module_return_address bounds its reads by the
region (31-35).

Runtime / fix/w49-kit:
Confirmed at runtime (lane 7, `SPORE_FIX_OFF=vehbounds`, labs 20261113-011047, 20261113-011847-2): `kit.test.vehedge` faults with esp 8 bytes
below the end of its stack's committed region; the fault logger's 12-dword copy read the reserved page after it and
faulted inside the logger; that fault was ignored (SK_TF_IN_EXC) and the first one was never recorded (the logger's
last fault was not it). probe.cpp / ftrace took MEM_COMMIT as readable (code).

## Fix idea
One helper `readable_range(p, n)` (MEM_COMMIT, no PAGE_NOACCESS / PAGE_GUARD, the whole range inside the region)
used by the VEH copy (copy min(12, dwords left in the region)), probe.cpp and the report's stack scan; write the
fault's record (g_last_*) before any read of the faulting stack. Switch: `kit.fix vehbounds`.

## How to test
New `kit.test.excfault offstack_edge` (test command): set esp to 8 bytes before the end of a committed heap block and
fault (the BUG-0956 test does an off-stack fault; reuse its harness). Assert the fault appears in `exc` and the log.
Main: no record of the first fault: FAIL. Lab test on one lane (coop_offstack_report.py extended).

## Fix
sporekit/exceptions.cpp, `kit.fix vehbounds`: the fault's record (g_last_*) is written before any read of the faulting
stack; the copy takes min(12, dwords left in esp's region) and only when the region is not PAGE_NOACCESS /
PAGE_GUARD. probe.cpp (probe and ftrace stack walks) also stops at PAGE_NOACCESS / PAGE_GUARD. Test command
`kit.test.vehedge` (a thread whose TIB stack limits point at a block of ours, 192 KB committed then reserved; a last
vectored handler sends it back).

## Verification
Test kit_vehedge (`kit_freeze.py --case vehedge`, one game):
* FAIL `SPORE_FIX_OFF=vehbounds`: out/lab/20261113-011047 (recorded=0) and out/lab/20261113-011847-2 (recorded=0, nested=1: the second fault inside the logger).
* PASS: out/lab/20261113-010700 (recorded=1, faults=1) and out/lab/20261113-011847 (recorded=1, nested=0); kit_freeze (all cases) PASS out/lab/20261113-011847.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit (FAIL/PASS twin).
