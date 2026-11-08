---
id: BUG-0721
title: Cell, Tribe, Civ, Space: editor requests built by the mod
status: not-reproduced
kind: bug
stage: All
reported: 2026-11-08
source: BUG-0431 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in:
branch: fix/w46-k2b-content-a
commits:
tests: editor_request
signature:
docs: docs/re/creations.md#creations-editor-request-builder
related: BUG-0431, BUG-0720
---

## Report
From the BUG-0431 audit (K2b, 2026-11-08): the Cell, Tribe, Civ and Space editors were not checked against the game's requests.

## Root cause
Code (fix/w46-k2b-content-a): the mod builds an EditorRequest in three places only, all through build_editor_request (BUG-0720): open_own_editor, reached only from own_editor_tick, which opens it only in the Creature stage (`game::mode_id() != modes::kCreature` returns; the due editor is dropped when another stage is entered); `coop.vote editor` and `coop.vote evolve` (test commands; `editor` also in Tribe for a member's species, BUG-0217). In Cell, Tribe, Civ and Space every editor of a vote is the game's own request: h_Submit only holds it for the vote and submits it again unchanged, it builds none. So there is no mod-built request outside Creature to be incomplete.

## Fix
None needed.

## Verification
no-fail-run: nothing to reproduce (code inventory above); the one builder is checked by editor_request (out/lab/20261108-035848).

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: closed as not-reproduced: no mod-built editor request outside Creature.
