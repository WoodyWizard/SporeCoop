---
id: BUG-9341
title: Crash in Creature after another era's vehicle bake was asked (era visitors, the creature dead in its death cinematic)
status: fixed
kind: crash
stage: creature
reported: 2026-11-22
source: TITAN QA run 2026-11-22 (crash-20261122-194216-i8)
role: host
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 9c698468
tests: tools/qa_win (manual, 2026-11-22): TITAN, QA 8 hosting Abber (Creature) in its death caption, QA 9 entering Abber (Civ); coop.fix galaxy.editor_snapshot_guard off/on
signature: Editors::cEditor::HandleMessage+0x11E (data 0x00000000); Editors::EditorModel::GetCreationName+0xC from cEditor::HandleMessage+0x147; ~6 s after era visitors' bake ask
docs: docs/re/creations.md#creations-editor-snapshot-message
related: BUG-9296, BUG-9290, BUG-9291
---

## Report
TITAN QA run (feat/lineages-alpha, 2026-11-22): QA 8 hosted Abber (Creature), its creature dead (hp 0, the death
cinematic and input lock for minutes, nobody pressing anything). QA 9 entered Abber (Civ) as its host. 6 s after QA 8 got
QA 9's city vehicle design ("galaxy creations: ... (BUG-9290)", "era visitors: ... baked levels 0: asked the rest") it
crashed: 0xC0000005 at Editors::cEditor::HandleMessage+0x11E, a read of 0, the game's main thread (crash-20261122-194216-i8).
To reproduce: one game hosts a planet's Creature era (dead or not: not checked), another enters the same planet's Civ era.

## Root cause
Observed (Ghidra on the Steam dump, the code read live, three crashes): a bake's render (0x410DC0, its picture taken)
posts message 0x3FC3F13 to every listener. Every initialized cEditor listens to it (cEditor::Initialize 0x584300 adds
the listener, Dispose drops it), also outside the editor. Its handler (cEditor::HandleMessage 0x591FA0, the case at
+0x108) reads Resource::GetDatabaseByID([this + 0x29C]) and [this + 0x88] (the editor model) and uses both without a
null check. On a galaxy planet the database was missing (crash at +0x11E, twice in the death caption and once in plain
play), or the model (crash at +0x147 in GetCreationName, right after the first-creature editor). Inferred: the editor's
save area belongs to the world it was set up for, gone after the galaxy's world swap.
CORRECTION (2026-11-22): first taken for a busy-state problem (the asks came during the death caption): a gate that held
the asks until plain play still crashed after the respawn (crash-20261122-201313-i8), so it was dropped.
Earlier note, kept: The signature is BUG-9296's (the bake's end reporting to an editor), whose fix allows the bake in
the Creature stage only because "the Creature stage bakes the same models without trouble" (BUG-9291 runs): this run
contradicts that for at least a dead creature in its death cinematic. CORRECTION recorded in
docs/re/creations.md#creations-bake-in-tribe.

## Fix
mods/coop/era_visitors.cpp: a hook on cEditor::HandleMessage skips message 0x3FC3F13 for an editor without its save area
(GetDatabaseByID null) or model (it has nothing to save the picture to); every other message and editor as before.
Switch `coop.fix galaxy.editor_snapshot_guard off`: the game's handler as it is. `coop.visit status` counts the snapshots
seen and skipped. New symbol: Editors::cEditor::HandleMessage (overrides_dc04aee5.sym, verified).

## Verification
TITAN, a fresh copy of the test galaxy each run (worlds_9337: Abber's Creature world stored with its creature dead), QA 8
(DuoQAPanelHost) enters Abber (Creature) and stays in the death caption, QA 9 enters Abber (Civ):
- FAIL (`coop.fix galaxy.editor_snapshot_guard off`): the bake ask, 5.7 s later the crash at cEditor::HandleMessage+0x11E
  (crash-20261122-203011-i8). Also on the build before the fix: crash-20261122-194216-i8, -195903-i8 (death caption),
  -200836-i8 (+0x147, just out of the editor), -201313-i8 (plain play after the respawn, with the dropped busy gate).
- PASS (on): the bake ask, 5.7 s later "an editor without its save area skipped a bake's snapshot message"; QA 8 left
  the death caption, played on (images/bug9341_pass_play.png) and ran 10 minutes with 3 294 objects received, no crash.
- Seen, not followed: after the first snapshot the other keys stayed "asked, not done" (pending 93 checks over 7 minutes):
  whether other-era props bake and draw on a galaxy planet is BUG-9290's open question.
no-fail-run: Windows QA by hand; the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: root cause found (the editor's snapshot message), fixed on feat/lineages-alpha; a busy-state gate tried first and dropped.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
