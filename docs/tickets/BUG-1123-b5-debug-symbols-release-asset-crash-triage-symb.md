---
id: BUG-1123
title: B5: debug-symbols release asset; crash_triage symbolises coop.dll frames with the report's version
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-build
commits: acf0ef0e, 8cb05198
tests: tools/tests/triage_symbols_check.py (make test part unit-symbols), tools/tests/tools_selftest.py triage
signature:
docs: docs/06-testing.md#testing-crash-symbols, docs/10-dev-pipeline.md#pipeline-release-symbols
related: BUG-0158, BUG-0688, BUG-0784, BUG-1120
---

## Report
Plan 0.7.15 B5 (audit KIT-3, owner decision 6: symbols public): player crash frames in coop.dll could not be
symbolised reliably: the release DLLs are stripped, no debug copy was kept per release, crash_triage.py only
printed an llvm-symbolizer call on the current dev build.

## Root cause
No per-release symbols; the triage tool had no way to get the build of the report's version.

## Fix
tools/symbols_pack.sh (called by package.sh) writes SporeCoop-<ver>-debug-symbols.tar.xz (unstripped coop.dll,
sporekit.dll, steam_api.sporekit.dll, SYMBOLS.txt with commit and hashes; 14 MB); the release job requires it,
lists it in SHA256SUMS.txt, publishes and attests it; the player files stay stripped. crash_triage.py prints
`symbol     coop.dll+0x...  function+0xN  file:line` for the fault and the chain (coop, sporekit, loader) with
the symbols of the report's coop version: --symbols <build dir|tar.xz> (the DLL's version must match:
VERSIONINFO, or the COOP_VERSION string for builds before 0.7.12), the cache (~/.cache/sporecoop/symbols/<ver>),
or `gh release download v<ver>` checked against SHA256SUMS.txt. Versions before 0.7.15: the tool says how to
build the tag. --no-symbols turns it off. No coop.fix switch (tooling).

## Verification
- PASS: triage_symbols_check.py 12 checks (tarball, build folder, other version refused, download with a fake
  gh: sha256 checked and cached, cache reuse, wrong sha256 refused, gh failing, pre-0.7.15 never calls gh);
  under Python 3.10 too.
- FAIL as wanted: the same checks against the old crash_triage.py (SELFTEST_TRIAGE): 12 failures.
- Real frame: BUG-0688's coop.dll+0x4158E (0.7.9) with a build of tag v0.7.9 from git archive names
  civ::weapon_of (inlined into civ::tick), the ticket's root cause.
- no-fail-run: tooling; the FAIL side is the old tool above.

## History
- 2026-11-15: opened.
- 2026-11-18: released in 0.7.15.
