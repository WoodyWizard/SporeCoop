---
id: BUG-0578
title: Crash at Graphics::cModelWorld::FinishBackgroundLoad+0x2735 on 0.7.3.6 (3010a413, mode none)
status: open
kind: crash
stage: none
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.3.6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
A crash in an older run (coop 0.7.3.6, SporeKit 0.3.6, 2026-11-03), carried in the 2026-11-04 report: 0xC0000005 at 0x00AF3D05 Graphics::cModelWorld::FinishBackgroundLoad+0x2735, chain StandardFileFilter::IsValid <- cFrustumCull::FrustumTestAABB <- Model::Release <- GeneralAllocator_FreeInternal. Game mode none (a load).
Reports: SporeCoop-report-2026-11-04 175959 (crash-20261103-210113) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
