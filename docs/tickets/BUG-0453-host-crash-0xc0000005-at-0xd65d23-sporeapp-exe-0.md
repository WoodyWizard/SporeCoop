---
id: BUG-0453
title: host crash 0xC0000005 at 0xD65D23 (SporeApp.exe+0x965D23, read 0) inside the NPC behaviour tree tick, 0.7.3.3 and 0.7.3.4
status: open
kind: crash
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
Two host crashes with the same signature: 2026-11-01 14:27 (0.7.3.3) and 2026-11-02 18:03 (0.7.3.4), Creature, main thread, read of address 0. crash_triage chain: SporeCoop::BT_HerdLandScore+0x27C2 <- (frame names misleading, BUG-0346) <- Simulator::BehaviorTreeTickGeneric+0x540 <- ... i.e. a behaviour-tree node of an NPC reads a null object. Inferred: a node whose target is one of our objects (puppet, proxy, nest egg copy, pack member) or an object freed by the mod. Next: Ghidra on 0xD65D23 and the node's caller, the host log around the crash (report folder 20261102-180656). Class G9 (BUG-0426).
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
