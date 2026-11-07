---
id: BUG-0665
title: Tribe: a guest that joins while a dead member's body lies on the host shows no body for it
status: open
kind: desync
stage: Tribe
reported: 2026-11-07
source: fix/w46-tribe-missing (BUG-0664 analysis)
role: guest
versions: 0.7.9-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Found while fixing BUG-0664: a guest spawns no mirror for a body entry of the host's snapshot (tribe.member_body,
BUG-0620), so a guest that joins or reloads the world while a dead member's body lies on the host (10-20 s, at most
60 s) has no copy of it. Reproduce: tribe_dead_rejoin (coop_tribe_dead_member.py --rejoin): the host sends the body
(bodies_sent grows) for 24 s after the join, the guest's `coop.sync` never lists its key (lab 20261107-014244).
Not checked: whether the guest's loaded world holds a body of its own there (the shared save). See
docs/known-issues.md#issue-tribe-join-body.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
