---
id: BUG-0575
title: Tribe: the guest crashed after double-clicking the home icon (3010a413:SporeApp.exe+0x6826F3)
status: open
kind: crash
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The guest crashed after double-clicking the home icon in Tribe. Crash 0xC0000005 at 0x00B626F3 (SporeApp.exe+0x6826F3; chain cSpatialObject::SetModelKey+0x594 <- QuaternionToMatrix <- cModelObject <- cPlanetaryArtifact::SetLocomotion <- Property::GetValue <- SerializerReadStream::GetRecord), Steam build 3010a413, coop 0.7.5. The chat just before: the guest gave tribe members to the host several times.
Reports: SporeCoop-report-2026-11-04 200118 (crash-20261104-194521) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
