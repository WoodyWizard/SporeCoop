---
id: BUG-0242
title: Updater: the "file listed twice" check misses names that Windows treats as the same file ("coop.dll" and "coop.dll.")
status: fixed
kind: desync
stage: Installer
reported: 2026-10-28
source: GitHub #176 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-net
commits: 2a6131b, f34f769
tests: tests/net_tests.cpp test_updater_paths (net_tests --off update_paths fails); tests/win_tests.cpp test_apply_rollback (win_unit --old-paths fails)
signature:
docs:
switch: update.paths
related:
---

## Report
From GitHub issue #176: https://github.com/WoodyWizard/SporeCoop/issues/176 (the full analysis, links and the scenario are there).

**Summary**
The BUG-0089 fix rejects an update pack that lists one file twice, but it compares the paths only after lowercasing ASCII letters. `allowed_path` accepts path components that end in a dot, and Windows strips a trailing dot, so `SporebinEP1/mods/coop.dll` and `SporebinEP1/mods/coop.dll.` (or `.../sporekit./symbols/x.sym`) are the same file but pass both checks. Such a pack reaches `apply_pack`, where the second entry can still lose the original file on a rollback, the outcome BUG-0089 was about. Severity: minor (needs a pack with such a path, for example from a packaging mistake). Left incomplete by BUG-0089 (commit 6e8e765) in release 0.7.3.0.

**Where**
- `allowed_path`: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/updater/pack.cpp#L10

## Root cause
Observed: allowed_path accepted components ending in a dot and duplicate_path compared case-folded text only, so "coop.dll" and "coop.dll." (one file on Windows) passed; under Wine the rollback of such a pack ended with the original coop.dll gone (win_tests --old-paths: coop.dll = "N1").

## Fix
`upd::canonical_path`: the old rules plus no trailing dot, no DOS device names (con, nul, aux, prn, com0-9, lpt0-9, conin$, conout$, also with an extension), no names of the updater's own files (.new, .sporecoop-old, .old, .sporecoop-tmp*, SporebinEP1/mods/sporecoop.pending); lowercased. allowed_path and duplicate_path both use it. Every path of the real release pack still passes (tested). `upd::g_path_fix` (net_tests --off update_paths, win_tests --old-paths). Test hook `upd::g_fail_swap_at`. Not registered in coop.fix (the registry and the UNSAFE mark are #148's, fix/w5-tools). Not done: the CI pack_check of the issue's test plan 3 (old updaters against the real pack).

## Verification
net_tests PASS; --off update_paths FAIL (the 4-entry pack parsed, 10 bad names allowed). win_unit lane 6 PASS out/lab/20261028-230252; `win_unit.py --old-paths` FAIL (coop.dll lost: "N1").

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-net (wave 5).
