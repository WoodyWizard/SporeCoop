---
id: BUG-0188
title: Civilization: the NPC AI still spawns vehicles on guests
status: fixed
kind: desync
stage: Civ
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-civ
commits: 882e6e2
tests: tools/tests/coop_civ_ai_spawn.py (suite civ_ai_spawn)
signature:
docs: docs/re/civ.md#civ-ai-vehicle-spawns; data/symbols/overrides_dc04aee5.sym (SporeCoop::CivAI_*_SpawnVehicle_Ret)
related:
switch: civ.aispawn
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The guest's civ AI spawns vehicles for the other nations; the next snapshot kills them
  (`civ(guest)` `removed=` grows by a few per minute). `SpawnVehicle` callers 0xBE7643 / 0xBFBCF7
  use the result without a null check, so blocking needs a hook at the AI decision instead.

## Root cause
Observed (lane 7, GOG 2024, 2026-10-28): a pass-through hook on `cCity::SpawnVehicle` counted the guest's own calls by return address. In a 10 minute `play_civ.py --seed 9` session the guest's game spawned 11 vehicles by itself: 6 from the NPC city tick FUN_00BE5890 (return 0xBE5A70: a new city buys its first vehicle at once), 3 from civ AI FUN_00BF9960 (0xBF9F58), 2 from civ AI FUN_00BFA7A0 (0xBFB0B6). All of these run from the civilization AI update FUN_00BFBD30 (fastcall civ), which the guest runs for every nation like the host. The co-op code did not block them; the next full snapshot killed them (`removed=`), or the guest bound one as the mirror of the host's own new vehicle.
The old note "callers 0xBE7643 / 0xBFBCF7 use the result without a null check" is about two other callers (GOG 2016 addresses; GOG 2024 0xBE7663 in a city message handler, 0xBFBD17 in the Space-only FUN_00BFBA70), not about the AI call sites seen at runtime.

## Fix
Fixed 2026-10-28 (fix/w2-civ, mods/coop/civ.cpp `h_SpawnVehicle`): on a guest in Civ (not applying a host message), `SpawnVehicle` returns null when it is called from one of the six NPC AI call sites whose code was checked to throw the result away or test it for null (disassembly, docs/re/civ.md#civ-ai-vehicle-spawns): 0xBE5A6B (city tick), 0xBDEA1C (null check before SetOrder), 0xBF9F53, 0xBFA746, 0xBFB0B1, 0xBFB692 (civ AI: only budget arithmetic / SpendMoney after the call). Their return addresses are `.sym` entries (`SporeCoop::CivAI_*_SpawnVehicle_Ret`). Other callers still spawn. `coop.civ spawns` lists every guest spawn by caller; counter `ai_spawns_blocked`; switch `coop.civ fix aispawn 0`.

## Verification
Two-game test `tools/tests/coop_civ_ai_spawn.py` (civ_npc): the host founds two NPC cities; the guest's game must make no vehicle by itself and the snapshot must kill none.
- FAIL with `--nofix` (lane 7, out/lab/w2civ-manual/ai_nofix.log): `spawn from 0x00BE5A70 x2`, `removed 0 -> 1` (the second one was bound as a mirror).
- PASS with the fix (out/lab/w2civ-manual/ai_fix1.log: both refused, removed unchanged; lab run out/lab/20261027-233457).
Not checked: the guest's NPC budgets now differ from the host's (they always did: the host's money of the other nations is not synced, and the AI's purchases on the guest never counted).

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: the guest's own spawns traced to the NPC AI call sites at runtime; fixed on fix/w2-civ (null for those call sites on guests; coop_civ_ai_spawn FAIL without, PASS with).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: civ.aispawn (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
