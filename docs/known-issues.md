# Known issues

Bugs found and not fixed yet that a player or a developer can meet: what happens, how to reproduce it, what
is known, each with its ticket id. An entry stays while one of its tickets is open (or `game-bug`: Spore itself
does it and the mod works around it or cannot). When its tickets are closed (fixed, wontfix, not-reproduced,
duplicate), the entry moves to [archive/known-issues-2026.md](archive/known-issues-2026.md) (the history, with
its anchors; BUG-1126 moved 237 entries there on 2026-11-15). Every open ticket: `tools/tickets.py list
--status open`.

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
* Fixed (unreleased, fix/w49-civ, 2026-11-13): the host owns them (npc.civ_wild, the Tribe stage's NPC proxies), test
  civ_wild_sync (docs/re/civ.md#civ-wild-sync).
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
* Fixed (unreleased, fix/w49-mirror, 2026-11-13): a relayed restart of a gesture that waited behind the running one
  came back after the next relayed animation and the idle fix left it (it was not the last relayed start): the guest's
  copy of the chief stayed in a gesture for up to 37 s while the host's stood. Such an animation is ended now
  (`tribe.mirror_stale_anim`, test tribe_mirror_stale); the gesture's moment follows the host's too (BUG-0988,
  `tribe.mirror_phase`, test tribe_mirror_phase). docs/re/tribe.md#tribe-mirror-pose-phase.
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
* Fixed (unreleased, fix/w49-civ, 2026-11-13): the question goes to the city's owner (civ.specialty_buyer), test
  civ_buy_city (docs/re/civ.md#civ-specialty-buyer).
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
* Fixed (unreleased, fix/w49-mirror, 2026-11-13): confirmed (a rejoin on the fixture: 0.06 m apart for 20 s). The
  guest's creature is put 5 m beside the host's once per arrival (`core.join_apart`, test creature_join_apart).
  docs/re/creature.md#creature-join-apart.

## Creature: the host's camera looked into the ground at the end of a campaign creature play (2026-11-12, key frames: fix/w48-fui)
<a id="issue-camera-in-ground"></a>

* What happens: the host's picture was only terrain (no own creature), the guests' normal. Seen once
  (lab 20261112-175447, campaign_short play-creature).
* Reproduce: not known.
* Ticket: BUG-0990
* Not reproduced (fix/w49-mirror, 2026-11-13): a scan of the key frames of every lab run on this machine (321 run
  folders, 886 Creature stage frames of host and guests) by sky and contrast found this one frame only; the other
  flagged frames were the death screen's top view or dark scenes. Closed; reopen with the camera's height over the
  ground when it shows again.

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
* Fixed (unreleased, fix/w49-space, `space.war_npc_hits`): at War the hits count both ways (owner decision 1).

## Space: a gift or a sale right after a guest's rejoin is lost (2026-11-13, audit 0.7.14)
<a id="issue-trade-before-restore"></a>

* What happens: a tool gift, a spice sale to the guest's colony or an offer the guest accepts in the first seconds
  after its rejoin goes into the hold that its kept hold then replaces; the sender was told it arrived.
* Reproduce: give a tool within about 20 s of the guest's rejoin; not run.
* Known: by code, K_TOOL / K_SELL / answer_offer do not wait for inv_restore_pending (space_trade.cpp).
* Ticket: BUG-1034
* Fixed (unreleased, fix/w49-space2, 2026-11-13): gifts and sales wait for the restore, offers too (`space.trade_wait_restore`); FAIL lab 20261113-022524, PASS 20261113-022524-2.

## Space: old partners' colonies come back as allies in a new session (2026-11-13, audit 0.7.14)
<a id="issue-allies-end-pending"></a>

* What happens: if a session ends while the player is on an ally's mirror planet and the player then goes to the
  menu and joins or hosts another Space session, the old partners' mirror colonies appear as frozen allies.
* Reproduce: not run; the steps in the ticket.
* Known: by code (space_allies.cpp tick returns outside Space before the pending cleanup); trigger 5 of BUG-0038.
* Ticket: BUG-1036
* Fixed (unreleased, fix/w49-space2, 2026-11-13): `space.allies_end_pending`; FAIL lab 20261113-023145, PASS 20261113-023145-2.

## Space: a mission reward tool is never given while the host has Space contracts off (2026-11-13, audit 0.7.14)
<a id="issue-owed-tool-contracts-off"></a>

* What happens: a partner's mission reward tool that comes while the guest is outside Space is owed and never paid
  while the host's session settings have Space contracts off.
* Reproduce: not run.
* Known: by code, owed_tick runs only in the contracts feature's tick.
* Ticket: BUG-1037
* Fixed (unreleased, fix/w49-space2, 2026-11-13): `space.owed_always`; FAIL lab 20261113-022716, PASS 20261113-022655.

## Space: an armada wave without ships waits forever (2026-11-13, audit 0.7.14)
<a id="issue-armada-empty-wave"></a>

* What happens: if a wave's raid brings no ships in 30 s, the contract resumes the same raid every 30 s instead of
  starting a new one.
* Reproduce: not run (needs a raid that brings no ships).
* Known: by code, space_contracts.cpp armada_tick.
* Ticket: BUG-1038
* Not reproduced (fix/w49-space2, 2026-11-13): the game keeps no empty raid running for 30 s (labs 20261113-022716, 20261113-023145); the abort is kept as hardening (`space.armada_empty_wave`). BUG-1038.

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
* Fixed (unreleased, fix/w49-space, `space.terra_rec_checked`): only a generated planet of a known star, in Space.

## Space: "Fly to" a partner at the galactic core costs energy (2026-11-13, audit 0.7.14)
<a id="issue-finale-fly-energy"></a>

* What happens: from the galaxy view the finale's "Fly to" uses the game's paid travel. Energy is spent, and a ship
  with no energy takes damage. A ship disabled after the order still flies.
* Reproduce: not seen. Near the core, galaxy view, low energy, "Fly to" a partner.
* Known: space_finale.cpp fly_step (observed in code).
* Ticket: BUG-1045
* Fixed (unreleased, fix/w49-space, `space.finale_fly_free`): the galaxy step goes through teleport_in_galaxy.

## Space: a guest sees a host NPC ship fire a burst of 4 shots for one (2026-11-13, audit 0.7.14)
<a id="issue-space-shot-burst"></a>

* What happens: a host ship that fires again 1 to 2.3 s after a pause shows 4 shots on the guest (or none).
  Cosmetic: the copies hurt nothing.
* Reproduce: not seen. Watch a host escort or pirate that fires in short bursts.
* Known: the host's shot count restarts at 1 while the guest still has the old count (observed in code).
* Ticket: BUG-1046
* Fixed (unreleased, fix/w49-space, `space.shot_count`): the host's shot count outlives the 1 s list.

## Space, 3-4 players: "Give 10 to <player>" gives the spice to someone else (2026-11-13, audit 0.7.14)
<a id="issue-space-give-recipient"></a>

* What happens: the button names co-op's give target (a pick made in Tribe or Civ stays), but the spice goes to the
  first other player of the session. In Space the ">" picker is hidden.
* Reproduce: not seen. 3 players: pick the third player with ">" in Civ, go to Space, click Give.
* Known: give_spice ignores the recipient (observed in code).
* Ticket: BUG-1050
* Fixed (unreleased, fix/w49-space, `space.give_pick`): the spice goes to the named player; ">" in Space.

## Creature, Tribe: a fight sound or effect can keep playing on a guest's copy of a host creature (2026-11-13, audit 0.7.14)
<a id="issue-mirror-fx-keys"></a>

* Fixed (unreleased, fix/w49-life, BUG-1049): the table ends with the session and the world, and a full table
  stops its oldest effect on the guest (`mirror.fx_keys`).
* What happens: the host's list of effects it relayed a start for is never cleared. When it fills (256), the stop
  of one effect is no longer sent, and that effect keeps running on the guest's copy.
* Reproduce: not seen; long Creature or Tribe sessions with many fights.
* Known: mirror_fx.cpp g_keys (observed); whether the list fills in real play is inferred.
* Ticket: BUG-1049

## Creature, Tribe: a host animal knocked aside slides back on the guest instead of jumping (2026-11-13, audit 0.7.14)
<a id="issue-npc-snap-dead"></a>
* Fixed (unreleased, fix/w49-small, BUG-0997): the snap compares this frame's path point, not the aim
  (`npc.place_now_snap`); test npc_snap (median 0.03 s with the fix).

* What happens: when the host's animal is thrown aside or put back by its game while it walks, the guest's copy
  closes the 2-7 m at catch-up speed over up to about 2 s instead of being placed there at once.
* Reproduce: not run; npc_walk_player with a host-side 5 m teleport of a walking NPC.
* Known: by code (high confidence): npc.place_now (BUG-0757) aims the proxy one frame ahead, so drive::walk_place's
  snap test (driver.h) never holds for a moving proxy.
* Ticket: BUG-0997

## Tribe: after joining another (or a restarted) host the guest sees no Tribe updates (2026-11-13, audit 0.7.14)
<a id="issue-tribe-cycle-not-reset"></a>

* Fixed (unreleased, fix/w49-life, BUG-1051): the tracker starts over with every session and world
  (`tribe.cycle_reset`, `civ.cycle_reset`).
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
* Fixed (unreleased, fix/w49-small, BUG-1052): 30 % of game::max_health (`tribe.rescue_max`); test tribe_chief_rescue
  (168 hp chief: 50.4 on both games).

* What happens: with the Chief aura setting on (the default), "<A> saved <B>'s chief" leaves the chief 0.3 hit
  points instead of 30 % of its health.
* Reproduce: a partner's member next to a chief that takes a killing blow (code reading, not seen at runtime).
* Known: the rescue reads the maximum from the citizen's +0x5D4 (1.0), the field BUG-0745 found wrong for the heal.
* Ticket: BUG-1052

## Tribe: the host's game reloads its save once in a later session (2026-11-13, audit 0.7.14)
<a id="issue-tribe-stale-host-reload"></a>

* Fixed (unreleased, fix/w49-life, BUG-1054): the reload due is forgotten when the host's world changes
  (`tribe.host_reload_reset`); in the same live world it stays due on purpose (BUG-0332).
* What happens: if a session ended in the seconds after the host's live Creature to Tribe change (before the world
  went out), the first world share of a later session in Tribe makes the host's game load its own save again.
* Reproduce: the guests leave right after the host enters Tribe live; then a new join while the host plays Tribe
  (code reading, not seen at runtime).
* Known: world.cpp g_reload_due survives the session end.
* Ticket: BUG-1054

## Tribe: the guest's copy of a host member keeps a tool the host's member put down (2026-11-13, audit 0.7.14)
<a id="issue-tribe-tool-zero"></a>
* Fixed (unreleased, fix/w49-small, BUG-1043): each field has its own accepted range, tool and hunger take 0
  (`fields.zero_values`); test tribe_zero_fields.

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
* Fixed (unreleased, fix/w49-cell, BUG-1006): a mirror with a non-finite field is rebuilt at the host's pose before
  it is moved, and the turn skips a NaN angle (`cell.mirror_nan_heal`).
* Ticket: BUG-1006

## Cell: a host creature a guest's jaw finishes off turns its kind against the host (2026-11-13, audit 0.7.14)
<a id="issue-cell-chunk-kill-host"></a>

* What happens: when the last chunk a guest's mouth tears off kills a host NPC, the host's game counts the kill as
  the host's own cell's, so that species turns on the host's cell, not on the guest.
* Reproduce: not seen at runtime; a guest with a jaw part kills a host NPC by chunks.
* Known: by code (cellsync.cpp MSG_CELL_HIT type 6 calls h_KillCell with the host's avatar as the killer; the eat
  path uses the guest's puppet).
* Fixed (unreleased, fix/w49-cell, BUG-1008): the kill is the guest's puppet's, and a dead cell refuses the hit
  (`cell.chunk_killer`).
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
* 2026-11-13 (fix/w49-net): fixed (`coop.fix net.join_seen`): after the start a player comes back only by its rejoin
  token (kept for the same host and name on a leave), without a token by a name nobody has and that dropped recently;
  every live entry counts against the limit. Tests: net_tests test_join_seen, lab join_late_names
  (docs/04-netcode.md#netcode-n2-join-input).

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
* Fixed (unreleased, fix/w49-ui, 2026-11-13): `diag.summary_wide`; FAIL lab 20261113-032710, PASS 20261113-032833.

## Session report: a long non-Latin host name ends in a broken character in the guest's report question (2026-11-13, audit 0.7.14)
<a id="issue-sreport-host-cut"></a>

* What happens: "<host> asks for your problem report" shows the host's name cut to 40 bytes, so a CJK name of 14
  or more characters ends in a broken character.
* Reproduce: a host named with 14 CJK characters, a session report round (not run).
* Known: sreport.cpp calls substr(0, 40) on the UTF-8 name (observed in code).
* Ticket: BUG-1013
* Fixed (unreleased, fix/w49-ui, 2026-11-13): `core.sreport_host_cut`; FAIL lab 20261113-032307, PASS 20261113-032426.

## Update: Try again shows the last download's progress for a moment (2026-11-13, audit 0.7.14)
<a id="issue-update-retry-progress"></a>

* What happens: after a failed update, Try again first shows the old "Downloading ... N %" until the new download
  reports progress.
* Reproduce: a failing update download, then Try again (not run).
* Known: autoupdate.cpp never resets g_got / g_total (observed in code).
* Ticket: BUG-1015
* Fixed (unreleased, fix/w49-ui, 2026-11-13): `core.update_busy`; FAIL lab 20261113-032710, PASS 20261113-032833.

## UI: text typed into the co-op window under a caption or the game saved box stops at half its length (2026-11-13, audit 0.7.14)
<a id="issue-locked-edit-utf8"></a>

* What happens: while a caption or a modal game box holds the input, the mod types the player's keys into the co-op
  text line itself. A name or chat line in Cyrillic, Greek or CJK stops at half its limit (a third for CJK), DEL is
  taken as a character, and an emoji becomes two replacement characters. Normal typing is not affected.
* Reproduce: type 30 Cyrillic letters and an emoji into the chat while a caption holds the input (not run).
* Known: locked_edit_apply (coop_ui.cpp) counts UTF-8 bytes against the UTF-16 limit and encodes each UTF-16 unit
  on its own (by code, not seen at runtime).
* Ticket: BUG-1027
* Fixed (unreleased, fix/w49-ui, 2026-11-13): `core.ui_locked_edit_utf16`; FAIL lab 20261113-032307, PASS 20261113-032426.

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

## Creature: after a Cell session the partner's creature shows its own full health for a while (2026-11-13, fix/w49-life)
<a id="issue-puppet-hp-after-cell"></a>
* CORRECTION (2026-11-13, fix/w49-small, BUG-1061): not a game desync. Both games had the right health (host 10/10, the
  guest's puppet of it 10/10); the monitor matched the guest's Creature line at s 30 with the host's own line of the
  Cell session before it at s 30 (6/6): each session's clock starts at 0. Fixed in tools/tests/monitor.py
  (`same_session`: only own lines of the stage and world the partner line names).

* What happens: two players play Cell, end the session, load a Creature game and join again: for 10 s or more each
  game shows the partner's creature at its species' own full health (10/10, 18/18) while the partner has 6/6.
* Reproduce: `SPORE_STAGES=cell,creature python3 tools/tests/coop_lifecycle_reset.py` (the monitor's puppet_hp rule).
* Known: seen in 3 of 4 runs, also with `core.lifecycle_reset off`; not with Creature first.
* Ticket: BUG-1061

## Space: the Space lobby's "Give 10 to <player>" button wraps its caption (2026-11-13, fix/w49-space key frame)
<a id="issue-give-caption-wrap"></a>

* What happens: the co-op window's Space row shows "Give 10 to / Carol" on two lines, cut by the button's frame.
* Reproduce: tools/tests/coop_space_give_pick.py (or 2 players with spice in the cargo), F9.
* Known: seen at 150 and 170 px; the cause is not analysed (the button's wrap width).
* Ticket: BUG-1062
* Fixed (unreleased, fix/w49-space2, 2026-11-13): `core.ui_give_fit`; FAIL lab 20261113-024426, PASS 20261113-024531-2.

## Tribe: a member's thought bubble shows another icon on each game (2026-11-13, fix/w49-mirror)
<a id="issue-tribe-bubble-icons"></a>

* What happens: two members chatting (the animations 0x04FFA012..0x04FFA019) show a cloud with a pictogram (a fish, a
  spear, a horn, a ball, ...) over their heads at the same moment on both games, but each game shows its own
  pictogram. It is chat, not a member's wish: the pictogram changes with each bubble.
* Reproduce: `coop.tribe` probe of fix/w49-mirror: the host plays 0x04FFA012 on a member (its CreaturePlayAnim), both
  games' shots 1-3 s later; or tribe_mirror_idle's rest key frame.
* Known: docs/re/tribe.md#tribe-bubble-icons. The bubble is the animation event effect 0x883816C2, made on both
  games by the relayed animation; the pictogram is picked by the effect system while the effect runs. Not BUG-1043's
  cause (no field is involved). Setting the effects' random generator to one seed right before the effect is built,
  and before each of its updates, still gave other pictograms on the two games and on one game from bubble to bubble.
* Next steps: find the particle component's own random source (Swarm), or leave it (cosmetic chat).
* Ticket: BUG-0987 (wontfix)

## Tribe: a chief's staff in the hand on one game and in the grip on the other in a still (2026-11-13, fix/w49-mirror)
<a id="issue-tribe-still-staff-grip"></a>

* What happens: same_view_tribe stills after BUG-0972 / BUG-0988: the host's chief holds its staff in the hand, the
  guest's copy has it upright in the resting grip (or held another way) at the same moment.
* Reproduce: `lab.py run same_view_tribe --repeat 3`, compare the chief's staff; `coop.tribe anims` (`hold=`, `t=`).
* Known: the animation and its moment agree (tribe_mirror_phase); the hold tests pass. Not analysed.
* Ticket: BUG-1069
* Fixed (unreleased, fix/w49-tribe3, 2026-11-13): the chief's gesture from the load got no moment relay, a moment
  5 s+ off was never set, a restart over a just-bound hold dropped it (`tribe.mirror_phase_any`, `tribe.hold_restart`;
  docs/re/tribe.md#tribe-chief-load-gesture).

## Tribe: right after the session start a returned gesture of the chief's copy can stay (2026-11-13, fix/w49-mirror)
<a id="issue-tribe-stale-first"></a>

* What happens: one round of 12 in tribe_mirror_stale (the first of a session, the mirror's last relayed animation 0)
  kept the gesture for 9 s while the host's chief stood; every other round ended it within 0.5-2.1 s.
* Reproduce: `lab.py run tribe_mirror_stale --repeat 3`; `coop.tribe holdtrace 1` on the guest (Z / I events).
* Ticket: BUG-1071
* Fixed (unreleased, fix/w49-tribe3, 2026-11-13): no idle news went for a member with no behaviour start since the
  load (`tribe.mirror_idle_loco`).

## Tests: same_view_tribe's host game gone 7 s into Tribe (2026-11-13, fix/w49-mirror)
<a id="issue-same-view-tribe-host-gone"></a>

* What happens: lane 7, a cold start: the host's game log ends 6.7 s into Tribe, no fault, no crash report; the test's
  console call breaks. Seen once (out/lab/20261113-052132).
* Reproduce: `lab.py run same_view_tribe --lane 7 --repeat 5`.
* Ticket: BUG-1070
* Explained (fix/w49-tribe3, 2026-11-13): not the game or the mod: a second lab run started on lane 7 at that second
  and killed the lane's games (docs/06-testing.md#testing-lane-lock); the lab now refuses a run on a held lane.

## Tribe: the host's chief at the same moment of the same animation stands another way on the guest (2026-11-13, fix/w49-tribe3)
<a id="issue-tribe-chief-overlay-pose"></a>

* What happens: in two same_view_tribe stills of fix/w49-tribe3 (12 views each run), the chief's main animation and its time agree
  on both games (within 0.03 s) but the body is bent on one game and upright on the other, the staff at another angle.
* Reproduce: `lab.py run same_view_tribe --repeat 3` (the `host's chief at the still:` lines next to the sheets).
* Known: only the main track is relayed and timed; the other playing tracks are not compared yet.
* Ticket: BUG-1072

* **BUG-1078** (Tribe, guest, caught fault): a guest that leaves after the host's game over and reload faults once
  in its own creature_attack event (0x00D521BA, data 0x120): the event its Drama Manager held back as a guest starts
  alone after the leave. Found by the lab's warm resets (fix/w49-warm, lab 20261113-103507); tribe_bounce_world stays
  cold until it is fixed. Reproduce: `tools/lab.py run tribe_bounce_world,tribe_member_air --in-order --lane N`.
* **BUG-1077** (test): attacker_owner fails the monitor's posse_damage gate now and then (3 of 6 recent runs, cold).
## Tribe: a village only guests raided does not count for the stage's goal (2026-11-13, fix/w49-tests)
<a id="issue-tribe-guest-raid-goal"></a>

* What happens: a guest's raid takes an NPC village's hut down (the village is conquered in every game), but the
  stage's goal bar on the host stays where it was: no conquest cinematic, no goal. Only a village the host's player
  attacked itself at least once counts.
* Reproduce: tribe fixture, the guest box-selects members and right-clicks the NPC village's hut in attack mode until
  it falls (`coop.tribe huthp <x y z> 40` shortens it); `coop.wstate` `progress=` stays 0 on the host. The campaign's
  Tribe end needs the host's own click for this reason (coop_campaign.py tribe_clicks).
* Known: the village's cTribeToTribeMission is made by the host game's own input actions (TribeMission_SetTargetTribe,
  docs/re/tribe.md#tribe-wsync); a guest's order runs on the host as a DoAction without it (inferred, not traced in
  Ghidra). Observed in campaign_tc2 lab 20261113-050826 and the fix/w49-tests exploration on lane 4.
* Next steps: host_command calls SetTargetTribe for a guest's attack / gift on another tribe's object.
* Ticket: BUG-1068.
* Fixed (2026-11-13, fix/w49-drama) **BUG-1078** (Tribe, guest, caught fault): a guest that left after the host's game
  over and reload faulted once in its own creature_attack event (0x00D521BA, data 0x120): the event's attacked tribe
  was a village the re-alignment had removed (`tribe.attack_target` forgets it before the start;
  docs/re/tribe.md#tribe-attack-event-target). Test: tribe_attack_stale.
* Fixed (2026-11-13, fix/w49-drama) **BUG-1077** (test): attacker_owner failed the monitor's posse_damage gate now and
  then: the monitor's line read the UFO attacker as a creature (`npc.hit_log_object`); the gate was right.
* Fixed (2026-11-13, fix/w49-fault) **BUG-1076** (Tribe, guest, caught faults at 0xCE69F9): the players' hut dead in
  the host's game fell in the guest's game too, which ran its own defeat and reloads in a loop; a village caption after
  such a reload read the freed tribe (`realign.own_hut`; docs/re/tribe.md#tribe-caption-tribe-token). Test:
  tribe_own_hut_fall.
* Fixed (2026-11-13, fix/w49-hutwait; `tribe.hut_defeat_wait`, test tribe_hut_defeat_wait) **BUG-1081** (Tribe, host): the host's "Your tribal hut has been destroyed" caption holds the host's game only;
  the guests play on until the host's OK and reload (BUG-0960's wait covers the tribe knocked out only). Reproduce:
  `coop.tribe hutfall` on the players' hut in the host's game (docs/06-testing.md#testing-own-hut-fall).
* Open **BUG-1083** (test, Tribe): tribe_gameover_partner flakes about 1 run in 5: the host's game-over caption never
  comes because the host's knocked-out chief has health again (168) in phase 2 (lab 20261113-140322; the rerun
  20261113-141231 passed). Reproduce: run tribe_gameover_partner a few times on one lane.
* Fixed (2026-11-13, fix/w49-tctl2; `core.editor_now_tag`, tests stage_end_vote, stage_end_guest, editor_now)
  **BUG-1085** (Creature, all): `core.editor_now` (BUG-0516) acted on the stage-end editor (tag 0x64AA168), not on the
  mating editor (0x12191CA): the host's Creature -> Tribe editor opened without a vote and the guests got an "Evolve
  now" offer; mating evolutions waited for the vote; a guest's own stage-end press took it into a Tribe of its own
  (lab 20261113-154923). Reproduce: `SPORE_LAB_FIXOFF="stage_end_vote=core.editor_now_tag"`.
* Fixed (2026-11-13, fix/w49-tctl2; `tribe.host_pause_wait`, test tribe_host_pause) **BUG-1086** (Tribe, guest): while
  the host's game was paused (pause menu, Sporepedia, a modal) the guest's orders waited and its members stood, with
  no note on the guest. Reproduce: Tribe, the host's Esc, the guest's `busy` stays `-`.
* Fixed (2026-11-13, fix/w49-tctl2; `tribe.foreign_note`, test tribe_foreign_click) **BUG-1087** (Tribe, guest): a
  click on another player's member or its roster card selected nothing and said nothing (the split of the player tribe
  is by design). Reproduce: Tribe, two players, the guest clicks a host member.
## Cell: a level down removes more of the cells round a partner than round the host in the host's game (2026-11-13, fix/w49-cell2 test)
<a id="issue-cell-first-down-partner"></a>

* What happens: on a level down (+-150 food, the guest 60-70 m away) the host's game removed more of the cells in the
  box round the partner than of its own, pooled over three down changes 28 / 16 %, 87 / 24 %, 74 / 22 % (3 of 5 runs);
  the guest sees it as a big part of its screen replaced.
* Reproduce: `tools/lab.py run cell_levelup_area --lane N` (the printed area shares; `--strict` asserts them).
* Known: by the cull 0xE7C3E6 (a cell without a parent whose tile IsTileActive refuses, docs/re/cell.md#cell-levelup-partner-area).
  One cause is fixed (`cell.view_centre_scale`: the partner's view centre was 17-37 m off while the scale moved); the
  centre is right now (0.7-3.6 m) and the loss stays in some runs.
* Not checked: whether those cells had a parent, which tiles were active round the partner then, the camera lag (the
  host's view box trails its cell by up to 40 m right after a down change, the partner box is the host box moved).
* Ticket: BUG-1064 (BUG-1080 folded in)
* Resolved (2026-11-13, fix/w49-cell3): not a co-op loss. The test's guest stood 1-8 m from the host at every mark
  (its one `tp` did not hold), so the partner's area was the band outside the host's box, the cells a down change
  pushes out first. With the areas apart (the guest moved before each change) the partner's area lost no more than
  the host's beyond noise on the down changes in 20 runs (pooled -38 .. +9 points): the rest is the game's own cull at the edge of
  a whole-tile range (docs/re/cell.md#cell-levelup-cull-measured). `cell_levelup_area` asserts the shares now.

## Tribe: a guest's mirror of an NPC village member 114 m off the host's during raids (2026-11-13, fix/w49-camp)
<a id="issue-tribe-npc-member-apart"></a>

* What happens: one NPC village member of the host's game is drawn 114 m away on one guest for two sync checks in a
  row (sync-check MISMATCH, `t: 1 apart`), the other guests match.
* Reproduce: campaign_tribe_end (lab 20261113-125221, 4 s after a `coop.tribe spawn 6` during the raids).
* Ticket: BUG-1082.
* Fixed (2026-11-13, fix/w49-raid2): the host's member was in the air (114 m and 12 m, purely radial), the guest's
  mirror on its ground: `tribe.mirror_hang` + `game.air_culled` (docs/re/tribe.md#tribe-member-hang), test
  tribe_member_hang. Why the host's game holds members up there is not known (below the ticket).

## Tribe: caught faults at 0xC23469 on a guest while the host edits the City Hall (2026-11-13, fix/w49-camp)
<a id="issue-tribe-holdbundle-fault"></a>

* What happens: BUG-0684's null bundle (Citizen_HoldBundle, from the heal / gather node) on a guest in Tribe while the
  host is in the Tribe end's editors; the game survives (caught), the lab fails the run on the fault count.
* Reproduce: campaign_tribe_end (lab 20261113-142311, Dave).
* Ticket: BUG-1084.

## Tribe: a player member stays several metres up for half a minute in a fight (2026-11-13, fix/w49-camp)
<a id="issue-tribe-member-up"></a>

* What happens: a member in the fight node (`bt=05218BB3`) is 3-8.75 m over the ground for 36 s on every game during
  the Tribe play; the monitor's ground gate fails the campaign.
* Reproduce: the full campaign (lab 20261113-142203, member #16 of Carol).
* Ticket: BUG-1088.
* Fixed (2026-11-13, fix/w49-raid2): the member was swimming (radius 499.77, the planet's water surface), the game's
  own; the mod's member heights count over the water's surface now (`monitor.tribe_water`,
  docs/re/tribe.md#tribe-member-swim), test tribe_member_air.

## Civ: a guest's proxies of host NPCs that the host has at the planet's centre (2026-11-13, fix/w49-camp)
<a id="issue-civ-npc-centre"></a>

* What happens: right after a guest's rejoin in Civ, 14 NPCs of the host's game stand at (0 0 0) while another guest
  shows their new proxies on the ground 525 m away (sync-check MISMATCH `n: 14 apart`).
* Reproduce: the full campaign (lab 20261113-160426, Civ, 17:00:23).
* Ticket: BUG-1089.

## Tribe: tribe_editor_world fails: the co-op epic outlives the editor's share save on the host (2026-11-13, fix/w49-bundle)
<a id="issue-tribe-editor-epic"></a>

* What happens: after the host's editor visit and the share that follows, the host's game still has the co-op epic
  alive and tracked (`live_epics=2`), where the test expects it to have left before the share save (BUG-0121's guard).
* Reproduce: `lab.py run tribe_editor_world` (3 of 3 on rel/0.7.14 + fix/w49-bundle, also with BUG-1084's switch off:
  lab 20261113-183059, -184241, -184244).
* Ticket: BUG-1090.
* Fixed (2026-11-13, fix/w49-proxy): creatures the host's game parked in its pool during a pause; `npc.frozen_pooled_rts`,
  test civ_wild_pooled (BUG-1089).

## Creature: a hit animal's copy steps about 1 m in single frames while it flees (2026-11-13, test npc_walk)
<a id="issue-npc-walk-hit-jumps"></a>

* What happens: npc_walk's check "no jump of a moving proxy" fails now and then: one of the two walkers the host hits
  steps 0.75-1.13 m in single frames on the guest's screen, in two bursts of 3 at t 45.3-45.6 s and 47.0-47.3 s.
* Reproduce: `tools/lab.py run npc_walk --lane 4 --repeat 3` (3 of 6 runs failed on fix/w49-proxy2, labs
  20261113-202612, -204146 #2, -204750).
* Known: also with BUG-0440's npc.proxy_hold off in Creature (-204750); not run on rel/0.7.14 itself.
* Cause (2026-11-13, fix/w49-npcwalk): not the test. The walker the hit throws is placed in the air every frame
  (drive::air_place); the guest's game puts it back where it was in ~6 of 7 frames at 52-67 fps and takes one placing
  every ~0.1 s (0.6-1.1 m steps). On 0.7.13's code too (rows back to 20261106). npc_walk now judges thrown proxies on
  and off the screen ("a thrown proxy flies with its NPC"), which fails in every run with a throw (live: lab
  20261113-221929 #2, #3: 128 of 166, 148 of 174 frames standing).
* Not fixed by: the teleport mark, putting the hold back after the creature and physics updates, no velocity, the
  animation relay off: all still stood (docs/re/creature.md#creature-air-place-lost).
* Fixed (fix/w49-throw, `coop.fix drive.agl_extrap`): the mod's own target height. The lead moved a thrown NPC's state
  on at its velocity but kept the newest state's height over the host's terrain (agl), and the air placing goes by
  that agl, so the proxy was put at one height for a whole state period. The agl now goes with the position.
* Ticket: BUG-1094

## Tests: npc_ai_lookup finds no freed proxy block reused on a well-used guest game (2026-11-14, fix/w49-warmfault)
<a id="issue-npc-ai-lookup-reuse"></a>

* What happens: `npc_ai_lookup` fails "a social click on a new creature at a freed proxy's address (0 reuses, 0
  posted)": the reuse hunt (`coop.npc reusehunt`, 150 creatures per proxy) found no new creature at any of the 21
  freed addresses (final check 20261114-005006, lane 4, warm run 7 of these games).
* Reproduce: not reproduced; seen once. It passed cold (20261114-022615, hit on the 5th proxy) and warm
  (20261113-025531, 20261114-030034, hit on the 1st).
* Known: not the fault inheritance of BUG-1097 (those faults are on the host, the hunt on the guest). Inferred:
  the guest's heap after many tests seldom hands the freed block to the next creature.
* Not checked: a heap census of the guest in the failing run.
* Next steps: `warm=False` for npc_ai_lookup and proxy_identity(_tribe), or a hunt that tries harder.
* Fixed (2026-11-14, fix/w49-reuse): the hunt waits for the free and the free check sees a merged chunk; see
  [06-testing](06-testing.md#testing-w49-reuse). CORRECTION (2026-11-14, fix/w49-reuse): "seldom hands the freed block to
  the next creature" was the hunt: the block was freed and merged, the old check read a stale header
  ([re](re/creature.md#creature-reuse-merged)).
* Ticket: BUG-1099
## Lab: broken textures, a white wedge, garbage geometry and a smeared HUD on key frames when the GPU memory is overcommitted (2026-11-14, fix/w49-spacegfx)
<a id="issue-gpu-overcommit-garbage"></a>

* What happens: on a loaded final check a game draws its scene with wrong buffer contents: flat coloured rectangles
  and a mosaic of small squares instead of textures, big translucent white wedges, stripes of garbage geometry, and
  the HUD smeared into streaks at its place (it looks gone). The game goes on, its logs are clean, the test passes.
  Seen: rel/0.7.14 final check 20261114-005006, campaign_space 02/03 (guest-Bob, guest-Carol), with 22 test games
  running at once.
* Reproduce: not on a quiet lane (campaign_space on lane 4 alone: 20261114-025653 and 20261114-031143, all frames
  clean). It needs the whole lab: more games than the 16 GB of VRAM hold (about 0.66 GB a game over a ~3.9 GB base;
  docs/06-testing.md#testing-gpu-overcommit) and little free RAM.
* Known: not a mod bug (no mod or SporeKit line, no fault, no hidden UI: the HUD is drawn, from a bad buffer); the same
  Space entry of the same build was clean on campaign_civ_jump 5 min later in the same run. How to tell it on a key
  frame: the garbage is in the 3D scene, the textures and the HUD at once, and the HUD's own colours are in the streaks.
  A mod bug that hides a UI layer leaves a clean scene without the panel.
* Not checked: the driver level (no amdgpu error in the kernel log); whether fewer games at once removes it for good.
* Next steps: keep the final check's games at once under ~18 (VRAM under the card), or repeat a test whose key frames
  show this alone on a quiet lane before judging it.
* Ticket: [BUG-1098](tickets/BUG-1098-space-guests-screens-show-broken-textures-a-whit.md)
## Tribe: a guest's game time stays paused after a village caption it clicks as it closes itself (2026-11-14, final check 0.7.14)
<a id="issue-guest-caption-click-pause"></a>

* What happens: after a village conquest on the host, the guest's game plays TRG_TotemPole_Conquer and TRG_Upgrade too
  and the mod ends each after 8 s (BUG-0983). In campaign_tribe_noclick (lab 20261114-005006) Carol's game time then
  stood (`busy=paused`, no input lock, no cinematic) for the rest of Tribe (30 min), Dave's from a later conquest
  (16 min): its world frozen, an epic's proxy hanging 9 m up (sync-check n204 9.1).
* Known (logs): both began in the second where the campaign's driver clicked the caption's check ("Carol: caption ok"
  01:01:02, the forced end 01:01:02.486; "Dave: caption ok" 01:14:50, the end 01:14:50.030); the six other caption
  ends of the run (clicks a second or more apart) left nothing. BUG-1086's wait was balanced (up / gone pairs).
  Inferred: the OK and the forced EndCinematic both end the cinematic and the stage's end listener runs twice (a pause
  counted twice, lifted once).
* Reproduce: not yet; `coop_tribe_conquest_guest.py --click-at-close` (3 runs, lab 20261114-031126) had no caption in
  two runs and no stuck pause in the third. `coop.pauses` on the stuck guest names the reason left.
* Next steps: catch the reason with `coop.pauses`; then guard the forced end against a cinematic already ending.
* UPDATE (2026-11-14, fix/w49-pause): mitigated, the cause still unknown. A guest's game time that stands 20 s in
  Tribe with only a caption's or a UI modal's pause counted and nothing holding it is released and logged
  ("this game's time stood 20 s with nothing holding it (reasons: ...): released", `coop.fix tribe.guest_pause_net`):
  the stand now lasts about 20 s instead of the stage. Not reproduced with the click (9 conquests) nor with the caption's
  messages sent or posted around the forced end in the same call, slow frames or stalls
  (docs/re/tribe.md#tribe-caption-pause-leak); one way the game leaves such a pause is known (the cinematic's pause
  slot cleared without a Resume by the next start). Next hit: the net's log line names the reason; with
  `coop.pauses trace` on the guest also its caller.
* Ticket: [BUG-1101](tickets/BUG-1101-tribe-a-guest-s-game-time-stays-paused-for-the-r.md)

## Tribe: a partner's member 1.0-1.5 m under the host's ground for 10-20 s (2026-11-14, final check 0.7.14)
<a id="issue-tribe-member-under-host"></a>

* What happens: the monitor's ground rule on the host: "the tribe members of partner 1 1.26 m under the ground (Tribe)
  on 2 lines" (campaign, lab 20261114-005006); single lines in campaign_tribe_end (Nov 13) and the 0.7.10 campaign.
* Known: not the water rule (BUG-1088); only in the 4-player campaigns under load; the member was never named.
* Next steps: the tribe watch logs the member now (BUG-1104): read its node and height over the water in the next hit.
* Known (2026-11-14, fix/w49-under, not reproduced): every hit falls in a raid on an NPC hut (campaign_tribe_end) or
  an epic fight (campaign); in three a member of the same player was launched 10 m up at 22 m/s seconds later (pushed
  out of the ground), in one a member of the same player stood 3.3 m up at the raided hut at speed 0 (the BUG-1082
  hold). The game's physics puts a member placed under the ground back within 0.3 s, so no placing (the mod's
  either) explains 10-20 s there: inferred the game's own crowd/collider state, on the host's screen only.
  The fixture's raids and epic fights do not show it (labs 20261114-034621 .. -042624, `tribe_under_probe`);
  docs/re/tribe.md#tribe-member-under.
* Ticket: [BUG-1104](tickets/BUG-1104-tests-the-monitor-s-ground-rule-names-no-tribe-m.md)

## Tribe: a standing herd's animals stay 6-9 m off on one guest at one spot of the campaign planet (2026-11-14, rel/0.7.14 final check)
<a id="issue-tribe-proxy-spot-cadian"></a>

* What happens: in the full campaign's Tribe world (Cadian) one guest's proxies of a standing herd (6 animals) stay
  5.7-9.8 m from the host's NPCs for 70 s, at the same two points, although the driver teleports them about 5 times a
  second; the other guests are fine. The lab's monitor gate `proxies` failed `campaign` once (out/lab/20261114-050940,
  extra2, s 3340-3400, after the host came back from its Tribe editor).
* Reproduce: only in the full `campaign` (lane 2, about 55 min in). Seen at the same spot in rel/0.7.11
  (20261108-114435 campaign extra2 s 1584, one trace line).
* Known: not a 0.7.14 regression and not one of 0.7.14's Tribe proxy switches (BUG-1106 Root cause). Inferred: the
  host's NPCs stand where this guest's game does not let a creature stand (an object of this game only, near the
  host's village), and every teleport is pushed out again.
* Not checked: what stands at the host NPC's spot on that guest. The audit line prints it now (`objects within 3 m of
  its target`, `air hold`).
* Next steps: at the next occurrence read the audit line's new fields; a Tribe fixture of the campaign world near
  the host's village would make a focused test possible.
* Ticket: BUG-1106

## Space: "Name Your Captain" can leave the empire without a captain (2026-11-14, GitHub #202, fix/w50-captain)
<a id="issue-captain-uncopyable-species"></a>

* What happens: a Space world without a captain asks "Name Your Captain" after the load; when the game cannot copy
  the world's player species, the check button sets no captain (key 0) and the game does not ask again until the next
  load. The captain screen then shows the world's species as the portrait (BUG-1111 guard; before: a crash at
  0xA055B5), its equipment panel faults inside the game (handled, 0x5971D4 / 0xC02A14).
* Reproduce: `space_captain_none` (the species manager pointed at an unknown species with `coop.captain species`).
* Known: docs/re/space.md#space-captain-portrait-null. A host creation that the mod registered on the guest copies
  fine (lab).
* Not checked: why the reporter's species could not be copied on the guest (its log has no line on the species or
  the captain), and how the reporter's world lost its captain in the first place (the host's naming, or a guest's).
  Whether "Enter Outfitter" with captain 0 is safe.
* Next steps: a log line when the captain is set to 0 (hook `SporeCoop::cEmpire_SetCaptainKey`), so the next report
  names the species; then give such a world a captain from a species this game has.
* Ticket: BUG-1111

## Tools: tickets.py check fails on the other branches while a private ticket lists an unmerged branch's commits (2026-11-15, feat/w51-labverdict)
<a id="known-ticket-check-private"></a>

* `make test` (unit-lint) fails on every branch of a batch but one: tickets.py check applies the per-branch rules
  (commits in this branch's history, test names this tree knows) to the private tickets of the main checkout's
  out/tickets/, which every worktree shares; a private ticket fixed on a branch not merged yet lists that branch's
  commits.
* Reproduce: `python3 tools/tickets.py check` on a 0.7.15 branch other than the one that fixed the private tickets.
* Ticket: BUG-1129
