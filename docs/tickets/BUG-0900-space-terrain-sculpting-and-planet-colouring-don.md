---
id: BUG-0900
title: Space: terrain sculpting and planet colouring done with no partner on the planet never reach the partners
status: open
kind: desync
stage: Space
reported: 2026-11-10
source: fix/w47-b2-space (BUG-0763, 2026-11-10)
role: both
versions: 0.7.12 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/space.md#space-sculpt-colour
related: BUG-0763, BUG-0883
---

## Report
Found while fixing BUG-0763 (not run). The sculpting / colouring relay (spacecoop.cpp K_SCULPT / K_COLOUR) reaches only
partners whose ship is on the same planet at the moment of the hit. A player who sculpts or recolours a planet alone,
or a partner who lands later or rejoins, sees the old terrain and colours for good (the same class as BUG-0883 for the
atmosphere / temperature).

Reproduce: space_free, the guest lands on 01460035, the host stays in the system; the guest uses the first sculpting
tool (tab W-226, slot (1108, 966)) and the first colour (tab W-186, slot (1121, 966)) on the ground
(coop_space_sculpt_tool.py's clicks); the host lands: coop.ground at the guest's `coop.space sculpt` `last=` spot and
`coop.space sculpt` `colours` differ.

## Root cause
* Observed (code): apply_sculpt / apply_colour return when the receiver is not on that planet.
* Inferred: the colours live in the terrain's prop list (sphere +0x28, props 0x3A23F9A / 0x536250C / 0x536250D) and the
  sculpts in the sphere's terrain effect list +0x798 (docs/re/space.md#space-sculpt-colour); where the game keeps them
  for a planet that is not loaded (the planet record or a planet file) is not known. A fix needs that first: then the
  host keeps a per-planet list of the hits (effect id + transform, last colour triple) and sends it to a partner who
  lands (or applies it to the planet's stored data).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened.
