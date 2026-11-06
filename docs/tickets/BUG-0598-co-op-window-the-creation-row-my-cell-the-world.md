---
id: BUG-0598
title: Co-op window: the creation row 'My cell: the world's own' has no arrows and no hint how to get a choice
status: fixed
kind: bug
stage: Creature
reported: 2026-11-05
source: player question and owner's manual test 2026-11-05 (0.7.7)
role: guest
versions: 0.7.7
fixed_in: 0.7.8
branch: fix/w44-coop-window
commits: afebdb63
tests: ui_explain (coop_ui_explain.py)
signature:
docs: docs/08-ui.md
related: BUG-0597
---

## Report
A player asked where the co-op window is and where to pick the own creation. The owner (0.7.7) did not see the
choice: the guest profile had no own cells, the row said "My cell: the world's own" with no arrows and no hint.

## Root cause
Observed (code): refresh_lobby shows the arrows only with own creations of the kind (`cown > 0`) and the name
"the world's own" for an empty name; nothing says that a creation made in an editor gives the choice, nor whose
"the world's own" is.

## Fix
`core.ui_creation_hint`: an empty name reads "the host's species" on a guest, "your world's species" on the host,
"your empire's ship" for a ship; a guest without own creations of the kind gets " (make one in an editor to pick it)"
("(создайте своё в редакторе)"), and the row takes the arrows' room (up to x 720) then. Off: "the world's own".

## Verification
Lab: ui_explain FAIL with `SPORE_FIX_OFF=core.ui_share_explain,core.ui_creation_hint` out/lab/20261105-095220
("My creature: the world's own"), PASS out/lab/20261105-095343 ("My creature: the host's species (make one in an
editor to pick it)", no arrows). Layout: out/lane6/shots/ui_explain_creation.png and _ru.png (the first Russian text,
66 letters, wrapped onto the chat line: shortened).

## History
- 2026-11-05: opened.
- 2026-11-05: fixed on fix/w44-coop-window (core.ui_creation_hint).
- 2026-11-05: released in 0.7.8.
