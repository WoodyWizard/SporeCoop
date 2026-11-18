---
id: BUG-9028
title: Windows staged save-folder moves fail on brief file locks
status: fixed
kind: bug
stage: All
reported: 2026-11-04
source: m2-conquest-win-tests.log
role: Windows regression
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: build/win_tests.exe --staged-move-only
signature: test_slot_busy second write fails and Terra.spo is missing
docs: docs/known-issues.md
related: BUG-0035
---

The Windows regression suite reports two assertions in test_slot_busy: its
second world write fails and Terra.spo is absent. Reproduced outside the
sandbox, while local HTTP fixture tests pass. Existing error output omitted
the reason; add it and a bounded runner before diagnosing. No production
root cause or fix claimed yet.

Focused rerun m2-slot-busy.log passes. The original failure is intermittent;
retain the diagnostic and check the full suite without live games before
attributing it to production save handling.

Full rerun with both games closed still fails the second slot move with
Win32 ERROR_ACCESS_DENIED (5), m2-conquest-win-tests-isolated.log. The destination
is a new Game1 folder, so investigation should include transient source-folder
locks. The same run also failed the installer's close-during-job assertion;
that is separate from the successful Tribal gameplay acceptance.

Further qualification reproduced ERROR_ACCESS_DENIED in several different
slot writes (m2-slot-series-4/5/7/9.log), including a first write. The identity
of the interfering handle is not established; do not attribute it to a specific
scanner. A real Win32 handle opened without FILE_SHARE_DELETE reproduces the
same directory rename failure. Microsoft documents that this sharing mode
controls rename/delete access:
https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew

Fix: only the final staged-folder commit retries access/sharing failures for
up to 500 ms, with 25 ms intervals. It does not copy into, overwrite or delete
the destination. Other errors return immediately. Persistent locks still
report failure and use the existing rollback path. Switch: world.staged_move.

FAIL m2-staged-move-control.log (--old-staged-move): a real source-file lock
released after 150 ms aborts the move. PASS m2-staged-move-fixed.log: it succeeds
after release, while permanent locks and occupied destinations preserve data.
PASS m2-slots-fixed.log: the complete focused save-slot suite.
PASS m2-win-staged-move-fixed.log: full Windows suite, ALL OK.
