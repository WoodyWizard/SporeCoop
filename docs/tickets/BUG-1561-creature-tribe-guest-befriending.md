---
id: BUG-1561
title: Creature & Tribe: guest befriending creatures and tribes fails in co-op
status: open
kind: bug
stage: Creature
reported: 2026-11-25
source: Discord (#bug-reports, Smile / Szmajol)
role: both
versions: 0.8.0.1
fixed_in:
branch: w0802/guest-befriend
commits:
tests:
signature:
docs: docs/re/creature.md, docs/re/tribe.md
related: BUG-0748, BUG-1256
---

## Report
Discord (`Smile / Szmajol`): Player 2 (guest) reports that befriending creatures in Creature stage (including host's own species) and befriending villages in Tribal stage (even when host gives all tribe members to Player 2) does not work properly in co-op.

## Root cause
Pending deep audit of `mods/coop/social.cpp` (Creature guest social/befriending) and `mods/coop/tribe.cpp` / `mods/coop/tribe_host.cpp` (Tribe guest social performance / gifting / mini-game controls).

## Fix
Pending audit and implementation.

## Verification
Pending Wine lab verification.
