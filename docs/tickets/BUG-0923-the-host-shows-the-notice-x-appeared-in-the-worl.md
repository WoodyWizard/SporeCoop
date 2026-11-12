---
id: BUG-0923
title: The host shows the notice 'X appeared in the world' twice within 0.4 s when a guest joins
status: fixed
kind: bug
stage: Core
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: host
versions: 0.7.x
fixed_in: 0.7.12
branch: fix/w47-roster-portraits
commits: 9630ed89
tests: appeared_once
signature:
docs: docs/re/creations.md#creations-bundle-png
related: 
---

## Report
Key-frame review of the final check out/lab/20261111-044257 (rel/0.7.12 2f8f6ba4).
Sheets: `out/lab/20261111-044257/frames/sheets/038_play_space_01_start.jpg` and `out/lab/20261111-044257/frames/sheets/029_play_cell_01_start.jpg`; the same in
`out/lab/20261111-044257/frames/sheets/035_play_creature_01_start.jpg` and `out/lab/20261111-044257/frames/sheets/027_one_creature_01_host_creature.jpg`.
Frames: `out/lab/20261111-044257/frames/play_space/01_start_host.jpg`, `01_start_guest.jpg`.

* Host: the notice list shows "Bob appeared in the world" two times, one under the other (in one_creature a third
  one slides in after the creature switch).
* Guest: one "Alice appeared in the world" (after "receiving world" and "loading the world").
* Host logs: the line is written twice within 0.3 to 0.4 s at the join (play_space 05:18:44.481 and .907, with
  "creations: player 1 ufo = ..." between them; play_creature 05:31:24.382 and .713).

## Root cause
Observed (the final check's host logs: play_space, play_cell, play_creature, one_creature): the second line always
follows a `creations: player 1 <kind> = ...` line in the same millisecond. drive_puppet makes the puppet from the
guest's first state and tells "appeared in the world"; the guest's creation bundle arrives 0.3-0.4 s later,
creations' `appearance_changed` takes the puppet away so that it is built with the creation's look, the next frame
makes it again and told the player again. The same at every later change of a partner's creature (one_creature's
third notice). The guest sees one because the host's bundle is there before the guest's game has loaded the world.

## Fix
`notify.appeared_once` (mods/coop/coop.cpp): `appearance_changed` marks the remote (Remote::relook) when it takes a
shown puppet away; the puppet made next is not told. The mark is dropped when the partner is not visible (another
world, dead, silent), so a real return is told. `coop.status` prints `appeared notices: told N, puppet rebuilds not
told M`. Off: every puppet made is told (0.7.11).

## Verification
Test appeared_once (coop_appeared_once.py, 1 min, creature fixture): (1) the host's log has one "Bob appeared in the
world" within 3 s of the join; (2) after the join's panels ran out the guest takes another creature
(`coop.creations next creature`): the host's game rebuilds the puppet (its species changes) and the host's
notification panels (`coop.status` toast lines, asked every 0.2 s for 12 s) never show a new "Bob appeared in the
world".
- FAIL (SPORE_FIX_OFF=notify.appeared_once): out/lab/20261111-065449 (a new panel and a new log line after the
  creature change; the switch goes off after the lab's join, so part 1 runs fixed in that run: there the join's
  rebuild was kept quiet, "puppet rebuilds not told 1").
- PASS: out/lab/20261111-065556, out/lab/20261111-070559.
- campaign_ct with four players (out/lab/20261111-070555): one line per guest at the join in the host's log.
- Not verified: the doubled line at the join itself with the switch off (the lab joins before SPORE_FIX_OFF is
  applied; the join's rebuild comes only when the bundle arrives after the first state: 1 of 3 starts here).

## History
- 2026-11-11: opened.
- 2026-11-11: fixed in fix/w47-roster-portraits (notify.appeared_once).
- 2026-11-11: released in 0.7.12.
