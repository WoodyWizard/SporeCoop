---
id: BUG-1557
title: Rivals per-player evolutionary path, Tribe/Civ consequence cards and Space philosophy
status: open
kind: feature
stage: All
reported: 2026-11-25
source: Discord (@Хлебушек)
role: both
versions: 0.8.0.1
fixed_in:
branch: w0802/rivals-history
commits:
tests: rivals_history, rivals_history_off, history_rivals_tests
signature:
docs: docs/11-rivals.md
related: BUG-1470, BUG-1471, BUG-1548, BUG-1549, BUG-1550
---

## Report
In competitive (Rivals) mode, players share the host's Tribe and Civ consequence cards (and therefore end up with the same or nearly identical evolutionary path / Space philosophy) instead of earning independent Tribe/Civ cards and distinct Space philosophies from their own actions.

## Root cause
`mods/coop/history.cpp:306-307` computes per-player consequence cards for `kCell` (`x.cell_own`), `kCreature` (`creature_card_for`), and `kSpace` (`space_card_for`), but returns `0` for `kTribe` and `kCiv` (from 0.7.17 Together mode where all players shared one tribe and one nation). Returning `0` tells `history::update()` to copy the host's Tribe and Civ cards into every guest's history record. Furthermore, `tribe_rivals`, `civ_rivals`, and `creature_rivals` need to feed each player's own Tribe/Civ/Creature outcomes into `history`, and `civ_rivals` should prefer matching an NPC nation specialty to the player's own Tribe card on Civ entry.

## Fix
- Extend `history_logic.h`, `history.h`, and `history.cpp` (behind `COOP_FIX("rivals.own_history", "BUG-1557")`) to track per-player Tribe outcomes (`allied` vs `conquered`) and Civ outcomes (`military` / `economic` / `religious` specialty and city actions) with backwards-compatible store persistence.
- Compute each player's own `kTribe` and `kCiv` cards when Tribe/Civ Rivals is active (or per-player outcomes exist), feeding into `space_card_for(x.cards)` and `SpaceGame_SetArchetype` so each player earns their own Space philosophy/archetype and archetype tool.
- Wire `creature_rivals`, `tribe_rivals`, and `civ_rivals` to credit per-player outcomes to `history`, and prefer matching NPC nation specialty to each guest's Tribe card on Civ takeover.
- Ensure Space Rivals emigration coordinates cleanly with `history`'s `SpaceGame_SetArchetype` application so emigrated guest empires receive their own archetype index (`cEmpire +0x58`).

## Verification
Pending implementation and Wine lab verification (`rivals_history` PASS + `rivals_history_off` FAIL).
