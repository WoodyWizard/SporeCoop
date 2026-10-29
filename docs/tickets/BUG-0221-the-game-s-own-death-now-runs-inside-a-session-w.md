---
id: BUG-0221
title: the game's own death now runs inside a session when the only partner is knocked out, and no test watches the respawn that follows
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #155 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-cell
commits: b57410f
tests: cell_respawn, cell_respawn_swap
signature:
docs: docs/re/cell.md#cell-rebuilds-in-session
related: #155, BUG-0131, BUG-0237, BUG-0238
---

## Report
From GitHub issue #155: https://github.com/WoodyWizard/SporeCoop/issues/155 (the full analysis, links and the scenario are there).

Since BUG-0131 (#110, commits 8f749d0, 153ec67, release 0.7.3.0), a knocked-out partner no longer counts as a helper. With two players, a lethal hit on the second one therefore runs the game's own death and respawn whenever the first is still knocked out. This is the intended behaviour of the ticket ("if nobody can help, the normal death happens"). The level rebuild that follows a death is documented, and an in-session rebuild with partners in the pool already works and is tested: the mod triggers one itself when it applies a player's own cell. What is new and unverified is the death-specific part (the death UI, the death count, the hatch intro) with a knocked-out partner in the pool, and the BUG-0131 test stops 1 s after the death. Severity LOW: a test gap plus a short runtime check.

## Root cause
No problem found. Step 1 of the issue (the runtime measurement), lane 2, GOG 2024, both roles: the game's death with the only partner knocked out rebuilds the dying player's pool; the generation keeps counting (no restart), the rebuild's DestroyCell calls are not refused (`puppet_saves=0`, `kept=0`, `coop.pool why` empty), the guest recreates every mirror and each maps to a live host cell, each game shows the other player's cell at its real place, the death caption is gone (`cine=0`), the knocked-out partner respawns after 10 s, no faults. G+0x51D8 is set and cleared within one frame (never seen by a 50 ms console poll). Details: docs/re/cell.md#cell-rebuilds-in-session.

## Fix
No product change (step 2 of the issue was conditional on step 1 finding a problem). Added the test-only print `coop.cell pool` (pool header, avatar index, G+0x51D8, the avatar's pointer, +0x111, +0xFC) and the guard test `coop_cell_respawn.py [--swap]` that watches the respawn the BUG-0131 test stopped before.

## Verification
`tools/tests/coop_cell_respawn.py` (suite `cell_respawn`) and `--swap` (suite `cell_respawn_swap`): PASS on lane 2 (host's pool rebuilt; guest's pool rebuilt). It has no FAIL run: nothing was broken, so this is a guard, not a fix test. Not checked: the death count S+0x70 and cCellUI+0x936 directly (the caption check `cine=0` covers the UI the player sees), 3 players, a lossy link.

## History
- 2026-10-28: opened.
- 2026-10-28: fix/w5-cell: measured (both roles), no problem; guard tests cell_respawn / cell_respawn_swap and `coop.cell pool` added.
