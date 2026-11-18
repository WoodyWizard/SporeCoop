---
id: BUG-9107
title: Co-op window: buttons have no tooltips
status: fixed
kind: feature
stage: All
reported: 2026-11-10
source: player request (roadmap 42)
role: all
versions: 0.7.3.6-lineages41-dev
fixed_in: 0.7.3.6-lineages42-dev
branch: feat/lineages-42
commits: 63abe7bf 08b969f4 91b64154
tests: QA hover screenshots, see Verification
signature: -
docs: -
related: BUG-9105
---

## Request

"Apply tooltips to the majority of the buttons in our menu: very short, a couple of words."

## Implementation (mods/coop/coop_ui.cpp, mouse.cpp)

- `tip_for(name)`: two to five words (English and Russian) for every co-op window button by its
  name (Connect, Lobby and Space pages, the eye buttons, server rows); the Space page's action
  buttons change meaning per row and have none.
- mouse.cpp keeps the last WM_MOUSEMOVE over the game window (client pixels, `mouse::last_move`).
- Look of Spore's own tooltips (sampled from the native "Mating Call [Backspace]" tooltip, window
  03754E6C): a UTFWin::StdDrawable (ctor 0x988350, 0x7C bytes) drawing a 50x20 pill image cut in
  thirds, and the tooltip font 025A6D0F (about 6.5 px a character at 800x600), placed just above
  the control. Ours: data/ui/lineages_tooltip.png (navy pill, light grey edge, slightly
  see-through) in a StdDrawable with the native layout values (`sui::std_drawable_tooltip`) as a
  button window, plus a label in font 025A6D0F, made at the first hover (after every page widget,
  so on top) and moved/shown outside Present. `coop.fix ui.tooltips off` hides them;
  `ui.tooltip_pill off` (tests) uses the stock button look.
- The native tooltip window is made per hover from a text id (0x510A95B, 835D13) and destroyed
  after, so it cannot be reused; its image object carries no resource key.
- First version drew a square box with the Arial overlay font: the player asked for the native look.

## Verification

no-fail-run: new feature (nothing to fail before it); checked by hand on the Windows QA copy below, no lab test yet

QA, second TITAN instance (LineagesDiff41Guest), Connect page at 800x600: Refresh "Find LAN
games", eye "Show or hide", Fast "Fastest world transfer", Start Server "Host a new session"
(near the right edge, kept on screen). 0 faults.

## History
- 2026-11-10: built on feat/lineages-42; QA above.
- 2026-11-10: native look; QA second TITAN instance with the QA package mounted (`coop.creations
  mount`): Find LAN games, Show or hide, Fastest world transfer, Host a new session, compared
  with the native tooltip (screenshot), 0 faults. Test pitfall: the QA copy loads SporeCoop.package
  from the registered Steam Data folder, so new images need `coop.creations mount <package>`
  before the first use (the game remembers a missing image).
- 2026-11-10: released in 0.7.3.6-lineages42-dev.
