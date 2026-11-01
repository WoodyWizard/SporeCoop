---
id: BUG-0301
title: space: a share save posted in the frame the host's ship left its home planet (the place one frame stale): the guests' ships went down to the planet again
status: fixed
kind: bug
stage: space
reported: 2026-10-30
source: lab 20261030-075052 campaign_space#3 (lane 7)
role: 
versions: main 390f73d
fixed_in: 0.7.3.2
branch: fix/w11-space-join
commits: 1b8430e
tests: space_share_takeoff (coop_space_share_takeoff.py), campaign_space
signature: host log "saving for the share of player N (Space, <many> s in this place)" on the line before "stage: Space 40xxxxxx -> Space 80xxxxxx"; guests "Space 80xxxxxx -> Space 40xxxxxx" 2-3 s after the load
docs: docs/re/space.md#space-share-save-stale-place
related: BUG-0287, BUG-0276, BUG-0258
---

## Report
Final four-bot run on main (lab 20261030-075052 campaign_space#3, lane 7, campaign folder
out/lane7/campaign/20261030-091423): after Civ -> Space, "Bob, Carol, Dave did not get into the host's
world in space within 300 s (together on the host: [])". The host was in its solar system (place
0x80AE3044), the three guests on the home planet (0x40AE3044) of the same world. A rejoin of each
brought them together. Same signature as BUG-0287 (fixed in 0.7.3.1): its fix did not cover this case.

## Root cause
Observed (logs): the host's shares waited on the home planet ("leave the planet to save"). The share
save was posted with "Space, 80.1 s in this place" at 10:24:54.617 and the host's stage line
"Space 40AE3044 -> Space 80AE3044" followed 1 ms later in the same frame. `world::tick` runs before
coop.cpp updates `g_world` (the place probe of the settle clock, BUG-0276/0287) in its frame, so in
the frame the ship left the planet the place still said "planet, settled for 80 s" while
`save_game`'s own check (`Simulator::GetCurrentContext`) already said "solar system": the 2 s settle
did not hold the save. The guests loaded it into the system and 2.9 s later every ship was on the
home planet. The passing runs of the same lab saved 2.0-2.2 s into the system.
Reproduced (lab 20261030-100130): with the share's retry every frame (`coop.worldfix retry 0`, the
lab's 2 s retry landing in that one frame made certain) and the fix off, the host saved "16.1 s in this
place" in the takeoff frame and the guest's ship went from the system to the planet 2.5 s after the
load. So a Space save made in the first frame after leaving a planet loads on the planet (observed;
why the game does that is not known).

## Fix
`world.share_settle_context` (world.cpp `via_menu_tick`): in Space the settle clock also restarts when
`Simulator::GetCurrentContext` changes, read in the same frame as the save, so the share save waits
2 s in the solar system whatever the place probe says. Test knob: `coop.worldfix retry <ms>`.

## Verification
- FAIL (fix off, SPORE_FIX_OFF=world.share_settle_context): lab 20261030-100130 space_share_takeoff
  (lane 4): host place 80460035, guest place 40460035.
- PASS: lab 20261030-100310 space_share_takeoff #1 (lane 4) and #2 (lane 7): saves 2.1 s / 2.0 s into
  the system, the guest in the host's system.
- Related: lab 20261030-100500 campaign_space #1 (lane 4) and #2 (lane 7) PASS (4 bots, Civ -> Space;
  the Space share saves 2.2 s / 2.0 s into the solar system).

## History
- 2026-10-30: opened (fix/w11-space-join) from lab 20261030-075052 campaign_space#3; reproduced with
  a 2-player test, fixed.
- 2026-11-01: released in 0.7.3.2.
