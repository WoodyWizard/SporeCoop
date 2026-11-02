---
id: BUG-0440
title: Tests: chaos_tribe fails on the monitor's proxies gate (one Tribe NPC proxy over 3 m off the host's NPC)
status: open
kind: test
stage: Tribe
reported: 2026-11-01
source: fix/w34-ability-pop smoke (lab 20261101-173425)
role: guest
versions: 0.7.3.3+
fixed_in:
branch:
commits:
tests: chaos_tribe
signature:
docs:
related: BUG-0402
---

## Report
Smoke on lane 4 (lab 20261101-173425, fix/w34-ability-pop: changes only coop.track columns and the puppet_fight
test, nothing in Tribe): chaos_tribe failed on the log gate `monitor: 1 on guest (proxies): proxies: guest: 1 NPC
proxies over 3 m off the host's NPC (Tribe) on 2 lines from s 60`; the test's own checks passed.

## Root cause
Not analysed. Possibly the BUG-0402 class (one animal apart on a guest).

## Fix
Not fixed.

## Verification
no-fail-run: not fixed (seen once in another branch's smoke run).

## History
- 2026-11-01: opened (fix/w34-ability-pop).
