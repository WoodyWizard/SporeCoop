---
id: BUG-0958
title: Cell: a partner's poison clouds hurt and knock out the other player's cell
status: fixed
kind: bug
stage: Cell
reported: 2026-11-12
source: fix/w48-cell lab 2026-11-12 (coop_cell_ko_poison.py)
role: guest
versions: 
fixed_in: 0.7.13
branch: fix/w48-pass
commits: a52c61de, febc5c8e
tests: cell_partner_poison, cell_partner_poison_host, cell_partner_poison_ff
signature:
docs: docs/re/cell.md#cell-c1-0713, docs/re/cell.md#cell-partner-poison
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

Confirmed (lab, fix/w48-pass, 20261112-115819, switch off): the guest's own game calls DamageCell(victim = its
avatar, attacker = the mirror of a host's poison cloud, type 3) when it touches such a mirror: 18 hits, hp 4 -> 1,
knocked out, in 11 s on 10 clouds. The other way (a guest's clouds, made by the host's game for its puppet with the
avatar index lent, +0x110 set) never hurt the host's avatar: no DamageCell of the host's cell at all on 10 such clouds
(20261112-120150, switch off): the game itself spares its avatar from clouds that carry +0x110.

## Fix
cellsync.cpp `cell.partner_poison`: the host sends +0x110 (a player's product) as flag 4 of each cell in
MSG_CELL_STATE (older guests ignore the bit); a guest keeps it on the mirror and refuses DamageCell of its own cell by
a poison mirror with it, the host refuses DamageCell of its own cell by a cloud its game made for a partner's puppet
(noted in h_Cloud: the cells the cloud maker added), both only while friendly fire is off and outside Rivals
(`all.friendly_fire` on or `coop.find rivals 1`: as 0.7.12, for Rivals 0.8). `coop.pool` line `cell_partner_poison:`
(refused / taken, the last hit our cell took). Switch `coop.fix cell.partner_poison off` (0.7.12).

## Verification
`coop_cell_partner_poison.py` (suite `cell_partner_poison`, cell fixture, ~1 min): the host's cell gets the poison
part, the guest is put onto 10 of the host's clouds in its own game; its hp (the game's +0x244) must not drop and it
must not be knocked out.
* FAIL with `SPORE_FIX_OFF=cell.partner_poison`: lab 20261112-115819 (hp 4 -> 1, knocked out, 18 cloud hits taken).
* PASS: lab 20261112-115931 (hp 6 -> 6, 37 hits refused), with cell_partner_poison_host.
* `--ff` (friendly fire on): the old behaviour, lab 20261112-120253 (hp 6 -> 1, knocked out).
* `--who host` (extended): the guest's clouds on the host's cell; no-fail-run: the game never hurt the host's avatar
  with them, also with the switch off (20261112-120150), a regression check only.

## How to test
`coop_cell_partner_poison.py` (see Verification).

## History
- 2026-11-12: opened.
- 2026-11-12 (fix/w48-cell): opened from the BUG-0799 runs.
- 2026-11-12 (fix/w48-pass): fixed (cell.partner_poison), FAIL and PASS runs in Verification.
- 2026-11-12: released in 0.7.13.
