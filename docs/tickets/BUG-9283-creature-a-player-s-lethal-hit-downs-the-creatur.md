---
id: BUG-9283
title: Creature: a player's lethal hit downs the creature while a partner is here; a partner revives it, else it respawns at its own nest; everybody down: the party wipe
status: fixed
kind: feature
stage: Creature
reported: 2026-11-20
source: Lineages port (PR #196: BUG-9118, BUG-9146, BUG-9149, BUG-9226)
role: both
versions: 0.7.15.2
fixed_in: unreleased
branch: feat/lineages-rescue
commits:
tests: tests/creature_rescue_tests.cpp (make test), tools/tests/coop_creature_rescue.py (suite creature_rescue, creature fixture); manual two-instance runs on lane D (Windows), see Verification
signature: -
docs: docs/re/creature.md#creature-pack-rescue-downed
related:
---
## Report
The Creature part of the Lineages port's "separate lives" (owner, 2026-11-20): a player of the party who goes down in the
Creature stage is rescued by a partner or respawns at their own nest, and the party only pays when nobody helps; when
every player is down at once the party is wiped (everyone respawns). Lineages (PR #196) had it in creature_rescue.cpp
(BUG-9118 knockout and give-up, BUG-9146 any partner revives, BUG-9149 a minute, BUG-9226 party wipe); 0.7.15 had only
the pack's one-off rescue (teamplay.cpp, BUG-0099: a lethal hit near a partner leaves 40 % once a minute).

## Root cause
Not ported yet (a feature).

## Fix
`mods/coop/creature_rescue.cpp` (rules in `creature_rescue_logic.h`), MSG_CREATURE_RESCUE 95 (creature family 1.2: an
older peer ignores the type; a guest whose host never answers ends its own window 10 s after its end):
* `creature.rescue` (on): every game guards its own creature in teamplay::on_own_damage (before the pack's one-off
  rescue, inside the session setting creature.pack_rescue): a lethal hit while a partner plays in the same world puts it
  down (1 health, held at the spot, no hit lands) for 60 s (`creature.down_60`; off: 30 s). The host watches every window
  (a guest's from K_DOWN, repeated every second); a standing partner within 6 m for 1.5 s revives it (40 % health, 2 s
  without hits). Nobody came, or its player held G for a second (`coop.rescue giveup`): its game kills it the game's way
  (the respawn at its own nest, BUG-9262) and the pack loses 10 % of its DNA (the host's total). Off: the old code (the
  pack's one-off rescue, the game's death).
* `creature.party_wipe` (on): every player down for 2 s: every window runs out at once. Off: each waits out its time.
* A verdict the game cannot take (a caption that pauses the game: the hit does nothing) is tried again every second.
* The host sends the windows every second (K_LINES): "DOWN <s>: <name>" over a downed partner, "(DOWN <s>)" on its
  off-screen arrow, a red line for the own downed creature. No world save while a player is down (save blocker).
  `coop.rescue [down | giveup]` (status, tests).
Not ported: the progress debt (Lineages BUG-9119), the revive ring (BUG-9141), the death pose (BUG-9156), the spectator
camera, the adventure captain (BUG-9089).

## Verification
no-fail-run: a new feature (on the base, feat/lineages-tribe, `coop.rescue` is unknown and a lethal hit kills; with
`coop.fix creature.rescue off` the guest died at once: seen in the run below). Not run in the Linux lab (no lab here).
Manual runs, lane D on Windows 11 (Steam build), instances 7 (host, dev7) and 8 (guest, dev8), the Creature fixture
Boheus.spo (nests/fixture_boheus) with the guest playing its own creature 7CA81684 in its own nest, 2026-11-20:
* the guest's lethal hit (`coop.rescue down`): the guest's game hp 1.0 / 15, down; the host a window for player 1
  (58.5 s left); the guest the host's line; `coop.save` on the host refused ("a player is down"); a hit of 50 not dealt;
  the guest's creature put elsewhere came back to its spot (0.00 m). Host screenshot: "DOWN 55s: Bob" over the guest's
  creature, its arrow "Bob (DOWN 58s) 27 m"; the guest's game: "You are down ..." and its red line.
* the host's creature put 2 m from it: revived within a second, the guest at 6.0 / 15 (40 %).
* down 30 m from the host, nobody came (60 s, and 30 s with creature.down_60 off): the guest died after the window,
  the caption "You were killed in battle!", respawn 0 m from its own nest (60 m from the host's); DNA 90 -> 81 on both
  games.
* both down: the party wipe 2 s later, each respawned at its own nest.
* the guest gave up (`coop.rescue giveup`): its window ended at once, respawn at its nest.
* the host down, the guest put next to it: revived (4.0 / 10); the host gave up: respawn at its nest.
* `coop.fix creature.rescue off` on both: the guest's lethal hit killed it at once (hp 0.0), respawn at its nest.
* found on the way: a pausing caption (the brain upgrade's) made the verdict's lethal hit do nothing (the guest stayed at
  1 health, up): fixed in this ticket (the hit is tried again every second until the game takes it).
* found on the way: the first retry set 1 health every frame and undid the game's death; after a party wipe the host's
  creature stayed up at 1 health (236 retries): the retry no longer touches the health.

## History
- 2026-11-20: opened; implemented on feat/lineages-rescue (ported from Lineages' creature_rescue.cpp).
