---
id: BUG-1565
title: History: per-player starting evolutionary path / philosophy picker for worlds started past Cell
status: fixed
kind: feature
stage: All
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in: 0.8.1
branch: w081/rivals-midgame-history
commits: 791d535b
tests: rivals_midgame_history, rivals_midgame_history_off
signature:
docs: docs/11-rivals.md
related: BUG-1470, BUG-1557
---

## Report
When a host starts a new game directly at Creature, Tribe, Civilization, or Space stage (skipping earlier stages), `history::seed()` copies the host's prior-stage consequence cards to every guest on first sight. Guests have no way to choose their own starting evolutionary path or Space philosophy (unlike solo new-planet setup).

## Root cause
`mods/coop/history_logic.h` and `mods/coop/history.cpp` only seeded a newcomer's record from the host's cards and accepted live Cell/Creature/Tribe/Civ updates; there was no guest-to-host prior-stage path selection request, persistence flag protecting picked cards from `history::update()`, console picker, or F9 UI control. Additionally, `space_card_for()` rejected zero prior cards instead of letting `SpaceArchetype_FromTraits(0, 0, 0, 0)` return Wanderer (`0xA5656E84`).

## Fix
- **Portable logic (`mods/coop/history_logic.h`)**:
  - Added canonical 10-philosophy preset table `kPhilosophies` (Warrior, Trader, Scientist, Shaman, Bard, Zealot, Diplomat, Ecologist, Knight, Wanderer), verified against Spore's native `SpaceArchetype_FromTraits` 4x4x4x4 table (`.rdata 0x013F3E00`), plus stage/card parsers, EN/RU labels, and cyclers.
  - Added `Record::picked_mask` (4-bit mask packed into the upper nibble of the extended v1 43-byte store record, preserving 32-byte legacy and 43-byte Rivals compatibility) and `apply_midgame_pick()`: validates all non-zero card IDs, allows only already-completed/past stages (never lets guests forge future stages), supports explicit all-zero Wanderer selection in Space, and clears `cards[kSpace]` for recomputation.
  - Updated `history::update()` to honor `picked_mask` across ticks and reloads in both Together and Rivals modes while clearing picked bits if the host reloads an earlier save or when live Tribe/Civ outcomes arrive.
- **Wire protocol (`mods/coop/wire/history.h`, `mods/coop/net/messages.def`, `mods/coop/net/messages.lock`, `docs/04-netcode.md`)**:
  - Added backwards-compatible `K_PICK = 3` (`wire::history::Pick`: 4 x u32 = 16 bytes) inside `MSG_PLAYER_HISTORY` (75) and bumped `session` minor `1.4 -> 1.5` (older hosts ignore unknown kind 3).
- **Runtime & console (`mods/coop/history.h`, `mods/coop/history.cpp`)**:
  - Added `COOP_FIX("history.midgame_pick", "BUG-1565")` gating guest pick sending, host `K_PICK` handling (recomputing `kSpace` via `space_card_for()` with `card_ok()` so Wanderer works natively and immediately replying with `K_RECORD`), and `update()` preservation.
  - Added `coop.history pick philosophy <name>`, `coop.history pick <cell|creature|tribe|civ> <card_name>`, and `coop.history presets`.
- **F9 UI (`mods/coop/coop_ui.cpp`)**:
  - Added compact "Starting Path (Mid-Game)" rows on the guest's F9 Rivals page (`refresh_rivals()` only, zero changes to lobby/Together pages so all 12/12 `ui_golden` screenshots remain pixel-identical).

## Verification
- Unit & lint checks: `make -j$(nproc)`, `make unit-cpp` (`history_rivals_tests` with all 10 philosophy presets, anti-forge checks, Wanderer, fresh mid-game start, reload clearing, 43-byte store round-trip, and 16-byte `wire::history::Pick` codec), `make unit-net`, `make unit-lint` (`msg_manifest.py check`, `tickets.py check`, `glyph_lint.py`), and `python3 tools/tests/test_lint.py` all PASSED.
- Wine lab ON/OFF twin (lane 5, `out/lab/20261125-174338`):
  - `rivals_midgame_history` **OK** (42 s, 0 faults): 10/10 native `SpaceArchetype_FromTraits` presets matched, guest picked `zealot` (`cEmpire +0x58` -> 14) and switched to `shaman` (`cEmpire +0x58` -> 12) + single-stage Cell pick without modifying host (`arch=17`), and retained picked philosophy and archetype across rejoin.
  - `rivals_midgame_history_off` (`SPORE_FIX_OFF=history.midgame_pick`) **FAIL** (116 s): pick refused, guest stayed on host's seeded cards and `arch=17`.
- Regression check (lane 5, `out/lab/20261125-174633`): `rivals_history` **OK** (40 s) and `ui_golden` **OK** (86 s, 12/12).
