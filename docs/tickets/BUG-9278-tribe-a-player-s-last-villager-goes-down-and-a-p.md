---
id: BUG-9278
title: Tribe: a player's last villager goes down and a partner revives it; unrevived, the party loses its newest win
status: fixed
kind: feature
stage: Tribe
reported: 2026-11-20
source: Lineages port (PR #196: BUG-9118, BUG-9135, BUG-9155)
role: host
versions: 0.7.15.2
fixed_in: unreleased
branch: feat/lineages-rescue
commits:
tests: tests/tribe_rescue_tests.cpp (make test), tools/tests/coop_tribe_rescue.py (suite tribe_rescue, tribe fixture); manual two-instance run on lane B (Windows), see Verification
signature: -
docs: docs/re/tribe.md#tribe-founder-rescue
related: BUG-9268 BUG-9260
switch: tribe.founder_rescue tribe.party_wipe tribe.down_60 tribe.wipe_reload
---

## Report
The Tribe PR of the Lineages port (BUG-9268, every player's own village) left out "the rescue of a downed founder and
the party wipe" (pr-tribe.md, "Not ported from Lineages (yet)"). In Lineages (PR #196) a player's last villager is not
killed while a partner can help: it goes down, a partner's villager revives it, and when nobody comes the whole party
pays (owner decision 1U, 2026-11-12: "a players whole tribe causes the loss of progression for both players,
encouraging rescues").

## Root cause
Not ported yet (a feature).

## Fix
`mods/coop/tribe_rescue.cpp` (rules in `tribe_rescue_logic.h`), host side in the citizen damage hook, the behaviour tick,
the orders and the host tick:
* `tribe.founder_rescue` (on): a lethal blow on the last living member of a player's village (the host's or a partner's
  own village) while another player with a village of their own is here downs it for 60 s (`tribe.down_60`; off: 30 s;
  Lineages' core.down_60 / BUG-9118's 30 s). Down: 1 health, held at its spot, no behaviour, no orders, no blows taken or
  dealt. A living villager of another player's village within 15 m for 1.5 s revives it (40 % health). Off: the game's
  death as before.
* `tribe.party_wipe` (on): the window ends without a revive (or its player held G for a second / a guest's
  CMD_GIVE_UP = 5): the newest win of the stage goes (the goal history record, a step of the goal bar, the top totem piece
  of every player village with that many pieces, an ally becomes a rival at -5); with no win every player village loses
  10 % of its food. The villager gets up at its village with 40 % health. Off: it gets up at home for 10 % of its own
  village's food (Lineages' non-capture fallback).
* `tribe.wipe_reload` (on): after a wipe that took a win the host saves, sends every guest the world and reloads it, so
  every game redraws the totem and the goal history (Lineages reloaded after every wipe; a food loss needs none).
* The guests get the windows (MSG_TRIBE_ANIM / MSG_NPC_ANIM kind 22): both games draw "DOWN <s>: <name>'s last
  villager" over it; the guest's own villager down: a feed line and hold G to give up. No world save while one is down
  (the save blocker). `coop.tribe rescue [giveup | wipe <seat>]` (status, tests).
Not ported: the hut's "village down" window, the capture of an unrevived village, the party wipe from the population
event with every player village empty (Lineages BUG-9147), the red lost step on the bar (progress_debt).

## Verification
no-fail-run: a new feature (on the base, feat/lineages-tribe, `coop.tribe rescue` is unknown and the last villager dies;
with `coop.fix tribe.founder_rescue off` it dies too: seen in the run below). Not run in the Linux lab (no lab here).
Manual run, lane B on Windows 11 (Steam build), instances 3 (host, dev3) and 4 (guest, dev4), the Tribe fixture
civ/fixture_tribe (Boheus.spo with the guest's own village, seat 1), 2026-11-20:
* the guest's two other villagers killed by test blows (`coop.tribe pincer`), the lethal blow on the last one: host hp
  1.0, dead 0, window 60 s; the guest's game: its line for seat 1 and the mirror alive at 1; "DOWN 57s: Bob's last
  villager" over it on both games (screenshots); a world save refused while down; a further blow not dealt.
* a host villager walked there by the game's walk order: revived 21 s later at 12 m, 60 / 150 health on both games,
  "Player revived Bob's last villager." on both.
* a real alliance of the guest's village (team alliance): 1 win, bar 1.0 on both games; downed again and the guest gave
  up: "Bob gave up. Party wipe: ... its newest ally (000022A6) is a rival again ...": 0 wins, bar 0.0 on both games,
  relation -5 with both villages, the guest's game showed "The Cyan Village dislikes your tribe."; the world refresh:
  the guest loaded the new world, the host reloaded, the state kept.
* the host's own last villager down; the guest selected its villager and walked it there with its own orders: revived
  ("Bob revived Player's last villager."); the host's walk order on its downed villager left it where it was.
* the host's last villager down and nobody came: the wipe after 61 s with no win: food 11.7 -> 9.7 and 14.9 -> 12.9,
  the villager up at its village.
* switches (see the second run in the History): party_wipe off: the setback at home; founder_rescue off: the game's death.

## History
- 2026-11-20: opened; implemented on feat/lineages-rescue (ported from Lineages' tribe.cpp).
