---
id: BUG-0563
title: Tribe: the guest's creature from Creature stage appears as an enemy monster at the start of Tribe; killed, it leaves meat that never runs out
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-realign
commits: 736eaef5
tests: tribe_epic_species (coop_tribe_epic_species.py)
signature:
docs: docs/re/tribe.md#tribe-realign
related: BUG-0345
---

## Report
The host: after starting Tribe the guest's creature appeared as an enemy monster; they killed it and its meat is infinite. On Discord a video: the creature in the cutscene was the guest's previous creature from Creature stage.
Reports: SporeCoop-report-2026-11-04 190929 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs): at the host's Creature -> Tribe change (18:57:47) the guest's game was still in Creature
  ("partner in another mode"), the host's game had the guest's puppet (species 7C6E99B3, the guest's own creation
  29158399) until then and saved for the share ("save: 4 objects of ours are kept out of it").
* Inferred: the game's Creature -> Tribe conversion took the guest's puppet (or its herd) into the Tribe world
  as an animal (the cutscene creature, the "monster" with endless meat: a herd of ours without the game's
  tuning). Our objects must not cross the stage change (T1/W2).
2026-11-04 (fix/w43-realign): observed (player log): "an epic creature comes! together: 80 food" at 19:05:47, 8 min after the live Creature -> Tribe change at 18:57:47 (the co-op epic's first timer, tribe.cpp epic_tick); spawn_epic takes the species of the animal nearest the player village, and the guest's creature (its puppet, species of the guest's creation) had stood next to the host until the change. Observed (lab): after the live change the co-op epic was of a player's species (`player_species=1`, out/lab/20261104-214152). So the "monster" was the co-op epic with the guest's creature's species (the epic's meat: the game's own corpse of an epic). Inferred: the puppet (or an animal of the player's species) crossed into the Tribe world; not checked which object it was.

## Fix
tribe.cpp spawn_epic: the epic's species is never a puppet's, a proxy's or a session player's creature's (creations::player_species of each player). `coop.fix tribe.epic_species off`: the nearest animal (0.7.6). Not changed: whether our Creature objects stay in the Tribe world (T1's stage-change package).

## Verification
tribe_epic_species (the live Creature -> Tribe change from the creature fixture, then `coop.tribe epic` on the host): FAIL with `SPORE_FIX_OFF=tribe.epic_species` out/lab/20261104-214152 (species 7C329711, player_species=1), PASS out/lab/20261104-213045 (species 8E6A1FF5, player_species=0, 1 animal skipped).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), cause inferred from the logs.
- 2026-11-04 (fix/w43-realign): fixed (tribe.epic_species); runs above.
- 2026-11-05: released in 0.7.7.
