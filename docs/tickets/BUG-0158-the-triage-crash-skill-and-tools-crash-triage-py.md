---
id: BUG-0158
title: the triage-crash skill and tools/crash_triage.py are referenced but do not exist
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#137
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: ee9b778
tests: tools/tests/tools_selftest.py triage (make test)
signature:
docs:
related: #137
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/137 (full text, discussion).

CLAUDE.md section 5 lists three agent skills, including `triage-crash` ("turn a crash report into a build, a signature and an action"). Only two exist in `.claude/skills/`: `fix-player-report` and `merge-batch`. The fix-player-report skill and the agent brief template in docs/10-dev-pipeline.md also refer to `tools/crash_triage.py`, which is not in the repository. An agent following the documented steps for a crash report finds neither.

### Where
- CLAUDE.md skill list: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L133-L136
- fix-player-report step 1: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/.claude/skills/fix-player-report/SKILL.md#L12-L15
- Agent brief template: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/10-dev-pipeline.md#L143
- The skills that exist: https://github.com/WoodyWizard/SporeCoop/tree/563f72d98cce2acf2c44b859f1dfe32f9588d401/.claude/skills

### How to reproduce
**How to reproduce / check**

`ls .claude/skills tools/crash_triage.py` on main.

### Expected
The documented skill and tool exist, or the docs list only what exists.

## Root cause
**What happens**

- The claims:
  - CLAUDE.md: "`.claude/skills/`: `fix-player-report` (...), `triage-crash` (turn a crash report into a build, a signature and an action), `merge-batch` (...)".
  - SKILL.md: "**Identify**: `tools/tickets.py find <address | symptom>` and (when it exists) `python3 -I tools/crash_triage.py <file>`".
  - 10-dev-pipeline.md brief: "Context: <known-issues anchor, docs/re anchor, crash_triage output>".
- The reality: `ls .claude/skills` shows `fix-player-report` and `merge-batch` only, and `tools/crash_triage.py` does not exist. The skill hedges ("when it exists"); CLAUDE.md and the brief template do not.

### Confidence
Confirmed by listing the tree. A second reviewer verified it.

## Fix
Both written (the tool is small): `tools/crash_triage.py` reads crash reports, hang reports and
the fault lines of sporekit.log (files, folders, zips read in memory) and prints the build, the
signature `<exe hash 8>:<where>` (instance tags folded: coop.i7.1.dll -> coop.dll), the first own
frames, the tickets that already name it (`tickets.py find` on the place, the address, the symbol,
the first own frames) and the action. `.claude/skills/triage-crash/SKILL.md` is the checklist;
fix-player-report and the brief template in docs/10-dev-pipeline.md point at it.

## Verification
`tools/tests/tools_selftest.py triage`: a made-up report of the known crash 0xC6AE60: build, version, stage, signature
read, BUG-0018 found.
- FAIL with the tool missing (main): "tools/crash_triage.py exists" FAIL.
- PASS with the fix. Also run on a real lab crash report (lane 4) and on a game log with a fault line.

### Related
None of #7 to #82 covers this.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #137.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
