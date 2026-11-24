---
id: BUG-9375
title: A partner's game can keep the pause 0x4BF38A6 after a refused stage-end request and a later editor
status: open
kind: bug
stage: Civ
reported: 2026-11-24
source: deep playtest 2026-11-24
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tools/qa_win (manual, the deep playtest of 2026-11-24)
signature:
docs: docs/known-issues.md#known-pause-after-refused-stage-end
related:
---

## Report
Deep playtest 2026-11-24 (Civ -> Space, the MSI): a partner's spacecraft request was refused ("Only your party's
leader moves your party on to the next stage", the first form of BUG-9368's fix; the log: "the game's editor-pending
flag was left set with no editor coming (the request was refused): cleared, the stage is drawn again (BUG-0920)").
Its next request opened the editor; after the editor and the change into Space the game kept the pause reason
0x4BF38A6 ("an editor is coming"): Mission Control did not open and the load of the shared orbital save was held
until `coop.pauses sub 04BF38A6`. Inferred, not shown: the refusal's cleanup leaves one Pause without its Resume.
BUG-9368's corrected fix no longer refuses that request; the same refusal exists for a party's partner pressing
History's check at the end of Creature or Tribe (not run). Reproduce: as a party's partner on a galaxy era press
the stage's Advance and History's check (refused), then open any editor and leave it; `coop.pauses`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-24: opened.
