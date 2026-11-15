---
id: BUG-9209
title: Creature: a guest (MSI) could not name its creature in the pre-game start editor, so it played its old creature
status: open
kind: bug
stage: Creature
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-9207 BUG-9208
---

## Report
Owner play test 2026-11-15 (3 players, a new Creature world from the galaxy wizard): "MSI was unable to name their creature, therefor they werent able to join with their own creature."

## Root cause
Observed (the MSI log): the pre-game start editor opened at 02:09:31 with the host's parts list; the check unlocked at 02:13:29; the guest left the editor at 02:19:11 without a save, the world loaded, the old pick 'Lilku' played, the BUG-9164 editor followed in the world and 'Yankin' was saved there at 02:22:28 (named there). The MSI opened Settings (the Graphic page) twice while in the editor (02:17). Titan (this PC) named 'Slank' in the same editor. Not known yet why the name could not be typed; the Settings keyboard capture (coop_ui graphics_input) only runs while Settings shows. Second fault: a guest leaving the pre-game editor without a save plays its old pick (creations::needs_first_creature is false while g_first_pending is still set), against BUG-9208's rule.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
