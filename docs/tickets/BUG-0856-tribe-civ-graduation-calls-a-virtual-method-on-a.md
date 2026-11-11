---
id: BUG-0856
title: Tribe -> Civ: graduation calls a virtual method on a null domesticated-animal pen (cTribe +0x19C4) (suspected for our saves)
status: open
kind: crash
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9049
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: fork: runtime 0xFCD466 = canonical 0xCDD466, null data; stack SimCamera_Update+806 (B14296), CDECAC, CDF11C
docs: docs/re/tribe.md#tribe-fork-graduation-notes
related: BUG-0576, BUG-0495
---

## Report
The fork's host crashed right after saving its City Hall design (Editor -> Tribe, paused): graduation hides the
village's assets and calls a method on the tribe's domesticated-animal pen, which was null. All seven tribes of the
fork's restored fixture had null pen and egg-pen pointers. Our tribe_civ test passed, so it is open whether our saves
or fixtures can have a null pen.

## Root cause
* Observed (fork, decoded): CDD461 calls C90060, which returns tribe+0x19C4 (mpDomesticatedAnimalsPen, ModAPI
  cTribe.h), then calls its vt 0x54 with argument 0 and no null check.
* Ours: no guard (code read). Unknown whether a vanilla tribe can have a null pen (probably not normally; maybe after
  a load of a save made with our savestrip or with a fixture). Suspected, confidence low.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9049, switch tribe.graduation_pen); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

gamefixes.cpp: an instruction-site hook on the sequence at symbol SporeCoop::TribeGraduation_HidePen
(`mov edx,[eax]; mov ecx,eax; mov eax,[edx+54]; push 0; call eax`, 11 bytes 8B 10 8B C8 8B 42 54 6A 00 FF D0,
installed only on an exact byte match). A naked stub keeps every register, the flags and the stack: if the switch is on
and eax (the pen) is 0 it counts and jumps past the 11 bytes, else it jumps to the trampoline. Neither branch after the
sequence uses its flags or result. Switch `tribe.graduation_pen` (BUG-9049); status line "absent graduation pens N".

## How to test
First read player tribe +0x19C4 / +0x19C8 on our tribe fixture and after a guest-made save before City Hall. If a
null pen exists, tribe_civ on that fixture must FAIL on main (crash after the City Hall editor) and PASS with the
guard. If no null pen can be made, close as not-reproduced and keep the RE note.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
