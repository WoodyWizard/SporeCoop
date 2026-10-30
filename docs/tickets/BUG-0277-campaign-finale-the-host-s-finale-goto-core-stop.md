---
id: BUG-0277
title: campaign finale: the host's 'finale goto core' stopped near the core while a cinematic ran (zone 2, busy=1)
status: fixed
kind: tooling
stage: space
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: main (fix/w8-stuck), closed on fix/w9-tests
commits: 1ff5d07
tests: campaign (finale)
signature:
docs: tools/tests/coop_campaign.py finale()
related:
---

## Report
Lab 20261029-122937 campaign#2: `coop.space finale goto near` then `goto core` one second later;
the host stayed at zone 2 (dist 61.8 from the core) with a cinematic running (busy=1 cin=B6991750,
ctx 0), "the host waits at the core" failed and the guests flew to the host's place but not into the
core. The other two campaigns of the run passed the finale. Likely the campaign sends `goto core`
while the arrival cinematic near the core still runs.

## Root cause
Inferred from the run: the arrival near the core plays a cinematic (busy=1); a `goto core` sent while it runs is not taken, the host stays in zone 2.

## Fix
Test tool only (1ff5d07, already on main before 0.7.3.1): `finale()` waits until the host's cinematic is over (`coop_space_finale.settle`) before `goto core`, checks zone 3 within 30 s and tries again up to 3 times (back to the galaxy view between tries).

## Verification
FAIL: lab 20261029-122937 campaign#2 (host stayed at zone 2). PASS: lab 20261029-203821 campaign#1, #2, #3: "the host waits at the core" ok in all three (zone=3 at the first try). no-fail-run: a timing race of the tool, not reproducible on demand.

## History
- 2026-10-29: opened.
- 2026-10-30: fix/w9-tests: found fixed by 1ff5d07 (main); the final campaign 20261029-203821 passed the goto core step 3 of 3. Closed.
