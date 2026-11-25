---
id: BUG-1551
title: Rivals Core: editor return race wipe, persistence, duplicate notices, Influence UI parity, lobby overlap and security fixes
status: open
kind: bug
stage: All
reported: 2026-11-25
source: v0.8.0 independent audit & campaign_rivals
role: both
versions: 0.8.0
fixed_in:
branch: w0801/core
commits:
tests:
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
TBD.

## Verification
TBD.
