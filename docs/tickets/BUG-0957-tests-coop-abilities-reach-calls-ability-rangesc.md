---
id: BUG-0957
title: Tests: coop.abilities reach calls Ability_RangeScale without its creature argument; the game faults reading the caller's stack (play_creature bots hang)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-12
source: lab, fix/w48-reports (play_creature)
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-reports
commits: f714646d
tests: play_creature, avatar_stale_target
signature: 0xC0000005 at 0x00D38A56 SporeCoop::Ability_RangeScale+0x26 (data 0x000008F8), ecx = a stack address, on host and guest
docs:
related: BUG-0618, BUG-0917, BUG-0779
---

## Report
Lab, fix/w48-reports: play_creature failed 4 of 4 on lanes 4 and 5 (lab 20261112-053508, -055106, -055415, -055727):
both games logged one fault `0xC0000005 at 0x00D38A56 SporeCoop::Ability_RangeScale+0x26 (data 0x000008F8)` 20-30 s
into the play, both bots then timed out on their console calls, no hunt happened. The same test on the branch's base
ab74ffdf passed (lab 20261112-060019-tmp-w48-reports-base); the branch's first commit 87d4b985 (SporeKit only)
already failed (lab 20261112-061202-tmp-w48-bis-kit).

## Root cause
Observed (disassembly of 0xD38A30): Ability_RangeScale is `cdecl float(int kind, cCreatureBase* c)`: it reads the
creature from its second argument (`mov esi,[esp+0x10]` after two pushes), `c+0xE84` (getter 0xC0C300) and that
object's +0x358. The test command `coop.abilities reach` (debug.cpp, BUG-0618; the play bots poll it) called it as
`float(int)` with 0 only: the function took whatever the caller's stack held as the creature. On the base build that
slot happened to hold 0 (the function returns 1.0); the SporeKit changes of this branch moved what lies on the stack
there and the game read a stack address as the creature (ecx 0x0012D120). Players were never affected (a test
command).

## Fix
`coop.abilities reach` passes the avatar: `Ability_RangeScale(0, avatar)`. The .sym entry is corrected (CORRECTION).
No switch (a test command's wrong call).

## Verification
* FAIL before the fix: lab 20261112-053508, 20261112-055106 (play_creature, the fault above).
* PASS with it: avatar_stale_target lab 20261112-061656 (polls `coop.abilities reach` every 50 ms); play_creature lab 20261112-061627.

## History
- 2026-11-12: opened.
- 2026-11-12: opened and fixed (fix/w48-reports), met by the final check of R3.
