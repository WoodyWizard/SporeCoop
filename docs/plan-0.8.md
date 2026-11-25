# Plan for 0.8.0 (Rivals: own bases in one world)

Branch: `rel/0.8.0` (branched from `main` / `v0.7.99.1`, `dcc1047a`).
Design spec: `docs/11-rivals.md` (11.1-11.11).
Goal: Ship full **Rivals mode** across all five stages (Cell, Creature, Tribe, Civilization, Space) with shared Rivals systems (per-stage Together/Rivals table, unified 4x4 player relations with 60 s war notice, influence and per-player NPC diplomacy, Race/Chronicle page, catch-up timer, stage-end winner draft, and mixed-mode transitions), plus Shared Adventure Editor (`BUG-0863`), Lobby QoL (`BUG-0867`, `BUG-0874`), Cell warm reconnect (`BUG-0869`), and open Tribe bugs (`BUG-1542`, `BUG-1543`).

## Wire Reservations (WP-0)

Message IDs reserved in `mods/coop/net/manifest.h` (`kReservedMsgs[]`):
- `80`: `RIVALS` (`Family::session`, WP-R1) - stage modes table, 4x4 player relations, alliance/peace proposals, 60 s war countdown, influence & NPC diplomacy actions, Race/Chronicle snapshot, stage win + catch-up timer, draft offers/picks/payouts.
- `81`: `RIVALS_FACTION` (`Family::npc`, WP-R3/R4/R5/R6) - faction ownership, base sites, per-player NPC relation tables, perspective assignment, Space homeworld emigration.
- `82`: `ADV_EDITOR` (`Family::creature`, WP-AE) - shared Adventure Editor (`BUG-0863`) placements, transforms, acts, goals, captain, leases, undo/redo.

Family minors bumped once in WP-0 (`mods/coop/net/messages.def`, re-locked with `--rebase` per WP until 0.8.0 ships):
- `session 1.4`: `MSG 80 RIVALS`, `CAP_JOIN_PROOF` (`BUG-0867`), `CAP_RESUME_WORLD` (`BUG-0869`).
- `world 1.2`: Cell warm reconnect receipt on `MSG_WORLD_DONE` (`BUG-0869`).
- `creature 1.5`: `MSG 82 ADV_EDITOR` (`BUG-0863`), Creature Rivals (`BUG-1547`).
- `npc 1.3`: `MSG 81 RIVALS_FACTION`, Tribe visiting UFO mirror (`BUG-1543`).
- `cell 1.4`: Cell Rivals (`BUG-1546`).
- `tribe 1.3`: Tribe Rivals (`BUG-1548`), Tribe epic friendly-species fix (`BUG-1542`).
- `civ 1.4`: Civilization Rivals (`BUG-1549`).
- `space 1.6`: Space Rivals (`BUG-1550`).

## Work Packages & Waves

### Wave 1 (Parallel on `rel/0.8.0`)

| WP | Branch | Tickets | Scope |
|---|---|---|---|
| WP-R1 | `w08/rivals-core` | `BUG-0794` | Shared Rivals systems (`docs/11-rivals.md` 11.2, 11.4-11.8): `rivals_logic.h`, `wire/rivals.h`, `rivals.{h,cpp}`, `MSG 80 RIVALS`, per-stage mode table, 4x4 relations + 60 s break notice, influence/diplomacy ledger, Race & Chronicle page, catch-up timer + Draft framework, `store.cpp` persistence, `vote.cpp` mode preview, `coop.rivals` / `coop.tribe factions` / `coop.civ factions`, `space_rivalry.cpp` sync. |
| WP-T | `w08/tribe-fixes` | `BUG-1542`, `BUG-1543` | Tribe co-op epic skips domesticated pets & friendly species and sets hostility (`tribe.cpp`); extend `player_rules.cpp` `K_UFO` mirror to Tribe stage (`modes::kTribe`). |
| WP-Q | `w08/lobby-qol` | `BUG-0867`, `BUG-0874` | Optional session password (`CAP_JOIN_PROOF`, SHA-256 connection-bound proof, LAN lock bit, masked from logs/reports) + recent servers list; Spore-style tooltips on co-op window buttons (0.8 s hover, EN + RU). |
| WP-C | `w08/cell-warm` | `BUG-0869` | Cell warm reconnect receipt (`CAP_RESUME_WORLD`, host 32-entry table, unchanged Cell slot reuse skipping `MSG_WORLD_OFFER`, fallback on host save/change). |
| WP-AE | `w08/adv-editor` | `BUG-0863` | RE + verified symbols for Adventure Editor (`docs/re/adventure.md`), shared Adventure Editor (`adv_editor_logic.h`, `wire/adv_editor.h`, `adv_editor.{h,cpp}`, `MSG 82 ADV_EDITOR`): placements, move/rotate/scale, acts, goals, captain, leases, Undo/Redo, Save. |

