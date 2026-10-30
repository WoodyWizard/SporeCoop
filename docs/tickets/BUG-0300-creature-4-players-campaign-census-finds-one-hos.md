---
id: BUG-0300
title: Creature, 4 players: campaign census finds one host NPC missing on a guest at 90 m, the edge of the NPC range
status: open
kind: test
stage: Creature
reported: 2026-10-30
source: lab 20261030-060122 campaign_short#3 (fix/w10-cell-4p)
role: guest
versions: fix/w10-cell-4p a964c90
fixed_in:
branch: fix/w13-creature
commits: 2d444ef, 65cf54c
tests:
signature:
docs: docs/re/creature.md#creature-proxy-guest-side
related:
---

## Report
campaign_short#3 (lab 20261030-060122, lane 4, after the Cell stage passed): `FINDING [census] Bob: c35: the host has it, the guest not (npc 067B0EFF alive=1 hp=6 ai=1, 90 m from the guest) (creature)`. One NPC exactly at the 90 m boundary the census uses (the host streams NPCs within 110 m, npc_near 90). Seen once; the other two runs of the batch passed. Not analysed (outside fix/w10-cell-4p's Cell task). Likely a check at the boundary (the guest moved, the NPC entered 90 m between the two samples), not a lost NPC.

## Root cause
Not found. What the run shows (fix/w10-cell-4p lab 20261030-060122, both censuses and Bob's log):
* Not a range edge: three other members of the same herd (c34, c36, c37, 77-84 m from Bob) were on
  Bob; c35 at 90.0 m was not. The host sends NPCs within 110 m of a guest (keeps them to 130 m).
* Bob's trace 1 s later counted 20 proxies with states, its census 19 creatures that are proxies:
  the proxy was in Bob's list (states arriving) but its object was no creature of Bob's world (not in
  the noun list, destroyed, or without a species). Bob's sync-check said the same ("n35 missing on
  the guest") at 07:10:18. No "proxy spawn failed" line in Bob's log. Bob had rejoined 1.5 min before.
* Ruled out (lane 4, 2026-10-30): the guest's own relevance pass does not pool proxies (they have
  `+0xB67` = 0, the pass needs it; the guest's camera 305 m from its proxies for 45 s pooled none);
  4 rejoins of a 2-player guest at 40-100 m from the start: no proxy missing at any census.

## Fix
No fix of the cause. Diagnostics and a guard, `npc.proxy_listed` (npc.cpp): once a second the guest
checks every spawned proxy's object against the creature noun list. One that is not there was freed
behind the mod's back (driving it wrote into freed memory): the log says "npc N: the proxy's creature
... is no creature of this world any more", the pointer is forgotten without a read and the proxy is
made again. A listed proxy without a species is logged ("has no species"). `coop.fix
npc.proxy_listed off`: logged only.

## Verification
no-fail-run: not reproduced (no way found to make a proxy leave the world unnoticed); the next
campaign census finding of this kind comes with the log line that names what happened.

## History
- 2026-10-30: opened.
- 2026-10-30: fix/w13-creature: analysed (not a range edge; a proxy in the list without a creature in the world), not reproduced; diagnostics + guard npc.proxy_listed; stays open.
- 2026-10-30: fix/w14-creature: guard kept; no "no creature of this world" / "has no species" line in any lab game log of fix/w13-creature and fix/w14-creature (about 40 Creature runs, lanes 4-5); stays open.
