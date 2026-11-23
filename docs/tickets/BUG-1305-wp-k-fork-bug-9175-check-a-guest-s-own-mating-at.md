---
id: BUG-1305
title: WP-K: fork BUG-9175 check: a guest's own mating at the nest never finishes (its mate is a host-run copy)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: plan
role: guest
versions: 0.7.15.2 and older (observed on the 0.7.16 tree before the fix)
fixed_in: 0.7.16
branch: feat/w56-cocoon
commits: cc4dc305 b67a4a56
tests: guest_mating, guest_mating_off
signature:
docs: docs/re/creature.md#creature-guest-mating
related: BUG-0177, BUG-1222, BUG-1246, BUG-1304
---

## Report
The Lineages fork (PR #196, its BUG-9175): a guest's own mating at the nest never finished: the guest's mate is a copy
of the host's family member, run by the host, whose own AI must answer the pairing (0xD6D360), so the guest's creature
stood in its mating action (node 02D852F0) for ever. Our tests never mated on a guest the players' way: editor
tests ask for the guest's editor with `coop.vote evolve` (the request the mating builds), creature_mating mates on the
host; BUG-1246's MSG_CELL_MATE is the Cell stage's.

## Root cause
Observed (lab lane 6, 20261119-111735 guest_mating, before the fix): the guest's game picks a mate that is one of the
host's copies (a proxy listed by `coop.npc`); the mating call and a real click put the guest's creature into its
mating action 02D852F0, the mate's node stays FFFFFFFF (its AI does not run on the guest) and the creature waited 90 s:
no egg scene, no editor. As the fork found.

## Fix
`mods/coop/guest_mating.cpp`: on a guest, a creature in its mating action whose mate is a host copy, once within 4 m
of it for 1 s (or 12 s after the action began), gets the creature game's own "mating done" message 0x4D9686F (outside
the frame); the game then builds the mating editor's own request (0xD3C650, tag 0x12191CA), which the Submit hook
takes as the guest's own evolution. Once more after 8 s if no editor came. Switch `coop.fix creature.guest_mating off`:
the creature waits (0.7.15). docs/re/creature.md#creature-guest-mating.

## Verification
* PASS: guest_mating 20261119-112614 (lane 6): "the pairing ends here after 1.0 s, 2.8 m from it", the message, the
  request "editor 312E9D6A tag 012191CA", the guest's editor open, done, back in the Creature stage.
* FAIL twin: guest_mating_off 20261119-112934 (no editor within 60 s).

## History
- 2026-11-19: opened; test written.
- 2026-11-19: reproduced on lane 6 and fixed (creature.guest_mating).
- 2026-11-23: released in 0.7.16.
