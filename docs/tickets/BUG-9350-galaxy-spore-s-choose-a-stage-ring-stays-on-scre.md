---
id: BUG-9350
title: Galaxy: Spore's Choose A Stage ring stays on screen into the planet when a join or load starts while it is open
status: fixed
kind: bug
stage: civ
reported: 2026-11-23
source: QA (MSI)
role: guest
versions: 0.7.14.1-lineages69-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: ee08d149
tests: tools/qa_win (manual, 2026-11-23): the MSI's game, TITAN's galaxy server; coop.gmenu play Abber, then coop.galaxy enter Abber|Civ; coop.fix galaxy.stage_screen_close off/on
signature: SporeApp.exe+0xC5E4D hash_map__int_int__Find+0xD (the X of the stuck screen in Civ; NewGameFlow_OnButton Back, edi 0x595E0A8)
docs: docs/re/space.md#space-stage-screen-outside-menu
related: BUG-9337, BUG-9335
---

## Report
Cross-PC QA (2026-11-23, TITAN's galaxy server, two test games on the MSI). A game that had joined a Space era's host
(the guest's "Civilization victory" ship-design path) went back with `coop.menu` (still the host's guest in the galaxy
menu) and pressed Play on the same planet: Spore's Choose A Stage ring opened (log "Spore's Choose A Stage screen on
'Abber': Space, Civ (BUG-9337)"), and at once the guest's rejoin card ("Joining MSISpace, MSISpace is saving the
world...") came over it. The card ended by its 180 s time limit. `coop.galaxy leave` and `coop.galaxy enter Abber|Civ`
then loaded Abber's Civ era from the menu: the ring (paw, fire, globe, rocket and its X) stayed drawn in the middle of
the Civ game (images/bug9350_stage_ring_over_civ.png), while `coop.gmenu status` said "closed 1, open now 0". A click on
its X closed it.

## Root cause
Inferred from galaxy_menu.cpp, then seen: tick() marks the screen closed when the game leaves the menu but does not
close Spore's screen; only a stage press (h_FlowButton: the flow's Back), the player's Back or Esc closes it. A load
that starts while the screen is open (a rejoin, a party move, a console enter) leaves the menu with the screen's
window still up, and the planet draws it (the menu's state stays 4: log "counted closed: mode 01654C08, menu state
4" as the load began).
Worse: its X in the planet crashed the game once (2026-11-23 12:25, the MSI, switch off: access violation in
hash_map__int_int__Find+0xD, stack NewGameFlow_OnButton -> 0xDF0640 (the page close) -> GGE_PlayStep code; edi the
Back id 0x595E0A8). The first time the X was clicked in Civ (the report above) it closed without a crash. So the
screen's Back is not safe outside the menu: the fix closes it before the load, never after.

## Fix
galaxy_menu.cpp stage_screen_holds_load(), called by world.cpp's load from the galaxy menu (via_menu_tick, after the
menu settle): while the menu shows the new-game screen (state 4, ours or an empty star's), the load waits and the
screen's Back is pressed outside the frame (o_FlowButton(flow, Back), the path a stage press uses); the load runs
once the menu has left state 4. If the screen is still open 5 s on, the load runs as before (logged). Switch `coop.fix
galaxy.stage_screen_close off`: the old behaviour. `coop.gmenu status`: "closed before a load (BUG-9350, on): N,
timeouts N". A first try that pressed Back after the menu was left was dropped before commit (the crash above).

## Verification
The MSI (one game, profile MSIGuest, 1280x960, 10 fps), TITAN's galaxy server on a fresh copy of the test galaxy
(Abber with Civ and Space): `coop.gmenu forget Abber`, `coop.gmenu play Abber` (the ring opens:
images/bug9350_menu_ring.png), then `coop.galaxy enter "Abber|Civ"` (the mod's load from the menu).
- FAIL (`coop.fix galaxy.stage_screen_close off`, the same build): Abber's Civ game with the ring in the middle of the
  city (images/bug9350_off_civ.png); also on the build before (the report).
- PASS (on): log "Choose A Stage closed before a load from the menu (BUG-9350)" 38 ms before "world: loading from the
  galaxy menu"; Abber's Civ game without the ring (images/bug9350_on_civ.png).
- No crash in either run (the one crash file is the X click of 12:25 above).
no-fail-run: Windows QA by hand (no lab lane on Windows); the runs above are the switch off and on.

## History
- 2026-11-23: opened (seen on the MSI in a cross-PC run).
- 2026-11-23: fixed on feat/lineages-alpha (ee08d149): closed before a load from the menu; seen off and on on the MSI.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
