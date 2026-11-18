---
id: BUG-9117
title: co-op window looks unpolished (wrapping text, uneven rows, unframed difficulty, ping wraps, cluttered lobby)
status: fixed
kind: bug
stage: All
reported: 2026-11-11
source: owner review of 0.7.8-lineages47-dev
role: all
versions: 0.7.8-lineages47-dev
fixed_in: 0.7.8-lineages48-dev
branch: feat/lineages-47
commits:
tests: QA host 9 + guest 8 at 1440x1080: connect page, host and guest lobbies at the galaxy menu and in a Creature game, a pretend update (coop.update fake <version>), screenshots
signature: -
docs: docs/06-testing.md#test-coop-window-polish
related: BUG-9107
switch: ui.polish
---

## Report
"Seats 1-4 have more spacing than the fields; the 100% difficulty should be a bordered field like IP and
Port; at 3-digit ping the ping wraps; field labels should sit right next to the fields; 'Waiting for the
host's world...' is the same colour as and right next to 'Session Chat'; the update text is messy; text
clutters the lobby, especially for a connected player."

## Root cause
* The game's labels centre their text whatever alignment they are given (tried 0, 1, 2, 3, 4, 8, 0x10,
  0x22: all centred). The window sized "left aligned" labels with a rough guess (9.3 px a character +
  30): labels started at different places, and a box shorter than its text wrapped it (the ping column
  was 52 px: "103ms" became "103m / s").
* Rows: fields 26 px tall every 29 px, seat buttons 28 px, transfer buttons 30 px, at other offsets.
* The field frames are painted into lineages_connect_panels.png; the difficulty field had none.
* Lobby: chat lines 17 px apart in a ~19 px font (overlapping), the state line and the "Session Chat"
  heading in one 48 px gap in gold, "Select your units to give them to a partner" shown wherever a
  partner exists (galaxy menu, Cell, Creature), "Lineages is up to date" plus the version at the bottom of
  every lobby, the creation row against the chat box and its arrows touching Send.
* Update line: "SporeCoop 0.7.8-lineages48-dev is available (you have 0.7.8-lineages47-dev)" wrapped.

## Fix (`ui.polish`)
* Text widths measured on screen per character class (body font; the button font 0.70 of it), plus the
  label's inner margin (16 px a side): left labels start at their edge, right labels (field labels, the
  difficulty values, the ping) end at their edge. Field labels sit beside their fields.
* Host rows every 29 px, all 26 px tall; seat and transfer buttons span the field width.
* Difficulty: a framed field (panel image), "Difficulty" beside it, a table of names left and values right.
* Lobby: ping column 64 px, "103 ms" in the button font; no "Session Chat" heading, the state line centred
  on its own row; chat in the button font; the give line only with units to give; the update line only
  when there is something to do; even spacing under the chat box.
* Update lines name the build ("Lineages 48 is available", "Downloading Lineages 48... 42%",
  "Lineages 48 installed: restart Spore"); `coop.update fake <version>` shows the available state (tests).

## Verification
Screenshots 2026-11-11 (QA copy, package mounted with `coop.creations mount`): connect page, host and
guest lobbies at the galaxy menu and in Samuth's Creature game, the connect page and lobby with a pretend
update. No wrapped text, labels flush with their fields, 101-110 ms pings on one line.
Not checked: Russian text, other resolutions, the Space / Village / Nations pages.

## History
- 2026-11-11: reported and fixed on feat/lineages-47.
