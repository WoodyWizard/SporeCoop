---
id: BUG-1285
title: WP-F2: a guest's city religious conversion meter stays at zero (fork BUG-9084)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9084
role: guest
versions: 0.7.15.2
fixed_in: unreleased
branch: fix/w56-sync
commits: bc78d7e3, 6fdea267
tests: civ_religion_state, civ_religion_state_off
signature:
docs: docs/re/civ.md#civ-influence-relay
related: BUG-1287, BUG-1286
---

## Report
Found by the Lineages fork (PR #196), its BUG-9084 (row 6): the host's game converts citizens (follower map +0x5A0,
converter +0x754); the city snapshot carried the military capture meter only, so the guest's religious meter over a
city stayed 0 while the host's went up.

## Root cause
Observed (code + Ghidra): civ_host.cpp sends kCityCapture only; the meter +0x750 is recomputed every tick by the city
update 0xBE2260 from the map, so the map and +0x754 are the state to send (docs/re/civ.md#civ-influence-relay).

CORRECTION (2026-11-19, fix/w57-sync, lab run 20261119-104136): the test converted the whole city (4 x 5 of 15): at a
meter of 100 the game captured it ("Alice captured a city"), the host's meter read 0 while the guest's showed 100, so
both "host (control)" and the meter check failed. The test converts 2 at a time until the player has followers (2 of 15:
26.7 on both games). No mod change for this ticket.

## Fix
`coop.fix civ.religious_state` (civ_influence.cpp): once a second the host sends every city's follower map and
converter in MSG_CIV_STATE chunk 0xF9 (chunks 0; civ family 1.1; a 0.7.15.2 guest drops it as a bad snapshot header);
the guest writes them with the game's own map index 0x643A00 (no node erased, a value the host has not is set to 0).
While they come the guest's own ConvertCitizens returns (the host's world decides; a 0.7.15.2 host sends none and the
guest converts as before). Test commands: `coop.civ influence [city]`, `coop.civ convert <city> <n>` (host).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Lab, first night run (rel/0.7.16 c7699dca, rel/0.7.16 worktree): civ_religion_state FAIL out/lab/20261119-104136 (the
  capture above).
- Lab with the test fixed, merged tree (fix/w57-sync 3801b9f5, fix/w57-sync worktree): PASS civ_religion_state
  out/lab/20261119-121505 (followers 2 / 2, converter 800021CA on both, meter 26.7 / 26.7; key frame: the meter at the
  same level over city 1 on both screens); also 20261119-111559. FAIL twin civ_religion_state_off
  (SPORE_FIX_OFF=civ.religious_state) out/lab/20261119-121505 for the right reason: the guest has 0 followers, no
  converter, meter 0.0.

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
- 2026-11-19: fixed (fix/w57-sync): see the CORRECTION under Root cause; PASS and FAIL twin run dirs above.
