---
id: BUG-1174
title: C2 (decision 2) for NPC: the guests' NPC proxies are destroyed and remade around the host's editor visit instead of held
status: fixed
kind: desync
stage: Creature
reported: 2026-11-15
source: plan (orchestrator, after feat/w51-core2)
role: 
versions: 0.7.14.1, rel/0.7.15 96e0fd4c
fixed_in: 0.7.15
branch: feat/w51-npc
commits: 76cc685f, 04a9d820
tests: npc_editor_hold, tests/npc_tests.cpp (test_proxy_rules)
signature:
docs: docs/01-architecture.md#arch-npc-proxy-rules
related: BUG-1141, BUG-1142, BUG-0205, BUG-0412, BUG-0611
---

## Report
Owner decision 2 of docs/plan-0.7.15.md (C2, BUG-1141): an editor visit of the host is the host away, not a world
change, the mirrors stay. feat/w51-core2 made it so for the world resets; npc.cpp's host-away boundary still ended the
guests' NPC copies when the host went into its editor (orchestrator, after merging feat/w51-core2).

## Root cause
Observed (npc_editor_hold with `SPORE_FIX_OFF=npc.editor_hold`, lab 20261115-194352): 4 s into the host's editor the
guest counts the host away (coop.cpp host_away_tick, 'e'), and npc.cpp's tick_frame handed the planet to the guest's
game: away_locals made its own NPCs from the snapshot and every proxy was destroyed (0 of 21 kept). After the editor the
host's host_tick (which does not run outside its stage) found every NPC's last update an editor's length old and retired
all of them (despawns, then spawns under new ids).

## Fix
Guest (npc.cpp tick_frame, npc_rules::editor_hold): while the host counts as away because its latest state is in an
editor (`Ctx::host_mode`, wired in coop.cpp), and until that away state ends, the proxies are held: no hand-over, the
update filter keeps removing the guest's own NPCs, no states come so they stand (the stale drop waits for states, the
thaw of BUG-0611 covers the first after it). A host that goes on to the main menu or a load is away for real. Host
(host_tick): a gap without a host pass over 1 s does not count as unseen (last_seen moved on). `coop.fix
npc.editor_hold off`: as 0.7.14. coop.npc status: `editor_holds= held= host_gaps=`.

## Verification
FAIL (switch off): out/lab/20261115-194352 (0 of 21 copies kept while the host edits, 0 after).
PASS: out/lab/20261115-194532 (2 of 2), out/lab/20261115-194052 #2 (21 of 21 kept in and after the editor, states fresh
again). Unit: test_proxy_rules (editor_hold). away_editor (BUG-0205) PASS with it: out/lab/20261115-194833.

## History
- 2026-11-15: opened (orchestrator's request after feat/w51-core2).
- 2026-11-15 (feat/w51-npc): fixed behind npc.editor_hold, test npc_editor_hold.
- 2026-11-18: released in 0.7.15.
