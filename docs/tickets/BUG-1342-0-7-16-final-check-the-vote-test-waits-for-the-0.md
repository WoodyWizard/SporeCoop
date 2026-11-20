---
id: BUG-1342
title: 0.7.16 final check: the vote test waits for the 0.7.15 notice 'evolved the species' (BUG-1258 says 'evolved its creature' in creature.mode own)
status: open
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: guest
versions: 0.7.16 (unreleased)
fixed_in:
branch: fix/w60-dna
commits:
tests: vote
signature:
docs:
related: BUG-1258
---

## Report
rel/0.7.16 worktree out/lab/20261120-041304 vote: "FAIL everybody is told about the evolution"; every other check passed (the editors opened and closed, the
guest got the world back and plays its own creature).

## Root cause
A test bug: the guest's log has "09:47:43.954 coop: Alice evolved its creature" (vote.guest.game.log). Since BUG-1258
(core.evolve_notice, 0.7.15.2) the host's Creature evolution is told as "<host> evolved its creature" in creature.mode
own (the default); "evolved the species: everybody gets the new body" only in creature.mode host. coop_vote.py still
waited for the old text (its wait loop too, so it also slept its full 120 s).

## Fix
coop_vote.py expects the text of the session's creature.mode (settings_lib.get(host, 'creature.mode')): "Alice evolved
its creature" (own) or "evolved the species" (host). The check stays.

## Verification
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run vote --lane 2 --retry 0 --no-build` must
PASS. no-fail-run: test change only (the FAIL run with the old test is rel/0.7.16 worktree out/lab/20261120-041304 vote; `SPORE_FIX_OFF=core.evolve_notice`
gives the old text, which the test then does not find: FAIL).

## History
- 2026-11-20: opened.
