---
id: BUG-0788
title: drive::step sets the catch-up speed before MoveTo, and MoveTo resets it to the standard speed (suspected)
status: not-reproduced
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main fbc19da6 (docs/re/creature.md adventure follower locomotion)
role: 
versions: 
fixed_in:
branch: fix/w48-creature
commits: 075bbcc2
tests: puppet_speed_probe
signature:
docs: docs/re/creature.md#creature-c2-audit-0713
related: BUG-0365, BUG-0281
---

## Report
No player report. Found while reading the GA fork's locomotion RE: a driven puppet or NPC proxy that is behind
(drive::step "walk" plan, catch-up) may walk at its species' standard speed instead of the faster catch-up speed, so it
lags and falls back to the 4 m snap more often than the code intends.

## Root cause
Verified in the code (Ghidra, dc04): MoveTo 0xC435F0 ends with vt 0xCC GetStandardSpeed and vt 0xC4 SetDesiredSpeed(that, 0) on every path. But the order has no effect on a Creature puppet: the game sets its desired speed (+0x1E0) again on every frame. Standing, a speed set by SetDesiredSpeed (0xC11570 writes +0x1E0) is 0 on the next frame (puppet_speed_probe, lab 20261112-074517: 7.0 / 2.5 / 9.0 -> 0.0 in all 6 samples). Walking to the step's goal with the speed set after the MoveTo, the desired speed was the standard 4.0 in all 6 samples while 3.02 was asked (lab 20261112-073116, the first version of the test with the swapped order).

## Fix idea
From the fork (jwhans0n/SporeCoopGA fbc19da6, docs/re/creature.md "Adventure follower locomotion"); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
The fork's adventure follower issues MoveTo first and the desired-speed adjustment after it (they also added
`game::standard_speed(c)` = spatial vt 0xCC to read the native standard speed and scale catch-up from it). For us: in
drive::step swap the two calls (MoveTo, then set_speed) behind a twin switch (e.g. `coop.fix drive.speed_after_moveto`),
or make `speed_after_goal` the default for drive::kinematic as well.

## How to test
Two games, Creature: the partner walks a straight line at a fixed speed while its puppet starts ~3 m behind
(`coop.puppet` hold then release). Sample the puppet's native speed on the watching game (spatial +0x1E0 desired speed and
velocity +0x288) right after each drive::step command. FAIL (old order): desired speed equals the standard speed (+0x1E4)
and the puppet does not close the gap; PASS: desired speed equals want_sp and the error shrinks without a snap.

## Fix
None: the swapped order (tried as drive.speed_after_moveto) changed nothing and was taken out again; drive::step keeps its order with a comment. Test commands `coop.puppet speed` / `speedset` stay.

## Verification
no-fail-run: the reorder showed no effect (lab 20261112-073116: desired 4.0 = standard in all samples with it); puppet_speed_probe 20261112-074517 shows the per-frame reset.

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): not reproduced: the game resets a puppet's desired speed every frame; the reorder had no effect and was removed.
