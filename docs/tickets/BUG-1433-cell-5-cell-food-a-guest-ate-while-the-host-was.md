---
id: BUG-1433
title: CELL-5: Cell: food a guest ate while the host was away is dropped when the guest opens its own Cell editor
status: fixed
kind: bug
stage: Cell
reported: 2026-11-22
source: audit
role: guest
versions: 0.7.16.1
fixed_in: 0.7.16.1
branch: fix/w63-life
commits: 75d69f4a, 39f2f060
tests: cell_away_guest_editor (coop_cell_away.py --guest-editor), cell_away_guest_editor_shared; twin cell_away_guest_editor_off
signature:
docs: docs/06-testing.md#testing-w63-life-knobs
related: BUG-0205, BUG-0051
---

## Report
External static audit of 0.7.15.5, item CELL-5 (out/research/ext-audit-verify-uifx-cell.md): the food a guest ate while the host was in an editor, the menu or loading is lost when the guest opens its own Cell editor (its mating) before the host is back. The audit's "billed for DNA from it" part is only that loss of food (the editor spend itself is billed normally).

## Root cause
Inferred from code: the food eaten alone is kept in `g_solo_food` (it is already in the guest's own S+0x1C) until the host is back. The guest's own Cell editor is mode kEditor, `in_cell()` false, and both resets ran: the tick's `if (!cell && (g_solo || g_solo_owed)) solo_reset()` ("the stage was left") and solo_tick's `else solo_reset()`. The food never reached the host, and the host's next total took it back from the guest (Meals::diff).

## Fix
Both resets wait while the mode is the editor (`solo_keep_in_editor`): back in Cell the solo state is decided again (the host back: the food goes out before its first state; still away: kept). The creature editor at the end of the Cell stage is the editor too: the food is dropped when the next stage starts, as before. Switch `coop.fix cell.solo_editor off`: as 0.7.16. No message changed.

## Verification
Open, waiting for the lab run. PASS expected: cell_away_guest_editor and cell_away_guest_editor_shared (the host in the menu, the guest eats 10, opens its own cell editor, the host loads its world again, the guest leaves its editor: the host's total has all the guest ate, the guest's total never dips below it). FAIL expected: cell_away_guest_editor_off (SPORE_FIX_OFF=cell.solo_editor). The food total is the party's in both DNA modes.

- 2026-11-23 (0.7.16.1 final check): cell_away_guest_editor and cell_away_guest_editor_shared PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened (audit item); fixed on fix/w63-life with its test and twin, waiting for the lab run.

- 2026-11-22: renumbered from BUG-9318 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9318.
- 2026-11-23: released in 0.7.16.1.
