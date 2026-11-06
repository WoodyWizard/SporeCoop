---
id: BUG-0640
title: Creature: the host's species migrates to a new nest, the guest's game never does (no claim, no DNA, its respawn stays at the old nest)
status: open
kind: desync
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-04, M0ng0r-07
role: both
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0019, BUG-0508, BUG-0496, BUG-0418
---

## Report
Discord report 2026-11-06 (0.7.8): the host (discord-2026-11-06/Sterben-04): "desynced nest for players"; the guest
(M0ng0r-07): "after going to the new nest I didn't get DNA or the cutscene, and because of that my spawn point wasn't
there either".

## Root cause
Observed (the respawn places in the trace lines): the host's respawn nest moved twice: (136,-473,-107) until s 1440,
(-279,-419,14) from s 1761, (-489,-63,141) from s 2506: the host's game migrated twice. The guest always respawned at
the first nest ((136.7,-472.9,-107.1) at s 1859, 2324, 3875); before dying at s 1857 it stood ~410-440 m from its own
nest (about the distance between nest 1 and the host's new nest 2). No CRG_ClaimNest, migration or goal line in the
guest's log.
Observed: both in-place decisions logged "migration 0 -> 0" (s 792 "cinematic 0"; s 2295 "age 1 -> 0, cinematic 1"),
so no world carrying the migration was sent: the code sends the world only when it sees a migration started.
Inferred, not checked: the decision read the migration property 0x514A219 before CreatureGame_AfterEditor set it (no
cinematic waited for at s 792), or the guest's own species (not the world's player species) cannot claim a nest:
CreatureGame_NestHerdFor(1) looks for nests of the player species (BUG-0642, class BUG-0418).
Related: BUG-0019 (the migration reaches the guest only through a world share, fixed), BUG-0508 (the evolve-in-place
path that sends no world, fixed 0.7.4), BUG-0496 (no heal at the home nest, open). Possible regression of the BUG-0019
class through the BUG-0508 in-place path.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: coop_migration.py through the in-place path: the host crosses a brain threshold, then the editor vote; log
     host_migration() at the decision and 30 s later; assert the guest's 0x514A219 reaches 2 and it claims the nest,
     once with the world's species and once with an own creature. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
