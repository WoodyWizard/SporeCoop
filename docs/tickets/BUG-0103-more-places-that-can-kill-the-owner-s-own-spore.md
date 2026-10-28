---
id: BUG-0103
title: more places that can kill the owner's own Spore on lane 1 (BUG-0025)
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#81
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 14463ee
tests: tools/tests/tools_selftest.py kill (make test)
signature:
docs:
related: #81
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/81 (full text, discussion).

BUG-0025 records that `spore.sh kill` on lane 1 is not PID-scoped and can kill a Spore the owner started from the real install. Two more places do the same, and the planned fix (PID files per run) has to cover them too: `lab.py kill` includes lane 1 by default, and `coop_creature_desync.py` SIGKILLs every `SporeApp.exe` of the real install that was started without `-sporekit-instance`, which is exactly how the owner starts the game.

### Where
- Ticket: ``https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/tickets/BUG-0025-spore-sh-kill-on-lane-1-could-kill-a-spore-the-o.md``
- `lab.py kill`, default `--lanes 5` from lane 1: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L295-L306
- `lab.py run` takes lane 1 first unless `--skip 1`, and kills it at the start and end of every run: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L143 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L151-L152
- `coop_creature_desync.kill_game`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_creature_desync.py#L125-L135

### How to reproduce
By reading. On a machine where the owner plays Spore from the lab's real install: `python3 tools/lab.py kill`, or `SPORE_LANE=1 python3 tools/tests/coop_creature_desync.py`, ends the owner's game.

### Expected
Tools and tests only kill processes they started: PIDs recorded by `spore.sh run` per lane and instance (the BUG-0025 plan), or at least never lane 1 unless asked for explicitly.

## Root cause
**What happens**

```python
k = sub.add_parser('kill')
k.add_argument('--lanes', type=int, default=5)
...
for lane in range(1, a.lanes + 1):
    spore(ROOT, lane, 'kill')
```

`lab.py kill` with no arguments runs `spore.sh kill` for lanes 1..5, so it hits the BUG-0025 case on lane 1. It also misses lanes 6..10, although the lab can run up to 6 lanes and lanes go to 10.

```python
folder = f'\\lane{lane.LANE}\\' if lane.LANE > 1 else '\\SPORE\\SporebinEP1\\'
...
if folder in cmd and ((arg in cmd + '\0') if inst != 1 else '-sporekit-instance:' not in cmd):
    subprocess.run(['kill', '-9', pid])
```

On lane 1, `kill_game(1)` (simulating a host crash) SIGKILLs every SporeApp.exe whose command line has the install's folder and no `-sporekit-instance:` argument. The owner's own game started normally matches; the test's host (started with `-sporekit-profile:test` but instance 1, so no instance argument) matches too, and there is no way to tell them apart. The folder name `SPORE` is also hard-coded, so on an install with another folder name the function kills nothing and the test fails for the wrong reason.

### Confidence
CONFIRMED by reading.

## Fix
- `spore.sh kill` on lane 1 ends only test games (BUG-0025).
- `lab.py kill` cleans lanes 2..10 by default (`--lanes 2,3` to choose); lane 1 only with
  `--include-lane-1`, and then only its test games.
- `coop_creature_desync.kill_game` asks `spore.sh pids <inst>` (the lane's real folder, no
  hard-coded `SPORE`; on lane 1 only test games, so instance 1 is the test host started with
  `-sporekit-profile:test`, never the owner's game).
- `lab.py run` still uses lane 1 unless `--skip 1`: with the kill fix that no longer endangers the
  owner's game; whether the lab should skip lane 1 by default is left to the lab harness (wave 4,
  fix/w4-lab).

## Verification
`tools/tests/tools_selftest.py kill`: `spore.sh pids 1` on lane 1 returns only the test host, not the owner's fake
game; `pids 2` only the guest; lane 1 kill leaves the owner's game. FAIL with main's spore.sh (no
`pids`, the owner's game killed), PASS with the fix. `lab.py kill` by reading (it only calls
`spore.sh kill` for lanes 2..10).

### Related
docs/tickets/BUG-0025; CLAUDE.md section 3 ("Tests must not touch the player's own Spore profile"; pitfalls about killing processes).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #81.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
