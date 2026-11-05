---
id: BUG-0402
title: Tribe: one animal 8-25 m apart on every guest for minutes in campaigns (sync-check n apart), after the co-op epic comes
status: not-reproduced
kind: desync
stage: Tribe
reported: 2026-11-01
source: lab log gates over recorded campaign runs (BUG-0381 analysis, fix/w32-tribe-sync)
role: guest
versions: 
fixed_in:
branch: fix/w33-proxy-ground
commits:
tests: campaign_tribe, play_tribe, tribe_epic (sync-check gate)
signature:
docs: docs/re/tribe.md#tribe-npc-apart
related: BUG-0329, BUG-0425
---

## Report
Split off BUG-0381 (its `n apart`, 8 results). In the recorded Tribe runs `n apart` is of three kinds:
* **one NPC on every guest, 8-25 m, for minutes**: out/lab/20261031-024148 campaign#1, `n327` from 04:10:50 to
  04:21:38 on all three guests (24.0, 24.8, 14.5, 15.5, 10.1, 8.1, 13.8, 10.3, 17.5 m), the first MISMATCH 6 s
  after the host's "an epic creature comes! together: 80 food" (04:10:44; the next epic 04:32). Also
  20261031-185843 campaign#2 `n135` 11-16 m 2 and 7 min after the epic, 20261031-214905 campaign#1 `n312`
  15 m 2 min after the epic. Not every case follows an epic (214905 campaign#3 `n398` 18-26 m came a minute
  before it).
* 2-3 animals just over the 8 m limit (8.2-10.5 m) for one check pair: a running herd (the BUG-0305 /
  BUG-0281 class of proxies trailing on their path), e.g. 20261031-000328 campaign_tribe n156 / n157.
* one 510 m (20261030-210914 campaign#1 n658): an NPC at the planet's centre (the host logs "npc N is
  updated at the planet's centre ... dead=1" warnings in Tribe).
Not reproduced on lane 7 (fix/w32-tribe-sync): play_tribe 2 x 10 min, chaos_tribe 2, tribe_coop 2, campaign_tribe 1
(out/lab/20261101-131136), all without an `n apart`; a co-op epic spawned next to the village and left alone
(`coop.tribe epic`, 100 s, sampled every second on both games) stayed 0.5 m from its proxy (it stood still); hit once
(`coop.tribe epic hit <member> 1`) it walked at 4-5.5 m/s for ~10 s, its proxy at most 2.3 m off (90 s).

## Root cause
Open. Inferred for the first kind: the co-op epic (huge, fast while it fights the village) and its proxy
on the guests: the proxy does not keep up (the proxies' walking, npc.cpp; reworked on another branch now).
Not checked: which NPC n327 was (the logs had no per-NPC line). Since fix/w32-tribe-sync a MISMATCH line is
followed by `sync-check:   n327 here: <the NPC's coop.npc list line>` on each game (species, hp, speed, pack /
family on the host; err / age / want / at on a guest), so the next campaign run says what the NPC was.

Update (fix/w33-proxy-ground, lane 7, current code with npc.walk_tribe / walk_place):
* Every recorded case (2026-10-30/31 campaign runs, 0.7.3.1 builds) predates `npc.walk_tribe` (BUG-0361, cc489d4,
  2026-11-01 08:04), the change that made a moving Tribe animal's proxy walk on the host's path (before: drive::step,
  0.6-1.4x the path in only 72-77 % of the windows, steps of 0.8-2.3 m).
* Not reproduced with the sync-check gate on: campaign_tribe (4 players, live Creature -> Tribe, out/lab/20261101-153010,
  OK, no MISMATCH), play_tribe 10 min with the co-op epic at s 480 hit by both players (same run: no `n apart`; it
  failed only on its citizens p90, BUG-0397), tribe_epic, tribe_coop, tribe_play, chaos_tribe, tribe_walk(_player),
  npc_walk_tribe (same run), and no `n apart` in any lane 7 run of this branch (20261101-140615 ... 161258).
* The first kind's inference tested: a co-op epic spawned on the tribe fixture (`coop.tribe epic`) and hit every 12 s
  (`coop.tribe epic hit <member> 1`, 75 s, sampled every 0.5 s on both games, host position taken to the proxy's
  distance from the centre): with npc.walk_tribe proxy off p50 0.13 m, p90 0.99 m, max 2.08 m (epic up to 6.5 m/s);
  with `coop.fix npc.walk_tribe off` (the old drive::step) p50 0.51 m, p90 0.61 m, max 2.57 m. So an epic that walks
  and fights a little does not leave its proxy 8-25 m behind, with either driving; what the campaign's epic did (a
  village fight) is not known.
* The third kind (510 m, an NPC the host's game updates at the planet's centre) is BUG-0339's state (Creature too),
  not this ticket's.
* The second kind (a herd 8.2-10.5 m for one check pair): the sync-check compares absolute positions, while a guest
  drives Tribe proxies at its own ground height (BUG-0329: 3.9-5.3 m radial difference in the live campaign worlds);
  radial 4-5 m plus the lag of a running herd reach 8 m. No current-code run showed it, so the check was not changed;
  if it comes back, compare Tribe NPCs at the guest's distance from the centre (as the monitor's proxies rule does)
  and give running ones a speed slack (BUG-0378's players).

2026-11-05 (fix/w44-height), the fourth kind: observed in the host's own line of the same run, `n146 ... agl=0.00`: the
host's NPC stood on the host's terrain; the guest's terrain at that point lies 8.39 m higher (`want_agl=-8.39`) and
the proxy stood on the guest's ground (`agl=0.00`). That is the height rule's answer (on the sender's ground: on this
game's ground), so the proxy was placed right; the `n apart 8.4` is the terrain difference between the two games
(BUG-0329's kind, 8.4 m here) in a check of absolute positions (docs/re/tribe.md#tribe-animal-height). Not changed:
the sync-check stays as strict as it is.

## Fix
None: not reproduced on the current code (see the update above). The diagnostics stay: the `sync-check:   nNNN
here:` lines, and `coop.npc list` lines now end with the heights over the ground (`agl=`; guest `want_agl=`), so a
radial difference shows in them.

## Verification
no-fail-run: not reproduced on the current code (campaign_tribe, play_tribe with the co-op epic, tribe_epic in
out/lab/20261101-153010; the epic experiment above).

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w33-proxy-ground: not reproduced on current code; the epic inference tested (no 8 m with either driving).

Update (2026-11-05, rel/0.7.7 final check, out/lab/20261105-015350 campaign_ct): a fourth kind. One standing NPC
(`n146`, species 06725707, spd 0) 8.4 m apart on two guests for one check pair at s 662: the host's NPC is 8.39 m
under the terrain (dist 522.2 from the centre, the guest's `want_agl=-8.39`), the guest's proxy stands on the
ground (`agl=0.00`, dist 530.6). So the gap is radial: the guest grounds a proxy whose host NPC is below the
ground (in water or sunk). Not a walking proxy trailing. Not a release blocker; left open.
- 2026-11-05 (fix/w44-height): fourth kind analysed (a terrain difference; the proxy is where the rule puts it); left open.
