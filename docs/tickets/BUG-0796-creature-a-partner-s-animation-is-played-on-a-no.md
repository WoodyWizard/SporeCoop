---
id: BUG-0796
title: Creature: a partner's animation is played on a non-creature puppet (a Cell cell or a Tribe marker) around a stage switch
status: fixed
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-freed
commits: 570ee491
tests: anim_not_creature, anim_not_creature_tribe
signature:
docs: docs/06-testing.md#testing-freed-block-reuse
related: BUG-0091 BUG-0426
---

## Report
No player report. A partner's reliable MSG_ANIM that arrives while this game shows that partner as a cell (Cell) or a pointer marker (Tribe) can crash this game.

## Root cause
Observed at runtime (fix/w48-freed, 2026-11-12): a partner's MSG_ANIM handled while the guest shows that partner as a Cell cell or a Tribe marker (`coop.overlay animrx 0 <anim> 5`) went to cCreatureBase::PlayAnimation on that object 5 of 5 times (lab 20261112-011637 Cell, 20261112-011730 Tribe). No fault was seen in those runs (what the game read at +0xB54 of a cell or a marker did not fault there); the call itself is the wrong one on the wrong object.

## Fix
mods/coop/coop.cpp on_remote_anim: an animation is played only on a Creature puppet (creature_puppet), any other is dropped and counted (`coop.framefix`: not_creature=). Switch: `coop.fix core.anim_creature_only off`. Test hook: `coop.overlay animrx <player> <anim> [n]`.

## Fix idea
Return in on_remote_anim unless creature_puppet(it->second.puppet_adapter) (and the puppet is listed).

## How to test
Test hook: `coop.anim inject <player> <id>` on the guest while it shows the host as a cell (both in Cell). Main: PlayAnimation on the cell (fault or a logged probe); with the fix: dropped and counted.

## Verification
Tests `anim_not_creature` (Cell) and `anim_not_creature_tribe` (Tribe), tools/tests/coop_anim_not_creature.py.
* FAIL (SPORE_FIX_OFF=core.anim_creature_only): out/lab/20261112-011637 (Cell: played=5), out/lab/20261112-011730 (Tribe: played=5).
* PASS: out/lab/20261112-012019 (Cell and Tribe), out/lab/20261112-013003 (Cell).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12: fixed on fix/w48-freed (core.anim_creature_only).
