---
id: BUG-0412
title: creature: a guest that left, or rejoins while the host is in its editor, plays on a planet without creatures
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: join_during_host_editor, lab 20261101-145528 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
- 2026-11-01 (feat/w33-tests-session): the same empty copy fails the players suite's party_creature4_mobile and party_creature4_players (lab 20261101-160125) by the sync-check gate: Dave leaves (the vote step), joins again over the bad network and plays his old copy, now without creatures, until the host's world arrives: `sync-check: MISMATCH with player 3 'Dave' (Creature 68AC222A, 4 objects compared): n: 17 missing on the guest` 24 s after his leave (diag.rejoin_grace did not cover the slower share).
- 2026-11-01 (fix/w37-vote-leaver, seen, not analysed): party_creature4 in lab 20261101-224704 (lane 5) failed only the sync-check gate: Dave rejoined after leaving during a vote (Loading -> Creature at s 85.8), `sync-check: MISMATCH with the host ... n: 16 missing on the guest` at s 93.5 (7.7 s after his load), `sync-check: OK ... 26 objects` at s 123.8. A transient after a rejoin, not the permanent empty planet of this ticket; same area. The test's own checks passed.
