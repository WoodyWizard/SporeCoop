---
id: BUG-9298
title: lab self-test mem_script fails now and then on a loaded WSL (the 400 MB script measured at 163 MB, the 25 s limit)
status: open
kind: test
stage: Tools
reported: 2026-11-21
source: make test on WSL, fix/civ-own-camera, 2026-11-21
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
`SKIP_SANITIZERS=1 make -k -j8 HOSTCXX=clang++ test` in WSL (Ubuntu-24.04, Windows 11 host with four Spore games of
other lanes running, 2026-11-21 09:2x) failed once in unit-lab: `FAIL mem_script: a script of 400 MB over a 150 MB
limit is killed and fails (FAIL lab: the test script used 163 MB of memory (limit 150 MB, SPORE_LAB_SCRIPT_RSS_MB): k)`.
The run right after passed with no change (log agentB/test-fix-civ-own-camera.log of lane B).

## Root cause
Not analysed. tools/tests/lab_selftest.py mem_script needs the kill and the whole check within 25 s
(`time.time() - t0 < 25`); a slow allocation on a loaded machine (163 MB seen, not 400) may pass that limit, or the kill
came later than the check expects. The same class as the known "zombie leader" WSL flake of unit-tools.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
