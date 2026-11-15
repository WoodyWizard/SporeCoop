---
id: BUG-0703
title: Creature: the host's last species table was written into the guest's own world every 0.5 s while the guest played another world
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
K2a audit of the class BUG-0420 (item 4): the host's species relationship table was enforced on a guest every 0.5 s also after the guest went to play another world of the stage: the guest's own relations (allies, hostiles) in its own world were reset to the host's.

## Root cause
Observed (lab 20261107-223012, `SPORE_FIX_OFF=social.world_gate`): a relation the guest set in its own world (2) went back to the host's (6) within 6 s. social.cpp tick ran `enforce_table` / `herds_follow_table` for any connected guest; only new MSG_SPECIES were gated, the last table stayed.

## Fix
`social.world_gate`: while the guest is elsewhere its tick forgets the host's table (`table_forgot=` in coop.status) and writes nothing; the host's next table comes with the host's world.

## Verification
coop_world_gate_vote.py step 3:
* FAIL with `SPORE_FIX_OFF=social.world_gate`: out/lab/20261107-223012 (relation seen [2, 6]).
* PASS: out/lab/20261107-222542, out/lab/20261107-223602.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): fixed, social.world_gate.
- 2026-11-08: released in 0.7.11.
