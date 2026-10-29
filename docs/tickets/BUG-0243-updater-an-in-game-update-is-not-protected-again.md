---
id: BUG-0243
title: Updater: an in-game update is not protected against a second start of the mod (hot reload, second instance) or against the game exiting between two file moves
status: fixed
kind: bug
stage: Installer
reported: 2026-10-28
source: GitHub #177 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-net
commits: f34f769
tests: tests/win_tests.cpp test_update_lock (win_unit --old-lock fails); part (b): written argument below
signature:
docs:
switch: update.lock, kit.exitwait
related:
---

## Report
From GitHub issue #177: https://github.com/WoodyWizard/SporeCoop/issues/177 (the full analysis, links and the scenario are there).

**Summary**
Two low-probability ways for an in-game update to leave the game folder damaged. (a) While the update thread swaps files, a new start of coop.dll on the same folder (a hot reload, which BUG-0060 now lets the old thread survive, or a second game instance) runs `cleanup_old` and deletes the `.sporecoop-old` backups the running update needs for its rollback. (b) The game can exit between the two moves of one file, leaving `steam_api.dll` or `sporekit.dll` missing or a mix of old and new files; BUG-0139 closed this window for the setup window but not for the in-game updater. Severity: low (both PLAUSIBLE, tiny windows). (a) is a conflict between BUG-0060 (commit 2bb6835) and BUG-0114 (commit 6e8e765); (b) is left incomplete by BUG-0139 (commits eaf0a7c, 52be656), release 0.7.3.0.



## Root cause
Observed in win_tests (part a): a cleanup_old while an apply_pack of the same folder was between swaps deleted the backup its rollback needed (sporecoop.version missing). Part (b) (exit between two moves) by reading only.

## Fix
`sc_lock_name` (updater/pending.h, shared by the loader, the updater, SporeKit, the setup): "Local\<prefix><FNV-1a of the lowercased final path>". apply_pack takes "SporeCoopUpdate-<folder>" (10 s, NULL DACL so an elevated setup and the game share it) around the swaps and the rollback; cleanup_old and the loader's / setup's pending swaps try it with 0 ms and skip while it is held. SporeKit's ExitProcess and self-TerminateProcess hooks wait for it (5 s at most) before the exit report (`kit.fix exitwait`). Switch `upd::g_lock_fix` (win_tests --old-lock). The Low integrity label of the issue is not set (Wine has no split; a NULL DACL). Hooks `upd::g_swap_delay_file`, `g_fail_swap_at`.

## Verification
win_unit lane 6 PASS out/lab/20261028-230252; `win_unit.py --old-lock` FAIL (sporecoop.version "<missing>"). Part (b) has no game test (the issue's coop_update_exit.py needs a setup install on the free lane): the exit hook only waits on a mutex that apply_pack holds exactly around the swaps, with no logging or calls inside; the update thread needs nothing the exiting thread holds, and the wait is capped at 5 s. Left: tools/tests/coop_update_exit.py.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-net (wave 5).
