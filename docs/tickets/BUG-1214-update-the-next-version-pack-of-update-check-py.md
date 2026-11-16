---
id: BUG-1214
title: update: the next-version pack of update_check.py holds the unstripped coop.dll (102 MB) and the in-game updater refuses it (96 MB limit)
status: fixed
kind: test
stage: Tools
reported: 2026-11-16
source: fix/w51-t1 rerun of update
role: 
versions: 0.7.15 dev
fixed_in: unreleased
branch: fix/w51-t1
commits: f7f2132d
tests: update (tools/tests/update_check.py)
signature:
docs: docs/06-testing.md#testing-w51-t1-triage
related: BUG-1189, BUG-1123
---

## Report
After BUG-1189 (the next-version build compiles again) `update_check.py --lane 9` on fix/w51-t1
(out/manual/update_check-lane9-1.log, not a lab run: the lab gives this test only a free lane outside its run): the
game found 0.7.14.2, `coop.update apply` ended with `update: failed ... error=the download is too big`, so "FAIL the
update is downloaded, checked and installed", "FAIL the version file says 0.7.14.2", "FAIL after the restart coop
0.7.14.2 runs", "FAIL no further update offered".

## Root cause
Observed: update_check.py's `build_new` copies `build-v<NEW>/coop.dll` into its stage as built, with its debug
information (`-g`): 102 MB on rel/0.7.15 (78 MB on v0.7.14.1), so the pack was 103 MB. The in-game updater downloads at
most 96 MB (`winupdate.h` max_bytes `96u << 20`). A real release strips coop.dll in tools/package.sh (5.7 MB; the pack
7.3 MB); the test's fake release did not. Not a problem of the updater: 0.7.15's larger code pushed the test's
unstripped DLL over the limit (it was under it with 0.7.14.1's 78 MB).

## Fix
`build_new` strips the copied coop.dll with the toolchain's llvm-strip, as package.sh does for a release. The checks
are unchanged.

## Verification
* FAIL: `update_check.py --lane 9` by hand with BUG-1189 fixed (out/manual/update_check-lane9-1.log of fix/w51-t1):
  "error=the download is too big", the pack 103 MB.
* PASS: the same by hand twice (out/manual/update_check-lane9-2.log, -3.log): RESULT: OK.
* no-fail-run: no lab run id: the lab runs `update` only on a free lane outside its run, and this task has lane 9 only.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t1.
