---
id: BUG-0639
title: Creature: the guest can attack the creatures of a nest that is evolving on the host
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-05
role: guest
versions: 0.7.8
fixed_in:
branch: fix/w46-creature-p0
commits: 5a310d25, fc639e24, 3ce217e3
tests: family_attack (coop_family_attack.py), nest_evolve_probe (coop_nest_evolve_probe.py)
signature:
docs:
related: BUG-0464, BUG-0460, BUG-0418, BUG-0640, BUG-0668
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-05 (the guest): "for the host the nest is evolving, for the
guest it can attack them in creature stage".

## Root cause
Observed (logs): nothing explains it directly; the host's game refused none of the guest's hits in Creature (no
"spared" lines); around s 1265-1321 the host logs kills of several species (e.g. 06539227 "relation 5 -> 5 ... killed
0 -> 4", event 5 at s 1314).
Inferred, two readings: (a) an NPC species evolving at its nest is shielded on the host (the game's evolve state) but
its proxies on the guest are not; (b) the "nest" is the host's family nest: the guest plays another species (its own
creature), so its game treats the host's family as foreign and attackable (the BUG-0418 class, see BUG-0642).
Not determined: which nest the player meant (the screenshot was not decisive). Related: BUG-0464 (NPC nest evolution
animations relayed, fixed 0.7.4), BUG-0460 (nest state, fixed 0.7.4).

## Fix
Not fixed: not reproduced. `coop.nest evolve` (nest.cpp) prints every herd's evolution fields on either game for the
next report; the probes nest_evolve_probe and migration_probe (extended suite) and the test family_attack stay.

## Verification
no-fail-run: not reproduced, after three attempts:
* Reading (b), the guest's own creature against the family nest (the guest played its own creature 'Brogity'; the
  host's game had the goal "Migrate to a new nest"): family_attack, out/lab/20261107-081513: the guest bites a family
  member with real input 15 times, with the world's species and with its own creature: the host's member keeps 10.0
  hp, and vt 0xD4 (friendly) is (1, 0, 0) in both games. The guest cannot attack the family.
* Reading (a), an NPC nest evolving: nest_evolve_probe. The first run (out/lab/20261107-080730) printed only herds with
  +0x85 = 1 or a generation, and +0x85 is 1 from the herd's constructor 0xC6C2B0 for every herd: the rows were the
  family's, the NPC herds' evolution was filtered out (and the guest's mirrors were not read: wrong row format). The
  fixed probe (out/lab/20261107-081734): +250 DNA evolves every NPC herd (generation +0xF0 0 -> 1, a new species) at
  once on the host and within 18 s on the guest, with other thresholds +0x94 in each game; a nest the host's avatar
  made extinct stays generation 0 on the host and evolves to a new species on the guest: a desync, opened as
  BUG-0668. The evolving members themselves (out/lab/20261107-082348): the guest waited at the next nest, the host's
  herd there went to generation 1 at the DNA (its members keep the old species 065CB8BD for now); the guest hit their
  mirrors at once and the host's game took every hit (#6 6.0 -> 3.0 -> 0.0, #7 7.5 -> 4.5): the host's game does not
  shield an evolving nest's members, so "the guest can attack them" is the same in both games. migration_probe (out/lab/20261107-081510):
  no family herd evolution with the editors.
* The report's logs: at the mark (s 1321) the host had just killed the 4th member of species 06539227 (its hunt card
  "The Squibble species is extinct!"); its log has no evolve line; members of that species were pooled by the host's
  game at s 1325 (BUG-0339 lines), during the guest's 9 s freeze. Inferred: what the guest saw was the
  migration desync (BUG-0640, fixed: the host's game had migrated, the guest's never did) or the pooled members still
  shown for a moment, or the herd evolution running apart in the two games (BUG-0668), not a rule that shields an
  evolving nest on the host.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07 (fix/w46-creature-p0, P0-Creature of 0.7.10): closed not-reproduced: family_attack PASS 20261107-081513 (the guest cannot hurt the family, own creature too), nest_evolve_probe 20261107-081734: the NPC herds evolve in each game apart (new ticket BUG-0668), the members of an evolving herd take the guest's hits in the host's game (20261107-082348), no shield; likely causes BUG-0640 (fixed) and BUG-0668.
