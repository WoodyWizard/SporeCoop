---
id: BUG-1608
title: Tribe Rivals: personal chieftain KO, PvP shield, and draft payout notices broadcast globally to all players and stack duplicate banners
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-27
source: 4-player Tribe Rivals AI playtest (v0.8.2.4, out/ai-playtest/20261127-152410, PLAYTEST-019, PLAYTEST-061, PLAYTEST-066)
role: both
versions: 0.8.2.4
fixed_in: 0.8.2.5
branch: main
commits: edafb9ef
tests: rivals_tribe, make test
signature: Host and other guests receive "Your Chieftain was knocked out!" and 3x duplicate "PvP shield for 5:00" notices
switch: rivals.tribe_local_notices
docs:
related: BUG-1571 BUG-1580
---

## Report
During a 4.5-hour 4-player Tribe Rivals AI playtest (`out/ai-playtest/20261127-152410`):
- `PLAYTEST-019`: Host received `"Your Chieftain was knocked out! Reviving in 1:30"` and `"Your Chieftain has recovered!"` banners triggered by guests (`guest1`/`guest3`).
- `PLAYTEST-061`: After a tribe wipe, Host and guests saw three duplicate `"PvP shield for 5:00"` notices.
- `PLAYTEST-066`: Guest saw stacked duplicate `"Your Chieftain was knocked out! Reviving in 0:43 / 0:41 / 0:37"` banners within seconds.

## Root cause
1. `mods/coop/tribe_rivals.cpp` called `tribe::C.notice(...)` (which broadcasts over `MSG_NOTICE` to every player in the session) instead of local-only `tribe::C.feed(...)` for per-player guest/host notices (`CustomOp::ChiefRespawn`, `CustomOp::Immunity`, `DraftApplyKind::Payout`, and host-only gameplay hints). Every guest receiving `Immunity` echoed `"PvP shield for 5:00"` back to all 4 players, and every guest's `"Your Chieftain was knocked out!"` notice was relayed to Host and all other guests.
2. In `host_wipe_recovery_tick`, `g_chief_ko_active[p]` was gated on `live > 0`, allowing a tribe wipe tick to flip `ko -> recovered -> ko` while the chieftain was still knocked out, and Guest had no cooldown debounce on `ChiefRespawn` KO notices.

## Fix
- Added `COOP_FIX("rivals.tribe_local_notices", "BUG-1608", ...)` and routed per-player notices (`CustomOp::ChiefRespawn`, `CustomOp::Immunity`, `DraftApplyKind::Payout`, host payout, and host-local hints) through local-only `tribe::C.feed(...)`.
- Added pure state helpers `decide_chief_ko_notice` (15-second cooldown debounce on KO announcements, ignoring non-local player IDs and periodic sync updates) and `host_chief_ko_state` (preserving KO state across wipe ticks while the chieftain remains knocked out) in `mods/coop/tribe_rivals_logic.h` with unit tests in `tests/tribe_rivals_tests.cpp`.

## Verification
- PASS run: `out/lab/20261127-200133` (`rivals_tribe`, PASS, 39 s, 0 faults) and full `make test`.
- no-fail-run: reproduced in 4-player AI playtest `out/ai-playtest/20261127-152410` (`PLAYTEST-019`, `PLAYTEST-061`, `PLAYTEST-066`, `shots/2376-guest2-shot.png`).
