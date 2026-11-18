# Party card layers (decision 3G)

Every image the Party cards draw, per stage (`cell`, `creature`, `tribe`, `civ`, `space`) and shared
(`shared`: bars and icons). `1x` is the size in the game (at Interface Size 100 %); `4x` is the same image
drawn four times as large, for editing. Drop finished 1x PNGs (same names, same sizes, transparent
background) back in this folder and they go into the game as they are.

Made by `tools/party_card_assets.py` (`--layers <folder>` writes this set again).

## One card (1x pixels, from the card's top-left corner)

| Layer | File | Place (x, y) | Size |
|---|---|---|---|
| Plate | `<stage>/plate.png` | 0, 2 | 128 x 67 |
| Picture frame | `<stage>/frame.png` (`frame_hover.png` while the pointer is on it: lit, 3 px larger each way) | 124, 0 (over the plate's right end) | 71 x 71 |
| Creature picture (the game's) | - | 130, 6 | 59 x 59 |
| Name (text) | - | 9, 9 (its letters' tops 8 px under the plate's top) | to 18 px tall |
| Place (text) | - | 9, 27 | to 14 px tall |
| Health icon | `shared/icon_health.png` | 7, 40 | 12 x 12 |
| Health bar | `shared/health_bar_back.png`, `health_bar_fill.png` (cut at the health), `health_bar_ring.png` on top | 22, 42 | 97 x 8 |
| Second icon | `shared/icon_hunger.png` (Creature), `icon_energy.png` (Space) | 7, 52 | 12 x 12 |
| Second bar | `health_bar_back.png`, `bar_fill_orange.png` / `bar_fill_blue.png`, `health_bar_ring.png` | 22, 54 | 97 x 8 |

The whole card is 195 x 71. Inside the plate every edge and row is 6 to 8 px apart (measured on screenshots). The images stretch: the corners stay, the middle is stretched, so a plate
or tab of another size still works (the game cuts each image in thirds).

## The stack

* Cards 6 px apart, the stack 4 px from the screen's right edge, its bottom 6 px above the game's own
  UI there (the avatar or ship card, the Pack list, the Tribe and Military tabs).
* The tab: `<stage>/tab.png`, 92 x 30, right-aligned, 6 px above the top card; "Party" written on it, its letters 6 px from the tab's top and bottom.
* The tab takes the art of your own stage; each card the art of that partner's stage.

## Text (drawn by the game, not images)

| Text | Font | Colour |
|---|---|---|
| Name | the game's body font (bold, about 15 px) | white (Cell: dark blue `#182244`) |
| Place | the game's button font (about 11 px) | Space `#96CDFF`, Cell `#2A3E8A`, others gold `#E8C870` |
| "Party" | the body font | white |
