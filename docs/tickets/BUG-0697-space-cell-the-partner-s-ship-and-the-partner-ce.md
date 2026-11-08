---
id: BUG-0697
title: Space / Cell: the partner's ship and the partner cell are drawn at full health on the other game (the sent health is not applied)
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0419 (0.7.11)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b
commits: f1d45292
tests: space_partner_hp
signature:
docs: docs/re/space.md#space-partner-ship-health
related: BUG-0419
---

## Report
From the K2a audit of BUG-0419. Space: space_fill sends the ship's health and max health, space_drive writes the puppet's own max into its health every frame (s.health never read): the partner's ship always shows full health. Cell: cell_fill sends the cell's health (+0x244), the puppet setup writes 6 and invulnerable: the partner cell never shows damage.

## Root cause
Observed (lab): space_drive wrote the puppet's max into its health every frame (1.00 on the other game for a 0.60 ship). Cell part: not changed on purpose: no UI shows another cell's health (the health ring only for the avatar's own bites, docs/re/cell.md#cell-guest-bite-ring) and the puppet stays invulnerable (+0x111) with 6 hp so that this game never kills it.

## Fix
`drive.space_hp`: the partner's share of its max on the puppet's max, at least 2 % (modes.cpp space_puppet_health). Test readout `coop.puppet hp`.

## Verification
* FAIL with `SPORE_FIX_OFF=drive.space_hp`: out/lab/20261107-222651.
* PASS: out/lab/20261107-222455, out/lab/20261107-225634.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
