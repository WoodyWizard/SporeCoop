---
id: BUG-0247
title: Tools: crash_triage.py gives exit-time crash reports a different signature per report, and finds no signature in sporekit.log or in a short crash file
status: fixed
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #181 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-tools
commits: 19084de
tests: make test (tools_selftest.py triage)
signature:
docs: 
related:
---

## Report
From GitHub issue #181: https://github.com/WoodyWizard/SporeCoop/issues/181 (the full analysis, links and the scenario are there).

**Summary**
`tools/crash_triage.py` is new in 0.7.3.0 (BUG-0158, commit ee9b778). It turns a crash report into a signature `<exe hash>:<where>` and looks for earlier tickets. Three cases go wrong:
- When the report was written on the game's main thread, `where` keeps the text `(thread N, the game's main thread)`, so every such report gets its own signature. Today these are the reports of a game that ended right after a fault (`exit_report`). #182 will make every main-thread crash report look like this.
- In `sporekit.log`, the `the game crashed: ...` line is found only when it is the last line of the file.
- The short crash file that SporeKit writes first, before the full report, is not recognised: no signature, and no exe hash.

Severity: minor (triage tool). One crash splits into several

## Root cause
Confirmed with the three inputs of the issue: REPORT_EXC's optional thread group did not match `(thread N, the game's main thread)`, FAULT had no re.M (the crash line counted only as the last line of a log) and did not accept text between the code and "at"; the short crash file has no exe hash.

## Fix
crash_triage.py: `clean_where()` strips any thread text before signature, search terms and dedupe; FAULT with re.M accepts every exception line form SporeKit writes (fault logger, crash line, stack overflow line, short crash file); faults are ordered report exception, `the game crashed:` lines, then the rest; `build_of` maps the build name back to its hash (GOG 2016, GOG 2024, Steam, `unknown build xxxxxxxx`); control characters stripped from printed fields.

## Verification
tools_selftest.py triage cases (a) to (e), exact line match. FAIL: with SELFTEST_TRIAGE=<b423f56 crash_triage.py> (a) to (d) FAIL (signature with the thread text, no signature in a log, build unknown for the short file, the crash line not first); (e) passes there too. PASS: all ok on fix/w5-tools.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (19084de).
