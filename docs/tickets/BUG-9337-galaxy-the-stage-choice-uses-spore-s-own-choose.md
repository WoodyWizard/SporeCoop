---
id: BUG-9337
title: Galaxy: the stage choice uses Spore's own Choose A Stage screen (8L A)
status: fixed
kind: feature
stage: all
reported: 2026-11-22
source: decision 8L A (owner 2026-11-22)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: b39d1047
tests: tools/qa_win (manual, 2026-11-22): TITAN's test galaxy (Samuth Creature only, Abber Creature + Civ), QA 9 (every stage reached), real clicks on the star, Play, the stage buttons and the close button; coop.fix galaxy.native_stage_choice off/on
signature: 
docs: docs/12-server-and-players.md#server-native-stage-choice, docs/re/space.md#space-native-stage-choice
related: BUG-9335, BUG-9336, BUG-9338
---

## Report
Decision 8L (owner 2026-11-22: "Why did we create a new menu instead of using the native planet / galaxy menu?"),
answered "8LA": the galaxy's stage choice (BUG-9335, BUG-9336) should be Spore's own Choose A Stage screen (the paw,
fire, globe and rocket around the planet), not our "Choose a stage" box.

## Root cause
Not built yet (a feature). The box was the quick route: Spore's screen belongs to its new-game flow, which makes a new
planet and save.

## Fix
Play on a server planet that wants a stage choice (several eras, or the new Tribe stage of BUG-9336; none played yet)
no longer goes to the menu's Play state (0xC: the stage card and the save's load) but to its new-game state 4 with
that planet's entry, as the panel of an empty star does (hook on SporeCoop::GGE_SetState). The game opens its own
Choose A Stage screen for the planet. After the screen set its buttons (hook on SporeCoop::NewGameFlow_Open) the mod
enables the stages the planet offers and the player reached in its own Spore, and greys the others (Cell and stages
without an era). A stage press (hook on SporeCoop::NewGameFlow_OnButton) closes the screen as its Back does and enters
that era, or starts the new Tribe stage (on_stage_pick, the code of BUG-9335 and BUG-9336): never a new game. Closing
the screen (its X or Esc) returns to the planets; the menu's entries are rebuilt after the screen, which wrote into the
planet's entry. Lost against the box: the players per stage (the screen has no place for them). The test command
`coop.gmenu play <planet>` opens the same screen without a click. Switch `coop.fix galaxy.native_stage_choice off`: our
box, as before. Found on the way: BUG-9338 (the box opens on top of Spore's stage loading card).

## Verification
TITAN, the test galaxy (a fresh copy of the Samuth-Creature-only galaxy each run), QA 9 with every stage reached and
nothing remembered on Samuth or Abber:
PASS (`galaxy.native_stage_choice` on): a real click on Abber's star, then its Play: Spore's Choose A Stage screen
(images/native_stage_choice.png) with Creature, Tribe (new) and Civ lit, Cell and Space grey; a real click on its X:
back to the planets, the menu rebuilt, Abber's panel unchanged (Civilization Stage); Play again and a real click on
the globe: "Abber|Civ" entered as host and the Civ stage loaded (images/native_stage_choice_civ.png). On Samuth (the
test command's route): Creature and Tribe lit; a real click on the fire started the new Tribe stage (BUG-9336's
crossing, 4698 KB) up to the stage-end editor.
FAIL (`coop.fix galaxy.native_stage_choice off`, the old behaviour): Play on Abber opened our box
(images/native_stage_choice_off.png), Spore's screen not opened (opened 0).
Not run: Esc on the screen (the mod sees the menu leave state 4 and rebuilds, the same as the X), the MSI.
no-fail-run: Windows QA by hand (no lab on this PC); the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: built on feat/lineages-alpha (b39d1047) and seen in game through the real Play route to a loaded Civ
  stage; switch off: our box.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
