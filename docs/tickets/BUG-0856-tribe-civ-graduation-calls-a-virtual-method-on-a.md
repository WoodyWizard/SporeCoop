---
id: BUG-0856
title: Tribe -> Civ: graduation calls a virtual method on a null domesticated-animal pen (cTribe +0x19C4) (suspected for our saves)
status: not-reproduced
kind: crash
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9049
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch: fix/w48-tribe-civ
commits:
tests: tribe_pens
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
Not reproduced: our player tribes have their pens.
* Observed (Ghidra): the call is in 0xCDD0F0 (called from 0xCDE7E0), which takes `GetPlayerTribe` and at 0xCDD461
  calls 0xC90060 (returns tribe +0x19C4) on **the player tribe only**, then its vt 0x54(0).
* Observed (runtime, lab 20261112-091444 and 20261112-091646, `tribe_pens`): the player tribe of the host's and the
  guest's game has both pens (+0x19C4, +0x19C8 non-null) on the tribe fixture, after the host's game formed a tribe,
  and on the guest after a rejoin (its world loaded from the host's share save). Every computer village has null pens
  in the game itself (the fixture's and the ones the game forms), which graduation does not touch.
* tribe_civ (the end of Tribe through the City Hall editor) passes on our fixture. The fork's restored fixture had
  null pens in all its tribes, the player tribe included: a state of its restore, not of the game or our saves.

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
None: graduation reads only the player tribe's pen, which the game always has in our games (docs/re/tribe.md#tribe-graduation-pen).

## Verification
no-fail-run: no null pen of a player tribe found. Evidence: `coop_tribe_pens.py` (suite `tribe_pens`, tribe
fixture, ~1.5 min): lab 20261112-091444 (every tribe printed: the player tribe's pens set, the villages' null) and
20261112-091646 (the player tribe's pens on both games, after a formed tribe and after a rejoin).

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12 (fix/w48-tribe-civ): not reproduced: the call reads the player tribe's pen only; the player tribe has its pens in every game state looked at.
