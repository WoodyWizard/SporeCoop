---
id: BUG-1496
title: Tribe: a host theme the relay started keeps playing on the guest after the session ends (TR-11)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w79-tribe
commits: 70979dc4
tests: tribe_theme_end, tribe_theme_end_off
signature:
docs: docs/re/tribe.md#tribe-audit-wp-b1
switch: tribe.theme_end
related: 
---

## Report
External static audit of 0.7.15.5, item TR-11 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-cr-tr.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-B1 (fix/w79-tribe). A behaviour change gets a switch and a twin.

## Root cause
* Code (inferred, not run): tribe_anims.cpp theme_forget (called at the session's end) only cleared g_theme_local; the
  guest's game stays in Tribe and the theme slots the relay started (BUG-0568) played on. A kind 6 stop that came while
  the guest was not in Tribe (an editor) was lost: on_member_anims returns outside Tribe before on_theme. Same shape as
  BUG-0890 (party music, fixed for kind 8 only). ? Whether a theme track loops until stopped is not verified.

## Fix
tribe_anims.cpp: `theme_session_end` (on_session_end) stops every relay slot with TribeTheme_Stop when the game is in
Tribe with its theme manager, else keeps the stop for the return; a kind 6 outside Tribe (`theme_stop_outside`) is
kept the same way; `theme_tick` (every frame in Tribe, also after the session) runs the kept stops; a world change
forgets them (another theme manager). Rule `tribe_logic::theme_end` (unit test). Counters `tribe audit 0.7.17:
theme_end ended= later= lost=`. Switch `coop.fix tribe.theme_end off`: 0.7.16.1.

## Verification
open, waiting for the lab run. pending night run: tribe_theme_end, tribe_theme_end_off.
`coop_tribe_theme_end.py` (suite `tribe_theme_end`, tribe, warm): the host's members fight, the guest's `coop.audio
list` has a theme track (caller 0xBC2414) with no stop; the guest leaves; within 4 s each such track got the game's
stop command. Twin `tribe_theme_end_off`: no stop, must FAIL. If the twin passes (a theme ends by itself) the
session-end half is not reachable: then close it with `no-fail-run:` and that run dir; the editor half stays covered by
the unit test.
0.7.17 release lab: PASS: tribe_theme_end (lab 20261124-020506).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-B1), branch fix/w79-tribe.
- 2026-11-23: fix on fix/w79-tribe (switch tribe.theme_end, tests tribe_theme_end, tribe_theme_end_off); lab run pending (no lab by day).
- 2026-11-24: released in 0.7.17.
