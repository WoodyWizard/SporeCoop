---
id: BUG-1046
title: Space: a guest's mirror of a host NPC ship fires 4 shots for 1 (or skips the first) when the ship shoots again 1-2.3 s after a pause (host and guest forget a shooter at different times, the count restarts)
status: open
kind: desync
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-13
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-space-shot-burst
related: BUG-0677
---

## Report
Audit SPC-13. Not seen at runtime, found by code reading.
A guest watching a host NPC ship that fires, pauses a little over a second and fires again sees its mirror fire a burst of 4 shots for the one shot (or, after a single earlier shot, none). The copies hurt nothing (BUG-0677), so it is cosmetic: extra lasers / missiles on the guest's screen.

## Root cause
Observed in code (main efd2e0ba):
* Host: squadron.cpp:605 drops a shooter kShotKeep = 1.0 s (175) after its last shot; a new shot recreates the entry with `count = e.count + 1` = 1 (336-338: the erased entry starts at 0).
* Guest: squadron.cpp:754 updates `seen` with every list that has the ship (lists go every 0.2 s while the host keeps it), and 827 forgets it only kShotKeep + 0.3 s after the last list, i.e. up to about 2.3 s after the host's last shot.
* A shot in that window: the guest's entry is not fresh, `done` is the old count (e.g. 7), the new count is 1: `(uint8_t)(1 - 7) = 250 > 4` gives done = count - 4 (756-757), so guest_fire (858-866) fires 4 times. With an old count of 1 the new count equals done and nothing fires.
The audit's window "1.0-1.3 s" is off: it is from the host's 1.0 s drop until the guest's drop (about 1.8-2.3 s after the last shot). Confidence: high.

## Fix idea
Do not restart the host's count: keep the per-ship counter in its own map that survives the 1 s list drop (erase it with the mirror), or send an epoch byte per entry so the guest resets done when the host recreated the entry. Switch: `coop.fix space.shot_count off`.

## How to test
`coop_space_shots.py`: a host NPC ship (escort / pirate test spawn) fires one shot, waits 1.5 s, fires one more (a host test command calling FireTool on it, or the game's own fight with a timed target); assert the guest's game made exactly 1 FireTool on the mirror for the second shot (`coop.space shots` counters of the game's tool, or the guest's FireTool hook count). Main: 4 (FAIL).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
