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
branch:
commits:
tests:
signature:
docs: docs/re/civ.md#civ-partner-sw-tools
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
- 2026-11-21: found while testing BUG-9310 (not fixed; a visual relay of the launch is new work).
