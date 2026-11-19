---
id: BUG-1308
title: The guest's own Tribal Outfitter for its own creation: Done does nothing (the part-list error of the creation's Creature Creator parts is waived only in the own Creature editor)
status: fixed
kind: bug
stage: Tribe, Civ
reported: 2026-11-19
source: lab
role: guest
versions: rel/0.7.16
fixed_in: unreleased
branch: fix/w57-own-editors
commits: 800a17e6
tests: tribe_own_outfitter, own_editor_drop
signature:
docs: docs/re/creations.md#creations-own-outfit-parts
related: BUG-1272, BUG-1275, BUG-1222, BUG-1298
switch: editors.own_outfit_parts
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
FAIL: rel/0.7.16 out/lab/20261119-095414 tribe_own_outfitter and 20261119-104136 own_editor_drop ("the guest is back
in Tribe (0x00DBDBA1)"); twin on the merged tree fix/w57-own-editors worktree out/lab/20261119-130049 tribe_own_outfitter_parts_off FAIL the same.
PASS of that step: fix/w57-own-editors worktree out/lab/20261119-111652, -113002, -122505, -130049 tribe_own_outfitter, own_editor_drop and
own_editor_key ("the guest is back in Tribe (0x01654C02)", log "parts outside its part list: allowed (BUG-1308)");
own_editor_key PASS whole in 20261119-130049. (tribe_own_outfitter and own_editor_drop then fail only the host save's
check: BUG-1326.)

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w57-own-editors, lab runs above.
