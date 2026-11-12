---
id: BUG-0805
title: Creature: proxy pointers used by message handlers without a liveness check (MSG_DAMAGE attacker, social prompt / result)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-freed
commits: 570ee491
tests: npc_handler_live
signature:
docs: docs/06-testing.md#testing-freed-block-reuse
related: BUG-0426 BUG-0453
---

## Report
No player report. In the frame after the guest's game removed a proxy by itself, a host hit or a social message for it can hand a destroyed creature to the game.

## Root cause
Observed at runtime (fix/w48-freed, 2026-11-12): with the guest's proxy destroyed by its own game in the same frame (`coop.npc handlerkill <id> damage`: DestroyInstance, then the host's MSG_DAMAGE naming that NPC as the attacker), the guest gave the destroyed creature to TakeDamage as the attacker: 5-10 stimuli of the guest's NPCs named it afterwards (`coop.staleref`, AI blocks, the attacker +0x10 / sender +0x2C of a stimulus slot; lab 20261112-010212, 20261112-005401) - the BUG-0926 class: the herd-help node calls that creature after it is freed (R6025). The same raw pointer went to the social prompt / result (npc::proxy), the NPC animation relay, the fight effects (npc_mirror) and a body's meat; on the host a guest's puppet went to TakeDamage as the attacker unchecked.

## Fix
mods/coop/npc.cpp: `handler_c(px)` gives a handler the proxy's creature only while it is the live object the proxy made (game::alive: list + serial); used by MSG_DAMAGE, npc::proxy (social), the anim relay, npc_mirror and the meat message; on the host the attacker puppet of MSG_DAMAGE_REQ and of a friendly-fire hit is `puppet_live` (a live Creature puppet). Switch: `coop.fix npc.handler_live off`.

## Fix idea
npc::proxy() and the MSG_DAMAGE lookup return null when !object_alive(c) || is_destroyed(c).

## How to test
Guest: remove a proxy with the game's own destroy (destroy_untracked, a proxy variant of `coop.puppet gamekill`) and inject a MSG_DAMAGE / social prompt for it in the same frame; assert no TakeDamage with that attacker (dmglog) and no fault. Main: the call goes through.

## Verification
Test `npc_handler_live` (tools/tests/coop_npc_handler_live.py).
* FAIL (SPORE_FIX_OFF=npc.handler_live): out/lab/20261112-010212 (10 stimuli named the removed creature).
* PASS: out/lab/20261112-010824-2, out/lab/20261112-011341, out/lab/20261112-013003 (0 stimuli, no faults).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12: fixed on fix/w48-freed (npc.handler_live).
