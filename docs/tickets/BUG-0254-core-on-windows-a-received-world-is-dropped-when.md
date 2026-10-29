---
id: BUG-0254
title: Core: on Windows a received world is dropped when another program holds a file in its old co-op slot, and failed writes leave full world copies in Games-sporecoop-tmp
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #188 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-world
commits: 8228f24 976af47
tests: win_unit (tests/win_tests.cpp test_slot_fallback, test_slot_leftovers)
signature:
docs: docs/06-testing.md#testing-w5-world
related:
switch: world.slot_fallback, world.tmp_cleanup
---

## Report
From GitHub issue #188: https://github.com/WoodyWizard/SporeCoop/issues/188 (the full analysis, links and the scenario are there).

**Summary**
Since 0.7.3.0 a guest writes a received world into a temporary folder and then swaps it in for the old co-op slot of the same host (BUG-0105 / BUG-0087, commit df2f0a9). On Windows that swap is a folder rename, and it fails while any program has a file in the old slot open: an antivirus scan, the search indexer, an Explorer preview. The guest then gets "could not save the world", no world for that phase, and the host does not send again. Folders left after a crash during the swap are never cleaned up, and one kind of them is the only copy of the old world. This is not a regression: before the fix the same lock produced a half-written slot that hung LoadGame, and the new failure is safer. Severity: wrong behaviour, low, Windows only (Wine renames folders with open files, so the 

## Root cause
By reading (Windows only; Wine renames folders with open files): `slotfs::write` failed when the old slot could not be moved aside and `finish()` gave up; folders of interrupted writes were never cleaned.

## Fix
* `slotfs::write` reports why it failed (`WriteFail`); `write_or_fallback` (used by world.cpp `finish`) writes once more into another slot (`pick` with the held slot avoided, no planet backup) when the replace failed, then `tidy` moves other copies of the same host world (not busy) to Games-sporecoop-backup. Switch `coop.fix world.slot_fallback`.
* Leftovers in Games-sporecoop-tmp: temp copies older than 10 minutes (creation time, UTC) deleted; aside copies moved back into a missing slot, else to the backup folder, never deleted; all of `write` under the mutex `Local\SporeCoopSlotWrite` (10 s, else `Busy`). Switch `coop.fix world.tmp_cleanup`.
* Under Wine the held slot is simulated by `slotfs::g_test_held`. Not done: the manual check on real Windows (the lab has no Windows lane).

## Verification
* FAIL: `SPORE_LANE=5 python3 tools/tests/win_unit.py --old` (world.slot_fallback and world.tmp_cleanup off with the other old switches, direct run on lane 5, 2026-10-28): test_slot_fallback and test_slot_leftovers fail (no fallback slot, the held copy overwritten in place, leftovers stay).
* PASS: lab runs 20261028-232442, 20261028-235458, 20261029-003648 (win_unit), lane 5.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-world (8228f24); win_unit: FAIL with --old (direct run), PASS 20261028-232442, 20261028-235458, 20261029-003648
