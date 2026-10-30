---
id: BUG-0287
title: space: after the host left its home planet, three guests loaded its world and their ships went down to the planet while the host stayed in the solar system
status: fixed
kind: bug
stage: space
reported: 2026-10-29
source: lab 20261029-174717 campaign#2 (lane 4)
role: 
versions: main b5ef74d
fixed_in: 0.7.3.1
branch: fix/w8-stuck
commits: 1e5edc8
tests: campaign_space (coop_campaign.py --from civ --to space)
signature: host place 0x80xxxxxx (solar system), guests 0x40xxxxxx (home planet) after the first Space share
docs: docs/re/space.md#space-share-save-at-context-change
related: BUG-0276, BUG-0258
---

## Report
Final four-player campaign on main b5ef74d (lab 20261029-174717 campaign#2, lane 4, campaign folder
out/lane4/campaign/20261029-174725, finding 6): after Civ -> Space, "Bob, Carol, Dave did not get into
the host's world in space within 300 s (together on the host: [])". The host was in its solar system
(place 0x80AE3044), all three guests on the home planet (0x40AE3044) of the same world 'Tavael.spo'.
A rejoin of each (the host saved again in a settled solar system) brought them together.

## Root cause
Observed (logs): the share was saved, sent, received and loaded. The host's shares waited on the home
planet ("leave the planet to save") and the save was posted at 19:52:27.790, 9 ms after the host's
place went from the planet to the solar system (27.781); the box came, the files went out. Every guest
loaded it into the solar system (Space 80AE3044, together with the host, "Alice appeared"), and 2.2-2.4 s
later each guest's ship was on the home planet (Space 80AE3044 -> 40AE3044, nobody steering it; the
host stayed in the system for 5 min). After the rejoins the guests loaded saves made 4-5 min into the
system and stayed there. In lane 2's game of the same lab the save came 1.7 s after the host reached
its system, and the guests stayed in the system.
Inferred (not reproduced): a Space save made in the first frame after leaving a planet still holds the
ship's way down / the planet context, and a game that loads it goes back to the planet. The share's
settle clock (BUG-0276, `world.share_settle`) did not restart on a Space context change (same mode,
avatar all the time), so nothing held the save.

## Fix
Same change as BUG-0276 (reopened, wave 8): `world.share_settle_reset` restarts the share's settle
clock at every mode or place change, so a Space share save waits until the host has been 2 s in its
solar system (the place is the adapter's, `coop.cpp g_world`). Log: "world: saving for the share of
player N (Space, X s in this place)".

## Verification
- no-fail-run: a timing race (the host's share retry, every 2 s, must land in the first frames of
  the solar system); with the fix off (SPORE_FIX_OFF=world.share_settle_reset) lab 20261029-192222
  campaign_space#2 posted the save 1.8 s into the system and the guests followed (#1 never got to
  Space: its Civ -> Space change ran out of time). Not reproduced.
- With the fix: lab 20261029-194928 campaign_space#1 and #2 PASS (share saves 2.1 s into the solar
  system, the guests together with the host).

## History
- 2026-10-29: opened (fix/w8-stuck) from the logs of lab 20261029-174717; fixed by BUG-0276's second switch, root cause inferred, no FAIL run.
- 2026-10-29: released in 0.7.3.1.
