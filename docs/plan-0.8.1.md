# SporeCoop 0.8.1 Plan - Independent Evolution & Full Rivals Isolation

Target: close every remaining shared/host-coupled evolution, design, ability, and telemetry leak across Rivals mode (and Together where applicable) identified in the post-0.8.0.2 codebase audit.

## Work Packages

| WP | Ticket | Branch | Lane | Scope |
|---|---|---|---|---|
| WP-081-1 | BUG-1562 | `w081/rivals-designs-jump` | lane 2 | Isolate `designs.cpp` (no host Tribe outfit / Civ / Space design overwrite onto rival players) and block Space `Jump to` against Neutral/War rivals in `spacecoop.cpp` |
| WP-081-2 | BUG-1563 | `w081/civ-spec-superweapon` | lane 3 | Guarantee Civ Rivals taken-over NPC nation/city specialty matches the player's own Tribe card even when no matching free NPC exists, and fix guest Civ Rivals superweapon `mWealth` spending and gating |
| WP-081-3 | BUG-1564 | `w081/tribe-consequence-pets` | lane 4 | Route guest Tribe consequence abilities (`Fire Bombs`, `Refreshing Storm`, `Flying Fish`, `Traps`, `Fireworks`) to the host per player's own cards/tribe, and transfer each player's Creature pack species as domesticated pets to their own village on `Creature -> Tribe` in Tribe Rivals |
| WP-081-4 | BUG-1565 | `w081/rivals-midgame-history` | lane 5 | Allow guests joining a new world started past Cell (Creature/Tribe/Civ/Space) to pick their own prior-stage consequence cards / Space philosophy instead of permanently copying the host's history |
| WP-081-5 | BUG-1566 | `w081/rivals-editor-drama` | lane 6 | Isolate `drama.cpp` in Rivals so host-centric drama events do not target rival villages/nests unfairly, and eliminate unnecessary guest world reloads / progress disruption when host enters Creature Editor in Rivals |

## File Ownership (Disjoint)
- **WP-081-1 (`BUG-1562`)**: `mods/coop/designs.cpp`, `mods/coop/spacecoop.cpp`, `mods/coop/space_logic.h`, `tests/space_tests.cpp`, `tools/tests/coop_rivals_designs_jump.py`, `docs/tickets/BUG-1562-*.md`
- **WP-081-2 (`BUG-1563`)**: `mods/coop/civ_rivals.h`, `mods/coop/civ_rivals.cpp`, `mods/coop/civ_rivals_logic.h`, `mods/coop/civ.cpp`, `mods/coop/civ_arrival.cpp`, `tests/civ_rivals_tests.cpp`, `tools/tests/coop_rivals_civ_spec_sw.py`, `docs/tickets/BUG-1563-*.md`
- **WP-081-3 (`BUG-1564`)**: `mods/coop/tribe_rivals.h`, `mods/coop/tribe_rivals.cpp`, `mods/coop/tribe_rivals_logic.h`, `mods/coop/tribe.cpp`, `mods/coop/tribe_host.cpp`, `mods/coop/tribe_logic.h`, `mods/coop/npc.h`, `mods/coop/npc.cpp` (only read-only helper `pack_species_of(pid)`!), `tests/tribe_rivals_tests.cpp`, `tools/tests/coop_rivals_tribe_abilities_pets.py`, `docs/tickets/BUG-1564-*.md`
- **WP-081-4 (`BUG-1565`)**: `mods/coop/history_logic.h`, `mods/coop/history.h`, `mods/coop/history.cpp`, `mods/coop/wire/history.h`, `mods/coop/coop_ui.cpp`, `tests/history_rivals_tests.cpp`, `tools/tests/coop_rivals_midgame_history.py`, `docs/tickets/BUG-1565-*.md`
- **WP-081-5 (`BUG-1566`)**: `mods/coop/drama.cpp`, `mods/coop/world.cpp`, `mods/coop/host_busy.cpp`, `mods/coop/creature_rivals.cpp`, `mods/coop/creature_rivals_logic.h`, `tests/creature_rivals_tests.cpp`, `tools/tests/coop_rivals_editor_drama.py`, `docs/tickets/BUG-1566-*.md`
- `tools/tests/suite.py` (append-only, resolved cleanly on merge to `rel/0.8.1`), `docs/tickets/README.md` (regenerated via `tickets.py index` on `rel/0.8.1` after merges).
