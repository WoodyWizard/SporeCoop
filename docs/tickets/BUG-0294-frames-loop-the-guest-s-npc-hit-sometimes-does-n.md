---
id: BUG-0294
title: frames_loop: the guest's NPC hit sometimes does not arrive (test picks an NPC the guest has no proxy for)
status: fixed
kind: test
stage: creature
reported: 2026-10-30
source: lab 20261030-033555
role: test
versions: 
fixed_in: 0.7.3.2
branch: main
commits: 75a05bf
tests: frames_loop
signature:
docs:
related:
---

## Report
Smoke run 20261030-033555 on lane 4: `frames_loop` failed one check, `host: the guest hit arrived (2 -> 2)`. Every other check passed, there were no faults, and 4 reruns passed.

## Root cause
Inferred, not observed: the test took the first NPC in the host's `coop.npc list` and did not check the guest's answer to `coop.npc hit`. The host lists every NPC it tracks. The guest answers "no such spawned proxy" for an NPC it has no proxy for yet, and the host drops a MSG_DAMAGE_REQ for an NPC that is not in its current seen set (npc.cpp MSG_DAMAGE_REQ). Right after a warm reset either case can happen. Not a game or mod fault.

## Fix
Test only: the test tries the listed NPCs for up to 15 s until the host's `hits_applied` goes up, and logs the guest's answers.

## Verification
no-fail-run: an intermittent test failure (1 in 9 runs); the old test passed 4/4 in 20261030-034613. The new test passed 4/4 in 20261030-034952.

## History
- 2026-10-30: opened.
- 2026-11-01: released in 0.7.3.2.
