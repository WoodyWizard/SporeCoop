---
id: BUG-0799
title: Cell: a knocked-out cell's poison clouds still hurt and kill NPCs and earn kills and meat
status: fixed
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-cell
commits: 3e554da8, a1ae1173, 08ed06f5
tests: cell_ko_poison, cell_ko_poison_guest
signature:
docs: docs/re/cell.md#cell-c1-0713, docs/re/cell.md#cell-electric-attack
related: BUG-0509 BUG-0746 BUG-0716
---

## Report
No player report. While a player with a poison part is knocked out (10 s), its clouds keep damaging and killing NPCs and the kills count as its own.

## Root cause
Observed (lab, `cell.ko_no_poison` off): the host knocked out with a grafted poison part: its game made 18 clouds of
its cell in the 8 s (20261112-065846); a knocked-out guest's puppet in the host's game made 21 (20261112-070740). The
game's poison step (0xE68630 -> Cell_PoisonCloud 0xE76660) asks only for a live cell and a knocked-out cell is held
at hp 1. On the host the guest's clouds come from puppet_poison (BUG-0716) AND from the game's own poison step, which
also runs for a partner's puppet drawn with a poison part (seen once the grafted model data was shared by both
cells: puppet_poison skipped, the puppet still made clouds, 20261112-064328). Damage by the clouds was not measured
(the graft makes clouds without the game's poison damage, docs/re/cell.md#cell-electric-attack); the audit's DamageCell
path is not changed (clouds made before the knockout still act for their 3-7 s).

## Fix
cellsync.cpp `cell.ko_no_poison`: h_Cloud (the cloud maker) refuses a cloud of our knocked-out avatar and of a
knocked-out partner's puppet (by its slot, puppet_cell + g_ko), and puppet_poison skips a knocked-out partner.
Switch `coop.fix cell.ko_no_poison off` (0.7.12).

## Fix idea
Skip puppet_poison for knocked-out puppets and refuse own_product(attacker) damage in h_DamageCell while g_downed (extend cell.ko_no_bite).

## How to test
Host `coop.pool borrow poison graft`, knock the host out (`coop.pool selfhit`), put an NPC on it (`coop.pool tp`); its hp (`coop.pool cell <idx>`) drops on main, stays with the fix. Guest variant: `puppet_poison_steps=` must not grow while that guest is knocked out.

## Verification
`coop_cell_ko_poison.py --who host|guest` (suites `cell_ko_poison`, `cell_ko_poison_guest`, ~1 min each): the cell
gets the poison part, a control that the host's game makes its clouds, then the knockout: no cloud of that cell
(the game's cloud maker calls by that maker, `coop.pool borrow status`) and (host) no new poison cell within 3 m of it
in the host's game for 8 s.
* FAIL with `SPORE_FIX_OFF=cell.ko_no_poison`: lab 20261112-065846 (host: 38 -> 56 clouds), 20261112-070740 (guest:
  33 -> 54).
* PASS: host 20261112-062620, 20261112-063438; guest 20261112-070632.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-cell): fixed (cell.ko_no_poison), FAIL and PASS runs in Verification.
