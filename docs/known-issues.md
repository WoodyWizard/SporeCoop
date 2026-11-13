# Known issues

Bugs found and not fixed yet. Each entry: what happens, how to reproduce it, what is known.
Fixed entries move to the commit message (and a test).

A new entry (and every branch that stops without a fix leaves one) uses this shape, with an
anchor so other docs can link to it:

```
## <Stage>: <what the player sees> (<date>, <source: player report / test / branch>)
<a id="issue-<slug>"></a>

* What happens: ...
* Reproduce: ... (test, fixture, steps)
* Known: ... (observed vs inferred, addresses, links to docs/re anchors)
* Not checked: ...
* Next steps: ... (branch, if any)
* Ticket: BUG-NNNN (tools/tickets.py new)
```

## Session: cleanup differs between leave, disconnection and unload (2026-10-27, code review)
<a id="issue-session-cleanup-paths"></a>

* [BUG-0033](tickets/BUG-0033-leaving-re-hosting-and-hot-reload-each-clean-up.md): fixed
  (unreleased) for the stage modules: every way a session ends now runs the same cleanup. Still
  open: `g_host_away` is not reset on any path. The vote (BUG-0043)
  and the world share queue (BUG-0140) are reset by the same cleanup since fix/w1-core; the
  `shared` resources (BUG-0049) and `g_roles` (BUG-0064) since fix/w2-civ.
