---
id: BUG-9126
title: Creature: the heal effect loops forever on both players
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev ("generally after we clear a nest or befriend one")
role: all
versions: all with teamplay regen
fixed_in: 0.7.8-lineages50-dev
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9 + guest 8 near each other, `coop.hp` sampled every ~1 s
signature: coop.hp own hp just under max (30.96..30.99 / 31) and rising every second
docs: -
related: -
switch: creature.regen_hysteresis
---

## Cause
teamplay.cpp's pack regen (a partner within reach) set the health up every second whenever any health was
missing. Hunger drains a few hundredths of a point all the time, so the regen topped it up every second forever
and the game's heal effect played on both players (seen after fights at a nest, where players are hungry).

## Fix
The regen starts below 95% of max health and heals to full, then waits until the next real loss.

## Verification
Before: host hp 30.96 / 30.99 / 30.98 / 30.96 / 31.00 / 30.98 (topped up each second). After: 30.33 / 30.29 /
30.23 / 30.18 (hunger only, no heal).
