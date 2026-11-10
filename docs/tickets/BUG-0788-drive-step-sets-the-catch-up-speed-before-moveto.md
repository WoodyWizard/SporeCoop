---
id: BUG-0788
title: drive::step sets the catch-up speed before MoveTo, and MoveTo resets it to the standard speed (suspected)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main fbc19da6 (docs/re/creature.md adventure follower locomotion)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0365, BUG-0281
---

## Report
No player report. Found while reading the GA fork's locomotion RE: a driven puppet or NPC proxy that is behind
(drive::step "walk" plan, catch-up) may walk at its species' standard speed instead of the faster catch-up speed, so it
lags and falls back to the 4 m snap more often than the code intends.

## Root cause
Inferred (from the fork's disassembly, not verified by us; confidence medium):
`mods/coop/driver.h:223-224` (drive::step, `pl.walk` branch) calls `game::set_speed(p, want_sp)` and then
`game::move_to(p, tgt, 0.3f)`. The fork found that native `cLocomotiveObject::MoveTo` (creature spatial vt 0xE0,
dc04 0xC435F0) restores the standard speed itself: at 0xC437C8 it calls spatial vt 0xCC (GetStandardSpeed, 0xC42080,
reads spatial +0x1E4) and at 0xC437E3 calls vt 0xC4 (SetDesiredSpeed) with that value and 0. So the speed set just before
is overwritten. We already saw the effect in Tribe (BUG-0365: "a MoveTo given after SetDesiredSpeed resets the speed")
and added `speed_after_goal` to drive::kinematic (`driver.h:335/345`, only Tribe sets it, `tribe.cpp:2731`), but the plain
drive::step walk path (creature puppets `modes.cpp:427`, NPC proxies `npc.cpp:1961`) still has the old order.
Suspected: the real impact depends on whether `want_sp` differs from the standard speed in practice.

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
## Verification

## History
- 2026-11-10: opened.
