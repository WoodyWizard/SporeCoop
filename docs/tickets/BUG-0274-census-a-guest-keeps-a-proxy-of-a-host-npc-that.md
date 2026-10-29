---
id: BUG-0274
title: census: a guest keeps a proxy of a host NPC that the host has at (0 0 0), 504 m apart (Creature)
status: open
kind: desync
stage: creature
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
campaign_short#2 and #3 of lab 20261029-122937 (out/lane2/campaign/20261029-135237 and
out/lane3/campaign/20261029-135721): census finding "c90: 504 m apart (creature)" (and c62 on Dave).
The host lists `c90 key=290D67AD alive=1 hp=10 ai=1 own=npc pos=(0.0 0.0 0.0)`; the guest has
`c90 key=7D5730C8 own=proxy pos=(136.0 -473.6 -106.5)` on the surface. A host NPC at the planet's
centre (not placed yet? removed without the alive flag?) whose proxy stays on the guest. 504 m both
times. Census files: 01_census.txt in those run folders.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
