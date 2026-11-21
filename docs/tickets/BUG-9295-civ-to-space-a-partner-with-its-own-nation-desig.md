---
id: BUG-9295
title: Civ to Space: a partner with its own nation designs its own spaceship in its own History editor and launches with it
status: open
kind: feature
stage: Civ, Space
reported: 2026-11-21
source: owner (separate lives batch)
role: guest
versions: 0.7.15.2
fixed_in:
branch: feat/lineages-space-guest-launch
commits: 3a0c12d3 e162f732
tests: by hand on lane C with the batch driver agentC4/t1.py (not in the repo; no lab test yet)
signature:
docs: docs/re/civ.md#civ-guest-launch, docs/re/space.md#space-own-ship-captain
related: BUG-9279, BUG-9274
---

## Report
The owner wants every player's own life in the same world (separate lives batch). At the Civ -> Space change only the
host designed a spaceship: after the host's History check and the spaceship editor vote, a guest that plays its own
nation (BUG-9274) clicked its own History check and nothing happened (the History just closed); it then got the host's
Space world with the host's ship (or a ship made earlier in the galaxy menu's Spaceship Creator).

## Root cause
Observed at runtime (lane C, Kealeneon fixture, docs/re/civ.md#civ-guest-launch): the guest's game never sees the
planet won. The team campaign (civ_nations.cpp campaign_task) raises the nations' levels and calls
GameCiv_ShowPlanetWon + GameCiv_PlanetWonPause on the host's game only; on the guest the player civilization stays at
level 3 and its History check only closes the History (no EditorRequest::Submit). Level 4 alone is not enough; level 4
+ ShowPlanetWon + PlanetWonPause on the guest's game makes its History check submit the game's own spaceship editor
request (0x96B24187), and the editor's Done launches the guest's own game into Space with its ship.
A second problem appears then: the host's Space world is shared when the host leaves its home planet; without a hold the
guest, still on its own home planet after its own launch, is taken out to load it at once.

## Fix
New civ_launch.{h,cpp} (the idea of Lineages PR #196's per-guest launch, BUG-9071, written new for 0.7.15):
* session_glue.cpp: when the spaceship editor vote ends on a guest with its own nation, civ::launch::on_vote_end arms
  the own launch; outside the frame its game gets level 4 (cCivilization_RaiseLevel) and the planet-won state
  (GameCiv_ShowPlanetWon + GameCiv_PlanetWonPause), and a feed line tells the player to use its History check.
* world.cpp: guest_tick runs civ::launch::tick; via_menu_tick and deferred_tick hold a received world while the own
  launch runs (Civ after the vote: 10 min at most; its editor; Space on its home planet, context 0). It ends when the
  guest's ship leaves the planet (context 1 / 2), after 30 min, at the menu, in another world, or at the session end.
  space_empires.cpp then flies the newest design of the guest's library (the ship it has just made).
* `coop.civ launch` prints the state (tests). No new symbol (all already in overrides_dc04aee5.sym).
* Switch: `coop.fix civ.guest_launch off` (the old behaviour: the guest's History does nothing, it gets the host's
  world at once).

## Verification
By hand on Windows lane C (two instances of the Steam exe 3010a413, the GOG 2024 code), driver
`agentC4/t1.py` (the batch folder, not in the repo): the Kealeneon Civ fixture, the
guest's adopted nation 000021D0, every other city captured, the host's History check -> the spaceship vote. Not a lab
run (no out/lab id): the Linux lab has no Civ fixture with a guest nation yet.
* PASS (switch on, 2026-11-21 09:02-09:10, logs t1_on2.log, t1_on2_host.log, t1_on2_guest.log, screenshots
  t1_on_*.png): the guest's game shows the planet won (level 3 -> 4); its History check opens its own spaceship editor;
  it builds 'Bob2521' (292B761E) and its own game launches into Space ("Lift off!" on its home planet); the host's
  world arrives (09:08:13) and waits while the guest stays on its planet (phase space, 494 held frames); the guest
  leaves the planet, the hold ends (09:09:42, "my ship left its home planet"), the host's world loads and the guest's
  empire flies 292B761E; on the HOST's game the guest's ship is "player 1 ufo ... 'Bob2521'" and its ship labelled
  "Bob" flies in the host's solar system (t1_on_host_end.png).
* FAIL (`coop.fix civ.guest_launch off` on the guest, 09:11-09:17, logs t1_off.log, t1_off_host.log,
  t1_off_guest.log): the guest's History check does nothing (no editor); the host's world is loaded at once (no hold);
  the guest's empire flies the newest ship of its library from before ('Bob2521'), not a ship of this launch, and the
  host sees that one ('Bob3054' was never made).
No crash report in either run (the 09:10 report is the forced close of the ON run's games).

## History
- 2026-11-21: opened.
- 2026-11-21: built (civ_launch.cpp), PASS on / FAIL off on lane C.
