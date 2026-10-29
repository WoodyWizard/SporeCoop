---
id: BUG-0243
title: Updater: an in-game update is not protected against a second start of the mod (hot reload, second instance) or against the game exiting between two file moves
status: open
kind: bug
stage: Installer
reported: 2026-10-28
source: GitHub #177 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #177: https://github.com/WoodyWizard/SporeCoop/issues/177 (the full analysis, links and the scenario are there).

**Summary**
Two low-probability ways for an in-game update to leave the game folder damaged. (a) While the update thread swaps files, a new start of coop.dll on the same folder (a hot reload, which BUG-0060 now lets the old thread survive, or a second game instance) runs `cleanup_old` and deletes the `.sporecoop-old` backups the running update needs for its rollback. (b) The game can exit between the two moves of one file, leaving `steam_api.dll` or `sporekit.dll` missing or a mix of old and new files; BUG-0139 closed this window for the setup window but not for the in-game updater. Severity: low (both PLAUSIBLE, tiny windows). (a) is a conflict between BUG-0060 (commit 2bb6835) and BUG-0114 (commit 6e8e765); (b) is left incomplete by BUG-0139 (commits eaf0a7c, 52be656), release 0.7.3.0.



## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
