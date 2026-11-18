---
id: BUG-9120
title: Spore's Settings dialog vanishes on Audio (and every other tab) with the Lineages graphics controls
status: fixed
kind: bug
stage: all
reported: 2026-11-11
source: owner report on 0.7.8-lineages48-dev
role: local
versions: 0.7.3.6-lineages41-dev to 0.7.8-lineages48-dev
fixed_in: 0.7.8-lineages49-dev
branch: feat/lineages-47
commits:
tests: QA host 9 (DuoQACreatureHost), galaxy menu -> Options -> Settings, every tab, coop.fix ui.settings_tabs on/off, see Verification
signature: Settings dialog anchor (id 0) hidden (fl=0) after a tab click; coop.ui where <page id> = none
docs: docs/06-testing.md#test-settings-tabs
related: BUG-9102
switch: ui.settings_tabs
---

## Report
"The Audio Settings page and possibly others disappear when in the options menu."

## Root cause
The Lineages Frame Rate Cap / Borderless controls (BUG-9102, coop_ui.cpp refresh_graphics_options)
are children of Spore's Settings window 0x043DE590, next to its tab buttons. A tab click goes
through that window's buttons and shows or hides the window whose id is each button's command id
(the tab buttons' commands are their pages: 0x0437A968 Graphic, 0x0437A960 Audio, 0x046C1800 Game &
Capture, 0x0679E383 Control, 0x0437A978 Online, 0x0463F008 Credits). Our label and button windows
(both IButton) had command 0: the lookup found the dialog's own frame (id 0, the window above the
Settings window) and hid it, so the whole dialog vanished on any tab but Graphic. The background
stayed dimmed (the modal layer).

Bisected on a QA game (one control at a time): the label, the hint label or the Borderless button
alone break it; the text field (ITextEdit) and our message handlers do not.

## Fix
Each of our controls gets its own id as its command id (C00F0110..C00F0113): the tab lookup finds
the control itself and hides it on the other tabs. The tab switch also disables it; the refresh
enables it again on the Graphic tab. `coop.fix ui.settings_tabs off`: command 0 (the bug).

## Verification
QA host 9, galaxy menu -> Options -> Settings:
* fix on: Audio, Game & Capture, Online, Credits each shown (`coop.ui where <page>`), our controls
  hidden there; back on Graphic our controls shown and enabled; Borderless toggles On -> Off with the
  Graphic page still shown; Cancel closes the dialog.
* fix off: "audio: 0437A960 none" (the dialog gone), as reported.
* Without coop.dll at all: Audio shown (the game's own behaviour).

## History
- 2026-11-11: reported by the owner, fixed on feat/lineages-47.
