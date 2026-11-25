---
id: BUG-9340
title: Galaxy: a planet details panel beside Spore's Choose A Stage screen (players and host per stage)
status: fixed
kind: feature
stage: menu
reported: 2026-11-22
source: roadmap 8M A (owner 2026-11-22)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 4fed29a5
tests: tools/qa_win (manual, 2026-11-22): TITAN's test galaxy, QA 9 on Abber's Choose A Stage screen by real clicks, QA 8 hosting Abber (Creature); coop.fix galaxy.planet_panel off/on
signature:
docs: docs/12-server-and-players.md#server-planet-details-panel
related: BUG-9337, BUG-9339
---

## Report
Decision 8M A (the owner, on the mockup images/mockup_planet_details.png): Spore's Choose A Stage screen (BUG-9337)
lost the players per stage that our box showed; a panel beside it, "similar to party player name panel", shows the
planet's details.

## Root cause
Not built yet (a feature).

## Fix
coop_ui.cpp (the planet details panel), coop.cpp planet_panel_tick, galaxy_menu::stage_screen. While the screen is open
for a server planet: a panel right of its stage buttons, centred on them, in the Social Menu's look (the Co-op window's
native frame, now shared: frame_build / frame_layout). The planet's name, "Online world - N players here, M friends";
a row per stage with Spore's own stage icon (the screen's button's drawable, shared: its reference count goes 1 -> 2),
its players and host from the server's planet and players lists, "New here: start it" (BUG-9336), grey "Not on this
planet" / "not reached yet"; the stage under the pointer (a circle test on the button's drawn rectangle) lit gold with
its players' names; friends on the planet; "Saved on the galaxy server". It goes at once with the screen (a press,
Back, Esc, any menu state switch: galaxy_menu::on_screen_gone), the icons' drawables dropped first. With the Social
Menu open it moves under it, never left over Spore's planet. It takes no clicks.
Switch `coop.fix galaxy.planet_panel off`: no panel, the screen alone (BUG-9337 as it was).
Not as in the mockup: "Last saved 4 min ago" (the planet list holds no save time; "Saved on the galaxy server").

## Verification
TITAN, test galaxy (out/galaxy/worlds_conn, a copy of worlds_9337), QA 9 (1024x768) by real clicks: star, Play:
- PASS (on): the panel right of the stage buttons: ABBER, Cell grey "Not on this planet", Creature "Nobody here - saved",
  Tribe gold "New here: start it", Civilization "Nobody here - saved", Space grey; Spore's icons in the rows
  (images/planet_panel_empty.png). The pointer on the paw: the Creature row lit gold, Spore's own "Creature Stage" card.
  QA 8 entered Abber (Creature) and hosted it: "1 player - host PanelHost", hovered: "PanelHost (host)"; added as a friend
  through the Social Menu's right-click: "1 player here, 1 friend", "Friends here 1: PanelHost, Creature"; with the
  Social Menu open the panel stood under it (images/planet_panel.png).
- Esc closed the screen and the panel at once, the planets rebuilt (BUG-9337's untested Esc, now seen); the globe press
  closed both and loaded Abber (Civ) as its host; Back (X) pressed with the panel open twice, no crash.
- FAIL side (`coop.fix galaxy.planet_panel off`): the screen alone, no panel (images/planet_panel_off.png).
- Found on the way: the first placement rule moved the panel left of the Social Menu, over Spore's planet, whose picture
  drew over it (fixed before commit: under the Social Menu, as low as the screen allows).
- QA 8 crashed during the run (BUG-9341, era visitors' bake in Creature): not this panel's code (QA 8 was in a stage, the
  panel ran only on QA 9's menu).
no-fail-run: a feature, Windows QA by hand; the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
