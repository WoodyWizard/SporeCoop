---
id: BUG-9178
title: UI: the downed box: arrows off-centre, a long name on two lines, square corners with an older package
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: all
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, 2026-11-13: coop.giveup down on a TITAN guest (1024x768) and the MSI guest (1280x960), screenshots)
signature:
docs: docs/06-testing.md#testing-qa-pitfalls-1113
related: BUG-9156 BUG-9171
---

## Report
Owner (2026-11-13): "The spectating background isn't rounded on the corners I noticed. The arrows inside the yellow buttons aren't centered either. We'll need to do a quality check." Also seen in the check: a 12-character name ("CreatureHost") broke in two lines over the title.

## Root cause
Observed. Corners: sui::image of lineages_downed_panel returned null on TITAN's QA games while the tooltip, icon and toast images loaded: those games read Data\SporeCoop.package of the installed game (the registry's DataDir), a package of 2026-11-11 without the image, not the QA copy's Data folder; the code then draws its square fallback. The MSI's package (2026-11-12) has it: rounded there. Arrows: a button copied from the Co-op window's (sui::button_like) keeps that button's caption offset and borders: "<" and ">" sat about 5 px right of the middle. Name: the label between the arrows was 100 px wide at 800x600 (the box 220).

## Fix
coop_ui.cpp: the arrow buttons are empty and carry centred labels of their own (as GIVE UP), kept in front with it; a warning in the log says whether the game's package is an older one. coop.cpp draw_downed_hud: the box (and GIVE UP with it) grows with the name: about 11 px a character plus the arrows, 60% of the screen at most. tools/qa_win/msi.ps1 deploy copies build/SporeCoop.package too. No switch (layout).

## Verification
no-fail-run: layout, manual. Before: out/shots/fresh-box.png (TITAN: square, arrows right of the middle, the name in two lines). After: out/shots/downed-box-msi-vs-titan.png: the MSI rounded with centred arrows; TITAN centred arrows, "CreatureHost" on one line, GIVE UP as wide as the box, still square (the installed game's package is the owner's to update: a new installer or their OK). Not checked: Cell, Adventure, 1440x1080.

## History
- 2026-11-13: opened.
- 2026-11-13 (later): the owner: "The arrows look to be too low still. Can we maybe use the triangles? Make sure this is consistent." Every arrow button now shows a triangle of the Co-op window's font (U+25C4, U+25BA, U+25B2, U+25BC: the font has them) in the middle of the button: coop_ui.cpp add_arrow_button and sui::center_caption (no caption offset, even borders) for the downed box's two, the Co-op window's creation / give / space arrows and the chat's up / down; the arrows' overlay labels of the first fix are gone again. Seen: the MSI (out/shots/downed-box-final-msi.png: rounded, centred), TITAN's downed box and Co-op window (out/shots/coopwin.png). Found, not fixed: the Co-op window's player list breaks "CreatureHost (host)" in two lines (the name column is 130 px).
- 2026-11-13 (later): the owner: "Is the background overlay rendering above the text and buttons? It looks dark in that screenshot." Yes, by chance: one TITAN shot (10:15) had the title grey and the buttons olive (the box's 60% panel over them). The parts were siblings in the game's main window, each brought to the front in turn; their order came out by chance and the stage's UI reorders that window's children (BUG-9171). Now one root window holds the parts, made back to front (the box, its edges, the title, the name, the arrows, GIVE UP, its caption); only the root is kept in front (twice a second). Checked: three shots over 15 s on TITAN and one on the MSI: the title pure white, the buttons' bright yellow pixel count the same (597, 598, 598); a click on GIVE UP through the root works on both ("gave up"). Not checked: the arrows with two partners to switch between, Cell, Adventure.
- 2026-11-13 (later): with the owner's OK ("Yes") the installed game's Data\SporeCoop.package on TITAN was replaced by build/SporeCoop.package (the old one, 99378 bytes of 2026-11-11, kept in out/owner-install-backup/); the running QA games held the file open (closed first). After a restart: "ui: downed box background: image 221C5EE8 ..." and the rounded box on TITAN (out/shots/downed-box-final-titan.png).

