---
id: BUG-1071
title: Tribe: a guest's mirror of the host's chief can stay in a returned gesture for 9 s right after the session start (relayed animation 0; 1 of ~12 tribe_mirror_stale rounds)
status: open
kind: desync
stage: Tribe
reported: 2026-11-13
source: fix/w49-mirror, lab 20261113-054031-2 lane 7
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
tribe_mirror_stale on fix/w49-mirror (BUG-0972's fix on), out/lab/20261113-054031-2 round 1: the first round of the
session (the mirror's last relayed animation still 0), `coop.tribe mplay 1 04C6CB02` while the host's chief stood: the
mirror stayed in the gesture for the 9 s of the round (the other 11 rounds of the branch's runs: back within 0.7-2.1 s).
Not analysed. Candidates: the forced idle refresh after the pending tracks are ended picks the gesture again on this
mirror (its own save's chief), or the host's idle news (kind 10) for the chief did not come then.
Reproduce: `lab.py run tribe_mirror_stale --repeat 3`; read `coop.tribe holdtrace 1` (Z / I events) on the guest.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
