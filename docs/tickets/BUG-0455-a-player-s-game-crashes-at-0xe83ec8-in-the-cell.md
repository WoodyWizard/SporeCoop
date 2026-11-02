---
id: BUG-0455
title: a player's game crashes at 0xE83EC8 in the Cell editor when the host leaves the evolution (editor) screen, 0.7.3.4 (BUG-0004 signature again)
status: open
kind: crash
stage: Cell
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
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
Another player (forum post 'Cell Editor 0.7.3.4 Crash', with a video): crash dump crash-20261102-144335.dmp: 0xC0000005 at SporeApp.exe+0xA83EC8 (base 0xD90000) = dc04aee5 0xE83EC8, read of 0x9C; 'Game crashed when host leave evolution tab'. 0xE83EC8 is BUG-0004's signature (editor desync, guest crash, fixed long ago): a sibling path or regression. No report zip (the player could not send it); the .txt was only a Discord preview.
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
