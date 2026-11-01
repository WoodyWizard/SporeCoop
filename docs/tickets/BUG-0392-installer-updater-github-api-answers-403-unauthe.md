---
id: BUG-0392
title: installer/updater: GitHub API answers 403 (unauthenticated rate limit, shared IP / VPN) and the setup cannot install; fall back to the releases/latest web redirect and the release asset URL
status: open
kind: bug
stage: Installer
reported: 2026-11-01
source: player report on Discord 2026-11-02 (setup 0.7.3.3: 'GitHub not reachable (the server answered 403 for ...)')
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
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
