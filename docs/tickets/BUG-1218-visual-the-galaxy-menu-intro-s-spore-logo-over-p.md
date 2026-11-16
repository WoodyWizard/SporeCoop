---
id: BUG-1218
title: Visual: the galaxy menu intro's SPORE logo over play on the host after a quick load from the menu (menu_splash, civ_designs key frames)
status: open
kind: bug
stage: Menu
reported: 2026-11-16
source: plan
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Key frame review of the 0.7.15 final check (lab 20261116-025154): the host's frame of menu_splash (S/062, Creature play
with the HUD, the status block, "Bob appeared" and the arrow over a black starfield with the big glowing SPORE logo) and
of civ_designs (S/035, the Civ HUD over a white glow with the SPORE logo and letterbox bars). Both frames were shot
0.3-0.9 s before the host's log line "world: the galaxy menu intro would start outside the menu (mode ...): not started
(BUG-0626)", ~16 s after its galaxy menu came up; both hosts had loaded their world from the menu ~4 s after it came
up (the lab's quick start, A9). The question: a real overlay a player sees, or a loading frame shot too early?

## Root cause
A real overlay, not a frame shot too early. Observed:
* The picture is the game's start-up video (Movie::MovieSystem, docs/re/graphics.md#graphics-startup-video), not the
  menu's GGE_intro cinematic and not a splash window (BUG-0938's table): the video plays while the galaxy menu comes up
  and ends ~16 s later with StopMovie, which posts 0x49731AD, the message the menu starts GGE_intro on; that start is
  the "would start outside the menu" line the frames came 0.3-0.9 s before (the video's last part: the big logo, the
  white flash).
* A load from the menu before that end does not stop it: the video's layer (DrawLayer) is drawn over the 3D view of the
  stage, under the HUD, until its own end. Lab (fix/w51-visual, the guest joining 0.5 s after its menu, switch off):
  `[[0x015FCC68]+0x34]` (MovieIsPlaying) non-zero in the guest's Creature stage 0.6-3.5 s with the natural timing
  (20261116-100245, 20261116-100032), and with the video held (the test seam) the whole run, its frame over the Creature
  HUD with letterbox bars: out/lab/20261116-100758/frames/menu_movie/01_stage_8-0s_guest.jpg.
* Who sees it: any game that loads a world within ~16 s of its galaxy menu coming up, for the rest of those 16 s: a
  guest that joins from its menu right away (the mod loads the received world as soon as it arrives, world.cpp), the
  lab's quick start (A9) on hosts and guests. In the final check the host of menu_splash had it from its load at menu +4 s
  to the video's end at menu +16 s (~11 s of play under it).
* find_partners (S/051, no arrow or tag for Bob 1.7 s after the video's end): not explained by the video (it had ended);
  not analysed further.

## Fix
world.cpp `menu_movie_tick` (session ticks of host and guest): in a stage, a start-up video still playing after 0.5 s is
stopped outside the frame with Movie::MovieSystem::StopMovie (0xFD79C0, thiscall), as its own end stops it; the
GGE_intro start it posts is refused outside the menu by BUG-0626 (`world.intro_at_once`). Log line "world: the start-up
video was still playing in the stage (mode ...): stopped (BUG-1218, n)"; coop.status "start-up video stopped N
(playing P)". Switch `world.menu_movie` (off: it plays on to its end). Symbols Movie::MovieSystem::Get / StopMovie /
PauseMovie verified in data/symbols/overrides_dc04aee5.sym.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
