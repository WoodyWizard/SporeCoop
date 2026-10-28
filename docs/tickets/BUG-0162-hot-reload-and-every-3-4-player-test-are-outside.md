---
id: BUG-0162
title: hot reload and every 3-4 player test are outside the lab suites, so they never run
status: fixed
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#141
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tests
commits: 95cc55a, 352f164
tests: hotreload, multi3, npc_multi3
signature:
docs: docs/06-testing.md#testing-w4-tests
related: #141
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/141 (full text, discussion).

Three test scripts that cover advertised features are in no suite of `tools/tests/suite.py`, so `lab.py run smoke|full|nightly` never runs them:
- `hotreload_check.py`: hot reload of coop.dll, the documented dev loop `lab.py reload`.
- `coop_multi.py`: a 4-player chaos session with rejoins.
- `coop_npc_multi.py`: NPC sync with 4 players.

The two multi-player tests need a second lane (`SPORE_LANE_B`), and the lab gives each test exactly one lane, so there is no way to run them in the lab at all. As a result, no 3- or 4-player path is ever tested automatically, although the README advertises "2-4 players" and the stability report lists `coop_multi.py` as the 4-player test. (`coop_creature_social.py` is also not in a suite, but on purpose: docs/06-testing.md 6.24 and BUG-0021.)

### Where
- The suite list (no entry for these scripts): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/suite.py#L22-L111
- `hotreload_check.py`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/hotreload_check.py#L1-L3
- Second lane in coop_multi.py: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_multi.py#L1-L14 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_multi.py#L34
- Second lane in coop_npc_multi.py: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_npc_multi.py#L1-L14
- The lab runs a test with one `SPORE_LANE`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L99-L102
- The 2-4 player claim: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/README.md#L11 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/stability-report.md#L15
- The dev loop that hotreload_check covers: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L104

### How to reproduce
**How to reproduce / check**

Run the loop above, or `python3 -c "import sys; sys.path.insert(0,'tools/tests'); import suite; print(sorted(t['name'] for t in suite.T))"`.

### Expected
- hot reload runs at least nightly;
- the lab can run a test that needs two lanes;
- at least one 3- or 4-player scenario runs nightly.

## Root cause
**What happens**

```
$ for f in tools/tests/*.py; do grep -q "$(basename $f)" tools/tests/suite.py || echo $f; done
... tools/tests/coop_creature_social.py  tools/tests/coop_multi.py  tools/tests/coop_npc_multi.py
    tools/tests/hotreload_check.py  (plus helpers: twoinst, lane, fixtures, ...)
```

- `coop_multi.py` reads `SPORE_LANE_B` (default `SPORE_LANE + 1`) and starts the extra guests in that lane's bottle. The lab neither reserves nor passes a second lane, so even a hand-added suite entry would collide with the test running on lane N+1.
- Code that only runs with 3+ players (player ids 2-3, relays between guests, roster rows, "Jump to" with three players) has no automated test. Several open bugs live there (#50, #53, #54, #57, #42).
- `hotreload_check.py` uses `twoinst.start` and `Checks` and would run as is. It is simply missing, so a hot-reload regression goes unseen (#38).

### Confidence
Confirmed by reading. A second reviewer verified it, and noted that coop_creature_social is excluded on purpose.

## Fix
Fixed on fix/w4-tests (95cc55a, 352f164), differently from the suggestion: no two-lane scheduling.
A third and fourth player run in the test's own lane: instances 20+N and 30+N in lane N's bottle
(`coop_multi.extra_instances`; console ports 47820..47839 and co-op ports 47830..47849 belong to no
lane). `SPORE_LANE_B` is now optional (its old default, lane N+1, was another lab worker's lane).
Suite `extended` (`lab.py run extended`, also in nightly, not in full or smoke): `hotreload`
(hotreload_check.py), `multi3` (coop_multi.py --fixture creature --players 3 --minutes 3) and
`npc_multi3` (coop_npc_multi.py --players 3). Four players in one lane (4 games on one lane's GPU
share) were not tried: `--players 4` works the same way when wanted.

## Verification
- `lab.py run extended`, lane 5: out/lab/20261028-030927, 3 of 3 passed (multi3: Bob and Carol in the
  host's world, sync p90 0.17-0.67 m over 3 rounds; npc_multi3: NPC p90 0.25/0.35 m; hotreload).
- A suite membership change: nothing to fail without it.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #141.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fix/w4-tests: extended suite with hot reload and 3 players in one lane; 3/3 passed.
- 2026-10-28: released in 0.7.3.0.
