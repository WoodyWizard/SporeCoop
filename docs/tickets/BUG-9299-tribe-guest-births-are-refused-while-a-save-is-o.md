---
id: BUG-9299
title: Tribe (guest): births are refused while a save is only queued or the host's own birth is pending
status: open
kind: bug
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Live 67-dev, Tribe: "Adding babies seemed inconsistent and broke a lot of the time." Host log: one guest baby at 21:31:38,
then five refusals "Births are not available at this point in the Tribal campaign." (21:31:39-47); two babies at 21:39:06;
41 refusals "The host could not add that baby just now (your village, or a save running)" from 21:46:39 to 21:56:50.

## Root cause
1. The 41 refusals: `world::save_pending()` in `buy_guest_baby` (tribe.cpp ~8566) while the stuck outfit vote held a share
   queued (the outfit vote ticket opened with this one). A queued share has not started writing; the purchase would be in it.
2. The five refusals (likely, not proven at runtime): `!F<uint8_t>(ui, 0x3A3)` reads the HOST's TribalGame
   mbMatingAllowed (docs/re/tribe.md), cleared while the host's own birth is on its way (the host's food fell 49 -> 29 ->
   19 in that window: the host was buying babies). The guest's births depend on the host's own birth state.
3. The guest sends one order per click with no in-flight guard (`h_HutUI` ~2870) and the reasons are lumped into one
   message, so a player clicks 5-14 times and learns nothing.

## Fix
Planned: drop `save_pending()` (keep `save_busy()` and `load_queued()`) in buy_guest_baby and buy_guest_tool; skip the
host's +0x3A3 flag for a guest purchase (keep BirthBlocked) after logging its value once; separate refusal messages; a
0.5 s in-flight guard on the guest. Switch `tribe.guest_birth_gate`. Test duo_tribe_birth.py with a queued share and a
host birth in flight: FAIL / PASS.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
