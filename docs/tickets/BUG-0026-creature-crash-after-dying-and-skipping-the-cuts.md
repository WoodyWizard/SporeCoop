---
id: BUG-0026
title: Creature: crash after dying and skipping the cutscene
status: not-reproduced
kind: crash
stage: Creature
reported: 2026-10-24
source: player report (Reddit)
role: 
versions: 0.7.2
fixed_in: 
branch: fix/w46-n1-crashes
commits: 
tests: creature_death, creature_death_coop, creature_death_coop_slow
signature: 
docs: docs/known-issues.md
related:
---

## Report
"Every time I die and skip the cutscene my game crashes."

## Root cause
Not reproduced (0.7.11 N1, see Verification). The report (known-issues: "in the second phase (playing alone)") is
the Creature stage alone with the mod loaded: 16 trials in 0.7.2, and on the current build (rel/0.7.11) the 4 skip
variants alone again plus 16 deaths in a session (the host dying while the guest watches, then the guest), also at
20 fps over the player network: every respawn came, no fault on either game.
Reasoning (inferred, no crash file or log of the player): alone, the mod's own work around a death is its hooks on
the game's paths (the damage path, the stage event handler h_StageMsg, the selection UI, effects); the death caption
and the respawn cutscene are the game's. The 0.7.2 code had faults in those hooks fixed since (BUG-0008 the stage
event handler reading +0xE84 of a creature without it, 0.7.2.3; BUG-0014 the respawn as a hatchling, 0.7.2.4), and
no fault of that kind shows on the current build under the same skips. The player's build (GOG 2016 is unsupported
since 0.7.8) and Windows are not known; a Windows-only crash cannot be excluded.

## Fix
None.

## Verification
* 2026-10-25: coop_creature_death.py 16 trials (death, caption, skip), alone.
* 0.7.11 N1 (fix/w46-n1-crashes, GOG 2024): creature_death OK, out/lab/20261107-223550 (lane 9);
  creature_death_coop (`--coop`: the session kept, the host then the guest dies, 4 skip variants each) OK, 0 faults
  on both games, out/lab/20261107-231415 (lane 9); creature_death_coop_slow (the same at 20 fps over the player
  network) OK, 0 faults, out/lab/20261107-231415.

## History
- 2026-10-24: reported (player report (Reddit)).
- 2026-10-25: not reproduced, see Root cause.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-30: fix/w13-creature: creature_death (die, skip the caption and the respawn cutscene) OK in out/lab/20261030-141545 (fix/w13-creature, lane 5, GOG 2024), 0 faults. Not reproduced; stays.
- 2026-11-07 (fix/w46-n1-crashes, 0.7.11 N1): one more attempt in a session (host and guest die, 20 fps, player network): not reproduced; closed with the runs (lab 20261107-223550, 20261107-231415).
