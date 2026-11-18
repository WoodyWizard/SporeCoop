---
id: BUG-9123
title: Cell: a guest's DNA drops when the host's editor spends it, with no word why
status: fixed
kind: bug
stage: Cell
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev ("the connected player saw 21 DNA, the host spent 21, it reduced theirs")
role: guest
versions: all
fixed_in: 0.7.8-lineages50-dev
branch: feat/lineages-47
commits:
tests: QA Cellfive.spo host 9 + guest 8, `coop.pool dna spend 5` on the host, guest log / feed
signature: -
docs: docs/re/cell.md#cell-editor-dna
related: BUG-0560
switch: cell.shared_dna_note
---

## Cause
By design the Cell DNA is the party's, as the Creature pack's: DNAForFood(shared food) - spent, and the host's
spent (S+0x2C) is synced to the guests (wsync.cell_spent, BUG-0560). The guest's number dropped with no message.

## Fix
When a guest's spent DNA goes up from a partner's editor (set_spent_dna), its feed says "A partner spent N of the
shared DNA in the editor (M left)." Test command: `coop.pool dna spend <n>` (host).

## Verification
Host spend 5: both games food=204 earned=16 spent=5 budget=11; guest log "A partner spent 5 of the shared DNA in
the editor (11 left)."
