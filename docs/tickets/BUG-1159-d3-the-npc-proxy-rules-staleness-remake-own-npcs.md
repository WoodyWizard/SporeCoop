---
id: BUG-1159
title: D3: the NPC proxy rules (staleness, remake, own NPCs, placing, mound) in a portable header with unit tests
status: fixed
kind: tooling
stage: Creature, Tribe
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-npc
commits: 314a9f21, aa4b0a16, 76cc685f
tests: tests/npc_tests.cpp (test_proxy_rules)
signature:
docs: docs/01-architecture.md#arch-npc-proxy-rules
related: BUG-1140, BUG-0009, BUG-0611, BUG-0383, BUG-0412, BUG-0550, BUG-0562
---

## Report
Plan item D3 of docs/plan-0.7.15.md (audit STG-8, TST-9) for NPC: the guest's proxy rules (staleness, placement, hold
decisions) as a portable header with unit tests.

## Root cause
Structure: the rules lived inline in npc.cpp's guest_tick and update_filter, next to game calls, testable only in the lab.

## Fix
mods/coop/npc_proxy_rules.h (namespace npc_rules): `states_flow` / `stale` (Fresh / Thaw / Drop: BUG-0009, BUG-0611),
`remake` (BUG-0383), `keep_locals` (BUG-0412), `place` (Air / Walk / Stand / Corpse: BUG-0550, BUG-0281, BUG-0361) and
`mound_lift` (BUG-0562). npc.cpp asks them (guest_tick's stale drop, which is the registry's sweep since C1, the remake,
the placing; update_filter; proxy_on_mound): the same decisions as before (a refactoring, no switch).

## Verification
no-fail-run: a refactoring (the same decisions); tests/npc_tests.cpp test_proxy_rules (make test, ASan / UBSan) fails on
mutations of the rules (no thaw, a body always remade: 4 checks fail). The lab proof is BUG-1140's run of the stage's
tests (the rules are in the code those tests ran).

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-npc): the header and its tests (314a9f21), npc.cpp asks it (aa4b0a16).
- 2026-11-18: released in 0.7.15.
