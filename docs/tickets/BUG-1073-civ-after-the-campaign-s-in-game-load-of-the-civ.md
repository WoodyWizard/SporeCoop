---
id: BUG-1073
title: Civ: after the campaign's in-game load of the civ_npc fixture from Tribe, cCity::Capture with its cinematic never gives the city (Civ end stuck)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-camp
commits: 2623e169
tests: cine_load, campaign_civ_jump, campaign (the jump path)
signature:
docs: docs/re/civ.md#civ-cinematics-enabled, docs/06-testing.md#testing-w49-camp
related: BUG-0311, BUG-0982, BUG-1055, BUG-0332, BUG-1079
---

## Report
Found by fix/w49-tests (Q1) in two 4-player campaigns on rel/0.7.14 + fix/w49-tests (lab 20261113-060756 lane 4, the
full campaign; 20261113-060758 lane 5): the Tribe end did not come in 20 min, the host jumped (`coop.load` of the
civ_npc fixture in game, slot 10, the guests followed), Civ was played 3-8 min, then the Civ end's shortcut
`cCity::Capture(city, player pol, 0, 0 /* not instant */, 0, 0)` ran every 30 s for 20 min: the mod logged
"Alice captured a city" (lane 4) / "Carol captured a city" (lane 5) each time (h_Capture after o_Capture), but no city
changed hands (`coop.civ`: still pol=000021CB ...), no cinematic, `busy: -`: "the stage change civ -> space did not
finish in 20 min" (stuck). The same call with instant=1 on the stuck host took the city at once (lane 4, by hand).
In 0.7.13's final check the same jump path captured all 9 cities (out/lab/20261112-175447).
Not reproduced on a fresh world: capprobe (civ_npc, 2 games, the same call) took 2 of 2 cities after 8 s on
rel/0.7.13 and on this branch; campaign_space (4 players, civ fixture, 2 min play, no jump) captured all
(lab 20261113-072523).
Seen again in the full campaigns lab 20261113-082316 and 20261113-101808 (lane 4, both after the civ_npc jump):
every capture logged, no city taken, 20 min, stuck (four runs of four with the jump).
Reproduce: `coop_campaign.py --from tribe --to space` with a Tribe end that does not finish (CAMPAIGN_OLD_TRIBE=1),
so the host jumps to civ_npc; watch "cCity::Capture of 1 city ... (9 of other nations listed)" repeat.

## Root cause
A mod bug a player can hit, not a test artefact. Observed (runtime, a watcher on the host's cinematic manager
`[0x167EB50]` +0x15F / +0x2C / +0x148 every 5 s, and Ghidra; docs/re/civ.md#civ-cinematics-enabled):
- `PlayCinematic` plays nothing while the manager's enabled byte +0x15F is 0. The live Creature -> Tribe change sets
  it to 0 right after CRG2TRG (cCinematicManager_SetEnabled(0) at 0xD3FFD2); the Tribe start sets it to 1 again.
- The mod reloads the host's Tribe 6-7 s after that change (tribe.host_reload, BUG-0332: Tribe -> galaxy menu ->
  Loading -> Tribe). In campaign_tribe_end lab 20261113-133534 (lane 5) the reload came before the Tribe start's
  SetEnabled(1): the flag was 0 from 13:40:52 (CRG2TRG) on, in the galaxy menu and after the load (13:42:24), for the
  whole Tribe (0 of 5 villages counted in 30 min), and after the jump (`coop.load` civ_npc in game: galaxy menu, load)
  still 0 in Civ. A load never sets it; the galaxy menu's enter did on another run (lane 4, 13:38:02) but not here.
- In Civ, `cCity::Capture(city, pol, type, instant 0, ...)` sets the city's pending flags and leaves the ChangeOwner
  to the end of CVG_Capture; with no cinematic the city never changes hands. instant=1 (a surrender) takes it at once:
  the ticket's observation. On a fresh game (boot + load) the flag is 1: capprobe and campaign_space captured.
- The four jump campaigns of fix/w49-tests had no cinematic on the host after the Creature -> Tribe change
  (`busy=cinematic` in the host's trace lines stops at 10:39 in lab 20261113-101808; 0.7.13's final check,
  20261112-175447, had them in Tribe and Civ): the same race. The campaign's jump only made it visible; a player hits it
  in every co-op Creature -> Tribe change where the reload wins the race: no Tribe cinematics on the host, and after a
  later load of a Civ save in that game, sieges, conversions and purchases that never take the city. Space after
  such a load: a destroyed ship without its death cinematic (spacecoop's forced respawn, BUG-1079).
Observed in the same run: with the flag 0 in Tribe the host's own raid click never came (`no member selected` 32 times
in 30 min; the box drag around its members found none on screen), the hut never fell, the goal bar stayed 0/5; in the
two runs before it (flag 1, lab 20261113-125218 and -125221) every host click came and Tribe ended by the game.
Inferred: the view moves of a click on the minimap or on a member's card play through the cinematic manager, so with
it off the view stays where it is (the full campaigns' "minimap calibration: o == a == b" lines, 101808): the bots and
a player can not jump the view; this made the Tribe end on the campaign's own planet unreliable in Q1.

## Fix
gamefixes `game.cinematics_on_load` (BUG-1073): when a load ends in Cell / Creature / Tribe / Civ / Space with the
flag 0, the mod calls the game's own `cCinematicManager_SetEnabled(1)` (0xADF310, a byte write when 1) and logs
"the cinematics were off after the load into mode ...". A save loaded into a stage starts with cinematics on, as after
the game's start. This also covers the host's Tribe reload itself (Loading -> Tribe) and a guest's load of the host's
world. `coop.cine [on|off]` shows the flag and the restores (tests set it as the game's stage changes do).
`coop.fix game.cinematics_on_load off`: the load keeps it as found (0.7.13).

## Verification
Test `cine_load` (tools/tests/coop_cine_load.py, tribe fixture, 2 games, ~2 min): the host's cinematics off as the
Creature -> Tribe change leaves them, the host's `coop.load` of civ_npc in game, then a Capture with its cinematic:
the cinematic must play and the city must be the player civilization's in the host's and the guest's game.
- FAIL with SPORE_FIX_OFF=game.cinematics_on_load: lab 20261113-141811 (lane 4): flag 0 after the load, no cinematic,
  the city not taken on either game (the ticket's symptom).
- PASS with the fix: lab 20261113-142022 (lane 4): restored_on_load=1, the cinematic played, the city taken on both.
- The natural path without the fix: campaign_tribe_end 20261113-133534 (lane 5, the build before the fix): the flag 0
  from CRG2TRG through the reload and the whole Tribe (no host click, 0/5 in 30 min), then the jump; the run ends at Civ
  (no capture in that test).
- With the fix, the natural path: the full campaign 20261113-142203 (lane 4): "the cinematics were off after the load
  into mode 01654C02: on again" at the host's Tribe reload (14:43:21), the Tribe timeout at 3/5 made the campaign jump
  to civ_npc, all 9 captures played their cinematics and took their cities, Civ -> Space by the game, the finale
  complete. campaign_tribe_end 20261113-142311 and -153633: the restore fired and Tribe ended by the game.
- The full campaign 20261113-160426 (lane 4): the restore fired at the Tribe reload (16:25:54), Tribe -> Civ -> Space
  by the game (9 captures with their cinematics), no jump, campaign RESULT: OK (the lab failed it on BUG-1089).

## History
- 2026-11-13: opened.
- 2026-11-13: root cause found and fixed on fix/w49-camp (game.cinematics_on_load), test cine_load.
