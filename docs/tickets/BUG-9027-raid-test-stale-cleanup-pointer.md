---
id: BUG-9027
title: Native raid test cleanup calls a retired citizen after timeout
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-04
source: m2-conquest-goalc.log
role: test harness
versions: duo10-dev
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_raid.py --conquest
signature: App::Property::SetValueVector3+0x3F reads 00000124
docs: docs/re/tribe.md
related: BUG-9025 BUG-9026
---

The test failed to acknowledge a delayed second upgrade caption, timed out,
then ran cleanup using cached citizen pointers. Native deaths had retired a
citizen: its owner was null, so cleanup passed address 124 as a Vector3.
Crash: crash-20261104-155811-i4.txt. Stack and console exception identify the
test's direct SetValueVector3 call. This is not evidence of a production raid
crash. Cleanup now resolves current mapped living guest citizens and requires
a non-null owner. The result acknowledgment remains active during transfer
waiting, including the delayed upgrade screen. Qualification pending.

PASS safe cleanup on the failed battle m2-conquest-goald.log, both processes
still alive and zero faults. PASS full conquest m2-conquest-goale.log, including
cleanup and zero faults. The native crash report remains the negative control.
