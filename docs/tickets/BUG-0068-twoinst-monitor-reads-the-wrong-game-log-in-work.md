---
id: BUG-0068
title: twoinst Monitor reads the wrong game log in worktrees, world loads over 10 s become FROZE
status: fixed
kind: test
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#46
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-lab
commits: 824813c, 6f2d12e, f8b9733, bb7154e
tests: tools/tests/lab_selftest.py (make test)
signature:
docs: docs/10-dev-pipeline.md 10.3/10.4, docs/06-testing.md
related: #46
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/46 (full text, discussion).

`twoinst.Monitor.game_log()` builds the path of the game log from the location of the source tree. That only works for the main checkout inside the game folder. In a `../SporeCoop-wt/<branch>` worktree, where every agent's `lab.py run --branch` runs, the path does not exist. `loading()` is then always false, and the 60 s allowance for world loads never applies: any load that blocks frames for more than 10 s is reported as `FROZE` and fails the test. `update_check.py` has the same path mistake and works on a folder that does not exist.

### Where
- https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/twoinst.py#L67-L85
- https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/update_check.py#L122-L124
- The right way, already in `lab.py`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L69-L71 (and `tools/spore.sh` `MAIN_GAME`, `coop_rejoin_net.py` `log_path`)

### How to reproduce
Lay out a fake game folder: clone the repo to `<tmp>/SPORE/SporeCoop` and add a worktree `<tmp>/SPORE/SporeCoop-wt/fix-a`. Then, in the worktree:

```sh
SPORE_LANE=2 python3 -c "
import sys; sys.path.insert(0,'tools/tests'); sys.path.insert(0,'tools')
import twoinst, lab
print(twoinst.Monitor(twoinst.lane.HOST,'host').game_log()); print(lab.lane_game(2))"
```

Output:

```
<tmp>/SPORE/SPORE-lab/lane2/SporebinEP1/mods/logs/sporekit.3.log
<tmp>/SPORE-lab/lane2
```

The monitor and the lab disagree; the lab (and `spore.sh`) are right. Lane 1 prints `<tmp>/SPORE/SporeCoop-wt/SporebinEP1/...` against `<tmp>/SPORE`.

### Expected
The monitor reads the real game log of the lane from any worktree, so a world load gets its 60 s.

## Root cause
**What happens**

```python
ln = (self.inst + 1) // 2
root = os.path.dirname(lane.ROOT)
game = os.path.join(root, 'SporebinEP1') if ln == 1 else \
    os.path.join(os.path.dirname(root), 'SPORE-lab', f'lane{ln}', 'SporebinEP1')
```

With the main checkout at `<games>/SPORE/SporeCoop` this gives `<games>/SPORE/SporebinEP1` and `<games>/SPORE-lab/laneN`, which is right. With a worktree at `<games>/SPORE/SporeCoop-wt/fix-x` it gives:

- lane 1: `<games>/SPORE/SporeCoop-wt/SporebinEP1/mods/logs/sporekit.log`
- lane N: `<games>/SPORE/SPORE-lab/laneN/...`

Neither exists. `loading()` catches the `OSError` and returns False, so `run()` uses `FREEZE` (10 s) instead of `LOAD_FREEZE` (60 s) and appends `FROZE (no frame for 10 s ...)`, which `Checks.faults()` turns into a failure. The class comment says civ planets took 14 s to load with 16 games running.

`update_check.py` uses `dirname(dirname(ROOT))`, which gives `<games>/SPORE/SPORE-lab/laneN` in a worktree, so its file operations on the lane (restoring the game's own files, removing `mods`) do nothing.

### Confidence
CONFIRMED by running (path computation); the false FROZE follows from the code (CONFIRMED by reading).

## Fix
`lane.game_dir(n)` / `lane.game_log(inst)` from the git common dir (as lab.lane_game); `twoinst.Monitor.game_log`, update_check.py (which wiped a folder that does not exist from a worktree), diag_check.py, kit_crash.py, kit_freeze.py use them. BUG-0203 is the same bug (closed as duplicate).

Suggested in the review:

One helper in `lane.py`, used by `twoinst.Monitor`, `update_check.py`, `diag_check.py` (which hard-codes `~/Games/Modding/SPORE`) and `coop_rejoin_net.py`:

```python
def game_dir(n=LANE):
    common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                            cwd=ROOT, capture_output=True, text=True).stdout.strip()
    main = os.path.dirname(os.path.dirname(common))
    return main if n == 1 else os.path.join(os.path.dirname(main), 'SPORE-lab', f'lane{n}')
```

## Verification
A self-test of the tool (no game): case game_log (fake layout SPORE/SporeCoop + SporeCoop-wt/fix/x, lanes 1 and 2, from both checkouts). `python3 tools/tests/lab_selftest.py` (in `make test`): all cases PASS on fix/w4-lab; `--tree <main checkout>` (main 7874654): this case FAILs (every case except clean / harness_once and the main-checkout game_log lines fails there). Smoke on lane 2 with the new lab: out/lab/20261028-030019 (13/14, the one FAIL was the self-test's own fake fault line seen by the outer lab, fixed in f8b9733).

Planned in the review:

A unit test with the fake layout above (no game): `Monitor.game_log()` must equal `lab.lane_game(lane)/SporebinEP1/mods/logs/...` from the main checkout and from a worktree. It fails today from the worktree.

### Related
CLAUDE.md section 4 (parallel work in git worktrees), section 3 (lab runs). docs/10-dev-pipeline.md 10.3.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #46.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-lab (wave 4), test: lab_selftest.py.
- 2026-10-28: released in 0.7.3.0.
