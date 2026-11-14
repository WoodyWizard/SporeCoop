---
id: BUG-1099
title: tests: npc_ai_lookup finds no freed proxy block reused on a guest game after many warm runs
status: open
kind: test
stage: Creature
reported: 2026-11-14
source: lab 20261114-005006 npc_ai_lookup
role: guest
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests: npc_ai_lookup
signature:
docs: docs/known-issues.md#issue-npc-ai-lookup-reuse
related: BUG-0994, BUG-1097, BUG-0842
---

## Report
Final check 20261114-005006, `npc_ai_lookup` (lane 4, warm run 7 of these games, after kit_present_fault): besides
the inherited host fault (BUG-1097) "FAIL a social click on a new creature at a freed proxy's address (0 reuses, 0
posted)": all 21 spawned proxies (#148-#168) were hunted, 65-150 tries each, and no new creature took a freed block.

## Root cause
Not the fault inheritance of BUG-1097: the injected faults are on the host, the reuse hunt (`coop.npc reusehunt`)
runs on the guest's game. Inferred: the hunt depends on the guest's heap handing the freed creature block to the next
creature of the species; a guest game that has run many tests (warm run 7) has more free blocks of that size and
seldom reuses this one in 150 tries. Recorded runs: warm run 2 (20261113-025531) hit on the first proxy, cold
(20261114-022615) on the fifth, warm after three kit fault tests (20261114-030034, warm run 3) on the first.
Not checked: the guest heap itself (no heap census of the failing run).

## Fix
Open. Options: `warm=False` for npc_ai_lookup (and proxy_identity / proxy_identity_tribe, the same hunt), or a
hunt that frees several blocks of the size first, or more tries.

## Verification
no-fail-run: open; seen once in the final check, not reproduced on lane 6.

## History
- 2026-11-14: opened (fix/w49-warmfault, while checking BUG-1097).
