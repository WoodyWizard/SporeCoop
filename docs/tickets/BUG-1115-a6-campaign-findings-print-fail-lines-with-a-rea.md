---
id: BUG-1115
title: A6: campaign findings print FAIL lines with a reason, a failed segment reruns from its own save
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-labverdict
commits: 0481ee6e, dd60d4f5, bee6f533
tests: campaign_ct, campaign_segment, tools/tests/labverdict_selftest.py (make test)
signature:
docs: docs/06-testing.md#testing-campaign-segments
related: BUG-0152
---

## Report
Plan 0.7.15 A6 (audit docs/audit-0.7.15/tests.md TST-1, owner decision 10). The campaign is the most expensive test
(23 % of all lab time) and its failures were partly invisible: it printed findings as `FINDING [stuck] ...` /
`[stuck] ...` and the lab collected only lines starting with `FAIL`, so results.json, summary.md and the flake
history showed an empty reason (replay 2026-11-15: 61 of 144 recorded campaign failures had none). A final-check
rerun had to repeat the whole 82 min campaign.

## Root cause
Reporting, not the mod: coop_campaign.py's `finding()` printed no FAIL line; tools/lab.py took `fails` from FAIL
lines only; no save of the campaign's own world was kept, so a failed segment could only be replayed from a fixture.
The jump after a stalled stage change was already on by default (`--no-jump` turns it off, since the first version
on 2026-10-29): CORRECTION recorded in docs/06-testing.md#testing-campaign-segments (the audit read it as off).

## Fix
* coop_campaign.py: every finding that fails the run prints `FAIL <kind> <stage/phase>: <text>` when it is found.
  The lab (tools/labverdict.py `reasons`) also takes the findings, the report's bracket lines, a traceback's exception
  or the timeout as the reason of a failed test without a FAIL line (old logs too).
* Segment saves: at the start of each played stage the host saves (`coop.save`) and the run keeps the slot as
  `<run>/segments/<stage>.tgz` + segments.json (campaign_lib pack_slot / unpack_slot / slot_planet / newest_slot).
  A failed run prints `CAMPAIGN-SEGMENT {...}` (the first failing finding's stage, its save, seed, minutes) and writes
  segment.json; the lab keeps it in the result and prints `rerun only the failed segment ...: lab.py run
  campaign_segment --segment <run>`.
* `coop_campaign.py --save <tgz>` / `--segment <segment.json>`: the host starts on the stage's fixture, loads the
  save in game into slot 30, the guests follow by the world share (the jump's path, now `load_world`); suite entry
  `campaign_segment` (`--segment "$CAMPAIGN_SEGMENT"`), `lab.py run --segment <run id | dir | segment.json>`.
* `--jump` accepted explicitly (the default). Twin of the reporting: `CAMPAIGN_PLANT_STUCK=<stage>` (that stage change
  is not tried, the finding says "planted"). CAMPAIGN_SEGMENTS=0: no saves.
* switch: none (test tooling, no mod change).

## Verification
* FAIL: lab 20261115-012138 (lane 2, `CAMPAIGN_PLANT_STUCK=creature lab.py run campaign_ct`): Cell played, the real
  Cell -> Creature change, segment saves of cell (Game0, Cadian) and creature (Game0, Cadian, 01:26:55), the planted
  stuck Creature -> Tribe, the run went on by the jump to the tribe fixture (segment save Game10, Zorispe) and played
  Tribe; result `FAIL stuck creature/advance: the stage change creature -> tribe did not finish in 10 min`, the
  segment (creature -> tribe, segments/creature.tgz) in results.json and the rerun line printed. 11.4 min.
* PASS: lab 20261115-013313 (lane 2, `lab.py run campaign_segment --segment 20261115-012138`): the host loaded
  Game30 / Cadian.spo from that save (host game log 01:35:22 "loading Cadian.spo ... Game30", shared with both
  guests), Creature played, the real Creature -> Tribe change in ~3 min, Tribe played; 0 findings, RESULT: OK.
  10.7 min instead of the 82 min whole campaign.
* Unit: tools/tests/labverdict_selftest.py `reasons`, `campaign` (pack / unpack, only Games/Game0 members extracted,
  the failed segment), `lab_run` (a campaign's finding as reason, its segment, find_segment); `--replay`: 61 of 61
  empty-reason campaign failures get a reason.
* Not verified in the lab: a failed segment in Space (its save is refused on a planet: the rerun then starts from the
  stage's fixture, or the jumped fixture after a jump, as the code says).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labverdict (unreleased).
