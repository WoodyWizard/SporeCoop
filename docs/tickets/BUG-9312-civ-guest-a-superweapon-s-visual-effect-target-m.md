---
id: BUG-9312
title: Civ (guest): a superweapon's visual effect (target marker, impact) is drawn on the host's screen only, never on a guest's
status: open
kind: bug
stage: Civ
reported: 2026-11-21
source: live QA 2026-11-21 (TITAN, fix/l47-civ, BUG-9310 test)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-decide
commits: 2d52f56a
tests: manual two-game live test (docs/06-testing.md#testing-global-sw-newcomer); no lab test yet
signature:
docs: docs/re/civ.md#civ-global-sw-party
related: BUG-9306, BUG-9310
---

## Report
Live QA 2026-11-21 (fix/l47-civ, TITAN QA copy, host DuoQACivHost / Abber instance 9, guest DuoQACivGuest instance 10,
both cameras on the host's capital, 10 FPS). A non-global superweapon (type 3) fired at open ground beside the host's
capital, once by the guest from its own nation (BUG-9310) and once by the host from its own: the host's game drew the
targeting marker (a white cross with rings) and then a large green burst; the guest's game drew nothing in screenshots
at 0.3 / 1 / 1.5 / 2 / 3 / 6 s, only the caption "CivHost spent 2000 money (shared)" for the host's own fire
(out/qa-shots/bug9310_baseline_host_own_sw3_host_left_guest_right.png of fix/l47-civ: host left, guest right). A type
0 fired at a city: its hp fell on both games (the building hp relay), so results reach the guest, the weapon's own
show does not.

## Root cause
Observed: every superweapon is fired on the host only (the vote / request path; a guest's own FireSuperweapon is consumed
and sent as CMD 5, docs/re/civ.md "Superweapons"); the launch 0xBE5D30 -> 0xBDC2F0 fires the type's tool through
cToolManager::FireTool on the host's game. The mod relays vehicle and turret shots (civ.cpp guest_fire, BUG-0530) but no
superweapon launch, so a guest's game never plays the tool. Inferred: the global types 2 / 5 / 8 (their own launch
functions 0xBE4300 / 0xBDC4A0 / 0xBE4550 and the camera move) are not shown on a guest either (not tested).

## Fix
`civ.sw_show` (`coop.fix civ.sw_show off`: no relay). The host hooks Simulator::cCity::LaunchSuperweapon 0xBE5D30 (the
launch every superweapon goes through) and notes the FireTool aims made inside it (the squadron FireTool hook ->
civ::fire_observed); after the launch it sends MSG_CIV_CMD kind 12 (type, nation, the launching city's place, the aims)
to every guest. A guest finds its copy of that city, loads that nation's superweapon tools (ensure_sw_tools) and fires
the same tool from the city hall at each aim, as the game's launch does: the target marker, the projectile and the
burst are drawn. For 20 s its damage, capture, conversion and purchase hooks drop every effect of that nation (the host
stays the authority; its results arrive through the usual relays). Type 5 fires no tool at its launch: nothing sent.

## Verification
Live, TITAN QA copy (build fix/l47-decide), host DuoQACivHost / Abber (instance 9; `.duo-seats` byte 4 patched 0x10 -> 4 in a backed-up folder, every QA save folder restored as found afterwards), guest DuoQACivGuest (instance 10), 2026-11-21 09:33-09:47. Logs kept in the run's scratch; screenshots in out/qa-shots/ of the worktree (not in git).
Both cameras on the host's capital, `coop.settings force civ.superweapon_vote 0`, target 75 -410 -285 (open ground
beside it), shots of host and guest one after another (~0.5 s each).
- FAIL (`coop.fix civ.sw_show off` on the host): host's own type 3: the host's screen the marker and the green beam, the
  guest's nothing in 4 shots (out/qa-shots/bug9312_fail_off_montage_host_left_guest_right.png); `sw_show sent=0`, guest
  `in=0`.
- PASS host-fired (on): type 9: the guest's screen the projectile leaving the host's city hall and the impact flash
  (out/qa-shots/bug9312_pass_host_sw9_montage_host_left_guest_right.png); guest "superweapon 9 of nation 800021CA shown
  ... 1 of 1 tool shot(s) fired".
- PASS guest-fired (on): seat 4's type 3: the guest's screen the target marker (cross and rings) and the green burst as
  on the host (out/qa-shots/bug9312_pass_guest_sw3_montage_host_left_guest_right.png); guest dropped 8 effects of that
  nation; both games' city hp equal afterwards.
- PASS global types (BUG-9313 runs): the host's type 8: two blue beams leave the host's capital on both screens
  (out/qa-shots/bug9313_pass_host_sw8_montage_host_left_guest_right.png); the host's type 2 (fix off run): 3 aims shown,
  29 effects dropped on the guest, the captured cities' hp and ruins equal on both games afterwards.
- No crash report on either game.

## History
- 2026-11-21: opened.
- 2026-11-21: found while testing BUG-9310 (not fixed; a visual relay of the launch is new work).
- 2026-11-21: fixed on fix/l47-decide (civ.sw_show: the host relays the launch, a guest fires the same tool without damage), live FAIL / PASS on TITAN.
