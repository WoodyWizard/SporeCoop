---
id: BUG-0220
title: Core: shared money or tribe food spent by a guest right when another player joins comes back (spent twice)
status: open
kind: desync
stage: All
reported: 2026-10-28
source: GitHub #154 (review of release 0.7.3.0, 5801eea)
role: all
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
From GitHub issue #154: https://github.com/WoodyWizard/SporeCoop/issues/154 (the full analysis, links and the scenario are there).

**Summary**
Every `player_join` callback resets the shared resources' sync state. This includes guests, which get the callback for each other player who joins. Until the host's next total arrives, the guest's tick records no local changes, and that total then overwrites a purchase made in the window. The money or food comes back on the guest's screen, and the host never counts the spend.

Severity LOW: the window is about one round trip per join, so it needs 3-4 players. The result is the same "spent twice" that BUG-0106 (caae221) set out to remove; this is a path that fix did not cover. The code is older than the range and shipped unchanged in 0.7.3.0.

This issue also defines the shared-ledger semantics that #165 builds on.

**Where**
- The reset on every join, host and guest: https://gi

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
