---
id: BUG-0702
title: Creature: the host's evolution vote opened its vote screen and then the evolution editor on a guest that played another world of the host's stage
status: fixed
kind: bug
stage: Creature
reported: 2026-11-07
source: K2a audit of BUG-0420 (0.7.11)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state
commits: 6b49ee3d
tests: world_gate_vote (coop_world_gate_vote.py)
signature:
docs: docs/07-modes.md#modes-message-classes
related: BUG-0420, BUG-0231
---

## Report
K2a audit of the class BUG-0420 (item 3): MSG_VOTE was not world-gated: a guest that played another world of the host's Creature stage got the host's vote screen, and at the vote's end the evolution editor of its own creature opened in its own world.

## Root cause
Observed (lab 20261107-223407, `SPORE_FIX_OFF=core.vote_world`): the guest elsewhere showed "vote active: Alice wants to evolve (Creature)" and its game went into the editor (game.mode 0x00DBDBA1 Editor) after `coop.vote go` on the host. coop.cpp's MSG_VOTE handler set `g_own_editor_due` for any Creature guest.

## Fix
`core.vote_world`: a vote that starts while the guest is elsewhere is not taken (no screen, no ready entry), and the end of one opens no own editor while elsewhere; a vote the guest saw start in the host's world still ends normally. Counter: "votes of the host not taken elsewhere" in coop.vote.

## Verification
coop_world_gate_vote.py (the guest loads a world of its own while connected, the host votes):
* FAIL with `SPORE_FIX_OFF=core.vote_world`: out/lab/20261107-223407.
* PASS: out/lab/20261107-222542, out/lab/20261107-223602.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): fixed, core.vote_world.
- 2026-11-08: released in 0.7.11.
