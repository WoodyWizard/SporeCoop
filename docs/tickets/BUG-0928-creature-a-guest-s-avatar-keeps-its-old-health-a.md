---
id: BUG-0928
title: Creature: a guest's avatar keeps its old health after its creature is swapped (16 hp on a body with 10 at most)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257 (BUG-0920's side note)
role: guest
versions: 0.7.3 and later
fixed_in: unreleased
branch: fix/w47-one-creature-black
commits: a9206649
tests: one_creature
signature:
docs: docs/re/creature.md#creature-swap-health
related: BUG-0920, BUG-0769
---

## Report
Side note of BUG-0920 (final check out/lab/20261111-044257, one_creature, guest log and sheet 027): the guest's avatar
had `hp=16/10` after the switch from its own creature (16 hp at most) to the host's (10 at most), for the 40 s to the
end of the test; the HUD's bar showed "16" with the green regeneration crosses over the body. The other way a full
creature of 10/10 became 10/16 (a full creature looks wounded after the player picks another creature).
Not only creature.mode host: every swap of the own avatar's creature in the Creature stage (the creation arrows, the
own creature put back after a load).

## Root cause
Observed: `cCreatureBase::SetSpecies` (creations.cpp apply_own_avatar) changes the health maximum at once and leaves the
health number (lab 20261111-064711: 16.0 of 10.0 after the swap, fix off). The game never clamps it later.

## Fix
`creations.swap_health` (creations.cpp swap_health): the avatar keeps its share of health over the swap
(hp = old share x new maximum, at least 1, at most the maximum; not for a dead avatar). `coop.fix creations.swap_health
off`: the old number stays. Log line "own avatar's health a/b -> c/d over the swap".

## Verification
Test `one_creature`: "the guest's full health is full in its own creature's body" (step 1) and "the guest's health fits
its new body after the swap" (steps 2 and 4), read from `coop.avatar`.
* FAIL: out/lab/20261111-064711 (`SPORE_FIX_OFF=creations.swap_health`, lane 10, warm): "the guest's health fits its
  new body after the swap ((16.0, 10.0))" in steps 2 and 4. (On a cold lane the step 1 check fails instead: 10 of 16.)
* PASS: out/lab/20261111-064714 (x5, lane 9: 10/10 -> 16/16 -> 10/10 in every run).

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-one-creature-black (creations.swap_health).
