---
id: BUG-0825
title: Creature: a deferred destroy can destroy a real creature at a reused address (no serial check) (suspected)
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0018 BUG-0066 BUG-0426
---

## Report
No player report. Suspected: a creature of ours queued for destruction during a cinematic, freed by the game meanwhile and replaced by a new NPC at the same address, gets that NPC destroyed when the cinematic ends; until then the NPC counts as a puppet (no AI, stripped from saves).

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): game.cpp destroy_deferred_tick (1611-1617) destroys a queued pointer that is merely still listed as a creature; no object serial, no 'still ours' check; while queued it stays in g_puppets (npc.deferpuppet), so is_puppet() is true for whatever object lives there. Confidence: low (needs the game to free it within the cinematic).

## Fix
Not fixed yet.

## Fix idea
Record game::object_serial when queueing and compare it before the destroy (and in is_puppet for queued entries).

## How to test
Test: during a cinematic queue a puppet's destroy, destroy it with destroy_untracked, spawn a console NPC at the same address (repeat until reused); after the cinematic the NPC must live (fix) vs destroyed (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
