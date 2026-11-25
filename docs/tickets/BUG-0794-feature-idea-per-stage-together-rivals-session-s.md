---
id: BUG-0794
title: Feature idea: per-stage Together/Rivals session state, relation table and faction diagnostics (Rivals foundation)
status: fixed
kind: feature
stage: All
reported: 2026-11-10
source: fork nvstlex/SporeCoopNVST nvst/rivals-foundation
role: 
versions: 
fixed_in: 0.8.0
branch: w08/rivals-core
commits: 9f231149, 2a5cd27c
tests: rivals_core, rivals_core_off, rivals_tests
signature:
docs: docs/04-netcode.md, docs/11-rivals.md
related: docs/11-rivals.md, docs/plan-0.7.12.md F3, docs/plan-0.8.md
---

## Report
Feature idea, not a bug. The fork nvstlex/SporeCoopNVST (branch nvst/rivals-foundation, 138 commits of 2026-11-03,
+1264 / -43 lines, MIT licence with our copyright line) started a "Rivals foundation". Most of it is a design; the parts
that exist are useful for our docs/11-rivals.md (per-stage Together / Rivals table, 11.2) and for docs/plan-0.7.12.md F3
(session settings).

## Root cause
Not a bug. What exists (not verified in game by us; the fork has no in-game tests):
* `mods/coop/rivals_state.h` (178 lines, works as data, unit test tests/rivals_tests.cpp): a per-stage Together / Rivals
  choice for five stages, a symmetric 4x4 relation table (Neutral / Ally / War), a versioned fixed-size snapshot decoded
  into a temporary and size-checked; it rejects Civilization = Rivals (the fork keeps Civilization co-op only).
  Network message MSG_RIVALS_STATE = 61 (collides with our MSG_EFFECT = 61; 62-65 are taken too).
* coop.cpp (+224) / coop_ui.cpp (+64), partial: console `coop.rivals`, a lobby page "Stage modes" (host edits, guests
  view), the host sends the state on join and leave, guests accept it only from player 0; Space switching reuses our
  space_rivalry with a 5-line `set_enabled(bool)`, only while in Space.
* Tribe (tribe.cpp +88), partial: attack orders (CitizenAction 7) on another player's citizen are allowed only at War,
  for guest commands and the host's own selection, with a notice when blocked. Their own doc says real PvP damage will not
  happen this way because all player citizens belong to one native cTribe (mpOwnerTribe at citizen +0x1014).
* shared.cpp (+49), partial: in Tribe Rivals food is per player; on return to Together each guest's balance is merged
  once, guarded against a race with the host's total.
* Read-only diagnostics (work, cheap): `coop.tribe factions` (every cTribe: pointer, player mark, position, members, hut,
  food, archetype, food container) and `coop.civ factions` (every civ: political id, player flag +0x89, money, city and
  vehicle counts, culture set +0x6C).
* Design: docs/11-rivals-research.md (a real rival needs its own cTribe / cCivilization and a separate local-player
  pointer per client; prototype order for Tribe: SpawnNpcTribe an empty tribe, move one citizen with AddMember /
  RemoveMember vt 0x98 / 0x9C, then find how to set the player-tribe pointer per client, or stop) and
  docs/12-four-player-coop.md (four-player layouts per stage). Their direction differs from ours: it calls our PvP design
  "legacy" and aims at four-player co-op with separate native factions.
* Do NOT copy: their civ.cpp `host_assign_cities` change (city i -> players[i % n]) drops the shared capital (0xFD) and
  gives a guest no city with one city and two players.

## Fix idea
From the fork (nvstlex/SporeCoopNVST nvst/rivals-foundation); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
Cheap to adopt: (1) the stage-mode / relation table as the data model of docs/11-rivals.md 11.2, but stored in our F3
session settings (saved with the world, which theirs is not), with a new message number; (2) the two faction diagnostics
as debug commands; (3) the research note's blocker list into docs/11-rivals.md for the Tribe and Civilization sections
(a separate native faction per player is required, an owner filter on orders is not enough). Everything else waits for
the Rivals 0.8 architecture.

## How to test
Unit: snapshot round trip, size and version rejection, symmetric relations (as their rivals_tests). In game: four players,
host changes a stage's mode in the lobby; every guest's lobby page shows the same table (read from the UI caption), a
rejoining guest gets it on join.

## Fix
Implemented WP-R1 Rivals shared systems (`COOP_FIX("rivals.core", "BUG-0794")`) on `w08/rivals-core`:
* `mods/coop/rivals_logic.h`: portable per-stage Together/Rivals mode table + presets (`AllTogether`, `AllRivals`, `Custom`), catch-up timer (60..600 s), draft picks (0..5), base distance (`Near`/`Middle`/`Far`), symmetric 4x4 player relation table (`Neutral`/`Ally`/`War`) with alliance/peace proposals and 60 s alliance-break countdown to War, Influence ledger (earn, rising buy cost `base * (1 + buys)`, `TurnAgainst`, 10 min `Protect`, `SmoothOver`), stage race progress & catch-up timer, stage-end winner Draft with rank penalty & escalating resource payouts (`100 + 50 * total_picks`), Chronicle scoreboard, and persistent store serialization.
* `mods/coop/wire/rivals.h`: strictly bounds-checked binary codec for `MSG_RIVALS = 80` (`Family::session` 1.4, `Route::Rivals`), locked via `tools/msg_manifest.py lock --rebase session`.
* `mods/coop/rivals.{h,cpp}`: runtime module with public C++ API for Wave 2 (`WP-R2..R6`), `store.cpp` per-world persistence (`"rivals.state"` v1), `LIFECYCLE_HOOK("rivals.player")` + `SESSION_STATE("rivals.session")`, `sync_space_rivalry()` wired to `space_rivalry.cpp`, and `coop.rivals` console command.
* `mods/coop/coop_ui.{h,cpp}` & `mods/coop/vote.cpp`: dedicated F9 `"Rivals"` / `"Race"` page (host edits stage modes/settings, guests view read-only; in-game Race progress, catch-up timer, relations, influence, draft picks, and Chronicle) plus stage-transition vote next-stage mode preview and host toggle.
* `mods/coop/tribe_debug.cpp` & `mods/coop/civ_debug.cpp`: read-only `coop.tribe factions` and `coop.civ factions` diagnostic commands.

## Verification
* Native unit + ASan/UBSan + fuzzer + linters: `make -j$(nproc)`, `make unit-cpp` (`tests/rivals_tests.cpp`, `tests/validate_tests.cpp`), `make unit-fuzz` (`wire::rivals` decoders), `make unit-net`, `make unit-lint` (`msg_manifest.py check`, `re_lint.py`, `docs_lint.py`, `ratchet_lint.py`, `tickets.py check`), and `tools/tests/test_lint.py` all passed.
* Wine lab run `20261125-030244` (lane 2):
  * `rivals_core` (ON): **PASS** (38 s, 0 faults) - verified host/guest stage modes + F9 Rivals UI (read-only on guest) + rejoin persistence, Alliance -> Break (fast 2 s timer) -> War -> Peace, Influence earn/protect/turn/buy, stage win -> catch-up timer expiry -> Draft picks & escalating 250 payout -> Chronicle totals, and `coop.tribe factions` / `coop.civ factions`.
  * `rivals_core_off` (`SPORE_FIX_OFF=rivals.core`): **FAIL** cleanly (37 s, 0 faults).

## History
- 2026-11-10: opened.
- 2026-11-25: implemented WP-R1 Rivals shared systems on `w08/rivals-core`, verified ON PASS and OFF FAIL in lab run `20261125-030244`.
