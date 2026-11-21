---
id: BUG-1362
title: Lab summary: a test that passed in this run is still listed under 'Ticket candidates (INCONCLUSIVE in a row)'
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-20
source: lab 20261120-133703
role: host
versions: 0.7.16 (unreleased)
fixed_in: unreleased
branch: fix/w61-creature-e
commits: 55d39747
tests: unit (tools/tests/labverdict_selftest.py lab_run)
signature:
docs:
related: BUG-1346
---

## Report
The summary.md of the second 0.7.16 final check (rel/0.7.16 worktree out/lab/20261120-133703) listed
social_sing_own_lowfps ("INCONCLUSIVE in its last 5 runs") and social_sing_partner ("in its last 3") under "Ticket
candidates (INCONCLUSIVE in a row)", and the fix brief took them as failures of the release rule. Both were judged OK
in that very run (their logs: RESULT: OK, every check printed; BUG-1346's test fix 48dfc9fa was in 673f7a55).

## Root cause
Observed in tools/lab.py summary_history: the candidates came from `labverdict.ticket_candidates(ledger)` where the
ledger is the history BEFORE this run, filtered only by "the test ran in this run". A judged result in this run (PASS
or FAIL) ends the streak, but the summary did not look at it. The run's own verdict was right (streak_fails only fails
a test INCONCLUSIVE in this run too); only the summary's list was stale. All earlier INCONCLUSIVE runs of both tests
were before 48dfc9fa (rel/0.7.15.1 -110806, 0.7.15.2 -134852 / -144132, rel/0.7.16 20261119-214823 and
20261120-041304 at f6c16890, which does not contain 48dfc9fa).

## Fix
lab.py summary_history: a candidate is a test INCONCLUSIVE in this run (and not judged in another copy of it), its
streak counted as the history's streak plus this run's INCONCLUSIVE copies (twin, fix-off and quick runs left out, as
in streak_fails). A tooling fix, no game code, no switch.

## Verification
labverdict_selftest.py lab_run (make test, unit-lab, no game): three INCONCLUSIVE runs in a row put the test under
"Ticket candidates" in summary.md, a PASS in the next run takes it off. With the lab.py of 673f7a55 both new checks FAIL
(the third INCONCLUSIVE was missing from the list: the history's streak was 2; the PASS run was listed: the history's
streak was 3); with the fix both PASS (fix/w61-creature-e).
* no-fail-run: a tooling fix checked by the unit self-test, no lab run needed.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-creature-e): fixed in lab.py summary_history, self-test case added.
