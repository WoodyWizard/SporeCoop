---
id: BUG-0263
title: campaign: a noise pause left open on the host blocks the stage change (History clicks do nothing)
status: fixed
kind: test
stage: Creature
reported: 2026-10-29
source: fix/w5-snapshot, lane 3 campaign run 20261029-060328
role: 
versions: 
fixed_in: unreleased
branch: main
commits:
tests: campaign
signature:
docs:
related:
---

## Report
`SPORE_LANE=3 python3 tools/tests/coop_campaign.py --from creature --to tribe --minutes 3 --advance-minutes 10`
(seed 67500682, run out/lane3/campaign/20261029-060328 of fix/w5-snapshot): the play phase ended
with "Alice noise_pause=1"; at the advance the host's pause menu (Options) was still open, so the
History clicks of kick_creature did nothing and the stage change timed out after 10 min (finding
`stuck`, the screenshot shows the Options box). The same run with `--noise 0` reached Tribe in
125 s. Likely fix: close the pause menu ("Resume Game") at the start of advance() and before each
kick on the host.

## Root cause
The noise pause pressed Esc twice; the second Esc can land on the pause menu's Options box instead of closing the menu (as in the screenshot), so the host stayed paused.

## Fix
The pause noise closes the menu with its own "Resume Game" button (as play_creature.py does), and advance() closes a pause menu on every player before the stage change. Test tool only, no switch.

## Verification
no-fail-run: test tool change; the failing run is out/lane3/campaign/20261029-060328 (fix/w5-snapshot worktree); the final campaign run with the default noise checks it.

## History
- 2026-10-29: opened.
