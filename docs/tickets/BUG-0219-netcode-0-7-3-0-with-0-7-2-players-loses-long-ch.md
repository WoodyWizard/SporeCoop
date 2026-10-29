---
id: BUG-0219
title: Netcode: 0.7.3.0 with 0.7.2 players loses long chat lines and big species tables (or empties the table), and the "protocol did not change" note is wrong
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #153 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #153: https://github.com/WoodyWizard/SporeCoop/issues/153 (the full analysis, links and the scenario are there).

**Summary**
The 0.7.3.0 release notes say "The network protocol did not change: 0.7.3.0 plays together with 0.7.x". But 0.7.3.0 has a new wire feature. BUG-0034 (commit a8341cf) splits a reliable message bigger than one packet into pieces marked with flag bit 2, and a 0.7.2 receiver does not know that bit. It reads every piece as a whole message:
- a long chat line is lost;
- a host's species table with 42 to 83 species is read as an empty table (bigger tables are lost or, rarely, read as a few wrong records).

In the other direction, a 0.7.3 guest with a 0.7.2 host keeps every Cell meal it eats in a list that is never trimmed (BUG-0051 code).

Severity: minor desync in mixed-version sessions (no stall, no crash), plus a small memory growth per session, plus a wrong compatibility note.

**

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
