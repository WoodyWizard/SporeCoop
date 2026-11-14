---
id: BUG-9202
title: A joining partner sees the plain galaxy menu and a black screen while the host's world arrives and loads: no loading screen
status: fixed
kind: bug
stage: all
reported: 2026-11-14
source: qa
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: e9ec6026
tests: tools/qa_win (two-PC runs, TITAN + MSI: a frame sampler and a screenshot every 1-2 s through each join and reload)
signature:
docs: docs/06-testing.md#testing-loading-screens
related: BUG-9195 BUG-9071 BUG-9159 BUG-9170
---

## Report
Owner 2026-11-14 (decision 3S A): "please confirm all loading screens work and play when things are loading. Use both PCs". The two-PC check (TITAN hosting, the MSI joining) found: a joining partner saw the plain galaxy menu for about 20-25 s (the host saving and sending about 5-9 MB) and then a black screen (2-30 s), with no loading screen; the mod's own loading screen covered only a reload in the middle of a stage. Found on the way: (1) that reload screen and the new join screen lifted 2 s after the game reported the stage, while the game still showed black for up to 30 s (Tribe); (2) in a Space join (the personal launch) the screen stayed over the partner's playable Civ homeworld for 2+ minutes; (3) in Tribe at night the ring drew dark blue.

## Root cause
Observed (MSI and TITAN logs, a `frame` sampler every 0.4 s, screenshots every 1-2 s, 2026-11-14): (a) the join had no cover at all: `ui.reload_cover` starts only when a world arrives while a stage runs. (b) the game switches to the stage mode before its own black loading ends (Creature 2-10 s, Civ 7 s, Tribe 12-30 s at ~21 fps, then the frame rate jumps to 60+ when the world shows); the player's creature (g_local) exists and no cinematic runs (0.0 s) during that black, so neither is a usable signal; the frame's brightness is. (c) a guest on its personal Space intro (BUG-9071) holds the host's world as a queued load until it launches (`world::load_queued()` stays true), so the cover never saw the stage as settled. (d) the ring's draw state did not turn the game's fog off. Frames never stopped during any load (longest gap 0.8 s): the screen keeps turning while things load.

## Fix
ui.join_cover (on, coop.cpp on_overlay): the same loading screen covers a guest's join, from the moment the host plays a stage while the guest is in the galaxy menu or loading ("Joining <host>": "<host> is saving the world...", "Receiving <host>'s world..." with its share, "Loading...", "Final touches..."), 180 s at most, not while the Co-op window is open (a stuck join can still be left). Both covers now end when the stage is really there: the player's own object exists, no cinematic runs, no load is coming now (a load held for the personal Space intro does not count, world::local_intro_running) and the game's frame under the screen is no longer black (ring_timer::scene_luma: the blur's 1/16 copy read back; mean brightness >= 16, a dark place passes after 60 s), then 2 s. A world arriving during the personal intro starts no reload cover. The ring's draw state turns fog off (and colour write, clip planes, fill mode). The support log names how each cover ended ("ui: the join loading screen ended (playing) after ... s, ... s ready but black, brightness ...").

## Verification
Two PCs, 2026-11-14, 1024x768, not lab runs (no-fail-run: two-PC QA by hand; the FAIL and PASS runs are named by their tags, screenshots in the session scratchpad):
* FAIL: Creature `lc` (before the fix) and Tribe `t0` (`coop.fix ui.join_cover off` on the MSI before the join): the galaxy menu without any screen for ~20 s, then black (Tribe: 30 s of black in `t1`, before the brightness check).
* PASS, TITAN hosting and the MSI joining, a join and a forced mid-stage reload (`coop.worldfix queue 1`) each: Creature `c4` (join 2.8 s black held, reload 1.7 s), Civ `v1` (6.9 s black held), Cell `e1` (a 5.6 s cinematic held), Tribe `t2` (12.5 s black held), Space `s2` (the screen lifts on the playable Civ homeworld of the personal launch, 17.5 s after the stage). Roles swapped (the MSI hosting, TITAN joining), Tribe `w1`, `w2`, `w3`: join 11.2-15.8 s black held, reload covered; `w3` is the final build (the fog fix: the ring yellow at a Tribe night, where `w1` and `t2` drew it dark blue). No screenshot without the ring showed a black screen in the PASS runs; frames kept coming in every load (longest gap 0.8 s), the ring's turning arc moves between shots. 0 faults on both PCs.
* Not re-run: the late joiner's "Founding your village" / "Founding your capital" screens (BUG-9159, BUG-9170: seen live 2026-11-12/13; they need the creature editor saved by hand); they share the draw code (the fog fix applies).

## History
- 2026-11-14: opened.
- 2026-11-14: found by the owner's loading screen check (3S A); fixed and checked on both PCs.
