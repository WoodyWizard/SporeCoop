---
id: BUG-9219
title: Co-op window: Settings in the World & Session panel, no Ask for Reports button
status: fixed
kind: feature
stage: menu
reported: 2026-11-15
source: owner 2026-11-15 (decision 4I A)
role: all
versions: 0.7.14.1-lineages56-dev
fixed_in: 0.7.14.1-lineages57-dev
branch: feat/lineages-47
commits: 6e8f678e
tests: manual Windows QA: the MSI, two games (host MSICivHost, guest MSIGuest), Abber.spo
signature:
docs:
related:
---

## Report
The owner (2026-11-15, decision 4I A): the Co-op window's Settings button "looks like it's floating" (top left above the panels); "What is Ask for Reports?" (upstream F4, the host asks every guest for its report; Report Issue asks them too).

## Root cause
Not a bug: upstream 0.7.12 placed F3's Settings top left and F4's Ask for Reports in the World & Session panel.

## Fix
- Settings is a row of the World & Session panel in Ask for Reports' place (host) or in the Ready / Create row while that is hidden (guest); hidden while a vote runs (no free row). The Ask for Reports button is not shown (switch ui.settings_in_panel).

## Verification
FAIL (switch off, MSI host window): Settings top left, Ask for Reports in the panel (out/shots/r57/host_window_off.png, not in git).
PASS (switch on, MSI host and guest windows, Abber.spo): Settings the panel's last row, no Ask for Reports.
no-fail-run: Windows QA by hand (no lab on this PC); a layout change, checked on screenshots.

## History
- 2026-11-15: opened.
- 2026-11-15: done and checked on the MSI.
