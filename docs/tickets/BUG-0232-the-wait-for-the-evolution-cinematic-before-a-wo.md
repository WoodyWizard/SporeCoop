---
id: BUG-0232
title: the wait for the evolution cinematic before a world share adds up per guest and can go stale, so a later share goes out during the cinematic (BUG-0019 back once)
status: open
kind: bug
stage: Creature
reported: 2026-10-28
source: GitHub #166 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #166: https://github.com/WoodyWizard/SporeCoop/issues/166 (the full analysis, links and the scenario are there).

**Summary**
The host waits for the evolution cinematic before it sends its world (fix for BUG-0019, commit 4096536, release 0.7.3.0). That wait uses one global timer for all guests, and the timer is reset only when a world goes out. With two or more waiting guests the 120 s limit counts again for each guest in turn. A timer left over from a guest who left, or from a host who went to the galaxy menu, lets the next share go out at once during a cinematic: the guest then gets the hatchling world again (the BUG-0019 symptom, once). The timers are also not reset at the session end. Severity: wrong behaviour, low.

**Where**
- The timers: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/mods/coop/coop.cpp#L969-L977
- Set at a mode change: https://github.com/

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
