---
id: BUG-0505
title: play_tribe: guest orders the host takes but its members do not act on (12 idle of 28), the host's bot gives too few orders (minimap look_far 24)
status: open
kind: test
stage: Tribe
reported: 2026-11-03
source: fix/w40-play final check, lab 20261103-160342
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0481
---

## Report
Seen once the citizens' sync check passed (BUG-0481): play_tribe#1 of out/lab/20261103-160342 (fix/w40-play,
lane 6) failed `host: gave orders through real clicks (8)` and `guest: the members act on the orders ({'ok':
12, 'lost': 0, 'idle': 12, 'none': 4})`. The guest's orders reached the host (`cmds_done=29 rejected=0`), but the
ordered members did not move or hit there ("no movement": gather fruittree, the epic n11 in the epic phase, a
walk after a gift). The host's bot: look_far=24 (its minimap look did not land within 30 m), no_selection=18,
84 actions in 10 min. Replay: `--seed 655950227`.

## Root cause
Not analysed. Inferred: the idle orders cluster in the second half (gather / epic) after member gifts
(`gave=1/2`): the ordered member may be the one just given; the host's look_far suggests its view calibration
went off after the epic arrived.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03 (fix/w41-tribe-actions): replayed with `PLAY_TRIBE_SEED=655950227` (new: the lab passes the seed this way) on lane 2, SPORE_DETECTOR=20: not reproduced, PASS (out/lab/20261104-000550: host 19 orders all acted on, guest 15 ok / 2 idle / 0 lost of 17; the seed fixes the bots' choices, not the game). play_tribe now logs, for an order nobody acted on, the host's line of each ordered member (behaviour node `bt=`, place, health) to tell the next failure apart. Suspect (inferred, not shown): gathers on trees the host's game has picked empty while the guest's copy still shows fruit (worldobj tree snapshots cover at most 48 trees within 70 m of the village; the bot picks trees within 90 m).
- 2026-11-04 (fix/w41-tribe-actions): reproduced in the regression, play_tribe#2 of out/lab/20261104-010027 (guest ok 11 / idle 8 / lost 0; host gave 14 orders: 'gave orders' passed, ok 9). Observed with the new host lines (the host's game state 8 s after the order): epic order (action 10 Hunt, target the co-op epic at (373 -168 295)) on members #12 hp 23, #13 hp 8.5, #18 hp 112, all standing (speed 0) ~85 m from the epic at (320 -157 360), all three in the same behaviour node bt=05F79546; gather order (action 2, a tree at (425 -79 265)) on #20 at the village (405 18 303), 100 m off, standing in node bt=0569236E. So the host's game took the orders (no rejection) and its members run another behaviour: inferred, the order's AI record is replaced at once by the game's own (hurt members: a heal / flee node; the gather: the tree out of the member's reach or the order dropped for distance). Next: name the nodes 05F79546 / 0569236E (behaviour ids, docs/re/tribe.md section 4) and log the member's action queue right after host_command's DoAction.
- 2026-11-04 (fix/w41-tribe-actions): the same symptom in tribe_tools (BUG-0514's test) in 3 of 8 runs with the fix on: the guest's GrabTool order reaches the host (`-> <rack>`, cmds_done+1) and the host's member stands 6 m from the rack (speed 0, bt=0462854D) and never takes the tool, also after a second order at 40 s (out/lab/20261104-012256 tribe_tools#2, out/lab/20261104-005643); 5 runs took it in 3-37 s. So a guest's order done on the host by host_command's DoAction(c, action, target, 0) is sometimes not carried out by the host's game: this ticket's root cause, not the tool sync.
