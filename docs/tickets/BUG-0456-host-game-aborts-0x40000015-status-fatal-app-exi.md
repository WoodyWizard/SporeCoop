---
id: BUG-0456
title: host game aborts (0x40000015 STATUS_FATAL_APP_EXIT) at 0xD8F2AD (SporeApp.exe+0x98F2AD), 0.7.3.4
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
Dump crash-20261102-184600.dmp: exception 0x40000015 (a fatal app exit / abort) at SporeApp.exe+0x98F2AD (base 0x150000) = dc04aee5 0xD8F2AD, host, 2026-11-02 18:46. Cause unknown; the .txt was only a Discord preview. Next: Ghidra on 0xD8F2AD (who calls abort: a CRT check, pure virtual call, assertion).
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
