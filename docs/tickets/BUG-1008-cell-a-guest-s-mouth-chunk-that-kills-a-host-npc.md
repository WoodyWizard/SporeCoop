---
id: BUG-1008
title: Cell: a guest's mouth chunk that kills a host NPC is credited to the host's own cell (and a dead cell is not refused)
status: fixed
kind: desync
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-20
role: host
versions: 
fixed_in: unreleased
branch: fix/w49-cell
commits: 23b91609, bb9e3b4a
tests: cell_chunk_killer
signature:
docs: docs/known-issues.md#issue-cell-chunk-kill-host, docs/re/cell.md#cell-loot-alert, docs/06-testing.md#testing-cell-c1-0714
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-20. Not seen at runtime, found by code reading (main efd2e0ba). When a
guest's mouth tears the last chunk off a host NPC (EatChunk, MSG_CELL_HIT type 6), the host kills the NPC with
`h_KillCell(c, avatar_idx(), avatar's res, ...)`: the kill is the host's own cell's. KillCell then alerts the
victim's species against the HOST's cell (not the guest's puppet) and counts the kill for the host. The same
fallback is used for other hit types when the BUG-0714 path (`guest_attack`) returns false. Also the message is
refused only for a dying cell (`kDying`), not a dead one (`kDead`): a chunk hit on a dead cell with hp 0 goes to
KillCell again (inferred: a second death and loot).

## Root cause
Observed in code (mods/coop/cellsync.cpp on main): 4445 the gate checks `kDying` only; 4456 `pc = puppet_cell(from)`;
4457 `t != 6 && guest_attack(...)` (type 6 skips the game's path by design: "EatChunk itself writes the hp", 4463);
4486-4494 `hp -= dmg`, then `h_KillCell(c, avatar_idx(), F<void*>(avatar(), kRes), true, t, 0.f)`: killer = the
host's avatar even when `pc` (the guest's puppet) is known; `avatar()` is not null-checked there (in Cell the host
has an avatar, inferred safe). KillCell's alert of the same species (docs/re/cell.md KillCell row, 0xE71F00) uses
the killer. Inferred: the kDead case (whether a cell can be dead, not dying, and still get a chunk hit). Severity:
low to medium (a guest's carnivore chunk kills make the host's cell the target of that species).

## Fix idea
Use `pc ? F<uint32_t>(pc, 0) : avatar_idx()` and `pc ? F<void*>(pc, kRes) : ...` as the MSG_CELL_EAT path does
(4572, `h_KillCell(c, pc ? F<uint32_t>(pc, 0) : avatar_idx(), ...)`), null-check `avatar()`, and refuse `kDead` in the gate. Switch:
`cell.chunk_killer`.

## How to test
tools/tests/coop_cell_* (cell fixture): the guest with a jaw part bites chunks off a host NPC to its death (a test
command sending MSG_CELL_HIT type 6 with the NPC's remaining hp); assert on the host's game the alerted species' AI
target (the game's own target field of a same-species neighbour) is the guest's puppet. Main: the host's cell (FAIL),
fixed: the puppet (PASS).

## Fix
cellsync.cpp MSG_CELL_HIT on the host: a lethal hit calls KillCell with the guest's puppet as the killer (its index
and resource, as the MSG_CELL_EAT kill does; the host's avatar only when the puppet is not known, null-checked), and a
cell with its dead flag (+0x112) refuses the hit like a dying one. Switch `coop.fix cell.chunk_killer off`.
RE: KillCell's alert 0xE71F00 sets flee target +0x220 = killer on the cells of the victim's resource within a radius,
only when the AI block's +0x2C is not 0 (docs/re/cell.md#cell-loot-alert). Test commands `coop.pool sendhit`,
`herd`, `targets`, `npcflag c<key> alert <r>`.

## Verification
Test `cell_chunk_killer` (a type 6 hit of 6 on a host NPC with hp 2 and 4 of its kind round it, with an alert radius;
the flee / chase targets in the host's game).
FAIL (SPORE_FIX_OFF=cell.chunk_killer): out/lab/20261113-023438 (4 cells of the kind flee the host's cell, none the puppet).
PASS: out/lab/20261113-023139, out/lab/20261113-023438-2.
The first runs (20261113-022529-2, 20261113-022622) were SKIP: the fixture's kinds have no death alert (+0x2C 0).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-cell (package C1).
