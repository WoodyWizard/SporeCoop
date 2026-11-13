---
id: BUG-1008
title: Cell: a guest's mouth chunk that kills a host NPC is credited to the host's own cell (and a dead cell is not refused)
status: open
kind: desync
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-20
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-cell-chunk-kill-host
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
