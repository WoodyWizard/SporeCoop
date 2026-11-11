---
id: BUG-0916
title: lab: a test script took 17 GB (the detector's bodyrec read never ended in a 4-player Cell), four campaigns filled the machine's memory
status: fixed
kind: tooling
stage: lab
reported: 2026-11-11
source: the final check of 0.7.12 (rel/0.7.12 lab out/lab/20261111-000117, 4 lanes)
role: 
versions: 
fixed_in: unreleased
branch: fix/w47-lab-memory
commits: a0630fc8
tests: campaign, campaign_ct, campaign_short
signature:
docs: docs/06-testing.md#testing-lab-memory
related: BUG-0609, BUG-0663, BUG-0917
---

## Report
The final check of 0.7.12 (rel/0.7.12 1e37ae95, four lanes, `SPORE_DETECTOR=20`) started the four campaigns at once
(`campaign`, `campaign_ct`, `campaign_space`, `campaign_tribe`: 16 games). About 11 minutes in the machine was at 60
of 62 GB with the swap full and nearly froze; `python3 tools/tests/coop_campaign.py --minutes 8` (the test
`campaign`, 675 s old, its Creature play just begun) was seen at 16.9 GB RSS and seconds later at 3.6 GB. The run was
stopped by hand. The same final check had passed for 0.7.11 two days before.

## Root cause
Observed (the campaign alone on lane 9 with the old code under a watchdog, lab out/lab/20261111-001707;
`SPORE_MEMPROBE=1` wrote the script's RSS every second and every thread's stack past 300 MB):

* The script's RSS: 44 MB 24 s into the Cell play, 405 MB at 0:39, 793 MB at 0:54, 1.2 GB at 1:09, 2.0 GB at 1:40,
  2.9 GB at 2:10, 3.7 GB at 2:40, 4.1 GB at 2:56 and 4.2 GB when the watchdog stopped the run: +27 MB/s from the
  detector's second period on. The four games stayed at 0.5 GB each.
* Every stack dump (12, from 323 MB on) has the monitor thread in `monitor.Watch.detect_trail` ->
  `body_trail.since` -> `Console.call('coop.bodyrec since ...')`.
* The live games: the host listed 1100 rows a frame (4 players, 1096 cells), its ring of 400000 rows was 4.9 s long,
  a page of 2000 rows held one whole frame and 900 rows of the next; a guest listed 325 rows a frame.

`since` (BUG-0609, in the tree since 0.7.9) asked for the next page from the printed time of the last whole frame
and went on until a page came back short. The game prints a row's time with 4 decimals and answers "rows after t" on
the full double, so a frame whose time was rounded down came back again. With several whole frames in a page that
repeats one frame and goes on; with exactly one whole frame in a page (over 1000 rows a frame) the reply is the same
page again until the ring drops the frame, then the next frame sticks: the read stays 5 s behind "now" for as long
as the stage lasts and keeps every copy. It ended only when the Cell stage ended (16.9 GB after 9.5 minutes of it;
the 3.6 GB seconds later is the list freed). Two of the four campaigns start in Cell, so about 34 GB of scripts on a
machine where other programs held about 34 GB; the 16 games were about 9 GB.

Not new in 0.7.12 (inferred from the logs, nobody measured the memory then): a stuck read prints no detector
sample, and the final check of 0.7.11 (rel/0.7.11 lab 20261108-114435) has 4 `DETECTOR-SAMPLE` lines of stage Cell
in `campaign.log` and 4 in `campaign_ct.log` over an 8-minute Cell play, as the 0.7.12 run has (4 and 5); with the
fix the same play gives 69. The suspects named in the report (key frames, the hunt traces, worlddiff / census dumps,
monitor.py reading the logs, the kit's 1 MB replies) were not it: with the fix the whole campaign's script stays
under 90 MB.

## Fix
* tools/tests/body_trail.py `since`: the next page starts half a printed digit past the last whole frame (`T_EPS`),
  a page with fewer than three whole frames is asked for larger (to 8000 rows; a frame larger than the page: to
  16000), only the kinds the caller wants are kept (the detector: `detect_kinds`, no cells), and a read ends at 120000
  kept rows, 120 pages or 10 s (`since.cut`). `SPORE_BODYTRAIL_OLD_SINCE=1`: the old loop (the twin).
* tools/tests/monitor.py `detect_trail`: passes the judged kinds; after a cut read the next period starts from now.
* The guard, tools/lab.py: `MemSampler` (every test: the script's peak RSS, the lane's games' peak RSS, the lowest
  MemAvailable into `results.json` `mem` and the summary's `## Memory` table; a script over 2048 MB,
  `SPORE_LAB_SCRIPT_RSS_MB`, is killed and the test fails with `FAIL lab: the test script used N MB of memory`),
  `MemGate` (a test starts only when its games fit: `suite.games(t)` x `SPORE_LAB_GAME_GB` 1.0 with
  `SPORE_LAB_MEM_FLOOR_GB` 8 left of MemAvailable; else it waits, see docs/06-testing.md#testing-lab-memory).
* Tools: tools/memwatch.py (a watchdog beside a run), tools/tests/memprobe.py (`SPORE_MEMPROBE=1|trace`).

## Verification
All on lanes 9 / 10 of fix/w47-lab-memory, `SPORE_DETECTOR=20`, each run beside `tools/memwatch.py` (its log is
`memwatch.log` in the run folder).

* FAIL (the old read): lab 20261111-001707, `campaign` alone on lane 9: the script at 4.2 GB 3 minutes into the Cell
  play (27 MB/s), the run stopped by the watchdog; 3 Cell detector samples, then none.
* PASS (the fix): lab 20261111-002733, the `campaign` suite on lane 9, one test at a time (124 min): the script's peak
  RSS 84 MB in `campaign` (81 min, Cell to Space; 69 Cell samples), 71 MB campaign_ct, 56 MB campaign_short, 54 MB
  campaign_tribe, 50 MB campaign_space; no read was cut. campaign_ct, _space, _tribe, _short passed; `campaign`
  ended FAIL on one caught game fault that is not this bug (BUG-0917).
* The other long tests: lab 20261111-002757 (lane 10: play_cell 46 MB, play_creature 58, play_tribe 53, play_civ 42,
  play_space 53, long_creature_short 35, long_tribe_short 42; 7 of 7 passed), lab 20261111-014656 (long_creature,
  45 min: 38 MB; it failed on BUG-0917's fault too).
* Self-tests (make test): `tools_selftest.py trail` (a model of the game's `coop.bodyrec since`; with
  `SPORE_BODYTRAIL_OLD_SINCE=1` the read does not end on it), `lab_selftest.py` mem_script (a 400 MB script over a
  150 MB limit is killed and fails, a small one passes with its peak recorded) and mem_gate (the fourth campaign at
  once waits and starts when two ended; not started after the wait with nothing running).
* The games' memory and the budget of a 4-lane final check: docs/06-testing.md#testing-lab-memory.

## History
- 2026-11-11: opened.
- 2026-11-11: cause measured, fixed with the lab's guard (fix/w47-lab-memory).
