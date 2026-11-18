---
id: BUG-9097
title: Healthy partners have no visible off-screen direction indicator
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: Player feedback accompanying session report
role: host guest
versions: 0.7.3.6-lineages27-dev
fixed_in: unreleased
branch: fix/lineages-player-session
commits: 75b3901b
tests: tools/tests/lineages_cell_report.py
signature:
docs: docs/re/cell.md#cell-lineages-player-report
related: BUG-9096
---

## Report and cause

The player cannot easily locate their partner and requests a larger arrow.
Healthy player tags are projected as text without screen-edge clamping. Only
knocked-out cells and temporary marks have off-screen arrows. Add a persistent
direction indicator with name/distance for a live partner in the same place;
do not point at stale states or players in another stage/world.

## Fix and verification

Large filled arrows with name/distance for fresh partners in the same place.
Uses SK_EV_OVERLAY and a restored D3D9 state block. Cell KO markers retain their
red rescue label; healthy markers do not overlap it. Guard: ui.partner_arrows.

## Verification

Native on-screen and off-screen screenshots in
out/player-report-20261107-223741/native. The native-camera.log test placed the
guest 60 units away and verified a marker; screenshots were visually inspected.
no-fail-run: requested visual enhancement; prior code had only name tags.
Full gameplay positioning in every later stage and a 3D behind-camera bearing
compass were not qualified. See docs/re/cell.md#cell-lineages-player-report.
