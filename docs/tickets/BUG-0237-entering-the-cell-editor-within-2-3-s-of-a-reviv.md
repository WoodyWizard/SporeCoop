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
commits: b57410f
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
* Lab: PASS out/lab/20261028-231910 (cell_editor_invuln).
* no-fail-run: the problem does not occur (the avatar is recreated by the editor's rebuild).
* Timed re-check 2026-10-30 (fix/w9-cell-away, lane 3): `coop_cell_editor_timed.py` (suites cell_editor_timed, cell_editor_timed_revive), out/lab/20261030-002235-fix-w9-cell-away: the stage is left ~10 s after the mate is touched (9.86 s respawn, 10.02 s revive), so the 3 s / 2 s timer has always run out first; the flag is clear after the editor and a bite hurts. Both runs ended as SKIP under the first version of the test (the window not reached); the test now passes as a guard and prints the time.

## History
- 2026-10-28: opened.
- 2026-10-28: fix/w5-cell: the avatar comes back from the cell editor recreated with +0x111 clear; closed as not reproduced, guard test cell_editor_invuln.
- 2026-10-30 (fix/w9-cell-away, GitHub #171 re-check): timed test with a real respawn and a real revive (`coop_cell_editor_timed.py [--revive]`): the cell put onto its mate the moment the knockout ended; the Cell stage was left 9.86 s (respawn) and 10.02 s (revive) later (the mating cinematic), +0x111 already cleared by our timer before (av_invuln=0 on the last read in Cell), and the avatar came back recreated (new index and pointer) with +0x111 = 0; a host bite then took 6 -> 5 hp. The 2-3 s window cannot be reached through the mate. Still not reproduced.
- 2026-10-30 (fix/w13-cell, re-check): cell_editor_timed and cell_editor_timed_revive PASS on lane 3 (out/lab/20261030-133815):
  the stage was left 10.51 s (respawn) and 10.41 s (revive) after the knockout ended, outside the
  3 s / 2 s window again; a host bite took 6 -> 5 hp after the editor. The mating cinematic is the
  only way into the cell editor, so the window stays unreachable. Still not reproduced.
