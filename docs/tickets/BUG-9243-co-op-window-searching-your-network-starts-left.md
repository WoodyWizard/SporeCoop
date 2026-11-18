---
id: BUG-9243
title: Co-op window: 'Searching your network...' starts left of its box and the update line wraps and sits on the host box
status: fixed
kind: bug
stage: all
reported: 2026-11-16
source: owner 2026-11-16 (62-dev)
role: host
versions: 0.7.14.1-lineages62-dev
fixed_in: 0.7.14.1-lineages63-dev
branch: feat/lineages-47
commits:
tests: manual Windows QA (TITAN, a screenshot)
signature:
docs:
related: BUG-9117
---

## Report
Owner 2026-11-16 (62-dev, the Co-op window before connecting): "These are not centered properly. Searching is outside the box, and the update is way too close to the host setup. Text shouldn't wrap here, its too small of a space." The screenshot: "Searching your network..." starts on the left border of the server list's first row; "Lineages is up to date" sits right under the host-address box.

## Root cause
coop_ui: the search line is a label at x 37 (the row starts at 35) and `fit` moves a left label's text to its box's left edge, so the text starts on the row's border. The status line (40, 535-558) starts 1-3 px under the host box (to about 534) and wraps a text longer than its 463 px onto a second line under it.

## Fix
- ui.connect_lines: the search line starts 10 px inside its row (45, 177-202); the status line sits at 547-571 (centred on Check for Updates) and a text longer than its line takes the button font instead of wrapping. `coop.fix ui.connect_lines off`: 62-dev's places.

## Verification
TITAN QA game 2026-11-16 (Co-op window at the galaxy menu, 1280x960, out/shots/r66/connect_window.png): "Lineages is up to date" level with Check for Updates and clear of the host box. Not seen: the search line (that profile lists saved servers, so it does not show) and a long status in the button font.

## History
- 2026-11-16: opened from the owner's screenshot; fixed in code (ui.connect_lines), not verified yet.
- 2026-11-16: released in 0.7.14.1-lineages63-dev.
