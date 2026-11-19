---
id: BUG-1305
title: WP-K: fork BUG-9175 check: a guest's own mating at the nest never finishes (its mate is a host-run copy)
status: open
kind: bug
stage: Creature
reported: 2026-11-19
source: plan
role: guest
versions: 0.7.15.2 (to check)
fixed_in:
branch: feat/w56-cocoon
commits:
tests: guest_mating
signature:
docs: docs/re/creature.md#creature-lineages-cocoon-mate
related: BUG-0177, BUG-1222, BUG-1246, BUG-1304
---

## Report
The Lineages fork (PR #196, its BUG-9175): a guest's own mating at the nest never finished: the guest's mate is a copy
of the host's family member, run by the host, whose own AI must answer the pairing (0xD6D360), so the guest's creature
stood in its mating action (node 02D852F0) for ever. Our tests never mated on a guest the players' way: editor
tests ask for the guest's editor with `coop.vote evolve` (the request the mating builds), creature_mating mates on the
host; BUG-1246's MSG_CELL_MATE is the Cell stage's.

## Root cause
To check at runtime: `coop_guest_mating.py` (the guest's game names a mate, Backspace, a real click on it, then the
pairing, the egg scene and the guest's own editor with no key pressed).

## Fix
Pending the lab result.

## Verification
Lab not run yet (the orchestrator's "lab ok" is pending).

## History
- 2026-11-19: opened; test written.
