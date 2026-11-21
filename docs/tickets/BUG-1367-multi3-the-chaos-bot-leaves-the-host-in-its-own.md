---
id: BUG-1367
title: multi3: the chaos bot leaves the host in its own evolution editor, so no guest is in the host world and a rejoiner waits for good
status: open
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-133703
role: host
versions: rel/0.7.16 673f7a55
fixed_in:
branch: fix/w61-multi
commits:
tests: multi3, multi4_creature_seed, multi4_creature_slow
signature:
docs: docs/06-testing.md
related: BUG-0516, BUG-1368
---

## Report
Nightly of the second 0.7.16 final check (lab 20261120-133703, lane 4, seed 637600690): `multi3` failed
`[min 2] all guests in the host world (together [])` and `the session survived` (`!! guest did not get back into the
host world`). The start and minute 1 rounds passed (coverage 100%, n in sync). multi3 did not run in the first final
check (20261120-041304).

## Root cause
Test (the chaos bot). Observed in the host's game log: a mating cinematic from s 70 (`busy=cinematic,input_lock`,
the bot's clicks at the host's nest), then at s 93.1 `editor request in Creature ... (an evolution in the stage)`,
`evolution: the host's editor opens at once, the others get an offer (core.editor_now)` and `stage: Creature ->
Editor`; the host stayed in that editor to the end of the test (trace lines `Editor ... busy=editor` until s 280).
In its editor the host has no avatar, so its `together` lines are 0 for every guest (`puppet=hidden: no own avatar
here`); the round's wait_all saw `together []` for 60 s. Bob's churn at 17:50:15 rejoined into the host's editor
(`Bob waits for your world: load or start a game`) and back_in waited 120 s for a world that comes only when the host
leaves its editor. Bot.leave_editor() existed for a rejoined guest only; nothing made the host leave an editor the
bot's own clicks had opened. Not a mod bug: the host's editor at once is BUG-0516's design, the guests got the offer.

## Fix
coop_chaos.py: Bot.leave_editors() (every player in an editor leaves it with "done" like a player, the host too;
the mode probe writes no log line), Bot.rejoin() (the churn, the same rng calls: a seed replays the same actions).
coop_multi.py: the rounds' and back_in's wait_all() call bot.leave_editors on every poll, check_round's second
measurement too. The check itself is unchanged: every guest must be in the host's world for the round.

## Verification
Open, waiting for the lab run.
* FAIL (the old bot): out/lab/20261120-133703 multi3 (seed 637600690: the host's evolution editor at s 93).
* Wanted PASS, the same seed: `SPORE_DETECTOR=20 python3 tools/lab.py run multi3 --lane N --retry 0 --no-build`
  and the replay `SPORE_LANE=N SPORE_LANE_B=N python3 tools/tests/coop_multi.py --fixture creature --players 3
  --minutes 3.0 --seed 637600690` (the host's editor is left, Bob's rejoin gets the world).
* no-fail-run for a twin: the bot's behaviour, not a mod switch; the FAIL run above is the old bot.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-multi): the host's own evolution editor (core.editor_now) after a mating the bot started; the bot
  now leaves every player's editor before a round and while it waits for a rejoin.
