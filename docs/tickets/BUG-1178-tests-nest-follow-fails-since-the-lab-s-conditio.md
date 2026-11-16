---
id: BUG-1178
title: Tests: nest_follow fails since the lab's condition start-up waits (A9): the guest's game gets the brain upgrade's DNA 6 s after entering and plans no migration
status: fixed
kind: test
stage: Creature
reported: 2026-11-15
source: feat/w51-npc lab 20261115-165219
role: 
versions: rel/0.7.15 since ec51786b (the lab start of A9)
fixed_in: unreleased
branch: feat/w51-npc
commits: ef715c49
tests: nest_follow
signature:
docs: docs/06-testing.md#testing-w51-npc
related: BUG-1112, BUG-0640
---

## Report
nest_follow failed in feat/w51-npc's stage run (lab 20261115-165219 lane 3) and again with and without npc.registry
(20261115-192704 lane 3 off, 20261115-192709 lane 2 on): "the guest's own game claims the host's new nest", "the guest's
creature lives in the host's new nest's herd", "the guest's new home nest heals its creature".

## Root cause
Bisect on lane 3 (a worktree of the tag): v0.7.14.1 PASS (20261115-193814), d0cdc8ea PASS (20261115-194716),
b298b893 PASS (20261115-195218), ec51786b FAIL (20261115-195713). Between them only the rel/0.7.15 merge b20b30e9
with A9 (BUG-1112, d1fe9727: the lab's start-up condition waits instead of the fixed 4 s / 3 s sleeps) and BUG-1134's
call guards. The final tree with `SPORE_START_FIXED=1` (the old sleeps) PASS (20261115-200355). Observed over nine
runs: the guest's game got the brain upgrade's 250 DNA at session s 6.5 / 7.9 / 7.9 / 10.0 in the four FAILs and at
s 12.2 / 12.8 / 14.4 / 16.4 / 25.0 in the PASSes (guest log `dna: 0 -> 250`): with the DNA that early the guest's
game planned no migration (its next nest stayed its home nest, wsync's creature_nest row found nothing to follow;
`guest migration state 0, its next nest` = its home). Inferred, not verified: the guest's game ignores the brain
threshold's migration planning in its first ~11 s in the world (after the join's hatch). A player whose host crosses a
brain threshold within ~10 s of the join would not follow the next migration either (not seen in reports).

## Fix
coop_nest_follow.py: the guest's game plays its world 8 s before the test gives the DNA (`GUEST_PLAY_S`, the old
start's margin); `NEST_FOLLOW_NO_PLAY=1` gives it at once. Test-only.

## Verification
FAIL: 20261115-165219, 20261115-192704, 20261115-192709, 20261115-195713 (A9's start, DNA at s 6.5-10.0).
PASS: 20261115-201309 (2 of 2 with the fix). The twin NEST_FOLLOW_NO_PLAY=1 passed once (20261115-200855) because that
cold start was slow (DNA at s 16.4): the timing decides, not the switch of a test.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-npc): bisected to the lab's start-up of A9, test fixed.
