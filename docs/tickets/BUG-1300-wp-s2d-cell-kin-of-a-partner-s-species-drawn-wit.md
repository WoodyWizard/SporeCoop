---
id: BUG-1300
title: WP-S2d: Cell kin of a partner's species drawn without the model preload
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-11-19
source: plan
role: 
versions: 
fixed_in:
branch: feat/w56-editors
commits:
tests: cell_kin_species (its key frames)
signature:
docs: docs/re/cell.md#cell-kin-preload
related: BUG-1257
---

## Report
Lineages comparison (item 7): the fork preloads every player model it puts on a native cell (PreloadModel +
cCellGFX::PreloadCreature 0xE64980), "not proven necessary"; our partner kin (BUG-1257) do not.

## Root cause
✔ (code) not a bug in our design: a kin is given only a key the game already draws, the partner's puppet's key
(species::key(partner, Cell)), and only for a partner whose puppet exists on that game (the host's view centres come
with the partner's state, a guest's solo pool lists its puppets), and only when key_known; the model data comes from
GetCellModelData's map built on first use, as for the puppet; puppets were never preloaded. Evidence:
docs/re/cell.md#cell-kin-preload.

## Fix
None.

## Verification
no-fail-run: not reproducible by our code path (the kin's model is always one its game already draws for the
partner's puppet); the key frames of cell_kin_species (a partner's kin and its puppet look the same) confirm it when
the night lab runs it.

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-editors: not a bug in our code (evidence above), closed as not-reproduced.
