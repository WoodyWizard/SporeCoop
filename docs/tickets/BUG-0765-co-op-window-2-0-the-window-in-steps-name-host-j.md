---
id: BUG-0765
title: Co-op window 2.0: the window in steps (name, Host / Join, lobby with the session settings), Report a problem in the pause menu
status: fixed
kind: feature
stage: All
reported: 2026-11-08
source: Discord suggestions 2026-10-31..11-04 (Restyle coop menu), docs/plan-0.7.12.md F1
role: all
versions: 0.7.11
fixed_in: unreleased
branch: feat/w46-coop-window
commits: ea0f5a5b, d07c2f08, 2294d3df, 245f4017
tests: ui, ui_status, tod_early, ui_explain, window_layout
signature:
docs: docs/08-ui.md#ui-window-steps, docs/06-testing.md#testing-ui-steps
related: BUG-0762, docs/plan-0.7.12.md F1 F3 F4
---

## Report
Discord suggestion "Restyle coop menu" (forum "suggestions", 2026-10-31..11-04): a co-op window in steps (name, Host /
Join, lobby), the session settings in the lobby, the bug report in the pause menu. Plan 0.7.12 F1 (approved by the owner
2026-11-08: the player types the name, no Steam / GOG profile name).

## Root cause
Not a bug: the 0.7.11 window had one connect page with every field at once (name, address, the LAN list, the port,
Host and Join), the session settings of BUG-0762 had no place in the UI (console only), and "Report a problem" was
only in the co-op window.

## Fix
coop_ui.cpp: the connect page is split into steps (name at the first start, Host or Join, Host: port, Join: the LAN list
and an address or host name with the port 47810 by default); the lobby gets a colour column (F2 fills
`PlayerRow::color`), the host's address line, a Settings page listing settings_core.h by group (the host edits through
settings::set, guests read-only, session rows locked once the host plays, live rows editable in play), "Close
session", and the host's "Ask the players for their reports" (`Api::ask_reports` -> coop.cpp
`ask_players_for_reports`, a stub for F4). A "Report a problem" button is added to the game's pause menu (window
0x064C23F8; its buttons are anchored to the bottom / right edge, docs/08-ui.md#ui-window-steps). Switches:
`coop.fix core.ui_name_first off` (no name step), `coop.fix core.ui_pause_report off` (no pause menu item).

## Verification
`coop_ui.py` (suite `ui`, lane 4): FAIL with `SPORE_FIX_OFF=core.ui_name_first,core.ui_pause_report`
(out/lab/20261108-160505: no name step, the names stay "Player"), PASS with the switches on (out/lab/20261108-160811,
also 20261108-155232). The other window tests on lanes 4-5 (out/lab/20261108-155445): ui_explain, ui_menu_leave,
tod_early, window_layout, lan_dedup, menu_escape, space_guest_ui, vote_caption, chat_caption, chat_modal, chat_pause,
chat_held_send, report_shot, save_box, creature OK; ui_status failed once (other lanes' hosts filled the three LAN rows
before the made-up games, a test race; the wait now needs "More >") and passed in out/lab/20261108-160345.
The pause menu item was seen in Cell, Creature, Tribe, Civilization and Space (lane 5 probe, the same window, the
last of nine buttons at a 36 px pitch).

## History
- 2026-11-08: opened; implemented on feat/w46-coop-window (F1 of 0.7.12).
