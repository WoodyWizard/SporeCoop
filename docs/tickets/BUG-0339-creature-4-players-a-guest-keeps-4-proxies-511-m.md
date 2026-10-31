---
id: BUG-0339
title: Creature, 4 players: a guest keeps 4 proxies 511 m from the host's NPCs, which sit at the planet's centre (0 0 0) and are still tracked
status: open
kind: desync
stage: Creature
reported: 2026-10-31
source: lab 20261031-083425 campaign#2 08:53
role: guest
versions: main 39f4004
fixed_in:
branch: fix/w21-creature
commits:
tests:
signature:
docs:
related: BUG-0274, BUG-0280, BUG-0300
---

## Report
Lab 20261031-083425 campaign#2 (lane 4, 4 players), creature/play 08:53:10-13 (game clocks 09:53):
"[desync] Bob: too few of the host objects" (coverage), census "c12/c17: the host has it, the guest
not (81/88 m from the guest)" and "c40, c41, c42, c43: 511 m apart". The host's sync-check said the same
at 09:53:03 and 09:53:36 ("n40 510.6 ... apart", n12/n17 missing on the guest). Only Bob; Carol and
Dave were fine.

## Root cause
Partly observed, not reproduced (fix/w21-creature, lane 5, 2026-10-31):
* Host census (6 dumps over 2 s, and the sync-check 33 s apart): c40-c43 (species 0680BF12, a 4-member
  herd, the newest ids) were own=npc, alive, at **(0 0 0)**, the planet's centre where the game parks
  pooled creatures. The sync-check lists only NPCs updated in that frame, so the game still updated
  them there. BUG-0274's `retire_pooled` only looks at NPCs the game no longer updates, so the host
  kept them.
* Bob's census: proxies c40-c43 alive on the surface at (24-36 -447..-451 -238..-245), 511 m from the
  host's objects, about 170 m from Bob (more than the 130 m at which the host despawns them for a
  guest). They were not dropped as stale either, so Bob was still getting states for them.
* Before: Bob rejoined at 09:50:15 and died at (118 -450 -188) at 09:52:31. He was dead for 22 s
  (no view centre on the host), then respawned at the nest (137 -473 -107). The host had died at 09:51:50.
* Not explained: why the game updates creatures at the centre, and why the host did not despawn the
  proxies for Bob. Ruled out: `coop.npc pool` (the game's own pool call, creature vt 0xC8(1)) on a host
  NPC: it stops being updated, the host retires it and the guest drops the proxy within 1 s (lane 5).
  A host NPC pinned at the centre after each update (`coop.npc centre`) did not reproduce it either:
  the game puts it back each update, and the guest dropped the proxy (it was far from the guest).
  Neither probe matched the campaign.

## Fix
No fix yet: the chain is not understood, and no probe reproduced it.
* A defensive change (the host ignoring NPCs updated at the centre, a guest never driving a proxy
  to the centre) was tried and dropped. The only state I could make in a test (an NPC pinned at
  the centre) passes without it, so it would have had no FAIL run. It may also not match what
  happened: the host's frame-time positions can differ from the console's census.
* In now, diagnostics only: the host logs "npc N (species ...) is updated at the planet's centre:
  herd ..., X m from where it was a frame before, dead=..." once per stay. The next campaign
  occurrence then names the NPC, its herd and the jump.
* Test command `coop.npc pool <id>` (host) calls the game's own pool on an NPC
  (creature vt 0xC8(1)).

## Verification
no-fail-run: not reproduced (see Root cause); diagnostics only.

## History
- 2026-10-31: opened.
- 2026-10-31: fix/w21-creature: analysed (host NPCs at the planet's centre still updated, a guest keeping their proxies on the surface), not reproduced; diagnostics in; stays open.
