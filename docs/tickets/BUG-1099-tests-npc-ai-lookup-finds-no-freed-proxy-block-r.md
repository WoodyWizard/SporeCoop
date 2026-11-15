---
id: BUG-1099
title: tests: npc_ai_lookup finds no freed proxy block reused on a guest game after many warm runs
status: fixed
kind: test
stage: Creature
reported: 2026-11-14
source: lab 20261114-005006 npc_ai_lookup
role: guest
versions: rel/0.7.14
fixed_in: 0.7.14
branch: fix/w49-reuse
commits: 9b1a6c93
tests: npc_ai_lookup, proxy_identity, proxy_identity_tribe
signature: "FAIL a social click on a new creature at a freed proxy's address (0 reuses, 0 posted)"
docs: docs/re/creature.md#creature-reuse-merged, docs/06-testing.md#testing-w49-reuse, docs/known-issues.md#issue-npc-ai-lookup-reuse
related: BUG-0994, BUG-1097, BUG-0842
---

## Report
Final check 20261114-005006, `npc_ai_lookup` (lane 4, warm run 7 of these games, after kit_present_fault): besides
the inherited host fault (BUG-1097) "FAIL a social click on a new creature at a freed proxy's address (0 reuses, 0
posted)": all 21 spawned proxies (#148-#168) were hunted, 65-150 tries each, and no new creature took a freed block.

## Root cause
Observed (lane 6, 2026-11-14, `coop.npc chunk`, the status's `hdr_now=`): not the guest's heap age and not BUG-1097.
Outside a cinematic the game frees a proxy's creature block at once, and the chunk merges with the free chunk after
it (the creature's own blocks behind it): the header before the creature grows (0x16A9 -> 0x2451..0x9229). The hunt's
free check read bit 0 of the next chunk's header at the old size, a stale word then: 13 of 14 rounds that looked
"never freed" had a grown header. So the `f<N>` limit never started, and the old hunt (a creature a frame from the
destroy on, each miss destroyed at once: it gave its own fresh block back and the next try took that again) reached
the merged chunk only by luck: 0 of 21 rounds in the final check, 1 of 8 / 2 of 10 on an aged lane 6
(20261114-031112). A header with bit 0 clear (the chunk before is free: a backward merge) can never be reused at the
address; hunting adjacent proxies one after another makes that common (8 of 21 on a cold guest). The +8 count and the
distance to the avatar did not predict anything.

## Fix
`coop.npc reusehunt ... w<N>` (mods/coop/npc.cpp ReuseHunt free_first): no creature until the block is free (the free
check also takes a changed header of the target as free), then the tries at once; the round ends when it is not free
after N frames (60) or at once when the chunk before is free. `coop.npc chunk p<id>|<addr>`: size, prev_used, used,
next_used, refs. coop_npc_ai_lookup.py / coop_proxy_identity.py: `300 xwkb4f40` (4 a frame, misses kept, 40 tries
after the free), proxies with both neighbours in use first (`next_proxy`), a proxy made again is hunted again, up to 60
rounds in 170 s; `SPORE_REUSE_FLAGS=x` is the hunt before. Test-only commands: no switch.

## Verification
- FAIL-prone before: the final check 20261114-005006 (npc_ai_lookup, 0 reuses in 21 rounds); on lane 6 after 5 warm
  tests with the old hunt 20261114-031112: npc_ai_lookup 1 reuse in 8 rounds, proxy_identity 2 in 10.
- PASS after, the same 5 warm tests first, `--in-order`, 3 of 3: 20261114-043831, 20261114-044423, 20261114-045018
  (npc_ai_lookup reused in round 1 each time; proxy_identity 2 reuses in 2-3 rounds). One cold guest hunted to the
  end: 21 of 27 rounds reused. proxy_identity_tribe 20261114-045609 PASS (1 reuse in 3 rounds).
- Twins still FAIL: 20261114-045701 (npc.ai_lookup_live off: "engaged ['#1 (engages +1)']"; npc.proxy_identity off:
  "driven: [1, 2]").
- Other users of the hunt: defer_identity, npc_air_live, npc_herd_identity PASS (20261114-050153).

## History
- 2026-11-14: opened (fix/w49-warmfault, while checking BUG-1097).
- 2026-11-14: fixed on fix/w49-reuse (9b1a6c93).
- 2026-11-14: released in 0.7.14.
