---
id: BUG-1535
title: Tests: campaign_tribe_noclick stuck at goal bar 1/5: the host's raid click fails for 30 min (view stays at its avatar 90 m off its members: 'no member selected', 'the view did not reach the hut'; BUG-1091 class again)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 0.7.16, 0.7.16.1
fixed_in: 0.7.99
branch: rel/0.7.99
commits: 89c2fe81
tests: campaign_tribe_noclick
signature:
docs:
related: BUG-1091, BUG-1165, BUG-1169, BUG-1460
---

## Report
0.7.16.1 final check (lab 20261123-111750, rel/0.7.16.1 30be883d, `--repeat 3`): campaign_tribe_noclick#2 (lane 3)
"FAIL stuck tribe/advance: the stage change tribe -> civ did not finish in 38 min (goal bar (1.0, 5.0))"; #1 and #3
passed. Same shape as 20261123-023643 (rel/0.7.16, mod code 5376d388, before any 0.7.16.1 change, goal bar 3/5).
`lab.py flaky`: known flake, 10 of 18 failed on rel/main, 17 of 31 on all branches since 2026-11-13.

## Root cause
Test driver (observed in the run's log, not the mod):
* Village 1 fell at 368 s; then every raid without the host's own click (village 3 planted by CAMPAIGN_PLANT_NOCLICK
  until 11:50, villages 2, 4, 3 after it) left its hut at 750/750 (only the forced huthp cut brought village 4 to 92).
  A guest-only raid makes no goal (BUG-1068), so the bar stayed at 1 for 31 min.
* The host's click failed 18 times: "no member selected (4-8 own)" 10x, "the view did not reach the hut" 8x. The
  BUG-1091 diagnostics: 11 of 17 with "own on screen 0/N, members on screen 0, objs 250, view centre (-134, -458,
  -165)": the host's view sits at its avatar position (the host trace `pos=(-134.3 -457.8 -164.9)`) about 90 m from
  its members at (-150..-190, -470, -60..-110), and `look()` of the home village or the hut does not move it there
  (look_far 93 for the host bot against 24-30 for the guests). Screenshot out/lane3/shots/bug1091_115712.png: a hill
  side with no member, the HUD normal.
* Any bot's click came in only 2 of 6 raids (villages 1 and 4; the planted village 3 gets none by design), so the forced `coop.tribe order` (0-18
  members) had nothing to make a goal from.
* The same counts in history: failures 20261123-023643 (reach=8, no member selected 10, screen 0 12),
  20261115-133237 (reach 8), 20261115-034753 (reach 10); passing runs have 0-6.
Not the 0.7.16.1 mod changes: no exception line in any of the 12 game logs of the run (no 0x00C8F6D3, `faults 0`); the
pattern is older than 0.7.16.1. The makeway_null / bodies counters are only in `coop.status`, which the run did not
keep (the campaign now saves it with each finding).

## Fix
Not yet. Diagnostics (fix/w69-triage2): every finding's evidence now keeps each player's `coop.status`
(`<tag>_<name>.status.txt`) and prints its `tribe bodies:` line (BUG-1460's makeway_null skipped=, bodies stilled=).
Proposal for the driver: when the view centre stays at the avatar after `look()` (BUG-1091 diag "members on screen 0"
twice in a row), re-centre on the members the way a player does (the HUD's home button, or a click on a member's
portrait), then box-select; else fall back to the `coop.tribe` host click of BUG-1068's path for that raid.

## Verification
PASS: out/lab/20261124-152417 (campaign_tribe_noclick, 0 faults: stuck host Tribe view recovered, all 5 villages conquered, advanced into Civ).
no-fail-run: test-driver recovery in tools/tests/play_tribe.py (no gameplay fix switch).

## History
- 2026-11-23: opened (fix/w69-triage2, triage of the 0.7.16.1 final check); campaign evidence keeps coop.status.
- 2026-11-24: released in 0.7.99.
