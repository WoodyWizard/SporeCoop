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
branch: fix/w47-play-bots
commits: df408715, 48cbb0f2, 2c012d76, 3c7dd711, e6158137, 8b46435b, 5a0f08ed, 29f42cd6
tests: play_creature
signature:
docs: docs/re/creature.md#creature-attack-reach, docs/re/creature.md#creature-combat-click-target, docs/re/creature.md#creature-walk-and-use-blocked, docs/06-testing.md#testing-w45-last, docs/06-testing.md#testing-w47-play-bots
related: BUG-0099, BUG-0913, BUG-0500
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
W47 (2026-11-10): the bot checks that the game's target is the animal it hunts (a click picks the partner's creature
or a herd mate), walks to a side when the game's walk-and-use stops behind the partner, and whoever is in reach first
waits for the partner; see the History entry. Earlier rounds:
Bot only, the game and the limit unchanged: `coop.abilities reach` (debug.cpp: the target, its gap, every ability's
reach and status, the HUD bar's rows) tells the bot the game's own reach; the bot clicks the target once, presses a key
of the bar's combat row out of reach once to start the game's walk-and-use (again only when the gap does not shrink for
2.5 s) and in reach when the ability is ready; while the partner leads a hunt the other joins it or follows instead of
leading its own. `play_creature.py --old-attack`: the old loop. The log prints each player's key presses by the reach.
Earlier: No game change, no looser limit. Diagnostics: coop.team prints "hits host N, partners M" (the hits the joint table
counted), play_creature prints "hits: guest sent / host applied; joint table: host / partners", so the next failure
shows whether the guest's hits did not land, were lost, or did not overlap with the host's.

## Verification
W47 (2026-11-10, fix/w47-play-bots): FAIL (the bot of 0.7.11): out/lab/20261110-210848 #2 on this branch (+0), rel/0.7.12
out/lab/20261110-200504, feat/w47-visual out/lab/20261110-201619, fix/w47-proxy-destroy out/lab/20261110-175415. PASS
(the new bot) 7 of 7: out/lab/20261110-220532 #1-#3 (+29, +66, +42), out/lab/20261110-223754 #1-#4 (+43, +19, +37, +24).
Earlier rounds:
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
- 2026-11-07: released in 0.7.9.
- 2026-11-07 (fix/w46-final-triage): seen once more on rel/0.7.10 e7d1734c (lab 20261107-141346 play_creature, lane 3):
  +0 joint hits with many hits on both sides (guest sent 60, host applied 60; joint table host 48, partners 60), so not
  the low landing rate of this ticket: the hits did not overlap within kHuntWindow (6 s). From 124 s to 248 s the host's
  count stood at 24 while the guest's went 1 -> 29 (the host bot lost its targets: hunts n92 "lead lost 20 s 4 hits",
  n95 "timeout 35 s", while the guest's bot killed n92 with 15 hits). The hit path (teamplay.cpp, hunt_hits.h, the
  npc.cpp hit handling) and play_creature.py are unchanged since v0.7.9; the 14 play_creature runs in the lab dirs since the fix
  (2026-11-06 12:50 to 2026-11-07) gave +4..+14, rel/0.7.10 20261107-110531 +8. Verdict: a bot flake (the two bots'
  hunts did not overlap), not a regression; left fixed, reopen when it repeats.
