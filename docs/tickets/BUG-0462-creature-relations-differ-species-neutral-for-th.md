---
id: BUG-0462
title: creature relations differ: species neutral for the host are hostile for the guest; the guest's roar does nothing
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
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
Host's mark (s 2109): 'creatures which show neutral for host are hostile for guest + ROAR ability is broken for guests and work only host'. Relations (social table) and the guest's social / scare abilities on host NPCs (the host's AI runs them) are not shared. Classes G1/G3 (BUG-0418, BUG-0420).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
