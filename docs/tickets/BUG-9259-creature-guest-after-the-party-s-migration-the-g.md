---
id: BUG-9259
title: Creature guest: after the party's migration the guest's home nest flips back to the old nest (no nest, no mating, repeated 'Bringing your nest')
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: player report (SporeCoop-report-20261118-215146.zip), owner
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-9128, BUG-0640, BUG-9151, BUG-9175, BUG-9210, BUG-9163
---

## Report
Owner 2026-11-18: BigTexas "is also unable to see their own nest, and they cannot mate", and "has been repeatedly
getting Bringing you to your nest notifications and being forced into loading."
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed in the report (guest log) and the host's log, not reproduced yet. Host 22:42:48: "seat 1 (BigTexas)'s nest at
(-257 431 -28) follows its evolution", then "BigTexas's species is migrating to a new nest. Everyone migrates
together". Guest: 21:42:47 "our home nest follows the host's (-257 431 -28)", 21:42:47 "wsync: the host's species
migrates ... this game migrates to (-295 406 -67) too (BUG-0640)", 21:43:08 joined (-295 406 -67); from 21:43:18 every
10 s for 10+ minutes "nest: our home nest (-257 431 -28) has no members here yet: joined later" (nest.cpp follow_home,
BUG-9128). Host 22:45 and 22:49: "seat 1's nest has no living member: the next one in 2 s (BUG-9175)".
Inferred: two rules pull the guest's home two ways: the migration follow (BUG-0640) moved it to the new nest, while the
host's home message (MSG_NEST kHome, the guest's own seat nest of home_nests) still names the old one, whose herd is
empty here. The "Bringing your nest" loads (world shares of BUG-9167 / BUG-9210) were not found in this log (the player
restarted the game six times in 30 minutes; the earlier logs hold some).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix

<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification

<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
