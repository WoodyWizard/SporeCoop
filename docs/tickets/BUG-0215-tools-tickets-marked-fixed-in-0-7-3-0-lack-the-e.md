---
id: BUG-0215
title: Tools: tickets marked fixed in 0.7.3.0 lack the evidence CLAUDE.md asks for, and tickets.py check does not notice (empty commits, no FAIL run, wrong fixed_in)
status: fixed
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #149 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-tools
commits: 964b098
tests: make test (tools_selftest.py tickets_check; tickets.py check)
signature:
docs: 
related:
---

## Report
From GitHub issue #149: https://github.com/WoodyWizard/SporeCoop/issues/149 (the full analysis, links and the scenario are there).

**Summary**

Of the 157 tickets with `fixed_in: 0.7.3.0`, a good number do not meet CLAUDE.md 1 and 3:
- 8 tickets have no FAIL side at all;
- 17 game-test tickets describe a FAIL and a PASS but name no lab run directory;
- 3 fixed tickets have an empty `commits:` field, and one misses a commit;
- one ticket is "fixed in 0.7.3.0" although its only commit shipped in 0.6.0;
- one not-reproduced ticket's guard is listed in the release notes as a fix;
- one new test asserts only a mod counter.

`tools/tickets.py check` passes on all of them (`212 ticket(s), 0 problem(s)`): for a fixed ticket it only requires `fixed_in` and `tests` to be non-empty. Severity: low (process and tooling). This issue lists the cases and proposes checks that catch them automatically without blocking honest tickets.



## Root cause
Confirmed: tickets.py check only required fixed_in and tests of a fixed ticket; release gave every fixed ticket the version whatever its commits. `check --all` on b423f56 lists 74 old problems (report only).

## Fix
tickets.py check: CHECKS framework (rule list, scope new / --all, cached git helper skipped in a shallow clone). Rules for fixes after 0.7.3.0: commits listed, existing, in history and not all in an earlier release; tests name a suite, file or make test; Verification names FAIL and PASS with lab run ids for a lab test, or `no-fail-run: <reason>`. For every ticket: no path outside the repository, known `switch:` names. `release` refuses fixed tickets without a commit of the release (--force). Skills and `tickets.py new` say it. Data: BUG-0207/0208/0209 commits, 9d287af for BUG-0028 (no-fail-run), BUG-0167 not-reproduced (its only commit is in v0.6.0), FAIL logs and PASS lab runs named in 18 tickets (found in the lab folders and agent logs), scratch paths removed from 12 tickets, CHANGELOG BUG-0172 line corrected.

## Verification
tools_selftest.py tickets_check, 13 cases on a temporary repository. FAIL: with SELFTEST_TICKETS=<b423f56 tickets.py> 10 of 13 fail (every rule and the release guard). PASS: 13/13 on fix/w5-tools; `tickets.py check`: 255 tickets, 0 problems. Not done: coop_social_console.py still decides on the mod counter acts_in (the issue's item 6, a game test change: left open).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (964b098). Left: BUG-0149's test oracle (item 6 of #149).
