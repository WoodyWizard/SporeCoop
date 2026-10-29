---
id: BUG-0258
title: after a live Creature to Tribe stage change the host plays 'snapshot.spo' and no guest is ever together with it
status: open
kind: desync
stage: Tribe
reported: 2026-10-29
source: coop_campaign.py debug run 2 (dev/campaign, lane 7)
role: 
versions: main 8f1944b
fixed_in:
branch:
commits:
tests: tools/tests/coop_campaign.py --from creature --to tribe
signature:
docs:
related: BUG-0152
---

## Report
Found by the co-op campaign (coop_campaign.py, four players, lane 7, main 8f1944b, debug run
out/lane7/campaign/20261029-001119). The host ends the Creature stage the players' way (bar full,
History and its check, the evolution vote with every guest's "I'm ready", the editors, the fire
cinematic and its caption). The host enters the Tribe stage, sends its world to all three guests
twice (`next phase: sending the world to ...`), every guest loads it and plays the Tribe stage, but:

* the host's `coop.status` says `together 1 0`, `together 2 0`, `together 3 0` for five minutes and
  more, `remote N ... puppet=00000000` (no puppets of the guests on the host);
* a guest's log has "Alice hidden: flags=0x1 ... same_place=0" at the stage change and never
  "Alice appeared in the world" afterwards (the guests see each other: "Carol appeared", "Dave
  appeared");
* `coop.avatar` on the host: `Tribe: local=... place=0x6F3BE084 world 'snapshot.spo'`, on a guest:
  `Tribe: local=... place=0x68AC222A world 'Scalinus.spo'`.

In a manual two-player run the same day (lane 7, same path, the guest's "I'm ready" missed, so the
vote ran out of time) the guest was together with the host (`together 1 1`) about 30 s after the
change: the number of guests, the vote or the timing of the shares may matter (not checked). Cell -> Creature in the campaign (run 20261029-000344) had no such
problem: every guest was together with the host 19 s after the change.

How to reproduce: `SPORE_LANE=7 python3 tools/tests/coop_campaign.py --from creature --to tribe
--minutes 3 --advance-minutes 10` (finding `stuck` "... did not get into the host's world in tribe"),
or by hand from the `creature` fixture: `call Simulator::cCreatureGameData::AddEvolutionPoints cdecl
f:3000` on the host, Enter on the brain caption, click 1400 1062 (History), 1415 1055 (its check),
the guests' "I'm ready", "done" in the editor (1416 1062), the check of the caption "Congratulations!
You are about to enter the Tribal stage." (1410 1048); then `coop.avatar` on both.

## Root cause
Not analysed (this ticket only reports it). Observed: the place id of a player is
`modes::current_world()` = crc32 of `SporeCoop::CurrentGameName` (modes.cpp), and right after the
game's own stage change that name on the host is `snapshot.spo`, not the planet's file the host
shares (Scalinus.spo). Inferred: the guests' place never equals the host's, so `together` stays 0
and no puppets are made, until the host loads its planet again (a jump by `coop.load` made them
together at once in the same run).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened by dev/campaign (the campaign's first Creature -> Tribe change with four players).
- 2026-10-29: reproduced in the whole-game debug run out/lane7/campaign/20261029-011243 (cell fixture, host `world 'snapshot.spo'`, guest `world 'Cadian.spo'`, `together N 0` for all three guests): 2 of 2 campaign runs with four players.
