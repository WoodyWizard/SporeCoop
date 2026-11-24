---
id: BUG-1493
title: Tribe: a guest's member mirror hangs in the air after the host went away or the snapshots stopped (TR-7)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w79-tribe
commits: 01e1e6c0
tests: tribe_air_release, tribe_air_release_off
signature:
docs: docs/re/tribe.md#tribe-audit-wp-b1
switch: tribe.air_release
related: BUG-0946
---

## Report
External static audit of 0.7.15.5, item TR-7 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-cr-tr.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-B1 (fix/w79-tribe). A behaviour change gets a switch and a twin.

## Root cause
* Code (inferred, not run): tribe_mirrors.cpp guest_tick returned early while `local_play()` (the host away, or this
  guest elsewhere) and, per mirror, when no snapshot came for 3 s, leaving `g.air` and the game::air_hold entry as they
  were. air_reassert re-places a held creature at the hold's position whenever the game grounds it, so a mirror that
  was airborne (BUG-0946 / BUG-1082) at that moment hung there until the host was back or the snapshots returned.
* Not a use-after-free feeder (the audit's link to CR-1 is wrong): remove_member, on_world_change and on_session_end
  release the hold, g_air keeps the serial (BUG-0995).

## Fix
tribe_mirrors.cpp: `air_release` on both early returns: the hold goes (`g.air = false`, `game::air_hold(c, false)`, a
pointer compare only), the mirror's game lands it; the next air list holds it again once guest_tick places it. Rule
`tribe_logic::air_release` (tests/tribe_tests.cpp). Counter `coop.tribe anims` `air ... released=`. Switch `coop.fix
tribe.air_release off`: the hold stays (0.7.16.1).

## Verification
open, waiting for the lab run. pending night run: tribe_air_release, tribe_air_release_off.
`coop_tribe_air_release.py` (suite `tribe_air_release`, tribe, warm): the host holds a member 12 m up (`coop.tribe
hang`), the guest stops taking tribe states (`coop.tribe statehold 1`); 7 s later the guest's mirror is under 2 m over
the guest's ground, and up again within 6 s of `statehold 0`. Twin `tribe_air_release_off`: it hangs at 12 m, must
FAIL. The host-away half shares the code (unit test only).
0.7.17 release lab: PASS: tribe_air_release (lab 20261124-020506); FAIL (fix off): tribe_air_release (lab 20261123-125538).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-B1), branch fix/w79-tribe.
- 2026-11-23: fix on fix/w79-tribe (switch tribe.air_release, tests tribe_air_release, tribe_air_release_off); lab run pending (no lab by day).
- 2026-11-24: released in 0.7.17.
