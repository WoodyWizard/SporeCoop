---
id: BUG-1283
title: WP-F2: a guest does not see the host's nest alphas as alphas (fork BUG-9115)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9115
role: guest
versions: 0.7.15.2
fixed_in: 0.7.16
branch: fix/w56-sync
commits: cdfe214d, 618fbd6e
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

CORRECTION (2026-11-19, fix/w57-sync, lab run 20261119-104136): `host_sends_alpha` asked `peer_families(0)`, which is
false on every guest, so the bit was never taken ("4 of 20 differ" with the fix on). Fix: `Session::host_families`
(see BUG-1282, docs/04-netcode.md#netcode-host-families).

## Fix
`coop.fix npc.alpha` (npc.cpp): NF_ALPHA = 64 (net/protocol.h) in the NPC state and spawn flags; a guest whose host
announces npc 1.2 or newer (0.7.16; 0.7.15.2 ships npc 1.1) sets the proxy's bit 0x1 to the host's every state (game::set_alpha). `coop.npc list` prints
`alpha=` (the game's own bit) on both roles for the test. Older peers ignore the bit.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Lab, first night run (rel/0.7.16 c7699dca): npc_alpha FAIL out/lab/20261119-104136 (rel/0.7.16 worktree), the gate bug
  above (identical to its twin).
- Lab after the fix (fix/w57-sync 6fdea267, run dirs in the fix/w57-sync worktree): PASS npc_alpha
  out/lab/20261119-111559 (0 of 20 proxies differ; a planted mark goes back; the host's new alpha is one on the guest
  and goes again). FAIL twin npc_alpha_off (SPORE_FIX_OFF=npc.alpha) out/lab/20261119-112631 for the right reason: the
  planted mark stays, the host's new alpha is not one on the guest.
- Rerun on the tree merged with rel/0.7.16 (fix/w57-sync 3801b9f5): PASS npc_alpha out/lab/20261119-121505.

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
- 2026-11-19: fixed (fix/w57-sync): see the CORRECTION under Root cause; PASS and FAIL twin run dirs above.
- 2026-11-23: released in 0.7.16.
