---
id: BUG-9322
title: UI: the co-op notices are drawn over the Spore Guide window
status: fixed
kind: bug
stage: all
reported: 2026-11-21
source: QA 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-final-rings
commits: 30549e5b
tests: tools/qa_win (manual, two TITAN QA games, 2026-11-21)
signature:
docs: docs/08-ui.md#ui-spore-guide-window, docs/08-ui.md#ui-guide-notices
related: BUG-9319
---

## Report
Live QA 2026-11-21 (TITAN QA copy, Civ, guest screen): with the Spore Guide open (H) a co-op notice plate ("CivHost
earned 1040 money (shared)") was drawn over the guide's text (out/qa-shots/civ_bug9319_guide_PASS_switch_on.png, top
right). Reproduce: Civ with a partner earning shared money, press H.

## Root cause
Observed in the code: the notices (notify.cpp) are game windows with kFlagAlwaysInFront in the main window; BUG-9319
hides only the overlay's world labels while the guide is open, nothing moved the notice panels.

## Fix
menu_check_task reads the guide's frame once (the BUG-9319 check) and tells notify::set_covered; while covered, the
panels stay just off the right edge and their time stands still, so they slide back in for the rest of their time when
the guide closes. Switch `ui.guide_notices` (off: over the guide, 67-dev). `coop.status`: `notify: ... covered=
covered_frames=`.

## Verification
- Live 2026-11-21 (TITAN QA copy, 1024 x 768), the Spore Guide opened from the pause menu:
  - Space (host screen): FAIL with `coop.fix ui.guide_notices off` (a `coop.notify` plate over the guide:
    bug9322_space_guide_FAIL_switch_off.png), PASS on (no plate, covered=1, the panel kept at x 1034 and not expired:
    bug9322_space_guide_PASS_switch_on.png); the guide closed: the plate back in place
    (bug9322_space_guide_closed_notice_back.png).
  - Civ (guest screen, a real "CivHost earned 1000 money (shared)" plate and a test one): FAIL off, PASS on
    (bug9322_civ_guide_FAIL_off_vs_PASS_on.png); after the close both plates back (coop.status toast lines).
- no-fail-run: not a lab run (TITAN QA games by hand).

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-final-rings (30549e5b), FAIL / PASS live in Space and Civ.
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
