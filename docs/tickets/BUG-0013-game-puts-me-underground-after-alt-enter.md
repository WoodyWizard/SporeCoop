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

* Re-check 2026-11-08 (fix/w46-n2-desyncs, N2 of 0.7.11): alt_enter OK out/lab/20261108-030127 and out/lab/20261108-032648 (SPORE_NET=players, guest at
  20 fps): after Alt+Enter x2 and Alt+Shift+Enter the host's avatar walks with its lowest height over the ground 0.00 m.
  Why it cannot be decided here: the lanes run Wine/DXVK, where the device Reset rebuilds the terrain from the same
  data, and the game puts a creature below the ground back onto it. The player's case (Windows, AMD) is inferred to be
  the terrain drawn higher than its collision after the Reset; no player log with Reset lines came. A mod fault is
  ruled out on this path: the mod draws no terrain and the avatar's height is the game's own.
* no-fail-run: not reproducible under Wine/DXVK.

## History
- 2026-10-25: reported (player report (Discord)).
- 2026-10-25: not reproduced, see Root cause.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-30: fix/w13-creature: alt_enter OK in out/lab/20261030-141545 (fix/w13-creature, lane 5, GOG 2024) (Wine/DXVK, on the ground after the toggles). Not reproduced; still needs the player's log / a Windows AMD machine.
- 2026-11-08 (fix/w46-n2-desyncs, N2): alt_enter OK twice more (plain, and over the players' network at 20 fps); closed with that evidence.