* CORRECTION (2026-10-28, fix/w5-tools): `g_host_away` is reset at every session end since
  BUG-0085 (0.7.3.0). Since BUG-0214 the core resets run on every path also with `coop.sessionfix
  0` (`end_session` in coop.cpp, docs/06-testing.md#testing-fix-rules): only the module calls of
  before BUG-0033 depend on that switch.

## Creature: a guest that gets the world right after the host's editor stays a hatchling

* After an evolution that starts a migration, the host's game plays CRG_SpeciesMigrate; the world
  the host sends then is saved during it, with the host's creature still a hatchling (+0xB34 age 0)
  and the migration at state 1. The guest's game loads that and its creature stays at age 0: its
  game never sets the goal "Migrate to a new nest" (needs age 1) and never claims a nest (seen 6
  times on lane 6, 2026-10-25, `coop_migration.py` steps 1-2; a save made after the host's caption
  has age 1 and state 2). Not looked into: whether the hatchling grows later (another editor, a
  load) and what the guest sees (size). docs/re/creature.md 16.
* While a cinematic plays the creatures the mod removes on a guest (its own game's NPCs, proxies,
  puppets) stay where they are until it is over (the fix for the guest crash at 0xC6AE60): they
  stand still, a proxy the host replaced in that time can be seen twice for the cinematic.
  ~ superseded (2026-10-27): see BUG-0066 (#44).
* CORRECTION (2026-10-27, claude/clever-fermi-h57l81): such a creature does not simply stand still: `game.cpp`
  `destroy_creature` unregisters the puppet or proxy before the cinematic deferral, so its
  NPCTickAI runs again and it can move, and the host may send it to the guests as an NPC (code
  reading, verified by a second reviewer; not seen in the game yet). BUG-0066.
* FIXED (2026-10-28, fix/w2-creature): seen in the game (`coop_cinematic_puppet.py`: the host's old
  puppet got NPC id 23 during CRG_ClaimNest); a puppet or proxy that waits for the end of the
  cinematic now stays a puppet (no AI, no host NPC id) until it is destroyed. Local NPCs the guest
  removes in that time still run their AI until then. BUG-0066.
* FIXED (2026-10-28, fix/w3-creature): the host's world after an evolution now waits until its
  cinematic is over (the caption confirmed, at most 120 s): the guest's creature grows up and its
  game migrates as the host's (`coop_evolve_world.py`). BUG-0019.
* Ticket: BUG-0019.

## Guest: fault in the tribe setup with a null simulator object (seen once)

* `exception 0xC0000005 at 0x00B12D32` (data 0x2B4) on a guest of a 4-player tribe churn
  (`coop_multi.py`, 2026-10-23): the tribe setup 0xCD78E0, one step after a load, calls 0xB12CE0 on
  `[0x167FAAC]` (getter 0xB3D350), which was null (it is null in the galaxy menu). Not reproduced on
  2 players (`coop_rejoin_loop.py` plain, `--race`, `--reshare`, `--menu`: 0 of about 250 rounds).
  Since 2026-10-23 a load leaves a stage only when it has been playable for 3 s (`world.cpp`
  `kStageSettle`), which should avoid a quit during the setup; if it comes back, the fault log now
  shows the call chain (`exc`: "calls:"). docs/re/space.md 8.3.
* 2026-10-23: it came back once more (4-player tribe, `coop_multi.py`). Since then `gamefixes.cpp`
  skips `SimCamera_LookAt` and `SimCamera_Update` while the camera object is null (`coop.status`:
  "tribe camera skipped N"); the stage setup goes on without pointing that camera.
* 2026-10-27 (fix/w1-tribe-civ): closed as fixed by that guard (commit d605e96): the null object
  is the only cause seen, and the guard is on in every tribe run (`game fixes: tribe camera guard
  on`, GOG 2024). Not seen since in the lab (tribe_play, chaos_tribe, tribe tests).
* FIXED, cause found (2026-11-07, fix/w46-n1-crashes): the galaxy menu's intro timer fires in the first frames of the
  guest's Tribe stage (a join ~12.5 s after its menu came up) and nulls the camera before the setup's LookAt;
  reproduced with the guard off (`tribe.camera_guard`, test tribe_camera_null), and the intro no longer starts
  outside the menu since 0.7.9 (`world.intro_at_once`). docs/re/tribe.md#tribe-camera-null.
* Ticket: BUG-0167.

## Creature: NPC aggression towards a partner is untested end to end

* Since 2026-10-25 an NPC on the host that is nearer to a partner's creature ticks its AI with that
  creature as the player (docs/re/creature.md 14.2): it notices the partner (checked: the "noticed"
  stimulus names the puppet, `coop_creature_kills.py`), and retaliation and epics hurt the partner
  (they did before too). That an NPC of an aggressive archetype then attacks the partner on sight
  is not checked: no such species in the `creature` fixture attacked even the host's avatar on
  sight, and spawned epics (`coop.npc epic`) do not attack the avatar.
* To check it: a save with an aggressive (red face) nest nearby, the guest next to it and the host
  60 m away: `damage_in` on the guest should grow. Side effects to watch: avatar-only UI of the
  host (hints, music) while an NPC hunts the partner.
* Ticket: BUG-0168.

## Creature: puppets still go into saves (removed after a load)

* Mostly fixed since 2026-10-25 (docs/re/creature.md 12): a puppet reuses the herd of its species
  (no new herd per spawn), and every creature without tuning in a herd like ours that is not ours
  is removed once a second in the creature stage (`strays_removed=` in `coop.npc`), so a puppet a
  save brought back (the 0xD48634 crash: "these guys appeared after I loaded the save and crashed
  my game after I killed them") is gone right after the load, also from saves made before.
* Fixed 2026-10-28 (fix/w3-cell, docs/re/creature.md#creature-save-strip): our herds and the
  creatures in them are written as null references into saves (`coop.savestrip`), so a new save
  holds no puppet and no herd of ours. Older saves keep what they have (the removal above handles
  their creatures). Still saved: a puppet in the host's avatar herd (next point) and, in a guest's
  own save during a session, the proxies in the game's nest herds (a null member in a game herd was
  not tried).
* ? A guest without an own creature has its puppet in the host's avatar herd (host species, with
  tuning): a save probably keeps it as an extra member of the host's family, which the removal
  does not touch (not checked).
* Ticket: BUG-0020.

## Creature: one NPC 4 m off on every guest for 30+ s (seen once)

* `out/join_desync.py`-style probe, 4 players, 2026-10-23 (after the menu-load fix): NPC n11 stayed
  4 m from the host's on Bob, Carol and Dave alike from 8 s to the end (36 s); the 19 others < 1 m.
  The same offset on every guest points at the host side or at a spot the proxy cannot stand on
  (pushed out by collision after each stuck-teleport of `drive::step`). Not seen again in 5 min of
  watching nor in 4 more sessions. Hunt: `coop.npc list` on host and guests for that id (err, want).
* 2026-10-28 (fix/w2-creature): not reproduced (no runs of the 4-player probe). Code reading of
  `drive::step` (driver.h) gives a candidate (inferred, not observed): a proxy 4 m off is under the
  8 m snap, and the stuck teleport fires only while the host's NPC reports less than 0.3 m/s. An NPC
  of the host that pushes against something (velocity reported, position fixed) keeps the proxy
  walking towards a spot it cannot reach on any guest (collision), for as long as it pushes. A
  second candidate was fixed on the way: a reused creature object kept its net id (BUG-0044), but
  that shows the old species or a corpse, not an offset. Next time: `coop.npc list` on the host
  for that id (its vel: `coop.npc` sync entries) and on a guest (err, want).
* FIXED (2026-10-28, fix/w3-creature): the candidate cause: an NPC proxy that stays more than 2.5 m
  off for 3 s without getting closer is now teleported whatever speed the host's NPC reports
  (`drive_plan.h`, unit test in `make test`). Not reproduced in the game; if an offset like this is
  seen again, `coop.npc list` on a guest shows whether the proxy's teleports grow. BUG-0169.
* Ticket: BUG-0169.

## Creature chaos: a guest crashed with a corrupted heap (seen once)

* `coop_multi.py --fixture creature --players 4 --minutes 6`, seed 1035603402, lanes 1+8, 2026-10-23
  (branch fix/npc-lag): Bob (lane A guest, never rejoined) got a first-chance AV at 0x009ABB5C
  (`test byte [eax],1` after `call 0x9AC1B0`, eax 0xC8124380) 0.1 s after "Carol pings a place",
  then an AV in `GeneralAllocator_FreeInternal`+0xAD 3 s later, and froze in `MallocInternal` (watchdog:
  no frames). Looks like heap corruption from earlier; the bot's last actions on Bob were clicks,
  wheel and `coop.targets`. Not seen in 4 other runs of the same test. Replay with the seed above.
* 2026-10-27 (fix/w1-creature, lane 4, GOG 2024): not reproduced in 2 runs of `coop_chaos.py
  --fixture creature --minutes 6` (2 players; seed 417787344 on main 88341cf and seed 71370752
  with the BUG-0082/0091 fixes; 904 + 839 actions, 4 + 8 rejoins, 0 faults on both games; lab run
  20261027-183439). The 4-player replay of seed 1035603402 needs two
  lanes and was not run. Two freed-pointer paths that fit "heap corruption from earlier" were fixed
  on the way: BUG-0091 (15 animations played on freed puppets in 6 rounds of a guest's world
  teardown) and BUG-0082 (the guest's kill list kept across the host's absence).
* 2026-11-08 (fix/w46-n1-crashes, 0.7.11 N1): the 4-player replay of seed 1035603402 run 6 times on rel/0.7.11 (also
  at 20 fps over the players' networks, and with `core.frame` / `npc.kill` off): no fault on any game. Closed as
  not-reproduced (multi4_creature_seed, multi4_creature_slow).
* Ticket: BUG-0170.

## Tribe: the host froze once while the epic creature was being killed

* `coop_tribe_play.py` on branch feat/net-window, lane 2 (2026-10-22): the host stopped drawing
  during `coop.tribe epic hit` (monitor: FROZE); not reproduced in 14 more runs. The game logs of
  that run were lost (now kept by lab.py). Hunt: `lab.py run tribe_play,chaos_tribe --repeat 10`.
* 2026-10-27 (fix/w1-tribe-civ): not reproduced on main in 4 tribe_play + 4 chaos_tribe runs (lab
  run 20261027-183142, GOG 2024). Hardened on the way: the epic is checked against the game's object
  list, `epic hit` stops at a dead victim, the epic state ends with the session and the world.
  If it comes back: the game logs are kept by lab.py now.
* 2026-11-07 (fix/w46-n1-crashes, 0.7.11 N1): the candidate (the test loop's blows on the epic at hp 0) tried on
  purpose with the old command (`tribe.hit_dead` off, tribe_epic_dying, also at 20 fps over the player network): no
  freeze; the 2026-10-22 lane 2 was a GOG 2016 lane. Closed as not-reproduced.
* Ticket: BUG-0171.

## Tribe: host fault and freeze when a member is added during the game-over caption

* When the player tribe dies out the game shows "Alas! Your population has fallen below the
  minimum needed to survive ... Bounce back in time" and waits for OK (the bounce back reloads the
  last save; with co-op the guest follows, checked 3x in play_tribe work, 2026-10-23). If a member
  is added to the player tribe while that caption is up, pressing OK faults at **0x00C0DAE3**
  (in 0xC0DAD0, a creature method reading `this+0xB54` and the species `this+0xB20`, both null for
  the new member; callers 0xC09CAE in cCreatureAnimal::Create, 0xC0AB99, 0xC0E74D, 0xC1CEAB,
  0xD410F8) and the host stops drawing for good (2 of 2 tries).
* Reproduce (lane N): host kills its own members through the damage path
  (`coop.tribe pincer <own id> <enemy id> 300`, a few times each, until only the knocked-out
  chieftain is left and the caption shows), then `coop.tribe spawn 2`, then `key enter` on the
  host. Without the spawn the bounce back works (frames go on, both machines have the same 3
  members afterwards).
* Only the test command adds members at that moment today; a guest adds mirrors (SpawnMember)
  whenever the host has members it lacks, so a guest that shows its own game-over caption while
  the host already bounced back could hit the same path (not seen: the guest showed no caption in
  4 tries, once it did while the host had none left alive).
* 2026-10-27 (fix/w1-tribe-civ): not reproduced on GOG 2024: caption up, `coop.tribe spawn 2`, OK,
  4 of 4 tries without a fault, the bounce back worked (`coop_tribe_gameover.py --nofix`). The
  2026-10-23 tries were probably on the GOG 2016 lanes. The mod now adds no member to a player tribe
  without health (`coop.tribe fix gameover 0`: the old way).
* 2026-11-07 (fix/w46-n1-crashes, 0.7.11 N1): not reproduced on GOG 2024 with the guard off either (the caption, 6
  members at 20 fps over the player network, spawns 0.5 s and 2.5 s into the bounce back's reload; lab
  20261107-231709). 0xC0DAE3 is the GOG 2016 address of GOG 2024 0xC0DB23 (SetMoveScale+0x13, the BUG-0285 site);
  no GOG 2024 lab log of 2026-11-04..07 has a fault there. Closed as not-reproduced.
* Ticket: BUG-0172.

## Cell: a guest far from the host swims in an empty pool

* The host simulates and sends only the cells around its own cell (its active tiles, docs/re/cell.md
  5; `host_tick` sends cells within 120 m of the host's avatar), the guest spawns none of its own. A
  guest more than ~100 m away meets no NPCs and no food until it comes back (play_cell seed 9,
  2026-10-23: partner 211 m away at the end, the guest's 538 local cells were all around the host;
  the pair was over 60 m apart half the session). Not unfair in itself (no NPC there to hunt either
  player), but the guest eats less there. A fix needs the host to populate and keep tiles around
  each partner (PopulateTiles 0xE7D370 around `G+0x4128` is the host's view only).
* Reproduce: `SPORE_LANE=N python3 tools/tests/play_cell.py --minutes 10 --seed 9` and watch
  `partner` distances and the guest's `local_gameplay_cells` vs where its cell is.
* Ticket: BUG-0174.
* CORRECTION (2026-10-29, fix/w7-eat): the empty pool starts much nearer: the host's game keeps only
  about one screen around its own cell populated (PopulateTiles, docs/re/cell.md#cell-new-players-food).
  A guest swimming away from the host (level 2) saw 41-51 food cells on its screen up to 42 m and none
  from 44 m on; right after a new game's start none from 23 m on. Most likely what two new players reported on Discord ("even at stage 1 only 1 of us
  could eat", BUG-0279): the guest, exploring on its own, finds no food at all while the host does.
  Measure: `SPORE_LANE=N python3 tools/tests/coop_cell_new_game.py --diet carn --minutes 1 --apart 70`
  (its last check fails while this is open).
* FIXED (2026-10-30, fix/w9-cell-food, unreleased): the host populates and keeps the tiles around
  every partner (`coop.fix cell.partner_tiles`, docs/re/cell.md#cell-partner-tiles); test
  `cell_far_guest`. Left: a host level-up wipes and repopulates the areas around partners too.

## coop_sync.py: mirrors trail more at low frame rates, the GOG 2024 guest pauses after a load (the test failures were fixed 2026-10-24, fix/gog2024-sync)

* The full lab run 20261023-225702 (feat/space-07, lane 10, 9 lanes busy) failed `coop_sync` twice:
  cell/players facing p90 15.6°, tribe/citizens 2.39 / 2.13 m, civ/vehicles 4.4 / 6.0 m and 33.6°,
  space/ships 33 m (first run). It had never run on GOG 2024, and ran on the new fixtures.
* Cause 1 (GOG 2024, test timing): the 2024 guest keeps its game time paused (cGameTimeManager
  +0x48 = 3, black screen, ~23 fps) for ~7 s after its world load in the tribe stage, exactly the
  window the test measured in. Citizens do not get `Update` then (`coop.tribe` on a guest prints
  `ai_skipped=0/0`), so mirrors do not walk and follow the host by drive.h's per-frame 15 % nudge
  only: the error is speed x frame time / 0.15 (2.4 m at 5.5 m/s and 15 fps; the lab's load lowered
  the frame rate). Forcing 15 fps on both builds (`frame.limit 66`) reproduced it on 2024 (citizens
  2.7 m) and not on 2016 (0.6 m). No wrong symbol: once the game time runs the 2024 mirrors walk.
  Players do not see it (the screen is black then).
* Cause 2 (test, both builds): the facing checks counted the guest's designed lag. The cell host
  turns every 4th round and the civ fixture has a single vehicle that turns in place, so 1 of 8
  samples sat at 15-30° (raw p90 2016: 12-21.5°, 2024: 2.6-18°).
* Vehicles at a low frame rate trail by ~2 m on both builds (15 fps: 4.2 m on 2016 and 2024), which
  is where the lab's 4-6 m came from.
* Fix: coop_sync.py waits until neither game is paused and compares within a 0.15 s lag window
  (docs/06-testing.md 6.5 "Sync accuracy"). After: 3 of 3 passes on lane 10, 2 of 2 on lane 3.
* Not fixed: drive.h's nudge is per frame, so a mirror that cannot walk trails more at low frame
  rates; the 2024 guest's paused seconds after a load are not explained (planet still loading?).
* Ticket: BUG-0175.
* 2026-10-30 (fix/w13-net): drive::step's nudge is per second now (`coop.fix drive.nudge_dt`), the
  civ vehicle glide stays per frame (no measurable difference in the lab); the GOG 2024 guest's
  paused seconds after a load are BUG-0313 (below).

## Creature: the mating call did not lead to the editor in the bot

* Backspace (Mating Call) at the nest with DNA shows "Mate: Locate your Mate!" and a thought bubble,
  but clicking the family members around did not open the editor within 30 s. Not a confirmed bug:
  how a player answers the call is not explored yet (docs/re/creature.md 5). The evolution vote is
  covered by `coop_vote.py` through `coop.vote editor`. The Cell stage's mating call is driven for
  real since 2026-10-24 (`coop_editor_desync.py`, docs/re/cell.md 12), and the Creature stage's first
  editor through the stage switch, not the creature mating call.
* Ticket: BUG-0177.

## Editors: what is still open after the 2026-10-24 report (item 4)

* The Cell -> Creature switch itself has no vote (9.3 wants one): the host's click on the history's
  check starts the Creature stage at once and the guests follow; only the first creature editor waits
  for the vote.
  Done (2026-11-08, fix/w46-e1-features, BUG-0315): the check starts a vote now (`core.switch_vote`).
* A guest without own creations gets its own editor on the host's creature as it was before the
  host's editor (the legless one at the start of the Creature stage). If it saves an edit, that
  edited creature becomes its own while the host plays the legged one: by design (everybody evolves
  its own creature). Done without a change, cancel and Don't Save keep the world's species (the
  host's evolved one after the reload). Fixed (BUG-0278): the guest no longer ends up with the
  host's legless creature itself (0.7.3.0 pinned it in `choices.ini` after Replace, and Save New kept
  the host's key), and the co-op window's arrows lead back to "the world's own". Still open: that
  editor is the "edit a creation" one (save dialogs, a red cross), not the evolution editor the host
  gets. (BUG-0283, "it shows the host's new parts while it is open", was the test's own edit: its
  `--guest cancel` added legs and pressed the save dialog's Cancel, BUG-0317.)
* The guest's mating-call button needs a collected part (`S+0x69`): whether a guest touching a host's
  part token (a mirror) sets it was not checked; the test sets it by console (BUG-0316).
* The report's Creature case ("3 rejoins did not fix it") did not reproduce as such on lane 5 with
  0.7.2.1: the guest saw the host's legged creature after the editor, also after a rejoin. Fixed
  causes that can show a stale creature: a creation sent without its records (the guest then shows
  its own body for the host), a rejoin applying the same version again (writing its package failed,
  nothing was applied).
* Ticket: BUG-0178 (closed 2026-10-30: the vote for the switch is BUG-0315, the part token check BUG-0316).

## Creature (guest): the social game is simplified

* Since 2026-10-23 a guest befriends a mirrored NPC (click in the social stance, a social ability
  key; the host applies the native win, docs/re/creature.md 8.3), but the guest does not see the
  native social meter and the NPC's ability prompts (they belong to the NPC's behaviour tree, which
  runs only on the host): any social ability counts as the answer, one press per round (a key or,
  since BUG-0518, a click on the ability's button of the bar). The host's own
  game is native. The success message 0x60B4123 (CreatureStageEventHandler) is not posted for a
  guest's win. Reproduce: `tools/tests/coop_creature_social.py`.
* 2026-11-03 (fix/w41-creature-left): the meter and the win message are shown / posted on the guest now
  (BUG-0179, social.guest_meter); still open: the NPC's prompts and any social ability counting as the
  answer (BUG-0525, docs/re/creature.md#creature-guest-meter).
* Ticket: BUG-0179 (meter, message: fixed), BUG-0525 (prompts: open).

## Creature: while the host is in the main menu the guest's planet fills up slowly

* The host leaves to the main menu: the guests are told and play on alone (their own game runs
  their NPCs again, `coop.npc` host_away=1), but the game respawns the removed NPCs only a few at a
  time (`local_alive` 0-1 after 30 s in `coop_exit_menu.py`). Making the mirrors local NPCs
  instead failed (a mirror with AI leaves its herd and is destroyed, docs/re/creature.md 8.3).
* Ticket: BUG-0180.
* Fixed (2026-10-30, fix/w13-creature, `npc.away_locals`): the guest replaces the host's NPCs it
  showed last (in nest herds of its game) by creatures of its own game at the same places in the
  same nest herds (docs/re/creature.md#creature-proxy-guest-side). `coop_away_refill.py`: 0 own NPCs
  near the guest for 25 s without the fix, 11 of 11 at once with it.

## Space: no test save after the tutorial missions

* The `space` fixture starts at the first mission ("Fly to planet Vulpica", the wreck): the ship has
  no interstellar drive (tests add it with `coop.space tool add interstellardrive`), no
  terraforming tools, no colony besides home and no other empire has been met. `space_empire`
  (docs/06-testing.md 6.15) adds the drive, money, two colonies with spice and two contacts, made
  with test commands; it is still inside the tutorial (travel to stars that are not own stars is
  refused there), and has no raids, missions or trade. A save played through the tutorial would
  still be better.
* 2026-11-08 (fix/w46-g4b-coverage): FIXED as a fixture: `space_free` = `space_empire` + `cheat spaceCreate`
  (the tutorial lock mbMission201Finished = 1, docs/re/space.md#space-tutorial-lock-runtime) + the interstellar
  drive again; `coop_space_free.py` (suite `space_free`): galaxy clicks on unowned stars fly both ships there.
* Ticket: BUG-0181.

## Space (GOG 2024): zooming in does not land in the first minute after a load

* On lane 10 (GOG 2024, `space` fixture), right after `twoinst.start('space')` the ship of the host
  and of the guest flies to the home planet on a click but zooming in (wheel at the ship) does not
  land on it; the same zoom lands 50-80 s later. Also with the menu intro fix disabled (so not the
  intro of docs/re/space.md 7.9, which only lasts ~10 s); `movie=0` and the HUD answers meanwhile.
  Made round 1 of `coop_space_rescue.py` fail ("both on the home planet"); the test retries the first
  landing for 150 s now. Not seen on GOG 2016 lanes (rescue round 1 passed there); cause unknown
  (tutorial state of the fixture, planet LOD loading at ~66 fps?). Reproduce: SPORE_LANE=10, `start
  ('space')`, then `Player.land(home)` at once. (The guest "movie" that made the guest's clicks
  vanish on this fixture, described on feat/space-trade, is fixed: docs/re/space.md 7.9.)
* The extra zoom-ins of those retries leave the two players at different heights over the planet
  (a ship's height is its zoom, docs/re/space.md 7.10). That made the rescue fail on GOG 2024 (the
  3D distance never got under 60 m): fixed, the repair measures along the ground now, and the bot
  zooms to the partner's height. The landing delay itself is still not understood.
* 2026-10-30 (fix/w13-space): not reproduced. `coop_space_land_early.py` (both players click the
  home planet and zoom in right after `start('space')`, the game's own landing conditions printed by
  `coop.space landcheck`) landed 7-16 s after the load in 3 direct runs on lane 9 and 2 lab runs on
  lane 8 (GOG 2024). The conditions are in docs/re/space.md#space-seed-profile. A landing itself
  shows `paused 1` for about a second (the planet loads).
* Ticket: BUG-0182.

## Galaxy menu: a host that stopped answering right after "Co-op" during the intro (once)

* `coop_ui.py` on lane 9 (GOG 2024, branch fix/gog2024-tests, 2026-10-24): once in 12 runs of the
  final code the host's console timed out right after its click on "Co-op" during the menu intro
  (the mod posts Escape to skip the intro, docs/re/space.md 7.9); the game log was overwritten by
  the next test before it was read. The other 11 runs (8 of them in a row) passed. An earlier version
  that ended the intro with EndCinematic in the menu broke the menu twice (an access violation in
  `UTFWin::Window::SetFlag`, a game that stopped reacting): that one is gone. If it comes back, keep
  `out/lab/<run>/ui.host.game.log` and look for what the main thread did after "Co-op clicked
  during the menu intro".
* 2026-10-28 (fix/w1-core, lane 2): 3 of 3 more runs passed with the click during the intro: not
  reproduced.
* 2026-11-08 (fix/w46-n1-crashes, 0.7.11 N1): the Escape candidate is disproved (menu_escape: Escape with no intro and
  at its start, "Co-op" clicked 0-7 s into the menu's own intro: the frames go on, the window opens); coop_ui.py 3 of
  3 with the click during the intro. Closed as not-reproduced.
* Ticket: BUG-0184.
* 2026-10-30 (fix/w13-net, lane 10): 3 of 3 passed, but the intro was not playing at the click in
  any of them (path not exercised). Candidate (not verified): the posted Escape reaching the menu
  outside the intro, see the ticket.

## Space: an unvisited star's planets can still differ between the two games

* Each game generates the planets of a star the first time it is needed (a flight there, the jump);
  the generation draws from the game's global random generator, so host and guest made different
  planets for the same star (count and PlanetIDs, e.g. 4 and 6). Since fix/space-jump the generation of
  the generating thread draws from a private generator seeded by the star (docs/re/space.md 7.3): the
  same planets in 8 of 8 probe sessions and in most `coop_space_jump.py` runs, but once the host made 10
  planets of 0046101B and the guest 6 (lane 7, GOG 2016, 2026-10-23). Not found: which draw still
  differs (another generator object, `rand()`, or state such as star +0x0C = play minutes, set right
  before the generation, or the empire's state read by 0xC309F0). `coop_space_jump.py` prints "same" or
  "DIFFERENT planets" without failing. A sure fix: the host sends the planet records of a newly
  generated star (or the guest asks for them before entering).
* 2026-10-30 (fix/w13-space): FIXED. The draw that differed: a first load of the star's species
  profile inside RequirePlanetsForStar drew on the seeded generator (docs/re/space.md#space-seed-profile).
  Test `coop_space_seed_profile.py`. Left open: `GenerateRandomName` draws stay on the global
  generator, so planet names may still differ (not compared).
* Ticket: BUG-0185.

## Civilization: single vehicle sync outliers of 20-27 m

* `play_civ.py` sessions: p90 of the vehicle position error 0.9-1.5 m, but the maximum of ~2500
  samples was 22-27 m in three sessions (one sample each). Not traced yet: candidates are a guest
  mirror bound to a nearby local vehicle of the same kind right after a purchase (the 25 m bind
  radius in guest_state) or the velocity estimate around a capture caption pause. Reproduce:
  `SPORE_LANE=N python3 tools/tests/play_civ.py --minutes 10 --seed 9` (it prints the outliers):
  `v13 22.7 m host (-497, 27, 66) guest (-493, 45, 79)` three samples in a row, an NPC vehicle on
  the far side of the planet; not seen on player vehicles.
* 2026-10-28 (fix/w2-civ): seed 9 again on lane 7 (GOG 2024): n=2430, p90 0.34 m, max 17.4 m
  (`v2`, one sample). Both candidates are being removed by other fixes: the receive-time velocity
  estimate (BUG-0122, fix/w2-net) and the guest's own NPC AI vehicles that could be bound as a
  mirror (BUG-0188, fix/w2-civ: one of two such spawns was bound, not killed, in
  `coop_civ_ai_spawn.py --nofix`). Re-measure after both are merged.
  2026-10-28 (fix/w2-net): BUG-0122 removed one candidate, the velocity from the arrival time
  (one late packet put a mirror 22 units ahead at 100 u/s); the snapshots now carry the host's
  time. Not re-measured with play_civ.py yet.
* 2026-10-28 (fix/w2-civ), with both fixes merged: seed 9 gave p90 0.15 m, max 17.4 m, and the max
  is a standing vehicle (v2) with the same ~17 m offset at the same place in both seed 9 runs: a
  different cause, open (details in the ticket).
* Ticket: BUG-0186.

## Civilization: no building placement through the city planner in tests

* In `civ_npc` (a new game) the planner's house / factory / entertainment have no design yet; a
  click on a tile opens the building editor (co-op vote). The bots only open and close the planner;
  guest buildings are tested through `coop.civ build` (coop_civ.py). A fixture with designs (or a
  bot that makes one in the editor) is needed.
* 2026-11-08 (fix/w46-g4b-coverage): FIXED: `coop_civ_planner_build.py` (suite `civ_planner_build`) gives the
  player civ the NPC cities' house design (`coop.civ bdesign`), then the guest drags the house tile onto a plot
  with real mouse input; the host's game has the house, the money fell on both (docs/re/civ.md#civ-planner-palette-runtime).
* Ticket: BUG-0189.

## coop_sync.py space: the ships' p90 sits near its 10 m limit

* 2026-10-23, 4 runs: ships p90 7.4-10.6 m (one run over the limit), single samples up to 690 m.
  Ships fly fast and the test reads host and guest one after the other, so most of it is the time
  between the two reads plus the interpolation delay; the 690 m sample is the first sample after the start:
  the host's new ship still stands at its default spot (0, 0, 699) for that moment (play_space.py
  saw one 412 m sample in 2 h). The p90 of real play is
  1.2-8.3 m (docs/stability/space.md). Since then the test compares the guest with the host's path between
  its two reads (the guest is read in between); if it still fails, look at the samples, not at the limit.
* Ticket: BUG-0190.

## Space contracts: what is not done (space_contracts.cpp, 2026-10-23)

* The armada attacks a colony of the host only: its waves are the game's raids, and a raid needs a
  planet of the raided empire in the host's save; the guests' colonies (feature 1's roster,
  `space_map.h`) are not in it. The host's NPC ships are the shared ones (squadron.cpp), so with three
  or four players the host has to be one of the two ships on the colony.
* The colony's turrets fight the raid too (on the homeworld they shot down 20 plain raid ships in
  ~25 s); the armada's ships have double HP, but a well-defended colony may still win a wave nearly
  alone. A raid the pirates win fails the contract; a raid still running at a timeout goes on as an
  ordinary raid.
* "Jump there" to the armada's star lands a guest next to a planet the travel picks (an own colony
  first): the host's colony is not the guest's, so the guest may have to fly to it in the system
  view before "Land" takes it to the right planet.
* SpawnUFO pirates do not fight (docs/re/space.md 10.2): an armada anywhere else than on an own colony
  (a neutral star, a partner's colony) would need the UFO AI's target selection, not found.
* While the game's badge panel ("Reward ... 2 badge points" + OK, after a defended raid) is up, the
  co-op window's buttons (all F9 pages) get no clicks; Enter or its OK closes the panel. The panel is
  modal (only it gets the mouse): bringing the co-op window to the front every refresh did not help
  (tried 2026-10-23, lane 3) and would have covered the panel's OK button, so it was reverted.
* A partner who leaves the session fails a running contract when the crew gets smaller than two
  (no grace time for a rejoin); a rejoining player sees the current contract within 5 s and can
  accept a running one again. Not covered by the test: a partner leaving, a rejoin.
* Joint terraforming contracts (a planet to a T-score together) are not done: the fixture has no
  terraforming tools to test them with.
* The reward tool is chosen from the host's cargo (the first one of the list it lacks); a guest who
  has it already gets only the money.
* Ticket: BUG-0191.

## Space rivals mode: limits (space_rivalry.cpp, 2026-10-23)

* PvP damage is the raw damage of the shot: the shooter's damage-boost tool does not count (the base
  TakeDamage of the shooter's game is not called for the puppet, docs/re/space.md 11.2).
* Only hits of the own ship count: NPC ships and turrets of a player's game hit an enemy puppet only
  on the host (squadron relays NPC hits on puppets to their owner), not on a guest's game.
* A player who joins for the first time while rivals mode is on keeps the money of the host's save
  it loaded (it had no budget yet); a rejoining player gets its own budget back from the host.
* A guest that is not in space when the host switches the mode gets its share or merge when it is
  back in space; the merge counts its last reported budget.
* Spice gifts, tool gifts and the jump work between rivals as before (deliberate acts); mission
  rewards are shared between allies only; raid alerts reach everybody.
* Ticket: BUG-0192.

## Space tests on the new `space` fixture (Madas): two flaky runs seen (lane 6, 2026-10-23)

* `coop_space_rescue.py` round 1 failed once: the host had flown ~550 m off with 2.5 s of W and came
  back too late (repair 1.0 of 2.0 s when the 20 s ran out); round 2 then started 1330 m apart. Passed
  in the next run and on the base branch. The ship of the new fixture is faster: a shorter flight off
  would make it stable.
* `coop_space_death.py` failed once in the forced-jump step: after the OK the ship stayed dead for 47 s
  and the guard "respawn caption without a planet" did not fire (0), so the respawn caption had ended
  some other way; passed in the next run and on the base branch.
* 2026-11-04 (fix/w42-space-play): coop_space_rescue.py flies off in 0.3 s pulses until the repair distance is
  120 m (at most 2.5 s), not 2.5 s of W at once.
* Ticket: BUG-0193.

## Space diplomacy: what is not shared or may drift (space_diplomacy.cpp)

* **Names and species of NPC empires differ between the copies**: each copy picks them at the first
  contact (from that player's creations). The relation, contact and war/alliance state are shared
  by political id; notices name both ("Kirgle Empire (Alice: Judge Empire)"). Sharing names would
  need the species too (docs/re/space.md 12.3).
* Events the game raises on every copy by itself are not sent (personality / archetype at contact,
  the empire AI's be nice / be nasty, embassies, super power, avoided contact). Any other event
  between the player and an NPC empire is sent, also when the local AI caused it (for example an
  NPC destroying the player's buildings): if both copies' AI do the same, the host counts it twice.
  Guests converge on the host's table within 20 s; the host's own copy is the reference.
* An empire created after the join (uplift of a civilization, ...) gets a political id on each copy
  by itself (`NextPoliticalID`), the ids may differ: its events are dropped as "unknown empire"
  (`coop.space diplomacy` counter `unknown`). Not seen in tests.
* The comm screen is not driven by clicks in the test: `coop_space_diplomacy.py` calls the same
  game functions its buttons do (ApplyRelationship with the gift event, DeclareAlliance,
  DeclareWar, DeclarePeace); the money of a gift goes through the shared budget. Attacks on an
  empire's ships, missions and trade use the same hook but are not played in the test.
* Rivals mode (feature 10) turns sharing off; it is not tested here (the switch belongs to
  space_rivalry.cpp).
* 2026-11-04 (fix/w42-space-play): partners' changes arriving while a game is out of Space (an editor) are kept
  and replayed (BUG-0544, `diplomacy.keep_away`); before, the host lost them and its next table took them from
  the guest too.
* Ticket: BUG-0194.

## Space map: a guest's colonies last only until it rejoins

* Every player flies in a copy of the host's save: a colony the guest founds exists only in the
  guest's running game. When the guest leaves and joins again it loads the host's world and the
  colony is gone (`coop_space_map.py` checks exactly this). The host's own colonies survive (they
  are in the save the guests get). Shared colony ownership is not done (docs/re/space.md 5.3).
* The names of stars nobody named differ between the copies (docs/re/space.md 13.1): the roster
  sends the owner's names, the map shows mine for stars without a roster name.
* The test colony is founded with `coop.space map colonize` (the tool's effect without the aiming
  check): the real aiming refused every planet of the `space` fixture but the home planet ("Can't use
  in this system", "Colonies must be placed near spice"). A fixture with a planet with spice
  geysers would let the test aim like a player.
* The map is overlay text (a ring of dots, a name tag), not the game's own star marks; tags are not
  hidden behind the game's own galaxy panels (the star info card), only under the pause menu, the
  co-op window, comm screens and cinematics.
* 2026-11-04 (fix/w42-space-life): the rejoin is fixed (BUG-0195, `space.keep_colonies`, test
  space_colony_rejoin): the host keeps a guest's own colonies and gives them back when it rejoins. Still open: a
  host that restarts its game keeps nothing (memory only), the colony comes back with one city and no stored
  spice, the list is keyed by the player's name.
* Ticket: BUG-0195.

## Space trade: open ends (space_trade.cpp, 2026-10-23)

* **Rivals mode is tested only in a local merge**: space_rivalry.cpp (feature 10) is not in this
  branch; coop_space_trade.py passed its rivals part (offer, accept by a real click, both own
  budgets, an open offer declined when the host ends rivals mode) on a throwaway merge with
  feat/space-rivalry 2515362 (lane 10, GOG 2024, 2026-10-23). Without space_rivalry the test
  prints "rivals mode: UNTESTED".
  ~ superseded (2026-10-27): space_rivalry.cpp is on main and coop_space_trade.py runs its rivals
  part (`rivals()`), BUG-0153.
* **The guest's UI clicks do not arrive on the new space fixture (GOG 2024, lane 10)**: after the
  join the guest's game sits in a HUD-less movie state (`coop.space hp` shows `movie=1`, Enter
  does not end it), the co-op window opens with F9 but a click on "Space >" changes nothing. The
  same state makes coop_space_page / coop_space_death / coop_space_rescue fail on lane 10, also on
  the plain feat/space-map build (not caused by space_trade). coop_space_trade.py therefore lets
  the host do every real click and presses the guest's offer row through `coop.space press`.
  ~ superseded (2026-10-27): the movie state is fixed (docs/re/space.md 7.9, see "Space (GOG 2024):
  zooming in" above); the test still presses the guest's row through `coop.space press`, BUG-0153.
* **A gift or sale on its way when the own session ends comes back** (the sender cannot know
  whether the partner already got it): leaving right after "Give" can duplicate the charges.
  A partner who leaves before answering is handled (the sender gets it back).
* **Demand cap is per seller game**: the 30 units / 10 minutes of a partner colony are counted by
  the seller and forgotten on a restart; two sellers each get their own 30.
* **The F9 Space page has 9 rows for every feature together** (coop_ui.cpp kSpaceRows): the trade
  rows (gift, next tool, other partner, sale, offers) come after the features registered before
  it and can fall off the page when many features show rows at once.
  ~ superseded (2026-10-27): the page has 12 rows with paging since 0.7.0 (coop_ui.cpp
  `kSpaceRows`), BUG-0153.
* Spice values are the game's own (`spacetrading~` 0x34F1A4A x the difficulty multiplier, about
  5 600 to 24 000 per unit on normal), so one sale of 10 is worth 110 000 to 490 000 at 2x: big
  next to the fixture's $100 000. NPC stores pay similar money for the spices they want.
* 2026-11-04 (fix/w42-space-play): "a gift or sale on its way when the own session ends comes back" is fixed
  (BUG-0542, `trade.delivered`): what the partner's side acknowledged stays there. The leftover that did not fit
  is lost when its answer is lost.
* Ticket: BUG-0196.

## Space allies: what the mirror of a partner's colonies does not do (space_allies.cpp)

* Only stars nobody owns in my galaxy are mirrored. A partner's colony in a system that is mine or
  an NPC empire's in my copy is not shown (`coop.space allies list`: "skipped ... a star system of
  another empire here"); in the usual session (the guest plays a copy of the host's save) all
  colonies both had at the join are shared and need no mirror.
* The mirror empire has my own species (citizens, buildings, portrait): the partner's species is
  not in my files when the saves differ. Its colour is the palette colour nearest to the partner's.
* A mirror colony gets the game's own record-level colony: one city on landing (10 cCityData), not
  the partner's layout, buildings or spice.
* The relationship entry of the mirror (flags cleared) stays in a save, like the game's own entries
  of destroyed empires (there is no erase). The political id counter is not given back.
* Raids: only a raid while I am on that planet (the owner elsewhere) gets a raid of my own; the
  game's alert there says "your cities". Plunder raids (0xC4956AA7) are mirrored like raids
  (untested). A raid on a guest's colony helped by the host is untested (squadron makes the host's
  NPCs authoritative on both sides there).
* A partner who leaves stays mirrored ("away") until my session ends; with two players the host
  leaving ends the session, so the frozen ally is only seen on the host (a guest left) or with 3+
  players (untested).
* Rivals mode (feature 10): a war between the players makes the mirror an enemy (relationship war
  bit, enemies vectors); not tested here (feat/space-rivalry not merged).
  ~ superseded (2026-10-27): space_rivalry.cpp is on main now; whether this path is tested was not
  re-checked, BUG-0153.
* 2026-11-04 (fix/w42-space-play): a partner's colony in a star system that is mine here (both own it since the
  join) is now a record-level colony of my empire, undone before saves (BUG-0543, `allies.own_star`). Systems of an
  NPC empire in my copy are still skipped.
* Ticket: BUG-0197.

## Space: the galactic core finale, open ends (space_finale.cpp)

* A guest joins with a copy of the host's save, so it inherits the host's core flags: after the
  host entered the core, a guest that joins later has "entered" and gets the gift, but it can no
  longer see the cinematic itself (the game opens the core once per save).
* The core waits only for partners who are in the Space stage; a partner who never comes keeps
  it closed until "Enter now" (Space page). There is no timeout.
* "Fly to <name>" from a system jumps into the partner's system, or for a partner at the gate into
  the system it came from (the core is no solar system): the player zooms out and presses "Fly to"
  again in the galaxy view. The mod does not switch a system to the galaxy view itself (16.4 of
  docs/re/space.md). The flight skips the Grox: it is offered only to a partner who is there.
* The trip through the Grox space was not played in tests (the test teleports next to the core);
  the Grox fights there are squadron.cpp's normal NPC sync.
* GOG 2024: the addresses are ported and checked statically (docs/re/space.md 16), the test ran on
  GOG 2016 only.
* Ticket: BUG-0198.

## Not covered by tests yet

* Tribe: duet and healer bonus in real play (play_tribe.py plays real raids since 2026-10-23:
  pincer, the chief's aura and rescues happen there; the fixture's tribe has no instruments and
  no healer tool, so a social click on another tribe's hut is a food gift, CitizenAction 9, and
  nobody can heal: a fixture of a later tribe save is needed). Civilization: the religious role
  and trade in real play (play_civ.py plays military vehicles: the military role and joint sieges
  by real vehicles are covered). Space: raids and missions (no live mission in
  the fixture), spice trading with NPC stores (selling to a partner colony is covered by coop_space_trade.py), terraforming with the tools, buying in the city planner (the bot
  opens and closes it only).
* 2026-11-08 (fix/w46-g4b-coverage): healer bonus `tribe_healer` (found and fixed BUG-0745), NPC spice store
  `space_store` (BUG-0747 open: each game its own store), Civ planner buy `civ_planner_build` (BUG-0189); the rest are
  concrete tickets: duet BUG-0748 (test tribe_duet since 2026-11-08), terraforming with a tool BUG-0749, NPC missions BUG-0750, Space colony planner
  BUG-0751.
* Ticket: BUG-0199.

## Creature stage: a crash after dying and skipping the cutscene (player report, not reproduced)

* Reddit, 2026-10-24: "in the second phase (playing alone), every time I die and skip the cutscene my
  game crashes". Build, system and SporeCoop version not known yet.
* Not reproduced on GOG 2024 under Wine (SporeCoop 0.7.2, alone with the mod loaded): 16 deaths
  through the game's damage path (`coop.team die`) with the death caption and the respawn cutscene
  skipped by Esc during the caption, clicks on the cutscene, Esc after OK and Enter spam, with frames
  from Present and from the game loop (`frames=loop`): no fault, the creature always respawned.
  Regression test: `tools/tests/coop_creature_death.py` (suite `creature_death`).
* Next: the player's `mods\logs\sporekit.log` (a crash inside the mod is logged with its module and
  address; "exception ... calls:"), the game build (GOG / Steam) and Windows or Linux.
* 2026-11-07 (fix/w46-n1-crashes, 0.7.11 N1): not reproduced on rel/0.7.11 either: alone (creature_death) and in a
  session, host and guest dying, also at 20 fps over the player network (creature_death_coop, _slow): closed as
  not-reproduced with the runs in the ticket.
* Ticket: BUG-0026.

## Creature: "he is my species from my view", "I don't see his changes" (player report 2026-10-25)

* Not reproduced on 0.7.2.2 (lane 4, `coop_creature_desync.py`, the players' order: editors, both
  games crash, the host loads its save, the guest joins again; the guest with and without own
  creations): the host showed the guest's edited creature after the editors, after a rejoin and
  after the reload. Two 0.7.2.1 causes fit the report and are fixed in 0.7.2.2: a rejoin of the
  guest into a host that kept running did not apply the guest's creation again (writing its
  package failed: the host's puppet of the guest fell back to the host's own species, `modes.cpp`
  `creature_spawn`), and a new creation sent before it was on disk went out without its records
  (unknown on the host: the same fallback until the next send).
* ~ The guest's crash at 14:13 (the host's crash ended the session; fixed on fix/crash-reports-0725)
  may have come while the guest was in its own editor: an edit not saved by "done" is lost, and a
  guest without own creations then plays the world's species again, which the host shows as its own.
* Still so: a puppet whose player's creation cannot be used here (no records and an unknown key)
  is built from the local avatar's species, without any notice.
* Ticket: BUG-0011.

## Creature: "Game puts me underground after I use Alt + Enter" (player report 2026-10-25, not reproduced)

* The host (Windows, AMD GPU driver AMDXN32.DLL, SporeCoop 0.7.2.3) after Alt+Enter to fullscreen.
  Its two crashes the same day were Alt+Shift+Enter (the game's "next display adapter" key, a game
  bug, fixed: docs/re/graphics.md 3), so Shift was likely held on some of the Alt+Enter presses.
* Not reproduced under Wine/DXVK (lane 2, `coop_alt_enter.py` and by hand): every Alt+Enter is one
  device Reset, the avatar's height over the terrain (`coop.avatar` `above=`) stays 0.0 while it
  walks, the ground radius under it does not change. The game puts a creature below the terrain back
  on it within a frame, so a fix that lifts the avatar would do nothing: on that machine the terrain
  is most likely drawn higher than its collision height after the Reset (the terrain redraws on
  message 0x44EDD9C). SporeCoop does not move the local avatar.
* Next: the player's `mods\logs\sporekit.log` around the toggle ("device Reset -> 0x..." lines:
  how many, which result), a screenshot, whether saving and loading again (or leaving to the galaxy
  and back) fixes it, and whether it happens with SporeCoop removed. Until then: set fullscreen in
  the game's options before loading a planet instead of using Alt+Enter in game.
* Ticket: BUG-0013.

## Creature: the host's own creations on the guest, what is still open (2026-10-25)

* Since fix/nest-species a guest builds the host's NPCs of the host's own creations from the host's
  records under a key of its own (0x7D......, docs/re/creature.md 15). The host's world on the guest
  still names them by the host's key, which is the guest's own creation there: a card the guest's
  game makes from a nest (not from a creature) shows the guest's creation's name (with the host's
  relation). Rewriting the save to the guest's key lost those herds at the load and crashed the
  pack panel (section 15 there). Both players need this version: an older host sends nothing (the
  guest shows its own creation of the key, as before), an older guest drops kind 5.
* Not checked: a real nest (with an archetype) of a host creation on the guest (the fixture planet
  has Maxis species only).
* Ticket: BUG-0200.
* FIXED (2026-11-07, fix/w46-p4-creature, unreleased): the guest's species manager answers for the host's keys with
  the host's creation (`creations.host_key_alias`, docs/re/creations.md#creations-species-map); nests and their cards
  of the host's creations show the host's creature. `tools/lab.py run nest_species`.

## All stages: 138 bugs from the code review of main 563f72d (2026-10-27, claude/clever-fermi-h57l81)
<a id="issue-code-review-2026-10-27"></a>

Two read-only reviews of main at 563f72d (round 1 per area, round 2 per cross-cutting concern
with independent verification) found these bugs. None was checked in the running game yet.
Each line is one open ticket: what happens, how to reproduce it, what is known (confidence), the
suggested fix and the planned test are in the ticket and in its GitHub issue. The tracking issue
WoodyWizard/SporeCoop#82 orders them by priority. When a fix lands, its line goes (the ticket
keeps the history).

**All stages (netcode, core, UI, SporeKit, updater, tools, tests, docs)**

* BUG-0033 (#11, bug): leaving, re-hosting and hot reload each clean up a different subset of the co-op state.
* BUG-0035 (#13, bug): a received world can move and overwrite the save slot the guest is playing (auto_load_world=0)
* BUG-0034 (#12, desync): one reliable message bigger than a packet stalls all reliable traffic and floods empty packets.
* BUG-0056 (#34, bug): a stack overflow ends the game without a crash report.
* BUG-0053 (#31, bug): partner's puppet freezes when the sender's millisecond clock wraps (49.7 days uptime)
* BUG-0057 (#35, tooling): tickets.py new reuses ticket ids of branches whose worktree is gone.
* BUG-0062 (#40, tooling): tickets.py new --private in a worktree writes to that worktree's out/ and the ticket is lost.
* BUG-0068 (#46, test): twoinst Monitor reads the wrong game log in worktrees, world loads over 10 s become FROZE.
* BUG-0073 (#51, tooling): a lab test that FAILs and then passes on retry counts as passed.
* BUG-0074 (#52, bug): after leaving a session, the game quits to the menu and loads the old host world once the editor closes.
* BUG-0076 (#54, desync): a new player is shown with the creature or ship of the player who had their id before.
* BUG-0096 (#74, test): coop.vote editor submits the editor request inside Present, a path the real game never takes.
* BUG-0097 (#75, tooling): release job does not check tag against COOP_VERSION, and notes for vX.Y.Z take every X.Y.Z.N section.
* BUG-0101 (#79, tooling): spore.sh kill SIGKILLs half-dead SporeApp processes of every lane, not only its own.
* BUG-0102 (#80, tooling): spore.sh fixture restore with profile "-" deletes the player's own saves.
* BUG-0103 (#81, tooling): more places that can kill the owner's own Spore on lane 1 (BUG-0025)
* BUG-0100 (#78, tooling): a test that prints RESULT: SKIP is counted as passed by the lab.
* BUG-0105 (#84, bug): a received world is loaded even when writing its files failed (disk full, locked or read-only file)
* BUG-0106 (#85, bug): shared money can be spent twice in one round trip, and Civ/Space money goes negative.
* BUG-0112 (#91, bug): with 2+ guests waiting for the world, the host posts one save per guest in the same frame.
* BUG-0109 (#88, desync): with 3+ players a departed player's last state arrives after PLAYER_LEAVE and leaves a ghost partner.
* BUG-0116 (#95, test): the required fix switch `coop.fix NAME off` does not exist, and several 0.7.2.x fixes have no switch.
* BUG-0124 (#103, test): a fixture that lacks its facts never fails the test (check_fixture result ignored)
* BUG-0130 (#109, bug): a guest in another world gets the host's DNA, NPCs, tribe and cells applied to its own game, which is then saved.
* BUG-0133 (#112, desync): after the latency rises, partners and NPCs move in steps for about 50 s.
* BUG-0135 (#114, test): coop_team.py checks the joint-hunt bonus only by a mod counter, never the extra damage on the NPC.
* BUG-0134 (#113, tooling): new_stage_game.py --npc-cities uses GOG 2016-only symbols, and 0xAE3A80 has two contradicting signatures.
* BUG-0137 (#116, bug): crash dumps, reports and report zips (up to 24 MB each) pile up in mods\logs and are never removed.
* BUG-0142 (#121, bug): a Cyrillic or CJK player name becomes "????" after a restart, and a name typed into coop.ini by hand is garbled.
* BUG-0143 (#122, bug): 43 game symbols the code uses (several hooked) have no checked signature on any build.
* BUG-0202 (crash): making a hook while another thread runs the first bytes of the hooked function can crash that thread (hot reload re-hooks RandomInt).
* BUG-0138 (#117, bug): a pasted host address with a leading space fails as "cannot resolve", and a wrong port is silently accepted.
* BUG-0144 (#123, bug): the netcode message table lists PING/PONG, a mod version and fields the protocol does not send.
* BUG-0152 (#131, test): most 3-4 player gameplay paths have no test, and coop_multi runs in no lab suite.
* BUG-0205 (bug): host in an editor or a loading screen: Cell mirrors freeze (cellsync has no host-away state, also not for the main menu). The goal reward part is fixed and the Creature side counts an editor or a load as away since fix/w4-census (not proven in game, see the ticket).
* BUG-0153 (#132, bug): known-issues.md has a merge-damaged entry, stale "fixed" and outdated entries, and no ticket ids.
* BUG-0162 (#141, test): hot reload and every 3-4 player test are outside the lab suites, so they never run.
* BUG-0156 (#135, test): test profiles redirect only AppData, so tests on lane 1 can write into the owner's own Documents (My Spore Creations)
* BUG-0158 (#137, bug): the triage-crash skill and tools/crash_triage.py are referenced but do not exist.
* BUG-0165 (#144, bug): INSTALL check line, README latest version and stability-report lane count are out of date.

**Creature**

* BUG-0119 (#98, bug): after a load the guest may keep the host's species, or get SetSpecies on a world still loading (avatar pointer used as world identity)
* BUG-0125 (#104, bug): a guest that rejoins while the host is paused sees no animals near it.
* BUG-0256 (crash, caught): a guest that rejoins and loads a host save written while its puppet existed faults every frame in Herd_Update (cCreatureAnimal::Update+0xAA2) after the stray removal. Fixed on fix/w9-creature (savestrip.pending, npc.herd_nulls).

**Tribe**

* BUG-0067 (#45, bug): chiefs stay 1.3x big after the session ends, and the epic creature state survives a leave or a load.
* BUG-0121 (#100, bug): every co-op epic creature leaves a mod-made herd in the world, and a live epic can go into the host's save.
* BUG-0146 (#125, bug): a member of an NPC tribe counts as a second player hitting the epic creature (80 food instead of 30)

**Tribe, Civ**

* BUG-0075 (#53, bug): gifts can go to a player who is not there, and with 3+ players the Give button only reaches one player.

**Civ**

* BUG-0031 (#9, bug): vehicles of a player who left stay theirs, nobody can order them, and captured cities can go to them.
* BUG-0117 (#96, bug): a second superweapon request changes the weapon and target of the vote already running.
* BUG-0161 (#140, bug): the "under attack" / "being converted" city beacons probably start with an empty box instead of the swords or cross symbol.

**Space, Cell**

* BUG-0131 (#110, bug): a disabled, knocked-out, dead or frozen partner counts as the partner who rescues you.
**Space**

* BUG-0046 (#24, bug): armada contract starts a second raid when the host comes back, and the old raid decides the wave.
* BUG-0063 (#41, bug): flying to a partner who is in the galaxy view costs energy, can damage the ship and works while disabled.
* BUG-0072 (#50, bug): with 3 or more players the "Jump to" button fails, and the jump message names the wrong player.
* BUG-0090 (#68, bug): galaxy map name tags react to mouse clicks in other windows.
* BUG-0111 (#90, bug): the guest sees an empire's defender ships twice, because any UfoType 6 ship counts as "our puppet".
* BUG-0123 (#102, bug): planet seeding flags are shared by all threads, so another thread can switch the seeding off.
* BUG-0126 (#105, bug): coop.space empire contact leaks one x87 register per call (ApplyRelationship called as void)
* BUG-0136 (#115, bug): a won raid on a partner's colony can count as aborted, so the owner's raid is never completed.
* BUG-0145 (#124, bug): with swapped mouse buttons, a click on a partner tag does nothing and a camera drag flies the ship.

**Installer**

None open (BUG-0077, BUG-0080, BUG-0094 and BUG-0139 fixed on fix/w4-installer).

## Co-op window: a host on the network with two addresses is listed twice (2026-10-28, fix/rel-flaky)
<a id="issue-lan-host-twice"></a>

* What happens: under "Games on your network" a host whose PC has two addresses on the network
  (cable and Wi-Fi) shows up in two rows, one per address. Both rows join the same game.
* Reproduce: lab runs with several lanes on the lab PC (wired and Wi-Fi address):
  out/lab/20261028-043811/ui_status.log lists every other lane's host at 192.168.1.113 and at
  192.168.1.138.
* Known: `net::Discovery::on_reply` keeps one entry per address and port; the discovery answer has
  no host or session id to merge them by.
* Not checked: two separate PCs.
* Ticket: BUG-0210.
* 2026-10-30 (fix/w13-net): fixed (switch `coop.fix net.lan_dedup`): the LAN answer carries a game
  id, one row per id; a host of 0.7.3.1 or older (no id) is still listed once per address.

## Space trade: an owed refund can be paid into another game (BUG-0225, fixed on fix/w9-space)
Fixed (unreleased): owed trade and spice refunds go by the Space world and its save file
(docs/re/space.md#space-owed-refund-identity). Left: a save that fails to write is still taken as
saved (the game's SaveGame reports nothing), and the owed queue is memory only (a hot reload or a
game restart drops it).
* Ticket: BUG-0225.
## Cell: a guest may bite one NPC more often than the host can (2026-10-28, fix/w5-cell)
<a id="issue-cell-guest-bite-lock"></a>

* What happens: after a bite of the host's own cell the game does not let the same NPC be hurt
  again for a moment (a lock). A guest's bite is applied by the host as an hp change and sets no
  lock, so a guest biting fast may take hp more often than the host.
* Reproduce: not measured. Host `coop.pool kill c<key> hit` twice within 0.5 s, then the guest the
  same, and compare the hp the host's game lost.
* Known: Bite sets +0x178 on both cells before DamageCell; DamageCell refuses while the victim's
  +0x17C or +0x111 is set (docs/re/cell.md#cell-damage-gate). Who sets +0x17C and who clears the
  locks is open. The cell type and AI type part of the gate is fixed (BUG-0240,
  docs/re/cell.md#cell-ko-grace).
* Not checked: whether the guest's own collision code starts bites faster than the host's.
* Next steps: measure the lock bytes per frame after a host bite; making a guest bite take the
  game's own path (DamageCell on the host with the puppet as attacker) is a separate design.
* Ticket: BUG-0240 (the lock part left open there).

## Space: one caught fault in the game's UI code while a guest loads a shared world (2026-10-29, fix/w6-campaign2)

* What happens: right after the load of a shared Space world (the campaign's jump to the space
  fixture) a guest caught `0xC0000005 at 0x00000065` from the game's UI code (0x0095904B, near
  UTFWin::Image / UILayout). The game handled it and played on; seen once in three campaigns.
* Reproduce: not reproduced. Lab 20261029-093233 campaign#2, out/lane4/campaign/20261029-093248.
* Contained (2026-10-30, fix/w9-space): the window manager's modal window was a destroyed object;
  a guard now drops such a window before a mouse message uses it (`game.dead_modal`,
  docs/re/space.md#space-owed-refund-identity). Still open: which game window it was and why the
  game destroyed it without PopModal (the guard logs "the window manager's modal window ... was
  destroyed" when it happens again).
* Ticket: BUG-0270.

## Tests: the smoke test `select` sometimes finds the partner off the screen (2026-10-29, fix/w6-cell)
<a id="issue-select-partner-off-screen"></a>

* What happens: `coop_select.py` fails with "the partner is on the screen (point 0 X Y)": the
  partner projects outside the 1440x1080 window (x 1859, 2395, 4078, -151).
* Reproduce: `tools/lab.py run select --lane 4 --repeat 3`, about 1 in 3 (lab 20261029-121511,
  20261029-121818, also with `SPORE_FIX_OFF=core.stale_local`).
* Known: older than BUG-0271's change (lab history 20261027-230552, 20261028-011940).
* Not checked: where the partner stands relative to the camera at that moment.
* Next steps: wait until the partner is on the screen (or turn the camera) before clicking.
* Ticket: BUG-0272.

## Four-player campaign: findings of the final run (2026-10-29, main)

Lab 20261029-122937 (4 players, three full games, three Cell -> Creature games; no crash, no fault).
All fixed in wave 7: the lost "I'm ready" clicks under a caption (BUG-0273), the proxy of a pooled
host NPC (BUG-0274; what is left: [the entry below](#issue-npcs-only-around-host), BUG-0280), the
3rd and 4th players' false desyncs (BUG-0275, the check; BUG-0281 and BUG-0282 are what the
per-player measurement found), the stale world share right after a stage change (BUG-0276).
Reproduce a full game: `tools/lab.py run campaign --lane N` (about 85 min) or `campaign_short` (9 min).

## Creature: NPCs live only around the host; a guest far from it sees an empty world (2026-10-29, fix/w7-npc)
<a id="issue-npcs-only-around-host"></a>

* What happens: the host's game puts every NPC farther than about 150-200 m from the host's own
  avatar back into its creature pool, also the ones right next to a guest. The guest then sees no
  NPCs there, and a nest that misses a member spawns it again and the game pools it again 1-3 s
  later: on the guest a family member appears at the nest and vanishes every 3-6 s (a new net id
  each time).
* Reproduce: creature fixture, the guest at the nest, the host 300 m away (`coop_npc_pooled.py`
  does it; its "matched proxies" stop while the host stands far away).
* Known: observed on lane 4 (2026-10-29), docs/re/creature.md#creature-npc-pool-cull. The cull
  distance and the game function were not looked up.
* Not checked: whether the guests could keep the NPCs of their own area (their game runs them
  while the host is away) or the host's game can be made to keep creatures around the puppets.
* Ticket: BUG-0280.
* Fixed (2026-10-30, fix/w9-creature, `npc.view_centres`): the cull is the game's relevance pass
  around the local camera (docs/re/creature.md#creature-view-centres); the host now adds a sphere
  per partner, in Creature and (npc.view_centres_tribe) Tribe; in Tribe the host also sends a guest
  the animals around its pointer (BUG-0291, npc.rts_views). Still open: the herds' brain range
  filter uses the host's brain level (matters for Rivals); Civ is not covered.

## Creature: an NPC proxy next to a guest's avatar stays ~4 m off (2026-10-29, fix/w7-sync)

* One host NPC standing 3-5 m from the guests' avatars was 3.8-4.3 m from its interpolation target
  on every guest at once (`coop.npc list` on a guest: `err=4.33`). Likely pushed by the guest's own
  avatar (inferred). Reproduce: `SPORE_LANE=N python3 tools/tests/coop_sync_multi.py --fixture
  creature --rounds 45` and `coop.npc list` on a guest. Ticket: BUG-0281.
* Not reproduced (2026-10-30, fix/w9-creature): with 2 players a guest's avatar 0.3-3 m beside
  a standing host NPC did not push its proxy (0.1-0.6 m off); a 4-player measurement showed large
  errors only for moving NPCs in a fight (2-19 m on 1-3 guests).
* Checked again (2026-10-30, fix/w13-creature, lane 5, docs/re/creature.md#creature-proxy-guest-side):
  standing NPCs next to a 15 fps guest's avatar stayed within 0.5 m; two NPCs running side by side
  at 4 m/s were p90 6.2 m off on a 15 fps guest without `npc.trail` and 3.1-3.4 m with it (Tribe
  0.7-0.95 m): Creature proxies of that species hardly walk by themselves. Measure:
  `SPORE_LANE=N DASH_FIXTURE=creature python3 tools/tests/coop_tribe_npc_dash.py`.
* Fixed (2026-10-30, fix/w14-creature, `npc.kinematic`): the proxies of running NPCs did not walk (a
  walk order every 0.1 s restarts the walk); they now follow the host's path (p90 0.3 m at 15 fps,
  docs/re/creature.md#creature-kinematic-proxies). Standing NPCs 4 m off were never reproduced.

## Cell: a guest's mirrors trail the host's cells at a low frame rate (2026-10-29, fix/w7-sync)

* At 15 fps (`frame.limit 66`) 283 of 13165 compared cells were 1.2-2.3 m behind the host's (about
  0.65 s of their way), at 30 fps 2, at 65 fps 1. Reproduce: `FPSLIM=Carol:66 SPORE_LANE=N python3
  tools/tests/coop_sync_multi.py --fixture cell --rounds 25`. Ticket: BUG-0282.
* 2026-10-30 (fix/w9-cell-food): the 4-player campaign's "Bob:c:2.2" (BUG-0289) was not this: Bob's
  pause menu was open (no simulation, mirrors moved only by the 3 m jumps). The campaign check now
  skips a paused guest.
* 2026-10-30 (fix/w13-cell): not reproduced on 5d7f20e (2 players at 15 fps: 0 of 16 616 cells over
  1.2 m; 4 players at 15 / 30 fps after level-ups: p90 <= 0.05 m). BUG-0282 is not-reproduced, guard
  test `cell_lowfps`.

## Tribe: a handled game fault in a citizen's walk while the stage is left (2026-10-29, 4-player campaign)
<a id="issue-tribe-leave-hears-fault"></a>

* What happens: in the frame the host's Tribe stage is left for the galaxy menu, the game faults in
  `CreatureHearsTarget+0x463` (0x00C0DB23, read of +0x3C of null) under
  `cCreatureCitizen::DoAction > cCreatureBase::WalkTo`, and its own handler goes on. The handler
  unwinds past the menu's player reset; the next load from the menu crashed until BUG-0284.
* Reproduce: not reproduced; seen once (lab campaign 20261029-174717, lane 7, 4 players, after 30
  min of tribe with raids and an epic creature).
* Known: docs/re/space.md#space-menu-player-reset (what the unwind skips).
* Not checked: which object is null; whether a mod object is involved; what else of the menu
  reset (0xB32C30) is skipped besides the player record.
* Next steps: probe 0xC0DB10 during `coop_tribe_*` leaves to the menu with 4 players.
* Update (2026-10-30, fix/w9-tribe): the null object is the citizen's own `+0xB54`, released by its
  cleanup 0xC1CA40; the game updates it once more through a handler that runs only when the mode is
  no longer Tribe (0xD673B0 -> 0xACA2A0 -> 0xD57680 -> WalkTo -> 0xC0DB10). No coop.dll frame in the
  chain; members spawned by `coop.tribe spawn` have +0xB54 and 4 leaves with 28 of them caught no
  fault (lane 8). The co-op epic destroyed by the quit's save is an animal (another branch).
  docs/re/tribe.md#tribe-leave-walk-fault. Still not reproduced; no fix. Its consequence is
  contained by BUG-0284 (world.menu_player).
* Update (2026-10-30, fix/w13-tribe-civ): seen a second time (BUG-0310, campaign#3 of lab
  20261030-104149): the next load from the menu then faulted in the space graphics and never ended.
  Guarded since (`tribe.leave_walk`: a cleaned-up citizen is not checked for a new walk); the
  stand-in test `tribe_leave_walk` reproduces fault and freeze with the guard off. Which citizen the
  game cleaned up before its action ended is still not known (docs/re/tribe.md#tribe-offstage-action).
* Ticket: BUG-0285
## Space: guests' ships go down to the home planet after the first world share (2026-10-29, fix/w8-stuck)
<a id="issue-space-share-first-frame"></a>

* What happens: after Civ -> Space with four players the host leaves its home planet; the guests
  load its world into the solar system and 2 s later their ships are on the home planet, apart from
  the host, until they rejoin (lab 20261029-174717 campaign#2).
* Reproduce: `tools/lab.py run campaign_space --lane N --repeat 2` with
  `SPORE_FIX_OFF=world.share_settle_reset`; not reproduced so far (the save must land in the first
  frames of the solar system).
* Known: the save was posted 9 ms after the host reached the system
  ([space.md](re/space.md#space-share-save-at-context-change)); the share save now waits 2 s after
  every place change (`world.share_settle_reset`). Inferred, not proven, that this was the cause.
* Next steps: if it is seen again with the fix in, the "saving for the share" line of the host's log
  says how long the host had been in the system.
* Ticket: BUG-0287.
## Wine: other programs of the same prefix can still collide with the address lookup (2026-10-29, fix/w8-adapters)
<a id="issue-wine-nsi-other-callers"></a>

* SporeCoop no longer calls iphlpapi under Wine (BUG-0286), but Wine's own code inside the game
  (ws2_32 `gethostbyname` of the own host name, winhttp proxy detection) and an older SporeCoop in
  the same prefix still use Wine's nsi device. Seen only with several games in one
  bottle (lab). Reproduce the collision: two games of a lane, `coop.fix core.adapters off` on both,
  `coop.adapters stress 300` on both at once. Ticket: BUG-0286.

## Cell: a guest's cell position became NaN, then a crash in PopulateTile (2026-10-30, fix/w9-cell-food)
<a id="issue-cell-guest-nan-populate"></a>

* What happens: once in four `cell_new_game` runs the guest's own cell position became NaN after a
  handled fault at PopulateTile+0x8A (0xE7C74A); its game crashed there at the next level change.
* Reproduce: `tools/lab.py run cell_new_game --lane N` (seen in lab 20261030-012925 only).
* Known: a NaN view point gives PopulateTiles a garbage tile range (inferred to be the consequence,
  not the cause). The host had no fault.
* Not checked: where the NaN comes from; whether main has it.
* Guarded (2026-10-30, fix/w9-cell-food, `pool.finite_state`): non-finite cell poses are not sent or
  mirrored and non-finite positions are no view centres; not reproduced since (15 runs). If a log
  shows "our own cell's position is not finite" or "pose that is not finite", reopen with it.
* Ticket: BUG-0292.
## Tribe: guests miss host objects while the host's tribe is knocked out (2026-10-30, fix/w9-tribe)
<a id="issue-tribe-coverage-before-bounce"></a>

* What happens: in the 4-player campaign (lab 20261029-203821 campaign#2) two guests had less than
  85 % of the host's objects at two checks (21:07:27 and 21:07:57 lab clock), while the host's
  player tribe was being knocked out (members 13 -> 5, then the game-over caption's cinematic).
* Reproduce: not reproduced apart from the part after the bounce back (BUG-0288, fixed).
* Known: the guests' mirror counts followed the host's member counts in the 30 s trace lines; the
  coverage check counts the host's members (t) and the host's animals within 90 m of the guest's
  pointer (n). Which kind was missing is not logged. Candidates: a new member of the host (born or
  saved) refused by the guest's BUG-0172 guard while every mirror was dead (fixed by
  `tribe.mirror_out`), or animals near a guest's pointer far from the village (BUG-0291).
* Next steps: the campaign's coverage finding now names the missing keys per kind; read them in the
  next 4-player campaign.
* Ticket: BUG-0288 (fixed part), BUG-0291

## Cell, 4 players at high levels: the partners' areas are thinner than the host's (2026-10-30, fix/w10-cell-4p)
<a id="issue-cell-4p-budget"></a>

* What happens: with four players apart at level 4 the host already holds 1000-1400 cells, so the
  cell budget (BUG-0295: partner tiles populated only below 1100 host cells, its screen below 1300, the game's collision
  step holds ~2048) leaves part of the partners' new tiles empty until cells die or the host's
  area shrinks. Since bea15ec every player's screen (the host's too) is filled first, up to 1300
  cells, and the rings around the screens only below 1100: food appears at the edge of a screen a few
  frames late when the host's pool is busy. Seen in `cell_4p` (lab 20261030-052856):
  542 tile populations postponed, 99 done later; the far guests still had 185-322 host cells within
  60 m, but food on a screen was sometimes 0.
* Reproduce: `SPORE_LANE=2 python3 tools/tests/coop_cell_4p.py`, read `starved=` and `fed=` in the
  host's `coop.pool`.
* Known: docs/re/cell.md#cell-broadphase-cap. A fair split (each centre's own share of the budget,
  shedding cells around the centre that holds the most) would need cells destroyed by us on the
  host, not done.
* Not checked: Rivals (0.8) with every player far away at level 5.
* Next steps: per-centre shares before Rivals (docs/11-rivals.md).
* Ticket: BUG-0295

## Cell: a guest's own cell becomes NaN without a NaN mirror (2026-10-30, fix/w10-cell-4p)
<a id="issue-cell-own-nan"></a>

* What happens: once in a 4-player campaign_short (lab 20261030-053709, guest Bob, level 4, 900
  mirrors, none of them NaN) the guest's own cell position became NaN. Before BUG-0299's guard its
  game then faulted in PopulateTiles and crashed at the next level-up; with the guard the cell is
  put back at its last finite position.
* Reproduce: not reproduced on purpose; `coop.pool nanself` (tests) makes the state
  (`coop_cell_nan.py`).
* Known: docs/re/cell.md#cell-broadphase-cap (end). BUG-0292's guard (no NaN mirror) did not catch it.
* Not checked: the soft-body step with hundreds of overlapping mirrors (the guest's steering of
  mirror particles, cellsync guest_tick) as the source.
* Next steps: log the own cell's particles and the nearest mirrors on the first NaN frame.
* Ticket: BUG-0299

## Space: opening the captain screen logs a caught fault in the game (2026-10-30, fix/w11-finale)
<a id="issue-captain-screen-fault"></a>

* What happens: every opening of the captain screen (the rank badge over the ship's picture) logs
  `0xC0000005 at 0x005971D4` (named Simulator::cCollectableItems::LockPart+0x3C4). The game handles
  it and plays on; the screen's left panel shows a missing string.
* Reproduce: `tools/lab.py run space_captain`, or click the badge (1390 890) in any lab Space save;
  also with `mod.unload coop`.
* Known: the game's own null object in the captain screen's update (docs/re/space.md#space-captain-screen-fault).
  A campaign [fault] finding with this address is this, not a coop bug.
* Not checked: a save with a Galactic Adventures captain, the GOG 2016 build.
* Ticket: BUG-0304 (game-bug).

## Tribe, 4 players: one animal stands ~4 m off on every guest at once (2026-10-30, fix/w11-finale)
<a id="issue-tribe-npc-4m"></a>

* What happens: the campaign's sync check finds one host animal 3.4-4.4 m off on all three guests at
  once for a minute or more; with few animals near the guests two such objects make a [desync]
  finding (lab 20261030-075052 campaign#3, n212).
* Reproduce: `SPORE_LANE=N python3 tools/tests/coop_sync_multi.py --fixture tribe --rounds 30 --jsonl x.jsonl`
  (an animal off by 4-5 m on two guests in the same round, about once in 30 rounds).
* Known: not caused by the view centres merged on 2026-10-30: the same rate with
  `SPORE_FIX_OFF=npc.rts_views,npc.view_centres_tribe` (2.2 % vs 2.6 % of animals over 1.5 m). The
  same pattern as BUG-0281 in the Creature stage.
* Not checked: the proxy's own error (`coop.npc list` err=) and the host animal's speed at that moment.
* FIXED (2026-10-30, fix/w12-tribe-npc): the proxies of a running herd push each other off the path
  on a guest and never run faster than the animal, so one trailed 2-4 m on every guest
  (docs/re/tribe.md#tribe-animal-proxy-trail). A proxy 1.5 m off for 0.5 s is put on its path and
  held there for 1 s (`npc.trail`). Test: `tribe_npc_dash`.
* Ticket: BUG-0305.

## Cell, 4 players: a guest's own cell goes NaN again and again, then its game slows down and drains memory (2026-10-30, fix/w12-cell-fault)
<a id="issue-cell-nan-repeat"></a>

* What happens: lab 20261030-104149 campaign#2, guest Carol (3rd player, 900 mirrors, far from the
  host): the own cell NaN 26 times in 6 minutes despite BUG-0299's heal, the mirror count swings
  900 / 300 / 40, fps 26 -> 1-3, free address space 3502 -> 1427 MB. See
  #issue-cell-own-nan for the NaN itself.
* Reproduce: not on purpose; a 4-player `campaign` (Cell, level 4) on a loaded machine showed it.
  The repetition is reproduced and fixed (`cell.nan_heal_all`, cell_nan_all): the old heal left
  NaN matrices / rest offsets that made the position NaN again (docs/re/cell.md#cell-nan-fields).
* Still open: what makes the first NaN (the own_probe log lines name the step) and what drains the
  address space (not the mirror churn, not the NaN state: cell_churn and cell_nan_all measured 0 MB).
* 2026-11-06 (fix/w45-cell): one natural case logged: a 2-player guest's level-up (lab 20261105-142923
  cell_new_game#1), velocity and particle velocities NaN (mask 0x104) for 17 frames, after the collisions and
  before our guest tick; its camera stayed NaN (BUG-0604, fixed by cell.camera_heal). The writer of the NaN
  velocity is still not known (docs/re/cell.md#cell-view-nan).
* 2026-11-06 (fix/w45-cell-nan): the writer found and fixed: the collision contact solver after the pair loop, fed
  an unset (non-unit) contact normal when two collision spheres share a centre (docs/re/cell.md#cell-contact-nan,
  `cell.contact_finite`). The address space drain is BUG-0621 now.
* 2026-10-30 (fix/w13-cell): the coverage loss was the jumps of the NaN heals leaving background
  cells behind until the guest's pool hit its mirror cap (BUG-0314, fixed by `cell.bg_cull`). The
  first NaN and the address space drain are still not reproduced (docs/re/cell.md#cell-bg-leftovers).
* 2026-11-06 (fix/w46-addr-space): the address space drain is BUG-0355's per-frame soft body calls on a full cell
  graphics pool (8.3 MB/min with cell.gfx_full off in a crowd of 1000 cells, 0 with it on: test
  mem_census_cell_crowd; docs/re/graphics.md#graphics-address-space), fixed in 0.7.3.2.
* Ticket: BUG-0307 (the NaN, fixed), BUG-0621 (the address space drain, fixed by BUG-0355 in 0.7.3.2)

## Cell, 4 players: two one-off faults of the 4th player (2026-10-30, fix/w12-cell-fault)
<a id="issue-cell-4th-player-faults"></a>

* A rejoined guest leaving Cell for the host's world found a corrupt heap when the cell pool was
  freed (`GeneralAllocator_FreeInternal` under `cObjectPool::_dtor` < SimUpdate+0x6ED), lab
  20261030-104149 campaign_short#3. Ticket: BUG-0308.
  Fixed (2026-11-05, fix/w45-crashes): the guest sat in its pause menu while its mirrors churned, BUG-0348's
  broadphase overflow (fixed by `cell.sap_pending` in 0.7.3.2); the path is test `cell_pause_rejoin`
  (docs/re/cell.md#cell-rejoin-paused-heap).
* A guest 6 s into Cell jumped to address 0 in UI code right after its test window was moved and its
  main thread stopped, lab 20261030-104149 campaign#1. Ticket: BUG-0309.
  CORRECTION (2026-11-01, fix/w33-null-call): not UI code; SporeKit's GetAsyncKeyState detour
  called its still-null original while the input hooks were being made (inferred from the registers;
  BUG-0398, fixed by `kit.fix hookorig`; esi = 0x78 = VK_F9, polled by coop every frame).
* Both seen once, on an overloaded machine. Reproduce: not yet.

## GOG 2024 guest: the game time stays paused for seconds after its world load (2026-10-30, fix/w13-net)
<a id="issue-2024-load-pause"></a>

* What happens: after its world load in the Tribe stage the GOG 2024 guest keeps its game time
  paused (cGameTimeManager +0x48 = 3, black screen, ~23 fps) for ~7 s, up to ~20 s on a loaded lab
  PC (20261030-133141); the GOG 2016 guest runs at once. Mirrors do not walk meanwhile.
* Reproduce: `SPORE_LANE=10 python3 tools/tests/coop_sync.py tribe` prints "game time running N s
  after the start".
* Not known: what holds the pause (planet still loading?). Players see a black screen, no mirror
  error.
* Ticket: BUG-0313.
## Civilization: the campaign's end of Civ never comes after the last city changed hands by a talk (2026-10-30, fix/w13-tribe-civ)
<a id="issue-civ-end-after-talk"></a>

* What happens: campaign_space#1 (lab 20261030-104149): every city of the planet was the players'
  (`owned_cities=10` of 10 on the host), the last one without a capture (a nation's talk was answered,
  another closed with Esc), and the game's end of Civ (spaceship editor vote) did not come in 14 min.
* Reproduce: not reproduced; `coop_campaign.py --from civ --to space` (1 of 3 runs that day).
* Known: the shortcut's "Capture of 0 city (0 of other nations listed)" is right (nothing left to
  capture), it is not blind to other nations. In passing runs the last city was a capture and the
  vote came 8 s after its cinematic.
* Not checked: whether the game ends Civ only through a capture, or the Esc closed its prompt.
* Next steps: log `coop.ui` dialogs on every player when all cities are owned and no end comes.
* Ticket: BUG-0311

## Civilization: an NPC city founded late in a session is missing on the guest for up to a minute (2026-10-30, fix/w13-tribe-civ)
<a id="issue-civ-late-npc-city"></a>

* What happens: in the last minute of a `play_civ.py --seed 9` session the guest lacked one new NPC
  city and two vehicles for 40-60 s (every audit); it caught up afterwards (`cities_caught_up=4`).
* Reproduce: `SPORE_LANE=N python3 tools/tests/play_civ.py --minutes 10 --seed 9` (1 of 3 runs).
* Known: vehicle positions of the same run were in sync (p90 0.64 m).
* Not checked: the guest's city catch-up timing, whether it was in the city planner then.
* Ticket: BUG-0319
* 2026-10-30 (fix/w13-cell): BUG-0308 got a guard (`cell.leave_guard`: none of our cell work runs on
  the pool while the stage is being left); the crash itself is still not reproduced. BUG-0309: the
  stack words are stale frames, nothing to go on (docs/re/cell.md#cell-leave-guard).
## Creature, 4 players: a guest misses one of the host's NPCs although it has the rest of its herd (2026-10-30, fix/w13-creature)
<a id="issue-proxy-not-in-world"></a>

* What happens: campaign_short (lab 20261030-060122 on fix/w10-cell-4p): for 30 s and more a guest
  lacked one NPC 90 m from it (census and sync-check), while three members of the same herd 77-84 m
  away were there. The guest's proxy list had it (states arriving), its world had no creature for it.
* Reproduce: not reproduced (2-player rejoins, guest camera far from its proxies); seen once in 4-player
  `campaign_short`.
* Known: not a range edge; the guest's relevance pass never pools proxies (`+0xB67` = 0); since
  fix/w13-creature the guest logs "the proxy's creature ... is no creature of this world any more"
  or "... has no species" and makes such a proxy again (`npc.proxy_listed`).
* Next steps: when the line shows in a campaign, follow what freed or emptied that creature.
* Ticket: BUG-0300.
## Space finale: the host's "entered with the partner" counter after a wait (2026-10-30, fix/w13-space)
<a id="issue-finale-entries-with"></a>

* `coop_space_finale.py` failed once (lab 20261030-142729, lane 9): both entered the core
  together, but the host's `entries_with` stayed 0 because it entered before the guest's "reached"
  message came (the wait flag is cleared when everybody is at the gate). Players see nothing wrong;
  the test's check reads the counter. Reproduce: run `space_finale` a few times.
* 2026-10-30 (fix/w14-space): FIXED (space.finale_waited), test `coop_space_finale_wait.py`.
* Ticket: BUG-0320.

## Space: a guest fault at 0xE98DD0 when the host's ship appears, lane 8 only (2026-10-30, fix/w13-space)
<a id="issue-space-e98dd0-lane8"></a>

* Since ~15:00 on 2026-10-30 every `space_jump` / `space_seed_profile` run on lane 8 had a caught
  guest fault `0xC0000005 at 0x00E98DD0` (labelled cPlanetGfx_Create+0x8F0; a null from a virtual
  getter in 0xE98D90, gfx/effects code) ~1.4 s after the host's ship appeared in the NPC system the
  guest had jumped to; also with main 4dbacc5 (lab 20261030-151438). Lane 9 is clean with the same
  code. Reproduce: `tools/lab.py run space_jump --lanes 1 --skip 1,2,3,4,5,6,7,9,10`.
* 2026-10-30 (fix/w14-space): FIXED. A host NPC ship model (from the host's Pollination.package)
  the guest's game lacked: docs/re/space.md#space-mirror-model. Test `coop_space_mirror_model.py`.
* Ticket: BUG-0323.

## Civilization: a guest's gift or command is refused now and then in play_civ (2026-10-30, fix/w14-tribe-civ)
<a id="issue-civ-rejected-commands"></a>

* What happens: `play_civ.py` seeds 11 and 5: 1-2 guest commands rejected by the host per 10 min, once
  a gift of one vehicle that did not reach the host.
* Reproduce: `SPORE_LANE=N python3 tools/tests/play_civ.py --minutes 10 --seed 11` (2 of 3 runs).
* Known: the host logs each rejection with its kind since fix/w14-tribe-civ.
* Not checked: which command kinds, and whether the vehicle changed owner or died first.
* Ticket: BUG-0325

## Tribe: a rejoined guest lacked 6 of the host's NPCs (2026-10-30, fix/w16-tribe)
<a id="issue-tribe-rejoin-npcs"></a>

* What happens: after a guest's rejoin in Tribe (final lab 20261030-175722 campaign_tribe#1) it had
  75 % of the host's nearby objects, 6 NPCs missing, for the last ~50 s of the play.
* Reproduce: not reproduced (3 replays of seed 612535391, `coop_tribe_rejoin_npcs.py`).
* Known: suspected window while the guest loads the host's world (docs/tickets BUG-0328). Not reproduced in 9 rejoins
  (2026-11-06, fix/w45-tribe): ticket status not-reproduced.
* Fixed (2026-11-08, fix/w46-n2-desyncs): the state it shows (a guest without proxies of NPCs the host counts as
  known) is what npc.resync (BUG-0487, 0.7.4) repairs; shown in Tribe by npc_resync_tribe (SPORE_FIX_OFF=npc.resync
  fails). Ticket status fixed (0.7.4).
* Ticket: BUG-0328

## Tribe: in a long campaign every guest shows tribe members 4-6 m off (2026-10-30, fix/w16-tribe)
<a id="issue-tribe-members-offset"></a>

* What happens: campaign#3 of labs 20261030-104149 and 20261030-175722: kind t p90 3.9-5.9 m on all
  guests at every check of tribe/play (members varying), sometimes an animal too.
* Reproduce: not reproduced with the tribe fixture (2 and 4 players, `coop.tribe spawn 12`) or the
  campaign_tribe replay; only in the full campaign's own evolved world.
* Known: the guests agree with each other; the campaign now prints the worst member's lines.
* Ticket: BUG-0329

## Creature: a guest's avatar ~14 m from where it joined (2026-10-30, fix/w17-creature)
<a id="issue-guest-avatar-drift"></a>

* What happens: in 2 of ~60 `team` runs the guest's avatar stood ~14 m from where it joined, both
  times at the same spot (-159 -471 -86 on the creature fixture), a few seconds after its own
  creature was put on it.
* Reproduce: `tools/lab.py run team --repeat 10` (cold); the log line "players N m apart before the
  regen check" with N > 5.
* Known: not the debug hit, not npc.kinematic, not the editor wait (BUG-0330 analysis).
* Ticket: BUG-0333.
* NOT REPRODUCED (2026-11-07, fix/w46-p4-creature): on rel/0.7.11 the guest no longer swaps to its newest own creation in a
  world it never played it in (BUG-0345, BUG-0638); with the swap forced (`SPORE_FIX_OFF=creations.guest_world_species`)
  16 runs (team, `tools/lab.py run guest_spot,guest_spot_swap`) kept the guest within 0.4 m. Closed as not-reproduced.
## Tribe: the village ground is up to 5 m higher or lower on the guests (2026-10-30, fix/w17-tribe)
<a id="issue-tribe-village-ground"></a>

* What happens: after the host's live Creature -> Tribe the guests (who load its save) have the ground
  around the village up to 5 m higher or lower; members and animals stand on each game's ground
  (drive.own_height, BUG-0329), so they look right, but the ground itself differs.
* Reproduce: the full campaign (cell -> tribe); not on flat ground (creature_cadian fixture).
* Ticket: BUG-0332
* FIXED (2026-11-06, fix/w45-village-ground, unreleased): the host's live render of the Tribe start's terrain stamps
  differs from the render every load gives; the host now loads its own share save after the live change
  (tribe.host_reload). docs/re/tribe.md#tribe-terrain-stamps. Reproduced on creature_cadian with ground maps
  (`tools/lab.py run tribe_village_cadian`).

## Cell: a guest's cell broadphase left ~1024 objects open (2026-10-31, fix/w20-cell)
<a id="issue-cell-sap-broken-order"></a>

* What happens: in lab 20261031-054758 campaign#1 a 4-player guest (927 cells, 900 mirrors) ended
  every broadphase sweep with ~1024 objects still open (EDX at its faults 0x3F9-0x407): more than
  its live cells, so the endpoint arrays held entries of removed objects or a broken order. That
  overran the game's open list (BUG-0337, fixed by `cell.sap_sweep`: no fault any more). The broken
  order itself may still give extra collision pairs.
* Reproduce: not reproduced (three replays of that Cell stage, seed 755764009, lanes 5 / 6: 0 left
  open, 0 duplicate entries). `coop.pool sap` shows the state; with the fix the game logs "N
  broadphase objects left open by the sweep (BUG-0337): sap: ..." when it happens.
* Known: docs/re/cell.md#cell-sap-active-list (RemoveObject marks entries by the indices stored in
  the object record; stale indices would leave entries behind; the pending sort never says equal).
* Next steps: read the logged report the next time a campaign hits it.
* Ticket: BUG-0337 (follow-up)
* CORRECTION (2026-10-31, fix/w22-cell): explained and fixed by BUG-0348: a guest in its pause menu
  kept creating mirrors, the broadphase's pending arrays overflowed (docs/re/cell.md#cell-sap-pending).

## Cell: a guest faults at 0x00E4E384 next to a pack of 1100 host cells (2026-10-31, fix/w20-cell)
<a id="issue-cell-advect-fault"></a>

* What happens: 161 faults at 0x00E4E384 (advect grid query, a garbage output pointer) on a guest
  mirroring 1100 host cells packed on one 40 m line; once in two runs.
* Reproduce: `coop_cell_sap.py` with the guests left next to the host (lab 20261031-075645-fix-w20-cell #1).
* Known: BUG-0338 (stack arrays of the advect grid, not analysed). Also on the host (0x00E4DBD9, 1332
  faults, lab 20261031-124047-fix-w22-cell): `cell_sap` fails now and then because of it.
* Ticket: BUG-0338
* CORRECTION (2026-10-31, fix/w23-cell): fixed (BUG-0338, `cell.particles`): the soft-body particle
  pool of 1500 got full; docs/re/cell.md#cell-particle-pool.

## Creature, 4 players: proxies 511 m from host NPCs at the planet's centre (2026-10-31, fix/w21-creature)
<a id="issue-npc-at-centre"></a>

* What happens: four host NPCs of one herd at (0 0 0) still updated by the host's game; one guest
  kept their proxies on the surface (census "511 m apart") and missed two other NPCs 81-88 m away.
* Reproduce: seen once (lab 20261031-083425 campaign#2, after that guest rejoined, died 170 m from
  the host and respawned at the nest); not reproduced by the pool call or a pinned NPC.
* Known: docs/re/creature.md#creature-npc-at-centre; the host logs such NPCs now.
* Ticket: BUG-0339.
* CORRECTION (2026-11-06, fix/w45-leftovers): fixed (`npc.frozen_pooled`): NPCs the host's game pools while it
  stands still were never despawned; docs/re/creature.md#creature-freeze-edges.

## Cell: the view is off centre after a co-op small respawn (2026-10-31, player report)
<a id="issue-cell-respawn-camera"></a>

* What happens: after a knockout and "respawned small next to <partner>" the host's view stayed off
  the cell.
* Reproduce: not tried; knock the host out with a partner, wait for the 10 s respawn.
* Ticket: BUG-0343
* CORRECTION (2026-11-04, fix/w41-cell-left): fixed (BUG-0343, `cell.camera_snap`): the cell camera's target
  follows a stopped cell only while it swims; every jump of the own cell by the mod now resets the camera as the
  game's own jumps do (docs/re/cell.md#cell-camera).

## All: the watchdog dumps stacks while the game is minimized (2026-10-31, player report)
<a id="issue-watchdog-minimized"></a>

* What happens: `WATCHDOG (no frames ...)` with a stack dump each time a player's game is minimized
  (main thread in Canvas::Tick's minimized sleep); harmless, noisy in reports.
* Ticket: BUG-0347
## Cell: after a world reload a guest starts at the pool's start, far from the host (2026-10-31, player reports)
<a id="issue-cell-reload-own-cell"></a>

* What happens: after a reload of the host's Cell world the guest starts at the pool's start, far
  from the host. The part where the guest kept the world's cell (BUG-0349) is fixed on
  fix/w22-reports (`creations.cell_reapply`).
* Ticket: BUG-0352
## Cell: a guest's bites show no health ring of the host's creature (2026-10-31, player report)
* What happens: a guest with an own cell (choices.ini or its newest creation) gets it applied only at
  the first world load of a session; after the next load (the host's evolution, a manual share) its
  game keeps the world's cell while the host draws the guest's own one. The guest's next own editor
  then edits the host's species.
* Reproduce: guest with an own cell, host loads a Cell world, then shares it again; guest log
  `own avatar cell model <own> -> <world's> ... (DIFFERENT)` without `level rebuilt`.
* Known: `creations.cpp apply_own_cell` skips a key equal to `g_cell_set`, cleared only at the
  session end (code). Observed in two sessions of the 2026-10-31 reports.
* Next steps: re-apply after every load once the Cell game has settled (BUG-0342).
* Ticket: BUG-0349 (BUG-0352, the guest left at the pool's start after such a load, is fixed).

## Cell: no hurt or eating animations across machines (2026-10-31, player report)
<a id="issue-cell-guest-bite-ring"></a>

* What happens: a guest's bite on a host creature shows the health ring since BUG-0350, but not the
  creature's hurt animation (the host plays it); the host's picture of a guest (its puppet) shows no
  eating or biting.
* Reproduce: guest with a weapon cell bites a creature next to the host (coop_cell_bite_ring.py setup).
* Known: docs/re/cell.md#cell-guest-bite-ring (the hurt animation's event 2 runs the after-hurt
  reaction, not wanted on a passive mirror; puppets get no animation from the mod).
* Ticket: BUG-0350 (the ring fixed, the animations open). Open question "a bunch of DNA" after a
  cell editor: BUG-0351 (the log now has the numbers).

## Tribe: the host froze 0.25 s into Tribe after a heap fault freeing a tree's grid node (2026-10-31, lab campaign)
<a id="issue-host-heap-creature-tribe"></a>

* What happens: in a 4-player campaign the host faulted at `GeneralAllocator_FreeInternal+0x64`
  (from 0x702C44) 0.25 s after the game entered Tribe, while a world share was waiting to save
  again; the host then froze (no frames). The guests were fine.
* Reproduce: not reproduced. Seen once in 72 Creature -> Tribe changes in the lab's logs (lab
  20261031-185843 campaign#3, seed 132463235). Stress: `tools/lab.py run campaign_ct,campaign_tribe
  --lane N --repeat N --retry 0`.
* Known: the flora rebuilds its trees as Tribe begins and frees their spatial-grid nodes; one node's
  heap header held the float 0.51 (written over by someone earlier). Not today's Cell switches (Cell
  only code, host left Cell 11 min before). docs/re/tribe.md#tribe-flora-grid-free.
* Not checked: who wrote the float; whether the heap was damaged in Creature or in the editors.
* Next steps: a heap walk tripwire (PPMalloc chunk sizes) run once a second in a stress campaign
  would date the damage.
* 2026-11-05 (fix/w45-crashes): not reproduced in 5 more Creature -> Tribe changes (2 x campaign_tribe, 3 x
  campaign_ct, SPORE_DETECTOR=20; none took the original path, a share save during the change); the signature is in no other lab log; the grid's slot layout is
  in docs/re/tribe.md#tribe-flora-grid-slots.
* 2026-11-08 (fix/w46-n2-desyncs): the original path forced (`CAMPAIGN_SHARE_SPAM=1`, suite campaign_tribe_share: the
  host's share posted every ~1.5 s through the change) and reached 4 times in 12 four-player Creature -> Tribe changes
  (2 with Cell first, 4 over SPORE_NET=players): 0 faults. Ticket closed as not-reproduced; the tripwire idea stays.
* Ticket: BUG-0359

## Creature: running NPCs probably slide without a walk gait on guests (2026-11-01, fix/w29-puppet-move)
<a id="issue-proxy-no-gait"></a>

* FIXED (2026-11-01, fix/w30-npc-walk): measured (gait 3-5 % of the moving frames on screen) and fixed by
  `npc.walk` / `npc.walk_tribe` (docs/re/creature.md#creature-npc-walk).

* What happens (inferred): `npc.kinematic` teleports the proxy of a running host NPC onto the host's
  path every frame; a creature teleported every frame has no walk gait (measured on the partner's
  puppet: gait state 0 in 97-100 % of its moving frames), so it slides.
* Reproduce: `creature_npc_dash` with `coop.track` extended to proxies (not done).
* Known: teleport (vt 0x44) sets +0xB5D and restarts the gait; `game::place` (teleport, then
  +0xB5D = 0) keeps it (docs/re/creature.md#creature-puppet-walk).
* Next steps: the `creature.puppet_walk` technique (MoveTo ahead + speed, `game::place` every frame).
* Ticket: BUG-0361

## Lab: two runs started in the same second share one run directory (2026-11-01, fix/w29-puppet-move)
<a id="issue-lab-run-id"></a>

* What happens: `out/lab/<YYYYMMDD-HHMMSS>-<branch>` is the same for both runs; the second run's
  `summary.md` / `results.json` replace the first's.
* Reproduce: start two `lab.py run ... --branch <b>` in one second.
* Next steps: a suffix when the directory exists. Meanwhile start parallel runs a few seconds apart.
* Ticket: BUG-0362

## Tests: creature_npc_dash fails about 1 in 3 runs (2026-11-01, fix/w29-puppet-move)
<a id="issue-npc-dash-flaky"></a>

* What happens: p90 2.4-6.4 m against the 1.2 m limit in 4 of 9 runs, also with the partner puppet
  driving of 0.7.3.2 (`SPORE_FIX_OFF=creature.puppet_walk`); other lanes were busy.
* Reproduce: `tools/lab.py run creature_npc_dash --repeat 3 --retry 0`.
* Not checked: whether main fails as often on an idle machine; the cause.
* Ticket: BUG-0363

## Creature: eggs at other species' nests differ between host and guest (2026-11-01, fix/w30-npc-walk)
<a id="issue-nest-eggs"></a>

* What happens: the nest food objects (noun 0x2C9CC91, most likely the eggs) are each game's own; their
  numbers differed at 3 of 3 nests (14/13, 6/9, 12/8); at one nest 4 of them were 3-4 m over the ground on
  both games. A player saw eggs "desynced and flying mid air".
* Reproduce: `SPORE_LANE=N python3 tools/tests/coop_nest_objects.py` (lists both games' objects per nest).
* Not checked: that these objects are the eggs; why some float.
* Ticket: BUG-0384

## Creature: corpses reported out of sync (not reproduced) (2026-11-01, fix/w30-npc-walk)
<a id="issue-corpses"></a>

* What happens (player report): bodies of killed NPCs in other places or states on the two machines.
* Reproduce: not reproduced; `coop_npc_corpse.py [--by guest]` compares the bodies (0.00 m apart in 6 kills).
* Not checked: a corpse eaten on one machine, NPCs killed by NPCs, low frame rates.
* Ticket: BUG-0383

## Tests: puppet_walk fails when the observer runs below ~40 fps (2026-11-01, fix/w30-npc-walk)
<a id="issue-puppet-walk-low-fps"></a>

* FIXED (2026-11-01, fix/w30-npc-walk): duplicate of BUG-0394; causes and fixes in
  docs/re/creature.md#creature-walk-30fps (creature.puppet_path, npc.path, the tests' timing and windows).

* What happens: at 25-37 fps the walked puppet moves at 0.6-1.4x the walker's speed in 76-82 % of the
  0.1 s windows (limit 85 %); at 42-55 fps 93-99 %. Seen while other lanes ran games.
* Reproduce: `tools/lab.py run puppet_walk` with other lanes busy (`PUPPET_WALK_CMDS="frame.limit 33"` not tried).
* Ticket: BUG-0387
## Tribe: a walking mirror held back on the guest lurches to catch up (2026-11-01, fix/w30-tribe-walk)
<a id="issue-tribe-mirror-held"></a>

* What happens: seen once by tribe_walk_mobile (30 fps, mobile network): a guest's mirror walking at
  9 m/s fell 3.6 m behind its target in 0.8 s although it was placed on the path every frame (its game
  pushed it back), then moved ~1 m per frame for three frames to catch up. Cause not found (an obstacle
  on the guest is the guess).
* Reproduce: `tools/lab.py run tribe_walk_mobile --repeat 5 --retry 0`, look for "no jump" failures.
* Seen again (2026-11-06, fix/w45-remote-bodies-2): the detector's trail rows (BUG-0609) caught one member mirror
  over 2 m off for 2.34 s in play_tribe (out/lab/20261106-071756); what holds it is still not found.
* Ticket: BUG-0386
## Creature: after an editor a guest asks the host for its own previous creature and gets nothing (2026-11-01, player report)
<a id="issue-own-old-species"></a>

* What happens: right after the host's evolution (both players evolved), the host's world share
  still held a creature of the guest's previous creature (its key on the host, 7CA388E5). The guest
  asked the host for that species ("creations: player 1 asks for species 7CA388E5: not found here" on
  the host), could not build that NPC ("npc 142: proxy spawn failed (unknown species)") and the
  sync-check reported it missing for one check; the next world share fixed it (OK 30 s later).
* Reproduce: not tried. The player's logs: the guest's own editor gave it a new creature
  (7CC73097), the host had registered the old one (7CA388E5) for player 1 and no longer finds it;
  the host removed "a creature of species 7CA388E5 ... from a save made while a partner's puppet
  existed" at the same moment.
* Known (inferred): the host's `records_from_game` finds only creations still registered; a
  partner's replaced creature is not, while a leftover of its puppet can still be in the world.
* Not checked: whether the leftover NPC is the guest's old puppet or a herd member made from it.
* Ticket: BUG-0370
* FIXED (2026-11-01, fix/w31-creature-gate, unreleased): reproduced (`coop_old_species.py`: the guest
  switches its creature, the host's game still makes creatures of the old key); the host now keeps the
  records of the players' last creatures and answers from them (creations.old_species,
  docs/re/creations.md#creations-old-species).

## Editor: the game runs out of address space in the skin painter (2026-11-01, player report)
<a id="issue-editor-skin-painter-oom"></a>

* What happens: the host crashed in its creature editor at RenderWare::Raster::Fill+0x7 (a null
  raster, read of 0x4) from the skin painter (Skinner::cSkinPainterPipeline::ExecutePipeline) after
  55 minutes of play: 1554 of 2047 MB of the game's address space used (493 MB free), large-address
  flag off; 12 s before, 868 MB were free (largest block 346 MB). The editor before it had taken
  ~180 MB (939 -> 763 MB free, largest block 413 -> 181 MB).
* Reproduce: not reproduced; a long session (Cell > Creature, many editors) on a 32-bit game
  without the 4GB patch.
* Known: no coop.dll frame in the chain; the mod does not hook the skin painter or the texture
  manager; the host's own creature had no partner puppet in the editor. Inferred: the game's own
  allocation of a paint raster failed (the game does not check it). The mod's warning
  (`mem: the game's address space runs low`) fires at < 300 MB free or a block < 48 MB and was not
  reached at the last minute line.
* Next steps: measure the address space the editor's paint mode takes on lane 10; tell players with
  the large-address flag off to use the 4GB patch.
* Ticket: BUG-0371
* CORRECTION (2026-11-03, fix/w40-crashes): ~ superseded: the same host crashed twice more in 15
  minutes on 0.7.3.4 with 868+ MB free; the dumps show a texture the texture manager returned without a
  raster (the driver did not make it), not an allocation of ours or of the address space
  (docs/re/graphics.md#graphics-null-raster). Guarded (game.null_raster_fill, BUG-0454): the fill is
  skipped; why the driver refuses that texture is still not known.
## Creature: the partner's jumps are mostly not shown (2026-11-01, fix/w30-puppet-jump)
<a id="issue-puppet-jumps"></a>

* What happens: when the partner's creature leaves the ground (a jump, a fall), its puppet placed
  in the air every frame (0.7.3.3) is put back on the ground by the game while it is grounded: in
  21-22 of 24 simulated hops it stayed on the ground, in 2-3 it fell with the partner.
* Reproduce: `coop_puppet_walk.py --dir guest --jumps 0.6` (hops by `coop.abilities lift 1.5`; the
  fixture's creatures cannot jump), compare the walker's and the puppet's airborne episodes in the
  track json (docs/re/creature.md#creature-puppet-ground).
* Known: the game keeps a grounded creature (+0x137 = 1) on the terrain; the partner sends no
  airborne flag (PF_JUMPING is never set).
* Next steps: send the airborne state and put the puppet into the game's fall (or play the jump)
* FIXED (2026-11-04, fix/w41-puppet-look, unreleased): the partner's game sends PF_JUMPING while its creature is off
  the terrain; the puppet is placed in the air and marked not grounded (`creature.puppet_air`, suite `puppet_air`).
  Not checked with a creature that can really jump (the fixture's cannot).
  on the observer; check with a creature that can jump.
* Ticket: BUG-0376

## Space: a fast partner ship is drawn off its path for a moment when it turns or stops (2026-11-01, fix/w31-space-gate)
<a id="issue-space-fast-overshoot"></a>

* What happens: in the galactic core's system the guest's ship (moved by the core's orbit at up to
  ~210 m/s) was drawn on the host 20-31 m away from every point of its own path over the 0.5 s
  before, for ~0.2 s, while it stopped (lab 20261101-100032, 11 of 84 `coop.fleet track` rows; the
  run before: within 0.1 m at 207-235 m/s).
* Reproduce: `lab.py run space_finale_wait`, the printed line `core: the guest ship on the host`
  (not checked by the test).
* Known: the pin itself is right (the host's still ship is drawn within 0.0 m on the guest);
  inferred: the partner interpolation (extrapolation up to 0.25 s with the last velocity) or late
  snapshots during the host's cinematic.
* Not checked: a frame-by-frame look at the received states around the moment.
* 2026-11-04 (fix/w42-space-life): not a drawing error (BUG-0388): the console cut the track dumps at 8 KB, the
  guest's path had gaps and the check measured against the chord across them; with complete rows the ship is
  drawn within 0.17 m (docs/re/space.md#space-fast-ship-track). SporeKit `kit.fix longout`.
* Ticket: BUG-0388.
## Tests: the creature test's hit lands on the host's family and is dropped (2026-11-01, fix/w30-tribe-walk)
<a id="issue-creature-test-party-hit"></a>

* What happens: coop_two_instances.py (suite `creature`) sometimes hits an NPC of the host's family; since
  BUG-0374 the host drops a partner's hit on it, and the check `hits applied 0 -> 1` fails (2 of 4 runs).
* Reproduce: `tools/lab.py run creature --repeat 4 --retry 0`; a failing run prints `host hp 10.0 -> 10.0`.
* Ticket: BUG-0390

## Lab gate: sync-check "p: 1 apart" in Tribe while the view jumps (2026-11-01, fix/w30-tribe-walk)
<a id="issue-tribe-p-gate"></a>

* What happens: chaos_tribe and tribe_walk_mobile fail on the sync-check gate with only the players' RTS
  point apart (p0 20-70 m), every member and animal matching; the tests move the view by the minimap.
* Reproduce: `tools/lab.py run chaos_tribe,tribe_walk_mobile --repeat 3 --retry 0`.
* Ticket: BUG-0393

## Creature: nest eggs "flying mid air" not reproduced (2026-11-01, player report, fix/w32-nest-eggs)
<a id="issue-nest-eggs-floating"></a>

* What happens: a host's note (0.7.3.3) said the eggs at other species' nests were "bugged (flying mid
  air)". The desync part is fixed (`nest.eggs`, BUG-0384: the guest had none of the host's eggs).
* Reproduce: not reproduced. On lanes 9/10 every egg lay on the ground or in the nest bowl (0.74-1.2 m
  over the terrain: the nest model, screenshots of both games); a freshly laid egg can be ~1 m up for a
  few seconds and then falls (physics). The objects 3-4 m up seen on fix/w30-npc-walk were cFruit
  (noun 0x2C9CC91, hanging fruit at the same places on both games), not eggs.
* Known: NPC eggs are laid where the layer stands, not snapped (cHerd::LayEgg from 0xD6CDB0); a hatch
  or an eat snaps them onto the collision surface (docs/re/creature.md#creature-nest-eggs).
* Not checked: the guest's own eggs while its game runs its NPCs (the host in the main menu: no
  snapshots, so the guest's own herds may lay eggs nobody else has); eggs laid by a layer on a slope,
  rock or in the water; the player's world.
* UPDATE (2026-11-01, fix/w35-egg-kick, BUG-0443): the floating is reproduced and fixed on the host
  (`nest.egg_ground`): an asleep egg has no gravity, and the eggs the game lays settle up to 0.5 m over
  the surface (docs/re/creature.md#creature-egg-gravity). Still open: eggs laid while the host is far
  from the nest (a guest keeps the herd running) lie inside the nest mound on the host, on the mound on
  the guest.
* Also open: a guest eating a host egg does not change the guest's relation to that species (the
  host's eat node does that for the avatar only).
* Ticket: BUG-0384

## A world transfer to a guest that loads resends most of its packets (2026-11-01, feat/w33-tests-session)
<a id="known-transfer-resends"></a>

* Seen: rejoin_race_long on a clean loopback link: while the host sent its world (3.4 MB) to a guest whose
  frames stalled in a load, `resends=4673/6838` and `pending=600` on the host's partner line, 324 KB/s
  instead of ~2.6 MB/s. The transfer completed every time.
* Reproduce: `SPORE_LANE=N python3 tools/tests/coop_rejoin_loop.py --rounds 40 --race 1:9`, the host's
  trace lines around each `world: sharing`.
* Ticket: BUG-0405

## Tribe: the player tribe's citizens trail on the guest in play (2026-11-01, feat/w33-tests-session)
<a id="known-tribe-citizens-trail"></a>

* Seen: play_tribe, citizens on the guest p90 1.89 / 1.98 m from the host's (limit 1.5 m) in both runs;
  animals 1.0 m.
* Reproduce: `SPORE_LANE=N python3 tools/tests/play_tribe.py --minutes 10 --seed 789470738`.
* Ticket: BUG-0410 (fixed on fix/w45-remote-bodies, unreleased: drive.lead, the 0.2 s mirror delay was the trail;
  docs/re/tribe.md#tribe-remote-lead)

## Creature: a guest that left, or rejoins while the host edits, has no creatures (2026-11-01, feat/w33-tests-session)
<a id="known-empty-planet-after-leave"></a>

* Seen: join_during_host_editor: after its Leave the guest plays its copy without any creature (the host's 20
  proxies destroyed, none kept as its own NPCs); rejoining while the host is in its editor, `0 of 0`
  creatures until the host is back.
* Reproduce: `SPORE_LANE=N python3 tools/tests/coop_join_during.py --case editor`.
* Ticket: BUG-0412

## Creature: the guest loses the host's NPCs under the brain-upgrade caption (2026-11-01, feat/w33-tests-session)
<a id="known-npcs-lost-in-caption"></a>

* Seen: transition_net (1 of 2 runs): after the host's AddEvolutionPoints 3000 the guest's mirrored NPCs went
  13 -> 1 -> 0 under the caption while the host kept 13; sync-check `n: 15 missing on the guest`.
* Reproduce: two games on the creature fixture, `AddEvolutionPoints f:3000` on the host, `coop.npc` on the guest.
* Ticket: BUG-0413
* CORRECTION (2026-11-06, fix/w45-leftovers): the loss after a freeze is BUG-0611 (the guest's stale rule counted
  the frozen time), fixed (`npc.thaw_stale`); docs/re/creature.md#creature-freeze-edges.

## Crash: hosting before the main menu (2026-11-01, feat/w33-tests-session)
<a id="known-host-before-menu"></a>

* Seen: a test host whose world load failed was told `cheat coop host` while its game was still starting
  (game mode none); 3 ms later it crashed at 0x01020F35 (TimeOfDay_Unavailable, read 0x10) from the host's
  time-of-day sync.
* Reproduce: `cheat coop host <port> Alice` on a game before its galaxy menu is up.
* Fixed on feat/w33-tests-session (core.tod_guard): no time-of-day read before SpacePlayerData exists.
* Ticket: BUG-0433

## Civ: a vehicle trails under loss bursts (2026-11-01, feat/w33-tests-session)
<a id="known-civ-vehicle-bursts"></a>

* Seen: sync_burst / sync_fps15burst, `civ/vehicles` p90 4.3-7.3 m (limit 3 m), one vehicle sampled.
* Reproduce: `SPORE_LANE=N SPORE_NET=burst python3 tools/tests/coop_sync.py`.
* Ticket: BUG-0438
## Tribe: one animal 8-25 m apart on every guest for minutes (2026-11-01, lab log gates, fix/w32-tribe-sync)
<a id="issue-tribe-npc-apart"></a>

* What happens: in long 4-player campaigns one NPC animal stands 8-25 m from where the host has it, on
  every guest, for up to 10 minutes (sync-check `n: 1 apart (n327 24.0)`); in the clearest run it began
  6 s after the co-op epic came.
* Reproduce: not reproduced on lane 7 (play_tribe, chaos_tribe, tribe_coop, campaign_tribe once); seen in
  recorded `campaign` runs (out/lab/20261031-024148 campaign#1).
* Known: the three kinds and the run ids in BUG-0402; inferred: the epic's proxy does not keep up.
* Not checked: which NPC it was; since fix/w32-tribe-sync a MISMATCH names it on each game (`sync-check:
  nNNN here: ...`).
* Next steps: the next campaign run with the gates; the proxies' walking (npc.cpp) is reworked on another
  branch.
* Update (2026-11-01, fix/w33-proxy-ground): not reproduced on the current code (campaign_tribe 4 players, play_tribe
  10 min with the co-op epic, tribe_epic: no `n apart`); every recorded case predates npc.walk_tribe (BUG-0361). An
  epic hit now and then kept its proxy within 2.6 m with both drivings. Ticket status not-reproduced.
* Ticket: BUG-0402
## Creature: the guest's own creature under the terrain after the species swap (2026-11-01, feat/w32-monitor-diag)
<a id="issue-creature-own-under"></a>

* What happens: when the guest's own creature replaces the host's species as its avatar, it stood
  0.94 m under the terrain (own trace line `agl=-0.94`) in one warm creature test on lane 6.
* Reproduce: not reliably; the lab's ground rule reports it (allowed in the lab).
* Ticket: BUG-0400
* Fixed (2026-11-01, fix/w33-unlocks): not the swap. The `team` test's `coop.npc gopos` to host x + 1.5 m
  (a chord of the planet, 0.5 m nearer its centre there) put the creature under the terrain, where the
  game leaves a grounded creature; gopos now puts it onto the ground (`tools.gopos_ground`), the lab
  allowance is gone.

## Creature: the host's part unlocks never reach the guests (2026-11-01, feat/w32-monitor-diag)
<a id="issue-creature-unlocks-sync"></a>

* What happens: parts the host unlocks during a session stay locked in the guests' games (their own
  editor offers the collected parts of the save they loaded).
* Reproduce: `coop.parts unlock 3` on the host, `coop.parts` on both (monitor_plant does it).
* Ticket: BUG-0401

## SporeKit: a function hooked after boot can catch a thread in its first bytes (2026-11-01, fix/w33-null-call)
<a id="issue-late-first-hook"></a>

* What happens: nothing seen yet. The first hook of a function writes MinHook's 5-byte jmp without
  stopping other threads (BUG-0202); hooks made at boot are safe, but the devkit input hooks
  (GetAsyncKeyState / GetKeyState at the first injected input, test games only) and coop's Cell
  SimUpdate / GetModelKey hooks (at the first cell, main thread) are made while the game runs.
* Reproduce: not reproduced; `kit.test.hooknew` avoids this race on purpose (targets start with a
  5-byte jmp). Its first version, whose targets started with two short instructions, crashed at an
  eip 4 bytes into a target (lab 20261101-134603 / 134623-fix-w33-null-call).
* Known: inferred from the code; the window is nanoseconds per call.
* Next steps: make the input hooks at boot or on the main thread.
* Fixed (2026-11-05, fix/w45-crashes): an int3 gate and a 50 ms window before the bytes are written
  (`kit.fix hookfirst`, test `kit_hooksplit`, docs/02-modkit.md#modkit-hook-first-write).
* Ticket: BUG-0403

## Creature: the partner's creature slides in its standing pose for a whole walk (2026-11-01, fix/w34-walk-stable)
<a id="issue-puppet-no-walk-anim"></a>

* What happens: on the host, the guest's creature moves on its path but keeps its standing animation (02481DE5,
  walk gait 0) for a whole 25-40 s walk.
* Reproduce: about 1 run in 10 of the cold tests puppet_walk / puppet_slope / puppet_under (host watches guest).
* Resolved (2026-11-04, fix/w41-puppet-look): in all three recorded runs the observer's camera was far away (a 30 m
  walk spanned 20-30 pixels, ~1 px/m; 25-90 px/m in the passing runs): its game animates a creature that far without a
  walk gait, its NPC proxies alike. A test artifact of a cold start, not the driving (inferred: the camera's state at
  that moment was not read). puppet_walk now judges the gait near the camera only (`coop.track` column ppm).
* Ticket: BUG-0436

## Creature: a hit animal is held off its path on the guest for a second (2026-11-01, fix/w34-walk-stable)
<a id="issue-npc-hit-held"></a>

* What happens: after the host hits an animal, the guest's game moves its copy 3-4 m in one frame (or throws it at
  8-11 m/s) against the placing; it stands 4.8 m off for 1.1 s or is put back 2.4-4 m in one frame.
* Reproduce: npc_walk's flee phase (`coop.npc hosthit`), a few runs in 20.
* Not reproduced (2026-11-06, fix/w45-remote-bodies-2): the guest's game never throws a living proxy (locomotion mode
  column of coop.npctrack); after a push the copy is up to ~4 m off for under 0.2 s (the host's throw seen one state
  period late), never held (body_trail_hits, 13 runs; docs/re/creature.md#creature-hit-throw).
* Ticket: BUG-0409

## Creature: the partner's creature slides past a stop (2026-11-01, fix/w34-walk-stable)
<a id="issue-puppet-stop-slide"></a>

* What happens: when the partner stops, its creature on the other screen slides 0.3-0.7 m past the spot in ~2 % of
  the stops; on a slope it may play the fall animation for 0.2 s there.
* Reproduce: puppet_walk prints the stops (`stops: N, drawn past the stop by more than 0.3 m in M`).
* Not reproduced again (2026-11-04, fix/w41-puppet-look): 21 stops of `coop_puppet_walk.py --stop-walk` on the slope
  (guest 30 fps, `player` network), 10 of them abrupt (`coop.abilities halt`): none slid on more than 0.05 m with the
  0.7.4 driving, so the hold after a stop (creature.puppet_stop, tried) was not kept.
* FIXED (2026-11-06, fix/w45-remote-bodies-2, unreleased): seen again on both games' own positions (puppet_stops,
  0.50 / 0.88 m past the stop, lab 20261106-053105; FAIL 20261106-083248 off, PASS 20261106-083011); `creature.puppet_stop` holds the puppet where the partner stopped
  from the moment its own speed falls (see BUG-0408).
* Ticket: BUG-0408
* Fixed (2026-11-01, fix/w33-unlocks): the party shares its collected parts (`parts.shared`, test
  `parts_shared`).

## Cell: editor_desync, the guest finds no mate after the host's evolution (2026-11-01, fix/w33-unlocks)
<a id="issue-cell-guest-mate-after-evolution"></a>

* What happens: after the host evolved its cell (vote, editor), the guest's mating call shows the button
  but no mate comes (`mate=00000000`), so the guest's own cell editor does not open.
* Reproduce: `tools/lab.py run editor_desync --lane 6` (4 of 4 on 2026-11-01, also on main f0cf695).
* Ticket: BUG-0411

## Creature: a vote during the evolution's cinematic leaves the host in an editor without buttons (2026-11-01, feat/w33-tests-creature)
<a id="issue-vote-during-evolution-cinematic"></a>

* What happens: right after a vote's editors were left, the host's game plays the evolution cinematic
  F4F96E43. A second vote started then (`coop.vote editor` + the guest's ready) took both games into their
  editors; the guest left with "done", the host's editor had no buttons (no "done", no top bar), the cinematic
  stayed "playing" with the input lock on, and the host's game stayed in the editor for minutes.
* Reproduce: creature fixture, `coop.vote editor`, the guest's ready, "done" on both, and at once the same again
  (coop_editor_soak.py before it waited for the cinematic).
* Known: the creature stage's cinematic does not end in the editor mode; a deferred creature removal waits for it.
* Not checked: whether a player can start a vote during this cinematic (the mating call, a guest's vote).
* Ticket: BUG-0404
* FIXED (2026-11-01, fix/w34-vote-cinematic, unreleased): the vote's editor waits at one busy gate
  (mods/coop/busy.h: a cinematic, the input lock, the game time paused); the guest's own editor and an
  action vote's result use the same gate, the share after an evolution its cinematic part (BUG-0019). Switches
  `core.editor_cinematic`, `core.own_editor_busy`, `core.vote_action_busy`; tests `vote_during_cinematic`,
  `busy_gate`.

## Creature: the partner's creature pops 1 m away and back when it uses an ability (2026-11-01, feat/w33-tests-creature)
<a id="issue-puppet-ability-pop"></a>

* What happens: a guest at 30 fps over the `player` network sees the host's standing creature drawn ~1 m off for
  0.13 s and back while the host uses an ability (its animation 02481DE5).
* Reproduce: `puppet_fight_player` (coop_puppet_fight.py with SPORE_FPS=guest:30 SPORE_NET=player): `drawn jumps
  within the bound` fails, 2 of 2 runs.
* Not checked: which abilities, whether the frame cap or the network is needed.
* Resolved (2026-11-01, fix/w34-ability-pop): not an ability. The steps were out of the observer's view (the
  game puts a creature outside the view on the plain terrain, under a nest mound); the test now judges what is on
  screen. What remains visible: [issue-puppet-mound-entry](#issue-puppet-mound-entry) (BUG-0434).
* Ticket: BUG-0414

## Creature: the partner's creature on a nest mound is drawn 1 m low for one frame when it comes into view (2026-11-01, fix/w34-ability-pop)
<a id="issue-puppet-mound-entry"></a>

* What happens: the partner stands on the raised ground of a nest mound (up to 1 m over the plain terrain); when
  the observer turns its camera so that the partner's creature comes into view, its first frame is drawn on the
  plain terrain (up to 1 m low), the next one on the mound.
* Reproduce: docs/tickets BUG-0434 (the host on the fixture's nest mound, the guest turning its camera);
  coop_puppet_fight.py prints these steps as `entry`.
* Known: the game puts every creature of ours outside the view on the plain terrain; writes at the mod's frame
  cannot keep it up (docs/re/creature.md#creature-out-of-view-ground).
* FIXED (2026-11-06, fix/w45-remote-bodies-2, unreleased): `creature.culled_ground` lifts the puppet back after the
  physics update (puppet_mound_view: 0 low frames, 200 with it off; docs/re/creature.md#creature-culled-ground-fix).
* Ticket: BUG-0434

## Creature: on the lower part of a nest mound the partner's creature stands 0.4 m in the ground (2026-11-01, fix/w34-ability-pop)
<a id="issue-puppet-mound-sink"></a>

* What happens: a partner standing 0-0.5 m over the plain terrain on a mound is drawn 5 cm into the plain terrain,
  i.e. up to 0.45 m under the mound's surface; crossing 0.5 m it steps 0.4-0.5 m up or down.
* Cause: `creature.puppet_ground` takes the plain terrain (`GetHeightAt`, without the mound) as the ground.
* FIXED (2026-11-04, fix/w41-puppet-look, unreleased): a grounded partner 0.1-0.5 m over `GetHeightAt` is put 5 cm
  under its own height (`creature.puppet_mound`, suite `puppet_mound`).
* Ticket: BUG-0435
## Creature: parts not shared after Cell -> Creature the players' way (2026-11-01, feat/w33-tests-session)
<a id="known-parts-after-cell"></a>

* Seen: transition_net on main 6ac351e: host `parts=55`, guest `parts=49` for the whole Creature stage, no
  `parts:` log line on either game (monitor rule `unlocks`).
* Reproduce: `SPORE_LANE=N SPORE_NET=player SPORE_FPS=guest:30 python3 tools/tests/coop_transition_net.py --to creature`.
* FIXED (2026-11-01, fix/w36-parts-transition, unreleased): not a missing sync. The 6 parts were the
  creature twins of the host's Cell parts, which the live Cell -> Creature start adds to its unlockable
  map; the guest's loaded world had them unlocked in its item states (its editor offered them) and the
  census counted the map only. `parts.census_states` counts both
  (docs/re/creations.md#creations-cell-twins).
* Ticket: BUG-0441
## Tribe: walked animals step ~3 m in one frame on the guest's screen (2026-11-01, fix/w35-monitor-gates)
<a id="issue-tribe-proxy-step"></a>

* What happens: while the host walks its animals (`coop.npctrack walk`), a guest sometimes draws a walked animal
  2.6-3.1 m further in one 15 ms frame, at times several animals in the same frame.
* Reproduce: `tools/lab.py run npc_walk_tribe --repeat 3`: "no jump of a moving proxy" fails in about 2 of 3 runs
  on lane 7 (also with main's own test file).
* Not checked: whether the host's states arrive bunched (late) then, or the walk driving.
* Not reproduced (2026-11-06, fix/w45-remote-bodies-2): 8 of 8 npc_walk_tribe runs passed on 0.7.9 (see BUG-0445).
* Ticket: BUG-0445
## Civ: the mirror vehicles (and other objects the mod creates on a guest) go into the guest's save (2026-11-02, fix/w39-content-all)
<a id="issue-civ-mirrors-in-save"></a>

* What happens: the mod's save filter only knows creatures and herds, so the mirror vehicles a guest shows
  for the host's civilization are written into the guest's own save (the mod's objects, CLAUDE.md 2). They
  come back as ordinary vehicles of the loaded world without a co-op session, and nothing updates them.
* Reproduce: `tools/tests/coop_content_civ.py` steps 6-7 (save with `coop.save`, restart without a session,
  load): the reloaded world lists 2 vehicles. Seen in out/lab/20261102-213135.
* Not fixed: a strip needs the civ / tribe / space object registry and a null-safe removal from
  `civ+0xB0` / `civ+0xD4` and the city's lists (BUG-0020's null rule), with a save + reload test per stage.
* Ticket: BUG-0470
## Civ: city citizens have another size on each game (2026-11-03, fix/w40-scale)
<a id="issue-civ-citizen-scale"></a>

* What happens: the detector's `Civilization:scale:citizen` (50 % of the Civ comparisons). Each game simulates
  its own city citizens (BUG-0476): the citizen the detector pairs by position is another individual with its
  own random size (GetDesiredScale vt 0x80), not a mirror. The replicated-field engine
  (docs/sync-architecture-plan.md#sync-fields) needs a mirror to apply a size to.
* Not fixed: comes with mirrored citizens (BUG-0476): then they are one more field kind.
* Fixed (2026-11-04, fix/w41-civ-citizens): citizens near a guest follow the host's (`civ.citizens`) and get their
  size (`civ.citizen_scale`). Left: the detector's greedy pairs cross in a crowd of citizens, so a few
  `scale:citizen` rows remain without a real difference (docs/re/civ.md#civ-roads-citizens).
* Ticket: BUG-0475 (its Tribe half is fixed), BUG-0476
## Commodity nodes at scale 10 on the host only (2026-11-03, fix/w40-scale)
<a id="issue-node-scale"></a>

* What happens: after the live Cell -> Creature transition the host's game has one node (noun 0x0403DF5C,
  vtable 0x1469AC0) at scale 10.0 near the players, the guest's copy (from the shared world) 1.0
  (`Creature:scale:node` / `Tribe:scale:node`, campaign_short, campaign_ct). On the `creature` / `tribe`
  fixtures (loaded saves) all 16 nodes are 1.0 on both games: the 10 is set at runtime, not saved.
* Not fixed: who sets it and what a player sees are not known; the SetScale hook (squadron.cpp) logs
  `scale 10 set on an object with vtable ... from ...` once per caller, so the next campaign run names it.
* Reproduce: `tools/lab.py run campaign_short` with SPORE_DETECTOR=20 (detector line `scale:node`).
* Ticket: BUG-0485
* 2026-11-06 (fix/w45-placement): fixed (`wobj.node_scale`, test node_scale): the setter is the live node placement
  0xBC02F0 (return address 0xBC0641); the scale is not saved, so loads give 1.0 (docs/re/creature.md#creature-node-scale).
* 2026-11-03 (fix/w40-civ-crashes): the Civ half is fixed (`civ.save_strip`, test `civ_save_strip`):
  the guest's mirror vehicles stay out of its saves and leave no null in the civilizations' lists. Still
  open: the Tribe mirrors (SpawnNpcTribe tribes and their members) and the Space UFO mirrors / puppets
  (BUG-0495).

## Civ: roads between cities built on the host are missing on the guest (2026-11-03, fix/w40-civ-crashes)
<a id="issue-civ-roads-not-mirrored"></a>

* What happens: the detector finds `noun_only_host:inter_city_road` in 10% of the Civ comparisons: the
  host's game has a road (noun 0x02B8A4E7) between two cities, the guest's has none. Cosmetic (no
  gameplay of the guest depends on it as far as known).
* Not fixed: the code that makes a road in the host's game is not traced (docs/re/civ.md#civ-planner-lifetime);
  a fix needs that function (and its arguments: the two cities) to replay it on the guest from the
  host's snapshot.
* Fixed (2026-11-04, fix/w41-civ-citizens): the road is a trade route's object (cCity::AddTradeRoute); the guests
  add the host's routes (`civ.roads`, test `civ_roads`, docs/re/civ.md#civ-roads-citizens). Left: a route the host
  removes without a capture stays on the guest (removal not replayed).
* Ticket: BUG-0494 (split from BUG-0478, whose vehicle half is fixed: `civ.mirror_remove`).

## play_creature: a puppet once far off, no joint hunt (2026-11-03, fix/w40-effects)
<a id="issue-play-creature-puppet-far"></a>

* One sample of ~140 per player: the partner's puppet 11-177 m from where its own game has it,
  speed 0 (BUG-0499); some runs have no joint hunt or the host's creature 0.71 m under the ground
  for 2 log lines (BUG-0500). Both with and without the effect relay (out/lab/20261103-124913 in
  SporeCoop-wt/fix/w40-effects, relay off) and on main.
* Reproduce: `tools/lab.py run play_creature --repeat 4` (about 1 run in 4).
* Tickets: BUG-0499, BUG-0500
* UPDATE (2026-11-10, fix/w47-play-bots): the "no joint hunt" runs that came back after BUG-0500's fix (4 of 10
  runs on 2026-11-10) were the bots, not a puppet far from its player: BUG-0618 third round,
  docs/06-testing.md#testing-w47-play-bots (7 of 7 runs pass with the new bot).

## Creature / Tribe: world objects not yet owned by the host (2026-11-03, fix/w40-world-objects)
<a id="issue-world-objects-left"></a>

What mods/coop/worldobj.cpp does not cover yet (docs/re/creature.md#creature-world-objects):
* A fruit that fell in the host's game where the guest's tree had no fruit left in that slot (the guest's
  own fall came first) is not shown on the guest: the mechanism does not make fruit, it only binds, eats and
  drops them. Trees that grow fruit again (not seen, ~) would not be copied either. Ticket: BUG-0497.
* Plants and rocks (`game_plant` 0x18C84A9, `rock`): not a desync (2026-11-03, fix/w40-cell-evo): each game
  makes them around its own player's camera (tools/tests/coop_world_lod.py, docs/06-testing.md#testing-world-lod);
  the detector leaves them out. Tickets: BUG-0473, BUG-0498.
* The guest's home nest heal ("I don't get any heals from it", BUG-0496, split from BUG-0460) is not reproduced or analysed;
  only the extinct nests are copied.
* A Cell part a guest collects (BUG-0467, open) is not unlocked for the host: parts.shared polls the creature
  parts (cCollectableItems) in Creature only; the Cell game keeps its parts in its own state
  (`S+0x34+4*part = 2`, the token pickup 0xE5D7B0, docs/re/cell.md section 12). A Cell variant needs a poll
  of that array and the host's unlock of it with the game's own pickup path (not the world object table:
  it is a ledger, like the creature parts).
* Ornaments in Tribe: no reward handler there (no cCreatureModeStrategy), the kind runs in Creature only.

## Creature: after the host's evolution editor a guest may still show the host's previous creature (2026-11-03, fix/w40-editor)
* Report 2026-11-02 (0.7.3.4): the guest's screen showed the host's creature from before its editor.
* Reproduce: not reproduced on 0.7.4 (2026-11-03, fix/w40-cell-evo): `coop.vote evolve` does apply the
  host's evolution once the editor really adds a part (the test dragged a locked one before), every evolution
  gives the host a new species key, and the guest's game draws the host with it (`editor_sync_host`,
  `editor_sync_host2`: one and two evolutions in a row; docs/re/creations.md#creations-host-evolution).
* Still suspected, not seen: a host species changed under the same key (a key-only bundle, the guest's cached
  species profile), or a bundle the guest cannot use (another build's parts) on the reporter's PCs.
* Ticket: BUG-0457

## Creature: what fix/w40-creature-acts left open (2026-11-03, fix/w40-creature-acts)

- Creature: a picked-up and thrown stick (or rock) moves in each player's game separately (BUG-0458).
- Creature: the animals' own animations (eating, social, a nest's evolution) do not show on a guest; its copies
  only walk and stand (BUG-0464, test tools/tests/coop_npc_anims.py fails). Fixed on fix/w40-npc-anims (npc.anims,
  2026-11-03); a nest's real evolution (the herd's new generation) is not reproduced in a test.
- Creature: the sea monster ("kraken") of a player in deep water shows only on that player's screen (BUG-0465).
- Creature: a guest's roar does nothing to the animals (BUG-0501).

## Player reports of 2026-11-04 still open after rel/0.7.4 (2026-11-03, fix/w40-reports)

- ~~Galaxy menu (Steam build, no session): opening the game settings can crash the game (null resource database
  0x011AC197 at 0x6025A9) (BUG-0506).~~ Fixed on fix/w40-misc (game.save_areas).
- Creature: a guest's game can crash right after a long freeze (minimized / alt-tab) when the partner's creature
  it draws was freed (BUG-0507).
- Cell and Creature: every host evolution sends the whole world again; the guest goes to the galaxy menu and
  loads it (BUG-0508).
- Cell: a knocked-out cell can still bite and kill (BUG-0509).
- Cell and Creature: far more DNA than single player; a guest's meals land at once in the host's open editor
  (BUG-0510).
- Cell: other cells float around very fast on the guest after the cell grows (BUG-0511).
- Creature: the host's pack attacks the guest when the host targets it (BUG-0512).
- Creature: a guest whose game stops drawing for many seconds is shown sunk into the ground on the host (BUG-0513).
- Tribe: the guest's members cannot pick up tools from the host's tool buildings (BUG-0514).
- Tribe: epic creatures deal only 8 damage (unclear which side, BUG-0515).
Found by fix/w40-play (2026-11-03), not fixed:
* Tribe: an animal proxy that the guest places on the host's path can stay 3.5-8 m off the host's NPC for
  2-11 s (the lab's monitor rule `proxies`; play_tribe 1 of 3, campaign_tribe 1 of 3 in 20261103-012624).
  Ticket: BUG-0503. Fixed on fix/w43-tests (2026-11-04): on a guest at a low frame rate the placing was capped at
  0.1 s frames and fell behind (npc.proxy_slow_frames); not yet confirmed with a long session.
* Creature: under the brain upgrade caption both own creatures read 0.63-0.71 m under the ground once (monitor
  rule `ground`, 20261103-124913 play_creature#2); not reproduced at the fixture's nest. Ticket: BUG-0504. Fixed on fix/w44-height (2026-11-05) through
  creature.own_ground (BUG-0592); the caption itself not rerun.
* play_tribe: in 2 of 3 runs the guest's members stayed idle on 7-12 orders the host took, and the host's bot
  gave only 8-9 orders (its minimap look missed 24 times once). Ticket: BUG-0505.

## The game closes at start with the mod installed: the game's own start fails (2026-11-03, fix/w40-gh-issues)
<a id="issue-game-start-fails"></a>

* BUG-0521 (GitHub #193, 0.7.3.6, GOG 2024, Windows build 26300): SporeKit and the mod boot, then the game's own
  App::cSporeApp::Init returns 0 and the game ends with ExitProcess(0) at the first frame. Before it, the main
  thread sat 31 s in a modal window during App::cAppSystem::Init (a message of the game or of Windows). Cause not
  known (inferred: a damaged or partial game install or a data package the game cannot read; not checked
  whether the game starts there without the mod). From 0.7.4 SporeKit writes a report ("could not start") and
  shows a message with the checks: start once without mods (rename SporebinEP1\mods), repair the game files in
  GOG Galaxy / Steam, attach the report. To reproduce: not reproducible in the lab.

## Effects are not relayed in Tribe, Civilization and Space in 0.7.4 (2026-11-03, rel/0.7.4)
<a id="issue-effects-civ-space"></a>

* BUG-0451: in the final 0.7.4 check, effects_party_civ (3 players, Civilization) made the receiving games fault
  hundreds of times at Skinner::cSkinnerTexturePainter::StartRender+0x28 (read 0) and IO::ReadCString+0x3BA1, the
  chain EffectWorld_CreateEffect -> cSkinPainterPipeline::ExecutePipeline (out/lab/20261103-215559, both repeats).
  A relayed world effect starts the game's skin painter without its context there. So 0.7.4 relays effect starts
  only in Cell and Creature (`coop.fix effects.other_stages`, off by default; stops always pass); effects_party_tribe faulted the same way (out/lab/20261103-223036). To
  reproduce: `coop.fix effects.other_stages on` on every game, then tools/tests/coop_effects_party.py --fixture civ.
  To check for 0.7.5 (Civ) and 0.7.6 (Space): which effect ids need the painter, and whether a per-id allow list
  or the painter's context check is the right fix.
* 2026-11-04 (fix/w41-effects-stages): Tribe and Civilization fixed for 0.7.5. The relayed id was the skin
  painter's own paint effect `D723B947` (made in the painter's effect world, sent by the test's workload), not a
  game effect; such effects are never sent or played now (`coop.fix effects.main_world`) and
  `effects.other_stages` is on (docs/re/creature.md#creature-effects-painter). Space stays off
  (`coop.fix effects.space`, not verified). The fault itself did not come back on lane 4 even with the 0.7.4 build.
* 2026-11-12 (fix/w48-space): Space verified and on by default (BUG-0895: effects_party_space passes, fails with
  `SPORE_FIX_OFF=effects.space`).
## Space: the partners' UFO puppets may go into a guest's save (2026-11-03, fix/w41-tribe-world)
<a id="issue-space-objects-in-save"></a>

* What happens: the save filter knows creatures, herds, Civ mirror vehicles (civ.save_strip) and Tribe mirrors
  and made tribes (tribe.save_strip), not the Space objects the mod makes (the partners' UFO puppets). Not
  checked whether a Space save writes them at all.
* Not fixed: needs a Space save + reload test as tribe_save_strip.
* 2026-11-04 (fix/w42-space-life): fixed (BUG-0522, `space.save_strip`, test space_save_strip): a Space save
  did write them (they came back as plain Defenders of the guest's empire); now null references
  (docs/re/space.md#space-save-strip). Still open: the Creature-stage fallback puppet in the avatar's herd.
* Ticket: BUG-0522
## Tribe: tools stand elsewhere on each game; an emptied village keeps its hut (2026-11-03, fix/w41-tribe-world)
<a id="issue-tribe-tools"></a>

* What happens: the mod syncs no tribe tool: a tool bought after the world was shared is made by each game at its
  own spot (the detector's `Tribe:unmatched:tribe_tool`). A guest's tribe the host no longer has loses its members
  (tribe.guest_tribes) but its hut, tools and food mat stay: no game function to remove a whole tribe is known.
* Ticket: BUG-0523, BUG-0483
* Update (2026-11-04, fix/w42-tribe-left): the tools are synced now (BUG-0523 fixed: every tribe's tools are the
  host's; a guest's own tools the host has not are removed with the game's RemoveTool, so an emptied village loses
  its tools too). Its hut and food mat still stay (BUG-0483). Left: a tool made by the mod is in no village layout
  slot; a guest selling a tool is not sent (the host keeps it).
## Tribe: a member stalled in the gather behaviour, the chiefs at a 4-player arrival (2026-11-04, fix/w42-tribe-left)
<a id="issue-tribe-gather-chiefs"></a>

* What happens: play_tribe saw a member in the gather node (state 1, standing, its walk goal the old tree) that did
  not act on new orders (BUG-0531); with 4 players the chiefs arrive close together and the game's idle behaviour
  brings them back next to each other (BUG-0340).
* Not reproduced with 2 players: a guest's walk order took a gathering member out of the node in 7 of 7 runs
  (tribe_gather_stall), and chiefs put in each other after the arrival were parted by the game within 14 s
  (tribe_chiefs_apart check 5). Both tests are regression checks; next step a 4-player campaign
  (`coop_campaign.py --from creature --to tribe --players 4`) and play_tribe's idle order lines (`coop.tribe ai`).
* Ticket: BUG-0531, BUG-0340
* Update (2026-11-08, fix/w46-member-stall): BUG-0531 fixed. The standing members were the host's conquest caption
  (TRG_TotemPole_Conquer) waiting for the host's OK with the game time standing; it now closes itself after 8 s with
  guests in the session (tribe.conquer_caption, docs/re/tribe.md#tribe-conquer-caption). BUG-0340 stays as above.

## Space: ornaments by a colony city in one game only (2026-11-04, fix/w42-space-planets)
<a id="issue-space-ornaments"></a>

BUG-0541: on the `space` fixture's home planet one game can have 9 ornaments (noun 0x18C88E4) next to city 0 that the
other has not (both landed, `coop.census all 3000 0 0 0`); not the camera's level of detail. Cosmetic; not traced.
## Civ: the old tribe village stands in the guest's Civ world only (2026-11-04, fix/w42-civ-left)
<a id="issue-civ-tribe-leftovers"></a>

* What happens: in play_civ on civ_npc the guest's game has the Tribe stage's tribes, huts, tools, food mats and
  bundles at the old village (-234 -34 482) that the host's game does not have (the detector's
  `Civilization:noun_only_guest:tribe*`, `game_bundle`, 7-10 % of the comparisons). How to reproduce:
  `SPORE_DETECTOR=20 tools/lab.py run play_civ`.
* Fixed on fix/w42-civ-left (civ.tribe_ruins): the host's vehicles raid the villages, the guest now removes them too.
  Ticket: BUG-0539
## Space: a city's cultural targets stand at other places in each game (2026-11-04, fix/w42-civ-left)
<a id="issue-space-cultural-targets"></a>

* What happens: the 4 cultural targets per city (city setup 0xBE7D40) are placed by each game on its own; the
  detector's `unmatched:cultural_target` in campaign_space. Not seen in play_civ.
* Not fixed. Ticket: BUG-0540
- BUG-0550: Creature: an NPC 9 m over the ground on the host is drawn on the ground on the guest (seen once in play_creature, sync-check `n: 1 apart`). Fixed on fix/w44-height (2026-11-05, npc.air).
- BUG-0551: Creature: a chat line typed while the host pauses to save the world for a returning guest can be lost (seen once in play_creature).

Player reports 2026-11-04 (0.7.5, Discord bug-reports; not analysed yet):
- BUG-0554: Creature: the guest's creature is not stunned by an NPC's charge (fixed on fix/w43-guest-player, unreleased)
- BUG-0555: Creature: NPCs bite and charge the guest's creature while it flies (glides) (fixed on fix/w43-guest-player, unreleased)
- BUG-0556: Creature: creatures the host summons with Summon Flock are not friendly (green) to the guest (fixed on fix/w43-guest-player, unreleased)
- BUG-0557: Creature: the guest cannot eat some bodies of creatures the players killed
- BUG-0558: Creature: the UFO that picks up the players at the end of Creature cannot pick up the guest (the guest does not see the ship)
- BUG-0559: Creature: parts of a pack the guest lacks (Creepy & Cute) are drawn as big blocks, and the host's colours are another shade on the guest
- BUG-0560: Cell: the host gets a lot of DNA again after going into the cell editor
- BUG-0561: Creature: an allied nest has no ally (green) icon on the guest's minimap, only the grey discovered mark (fixed on fix/w43-guest-player, unreleased)
- BUG-0562: Creature: an NPC creature backing away from the guest gets stuck inside its nest (video)
- BUG-0563: Tribe: the guest's creature from Creature stage appears as an enemy monster at the start of Tribe; killed, it leaves meat that never runs out
- BUG-0564: Tribe: after the guest dies, its map still shows villages that do not exist any more (reproduce: tribe_bounce_world, the host's game over and reload; BUG-0566, 0567, 0569 the same class)
- BUG-0565: Tribe: every village's relation is neutral on the guest, while the host sees them aggressive and slightly aggressive
- BUG-0566: Tribe: the guest does not see an enemy village's hut (hub) health go down when the players attack it
- BUG-0567: Tribe: a building is broken (bugged) on the guest after the guest dies
- BUG-0568: Tribe: no music in the guest's Tribe game (fixed on fix/w44-tribe-guest, unreleased: tribe.theme_music, the hunt / fight theme)
- BUG-0569: Tribe: the guest sees no animations (fixed on fix/w44-tribe-guest, unreleased: tribe.mirror_anims)
- BUG-0570: Tribe: no red target circle under an enemy the guest targets (fixed on fix/w43-guest-player, unreleased)
- BUG-0571: Tribe: villages are founded (spawn) much faster than in the single-player game (fixed on fix/w44-tribe-guest, unreleased: tribe.form_pace)
- BUG-0572: Tribe: the guest's food counter is 0 while the host's tribe has food (reproduce: tribe_food_zero; the guest's game spends its last food, shared.cpp cannot write into an empty bundle list) (from about an hour into the session)
- BUG-0573: Tribe: the guest's tribe members do not eat on their own, they must be ordered to eat
- BUG-0574: Tribe: two buildings stand right next to each other on the guest (fixed in 0.7.6 by BUG-0523: tribe_tools_all, 2026-11-04 replay)
- BUG-0575: Tribe: the guest crashed after double-clicking the home icon (3010a413:SporeApp.exe+0x6826F3)
- BUG-0576: Tribe -> Civ: black screen after evolving from Tribe into the city stage
- BUG-0577: After a rejoin the game shows the Cell and Creature stages as not done yet (stage progress lost)
- BUG-0578: Crash at Graphics::cModelWorld::FinishBackgroundLoad+0x2735 on 0.7.3.6 (3010a413, mode none) (not reproduced: the game's own exit with the editor held or open is clean, exit_editor_held, 2026-11-07)
- BUG-0579: Tribe: day and night differ: the guest's world is at night while the host's is in daylight
- BUG-0582: Tribe: the host's game faults at 0xD72745 every frame during its own creature attack event (lab, ~1 run in 3 of tribe_bounce_world; its game-over caption may not come then)
- BUG-0583: Tribe: a guest faulted in a citizen's Update (GetAbilityIndexByType) after leaving and joining again (lab, once in realign_tribe_rejoin)
- BUG-0580: Tribe: the NPC tribes' food differs between the host and the guest (detector `Tribe:state:tribe_food`; `SPORE_DETECTOR=20 tools/lab.py run play_tribe`)
- BUG-0584: Creature: a caught fault on the host, a call through null from 0xB097C4 (once in a 44 min `long_creature`, lab 20261104-214938)

## Content one game lacks: what the lookalike parts do not cover yet (2026-11-04, fix/w43-content)
<a id="issue-content-lookalike-gaps"></a>

* [BUG-0424](tickets/BUG-0424-class-content-parts-paints-designs-the-other-gam.md) (open, class): since BUG-0559 a
  part a guest's game lacks in a player's creature, cell or UFO, or in a species of the host's world, is shown as
  the most similar part that game has (the sender's description of the part, docs/re/creations.md#creations-lookalike).
  Not covered: civ mirror vehicles, buildings and host ships in Space (`ensure_model` builds them from a key, no
  description comes with it: the 0.7.6 tables, a part of no table becomes a fixed detail); cell and UFO parts use the
  same code but have no test; an animation that only the missing part has is not played.
* Reproduce: `content_lookalike` with the host's creature changed to use a vehicle part of `Spore_Pack_03` in a civ
  mirror (not written yet).

## Tests: what G1 of 0.7.7 left open (2026-11-04, fix/w43-tests)
<a id="issue-g1-open"></a>

* Space: sync-check MISMATCH p0 46 m while the host flies a rescue with key pulses (BUG-0547); the speed slack uses
  the speed at the read. Not reproduced or changed here.
* Space: coop_space_death once kept the ship dead 47 s after the forced jump (BUG-0193); not seen since.
* Tribe: tribe_guest_tribes' fix-off forming failed 12 of 13 times on fix/w43-world-state's lanes 4/5; the test now
  says why the controller formed nothing (bake, tribe nouns, spawn markers) and retries only while it bakes; the
  cause on those lanes is not confirmed (BUG-0587).

## Creature: the host's drama UFO tries to take the guest's creature, the guest sees no ship (2026-11-04, player report 0.7.5)
<a id="issue-ufo-puppet"></a>

* What happens: in Creature the host's game starts its UFO visit (Drama Manager event kind 2); its ship beams at the
  guest's creature in the host's game, which does not rise; the guest's game shows no ship.
* Reproduce: creature fixture, `coop.drama start 2` on the host next to the guest's creature (lab: not run with a
  partner nearby yet).
* Known: docs/re/creature.md#creature-ufo-puppet (the start posts BT stimulus 2, the abduct tool's hit 0x10323E0
  gives a beam locomotion; the puppet is placed every frame by the mod). The guest's own event is held back on
  purpose (BUG-0486).
* Not checked: how the ship picks its creature; what the game does with an abducted avatar.
* Next steps: find the target pick of cAbductToolStrategy, keep puppets out of it on the host, or relay the visit.
* 2026-11-05 (fix/w44-guest-player2): the pick is the ship's BT node 0x0519E372 (docs/re/creature.md#creature-ufo-pick);
  `creature.ufo_puppet` keeps puppets out of it (not verified: in the lab the ship's BT stops after ~7 s and never
  picks). Still open: the guest does not see the host's ship.
* Ticket: BUG-0558

## Tribe: a guest's game crashes in a model's effect list after a camera jump (2026-11-04, player report 0.7.5)
<a id="issue-tribe-effect-list-crash"></a>

* What happens: the guest's game crashed (0xC0000005 at GOG 2024 0xA826F3) right after a double-click on the home
  icon in Tribe, after it had given members to the host.
* Reproduce: not reproduced (step 0: 8 gifts and the HUD's home button, which did not move the lab camera).
* Known: docs/re/tribe.md#tribe-model-effects-crash; the crash's own frames (GOG 2024): 0xC88F20+0x14,
  QuaternionToMatrix, 0xC8ABF0+0xD4 (the model's attached-effect loop), 0xE8C100 and the view update 0xE8E5B0
  (+0x23, +0x495), 0xC69BA0, 0xE8CD10: an object view brought into the camera's range starts its model's effects,
  one of which has a freed component.
* Not checked: which object's model (a mirror given to the host, an effect the relay attached).
* Next steps: a test that gives members and jumps the camera with the minimap's house icon (55, 918) double-click.
* Ticket: BUG-0575

## Tribe -> Civ: the evolve-in-place path and a sync-check MISMATCH between the two Civ editors (2026-11-04, test tribe_civ)
<a id="issue-tribe-civ-between-editors"></a>

* What happens: between the City Hall and the vehicle editor the host logs the Creature evolve-in-place lines for
  the Tribe editors, and its sync check reports its own NPCs at (0, 0, 0) (MISMATCH, 46 apart).
* Reproduce: `tools/lab.py run tribe_civ --lane N`; the host's log between the editors.
* Known: docs/re/tribe.md#tribe-civ-transition (last item). Both games still reach Civ.
* Not checked: whether a guest sees anything wrong then; whether the sync-check gate of a lab test trips on it.
* Fixed on fix/w44-world-state2 (unreleased): diag.stage_end and the notice, docs/re/tribe.md#tribe-stage-end.
* Ticket: BUG-0588
- BUG-0582: Tribe: the host's game faults at 0xD72745 every frame during its own creature attack event (lab, ~1 run in 3 of tribe_bounce_world; its game-over caption may not come then) (fixed on fix/w43-crashes, unreleased: tribe.attack_spot)
- BUG-0583: Tribe: a guest faulted in a citizen's Update (GetAbilityIndexByType) after leaving and joining again (lab, once in realign_tribe_rejoin) (guard on fix/w43-crashes, unreleased: tribe.leader_tool; not reproduced)
- BUG-0580: Tribe: the NPC tribes' food differs between the host and the guest (detector `Tribe:state:tribe_food`; `SPORE_DETECTOR=20 tools/lab.py run play_tribe`) (fixed on fix/w43-crashes, unreleased: wsync.tribe_food)
- BUG-0584: Creature: a caught fault on the host, a call through null from 0xB097C4 (once in a 44 min `long_creature`, lab 20261104-214938; analysed, not reproduced: docs/re/creature.md#creature-fruit-pool)
- BUG-0591: Tribe: in play_tribe the wsync rows tribe_rel and tribe_food stop reaching some villages on the guest from about 400 s on (detector rows; `SPORE_DETECTOR=20 tools/lab.py run play_tribe`)
- BUG-0592: Creature: after the guest returns from the main menu, the host's standing creature can stand 0.7 m under the ground for about 20 s, until it walks (`SPORE_DETECTOR=20 tools/lab.py run long_creature_short`). Fixed on fix/w44-height (2026-11-05, creature.own_ground)
- BUG-0593: Civilization: a city the player took can stay the NPC's on the guest, with that civilization's relations and money (detector rows; `SPORE_DETECTOR=20 tools/lab.py run campaign_space`)
- BUG-0594: Tribe: a totem pole on the guest only before the move to Civ (`SPORE_DETECTOR=20 tools/lab.py run tribe_civ`)
- BUG-0595: Space: the guest's money reads nan in the detector's state row (`SPORE_DETECTOR=20 tools/lab.py run campaign_space`)
- BUG-0596: Creature: a species has a relation in one game only (`SPORE_DETECTOR=20 tools/lab.py run long_creature_short`)
- BUG-0575: Tribe: the guest crashed after double-clicking the home icon (0xA826F3) (guard on fix/w44-crashes, unreleased: game.model_fx; not reproduced, the freeing of the effect's part not found: docs/re/tribe.md#tribe-model-fx-guard)
- BUG-0584: Creature: a fault on the host, a call through null from 0xB097C4 (fixed on fix/w44-crashes, unreleased: wobj.fruit_saved + game.fruit_pool; reproduced by `crash_fruit_eat`: docs/re/creature.md#creature-fruit-pool-guard)
- BUG-0600: Tribe: a guest's game plays none of the hit and fight sounds of the members' fights (the host's 40+ in a
  40 s fight, the guest's 0; `coop.audio list` on both after `coop.tribe act 7`).
- BUG-0591: Tribe: in play_tribe the wsync rows tribe_rel and tribe_food stop reaching some villages on the guest from about 400 s on (detector rows; `SPORE_DETECTOR=20 tools/lab.py run play_tribe`) (fixed on fix/w44-world-state2, unreleased: wsync.tribe_pos)
- BUG-0592: Creature: after the guest returns from the main menu, the host's standing creature can stand 0.7 m under the ground for about 20 s, until it walks (`SPORE_DETECTOR=20 tools/lab.py run long_creature_short`)
- BUG-0593: Civilization: a city the player took can stay the NPC's on the guest, with that civilization's relations and money (detector rows; `SPORE_DETECTOR=20 tools/lab.py run campaign_space`) (fixed on fix/w44-world-state2, unreleased: wsync.city_owner, wsync.civ_rel, wsync.civ_money)
- BUG-0594: Tribe: a totem pole on the guest only before the move to Civ (`SPORE_DETECTOR=20 tools/lab.py run tribe_civ`) (fixed on fix/w44-world-state2, unreleased: a detector read of the host's end of Tribe, docs/re/tribe.md#tribe-stage-end)
- BUG-0595: Space: the guest's money reads nan in the detector's state row (`SPORE_DETECTOR=20 tools/lab.py run campaign_space`) (fixed on fix/w44-world-state2, unreleased: a detector parse)
- BUG-0596: Creature: a species has a relation in one game only (`SPORE_DETECTOR=20 tools/lab.py run long_creature_short`) (fixed on fix/w44-world-state2, unreleased: detector reads, docs/re/creature.md#creature-species-only)
- BUG-0575: Tribe: the guest crashed after double-clicking the home icon (0xA826F3) (guard on fix/w44-crashes, unreleased: game.model_fx; not reproduced, the freeing of the effect's part not found: docs/re/tribe.md#tribe-model-fx-guard)
- BUG-0584: Creature: a fault on the host, a call through null from 0xB097C4 (fixed on fix/w44-crashes, unreleased: wobj.fruit_saved + game.fruit_pool; reproduced by `crash_fruit_eat`: docs/re/creature.md#creature-fruit-pool-guard)
- BUG-0602: Civilization: one building of a city stands at another place in the guest's game (detector `Civilization:planet:building_*`; `SPORE_DETECTOR=20 tools/lab.py run campaign_space`)
  Fixed on fix/w44-guest-player3 (unreleased, `mirror.fx`) except the attack animations' hit impacts: BUG-0603.
- BUG-0603: Tribe: a guest's game plays none of the hit impact effects of the members' attack animations (the
  animation event's per-target effects 0x682E8D4B / 0xF0D5A994 / 0xCFECC623, sound 0xD05B58ED; `coop_tribe_fight_sounds.py`
  prints the host's plays). docs/re/tribe.md#tribe-mirror-fx. (fixed on fix/w45-tribe, unreleased: effects.hit_targets,
  the event's object is the attack's target: docs/re/tribe.md#tribe-hit-impacts)
- BUG-0604: Cell: after the guest's level-up its screen showed only water (no cell drawn, nothing on screen for 7 min) in one cell_new_game run (`tools/lab.py run cell_new_game`; not analysed)
- BUG-0606: Cell -> Creature, 4 players: every guest's sync-check found the host's cell 359-740 apart right after the
  campaign's food 1000 shortcut in the Cell advance (lab 20261105-233139 campaign_ct#1, once; not analysed).
- BUG-0608: Tribe / Creature: a relayed world effect (a hit impact of BUG-0603) is made at the sender's absolute
  position: on a guest whose village ground is 5-6 m off the host's (BUG-0332, campaign planets only) it would be
  under or over the copies' feet (inferred, not seen; `CAMPAIGN_GROUND=1 tools/lab.py run campaign_ct` shows the
  ground difference when it happens)

## A remote body's error at the start of a walk over a bad network (2026-11-06, fix/w45-remote-bodies)
<a id="known-walk-start-latency"></a>

* Seen: body_trail_tribe_player (SPORE_NET=player): with drive.lead the guest's copies are within 0.2-0.8 m of the
  host's bodies once they walk, but 1.9-2.3 m (p90) in the first 0.75 s of a Tribe member's walk: the guest knows a
  start one latency (and a spike) late. What is left is the latency itself.
* Reproduce: `tools/lab.py run body_trail_tribe_player`; per-time analysis of the track JSON it writes
  (docs/re/creature.md#creature-remote-lead).
* Tickets: BUG-0425 (fixed part), BUG-0612 (the class's other parts)
  ground difference when it happens) (fixed on fix/w45-village-ground, unreleased: one ground for everybody,
  tribe.host_reload of BUG-0332)
- BUG-0617: Tribe / Civ: food a tribe member carries lies 1-3.5 m behind on the guest while the member walks, a guest
  member's own carried food is not in the host's game, and a few Civ bundles in other containers stay each game's own
  (the detector's `game_bundle` rows left after BUG-0548; `SPORE_DETECTOR=20 tools/lab.py run play_tribe`)

<a id="issue-tribe-lone-citizen"></a>

* What happens: in play_tribe the host's game sometimes has a citizen near the guest that stands still for 10-20 s and
  belongs to no tribe's member list; the mod does not mirror it, so the guest's game has nothing there (detector
  `Tribe:unmatched:citizen`, 1-3 of ~31 samples a run). What it is is not known yet.
* Reproduce: play_tribe with `SPORE_DETECTOR=20` and `coop_tribe_member_place.py --attach` (prints the object).
* Ticket: BUG-0620

- BUG-0622: Cell: once (lab 20261106-110218, a heavily loaded PC, the first load of a fresh game 4 s after the galaxy
  menu) the host's game faulted in its own Cell mode entry (the effect preload list at 0xE67610 ran past its block)
  and looped on faults; no co-op code was involved and 13 cold loads did not repeat it
  (docs/re/cell.md#cell-preload-fault). Reproduce: `tools/lab.py run cell_gone_play --cold --repeat N` under load.

## Player reports of 2026-11-06 (Discord, 0.7.8, Cell to Civ, two players) (2026-11-06, fix/w45-intake)
<a id="issue-discord-2026-11-06"></a>

Each line: what the players saw, what the logs show; reproduce and details in the ticket.
- Creature: every editor spend lowers the other player's evolution bar by the amount: the mod moves the shared DNA
  through AddEvolutionPoints, which also adds negative deltas to cPlayer +0x10F0 (the guest's bar fell to almost 0,
  the host's halved; the bars differ by one tier) (BUG-0630, docs/re/creature.md#creature-evolution-bar).
- Civ: the host's game crashed after ~2 h in one process with its 2 GB address space full (largest block 12 MB), in a
  model's vertex blend writing through a null buffer at +0x3995F0; ~740 MB of the drain in Creature (BUG-0631, see
  BUG-0621). 2026-11-06 (fix/w46-addr-space): the drain is the game's own cache of what it loaded (skins, models), the
  host filling it with its partner's surroundings too (no leak of the mod); core.mem_reserve gives a kept block back
  with a notice to save and restart (docs/re/graphics.md#graphics-address-space). Still open by nature: a long
  session on the 2 GB space (large-address flag off) can still fill it; see the ticket for the flag.
- Cell: a guest's kill is applied twice on the host (a giant piece of meat) (BUG-0632).
- Cell: the host cannot collect the part a kill dropped (BUG-0633).
- Cell: the guest's cell swims through other cells (BUG-0634).
- Cell: after the guest's level-up the bigger cells near it vanish and others appear (not BUG-0604) (BUG-0635).
- Cell: the guest can eat the same plant or meat piece twice: no tombstone of an eaten mirror (BUG-0636).
- Cell: after a guest's rejoin and the host's own-cell change at level 10, cells of another level (BUG-0637).
- Creature: a guest still in its own Cell editor when the host enters Creature plays its creature from another world
  (BUG-0345's rule, BUG-0638).
- Creature: the guest can attack the creatures of a nest evolving on the host (BUG-0639).
- Creature: the host's species migrates, the guest's game never claims the nest (respawn at the old nest, no DNA, no
  cutscene); the in-place path logged "migration 0 -> 0" (BUG-0640).
- Creature: spit does not interrupt charges between a guest and NPCs; some NPC strikes miss the guest (BUG-0641).
- Creature: a creature chosen in the co-op window changes only the avatar, not the nest and family (BUG-0642).
- Tribe: the guest's roster shows white squares instead of the member portraits (BUG-0643).
- Tribe: the NPC villages the guest's game makes have other colours (no colour copied in make_npc_tribes) (BUG-0644).
- Tribe: the guest cannot have more than 6 members, its village shows 12/6 (BUG-0645).
- Tribe: the guest does not hear the music of the dance around the fire (BUG-0646).
- Civ: the guest sees the host's ground vehicles as creature-like monsters (BUG-0647).
- Civ: the guest's military vehicles cannot shoot an enemy city hall nor capture it (BUG-0648).
- Civ: the guest does not see the host's name at the host's camera (BUG-0649).
- Also seen on 0.7.8, fixed unreleased in 0.7.9: the guest does not see the drama UFO (BUG-0558), city buildings
  rebuilt again and again after captures (BUG-0602).

## Tribe: a guest that joins while a member's body lies on the host shows no body (2026-11-07, fix/w46-tribe-missing)
<a id="issue-tribe-join-body"></a>

* What happens: tribe.member_body (BUG-0620) lets a mirror the guest already has lie where the host's body lies, but
  a guest spawns no mirror for a body entry: a guest that joins or reloads the world while the body lies on the host
  (10-20 s, at most 60 s) has none for it (the sync-check leaves bodies out since BUG-0664).
* Reproduce: tribe_dead_rejoin (`coop_tribe_dead_member.py --rejoin`): the guest's `coop.sync` never lists the body's
  key; watch the victim's place with `coop.census all` on both games.
* Known: inferred from the code (tribe.cpp, "no new mirror for a body"); not checked whether the shared save (written
  before the death in the test) or a later one holds the body as an object of the guest's own world.
* Not checked: what the guest's screen shows at the body's place.
* Fixed (2026-11-08, fix/w46-k8-open): `tribe.join_body` makes the body's mirror on the joined guest (test
  tribe_join_body).
* Ticket: BUG-0665
## Cell: no part token can be collected after the Goldie's demise tutorial (2026-11-07, fix/w46-part-token)
<a id="issue-cell-goldie-locks"></a>

* What happens: in the lab, after the shared food passes 120-150 (the game's one-time tutorial goldies_demise), both
  avatars keep +0x178 (and +0x16C, +0x17B, +0x17C) set and neither game collects a part token.
* Reproduce: Cell fixture, `coop.pool food 50` four times, `coop.pool tokens` shows `own_locks=0010`.
* Known: docs/re/cell.md#cell-goldie.
* Closed (2026-11-08, fix/w46-k8-open): not a mod bug. The cinematic waits for its button; the player's click or Esc
  ends it and frees the avatar, in a session too (cell_goldie, docs/re/cell.md#cell-goldie-button). Tests that pump
  food across 120-150 must press Esc or mark the tutorial played (`coop.pool mate nogoldie`).
* Ticket: BUG-0666
## Tests: the detector compares Cell positions across two world scales in a level change (2026-11-06, fix/w46-cell-p0)
<a id="issue-detector-cell-scale"></a>

* What happens: a `Cell:pos:cell` row (80-552 cells 7-14 m apart) in the one comparison that falls into a world
  scale tier change; filed under BUG-0480 by detector_report.py.
* Reproduce: `SPORE_DETECTOR=20 tools/lab.py run cell_levelup_view --lane N` (lab 20261106-224058 #1).
* Known: inferred to be BUG-0606's cause (raw positions of two games in different scales); worlddiff has no
  diag.cell_scale conversion. The guest draws the cells where they are.
* Ticket: BUG-0662
## Creature: each game evolves the NPC nests by itself (2026-11-07, fix/w46-creature-p0)
<a id="issue-herd-evolution-apart"></a>

* What happens: after the pack's DNA crosses the nests' evolution thresholds, every NPC herd gets its next
  generation (a new species) in each game at its own threshold; a nest the host's game keeps extinct evolves to a
  new species in the guest's game.
* Reproduce: `tools/lab.py run nest_evolve_probe --lane N` (lab 20261107-081734), compare the `coop.nest evolve` rows.
* Known: docs/re/creature.md#creature-herd-evolution-desync. Not known: what advances the generation +0xF0, and what
  the guest's screen shows at such a nest (the creatures are the host's NPCs).
* Ticket: BUG-0668

## Tribe: the guest does not hear the music of the dance at the fire (2026-11-07, player report 2026-11-06)
<a id="issue-tribe-dance-music"></a>

* What happens: the host hears music while its members dance around the fire, the guest does not.
* Reproduce: not reproduced in the lab (3 min idle with a 30 s day, a party order at the fire pit).
* Known: no theme music played on the host in those runs; the guest's mirrors play the same animations
  (docs/re/tribe.md#tribe-dance-music).
* Not checked: the dance at night with a goal open (the report's moment).
* Next steps: one host run with the dance, `coop.audio` on both games.
* Fixed unreleased (2026-11-07, fix/w46-tribe-dance): the party music 0x19DC06DF is relayed (`tribe.party_music`,
  docs/re/tribe.md#tribe-party-music).
* Ticket: BUG-0646

## Tribe: the guest's mirrors make almost no footstep sounds (2026-11-07, fix/w46-tribe-civ)
<a id="issue-tribe-mirror-footsteps"></a>

* What happens: footstep sound 0x4A776923 436 times on the host, 36 on the guest in 3 min.
* Known: FUN_009A6BD0 plays them while the animated creature's velocity (+0x1660) is above 0.1; mirrors have none.
* CORRECTION (2026-11-07, fix/w46-misc): 0x4A776923 is the foot's sound type, not a sound id; the host's footsteps were
  its epic's (the animation's epic flag), the guest's proxy of the epic had no flag. The mirrors' velocity was not it.
* Fixed unreleased (2026-11-07, fix/w46-misc): `npc.epic_steps`, docs/re/tribe.md#tribe-epic-steps.
* Ticket: BUG-0669

## Civ: the host's own building designs show as the guest's own creations (2026-11-07, fix/w46-tribe-civ)
<a id="issue-civ-host-buildings"></a>

* What happens (inferred): buildings of the host's own designs resolve to the guest's creation of the same id.
* Known: SetBuildingModel 0xBDB940 reads the civ's design key itself; vehicles are fixed (BUG-0647).
* Fixed unreleased (2026-11-07, fix/w46-misc): reproduced in the lab and fixed (`civ.host_buildings`,
  docs/re/civ.md#civ-host-buildings).
* Ticket: BUG-0670

## Tribe: a member the guest buys never reaches the host (2026-11-07, fix/w46-tribe-civ)
<a id="issue-tribe-guest-member"></a>

* What happens: the guest's game makes the member, the mirror reconcile removes it, the host has none.
* Fixed unreleased (2026-11-07, fix/w46-tribe-dance): the host makes it and it is the guest's (`tribe.guest_baby`,
  docs/re/tribe.md#tribe-guest-baby).
* Ticket: BUG-0671
* Fixed (2026-11-07, fix/w46-herd-evo): the host owns the evolution (wsync.herd_evo, test nest_evolve);
  docs/re/creature.md#creature-herd-evolution-owner.

## Space: a guest does not see the beam shots of the host's ships (2026-11-07, fix/w46-s1-sweep)
<a id="issue-space-beams"></a>

* What happens: the ships the host's game runs (the host's own, pirates, allies) fight in a guest's game without their
  beams; the hits still come from the host. Mirrors never fire in a guest's game (squadron.cpp h_FireTool) and only Civ
  shots are relayed (civ.shots, BUG-0477).
* Reproduce: play_space with SPORE_DETECTOR=20; `tools/detector_report.py <run>` lists
  `Space:host_only:default_beam_projectile`.
* Ticket: BUG-0677
* Fixed (unreleased, fix/w46-k56-space): the host relays its NPC ships' shots and a guest's mirrors fire them
  (`space.shots`, docs/re/space.md#space-ship-shots).
## Space: what the kept guest cargo hold does not cover (2026-11-07, fix/w46-space-items)
<a id="issue-space-kept-hold-limits"></a>

* What happens: a guest's own hold comes back after a rejoin (BUG-0678), but not when the host restarted its game
  meanwhile (memory only), not with changes made while the guest was not connected, cargo slots only grow, and a
  non-spice cargo item the host's copy lacks cannot be made again.
* Reproduce: `coop_space_items_rejoin.py` with the host restarted between the guest's leave and its join.
* Known: docs/re/space.md#space-kept-hold.
* Not checked: a guest that joins under another name (another player to the host).
* Next steps: a side file on the host per Space save (as docs/11-rivals.md#rivals-guest-colonies designs for colonies).
* Ticket: BUG-0679
* Fixed (unreleased, fix/w46-k56-space): the guest keeps its own record per host world on its disk (host restarts,
  changes while away), extra free slots go, plant / animal / artifact cargo is made by the game's factories
  (`space.keep_items_local`, `space.keep_items_slots`, `space.keep_items_cargo`,
  docs/re/space.md#space-kept-hold-limits). Still not covered: a guest that joins under another name or from another
  PC's profile.

## Monitor: the Cell drawn invariant during a level change (2026-11-07, fix/w46-detector-scale)
<a id="known-monitor-cell-drawn-scale"></a>

* What happens: cell_levelup_view can fail the lab gate on `MONITOR-VIOLATION drawn ... (limit 17.0 m, Cell)` right
  after a level change (1 of 4 runs, lab 20261107-143741 #1, 18.0 m).
* Inferred: the monitor's drawn invariant compares positions of two games in two world scales while the rescale runs
  (the class of BUG-0662 / BUG-0606); the partner line carries no scale.
* Reproduce: `tools/lab.py run cell_levelup_view --repeat 4`.
* Next steps: add the sender's scale to the partner line (or skip a pair whose scales differ) as worlddiff does.
* Fixed (unreleased, fix/w46-k7-findings): the own Cell trace line carries `wscale=` and the drawn rule compares in
  one scale (docs/06-testing.md#testing-monitor-cell-scale).
* Ticket: BUG-0681

## Creature: the host freezes after a guest's rejoin while its share save is never written (2026-11-07, fix/w46-advance-reg)
<a id="issue-creature-rejoin-share-freeze"></a>

* What happens: 4 players in Creature, a guest leaves and joins again; the host sends no state from then on (the
  guests hide its creature), its save for the rejoined guest's share writes no file ("no saved box and no file
  written ... saving again (try 2)", "(try 3)"), and about 30 s later the host stops for good (FROZE).
* Reproduce: seen once, campaign_tribe on lane 3 (lab 20261107-182231 #2, rel/0.7.10 4c1d891f);
  `SPORE_LANE=3 python3 tools/tests/coop_campaign.py --from creature --to tribe --minutes 2.0 --seed 953406079`.
* Known: the host's frames ran on between the rejoin and the last save post (DNA and save lines in its log).
* Not checked: the cause; whether v0.7.9 does the same (inferred not a 0.7.10 regression: nothing merged after
  e7d1734c touches the share path).
* CORRECTION (2026-11-08, fix/w46-k7-findings): the host's frames did NOT run on. Its last state reached the guests at
  s 86.3, 0.1 s after it handled Bob's leave, and its trace lines (end of on_frame) stop after s 80; every later line
  (the rejoin, the share save and its retries, DNA from the guests' messages) comes from the keepalive
  (coop.cpp keepalive_task: g_sess.update and world::tick from the window procedure when no frame came for 0.5 s).
  Inferred: the main thread sat in a nested message loop that was neither App::Canvas::Tick (SporeKit's game-loop
  fallback frame never started) nor Present; the share save needs the game's update, hence no file. The watchdog's
  dump (30 s) never reached the log (inferred: its SuspendThread + VirtualQuery stack walk can hang under Wine).
* Diagnostics since then: the keepalive logs, once per stall of 3 s, the main thread's call chain (return addresses
  into the game, user32 frames) and its windows (`keepalive: no frame for ...`, `keepalive: the main thread's
  windows ...`; test keepalive_stall). The next occurrence names the loop.
* Not reproduced in 4 more runs of the report's seed on fix/w46-k7-findings (see the ticket).
* Ticket: BUG-0687

## Tribe: the host's creature attack marker is not on the guests' maps (2026-11-08, fix/w46-n2-desyncs)
<a id="issue-tribe-attack-marker"></a>

* What happens: when the host's game runs a creature attack on the village (the Drama Manager's kind 7), its marker (an
  ornament of model 0x356EB8A) and minimap blip are only in the host's game; the guests see the attacking animals but not
  the marker. The detector reports it as `Tribe:unmatched:ornament` / `host_only:ornament`.
* Reproduce: campaign_tribe with SPORE_DETECTOR=20 (3 of 16 four-player campaigns on 2026-11-08); not yet on the fixture.
* Known: docs/re/tribe.md#tribe-attack-marker (the maker 0xD73930 -> 0xC9B140 -> 0xC911C0).
* Not checked: how the game removes the marker; why `coop.drama start 7` on the tribe fixture made none.
* Ticket: BUG-0740 (from BUG-0528)

## All stages: a model in a database the mod cannot read is built unchecked (2026-11-08, fix/w46-k2b-content-b)
<a id="issue-content-unreadable-model"></a>

* What happens: creations::ensure_model checks the parts of a model through the package file that answers it; a
  database without a file location (a folder database) cannot be read, and such a model is built as it is (log
  "creations: content: <kind> model ...: its records cannot be read here: built as it is").
* Reproduce: not reproduced (0 of 32157 lab logs; content_audit_<stage> shows no unreadable model).
* Known: every database of the GOG 2024 and Steam installs and of the profiles is a package file.
* Next steps: a mod installed as a folder database would show it; then read the record through the database's
  OpenRecord stream.
* Ticket: BUG-0727

## Creature: a guest's Summon Flock follows the host's creature after it came (2026-11-08, fix/w46-k2b-content-b)
<a id="issue-flock-follows-host"></a>

* What happens: since BUG-0736 the host's game makes a guest's Summon Flock next to the guest's creature and the flock
  joins the party's pack; the pack (cPosseSimulator, one per game) follows the host's creature, so the flock walks to
  the host over time. It defends the guest too (BUG-0735).
* Reproduce: flock_relay with the guest far from the host, then watch the members' distances (`coop.hp pack`).
* Known: the posse follows cGameNounManager +0x54; per-member following of a puppet is not in the game.
* Ticket: BUG-0736
## Cell: whether a guest's poison clouds hurt the host's creatures is not shown in the lab (2026-11-08, fix/w46-k2b-content-a)
<a id="issue-cell-guest-poison-damage"></a>

* What happens: since BUG-0716 the host's game makes a guest's poison clouds (from the guest's creation, through the
  game's own poison step). Whether they hurt a creature as the host's own clouds do is not seen: the lab fixture has no
  creation with a poison part, and the part written into a model's data (`coop.pool borrow poison graft`) makes clouds
  that hurt nothing even for the host's own avatar.
* Reproduce: a guest whose own cell has a poison part, next to a host creature (`coop.pool borrow status x p<id>` on
  the host: `puppet_clouds=`), the creature's health in the host's game (`coop.pool cell c<key>`).
* Known: docs/re/cell.md#cell-electric-attack (the poison step, the clouds, what the graft sets).
* Ticket: BUG-0716 (the clouds fixed; the damage not verified).
* Update (2026-11-12, fix/w48-small, BUG-0892): verified. A host creature put onto the clouds the host's game makes for
  the guest's puppet (`coop.pool place`) lost 6 hp in the host's game and the guest's mirror showed it (lab
  20261112-125720, 20261112-130016; with cell.puppet_poison off no cloud and no loss, 20261112-125843). The earlier "no
  loss" (lab 20261108-044202) had the creature only next to the clouds. Test cell_poison asserts it; nothing open here.
## Space: an NPC empire's store pays a different price in each game (2026-11-08, fix/w46-g4b-coverage)
<a id="issue-space-npc-store-prices"></a>

* Two players at the same NPC empire see different stores: each game generates its own (wanted spices and price
  factor at random). `coop_space_store.py`: the guest sold a Red Spice for 281, the host for 4,779 to the same empire.
  Both earnings go into the shared money, so nothing is lost, but the trade screens disagree.
* Fixed (unreleased, fix/w46-b1-space, `space.store_seed`): the store's fill is seeded from its planet in a session
  (docs/re/space.md#space-npc-store-seed). Left: a store the host's game made before the session started.
* Ticket: BUG-0747.
## Space: the host's copy of a guest's ship fails the sync-check while it flies key pulses (2026-11-08, fix/w46-final11-triage)
<a id="issue-space-guest-ship-path"></a>

* `sync-check: MISMATCH ... p: 1 apart (pN 30-80 m)` with N a guest's own id: the host's puppet of that guest's ship is
  a state behind on its path while the ship brakes or turns; `sync.own_path` (BUG-0547) takes back only the host's
  own ship (p0). Seen in campaign / campaign_space / play_space since 0.7.3. Nothing wrong to see in the game.
* Update (2026-11-08, fix/w46-b1-space): not reproduced in two-player runs (about 520 checks of a guest flying pulses,
  reversals, a disabled ship, spikes, a 12 fps host: 0 differences, docs/re/space.md#space-guest-ship-path); the lag
  explanation does not hold for a guest's own key (copy and digest share the link). The campaign cases persisted over
  the recheck and came while the host waited to share its world with a newcomer. `sync.own_path_guests` now logs a
  detail line for each p<guest> difference (copy and ship against the guest's received path): the next sighting says
  whether the copy was on the path. Ticket closed not reproduced; reopen with that line.
* Ticket: BUG-0758.
## Tribe: the host's chief jumps (~120 m/s) and a guest's mirror stays 10-12 m off (2026-11-08, fix/w46-final11-triage)
<a id="issue-tribe-chief-jump"></a>

* campaign_tc2, 4 players: `t: 1 apart (t4 11.5)`, the host's leader at 119.7 m/s, the guest's mirror at 0; once per
  run at most (0.7.10 and 0.7.11).
* Ticket: BUG-0759.
* 2026-11-08 (fix/w46-b1-tribe): not reproduced with 2 players (a teleport of 10-11.5 m, the chief and an NPC chief
  sleeping in the hut, the velocity held at 120 m/s, a 100 ms link: the mirror followed each time,
  docs/re/tribe.md#tribe-mirror-jump); the ticket is closed not-reproduced, the regression test is tribe_mirror_jump.
  Reproduce: campaign_tc2 (40 min, rare); next time save both games' coop.bodyrec rows (SPORE_BODYREC_DIAG=1).
## Tribe: a raid takes the NPC hut to health 0 and no conquest follows (2026-11-08, fix/w46-final11-triage)
<a id="issue-tribe-conquest-missing"></a>

* tribe_conquer_orders: 1 of 5 runs of 0.7.11 played no conquest cinematic in 60 s after the hut's health 0.
* Ticket: BUG-0761.
## Space: sculpting and colouring tools change the planet in one game only (2026-11-08, fix/w46-b1-space)
<a id="issue-space-sculpt-colour"></a>

* What happens: the tool palette's terrain sculpting (raise / lower / flatten) and planet colouring tools are not
  relayed; only the atmosphere / water / temperature deltas are (spacecoop.cpp K_TERRA). Inferred from the code.
* Reproduce: two games on `space_free` on planet 01460035, a sculpting tool used by the host (tab W-226).
* Ticket: BUG-0763.
* FIXED (2026-11-10, fix/w47-b2-space, unreleased): both are relayed to partners on the same planet
  (`space.terra_sculpt`, coop_space_sculpt_tool.py). Done with nobody else on the planet: still one game only (BUG-0900,
  below).
## Space: a guest's colony planner building is lost while the host is on another planet (2026-11-08, fix/w46-b1-space)
<a id="issue-space-planner-away"></a>

* What happens: since BUG-0751 the host builds a guest's planner building only when its own ship is on that planet;
  otherwise the host's world never gets it (the money is spent for both). Log `civ: space planner: ... not built`.
* Reproduce: coop_space_planner_build.py with the host's ship elsewhere.
* Ticket: BUG-0766.
* FIXED (2026-11-10, fix/w47-b2): the host keeps it until its ship is on that planet (civ.space_build_hold); the
  host's own colony buildings reach the guests too (BUG-0884). Open: whether a building the mod makes is kept when
  the planet is left and made again (BUG-0901).
* 2026-11-08 (fix/w46-b1-tribe): a test problem, fixed in the test (the hut at 0.25-0.5 still stood, raiders left the
  raid; docs/re/tribe.md#tribe-hut-fall).
## Found in the audit of fork PR #196: not fixed yet (2026-11-10, plan/w47-pr196)
<a id="issue-pr196-audit"></a>

Each line is one open ticket with its reproduction in "How to test" (plan 0.7.13):
* Creature guest crash risks from cached pointers: a proxy driven between the 1 s noun-list checks (BUG-0842), a
  freed cached nest herd (BUG-0843); 0.7.11 regression: destroy_proxy destroys an already destroyed proxy (BUG-0850,
  hotfix candidate).
* Tribe guest: RemoveMember through a stale mirror citizen (BUG-0844); a baby mirror spawned mid-growth (BUG-0857,
  suspected).
* Tribe -> Civ: a share save between the City Hall and vehicle editors (BUG-0849, suspected); a null animal pen at
  graduation (BUG-0856, suspected for our saves). Civ -> Space: the spacecraft editor under the launch pause
  (BUG-0855, suspected).
* Saves and shares: the watcher takes the specialty question (window 0x06318E78) for the saved box (BUG-0846);
  unreadable save files are left out of a share (BUG-0847); Windows staged slot move fails on a brief lock (BUG-0848).
* Adventures: two captains, the host crashes at the act change (BUG-0845, suspected); saves and the quit message in a
  standalone adventure (BUG-0854).
* Other: uninstaller weaknesses (BUG-0851), run-compressed paint headers skipped (BUG-0852), win_tests close race
  (BUG-0853), co-op buttons under a native modal with input_lock 0 (BUG-0858, suspected).

## Civ: vehicles get faster and faster during long sieges (2026-11-10, plan/w47-reports)
<a id="issue-civ-boost-stacking"></a>
* The city-alarm boost can take an already boosted speed as the vehicle's own (x1.5 per alarm cycle), and a boost
  forgotten while its vehicle lives is never taken back (docs/re/civ.md#civ-boost-stacking).
* Reproduce: a continuous NPC siege of a player city for several minutes; the defenders' `+0x218` grows.
* Tickets: BUG-0771, BUG-0773.

## Captures and conquests shown on one game only (2026-11-10, plan/w47-reports)
<a id="issue-capture-one-game"></a>
* Civ: a captured city goes to the player with the nearest vehicle, not the one who took it (BUG-0775, suspected).
* Tribe: a conquered village has no conquest row for the guests (BUG-0777, suspected).
* Reproduce: not yet; the player report had no files and no stage.

## Space: a guest's badges are the host's after a rejoin (2026-11-10, plan/w47-reports)
<a id="issue-space-badges-rejoin"></a>
* The badges and rank live in the host's save the guest loads again; nothing keeps the guest's own (as the cargo
  hold before BUG-0678).
* Reproduce: earn a badge as a guest, leave, join again.
* Ticket: BUG-0786.
## Core: session report and settings state survive a voluntary leave (0.7.12 only) (2026-11-10, plan/w47-core)
<a id="issue-core-sreport-leave"></a>

* What happens: after Leave / host / join (not a disconnect) a guest keeps its report upload and the previous host's
  settings table into the next session; a host keeps a running report round. Inferred from the code.
* Reproduce: the guest leaves while `coop.sreport status` says `guest=sending`, joins again (BUG-0770 How to test).
* Tickets: BUG-0770 (and BUG-0783: an Ask while busy waits 120 s; BUG-0780: the zip written in one main-thread call, suspected).
## Core: a leaver's I'm ready flag passes to the next player of its id (2026-11-10, plan/w47-core)
<a id="issue-core-ready-reuse"></a>

* What happens: a player that never sent a state keeps `g_ready` after leaving; the newcomer with its id counts as
  ready in the vote. Inferred from the code.
* Reproduce: BUG-0772 How to test.
* Ticket: BUG-0772.
## Net: hardening of untrusted input (names, discovery, half-open peers, device names) (2026-11-10, plan/w47-core)
<a id="issue-net-hardening"></a>

* What happens: received player names are not cleaned (BUG-0774); discovery answers any internet source (BUG-0776);
  16 half-open peers block joins (BUG-0778); `safe_rel` accepts DOS device names (BUG-0785). Inferred from the code.
* Reproduce: the How to test sections of the tickets.
* Tickets: BUG-0774, BUG-0776, BUG-0778, BUG-0785.

## Cell and Creature: findings of the code audit for 0.7.13 (2026-11-10, plan/w47-cellcreature)
<a id="issue-audit-cell-creature-2026-11-10"></a>

Read-only audit of main e49e1e23 (0.7.11.1); not checked in the running game. Each line is one open ticket (root cause,
fix idea, test in the ticket); "suspected" ones need a reproduction first.

* BUG-0795 (crash): a partner's effect message is played on a puppet freed between two frames (effects.cpp, BUG-0091 sibling).
* BUG-0796 (crash): a partner's animation is played on a Cell or Tribe puppet around a stage switch (on_remote_anim).
* BUG-0797 (bug): a Cell guest in another world (or still in Cell while the host is in Creature) gets an emptying pool.
* BUG-0798 (bug): a downed cell is held at its pre-rescale spot across a level change: no revive.
* BUG-0799 (bug): a knocked-out cell's poison still hurts and kills NPCs.
* BUG-0800 (bug): the guest's Summon Flock event is never forgotten: the host's own flock follows the guest.
* BUG-0801 (bug): votes wait 60 s when a guest plays another world (3-4 players).
* BUG-0803 (bug): NPC nests never evolve in a connected guest's own world.
* BUG-0805 (crash): proxy pointers used by MSG_DAMAGE / social handlers without a liveness check.
* BUG-0808 (bug): the social meter stays on screen after a leave mid social game.
* BUG-0811 (bug): the own editor's flags stay set after a refused Submit.
* BUG-0814 (crash, suspected): a carried rock stays held by a destroyed puppet.
* BUG-0817 (bug): the guest's mirror of the host's drama UFO goes into the guest's saves.
* BUG-0819 (bug, suspected): the BUG-0200 species alias also runs in the guest's own world.
* BUG-0821 (bug): the host's Cell kill ledger records a kill before refusing it.
* BUG-0823 (bug, suspected): cellsync::puppet_cell() without the pool index check.
* BUG-0825 (bug, suspected): a deferred destroy can hit a real creature at a reused address.
* BUG-0827 (desync, suspected): a whole swallow of a host cell feeds the guest while the cell lives on.
* BUG-0829 (desync, suspected): away DNA may move the guest's evolution bar twice.
* BUG-0831 (bug): a departed guest's social game goes on on the host.
* BUG-0833 (bug, suspected): nest egg mirrors leak their references at each guest world change.
* BUG-0835 (desync, suspected): touching a mirrored poison cloud counts as eating it.
* BUG-0837 (tooling): 0xC1B030 has two names in the .sym (docs/re/creature.md#creature-c1b030-two-names).
* BUG-0839 (bug): game::world_epoch() counts only Creature worlds (Tribe users of the epoch).

## Cell: a rename of an imported cell sends only metadata, the host crashes at 0xE83EC8 (2026-11-10, plan/w47-lineages)
<a id="issue-cell-metadata-only-rename"></a>

* What happens (fork finding, code audit on ours): a guest renames an imported cell in the Cell editor; the bundle
  has no bem record and the host faults building it (creations.cpp share_own / apply).
* Reproduce: see the ticket (2 instances, Cell, free rename of an imported cell).
* Ticket: BUG-0802.

## The mouse after the game saved box (2026-11-10, plan/w47-lineages)
<a id="issue-saved-box-cursor"></a>

* What happens: after every save the mod clicks the box's OK and the game reads the mouse at that point for ~5 s
  (sporekit virtual cursor); an OK button outside the window is clicked forever and never confirmed.
* Reproduce: `coop.save`, then watch the cursor / the box (tickets).
* Tickets: BUG-0804, BUG-0806.

## Smaller findings from the Lineages fork review (2026-11-10, plan/w47-lineages)
<a id="issue-fork-review-small"></a>

* Live Cell state goes to a guest still loading the world (suspected bandwidth cost): BUG-0809.
* `coop.ui show|btn|font|tree` cast a typed number to a window pointer (console crash): BUG-0812.
* A nest mate roaming beyond the native reply radius never answers the mating call (suspected): BUG-0815.

## Friendly fire only in the Creature stage (2026-11-10, feat/w46-settings-all)
<a id="issue-friendly-fire-stages"></a>

* What happens: the host's setting `all.friendly_fire` on lets players' creatures hurt each other in the Creature
  stage only; in Cell, Tribe and Civilization players still cannot hurt each other (Space has its rivals mode).
* Reproduce: `coop.settings force all.friendly_fire on` on the host in Cell: a bite on the partner's cell does nothing.
* Ticket: BUG-0899.
## Space: a colony building the mod builds may not survive leaving the planet (2026-11-10, fix/w47-b2)
<a id="issue-space-mod-building-kept"></a>

* What happens (suspected): the game makes a planet's cities again from its record when it becomes the active planet
  (a house destroyed at run time came back); a building made by build_in_city (a partner's planner building) may not
  be in that record, so it would vanish when the ship leaves and lands again. Not checked.
* Reproduce: docs/tickets BUG-0901 (How: free the plot with the planner's own demolish, not `coop.civ unbuild`).
* Ticket: BUG-0901.
* FIXED (2026-11-10, fix/w47-colony, unreleased): confirmed (lab 20261110-201410: the mod's building gone after leaving
  and landing); the game writes a live city into its cCityData only through City_WriteCityData (the planner's exit),
  the mod now calls it after its colony building (`civ.space_city_data`, coop_space_city_keep.py).
## Space: sculpting and colouring done alone stay in one game (2026-11-10, fix/w47-b2-space)
<a id="issue-space-sculpt-alone"></a>

* What happens: the sculpting / colouring relay (BUG-0763) reaches only partners whose ship is on that planet at the
  hit; a partner who lands later or rejoins sees the old terrain and colours. Inferred from the code.
* Reproduce: space_free, the guest sculpts / recolours 01460035 alone (coop_space_sculpt_tool.py's clicks), then the
  host lands there: coop.ground at the spot and `coop.space sculpt` colours differ.
* Ticket: BUG-0900.
* FIXED (2026-11-10, fix/w47-colony, unreleased): a partner's hits for a planet this game is not on are kept and done
  5 s after landing there (`space.sculpt_hold`, coop_space_sculpt_tool.py --away). Still open: a player who joins or
  rejoins after the hits does not get them.

## Tribe looks the review of key frames found (2026-11-10, feat/w47-visual)
<a id="issue-tribe-review-looks"></a>

* What happens: with the same fixed camera on both games (same_view_tribe), the guest draws the host's chief without
  the chief's staff the host's game shows (BUG-0906), and the guest shows a partner's name tag over empty ground with
  no member under it (BUG-0907; also seen in play_tribe's start frame).
* Reproduce: `tools/lab.py run same_view_tribe --lane N`, then `tools/frames_review.py <run>` and open the
  same_view_tribe sheets (lab 20261110-201619).
* Tickets: BUG-0906, BUG-0907.
* FIXED (2026-11-10, fix/w47-tribe-looks, unreleased): the members' handheld item is replicated (`tribe.held_item`,
  docs/re/tribe.md#tribe-held-item), the pointer has no name in Tribe (`tribe.pointer_tag`,
  docs/re/tribe.md#tribe-name-tags); tests tribe_held_item, tribe_name_tags.

## Tribe: the chief's staff is bigger on the guest's screen; a test's guest-side chief size check (2026-11-10, fix/w47-tribe-looks)
<a id="issue-tribe-staff-size"></a>

* What happens: with the held items replicated (BUG-0906) the guest draws the host's chief's staff about 1.2-1.3x
  bigger than the host does (sheet 008 of lab 20261110-214352). `tribe_chief_removed` fails its check "guest: the new
  chief is 1.3x its size" (1.58, 1.20; also with the branch's switches off).
* Reproduce: `tools/lab.py run tribe_held_item --lane N` + `tools/frames_review.py <run>`; `tools/lab.py run
  tribe_chief_removed --lane N --retry 0`.
* Known: inferred only: the item model is sized at the attach, the mod's 1.3x chief size comes after the host's attach
  and before the guest's; the guest's chief ratio is read against the guest's own base size while the scale field
  sets the host's size. docs/re/tribe.md#tribe-held-item.
* Not checked: citizen vt 0xE0, the model's scale on both games.
* Tickets: BUG-0911 (the test), BUG-0912 (the staff).
* FIXED (2026-11-10, fix/w47-tribe-looks-2, unreleased): the held model's size is replicated (`tribe.item_scale`,
  docs/re/tribe.md#tribe-item-size); the test compares the guest's chief sizes with the host's game.
* Still open: a guest's copy does not play the host member's staff swing (BUG-0914).
* FIXED (2026-11-10, fix/w47-colony, unreleased): a partner's hits for a planet this game is not on are kept and done
  5 s after landing there (`space.sculpt_hold`, coop_space_sculpt_tool.py --away). Still open: a player who joins or
  rejoins after the hits does not get them.

## Creature: the second attacker stops behind the partner's creature (2026-11-10, fix/w47-play-bots)
<a id="issue-creature-attack-behind-partner"></a>

* A player who attacks an animal the partner already fights, coming from the partner's side: the attack key walks
  the creature up and stops it about 2 m short, behind the partner's creature; further presses do nothing until the
  player walks around by hand. A click on the animal often picks the partner's creature in front of it (the attack
  key then does nothing). docs/re/creature.md#creature-walk-and-use-blocked, #creature-combat-click-target.
* Reproduce: two games on the creature fixture, combat stance; the host clicks an animal and presses 1, then the
  guest clicks the same animal from the host's side and presses 1 (the probes of fix/w47-play-bots, 2026-11-10).
* Ticket: BUG-0913.

## Creature: a caught fault in the game's reach check on a stale combat target (2026-11-11, fix/w47-lab-memory)
<a id="issue-creature-target-gap-fault"></a>

* In the campaign's Creature play with four players a guest's game caught one fault, `0xC0000005 at 0x3F800000`
  inside the game's `Ability_TargetGap` (the reach of an attack on the avatar's combat target): one of the two
  creatures was no live object any more. The game went on. Not known whether the caller was the play bot's test
  command `coop.abilities reach` or the game's own ability use (then a player can hit it). Seen twice in one night
  (campaign: a guest at 18 min; long_creature: the host at 27 min), each a second after a hunt's kill; both tests fail.
* Reproduce: `SPORE_DETECTOR=20 tools/lab.py run campaign --lane 9` (lab 20261111-002733 of fix/w47-lab-memory, the
  first guest at 18 min); not reproduced on purpose yet.
* Ticket: BUG-0917.
* CORRECTION (2026-11-11, fix/w47-target-gap): fixed (unreleased). The caller was the test command
  `coop.abilities reach` (the return address above the game function's frame is coop.dll `c_abilities`); it now
  calls the game only on a live creature of the object list (BUG-0917). The game's own ability use reads the same
  stale field (the game never clears the avatar's target when its creature is destroyed): the mod clears it at the
  destroy (BUG-0918). docs/re/creature.md#creature-avatar-stale-target, test `avatar_stale_target`.

## Tests: commands that read last tick's tables without the game's object list (2026-11-11, fix/w47-target-gap)
<a id="issue-test-commands-stale-tables"></a>

* What happens: nothing seen yet. A code audit for BUG-0917 (console commands run between frames) found two more
  commands of the play bots that use pointers of the mod's tables of the last tick with only `object_alive` (a read
  of the pointer's own +0x21) or no check: `coop.targets all` (npc::sync_entries, npc.cpp: `g_host_npcs` with
  `g_seen_now`, the proxies; tribe.cpp's entries: reads of position and orientation only, no calls) and
  `coop.status` in Tribe (tribe.cpp status: `game::scale(c)`, a virtual call, on the chiefs of `g_chief` without
  `active()`; coop_long_session.py asks it right after stage changes).
* Reproduce: not reproduced. `g_seen_now` is not rebuilt while the simulation stands (pause, the death caption, a
  cinematic); the guest's proxy table is checked against the object list once a second.
* Known: `coop.npc list` had the same pattern with virtual calls (body_print, noun_id on a cached herd) and checks
  the object list since BUG-0917. Safe by the audit: `coop.targets` (plain), `coop.npc`, `coop.team`, `coop.avatar`,
  `coop.status` in Creature, `coop.social`, `coop.dna`, `coop.cam`, the other forms of `coop.abilities`.
* Not checked: `coop.uistate`, civ::for_each_target, spacecoop::nav_entries.
* Next steps: the same one-pass check against `game::for_each_object` in sync_entries and the tribe status.
* Ticket: BUG-0919. FIXED 2026-11-12 (fix/w48-rest, `npc.entries_listed`): reproduced for the Tribe status (a drop and
  the status in the same pump); the entries and the status ask the game's object list.

## Key-frame review of the 0.7.12 final check (2026-11-11, plan/w47-frames)
<a id="known-keyframes-0712"></a>

Found by eye in the sheets of lab 20261111-044257 (54 sheets); details and how to test in the tickets.
* Creature, creature.mode host: the guest's screen is black with no HUD 15 s after the host's evolution and the
  guest's own refused evolution request (one_creature, 3 of 4 runs; the test passes on the mode). Ticket: BUG-0920.
  FIXED 2026-11-11 (fix/w47-one-creature-black, `core.evo_flag_clear`): the refused request left the game's
  editor-pending flag set (docs/re/creature.md#creature-editor-pending-flag).
* Tribe: after a Creature -> Tribe change in the session the guests' roster shows white squares for every member,
  the host's has the portraits (campaign, campaign_ct; not with the Creature fixture's species). Ticket: BUG-0921.
  Fixed 2026-11-11 (fix/w47-roster-portraits, creations.bundle_png).
* Tribe: the host sat in the game-over caption ("Alas! Your population...") for the last 40 s of play_tribe while the
  guest played on; the test's game-over rule counts the guest's members and never pressed OK. Ticket: BUG-0922.
* The host shows "X appeared in the world" twice within 0.4 s at a join (cosmetic). Ticket: BUG-0923.
  Fixed 2026-11-11 (fix/w47-roster-portraits, notify.appeared_once).
* Tests: seven key frames are shot in an editor, a cinematic or a pause menu, or with the camera on a bare slope.
  Ticket: BUG-0924. FIXED 2026-11-12 (fix/w48-rest): key frames wait for the games to settle (docs/06-testing.md#testing-w48-rest).

## Creature: a partner's name tag floats well over a low, long creature (2026-11-11, fix/w47-one-creature-black)
<a id="issue-creature-tag-height"></a>

* What happens: the name tag of a partner stands 1.5-2 m over its creature when the creature is low and long (the
  Creature fixture's red species): with the camera close and steep the tag is drawn over the background, about a
  body length away from the creature, and reads as a tag with no creature under it.
* Reproduce: `one_creature`, key frames `host_creature` and `evolved` (host: "Bob", guest: "Alice"); the same
  place on the screen in lab 20261111-044257 and 20261111-064331, so not a timing of the puppet's
  respawn. A tall creature (play_creature, the guest's own) has the tag just over its head.
* Known: the tag's height is `bounding_radius * 1.3 + 0.3` over the creature's origin (modes.cpp creature_tag):
  the radius of a long body is its half length, not its height.
* Not checked: the model's real height (a bounding box) as the source.
* Next steps: none planned (cosmetic).
* Ticket: BUG-0929 (fixed on fix/w48-labels, unreleased: `creature.tag_height`, the tag at the creature's posed head;
  the radius was not the cause: the rest-pose box and the radius are both a metre over the crouched body,
  docs/re/creature.md#creature-posed-top-portrait)
## Creature: NPCs keep plain pointers to a partner's puppet that was destroyed in a fight (2026-11-11, fix/w47-final12)
<a id="issue-creature-stale-puppet-pointers"></a>

* What happens: nothing seen yet (no fault, no stop in 20 removals with the puppet's block freed). The fields are a
  use of freed memory if the game reads them later; the stimulus fields of the same kind ended the host's game
  (BUG-0456, BUG-0926).
* Reproduce: `SPORE_LANE=2 python3 tools/tests/coop_stale_refs.py --rounds 6 [--kill-first --watch 6]`: the lines
  `ai noun=018EB45E +0x694 / +0x6D8 / +0x68C`, `object noun=018EB45E +0x354..+0x370`, `+0x5D0`.
* Known: docs/re/creature.md#creature-stimulus-attacker (the holders, the block's lifetime).
* Not checked: what the fields are, who reads them.
* Next steps: name the writers in Ghidra, clear the fields in the destroy hook (mods/coop/gamefixes.cpp).
* Ticket: BUG-0930. FIXED 2026-11-12 (fix/w48-rest, `npc.stale_last_attacker`): the "ai" holders were the creature's
  cModifiers (free slots' stale data, never read); the last attacker's reference kept the block alive and is released
  now (docs/re/creature.md#creature-stale-holders).
## Seen in the key frames of the second 0.7.12 final check (2026-11-11, plan/w47-frames2)
<a id="issue-frames2-0712"></a>

* Tribe: the guest's roster shows its members' cards red with an empty health bar after minutes of play while the
  host's roster has them healthy (play_tribe mid and end, campaign guests 2 and 3; lab 20261111-090547 and
  20261111-044257). Not analysed. Ticket: BUG-0935
* Tribe: members at rest at the fire sit or crouch on one game and stand on the other, both ways (same_view_tribe
  views 2 and 3, tribe_name_tags). Probably the idle animations are not relayed, as in BUG-0914. Ticket: BUG-0936
* Creature: the guest's HUD portrait keeps the first species after its creature changed (appeared_once) or after the
  host's evolution in creature.mode host (one_creature). Ticket: BUG-0937 (fixed on fix/w48-labels, unreleased:
  `creature.hud_portrait`, docs/re/creature.md#creature-posed-top-portrait)
* Civ: the galaxy menu's logo and legal lines over the play of a guest that joined 11 s after its menu came up
  (campaign_space, seen once). Ticket: BUG-0938. FIXED 2026-11-12 (fix/w48-rest, `world.menu_splash`,
  docs/re/creature.md#creature-menu-splash)
* Tribe: right after Creature -> Tribe the host draws an arrow "504 m" to the planet's centre for a guest that has
  no chief yet (campaign, one frame). Ticket: BUG-0939
* (2026-11-11, fix/w47-arrow-origin) BUG-0939 fixed: a partner without a place in this world (no chief yet and its
  pointer never put on the planet: position 0 0 0) has no arrow, blip or name (`find.placed`).

## Civ: a guest's vehicle purchase made no vehicle and no refusal while four players bought at once (2026-11-11, campaign, fix/w47-final12b)
<a id="issue-civ-buy-lost-at-once"></a>

* What happens: play_civ printed `PROBLEM Bob bought a vehicle in city 0 ... and the host made none (money 1000.0 ->
  0.0)` once (rel/0.7.12 lab 20261111-090547, campaign, 09:56).
* Reproduce: not reproduced; four bots buying vehicles in the first seconds of the Civ play with 3000 shared money.
* Known: three purchases of 1000 were made on the host in 14 s, `rejected=0`, `buys_refused=0`, the guest's
  `cmds_sent=0` shortly before; the last 1000 went to another player's purchase around the click. Inferred: the
  guest's own game refused the click for lack of money and nothing was sent (a race the bot's check does not know).
* Not checked: the guest's button state and money at the click.
* Next steps: play_civ `buy` counts "the money ran out meanwhile" by the guest's own money and `cmds_sent`.
* Ticket: BUG-0940. FIXED 2026-11-12 (fix/w48-rest): the race made on purpose (civ_buy_race): the guest's own game refuses
  the click, nothing is sent or lost; play_civ counts it as buy_broke.

## Space: a connected guest is not taken along when the host loads another Space save (2026-11-11, fix/w47-final12b)
<a id="issue-space-load-not-followed"></a>

* What happens: the host loads another Space save while a guest is connected in Space; the guest keeps playing its
  copy of the old save (the host's ship is shown there) until it leaves and joins again.
* Reproduce: two games in the space fixture; copy the host's slot to Game9 with the .spo renamed; `coop.load Game9
  <name>` on the host (lab 20261111-105829, 20261111-105831: no "receiving world" on the guest before its rejoin).
* Known: since BUG-0933 the guest hears the host's new save and holds the shared money meanwhile (docs/07-modes.md#modes-host-save-follow).
* Not checked: whether the host offers the world at all in this case, what a guest in another star system does.
* Next steps: decide whether the guest follows (as in the gated stages) or is told.
* Ticket: BUG-0941
* Fixed (2026-11-12, fix/w48-space): the guest follows; the host shares its world when it loads another Space save
  file (`space.load_follow`, docs/re/space.md#space-s1-0713).

## Tribe: a fault storm in the game's resource code on a guest after the host loaded its Tribe world again (2026-11-11, campaign, fix/w47-final12b)
<a id="issue-tribe-reload-fault-storm"></a>

* What happens: 813 caught faults `0xC0000005 at 0x008E8230 Resource::PFIndexModifiable::GetFileCount (read 0x154)` on
  a guest in Tribe, a 2 s hitch and a fault storm report; the game goes on.
* Reproduce: seen once, fix/w47-final12b lab 20261111-113158 (campaign, lane 2, Bob), 14 s after the host's reload of
  its Tribe world while the guest realigned (villages and tools removed, BUG-0564).
* Known: calls 00C90741 00C30069 00CD3770 00BA266E 00BA1EE0 00B5D229 00CDF46C; in no other log on this machine.
* Not checked: the callers, which index is null.
* Next steps: Ghidra on the callers; a test with a host reload in Tribe and a realigning guest.
* Ticket: BUG-0942 (fixed on fix/w47-getfilecount, unreleased: realign.village_goal. Not resource code: the
  guest's goal card was on a village its realign destroyed, the card's text function reads the hut of a tribe without
  members with no null check; docs/re/tribe.md#tribe-gone-village-mission; test tribe_bounce_goal)

## Tribe: a member thrown up by a hard blow stays on the ground on the guests' screens (2026-11-11, fix/w47-final12c)
<a id="issue-tribe-member-toss-guests"></a>

* What happens: on the host a member that takes a blow of 60 hp and more flies about 5 m up and lands 1.5-3 s later;
  the guests see it stand where it was.
* Reproduce: tribe_member_air (prints "the guest's mirror of #1 was at most 0.0x m over its ground"), or
  `coop.tribe pincer <leader> <other member> 100` on the host of the tribe fixture.
* Known: the mirrors keep their own height (BUG-0329, BUG-0444); the host's height over its ground is not sent for
  members. docs/re/tribe.md#tribe-member-toss.
* Not checked: the mirror's animation during the throw.
* Next steps: 0.7.13: a height-over-ground trailer for members while they are up.
* Ticket: BUG-0946

## Tribe: after a rejoin in Tribe the guest's ground round a village the host made live is 4.5-4.9 m off (2026-11-11, fix/w47-final12c)
<a id="issue-tribe-rejoin-village-ground"></a>

* What happens: the rejoined guest's animals (and whatever stands there) are 4.5-4.9 m lower than on the host round
  the village; everything stands on its own ground, so it shows as objects cut by or floating over the other
  player's view of the slope only where the two views are compared (the campaign's "ground height differs" note).
* Reproduce: seen once, rel/0.7.12 lab 20261111-151535 (campaign, Carol, rejoin at s 1473 in the Tribe play, the
  village made by the host at s 1290.9 at (-43.37 -504.67 -98.57)).
* Known: the host's stamp came from 0x0109DFBF, the rejoined guest's from 0x01091981
  (docs/re/tribe.md#tribe-terrain-stamps); the guests that were there when the village was made agree with the host.
* Not checked: the share save's stamp list at the rejoin; whether a second load on the guest evens it out.
* Next steps: 0.7.13: a two-game test (the host forms a village, the guest leaves and joins, `coop.terrain` round it).
* Ticket: BUG-0947

## Creature: the host's name tag over empty grass on the guest's screen (2026-11-11, plan/w47-frames3)
<a id="issue-creature-tag-empty-grass"></a>

* What happens: on the guest the tag "Alice" stands on an open slope with no creature under it; the only creature of
  the host's species in the picture is about 450 px away. On the host both creatures stand 2-3 m apart, tagged.
* Reproduce: seen once, rel/0.7.12 lab 20261111-151535, play_creature "mid" frame
  (`out/lab/20261111-151535/frames/sheets/038_play_creature_02_mid.jpg`).
* Cause (2026-11-11): not a missing or stale puppet. The labels of the overlay (name tags, arrows) are projected with
  the viewer as it is at Present, which is ahead of the picture just drawn while the camera turns; at rest the tag
  is over the creature. The same in v0.7.11.2. The play bots' camera drags make it hundreds of px; for a player it
  should be a tag that slides a little during a turn (not measured).
* Reproduce: `tools/lab.py run tag_camera` (shots inside a camera turn: `frames/tag_camera/tagcam_turn_N.jpg`).
* Ticket: BUG-0948 (fixed on fix/w48-labels, unreleased: SporeKit `kit.fix drawncam`, the labels projected with the
  camera the frame was drawn with, docs/re/graphics.md#graphics-drawn-camera)

## Find each other: arrows and name tags drawn over each other and over the HUD (2026-11-11, plan/w47-frames3)
<a id="issue-find-labels-overlap"></a>

* What happens: two partners in one direction get their arrow labels on top of each other (unreadable); an arrow can
  sit inside a notification card; Civ unit tags stand on the minimap and the bottom bar.
* Reproduce: 3-4 players; rel/0.7.12 lab 20261111-151535 sheets 016 (campaign_ct play-tribe, Bob), 017
  (campaign_short play-cell, Bob), 019 (campaign_short play-creature, host), 036 (play_civ end, host).
* Next steps: a layout pass for the labels and HUD rectangles they keep out of.
* Ticket: BUG-0949 (fixed on fix/w48-labels, unreleased: `find.layout`, docs/08-ui.md#ui-label-layout)

## Tribe and Civ: kept pointers checked by the object's own memory (2026-11-12, fix/w48-freed)
<a id="issue-tribe-civ-kept-pointers"></a>

* What happens: nothing seen yet; about 60 places in tribe.cpp and civ.cpp keep a game pointer across frames (chiefs,
  the target ring, the epic, held tools, vehicle and city tables) and check it with `object_alive`, which a freed
  block or a new object of the same type at that address passes (the class of BUG-0842 / BUG-0844, which crashed
  players' games).
* Reproduce: not tried per site; the tools are `coop.npc reusehunt` / `coop.tribe reusehunt`
  (docs/06-testing.md#testing-freed-block-reuse).
* Known: docs/re/creature.md#creature-freed-block-reuse (a block comes back within 0.3-1.3 s).
* Next steps: `game::LiveRef` at each site, a lint against `object_alive` on stored pointers.
* Ticket: BUG-0951
* Fixed on fix/w48-freed2 (2026-11-12): checked per site (BUG-0951's six groups); the reachable ones were the Tribe
  mirrors' other users (BUG-0955); the kept tables and references keep the serial now (`tribe.kept_refs`,
  `civ.mirror_lifetime`).
## Fixed on fix/w48-saves (2026-11-12): saves and the world share
<a id="issue-saves-w48-fixed"></a>

* Fixed (0.7.13): BUG-0846 (the saved box watcher and the game's questions), BUG-0804 (the mouse after the box),
  BUG-0806 (an OK outside the window), BUG-0847 (unreadable save files in a share), BUG-0848 (the staged move on a
  brief lock), BUG-0849 (a share save in the end of Tribe: reproduced as the fault at 0xBCAB07), BUG-0854 (saves and
  the quit in an adventure: reproduced at GetPlanetRecord+0x2D). The entries above that name them are history.
* Not reproduced: BUG-0817 (the guest's UFO mirror does not come back from a Creature save).

## Space: a guest that earned a badge faults when it loads the host's world at a rejoin from the same game (2026-11-12, test, fix/w48-reports)
<a id="issue-space-badge-rejoin-fault"></a>

* What happens: the guest earns a badge, leaves, joins again from the same running game; 4 ms into its load of the
  host's world (slot Game2) it faults at a changing address outside the exe (0xC0000005 / 0xC0000096 / 0xC000001D,
  edx=0x00420065) and dies without a crash report. With space.keep_badges on and off.
* Reproduce: `BADGES_RESTART=0 SPORE_DETECTOR=20 python3 tools/lab.py run space_badges_rejoin --lane 4 --retry 0`
  (9 of 10 runs).
* Known: not with the guest's game restarted before the rejoin; not for a host that earned a badge and loads its own
  save; not without an earned badge (space_items_rejoin). Inferred: something made at the badge's earning (its
  caption, card or event log entry) is used after the old Space world is gone.
* Next steps: why no crash report is written; the same in a solo game.
* Ticket: BUG-0956
## Cell: a partner's poison clouds hurt the other player (2026-11-12, test: fix/w48-cell)
<a id="issue-cell-partner-poison"></a>

* What happens: next to a player whose cell has a poison part, the partner's cell takes damage from those clouds in its
  own game and is knocked out, although players do not hurt each other in Cell.
* Reproduce: `coop_cell_ko_poison.py --who guest` without the guest's `coop.pool invulnfor` (the host's cell grafted
  with the poison part, the guest 6 m away): knocked out within ~20 s (lab 20261112-070110).
* Known: the host's clouds reach the guest as mirrors; the guest's game applies their contact to its own cell
  (inferred). docs/re/cell.md#cell-c1-0713.
* Not checked: the guest's clouds (made by the host for its puppet) on the host's cell.
* Ticket: BUG-0958
* Fixed (unreleased, fix/w48-pass, 2026-11-12): cell.partner_poison; the guest's clouds never hurt the host's cell
  (the game spares its avatar from them). docs/re/cell.md#cell-partner-poison.

## Cell: the host's trace calls a refused kill "applied" (2026-11-12, test: fix/w48-cell)
<a id="issue-cell-kill-trace"></a>

* What happens: the support trace's eat line says "applied" for a knocked-out guest's kill that the next line refuses.
* Reproduce: `coop_cell_kill_refused.py`, the host's game log.
* Ticket: BUG-0959
* Fixed (unreleased, fix/w48-pass, 2026-11-12): the line says "refused (knocked out)" (cell.ko_trace).
* Fixed (unreleased, fix/w48-new, 2026-11-12): a destroyed window in the window manager's list of modal windows that
  PopModal made the modal one (game.dead_modal_list); no report because Wine cannot dispatch a fault with esp outside
  the stack (kit.fix offstack writes one now). docs/re/space.md#space-modal-list-crash.

## Creature: the attack key sometimes does nothing; a stop beside the partner without a bite (2026-11-12, test: fix/w48-pass)
<a id="issue-creature-walk-use-stuck"></a>

* What happens: (a) after the attack key the guest's creature does not move toward its target at all, also on further
  presses (12 s); (b) with friendly fire off, coming past the partner's creature it stops beside it 1.4-2 m from the
  animal and does not bite.
* Reproduce: `coop_creature_partner_pass.py` prints them ("stopped by: no start", "beside the partner"), about 1
  round in 5 for (a) and 1 in 15 for (b). docs/re/creature.md#creature-partner-pass-rel.
* Ticket: BUG-0964, BUG-0965
* Fixed (unreleased, fix/w48-walk, 2026-11-12): not a player bug. The test's `coop.npc gopos` put the guest's creature
  at the terrain's height under a nest mound; inside the mound's collision Havok held it in every direction (a), also
  after a few steps toward the animal (b). gopos puts the point onto the game's surface (tools.gopos_surface); test
  walk_start. docs/re/creature.md#creature-walk-held-in-mound.

## Tests: unit's LAN discovery check can fail while another lane runs unit (2026-11-12, test: fix/w48-walk)
<a id="issue-unit-discovery-parallel"></a>

* What happens: `unit` fails in tests/net_tests.cpp test_discovery (`found.size() == 1`) once in a smoke while other
  lanes ran their smokes; a rerun passes.
* Reproduce: `unit` on two lanes at the same moment (the test hosts on a fixed port of 127.0.0.1).
* Ticket: BUG-0969

## Civ: wild creatures drift apart between host and guest (2026-11-12, rel/0.7.13)
<a id="known-civ-wild-creatures"></a>

* What happens: in the Civ stage wild creatures are not synced. Herds move, flee and die separately in each game, so the
  guest can see (and click) a creature the host's game no longer has there. Since BUG-0970 such an order is not lost.
* Reproduce: play_civ for ~9 min, compare `coop.census creatures` on both games near a herd.
* Ticket: BUG-0973
* Fixed (unreleased, fix/w48-last, 2026-11-12): the checks hosted on the fixed port kDefaultPort + 19 and asked the
  whole range, where another copy of the tests and the other lanes' games (they answer 127.0.0.1) live. They host on
  an ephemeral port and ask only it (Discovery::ports); test unit_parallel (3 copies at once).

## Tribe: a same-view still shows the host's avatar and a chief in other poses on the guest (2026-11-12, test: fix/w48-last)
<a id="issue-tribe-same-view-avatar-pose"></a>

* What happens: same_view_tribe view 1 (lab 20261112-172240, run 1 of 3): the host's creature crouches at the fire on
  the host, its puppet on the guest stands about 1 m off in a stepping pose; Bob's chief is mid-step on the guest only.
* Reproduce: `lab.py run same_view_tribe --repeat 3`, compare the 01_view1 pairs.
* Known: seen once; the other runs' views agree. Not checked whether it is an animation phase at the still's moment.
* Ticket: BUG-0972
* Fixed (unreleased, feat/lab-speed, 2026-11-12): the test hosts on a free port of the discovery range under its own
  process's name and counts only that game (docs/06-testing.md#testing-lab-speed).

## Cell: a guest at a high level dies when it joins a host world at a lower one (2026-11-12, crash: feat/lab-speed)
<a id="issue-cell-join-lower-level"></a>

* What happens: the guest's food drops from the level it had (327, level 4) to the host's (0) at the join, and the
  next tile pass faults in `Simulator::Cell::ShouldCullCell+0x190` (0xC0000005 at 0x00E5FA50, from PopulateTiles);
  the guest's game is gone without a crash report.
* Reproduce: two games in the `cell` fixture, food raised to level 4 (cell_particles does this), then the host loads
  the fixture again and the guest rejoins (the lab's warm reset). 27 more games lost in warm resets of the recorded
  runs, 25 in Cell (mostly cell_coop), probably the same.
* Ticket: BUG-0975
* Fixed (unreleased, fix/w48-cellcull, 2026-11-12): the game's own bug (a game alone dies on the same fall), reached
  only through the mod's shared food total. PopulateTiles writes its active set into a stack buffer of 511 without
  a bound; after a fall of several levels the world scale is still the old one for ~3 s and the set has thousands
  of tiles. No tile pass runs now while the scale is more than one level above its target (`cell.tile_cap`); test cell_level_drop. docs/re/cell.md#cell-level-fall.

## Tests: cell_levelup_view fails about half its runs on the level-ups (2026-11-12, test: fix/w48-cellcull)
<a id="issue-cell-levelup-view-ups"></a>

* What happens: on a +150 change the guest loses 70-100 % of the cells on its screen while the host's game loses
  0-60 %; the test's average check fails in about half the runs (also before BUG-0975's fix, and with its switch off).
* Reproduce: `tools/lab.py run cell_levelup_view --lane N --repeat 4`.
* Ticket: BUG-0976

## Civilization: a guest's city purchase asks the host for the city's specialty (2026-11-12, test: fix/w48-fmisc)
<a id="issue-civ-guest-buy-specialty"></a>

* What happens: the guest buys an NPC city; the host's game shows "You've bought out this military city ... Choose
  the city's specialty" (a modal box) and stands paused until the host answers; the guest is never asked.
* Reproduce: `tools/lab.py run civ_buy_city` (it prints `specialty question: host Military | guest None`); key
  frame 026 of rel/0.7.13 lab 20261112-175447 (the box on the host only, `busy host: paused` for 20 s).
* Known: the purchase is made in the host's game for the guest (`buys_done` on the host, civ.buy_city BUG-0877), so
  the game's own question (0xCF7A30, capture type != specialty: box 0xBD9330 + Pause, docs/re/civ.md section on
  GameCiv_IsPlanetOwned) comes up there. In civ_owners_reload a box left up by the test survived the host's quit to
  the galaxy menu and the load of the save (key frame 030); a player cannot quit with the box up the same way (not
  checked).
* Next steps: answer the box on the host by the guest's choice (forward the question to the buyer), or take the
  city's own specialty for a guest's purchase.
* Ticket: BUG-0982
## Tests: walk_start_walkin with every fix off crashed in DXVK after two caught memcpy faults (2026-11-12, test: fix/w48-d3d9)
<a id="issue-fixoff-dxvk-crash"></a>

* What happens: 18 ms after `coop.fix all off` (solo, Creature, the partner gone 3 s before) the host's game catches
  a fault in ucrtbase memcpy on the main thread (the console's frame), another 1 s later, then crashes in DXVK 3.1.1's
  CS thread (d3d9.dll+0x11395B, a command list with a null command pool: damaged heap). Seen once (lab
  20261112-175447); the same test passed on fix/w48-walk. Only with every fix off; not seen with fixes on.
* Reproduce: `tools/lab.py run walk_start_walkin --lane N --repeat 10` (not reproduced yet).
* 2026-11-12 (fix/w48-fui): seen again on lane 3 (rerun 20261112-205503 #1, a caught fault only); 6 clean runs on lanes 4
  and 5 (walk_start_walkin, fixoff_clean). The fault log now prints `module calls:` (return addresses into our DLLs)
  to name the caller next time.
* FIXED (2026-11-12, fix/w48-fui): the cause was core.adapters' off side (Wine's GetAdaptersAddresses on the main
  thread, BUG-0286); the switch is unsafe now, `all off` leaves it on.
* Ticket: BUG-0977

## Creature: a joining guest's creature stands inside the host's creature (2026-11-12, key frames: fix/w48-fui)
<a id="issue-join-same-spot"></a>

* What happens: on a join into Creature both creatures stand at one point, drawn inside each other, until one walks;
  the two games show the pair in other poses.
* Reproduce: any test on the creature fixture (key frames of play_creature start, same_view, find_layout).
* Known: the guest's avatar starts where the host's save has the player's creature (inferred, not analysed).
* Next steps: put the joining avatar a few metres aside on the game's surface.
* Ticket: BUG-0989

## Creature: the host's camera looked into the ground at the end of a campaign creature play (2026-11-12, key frames: fix/w48-fui)
<a id="issue-camera-in-ground"></a>

* What happens: the host's picture was only terrain (no own creature), the guests' normal. Seen once
  (lab 20261112-175447, campaign_short play-creature).
* Reproduce: not known.
* Ticket: BUG-0990

## Tests: play_tribe's planted desync no longer fails it (2026-11-12, test: feat/lab-speed)
<a id="issue-play-tribe-plant"></a>

* What happens: `PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6"` should make play_tribe fail its `sync t*` check (the
  guest's mirrors 0.6 s late); it passes at 5 and 10 min, the probe's best lag is 0.00 s. play_tribe has no known
  FAIL case.
* Reproduce: `PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6" tools/lab.py run play_tribe --lane N`.
* Ticket: BUG-0974
## Space: switching Rivals to Allies while a guest is joining gives the wrong shared money (2026-11-13, audit 0.7.14)
<a id="issue-rivals-merge-unknown-budget"></a>

* What happens: in Rivals a guest joins or rejoins; the host switches to Allies before the guest's game is in Space
  and has its budget back. The merge adds the guest's whole current money (the host's save money, or the shared
  total) instead of its budget; a rejoiner's own budget is lost.
* Reproduce: not run; switch to Allies while the guest loads (steps in the ticket).
* Known: by code (space_rivalry.cpp on_state / host_apply / apply_pending_money); the doubling case is inferred.
* Ticket: BUG-1029
* Fixed (unreleased, fix/w49-money, `space.merge_known_budget`): the merge credits every guest (a rejoiner its kept
  budget, a newcomer 0); a guest with an unknown budget sends no correction; a newcomer to Rivals starts with 0.

## Space: in Rivals at war, the host's escorts and a guest cannot damage each other (2026-11-13, audit 0.7.14)
<a id="issue-war-escort-hits"></a>

* What happens: a guest at war with the host is attacked by the host's escorts and Defenders, but its ship takes no
  damage; its shots at the host's escorts do nothing either.
* Reproduce: Rivals, declare war, fly near the host's escort; not run.
* Known: by code, the BUG-0420 friendly filter (squadron.cpp) and the host's empire id given as the attacker.
* Ticket: BUG-1032

## Space: a gift or a sale right after a guest's rejoin is lost (2026-11-13, audit 0.7.14)
<a id="issue-trade-before-restore"></a>

* What happens: a tool gift, a spice sale to the guest's colony or an offer the guest accepts in the first seconds
  after its rejoin goes into the hold that its kept hold then replaces; the sender was told it arrived.
* Reproduce: give a tool within about 20 s of the guest's rejoin; not run.
* Known: by code, K_TOOL / K_SELL / answer_offer do not wait for inv_restore_pending (space_trade.cpp).
* Ticket: BUG-1034

## Space: old partners' colonies come back as allies in a new session (2026-11-13, audit 0.7.14)
<a id="issue-allies-end-pending"></a>

* What happens: if a session ends while the player is on an ally's mirror planet and the player then goes to the
  menu and joins or hosts another Space session, the old partners' mirror colonies appear as frozen allies.
* Reproduce: not run; the steps in the ticket.
* Known: by code (space_allies.cpp tick returns outside Space before the pending cleanup); trigger 5 of BUG-0038.
* Ticket: BUG-1036

## Space: a mission reward tool is never given while the host has Space contracts off (2026-11-13, audit 0.7.14)
<a id="issue-owed-tool-contracts-off"></a>

* What happens: a partner's mission reward tool that comes while the guest is outside Space is owed and never paid
  while the host's session settings have Space contracts off.
* Reproduce: not run.
* Known: by code, owed_tick runs only in the contracts feature's tick.
* Ticket: BUG-1037

## Space: an armada wave without ships waits forever (2026-11-13, audit 0.7.14)
<a id="issue-armada-empty-wave"></a>

* What happens: if a wave's raid brings no ships in 30 s, the contract resumes the same raid every 30 s instead of
  starting a new one.
* Reproduce: not run (needs a raid that brings no ships).
* Known: by code, space_contracts.cpp armada_tick.
* Ticket: BUG-1038

## Space: the host's colony spice drops after a world load in the session (2026-11-13, audit 0.7.14)
<a id="issue-colony-spice-old-world"></a>

* What happens: after a guest loads another world in the same session (or after a Rivals period), its older and
  lower colony spice values are sent as "collected" and written into the host's colonies.
* Reproduce: not run.
* Known: by code, space_planets.cpp g_gc is not tied to the world epoch.
* Ticket: BUG-1039
* Fixed (unreleased, fix/w49-money, `space.colony_spice_epoch`): the guest's last-known values are cleared by another
  world or a Rivals period.

## Space: a partner's terraforming can make another player's game fault (2026-11-13, audit 0.7.14)
<a id="issue-terra-rec-unchecked"></a>

* What happens: when a partner terraforms, the other games write the planet's record through the game's
  GetPlanetRecord with no checks. A game that has not generated that star's planets, or is not in Space, can fault
  (a null read in the game).
* Reproduce: not seen. A partner terraforms a planet of a far star while you are elsewhere or load another world.
* Known: spacecoop.cpp apply_terra_rec (inferred from code; BUG-0284 shows the game's read does no checks).
* Ticket: BUG-1042

## Space: "Fly to" a partner at the galactic core costs energy (2026-11-13, audit 0.7.14)
<a id="issue-finale-fly-energy"></a>

* What happens: from the galaxy view the finale's "Fly to" uses the game's paid travel. Energy is spent, and a ship
  with no energy takes damage. A ship disabled after the order still flies.
* Reproduce: not seen. Near the core, galaxy view, low energy, "Fly to" a partner.
* Known: space_finale.cpp fly_step (observed in code).
* Ticket: BUG-1045

## Space: a guest sees a host NPC ship fire a burst of 4 shots for one (2026-11-13, audit 0.7.14)
<a id="issue-space-shot-burst"></a>

* What happens: a host ship that fires again 1 to 2.3 s after a pause shows 4 shots on the guest (or none).
  Cosmetic: the copies hurt nothing.
* Reproduce: not seen. Watch a host escort or pirate that fires in short bursts.
* Known: the host's shot count restarts at 1 while the guest still has the old count (observed in code).
* Ticket: BUG-1046

## Space, 3-4 players: "Give 10 to <player>" gives the spice to someone else (2026-11-13, audit 0.7.14)
<a id="issue-space-give-recipient"></a>

* What happens: the button names co-op's give target (a pick made in Tribe or Civ stays), but the spice goes to the
  first other player of the session. In Space the ">" picker is hidden.
* Reproduce: not seen. 3 players: pick the third player with ">" in Civ, go to Space, click Give.
* Known: give_spice ignores the recipient (observed in code).
* Ticket: BUG-1050

## Creature, Tribe: a fight sound or effect can keep playing on a guest's copy of a host creature (2026-11-13, audit 0.7.14)
<a id="issue-mirror-fx-keys"></a>

* What happens: the host's list of effects it relayed a start for is never cleared. When it fills (256), the stop
  of one effect is no longer sent, and that effect keeps running on the guest's copy.
* Reproduce: not seen; long Creature or Tribe sessions with many fights.
* Known: mirror_fx.cpp g_keys (observed); whether the list fills in real play is inferred.
* Ticket: BUG-1049

## Creature, Tribe: a host animal knocked aside slides back on the guest instead of jumping (2026-11-13, audit 0.7.14)
<a id="issue-npc-snap-dead"></a>

* What happens: when the host's animal is thrown aside or put back by its game while it walks, the guest's copy
  closes the 2-7 m at catch-up speed over up to about 2 s instead of being placed there at once.
* Reproduce: not run; npc_walk_player with a host-side 5 m teleport of a walking NPC.
* Known: by code (high confidence): npc.place_now (BUG-0757) aims the proxy one frame ahead, so drive::walk_place's
  snap test (driver.h) never holds for a moving proxy.
* Ticket: BUG-0997

## Tribe: after joining another (or a restarted) host the guest sees no Tribe updates (2026-11-13, audit 0.7.14)
<a id="issue-tribe-cycle-not-reset"></a>

* What happens: a guest that played Tribe with one host and joins another host, or the same host after it
  restarted its game, without restarting its own game drops every Tribe snapshot as an older cycle: the host's
  members do not move, appear or go on the guest until the new host's counter passes the old one.
* Reproduce: play Tribe as a guest, leave, restart the host's game, join again; `coop.status` on the guest shows
  `older_chunks=` growing.
* Known: tribe.cpp g_cyc / g_cycle_ids are never reset (code reading, not seen at runtime). Workaround: restart the
  guest's game before joining.
* Ticket: BUG-1051

## Tribe: a chief "saved" by a partner's member dies at the next blow (2026-11-13, audit 0.7.14)
<a id="issue-tribe-chief-rescue-hp"></a>

* What happens: with the Chief aura setting on (the default), "<A> saved <B>'s chief" leaves the chief 0.3 hit
  points instead of 30 % of its health.
* Reproduce: a partner's member next to a chief that takes a killing blow (code reading, not seen at runtime).
* Known: the rescue reads the maximum from the citizen's +0x5D4 (1.0), the field BUG-0745 found wrong for the heal.
* Ticket: BUG-1052

## Tribe: the host's game reloads its save once in a later session (2026-11-13, audit 0.7.14)
<a id="issue-tribe-stale-host-reload"></a>

* What happens: if a session ended in the seconds after the host's live Creature to Tribe change (before the world
  went out), the first world share of a later session in Tribe makes the host's game load its own save again.
* Reproduce: the guests leave right after the host enters Tribe live; then a new join while the host plays Tribe
  (code reading, not seen at runtime).
* Known: world.cpp g_reload_due survives the session end.
* Ticket: BUG-1054

## Tribe: the guest's copy of a host member keeps a tool the host's member put down (2026-11-13, audit 0.7.14)
<a id="issue-tribe-tool-zero"></a>

* What happens: the field relay drops the value 0, so "no tool" (and hunger 0) never reaches the guest. The guest's
  copy keeps the old tool type.
* Reproduce: not seen. A host member takes a weapon, then goes back to no tool; read +0x102C on both games.
* Known: fields.cpp drops every value under 0.001 (observed in code).
* Ticket: BUG-1043

## Cell: a copy of a host cell on a guest can go NaN for good (2026-11-13, audit 0.7.14)
<a id="issue-cell-mirror-nan"></a>

* What happens: on a guest, a mirror whose own soft body went NaN in the guest's cell step is never repaired: it
  drifts away or vanishes, and the guest's own cell touching it can go NaN too (then the BUG-0299 heal puts it back).
* Reproduce: not seen at runtime; 3-4 player Cell sessions are the likely place. Planned test: NaN written into one
  mirror's particles.
* Known: by code (modes.cpp cell_move adds `want - pos` with a NaN pos to every particle; cellsync.cpp h_Soft turns
  the particles by a NaN angle); no NaN check for mirrors.
* Ticket: BUG-1006

## Cell: a host creature a guest's jaw finishes off turns its kind against the host (2026-11-13, audit 0.7.14)
<a id="issue-cell-chunk-kill-host"></a>

* What happens: when the last chunk a guest's mouth tears off kills a host NPC, the host's game counts the kill as
  the host's own cell's, so that species turns on the host's cell, not on the guest.
* Reproduce: not seen at runtime; a guest with a jaw part kills a host NPC by chunks.
* Known: by code (cellsync.cpp MSG_CELL_HIT type 6 calls h_KillCell with the host's avatar as the killer; the eat
  path uses the guest's puppet).
* Ticket: BUG-1008

## Session: with late join off a new player with a name already seen (the default "Player") still joins (2026-11-13, audit 0.7.14)
<a id="issue-late-join-seen-name"></a>

* What happens: the host turned late join off and started; a new player whose name was used earlier in the session
  (two friends who kept the default "Player") joins as "Player 2". With a player limit below 4, a join with the name
  of a player that is loading (silent 2 s) goes over the limit.
* Reproduce: three games, late join off, two guests named Player; the second joins after the start (not run).
* Known: session.cpp counts any seen name as a rejoin even with the rejoin token on; the old entry of that name is
  not retired (observed in code).
* Ticket: BUG-1001

## Civ, Space: a shared money line names the wrong player or a false "earned" after two spends at once (2026-11-13, audit 0.7.14)
<a id="issue-shared-cause-floor"></a>

* What happens: when the host and a guest spend the shared money at the same moment and the pool hits 0, the
  guests' feed can say the host earned money it did not earn, or credit the whole spend to the guest; the money
  itself is right.
* Reproduce: Civ, 300 in the pool, host and guest buy at once (not run).
* Known: shared.cpp adds the guest's full change to applied/causes although the floor cut it (observed in code).
* Ticket: BUG-1009
* Fixed (unreleased, fix/w49-money, `shared.cause_clamped`): the host counts what the floor let through.

## All: the last session summary is missing from reports when the game folder has non-Latin letters (Windows) (2026-11-13, audit 0.7.14)
<a id="issue-summary-non-ascii-path"></a>

* What happens: coop-last-session.txt is not written or read when the path to the mod's logs folder has letters
  outside the ANSI code page, so the next problem report has no summary line at its top.
* Reproduce: Spore in a folder with a Cyrillic or CJK name on Windows; end a co-op session; the file is absent.
* Known: diag_stats.cpp calls fopen on a UTF-8 path (observed in code).
* Ticket: BUG-1011

## Session report: a long non-Latin host name ends in a broken character in the guest's report question (2026-11-13, audit 0.7.14)
<a id="issue-sreport-host-cut"></a>

* What happens: "<host> asks for your problem report" shows the host's name cut to 40 bytes, so a CJK name of 14
  or more characters ends in a broken character.
* Reproduce: a host named with 14 CJK characters, a session report round (not run).
* Known: sreport.cpp calls substr(0, 40) on the UTF-8 name (observed in code).
* Ticket: BUG-1013

## Update: Try again shows the last download's progress for a moment (2026-11-13, audit 0.7.14)
<a id="issue-update-retry-progress"></a>

* What happens: after a failed update, Try again first shows the old "Downloading ... N %" until the new download
  reports progress.
* Reproduce: a failing update download, then Try again (not run).
* Known: autoupdate.cpp never resets g_got / g_total (observed in code).
* Ticket: BUG-1015

## UI: text typed into the co-op window under a caption or the game saved box stops at half its length (2026-11-13, audit 0.7.14)
<a id="issue-locked-edit-utf8"></a>

* What happens: while a caption or a modal game box holds the input, the mod types the player's keys into the co-op
  text line itself. A name or chat line in Cyrillic, Greek or CJK stops at half its limit (a third for CJK), DEL is
  taken as a character, and an emoji becomes two replacement characters. Normal typing is not affected.
* Reproduce: type 30 Cyrillic letters and an emoji into the chat while a caption holds the input (not run).
* Known: locked_edit_apply (coop_ui.cpp) counts UTF-8 bytes against the UTF-16 limit and encodes each UTF-16 unit
  on its own (by code, not seen at runtime).
* Ticket: BUG-1027

## All stages: after a caught fault inside the game's Present the co-op stops while the game draws on (2026-11-13, audit 0.7.14)
<a id="issue-present-depth-stuck"></a>

* What happens: a fault inside Direct3D's Present (in the driver, or in a SporeKit task pumped by a message
  dispatched inside Present) that the game's own handler catches leaves SporeKit's Present depth set: no SporeKit
  frame runs again, so no co-op ticks, no network updates and no outside-frame console commands, while the game
  keeps drawing. A fault in an overlay handler leaves the overlay's scene open instead.
* Reproduce: not seen at runtime; needs a test command that faults inside Present.
* Known: sporekit/d3d9hook.cpp (plain g_present_depth++/-- around o_Present / o_SwapPresent; frame_cut_off does not
  reset the depth); observed in code, reachability inferred. Related to BUG-0966 (there the frames went on).
* Ticket: BUG-1024
* Fixed (unreleased, fix/w49-kit, 2026-11-13): `kit.fix presentunwind` resets the depth after the unwind and ends
  the overlay pass's scene; seen at runtime before the fix (lab 20261113-010320: no frames; 20261113-010700-2: the
  overlay pass dead, the back buffer held). docs/02-modkit.md#modkit-present-unwind
