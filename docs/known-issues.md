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


## Civilization: animals on a guest's screen stand or walk 8-200 m from where the host has them (2026-11-16, fix/w51-t4)
<a id="issue-civ-npc-apart"></a>

* What happens: in the 4-player campaign's Civ, one guest (sometimes every guest) draws a few animals of a herd away
  from the host's: on its own ground where the host's ground is 10 m lower, 8 m in the air, or 120-200 m off.
* Fixed (BUG-1216, fix/w51-civsync): the copies the Civ game moves elsewhere are put there at once (npc.civ_jump: no
  flight through the air, no target through the planet), and the sync-check compares animals by their height over
  each game's own ground (sync.npc_agl).
* Left: where the host's live world and a guest's copy of its save differ in height and the guest's ground is a steep
  slope, a standing animal's copy slides and is put back every ~2.5 s (BUG-1228); seen once, in a live Tribe -> Civ
  campaign (campaign_tc2, final check 0.7.15); the segment runs from the saved Civ do not show it.
* Ticket: BUG-1228 (BUG-1216 fixed)

## Civilization: a guest's walking city citizens trail the host's by a few metres (2026-11-17, fix/w51-citizens)
<a id="issue-civ-citizens-trail"></a>

* What happens: a city citizen that walks on the host's screen walks 3-6 m behind on a guest's; where two walk past each
  other the guest sees, for a few seconds, the other one (of another size) at a citizen's place.
* Reproduce: civ_citizens / civ_citizens_reload print `size pair host X / guest Y: the guest's is bound to host Z 4-6 m
  away` in about every second run; the size check fails when 10 % of the pairs are such (1 of 12 runs before 0.7.15's
  BUG-1221 change, 1 of 4 in lab 20261117-125035, 0 of 4 in -130515; the check now takes rounds until 20 pairs).
* Known: each copy has its host citizen's size; standing pairs never differ (0 of 172). The follow gives a MoveTo 1.5 s
  ahead, again after 2 m or 1.5 s, and puts only beyond 12 m (docs/re/civ.md#civ-citizens-reload).
* Next steps: a shorter lag for walking citizens, or a size check over pairs that stay together for two rounds.
* Ticket: BUG-1227
