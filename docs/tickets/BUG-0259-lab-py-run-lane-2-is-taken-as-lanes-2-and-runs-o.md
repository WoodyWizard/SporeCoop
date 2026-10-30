---
id: BUG-0259
title: lab.py run --lane 2 is taken as --lanes 2 and runs on lane 1
status: fixed
kind: tooling
stage: all
reported: 2026-10-29
source: fix/w5-cell, 2026-10-28
role: 
versions: 
fixed_in: 0.7.3.1
branch: main
commits: 11b61eb
tests: hotreload
signature:
docs:
related:
---

## Report
`lab.py run` has no `--lane` option (only `reload` and `kill` have one), but argparse accepts unique prefixes of long options, so `lab.py run cell_ko_gap --lane 2` parses as `--lanes 2`: the run takes two free lanes starting at lane 1 and ran the test on lane 1, which agents must never use. Seen on fix/w5-cell (main 8f1944b): runs 20261028-235046, -235143, -235236 ran on lane 1 although lane 2 was meant (the games were stopped by the lab afterwards). The wave 5 coordinator's note said `--lane <n>` is the new way and `--lanes N` is refused; in 8f1944b neither is true for `run`.

Reproduce: `python3 tools/lab.py run unit --lane 2` and read the lane column of the summary.

Suggested fix: `allow_abbrev=False` on lab.py's parsers, and a real `run --lane N` if that is the intended interface.

## Root cause
argparse matches unique prefixes of long options by default (`allow_abbrev=True`), and `run` had only `--lanes`.

## Fix
`allow_abbrev=False` on the parser and every subparser of lab.py, and a real `run --lane N` (repeatable; refused for a lane outside 1..10 or in --skip). The wave 5 coordinator's wrong note was corrected to the agents still running.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: argparse behaviour, shown by the report's runs 20261028-235046, -235143, -235236 (lane 1). After the fix: `lab.py run x --lan 2` exits 2 (unrecognized arguments); `lab.py run hotreload --lane 3` ran on lane 3 (out/lab/20261029-003406, OK).
ach with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
- 2026-10-29: released in 0.7.3.1.
