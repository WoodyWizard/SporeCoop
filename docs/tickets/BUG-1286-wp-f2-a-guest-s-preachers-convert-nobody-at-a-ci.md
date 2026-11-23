---
id: BUG-1286
title: WP-F2: a guest's preachers convert nobody at a city the host's camera never saw (fork BUG-9086)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9086
role: guest
versions: 0.7.15.2
fixed_in: 0.7.16
branch: fix/w56-sync
commits: bc78d7e3, 38278875
tests: civ_preach_far, civ_preach_far_off
signature:
docs: docs/re/civ.md#civ-influence-relay
related: BUG-1285
---

## Report
Found by the Lineages fork (PR #196), its BUG-9086 (row 12): religious vehicles at a city the host's camera never looked
at convert nobody (the city's preaching slots have 0 / 0 places) while the city still damages them. A guest away from
the host (whose orders run in the host's game) cannot take cities by religion. Not reproduced on our lab yet.

## Root cause
Ghidra (GOG 2024): the places are written only by City_SetPreachingCapacity 0xBD9FF0, whose only caller is the camera
city's crowd controller 0xD57B00; the preach tick 0xBEB310 bounds the converts by slot+0 minus slot+8. Our docs had the
preach tick as 0xBEB2F0 with one argument (GOG 2016): CORRECTION added, it is 0xBEB310 (city, slot, activePreachers) ret 8.

CORRECTION (2026-11-19, fix/w57-sync, lab run 20261119-104136): the first run failed in the test's setup (no religious
vehicle allowance on the civ_npc fixture, see BUG-1284). With `spawnveh ... force` and the preachers put along x only,
they stood 30 m over the ground on the host while the guest's game put its copy on the ground (sync-check MISMATCH
"v6 34.0", lab 20261119-114030); once the host faulted (BUG-1322). The test puts them on the city's sphere now. The hook
now sets a slot's places only for a slot with its live spatial object (+0x78), the fork's verified condition.

## Fix
`coop.fix civ.remote_preaching` (civ_influence.cpp): the host hooks the preach tick and gives a slot with 0 / 0 places
the controller's counts 10 / 2 before the game's tick runs.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Lab, first night run (rel/0.7.16 c7699dca, rel/0.7.16 worktree): civ_preach_far FAIL out/lab/20261119-104136 ("the
  host has religious vehicles ([])": the setup above).
- Lab with the test fixed, merged tree (fix/w57-sync 3801b9f5, fix/w57-sync worktree): PASS civ_preach_far
  out/lab/20261119-121505 (slot 2 of the far city gets 10 / 2 places, the preachers convert, the guest shows it, the
  preachers stand at the same spots on both games, no sync-check MISMATCH, no fault); also 20261119-115145 (x2). FAIL twin
  civ_preach_far_off (SPORE_FIX_OFF=civ.remote_preaching) out/lab/20261119-121505 for the right reason: slots 0 / 0,
  nobody converted, all three preachers gone (the city's retaliation).

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
- 2026-11-19: fixed (fix/w57-sync): see the CORRECTION under Root cause; PASS and FAIL twin run dirs above.
- 2026-11-23: released in 0.7.16.
