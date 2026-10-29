---
id: BUG-0237
title: entering the cell editor within 2-3 s of a revive or respawn can leave the cell invulnerable afterwards
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #171 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-cell
commits: see History
tests: cell_editor_invuln
signature:
docs: docs/re/cell.md#cell-rebuilds-in-session
related: #171, BUG-0070, BUG-0221
---

## Report
From GitHub issue #171: https://github.com/WoodyWizard/SporeCoop/issues/171 (the full analysis, links and the scenario are there).

BUG-0070 (#48, commit e7ab004, release 0.7.3.0) resets our knockout state when the Cell stage is left. When the stage is left through the cell editor, there is no avatar at that moment. The reset clears the timer that would end the 2 s (revive) or 3 s (respawn) invulnerability, but it cannot clear the avatar's `+0x111`. If the same avatar object is back after the editor, nothing ever clears the flag, and the player's cell takes no damage for the rest of the stage. Before 0.7.3.0 the timer survived the editor and cleared the flag on return. Severity LOW, PLAUSIBLE: it depends on the avatar surviving the editor, and the docs suggest the level is rebuilt after it.

## Root cause
Not reproducible, as the docs predicted. Measured on lane 2 (GOG 2024): guest `coop.pool invulnfor 120` (+0x111 = 1 and the timer), its own cell editor through the mating call (real clicks), cancel. On the first read back in the pool the avatar is a new object with a new index (7F010000 -> 808C0126, another pointer) and +0x111 = 0; 4 s later a host bite took 6 -> 5 hp. The editor's return rebuilds the pool and recreates the avatar, so the timer `reset_knockout` drops has no flag left to clear.

## Fix
None needed (step 2 of the issue was conditional). Added the test-only command `coop.pool invulnfor <s>` and the guard test.

## Verification
`tools/tests/coop_cell_editor_invuln.py` (suite `cell_editor_invuln`): PASS on lane 2 (main code). No FAIL run exists: it is the regression guard the issue asked for.

## History
- 2026-10-28: opened.
- 2026-10-28: fix/w5-cell: the avatar comes back from the cell editor recreated with +0x111 clear; closed as not reproduced, guard test cell_editor_invuln.
