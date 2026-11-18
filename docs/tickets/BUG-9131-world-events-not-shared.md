---
id: BUG-9131
title: Creature: meteors, rain, the UFO and the night predator run separately in each game
status: fixed
kind: desync
stage: Creature
reported: 2026-11-11
source: owner reports on 0.7.8-lineages50-dev ("meteor event and time of day seem desynced"; "the space ship wasn't synced either")
role: all
versions: all
fixed_in: 0.7.8-lineages51-dev
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9 + guest 8: host `coop.drama start 3` / `start 2`, guest `coop.drama start 3|5|1`, `coop.drama` counters on both, `coop.wstate` tod on both
signature: guest log "drama: night_predator event starts (guest)" / "baby_game ... (guest)"
docs: docs/re/creature.md#creature-drama-events
related: BUG-0486 BUG-0558 BUG-0579
switch: creature.shared_events
---

## Findings
* The time of day is synced (MSG_WORLD_TIME 1 Hz, the wsync tod row): QA readings of both games were within the
  1-2 s between the reads (host z 0.5448 / guest 0.5380 at the same second), the row re-applied 4 times after the
  join.
* Each game's Drama Manager ran its own events: a guest's game started its own night predator and baby game (logs),
  its own meteor strikes and rain storms; only the UFO was held back (BUG-0486), so the host's UFO was never seen.

## Fix (drama.cpp)
* Meteor strike (3), rain storm (5) and the UFO visit (2): the host's start goes to the guests (MSG_DRAMA 82), which
  start the same kind; a guest's own start of these kinds is refused. On a guest the UFO's pick treats every creature
  as epic (player_rules pick_epic): it takes nothing, the host's UFO takes the host's creatures and the NPC sync shows it.
* Night predator (1): a guest starts none (the host's predator is an NPC the guest already mirrors).

## Verification
Host `coop.drama start 3`: host meteor relayed_tx=1, guest relayed_rx=1 started=1 active=1. Guest's own
`start 3|5|1`: refused (meteor refused=1, rain refused=2, night_predator refused=1). Host `start 2`: ufo relayed_tx=1,
guest relayed_rx=1 active. Not seen on screen: the meteors and the ship (outside both cameras at the time).
