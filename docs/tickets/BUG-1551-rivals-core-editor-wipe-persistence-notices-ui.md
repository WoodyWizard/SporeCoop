---
id: BUG-1551
title: Rivals Core: editor return race wipe, persistence, duplicate notices, Influence UI parity, lobby overlap and security fixes
status: fixed
kind: bug
stage: All
reported: 2026-11-25
source: v0.8.0 independent audit & campaign_rivals
role: both
versions: 0.8.0
fixed_in: 0.8.0.1
branch: w0801/core
commits: b4ba3810, c05de51b, 0d7d9e7b
tests: rivals_core, rivals_core_off, ui_golden, password_recent, rivals_tests, campaign_rivals rivals_tests (ASan+UBSan), net_tests, test_lint.py
signature:
docs: docs/11-rivals.md
related: BUG-0794, BUG-0867, BUG-1543
---

## Report
Independent static audit of v0.8.0 and the first `campaign_rivals` run uncovered critical core Rivals and lobby bugs:
1. Returning from any host editor passes `from=kEdit/kEditor` to `rivals::on_stage_change`, wiping Race progress, catch-up timer, and Influence stage state (`rivals.cpp:684-703`).
2. Persistence bugs (`rivals.cpp:239,276-281,307-331`): `SESSION_STATE` fails to reset `g_store_serial`; `disk_opened()` sets `g_store_serial` without loading existing data; `disk_save()` fails to clear disk record when switching back to `AllTogether`; `PersistedWorld::history` stores volatile `player_id`.
3. Rivals notices are duplicated on both host and guests (`rivals.cpp` sends both `emit_notice` and `K_EVENT`, and guest `K_EVENT` bounces `MSG_NOTICE` back to host).
4. `Protect` ignores stage callback rejection and does not verify actor alliance (`rivals.cpp:447-456`).
5. 3-4 player disconnect breaks catch-up race completion (`rivals.cpp:663-673`, `rivals_logic.h:705-737`).
6. Unguarded OOB reads in `rivals.cpp:1546` (`chronicle add <pid>`) and `rivals.cpp:1187,1247-1255` (`ui_rows()`).
7. Influence/Relations F9 UI gaps: `TurnAgainst`/`Protect`/`SmoothOver` have no generic F9 buttons, `Buy +25` shows in Cell, no Decline Ally/Peace buttons, and rejected action errors are discarded silently.
8. Lobby `D.vote_mode_btn` overlaps `D.ask_btn` and `D.rline` (`coop_ui.cpp:1289,2545`), `vote.cpp:28` checks `"Civilization stage"` which is never emitted, and `vote.cpp:1103` lets host end Cell Rivals before catch-up/draft completes.
9. Pre-auth password eviction (`net/session.cpp:1455`), missing `std::isfinite` on `K_UFO` rx (`player_rules.cpp:1147`), and `tools/lab.py:1344` `lanes` `TypeError`.

## Root cause
See `docs/plan-0.8.0.1.md` WP-1.

## Fix
Implemented all WP-1 items on branch `w0801/core`:
1. Added `rivals::State::on_stage_enter()` comparing `to_s != race.stage` so `Stage -> Editor -> Stage` round-trips never wipe `race` or `inf` (`rivals_logic.h`, `rivals.cpp`).
2. Fixed Rivals store persistence: reset `g_store_serial` and persisted cache in `SESSION_STATE`; load existing `"rivals.state"` in `disk_opened()`; overwrite existing disk record with a clean default record when switching back to `AllTogether`; persist stable `player_key` in `PersistedWorld::history` (`hisk` format, keeping legacy `his` backwards-compatible), remap keys in `adopt_key()`, and remap `player_id` on player join/load (`rivals_logic.h`, `rivals.cpp`).
3. Made `rivals::Ctx::notice` local-only (`feed` + `chat_log`) in `coop.cpp` so Rivals events display once on host and once on guests via `K_EVENT` without duplicate `MSG_NOTICE` or guest bounce-back; send unicast `K_EVENT` error feedback to guests on rejected requests.
4. Added `Influence::can_spend_protect()` / `try_spend_protect()` and checked alliance via `on_npc_ui_list` plus stage callback return value before deducting influence or adding a protection (`rivals_logic.h`, `rivals.cpp`).
5. Tracked present-only finishers (`present_finished_count()`, `check_all_finished()`, `on_player_leave()`) so departed finishers are discounted and catch-up ends immediately when all remaining present players have finished (`rivals_logic.h`, `rivals.cpp`).
6. Added OOB bounds guards for `coop.rivals chronicle add <pid>` and `valid_player(me)` in `rivals::ui_rows()`.
7. Completed F9 Influence & Relations UI parity: hid `Buy +25` in Cell, added clickable `Decline` buttons for incoming Ally/Peace proposals, displayed local error notices on rejected F9 actions, rendered `on_npc_ui_list(me)` entries with status/protection timers and clickable `Protect (20)`, `Smooth (25)`, and per-rival `Turn (30)` buttons, and tightened `rprev`/`rnext` pagination bounds in `coop_ui.cpp`.
8. Placed `D.vote_mode_btn` cleanly at `{450, 558, 720, 592}` (or `{40, 558, 440, 592}` while a report round hides disabled `D.ask_btn`) with zero overlap against `D.ask_btn` and `D.rline`; emitted `"wants to move on to the Civilization stage"` for Tribe->Civ graduation editors in `vote.cpp` (matching both new and legacy text in `vote_next_stage()`); held `own_end_hold()` in Cell Rivals while `rivals::is_catchup_active() || rivals::draft_state().active`.
9. Fixed pre-auth password eviction (`net/session.cpp`), added `std::isfinite` validation on `K_UFO` rx (`player_rules.cpp`), and fixed `ensure_lanes(range(2, a.n + 1))` in `tools/lab.py`.
10. Made `report_stage_complete()` idempotent (`finish_rank != 0`) so Chronicle stage-win points and notices never duplicate, and moved the NPC Influence section below `on_extra_ui_rows()` so stage summary rows remain on F9 Page 1.
- `switch: rivals.core` (`SPORE_FIX_OFF=rivals.core`).

## Verification
- `make -j$(nproc)`: clean build (0 warnings, 0 errors).
- `make unit-cpp`: all ASan+UBSan unit tests pass, including `rivals_tests` (444 passed, 0 failed) covering editor round-trip, persistence clear/keyed history, `Protect` callback rejection, 3-4p disconnect race completion, and draft payout stage isolation.
- `make unit-net`: 8 shards + `fakepeer_check.py`, ALL OK.
- `python3 tools/tests/test_lint.py`: 0 problem(s).
- Wine lab verification:
  - FAIL runs: `out/lab/20261125-093651` (`campaign_rivals` duplicate notices & editor race wipe), `out/lab/20261125-114308` (`campaign_rivals` Chronicle inflation), `out/lab/20261125-111531` (`rivals_core_off` twin FAIL with `SPORE_FIX_OFF=rivals.core`).
  - PASS runs: `out/lab/20261125-130601` (`rivals_core` PASS 36 s), `out/lab/20261125-111531` (`ui_golden` PASS 86 s, 12/12), `out/lab/20261125-105211` (`password_recent` PASS 28 s), `out/lab/20261125-130606` (4-player `campaign_rivals` Cell->Galactic Core: 1 win/stage, `Alice=710`, 0 duplicate notices).
