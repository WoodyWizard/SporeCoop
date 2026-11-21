---
id: BUG-1368
title: multi4_tribe_slow: a rejoined guest counts as back before the host's world share to it, the round measures it while it reloads
status: open
kind: test
stage: Tribe
reported: 2026-11-20
source: lab 20261120-133703
role: guest
versions: rel/0.7.16 673f7a55
fixed_in:
branch: fix/w61-multi
commits:
tests: multi4_tribe_slow, multi3_round_rejoin, multi3_round_rejoin_oldwait (twin)
signature:
docs: docs/06-testing.md
related: BUG-1367
---

## Report
Nightly of the second 0.7.16 final check (lab 20261120-133703, lane 4, SPORE_FPS=20 SPORE_NET=players, seed
272986015): `multi4_tribe_slow` failed one check, `[min 1] Bob has the host's world (0%)` (first measurement 33%,
6 s + 4 s later 0% of 19, `npc(guest): proxies=0 ... local_removed=41`). Minutes 2-5 had Bob at 100%; the session
survived, no fault. Known flake (1 of 3 before); multi4_tribe_slow did not run in the first final check.

## Root cause
Test (its wait), not the lab's memory gate (BUG-1332: the games were up in 25 s and the test ran its full 6 minutes)
and not a join bug. Observed, the guest's and host's game logs: Bob's churn left at 16:17:14.8 and joined again at
16:17:18.4 into the Tribe world it still had (`Alice appeared in the world`, together at once); the host shares its
world with every joiner (`sending the world to Bob`, save 2 s, `sharing ... with player 1` at 16:17:20.9, `transfer
to 1 acknowledged` at 16:17:26.1 over the bad link). Bob's game received it at 16:17:25.98, left the stage
(`leaving the stage to the galaxy menu before loading`), loaded and was back at 16:17:31.0. The bot's back_in (and the
round's wait_all) counted only the host's `together` lines, which said Bob was together from 16:17:18.5 until his
stage change; the round measured him first right after the rejoin (his NPC proxies not yet sent again: 33%) and
again ~10 s later, inside the reload (0%). The world share on a rejoin in place is the mod's design (the joiner gets
the host's current world).

## Fix
coop_multi.py `settled()` / `wait_all()`: a guest is back when the host sees it together, the host owes it no world
(new test line `world_due N 0|1` in the host's coop.status, coop_cmds.cpp: needs_world or world::share_in_flight, i.e.
a share due, being saved, sent or not yet acknowledged; `share for N` / `sending world to N` on older builds), the
guest's own game receives no world (`receiving world`) and has its avatar, on 3 polls in a row (about 1 s apart: the stage change follows the end of the host's share lines by up to a
second). A wait for a state, not a longer sleep. The other tests that import wait_all with a number keep the old
count wait. New `--round-churn`: Bob rejoins right before every round and, once every guest counts as back, must stay
in the host's world for 15 s; `MULTI_WAIT=together` is the old wait.

## Verification
Open, waiting for the lab run.
* FAIL (the old wait): out/lab/20261120-133703 multi4_tribe_slow.
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run multi4_tribe_slow multi3_round_rejoin --lane N --retry 0
  --no-build`.
* Wanted FAIL (twin, the old wait): `SPORE_DETECTOR=20 python3 tools/lab.py run multi3_round_rejoin_oldwait --lane N
  --retry 0 --no-build` (`left: ['Bob']`: the host's share reloads him after he counted as back).

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-multi): the rejoined guest's world share was not waited for; coop_multi waits for the settled
  state, --round-churn and its twin added.
