---
id: BUG-0216
title: Docs: release notes 0.7.3.0 and README claim more than the code does (an open ticket as fixed, the protocol, GOG 2016 fixes that are off)
status: fixed
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #150 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-tools
commits: 7cbae8e
tests: make test (re_lint.py, tools_selftest.py relint, release)
signature:
docs: data/symbols/frozen_off_922af7c7.txt
related:
---

## Report
From GitHub issue #150: https://github.com/WoodyWizard/SporeCoop/issues/150 (the full analysis, links and the scenario are there).

**Summary**

CHANGELOG 0.7.3.0 and the README make claims that do not match the code:
- an open ticket (BUG-0205) is listed as fixed;
- a behaviour change for Tribe and Civ guests is not mentioned;
- the notes say "the network protocol did not change", although 0.7.3.0 added a wire flag that 0.7.2.x ignores;
- the README's compatibility line still stops at 0.7.2;
- the list of fixes that switch themselves off on GOG 2016 misses two fixes of this release (BUG-0188 and BUG-0020).

The last one cannot be caught today: `tools/re_lint.py` only sees symbol names written literally inside `sym("...")`, so a name looked up through a table is invisible to the frozen-build report that BUG-0108 relies on. Severity: low (docs and tooling). This issue proposes a re_lint rule and a release-check step, so

## Root cause
Confirmed: CHANGELOG 0.7.3.0 listed open BUG-0205 as fixed, omitted the Tribe/Civ away behaviour, called BUG-0149 untested, and the GOG 2016 list missed BUG-0188 and BUG-0020; re_lint only saw `sym("literal")`, so the six CivAI names (a table in civ.cpp) and four SporeKit hooks (`try_hook`) were never linted.

## Fix
CHANGELOG [0.7.3.0]: dated corrections in place (BUG-0205 still open, BUG-0149 has a game test, the BUG-0172 guard, GOG 2016 list, the Tribe/Civ behaviour change); README footnote 2. re_lint scans .cpp and .h of mods and sporekit and counts a string literal that is exactly a qualified .sym name (or any literal on a `// re_lint: sym` line); the 4 SporeKit hooks got checked overrides entries (ret N from the GOG 2024 disassembly, seen running in every log). data/symbols/frozen_off_922af7c7.txt: the one list of what is off on GOG 2016, re_lint fails both ways. `tickets.py notes-check X.Y.Z notes.md` (called by release_check.sh): an id under Fixed must be a fixed ticket unless its item says "still open" / "not reproduced" / "not a fix". The protocol sentence of the notes and the README "play together" line are left to BUG-0219 (fix/w5-net).

## Verification
FAIL: SELFTEST_RELINT=<b423f56 re_lint.py>: the table name and the stale frozen_off line pass unnoticed; notes-check on b423f56's [0.7.3.0] section fails on BUG-0205. PASS: tools_selftest relint and release all ok; notes-check on the corrected section 0 problems; re_lint 0 problems with 310 used names (252 before).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (7cbae8e). Left: the GitHub release text of v0.7.3.0 (owner, by hand).
