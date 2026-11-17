---
id: BUG-1223
title: Space: a guest's restored badges show their Reward box again in the next session (space_session key frame: Explorer 2 over the galaxy view)
status: open
kind: bug
stage: Space
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
Seen on fix/w51-visual while adding the space_session key frame (BUG-1220): in the next session (twoinst.session_restart)
the guest's game shows the badge "Reward" panel "Explorer 2 - You've explored 50 Solar Systems" (4 badge points, tools
Raise / Lower / Level Terrain) over its galaxy view, right after `badges: own badges back from the host: 144 badges (2
earned)` / "Your badges and rank are back." (space_session.guest.game.log 09:59:02.906). Frames:
fix/w51-visual out/lab/20261116-094321/frames/space_session/01_colony_guest.jpg and
out/lab/20261116-095532/frames/space_session/01_colony_guest.jpg (the panel holds the clicks: the guest's ship could not
land until it was closed). The badge was earned in the first session (`coop.space badges event 2 60`).

To reproduce: `tools/lab.py run space_session`; look at the guest's colony key frame, or `uifind.shown(guest, 'Reward')`
after the restore.

## Root cause
Not analysed. Inferred: the restore sets the earned badges through the game's own Update (counter path), which shows
the Reward panel as for a new badge; a badge already earned (and shown) in an earlier session should come back
silently. Not a 0.7.15 regression check yet (E1 is new in 0.7.15, so the restore itself is new).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
