---
id: BUG-0346
title: crash_triage misses known tickets of a relocated exe (Windows base 0x410000+, Steam 0x810000) and names unknown functions by the nearest symbol
status: fixed
kind: tooling
stage: All
reported: 2026-10-31
source: triage of the 2026-10-31 player reports
role: 
versions: all
fixed_in: 0.7.3.2
branch: fix/w22-reports
commits: 7ce1249
tests: tools/tests/tools_selftest.py triage (make test)
signature: tooling (no game fault)
docs:
related: BUG-0158, BUG-0247, BUG-0008, BUG-0012
---

## Report
Triage of two player zips (2026-10-31). The host's old crashes (Windows, exe loaded at 0x00760000)
were `SporeApp.exe+0x3777BE` and `Simulator::CreatureStageEventHandler+0x674` at 0x010A8634.
`crash_triage.py` called both new, although they are BUG-0012 (0x7777BE) and BUG-0008 (0xD48634):
tickets and `.sym` files number addresses from the preferred base 0x400000, the tool searched for
the RVA form and the relocated absolute address only. The guest's Steam exe was loaded at
0x00810000, so its absolute addresses (and the `calls:` lines of the log) were 0x410000 off too.

A second, smaller trap: SporeKit names a frame by the nearest preceding symbol of the `.sym` files,
without function bounds, so frames in unnamed functions get misleading names
("Simulator::cCreatureAbility::GetAnimationID+0x127D" is FUN_004d4f70, "SporeCoop::
cCreatureAnimal_GrowTo+0xF89" is FUN_00c05ea0). The names are right only for small offsets.

## Root cause
Observed: `triage()` built its search terms from `where` and `eip` only.

## Fix
`crash_triage.py` reads the exe's load address (the log's `exe ... base=0x...` line or the report's
module list) and prints an `address` line with the fault address numbered from 0x400000; that
address is also searched in the tickets. The frame-name trap is not changed (SporeKit side, a
possible later change: print `FUN_<addr>`-style names past a size limit).

## Verification
no-fail-run: tooling only, no game run. tools_selftest.py triage (f): two made-up relocated reports
(a crash file with a module list, a log with `base=`) at 0x00FCAE60 / +0x86AE60, base 0x760000:
FAIL with the old crash_triage.py (`SELFTEST_TRIAGE=<copy of main's tool>`: no address line, no
BUG-0018), PASS with the new one. On the real zips it now finds BUG-0008 and BUG-0012.

## History
- 2026-10-31: opened and fixed on fix/w22-reports.
- 2026-11-01: released in 0.7.3.2.
