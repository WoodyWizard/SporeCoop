---
id: BUG-9148
title: co-op window: no Give To button
status: fixed
kind: feature
stage: all
reported: 2026-11-12
source: owner decision 2026-11-12 (3B)
role: all
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 99b846b6
tests: tools/qa_win (manual QA 2026-11-12)
signature:
docs:
related: BUG-9144 BUG-0075
switch: ui.give_button
---

## Report
Owner decision "3B": the Co-op window's "Give To <player>" button (selected villagers, vehicles, a city, Space cargo spice) goes. It was read as a sync button for late joiners.

## Root cause
A request.

## Fix
The gift row and its buttons are hidden (`coop.fix ui.give_button on`: as before). The Civ role row keeps showing in Civ. The gift code and its messages stay (nothing else changes).

## Verification
no-fail-run: layout. QA 2026-11-12: a guest's lobby in Tribe shows no gift row (`coop.uistate`: give visible 0).

## History
- 2026-11-12: opened.
