---
id: BUG-1428
title: CR-2: Creature: a guest's hit or resync on an NPC the host's game removed reaches the freed creature (rare host crash)
status: open
kind: crash
stage: Creature
reported: 2026-11-22
source: audit
role: host
versions: 0.7.16.1
fixed_in:
branch: fix/w63-life
commits: 31ec75ce, 39f2f060
tests: creature_hit_gone (coop_creature_hit_gone.py); twin creature_hit_gone_off
signature:
docs: docs/06-testing.md#testing-w63-life-knobs
related: BUG-0453, BUG-0487
---

## Report
External static audit of 0.7.15.5, item CR-2 (out/research/ext-audit-verify-cr-tr.md): a guest's hit (MSG_DAMAGE_REQ) or resync request (MSG_NPC_RESYNC) on an NPC the host's game removed since its last NPC pass reaches the freed creature: a rare host crash or a hit on whatever reused the memory. Found in code, not seen by a player.

## Root cause
Inferred from code (rel/0.7.16 npc.cpp): both handlers asked `hn_obj(id)` (no liveness check) and `g_seen_now` (the previous host pass's list: `frame_receive` runs before `frame_world_ticks`, and no pass runs at all while the host has no avatar or is in its editor). A creature the game removed in between (a decayed or eaten body, a far animal it culls) still passed, then `game::apply_damage` (TakeDamage) ran on it, or `send_spawn` read it. `host_npc` (listed with its serial, exact through the destroy hook) is what the social handlers already used. `npc_each` (the fields tick) had the same window and read the destroyed byte of the freed creature.

## Fix
Both handlers also ask `host_npc(id)` and drop the message when the NPC is not alive now (counted `msg_gone_refused` in `coop.npc`); `npc_each` asks `ref_alive` of the entry. Switch `coop.fix npc.msg_live off`: as 0.7.16. Test fixture: `coop.npc gone <id> [secs]` (host): the game removes NPC #id with its own DestroyInstance (`game::destroy_untracked`) and the host pass holds secs, as in the host's editor. No message changed (no wire bump).

## Verification
Open, waiting for the lab run (no lab runs during the 0.7.16 final check). PASS expected: creature_hit_gone (hits_applied unchanged, msg_gone_refused + 1, no crash). FAIL expected: creature_hit_gone_off (SPORE_FIX_OFF=npc.msg_live: the hit is applied to the freed creature, or the host crashes). Own DNA / shared DNA: the handlers do not depend on all.shared_dna (the kill credit path after the check is unchanged).

## History
- 2026-11-22: opened (audit item); fixed on fix/w63-life with its test and twin, waiting for the lab run.

- 2026-11-22: renumbered from BUG-9313 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9313.
