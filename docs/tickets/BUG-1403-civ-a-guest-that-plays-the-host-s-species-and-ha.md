---
id: BUG-1403
title: Civ: a guest that plays the host's species and has own creatures cannot pick a civilization role (the creature row holds the arrows)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-21
source: lab 20261121-003202 play_civ (found on the way)
role: guest
versions: rel/0.7.16 21025f40
fixed_in: 0.7.16
branch: fix/w62-civrole
commits: 843ec8e1, d5426c83
tests: civ_role_row, civ_role_row_off, late_species_civ, play_civ
signature:
docs: docs/08-ui.md#ui-civ-two-rows
related: BUG-1289, BUG-1402
---

## Report
Found on the way (BUG-1402, lab 20261121-003202 play_civ): in Civ, a guest that plays the host's species and has own
creatures in My Creations sees "My creature: the host's species (in My Creations: 5) < >" in the co-op window's row,
where 0.7.15 showed "My role: none - pick one < >". The arrows pick a creature (BUG-1289, by design), so this guest cannot
pick a civilization role (military / economic / religious) unless it first picks a creature: an own one, or the host's
species again with the arrows, and then waits 30 s (creations::late_row keeps the row 30 s after a click; late_pick is
false only after an explicit choice). A player who wants to keep the host's species and take a role has no visible way.

## Root cause
Observed in the code: coop_ui.cpp refresh_lobby: `roles` needs `giving`, and `giving` needs an empty creature row
(`cl.empty()`); a.creation (coop.cpp) returns the row in Civ while creations::late_row is true. Not a test or flake
cause by itself; play_civ's guest role actions do not run while it holds (BUG-1402). New in 0.7.16 (c29e3ba4).

## Fix
The owner decided: two rows. fix/w62-civrole, switch `core.ui_civ_two_rows` (on): in Civ, while the creature row shows
for a guest with own creatures and nothing is selected, refresh_lobby moves it one line up (new label `creationup` with
its own arrows `cupprev` / `cupnext`, which always pick a creature) and the role row comes back under it in its old
place ("My role: ... < >", `creation` with cprev / cnext, which pick the role). The chat shows 3 lines meanwhile.
Everything else is unchanged: F5 "everybody plays the host's creature" (cown = 0: no creature row), a guest without own
creatures, the host's window, Tribe, Space, a selection (the give row). Off: the creature row alone (rel/0.7.16 before
the fix). The ui_golden references are all Space and menu windows: none changes. docs/08-ui.md#ui-civ-two-rows.
Tests: coop_civ_role_row.py (new) and its twin; coop_late_species.py --civ now clicks the upper row's ">" when it shows
(the creature row moved; the check is the same). play_civ's guest role actions (BUG-1402) find "My role" in the row
again and pick roles.

## Verification
Was waiting for the lab run (no lab runs in this round). make -j8, make test clean. Lab:
`SPORE_DETECTOR=20 python3 tools/lab.py run civ_role_row late_species_civ --lane N --retry 0 --no-build`: PASS;
`... run civ_role_row_off late_species_civ_off --lane N --retry 0 --no-build`: FAIL (civ_role_row_off: no role row under
the creature row, step 1). Frames: civ_two_rows / civ_two_rows_picked (`tools/frames_review.py <run> --test civ_role_row`):
both rows whole, no overlap, the chat above with 3 lines.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): civ_role_row and late_species_civ PASS in 20261122-193225, 20261122-194210, 20261123-023643; the twins civ_role_row_off and late_species_civ_off FAIL in 20261122-191140. Closed.

## History
- 2026-11-21: opened.
- 2026-11-21: fix in fix/w62-civrole, open until the lab run: two rows, switch core.ui_civ_two_rows, test civ_role_row + twin.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
