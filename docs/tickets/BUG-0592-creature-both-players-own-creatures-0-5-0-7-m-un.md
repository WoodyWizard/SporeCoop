---
id: BUG-0592
title: Creature: both players' own creatures 0.5-0.7 m under the ground right after the guest's return from the main menu
status: open
kind: bug
stage: Creature
reported: 2026-11-05
source: lab long_creature_short 20261105-002102
role: both
versions: 0.7.7-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The long-session bot (long_creature_short, rel/0.7.7 74f4ad0d, lab out/lab/20261105-002102) failed on the monitor's
ground gate: "host: its own creature 0.70 m under the ground (Creature) on 2 lines from s 380" and the same on the
guest (0.52 m), right after the guest's "menu" transition (exit to the main menu, join again, 16 s). Before it the
session had an editor transition. Not seen in the same test's earlier runs (D0 branch).

## Root cause
Not analysed yet. Both games' own creatures at once suggests the game's own ground (terrain) state after the
guest's world load, not a puppet; the host did not leave.

Update (2026-11-05, final check out/lab/20261105-015350 long_creature, full 40 min): the same gate on the host at
s 1400, again right after the guest's "menu" transition (which followed an editor one). The host's trace: at s 1390
the avatar stood at (-219.8 -454.1 -48.6) agl 0.00; the host saved the world for the share at s 1392; at s 1400 and
1410 it stood at (-219.5 -453.5 -48.6) agl -0.69: moved 0.7 m sideways at the same height, not sunk. At s 1420 it
walked and was back on the ground (agl 0.00). So something moves the standing avatar on the share's save or the
guest's join (a push by the partner's puppet when it is shown again is the first suspect), and the game puts it on
its ground only once it walks. Visual, 20 s, recovers by itself; not a release blocker for 0.7.7.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
