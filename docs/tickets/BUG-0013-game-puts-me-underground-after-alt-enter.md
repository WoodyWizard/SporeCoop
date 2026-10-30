---
id: BUG-0013
title: "Game puts me underground after Alt+Enter"
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-10-25
source: player report (Discord)
role: host
versions: 0.7.2.3
fixed_in: 
branch: fix/alt-enter
commits: 
tests: alt_enter
signature: 
docs: docs/known-issues.md, docs/re/graphics.md
related:
---

## Report
After Alt+Enter the host's creature was below the ground.

## Root cause
Not reproduced under Wine/DXVK (height over ground stays 0 after 2-8 toggles; the game puts a creature back on the ground). Likely the terrain drawn higher than its collision after the device Reset on that AMD machine.

## Fix
None.

## Verification
Ask the player for sporekit.log device Reset lines, a screenshot, whether a reload fixes it, whether it happens without SporeCoop.

## History
- 2026-10-25: reported (player report (Discord)).
- 2026-10-25: not reproduced, see Root cause.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-30: fix/w13-creature: alt_enter OK in out/lab/20261030-141545 (fix/w13-creature, lane 5, GOG 2024) (Wine/DXVK, on the ground after the toggles). Not reproduced; still needs the player's log / a Windows AMD machine.
