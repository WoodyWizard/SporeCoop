---
id: BUG-1308
title: The guest's own Tribal Outfitter for its own creation: Done does nothing (the part-list error of the creation's Creature Creator parts is waived only in the own Creature editor)
status: open
kind: bug
stage: Tribe, Civ
reported: 2026-11-19
source: lab
role: guest
versions: rel/0.7.16
fixed_in:
branch:
commits:
tests: tribe_own_outfitter, own_editor_drop
signature:
docs:
related: BUG-1272, BUG-1275, BUG-1222, BUG-1298
---

## Report
First 0.7.16 lab run: tribe_own_outfitter (20261119-095414) "the guest is back in Tribe (0x00DBDBA1)" and
own_editor_drop (20261119-104136) the same after the session's end: the guest's own Tribal Outfitter opened (for its
own creature 0x290225CD, the Creature Creator creation 'Kirgle'), a piece was dragged on, Done was clicked, and the
guest's game stayed in the editor for the rest of the test (90 s and more, no log line).

## Root cause
✔ (runtime, lab 20261119-111652) The guest's own outfitter edits its own creation, whose Creature Creator parts are not
in the editor's part list: `cEditor::GetBlockingErrors` 0x57AC00 sets bit 5 (docs/re/creations.md section 7), so
"done" only posts the error and stays (the case of BUG-1222 in the Creature editor). `h_BlockingErrors` (vote.cpp)
waived bit 5 only in the own Creature editor (`g_own_editor`), not in the own outfitters of BUG-1272 / BUG-1275 that
0.7.16 added. With the waiver the log shows "editors: our own outfitter's creation has parts outside its part list:
allowed (BUG-1308)" at the editor's load and Done brings the guest back to Tribe.

## Fix
`editors.own_outfit_parts` (vote.cpp): bit 5 is also dropped while our own Tribal Outfitter (designs::own_outfitter_open,
the request taken by own_outfitter until its return) or our own Civ outfitter (civedit::own_outfit_open: its request
of tag 0x66787BB started from our creature) is on its way or open. Other blocking errors still stop Done. `coop.vote`
counts them ("part list waived N (own outfitters M, BUG-1308)"). Off: only the own Creature editor (0.7.15).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
