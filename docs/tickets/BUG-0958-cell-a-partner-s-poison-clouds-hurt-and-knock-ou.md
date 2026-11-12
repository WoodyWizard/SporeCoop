---
id: BUG-0958
title: Cell: a partner's poison clouds hurt and knock out the other player's cell
status: open
kind: bug
stage: Cell
reported: 2026-11-12
source: fix/w48-cell lab 2026-11-12 (coop_cell_ko_poison.py)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md#cell-c1-0713
related: BUG-0716 BUG-0799 BUG-0899
---

## Report
Seen in the lab while testing BUG-0799: with the host's cell given a poison part (`coop.pool borrow poison graft`,
the game's own poison step makes its clouds), the guest's cell 6 m away (and once ~30 m after it drifted) was knocked
out in the guest's own game without any bite: lab 20261112-070110 (host log s 18.4 "Bob is knocked out", before the
test's own knockout at s 36.7) and 20261112-062128 (s 22.4). Friendly fire is off in Cell (players do not bite each
other), so a partner's poison should not hurt the other player either.

## Root cause
Inferred, not traced: the host's clouds are mirrored to the guest like any cell of the host's pool; the guest's own
game runs the cloud mirror's contact with its own cell (h_DamageCell's `victim == avatar()` path applies damage from
any attacker, `g_hurt_local` counts mirrors). A cloud's +0x110 (player-owned) is not checked there. Not checked
either: whether the guest's own poison clouds, made by the host for its puppet (puppet_poison), hurt the host's cell.

## Fix
Not fixed (not part of C1). Idea: on the guest refuse damage to the own cell from a mirror that is a player's product
(+0x110, or a poison cell near a partner's puppet) while friendly fire is off outside Rivals; the same on the host for
the clouds of partners' puppets. Rivals 0.8 needs friendly fire in Cell (docs/plan-0.7.13.md, owner 2026-11-11).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## How to test
`coop_cell_ko_poison.py --who guest` without `coop.pool invulnfor` on the guest: the guest 6 m from the host whose
cell has the graft; within 20 s the guest's own game knocks it out (downed=1, its hp from 6). Expected: its hp stays.

## History
- 2026-11-12: opened.
- 2026-11-12 (fix/w48-cell): opened from the BUG-0799 runs.
