---
id: BUG-0277
title: campaign finale: the host's 'finale goto core' stopped near the core while a cinematic ran (zone 2, busy=1)
status: open
kind: tooling
stage: space
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Lab 20261029-122937 campaign#2: `coop.space finale goto near` then `goto core` one second later;
the host stayed at zone 2 (dist 61.8 from the core) with a cinematic running (busy=1 cin=B6991750,
ctx 0), "the host waits at the core" failed and the guests flew to the host's place but not into the
core. The other two campaigns of the run passed the finale. Likely the campaign sends `goto core`
while the arrival cinematic near the core still runs.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
