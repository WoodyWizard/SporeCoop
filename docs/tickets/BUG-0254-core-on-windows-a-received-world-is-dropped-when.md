---
id: BUG-0254
title: Core: on Windows a received world is dropped when another program holds a file in its old co-op slot, and failed writes leave full world copies in Games-sporecoop-tmp
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #188 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #188: https://github.com/WoodyWizard/SporeCoop/issues/188 (the full analysis, links and the scenario are there).

**Summary**
Since 0.7.3.0 a guest writes a received world into a temporary folder and then swaps it in for the old co-op slot of the same host (BUG-0105 / BUG-0087, commit df2f0a9). On Windows that swap is a folder rename, and it fails while any program has a file in the old slot open: an antivirus scan, the search indexer, an Explorer preview. The guest then gets "could not save the world", no world for that phase, and the host does not send again. Folders left after a crash during the swap are never cleaned up, and one kind of them is the only copy of the old world. This is not a regression: before the fix the same lock produced a half-written slot that hung LoadGame, and the new failure is safer. Severity: wrong behaviour, low, Windows only (Wine renames folders with open files, so the 

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
