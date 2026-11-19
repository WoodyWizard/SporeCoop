---
id: BUG-1283
title: WP-F2: a guest does not see the host's nest alphas as alphas (fork BUG-9115)
status: open
kind: desync
stage: Creature
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9115
role: guest
versions: 0.7.15.2
fixed_in:
branch: fix/w56-sync
commits: cdfe214d
tests: npc_alpha, npc_alpha_off
signature:
docs: docs/re/creature.md#creature-nest-alpha
related: BUG-0461, BUG-0118
---

## Report
Found by the Lineages fork (PR #196), its BUG-9115 (row 8): a nest's alpha is cCreatureAnimal +0xB58 bit 0x1 (the card
"Alpha <name>", one more social round, SocialPropose 0xDA43D0). Our NpcFlags carried dead / family / impressed / pack /
summoned / air: a guest's proxy kept the bit its own game's herd gave it (the fork saw 4 of 15 proxies differ).

## Root cause
Observed (code): npc.cpp host_state / send_spawn never sent bit 0x1; the guest's state apply set impressed only.

## Fix
`coop.fix npc.alpha` (npc.cpp): NF_ALPHA = 64 (net/protocol.h) in the NPC state and spawn flags; a guest whose host
announces npc 1.2 or newer (0.7.16; 0.7.15.2 ships npc 1.1) sets the proxy's bit 0x1 to the host's every state (game::set_alpha). `coop.npc list` prints
`alpha=` (the game's own bit) on both roles for the test. Older peers ignore the bit.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
Pending: written and registered, not run yet (the lab waits for the orchestrator's "lab ok" in this batch: the CPU is
busy with another task). Runs to do: `npc_alpha` (must PASS) and `npc_alpha_off` (twin, must FAIL).

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
