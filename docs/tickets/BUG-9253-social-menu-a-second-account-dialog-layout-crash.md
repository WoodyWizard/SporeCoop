---
id: BUG-9253
title: Social Menu: a second account dialog layout crashes in cWindowManager::PopModal
status: fixed
kind: crash
stage: Menu
reported: 2026-11-18
source: qa
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 557f6e29
tests: tools/qa_win/qa.py (manual: F8 open, the close button, galaxy menu and a joined world; no lab test yet)
signature: access violation executing SporeApp+0x1040868, stack UTFWin::cWindowManager::GetModal / PopModal / ProcessMessage
docs: docs/08-ui.md#ui-native-frame-copy
related:
---

## Report
Never released (a development build of the Social Menu, 6L A). Opening the Social Menu (F8) in the QA game crashed
it: crash-20261118-191003-i8, access violation executing SporeApp+0x1040868 inside
`UTFWin::cWindowManager::GetModal` / `PopModal` from `ProcessMessage`.

## Root cause
Observed: the build that crashed made the panel from a second instance of the account dialog layout (`0x34A6339E`,
`sui::load_layout`), the layout the Co-op window already uses. Inferred: the layout is a modal window of the window
manager; with two instances the manager's modal stack is popped for a window it does not hold.

## Fix
The panel is a plain window that reuses the Co-op window's frame art: new windows share the drawables of its glow,
13 frame pieces, panel colour and close button, laid out at the panel's size (`coop_ui.cpp` so_build / so_apply). No
second layout is loaded. No switch: the crashing build was never released.

## Verification
no-fail-run: the crash was in an unreleased development build; the fixed build opened and closed the Social Menu
(F8, the close button) in the galaxy menu and in a joined Creature world (QA 8 with the MSI) with no crash,
screenshots in the 2026-11-18 session.

## History
- 2026-11-18: opened and fixed on feat/lineages-alpha.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
