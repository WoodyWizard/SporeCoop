---
id: BUG-0815
title: Creature: the nest mate does not answer the mating call when it wandered beyond the native reply radius, suspected
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-0177 follow-ups (commits d2c3b10, 4f21c55, adcacb0; lineages38..40-dev)
role: host guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests: creature_mating (tools/tests/coop_creature_mating.py)
signature: mating call plays, no heart over the mate, a click on the mate does nothing; BT node 0x02D852F0 stays in phase 1
docs: docs/re/creature.md#creature-mating-call
related: BUG-0177
---

## Report
Fork player reports (Lineages 37..40, from our 0.7.3.6 code): after the Mating Call the nest marker shows but no
heart cloud over a nestmate; clicking the mate does not start the egg cinematic, or only after it moved. Our
BUG-0177 (fixed in 0.7.2.4) covered the bot not reaching the editor; these are new failure modes.

## Root cause
Fork findings (runtime, GOG 2024 / Steam code; not verified by us, so suspected for our build):
* The mate's reply score `0xD7DAB0` (cdecl float(nestmate), 1 when player and mate are in range) uses a native
  radius around the nest of about 9.67 m. A trace caught the player 1.61 m from the nest and the selected mate
  11.30 m away: the mate scored 0 for good (it roams while the call waits).
* In phase 2 of the avatar's BT node 0x02D852F0 (tick 0xD6D560, our `SporeCoop::BT_MateTick`) the mate sometimes
  resumed roaming and the native code kept waiting for the paired WalkTo to finish
  (`0xC42F80`, thiscall bool(creature spatial +0xC0): locomotion goal reached).
* The heart effect 0x9005E011 was missing on an eligible mate (runtime check).
* The click classifier `0xD30830` (our `SporeCoop::CreatureCursorKind`) returns 9 for an eligible mate; it returned 0
  while the simulator input-block counter was 1 (left at 1 by native HUD hover after keyboard-only movement with
  the virtual cursor over the editor's Done button). Partly a co-op cause there: the fork's wide FOV lens and the
  virtual cursor (see BUG-0804).
* Unclear for us: a run with the mod unloaded reached phase 2; whether vanilla Spore has the same radius stall was
  not settled. Our code has no hook on 0xD7DAB0 / 0xD6D560 for this (grep: none).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages BUG-0177, commits 4f21c55 and adcacb0); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* Hook `0xD7DAB0` (their `SporeCoop::CreatureMateResponse`): call the original; when it returns 0, the avatar runs
  the mating action (0x02D852F0 or 0xD23FE7CA), the creature is the avatar's animal target and the classifier
  0xD30830 says 9, and the player is within radius + 0.25 m while the mate is within radius + 5 m, return 1.
* Hook `0xD6D560` (their `CreatureMateStep`, cdecl bool, six stack arguments): in phase 2 substep 1, if the mate is
  alive, not a puppet, in the avatar's herd and either side has not reached its position (0xC42F80 on +0xC0), redo
  the native staging step, at most twice, four seconds apart.
* A tick (`mate_hearts_tick`): for a living, eligible nestmate of the local herd without effect 0x9005E011
  (checked through the creature effect getter), start the native pooled effect once per continuous
  eligibility window (their 40-dev fix for stacked heart clouds).
* The native approach, egg cinematic, editor request and our vote keep handling the rest.

## How to test
First reproduce: lab, one instance, a Creature save with an eligible mate; `coop.pool mate`-style setup that moves
the mate to 10.9 to 11.3 m from the nest, then the Mating Call and a real click on the mate. FAIL before: no
phase 2, no egg cinematic within 30 s. PASS after: one egg cinematic, the Creature editor opens, zero faults.
Control: the same with the mod unloaded, to decide whether it is vanilla behaviour (then a fix is optional).

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (fork evidence only).
