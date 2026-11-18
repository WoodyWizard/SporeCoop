---
id: BUG-9156
title: Downed: the death pose and spectating a partner
status: open
kind: feature
stage: creature
reported: 2026-11-12
source: owner
role: both
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual two-PC run 2026-11-12, coop.giveup down, coop.track)
signature:
docs: docs/re/creature.md#creature-downed-pose-spectate
related: BUG-9141 BUG-9146 BUG-9118
switch: creature.downed_pose, creature.spectate
---

## Report
Owner decisions 1V and 1W (2026-11-12): "When a creature is downed, can we apply the dying animation to them so it's clear they're downed" and "While downed can we allow spectating with a clear HUD spectating? ... Tab works, but also a left and right arrow next to the name you're spectating." Before, a downed creature stood still and its player saw only the ring.

## Root cause
Design. Findings: play_death would set mbDead (puppets hide on PF_DEAD); the anchor hold teleported every frame, which restarts animations; the idle refresh 0xC148B0 replays an idle once an animation ends. The follow camera starts each update from the viewer's last pose, so a moved camera drifts unless it is put back first. Turning the camera rows split the picture (the planet and sky kept the old angle); a slide of both the viewer row and the render camera's frame (viewer +0x170) keeps the picture whole.

## Fix
creature.downed_pose (on): the own downed creature plays the game's death animation 0xF14FAA46 with play_animation (no Die), its idle refresh is held (game::hold_pose), the anchor hold uses place; partners do the same with its puppet on PF_DOWNED (modes.cpp puppet_downed). creature.spectate (on): while downed in Creature or Adventure, the camera slides to the followed partner after the game's camera update (the camera update runs with the partner's puppet as mpAvatar, game::run_as_avatar), "SPECTATING <name>" with arrow buttons under the ring, Tab or the arrows switch partners, a red screen edge (ring_timer::vignette). No native red vignette was found.

## Verification
no-fail-run (feature, manual two-PC run). 2026-11-12, TITAN host + MSI guest on Samuth.spo (Creature): `coop.giveup down` on the MSI 99-107 m from the host: the MSI's creature played F14FAA46 and lay on its nest (coop.track; screenshot with spectating off); spectating: the host's puppet at the screen centre (640, 562 of 1280 x 960), SPECTATING Host with the arrows and the red edge, 0 faults. Within 4 m the host revived it at once (as before). Not seen: the puppet's pose on the host's screen (off screen there), Tab and the arrows with two or more partners, Cell / Tribe / Space (not built there). Fixed the same day: the ground under a far partner drew faintly while the camera was moved after its update; the camera update now runs with the partner's puppet as mpAvatar (game::run_as_avatar): solid ground 95 m away, two games on the MSI (TITAN could not start games that evening).

## The downed screen (owner's mock-up, 2026-11-12)
The owner sent a mock-up: over the ability bar a dark box "SPECTATING" with the partner's name between two yellow arrows,
a red GIVE UP button under it, the revive ring under that, the red screen edge; nothing at the top. Built
(coop.cpp draw_downed_hud, ring_timer::panel): the GIVE UP button gives up as holding G does (G still works), the
arrows and Tab switch partners, the box is left out when there is nobody to watch (only GIVE UP and the ring), and the
"Hold G to give up" line in the status block is left out while the button shows. Live on the MSI: the box, the button
and the ring as in the mock-up, 0 faults.

The owner then asked for the look of the Co-op window ("The red button doesn't match our button on the co-op menu",
"the backdrop of the spectating box, should be similar to menus too. Transparency at like 60%"). The box, the arrows
and GIVE UP are now the game's own windows (coop_ui::set_downed_hud, made with the Co-op window's button and font):
the box is the Co-op window's panel colour (read from the window, #203A4A) at 40% opacity with its inner boxes'
#7891A0 edge, the arrows are its gold buttons, GIVE UP is shaded as its Leave button with a white caption on top.
The D3D drawing stays as the fallback when the native windows cannot be made; the ring stays D3D. Live on the MSI
(two games, Samuth.spo, 2026-11-12): side by side with the Co-op window's Leave and arrow buttons, 0 faults. First
build: the box drew over its own arrows and the button over its caption (see docs/08-ui.md, Stacking); fixed by
bringing the front-most window forward first.

Then (the owner): "make sure its rounded, and is consistent spacing / size? The give up button should be the same
size as the spectating rectangle." The box is now data/ui/lineages_downed_panel.png (tools/make_icons.py: the same
colour, 40% opacity and edge, corner radius 10) drawn cut in thirds like Spore's tooltip, so the corners keep their
size at any width; the old square box of filled labels stays for a package without the image. GIVE UP is as wide as
the box (max(220, 17% of the screen width)), 8 px between box, button and the ring's dark disc, 10 px padding inside
the box. Live on the MSI (both games restarted for the new package): rounded, same widths, even gaps, 0 faults.

## History
- 2026-11-12: opened.
