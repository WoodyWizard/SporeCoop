---
id: BUG-0370
title: Creature: after an editor the host cannot answer a guest asking for the guest's own previous creature (not found here), the NPC is missing on the guest
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: player reports 2026-11-01 (host GOG 2024 + Steam guest, 0.7.3.3)
role: guest
versions: 0.7.3.3
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-own-old-species
related: BUG-0278
---

## Report
Player logs (Creature, right after the host's evolution while the guest had evolved its own creature):
host `creations: player 1 asks for species 7CA388E5: not found here`; guest `npc 142: proxy spawn
failed (unknown species)`, then `sync-check: MISMATCH ... n: 1 missing on the guest (n142)`; OK again
30 s later (after the world share).

## Root cause
Not known. Inferred: 7CA388E5 was the guest's previous creature as registered on the host; after the
guest's own editor (new creature 7CC73097) the host no longer finds the old key, while a leftover of
the old puppet is still in the world (the host removed "a creature of species 7CA388E5 ... from a
save made while a partner's puppet existed" at the same moment).

## Fix
None yet (not part of fix/w30-progress's task). docs/known-issues.md#issue-own-old-species.

## Verification
no-fail-run: not reproduced, no fix.

## History
- 2026-11-01: opened from the 2026-11-01 reports (found while checking the "cubes" mark).
