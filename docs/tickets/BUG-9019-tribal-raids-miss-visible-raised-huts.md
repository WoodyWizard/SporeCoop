---
id: BUG-9019
title: Tribal raid picker misses visible huts above the home village sphere
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo native raid acceptance
role: guest
versions: unreleased after duo9
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_raid.py
switch: tribe.target_ray
signature: visible enemy hut yields no command when its camera ray misses the home-height sphere
docs: docs/re/tribe.md
related: BUG-9018
---

## Evidence

On DuoQAM2RaidFinal4/5, m2-raid-final.log passed friendly-hut rejection
but the enemy click sent no command. The native registration dialogs had
been dismissed and native camera movement worked. Native camera origin
(106.18, 533.30, 56.83), ray at pixel (444, 201) (-0.868, -0.228, 0.441),
and enemy hut (-46.0, 493.3, 134.1) identify the mismatch: the ray passes
above the 507.3-radius home sphere. A visible raised target must not require
an intersection with that lower sphere. The earlier modal-blocked attempt
is not used as evidence for this geometric cause.

## Verification

PASS build/local/m2-raid-acceptance.log on DuoQAM2RaidAcceptance4/5. The
same-session control sent no orders with the old sphere intersection, then
the corrected picker delivered three native orders. The fix takes the
camera ray without requiring a sphere hit, checks projected proximity, and
excludes the far side using the target's radius with the same terrain margin
as the existing native RTS camera test driver. Host type, faction, life and
ownership validation remains authoritative.

The first corrected-picker run exposed a separate native AI ownership
failure (BUG-9020). Both fixes together pass native hut damage and remote
health agreement; this ticket does not mistake delivered orders for combat.
