---
id: BUG-0762
title: Session settings: the host's co-op settings reach every guest and stay with the world; Cell shared healing optional
status: fixed
kind: feature
stage: All
reported: 2026-11-08
source: Discord suggestions 2026-10-31..11-04 (M0ng0r: shared healing should be optional), docs/plan-0.7.12.md F3
role: all
versions: 0.7.11
fixed_in: unreleased
branch: feat/w46-settings
commits: 51a4bafd, b876712c
tests: settings, cell_coop, play_cell, dna_pace, dna_pace_cell, team
signature:
docs: docs/07-modes.md#modes-session-settings
related: BUG-0510, docs/plan-0.7.12.md F3
---

## Report
Discord suggestions 2026-10-31..11-04: "Shared healing in cell stage should be optional" (3 reactions), "create unique
switches to every stage". Plan 0.7.12 F3 (approved 2026-11-08): one settings framework, the host's choice sent to every
guest, kept with the host's world, defaults for a new world from coop.ini, each mechanic a setting. This ticket is step 1:
the framework plus two settings (Cell regeneration near a partner, partner arrows).

## Root cause
Not a bug: the co-op mechanics were fixed (the Cell regeneration near a partner always ran, cellsync.cpp symbiosis_tick)
or read from each machine's own coop.ini (dna_pace on the host, npcs_target_partners, teamplay on every machine), so a
guest's game could play with other rules than the host's and nothing was kept with a world.

## Fix
settings_core.h declares every setting once (key, group, label, default, scope session|live, help, the coop.ini key of a
new world's default, off in Rivals); settings.cpp keeps the host's table, stores it per world
(<profile>/Spore/SporeCoop/settings/<crc>.txt), sends it to a joining guest and on every change (MSG_SETTINGS 66, class
Session; older peers ignore it), and `coop.settings` lists / sets them (a guest: refused; a session setting: refused once
the host plays its world). dna_pace, npcs_target_partners and teamplay are read through it now (their ini keys are the
defaults); cell.regen gates the Cell regeneration, all.partner_arrows is declared for F2. Twin switch:
`coop.fix settings.cell_regen off` (the regeneration ignores the setting, as in 0.7.11).

## Verification
coop_settings.py (suite `settings`), lane 4: PASS out/lab/20261108-143823 (regeneration on 2 -> 6 on both games, off
2 -> 2 on both, the session setting refused in play, accepted in the galaxy menu, both values back after both games
restarted, the new guest got them); FAIL with SPORE_FIX_OFF=settings.cell_regen out/lab/20261108-144201 (off: 2 -> 5 on
both games). Unit tests: tests/validate_tests.cpp test_settings.

## History
- 2026-11-08: opened.
- 2026-11-08: framework and the two settings on feat/w46-settings.
