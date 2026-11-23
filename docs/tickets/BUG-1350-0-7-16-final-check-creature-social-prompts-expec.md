---
id: BUG-1350
title: 0.7.16 final check: creature_social_prompts expects the fail right after one wrong answer; a game of 2+ prompts fails only at the end of its list
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: guest
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w60-dna
commits: 48dfc9fa
tests: creature_social_prompts
signature:
docs:
related: BUG-0525, BUG-1242
---

## Report
rel/0.7.16 worktree out/lab/20261120-041304 creature_social_prompts FAIL "the host counts that social game as failed".

## Root cause
A test bug: the mod, as the native game (docs/re/creature.md#creature-social-prompts, social.cpp host_prompt_tick ->
fail_game), fails a social game when its prompt list is done with the meter not full, not at a wrong answer. A Kook
of 2 or more rounds (an alpha one more) sends its next prompt 1.5 s later and the game goes on; the test waited a
fixed 4 s. Both failed runs show it: rel/0.7.16 worktree out/lab/20261120-041304 #215 prompts_sent 4 -> 5, prompt_fails 1 -> 1 (2 later); fix/w52-social
worktree out/lab/20261118-103216 #58 prompts_sent 7 -> 8, games=1. In the 15 passed runs the wrong answer met a game
of one prompt (prompts_sent unchanged, the fail at once).

## Fix
coop_creature_social_prompts.py: step 3 answers every further prompt of that game with the same wrong kind until the
host counts the fail or the game is gone (at most 4 + 15 s per prompt); the checks (not impressed on both machines, the
host counts the fail) unchanged.

## Verification
Was waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_social_prompts --lane 2 --lane 3 --retry 0 --no-build` PASS (the log line
"after K answer(s) of another kind to a game of P prompt(s)", P >= 2 must pass). no-fail-run: test change only.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): creature_social_prompts PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643 (FAIL before: 20261120-041304). Closed.

## History
- 2026-11-20: opened.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