- 2026-11-08 (fix/w46-g4-tests): G4 reopened and fixed again: the +0 joint runs repeated (fix/w46-p4-creature 20261107-212113, fix/w46-k2a-state 20261107-232019, fix/w46-k4-bodies 20261108-020506, fix/w46-k8-open 20261108-023113; 5 of ~33 played runs since the 0.7.9 fix, joint hits median ~6 of ~80 hits). Cause (G4 analysis of 52 runs, out/g4/joint in the fix/w46-g4-tests worktree): both bots hunted the same animals, the joiner started 1-3 s after the leader, but only the leader bit: a Kook dies ~10 s after the leader reaches it; the bot's walk-and-use key waited behind a 1e9 gap sentinel after every click, a moment's lost target (retarget_none 60-360 times a run) made a new click that cancelled the walk, and a leader that lost its prey left the pack apart. Fix (e6158137, bot only, the check unchanged): no sentinel, a short wait instead of a click when the game drops the target during its walk, the leader holds its first bite at most 8 s until the partner hunts the same animal and is near, a pack hunt passes to the partner when the leader's ends; the hunt log names each bot's own bites. PASS 4 of 4: out/lab/20261108-060716 (+27, +1, +32, +34 joint hits). The +1 run: the joiners often arrive after the 8 s hold (own bites 0 in most pack hunts), a thinner tail, no failure. FAIL (the old bot): the four runs above; --old-attack keeps the 0.7.8 loop.
- 2026-11-08: released in 0.7.11.
- 2026-11-10 (fix/w47-play-bots): reopened and fixed a third time: +0 joint hits in 3 of 4 lab runs of the day on
  0.7.11.1 / 0.7.11.2 code (fix/w47-proxy-destroy 20261110-175415, rel/0.7.12 20261110-200504, feat/w47-visual
  20261110-201619; fix/w47-proxy-destroy 20261110-180718 passed with +4) and in 1 of 6 runs of the unchanged bot on this
  branch (20261110-210848 #2; the others +3, +21, +8, +7, +2): 4 of 10, and 4 of 17 since the G4 fix. Not the mod: the
  joint-hunt setting was on (the table counted 28-65 host and 26-39 partner hits a run), every hit sent arrived, and
  coop_team's controlled joint hit works. Cause, from a trace of every hunt and the joint table's hit log (new: `team:
  hit <npc> by player <n>` in the host's game log): in the shared hunts one bot bit and the other could not.
  (1) The bot took any target of the game for the animal it hunts; a click picks the creature under the cursor, often
  the partner's creature standing at the prey (ability status 12: the key does nothing) or a herd mate, and a click
  that hits nothing keeps the old target (`wrong_target` 249-953 looks per player and run;
  docs/re/creature.md#creature-combat-click-target). (2) The game's walk-and-use stops 2 m short behind the partner's
  creature and further key presses do nothing (reproduced in a probe: 100 s at gap 2.3 m; BUG-0913 opened for players;
  docs/re/creature.md#creature-walk-and-use-blocked): the bot pressed the key every 2.5 s until its hunt timed out.
  (3) The walk-and-use bites on arrival, so G4's "the leader holds its first bite" held nothing back, and a joiner
  that arrived first bit alone (6 hp, a bite every 1.7 s). Not the cause: BUG-0500's puppet far from its player (the
  puppets were where the players were: sync p* p90 0.5 m), the animals fleeing, the 6 s window, K4's drive, the
  0.7.12 settings split. Fix (bot only, the check `joint > 0` unchanged): the bot compares the game's target with
  the animal's creature in its own game and clicks again (higher on the body, after six misses a turn of the camera
  or a step aside); a walk-and-use that does not get nearer is followed by a walk to one side and the key again; who
  is in reach first waits for the partner to be in reach of the same animal (14 s at most while the partner hunts it);
  a hunt ends when the "You were killed" caption is up. `--old-target` / `SPORE_PLAY_OLD_TARGET=1`: the bot of 0.7.11.
  A first version with only (1) and (3) failed with +0 (20261110-214123 #1: both bots stalled at `W5 W5 ...`), which
  showed (2). FAIL (the old bot): 20261110-210848 #2 and the three runs above.
  PASS (the new bot) 7 of 7: out/lab/20261110-220532 #1-#3 (+29, +66, +42 joint hits) and out/lab/20261110-223754 #1-#4
  (+43, +19, +37, +24); kills by both players 8-13 a run. Also found: BUG-0913 (open).
