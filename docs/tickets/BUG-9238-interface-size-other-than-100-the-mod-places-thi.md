---
id: BUG-9238
title: Interface Size other than 100 %: health bars over creatures off their creature; the mod placed things by Spore's layout places
status: fixed
kind: bug
stage: creature
reported: 2026-11-16
source: owner play test 2026-11-16
role: both
versions: 0.7.14.1-lineages60-dev
fixed_in: 0.7.14.1-lineages61-dev
branch: feat/lineages-47
commits: 7ea6c4ca
tests: manual Windows QA: TITAN host + guest and the MSI guest at 125 % (Dorger.spo), screenshots A/B by the switches
signature:
docs: docs/re/graphics.md#graphics-world-bars-layer
related: BUG-9191
---

## Report
Owner 2026-11-16: a guest with the Interface Size at 125 % (or anything but 100 %) saw text, health bars and other overlay things off center, not where they belong.

## Root cause
Observed (the MSI guest at 125 %, 1280 x 960, coop.ui path): the health bar over a creature (holder 0x93310E4A with the bar 0x026E0DF0 'health' / 'hurt' / 'none' and its number 0x04D039A8) is placed by the world's projection in real screen pixels, in a layer of its own (0xE9F70DF9). Decision 3X A scales every layer but the hints' (0xCBDF6E1D) by f about the top left, so the bar showed at 1.25 x its place: 160 px right and 90 px down of the creature at the screen's middle.
Observed (TITAN host at 125 %): the mod's click on the game saved box's OK used sui::screen_rect (the layout place, 1024 x 768 space); the click is then mapped like a player's (ns_remap) and missed. The box stayed up and the game stayed paused for everyone ("Paused by host") after every world share.
Inferred from the code: the progress debt tint (progress_debt.cpp) and the label layout's HUD boxes (find.cpp) read the same layout places for drawing in screen pixels.

## Fix
- The health bars' layer 0xE9F70DF9 is a world layer like the hints': not scaled, and kept at the whole screen (one made smaller before is put back). Switch ui.world_bars.
- sui::drawn_rect(): a Spore window's rectangle as drawn (its drawn transform, Window +0x148); the saved box click, the progress debt tint and the label layout use it. Switch ui.drawn_rect.
- coop.ui path <win>: a window and its parents (tests).

## Verification
FAIL (the MSI guest, 125 %, coop.fix ui.world_bars off): the bar '1' drawn at about (800, 445), the creature at x 640 (out/shots/r63/msi_bars_off.png; before the fix msi_sel125.png / msi_sel2.png the same).
PASS (on): the bar '2' at (640, 355), over the creature (msi_bars_fix.png).
FAIL (the old build, TITAN host at 125 % with a guest joining): the saved box stayed up (flag 0x43), the guest showed "Paused by host" (out/shots/r63/guest_125.png, host_125.png).
PASS (the new build): the box confirmed (flag 0x42) and the guest played on (guest_fix.png).
no-fail-run: Windows QA by hand (screenshots); no lab test on the Linux lanes for the Interface Size. Other stages' bars over units (Tribe, Civ, Cell's health ring) not checked yet.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed (health bars layer, drawn places).
- 2026-11-16: released in 0.7.14.1-lineages61-dev.
