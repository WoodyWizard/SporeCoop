---
id: BUG-0825
title: Creature: a deferred destroy can destroy a real creature at a reused address (no serial check) (suspected)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-freed
commits: 1e58b7e0 f9a88f86
tests: defer_identity
signature:
docs: docs/re/creature.md#creature-freed-block-reuse
related: BUG-0018 BUG-0066 BUG-0426
---

## Report
No player report. Suspected: a creature of ours queued for destruction during a cinematic, freed by the game meanwhile and replaced by a new NPC at the same address, gets that NPC destroyed when the cinematic ends; until then the NPC counts as a puppet (no AI, stripped from saves).

## Root cause
Observed at runtime (fix/w48-freed, 2026-11-12): the host queues the old puppet's destroy while CRG_ClaimNest plays (the guest switches its creation); the host's game destroys the queued puppet itself and a new creature of that species gets the block (`coop.npc reusehunt <old> 150 x`). When the cinematic ended, destroy_deferred_tick destroyed that new creature (it was a listed creature at the queued address; lab 20261112-011014).

## Fix
mods/coop/game.cpp: the queue keeps the serial; in every frame (also during the cinematic) an entry whose object is not the one queued (game::alive) is dropped and unregistered as a puppet. Switch: `coop.fix npc.defer_identity off`.

## Fix idea
Record game::object_serial when queueing and compare it before the destroy (and in is_puppet for queued entries).

## How to test
Test: during a cinematic queue a puppet's destroy, destroy it with destroy_untracked, spawn a console NPC at the same address (repeat until reused); after the cinematic the NPC must live (fix) vs destroyed (main).

## Verification
Test `defer_identity` (tools/tests/coop_defer_identity.py).
* FAIL (SPORE_FIX_OFF=npc.defer_identity): out/lab/20261112-011014 (the creature at the freed block was destroyed at the cinematic's end).
* PASS: out/lab/20261112-010824-2, out/lab/20261112-014310 (it lives). (A run without a reuse in its rounds fails its first check: out/lab/20261112-013003; the test tries 6 rounds since.)
* fix/w48-freed2 (the reuse is the game's chance: no reuse in 6 rounds of 150 frames in half the runs, rel/0.7.13
  20261112-031338, 20261112-041619 #2): a round without a reuse is inconclusive and the test plays another (300-frame
  hunts, up to 16 rounds within 9 min). PASS 10 of 10: out/lab/20261112-045530-2 (3), out/lab/20261112-050123-2 (7);
  FAIL with SPORE_FIX_OFF=npc.defer_identity: out/lab/20261112-050957 (2 of 2, the creature at the freed block
  destroyed with the queue).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12: fixed on fix/w48-freed (npc.defer_identity).
- 2026-11-12: fix/w48-freed2: the test retries the scenario until the reuse happened (10/10 with the fix).
