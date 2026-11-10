---
id: BUG-0799
title: Cell: a knocked-out cell's poison clouds still hurt and kill NPCs and earn kills and meat
status: open
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md
related: BUG-0509 BUG-0746 BUG-0716
---

## Report
No player report. While a player with a poison part is knocked out (10 s), its clouds keep damaging and killing NPCs and the kills count as its own.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): the BUG-0509 block in h_DamageCell (cellsync.cpp:857) refuses damage only when attacker == avatar(); poison damage has the cloud as attacker (own_product, +0x110). puppet_poison (cellsync.cpp:704-721) runs the poison step for every partner puppet without guest_knocked_out(id); the puppet looks alive (hp pinned to 6, modes.cpp:1404). Inferred from docs/re/cell.md (electric / poison notes) that cloud damage goes through DamageCell. Confidence: medium-high.

## Fix
Not fixed yet.

## Fix idea
Skip puppet_poison for knocked-out puppets and refuse own_product(attacker) damage in h_DamageCell while g_downed (extend cell.ko_no_bite).

## How to test
Host `coop.pool borrow poison graft`, knock the host out (`coop.pool selfhit`), put an NPC on it (`coop.pool tp`); its hp (`coop.pool cell <idx>`) drops on main, stays with the fix. Guest variant: `puppet_poison_steps=` must not grow while that guest is knocked out.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
