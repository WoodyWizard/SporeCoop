---
id: BUG-1187
title: tribe_zero_fields fails on rel/0.7.15 3a7f4b25: the host member's hunger 0 does not reach the guest's mirror (stays 100)
status: open
kind: bug
stage: Tribe
reported: 2026-11-15
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: tribe_zero_fields
signature:
docs:
related: BUG-1043, BUG-1140, BUG-1185
---

## Report
Found by feat/w51-harness (D4, BUG-1185) in its lab sample on lanes 4/5: `tribe_zero_fields` fails its BUG-1043 check
"the guest's mirror is as starving as the host's member": after `coop.tribe hunger <id> 0` on the host (the host's row
reads `(0, 0, 0.0)`) the guest's mirror keeps hunger 100 for the 5 s of the wait (`(0, 0, 100.0)`). The tool steps
before it (tool 1, then tool 0: also a zero value) pass, and so does the statistics check after the leave.

Three runs, all on lane 5, build of feat/w51-harness (its C++ is rel/0.7.15 3a7f4b25 unchanged, only tests and tools
differ): 20261115-225537 and 20261115-230446 (the codemodded test), 20261115-230558 (the test's text of rel/0.7.15
3a7f4b25 put back for the run): the same FAIL each time. So it is not the codemod. The last PASS on record is
feat/w51-tribe 20261115-182010 (before the merges of C1 / BUG-1140 into rel/0.7.15); 2 of 8 runs since 2026-11-13
failed in the ledger (before: other steps, fix/w49-small 2026-11-13).

To reproduce: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_zero_fields --lane N --retry 0 --no-build` on a build
of rel/0.7.15 3a7f4b25.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
