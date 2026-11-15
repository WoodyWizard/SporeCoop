---
id: BUG-0640
title: Creature: the host's species migrates to a new nest, the guest's game never does (no claim, no DNA, its respawn stays at the old nest)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-04, M0ng0r-07
role: both
versions: 0.7.8
fixed_in: 0.7.10
branch: fix/w46-creature-p0
commits: 6a3d9e08, 99320566, 4fbb7cb9, bdba70da
tests: nest_follow (coop_nest_follow.py), nest_follow_own (coop_nest_follow.py --own)
signature:
docs: docs/re/creature.md#creature-nest-follow
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
`wsync.creature_nest` (wsync.cpp, row 11, Creature): the host sends its migration state (cPlayer property 0x514A219)
and its home nest (its avatar's herd's nest). A guest with no migration of its own whose home nest is the one the host
migrates from (host state 1/2), or not the one the host lives in (state 0, after its claim), and whose own
`NestHerdFor(1)` is another nest, gets property 0x514A219 = 1 and its avatar herd's brain range emptied, as
CreatureGame_AfterEditor does: its own NestTick sets the goal and claims on arrival (CRG_ClaimNest, the DNA, the herd
= its respawn place and family). A guest whose state stays 1 for 15 s while the host's is not gets state 2.
`coop.fix wsync.creature_nest off`: each game migrates only at its own editor or respawn (0.7.9).

## Verification
coop_nest_follow.py (suite nest_follow), the report's way: the pack earns DNA for a bigger brain in play (no editor),
the host's creature dies and respawns (`coop.team die`): only the host's game migrates; the host claims its new nest,
then the guest walks there. Asserted on the guest's game: its own CRG_ClaimNest cinematic, its avatar's herd = the
host's new nest (respawn place and home), and the heal there.
* FAIL with `SPORE_FIX_OFF=wsync.creature_nest`: out/lab/20261107-074327 (no claim, the guest's home stayed the first nest).
* PASS: out/lab/20261107-074330, out/lab/20261107-074824; with the guest on its own creature (--own):
  out/lab/20261107-074827.
* The editor path (`--editor`: the vote, both editors) passed with the fix off and on (out/lab/20261107-073825,
  073828): each game's AfterEditor migrates by itself, so it is not this bug.
* Not verified: two migrations in a row (the report's second one); a guest joining during the host's migration.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07 (fix/w46-creature-p0, P0-Creature of 0.7.10): fixed (wsync.creature_nest); FAIL 20261107-074327, PASS 20261107-074330, 20261107-074824, own creature 20261107-074827.
- 2026-11-07: released in 0.7.10.
- 2026-11-10 (plan/w47-closed-fixed): fixed-ticket review: the guest's migration target is compared with the host's only after the host's claim (wsync.cpp:844), not while the host migrates; new ticket BUG-0881 for the two-migration / far-guest case.