### Wave 2 (Parallel on `rel/0.8.0` after Wave 1 merge)

| WP | Branch | Tickets | Scope |
|---|---|---|---|
| WP-R2 | `w08/rivals-cell` | `BUG-1546` | Cell Rivals (`11.3.1`): per-player bar & DNA, war-gated PvP bites & meat drops (no knockout/revive), first-to-full-bar stage win + catch-up timer + Cell part draft, edge arrows & Race stats. |
| WP-R3 | `w08/rivals-creature` | `BUG-1547` | Creature Rivals (`11.3.2`, RE R1/R2/R3/R4/R7): own nest & family herd takeover at base distance, guest perspective swap, per-player NPC species relations (`social.cpp`), Creature influence, war PvP & home nest protection, brain-level win + forced transition + Creature part draft. |
| WP-R4 | `w08/rivals-tribe` | `BUG-1548` | Tribe Rivals (`11.3.3`, RE R1/R2/R3/R4/R7): NPC `cTribe` takeover (`+0x290` AI off, species swap), guest perspective swap (`GetPlayerTribe`), per-player food/huts/tools, per-player NPC tribe diplomacy + influence, War raids & member PvP with inviolable main village, Alliance food/tool gifts, >50% NPC tribes win + Tribe draft. |
| WP-R5 | `w08/rivals-civ` | `BUG-1549` | Civilization Rivals (`11.3.4`, RE R1/R2/R3/R4/R7): NPC `cCivilization` takeover (AI off, species/designs, capital at base distance), guest perspective swap (`GetPlayerCivilization`), own money/cities/vehicles, per-player NPC nation diplomacy + influence, War city capture with inviolable capital & superweapon block, Alliance trade/gifts, >50% NPC cities win + Civ draft. |
| WP-R6 | `w08/rivals-space` | `BUG-1550` | Space Rivals (`11.3.5`, RE R5/R7): integrate `space_rivalry.cpp` with `rivals.h`, guest homeworld emigration (R5: similar nearby planet with matching cities + notice), homeworld inviolability (R7: block planet buster & capture), Space NPC empire influence, Chronicle galaxy core finale bonus & end screen. |

### Wave 3 (Integration & Release Gate)

- Merge Wave 2 into `rel/0.8.0`, enable all 5 stages in Rivals lobby table, verify mixed-mode transitions (`Together -> Rivals`, `Rivals -> Together`), run full unit tests + ASan + TSan + fuzz + Wine ON/OFF tests + smoke + play tests + `make release-dry`, merge to `main`, green CI, tag and publish `v0.8.0`.

## Rules
- Every new feature / bug fix gets a `COOP_FIX` switch, unit tests, real Wine lab test, and FAIL OFF-twin in `tools/tests/suite.py`.
- Any RE findings go into `docs/re/*.md` and `data/symbols/overrides_dc04aee5.sym` with confidence words (`verified` / `likely`).
- Any WP changing a locked wire family re-locks with `tools/msg_manifest.py lock --rebase <family> "<WP>: <what>, unreleased <family> 1.x of 0.8.0"` and updates `docs/04-netcode.md` via `tools/msg_manifest.py docs`.
- No em dash (U+2014) in any text.
