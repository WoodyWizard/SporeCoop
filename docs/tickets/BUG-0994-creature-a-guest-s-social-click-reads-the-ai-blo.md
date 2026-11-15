---
id: BUG-0994
title: Creature: a guest's social click reads the AI block of every proxy creature without a liveness check (proxy_id_of_ai)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-1
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-live
commits: ff031065
tests: npc_ai_lookup, npc_handler_live, proxy_identity
signature:
docs: docs/re/creature.md#creature-live-lookups, docs/06-testing.md#testing-same-pump-orders
related: BUG-0805, BUG-0842, BUG-0919
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-1. Not seen at runtime, found by code reading. A guest in Creature that clicks
an NPC in the social stance makes the game post the social stimulus (BT_PostStimulus 0xBC9940, hooked as social.cpp
h_Post, inside the simulation step). The hook asks npc::proxy_id_of_ai which proxy owns that AI block; that walks every
key of `g_proxy_of` and reads the creature's +0xB4C without asking whether the creature is still a live object. A
creature the game removed and freed while it was still in `g_proxy_of` is read (a caught fault, or a reused block that
matches by chance and sends the wrong K_ENGAGE id to the host: the host's social game starts with another animal).

## Root cause
Observed in code (main efd2e0ba):
* mods/coop/social.cpp:272-293 h_Post: `npc::proxy_id_of_ai(ai)` for every social stimulus the avatar posts while a guest.
* mods/coop/npc.cpp:3352-3358 proxy_id_of_ai: `for (auto& [c, id] : g_proxy_of) if (c && *(void**)((uint8_t*)c + 0xB4C) == ai)`:
  a raw read of every key, no `game::listed` / `px_alive`.
* `g_proxy_of` loses a creature in check_identity (npc.cpp:1915-1935, BUG-0842, Creature and Tribe) and check_listed
  (npc.cpp:1860, BUG-0931, Creature), both in guest_tick, which skips them in the first `g_settle_s` (1 s) after an
  avatar or world-epoch change (npc.cpp:1985-1987) and while the host is away; on_world_change clears it (npc.cpp:2567).
Inferred (medium confidence): the window is the settle second after an avatar change (editor exit, a species change)
and frames in which the game destroyed and freed a proxy before the next guest_tick; a freed creature is given to the
next creature of the species within 10-40 frames (BUG-0842), so a stale key can read another creature's AI block.
The audit's Tribe remark does not apply: h_Post acts only when the sender is the avatar, and Tribe has no avatar.
Severity: low (a read and a compare, the hook path runs only on a social click).

## Fix idea
Walk `g_proxies` instead of `g_proxy_of` and use only proxies with `px_alive(px)` (game::listed + serial), inside a
`game::LiveScope` (one list walk); or check `game::listed(c, 0x18EB45E)` per key. Switch: under `npc.handler_live`
(BUG-0805, same class) or a new `npc.ai_lookup_live`.

## How to test
Extend tools/tests/coop_npc_handler_live.py (Creature fixture): with `coop.npc settle 3` (debug_settle, the settle window
held) the guest's game destroys a proxy natively, then the guest's avatar posts a social stimulus to an NPC whose AI
block took the freed address (or a test command that calls proxy_id_of_ai with the freed creature's old +0xB4C).
Assert the guest sends no K_ENGAGE for the gone proxy (host's social target unchanged, the game's own field) and no
caught fault in the guest's log. FAIL on main (the stale id or a caught fault), PASS with the fix.

## Fix
npc::proxy_id_of_ai (`coop.fix npc.ai_lookup_live`) walks g_proxies and asks only proxies whose creature is the
object they made (px_alive: game::listed + serial, one walk in a local game::LiveScope; h_Post runs inside the
game's simulation and no game code runs in the loop); a gone one is counted (npc::ai_lookup_gone) and skipped. Test
command `coop.social post <creature>`: the avatar's social stimulus through the game's (hooked) BT_PostStimulus.

## Verification
Test npc_ai_lookup (tools/tests/coop_npc_ai_lookup.py, creature fixture): `coop.npc settle 6`, `coop.npc reusehunt
p<id> 150 x` until a new creature takes the freed proxy block, then `coop.social post` on it: the guest must engage
no gone proxy (K_ENGAGE for it).
FAIL (SPORE_FIX_OFF=npc.ai_lookup_live): out/lab/20261113-022821 (`engaged -1 -> 2, engages +1` for the new creature at
proxy #2's freed block; the host's game started no social game there: its NPC's social mode was 0).
PASS: out/lab/20261113-022929, out/lab/20261113-023224-2 (`engaged -1 -> -1, lookups skipped as gone +1`).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-live (package L2): proxy_id_of_ai over live proxies only (npc.ai_lookup_live); FAIL/PASS twin npc_ai_lookup.
- 2026-11-14: released in 0.7.14.
