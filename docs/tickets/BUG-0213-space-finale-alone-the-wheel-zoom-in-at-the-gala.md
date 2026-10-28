---
id: BUG-0213
title: space_finale_alone: the wheel zoom-in at the galactic core is one wheel short (test timing)
status: fixed
kind: test
stage: Space
reported: 2026-10-28
source: lab runs 20261028-055128, 20261028-062902 on main e5fe623
role: host
versions: 0.7.2.4 (main e5fe623)
fixed_in: 0.7.3.0
branch: fix/rel-finale
commits: 138a9a7
tests: space_finale_alone, space_finale
signature:
docs: docs/06-testing.md (6.17)
related: BUG-0182
---

## Report
Before 0.7.3.0 `space_finale_alone` failed 3 of 4 runs on main e5fe623 with "FAIL host: zoomed into the
core's system" (out/lab/20261028-055128, out/lab/20261028-062902 #1 and #2); it passed on main 1d4cd27
(out/lab/20261028-043811) and in #3 of 20261028-062902. Everything after the check passed, and the
status line printed a few seconds after the FAIL already said ctx=1 (the host was in the core's system).

## Root cause
Test timing, not the mod. Observed:
* Between the passing 1d4cd27 and the failing e5fe623 the mod changed only in coop.cpp (the
  `core.strays` switch, creature stage) and game.cpp (a log line): nothing in space, the camera or
  input; `wheel` (sporekit/commands.cpp) and the test file are unchanged since 0.7.
* The test zooms out of the home system with one wheel of -5 notches (all runs), flies to the gate
  (`finale goto core`, SpaceTeleportTo) and then wheeled in 3 notches every 1.5 s, at most 10 times.
  Counted on lanes 4-7 (out/lab/20261028-063645-fix-rel-finale, out/lab/20261028-063959-fix-rel-finale):
  the core's system opened after 10, 10, 11, 9 and 10 wheels (and 10 in each of the 4 verification
  runs). So the cap sat exactly on the number of
  notches the game needs; one wheel more (the system opens a few seconds later) failed the check.
* In the failing main runs the host's "waits for your partners" came ~10 s later after the guest's
  approach than in the passing ones (52-54 s against 42-43 s), consistent with one or two more wheels.
Inferred: the galaxy camera needs ~30 notches in from where the system -> galaxy switch leaves it,
and the wheel steps under load (6 lanes) do not always add up to the same zoom (easing per frame).
Players are not hit: the zoom-in works, it just takes about 30 notches of the wheel.
Not BUG-0182 (that one is the landing on a planet after a load on GOG 2024).

## Fix
tools/tests/coop_space_finale.py: the host keeps wheeling in (3 notches every 1.5 s) until the system
opens, up to 40 times, then waits up to 10 s for the switch; it prints how many wheels it took (and
the zoom-out count). The check is the same: the host's own wheel has to open the core's system.

## Verification
* FAIL before: out/lab/20261028-055128, out/lab/20261028-062902 (#1, #2) on main e5fe623.
* PASS after: out/lab/20261028-064627-fix-rel-finale, `space_finale_alone --repeat 4 --retry 0` on lanes 4
  and 5: 4/4 OK (10 wheels in each).

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/rel-finale): cause found (the 10-wheel cap equals the notches the zoom-in needs), test wheels up to 40 times; 4/4 OK in out/lab/20261028-064627-fix-rel-finale.
- 2026-10-28: released in 0.7.3.0.
