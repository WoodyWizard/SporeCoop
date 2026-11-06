---
id: BUG-0618
title: tests: play_creature fails 'joint hunts happened (+0 joint hits)' when the guest bot lands few hits (16 of 196 attacks)
status: fixed
kind: test
stage: Creature
reported: 2026-11-06
source: rel/0.7.9 integration run out/lab/20261106-040221 (fix/w45-int2)
role: 
versions: 
fixed_in: unreleased
branch: fix/w45-last
commits: df408715, 48cbb0f2, 2c012d76, 3c7dd711
tests: play_creature
signature:
docs: docs/re/creature.md#creature-attack-reach, docs/06-testing.md#testing-w45-last
related: BUG-0099
---

## Report
rel/0.7.9 integration run out/lab/20261106-040221 (lane 3): play_creature FAIL "joint hunts happened (+0 joint hits)".
Both bots hunted the same NPCs (10 kills seen by both), the guest bot pressed its attack keys 196 times, the host
applied 16 guest hits. A joint hit (teamplay.cpp, BUG-0099) needs a hit of the host and one of a partner on one
target within kHuntWindow = 6 s.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (counts over 14 play_creature runs, 2026-11-05/06): the number of guest hits the host applies varies
10-50 per 10-minute run and the joint hits 0-26 with it (rel/0.7.8 20261105-155603: 44 / +14; the first rel/0.7.9
integration run 20261106-013035, before the C2, L1 and P3 follow-up merges: 15 / +4; 20261106-040221: 16 / +0;
fix/w45-int2 on rel/0.7.9: 45 / +3 (051910), 50 / +26 (053225), 39 / +3 (054817)). Low guest hit counts happen
before the merges too, and no merged change touches the hit path (teamplay, MSG_DAMAGE_REQ, the damage filter):
not a regression. In 054817 every hit the guest's game sent arrived (sent 39, applied 39), so nothing is lost on
the way: the guest bot lands few of its key presses (39 of 282 attacks). Inferred, not checked: the bot presses an
ability key 0.2-0.5 s after its click on the target, often while its creature still walks to it (a joined hunt
starts 17-74 m away), and an ability out of reach does nothing.

* fix/w45-last (L5 of 0.7.9), observed (Ghidra + runtime, docs/re/creature.md#creature-attack-reach): a click on an NPC
  in the combat stance only sets the avatar's target (+0xE7C); the creature does not move (gap 39.4 m for 20 s on both
  games). A combat key out of the ability's reach makes the game walk the creature to the target and use it (goal 0x20 of
  the ability use 0xD1E930); a new click cancels that walk; in reach a key works when the ability is ready (status 0, 8 =
  recharging). The fixture creature's combat row has one key (1, type 0x1E, reach 1.0 m): keys 2-4 do nothing. The old
  bot clicked before every key (cancelling the walk), pressed 2-4 half of the time and pressed while recharging: so the
  guest landed few hits (the host's bot the same, it stands nearer its NPCs more often). And the joint hits need both
  biting one animal within 6 s: each player led its own hunt most of the time.

## Fix
Bot only, the game and the limit unchanged: `coop.abilities reach` (debug.cpp: the target, its gap, every ability's
reach and status, the HUD bar's rows) tells the bot the game's own reach; the bot clicks the target once, presses a key
of the bar's combat row out of reach once to start the game's walk-and-use (again only when the gap does not shrink for
2.5 s) and in reach when the ability is ready; while the partner leads a hunt the other joins it or follows instead of
leading its own. `play_creature.py --old-attack`: the old loop. The log prints each player's key presses by the reach.
Earlier: No game change, no looser limit. Diagnostics: coop.team prints "hits host N, partners M" (the hits the joint table
counted), play_creature prints "hits: guest sent / host applied; joint table: host / partners", so the next failure
shows whether the guest's hits did not land, were lost, or did not overlap with the host's.

## Verification
FAIL (the old bot): rel/0.7.9 out/lab/20261106-040221 (+0 joint hits, guest 16 hits for 196 presses; the same code's runs
before this branch: 0-26 joint, 10-50 guest hits). PASS with the new bot 4 in a row: out/lab/20261106-134606 (#1-#4:
+5, +21, +5, +7 joint hits; guest hits 16 / 55 / 18 / 26, every one sent arrived). Before the pack change:
out/lab/20261106-125018 (+10, guest 47 of 64 presses) and out/lab/20261106-130130 (3 of 4: +4, +3, +5; #3 failed on a
lost chat line, BUG-0629, its joint check passed +1).
Earlier: no-fail-run: a flaky bot outcome, not reproduced in 3 runs on lane 3 (051910, 053225, 054817, all PASS).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-int2): analysed, diagnostics added; open as a test flake (the bot's landing rate).
- 2026-11-06 (fix/w45-last, L5 of 0.7.9): root cause in the game (attack reach, the walk-and-use goal, one combat key); the bot attacks in the game's reach and the pack hunts together; 4 runs in a row pass.
