---
id: BUG-0412
title: creature: a guest that left, or rejoins while the host is in its editor, plays on a planet without creatures
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: join_during_host_editor, lab 20261101-145528 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in: 0.7.9
branch: fix/w45-one-game-objects
commits: 3f8ec9a3
tests: join_during_host_editor (coop_join_during.py --case editor)
signature:
docs: docs/re/creature.md#creature-npc-gaps, docs/06-testing.md#testing-npc-gaps
related:
---

## Report
`join_during_host_editor` (coop_join_during.py --case editor, creature fixture, lane 9): the guest leaves the
session after the start (it keeps playing its copy of the host's world), the host opens its creature editor
alone, the guest joins again. For the 25 s the host edits, the guest's game has no creature at all near it:
two census dumps 8 s apart, `0 of 0` creatures of its own or proxies (labs 20261101-145528 with every fix on,
20261101-145308 with core.away_editor off: the same). The guest's log: at its leave (`net: session closed
(user)`, 15:17:55) the host's 20 NPC proxies are destroyed; at the rejoin 17 s later (host away: in its
editor) `creature: the host left: 0 of its NPCs shown here go on as NPCs of this game (20 not)`, then
`npcs mirrored: 0 within 60 m ... 0 in all` every 10 s until the host is back (then 9 within 60 m at once).

How to reproduce: `SPORE_LANE=N python3 tools/tests/coop_join_during.py --case editor` (the line "the guest's
creatures that moved > 1 m in 8 s of the host's editor: 0 of 0"); or by hand: creature fixture, the guest
`coop.leave`, wait 20 s, the host `coop.vote editor`, the guest joins: its planet is empty.

## Root cause
Observed in the log, the code read (npc.cpp `away_locals`, `tick_frame`): the guest's own creatures were
removed by the update filter while it mirrored the host; a Leave destroys the proxies and turns none of them
into NPCs of the guest's game (away_locals runs only when the host goes away, not when the guest leaves), so
the guest plays on an empty planet. At the rejoin with the host away, away_locals converts the last proxy
snapshot only when it is under 15 s old (`now - g_away_snap_at > 15.0`): 17 s, so `20 not`. Inferred: the
same happens to any guest that left for more than 15 s before the host goes away.

## Fix
npc.cpp (`coop.fix npc.leave_locals`, off = 0.7.8), one rule "a guest's game takes over the host's NPCs whenever it
stops mirroring them": (1) a guest's session end in the Creature stage marks the proxies' snapshot (away_snapshot, at
most 0.5 s old) and the next frame, outside the session update, away_locals makes them NPCs of the guest's game (same
world epoch only, as when the host goes away); (2) the update filter keeps a guest's own NPCs while it has no proxy
and the host does not play its world (host_here(), new npc::Ctx::host_here), so a rejoin during the host's editor no
longer removes them in the 4 s before the away state. `coop.npc` (guest) prints `leave_locals=`.
Root cause confirmed at runtime (lab 20261106-015905, fix off): 1 own creature within 70 m 10 s after the rejoin where
the guest had shown 11.

## Verification
join_during_host_editor (the --allow-empty BUG-0412 is gone; it now checks the guest's own creatures within 70 m,
census `local`, alive, AI on, >= 60 % of what it showed before its leave): FAIL with SPORE_FIX_OFF=npc.leave_locals
out/lab/20261106-015905 (1, need 7 of 11), PASS out/lab/20261106-015417 (12 of 11; join_during_combat and away_refill
PASS in the same run). Not verified: whether the creatures made by away_locals walk later (none moved > 1 m in 16 s,
0.1-0.4 m idle moves).

## History
- 2026-11-01: opened.
- 2026-11-01 (feat/w33-tests-session): the same empty copy fails the players suite's party_creature4_mobile and party_creature4_players (lab 20261101-160125) by the sync-check gate: Dave leaves (the vote step), joins again over the bad network and plays his old copy, now without creatures, until the host's world arrives: `sync-check: MISMATCH with player 3 'Dave' (Creature 68AC222A, 4 objects compared): n: 17 missing on the guest` 24 s after his leave (diag.rejoin_grace did not cover the slower share).
- 2026-11-01 (fix/w37-vote-leaver, seen, not analysed): party_creature4 in lab 20261101-224704 (lane 5) failed only the sync-check gate: Dave rejoined after leaving during a vote (Loading -> Creature at s 85.8), `sync-check: MISMATCH with the host ... n: 16 missing on the guest` at s 93.5 (7.7 s after his load), `sync-check: OK ... 26 objects` at s 123.8. A transient after a rejoin, not the permanent empty planet of this ticket; same area. The test's own checks passed.
- 2026-11-06 (fix/w45-one-game-objects, L1): fixed (npc.leave_locals): the leave hands the proxies over, a rejoin keeps the own NPCs until the host is here.
- 2026-11-07: released in 0.7.9.
