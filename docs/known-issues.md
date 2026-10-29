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
* Ticket: BUG-0170.

## Tribe: the host froze once while the epic creature was being killed

* `coop_tribe_play.py` on branch feat/net-window, lane 2 (2026-10-22): the host stopped drawing
  during `coop.tribe epic hit` (monitor: FROZE); not reproduced in 14 more runs. The game logs of
  that run were lost (now kept by lab.py). Hunt: `lab.py run tribe_play,chaos_tribe --repeat 10`.
* 2026-10-27 (fix/w1-tribe-civ): not reproduced on main in 4 tribe_play + 4 chaos_tribe runs (lab
  run 20261027-183142, GOG 2024). Hardened on the way: the epic is checked against the game's object
  list, `epic hit` stops at a dead victim, the epic state ends with the session and the world.
  If it comes back: the game logs are kept by lab.py now.
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
* A guest without own creations gets its own editor on the host's creature as it was before the
  host's editor (the legless one at the start of the Creature stage). If it saves an edit, that
  edited pre-legs creature becomes its own (kept in `choices.ini`) while the host plays the legged
  one: by design (everybody evolves its own creature), but it may look like "the host's creature did
  not change". Cancel keeps the world's species.
* The guest's mating-call button needs a collected part (`S+0x69`): whether a guest touching a host's
  part token (a mirror) sets it was not checked; the test sets it by console.
* The report's Creature case ("3 rejoins did not fix it") did not reproduce as such on lane 5 with
  0.7.2.1: the guest saw the host's legged creature after the editor, also after a rejoin. Fixed
  causes that can show a stale creature: a creation sent without its records (the guest then shows
  its own body for the host), a rejoin applying the same version again (writing its package failed,
  nothing was applied).
* Ticket: BUG-0178.

## Creature (guest): the social game is simplified

* Since 2026-10-23 a guest befriends a mirrored NPC (click in the social stance, a social ability
  key; the host applies the native win, docs/re/creature.md 8.3), but the guest does not see the
  native social meter and the NPC's ability prompts (they belong to the NPC's behaviour tree, which
  runs only on the host): any social ability counts as the answer, one press per round. A click on
  the ability bar instead of a key is not counted (the mod polls the keys 1-4). The host's own
  game is native. The success message 0x60B4123 (CreatureStageEventHandler) is not posted for a
  guest's win. Reproduce: `tools/tests/coop_creature_social.py`.
* Ticket: BUG-0179.

## Creature: while the host is in the main menu the guest's planet fills up slowly

* The host leaves to the main menu: the guests are told and play on alone (their own game runs
  their NPCs again, `coop.npc` host_away=1), but the game respawns the removed NPCs only a few at a
  time (`local_alive` 0-1 after 30 s in `coop_exit_menu.py`). Making the mirrors local NPCs
  instead failed (a mirror with AI leaves its herd and is destroyed, docs/re/creature.md 8.3).
* Ticket: BUG-0180.

## Space: no test save after the tutorial missions

* The `space` fixture starts at the first mission ("Fly to planet Vulpica", the wreck): the ship has
  no interstellar drive (tests add it with `coop.space tool add interstellardrive`), no
  terraforming tools, no colony besides home and no other empire has been met. `space_empire`
  (docs/06-testing.md 6.15) adds the drive, money, two colonies with spice and two contacts, made
  with test commands; it is still inside the tutorial (travel to stars that are not own stars is
  refused there), and has no raids, missions or trade. A save played through the tutorial would
  still be better.
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
* Ticket: BUG-0184.

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
* BUG-0205 (bug): host in an editor or a loading screen: Cell mirrors freeze (cellsync has no host-away state, also not for the main menu). The goal reward part is fixed and the Creature side counts an editor or a load as away since fix/w4-census (not proven in game, see the ticket).
* BUG-0152 (#131, test): most 3-4 player gameplay paths have no test, and coop_multi runs in no lab suite.
* BUG-0153 (#132, bug): known-issues.md has a merge-damaged entry, stale "fixed" and outdated entries, and no ticket ids.
* BUG-0162 (#141, test): hot reload and every 3-4 player test are outside the lab suites, so they never run.
* BUG-0156 (#135, test): test profiles redirect only AppData, so tests on lane 1 can write into the owner's own Documents (My Spore Creations)
* BUG-0158 (#137, bug): the triage-crash skill and tools/crash_triage.py are referenced but do not exist.
* BUG-0165 (#144, bug): INSTALL check line, README latest version and stability-report lane count are out of date.

**Creature**

* BUG-0119 (#98, bug): after a load the guest may keep the host's species, or get SetSpecies on a world still loading (avatar pointer used as world identity)
* BUG-0125 (#104, bug): a guest that rejoins while the host is paused sees no animals near it.
* BUG-0256 (crash, caught): a guest that rejoins and loads a host save written while its puppet existed faults every frame in Herd_Update (cCreatureAnimal::Update+0xAA2) after the stray removal.

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

## Space trade: an owed refund can be paid into another game (BUG-0225, open part)
After a save of any game, a trade refund owed from a gift that came back outside Space is paid
into whatever Space game is loaded next (`spaceext::saves()` counts every save), and a reloaded
game whose cEmpire got the old address is taken for the same game. Reproduce: a profile with two
Space games; the receiver holds a tool gift (`coop.space trade hold 1`), the host saves, loads the
other game, the receiver declines: the other game's tool gets the charges. Needs the shared
after-save hook of BUG-0217 (#151) and a game key; the allies part of BUG-0225 is fixed.
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

## Creature: an NPC proxy next to a guest's avatar stays ~4 m off (2026-10-29, fix/w7-sync)

* One host NPC standing 3-5 m from the guests' avatars was 3.8-4.3 m from its interpolation target
  on every guest at once (`coop.npc list` on a guest: `err=4.33`). Likely pushed by the guest's own
  avatar (inferred). Reproduce: `SPORE_LANE=N python3 tools/tests/coop_sync_multi.py --fixture
  creature --rounds 45` and `coop.npc list` on a guest. Ticket: BUG-0281.

## Cell: a guest's mirrors trail the host's cells at a low frame rate (2026-10-29, fix/w7-sync)

* At 15 fps (`frame.limit 66`) 283 of 13165 compared cells were 1.2-2.3 m behind the host's (about
  0.65 s of their way), at 30 fps 2, at 65 fps 1. Reproduce: `FPSLIM=Carol:66 SPORE_LANE=N python3
  tools/tests/coop_sync_multi.py --fixture cell --rounds 25`. Ticket: BUG-0282.
