---
id: BUG-0869
title: Feature idea: warm reconnect reuses the unchanged Cell world instead of a new world transfer
status: fixed
kind: feature
stage: Cell
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages30-dev (world.cpp resume receipt, CAP_RESUME_WORLD)
role: both
versions:
fixed_in:
branch: w08/cell-warm
commits: 0db2c591, 25017314, e91ccf62
tests: unit, cell_warm_reconnect, cell_warm_reconnect_off
switch: cell.warm_reconnect
signature:
docs: docs/04-netcode.md
related:
---

## Report
A guest that drops for a moment (Wi-Fi, a brief leave/rejoin into the same unchanged Cell world) downloaded the whole world again (2-4 MB) and reloaded through the galaxy menu.

## Root cause
Not a bug, an optimisation of the reconnect path (`world.cpp` shared the full save folder on every new join).

## Fix
Implemented behind `COOP_FIX("cell.warm_reconnect", "BUG-0869", ...)` (WP-C of 0.8.0):
- Protocol & portable ledger (`mods/coop/world_resume.h`, `mods/coop/net/protocol.h`, `mods/coop/net/session.{h,cpp}`):
  - Added HELLO-only capability bit `CAP_RESUME_WORLD = 64` (bit 6, distinct from WP-Q's `CAP_JOIN_PROOF = 128`) carrying a trailing `u64 resume_receipt` after `CAP_FAMILIES` (`session 1.4`). Older hosts ignore unknown cap bits and trailing bytes.
  - Added optional trailing `u64 resume_receipt` and `u8 flags` (bit 0 `kFlagResumed`) on `MSG_WORLD_DONE` (`world 1.2`). Older guests ignore trailing bytes and standalone `MSG_WORLD_DONE` when no offer is active.
  - Added portable `world_resume::HostLedger` (32-entry ring buffer of `{receipt, slot_key, world_id, stage_epoch, slot_mtime, save_gen, food_total, transfer_world_id}`) and `world_resume::GuestState` (`Empty -> PendingLoad -> Active`).
- Runtime integration (`mods/coop/world.{h,cpp}`, `mods/coop/cellsync.{h,cpp}`, `mods/coop/space_allies.cpp`, `mods/coop/session_glue.cpp`):
  - Host generates an unpredictable 64-bit receipt per Cell world share (mixing `QueryPerformanceCounter`, atomic serial, `GetCurrentProcessId`, and tick count), records it in the 32-entry ledger, and appends it to `MSG_WORLD_DONE`.
  - Any host save (`world::save_game`, `h_SaveHandler`, `savestrip` write watch, and `space_allies.cpp` `h_Save` -> `world::on_host_save()`) bumps monotonic `g_save_gen` and updates `slot_mtime`, immediately invalidating older receipts.
  - On reconnect, the guest attaches its active receipt to `HELLO` only while still in that exact Cell world for the same host (cleared on leaving Cell, loading another world, starting a host session, receiving a new `MSG_WORLD_OFFER`, or switching hosts).
  - Host accepts warm reconnect ONLY when `cell.warm_reconnect` is ON, host is in Cell, receipt is in the 32-entry table, `world_id`/`stage_epoch`/`slot_key` match, `g_save_gen` and `slot_mtime` are unchanged, and no save is in flight. Accepted reconnects send a lightweight `MSG_WORLD_DONE` (`kFlagResumed`) with zero `MSG_WORLD_OFFER`/`FILE`/`CHUNK` and immediately re-sync Cell state, NPCs, food, and partner cells (`cellsync::on_warm_resume_host` / `on_warm_resume_guest`). Otherwise the host logs the rejection reason, bumps `warm_fallbacks`, and falls back to the normal full transfer.
  - Exposed `warm_reconnect:` counters (`warm_resumes`, `warm_fallbacks`, `world_offers_sent`, `world_offers_recv`, `host_receipts`, `guest_receipt`) in `coop.status`.

## Verification
- Unit tests (`tests/net_tests.cpp` `test_cell_warm_reconnect`): ring buffer 32-entry eviction, matching receipt accepted, wrong world/epoch/slot/save_gen/slot_mtime/save_in_flight/disabled rejected, `GuestState` lifecycle, `WorldDone` + `CAP_RESUME_WORLD` round-trip and backward compatibility with 0.7.14 peers (`make unit-net`: 8 shards, ALL OK).
- PASS (`cell_warm_reconnect`, lane 5): `out/lab/20261125-025448` (Part 1: `warm_resumes 0 -> 1` on host and guest, `world_offers_sent 1 -> 1`, `world_offers_recv 1 -> 1`, `left_cell=False`, partner cells returned in 0.35 s, NPC mirrors in 0.36 s; Part 2 after host save: `warm_fallbacks 0 -> 1`, `world_offers_sent 1 -> 2`, full reload into saved Cell world with new receipt armed; 0 faults on both games).
- FAIL (`cell_warm_reconnect_off`, `SPORE_FIX_OFF=cell.warm_reconnect`, lane 5): `out/lab/20261125-025448` (Part 1: `warm_resumes 0 -> 0`, `world_offers_sent 1 -> 2`, `world_offers_recv 1 -> 2`, `left_cell=True`, 0 faults).

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
- 2026-11-25: implemented in WP-C (`w08/cell-warm`, commits `0db2c591`, `25017314`, `e91ccf62`) behind `cell.warm_reconnect`, verified ON PASS + OFF FAIL in `out/lab/20261125-025448`.
