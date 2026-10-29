---
id: BUG-0235
title: Netcode: a player who is loading or saving is dropped when someone with the same name joins; a crash rejoin into a full session is refused
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #169 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #169: https://github.com/WoodyWizard/SporeCoop/issues/169 (the full analysis, links and the scenario are there).

**Summary**
The BUG-0209 fix (commit 1d4cd27, release 0.7.3.0) decides that a new player is the same person as an existing entry when the two have the same name and the old entry has been silent for 2 s. Loading or saving can keep a player silent for longer than that. So with the default name "Player", a friend who joins while another "Player" is busy takes over that player's entry. The busy player is dropped by the host without being told, and only notices 30 s later.

The same fix leaves two gaps:
- a crashed player who rejoins a full 4/4 session is still refused ("session is full");
- a player who does rejoin sees a copy of their own old entry as a partner for a moment.

Severity: desync / wrong behaviour (a player is dropped from the session).

**Where**
- The name and silence check, r

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
