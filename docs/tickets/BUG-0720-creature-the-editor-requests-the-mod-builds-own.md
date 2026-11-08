---
id: BUG-0720
title: Creature: the editor requests the mod builds (own editor, coop.vote editor) lack fields of the game's own request
status: fixed
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0431 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-a
commits: e81a56d9
tests: editor_request
signature:
docs: docs/re/creations.md#creations-editor-request-builder
related: BUG-0431, BUG-0368, BUG-0466, BUG-0457, BUG-0721
---

## Report
From the BUG-0431 audit (K2b, 2026-11-08): open_own_editor (a guest's own editor of an evolution vote) and the test command `coop.vote editor` built their EditorRequest with +0xC, the species and +0x94 only, while the creature stage's own request (0xD42E50) also sets flags +0x34..+0x3D, the +0x70 vector of cPlayer +0x10FC..+0x1108, +0x90, posts 0x5D02A72 and sets 0x1686AF0; `coop.vote evolve` had its own copy of the game's fields.

## Root cause
Observed (runtime, lane 6, lab 20261108-035759 with the fix off: the game's own builder run on each game and compared byte by byte): the own request differed in **+0x39 (1, game 0), +0x3B (1, game 0), +0x3C (0, game 1)** and the vector (empty, the game's 4 consequence traits). Static (Ghidra): the constructor 0x5A9140 sets +0x39 / +0x3B = 1 and +0x3C = 0, the game's builder overrides them (docs/re/creations.md#creations-editor-request-builder). The bytes +0x21..+0x23, +0x3E / +0x3F and +0x67 are never written by the constructor (allocator leftovers on both sides). ? What the editor does with the three flags and the traits is not seen in play (the own editor opened, priced and locked parts before too: editor_sync_guest, guest_editor_locks).

## Fix
One builder, `build_editor_request(kind, editor, species)` (coop.cpp), used by open_own_editor, `coop.vote editor` and `coop.vote evolve`: the game's fields (+0x94 in Creature, the flags, the traits), +0x90 only for the evolution kind. Allow-listed (documented): +0x90 for the own editor (BUG-0466 charges its price; the tag makes the creature game apply an evolution), the message 0x5D02A72 and the byte 0x1686AF0 (the evolution flow's; the mode switch pending covers 0x1686AF0's reader 0xB5EB94). `coop.editreq diff` (tests) runs the game's builder (its handler with message 0x4471C60, Submit captured, nothing opened) and diffs. `coop.fix creations.editor_request off`: the old requests.

## Verification
Test `editor_request` (tools/tests/coop_editor_request.py: the game's request against the mod's own and evolution requests, on the host and the guest).
* FAIL with `SPORE_FIX_OFF=creations.editor_request`: out/lab/20261108-035759 (own request: +0x39, +0x3B, +0x3C and the vector differ on both games)
* PASS: out/lab/20261108-035848, out/lab/20261108-052538 (no difference but the allowed +0x90, both games)
* Related PASS: editor_sync_guest out/lab/20261108-045207, guest_editor_locks out/lab/20261108-045521, party_creature4 out/lab/20261108-050537

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: the shared builder and the byte diff test (editor_request).
