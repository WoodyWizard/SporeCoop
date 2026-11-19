---
id: BUG-1302
title: WP-S2f: the fallback paint lists lack the stock Cell base paints
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-19
source: plan
role: 
versions: 
fixed_in:
branch: feat/w56-editors
commits:
tests: none (static evidence)
signature: Swarm::IEffectsManager::Get 0x0067DD90
docs: docs/re/creations.md#creations-own-editors-robust, docs/06-testing.md#testing-own-editors-robust
related: BUG-0559, BUG-0452
---

## Report
Lineages comparison (item 9): a native landfall creature keeps a Cell base paint; our paint check asks the effect
directories first (BUG-0559), only without an effects manager the fallback lists decide, and they lack the 19 stock
Cell base paints (the fork's kBaseCellPaints; ours has only A3163E4A of them).

## Root cause
✔ (byte scan of the GOG 2024 exe, 2026-11-19) not a bug: `IEffectsManager::Get` 0x67DD90 reads the global 0x15FD8E8;
its only writer is the setter 0x67DFB0, called once with the manager at the graphics start (0x7EADDB in 0x7EAAE0,
`push [esi+0x60]`) and once with 0 at the shutdown (0x7E9585 in 0x7E9520). paint_known runs for bundles and models in
a session, after the start and before the end, so the effect directories always answer and the fallback lists are
never asked for a cell paint.

## Fix
None (no guard needed).

## Verification
no-fail-run: the fallback branch cannot be reached in a session (the effects manager exists from the graphics start to
the shutdown); evidence in docs/re/creations.md#creations-own-editors-robust and the .sym entry of
Swarm::IEffectsManager::Get.

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-editors: not a bug in our code (evidence above), closed as not-reproduced.
