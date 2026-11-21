---
id: BUG-9316
title: Civ (host): after a guest's world reload the host logs a large shared-money spend from that guest (seen after coop.civ swgrant money writes)
status: open
kind: bug
stage: Civ
reported: 2026-11-21
source: live QA 2026-11-21 (TITAN, fix/l47-decide, Abber)
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-money
commits: 8692238f
tests: manual two-game live test (docs/06-testing.md#testing-reload-spend-sw-price); tests/npc_tests.cpp (stage_epoch::is_open); no lab test yet
signature:
docs: docs/re/civ.md#civ-reload-spend-sw-price
related: BUG-9309, BUG-9310, BUG-0220, BUG-0692, BUG-0231
---

## Report
Live QA 2026-11-21 (fix/l47-decide, TITAN, Abber with a fifth founder nation for the guest, BUG-9309). After each world
reload of the guest (a share after a capture, 20-40 s), the host logged `shared money -61900.0 from player 1` /
`CivGuest spent 61900 money (shared)` (09:44:04) and later -45880 (09:45:20); the shared money fell by that much on both
games. Before them the test had written money into nations directly (`coop.civ swgrant <seat> <type> <money>`, +0x98 of
the host's nation and of seat 4's) and fired global superweapons (seat 4's cost 48000 from its own money).
Reproduce (not tried again): the BUG-9309 newcomer setup, `coop.civ swgrant 4 2 100000`, the guest fires type 2, the
capture brings a world share; watch the host log after the guest's reload.

## Root cause
A real bug, not a test artefact (no swgrant needed). Observed at runtime (TITAN, logs with the new diagnostics):
a guest's world reload (a world share: Civilization -> Galaxy menu 02CCD1D2 -> Loading -> Civilization) kept its
shared-money sync state, because game::stage_epoch(kCiv) only moves when Civilization is entered again. A host total
(MSG_RES, at least every 2 s) that arrived in a Galaxy menu frame was folded (BUG-0220's `fold`) against the money
GetPlayerCivilization +0x98 read there (0, 320, 840 in three runs: the old world being torn down): "that value minus the
last total" went to the host as the guest's own spend, and the host's next total took it on every game. In the original
QA log the first one (-2660 at 09:39) came before any `coop.civ swgrant`. The same code (mods/coop/shared.cpp) keeps the
Tribe food and the Space money: the same bug and fix there (not run live in those stages).

## Fix
`res.load_hold` (mods/coop/shared.cpp, on by default; `coop.fix res.load_hold off` = before): while the stage's world is
not open (game::stage_open: false during a load and in the menus; an editor keeps it open, so BUG-0692 is unchanged), a
guest drops its sync state and ignores host totals; unsent and in-flight changes are kept and the first total back in the
stage is taken as after any load. game::stage_open / stage_epoch::is_open are new (portable, unit-tested). Diagnostics: a
guest logs every local change of 1000 or more (`shared money: a local change ...` with mode, stage open and epochs), and
each total ignored during a load; `coop.res` shows `load_totals=` and `big=`.

## Verification
Live, TITAN QA copy (build fix/l47-money), host DuoQACivHost / Abber (instance 9, `.duo-seats` byte 4 patched 0x10 -> 4,
restored byte for byte afterwards), guest DuoQACivGuest (instance 10), 2026-11-21 10:05-10:18. Run outputs and both logs:
out/qa-runs/20261121-l47-money/ of the worktree (not in git); screenshots out/qa-shots/bug9316_*.png.
- FAIL (`coop.fix res.load_hold off` on the guest), 3 of 3 reloads: the founding share 10:07:02 (guest "a local change
  -63565.0 (63565.0 -> 0.0) folded into a host total in mode 02CCD1D2, stage open=0", host "shared money -63565.0 from
  player 1", the shared money 64085 -> 520 on both games); `coop.share 1` 10:07:18 (-1040); `coop.share 1` 10:09:34
  (-72660: host HUD 72,120 -> 2,720, bug9316_fail_before_host.png / bug9316_fail_after_reload_host.png).
  run-fail-natural-1.txt, run-fail-natural-2.txt.
- PASS (on): 2 `coop.share 1` reloads (run-pass-natural-1.txt, -2.txt) and, in a second session, the join, the founding
  share and a capture share: the guest logs "a host total ... ignored while this game loads (mode 02CCD1D2)", the host's
  `coop.res` from_guests stays the same, no "from player 1" line, both games show the same money rising with income
  (bug9316_pass_after_reloads_*.png).
- tests/npc_tests.cpp `test_stage_epoch` (is_open over menu, load, editor) passes (built natively with llvm-mingw i686).
- No crash report on either game.

## History
- 2026-11-21: opened.
- 2026-11-21: analysed (a real bug: a reload's Galaxy menu frame, not swgrant), fixed on fix/l47-money (res.load_hold),
  live FAIL / PASS on TITAN.
