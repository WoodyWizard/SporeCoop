---
id: BUG-0909
title: UI: the lobby in the galaxy menu shows the Tribe / Civ hint 'Select your units to give them to a partner'
status: fixed
kind: bug
stage: UI
reported: 2026-11-10
source: docs/w47-readme-0712 (README shots, lane 2, 2026-11-10); F1 report
role: host
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in: unreleased
branch: docs/w47-readme-0712
commits: bc841b97
tests: ui (tools/tests/coop_ui.py)
signature:
docs: docs/08-ui.md#ui-window-steps
related: BUG-0765
---

## Report
The host's lobby in the galaxy menu, with a guest joined, shows the yellow line "Select your units to give them to
a partner" (a Tribe / Civilization hint; seen in the README picture `ui_lobby.jpg` and in the F1 agent's report).

## Root cause
Observed (code): coop.cpp `Api::give` returns a partner (`to`) whenever a session has one, and names what is given
(`what`) only in Tribe ("member"), Civilization ("city", "vehicle") and Space ("spice"). coop_ui.cpp refresh_lobby
showed the give row whenever there was a partner and no creation line: in the galaxy menu (and any mode without a
creation row) it read "Select your units to give them to a partner".

## Fix
The give row needs `what` (Tribe, Civilization, Space). Switch `coop.fix core.ui_give_hint off`: as before.

## Verification
FAIL: lab 20261110-203812 (`SPORE_FIX_OFF=core.ui_give_hint`, lane 2): "no Tribe / Civ give hint in the galaxy
menu ('Select your units to give them to a partner')", checked with the guest joined in the galaxy menu.
PASS: lab 20261110-204029 (lane 2).

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on docs/w47-readme-0712 (coordinator follow-up of the README pictures).
