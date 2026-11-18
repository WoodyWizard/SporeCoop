---
id: BUG-1260
title: Menu: a game that left the galaxy menu before its start-up video ended shows no main menu panel when it comes back to the galaxy menu
status: open
kind: bug
stage: Menu
reported: 2026-11-18
source: fix/w53-advorn: found while testing BUG-1254
role: both
versions: <=0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0626, BUG-1218, BUG-1254
---

## Report
Seen in the lab (lane 9, creature fixture, 2026-11-18): both games loaded the fixture before the galaxy menu's start-up
video ended (the lab's quick load; a real guest that joins from its menu at once does the same). After `coop.menu` (the
game's own quit) the galaxy menu came back without its main menu panel: no Galactic Adventures, Sporepedia or Co-op
entries, only the galaxy, the Spore button (Options) and Register (frames of lab 20261118-122432 adventure_tutorial,
out/lane9 menu1.png). Escape, mouse moves to the left edge and clicks on the galaxy did not bring it back; it stayed for
minutes. How to reproduce: start('creature'), `coop.menu` on the host, wait: `coop.ui find "Galactic Adventures"` lists
the entry with its parent panel 0x0615A5FF not visible (fl=0x2).

## Root cause
* Observed (log): "world: the galaxy menu intro would start outside the menu (mode 01654C01): not started (BUG-0626)":
  the video's end message 0x49731AD came while the game was already in the stage, the menu's GGE_intro was refused.
* Observed (runtime): `coop.menuintro` (tests) sends the video's end message 0x49731AD in the galaxy menu as StopMovie
  0xFD79C0 does (MessageSend(0x49731AD, null, 0)): the menu plays its intro and the panel is shown 10 s later.
* Inferred: the menu handler GalaxyGameEntry_IntroMessages (docs/re/space.md 7.9) shows its panel only at the end of its
  intro (state 6); a game that never got the video's end in the menu waits for it forever.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened (fix/w53-advorn, found while testing BUG-1254; the test works round it with `coop.menuintro`). Not fixed here: idea: remember a refused GGE_intro (BUG-0626) and, when the galaxy menu comes back, send the video's end there (or skip it as Escape does).
