---
id: BUG-0244
title: Tests: the setup's release check can undo a stored result and the BUG-0077 test cannot see it; the BUG-0142 test lacks the 0.7.2.x ANSI upgrade case
status: fixed
kind: test
stage: All
reported: 2026-10-28
source: GitHub #178 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-tools
commits: fd8c716
tests: make test (release_tsan, make test-tsan), win_unit (tests/win_tests.cpp test_ini_unicode)
signature:
docs: docs/06-testing.md#testing-make-test-needs
related:
---

## Report
From GitHub issue #178: https://github.com/WoodyWizard/SporeCoop/issues/178 (the full analysis, links and the scenario are there).

**Summary**
Two tests added in release 0.7.3.0 do not guard what their tickets are about. `tests/release_tsan.cpp` (BUG-0077) runs ThreadSanitizer on `upd::ReleaseBox` alone; meanwhile the setup's check thread does a read-then-write on the box under two separate locks, which can overwrite a good release the install job just stored (a minor status glitch the test cannot catch). `test_ini_unicode` (BUG-0142) has no case for a `coop.ini` written by 0.7.2.x in the ANSI codepage, which is the file every upgrading player has. Severity: minor (setup status) and test gaps. From BUG-0077 (commit eaf0a7c) and BUG-0142 (commit 363db2b).

**Where**
- `tests/release_tsan.cpp`: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/tests/release_tsan.cpp
- `make test-tsan

## Root cause
Confirmed by reading: setup.cpp check_thread did `if (!g_rel.get().ok || ri.ok) g_rel.set(ri);`, two separate locks; release_tsan only exercised the box. test_ini_unicode had no ANSI (0.7.2.x) coop.ini case.

## Fix
`upd::ReleaseBox::merge` compares and stores under one lock (keeps an ok release against a failed one); check_thread calls it. Switch `upd::g_release_merge_fix` (setup only, `// not a lab switch`): `release_tsan --old-merge`, setup `/oldmerge`. A test seam `ReleaseBox::test_between` runs another writer inside the old window. win_tests: the ANSI case builds the name in the current ANSI codepage (WC_NO_BEST_FIT_CHARS, falls back to other names; NOT RUN line when the codepage is UTF-8), reads it back as UTF-8 and checks the file on disk became UTF-16 LE with a BOM.

## Verification
FAIL: `release_tsan --old-merge` prints "merge: FAILED" and exits 1 (the install job's good release stored inside the window is overwritten); `make test-tsan` asserts that. ANSI case: with the CP_ACP fallback in sporekit/inifile.cpp replaced by CP_UTF8, a standalone `SPORE_LANE=7 win_unit.py` run FAILED at win_tests.cpp:314 and :319 (name read wrong, file not converted), log not kept. PASS: release_tsan ALL OK, `--old` still a TSan data race; lab run out/lab/20261028-225319 (fix/w5-tools, lane 7): win_unit ok, "ANSI codepage 1252", ALL OK.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (fd8c716).
