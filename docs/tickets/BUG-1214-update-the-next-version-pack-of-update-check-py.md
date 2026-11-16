---
id: BUG-1214
title: update: the next-version pack of update_check.py holds the unstripped coop.dll (102 MB) and the in-game updater refuses it (96 MB limit)
status: open
kind: test
stage: Tools
reported: 2026-11-16
source: fix/w51-t1 rerun of update
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
