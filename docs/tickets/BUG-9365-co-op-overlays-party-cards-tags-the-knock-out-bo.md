---
id: BUG-9365
title: Co-op overlays (party cards, tags, the knock-out box) are drawn over the History screen
status: fixed
kind: bug
stage: Cell
reported: 2026-11-24
source: deep playtest 2026-11-24
role: host
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 0d0214dc
tests: tools/qa_win (manual, TITAN host + MSI guest on a galaxy planet): History opened with a partner near, while knocked out
signature:
docs: docs/08-ui.md#ui-cover-history
related:
---

## Report
Deep playtest 2026-11-24 (Cell, a galaxy planet, a party of two): the host pressed the Advance button and was
knocked out by a predator in the same moment: Spore's History screen showed with the knock-out's red frame, the
SPECTATING box, GIVE UP, the revive mark, the partner's tag and arrow and the Party card on top of the timeline
(images/bug9365_history_off.png). Without a knock-out the partner's tag and the Party card were drawn over History
too (the direct session's Cell end).

## Root cause
Observed (code, `coop.ui where`): History is a full screen window of the stage's own mode; the overlay's cover
test knew the pause menu, the co-op window and the Spore Guide only. History's timeline body (control 0x54E504CD
under the container 0x8C509D6C) exists only while History is open (the container itself stays).

## Fix
coop.cpp: menu_check_task sets g_history_up from that control; History counts as a cover like the Spore Guide (no
tags, rings, arrows, marks), the knock-out box's windows are hidden under it (drawn again when it closes), the
Party cards are not shown and the notices wait. Switch `coop.fix ui.cover_history off`: all drawn over it.

## Verification
- FAIL (build ffd01832, 14:25): images/bug9365_history_off.png.
- PASS (the fix, 16:03, the Tribe stage's end with a party of two): History with no Party card and no tag
  (images/bug9365_history_on.png); the same at the Civ stage's end.
- Not run: a knock-out while History is open on the fixed build (the same g_history_up hides the box's windows).
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL is the picture of the build before.

## History
- 2026-11-24: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the deep playtest).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
