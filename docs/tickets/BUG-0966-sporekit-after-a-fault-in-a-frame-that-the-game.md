---
id: BUG-0966
title: SporeKit: after a fault in a frame that the game's handler caught, outside-frame console commands get no answer while the frames go on
status: open
kind: tooling
stage: Tools
reported: 2026-11-12
source: fix/w48-rest, lab 20261112-133753-2 / -134051 / -134349 (a fault of an unreleased first version of BUG-0930)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen three times on fix/w48-rest while testing a first (unreleased) version of BUG-0930's fix: the host's game faulted
at 0x00C0439F (a creature's Write called with no stream: a wrong vtable slot of ours) inside a frame, sporekit logged
`frames: the frame N was cut off by a fault the game's handler caught (1 so far); frames go on (BUG-0667)`, and from
then on the host answered no `coop.puppet respawn` (an SK_CMD_OUTSIDE_FRAME command; the test's console call timed out
after 90 s) while its frames, its trace lines and the main-thread / any-thread commands (`coop.status`, `mem.read`)
went on (lab out/lab/20261112-133753-2, 20261112-134051, 20261112-134349; lane 5 and lane 4).

## Root cause
Not analysed. Candidates: the outside-frame queue's deferral (g_out_deferred, BUG-0672) not resumed after the cut-off
on that path, or a lock of the mod held by the cut frame. How to reproduce: a fault inside a mod's frame handler that
the game's own handler catches (kit.test.excfault?), then any SK_CMD_OUTSIDE_FRAME command (`coop.ui where 0`).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
