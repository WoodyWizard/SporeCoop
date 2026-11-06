# Creature stage (RE notes)

Marks: ✔ verified (code read or seen at runtime), ~ likely, ? open.
Structures of `cCreatureAnimal` and the NPC sync: [05-re-notes.md](../05-re-notes.md) 5.4-5.6.

## 1. Playing through input (test bots, 2026-10-23)

* ✔ A left click on a creature walks the avatar to it and selects it (its health bar and species card
  appear); a click on the ground walks there. A click on the partner's puppet does not move the host
  (the selection of puppets is guarded, `CreatureSelectionUI`).
* ✔ The ability bar (bottom centre) has two stance buttons above it: social (green) at
  `(W/2 - 30, H - 82)` and combat (red) at `(W/2 + 43, H - 82)` (1440x1080: 690,998 / 763,998).
  The keys 1..4 use the abilities of the current stance. The fixture's creatures have one ability
  per stance (combat: bite / strike, social: sing). The stance a save starts in is social.
* ✔ Combat: the selected target takes ~1 damage per key press at melee range (Kook: 6-7.5 hp, the
  host's creature 10 hp, the guest's Kirgle 16 hp); a Kook nest fights back as a group.
* ✔ Social: key 1 on a Kook (social stance, next to it) impresses it ("Impress 3 Kook to ally" card,
  1/3) and pays 10 DNA on the host.
* ✔ Kill DNA: Kook 7.0-8.8 DNA, paid inside `TakeDamage` (below).

## 2. Kill credit in `cCreatureAnimal::TakeDamage` (0xC075A0)

* ✔ The DNA of a kill is paid inside TakeDamage: `AddEvolutionPoints` is called at 0xC07BAC
  (return address 0xC07BB1, seen at runtime) with the value of `0xC04260(victim, 0)` (thiscall,
  returns the victim's DNA worth as a float). Right after it 0xC75820 (via 0xF67D40) gets the same
  value (~ the "+N DNA" popup / statistics).
* ✔ It is paid only when the credit flag (`[esp+0x80]`) is set at 0xC07A78: the attacker creature
  (`ebp`, the attacker combatant's owner when its noun is 0x18EB45E, cCreatureAnimal) answers true
  to its spatial vtable +0x58 (~ "is the player's avatar"), or its species' relation to the player
  (0xB1FCD0 / 0xBA40C0) is 6 (~ ally) and a meter at victim+0xA38 passes a threshold.
* ✔ Consequence for co-op: a guest's hit is applied on the host with the guest's puppet as the
  attacker, the puppet is not the avatar, so guest kills paid nothing (no DNA for anybody, no goal
  progress). Fixed in `npc.cpp`: a lethal guest hit is applied as the host avatar's
  (`guest_kills=` in `coop.npc` on the host); regression check in `coop_team.py`.

## 3. The push vector of `TakeDamage`

* ✔ The 4th argument (`const Vector3*`) is the hit's push (knockback impulse), not a position: a
  bite or strike of the player (type 2) passes (0,0,0) (runtime log of every real call,
  2026-10-23). Our synthetic hits passed the victim's position (|v| ~ 505 on Scalinus): a forwarded
  guest hit pushed the NPC on the host, a 1000-damage kill threw the corpse ~80 m up, and the guest's
  local death replay (1e6 damage) threw its proxy the same way; the proxy corpse then hung 15-80 m
  away from the host's corpse until it was removed (`teleport` / `set_position` do not hold a dead
  creature). `game::apply_damage` / `game::hit` pass (0,0,0) now; the death replay uses a just-lethal
  hit. Regression: the corpse check in `coop_team.py`.

## 4. The avatar-event handler and +0xE84

* ✔ `Simulator::CreatureStageEventHandler` 0xD47FC0: a vtable method (pointer at .rdata 0x147B680),
  `thiscall(self, u32 msgId, msg*) ret 8 -> bool`; it handles the messages 0x61B1320, 0x49B56D7,
  0x5371F11, 0x60B4123, 0xD335362C, 0xD3353635, 0xD335363A when `msg+0` is the avatar (checked with
  0xB1FCD0). On the 0x60B4123 / 0xD335362C path it walks creatures and, for an adult (`+0xB34 == 1`,
  0xC0B890) creature of another species with `+0xB54` set, reads `(+0xE84)->+0x32C` (0xC0C2C0 is
  the getter of +0xE84) without a null check: our puppets and NPC proxies have no +0xE84 object, the
  guest faulted at 0xD48634 (data 0x32C) while it fought proxies. Same field as the
  `CreatureSelectionUI` crash (coop.cpp guards both: during this handler the puppets and proxies
  get a blank object whose +0x32C is -1, "none"). +0xE84 is the creature's archetype tuning, set by
  `Create` from the herd's archetype (docs/re/tribe.md E: +0x388 = isEpic); our herds have none.
  ? the meaning of tuning +0x32C.
* ✔ 0xC0B890: `+0xB34 == 1` (age: 1 = adult ~). 0xC04260 `thiscall(victim, 0)` -> float: the DNA a
  kill of this creature is worth.
* ✔ (2026-10-25, decompiled GOG 2024) The creature read at 0xD48634 is **not taken from a list**: it
  is `msg+4` of the message, cast with `0xAC8010(obj, 0x18EB45E)` (vt 0x20 noun id == creature).
  Path: 0x60B4123 (social win, `{avatar, other, 0}`) or 0xD335362C; for 0xD335362C `msg+0` may be
  any creature (the avatar check at the top is skipped for it). Then: `other+0xB54` and
  `(other+0xB54)+0x17C` set, `other+0xB58 & 2` clear (the handler sets it afterwards: one event per
  creature), species pointer `+0xB20` differs from the avatar's, adult (0xC0B8D0), and for
  0xD335362C the attacker is the avatar (`(+0xC0)->vt 0x58`) or of an allied species (relation 6)
  with 0xFEB920 and a timer: then `(+0xE84)->+0x32C`. -1 there keeps the event's flag at 1.
* ✔ Who posts 0xD335362C: **0xC02B40**, the creature's attack (creature vt 0xB8 at .rdata 0x146A138,
  `thiscall(attacker, target, a3, a4) ret 0xC`, called by the ability code 0xC1E8B0 at 0xC1F67F with
  `(target, attacker+0xE8C, 0)`): 0xC20390 does the hit, then
  `cCreatureModeStrategy::ExecuteAction(Get(), 0xD335362C, {attacker, target, 0})`.
  `App::cCreatureModeStrategy::Get` 0xD38840 = `mov eax,[0x169E294]`; `ExecuteAction` 0xD39360
  `thiscall(strategy, id, msg*) ret 8` hands the message to every listener (`+0xBC..+0xC0`, vt
  0x18), the event handler among them, then handles 0x533535DA / 0x45AB96E / 0x60B4123 / 0x60B4124 /
  0xD335362C / 0xD3353637 itself. Other posters of 0xD335362C: 0xD35190, 0xD3A5A0. The players'
  stacks (0xC1F681, 0xC02C5D, 0xD3938E) are exactly this path.
* ✔ `coop.npc attackmsg <id>` (host) posts that message for the host avatar and host NPC `id`
  (tests); `coop.npc untuned` / `creatures [species]` list creatures with their +0xE84, herd,
  archetype; `coop.npc archless` the herds without an archetype (ours).

## 5. What a player meets that tests did not do (play_creature.py)

* ✔ The pause menu (Esc) is "Options": Resume Game, Spore Guide, Spy Glass, Ban, Settings, Save,
  Exit to Main Menu, Quit Spore. `coop.ui find "<caption>"` lists a window with its parents
  (rectangles relative to the parent, flag 0x1 = visible); `tools/tests/uifind.py` turns that into a
  screen point. "Exit to Main Menu" asks "Do you want to save your game?" (Don't Save / Cancel /
  Save), then "Are you sure? All progress will be lost!" (Don't Save again). The menu stays
  "visible" under the dialogs it opened: check for the dialog, not for the menu to go away.
* ✔ A guest that exits to the main menu stays in the session (galaxy menu, co-op lobby open in the
  Co-op item); since 2026-10-23 the host sends its world again after 5 s (back in ~14 s).
* ✔ The mating call is Backspace (blue bar, middle button "Mating Call [Backspace]"). With DNA 0
  nothing happens; with DNA the "Mate: Locate your Mate!" card appears and a thought bubble shows
  over the nest. Clicking the nearby family members did not start the editor within 30 s (not
  explored further; the evolution vote is tested through `coop.vote editor`).
* ✔ The ability bar keys work on an unfocused test window (background mode); WASD and the arrow
  keys move the avatar; a right-drag turns the camera.
* ✔ On the guest, the goal card of a species said "The <species> species is extinct!" while the
  host's card said "Impress 3 <species> to ally": the guest counted the replayed deaths of mirrors
  as kills (section 8.1). Fixed 2026-10-23.

## 6. Herds of our own creatures fill themselves up

* ✔ `CreateHerd(pos, species, 0, 0, 0, 0)` (puppets of players with their own creature, NPC proxies
  on the guest) gives a herd with `mTargetHerdSize` (+0xF4) = 5 and `mbEnabled` (+0x218) = 1 a
  moment later (runtime, 2026-10-23; docs/re/tribe.md E has the herd fields). The game then spawns
  members up to the target: on the host 4 clones of the guest's Kirgle stood at the nest within
  seconds of the join, as real NPCs, mirrored to the guest. On the guest the proxies' herds did the
  same, the update filter removed them again (`local_removed` grew all session). `game::herd_quiet`
  sets the target to 0 and disables such herds (at creation and on every puppet update).
  Regression: `coop_team.py` ("no NPC clones of the guest's creature").

## 7. When the host's simulation stands still

* ✔ In the host's pause menu, in the death caption ("You were killed in battle! ..." with a green
  check that waits for OK, like the space captions in docs/06-testing.md 6.9) and in cinematics no
  creature is updated (`cCreatureAnimal::Update` is not called: the registry of the frame is
  empty). `npc.cpp` used to forget every NPC 2 s later and the guests lost them all until the host
  went on; a frame with no creature update now counts as a pause (the NPCs stay mirrored, standing
  still). Regression: `coop_exit_menu.py` (host paused 6 s: 21 -> 0 NPCs before, 21 -> 21 after).
* ✔ The death caption is `coop.ui find "You were killed"`; `key enter` presses its OK.

## 8. Social games, the species relationship table, goal cards (2026-10-23)

Static analysis: a full Ghidra analysis of the exe in a private project (`analyzeHeadless`, ~15 min,
then queries by a small script in seconds); runtime: `probe`, memory diffs, `call`.

### 8.1 The species relationship table

* ✔ `cSpeciesRelationshipManager::Get` 0xB3D590 (cdecl) -> manager. The table is a hash map at
  manager +0x20 (buckets +4, bucket count +8); a node is `{ResourceKey species, record {relation,
  ?, impressed, killed, -1}, ..., next at +0x20}`. `SpeciesRelationMap_Record` 0xBA3B20
  (thiscall on manager +0x20) returns the record, inserting a zero one.
* ✔ `GetRelationship` 0xBA40C0 `thiscall(mgr, key*, a, b) ret 0xC`: the record's relation, 0 = not
  set (then the archetype tuning +0x390 decides). Creature stage values: 6 allied (and the own
  species), 5 extinct, 4 / 3 impressed some (~), 1-2 hostile (~).
* ✔ `ApplyEvent` 0xBA49E0 `thiscall(mgr, int event, key*, creature) ret 0xC`: event 3 = one
  creature impressed (record +8 += 1; ally at tuning +0x40C, 4 at +0x408), 0 / 5 = killed (+0xC;
  extinct at tuning +0x400 via 0xBA4730), 2 = attacked, 6 / 7 nest events. A change refreshes the
  creature UI (`CreatureGoalUI_Get` 0xD37CB0 -> [0x169F210], vt 0x28(0)).
* ✔ 0xBA4730 "the species is extinct": event-log message, and every herd (noun 0x01BE418E) of the
  species gets 0xC6AEA0 (~ stopped). 0xBA4820 on a change: allied -> every herd of the species
  0xC6A1E0(creature).
* ✔ `TakeDamage` calls ApplyEvent 2 for every hit and 0 / 5 for every death (also without a
  player attacker; a killed creature of an allied species breaks the alliance: Kooks went from
  (6, 3 impressed) to (1, 0 impressed, 1 killed) when one of them died, on the host). On a guest every replayed death of a mirror counted as a kill of the species,
  and a mirror that re-entered the relevance radius dead replayed it again: three of them made the
  species extinct on the guest ("Hunt: The Kook species is extinct!" on its card). Fix: the guest
  drops relationship events on mirrors and puppets (`social.cpp` hook), the host sends its table
  (MSG_SPECIES, on change and every 10 s) and the guest writes it into its own every 0.5 s.
* ✔ Kook tuning (archetype tuning +0xE84 of the creature): +0x3FC 1, +0x400 3 kills to extinct,
  +0x404 1, +0x408 2, +0x40C 3 impressed to ally ("Impress 3 Kook to ally"), +0x418 25 (ally DNA),
  +0x420 20, +0x424 1 social round, +0x428 5.0 s, +0x42C 0.05.

### 8.2 Goal cards

* ✔ A species card ("Impress 3 Kook to ally", "Hunt ...") is a game object, noun 0x1406A572, vtable
  0x146BED8. Init 0xC30300 copies the species key and the tuning thresholds; `SpeciesGoal_Update`
  0xC2EE60 `thiscall(goal, out, out) ret 8` reads impressed / killed / relation and, when the species
  just became allied (6) or extinct (5), pays the card's DNA (AddEvolutionPoints) and posts
  0x43715491 / 0xB4987E04; `SpeciesGoal_Refresh` 0xC2FB90 runs it and sets the texts. It runs when
  a relationship message reaches the card (handler 0xC2FD40): a table written by the mod needs a
  call of 0xC2FB90 on every such object. A card exists only for a species the player selected
  (a click on one of its creatures shows it), and its init caches the current counts: a card made
  after the species became allied pays nothing (✔ runtime: cached +0x8C impressed 3, +0xA0 relation
  6). Field ints: +0x7C state, +0x80 species key, +0x8C / +0x90 cached impressed / killed, +0x94
  ally DNA (25), +0x98 hunt DNA (20), +0xA0 cached relation, +0xB0..+0xBC the tuning thresholds.
* Co-op: whichever machine's card pays the ally / extinction reward, it goes to the host, which pays
  it once per species and outcome into the shared pool (MSG_SOCIAL kind 3; `goal_dna_paid` in
  `coop.social` on the host). Seen: Kooks allied by the guest, 2 x 10 DNA for the impressions and
  25 for the ally, once.

### 8.3 How the native social game runs

* ✔ The avatar's click on an NPC in the social stance posts the social stimulus 0x10000 (value 5)
  to the NPC's AI (`BT_PostStimulus` 0xBC9940 `thiscall(ai(+0xB4C)+8, maskLo, maskHi, float,
  sender) ret 0x10`). The NPC's behaviour tree answers: node 0x341BD4BE (table at 0x15943A0: start
  0xDA43D0 "SocialPropose", tick 0xDA49C0, end 0xD9F810) builds the session (rounds = tuning +0x424,
  +1 when +0xB58 bit 0x1 is set: ~ the alpha, its card reads "Alpha Kook"; the selection guard in
  coop.cpp treats the same bit as "pack member" (~ superseded 2026-10-27: mask 0x1 is not the pack, see docs/re/creature.md#creature-selection-gate)), the meter UI (0xE31xxx) and the prompts. The player's ability keys
  answer it; without such a session the keys do nothing (no animation either).
* ✔ The win: `CreatureSocialSuccess` 0xD9BC40 `thiscall(session, npc, other) ret 8` (called at
  0xDA4A6B when the meter is full): ApplyEvent 3, DNA = `CreatureDnaWorth` 0xC04260(npc, 1) (Kook:
  10) + a meter bonus, popup, message 0x60B4123 {avatar, other, 0} via 0xD39360, npc +0xB58 |= 0x80
  (impressed: green face), UI refresh. Returns the reaction animation (by relation: 1 0xF37DB906,
  2 / 5 0xD37DB8F6, 3 0x538BF747, else 0x738BF75B) that the tick plays with `CreaturePlayAnim`
  0xC122B0 `thiscall(c, anim, 1, -1) ret 0xC`.
* ✔ `CreatureSocialMode` 0xC0E7C0 (thiscall, no args): 0 = no social game with this NPC.
* ✔ Abilities: creature vt 0xB0 count, vt 0xB4 GetAbility(i); ability +8 type, +0x10 is 1 on the
  social ability (~: sing, type 0x25, index 4 on both test creatures). `PlayAbility` 0xC1E580
  `thiscall(c, index, 0) ret 8` plays it (-1 while cooling down). The ability mode (stance) is the
  global 0x169F374: 0 social, 1 combat.
* ✗ Letting a mirror run its own AI on the guest (to get the native session there) does not work:
  the mirror left its herd (+0x1674 = 0) and was destroyed by the game a minute later (a dangling
  proxy, a fault in coop.dll; 0xBA4730 then faulted on the null herd). Not pursued.
* Co-op (social.cpp): the guest's click engages the mirror (the stimulus is seen in the hook), a
  social key plays the guest's ability and sends it to the host (MSG_SOCIAL), the host counts the
  rounds the native game would ask for and applies the win like 0xD9BC40 without its meter (event
  3, DNA, impressed flag, reaction animation on both). Not done: the meter and the NPC's prompts on
  the guest, message 0x60B4123, abilities used by a click on the ability bar.

### 8.4 Mirrors in the nest herd

* ✔ Herds are game objects, noun 0x01BE418E; species profile at +0xA4, nest at +0x160. Mirrors on
  the guest go into the nest herd of their species (from the local NPCs the update filter removes,
  else the first nest herd of the species among the game objects): `Create` gives them the herd's
  archetype tuning (+0xE84), which the social code and the avatar-event handler (section 4) read.

### 8.5 The pause menu window

* ✔ The pause menu "Options" is one window (control id 0x0615B50D) that stays loaded: shown when
  its flag 0x1 and its parents' are set. coop.cpp looks it up 5 times a second (outside the frame)
  and does not draw the world labels while it or the co-op window is up.

## 9. Saves made while an editor opens do not load (2026-10-23)

Found on branch fix/vote-load-hang (regression: `coop_exit_menu.py` part 4; repro scripts and the
broken save were kept in `out/vote-load-hang/`, not in git).

* ✔ What happened in play (docs/stability/creature.md, "a guest in the galaxy menu when an
  evolution starts hangs in the loading screen"): the host's evolution vote ran, the guest left to
  the main menu; 5 s later `follow_phase` sent the host's world again (`left_to_menu`), which first
  **saves** (`save_game`: MessagePost 0x1CD20F0, written by the game over ~2 s, the "saved" box
  after 1.7-2.0 s). The vote ended at the same moment and the host's editor request was submitted
  (outside the frame) while the game wrote that save. The guest received it and stayed in
  `LoadGame` 0x1654C08 for good: frames running (30 fps), no fault, no watchdog stack (the main
  thread is not stuck, the load mode never switches to the stage); the screen shows a space
  backdrop with an asteroid. The next world was then cancelled by `via_menu_tick` after 60 s ("the
  game did not go to the galaxy menu": the game never leaves LoadGame).
* ✔ The save itself is the cause, not the guest's state: the same files copied into the host's
  profile (`coop.load Game5 Scalinus`, in a game that had just loaded the good save) hang the host's
  game in LoadGame the same way.
* ~ What differs in the broken `Scalinus.spo` (DBPF; resource 0x0179D310 is a table of noun ids
  and their counts, the other resources are one per noun type): no cVisiblePlanet (noun
  0x44462A6, 4 in a good save), new cRock 0x2A8FB3F (1) and cInteractiveOrnament 0x3A2511E (12),
  far more 0x06ABEE7E (0x686 instead of 0x1C2) and cFruitGroup 0x2E96892 (0x3EC instead of 0x360):
  the save holds the world half torn down / rebuilt for the editor. Which of these keeps the load
  mode from finishing is not known (?); the mode switch handlers 0xB336B0 / 0xB33A50 (old, new)
  call 0xB28AA0 (persistence manager: load, [mgr]+0x20 = LoadGame, `SimulatorModeMgr_SetMode`) on
  the way into LoadGame and 0xB33210 / 0xB331C0 on the way out (not decompiled further).
* ? Not checked: whether a save made with the editor fully open (mode 0xDBDBA1) loads; SporeCoop
  does not try it any more.
* What SporeCoop does: `world::save_game` asks coop.cpp's `save_blocker` first and does not save
  while an editor is open (0xDBDBA1 / 0x1654C06), the game loads (0x1654C08), an editor request
  went to the game less than 5 s ago or is held, or the simulator's mode switcher has a switch
  pending (`SimulatorModeMgr_ptr` [0x167FACC]+0x28); a waiting share retries every 2 s (after the
  editor: once the host is back in the stage). Editor requests (the vote's, and the game's own when
  the host is alone) wait while a save of ours is being written (`world::save_busy`: saved for a
  share and not shared yet, or the saved box not seen within 10 s) or a share still waits for its
  save (`world::save_pending`: otherwise, when the vote ended a frame before the share posted its
  save, the editor opened first, the save was refused and the guest waited in the galaxy menu for
  the whole editor session; seen once as a `coop_exit_menu.py` part 4 failure "back in the host
  world" with "together" only after the host's evolution); at most 15 s, dropped if the game
  left the stage meanwhile. A share whose save saw the game change modes is saved again.
  `coop.vote` prints "editor held for a save N".
* Timing seen with the fix: "back to the world: sending it to Bob" at t, the vote ends 0.06 s later,
  the save's box at t+2.0 s, shared at t+2.6 s, the editor opens at t+2.6 s; the guest is back in
  the creature stage 7 s after the vote ended.
* The guest's own editor at the end of a vote (MSG_VOTE "ended") is opened only when the guest is
  still in the creature stage, no mode switch is pending and neither the pause menu nor one of the
  game's message boxes is up when the outside-frame task runs. ✔ Before: a guest in the "Are you
  sure? All progress will be lost!" box when the vote ended got its editor opened under the box,
  and the "Don't Save" click right after left it in Editor mode (0xDBDBA1) with a black screen for
  good. The pause menu's boxes ("Do you want to save your game?", "Are you sure?") use the same
  container control id 0x06318E78 as the "game saved" box; the pause menu window (0x0615B50D) is
  hidden while they are up.
* Neighbours checked on lane 3 (`out/vote-load-hang/neighbours.py`, not in git), all pass with the
  fix: A) the guest leaves to the menu while the host is in its editor: it gets the evolved world
  when the host is back (8 s); B) the guest is in the exit boxes when the vote ends (above);
  C) the host leaves to the menu (`coop.menu`, the quit message) the moment a guest joins.
* ✔ C before the fix: our share save (posted for the join) and the quit message's own save ran
  together; the host's slot was broken and its next `coop.load` of it faulted at 0xBA7F87
  (`cStarManager::GetStarGridPosition+0xD57`, data 0x1188, called from 0xB27DA6 with 0x1654C08 on
  the stack) and stayed in LoadGame. A save made in the galaxy menu (a share that waited there) did
  the same. Now: `save_game` refuses outside a stage (adventures 0x1654C10 allowed), a waiting share
  saves only once the player controls something again, and a hook on `Simulator_HandleMessage`
  0xB63610 posts the quit message 0x4715068 again while our save is written (`world::save_busy`),
  as the game itself does while a mode switch is pending (`coop.vote`: "quit held for a save N").

## 10. A herd's land search and its home (host crash 0xB88BEA, 2026-10-25)

Player report (0.7.2.1, GOG 2024, Windows): the host crashed at SporeApp 0xB88BEA reading
0x18693000 (a page start) inside an NPC's behaviour tree tick. Addresses here are GOG 2024
(dc04aee5); static analysis in the private Ghidra project, runtime on lane 3.

* ✔ Call chain (the exception report's frame walk): `cCreatureAnimal::Update` 0xC0AF68 ->
  `NPCTickAI` 0xC09B2E -> `BehaviorTreeTick` 0xD99585 -> `BehaviorTreeTickGeneric` 0xBC953A ->
  a BT node 0xD7B180 (no static references: a node table entry) -> 0xC6BF60 -> 0xC6B750 ->
  0xB8BCC0 -> **0xB888C0** (the fault). The other return addresses of the stack scan are stale.
* ✔ 0xD7B180 (BT node, returns a float score): `herd = creature+0x1674` (getter 0xC046F0), asks
  the herd's land point (0xC6BF60) and scores 1.0 when the creature can go there.
* ✔ `cHerd` 0xC6BF60 `fastcall(herd) -> Vector3*`: computes once (flag +0x208) and returns the
  point at +0x20C. 0xC6B750 `thiscall(herd)`: centre = the nest's position (`+0x160` mpNest, its
  +0x34 interface vt 0x2C) or, **without a nest, the herd's home +0x34**; radius = +0x108
  mTerritoryRadius, or 100.0 (0x1582E8C) when it is 0; calls 0xB8BCC0(centre, radius, &out)
  on the planet model (`cPlanetModel::Get` 0xB3D450). Other callers of 0xC6BF60: 0xCD48F0,
  0xD818C0; of 0xC6B750: 0xC6CD90 (the herd's flora cache, flag +0x7C, points at +0x68..+0x70).
* ✔ 0xB8BCC0 `thiscall(planet, const Vector3* centre, float radius, Vector3* out) ret 0xC` =
  0xB888C0(centre, radius, out, 1.0, 1, 0): the nearest point within `radius` whose cell has the
  sign bit set (~ land) in the planet's cube map. **0xB888C0** `thiscall(planet, centre, radius,
  out, step, bool want, uint minRegion) ret 0x18`: the cube map is `planet+0x4C`, 6 faces x 128 x
  128 dwords (bit 31 the flag, bits 16..25 a region index into the vector +0x50..+0x54); radius
  capped at 500 (0x14654D4); the face rectangle is `ceil(128 * radius / |centre|)` cells around the
  centre's cell (0x685410 -> 0x685070 clamps the centre face and spills the rest onto the
  neighbour faces through 0x684EA0). The spill is only right while it is smaller than a face: for
  `radius / |centre|` above ~1 (a centre deep inside the planet) the neighbour rectangles start far
  below row/column 0 and the loop reads before the map (no bounds check).
* ✔ The crash: centre |c| = 12.3 (direction (0.58, 0.58, 0.58)), radius 100 (the default), so
  1039 cells; spans on the stack: face 4 0..128 x 0..128, face 1 rows -850..128, face 0 rows
  -845..128, face 3, face 2 columns -890..128. At face 1 row -233 the read hit an unmapped page
  53 KB below the map (map at 0x186A0008, a block of its own there). On lane 3 (Wine) the same call
  with a home 7.3 from the centre (`call 0xC6BF60 thiscall <herd>`) read about 870 KB before the map
  without a fault (the map sits inside the heap) and returned a land point anyway: the reads past
  the map happen every time, the fault depends on what lies before it.
* ✔ Only enabled herds are updated (`cHerd` update 0xC6D960 is called from 0xD45740 only when
  +0x218 is set, getter 0xC6A1C0); it also runs 0xC6CD90 -> 0xC6B750. Our herds are disabled
  (`herd_quiet`), so the land point of a herd of ours is computed when an NPC of it runs the BT node
  above (seen: `land_cached` 1 on a herd whose only member was a left-over puppet), or when the game
  enables the herd again (seen after the editor: empty herds of ours with `en=1`).
* ✔ Why a herd of ours had such a home (mod bug, two parts, both seen on lane 3 with
  `coop.npc herds`):
  1. `CreateHerd(pos)` stores `pos` as the home (+0x34) and nothing moves it later. The puppet's
     `pos` came from `SnapshotBuffer::sample`, which blended the first snapshot of a player back in
     the world with the one before it, sent while the player was out of it (editor, load: position
     (0,0,0), no PF_IN_WORLD). A puppet made at that moment got a herd home anywhere between the
     centre and the surface: homes 264, 322, 114, 97, 73, 45, 32 seen (planet radius 505), one or
     two per evolution.
  2. When the local player leaves the world (`g_local` null in the editor) `forget_puppets` only
     unregistered the puppets ("destroyed with the world"), but the creature world stays through the
     editor: the old puppet stood on as an unregistered creature, an NPC with AI (NPCTickAI no
     longer skipped it) in its nestless herd, next to the new puppet, mirrored to the guests as an
     NPC (the players' "these guys look like our species"). Its BT ran the land search around the
     bad home: the crash.
* Fix (2026-10-25, fix/creature-ai-crash): the snapshot buffer does not blend across a change of
  mode, world or PF_IN_WORLD (`net/interp.h`, unit test `test_interp_gap`); puppets the game did
  not destroy are removed once the player is back in the same world (`coop.cpp` strays,
  `coop.status` "strays: pending removed"); a puppet's herd keeps its home under the puppet
  (`game::herd_home`, clears +0x208). Test: `coop_editor_strays.py` (before: homes [322, 73, 97],
  a left-over puppet per evolution on the host; after: all homes 505, none left over).
* ? Not done: puppets saved into the host's planet (a save while a partner plays) come back after
  a load as NPCs in herds we cannot tell from the game's (docs/known-issues.md); their homes are on
  the surface now. Each puppet spawn still makes a herd (2 per evolution on the host, empty
  afterwards and saved with the planet).

## 11. The pack, NPC ids across worlds, NPC species only the host has (2026-10-25)

From the player report "after customization ... he doesn't even see my companion ... he can't eat
the ones I killed since he still sees them alive" (fix/creature-desync, `coop_creature_desync.py`).

* ✔ The pack: `cPosseSimulator_Get` 0xD53980 (cdecl, no args; vtable 0x147B760) and
  `cPosseSimulator_AddMember` 0xD542E0 `thiscall(posse, creature) ret 4 -> bool` (GOG 2024). The end
  of a social game with an ally (0xD9F7A0, session flag `+0x1FC & 0x10`: "recruit") calls them.
  AddMember calls the noun manager's AddPosseMember 0xB23430: creature `+0xB58 |= 0x100` and a
  push onto `cGameNounManager +0x5C` (mPosseMembers, begin / end / capacity at +0x5C / +0x60 /
  +0x64; 0xB1F900 returns its address), then audio, a random name if it has none, the "first
  posse" hint (message 0x533535DA, `CRG_FirstPosse`) and achievements. ✔ runtime: a Kook made a
  member this way walks to the host and stays with it; it keeps its own (nest) herd, so the guest
  mirrors it like any NPC (into its nest herd, not the avatar's), also after the host loads a save
  with it. `coop.npc recruit <id>` (host) does it for tests; `coop.npc list` marks members `pack`.
* ✔ The host numbers its NPCs per world: when its avatar changes (back from an editor, a load) every
  NPC gets a new net id. Before 2026-10-25 the old ids were not despawned and the guest kept every
  old proxy (no timeout): after one host editor the guest showed 40 NPCs for the host's 20, the old
  ones standing where they were. A kill on the host killed the new copy only: "he still sees them
  alive". Now the host despawns them (`npc::on_world_change`) and a guest drops a proxy the host
  sent no state for in 5 s while it sends states for others (`stale_dropped` in `coop.npc`).
* ✔ NPC species of the host's planet need not exist on the guest: a creature of the host's own My
  Creations (a test: `coop.npc spawn 29020CC4`, the fixture's "Judge") was "proxy spawn failed
  (unknown species)" on a guest without it, never shown (the guest's own copy of the NPC, if its
  world had one, is removed by the update filter). A world load with such a creature did not fault
  on the guest. ~ Spore populates planets from the player's own creations and Sporepedia downloads
  (Pollination.package), so a real host's planet has such species and its allies may be of them.
  Now the guest asks the host (MSG_CREATION kind 4, id = crt instance), the host reads the records
  (bem / crt XML + pollen metadata) from the file of the database the game finds the key in
  (`FindRecord` RM vt 0x30, `GetLocation` db vt 0x28: EditorSaves.package DBPF or
  Pollination.package DBBF, read piece by piece) and sends them (kind 3); the guest registers them
  under the same key (priority 2000) and the next proxy try builds the NPC (~60 ms on one PC).
* ? Not checked: a downloaded Sporepedia creature as the species (the test profiles are offline:
  their Pollination holds 7 imported XML crt), the Tribe stage mirrors (tribe.cpp) of such species.

## 12. Saves keep our creatures (host crash 0xD48634, 2026-10-25)

* ✔ A creature-stage save holds the herds and, with them, their creatures: a host save made while
  the partner's puppet existed keeps the puppet and its herd (`CreateHerd(.., 0, 0, 0, 0)`:
  archetype +0x88 = 0, no nest +0x160, not avatar-owned +0x84; target size 0, enabled 0 or 1 as we
  left them). Every herd of the game has an archetype (137 herds on Scalinus, none without), so
  "no archetype, no nest, not the avatar's" marks a herd of ours.
* ✔ Loaded in the same game process (no restart) the puppet did **not** come back (its herds did).
  Loaded after a restart of the game (the players' "after I loaded save after crash"), it is back
  as a plain game creature: no tuning (+0xE84 = 0, its herd has no archetype), AI on, flags 0x800,
  in the last puppet herd, and of **another species** than the partner's (the partner's creation
  key 0x7C.. is unknown before it joins; runs gave 067CCFEC, 06538DD6, 065CBC7C, 065CBE27, 068058BD,
  a creature of the planet's species list): "these guys appeared after I loaded save". The host
  tracks it as an NPC; the first attack on it reaches the handler of section 4 and faults at
  0xD48634 (`eax=0`, `ecx=esi=` the creature, `edi=-1`, the registers of the players' report).
* ✔ Each puppet spawn used to make a new herd (`creature_spawn`), never destroyed: a session with
  rejoins and editors left one per spawn (4 after two rejoins), and every save kept them all.
* Co-op (fix/creature-e84): the handler's guard (coop.cpp h_StageMsg) also gives the creature of
  0xD335362C / 0x60B4123 `msg+4` a blank tuning when it has none, whatever it is;
  `npc.cpp remove_saved_strays` removes, once a second in the creature stage on every machine (also
  without a session), every creature without tuning in a herd like ours that is not ours (also from
  saves that hold them already; log line "removed a creature of species .. without tuning");
  `creature_spawn` reuses a herd like ours of the species (`game::own_herd_for`) instead of a new one.
  Not done: our creatures are still written into saves (the save is the game's generic
  `COMSerializer::SaveClassObjects` 0x69FB90 over all objects; no per-object "do not save" flag
  found), and the empty herds stay (harmless: target 0; ~ superseded (2026-10-28): see [empty puppet herds](#creature-save-strip-empty-herds)). Regression: `coop_creature_e84.py`.
* ? `+0xB54` of a creature (the handler needs it and its `+0x17C` set).
* ✗ Real input: in the fixture the host's clicks on the restored creature did not make the avatar
  walk or attack (WASD did move it; key 1 hit nothing at 3-5 m); the test posts the game's attack
  message instead (`coop.npc attackmsg`).

## 13. Respawn, the hatchling and its greeting (player report "growing up from egg", 2026-10-25)

Report (the guest "Dexie", GOG 2024, 0.7.2.x): "After my death I'm in constant state of growing up
from egg and going back into small version and again grow back. Can't move at all." Screenshots: the
guest's creature with the egg shell on its head on both screens, flower particles, at the nest.

* ✔ The respawn after the death caption's OK is `0xD40FA0` (the caller of `0xC04F30(0)` at 0xD4108A):
  the same avatar object (the pointer does not change) goes to its herd's nest (`0xC6AE60` herd ->
  nest position, the herd's home +0x34 without a nest), scale 0.1, flags `+0xB58 |= 0x800`, age 0
  (`+0xB34`), the same for every pack member (cPosseSimulator list `0xB1F900`), then the cinematic
  `CRG_Fade_From_Black_Egg`. The hatchling has half the health (Kook test creature 5/10, the guest's
  Kirgle 8/16) and its behaviour tree (`[creature+0xB4C]+0x1D8`) is `0x9413EDEB`: it waits.
* ✔ A **family member of the nest** greets it: the BT node `0xD64A30` (table at .data 0x1583EF8,
  id 0x9413EDEB at 0x1583F00) ticks on that NPC (`[esp+4]`, the tree owner) with the avatar
  (`GetAvatar`) as the other. States (`[esi]`, jump table 0xD650BC): 0 waits for avatar +0x135 and
  the avatar's tree 0x9413EDEB, plays 0xF41BB04C on the avatar and 0xB52C67F7 on the NPC; 2 the
  avatar's scale (vt 0x80(0)); 1 more animations (0x341B7A96 / 0x141B7A5C); 3; 4 abilities and a
  stimulus 0x10 to the creatures around; 9 when the NPC's animation ends: **avatar vt 0xE8(1)**
  (`0xC04F30`, `SporeCoop::cCreatureAnimal_GrowTo`) and `cPosseSimulator 0xD53980` -> `0xD52DF0(1)`
  (the pack grows too); 10 holds while the avatar stays within 5 m of the nest.
* ✔ `0xC04F30(1)` sets `+0x1688 = 1`; `cCreatureAnimal::Update` runs the grow sequence `0xC04FB0`
  (state +0x1688: 1 effect 0x3A616FEE/0x715FB4A7 and the target scale, 2 scales up and calls SetAge
  `0xC22370(1)`, combatant vt 0x2C(1), 3, 4 stops the effect). Then age 1, full health, the avatar's
  tree changes (0x02DDE8FE on both test creatures) and the player has control again. On the host this
  took ~13-19 s after the OK (probe: 0xC04F30(1) from 0xD65026 <- 0xD61C39 <- BehaviorTreeTickGeneric).
* ✔ **A guest has no family**: `npc.cpp update_filter` removes the guest's own NPCs (the host's NPCs
  are the world), and the host's family members on the guest are proxies, puppets without AI
  (`h_NPCTickAI`). Nobody greets the guest's hatchling: it stayed age 0, tree 0x9413EDEB, half the
  health, the egg shell on, W did nothing, for minutes (lane 3, with and without the guest's own
  creation, by `coop.team die` and by a lethal hit after a pack rescue). ~ The host's puppet of
  the guest replays the guest's animations, which fits the egg shell the players saw on the guest
  on the host's screen too (not checked in the test). The host itself always grew up (its own
  family greets it).
* ✔ Calling `0xC04F30(avatar, 1)` (`call 0xC04F30 thiscall <avatar> 1`) on the stuck guest grew it
  up within a second, the tree changed and W moved it again.
* Co-op (fix/death-loop): `npc.cpp hatch_tick` does what state 9 does (`game::grow_up`: 0xC04F30(1)
  and 0xD52DF0(1)) when the avatar has been a waiting hatchling for 4 s of simulated time without a
  cinematic on a guest (NPC sync on, the host present), or 40 s on the host (a greeter that never
  comes, e.g. a partner's puppet picked from the avatar herd). Log line "the hatchling waited for
  its family". `coop.avatar` prints `age=` and `hatchling_waiting=`; `coop.npc hatch off` turns it
  off (tests). Regression: `coop_creature_respawn.py`.
* ~ The same wait comes at the start of the Creature stage (the first hatch after the Cell stage);
  not tested on a guest here (the fixture starts adult), the fix covers it the same way.
* ✗ The jump of the guest's hatchling ~10 s after the OK (5-6 m, a different spot each time, not
  through `Teleport` 0xC0C040 or `SetPosition` 0xC0DEA0 as far as the probes saw) is not explained;
  the host's hatchling did not move. Harmless once the hatchling grows.
* ✔ Seen on the way: a lethal hit right next to the nest after a pack rescue shows the death
  caption, but `mbDead` (+0xB5E) stays 0 and the health climbs again while the caption waits (the
  nest heals, the pack regeneration too); the OK respawns it normally.

## 14. Deaths of mirrors, whom the creature AI targets (2026-10-25)

Player report (0.7.2.x, host Windows GOG 2024): "enemies still seen as alive even when dead for
quest" (a screenshot: a creature standing next to the player with its health bar at 0) and
"aggressive enemies didn't target my friend". Addresses GOG 2024 (dc04aee5); static analysis in
the private Ghidra project, runtime on lane 4 (fix/creature-kills, `coop_creature_kills.py`).

### 14.1 How a creature dies

* ✔ `cCombatant::TakeDamage` 0xBFCF10 (this = creature +0x5A8): combatant `+0x34` is the state
  (2 = dead; the tiny getter 0x8E8230 `mov eax,[ecx+0x34]` is what Ghidra calls
  `PFIndexModifiable::GetFileCount`), `+0x38` the health, `+0x80` a byte "invulnerable" (no
  damage), `+0x28` the last attacker. A hit that takes the health below 1.5e-5 sets health 0 and
  state 2 and posts message 0x1622184; nothing else. `cCreatureAnimal::TakeDamage` 0xC075E0 ->
  0xC1A520 (creature part: adventure armour, then cCombatant) and, on state 2, the kill credit
  (section 2), ApplyEvent 0 / 5 (the kill count of the species), 0xD3353639 to the strategy.
* ✔ The creature itself dies later, in its **behaviour tree**: the death node 0xD85B80 (start
  function of a BT node, no static references) calls the creature's `Die` = vt 0x8C 0xC08210
  (cCreatureAnimal vtable 0x146A080: Update is slot 0x60, attack slot 0xB8) and picks the death
  animation: 0xF14FAA46 by default, 0x54AC179 / 0x60DB981 / 0x779E183 for damage types 5 / 0x11 /
  0x12, 0x92AA59CE after a fall (> 50), 0x5807346 (- 1 for an epic) when it was dead already; the
  corpse stays 60 s in the creature stage (15 s in the tribe stage). `probe 0xC08210` on the host
  for an NPC kill: called from 0xD85BA7 <- `BehaviorTreeTickGeneric` 0xBC9666 (inside NPCTickAI).
* ✔ `Die` 0xC08210 (thiscall, no args): stops the locomotion (vt 0x84(0,0,0), spatial vt 0xDC),
  clears the velocity (0xC41EC0 with zero), resets targets (+0xE8C.. = -1), sets **mbDead +0xB5E =
  1**, clears the stimuli (0xBC9F50(-1)), stops effects (`StopEffectFromPools` 0x2C4BF1C). Other
  writers of +0xB5E: 0xC070D0 (adventure respawn / reset), 0xC0BAF0 (vt slot, sets 1), SetSpecies
  0xC03920 (clears), 0xC1FA30.
* ✔ So a creature without AI (our mirrors on the guest, puppets: NPCTickAI is skipped,
  `game.cpp h_NPCTickAI`) never dies by damage: a lethal hit leaves it standing at state 2 with 0
  health, alive for the game (`is_dead` false), the "standing at 0 hp" of the report. On the guest
  the replayed death of a mirror (npc.cpp, NF_DEAD from the host) did exactly that: `coop.npc
  creatures <species>` showed the mirror without " DEAD", a screenshot showed it standing with "0"
  over it (lane 4, 2026-10-25). The host's own NPCs and the kill count were right (the hunt card
  "Hunt 1 Joobel 2/3" on both). Since then npc.cpp calls `Die` and plays 0xF14FAA46 after the
  lethal hit (`game::play_death`, `deaths_played=` in the guest's `coop.npc`).
* ✔ The kill count of the hunt card: a guest's kill pays and counts on the host (the lethal hit is
  applied as the host avatar's, section 2), the guest gets the table (section 8.1); checked with
  real input (combat stance, a click, key 1: 11 bites for a 7.5 hp Kook).

### 14.2 Whom the creature AI notices

* ✔ `NPCTickAI` 0xC09570 (thiscall(creature, float dt)) returns at once when `*(+0xB4C)` (the AI
  block) is null. For a living NPC it takes **the avatar only**: `EDI = cGameNounManager +0x54`
  (`_Get_54` 0xB1FD00), and decides whether the NPC notices it: by sight (distance within a range
  from 0xD38A30(1 / 2) and 0xC0E650, inside a view cone cos(0xC0C170 / 2)) or by hearing
  (0xC0D6C0(npc, avatar, scale, dist): `avatar +0xB80` (a noise radius, 6.0; 0 while sneaking)
  times the tuning `+0x354` plus the bounding radii). Noticed: 0xC068B0 / 0xC07450 / 0xC07480
  (the NPC's awareness list `+0x1124`) and **`BT_PostStimulus(0x4000000, 0, 5.0, avatar)`** to the
  NPC's AI (ai + 8). Then, when the avatar sneaks (`avatar +0xBB1`, set by 0xC1B030 with the sneak
  ability type 0xD / 0x52), stimulus (0, 8) or 0x100 with the avatar as sender; NPC `+0x101C` =
  1 / 2 / 0. The behaviour tree reacts to these (attack, flee, look) by its archetype.
* ✔ No other creature is ever noticed this way: a partner's puppet on the host never got the
  0x4000000 stimulus (`coop_creature_kills.py` reads the NPC's 8 stimulus slots of 0x38 bytes:
  masks +0 / +4, value +8, sender +0x2C; the union of masks at ai+8 +0x1C0 / +0x1C4). A noticed
  stimulus decays out of its slot about 4 s after the last post.
* ✔ Retaliation is not avatar-only: `TakeDamage` posts 0x400 to the victim and 0x200 / 0x80000 to
  its herd with **the attacker** as sender, so NPCs a guest hit (the hit is applied with the
  guest's puppet as the attacker) fight back against the puppet, and their hits reach the guest
  (`damage_in` on the guest; a 065CB956 nest killed the guest in one test). Epic creatures
  (archetypes 0xFC811218 / 0x95511253, herds without a nest) attack any creature near them, the
  puppet too (a spawned epic killed another; a planet epic killed the guest with
  `coop.npc partners off`).
* ~ Not found in the `creature` fixture (Scalinus): a non-epic species that attacks the avatar on
  sight. Standing 3 m from members of 11 archetypes cost the host no health (time of day not
  controlled); the report's "aggressive enemies" are species with an aggressive archetype on the players' planet.
  The notice step above is the same for them (it is the only place the AI looks for the player).
* Co-op (npc.cpp `ai_target`, game.cpp `h_NPCTickAI`): on the host an NPC that is nearer (by 3 m,
  hysteresis) to a partner's puppet than to the host's avatar, and within 80 m of it, ticks with
  the puppet as mpAvatar (+0x54 swapped around `NPCTickAI`, the puppet borrows the avatar's tuning
  +0xE84 if it has none, `game::avatar()` returns the real avatar meanwhile). Not for the pack
  (+0xB58 & 0x100), the avatar's herd, dead NPCs, partners who are dead or out of the world.
  `coop.npc`: `partner_targets on_partner partner_picks ai_swaps`; `coop.npc partners on|off`,
  config `npcs_target_partners`. The NPC's hits on the puppet go to the partner (MSG_DAMAGE).
* ✗ A spawned epic (`coop.npc epic`, CreateHerd + archetype 0x95511253 or 0xFC811218, also with
  the planet epics' herd fields group +0x8C = 2, territory +0x108 = 50, personality +0x15C = 9 and
  the herd enabled) wandered and fought other creatures but never attacked the avatar 6-10 m away
  (the planet's own epic killed it within 4 s). What else the game sets up for its epics is open.
* ✗ `cTimeOfDay::SetTimeOfDay` 0xBC3070 takes `thiscall(tod, float, void* out)`; the presets
  0xBC30E0 `thiscall(tod, int 0..3, void*)` (times 199.68 / 285.12 / 679.68 / 760.32 at
  0x156CFB8). Called from the console with a null or a short string buffer as `out` it faulted at
  0xBC2DC8 and the game stopped drawing: needs a real output buffer (a quaternion?).

## 15. Another creation under the same key: the host's nests on the guest (2026-10-25)

From the player report (screenshots of both players at one nest): the host saw blue "Woopsies" and
"Impress 3 Woopsies to ally 0/3", the guest small creatures of another look and "The Datzu are your
allies! You can heal at their nest or add them to your pack!" (fix/nest-species,
`coop_nest_species.py`).

* ✔ Creation ids are per profile and sequential (docs/re/creations.md 1.2: the first own cell was
  0x290742F6 in both test profiles), and Spore populates a planet with the player's own creations
  too. A guest that made creatures owns other creations under the keys of the host's: its game
  resolved the host's species key to **its own creation** (the planet's herds and nests from the
  received save, the NPC proxies from MSG_NPC_SPAWN, the species card's name), and its species
  table entry of that key (the host's) went to a nest it saw as its own species. Nothing failed,
  so the 0.7.2.3 species transfer (asks only for unknown keys) never ran.
* ✔ **The game's caches go by key**: with the guest's My Creations holding the Kirgle under the
  Judge's key 29020CC4, the guest still built the Judge there (exp. 2026-10-25, lane 5): its
  `Pollination.package` held a `creaturedata` 0x0F43029A of 29020CC4 (and its graphics cache a baked
  model) from earlier sessions in which it had got the host's Judge. So registering the host's
  records under the host's key (priority 2000, above My Creations) cannot win over a guest that has
  ever used its own creation of that key: the guest needs a key of its own for it.
* ✔ `cSpeciesManager` keeps its profiles for the whole process (the map at mgr +4, node value
  +0xC): after a world load the guest's profiles of the planet's species were the same objects
  (pointers equal). `GetSpeciesProfile` 0x4DF5C0 (GOG 2024) creates one when the node's value is
  null; `0x4DF740` (the lookup) reloads a profile whose `+0x53C` (version) is 0 from its
  `creaturedata` (`LoadCreatureData` via 0x4D4F70). Not used by the fix.
* ✔ A planet save names its species big-endian `{group 40626200, type 2B978C46, instance}` in the
  records of herds (01BE418E), creatures (018EB45E), the cPlayer (02C21781), 03572E72 and the
  self-keyed 01897C18 (Scalinus: 60 species, the planet's list), most records RefPack-compressed;
  the pack list in 01897C18 names its members' species little-endian `{type, instance, group}`
  (`00000110`, then one key per member: the avatar's species and a recruited Judge).
* ✗ **Rewriting the received save to the guest's own key does not work.** With every key of a host
  creation replaced (big- and little-endian, the changed records appended uncompressed, nothing else
  different: a byte diff showed exactly those keys) the guest's load **dropped the herd and the
  creature of that species** (none of them after the load, while the same save with the host's key
  kept both: `coop.npc creatures`, exp. 2026-10-25), also with the species profile made beforehand,
  the creation's `creaturedata` in the package and its metadata naming the new key. A pack member of
  it then left an empty creature (+0xB20 and +0xB28 zero) in the pack vector (`cGameNounManager
  +0x5C`), and the pack panel faulted building its item (0xD2C361 in 0xD2C2E0, `creature+0xB20
  ->+0x50C`, then thousands of faults at 0x93B630; `coop_creature_desync.py`). The planet's
  `stars.db` rewritten the same way made the load fault at once in `cStarRecord::GetPlanetRecord`
  (0xBBABE2, null star record; `coop_editor_desync.py`). Why the game drops a species only a
  registered package knows (an unknown key is replaced by a planet species instead, section 12) was
  not found.
* Co-op (creations.cpp, MSG_CREATION kind 5): the host finds the species keys in the files it
  shares (`dbpf::find_keys`) and sends, before the offer, the records (bem, metadata and its
  creaturedata, from the files the game reads them from) of every one its EditorSaves database
  answers for; the same before an NPC spawn of one (`creations::host_species_out`). The guest
  registers each under `0x7D000000 | fnv(bem + metadata) & 0xFFFFFF` (the same creation gets the
  same key every session, which keeps the caches right), and under the host's key too when it has
  nothing there. Its NPC proxies of the species use that key (`creations::local_species` on
  MSG_NPC_SPAWN), go into the herd of the host's key (the save's nest: its archetype tuning), and the
  host's species table is written under both keys; a goal reward goes back under the host's key.
  The save keeps the host's key: the guest's own NPCs of it (built as the guest's creature) are
  removed by the update filter as always, so what the guest sees are the host's creatures, and their
  cards and relation are the host's. Premades and Sporepedia downloads keep their keys.
* ? Still the guest's: a card made from the nest itself (the "The Datzu are your allies!" kind: the
  nest's herd has the species of the host's key, which is the guest's creation there; its relation
  is the host's record, its name the guest's). Not checked: a real nest (herd with an archetype) of a
  host creation (the fixture planet has Maxis species only; the test's NPC comes from `coop.npc
  spawn`), the Tribe and later stages with such species, a downloaded Sporepedia creation under a
  colliding key (ids from the server: not expected to collide).

## 16. Migration to a new nest, cinematics and their actors (guest crash 0xC6AE60, 2026-10-25)

Player report (0.7.2.3, GOG 2024, Windows): "My friend crashed after I completed migration to a new
nest". The guest's game: ACCESS_VIOLATION reading 0x160 at 0xC6AE60, `ecx=eax=0`. Addresses GOG
2024, static analysis in the private Ghidra project, runtime on lane 6 (`coop_migration.py`).

* ✔ 0xC6AE60 `SporeCoop::Herd_NestPosition` (herd in ecx) -> `herd+0x160 ? nest+0x34 vt 0x2C : herd+0x34`,
  no null check: the herd was null. The caller 0xAD2A20 (ret 0xAD2CAC) is the cinematic code that
  computes an actor's position from a mode: mode 3 = "relative to the nest of the creature's herd"
  (`NounCast(actor, 0x18EB45E)` -> `+0x1674` getter 0xC046F0 -> 0xC6AE60). It runs in callbacks the
  cinematic manager executes for the posted message 0x4470A41 (msg+8 -> {+0xC arg, +0x10 fn},
  handler 0xAE0DB0 at 0xAE1E02, from `MessageSend` 0x88568C): the players' stack scan and the
  lane's `exc` give the same chain (0x88568C, 0xADB1E7, 0xAD8FF3, 0xAD37F1, 0xAD46A4, 0xAD91D9,
  0xAD7296, 0xAE1E04). Actor ids of the crashed cinematic on the stack: 0xEFE76209 (the actor
  placed with mode 3), 0xF772903D.
* ✔ Cinematic ids are the FNV-1 hashes of the lower-case names (`coop.status` "cinematic X
  playing"): CRG_SpeciesMigrate FBB3661D, CRG_ClaimNest 63B7060A, CRG_BrainUpgrade1..4
  F6A4B1E1..E4, CRG_Fade_From_Black_Egg F4F96E43, CRG_LaidEgg B6A1DBE1, CRG_FirstPartUnlock
  16339295. Started by hand on a guest (`call Simulator::cCinematicManager::PlayCinematic thiscall
  <cm> s:<name> 1 1 0 0 0`), only CRG_ClaimNest places actors with mode 3 (actor 0xEFE76209 = each
  member of the avatar's herd, +0x40..+0x44: the avatar, the family); CRG_SpeciesMigrate uses modes
  7/8, the brain upgrades and the egg mode 0.
* ✔ The migration (player properties at cPlayer +0x69C, `SporeCoop::Player_GetProperty` 0xC774E0;
  cPlayer = noun manager +0x74): the total DNA earned in the stage is cPlayer +0x10F0
  (AddEvolutionPoints adds to it, the co-op DNA sync too, so both players cross the thresholds
  together). Back from the editor (`CreatureGame_AfterEditor` 0xD40FA0, message 0x4F60B92) a total
  above the next threshold of tuning 0x4098097 (last one in property 0x54FB6BF) sets 0x514A219 = 1,
  empties the avatar herd's brain range (+0x120 = 5, +0x124 = -1) and plays CRG_SpeciesMigrate
  ("Your species is migrating to a new nest. Follow them ..."). The creature is a hatchling
  (+0xB34 age 0) until that cinematic ends (age 1). `CreatureGame_NestTick` 0xD41510 (every frame):
  0x514A219 == 1 and age 1 -> the goal "Migrate to a new nest" (25 DNA), state 2; state 2 and the
  avatar near the nest of `CreatureGame_NestHerdFor(1)` 0xD40AF0 (nearest herd of the player species
  with a nest whose brain range holds the level 0x169E370) -> `CreatureGame_JoinNestHerd` 0xD40C70
  (the avatar moves into that herd, the property goes) and CRG_ClaimNest ("You have claimed this
  nest"). 250 DNA from 0 gave CRG_BrainUpgrade1 and one migration; 1000 gave CRG_BrainUpgrade4
  ("Get ready to evolve to the tribal stage").
* ✔ The guest's game has the same migration when the host's world carries it: a save made after
  the host confirmed the caption has state 2 and the grown-up creature, so the guest's own game
  claims the nest when the guest arrives there with the host, and plays CRG_ClaimNest with the new
  nest herd's members as the family. On the guest those members are its own game's creatures; the
  update filter (npc.cpp, the host owns the NPCs) removes them in the same frame (DestroyInstance ->
  0xC083D0 removes the creature from its herd and clears +0x1674), and the placement callbacks of
  the cinematic run after that: 5 faults at 0xC6AE60 on the lane (`coop_migration.py --without-fix`),
  a crash on Windows. The host's own claim is fine (its game's creatures stay).
* ~ The world the host sends right after its editor is saved during CRG_SpeciesMigrate: the guest's
  creature is then the hatchling (age 0, state 1) and its game never sets the goal or claims (seen 3
  times on lane 6). The players' guest got a later save (a rejoin, a reload) or the timing differs.
* Fix (fix/migration-crash): `game::destroy_creature` keeps a creature (noun 0x18EB45E) while a
  cinematic plays (cCinematicManager +0x2C) and removes it when it is over
  (`destroy_deferred_tick`, from npc.cpp every frame; not after a world change). `coop.npc` prints
  "destroys during cinematics: deferred= done_after= pending="; `coop.npc cinedefer off` restores
  the old behaviour for tests. In the test 19-25 removals per claim waited, all gone afterwards.

## Selection gate of CreatureSelectionUI and the +0xB58 bits (2026-10-27, fix/w1-creature)
<a id="creature-selection-gate"></a>

* ✔ (BUG-0118, GOG 2024 disassembly of `Simulator::CreatureSelectionUI` 0xB6C970, re-read with
  objdump 2026-10-27) The read of `(+0xE84)->+0x32C` at 0xB6CA11 runs only when all of these hold:
  the selected object casts to cCreatureAnimal (vt 0xC, 0xD0036E08), adult (0xC0B8D0: `+0xB34 == 1`),
  `+0xB58 & 0x2` **clear** (`test al,2; jne skip` at 0xB6C9C9), `+0xB58 & 0x1` **set** (`test al,1;
  je skip` at 0xB6C9CD), creature vt 0xD0 and vt 0xD4 both false. Then `call 0xC0C300` (`mov
  eax,[ecx+0xE84]`) and `[eax+0x32C]`. The rest of the function (0xB6CA37..0xB6CB0E) and its direct
  callees (0xB6BBF0, 0xB678E0, 0xB686E0, 0xB683A0, 0xB67C90, 0xB69110, 0xB687A0, 0xB6A820,
  0xB69560, 0xC048B0) do not touch +0xE84.
* So the selection guard in coop.cpp (`h_SelectUI`: hides mask 0x1 for the call when +0xE84 is
  null) covers exactly the path that reads +0xE84. A set mask 0x2 makes the game skip the read; it
  is not a second way into it.
* CORRECTION (2026-10-27, fix/w1-creature): the `.sym` note of 0xB6C970 said "reads +0xB58 test
  al,2, then call 0xC0C300", which read like a gate on 0x2; 0x2 is the early-out and 0x1 the gate
  (`overrides_dc04aee5.sym` now says so). The GOG 2016 note "a pack member (+0xB58 bit 1)" (frozen
  file) means mask 0x1 and is not the pack: the pack is mask 0x100 (AddPosseMember 0xB23430,
  section 11). The coop.cpp comment called mask 0x1 "pack member"; it now names the mask only. What
  mask 0x1 means is still ~ the alpha (SocialPropose 0xDA43D0 adds a round for it); mask 0x2 is the
  bit the stage event handler 0xD47FC0 sets after an event (the 0xD48634 notes above).

## Guest kill list and the frame after a world teardown (2026-10-27, fix/w1-creature)
<a id="creature-kill-list"></a>

* ✔ (BUG-0082) npc.cpp's update filter runs inside the game's creature update and collects the
  guest's own NPCs into a kill list; `guest_tick`, later in the same frame, destroys them. Frames
  that skip `guest_tick` (the host away in the main menu, NPC sync off, another stage) must drop the
  list: `npc::tick` clears it at the end of every frame. Before, it waited until the host was back
  (seen: 5 entries held across 2.5 s of the host's absence, `coop_host_away_kill.py --without-fix`).
* ✔ (BUG-0091) A world teardown (the guest's `coop.menu` / a received world / a load) frees the
  creature puppets between two frames, and the host's reliable MSG_ANIM queued meanwhile arrive in
  the first frame after it. `on_frame` handled them before it noticed the avatar change: in 6 rounds
  of a guest's `coop.menu` with the host sending 10 animations a second, 15 `PlayAnimation` calls
  went to puppets no longer in the object list (`coop.framefix 0`), without a fault (the memory was
  still mapped). Now the avatar check runs before `g_sess.update()`; `on_remote_anim` and the
  remote despawn also check `game::creature_listed` (a walk of the noun manager's object list: too
  costly for every puppet every frame, fine for events).
* ✔ A guest's `coop.menu` round (galaxy menu, the host sends its world, the guest is back in the
  creature stage with the host's puppet) takes 11-12 s on a lane.

## The creature pool, NPC net ids and the AI swap (2026-10-28, fix/w2-creature)
<a id="creature-pool-ids"></a>

* ✔ `cCreatureAnimal::Create` (GOG 2024 0xC09B40) with its last argument (reuse) set calls
  0xAD2020(species profile, ...): it walks the creature noun's pool (0xAD0BE0(0x018EB45E): slots of
  0xC bytes, +4 in-use byte, +8 object) for a free slot whose object has the same species profile
  (+0xB20) and takes it; without one (only in the creature and tribe stages, mode 0x1654C01 /
  0x1654C02) it grows the pool. Without reuse it gets a new object of the noun (0xB20BF0). So a
  recycled creature object is normally of the **same species**: the case the host's NPC ids must
  catch is a dead NPC whose object is alive again (BUG-0044), besides another species.
* ~ Negative result: DestroyInstance on a fresh corpse of the host and then up to 8
  `Create(reuse)` of its species over 8 frames (or 24 `Create` without reuse of another species in
  the same frame) never returned the freed object (lane 5). When the game lets a freed creature go
  back to its pool slot was not found; `coop.npc reuse <id>` (tests) makes the dead object alive
  again instead.
* ✔ The host's NPC ids (npc.cpp host_tick): an object keeps its id while it is the same species
  and has not come back from death; otherwise the old id is despawned on every guest and the
  object gets a new id and spawn. Ids skip ids in use (`npc_ids.h`, BUG-0023).
* ✔ A creature removed during a cinematic stays in the puppet list until `destroy_deferred_tick`
  destroys it (BUG-0066): seen without that, the host gave the guest's old puppet an NPC id during
  CRG_ClaimNest (`coop_cinematic_puppet.py`). Its position is no sign of AI: the old puppet slid
  0.4 m and 1.4 m with and without the fix (its last velocity).
* ✔ `h_NPCTickAI` (game.cpp) writes a partner's puppet into `cGameNounManager +0x54` for one NPC
  tick. If that tick does not return (a fault the game's handler catches), `game::swap_guard_tick`
  (once per frame, outside every NPC tick) puts the real avatar back and takes the lent +0xE84 off
  the puppet (BUG-0110; `swaps_restored=` in `coop.npc`). Seen without it (a forced leave,
  `coop.npc swapfault`): +0x54 stayed the puppet and `ai_swaps` stopped growing.

## How a save writes the world, and keeping our objects out (2026-10-28, fix/w3-cell)
<a id="creature-save-strip"></a>

GOG 2024 (dc04aee5), static analysis (private Ghidra project, `tools/ghidra/q.sh --build 2024`) and
lane 4 at runtime. BUG-0020. CORRECTION (2026-10-28, fix/w3-cell): `CreateHerd` is 0x00B23920 in
GOG 2024; the 0xB238B0 of section 12 / the 05-re-notes table is the GOG 2016 address.

* ✔ `SaveGame` 0xB29000 is synchronous: the file is complete when it returns (no thread, no job).
  The 1.7-2 s before the "saved" box is the message that asks for the save, handled a frame or more
  later (wrapper 0xB29600 `(name or NULL, bool) ret 8`, from 0xB2A240 and 0xE033A0; 0xB29960 calls
  0xB29000 directly). Order: header and noun counts (0xB21170), `SaveDb_OpenTmp` 0x688F90
  (`<name>.tmp`, a new SerializerDatabase with its own COMSerializer at db+0x1C), the main stream
  (key 0x1897C18): the persistence manager's attributes, the simulator system (0xB5BF20: every
  subsystem's Write, among them the noun manager and cGameBehaviorManager), achievements, the cell
  game (mode 0x1654C00); then `SerializerDatabase::SaveClassObjects` 0x69DEA0 ->
  `COMSerializer::SaveClassObjects` 0x69FB90, commit, rename .tmp (0x6891E0), stars.db when the
  bool is set (0xBB4D40).
* ✔ What is saved: `cGameNounManager::Write` 0xB201D0 writes every noun of its list +0x78 (no filter,
  no flag, no virtual asked). Every reference goes through `COMSerializer::Write` 0x6A05F0
  (thiscall(ser, stream, obj) ret 8): the pointer as the id (0 for null), the noun id, and obj is
  queued (0x6A0110). SaveClassObjects writes every queued object's record (obj vt+0x10) until no new
  one is queued: the save is the closure of what the roots refer to. ~ A record whose Write returns
  false is rewound and dropped, the references to it stay (on load: a default object of that type,
  `COMSerializer::Read` 0x6A06A0).
* ✔ On load a null reference reads as null; `cGameNounManager::Read` 0xB1FBD0 skips null entries of
  the noun list. **cGameBehaviorManager** (vtable 0x0145CB68, name at slot +0x10) keeps the objects
  it updates in a vector at +0x20/+0x24 (our creatures among them); its Read 0xB0E570 keeps null
  entries, and its update 0xB0EA40 (slot +0x38, from the simulator update 0xB5D0F0) calls vt+0x1C on
  each without a check: a null entry faulted at 0xB0EC8C every frame (155 faults in 8 s, lane 4).
* What SporeCoop does (`mods/coop/savestrip.cpp`): a hook on `COMSerializer::Write` writes a null
  reference for our objects: herds without an archetype, a nest or the avatar's flag (the herds made
  for puppets and proxies, section 12) and every creature in them. The set is made once per save
  (a new serializer, or the first reference after 0.5 s) from the live noun list, with each object's
  Cast(0x179C807) pointer too (what WriteObjectPointer 0x69D950 passes); main thread only. A hook on
  the behavior manager's Read drops the null entries after a load. `coop.savestrip [0|1]` (status:
  `last_ours=`, `nulled=`, `behavior_nulls_dropped=`). Measured: a host save with a guest's puppet
  held 2 objects of ours (the puppet and its herd), 9 references were nulled; after a restart and a
  load no creature without tuning and no herd without an archetype, no fault.
* ? Not stripped (a null member in a game herd's member list +0x40/+0x44, whose load is not
  checked): a puppet in the avatar's herd (the fallback when the partner's creation is unknown) and
  a guest's proxies in the game's nest herds (only a guest's own save during a session holds them).
  ? Which other subsystem fields hold a reference to a creature (AI targets): they load as null; no
  fault seen in the test.
## World identity, the cheat console and proxy driving (2026-10-28, fix/w3-creature)
<a id="creature-world-epoch"></a>

* ✔ The avatar's address is no world identity (BUG-0119): the game pools creature objects, so the
  avatar of a reloaded world may have the old address. `game::world_epoch()` counts creature worlds:
  +1 when the creature stage is entered (every load and every received world passes the galaxy
  menu and kLoad; the editors kEdit / kEditor keep it) and when another avatar replaces the avatar
  inside the stage. Code that remembers "this world" keeps the counter with the avatar (creations'
  own-creature swap, the guest's first-second proxy wait, the deferred destroys). Seen with a
  simulated reuse (`coop.creations sameaddr`, `coop_world_reload.py`): without the counter the
  guest kept the host's species after the load.
* ✔ A share of the host's world after a reload: the guest's own creature is applied 3.1 s after
  its avatar appears (the 3 s settle wait of `creations.cpp`), lane 5.
* ✔ Cheat console (GOG 2024): `AppShortcuts_OnKeyDown` 0x814180 opens it on vk 0x43 with modifiers
  exactly 3 (shift + ctrl from GetKeyState at 0x848620), and only when `IWindowManager::Get`
  (0x67CA60, `[0x15FCC4C]`) vt 0x84 returns null; that method returns the manager's +0x82C (a
  window; on the lane games a full-screen window with control id 0xFFFFFFFF, meaning not found).
  It then calls `ICheatManager::Get` (0x67DDE0) vt 0x44 with 1 (0x67E730: calls vt 0x20(arg) of
  every listener in the list at +0x4C). Ctrl+Shift+C does not close it.
* ~ The console's window, seen once (2026-10-28): container control id 0x014A4606, rect
  [0 400 800 600], flags 0x47; a text edit 0x014A4608 (the typed text) and an output area
  0x014A4607. It is not in the UI tree while the console is closed. Injected Ctrl+Shift+C opened it
  once on a lane game and never again (6 tries, also on a fresh game and with +0x82C cleared);
  calling vt 0x44(1) directly did not open it either. Why: not found (BUG-0149).
* ✔ Detours that change game state around a call into the game (NPCTickAI, NoAttack, SelectUI,
  StageMsg, civ building damage) register their undo in a ledger (`swap_ledger.h`); whatever is
  still open once per frame is undone (BUG-0110).
* ~ NPC proxy driving (`drive::step`, BUG-0169): the snap teleport is at 8 m and the stuck
  teleport needs the host's NPC under 0.3 m/s, so a host NPC that pushes against something (a speed
  reported, a fixed position) kept its proxy walking towards a spot it could not reach. NPC proxies
  now also teleport after 3 s more than 2.5 m off without getting 0.5 m closer (then 10 s quiet).
  Inferred from code, not seen in the game.

## The cheat console's enabled flag (2026-10-28, fix/w4-tests)
<a id="creature-cheat-console-flag"></a>

* ✔ Why injected Ctrl+Shift+C did not open the console on the lane games (BUG-0149): the key reaches
  `AppShortcuts_OnKeyDown` 0x814180 with (vk 0x43, modifiers 3), `IWindowManager` +0x82C is null and
  `cCheatManager` vt 0x44 (0x67E730) is called (probes, lane 5), but 0x67E730 returns at once while
  the cheat manager's byte +0x64 is 0. It was 0 on a lane game in the creature fixture; after
  vt 0x4C (0x67E0A0, `func4Ch(1)`, sets +0x64) the same keys opened the console (control 0x014A4606
  in the UI tree, `coop.social keys` text_up=1). Esc closes it (and may open the pause menu).
* ~ Who clears it: only two callers of vt 0x4C after `ICheatManager::Get` exist, 0xDEA3D0 (sets 1)
  and 0xDEB450 (sets 0), in the GalaxyGameEntry UI code (after `GlobalGGEUI::Initialize` 0xDEA1B0).
  Inferred: the menu disables the console and leaving it the normal way enables it again; the
  lanes load their save by command (`coop.load`), so it stays off. Not checked which UI event calls
  which. The earlier note "opened once, never again" (creature-world-epoch) fits: that game had
  passed the menu.
* `coop_social_console.py` sets the flag first (`call App::cCheatManager::func4Ch thiscall <mgr> 1`).

## Empty puppet herds and one list per save (2026-10-28, fix/w5-world)
<a id="creature-save-strip-empty-herds"></a>

BUG-0255. GOG 2024, lane 5. Adds to [the save filter](#creature-save-strip).

* CORRECTION (2026-10-28, fix/w5-world): section 12's "the empty herds stay (harmless: target 0)"
  is superseded. After the partner left (`coop.leave`), the herd made for its puppet stayed with
  `members=0` and went into the save (`coop.npc archless` 1 after the load with
  `coop.fix savestrip.empty_herds off`). Now an own-kind herd (no archetype, nest or avatar flag)
  with no members is stripped too, but only when its member list (+0x40 / +0x44) is readable and
  empty, no creature of the save points at it (`herd_of`, every creature counted, the avatar and
  the pack included) and every creature of the save was readable. The load after it: no fault
  (8 s of simulator updates), the avatar there.
* ✔ `coop.save` in Creature: every Write of a save ran in one frame (`coop.savestrip`
  `max_save_frames=0`); one `coop.save` counted as two serializers (`saves` +2). So the save filter
  starts a new list at a new serializer or a new frame (`savestrip_rule.h`), not after a 0.5 s
  pause; switch `coop.fix savestrip.per_save` (its off side can write a game object at a freed
  puppet's address as null). Not measured: the share save, quit to the menu, a stage change and a
  Space planet save.

## Leaving the stage for the galaxy menu: the avatar in the window (2026-10-29, fix/w6-avatar)
<a id="creature-leave-avatar"></a>

BUG-0264. GOG 2024, lanes 4 and 5.

* ✔ The quit message 0x4715068 (docs/re/space.md 8.2) on a guest in the Creature stage, measured
  per frame (`coop.leaving`, log lines "game: leaving frame ..."): mode_id() reads Creature for
  0.63-0.71 s after the quit is posted; our frame hook runs 3 times in that window (right after the
  post, then twice ~10 ms apart after the game's save that blocks ~0.65 s), then the mode changes.
  In 30+ windows (2 players, also right after the Cell -> Creature change and the host's first
  editor) the avatar stayed in the noun manager's object list with the game's vtables in every one
  of those frames.
* ✔ The campaign's guests (4 players, 20261029-070811 campaign#2) faulted 1.06 s after the post,
  later than any window measured here: Bob in `game::max_health` (combatant vtable at avatar+0x5A8
  read 0x01), Carol and Dave in a call into the heap with the same return addresses on the stack
  (coop+0x8285B after the vcall in max_health, coop+0x88D33 in creature_fill): all three the same
  path, a freed avatar still in the noun manager's field while the mode read Creature.
* ? Why the campaign's window was longer and its avatar freed in it: not reproduced (4 games per
  lane under the load of other lanes; more frames inside the teardown are the guess).
* The fix (`world.leave_avatar`): `game::avatar()` is null from the quit (posted by the mod, or
  handled by `Simulator_HandleMessage` for any source) until the mode changes (30 s at most, cleared
  when a via-menu load is cancelled), and never returns an avatar whose object or combatant vtable
  is outside the exe. `game::avatar_field()` is the raw field for pointer comparisons (the save
  filter runs inside the quit's own save, when avatar() is already null).
* Diagnostic: once a second in a stage the mod checks that the avatar is still listed; a miss logs
  "game: the avatar ... is no longer in the game's object list" (`stage_gone` in `coop.leaving`).

## The game's input lock drops clicks on the co-op window (2026-10-29, fix/w7-vote)
<a id="creature-input-lock-clicks"></a>

* ✔ `Simulator::cGameInputManager::Get` (GOG 2024 0xB3D350, `mov eax,[0x167EAD0]; ret`) +0x110 is
  the input lock: the cinematic start (0xADFEC1) increments it, EndCinematic (0xADFF87) decrements
  it. While it is above 0 the game drops every click on the UI, our co-op window included (the
  window is a child of the main window, always in front, and still gets nothing). A caption's own
  OK button still works. Measured in coop_vote_caption.py: CRG_ClaimNest started by hand on the
  guest, `input_lock=1` in `coop.uistate`; real clicks on "I'm ready" did not reach the button's
  handler (lab 20261029-142824).
* In the four-player campaign the Creature -> Tribe vote started while the guests still had the
  brain-upgrade captions of AddEvolutionPoints up: their clicks were dropped and the vote ran out
  of time (BUG-0273). The first vote of the game (Cell -> Creature) came after the captions and
  ended in 2 s.
* The mod now takes a click from the window's messages (mouse.h, WM_LBUTTONDOWN) while the lock
  is held and presses the visible co-op window button under it after 0.5 s, unless the game
  delivered the click itself meanwhile (`locked_native` in `coop.uistate` counts those; it stayed
  0). Switch `core.ui_locked_clicks`.
* CRG_ClaimNest started with PlayCinematic does not end with Enter on the guest; EndCinematic
  (`call SporeCoop::cCinematicManager_EndCinematic thiscall <mgr>`) ends it and frees the lock.
## NPCs far from the host's avatar go back to the pool (2026-10-29, fix/w7-npc)
<a id="creature-npc-pool-cull"></a>

* ✔ The creature stage keeps NPCs only around the player's own avatar. Walking the host 300 m
  away from the nest (`coop.npc gopos` in 20 steps, lane 4) turned every NPC farther than about
  150-200 m into a pool object: still in the noun list and `object_alive`, species key and health
  kept, herd (+0x1674) 00000000, position (0 0 0), no more cCreatureAnimal::Update calls
  (`coop.npc creatures`). The partners' puppets next to them do not keep them. The cull distance
  was not measured exactly.
* ✔ A nest that misses a member spawns it again near the nest (the same pooled object comes back,
  `Create` with reuse, docs/re/creature.md#creature-pool-ids) and, with the avatar still far, the
  game pools it again 1-3 s later: a cycle of 3-6 s for as long as the avatar stays away.
* ✔ Walking back, the pooled family member returned as the same object with its herd and nest set
  again (flags 0x10) at the nest.
* ✔ For the NPC sync (BUG-0274): a host NPC that did not update in a frame is now checked at once:
  not in the noun list (freed) or listed without a herd or at (0 0 0) (pooled) -> despawned on the
  guests now, not after the 2 s grace. Before, the census caught such an object as
  `own=npc pos=(0.0 0.0 0.0)` with the guest's proxy still on the surface ("504 m apart"; 2 s per
  cull, every few seconds with the nest cycle, so two campaign checks 30 s apart could see it).
* Consequence (BUG-0280, open): a guest far from the host has no NPCs around it.

## The relevance pass and the view centres of all players (2026-10-30, fix/w9-creature)
<a id="creature-view-centres"></a>

BUG-0280. GOG 2024, disassembly plus runtime on lane 9. Explains [the pool cull](#creature-npc-pool-cull).

* ✔ `SporeCoop::Relevance_Tick` 0xAD2300 (thiscall(mgr, mode, int, dt_ms) ret 0xC, every frame,
  only caller 0xAD2520 from vtable 0x145A188) pushes **one** sphere `{centre, enter^2, leave^2}`
  (5 floats, 0x14 bytes) onto the vector `mgr+0x20` (begin) / `+0x24` (end) / `+0x28` (capacity),
  runs the mode's pass and erases the whole vector again. The centre is
  `cViewer_GetView(&out, 0, 0, 0)` of the game mode's viewer (IGameModeManager vt 0x50 -> vt 0x1C):
  the local camera, not the avatar. Radii from 0x1565D5C / 0x1565D60 (100 / 250 in the exe's
  data); **at runtime the leave radius was 150 m** (`coop.views`: "enter 100 m leave 150 m"), so a
  tuning value overrides the static one.
* ✔ `SporeCoop::Relevance_Update` 0xACE4E0 (thiscall(mgr, int, dt_ms) ret 8) is the pass of the
  Creature stage (mode 0x1654C01) and of the Tribe stage (0x1654C02, after 0xACE420 / 0xAD11E0);
  other modes run 0xACF400. It returns at once until 5 s (0x1565CE0) have added up. Then: spawn
  points (noun 0x36BE27E) inside an enter radius spawn their herd; **creatures** of the pool list
  (+0x135 set, +0xB67 set, not the player's tribe) outside the leave radius of every sphere
  (`Relevance_OutsideAll` 0xAC8CC0) get vt 0xC8(1): back to the pool (herd 0, position (0 0 0),
  BUG-0274); herds inside an enter radius (`Relevance_InsideEnter` 0xAC8D30) are enabled (at most 10
  per pass, 0x1565D04), herds outside every leave radius disabled; a herd whose brain range
  [+0x120, +0x124] does not hold the player's brain level (0x169E370) is disabled in the Creature
  stage whatever its distance.
* ✔ Not every creature goes: with the camera ~200-250 m from the guest, 8 of the 20 NPCs within
  120 m of the guest were pooled and 12 stayed (lane 9, fix off); the +0xB67 flag decides, not
  checked which creatures have it.
* ~ In the headless test games the host's camera does not follow `coop.npc gopos` all the way: the
  host's sphere stayed ~370 m behind an avatar teleported 600 m away and did not move for 40 s. A
  test that needs the camera far away has to check the sphere (`coop.views` prints it).
* The fix (`npc.view_centres`, `mods/coop/view_centres.cpp`): the host keeps a list of **the view
  centres of all players** (coop.cpp notes a partner's position whenever its puppet is shown in the
  host's place, any stage; dropped after 3 s, at a leave and at the session end). A hook on
  Relevance_Update lends the vector a buffer of ours for that one call: the game's spheres first,
  then one per partner with the radii of the game's first sphere; the game's pointers come back
  right after the call (the game's allocator never sees our buffer). Bounded cost: one sphere per
  partner (1-3), the game's own radii and its 10-herds-per-pass limit. Only in the Creature stage
  for now (`game::in_creature_stage()` in the hook); the Tribe stage runs the same pass and would
  take the same list (not tested).
* Rivals (docs/11-rivals.md 11.3.2): every player plays its own nest, possibly far from the host.
  The host simulates all of them; this list is what keeps each rival's nest, family, spawns and
  wild herds alive around it. Two things stay to do there: the brain-range filter above uses the
  host's brain level for every herd (a rival with another brain level needs the herds of its own
  range near it), and the sphere of a player is its avatar's position (not its camera).
* `coop.views` (host): the partners' centres, the game's own sphere, how many passes took partners.

## A null herd member after a rejoin: the stray waiting for its destroy (2026-10-30, fix/w9-creature)
<a id="creature-herd-null-member"></a>

BUG-0256. GOG 2024, lane 9, `coop_rejoin_saved_puppet.py`. Adds to [the save filter](#creature-save-strip-empty-herds).

* ✔ The fault 0x00C0B8D2 is `SporeCoop::Creature_IsAdult` 0xC0B8D0 (`[ecx+0xB34] == 1`, no null
  check) called from `Herd_Update`+0x61 (0xC6D9C1) with a **null member** of the herd's list
  (+0x40 / +0x44). The crash reporter's label "cCreatureAnimal::Update+0xAA2" was the nearest
  older symbol. Destroying a creature (DestroyInstance -> 0xC083D0) takes it out of its herd's list
  (0xACC940 erases it), so a null there does not come from a destroy in this world: it was loaded.
* ✔ The chain (host log of the run): the guest leaves during the vote while the host is in its
  evolution editor; the host's puppet of it is forgotten, not destroyed (the world stays through
  the editor, coop.cpp `forget_puppets`); back from the editor the host removes it as a left-over
  puppet ("a puppet left over from before") while the evolution cinematic plays, so the destroy is
  **deferred** (`game::destroy_creature`); the guest joins again, its new puppet goes into the
  **same herd** (herds are reused per species), and the share save is written in that window. The
  save filter stripped the new puppet (a null reference in the herd's list) but kept the herd for
  the left-over creature (no longer a puppet, not ours by the owner rule): the guest loaded a herd
  with a null member and faulted every frame (1229 faults in 20 s, fix off).
* Fix 1 (`savestrip.pending`): a creature waiting for its deferred destroy counts as ours in the
  save filter (stripped with its herd when nothing else of the game is in it). With it alone the
  guest's herds had no null member (`coop.npc herdnulls` removed=0, lane 9).
* Fix 2 (`npc.herd_nulls`): a hook on `Herd_Update` takes null members out of the list before the
  update (moving the others down changes no reference count). It covers saves already written by
  older versions and any other way a null gets there; the log line "herd ... null member(s) taken
  out" names the herd (archetype 0, no nest: one of ours in the runs).
* Not checked: whether other lists of the game hold a stripped creature as a null (the behavior
  manager's has its own cleanup, savestrip `h_BhvRead`).

## The view centres in the Tribe stage (2026-10-30, fix/w9-creature)
<a id="creature-view-centres-tribe"></a>

BUG-0280 (Tribe), BUG-0291. GOG 2024, lane 9, `coop_tribe_far.py`. Extends [the view centres](#creature-view-centres).

* ✔ The Tribe stage (0x1654C02) runs the same `Relevance_Update` pass. With the hook gated to
  Creature and Tribe (`npc.view_centres_tribe`), a guest's view centre 332 m from the host's camera
  kept 3 herds of the game enabled with 18 animals near their homes for 40 s; without it the host's
  game had none there (see the ticket for the runs).
* ✔ A Tribe partner's view centre is its **pointer** (modes.cpp `rts_fill`: the point of the planet
  under its mouse, cast from its own camera; it stays where it was while the mouse is off the
  planet). The mod has no other Tribe position of a guest (no chief, no camera target is sent); the
  pointer lies on the guest's screen, so it is inside its view. Tests pin it with
  `coop.npc marker x y z` (`coop.npc marker off` lets the mouse drive it again).
* ✔ Radii: the exe image has enter 100 (0x1565D5C = 42C80000) and leave 250 (0x1565D60 = 437A0000),
  but in a running game 0x1565D60 reads 43160000 = **150 m** (`mem.read 0x1565D5C 8` on lane 9,
  Creature and Tribe): something writes it at startup (a tuning value, not looked up). In Tribe the
  first sphere of the vector had enter 75 m / leave 112 m (and the vector held 2 spheres at times):
  the Tribe path pushes a sphere of its own before Relevance_Tick's (not looked up).
* ✔ BUG-0291 (npc.cpp, `npc.rts_views`): the host sent a Tribe guest only the NPCs within 250 m
  (`kRtsRelevance`) of the player tribe. Now also those within 110 m (`kRelevance`, as in Creature)
  of that guest's own view centre (`views::of`), with the same 20 m hysteresis. The guest's update
  filter still removes its own animals everywhere: what it shows far away is what the host sends.
* `coop.npc archless all` (any stage): every herd with its enabled flag, members, members in the
  world and members within 100 m of the home.
* Rivals: the same two pieces (spheres and the send set) are what a rival's area needs in Tribe.

## Proxies at low frame rates, missing proxies, the host's NPCs after it left (2026-10-30, fix/w13-creature)
<a id="creature-proxy-guest-side"></a>

BUG-0281, BUG-0300, BUG-0180. GOG 2024, lanes 4 and 5. Extends [the relevance pass](#creature-view-centres).

* ✔ A guest's NPC proxies are never sent to the pool by the guest's own relevance pass: the pass
  pools only creatures with `+0x135` and `+0xB67` set (0xACE7E4 / 0xACE7F0 in Relevance_Update);
  every proxy read `+0xB67` = 0 (21 of 21, `mem.read`, lane 4), every host NPC 1. With the guest's
  camera 305 m from its proxies for 45 s none was pooled and the census matched the host's.
  (Not the cause of BUG-0300.)
* ✔ The pool call itself: creature vt 0xC8, `thiscall(c, 1)` (`push 1` at 0xACE88C, ecx = the
  creature).
* ✔ Creature-stage proxies of the nest species 065CBE70 hardly walk by themselves: `own=` (their
  game speed) stayed 0 for most samples and reached 1-3.3 m/s at best while the host's NPC ran at
  4 m/s (`coop.npc list`); the per-frame nudge (driver.h, 15 % of the way, about half of the nudges
  undone by the next frame: `nudge=kept/all`) and the `npc.trail` teleports move them. Two NPCs
  running side by side at 4 m/s (`DASH_FIXTURE=creature coop_tribe_npc_dash.py`, guest at 15 fps):
  p90 6.2 m off the host's path with `npc.trail` off, 3.1-3.4 m with it (Tribe: 0.7-0.95 m). A
  nudge scaled by the frame time (the same pull per second at 15 as at 60 fps) was tried and gave
  3.4 m: not the cause, dropped. Not found why these proxies do not run at the host's speed.
* ✔ A proxy beside a standing host NPC is not pushed off by the guest's own avatar: guest avatar
  0.5 / 1.0 / 1.5 / 2.5 m beside three standing NPCs, 6 s each, guest at 15 fps: errors 0.0-0.5 m
  (only NPCs that walked off meanwhile showed 1-2.5 m for a moment).
* ✔ The host leaving to its main menu: its world change despawns every NPC at the guests a few
  seconds before the guest learns `host_away` (so the proxies are gone when the guest's own game
  takes over); the nests then bring their members back one at a time (0 own NPCs within 70 m for
  25 s, `coop_away_refill.py`). Fix `npc.away_locals` (BUG-0180):
  the guest keeps the last non-empty set of proxies it showed (species, place, orientation, only
  those in a nest herd of its game; every 0.5 s) and, when the host is away (within 15 s of that
  set), creates a creature of the game for each with `cCreatureAnimal::Create` in the nest herd of
  that species, as the nest does: 20 made, 11 of 11 within 70 m at once.
* ~ BUG-0300 (one proxy missing on a guest for 30 s and more, 4-player campaign): the guest's trace
  counted 20 proxies with states while its census listed 19 creatures as proxies, so the proxy was
  in its list but its object was no creature of the world (not listed, destroyed, or without a
  species); not a range edge (three members of the same herd 77-84 m away were there; the host sends
  within 110 m). Not reproduced in 4 rejoins with 2 players. Diagnostics `npc.proxy_listed`: the
  guest checks every second (Creature stage) that each proxy's object is a creature of the noun list (logs
  "is no creature of this world any more" and makes it again) and logs a proxy without a species.
* ✔ BUG-0118 re-read on the lane 4 exe (2026-10-30): CreatureSelectionUI 0xB6C9C3..0xB6CA11 reads
  `(+0xE84)->+0x32C` only with `+0xB58` mask 0x2 clear and mask 0x1 set; the guard (mask 0x1) covers it.

## Proxies of running NPCs: why they did not walk (2026-10-30, fix/w14-creature)
<a id="creature-kinematic-proxies"></a>

BUG-0281. GOG 2024, lane 5. Corrects [the proxy section](#creature-proxy-guest-side) ("not found why").

* ✔ A guest proxy's locomotion works: one `cLocomotiveObject::MoveTo` (creature spatial vt 0xE0)
  12 m ahead made the 065CBE70 proxy run at 4 m/s at once (`coop.npc pwalk`, test command).
* ✔ The same MoveTo given again every 0.1 s (drive::step while the target moves on) **restarts the
  walk**: the proxy stood, stepped back and crept at 1-2 m/s. Writing its position to itself or its
  orientation every frame did not slow it.
* ✔ While it walks, `set_position` (vt 0x38) towards the path is undone by the locomotion (14-76 of
  519-624 nudges kept); `teleport` (vt 0x44) puts it exactly there.
* ✔ Fix `npc.kinematic` (Creature only): while the host's NPC moves the proxy is teleported onto the
  interpolated host position every frame and gets the host's velocity (`+0xC0 + 0x1C8`); stills of
  the guest show running poses. p90 off the host's path at 15 fps 0.31-0.34 m (before 1.84 m with
  BUG-0175, 3.3-3.5 m without). Standing NPCs keep the driving.

## The pack's rescue cooldown across sessions (2026-10-30, fix/w17-creature)
<a id="creature-rescue-reset"></a>

BUG-0330. Mod logic, not game RE.

* ✔ The rescue cooldown (`teamplay::g_last_rescue`, 60 s) lived in the guest's process across a leave
  and join: the next session had no rescue for up to a minute (every warm `team` run, 4 of 4).
  `team.rescue_reset`: `teamplay::reset()` (session end, world change) makes the rescue ready.
* ✔ `coop.npc hit <id> <dmg>` on a guest (TakeDamage on the proxy with the guest's avatar as the
  attacker, no push) does not move the guest's avatar (proxies 14-37 m away, lane 4).

## Running proxies and the NPC's state (2026-10-31, fix/w18-creature)
<a id="creature-kinematic-sync"></a>

BUG-0334. Corrects [the kinematic proxies](#creature-kinematic-proxies).

* CORRECTION (2026-10-31, fix/w18-creature): the first `npc.kinematic` (BUG-0281) placed a running
  NPC's proxy and went on to the next proxy, so the code after the driving (health, the death replay,
  the impressed flag) did not run while the host's NPC moved. Observed: a fleeing NPC after a joint
  hit kept its old health on the guest (`team`: 4.2 / host 1.95, smoke 20261030-235253 and
  20261030-231732 #2); in `npc_run_hp` (a dashing NPC hit, then killed): health 6.0 / host 4.0 and no
  death on the guest within 3 s with `npc.kinematic_sync` off. Fixed: only the driving is skipped.

## NPCs updated at the planet's centre (2026-10-31, fix/w21-creature)
<a id="creature-npc-at-centre"></a>

BUG-0339. Extends [the pool cull](#creature-npc-pool-cull).

* ✔ The game's pool call on a host NPC (creature vt 0xC8, `thiscall(c, 1)`, test command `coop.npc
  pool <id>`) moves it to (0 0 0) with herd 0, and it is no longer updated. The host retires it
  (BUG-0274) and the guest drops its proxy within 1 s (lane 5).
* ~ In a 4-player campaign (lab 20261031-083425 campaign#2) four host NPCs of one herd sat at (0 0 0)
  in every census for 30 s and more and were still updated (the sync-check lists only NPCs updated in
  that frame). The cause was not found. A guest kept their proxies on the surface 511 m away.
  The host now logs "is updated at the planet's centre" for such an NPC.

## The creature ability bar and the own creature swap (2026-10-31, fix/w27-creature-fault)
<a id="creature-ability-bar"></a>

* ✔ The HUD's ability bar is a table at `[SporeCoop::AbilityBar_ptr]` (0x169E3EC, 0xD8 bytes,
  made by 0xD489B0 on the stage start, freed by 0xD48A90): 25 slots of `{int ability index (-1
  empty), u8}` at `row * 0x28 + col * 8`, `+0xC8` rows (5), `+0xCC` slots per row (creature 5,
  tribe 3), `+0xD0` / `+0xD4` the first button window ids. The row is the ability's `+0xC`
  (row type), the column comes from 0xD48FA0 (by the ability's type `+8`).
* ✔ `SporeCoop::AbilityBar_Rebuild` 0xD49400 (cdecl, no args, no null check of the table) clears
  the 25 slots and puts each ability index of the avatar (0xD1E610: the creature/captain avatar,
  the tribe's chief) into its slot, then refreshes the row buttons. Callers: the creature HUD
  refresh 0xD2C000 (stage start), the message handler 0xB5F7B0. **`cCreatureAnimal::SetSpecies`
  does not call it**: an avatar that gets another species keeps the old bar.
* ✔ `SporeCoop::AbilityBar_Ability` 0xD1E830 (cdecl(slot index) -> ability*): an index below the
  avatar's vt 0xB0 count is the avatar's vt 0xB4(index); a higher one in creature / captain mode
  reads `cSpeciesManager::Get()->GetSpeciesProfile(sm + 0xA4)->[+0x6D4][index]` without a bound
  check. `sm + 0xA4` was the loaded world's player species (065CBE70 on the creature fixture, not
  the guest's own creature).
* ✔ Every key without a modifier in the creature stage: OnKeyDown 0xD3AFF0 (thiscall(strategy, vk,
  modifiers)) -> `AbilityBar_OnKey` 0xD4A450(vk) -> `AbilityBar_FindByKey` 0xD4A320(GetAbilityMode,
  vk, 1), which walks every non-empty slot of the rows of the current stance through 0xD1E830 and
  reads the ability's `+0xC` / `+0x38` (key) before comparing keys. A slot past the avatar's
  abilities makes that a read past the species' ability array: garbage there faulted at 0xD4A397
  (`mov eax,[esi+0xC]`, esi = 0x41900000 = float 18.0) once per key press (BUG-0358). Zero there is
  skipped, a valid pointer is another species' ability: so the fault depends on the heap.
* ✔ Measured on lane 6 (creature fixture, the guest's own creatures): world species 065CBE70 has 11
  abilities (host bar 5 slots, highest index 7); the guest's own creatures 9, 13, 17, 16, 16 (bars
  of 3 / 7 / 11 / 10 / 10 slots, highest 5 / 9 / 13 / 12 / 12). The bar built for the 17-ability
  creature and then a swap to the 9-ability one (fix off): 11 slots, 5 past the abilities. On the
  fixture alone (world bar highest index 7, smallest own creature 9) no slot is past the abilities
  but the slots map keys to other abilities: why the creature tests never faulted, and why the
  campaign (a world species evolved in the session, with more abilities) did.
* Co-op (creations.cpp `bar_tick`, `coop.fix creations.ability_bar`): the bar is built again (outside
  the frame) when the avatar's abilities change under the same avatar (our SetSpecies) or a slot
  points past them. `coop.creations bar [rebuild]` prints the bar (`abilities= used= stale=
  max_index= slots=row.col:index,...`) or has the game build it for the current avatar.

## The partner's puppet: walk gait, teleport vs SetPosition (2026-11-01, fix/w29-puppet-move)
<a id="creature-puppet-walk"></a>

BUG-0360. GOG 2024, lanes 2-4. Measured with `coop.track` (every frame of the observer's game) and
`coop_puppet_walk.py` (the walker walks by WASD and clicks). Corrects [the kinematic
proxies](#creature-kinematic-proxies) on the animation.

* ✔ Where to measure: the mod's frame runs at Present, after the game's update and drawing. A
  position read after the mod moved the creature is the mod's own write, not what was drawn; the
  drawn one is the position at the start of the mod's frame (`drive::State::drawn`). The first
  measurements of this branch read after the write and showed SetPosition "on the path" (0.00 m)
  while the drawn creature trailed 0.5-1.9 m (p90 up to 5 m).
* ✔ `Anim::AnimatedCreature` (creature +0xB54): +0x04 is the model's position (SetPosition writes it
  too). vt 0x58 (GOG 2016 0xA05030, vtable 0x1449640, ret 0x10) `GetCurrentAnimation(u32* id,
  float*, int*, int*)` writes the current main animation id.
* ✔ The walk gait: `+0x17C` (anim cid) `+0x15B8` holds two ints, the first 0 when no walk gait runs,
  1-3 while it walks (a walking avatar: > 0 in 94-97 % of its moving frames, 0 standing).
  `Creature_ChooseIdleAnim` 0xC0C790 picks idle_walk_lookat (0x4866D8E / 0x4866DAB) from it and the
  speed state +0xF98 (2 walking, 3 running, 0 standing); otherwise the plain idle 0x2481DE5. The main
  animation id alone is no walk measure for puppets (a puppet can keep 0x4866DAB standing).
* ✔ A creature behind the observer's camera is animated rarely: the gait value of a puppet 8-14 m
  behind the camera stayed 0 for 18 s of walking and turned on when it stood. Judge the gait only on
  frames where the creature is on the screen (`H->world_to_screen`).
* ✔ The locomotion keeps its own position: SetPosition (creature spatial vt 0x38, 0xC0DEA0: the model
  and the animation's position) is undone by the next update while the creature walks (the drawn
  puppet trailed as if not placed), and StopMovement (vt 0xEC, SetDesiredSpeed(0, 1)) after such a
  walk put it back up to 3 m in one frame. Teleport (vt 0x44, 0xC0C040: SetPosition, SetOrientation,
  message 0x17F243B, then +0xB5D = 1) moves the locomotion too.
* ✔ +0xB5D (set by teleport) restarts the animation's walk gait: a creature teleported every frame has
  gait 0 in 93-100 % of its moving frames (it slides). Teleport and then +0xB5D = 0
  (`game::place`) keeps the gait: 99-100 %.
* ✔ The fix (`creature.puppet_walk`): while the partner moves, the game walks the puppet (its speed by
  SetDesiredSpeed, a MoveTo 12 m ahead given again only when the direction turns > ~6 degrees, at most
  every 0.3 s, at least every 1.5 s; given every 0.1 s a MoveTo restarts the walk, BUG-0281) and
  `game::place` puts it on the interpolated path with the partner's facing every frame. Drawn error
  p50 0.02 m, p90 0.04-0.07 m; 0 % standing 0.1 s windows; gait 99-100 % on screen. The old driving
  (MoveTo every 0.1 s + SetPosition pulls + 4 m snap): standing in 8-62 % of the windows, drawn error
  p50 1.3-1.9 m, snaps of 3.5-4 m.
* ~ Clearing +0xB5D right after the teleport: no other reader of the byte was found by a byte search
  (only the constructor writes it); side effects not known.
* ~ A running NPC's proxy (`npc.kinematic`, teleported every frame) is expected to slide without a
  walk gait for the same reason (inferred from the puppet measurement, BUG-0361).

## NPC proxies walking on the host's path (2026-11-01, fix/w30-npc-walk)
<a id="creature-npc-walk"></a>

BUG-0361. GOG 2024, lanes 4-5. Measured with `coop.npctrack` (every frame, every proxy within 40 m of
the guest's avatar, 150 m of its tribe in Tribe; the host's own NPCs as the reference) and
`coop_npc_walk.py` (the host's game walks NPCs around the guest with MoveTo at 2-6 m/s, then hits two;
the creature fixture's NPCs reach 4-4.5 m/s, the tribe fixture's animals 6.5-7.5 m/s).

* ✔ The proxies of moving host NPCs slid: `npc.kinematic` (BUG-0281) teleported them (vt 0x44, with the
  mark +0xB5D) onto the path every frame. On the path (0 % standing windows, drawn error p50 0.00-0.01 m)
  but the walk gait was on in 4-6 % of their moving frames on the guest's screen and the main animation
  stayed the plain idle 0x2481DE5. The host's own NPCs at the same time: gait 94-99 % on its screen.
  This confirms the inference of [the puppet walk](#creature-puppet-walk) for proxies.
* ✔ Which part matters (variants, `coop.npctrack variant N`): MoveTo ahead + speed but teleport with
  the mark: gait 6 %; placed without the mark, no MoveTo (the velocity field written): 68 %; MoveTo
  ahead + speed + `game::place`: 99-100 % (walk animations 0x4866DAB / 0x4866D8E).
* ✔ The fix (`npc.walk`, `drive::walk_place` in driver.h, the method of `creature.puppet_walk`): gait
  96-100 %, 0.6-1.4x the path's speed in 94-99 % of the 0.1 s windows, no jumps; also at 15 and 30 fps
  and over `SPORE_NET=mobile`.
* ✔ A host NPC thrown back by a hit (9.4 m/s, slowing down for 1.5 s): placed without the mark, the
  proxy's own locomotion moved it back each frame by as much as it was placed (it stood, 4.5 m off at
  the end). `walk_place` notices a growing error (0.75 m, 0.15 s) or a lost place and then moves it
  by teleports with the mark at the catch-up speed until it is on the path.
* ✔ At a stop the interpolated target can jump back up to 1 m in one frame (the extrapolation past the
  newest state corrected by the stop state); the last 0.1 s of a walk is interpolated towards a state
  with velocity 0. A walked proxy goes on with the path's speed, capped at the speed it walked.
* ✔ Tribe: the animals' proxies were driven by `drive::step` (no npc.kinematic there): walk gait
  98 %, but 0.6-1.4x the path in only 72-77 % of the windows, 4-7 % standing, drawn error p50
  0.27-0.30 m, steps of 0.8-2.3 m. With `npc.walk_tribe` (walk_place at the guest's own ground height,
  BUG-0329): 85-93 %, 1-3 % standing, error p50 0.01-0.02 m.
* ✔ A host NPC that reports its speed but moves slower (herd mates pushing, turning): walked at the
  reported speed the proxy runs ahead between two placings; it is walked at the path's speed
  (smoothed over 0.2 s, at least 0.5 m/s).
* ✔ The host's NPC itself jumping (creature_npc_dash's mate put 4.8 m to the side at the dash's start):
  closed at the catch-up speed the proxy stayed 2-7 m off for 2 s; `walk_place` teleports (with the
  mark) when it is more than 2.5 m off. Slowing the placing to the path's speed (only the walk should be)
  made it trail at every start of the dash.
* ✔ At 15 fps the drawn proxy is one locomotion step (speed x frame time, 0.26 m at 4 m/s) off its
  target: p50 0.25 m, p90 0.4 m (creature_npc_dash's NPCs, `coop.npctrack`).
* ~ Tribe herds 50-120 m from the tribe: the game itself moved a proxy 1.3 m in one 31 ms frame
  (collision with herd mates, inferred); off the screen the game updates far creatures in bursts
  (gv 0 for 0.6 s, then 1.2 m at once).
* ? One lab run (1 of 9 with the fix, Creature) had gait 0 on every walked proxy for the whole run
  (anim 0x2481DE5, +0x137 = 1, speed state 2) and drawn error p50 0.18 m. Its guest ended the menu
  intro in the stage (`world: cinematic 8915F9BB still playing in a stage`), but playing GGE_Intro on
  the guest by hand (also while its proxies were made again) did not reproduce it. Cause unknown; the
  test prints the walked proxies' (anim, gait, +0x137, +0xF98) counts now.
* ✔ (2026-11-01, fix/w30-tribe-walk) The walk branch of creature_drive is `drive::walk_place` /
  `drive::walk_end` in driver.h now, shared with the Tribe mirrors
  ([tribe.md#tribe-mirror-walk](tribe.md#tribe-mirror-walk), BUG-0365); the Creature puppet's driving is
  unchanged (the same constants, puppet_walk passes). After the merge with BUG-0366 the ground
  handling (`creature.puppet_ground`) is `drive::on_own_ground` in driver.h and `WalkPlace::ground` for
  other drivings; creature_drive applies it to the target before it chooses walk or stand, as before.

## Max hit points, the brain level and the players' health (2026-11-01, fix/w30-creature-hp)
<a id="creature-max-hit-points"></a>

GOG 2024 (dc04aee5), Ghidra 2026-11-01 and runtime on lane 9 (BUG-0373, BUG-0374, BUG-0375).

* ✔ `GetMaxHitPoints` = combatant vt 0x58 of the creature combatant vtable 0x1469E30 =
  **0xC05D50** (`SporeCoop::cCreatureAnimal_GetMaxHitPoints`, thiscall on creature+0x5A8, float in
  st0): `BaseMaxHitPoints` x (avatar bit `+0xB58 & 0x200` ? [0x1582E80] = 1.0 : 1) x (age `+0xB34`
  == 1 ? (`+0xB58 & 1` ? [0x1582F50] = 1.25 : 1) : [0x150C888] = 0.5, the hatchling's half).
  Adventures: rounded up times a scenario factor.
* ✔ `BaseMaxHitPoints` = creature vt 0x7C (vtable 0x146A080) = **0xC04170**
  (`SporeCoop::cCreatureAnimal_BaseMaxHitPoints`): for a creature of **the world's player species**
  (`+0xB20` == `cSpeciesManager_PlayerSpecies` 0x4DF490, the profile of the key at mgr +0xA4):
  `CreatureLevelHealth(mpAvatar vt 0xD8())` + species health `+0x56C`. vt 0xD8 (0xC02A60) is the
  brain level `[0x169E370]` for the avatar or a pack member, 0 for others. `CreatureLevelHealth`
  0x4D1310 reads floats 0x150C890 + 4 x level (tuning, written by 0x4D24B0): **10 15 25 40 100 100
  100** for levels 0..6 at runtime. Any other species: herd `+0x1674` -> `+0x154` if > 0, else tuning
  `+0xE84` -> `+0x340`, else (both ~0) `LevelHealth(0)` + species health, i.e. never the brain level.
* ✔ The report's numbers follow: the host's creature (species health 0, later 4) had 10 (level 0),
  15 (level 1), 19 (4 after an editor), 29 (level 2), 44 (level 3). The guest played its own creature
  (another species in its own game: its `+0xB20` is its creation, the player species stays the
  host's world species) and kept 10 + 4 = 14 at every level. Lane 9: the guest's Kirgle (species
  health 6) has 16 at level 0 and still 16 from the game at level 1 (21 with the fix); the host 10 -> 15.
* ✔ A puppet's max is computed the same way on the observer's machine (its species is the partner's
  creation, "other species": level 0 + species health), and mods/coop/modes.cpp pinned its health to
  that max every frame: the partner looked full and never grew. creature_hp.cpp now keeps the
  partner's reported health and answers the partner's max from GetMaxHitPoints for puppets
  (`creature.partner_hp`), and gives the own avatar of another species the player species' rule
  (`creature.own_brain_hp`). The brain-up caption heals to the new max on both.
* ✔ A won brain level is the same on both machines (the DNA total cPlayer +0x10F0 is synced, each
  game's NestTick raises `[0x169E370]` and plays CRG_BrainUpgradeN; the caption waits for Enter).
* ✔ Retaliation of the host's pack: a guest's hit on a host pack member (`+0xB58 & 0x100`) is applied
  with the guest's puppet as attacker; the victim's AI gets stimulus 0x400 with the puppet as sender
  and attacks the puppet for good (lane 9: a hit of 0.5, then a hit on the guest every ~1.5 s for
  minutes, `damage_in` 9 -> 64), every hit forwarded as MSG_DAMAGE. Not triggered: the pack does
  not attack a puppet on its own (30 s next to it), nor when the host's avatar "attacks" the puppet
  (`ExecuteAction(0xD335362C, {avatar, puppet})`, 3 times): no stimulus, no damage.
  `npc.party_peace`: the host drops a partner's hit on its pack or family and never forwards their
  hits on a puppet.
* Test helpers: `coop.hp` (own hp/max, level, species vs player species, the game's base without the
  fix; each puppet's game hp/max and the reported values), `coop.hp pack` (pack members, stimuli
  naming a partner's puppet), `coop.hp attack <player>` (host: the avatar's attack message on a
  puppet).
## The puppet on this game's ground: falls walking downhill, under the ground (2026-11-01, fix/w30-puppet-jump)
<a id="creature-puppet-ground"></a>

BUG-0366, BUG-0367, BUG-0376. GOG 2024, lanes 6-7, `coop.track` rows with the heights over the
observer's ground (`hd` drawn, `ht` target: |pos| - `cPlanetModel::GetHeightAt`).
* ✔ `+0x137` (`coop.track` column `moving`) is 1 while the creature is grounded on the terrain, 0
  while it falls or stands on an object (a rock: 100 % of the frames on the rocky hill near the
  fixture's spawn, own avatar and puppet alike). While it is 0 the game plays the fall animation
  **0x0692E6A9** (the walker's own avatar after `coop.abilities lift 1.5`: 35 % of its frames).
* ✔ A grounded creature (+0x137 = 1) placed over the terrain is put back on it by the game in the
  same frame (a puppet placed 1.5 m up: drawn 0.00 m); placed under the terrain it is **not** lifted:
  the 0.7.3.3 puppet driven 1 m under the ground (`coop.track lift -1`) was drawn at -0.64 to -1.00 m
  in 1279 frames, walking and standing.
* ✔ Placed exactly on the ground every frame (0.7.3.3 `game::place` onto the partner's path), a puppet
  walking DOWNHILL is 2-3 cm over the ground after the game's next step (the walk goes along, not
  down): +0x137 = 0 for 1-5 frames and the fall animation, again and again (4-7 times in 40 s on a
  slope, radial speed of the path -0.3 to -1.9 m/s; the walker's own creature 0). On flat ground 0-2.
  The 0.7.3.2 driving (no placing) 0-1.
* ✔ Put 5 cm INTO the ground instead (`creature.puppet_ground`, kSink), the game sets the puppet on
  the terrain (drawn -0.03 to 0.00 m) and it stays grounded: 0 falls in 4 runs on the slope. 15 cm:
  drawn up to 0.13 m under at times.
* ✔ The ground is the same on host and guest in the Creature stage: 283 points around both players
  after a live Cell -> Creature (the players' way in) and on the creature fixture: 0.00 m difference
  (unlike Tribe villages, [tribe-ground-height](tribe.md#tribe-ground-height)).
* ✔ The 0.7.3.3 walk with the guest at 30 fps (15 states a second) over a home connection (netem
  `player`: 45 ms, spikes +250 ms, 30 % loss bursts): puppet on its path (93-98 % matched windows, at
  most one 0.8 m step), no vertical bounce. The 0.7.3.2 driving under the same: 4 m snaps 5 times in
  40 s, 20 % standing windows.
* ✔ `GetHeightAt` is the terrain only: a creature on the rocky hill at (-205, -447, -118) of the
  fixture reads 1-16 m over it.
* ✔ The fixture's creatures cannot jump: Space and every ability (`coop.abilities play <i>`, types
  0xE 0x1D 0x28 0x10 0x25 0x1E 0x31-0x33, 0x11 0xD) leave the height at 0. A jump is simulated by
  `coop.abilities lift 1.5` (the fall half of a jump, ~0.5 s).
* ✔ BUG-0376: when the partner leaves the ground (lifted 1.5 m, 24 hops), the 0.7.3.3 puppet stays on
  the ground in 21-22 of them (grounded, the game puts it down) and falls with the partner in 2-3;
  the 0.7.3.2 driving teleports it up in 5.
* ? Why the players' puppet went under the ground: inferred, an extrapolated state (0.25 s at most)
  of a partner walking or running downhill when its states stall (rtt up to 1.1 s in their log), or
  any target under the terrain: the game never lifted it.
* ~ NPC proxies placed every frame (`npc.kinematic`, `drive::step` snaps) probably fall downhill the
  same way (not measured).

## Deep water: the caption cinematic holds the avatar, a guest holds the puppet on the water (2026-11-01, fix/w31-creature-gate)
<a id="creature-deep-water"></a>

Observed on lane 6 (GOG 2024, creature fixture Scalinus, BUG-0377):
* ✔ The fixture planet's water surface is at radius 499.2 m (the start ground ~505 m; 450 m away in
  some directions the sea floor is 484-497 m). `coop.abilities ground x y z` gives the terrain radius
  (cPlanetModel::GetHeightAt) in any direction, also far from the camera; it is the floor under water.
* ✔ An avatar over deep water starts the caption cinematic 5E205931 ("Dangerous creatures lurk in the
  deep. Don't stray too far from shore!"; `coop.status`: `cinematic 5E205931 playing`, trace
  `input_lock=1`). While it plays the game does not move the avatar: its velocity stays frozen and a
  teleport leaves it where it was put, also in the air.
* ✔ A puppet (any creature of ours) above the water there is put on the water surface by the game's
  update every frame (drawn at 499.2 m whatever the mod writes); one put under the surface stays
  there. coop.sync / sync-check read it between frames, so they see where the game draws it.
* ? Whether the cinematic would end on its own (with the sea monster) was not waited for: the test
  teleported the avatar every 1.2 s.

## Shared DNA and its causes (2026-11-01, fix/w31-creature-gate)
<a id="creature-dna-causes"></a>

* ✔ The game's DNA gains all pass `cCreatureGameData::AddEvolutionPoints` (hooked, game.cpp h_AddEvo
  -> coop.cpp on_dna); `game::dna_add_raw` calls the original and is not seen by the listener.
* ✔ A guest's lethal hit on a host NPC is applied as the host avatar's (npc.cpp kill credit); the
  game's DNA for it arrived within 3 s (coop_team.py, coop_dna_notice.py: 0 -> 8.8 after the kill).
  The ledger counts the host's gains within 2.5 s of a credit as that guest's hunt (~ inferred
  window: a host kill in the same 2.5 s is counted as the guest's).
* MSG_DNA (host -> guests) has an optional tail since 0.8: u8 n, then n x {u8 cause (1 host, 2 hunt,
  3 earned, 4 owed), u8 player, f32 amount}: the gains since the last total. Older guests read the
  f32 only. Every machine logs `dna: <a> -> <b> cause=<cause>:<player><+amount>[,...]` (`unknown`: an
  editor's spending, a catch-up, an older host); Cell: `dna: food <a> -> <b> cause=meal:<player>+N`
  (255 on a guest: the host's total, some partner's meal). mods/coop/dna_ledger.h.

## Eggs at the nests (2026-11-01, fix/w32-nest-eggs)
<a id="creature-nest-eggs"></a>

BUG-0384 (player report: "in enemy nests eggs are desynced and bugged (flying mid air)"). GOG 2024,
Ghidra (out/ghidra-2024) and lane 9/10 at runtime (`coop.nest`, creature fixture).

* ✔ Nouns (RegisterNounType calls in 0xB24720): **cEgg 0x2A034CD** (factory 0xB1EEA0, size 0x228,
  ctor 0xC6A410, vt 0x20 0xC6A500), **cNest 0x52AA6122** (size 0x130), **cFruit 0x2C9CC91**, cFruitGroup
  0x2E96892. CORRECTION (2026-11-01, fix/w32-nest-eggs) of the BUG-0384 note on fix/w30-npc-walk ("noun
  0x2C9CC91 ... most likely the eggs"): 0x2C9CC91 is cFruit (`RegisterNounType(0x2C9CC91, 0x2C9CC8E,
  "cFruit")`, vt 0x20 0xAFF290), a pool of fruit objects of the flora (made by 0xB00180 from the flora
  update 0xB0B0B0, model from the planet's property 0x463CDA3): fallen and hanging fruit, not eggs. Their
  host / guest counts differ (each game drops its own fruit); the ones 2-4.6 m over the terrain were at
  the same places on both games (hanging in trees, ~ inferred, not seen on a screenshot).
* ✔ cEgg fields (attribute table 0x134CF40): +0x1E8 mState, +0x1EC "changed" (the view replays its
  effect), +0x1F0 mTimeToHatch (ms), +0x1F4 mpHerd (ref-counted), +0x1F8 mBrokenTimer, +0x218 mpToHatch
  (a creature that comes out of it), +0x220 mEffectScale; spatial subobject +0x34 (position +0x38).
  States: 0 in the player's nest before the editor (cHerd::SetEggsInNest 0xC6EFE0), 5 the avatar's
  family eggs after a hatch (0xC6D290), 6 laid by an NPC, 3 hatched, 4 eaten / broken.
* ✔ cHerd fields (attribute table 0x134D380, all names): +0x40 mHerd (members), **+0x54 mEggs**
  (vector of cEgg*), +0x84 mOwnedByAvatar, +0xF4 mTargetHerdSize, +0xFC mbEggStatusChanged, +0x11C
  mRespawnRate (ms, 240000 at the fixture's nests), +0x12C mEggIndex (the egg's look, 0..9), +0x160
  mpNest, +0x164 mpHerdMom, **+0x194 mpEggLayer**, +0x198 mbEggsInNest, +0x218 mbEnabled; also
  mInitialShortfall +0xF8, mFeedingGrounds +0x68, mTerritoryRadius +0x108, mbExtinction +0x190.
* ✔ How NPC nests get eggs (cHerd::Update 0xC6D960): when members (+0x40 vector, **corpses count**)
  are fewer than the target, min(shortfall, cap 0x1582F34 = 2 at runtime) minus the eggs there, after
  mRespawnRate a living member nearest the nest becomes the egg layer (+0x194) with that many eggs to
  lay (creature +0x1678, setter 0xC02D80). Its behaviour node 0xD6CDB0 plays the lay animation and calls
  cHerd::LayEgg 0xC6D0B0 at the creature (state 6, **not snapped to the ground**: snap = the avatar bit),
  registers the egg's collision ("useGameplayPhantom", 0xB48870) and walks away. Runtime: 2 eggs within
  ~20 s with mRespawnRate 2000 and the target raised by 2 (`coop.nest respawn` / `target`); killing
  members alone laid nothing for 2 min (the corpses stayed members). Without a living member nothing is
  laid (a herd whose 3 of 4 were killed stayed so).
* ✔ Eggs fall and roll after laying (physics): one was laid 0.97 m over the terrain and lay at 0.14 m
  10 s later. In the nest's bowl an egg lies 0.74-1.2 m over the terrain (the nest model; screenshots
  show it in the nest, not floating): `over_ground` measures the terrain only.
* ✔ Hatch: cEgg update 0xC6B1E0 (only from cHerd::Update for the eggs of mEggs) counts +0x1F0 down
  (150-180 s left right after laying), at 0: state 3, cEgg::SnapToGround 0xC6A2A0 (onto the collision
  surface: the nest's top at its centre), the to_hatch creature placed there or a new member made
  (cCreatureAnimal::Create). The herd destroys hatched and eaten eggs ~10 s later.
* ✔ Eat: the eat node 0xDBD460 (animation 0x2796B30) calls cEgg::SetState(4) 0xC6A810, and when the
  eater is the avatar and the egg's herd is not its own, changes the relation to that species
  (0xC230B0 / 0xC90070, not followed). A hard collision (0xD3A450, impulse > 10) breaks an egg too.
* ✔ Drawing (cEggView, vtable 0x1486678, update 0xE8E5B0): an egg is drawn only while its mpHerd is
  set and the herd is enabled (+0x218, 0xC6A1C0); a change (+0x1EC) plays the state's effect
  (state 6: 0xB70973F3, 3: 0x4A9D2F4C, 4: 0x91363BFD, ...) with the scale +0x220 and the herd's
  mEggIndex +0x12C. An egg with no herd is invisible (first mirror attempt: listed, not drawn).
* ✔ Co-op (observed, lanes 9/10): the guest's own nest herds have **no members** (the mod removes the
  guest's NPCs; the proxies live in herds of ours), so no layer and no eggs: with the host's herd laying
  2 eggs the guest had 0. That is the "desync". Floating on the guest was not reproduced; the eggs that
  were "in the air" may be eggs in the nest bowl (0.8 m over the terrain), eggs falling after laying, or
  the guest's own eggs while its game ran its NPCs (host away); not checked.
* ✔ The fix (`nest.eggs`, mods/coop/nest.cpp): the host sends the eggs of NPC herds (not the avatar
  family's) within 110 m of each guest at 1 Hz (MSG_NEST 52: serial, position, state, scale, the herd's
  home +0x34); the guest makes a cEgg (CreateInstance) per host egg, sets its mpHerd to its own herd of
  the same home (AddRef, **not added to mEggs**: its update never hatches or clears ours), places and
  snaps it to its own ground, follows the host's state 3 / 4 by SetState and removes it (Release of the
  herd, DestroyInstance, Release) when the host's egg is gone; savestrip leaves them out of saves. An
  egg of ours eaten on the guest (state 4) sends MSG_NEST kind 1 and the host calls SetState(4) on its
  egg (the species' relation change of the host's eat node is not applied for the guest).
## Walks at 30 fps: one shared walk driver, moves without a speed (2026-11-01, fix/w30-npc-walk)
<a id="creature-walk-30fps"></a>

BUG-0394 / BUG-0387. GOG 2024, lanes 2-5, `SPORE_FPS=30`, `SPORE_NET=player`.

* ✔ One implementation: `drive::walk_place` / `walk_end` (driver.h) drive the partner's puppet
  (creature.puppet_walk), the Tribe mirrors (tribe.mirror_walk) and the NPC proxies (npc.walk,
  npc.walk_tribe). `WalkPlace` options: goal_ahead, turn_dot, min_gap, max_gap, catch_up, own_height,
  speed_after_goal and max_dt (Tribe), ground (on_own_ground), walk_speed, snap and keep_lost (NPC
  proxies), variant (tests). `drive::path_update` / `path_velocity` track the remote's interpolated path
  per frame for every user.
* ✔ A partner can move without a speed of its own: its creature carried by an animation (B4212A07 after a
  click on its screen: 3-4.6 m/s for a second with the game's velocity 0 and speed state 0). Its states
  then say "standing"; the puppet stood (drive::step) while its path went on, fell 2.9 m behind and was put
  in place (18 % standing windows in that run). `creature.puppet_path`: the puppet walks with
  `path_velocity` (the path smoothed over 0.2 s from 0.5 m/s, before that this frame's path from 0.8 m/s,
  never a path jump over 15 m/s) when the state reports no speed. Test lever `coop.track novel 1` (all
  partner states without their speed): off 39-63 % standing and 4-9 jumps per direction, on 0-1 %.
* ✔ Right after a join a guest loads for seconds (18 fps on average, single frames of 5.4-5.6 s) and its
  game did not update the partner's puppet for 7 s: it moved only where it was placed (game speed 0,
  walk gait 0, idle animation) even on the screen. Later it walks normally. Tests now wait until both
  games run steadily (`settle`), as a player would only judge after the load.
* ~ At 21-29 fps (a loaded machine) a 0.1 s window holds 1-3 frames: the drawn puppet's speed over it
  swung 0.3-2x the path's with the frame times (the game moves a creature by its own frame time) while its
  drawn error stayed 0.01-0.6 m; over 4 frames 89-98 % matched. Not a step a player sees (inferred); the
  walk tests' windows span at least 4 frames.
* ~ The walker of puppet_walk sometimes did not walk: its clicks on the partner's tag (to walk back when
  12 m away) did nothing after a first walk ended 15-25 m apart; the test now puts the walker next to the
  observer before its walk (`coop.npc gopos`).

## NPC proxies walking downhill fall (2026-11-01, fix/w33-proxy-ground)
<a id="creature-npc-ground"></a>

BUG-0406. GOG 2024, lane 7, creature fixture, `coop_npc_walk.py --goto=-195,-465,-45` (suite
`npc_walk_slope`: both avatars on the slope of [creature-puppet-ground](#creature-puppet-ground), the host's
game walks 4 NPCs to points 4-12 m around the guest, `coop.npctrack` every frame on both games; the test now
counts fall starts, +0x137 going 1 -> 0 while the host's NPC moves).
* ✔ The inference of creature-puppet-ground holds for NPC proxies: walked by `npc.walk` (placed exactly on
  the host's path every frame) they started to fall 51.1 and 22.7 times per 100 s of walking on the slope;
  the host's own NPCs walking the same paths 0.9 and 0.8 (out/lab/20261101-142745 npc_walk_slope#1, #2). Their
  walk gait (98 % on screen) and the path (93 % matched windows) were fine: the falls are single frames.
* `npc.walk_ground`: the proxies' walk is placed with `WalkPlace::ground` (on_own_ground, 5 cm into the ground)
  as the partner's puppet (creature.puppet_ground).
## A creature outside the view stands on the plain terrain, not on a nest mound (2026-11-01, fix/w34-ability-pop)
<a id="creature-out-of-view-ground"></a>

BUG-0414, BUG-0434. GOG 2024, lane 4, guest at 30 fps over netem `player`, `coop.track` rows (hd / ht: height over
`cPlanetModel::GetHeightAt` of the drawn puppet / of its target; vis / sx / sy: its screen position).
* ✔ The fixture's nest at about (-127, -488, -65) (the 065CB8AB herd, NPCs #18-#21) stands on a mound: a creature
  walks and stands there 0.4-1.03 m over `GetHeightAt` and is grounded (+0x137 = 1), own avatar and puppet alike
  (`GetHeightAt` is the plain terrain without the mound). The NPCs of that nest stood next to it at -0.05-0.0 m.
* ✔ A creature of ours (the partner's puppet) **outside the observer's view** is put on the plain terrain by the
  game: with the host standing on the mound at 1.03 m and the guest's camera turned away (right-drags), the
  puppet was drawn at 0.00 m in every frame it was off the screen (1503 of 2113 frames of `lod_exp`), at 1.03 m in
  the frames it was on it; the steps are 1.03 m each way, every time the view swings over it (at 7, 15, 25 and
  40 m from the observer alike). With the camera still and the puppet in view: no step.
* ✔ The first frame back in view is still drawn low; the game lifts the creature one update later (26-28 such
  frames in 70 s of camera turns): a visible one-frame step (BUG-0434).
* ✔ Writes at the mod's frame (Present) cannot keep it up while it is out of view: a teleport to the target with
  the teleport mark (the driver's stuck teleport every 1.5 s, `tp` 2 -> 19 in 70 s), the same plus +0x137 = 0,
  or clearing bit 0x800 of +0xB58 (+0xB59 & 0x08, set in 6 of 6 memory samples off screen and in 0 of 6 on it)
  each frame: the next update put it on the plain terrain again every time. `or [creature+0xB58], 0x800` is
  written by about 15 places (0xD2600D, 0xE8CB6A, 0xF10799, ...; 0xE8CB6A is in a behaviour node of the Creature
  and Adventure modes, not a view test) and tested at 0xC1CFCC in 0xC1CF30 (an ability tick, types 0xD / 0x52):
  so the bit is not (only) the view state. ? Which update grounds a culled creature on `GetHeightAt`.
* ✔ `cCreatureAnimal::Update` (0xC0AE30) does not move a puppet: its position before and after the call was the
  same in every sampled frame (moved 0.00 m, also in the frames of these drops). The game calls it about 15 times
  a second per creature (dt 31-100 ms; 22 puppets and proxies on a guest: ~300 calls a second), not every frame.
* ✔ BUG-0414's "pops when it uses an ability" were these steps: every 0.77-1.0 m step of the w33 runs
  (20261101-142305, 20261101-151044) was out of the observer's view (vis 0 or sx -800..4700) while the fighter
  stood or walked on the mound (its own hd 0.97 / 1.03), with the test's camera drags (`Watch`). The partner's
  animations played on the puppet (MSG_ANIM, `coop.track` columns ranims / ranim) are rare in a fight (7 in 50 s)
  and none came within 0.6 s before a step; 02481DE5 is the animation of a stop (speed state 0), not an ability.
* ✔ `creature.puppet_ground` (kOverGround 0.5 m) puts a target 0-0.5 m over `GetHeightAt` 5 cm into it: on the
  lower part of the mound (0.39-0.44 m) the standing puppet stays 0.44 m under the partner (drawn -0.05 m while
  the partner's own creature is at 0.39-0.44 m; the game does not lift a creature put under its ground), and a
  partner walking over the 0.5 m line makes the puppet step 0.4-0.5 m (BUG-0435).
## A cinematic left playing in an editor; the game's busy states (2026-11-01, fix/w34-vote-cinematic)
<a id="creature-busy-states"></a>

BUG-0404. GOG 2024, lane 5, `tools/tests/coop_vote_cinematic.py`.

* ✔ After every editor the Creature stage plays CRG_Fade_From_Black_Egg (F4F96E43) on both games: ~7 s on
  the host, ~10 s on the guest (`coop.status` "cinematic F4F96E43 playing", input lock 1), then it ends by
  itself. The same cinematic started by hand (`call Simulator::cCinematicManager::PlayCinematic thiscall <cm>
  s:CRG_Fade_From_Black_Egg 1 1 0 0 0`) behaves the same in the stage.
* ✔ An editor opened while it plays (EditorRequest::Submit from the vote's held request) comes up without
  its buttons (no top bar, no "done", the letterbox bars stay) and the cinematic never ends in the editor
  mode: `cinematic F4F96E43 playing` and `input_lock=1` for minutes; Enter and clicks on "done" do nothing.
  `SporeCoop::cCinematicManager_EndCinematic` called then (in the editor) ends it: the buttons appear at
  once and "done" works.
* ✔ `cGameTimeManager` +0x48 bit 0 is the game time standing still in Creature: the pause menu 0x03, the
  Sporepedia (the 3rd small HUD button bottom left, 84 1054 in the 1440x1080 window) 0x01, My Collections
  (the 4th, 114 1054) 0x03; 0 again after Esc. The input lock stays 0 for these windows. So "paused" is not
  only the pause menu's "Resume Game" button (BUG-0331, BUG-0336): the mod reads the flag (`busy: paused` in
  `coop.status`, `busy=` in the trace line).
* ✔ Right after a world load (a guest loading the host's share) the pause flag is set for some seconds
  (`busy=paused` in the guest's trace from the first Creature frame, gone ~10 s later; lab
  20261101-170147, run 2). An Esc in that window did not open the pause menu (the flag went to 0): tests wait
  for `busy: -` before they pause a game by hand.
* ✔ The game sets the pause flag itself, with no window of the player's: on the host at the start of the
  Creature stage after the Cell stage's editor (~18 s, `busy=paused`, input lock 0), after the first Creature
  editor (~12 s), and together with a caption cinematic (`cinematic,input_lock,paused` for minutes while the
  caption waits for OK); lab 20261101-172428 (guest_editor_hold), host trace. So a wait for "paused" also
  waits for these: the world share does not (busy.h kShareGate), editors and action votes do (a pause alone
  at most 300 s, a cinematic under it 60 s).
* ~ The co-op window opened by a vote does not stop the game time (the held editor opened right after the
  vote with the window just closed; inferred from the passing runs, the flag was not read with the window up).

## Eggs have no gravity while asleep (2026-11-01, fix/w35-egg-kick)
<a id="creature-egg-gravity"></a>

BUG-0443 (the host's screenshot of the "eggs flying mid air" report). GOG 2024, lane 4, creature fixture,
`coop.nest lift` / `objs` (over_surface).

* ✔ An NPC egg (state 6) does not fall by itself: moved 2-2.5 m up with SetPosition (`coop.nest lift`, the
  call cHerd_LayEgg makes with the layer's position), fresh (lifted within 1 s of being laid) or settled,
  it stayed at that height for 8-10 s and longer (host, nobody within 6 m). A creature that came to it
  (the partner's puppet put onto the other egg, ~2 m away) woke it: it fell from 2.71 m to 0.82 m over the
  terrain within 3 s. So the physics moves an egg only while it is awake (right after laying, or touched);
  asleep it stays where it is, in the air too.
* ✔ The eggs the game lays settle over the surface: right after laying the physics pushes the egg up out
  of the layer's body / the mound (0.76 -> 0.93, 1.04 -> 1.29 m over the terrain) and it goes to sleep
  there. Over the surface (the ray cast below), fix off, 5 nests of the fixture: 0.05/0.47, 0.04/0.05,
  0.21/0.33, 0.05/0.46, 0.05/0.43 m (2.5 m apart: not one egg on the other). One in two eggs hangs
  0.3-0.5 m in the air. Inferred: a bigger layer (the player's species) leaves it higher. On the screenshot
  of the FAIL run an egg 2.4 m up looks like the player's ("a green egg hanging over the slope").
* ✔ `SporeCoop::cPlanetModel_SurfacePoint` 0xB82AC0 (what cEgg_SnapToGround uses): the terrain point
  (0xB816F0) and, when the physics manager (0xB3D410) +0x20 is 0, a ray cast (0xB65C10, layer
  [0x167ECD0]) from the terrain's top down to it; a farther hit (the nest mound, a rock) wins. An egg
  snapped with cEgg_SnapToGround lies 0.00 m over that point. It hits only collision that is loaded:
  far from the host's camera the nest mound has none (below).
* ✔ Far from the host's camera (the host 450 m away, the guest at the nest: the herd kept by the view
  centres, BUG-0280) the members stand on the bare terrain (0.00 m over `GetHeightAt`; 0.75 m on the
  mound when the host is near) and lay their eggs there: inside the nest mound, asleep, still there after
  the host came back (-0.01 m). On the guest their copies (nest.eggs) are snapped onto the guest's
  surface (on the mound). Not fixed (BUG-0443 fixes eggs over the surface only).
* ✔ `GetHeightAt` around a nest is the same with the host near or 450 m away and on the guest (25
  points, 0.00 m): no terrain LOD in it.
* ✘ The partner's puppet does not launch eggs: the guest's avatar put onto a host egg (the host's puppet
  placed there) pushed it up 0.14 m (0.95 -> 1.09) and woke the other egg, which fell.
* ✔ The fix (`nest.egg_ground`, mods/coop/nest.cpp): every 0.5 s the host checks the eggs of NPC herds
  (state 5 / 6) within 60 m of its avatar; one that did not move for two checks and lies more than 0.3 m
  over the surface is put onto it with cEgg_SnapToGround (what the game does to the avatar's eggs and to
  every egg at its hatch).

## The size of NPCs and their proxies (2026-11-03, fix/w40-scale)
<a id="creature-npc-scale"></a>

* ✔ NPC sizes differ per individual: on the creature fixture the four members of one herd had 0.69, 0.75,
  0.85, 0.86 (the bigger one with 7.5 health instead of 6), another herd 0.87-1.06; all age 1 (+0xB34).
  A proxy made by `game::spawn_creature` gets a size of its own game's pick (0.65 / 0.73 / 0.81 / 0.86 for
  that herd): the detector's `Creature:scale:creature` in 94 % of the comparisons. A young one (age 0) is
  smaller on the host; its proxy was made at an adult's size (player report BUG-0461: babies shown as
  alphas). `NpcSpawn.scale` was always sent and never read.
* ✔ SetScale (spatial vt 0x40) on a proxy stays: 19 of 21 proxies set once, 0 set again over 2 minutes
  (`coop.fields`: applied 19, reapplied 0). The proxies' size is a replicated field now
  (docs/sync-architecture-plan.md#sync-fields, `npc.scale`); a young one that grows up on the host grows on
  the guest with the next delta.
* BUG-0487 (class G5): the host sends a spawn once per guest (npc.cpp `g_known`); a guest without the proxy
  of an id dropped that NPC's states for good. Now it asks for the spawn again (`MSG_NPC_RESYNC`,
  `npc.resync`; `coop.npc forget <id>` makes the case on purpose, `coop.npc resync` counts).
## Creature effects relay (2026-11-02, fix/w38-effects)
<a id="creature-effects-relay"></a>

BUG-0451: a player's creature made effects (the mating call, ability particles, ...) that only that
player saw; the partner's puppet played the relayed animation but no effect. The owner's screenshots:
hearts over the host's red creature (`out/hearts_host.png`), none over the host's puppet on the guest
(`out/hearts_guest.png`).

### Where the game makes a creature's effects

Two game functions make every effect on or at a creature (GOG 2024):

* **`Simulator::cCreatureBase::CreateEffect` 0xC150D0** (thiscall(creature, int bone, u32 effect id,
  u32 unused) ret 0xC -> effect*): a one-shot effect. It calls `Swarm::IEffectsManager::Get()` vt 0x2C
  (`Swarm::cEffectsManager::CreateEffect` 0xA6CAD0 -> `Swarm::EffectWorld::CreateEffect` 0xA71300),
  sets the effect's transform to the creature's model (+0xC0) position (vt 0x2C) and rotation (vt 0x30),
  starts it (effect vt 8) and, when bone != -1, attaches it to that bone (`0x9CB230` / `0x9CA970`).
  Callers for the player's creature: the ability/social code (`0xC155D0`, `0xC17DE0`, `0xC17E20`,
  `0xC17EC0`) and the creature-stage action handlers (`0xD75F70`, `0xD812C0`, `0xD8ECB0`).
* **The two effect pools** (persistent effects the creature keeps): pool 1 map at +0xCC8,
  `CreateEffectForPool1` 0xC1D3F0 and `CreateEffectForPool1Renamed` 0xC1D280; pool 2 map at +0xD68,
  `CreateEffectForPool2Renamed` 0xC1D5C0; `StartOrStopEffectFromPool1/2` 0xC20300 / 0xC1D740 start or
  stop by key; `StopEffectFromPools` 0xC174B0 stops a key (effect vt 0xC).

Effects created by the **animation system** of an animation the puppet plays are made by the guest's
own game (`FUN_009FBF00` -> effect world, and the manager for animation events): the animation relay
(MSG_ANIM, `on_remote_anim`) already shows those on the puppet. Measured on the creature fixture: the
footstep puffs (`2D4ADB36`) and the mating call's green ring (the `E699F6E4` copied from the effect
object's name `CRG_growUp_brainUp` was the brain-upgrade effect; the ring came through the animation
event path) were created on the guest before any effect relay. The **hearts** were not: the mating call
creates `E1B24DDB` (bone 9) through 0xC150D0 on the host's avatar only, and the pack members make
`4EC6CD85` through the same function (not the player's own creature, so not relayed).

### The relay (mods/coop/effects.cpp, game.cpp)

* The game hooks report every effect of `CreateEffect` (pool 3, key = bone) and of the pool creators
  and `StopEffectFromPools` (pool 1 / 2, key = pool key) for a creature. effects.cpp sends one only
  when the creature is the **local avatar** (`game::avatar()`, not a puppet, so a relayed effect is
  never echoed) and the session is connected: `MSG_EFFECT 61` = u8 player, u8 stop, u8 pool, u32 effect
  id, u32 key. The host relays a guest's message to the other guests. Rate limit 40/s (token bucket),
  a stop is sent only for a key this game sent a start for (the game stops some keys every frame).
* The receiver calls the **same game function** on the partner's puppet: pool 3 ->
  `CreateEffect(puppet, bone, id, 0)` (the game puts it at the puppet's model and bone); pool 1 / 2 ->
  `StartOrStopEffectFromPool1/2(puppet, 1, id, key)`. No per-effect list.
* Switch: `coop.fix creature.effects_relay off` (COOP_FIX, BUG-0451); counters and the last sent /
  received effect in `coop.effects` and `coop.status`; `coop.effects map` dumps the puppets' pool maps,
  `coop.effects probe <key>` asks the game's pool, `coop.effects log on` logs the hooks.
* The game also creates effects **inside its UI skinner's render pipeline** (Tribe and Civ):
  `Skinner::cSkinPainterPipeline::ExecutePipeline` -> `EffectWorld_CreateEffect`. Hooking the
  direct `EffectWorld::CreateEffect` there made the game fault at
  `Skinner::cSkinnerTexturePainter::StartRender+0x28` (4852 times on a Tribe host, 2863 in Civ);
  the hook was removed and re-installed at every stage change while the render thread was inside it.
  The final shape (### What was measured (lane 2, GOG 2024, 2026-11-02)

Every "made by" below is the game function the effect came from; "before" is without this relay
(the old behaviour) and "after" with `coop.fix creature.effects_relay on`.

| stage | effect | made on the actor by | on the partner before | after |
|---|---|---|---|---|
| Creature | mating call `E1B24DDB` bone 9 (host), bone 0x11 (guest) | `cCreatureBase::CreateEffect` 0xC150D0 | no (only the relayed animation) | yes, game effect active at the puppet (0.1-0.5 m) |
| Creature | being hit `E1A4BD93` bone 0 | 0xC150D0 | no | yes (0.1-0.5 m) |
| Creature | death: `EF8F883F`, `BED2AD51` (world), `7809F8FD` (pool 1), `E1A4BD93` | effect world + pool 1 + 0xC150D0 | no | yes (the world effects at the avatar; the pool and one-shot through their hooks) |
| Creature | a part found `A3C5EB2B` (world) | effect world at the avatar | no | yes (1.3-1.6 m) |
| Creature | brain upgrade `D7BE955C` (pool 2); the cinematic's `E699F6E4` | pool 2 here; the cinematic effect is made by both games (both got the DNA) | no (the cinematic effect: yes, the guest plays its own) | yes |
| Creature | an ability's kept effect `3A616FEE` (pool 1, `coop.abilities play 10`) | pool 1 | no | yes |
| Creature | social abilities (sing, dance, charm, pose) and the social result | the ability animation; animation-event effects (`FUN_009FBF00`) | yes: the animation relay makes them on the puppet | yes (not duplicated by this relay) |
| Creature | attack abilities bite, charge, strike, spit (`coop.abilities play 0..9`) | the ability animation; animation-event effects | yes (animation relay) | yes (no one-shot/pool effect was made by these in the fixture) |
| Creature | sprint, sneak, glide | the locomotion animation and its animation-event effects (footsteps `2D4ADB36`, dust `E6D770B8`) | yes (animation relay) | yes (not duplicated); the fixture cannot glide (no jump) |
| Creature | eating fruit / meat | not measured: the fixture has no reachable fruit and no deterministic eat command; the world relay would catch an effect at the avatar | not measured | not measured |
| Creature | level-up / brain upgrade (see above) | `AddEvolutionPoints` -> pool 2 + cinematic | no | yes |
| Cell | a hit / knock-out death `F3BA1C83`, `2C34420C`, `A6D4590B`, `44894CD7`, `94AF0B82` (world) | effect world at the cell | no | yes, the partner's game creates it at the same world position (test: 0.0-0.2 m from the cell, 0.05 m from the puppet) |
| Cell | eating, bites, electric/poison/spikes | not separated from the hit effects above; the same world path | no | yes (same relay; not a separate case) |
| Tribe | a member's death `EF8F883F`, `E55DC391` (world) | effect world at the member | no | yes, at the member's mirror (0.18 m; test) |
| Tribe | tool use, gathering, dance/social, fire | not measured: `coop.tribe act 7` ordered the attack but made no effect in the create/pool hooks in the fixture; their visible part is animation | not measured | not measured |
| Civ | vehicle weapon / ability | not measured: `coop.civ` has no command that fires a vehicle weapon in the fixture (move/buy/build/siege do not), and no effect appeared in the create/world hooks during them | not measured | not measured |
| Space | UFO weapon / ability | not measured: `coop.space hit 1|50` (the game's damage path on the own UFO) and `coop.fleet spawn 6` made no effect (`coop.effects world=0`); firing at a target would make one, but the fixture has no target in range and no test command fires the player's weapon | not measured | not measured |

The generic relay is in place for every stage that makes an effect at the local player's own
controlled object: `effects.cpp` decides by the effect's world position against
`coop.cpp`'s `near_local` (the avatar in Creature and Cell, `tribe::local_member_positions()` in
Tribe, `civ::local_positions()` in Civ, the adapter's local UFO in Space). The receiver creates the
same effect id at the same world position with `Swarm::cEffectsManager::CreateEffect` and the same
transform, so no per-stage puppet mapping is needed. Civ and Space are wired but no effect could be
made to test them (above).

### Review: cost, threads and hook lifetime below): every hook is installed
  **once** in `game::init`, the stage logic only flips a work flag, and a hook body on any thread
  but the game's main thread only calls the original and returns. The hooks record into a fixed
  open-addressing table (no allocation, no linear scan) and `effects::tick` (main thread, once a
  frame) reads the effect's position and sends. No allocation and no network call happens in the
  render thread.

### Review: cost, threads and hook lifetime (2026-11-02, fix/w38-effects)
<a id="creature-effects-relay-review"></a>

A code review of the first shape found three problems; this is what changed and what was measured
(lane 2, GOG 2024, 2026-11-02, `out/measure_effects_perf.py`).

* **Cost**: the base transform setter `VisualEffect_SetTransform` 0xA827B0 ran on every transform
  set of every effect, in every stage, and looked the effect up with a linear scan of 4096 entries.
  The setter is no longer hooked: `cEffectsManager::CreateEffect` and `EffectWorld::CreateEffect`
  record the effect in a fixed 1024-slot open-addressing table keyed by the effect pointer (one
  probe for nearly every effect, cleared once a frame), and `effects::tick` reads the position
  `VisualEffect_SetTransform` wrote (`effect+0xA0`, see the correction below) when it drains the
  queue. Per created effect the hooks cost 0.16-0.26 us (two QPC reads included), which is
  0.0001-0.0006 % of the main thread's time; `effects::tick` costs 0.001-0.02 ms/frame
  (0.01-0.1 % of a 15 ms frame). Frame time with the relay on against
  `coop.fix creature.effects_relay off`, 20 effects/s at the local player over four 20 s windows
  (the windows alternate and a second run swaps the order, because the fourth window of a session
  drifts down for both settings):

  | stage | fps off | fps on | ms/frame off | ms/frame on | difference |
  |---|---|---|---|---|---|
  | Creature (both orders, paired) | 64.74 / 64.81 | 63.41 / 66.19 | 15.447 / 15.439 | 15.779 / 15.107 | -0.002 % (ordering cancels) |
  | Tribe | 67.72 | 67.44 | 14.768 | 14.829 | +0.41 % |
  | Civ | 66.82 | 66.76 | 14.966 | 14.979 | +0.09 % |

  The measured cost is under 1 % in every stage (the Tribe +0.41 % is inside the run-to-run noise,
  the tick cost there is 0.016 ms/frame). Reproduce: `coop.effects load 20 1000 [hex id]` on both
  games (`coop.effects load 0` stops it; no id = the last effect id the game created, which is a
  valid id in every stage), then read `coop.effects` (tick cost, hook work, created/failed) and the
  `perf: effects` lines in the game log; compare four 20 s windows with
  `coop.fix creature.effects_relay on|off` and swap the order in a second run, because the fourth
  window of a session drifts down for both settings.

* **Threads**: the game creates effects from the UI skinner's render thread too. Every hook body
  now checks the game's main thread id (set by `effects::tick`, `game::set_effect_relay_enabled`)
  and only calls the original when it is another thread; the table and the queue are written on
  the main thread only. The naked stubs are gone (no shared return-address slot): the animation
  effect wrapper `AnimEffect_Create` 0x9FBF00 is a normal cdecl hook that sets a **thread-local**
  flag, so the effects of a relayed animation are still skipped (the animation relay shows them)
  without a caller-address match.
* **Hook lifetime**: `set_world_create_hook` called `hook_create`/`hook_remove` at stage changes;
  the removal while the render thread was inside the function is what faulted. All effect hooks
  are installed once in `game::init` for the life of the game; `game::set_world_relay` (the stage
  logic: Cell, Creature, Tribe, Civ, Space) and `game::set_effect_relay_enabled` (the fix switch)
  only gate the work.
* **Correction (2026-11-02, fix/w38-effects)**: `VisualEffect_SetTransform` 0xA827B0 assigns
  Transform+4 to the math Vector3 object at `effect+0x9C`, and that object has a 4-byte header: the
  position floats are at `effect+0xA0`. Reading `+0x9C` returned `(0, x, y)`, so every world effect
  looked like it was not on the local player and nothing was relayed (measured: part found
  `A3C5EB2B` at `(0.00, -170.10, -468.99)` while the avatar was at `(-169.51, -467.81, -87.11)`).
  The receiver's displayed position in `coop.effects` had the same wrong offset.

* Faults after the change: 0 in cell_coop, creature, tribe_coop, civ_coop, space_coop, chaos_tribe
  (`out/lab/20261102-180357`), guest_editor and editor_desync (`out/lab/20261102-170849`); every
  test prints `total faults: 0` for both games.

### Review fixes, second pass (2026-11-02, fix/w38-effects)

<a id="creature-effects-relay-review2"></a>

The orchestrator's review of the first relay found six more problems; fixed in this order (one
commit each, tests where asked):

1. **Three players**: the host's relay of a guest's message wrote only the header, so the 0x44-byte
   transform of a pool 5 effect was missing for the other guests and their `on_message` dropped the
   message. The relay now forwards the transform. Test: `tools/tests/coop_effects_party.py`
   (`effects_party`, three players from the same lane): the host's world effect reaches guest 2 and
   guest 1's world effect reaches guest 2. Without the forward the guest 1 -> guest 2 case fails
   (`out/lab/20261102-184838`); with it all pass (`out/lab/20261102-184722`).
2. **Spoofing** (BUG-0201): the `player` byte of a message chooses whose puppet plays it. The host
   drops a guest message whose byte is not the sender and a guest drops anything that did not come
   from the host (messages are never sent guest to guest). `coop.effects inject <from> <player>
   <stop> <pool> <id> <key> [x y z]` feeds a crafted message through the same `on_message` (the
   transport cannot make a guest claim another player); `coop.effects` counts the drops
   (`spoof=`).
3. **Validation before the game**: a stop must have pool 0 and a key; a start a non-zero effect id
   and a pool the sender uses (1, 2, 3, 5); a pool 3 bone index must be inside the puppet model's
   bone array; a pool 5 transform must have finite floats and a position within 1e5. Counters:
   `pool= id= bone= transform= short=`. The bone range: `cCreatureBase::CreateEffect` 0xC150D0
   indexes `[AnimatedCreature +0xB54] -> +0x17C -> +0x2E4` with `bone * 700` and has **no check**
   (the only test is `bone != -1`, which returns null); the array end at `model+0x2E8` gives the
   count (24 bones for the fixture creature). Test: `tools/tests/coop_effects_validate.py`
   (`effects_validate`): every rejection is counted and not played, a valid message still plays.
   Without the validation call the bad cases fail (`out/lab/20261102-185729`), with it all pass
   (`out/lab/20261102-185635`).
4. **A relayed world effect is stopped**: a looping effect (the mating-call hearts id stays active
   when the effect world owns it) never ends on the receiver. The receiver keeps a fixed list of 64
   live relayed world effects: the oldest is stopped when the 65th arrives, any still active after
   8 s is stopped (`IVisualEffect` vt 0xC with 0, the slot `StopEffectFromPools` 0xC174B0 calls),
   and a stage / world change (mode or world id differs) drops the list **without** calling into
   the effects (the game's effect manager freed them); a session end stops what is still there.
   `game::effect_active` and the new `game::stop_world_effect` only call the object when it and its
   vtable are readable. Test: `tools/tests/coop_effects_live.py` (`effects_live`): 20 effects/s for
   30 s, the guest stays at or under 64 (it reaches 64) and returns to 0 after the load stops
   (`out/lab/20261102-190208`; with the switch off nothing is relayed and it fails).
5. **Full transform**: the relayed 0x44-byte Transform had a zero scale and a zero 3x3 matrix. The
   layout (`cCreatureBase`'s creators, `VisualEffect_SetTransform` 0xA827B0): u16 flags, u16 change
   counter, Vec3 position at +4, **uniform scale at +0x10**, **3x3 rotation at +0x14** (36 bytes,
   the rest of the object). The relay now sends position, scale 1.0 and the identity rotation. Proof
   on the partner's screen: `out/effect_relay_on_1.png` (guest's view, the host makes the hearts
   with `coop.effects load 2 1000 E1B24DDB`: the effect is at the host's puppet "Alice"),
   `out/effect_relay_off_1.png` (same camera and moment after the switch off: no effect) and
   `out/effect_relay_host.png` (the host's own screen with the same effect, for comparison);
   `out/effects_shot.py` (lane 2).
6. **O(1) dedupe**: `process_world` scanned the 2048-entry `g_world_sent` ring for every queued
   world effect (the manager and the world create hooks queue the same effect). It is a fixed
   512-slot open-addressing set keyed by the effect pointer, cleared once a frame, and
   `coop.effects` shows the probe count (`sent_probes=`).

7. **Reference leak** (added after 15bd1ebc): `EffectWorld::CreateEffect` 0xA71300 writes the new
   effect into the caller's intrusive pointer and calls its AddRef (vtable slot 0x00 at 0x923720,
   refcount at object+0x10); the game drops such a pointer with Release (slot 0x04 at 0xA81CE0, as
   the pool map erase 0xD2C9F0 does). The relay never released it, so every received world effect
   stayed allocated for the session, even after Stop. The live list now owns that reference and
   releases it when the effect ends by itself, is stopped after 8 s, is evicted as the oldest, or
   the session ends. Because the reference is held until then, `IsActive` and `Stop` on a listed
   effect are safe even after the effect path ended (the object cannot be freed while we hold it);
   after the Release nothing uses the pointer. A stage / world change still drops the list without
   calling into the objects (their internals may be gone). `create_local_effect` (the perf
   workload) releases its reference right away. Switch `coop.fix effects.release off` is the old
   leaking behaviour, for the measurement; `coop.effects` prints `released=` and `dropped=`.
   Test `tools/tests/coop_effects_release.py` (`effects_release`, 60 s per window at the relay rate
   limit): fix on 2380 effects created, 2380 released, live 0, private commit flat/negative; fix off
   2320 created, 0 released, private commit +8.2 MB (about 3.5 KB per leaked effect), 25 MB
   difference (`out/lab/20261102-195942`). Without the release the on window fails
   (`out/lab/20261102-200305`).

Verification of the second pass (lane 2 and 3, GOG 2024): the effects tests (creature, cell, tribe,
effects_party, effects_validate, effects_live) pass in
`out/lab/20261102-191038` and fail with `SPORE_FIX_OFF=creature.effects_relay` in
`out/lab/20261102-193333`; the regression (smoke, creature, team, puppet_walk and the effect tests)
is in `out/lab/20261102-191038`; the `unit` test of that run fails in its own lab selftest's lane
lock checks under the run's CPU load (no game code; `make test` alone and `unit` alone in the lab,
`out/lab/20261102-194317`, pass). Item 7: `out/lab/20261102-202254` (effects tests + effects_release pass);
`out/lab/20261102-202617` with the relay switch off.

## The effects relay and play_creature: A/B, and effects only between players in one place (2026-11-03, fix/w40-effects)
<a id="creature-effects-relay-ab"></a>

BUG-0471 asked whether the relay (#creature-effects-relay) makes play_creature fail. Measured on one
build (fix/w40-effects fe5afc69, lane 4, GOG 2024), play_creature 10 min x4 with the relay on and x4
with `SPORE_FIX_OFF=creature.effects_relay`, sequentially (out/lab/20261103-120602 on,
out/lab/20261103-124913 off; table and parser `out/ab_parse.py` in out/REPORT.md):

* ✔ pass rate: relay on 3 of 4 test OK (2 of 4 with the lab's log gates), relay off 0 of 4. Main
  0.7.3.6 without the relay: 2 of 6 (nightly 20261103-000836, -012624). The failure kinds are the
  same with and without the relay: a chat line lost (BUG-0482), a puppet once 11-177 m off with
  speed 0 (BUG-0499), no joint hunt (BUG-0500), an NPC missing / apart on the guest (sync-check gate).
* ✔ net and sync numbers do not move with the relay: puppet p90 0.49-0.52 m on vs 0.50-0.72 off,
  NPC p90 0.30-0.39 vs 0.08-0.40, moving delay p90 0.24-0.26 s both, state_age p90 0.1 s / max
  0.5-0.7 s both, host backlog max 600 in both (the guest's return from the main menu: the world
  transfer), resends after the join minute 3.5k-10k on vs 3.2k-5.6k off (the same world transfers;
  main without the relay 5.4k-8.9k), rtt max 650-830 ms both, fps 64-68 both.
* ✔ the relay's traffic is tiny in play: 7-41 effects sent per 10 min by the host and 7-13 by the
  guest (under 0.1 messages a second, against the 40/s limit); head-of-line blocking of the reliable
  channel by effect messages is ruled out by the rate alone.
* ✔ main-thread cost in play: hooks at most 0.0006 % of the main thread, `effects::tick` 0.0032-0.0034
  ms a frame (0.02 % of a 15 ms frame).
* So the 1-of-5 of 2026-11-02 was play_creature's own flakiness plus a loaded machine (7 lanes),
  not the relay.

Found on the way (fixed, switch `coop.fix effects.here`, BUG-0471): the relay sent every effect to
every partner and the receiver created a world effect in whatever world its game had then. A guest
in the main menu (play_creature's guest leaves and comes back), an editor, a loading screen or at
another star got the host's effects created there. Now a start is sent only while some partner
plays in the sender's place (`same_place` + `PF_IN_WORLD` of its last state) and a received start
is played only while its owner plays in the receiver's place; stops are always passed. Counters:
`effects elsewhere: not sent=N not played=N` in `coop.effects`. Test `coop_effects_here.py`
(`effects_here`): the guest in the main menu, the host making 2 effects a second: fix on, host
`not sent` 24 and the guest's game called 0 times; back in the world the effects reach it again.

With `SPORE_FIX_OFF=effects.here` the guest's game in the main menu (GalaxyGameEntry) did create
12 of the host's world effects (`played` +12, none failed: the menu's effect world accepts them),
out/lab/20261103-141143; on, 0 (out/lab/20261103-133351). Three players in every stage
(`coop_effects_party.py --fixture cell|creature|tribe|civ|space`, out/lab/20261103-134650): the
host's and a guest's world effect reach the second guest at the actor (0.00-0.17 m) in all five
stages; the relay's cost with all three games making 2.5 effects a second is 0.11-0.51 % of the
frame time (the third game, two extra instances in one bottle, the highest).
## World objects the players take: fruit, trees, bone rewards, extinct nests (2026-11-03, fix/w40-world-objects)
<a id="creature-world-objects"></a>

BUG-0472, BUG-0459 / BUG-0473, BUG-0460, BUG-0474. GOG 2024, Ghidra (out/ghidra-2024) and lane 9 at runtime
(creature and tribe fixtures, `coop.wobj`). The mod's use: mods/coop/worldobj.cpp (one table of kinds,
docs/sync-architecture-plan.md L1 / L2).

* ✔ **Fruit** cFruit (noun 0x2C9CC91, size 0x170, vtable 0x145C568): +0x16C state, set by
  `cFruit_SetState` 0xAFFFE0 `thiscall(fruit, state, force) ret 8`: 0 hanging in a tree, 1 falling, 2 on the
  ground (+0x168 = 0, then a time there), 3 held by a creature eating it, 4 hidden (model off), 5 / 6 eaten
  (+0x108 holder dropped). +0x158 the plant species (0x029C388A big fruit tree, 0x6D60A1CC small plant),
  +0x160 the tree's index, +0x164 the slot on the tree (-1 once fallen). Runtime: SetState(5) on a fruit takes
  it out of the noun list within 2 s, on host and guest alike.
* ✔ The eat node 0xD87590 (case 3, the meal's end): fruit state 3 -> when +0x148 & 1 message 0xD335362B
  {creature, fruit, 0} and DestroyInstance; then SetState(5), 0xD335362B again, health; the avatar gets DNA.
* ✔ Hanging fruit are not lasting objects: the flora makes and frees their cFruit as it likes (the pointers
  near a resting avatar changed within minutes, the serials differ between games: 4013.. on the host, 4679..
  on the guest). Their truth is the tree: cTreeData +0x48 / +0x4C, a u64 mask of the fruit left (bit per slot:
  0x07 small plants, 0x1F big trees on the fixture). The same trees and masks on both games after a load.
* ✔ A fruit falls with `Flora_DropFruit` 0xB09AE0 `thiscall(flora, tree, slot, make_fruit) ret 0xC` (flora =
  `Flora_Get`): nothing when the slot's bit is clear, else 0xB09790 clears it and makes a falling cFruit
  (state 1) at the slot. The creature's tree bump 0xD38DD0 calls it for a hanging fruit it hits. Runtime: a
  drop on the host clears the bit and puts a fruit on the ground 1.1-1.3 m from the trunk; the same drop on
  the guest puts its fruit within 0.2 m of the host's.
* ✔ Fallen fruit from the save (state 2) are at the same places on both games (serials 317, 2561, 2562 on
  both); fruit that fell later fell in one game only (each game's flora and creatures drop their own): the
  detector's `unmatched:fruit`.
* ✔ **Interactive ornaments** cInteractiveOrnament (noun 0x3A2511E, size 0x260): from the save, the same
  objects at the same places with the same serials (2555..2568 on the fixture). +0x110 a sort (2 bones /
  skeletons with a part, 3, 6 pushable like a rock (0xD3A0D0), 7), +0x108 the creature using it, +0xA3 can be
  used, +0x248 / +0x24C..+0x250 the index into a list of 12-byte models (empty on the fixture: -1).
* ✔ The interact node 0xD869B0 walks the creature there, plays animation 0x3DF6DFF, calls
  `cInteractiveOrnament_NextModel` 0xC40360 (the next model; 0 at the last), clears +0xA3 and posts
  0xD3353638 {creature, ornament, 0} through `cCreatureModeStrategy::ExecuteAction`. The listener
  CreatureStageRewardHandler (vtable 0x147AD48, slot 0x18 = 0xD47860, one of strategy +0xBC..+0xC0) finds the
  reward with `CreatureReward_Find` 0xD475E0 `thiscall(handler, msg, obj) ret 8` (rewards made from the
  model's property 0x3D03B03 at the first lookup, per object) and pays it with 0xD46BE0 when +0x08 is 0 or
  the creature is the avatar.
* ✔ The reward {+0x04 kind (6 a part), +0x08 avatar only, +0x0C trigger (0xA07DDE8F any), +0x10 uses left,
  +0x14 part level, +0x1C effect, +0x20 event, +0x24 used up}. Runtime (lane 9): the 5 ornaments of sort 2
  near the fixture's avatar have a part reward (level 1 or 2, one use); sorts 3, 6, 7 none. A use paid one part
  (54 -> 55), +0x10 1 -> 0, +0x24 0 -> 1; a second use paid nothing. **The ornament itself does not change**
  (no next model on the fixture's bones): the used state lives in the reward only.
* ✔ So BUG-0459: each game has its own reward per bone; with parts.shared (the union) a bone used in both
  games unlocks two different parts for everybody (lane 9, switch off: 54 -> 56 on both games).
* ✔ **Extinct nests** (BUG-0460): cSpeciesRelationshipManager::ApplyEvent's "species extinct" 0xBA4750 (GOG
  2024; the 0xBA4730 above is its GOG 2016 neighbour): achievement, relation 6 via 0xBA5F10, and
  `cHerd_SetExtinct` 0xC6AED0 on every herd whose +0xA4 species is the dead creature's: +0x190 mbExtinction
  = 1 and the nest (+0x160) +0x120 = 1 (the extinct nest on the minimap, ~ inferred from the name). The guest
  writes the host's relationship table (social.cpp) but never runs 0xBA4750, so its herds and nests stayed
  alive. Other writers of +0x190: 0xC70160 (a setter, called from scenario code 0xFE0150 / 0xFF96B0).
* ✔ **Eggs in Tribe** (BUG-0474): the animal nests of the Tribe stage are the same cHerd / cEgg; the guest's
  game removes its animals there too (tribe proxies), so no layer, no eggs; the nest.cpp mirrors work there
  unchanged (the guest's own herds of the host's nests are found by home), centred on the player tribe when a
  player's position is the (0 0 0) of an RTS pointer. Runtime: 5 host eggs, 5 guest mirrors at the same places.
* What worldobj.cpp does with it: the host sends per kind the objects near each guest (1 Hz, unreliable,
  MSG_WOBJ); the guest binds them to its own by position (fruit 3 m, tree 0.5 m, ornament 1.5 m, herd 1 m) and
  applies the host's state with the game's own function (fruit eaten: SetState 5; tree: DropFruit of the
  slots the host lost; ornament: reward +0x10 = 0, +0x24 = 1, +0xA3 = 0, no reward; herd: cHerd_SetExtinct).
  Loose fruit of the guest's own (no host partner for 3 snapshots) are eaten away (SetState 5). What a guest
  player does to a bound copy (ate it, made a fruit fall, used a bone) goes to the host as a request; the host
  does the same to its object (no reward: the guest had it) and the next snapshot carries it to the others.
* ? Not known: whether trees grow fruit again (a host's new fruit cannot be given to the guest: no set-bit
  path is used); what the game shows on a used bone (no visual change seen); who else rewards ornaments in
  Tribe (no cCreatureModeStrategy there: the ornament kind is Creature only).
## NPCs that fight a creature which is destroyed (2026-11-03, fix/w40-crashes)
<a id="creature-lost-target"></a>

Player reports BUG-0453 (host crash 0xD65D23, read 0, twice) and BUG-0456 (host abort 0x40000015 at
0xD8F2AD). GOG 2024 addresses; static analysis in the private Ghidra project, runtime on lane 2
(`coop_npc_lost_target.py`).

* ✔ The creature attack node is BT id **0x02D852E6** (action table .data 0x1583F30: id, enter
  0xD67980, tick `SporeCoop::BT_AttackTick` 0xD67CD0, exit 0xD672A0). Enter takes the target from the
  "attacked" stimuli (0x200 / 0x400 senders, 0xD998E0 / 0xD99930) or its argument and calls the
  creature's **SetTarget** vt 0x84 (0xC0EEC0 / 0xC03DF0: thiscall(target, bool, int), the target is
  stored at **+0xE7C** as the target's combatant (creature +0x5A8), AddRef vt 0x60 / Release vt 0x64).
* ✔ The tick reads the target (0xC0EFC0 = `[c+0xE7C]`). Its own check for a missing target (0xD65980:
  null -> 1, the node ends with 0) is only called when the node's attack animation (node data +0x30)
  is finished (`*(data+0x30) == 0 || IsAnimDone 0xC0E420`). While it plays the tick goes on and
  0xD65BE0 (data +0x34 / +0x38 / +0x3C: other animations; game mode != 0x1654C10; `[c+0xB4C]+0x600`
  set) calls `target->vt 8` on null at **0xD65D23** (`mov edx,[ebx]`, ebx = the target argument).
  Both player dumps: ebx = 0, the node's tick on the stack (0xD680B2), cinematic F0428C29 playing.
* ✔ **The game clears the target of every NPC aimed at a creature it destroys** (lab: an NPC in node
  0x02D852E6 with the partner's puppet as target; the puppet destroyed by the guest's death: +0xE7C = 0
  within 0.2 s, the node changed to 0x02D852F3). An NPC whose attack animation plays at that moment
  then faults at 0xD65D23 in every tick (lab, fix off: 135 faults at 0xD65D23 with the players' call
  chain 0xD680B2 <- 0xD8F463 <- 0xD8B88A <- 0xD7D942 <- 0xBC8B44). The vanilla game never destroys a
  creature in a fight (corpses stay 60 s); co-op destroys a partner's puppet when the partner dies, is
  hidden, leaves or rejoins (both player crashes came ~4 s after the guest's death and the puppet's
  removal, 50 ms after the host's own respawn).
* ✔ **Stimuli outlive their sender**: BT_PostStimulus 0xBC9960 keeps a reference to the sender
  (slot +0x2C); the game's clear is `SporeCoop::BT_ClearStimulus` 0xBC9A60 (thiscall(stimuli, lo, hi)
  ret 8). After the puppet's destroy the NPC's "noticed" stimulus 0x4000000 still named it for 4 s
  (the object's vtables still cCreatureAnimal 0x146A080 / combatant 0x1469E30 in the lab), 28-37 stale
  senders over 6 removals in one run.
* ✔ BUG-0456: the herd-help node 0xD8F1C0 (`SporeCoop::BT_HerdHelpEval`, likely) takes the sender of
  the "attacked" stimulus 0x200 (0xD998E0), checks +0x135 and vt 0x2C, then calls its combatant
  `[c+0x5A8]` vt 0xC(0x18EB45E) at 0xD8F2AB. In the player's dump the object (ebp) had the root base
  vtable 0x13EB92C (slots 0 and 1 `_purecall` 0x11E0710): it had been destroyed; the call went to
  msvcr90's _purecall -> abort -> 0x40000015 (the context's eip is the return address 0xD8F2AD).
  **Who calls abort: the CRT's pure-virtual-call handler (R6025)**, reached through the destroyed sender.
* Fix (mods/coop/gamefixes.cpp): `npc.attack_lost_target` hooks BT_AttackTick: no target -> return 0
  (what the node's own check does); `npc.stale_sender` hooks `cGameNounManager::DestroyInstance`:
  before a creature (0x18EB45E) or citizen (0x18EB4B7) is destroyed, every creature's and citizen's
  stimulus naming it (sender inside [obj, obj+0x1700)) is cleared as 0xBC9A60 does (masks, value,
  sender released) and the union of the rest rewritten. Any destroy, ours or the game's.
* Test tools: `coop.puppet respawn [s]` (host: every partner's puppet goes now, shown again after s
  seconds), `coop.attackcut` (the next NPC in its attack animation has its target cleared by vt 0x84
  (null), as the destroy does).

## Sneak: the creature's state and the partner's puppet (2026-11-03, fix/w40-creature-acts)
<a id="creature-sneak"></a>

BUG-0463. GOG 2024, Ghidra (out/ghidra-2024) and lane 9 at runtime (creature fixture, `coop.sneak`).

* ✔ `CreatureSetSneaking` 0xC1B030 `thiscall(creature, bool on, bool force) ret 8`: on: without force it returns when
  the creature has no ability of type 0xD or 0x52 (`GetAbilityIndexByType`); sets +0xB80 (noise radius) = 0, +0xBB1 = 1
  and plays the procedural sound 0xC881120E at the creature. Off (only when +0xBB1 is set): +0xB80 back (the avatar,
  +0xB58 bit 0x200: from its sneak ability via 0xC19900; others: the constant at 0x150C8C0, 6.0), +0xBB1 = 0, sound
  0x529FEC69. 30 callers (TakeDamage ends a sneak, the creature-stage action handlers 0xD6xxxx / 0xD8xxxx).
* ✔ 0xC0C290 (fastcall) returns +0xBB1: NPCTickAI calls it 4 times (the notice step, docs/re/creature.md 14.2),
  cCreatureCitizen::Update and the stage's UI code too. 0xC11730 (a vtable slot, 0x146A650 / 0x146ADC0) returns false
  at once for a sneaking target (a sight / distance check).
* ✔ The sneak's look: `0xC1CF30` (called from cCreatureAnimal::Update 0xC0AE30 and cCreatureBase::Update every frame,
  for puppets too) eases +0xB7C towards 0.2 while sneaking (1.0 else) and writes it to the model (+0xB54) +0x80, unless
  +0xB5E or +0xB58 bit 0x800; with the sneak ability it consumes energy (+0xE58, ability +0x104 per ms) and ends the
  sneak at 0 energy. Runtime: the fixture's creatures have energy 1000; the alpha reached 0.2-0.26 within 2-5 s of the
  sneak on both the avatar and the puppet.
* ✔ The guest's fixture creature has no sneak ability (types 0xE 0x1D 0x28 0x10 0x25 0x1E 0x31 0x32 0x33), the host's
  has 0xD (index 7): the guest's sneak in the test uses the force flag.
* ✔ Before the fix the partner's puppet kept +0xBB1 0, noise 6.0, alpha 1.0 while the partner sneaked: nothing of it was
  sent. Now PlayerState flag PF_SNEAKING (modes.cpp) and `CreatureSetSneaking(puppet, on, 1)` on the receiver
  (`creature.sneak`); the puppet's energy is kept above 0.
* ~ Inferred, not run as a behaviour test: the host's NPCs with a guest's puppet swapped in as the avatar (npc.cpp
  ai_target) now get noise 0 and the sneak flag from the puppet, as from a sneaking host avatar.

## Relations to a player of another species, proxies without tuning (2026-11-03, fix/w40-creature-acts)
<a id="creature-relations"></a>

BUG-0462. GOG 2024, Ghidra (out/ghidra-2024) and lane 9 at runtime (`coop.social rel`).

* ✔ `GetRelationship` 0xBA40E0 `thiscall(mgr, key*, a, b) ret 0xC` in the Creature stage (mode not 0x1654C10): when
  neither a nor b answers creature vt 0xD0 (`cCreatureBase_IsPlayerSpecies` 0xC0C430: +0xB20 == cSpeciesManager
  PlayerSpecies 0x4DF490), it returns the **diet / size rule** 0xBA2900(a, b) (6 same species, else 1-4 by herbivore /
  carnivore and a size class from 0xC0BA40 against 0x156C194 / 0x156C198). Else: the species record (map +0x24, node
  +0xC) when not 0, else the tuning +0x390 of the one that is not the avatar (0xC0C300 = +0xE84), else the rule.
  CreatureSocialMode 0xC0E800 calls it with (avatar, npc); vt 0xD4 0xC0C3B0 ("friendly") with the same.
* ✔ So a guest playing its own creature (another species in its own game) got the rule for every NPC: on the fixture
  4 instead of the tuning's 3 for 12 herbivores (social mode 1 instead of 2). Fixed by `creature.relations`
  (creature_hp.cpp hooks vt 0xD0: the own avatar's and the puppets' species count as the player species).
* ✔ The archetype tuning: `cCreatureAnimal::Create` 0xC09B40 sets it with `cCreatureBase_SetTuning` 0xC0C2E0 (+0xE84,
  +0xE80 = tuning +0x10) from `cSpeciesManager_GetTuning` 0x4E00C0(mgr, key, variant) (the player species: key
  0xAED08DD4). The tuning is the value of a map node: the dword before it is its key, the same as the herd's archetype
  +0x88 (lane 9: tkey B5BC62BF = arch B5BC62BF on the host).
* ✔ A proxy made in a herd of ours (archetype 0: the species' nest herd not found on the guest yet) got no tuning (lane
  9, the first proxies of species 06805133 after a join); its relation then came from the rule: 1 (hostile) where the
  host's NPC had 3 (neutral). Made again a minute later, the same proxy went into the game's herd (arch B5BC62BF) and
  had the tuning. Fixed by `npc.tuning` (the host's key after MSG_NPC_SPAWN, the guest sets the same tuning).
* ~ Inferred: on the host the NPCs' AI with a guest's puppet as the avatar (npc.cpp ai_target) used the rule too; the
  hook covers the puppets' species there.

## Carried objects, herd evolution, NPC animations on the guest (2026-11-03, fix/w40-creature-acts)
<a id="creature-npc-anims"></a>

BUG-0458, BUG-0464 (both open). GOG 2024, Ghidra (out/ghidra-2024) and lane 9 (creature fixture). Findings only.

### Picking up and throwing (BUG-0458)

* ✔ The Creature stage's cursor choice 0xD311B0 asks 0xD30830 -> 0xD2FFD0 (the object under the mouse) for a cursor
  kind: 0xB / 0x19 load cursor 0x3B360DC ("cursor-crg-pickup", the strings are loaded in 0xD366C0), 0xD / 0x16 cursor
  0x3B360E4 ("cursor-crg-throw"). 0x19 / 0x16 are returned for an object of noun **0x2A8FB3F (cRock)** with interface
  0x1186577 (+0x50 bit 0x100, +0x70) when 0xC13DC0(obj, 0, ..) says it can be taken; 0x1A for an ornament of sort 2.
* ✔ The carry behaviour (a BT node without a direct caller, 0xD84120): walks to the object (WalkTo), plays animation
  0x39E4C77 (or 0x39E4C77 + 0x1597E79), and at the end, by the object: a fruit (0x2C9CC91), a cRock (0x2A8FB3F) or an
  interactive ornament (0x3A2511E) of **sort 3** (the stick, ~ inferred from the sort list of
  docs/re/creature.md#creature-world-objects) posts BT stimulus 0x40000 (value 60) with slot +0xC = 1 (carrying).
  0xD3A0D0 (a creature bumping into an object) pushes cRocks and sort-6 ornaments.
* ✔ The fixture has one cRock (census noun 0x2A8FB3F = 1, vtable 0x1473228) and 12 ornaments, 5 of them sort 2.
* Not done: the carried object follows the carrier's hand in each game separately, and a throw moves it with the
  physics of the game that threw it. A fix is a host-owned world object with a carrier (worldobj.cpp: a kind for
  cRock and sort-3 ornaments with pos / rot / carried-by at 10+ Hz, the guest's carry as a request) or the carry
  stimulus relayed to the partner's puppet. Neither tried.

### Herd evolution (BUG-0464)

* ✔ cHerd's serialized fields (descriptor 0x134D380): `mGeneration` +0xF0, `mEvolvedSpeciesProfileKeys` +0xA8 (a
  vector of species keys), `mDNAEvolutionThreshold` +0x94, `mbShouldEvolve` +0x85, `mbBestFriends` (after it).
  cHerd::Update 0xC6D960 reads the archetype tuning with `cSpeciesManager_GetTuning(herd +0x88, herd +0xF0)`: the
  generation is the tuning's variant. ~ Inferred: a nest "evolves" by a new generation / species key from the list;
  the guest's herds are not simulated (its NPCs are removed), so its nest never does.
* ✔ The NPCs' own animations do not reach the guest at all: sampled twice a second for 40 s (coop_npc_anims.py,
  `coop.npc cur`, vt 0x58 GetCurrentAnimation), the host's 21 NPCs played B31EB92C (760 samples), 731EA3EA, 0519E154,
  042A1CAF, 02481DE5, 05807346, ...; their proxies 02481DE5 (1444 of 1617), the death F14FAA46 and 04866Dxx. The proxy
  showed the host NPC's animation in 7-9 % of the samples (lab runs 20261103-153901, 20261103-154027).
* ✗ Tried and reverted: relaying the host NPCs' cCreatureBase::PlayAnimation / PlayAnimationTo calls (the hooks of the
  players' MSG_ANIM) to the proxies: 287 calls in 40 s (171 more over a rate limit of 80/s), 279 played on proxies, and
  the proxies' current animation did not change (7 % before, 9 % with it). So the behaviour animations (B31EB92C ...)
  are not started through those two functions, or the proxy's driving replaces them at once. Next: find the caller
  of the AnimatedCreature (Anim::AnimatedCreature::PlayAnimation 0xA0C5D0) for B31EB92C, or replicate the current
  animation as state (host: GetCurrentAnimation per NPC; guest: play it when the proxy's differs).
* CORRECTION (2026-11-03, fix/w40-npc-anims): the behaviour animations are started with SporeCoop::CreaturePlayAnim
  0xC122F0, not through PlayAnimation / PlayAnimationTo; relayed through it they stick on the proxies (79-82 % of the
  samples): docs/re/creature.md#creature-bt-anims.

## The sea monster (the players' "kraken", 2026-11-03, fix/w40-creature-acts)
<a id="creature-sea-monster"></a>

BUG-0465 (open). GOG 2024, Ghidra strings only, not seen at runtime.

* ✔ There is no "kraken" / "monster" creature or noun in the exe's strings; the game's sea monster is
  `cSeaMonsterEvent` (class name strings 0x147AEEC / 0x147B2F8, used by 0xD4AE00 and 0xD4C4D0; the component path
  "SP_Simulator/cSeaMonsterEvent/COM" at 0xD4D450). The avatar's death of cause 9 plays the cinematic
  `CRG_AvatarDied_SeaMonster` (0xD38E60, called by 0xD401E0; cause 5 starvation, others CRG_AvatarDied).
* ✔ Known before (docs/re/creature.md#creature-deep-water): an avatar over deep water starts caption cinematic
  5E205931 ("Dangerous creatures lurk in the deep").
* ~ Inferred: the monster is an avatar event (the player's own game makes it for its own avatar, as its death
  cinematic), not a world object: the partner's game has no such event for a puppet, so it never shows. Class B of
  docs/sync-architecture-plan.md (the game knows one player). A fix would relay the event (or the death cause 9) to
  the partner and play the monster there at the puppet; the event's object and how it is drawn are not analysed.

## Captions in play: the chat line, the brain upgrade, the jumps after a death (2026-11-03, fix/w40-play)
<a id="creature-chat-under-caption"></a>

* ✔ A click on a text line of the co-op window (the chat line) under the game's input lock (any caption,
  #creature-input-lock-clicks) is dropped like the button clicks: the line never takes the focus and
  the typing is lost (BUG-0482; coop_chat_caption.py with CRG_ClaimNest, both roles, out/lab/20261103-151853).
  The WM_CHAR messages still reach the game window's queue (a WH_GETMESSAGE hook sees them, mouse.cpp
  `take_chars`); the mod types them into the clicked line itself (`core.ui_locked_edit`, coop_ui.cpp).
  `locked_typed=` in `coop.uistate` counts the characters it typed.
* ✔ The brain upgrade at 100 DNA plays cinematic `F6A4B1E1` with the caption window (control id 0x0445C9AB)
  "Brain power! You've grown a little smarter and taken your first step towards sentience! ..." on both
  games (the DNA is shared); it holds the input lock and the game time stands until Enter / OK. At the
  fixture's nest the own creature's height over the ground did not change under it (0.32 / 0.39 m before
  and during, lane 6 probe 2026-11-03). play_creature's bots did not answer it and stood in it for the rest
  of the run (BUG-0500: no hunts from s 290, `+0 joint hits`, every chat line typed under its lock).
* ✔ After a death the partner's own game moves its creature at once twice: the respawn at the nest (while
  the puppet is hidden: dead) and, ~10 s later (the guest's hatchling; the host's waits for its family), the
  hatchling stepping out of its egg: 5.6-6.0 m in one frame at the fixture's nest (lane 6 probe and
  coop_puppet_jump.py), 10-11 m in play (out/lab/20261103-124913 play_creature#3: the guest's own position
  (-171.8 -467.8 -84.0) -> (-175.1 -464.2 -94.2) between two trace lines). The puppet's buffer blended across
  that step (the target slid the whole way in 0.1 s) and the puppet walked after it: 0.29-0.30 s for 6 m,
  6-23 m off on the sample play_creature took then (BUG-0499, `never more than 6 m apart`, puppet speed 0).
  The respawn blended from the dead state at the place of the death (no PF_IN_WORLD change) to the nest.
  `creature.puppet_jump` (modes.cpp, net::SnapshotBuffer::jump_speed 40 m/s, the PF_DEAD flag): no blend
  across those steps, a target that moves over 3 m in one frame teleports the puppet (`jumps=` in
  coop.status): 0.03 s for the same 6 m, and the puppet shows again 0.00 m from its target (0.35 m off).

## Plants and rocks are made by each game around its own camera (2026-11-03, fix/w40-cell-evo)
<a id="creature-plants-rocks-lod"></a>

BUG-0473 (plants, rocks), BUG-0498. Lane 4, `tools/tests/coop_world_lod.py` (out/lab/20261103-161705).

* ✔ (runtime) `rock` (noun 0x2A8FB3F, vtable 0x1473228): the fixture's save has 1 rock, in both games' lists
  at the same place. 28 more appear in the walking guest's list only (30-75 m ahead of it at the first step,
  then where it walks), never in the host's list although the host stood 1 m from the guest at the start;
  they stay in the guest's list while it walks 200 m away and back. Serials differ per game even for the
  shared rock.
* ✔ (runtime) `game_plant` (noun 0x18C84A9, vtable 0x146C2A8): 0-2 objects in a whole list, each 8-14 m from
  that game's own player, never in the other game's list. Its factory is registered at 0xC83B0B; who places
  them was not looked up (~ inferred: decoration near the camera, like the hanging fruit of
  #creature-world-objects).
* So neither is a world object the players share: no worldobj kind. The detector leaves them out
  (tools/tests/worlddiff.py IGNORE, docs/06-testing.md#testing-world-lod).
## How the behaviour tree starts an NPC's animations (2026-11-03, fix/w40-npc-anims)
<a id="creature-bt-anims"></a>

BUG-0464. GOG 2024, Ghidra (out/ghidra-2024) and lane 9 at runtime (creature fixture, `coop.npc cur`, `coop.npc anims`).

* ✔ The behaviour animations are started with `SporeCoop::CreaturePlayAnim` 0xC122F0 (thiscall(creature, anim, int a,
  int b) ret 0xC, returns the animation's handle, 0 when not played), not with cCreatureBase::PlayAnimation 0xC12470
  (that is why the PlayAnimation relay of fix/w40-creature-acts changed nothing). ~150 callers, the BT nodes
  0xD6xxxx-0xDBxxxx among them. Example: the idle / look-around node 0xD8DC30 (state in its node data +0, handle +0x10:
  it waits while the handle plays, 0xC0E420): carnivores B31EB92C, herbivores 731EA3EA, every third step
  0519E154 / 731EA494 / 051C689B / F31EA59C, a social variant 0x42A1CA0.. + RandomInt(species +0x5E4..0x5F0) by the
  species' kind +0x620 (0x22-0x25); with a target creature 0xC14AA0 (CreaturePlayAnim + a look-at 0xC0B670).
* ✔ The idle refresh `SporeCoop::Creature_IdleRefresh` 0xC148B0 (thiscall(creature, bool force) ret 4) plays the choice
  of Creature_ChooseIdleAnim 0xC0C790 through 0xC146F0 (mode 0: CreaturePlayAnim(anim, 1, -1), 1: PlayAnimation,
  2: a second layer) when it differs from the current animation. Creature_ChooseIdleAnim returns the BT's idle override
  +0xE3C first (set by 0xC0B4D0, -1 = none, reset in 0xC08210 / 0xC1FFE0). cCreatureAnimal::Update 0xC0AE30 calls the
  refresh only when no main animation is left (0xA02AE0 < 1), and ~120 BT nodes call it at their end.
* ✔ So a proxy (no NPCTickAI) keeps an animation played on it until it ends, then its own game picks idle / walk:
  the relay sticks. Runtime (lane 9, 40 s, 21 NPCs): the host's NPCs started ~11 behaviour animations a second
  (seen 355-627 per run, idle refresh plays 337-912 not counted); on the guest 93-100 % of them played.
* ✔ Fix `npc.anims`: the host hooks CreaturePlayAnim and PlayAnimation / PlayAnimationTo outside the idle refresh (a
  depth counter around 0xC148B0) for its NPCs and sends them once a frame (MSG_NPC_ANIM 59 kind 0, 12 bytes each);
  once a second it sends what each mirrored NPC plays now when that is its last BT animation or it is dead (kind 1,
  6 bytes each); the guest plays a snapshot's animation when the proxy plays another, not within 1.5 s of a start and
  at most every 2 s per proxy (a proxy made after the start, a lost message, an animation cut). Samples where the
  proxy plays the host NPC's animation: 15-22 % off, 79-82 % on; an NPC attacking the guest's puppet (attack node
  0x02D852E6, attack animations 044DDEA6, 026EDE51, 035EE245): 0 % off, 83 % on. The rest are phase differences of
  the 0.5 s sampling (a pair seen both ways: 0519E154 / B31EB92C).
* ~ A dead host NPC plays 05807346 (a body's animation) where the guest's play_death played F14FAA46: the snapshot
  now plays the host's on the body.
* ~ Not verified: a nest's real evolution (cHerd generation +0xF0, docs/re/creature.md#creature-npc-anims): an
  evolved NPC is a new species (host_tick gives it a new id and spawn), its animations go through the same relay.

## The roar: what is known (2026-11-03, fix/w40-npc-anims)
<a id="creature-roar"></a>

BUG-0501 (open). GOG 2024, Ghidra strings and decompiles only, not seen at runtime. Findings only.

* ✔ The only "roar" string in the exe is the BT blackboard variable `HeardRagingRoar` (0x147C000; "Raging Roar", the
  Creature stage's consequence ability of a carnivore Cell, ~ inferred from the name), registered with `Loudness`,
  `HeardSirenSong` and `WasMother` in the static init 0x13674D0 through 0xBC9FA0 (thiscall(var, float max, init,
  on_end, update, name) ret 0x14: a variable object at a global: Loudness 0x169F0F0, HeardRagingRoar 0x169F134,
  HeardSirenSong 0x169F178, WasMother 0x169F1BC). The roar and the siren song share the end callback 0xD85400 (posts
  BT stimulus (0, hi 0x4000000) with value FLT_MAX, clears stimulus 0x400000, stops effect 0x4F26350B on the NPC);
  the siren song has an update 0xD85470 (counts its value down, keeps it while the NPC's node +0x1D8 is 0x341BD4BE).
* ✔ The globals of HeardRagingRoar / HeardSirenSong are referenced only by that init (`scan`): the BT reads them by
  name / registry index, the setter (the ability that roars) was not found. Loudness is written by 0xD8B290.
* ✔ The avatar's ability use 0xC1E8B0 (thiscall(creature, float) ret 4) posts its hit stimuli (0x2000, or hi 0x80000
  for a citizen) with value 10.0 to the target or to every creature in range (0xD99A20 distance), sender the avatar.
* ~ Inferred: the roar sets HeardRagingRoar on the NPCs around the roaring avatar in that player's own game only; a
  guest's NPCs are AI-less proxies, so nothing reacts and the host's NPCs never hear it. A fix is an L1 request: the
  guest's roar sent to the host, the host sets the same variable on its NPCs around the guest's puppet. Neither
  fixture creature has the ability (types 0xD 0xE 0x10 0x11 0x1D 0x1E 0x25 0x28 0x31 0x32 0x33): a test needs a
  creature from a carnivore Cell.
## A partner's puppet freed by the game, liveness of puppets (2026-11-03, fix/w40-guest-combat)
<a id="creature-puppet-alive"></a>

* ✔ The Creature (and Adventure) adapter had no `alive` callback (Cell and Space had one), so `puppet_now()` and
  `drive_puppet()` never checked that the raw puppet pointer `Remote::puppet` was still a creature: a puppet the
  game destroyed by itself was driven on every frame (BUG-0507: a guest crash in set_speed, the memory reused by
  a cModelObject). `creature.puppet_alive` adds `creature_alive` (modes.cpp): the game's object list holds the
  pointer as a live creature (noun 0x18EB45E, +0x21 = 0) and its cGameData serial (+0x24) is the one recorded at
  the spawn; checked once per frame and puppet (cache by `frame_index`). A dead one is dropped from the puppet
  list (`puppet_unregister`, a new creature at the address is no puppet) and drive_puppet makes a new puppet.
* ✔ Lab (lane 2): cGameNounManager::DestroyInstance on a puppet (`coop.puppet gamekill`) takes it out of the
  object list at once; the memory was not reused within 4 s and driving the dead creature did not fault (with
  the switch off the mod kept the dead pointer, the partner was gone from the screen for good).
* ✔ Negative: two device Resets (Alt+Enter on the guest) do not free or replace the partner's puppet.
* ? What freed the puppet on the player's PC is not known: the log has no hide / despawn line of ours (the puppet
  was shown, d=7 m, 30/44 hp, a cinematic had run 40 s before), then a 4.1 s hitch, a device Reset and the crash
  0.6 s later. Candidates not checked: the game's own cleanup of a creature after a frame of 4 s (fell out of the
  world?), a cinematic's actor cleanup. The check covers any of them.

## The host's pack and a partner as its target (2026-11-03, fix/w40-guest-combat)
<a id="creature-pack-target"></a>

* ✔ A left click on a creature sets the avatar's target **+0xE7C** to that creature's combatant (+0x5A8): a click on
  the partner's puppet on the host gave avatar +0xE7C = puppet + 0x5A8 (lane 2, 3 times).
* ✔ Negative: neither that click, nor 10 PlayAbility calls of ability 0 or the key 1 after it, made the recruited pack
  member take the puppet as its target in 15 s (its +0xE7C stayed 0, node 0x02D852F6). The pack's "help the leader"
  trigger is not found.
* ✔ SetTarget (creature vt 0x84, `thiscall(target, 1, 0)`) takes the target's **combatant** (creature + 0x5A8), not
  the creature: called with the creature pointer the host game hung or died at once (lane 2). With the combatant the
  pack member held +0xE7C on the puppet for good (node 0x02D852F6, no bite in 10 s).
* ✔ The players' host log (0.7.3.6): during that fight the pack's hits on the guest's puppet arrived every 0.3 s and
  were all dropped by `npc.party_peace` (`hit by pack posse=1 amount=0.0 dropped`); the hits that reached the guest
  were from other NPCs (`hit by npc posse=0`, forwarded). So on 0.7.3.6 the pack fought the guest on both screens
  without damage; the guest's losses in that fight came from the wild NPCs.
* `npc.pack_spares_partners` (npc.cpp, BUG-0512): on the host every frame, a pack member (the pack list
  cGameNounManager +0x5C..+0x60: it is not always in the creature update list) or family member aimed at a partner's
  puppet gets SetTarget(null, 1, 0) and its stimuli naming the puppet cleared.

## A stalled partner drawn under the ground (2026-11-03, fix/w40-guest-combat)
<a id="creature-partner-stalled"></a>

* ✔ Players' logs (0.7.3.6, BUG-0513): the guest's own trace showed its creature at agl=-6.53 (6.5 m under its own
  ground) while `busy=paused` during a 12 s stall (receiving a world), then 0.31 after; the host drew the puppet at
  agl -6.32 and later -3.2 (states arriving), then hid it after 5 s without states (`state older than 5 s`).
  `creature.puppet_ground` (BUG-0367) leaves a target more than 3 m under this game's ground where it is.
* ✔ Lab (lane 2): with the guest's states 6 m under the host's ground (`coop.track lift -6`) a standing puppet stays
  on the ground (the game's own grounding); a walking one is drawn under it (144 frames, switch off).
* `creature.puppet_deep`: a target 3-15 m under this game's ground goes onto it (`drive::out_of_ground`, as the
  proxies, BUG-0399). `core.partner_silent`: "<name> is not responding (game minimized or frozen?)" once when a
  partner's states are older than 5 s, "<name> is responding again" when they come back (coop.cpp silent_tick).
* `frame.limit 9000` on a game stalls it in the lab (a frame every 9 s, no states between them): the hitch gate and
  the monitor's "hidden" rule see it.
* ? Why the guest's game holds its creature under its ground while paused or stalled is not known (inferred: the
  terrain of a world being received, or the creature not grounded while the game does not update).

## The party's DNA pace: each gain counts 1/N (2026-11-03, fix/w40-dna-social)
<a id="creature-dna-pace"></a>

BUG-0510 (players: "3-4x the DNA of a solo game", Cell and Creature). Code: `mods/coop/dna_pace.h`,
coop.cpp `dna_pace_gain` / `dna_pace_meal`, cellsync.cpp `h_AddFood`; test `coop_dna_pace.py`
(suite `dna_pace`, `dna_pace_cell`).

* ✔ observed (lab 20261103-180306, `SPORE_FIX_OFF=core.dna_pace`): two players' gains of 20 each
  (AddEvolutionPoints, the game's reward call) made the shared total grow by 40 on both games; Cell: two
  meals of 20 (AddFoodPoints) +40 food on both (lab 20261103-180539). The party's total is the sum of
  every player's progress, so N players evolve about N times faster than the game is tuned for.
* Design (orchestrator's decision): one shared total stays; every GAIN entering it is scaled by 1/N,
  N = the host plus the guests whose latest state has the host's mode and world crc (`pace_playing`,
  a guest in an editor or a load does not count while it is there); spends (< 0) stay whole.
* Each gain is scaled once, where its game made it: the host's own AddEvolutionPoints (its kills,
  meals, friends, a guest's kill credited to the host's avatar) in `on_dna`, a guest's forwarded gain
  before MSG_DNA_ADD (the host adds it whole), a guest's social win in `social::apply_success`, an
  own Cell meal in `h_AddFood` on the eater's game (the guest applies and sends its share, so its food
  bar does not jump up and back). Integer food points keep the remainder (`dna_pace::Carry`).
* Not scaled: a species goal card's reward (paid once per party, `social::goal_dna`), the owed / solo
  DNA paths (already scaled when they were forwarded, or earned alone while the host was away: N = 1
  then), the guest's own world (core.world_dna), and the test setup commands `coop.dna add` /
  `coop.pool food` (exact N; `coop.dna gain` / `coop.pool meal` are the paced reward).
* The host decides: `coop.ini [coop] dna_pace` (1 default, 0 = the full sum; `coop.dna pace 0|1`).
  It announces the divisor in an optional last byte of MSG_DNA (after the causes) and of the host's
  MSG_CELL_FOOD total (after the ack); older guests stop reading before it, a newer guest of an older
  host reads none and uses 1 (the full sum, as that host does).
* The host's open Cell editor (report 2, "+68 from the guest's meals" 7 s after the editor request):
  ✔ (Ghidra, #cell-editor-dna in cell.md) the editor's budget is computed once, at the request
  (0xE59800: DNAForFood(S+0x1C) - S+0x2C), and the meals are those that arrive during the ~10 s mating
  cinematic before the stage is left (the host is still in Cell, so they are applied); in the editor
  itself a guest's meal is refused (BUG-0239). Decision: nothing more is held; such meals go into
  the food total and count at the next editor, now at 1/N. ~ inferred, not checked at runtime: the
  85 DNA on the screenshot is the budget of the next request (or a negative "spent",
  cell.md#cell-editor-negative-spent).

## The guest's social game by the ability bar's buttons (2026-11-03, fix/w40-dna-social)
<a id="creature-guest-social-bar"></a>

BUG-0518 (from BUG-0179's reports). GOG 2024, Ghidra (`tools/ghidra/q.sh --build 2024`) and lane 4.

* ✔ (Ghidra) every ability use of the HUD goes through `SporeCoop::AbilityBar_Use` 0xD49560
  (cdecl(int ability index of the avatar)): the keys (OnKeyDown 0xD3AFF0 -> AbilityBar_OnKey 0xD4A450 ->
  AbilityBar_FindByKey 0xD4A320 -> 0xD49560) and a click on a bar button (`AbilityBar_OnButton` 0xD497D0
  (window id): 0x4C61B50 the social stance, 0x4C61DD9 the combat stance, else the button's slot via
  0xD48B70 -> 0xD49560). Inside: 0xD49220(index) is a status (0 = usable -> 0xD48AB0(index) uses it, else
  a hint: 2, 3/4, 6, 8, 10-15 with UI::cHintManager); it also remembers the last social (+0xE74) and
  combat (+0xE70) ability index of the avatar.
* ✔ (runtime, lab 20261103-181651) a guest's click on the sing button (1440x1080: 550,1020, the first
  button of the green social row) next to an engaged mirror reaches 0xD49560 and the game does nothing
  (no NPC session on the guest): no animation, nothing sent. The mod polled only the keys 1-4.
* ✔ (runtime, social probe on lane 4 with BUG-0462 merged) the mirrors near the fixture's nest have the
  host's relation 3 / social mode 2 on the guest; with an engaged mirror the social path impresses
  065CB8BD, 065CBF12 and 065CB8AB in one round each on the host; the own species 065CBE70 (relation 6,
  mode 0) is refused like in the game.
* Fix `social.bar_click`: the hook of 0xD49560 takes a social ability (+0x10 == 1) used in the social
  stance next to the engaged mirror like the key (plays it on the avatar, K_ACT to the host).

## Hold slots: how a creature carries an object (2026-11-03, fix/w40-misc)
<a id="creature-hold-slots"></a>

BUG-0458 (open). GOG 2024, Ghidra only (no runtime: the lane was busy with the regression).

* ✔ A creature's carried objects are in a vector of hold slots at `creature+0xE40..+0xE44`, 0x24 bytes each:
  `+0x00` a model socket index (-1: none), `+0x20` the held object.
* ✔ `SporeCoop::Creature_HoldObject` 0xC15B20 (thiscall `(obj, slot, replace)`, ret 0xC) puts an object into a slot:
  attaches it to the socket (0xC120B0, the socket transform of `creature+0xB54 -> +0x17C -> +0x2E4 + idx*700`), aligns
  it by the object's properties 0x670F1AF / 0x7E9994D (degrees), then 0xD7D1A0(obj): `obj+0x50 |= 0x100`,
  `obj+0x77 = 0`, the object's physics component (0x116DD1B) `+0x268 = 0`, `+0x1F0 = 3`; finally `obj+0x71 = 1`
  (held). `SporeCoop::Creature_ReleaseObject` 0xC15EB0 (thiscall `(obj, a, b)`, ret 0xC) finds the slot and
  releases it through 0xC15D30(slot, a, b).
* ✔ The NPC carry node 0xD84120 (state block: `[0]` phase 0 walk / 1 pick-up animation / 2 done / 3 give up,
  `[6]` the creature, `[7]` the object, `[9]` the animation) calls ReleaseObject(obj, 0, 0) and HoldObject(obj, 0, 1)
  in phase 1; the stimulus of phase 2 is only for NPCs (`+0xB58 & 0x200` clear). The player's own pickup goes
  through other callers of HoldObject (0xC23460, 0xD7EE10, 0xD87590, 0xF1D4B0: not looked at which is the avatar's).
* Plan for BUG-0458 (not done): hook HoldObject / ReleaseObject on every game; when the own avatar (or a host NPC)
  holds or lets go of a cRock (noun 0x2A8FB3F) or a sort-3 ornament, send it with the object's identity (a worldobj
  kind for rocks and sort-3 ornaments, matched by position on the shared save objects) and let the partner's game
  call HoldObject on the carrier's puppet / proxy with its copy; after a release the host owns the object's pose
  (worldobj snapshot at 10+ Hz while it moves) until it rests. Rocks made around a game's own camera (#creature-plants-rocks-lod)
  are not shared and cannot be matched: only the save's objects can.

## The UFO abduction event of the Creature stage (2026-11-03, fix/w40-gh-issues)
<a id="creature-ufo-abduction-event"></a>

Ghidra, GOG 2024 (BUG-0520, GitHub #194):
* ✔ The visiting UFO is a **Drama Manager event**: class `cUFOAbductionEvent` (factory 0xD4D3F0, 0x74 bytes, ctor
  0xD4C000, vtables 0x147B1D8 / 0x147B058, class id 0x5599EC7, its data class 0x5599EB8). cDramaManager
  (0xD4E020) builds its events from the prop group 0x0EBCFBC7 in Creature (0xE7CA1F54 in Tribe); one of nine event
  kinds (0..8, ids 0x5599D0C .. 0x609EEDF), kind 2 is the UFO.
* ✔ Start (vt+0x30, 0xD4F8A0): in Creature at the avatar's position (Tribe: the player tribe's), height prop
  0x9B0EF346 (default 20 m), "empire ship" chance prop 0x51BE5FAE; `SporeCoop::UFOAbductionEvent_SpawnUFO`
  0xD4C150 = stdcall(const Vector3* pos, float height, bool) ret 0xC: SpawnUFO(**type 7**, ...) placed above pos, in
  Creature `[ufo+0x6C8]+0x5FC |= 1`; the ship goes into the event's map (+0x5C..+0x60, 8-byte nodes, ship at +4).
* ✔ Update (vt+0x40, 0xD4FBA0, thiscall(event, float dt) ret 4): `SporeCoop::UFO_TickBehavior` 0xC388A0 =
  thiscall(ufo, float dt, double now s) ret 0xC runs the ship's behaviour tree (BehaviorTreeTickGeneric); when
  the AI's flag `+0x5FC & 2` is set and no BT state is left, 0x102C350 destroys the ship (it first clears +0x68C of
  every UFO that names it, then cGameNounManager::DestroyInstance). End (vt+0x2C, 0xD4DA90): DestroyInstance on
  every ship of the map.
* ✔ The beam: an abduct strategy's hit (0x10323E0) on a creature (cast 0xCE9F6639) plays the animations
  0x3CC7EAB / 0x3CC7EB4 and gives it a `cAbductionBeamLocomotion` (0x24 bytes, 0x1030E90); other targets
  (0xAEB336B4, 0x283D961) get the same locomotion. `cSpaceGameBehaviorAbducted` (0xA0 bytes, factory 0x1032790)
  is the abducted object's behaviour.
* ✔ Negative (lab, lane 4, 3 runs): a type 7 ship made with UFOAbductionEvent_SpawnUFO over the partner's puppet,
  also ticked with UFO_TickBehavior every frame (60 s each, twice per run), never got a target (+0x550, +0x7A8,
  +0x7AC stayed 0) and took nothing: outside the event its BT does not select a creature (the event's other
  state, or a stimulus it posts, is needed). How the event's ship picks its creature is not found.
* ✔ Negative: `npc ... updated at the planet's centre ... dead=1` (npc.cpp) is not a sign of an abduction: the lab
  prints it for one NPC 58 s into every Creature run with no UFO at all.
* ? Not known: whether the ship can take a partner's puppet (the players' report says a host crashed every time
  it took a creature, with the BUG-0507 signature: inferred, yes) and what the game does with the taken creature.

## Who asks "player species": the scope of creature.relations (2026-11-03, fix/w40-relations-scope)
<a id="creature-relations-scope"></a>

BUG-0462 follow-up. GOG 2024, Ghidra.

* ✔ `cCreatureBase_IsPlayerSpecies` 0xC0C430 has one direct caller (0xC228B0, a thunk) and is otherwise reached as
  creature vt 0xD0 (vtables 0x146A080 +0xD0 = 0x146A150 and 0x146A590 +0xD0 = 0x146A660): every caller is an
  indirect `call [reg+0xD0]`, so Ghidra lists none of them. Known readers: GetRelationship 0xBA40E0 (736 bytes),
  CreatureSocialMode 0xC0E800 (522 bytes), vt 0xD4 0xC0C3B0 ("friendly", via GetRelationship), and the family /
  hatchling / pack / nest code (seen at runtime: with the hook answering true everywhere, a guest's hatchling did not
  step out of its egg after a death, puppet_jump, BUG-0499 test, rel/0.7.4 runs 20261103-202813 fail /
  20261103-204331 pass with creature.relations off).
* What the mod does now: the hook gives the party species the "player species" answer only when its return
  address lies inside GetRelationship or CreatureSocialMode; the cursor / click (0xD2FFD0) and the faces reach the
  relation through those two. Other callers get the game's own answer (`coop.hp`: `other_callers=` counts them).

## The social meter and the win message on a guest (2026-11-03, fix/w41-creature-left)
<a id="creature-guest-meter"></a>

BUG-0179 (meter and message), BUG-0525 (prompts, open). GOG 2024, Ghidra and lane 9.

* ✔ (Ghidra) The BT node "SocialPropose" start `SporeCoop::SocialPropose_Start` 0xDA4360 builds the session (pool
  0xBCB220, session +0x1AC rounds = tuning +0x424, one less for a baby, +1 for an alpha), picks the NPC's prompt
  abilities (npc +0x1020 vector, random from its species profile +0x7A4, their points at species +0x6D4 [i] +0x3C)
  and opens the meter: meter = `[[cCreatureModeStrategy +0x68] +0x60]` (0xD2BA50; the Scenario mode has its own),
  stored at session +0x1DC, then `SocialMeter_Reset` 0xE33CA0, `SocialMeter_SetVisible` 0xE319D0(0),
  `SocialMeter_SetRounds` 0xE31AE0(rounds: meter +0x44), `SocialMeter_SetRelation` 0xE31C70(relation: the face),
  SetVisible(1), hint 0x1EA83595 the first time. The tick 0xDA4950 moves the bars (0xD9B9C0 -> `SocialMeter_SetBar`
  0xE31990(bar 0|1, frac)) from the ability animations' progress, and calls `CreatureSocialSuccess` 0xD9BBD0 when the
  meter is full (`SocialMeter_IsFull` 0xE31A50: +0x26). The win: `SocialMeter_Finish` 0xE31C50, SetRelation, the
  message 0x60B4123 `{avatar, npc, 0}`. The session end 0xD9F7A0 hides the meter (SetVisible 0).
* ✔ SetVisible is `UILayout::SetVisible(meter + 0xC)`; the layout's visible byte is `[meter +0xC +0x14] +0x8C`
  (0x8103B0). Runtime (lane 9): 0 before, 1 while the guest's meter is open, 0 after the close.
* ✔ (Ghidra) What 0x60B4123 does: CreatureStageEventHandler 0xD47FC0 rolls a part reward for the impressed creature
  (adult, another species, its `+0xB58 & 2` clear; chance 0x1582F7C, 0x1582F78 for an alpha) through 0xC0BD60 /
  0xD47E50 / 0xD3D4A0 (the same path as 0x5371F11 and an attack's 0xD335362C) and sets `+0xB58 |= 2` once a part was
  given. So a guest's friends never gave it a part before.
* ✔ (runtime) The meter's bar fields (+0x134..+0x13C for bar 0, range +0x3C / +0x40) stayed 0.0 on the guest while it
  was open for a Kook (one round): the geometry is not laid out within that second, so the bar is not a usable check.
  The meter shows (screenshot of lab 20261103-234410: the arc with the relation face at the top).
* `social.guest_meter` (social.cpp): the guest opens the meter as SocialPropose does when it engages a mirror (rounds
  from the mirror's tuning, the relation face), moves bar 0 to done / needed at each K_PROGRESS of the host, at its
  own win sets it full, Finish, the face and posts 0x60B4123 `{avatar, proxy, 0}` (coop.cpp h_StageMsg gives the
  proxy a blank +0xE84), and hides it when the engagement ends. `coop.social meter` prints the meter's fields.
* ? Not done (BUG-0525): the NPC's prompts. The host has no native session for a guest's game either (it counts the
  guest's rounds, social.cpp host_act), so no prompt list exists anywhere; a fix would let the host pick the prompts
  as 0xDA4360 does (species +0x7A4), play them on the NPC (relayed by npc.anims) and count only the matching answer.

## The roar, second look (2026-11-03, fix/w41-creature-left)
<a id="creature-roar-2"></a>

BUG-0501 (still open, time box of about 20 minutes, Ghidra only). Follows #creature-roar.

* ✔ The end callback of HeardRagingRoar / HeardSirenSong 0xD85400 clears stimulus 0x400000 (lo). Every `PUSH
  0x400000` next to a BT_PostStimulus call was listed (`bytes 68 00 00 40 00`): the two in 0xC23A10 are a creature's
  perception scan (its seen list +0x338..+0x33C, called only by the BT node 0xDAA000): stimulus 0x400000 "sees a
  creature" to adult creatures (+0xB5E == 1) and to citizens in reach; not the roar. The others (0xD69740, 0xD7B620,
  0xD7B690, 0xD8E840, 0xDAA100, 0xDAE910, 0xDBAAF0, 0xDBB7D0, 0xDBC730) are BT nodes, not looked at.
* ✔ The avatar's ability use 0xC1E8B0 posts its stimulus by the ability's kind (`GetAbility(i) +0x7C`): 0 one target
  (hit 0x2000 for a creature), 2 / 3 every creature in range (0xC42230 list, 0xD99A20 distance), 1 / 3 also 0xC155D0;
  kind 0x2B with model 0x2C39370 launches a projectile. ~ Inferred: the Raging Roar is a kind 2 / 3 ability whose
  stimulus the NPCs' BT turns into HeardRagingRoar; on a guest it reaches only AI-less mirrors.
* Not done: a fix (the host replaying a guest's area ability from the puppet) cannot be checked without a creature
  that has the roar (neither fixture creature has it; a carnivore Cell's consequence ability).

## The chat line under a modal box of the game (2026-11-03, fix/w41-creature-left)
<a id="creature-chat-under-modal"></a>

BUG-0482 follow-up. Lane 9, `tools/tests/coop_chat_modal.py`.

* ✔ (runtime) The "game saved" box (control id 0x06318E78, world.cpp) is the UTFWin window manager's modal window
  (`[IWindowManager::Get()] +0x82C` non-null while it is up, null after OK) and holds no input lock
  (cGameInputManager +0x110 = 0). Under it a click on the co-op window's chat line is dropped like under a caption:
  the line stays empty (out/lab/20261103-235548, switch off).
* `core.ui_modal_edit` (coop_ui.cpp `clicks_locked`): the locked paths of BUG-0273 (buttons) and BUG-0482 (text
  lines) take "a modal window of the game is up" (+0x82C, not our own window) like the input lock. `coop.uistate`
  prints `modal=` / `modal_seen=`; `coop.save hold` keeps our save's box up for a test, `coop.save confirm` lets the
  mod confirm it again, `coop.save box` says whether it is up.

## The home nest heals a guest (2026-11-03, fix/w41-creature-left)
<a id="creature-nest-heal"></a>

BUG-0496 (not reproduced). Lane 9, `tools/tests/coop_nest_heal.py`.

* ✔ (runtime, lab 20261103-235806) In the creature fixture both players at the home nest, health set to 3 and the
  co-op pack regeneration off (`coop.team off`): the host's own game healed it 3.2 -> 10.0 (full) and the guest's
  3.6 -> 16.0 (full, its own species Kirgle) in 12 s; 100 m from the nest neither healed (3.0 -> 3.0). So a guest's
  game runs the nest heal for its avatar at the save's player nest by itself.
* ? Not checked: a guest after the host's migration to a new nest (the guest's game may still have the old nest
  as its home: docs/re/creature.md 6.23 / coop_migration.py), a guest whose home nest the host's game made extinct
  (the player's mark said "for me nest is not extinct"), a guest whose own game has another nest object than the
  host's (BUG-0434 / BUG-0435 mound).

## Own creatures under the ground in the brain caption: not reproduced (2026-11-03, fix/w41-creature-left)
<a id="creature-brain-caption-ground"></a>

BUG-0504. Lane 9, probes (no test kept).

* ✔ (runtime) Both own creatures put at the report's spot (-86.1 -465.6 -177.1) with `coop.npc gopos`, then 100 DNA
  (`coop.dna add 100`): the brain caption F6A4B1E1 came (`busy=cinematic,input_lock`, then `paused`), and the height
  over the ground stayed 0.00 on both games before and for 18 s during it (coop.avatar `above=`, trace `agl=`).
* ✔ (runtime) `coop.npc gonpc <id> 3` (0.7.4: 2 m over the ground, then it falls) next to 10 NPCs: the host's
  creature rested 0.00-0.54 m OVER the terrain height (standing on something: the nest's sticks, a rock) until it
  walked, never under it.
* ~ Inferred: the -0.63 / -0.71 m of play_creature (lab 20261103-124913, creatures that walked there) is the game's:
  the mod does not move its own creature, and the caption does not lower it at that spot or at the nest. What the
  creatures stood on there is not known (the GetHeightAt terrain only sample against a collision surface; a walk
  path on the slope). The pack_target allow of 0.7.4 stays.
## The skin painter's own effects and the effects relay in Tribe / Civ (2026-11-04, fix/w41-effects-stages)
<a id="creature-effects-painter"></a>

BUG-0451, the other stages (docs/known-issues.md#issue-effects-civ-space). GOG 2024, lane 4, runtime counters
(`coop.effects`: `effects foreign world:`), the 0.7.4 fault logs (rel/0.7.4 out/lab/20261103-215559 and -223036).

* ✔ The skin painter (`Skinner::cSkinPainterPipeline::ExecutePipeline`, it paints a creature model's skin when the
  model is built: a partner's puppet, a tribe member, a citizen) creates its paint effects with
  `Swarm::EffectWorld::CreateEffect` 0xA71300 on **an effect world of its own**, not the game's (the one
  `cEffectsManager::CreateEffect` 0xA6CAD0 forwards to, manager +0x244), and on the **main thread** (the 0.7.4 faults
  are tid = main). Measured: in a 3-player Civ party the first guest's game made 32 such effects (20 ids, among
  them `D723B947`) while it built the partners' models; the host and the second guest 0 in that window.
* ✔ The world-effect hook (`h_WorldCreateFx`) took them like any effect: they were the "last effect id the game
  created" (`coop.effects load ... 0`, the test workload) and, when one stood within 1.8 m of the local player,
  a world effect to send. 0.7.4's effects_party_civ: guest 1's id was `D723B947` (a painter effect) in both
  repeats; its own load and the host (which plays and relays a guest's effect) faulted 680-936 times each at
  `Skinner::cSkinnerTexturePainter::StartRender+0x28` (read 0) within 2 s, while guest 2 (which also received
  it) did not fault: ~ inferred, StartRender reads the painter's current render target state, which only a game
  whose painter has run lately has. So such an effect created in the game's effect world faults every frame
  while it lives on a game whose painter is idle.
* ✔ The Tribe repeat fits too: there the **second** guest's last id was `D723B947` (lane 4 probe: the painter
  effect, its last any-world id for the whole session), sent in the test's cost phase where all three load; the
  host and guest 1 faulted (538 / 528), guest 2 (the sender) did not. `050B305A` and `85A02492` (Tribe) and
  `4C4BA0A9` (Civ) are effects of the game's own world.
* ✔ A player's real play would not have sent them: `near=0` in every run (Tribe, Civ, with the fix off: the painter
  effects stand nowhere near the player's own objects), so only the test workload (`coop.effects load ... 0`, the
  last id of any world) relayed one. The rule is still needed: nothing else keeps a painter effect out.
* ? Not reproduced on lane 4 on 2026-11-04: the 0.7.4 release build itself (e396d1cb, its own test with
  `effects.other_stages on`) ran effects_party_civ twice without a fault, and rel/0.7.5 with
  `SPORE_FIX_OFF=effects.main_world` relayed `D723B947` to both partners without a fault
  (out/lab/20261104-004532; also -003023, -003205). The 0.7.4 faults were on lane 2. So the fault needs a state
  of the receiving game that these runs did not have (~ inferred: StartRender reads the painter's render target
  state, set only while a paint is under way).
* Fix (`coop.fix effects.main_world`, on): the hook marks an effect made in another world than manager +0x244
  (`game::effect_foreign`); effects.cpp never sends it, the perf load uses the last id of the game's own world
  (`game::last_effect_id(true)`), and a receiver does not play a world start of an id it saw only in a painter
  world (`not played=`). `effects foreign world: created= queued= near=` counts them; `near` is how many a
  player's real play would have sent without the fix. With it, `effects.other_stages` (Tribe and Civilization)
  is on by default; Space stays behind `effects.space` (off: not verified, no UFO effect can be made in a test).

## The Drama Manager's world events in a session (2026-11-04, fix/w41-infra)
<a id="creature-drama-events"></a>

BUG-0486 (UFO), BUG-0465 (sea monster). GOG 2024, Ghidra (tools/ghidra/q.sh). Follows #creature-ufo-abduction-event
and #creature-sea-monster.

* ✔ `cDramaManager` is a singleton: `SporeCoop::DramaManager_Get` 0xD51610 (cdecl, the pointer at 0x169E43C, made
  on first use, 0x70 bytes). Vtable 0x147B520: +0x2C 0xD52270 the update (thiscall(mgr, uint64 ms) ret 8; Creature
  and Tribe only, both end in the event tick 0xD4E450 thiscall(mgr, float dt s) ret 4, which returns at once
  while the game time is paused), +0x38 0xD4E020 builds the events from prop group 0x0EBCFBC7 (Creature) /
  0xE7CA1F54 (Tribe), +0x3C 0xD4E300 ends them all.
* ✔ The nine event kinds (event +0x14), from the factory table 0x147AF4C (16-byte rows: class id getter,
  factory, name): 0 class 0x5599D0C (factory 0xD4D390), 1 cNightPredatorEvent 0x5599E3B, **2 cUFOAbductionEvent
  0x5599EC7**, 3 cMeteorStrikeEvent 0x5599F00, 4 cBabyGameEvent 0x5599F34, 5 cRainStormEvent 0x5599F5A,
  **6 cSeaMonsterEvent 0x5599F83**, 7 cCreatureAttackEvent 0x5ED29D9, 8 (0x609EEDF, cSummonCreatureEvent by the
  factory order). The class name string cTribeFoodRaidEvent has no reference.
* ✔ Event fields: +0xC active byte, +0x10 a mask of kinds it excludes while active, +0x18 uses left (-1 = any),
  +0x1C condition kind (1 avatar vs a value, 2 a distance (0xD2E360) vs a value, 3 the time of day between
  +0x20 and +0x24 hours, 4 never by the tick), +0x28 the wait left (s; reset from prop 0x920A2318 when it runs
  out), +0x2C the duration, +0x30 "not while the avatar is ..." (avatar +0xB5E), +0x40 its prop list.
* ✔ The tick 0xD4E450, for every idle event: no start during a cinematic (cCinematicManager +0x2C 1 / 2), vt+0x44
  pre-check, uses left, no excluded kind active, the wait run out, the condition, then vt+0x38 "can start" and
  `SporeCoop::DramaManager_StartEvent` 0xD4D4E0 (thiscall(mgr, event, 0) ret 8: +0xC = 1, uses - 1, the duration,
  event vt+0x30 start). An active event: vt+0x3C "done?" or its duration / condition over -> vt+0x34 end, else
  vt+0x40 update(dt). `SporeCoop::DramaManager_StartKind` 0xD4D740 (thiscall(mgr, kind, 0) ret 8) starts every
  idle event of a kind (a multimap by kind at mgr +0x50): the stage start 0xD40FA0 calls it with 4 (baby game), a
  message handler 0xD8E840 with 8.
* ✔ Runtime (lane 6, coop_drama_guest.py, out/lab/20261104-033630 / -033725): `coop.drama start 2` (StartKind 2) makes a UFO in the game's object
  list within 4 s where the event may start; the UFO starts at the own avatar (#creature-ufo-abduction-event).
* Decision (what the partner should see): every event is made by each game for its own avatar (start positions
  are the avatar's / the player tribe's), so the partner's game never has it. The host owns the world: a guest's
  own game starts no UFO visit (`creature.guest_events`, mods/coop/drama.cpp; the kinds are a mask, `coop.drama
  mask <hex>`, default 0x4 = kind 2 only), so no UFO takes the guest's mirrors of the host's creatures in the
  guest's game only. ~ Not done: the host's UFO shown on a guest (a mirror of the type 7 ship at the host's
  position: the ship's own BT moves it, so it would need the host's position each frame and its BT off).
* Decision for the sea monster (kind 6): kept on a guest. ~ Inferred (not seen at runtime): it is the avatar's
  deep-water death (death cause 9, CRG_AvatarDied_SeaMonster), the game's way of keeping a swimmer near the
  island; refusing it on a guest would let the guest swim off the map. The partner does not see it (the partner's
  game has no event for a puppet): a relay of the death to a cinematic at the puppet is not done.
## The partner's puppet: jumps, nest mounds, stops, the walk gait far from the camera (2026-11-04, fix/w41-puppet-look)
<a id="creature-puppet-look"></a>

BUG-0376, BUG-0435, BUG-0408, BUG-0436, BUG-0434. GOG 2024, lane 2, `coop.track` (host watches guest).
* ✔ Jumps (BUG-0376): a puppet placed (game::place, no teleport mark) at a target in the air and then marked not
  grounded (+0x137 = 0) is left in the air by the game's next update, which lets it fall from there (the fall
  animation 0x0692E6A9 plays, as on the partner's own creature); placed every frame it follows the partner's fall.
  `creature.puppet_air`: the partner's game sends PF_JUMPING (8) while its own creature is not grounded and more than
  0.15 m over `GetHeightAt`; the observer then skips creature.puppet_ground and places the puppet at the target with
  +0x137 = 0. Hops by `coop.abilities lift 1.5`: drawn at least half as high as the target in the next frame in 99 %
  of the frames with the target over 0.4 m (2 cold runs, 641 / 654 frames), 5 % without (1 of 15 hops shown).
* ✔ Walked in the air (walk_place with its MoveTo / SetDesiredSpeed while the target is in the air, `coop.track air
  3`) the puppet was put back onto its terrain in 15-20 frames of a 0.4 s fall (drawn 0.00 m under a target 1.4 m
  up), so the puppet is not walked in the air (walk_end, placed; its walk starts again after the landing). Both
  placing (no mark) and the teleport keep it up; the not-grounded mark is what keeps the game from grounding it.
* ✔ The puppet's own fall speed must be the partner's: left to the game it grew every frame while the placing held
  the puppet up, and after 0.3-0.5 s one update took it 1-1.6 m down onto its terrain (`coop.track air 4`); with the
  velocity (cLocomotiveObject +0x1C8) written to the partner's each frame 98-100 % of the frames stay up.
* ~ What is left (BUG-0532): 1-3 frames on the terrain in the middle of a fall, 0-3 times in 30 s, each time in the
  frame the puppet's main animation changes (fall -> idle 02481DE5 or the walk start 04866D8E). Inferred: starting an
  animation grounds the creature in that update.
* ~ A teleport up (the simulated hop) is sent as a grounded state for one or two states (the partner's game sets
  +0x137 = 0 one update later): the target rises 0.3-0.5 m per frame before the flag comes and is sunk / grounded in
  those 3-5 frames. A real jump starts from the ground in the game's own update (inferred: the flag comes with its
  first state; not checked, the fixture's creatures cannot jump).
* ✔ On a rock +0x137 is 0 and the height over `GetHeightAt` 1-16 m, so PF_JUMPING is set there too; nothing changes
  for the observer (a puppet on a rock is not grounded either and creature.puppet_ground left such targets alone).
* ✔ Nest mound (BUG-0435): a grounded partner 0.4 m over `GetHeightAt` on the mound's lower part, put 5 cm under its
  own height instead of 5 cm under the plain terrain (`creature.puppet_mound`, from 0.1 m over GetHeightAt up to
  kOverGround), is drawn on the mound: 0 of 1120 frames on raised ground more than 0.15 m under the partner's height
  (0.7.4: 179 of 303, drawn 0.45 m under). The game lifts a creature put 5 cm under a mound's surface as it does on the
  terrain.
* ✔ Stops (BUG-0408): no slide reproduced. 21 stops on the slope (guest 30 fps, `player` network), 10 of them made
  abrupt by `coop.abilities halt` (StopMovement, a teleport to where it is and velocity 0: the walker's state speed
  drops from 4.75 to 0 in one state while the puppet's game speed is 2.0-2.2 m/s): the puppet's drawn way after the
  stop less its target's was 0.00-0.05 m, never past the stop by more than 0.33 m. The game's walk to the MoveTo point
  of walk_end ends at that point also from 2 m/s. The rare slides of the ticket need something else (inferred: a
  target that moved back after an extrapolation).
* ✔ The walk gait far from the camera (BUG-0436): in the three recorded runs with gait 0 for a whole walk the observer
  drew a 30 m walk within 20-30 pixels (~1 px/m; passing runs 25-90 px/m and their screen ranges 700-1700 px): the
  observer's camera was hundreds of m away and its game animated the puppet (and in the npc_walk_slope run its NPC
  proxies) without a walk gait, as it does with creatures far from its camera. ? Why the camera was that far after a
  cold start (not read then). `coop.track` column 29 `ppm`: pixels per metre at the creature (a point 1 m over it
  projected), 0 when not on the screen. The mouse wheel (`wheel x y -5` x 12) zooms out only to 25-60 px/m, where the
  gait stays on (puppet 969 / 969 frames, the walker's own 1250 / 1296).
* ? BUG-0434 (the first frame back in view drawn on the plain terrain) not looked at again: the update that grounds a
  culled creature is still not found.

## The social game's prompts on a guest (2026-11-04, fix/w42-creature-left)
<a id="creature-social-prompts"></a>

BUG-0525 (fixed), follows #creature-guest-meter. GOG 2024, Ghidra and lane 6 (creature fixture).

* ✔ (Ghidra) The session block of SocialPropose (`S` = session +0x1A4): +8 prompt count (= rounds, tuning +0x424,
  -1 for a baby 0x1582F14, +1 for an alpha 0x1582F10), +0xC prompt index, +0x10 / +0x14 the open prompt's ability +8 /
  +0x10, +0x18 the meter scorer (needed = 2 x the sum of the prompts' rounded +0x3C points), +0x44 the answer wait,
  +0x58 flags. The tick 0xDA4950 (`SocialSession_Tick`, jump table 0xDA4E30) state 4 plays prompt i of the NPC's list
  (npc +0x1020, picked from species profile +0x7A4 at the start) with cCreatureBase::PlayAnimation(GetAnimationID(0),
  -1, 0), state 6 waits: 0xD9B070 takes the answer each frame, the wait is 10 s (0x1582F20) after the NPC's animation;
  state 7 plays 0x68D8F9C and goes back to 4; at the end of the list an unfilled meter is a failure (state 8, the NPC
  plays 0x53685D8F).
* ✔ (Ghidra) The answer is stimulus 0x10000000 kind 1, posted to the NPC by the answering creature's BT node 0xD9C100
  (the avatar's: ConsumeEnergy, `+0x14` = the ability's +0x10, `+0x18` its points 0xD9C060 = +0x3C plus 0.1 of it per
  pack member with an ability of the same +0x10). 0xD9B070 gives the points to the meter, x0.1 (0x1582F1C) when +0x14
  is not the prompt's; one answer per prompt. So the kind an answer must match is the ability's **+0x10**, not +8.
* ✔ (runtime, `coop.social abil`) In the creature fixture every creature has exactly one ability with +0x10 = 1
  (type 0x25, the sing: points 1 on the host's species and the NPCs, 5 on the guest's own creature) and the others
  +0x10 = 0; every species' prompt candidates (+0x7A4) are that one ability (index 4 or 5). The ability kind +0x7C:
  0 for all but type 0x1D (kind 1, range 100). So the fixture cannot show two social kinds; a combat ability (+0x10
  0) is the answer "of another kind" in the test.
* ✔ (runtime) The prompt's animation (Kook: 042A1CAF) is sometimes played as another of its family (042A1CB3) and the
  host's NPC, whose BT keeps running, plays 0404FC23 / 0404FC25 around it.
* Fix `social.prompts` (social.cpp): the guest tells the host when it engages a mirror (MSG_SOCIAL kind 4 K_ENGAGE
  {u16 npc}); the host picks the prompts as SocialPropose does, 2 s later starts them one by one: plays the prompt on its
  NPC (CreaturePlayAnim, relayed by npc.anims) and sends it (kind 5 K_PROMPT {u16 npc, u8 i, u8 n, u32 type, u32 key,
  u32 anim}: the guest plays it on its mirror); a guest's answer (K_ACT) counts only while a prompt is open (13 s), by
  the puppet's ability of that type: its points (at least 1) when its +0x10 is the prompt's, else 0.1 of them; won at the
  sum of the prompts' points; after the list a failure (K_RESULT ok 0 with 0x53685D8F, played on the mirror). The
  meter's bar follows the score (K_PROGRESS in tenths of points). Older SporeCoop ignores the two kinds.
* ? Not done: the native meter's own prompt marks (0xD9AFF0 -> 0xE33A70 on bar 0) on the guest's meter, and the x2 of
  0xBCA230 (not known what it asks): the guest's needed score is the sum of the prompt points, not twice it.
* `coop.social abil <npc id | -1>` lists a creature's abilities (+8, +0x10, +0x3C, +0x7C, range +0xA8) and its
  species' prompt candidates; `coop.social answer <npc id> <hex type>` answers with any own ability (tests).

## puppet_jump with the world's species: the game does not jump (2026-11-04, fix/w42-creature-left)
<a id="creature-world-species-hatch"></a>

BUG-0536 (not ours). Lane 6, `PUPPET_JUMP_WORLD=1 tools/tests/coop_puppet_jump.py` (the guest's record of the fixture
world cleared first: the lane's profile had 290225CD recorded, so the guest played its own creature by default).

* ✔ (runtime, lab 20261104-114050) With both players of the world's species (065CBE70), each dying at the fixture's
  nest, the dying player's **own game** never moved its creature more than 1.5 m in 0.5 s during the 28 s after the
  death caption (`coop.avatar` sampled on the dying game); the other game's puppet was shown again 0.00 m off the
  target. With the guest's own creature (another species, lab 20261104-113744) the guest's game stepped it 6 m at
  10.9 s (out of its egg) and the puppet followed in 0.08 s; the host (world species) again no step.
* ~ Inferred: a creature of the nest's species respawns in its egg at the nest where it died here, and its hatch is
  not a step; one of another species is hatched off its egg's place. Our track and puppet are not involved. The test
  keeps choosing the guest's own creature (a jump is needed to check the catch).

## Roar, nest heal, carried objects: what the third look found (2026-11-04, fix/w42-creature-left)
<a id="creature-leftovers-3"></a>

BUG-0501, BUG-0496, BUG-0458 (still open). GOG 2024, Ghidra and lane 6 (creature fixture).

* ✔ (runtime, `coop.social abil -1`) The host's avatar (world species 065CBE70) has 11 abilities: types 0xE 0x1D 0x28
  0x10 0x25 0x1E 0x11 0xD 0x31 0x32 0x33, ability kind +0x7C 0 for all but 0x1D (kind 1, range +0xA8 100 m); the
  ranges: 0x31 30 m, 0x32 25 m, 0xD 16 m, the bites 10 m, the sing 5 m. No ability of kind 2 / 3 (the area kinds of
  0xC1E8B0, #creature-roar-2), so neither fixture creature has an area ability like the Raging Roar: BUG-0501 still
  has no test creature.
* ✔ (Ghidra) 0xC155D0 (called by 0xC1E8B0 for kinds 1 / 3) is the ability's effect spawner (a slot of 4 at creature
  +0xEB0, Swarm::IEffectsManager), not a stimulus: the area kinds' stimuli are the loop over 0xC42230 in 0xC1E8B0.
* ✔ (runtime, `coop.social log`) The avatar posts stimulus 0x4000000 (value 5) to the creatures around it all the time
  (400 lines in a minute, the log's cap): a log of the avatar's stimuli needs a filter on that one.
* ✔ (runtime) An allied species' nest (the host's record of the Kooks set to ally 6, impressed 3, the guest's table
  following it) healed neither player in 12 s (host 3.0 -> 3.0, guest 3.0 -> 3.0, the pack regeneration off): a
  record alone does not make a nest heal (the game's "you can heal at their nest" needs more than relation 6; not
  looked at). BUG-0496 still not reproduced: the home nest heals both (0.7.5 final lab 20261104-061516 with the world's
  species: host and guest 3.3 -> 10.0).
* Not done (BUG-0458): the plan of #creature-hold-slots stands; a carried / thrown rock needs a host-owned world
  object kind with a follow (worldobj.cpp), which collides with the Space branch's new worldobj kinds in this batch.

## The puppet in the air through an animation start (2026-11-04, fix/w42-creature-left)
<a id="creature-puppet-air-anim"></a>

BUG-0532 (fixed). Follows #creature-puppet-look. Lane 6, puppet_air (`coop_puppet_walk.py --dir guest --jumps 0.9 --air`).

* ✔ (runtime, `coop.track air` counters) The game sets a held puppet's +0x137 (grounded) back to 1 inside an animation
  start: right after Anim::AnimatedCreature::PlayAnimation 0xA0C5D0 (the animation system's own start, a vtable slot
  every start goes through: 1-4 times in a 30 s run), after the idle refresh 0xC148B0 (2) and after
  SporeCoop::CreaturePlayAnim (1-2, the BT / relay starts); once after cCreatureAnimal::Update. With +0x137 = 1 the
  same update puts the creature onto its terrain: the 1-3 frame drops of BUG-0532.
* Fix `creature.puppet_air_anim` (game.cpp air_hold, modes.cpp): while a puppet is held in the air (creature.puppet_air,
  PF_JUMPING) the mod remembers where the partner is; after each of those calls on it, when the game marked it
  grounded, it is put back there, marked not grounded and given the partner's velocity. `coop.track air` prints
  `regrounded update= anim= idle= bt= low=` (the call after which it was found grounded).
* ~ The hooks on PlayAnimation / CreaturePlayAnim / the idle refresh alone (without 0xA0C5D0) left 1 step in 3 runs
  (lab 20261104-115008 #3): the locomotion starts its walk start / fall -> idle through the low start only.

## The guest as a player: the game's player checks and where the mod answers them (2026-11-04, fix/w43-guest-player)
<a id="creature-player-rules"></a>

P1 of docs/plan-0.7.7.md, BUG-0418 / BUG-0420 (umbrellas), BUG-0554 / 0555 / 0556 / 0557 / 0561 / 0570. GOG 2024,
Ghidra (tools/ghidra/q.sh) and lane 9 at runtime. In co-op one game decides what happens to the other player's
creature: the host's game runs the NPCs and hits the guest's **puppet**; the guest's game runs its **avatar** and
hits **proxies** (AI-less mirrors). A rule of the game that lands on the puppet / proxy, or a check that asks "is
this the player" and gets "no" for the partner, never reaches the player whose creature it is. The mod's one place
for such rules is `mods/coop/player_rules.cpp` (actions carried over the net, MSG_PLAYER_RULE 63) next to the
predicate answers in `creature_hp.cpp` (IsPlayerSpecies) and the table follower in `social.cpp`.

The checks found, what they read, and how the mod answers them now:

| Address | What it checks / does | Callers | The partner before | The mod now |
|---|---|---|---|---|
| vt 0xD0 0xC0C430 IsPlayerSpecies | +0xB20 == PlayerSpecies 0x4DF490 | GetRelationship 0xBA40E0, CreatureSocialMode 0xC0E800, vt 0xD4, family / hatchling / nest code | another species: the diet rule | `creature.relations` (BUG-0462): the party's species, only for the two relation callers |
| vt 0xD4 0xC0C3B0 CreatureFriendly | relation 6 to the avatar, impressed +0xB58 & 0x80, or IsPlayerSpecies; **not** the pack bit 0x100 | rollover 0xB6D4C0 -> CreatureSelectionUI 0xB6C970 (+0x78 window), cursor 0xD2FFD0 (kind 0x10), CreatureSocialMode | the host's pack / summoned flock: strangers on the guest | `creature.party_allies` (BUG-0556): true for the proxies whose host NPC is in the host's pack (NF_PACK) |
| vt 0x94 0xC02C70 CreatureStun | (the action) avatar seconds scaled / clamped (2 s -> 3.0), stimulus lo 0x1000 = seconds | the ability hit 0xC20390 (effect kind 1: the charge 0x1F, 1.5 s) and its counter-charge case 0x20 | on the puppet (no AI) / on a proxy | `creature.stun_relay` (BUG-0554): host -> guest kind 0 (its avatar), guest -> host kind 1 (the NPC; only inside the own avatar's hit, CreatureAttackTarget) |
| 0xD666B0 (attack node's ability choice, BT_AttackTick 0xD67CD0) | target noun 0x18EB45E, +0x137 == 0, +0x110 & 0x1000 clear, distance >= 1 m -> only types 0x20 / 0x3D / 0x42 / 0x48 | BT_AttackTick at 0xD67E7D / 0xD68474 | the puppet grounded at an animation start (BUG-0532) or an attack chosen before the take-off: melee hits in the air | `creature.air_immune` (BUG-0555): CreatureAttackTarget vt 0xB8 0xC02B40 on a puppet whose player is airborne (PF_JUMPING) carries no damage / stun of a melee hit > 1 m to the partner (the hit itself runs: it ends a charge); the guest answers the same rule for a hit the host decided late (MSG_DAMAGE carries the ability type) |
| minimap 0xD425A0 / 0xE10560 | nest icon ally flag 0x200000 = herd(nest +0x128) +0x191 mbBestFriends or herd species == PlayerSpecies | the minimap update every 2.5 s | +0x191 set only by ApplyEvent 0xBA4A00 -> 0xBA4840 -> cHerd_SetBestFriends 0xC6A210: never on a guest | `social.nest_ally` (BUG-0561): the guest's herds follow the host's table, +0x191 = (relation == 6) |
| friendly nest 0xC6ABF0 | own species, +0x191, +0x190 or relation 4 | 0xC084B0, 0xD401E0, 0xD41A20 (nest sound / music, ~ the heal at an allied nest) | as the minimap | the same +0x191 (BUG-0561); ~ inferred: also what an allied nest's heal needs (BUG-0496) |
| TakeDamage 0xC075E0 kill credit | attacker spatial vt 0x58 (avatar) or relation 6 | every hit | a guest's kill paid nothing | npc.cpp: the lethal guest hit lands as the host avatar's (docs section 2) |
| NPCTickAI 0xC09570 | notices cGameNounManager +0x54 only | every NPC | the puppet unseen | `npc.partners` (BUG-0016): the AI swap |
| Tribe rings 0xCCC7C0 | selected citizens' combat target +0xE7C (set by the AI attack node) | TribeUI vt 0x34 | a guest's mirrors run no AI: no ring | `tribe.target_ring` (BUG-0570), docs/re/tribe.md#tribe-target-ring |

* ✔ (runtime, lane 9) The NPC melee is `CreatureAbilityHit` 0xC20390 <- CreatureAttackTarget 0xC02B40 <- PlayAbility
  0xC1E5C0 +0x10C1 <- the BT: a bite of 065CB8BD is TakeDamage type 2, 0.5 hp, return address 0xC20A78 (`coop.hp
  dmglog`, a ring of the last 48 hits through the hook: victim, attacker, type, caller, filtered / applied /
  direct). Damage type 3 in TakeDamage is the **charm** (mCharmDamage +0x1694 / mCharmTime +0x169C / mpCharmer
  +0x1698, reflection 0x1334790: 20 to charm, 50 for an epic, 60 s), not a charge's stun.
* ✔ (runtime) The charge stun: `coop.rules charge <npc> 1` (SetTarget + PlayAbility of type 0x1F) on the host hit the
  puppet (type 2 0.5 hp) and sent kind 0; the guest's avatar then held stimulus 0x1000 for 3.0 s, counting down,
  and showed the stars (screenshot). CreatureStun called on the guest's avatar by hand does the same.
* ✔ (runtime) Summon Flock: `coop.drama start 8` (the event kind 8 the ability's handler starts) put 4 creatures of
  0672579F into the host's pack (+0xB58 & 0x100); the host shows hearts over them (effect 0x6A17B05A of the event's
  update 0xD512A0). Their vt 0xD4 is **false on the host** too (the pack bit is not read), so the host's "allied"
  look is the pack and the hearts; the guest gets both now (vt 0xD4 for NF_PACK proxies, the hearts by
  `npc.flock_hearts` for NF_SUMMONED proxies: herd +0x192).
* Ability types seen (`coop.social abil`, now with the effect list): 0x1E melee (bite, range 1), 0x1F charge (effect
  kind 1, chance 1.0, stun 1.5 s), 0x20 ranged (range 30, effect kind 2 fear, 6 s), 0x31 / 0x32 / 0x33 roar / siren
  song / summon flock (0xD8E840 kinds; the host's fixture avatar has 0x33 with 0 points: PlayAbility did not start it).
* ? Not covered: a guest's own Summon Flock (its game makes the flock locally: AI-less? not looked at), the roar
  (BUG-0501: kind 0x31 posts stimuli to enemies in the roaring player's game; the guest's roar reaches proxies only),
  the music (BUG-0568), the eating of pack kills (BUG-0557).
## The drama UFO and the partner's puppet (2026-11-04, fix/w43-transitions)
<a id="creature-ufo-puppet"></a>

BUG-0558. GOG 2024, Ghidra; the players' logs (0.7.5). Follows #creature-drama-events.

* ✔ (logs) The host's game started its UFO event ("drama: ufo event starts (host or alone)"); the guest's game
  refused its own five times (`creature.guest_events`, BUG-0486); nothing is sent for it. The host's screenshot shows
  the ship and its blue beam light; the guest's (same session second, same place on the minimap) no ship.
* ✔ (Ghidra) The event's start 0xD4F8A0 places the ship over the own avatar (Creature) or the player tribe (Tribe)
  with `UFOAbductionEvent_SpawnUFO` and posts **`SporeCoop::BT_PostStimulus(2, 0, duration, 0)`**: the creatures'
  behaviour trees get stimulus 2 for the event's duration. The ship takes its target through the space tool
  `cAbductToolStrategy` (0x1052920, the same tool as the player's abduct in Space); its hit 0x10323E0 (vtable
  0x1499780 slot) on a creature (cast 0xCE9F6639) plays 0x3CC7EAB / 0x3CC7EB4 and gives it a
  `cAbductionBeamLocomotion` (0x1030E90) that lifts it to the ship.
* ✔ (inferred from the code and the report) the partner's puppet is a creature of the host's game near the host's
  avatar, so the host's ship can take it: the beam's locomotion lifts it, but the mod places the puppet at the
  partner's position every frame (game::place), so it never rises: "the ship tries to pick up my guest but cannot".
  The guest's game has no ship (its own event is refused, the host's is not relayed).
* ~ Fix not done (T1, 0.7.7): either the host's ship never takes a puppet (a check in the tool's target pick or its
  hit 0x10323E0 scoped to puppets: the target pick is not found yet, the hit only gives the locomotion) or the
  visit is relayed: the guest's game starts its own event when the host's starts (its ship then picks in the guest's
  world: its mirrors, the BUG-0486 problem) and an abduction of a partner is played on the partner's own avatar.
  Both need the ship's target choice found first.
## The flora's fruit pool (2026-11-05, fix/w43-crashes)
<a id="creature-fruit-pool"></a>

BUG-0584 (not reproduced). GOG 2024, Ghidra.

* ✔ The flora (`SporeCoop::Flora_Get`) keeps two refcounted vectors of cFruit: **loose fruit** +0x1B905C..+0x1B9064
  (fallen, on the ground, held, hidden) and **free fruit** +0x1B8F44..+0x1B8F4C (eaten, kept for reuse). Its update
  0xB085D0 walks the loose ones by state (+0x16C): 1 falling (to the ground: 2), 2 on the ground (+0x168 time; hidden:
  4), 4 hidden (state 5 after a while, the holder +0x108 released), **5: 0xB069A0 moves it to the free vector**
  (state 6, holder released, model off). So a fruit in state 5 is not taken out of the noun list.
  CORRECTION (2026-11-05, fix/w43-crashes): the cFruit_SetState .sym note and the fruit kind's comment in
  worldobj.cpp say "the game takes a fruit in state 5 out of the list within a frame or two": it leaves the loose
  vector for the free one (state 6) and stays in the noun list (Ghidra, 0xB085D0 case 5).
* ✔ 0xB08DF0 (fastcall flora) gives a fruit for a drop: the last free one, else the loose fruit farthest from the
  camera that is not held (state 3), not flagged (+0x148 bit 0) and has no holder (it is retired with 0xB069A0(it, 0)).
  `Flora_DropFruit` 0xB09AE0 -> 0xB09790 calls its AddRef (vt 0) first: the BUG-0584 fault (a call through null at
  0xB097C2, `[esi]` = 0x15B40) is a free or loose entry whose memory was freed and reused. 0xB09B50 is the flora's
  timed fall of fruit near the camera (the caller in that fault).
* ✔ 0xB066A0 (a cFruit vtable slot at 0x145C58C, after cGameData's read): a fruit read from a save lies on the
  ground (state 2), gets +0x148 bit 0 and joins the loose vector. The eat node 0xD87590 (state 3, its meal's end)
  calls `cGameNounManager::DestroyInstance` on such a fruit before `cFruit_SetState(5)` and posts 0xD335362B;
  DestroyInstance (0xB22560) marks +0x20, keeps a reference in the manager's pending list (+0x84) and runs the
  object's teardown (0xB20CC0) at once.
* ~ superseded (2026-11-05): see #creature-fruit-pool-guard. Not found: where a reference is released once too often. The mod's fruit kind (wobj.fruit) sets state 5 through
  the game's SetState only (no release); the log of the fault has two `took fruit ...: done here too` 5 s before it.
  CORRECTION (2026-11-05, fix/w44-crashes): no reference is released once too often; the mod's SetState(5) on a fruit
  of the save (without the eat node's DestroyInstance) leaves it in the free fruit with one reference, and the take's
  pop frees it (lab 20261105-100942, #creature-fruit-pool-guard).

## The fruit pool guard (2026-11-05, fix/w44-crashes)
<a id="creature-fruit-pool-guard"></a>

BUG-0584. GOG 2024, asm and Ghidra. Extends #creature-fruit-pool.

* ✔ `SporeCoop::Flora_TakeFruitForDrop` 0xB08DF0 is thiscall(flora) (ecx = Flora_Get, plain ret), its only caller is
  the drop maker 0xB09790, which skips the drop on null (je 0xB0998E) and otherwise calls the fruit's AddRef (vt 0)
  at 0xB097C2 at once: the fault's site.
* ✔ 0xB069A0(entry, to_free) retires a loose fruit with balanced references (AddRef local, push to free + AddRef,
  erase from loose + Release, Release local); it clears +0x148 bit 1 only, so a saved fruit (bit 0) keeps its bit
  when the pool reuses it.
* ✔ cFruit_SetState(5) releases the holder +0x108 whatever the state was, also on a fruit in state 3 (held by a
  creature that eats it): an eat node that still runs has its fruit retired to the free vector under it.
* ✔ (Ghidra) the take's free path pops the last free fruit: AddRef, the vector's slot released, and its own reference
  released again before it returns: it returns a pointer whose pool reference is gone and that only other holders
  keep alive; the caller's AddRef comes after. A free fruit with one reference (+0x8, cFruit AddRef 0xC6AB00) is
  freed inside the take and the caller calls into freed memory (the Wine heap wrote its links over the vtable:
  [fruit] = a heap address, [that] = 0). The loose path skips fruit with +0x148 bit 0; the free path does not.
* ✔ (runtime, lab 20261105-100942, no plant) **the root cause of BUG-0584**: `coop.wobj eat` (the mod's fruit_eat, the
  path of a guest's "took fruit" on the host and of the host's eaten fruit on a guest) on a fruit of the save
  (+0x148 bit 0) set state 5 only: the flora retired it to the free fruit with **1 reference** (19 before, `coop.fruitpool`:
  `10F6CB20/r19/f1/s2` -> `r1/f1/s6`; the game's own free fruit had 7), and the next fruit that fell (`coop.wobj drop`,
  the flora's DropFruit) faulted at 0xB097C2 with the long session's chain (0xB097C4 <- 0xB023CC <- 0xB04B24 <-
  0xB0586D <- 0xB05893 <- 0xB09B3D). The game's eat node destroys such a fruit (DestroyInstance) before its
  SetState(5); the mod did not. The long session's two `took fruit ...: done here too` 5 s before its fault fit.
* Fix `wobj.fruit_saved` (worldobj.cpp fruit_eat): a fruit with +0x148 bit 0 is destroyed (DestroyInstance) before
  SetState(5), as the eat node does.
* Guard `game.fruit_pool` (gamefixes.cpp): before each take, entries of the free and loose vectors whose vtable is
  not `SporeCoop::cFruit_vftable` 0x145C568, and free fruit with one reference (the take would free them), are taken
  out without a Release (kept alive, a few hundred bytes lost); `coop.fruitpool` lists the pool, `coop.fruitcut`
  plants a freed fruit as the last free one (tests).

## The guest as a player, part 2: the roar, the UFO pick, the nest heal, eating (2026-11-05, fix/w44-guest-player2)
<a id="creature-player-rules-2"></a>

P2 of docs/plan-0.7.8.md, BUG-0418 (umbrella), BUG-0501 / 0558 / 0496 / 0557. GOG 2024, Ghidra (tools/ghidra/q.sh)
and lane 6. Follows #creature-player-rules; the code is in `mods/coop/player_rules.cpp` (MSG_PLAYER_RULE kind 2,
`coop.rules roar | ufo | heal | meat`).

### The roar (BUG-0501, fixed)
<a id="creature-roar-relay"></a>

* ✔ The player's roar / siren song / summon flock is the behaviour tree's action node **0x06005DAF** (node table
  row 0x158B1F8: enter 0xD7B5B0, tick `SporeCoop::BT_SpecialAbilityTick` 0xD8E840, exit 0xD7B600; rows are 16 bytes
  {id, enter, tick, exit}). The tick is cdecl(creature, 4 unused, u32* state): state[0] ability index, [1] phase.
  Phase 0: GetAbility vt 0xB4, 0xC19800 (recharge / energy), PlayAnimation, then by type +8: **0x31 roar** walks
  the creatures within +0xA8 + +0xF4 m (30 m) that are not the avatar (spatial +0xC0 vt 0x58), +0xB5E == 0, of
  another species (+0xB20) and not epic, and calls on each, in order: `BT_PostStimulus(ai+8, 0, 0x4000000,
  FLT_MAX, roarer)`, `BT_GoalSlotStart(+0xB48, 1, 1, 0, ability +0xA0 s (25 s), 0, target)` (0xBCA980), `BT_ClearStimulus(ai+8,
  0x400, 0)`, `BT_GoalSlotEnd(BT_GoalSlotFind(2, 1))` (the siren's slot). 0x32 siren song: other stimuli and goal
  slot 2 / 1 for the other species, calms its own species (0xD79FF0). 0x33: `DramaManager_StartKind(8)`.
* ✔ Goal slots (creature +0xB48): 8 slots of 0x44 bytes, +0x20 serial, +0x24 template id (-1 free), +0x28 group,
  +0x2C seconds, +0x34 end callback, mask of running slots +0x22C; handles are serial | slot << 16.
* ✔ (runtime, lane 6) The roar is the **player's** consequence ability, not the species': the guest's avatar (its
  own creature Kirgle) has 9 abilities with 0x31 at index 6, its puppet in the host's game 6 (0xE 0x1D 0x28 0x10
  0x25 0x1E, no 0x31). So the host cannot run the node on the puppet.
* ✔ (runtime) Before: the guest's roar reached only its proxies (no AI); the host's 4 NPCs within 20 m kept no slot
  and stayed (out/lab/20261105-110044). Fix `creature.roar_relay`: the guest's first tick of its own roar sends
  {type, range, seconds} (kind 2); the host's game makes the same four calls on its creatures around the guest's
  puppet with the puppet as the roarer: 4 of 4 got slot (1, 1), and 3 ran (8.5 -> 17.4, 6.0 -> 12.7, 8.5 -> 25.6 m in
  10 s; one at 12 m held the slot and did not move), out/lab/20261105-110141. Without the fix every NPC stayed within
  0.1 m.
* ✔ (runtime) The host's NPCs that got the slot run BT node **0x02D852E0** within 3 s (all 4, every run; none
  without the fix) and back away 2-4 m in warm games, 8-17 m in cold ones (out/lab/20261105-115630, -115727).
* ~ Not covered: the siren song (0x32) of a guest (its 8-dword slot data at 0xBCA980's 5th argument is not known);
  the guest's own Summon Flock (0x33) still makes its flock in its own game.

### The drama UFO's pick (BUG-0558, guard in place, not verified at runtime)
<a id="creature-ufo-pick"></a>

* CORRECTION (2026-11-05, fix/w44-guest-player2): #creature-ufo-puppet says the event's start posts BT stimulus 2 to
  the creatures; it posts it to the **ship's own** behaviour tree (0xD4FA40: ECX = [ship +0x6C8] + 8).
* ✔ (Ghidra) The ship's BT nodes are in the table 0x159D640. The **pick node 0x0519E372** (start 0xDBF1A0, tick
  0xDC26B0, cdecl(ship, ..., state at [esp+0x18])) runs a timed state machine (state[2] 0..3, timer prop 0x34E39082,
  7 s) and searches in state 3 only, and only while the event's counter (0xD51650, prop 0x60BE63B8) is above 0
  (else the ship climbs to 10 x its height and the node ends). The search: cGameViewManager vt 0x48 within prop
  0x9650F75F (50 m) of the avatar, filter 0xD9A920; each candidate must be +0xB5E == 0, spatial vt 0x48 true, spatial
  vt 0x58 false, not epic (**the call at 0xDC2B5A**, return 0xDC2B5F), +0x5A8 != the avatar's +0xE7C, another species
  profile than the avatar's, noun 0x18EB45E, [+0xB4C]+0x1D8 != 0x3704748, 5 m (prop 0x829DF2E1) or more away; the
  **last** qualifying one wins and gets stimulus 1 (sender its handle) on the ship's BT. The follow node 0x0519ABF8
  (start 0xDC1430, tick 0xDC1500) hovers 20 m (prop 0xA58ACB11) over it and aims ship +0x508 vt 0x50 at it while it
  is valid; no timeout. The abduct hit `cAbductToolStrategy::OnHit` 0x105A0D0 refuses epics, +0xB58 & 0x200 or
  0x100 and a creature with behaviour 0x3CDBBE9; else +0x2B0 = 4 and the Abducted behaviour (start 0x10323E0;
  update 0x1032BD0 at the beam point calls vt 0xC8(0): +0xB63 = 1, not destroyed).
* ~ Inferred: no check excludes a partner's puppet (it is not the avatar and of another species), so the host's
  ship can pick it and then hovers over a creature the mod keeps on the ground.
* ✔ (runtime) cCreatureAnimal_IsEpic 0xC0C240 touches EAX only and its callers keep **ECX / EDX** across the call:
  a C detour cleared ECX under CreatureStun's caller 0xC0C09E and BT_ClearStimulus faulted at 0xBC9A6D (lab
  20261105-113638, creature_stun). The detour is an asm stub (`p2_h_IsEpic`) that jumps to the game's code unless the
  return address is the pick's, and saves ECX / EDX around the decision there.
* Fix `creature.ufo_puppet`: cCreatureAnimal_IsEpic is hooked and answers "epic" for a partner's puppet only when
  called from 0xDC2B5F (`SporeCoop::UFOPick_IsEpicReturn`), so the pick skips it as it skips epics.
* ✔ Negative (runtime, lane 6, out/lab/20261105-110417 / -110601 / -110850): in the lab `coop.drama start 2` makes
  the ship 20 m over the host's avatar, its BT (`UFO_TickBehavior`) runs 206 ticks (~7 s) and then no more, the
  ship's position (the spatial object's) never changes and the pick never reaches a candidate (`coop.rules ufo`:
  seen=0), with the players 70 m from any other creature and next to the nest alike. So no FAIL run: the guard is
  not verified. ? Why the ship's BT stops after 7 s in the lab (the counter prop 0x60BE63B8 is a guess) is not known.
* Not done: showing the host's ship on the guest (a mirror ship placed at the host's ship every frame).

### The nest heal (BUG-0496, not reproduced)
<a id="creature-nest-heal-fight"></a>

* ✔ (Ghidra; the decompiler drops this block, see `fasm 0xC084B0`) The avatar branch (+0xB58 & 0x200, 0xC09210) of
  the perception update 0xC084B0 clears creature **+0xFB8** ("healing") every pass and sets it when, for a sensed
  object of the list +0x338 of type 0x52AA6122 (a nest): health +0x5E0 below max, the food meter +0xBBC >= 1, the
  friendly-nest test 0xC6ABF0, the surface distance below [0x1582FA8], the brain's node [+0xB4C]+0x1D8 !=
  0x2E5E38D, and no running "Fight" event 0x60995FC (made by 0xD676A0; list `SporeCoop::BT_EventList_ptr`
  0x168B56C, 0xBCA790) of the avatar. **0xC02E10** (creature vt 0x88, from cCreatureAnimal::Update while alive)
  adds max * [0x1687A24] * dt while +0xFB8 is set (not in a cinematic 1 / 2 or mode 0x1654C05); BT node 0x2D852E1
  (tick 0xD7B9A0) is the rest that heals the same way. The avatar's herd, its "home" nest and NestHerdFor 0xD40AF0
  are not read: any friendly nest heals. Event 0x60995FC is the Fight, not a heal event. The ~ of #creature-player-rules ("0xC084B0 ... ~ the heal at
  an allied nest") holds: 0xC6ABF0 gates the heal. The pack branch (+0xB58 & 0x100) sets the same flag.
* ✔ (runtime, lane 6, out/lab/20261105-111330; a probe test, not kept: it passed on the code it was to catch) Both players heal at the home
  nest before and after 30 s of combat key presses near a wild NPC (guest 3.5 -> 16.0, host 3.4 -> 10.0 in 12 s);
  the clicks did not damage the NPC and no Fight event was made, so the Fight case is not tested. Still not
  reproduced. `coop.rules heal` prints the inputs (flag, health, food, cinematic, node, Fight events).

### Eating a body (BUG-0557, not reproduced)
<a id="creature-eat-body"></a>

* ✔ (Ghidra) The cursor 0xD2FFD0 offers "eat" (8) on a creature with +0xB5E (dead) of another species than the
  avatar's whose **+0xB84** (its meat, 0xC0BC70) is above 0, outside mode 0x1654C10 (and 0x16 while the avatar holds
  something the check 0xD2EC30 refuses). Other readers of +0xB84: 0xD70E40, 0xD70FD0, 0xDBDABA / 0xDBDB63 (eat nodes).
* ✔ (runtime, out/lab/20261105-112339) A dead proxy on the guest has the host body's meat (100.00 / 100.00) and dead
  flag 1: the guest's game may eat it. Eating by clicks in the lab (`coop.nest look` + a click on the body, health
  before / after; a probe test, not kept) worked for the host once in 4 tries and never for the guest
  (0 of 3); the click harness is too unreliable to judge (the host's own control failed too). Inferred, as before:
  the player's case is a body missing from the guest's game (the screenshot shows none), not a refused eat.
## Remote bodies over the ground: the height rule (2026-11-05, fix/w44-height)
<a id="creature-height-rule"></a>

One rule for every remote body (BUG-0425): the sender's body is on its ground or off it for a reason its game
knows; the receiver puts it on its own ground in the first case and at the sender's height over the sender's terrain
(agl) over its own terrain in the second (`drive::at_sender_agl`, `drive::air_place`, driver.h).
* ✔ "Off the ground" is the game's grounded byte `cCreatureBase +0x137 == 0` and more than `drive::kAirOver`
  (0.15 m) off `cPlanetModel::GetHeightAt` (`drive::body_airborne`). The same test the partner's PF_JUMPING uses
  (BUG-0376). A creature on a rock is not grounded either (it is drawn at its height, which the rock has on both).
* ✔ Creature NPCs: the host sends `NF_AIR` (NPC state flag 32) and, after the entries of each MSG_NPC_STATE batch, a
  trailer of n x i16: each NPC's height over the host's terrain in cm (older guests ignore it, an older host sends
  none). Player states append `f32 agl` after the Cell scale (kNoAgl unless PF_JUMPING).
* ✔ A host NPC held 4 m over its ground every frame (`coop.npc lift <id> <m> <s>`, after the host's npctrack row, so
  the row is where the game drew it): the host's game kept it at 4.00 m (median over 584 frames). Without npc.air
  the guest's proxy was drawn at a median 1.05 m (only 50 % of its frames over 1 m: walked, the guest's game put it
  down onto its terrain again and again); with npc.air at 4.00 m in every frame, on its ground before and after
  (lab 20261105-112120 FAIL, 20261105-112004 PASS; coop_npc_air.py). `coop.npctrack dump` rows end with `ag mag`
  (the drawn and the model position's height over this game's terrain), `how` 5 = placed in the air.
* ✔ (BUG-0592) A standing own creature moved by a teleport to 0.7 m beside its spot and 0.69 m under the terrain there
  stays there (grounded byte still 1) until it walks; 0.32 m under, or moved by SetPosition (vt 0x38), the game
  puts it back onto its terrain at once (lab 20261105-113116, 113250, 113544). Two own creatures put 0.3 m apart on
  a slope (the guest's own creature with the host's puppet over it, a partner arriving at your spot): the game's
  separation left the guest's own creature 0.35 m under the ground (lab 20261105-113544, off). In the report (lab
  20261105-015350 long_creature, host) the guest came back from the main menu at the host's saved spot (its puppet
  0.2 m from the host's creature) and the host's creature stood 0.7 m to the side at the same radius, agl -0.69,
  for 20 s. `creature.own_ground` (modes.cpp own_ground_tick) puts a standing (< 0.3 m/s), living own creature that
  is more than 0.25 m under GetHeightAt for 0.5 s onto it (game::place); `coop.abilities put <x y z> [1]` teleports
  the avatar as given (1: SetPosition).
* ? Why a creature under the brain-upgrade caption was 0.7 m under (BUG-0504) is still not known; the same fix puts it
  back (the caption pauses the game time; not run under a caption).
## Species relations in one game only: detector reads (2026-11-05, fix/w44-world-state2)
<a id="creature-species-only"></a>

BUG-0596. From the 0.7.7 final check logs (rel/0.7.7 out/lab 20261105-015320 / 015350, 20261105-002042 / 002102).

* ✔ (observed, campaign_ct 20261105-015350) The guest was rejoining: its census listed 0 objects (`guest_rows: 0`)
  while the host's listed 150, its species table was not the host's yet: "2 species with a relation in one game
  only (host)". The detector now skips a sample where one game lists no object while the other lists >= 10.
* ~ (inferred) The other samples were single ones (never two samples in a row): a record the host's game had just
  made (sent once a second by social.cpp, applied by the guest at once) or the guest's own new record (undone within
  0.5 s by enforce_table) between the two games' dumps. The detector reads a guest's Creature state again 1.5 s
  later; a species counts as in one game only when it is so in both reads. Not reproduced in a lab run since.

## The sea monster at runtime: a death cinematic of the drowning player's game (2026-11-05, fix/w44-guest-player3)
<a id="creature-sea-monster-runtime"></a>

BUG-0465 (not fixed). GOG 2024, lab lane 5, creature fixture; follows #creature-sea-monster and #creature-drama-events.
* ✔ (Ghidra) cSeaMonsterEvent (kind 6; factory 0xD4D450, 0x74 bytes, ctor 0xD4C430, vtables 0x147B2B0 / 0x147B058):
  vt+0x38 "can start" 0xD4C5F0 = the avatar is over water deeper than its property 0x9388B444 (default 100) or,
  on the planet's water map, its depth value > property 0x58D4CF9A; vt+0x30 start 0xD4C580 = avatar vt 0xEC, then
  cCreatureModeStrategy action 0xD3353639 {avatar, 0, 9} and 0xD3CD70(0xD3353639): the caption cinematic 5E205931
  ("Dangerous creatures lurk in the deep") first; vt+0x40 update is a no-op (0x1082A20).
* ✔ (runtime, out/lab/20261105-140627, probe: the host's avatar put at the water surface 499.4 m where the floor is
  under 488 m) `drama: sea_monster event starts` 4 s after the put; the host's game then plays the death cinematic
  with the monster (a green crocodile-like head with yellow spikes rising out of the water, mouth open, the camera
  on it: shot kraken_host_2.png), and 8-10 s later the avatar hatches at its nest (hp 10 -> 5, the death's
  penalty). `coop.drama start 6` without deep water shows only the caption (out/lab/20261105-140146).
* ~ superseded (2026-11-06): see #creature-sea-monster-relay. ✔ The guest's game at the same time: its own camera at its own nest, no event, no new object, no effect
  (`coop.effects log`); the host's puppet just dies there (PF_DEAD). The monster exists in the cinematic of the dying
  player's game only.
* CORRECTION (2026-11-06, fix/w45-creature-reports): that run's own guest log has the monster: "effect: from 0 played
  on other id 8A71529C ... game 1 active 1" (fix/w44-guest-player3 out/lab/20261105-140627, guest game log 14:07:10):
  the world effect relay played it; the guest's camera was at its nest, so the shots had none.
* ✔ Negative: the six world effects the host's game made during the cinematic (0x2F49E706, 0xAECAF472, 0x8B61750F
  from 0x6A254B; 0x8A71529C, 0xB3965D1D from 0x932EBE; 0x7484A8E4), each played alone at the avatar
  (`coop.effects load 1 60000 <id>`, out/lab/20261105-140852), are splashes, light and hearts, not the monster:
  ~ inferred, the monster is a cinematic actor (a model of CRG_AvatarDied_SeaMonster's data), not an effect.
* Not done: showing it on the partner's game needs that actor (the cinematic's model and animation) at the puppet
  without the cinematic's camera; the cinematic's data (its actors) was not read.

## Player reports of 0.7.9: bodies eaten in one game, the host's UFO on the guest (2026-11-05, fix/w45-creature-reports)
<a id="creature-p3-reports"></a>

P3 of docs/plan-0.7.9.md. GOG 2024, Ghidra (tools/ghidra/q.sh) and lab lane 6, creature fixture.

### Eating a body: the click, the diet, one meat value per body (BUG-0557, BUG-0383)
<a id="creature-body-meat"></a>

* ✔ (Ghidra) The player's click on an object in Creature is **0xD32FD0** (`SporeCoop::CreatureClickObject`, thiscall
  (strategy, obj) ret 4, called from 0xD35190 with ECX = this - 0x48; ECX is only read for kind 0x1C): it asks the
  cursor kind **0xD30830** (`SporeCoop::CreatureCursorKind`, cdecl(obj)) and runs the avatar's command of that kind.
  Kind 8 (eat): avatar vt 0x84(0xAE66C0(obj, 1, 0)), 0xC032F0(0x40000, 0, FLT_MAX, 0, 0, obj, 0) (a player command
  through 0xC0C070) and the click marker 0xD2F940. Calling 0xD32FD0 with `App::cCreatureModeStrategy::Get()` and a
  body works like a click (`coop.rules click <npc id>`; `coop.rules cursor <id>` prints the kind and the body's state).
* ✔ (Ghidra) The eat node: start 0xD70E40 needs the avatar's target (+0xE7C, cast 0xCE9F6639) to be dead (+0xB5E) with
  **+0x135** set and meat (+0xB84, 0xC0BC70) above 0; the tick 0xD70FD0 walks there (state 0), takes the body
  (+0xB50 = the eater), plays the eat animation (state 1 / 2) and bites in 0xD70A80: meat -= [0x1687A40] x dt, the
  eater's food meter through 0xD85A50, health too when state +9 is set; state 3 ends the meal when the food meter
  (+0xBBC, 0xC0BB20) reaches [0x1687A00]. It bites **only when the eater's species +0x608 is not 0** (0xC0B8E0, named
  `cCreatureBase::IsHervibore` in the March 2017 symbols, ~ a "eats meat" score of the mouths; the name looks wrong).
* ✔ (runtime, out/lab/20261105-225528 / -225751) Both species of the creature fixture have +0x608 = 0: a click on a
  body walks the creature there and holds the eat node (0x02D852E2) with +0xB50 = the avatar, the meat stays 100.
  Earlier "eating by clicks is unreliable" (#creature-eat-body) was this, not the clicks. `coop.rules diet <n>` sets
  the own species' +0x608 for tests; with 1 both players eat (100 -> below 0 in ~8 s).
* ✔ (runtime, out/lab/20261105-225954) **Eating is each game's own act**: the guest ate its proxy of a body to -31,
  the host's body kept 100; the host ate its body to 12, the guest's proxy kept 100. An eaten-out body (meat below 0)
  stays where it is in the eater's game for a while but offers no "eat" any more; the host's game removes its own
  eaten-out body soon (the guest's proxy is then despawned).
* Fix `npc.body_meat` (npc.cpp, MSG_NPC_MEAT 65): the host sends the meat of the bodies a guest mirrors (a change of
  0.5, every 2 s); the guest sends what its creature ate of a proxy (the drop of +0xB84, 5 times a second) and the
  host takes it off its body; the guest's proxy takes the host's value when it is lower. With it the two games are
  within 2 meat during a meal and an eaten-out body goes from both (out/lab/20261105-230313; off: -230449).
* ? Not covered: a body eaten by a guest's own local creature (none: the guest's game has no AI creatures of its own
  in Creature), the bone pile / skeleton an eaten body may leave (not looked at).

### The host's UFO on the guest (BUG-0558)
<a id="creature-ufo-mirror"></a>

* ✔ (runtime, out/lab/20261105-232547) The drama UFO of `coop.drama start 2` lives ~80 s in the lab and flies off at
  ~12 m/s after a few seconds (it climbs and leaves, the pick never reached a candidate: `coop.rules ufo` seen=0).
* ✔ A type 7 ship made on the guest with `UFOAbductionEvent_SpawnUFO(pos, 0, false)` and placed only with the
  spatial SetPositionAndOrientation (vt 0x44 of cast 0x1186577) is put back by its own flight to where it was made in
  every other frame (out/lab/20261105-232301: up to 49 m off): the ship's next position **+0x718** and destination
  **+0x750** (cGameDataUFO, docs/re/space.md; spacecoop's `goto` writes them too) must be the pose as well. With all
  three it follows the host's ship within 0.2-1.9 m at 10 poses a second.
* Fix `creature.ufo_mirror` (player_rules.cpp, MSG_PLAYER_RULE kind 3): the host's ships to the guests, a mirror ship
  per ship on the guest (not ticked: no behaviour tree, no beam), destroyed through game::destroy_creature.
* ~ "The guest is taken": the pick (#creature-ufo-pick) skips the avatar and the avatar's species profile, so in a
  game of one the player's creature is never the ship's target; not changed. The screenshot of the guest did not show
  the ship (it was 20 m over the creatures, above the camera's view); its existence and pose are the game's object list.
* ✔ (runtime, out/lab/20261106-005607, warm games) The pick of #creature-ufo-pick ran on candidates for the first time
  in the lab: `coop.rules ufo` on the host seen=15 skipped=3 (creature.ufo_puppet answered "epic" for the guest's
  puppet 3 times). The cold runs before never reached a candidate (seen=0).
* ? Seen twice in this branch's runs, with and without the mirror: the guest's camera in the planet view from space
  right after the join (the log: "cinematic 8915F9BB still playing in a stage: ended it"); not this fix (not looked at).

### Carried and thrown rocks and sticks (BUG-0458)
<a id="creature-carry-relay"></a>

Follows #creature-hold-slots.
* ✔ (Ghidra) `Creature_HoldObject` 0xC15B20 and `Creature_ReleaseObject` 0xC15EB0 return a **bool** (AL) and take the
  object's **cSpatialObject** (interface 0x1186577), not its cGameData: the spatial's vt **0xB8** casts it to the
  cGameData (0x17F243B, as 0xAE6650 does), whose noun (vt 0x20) is cRock 0x2A8FB3F or an ornament 0x3A2511E; the hold
  slot's +0x20 holds the spatial pointer too. HoldObject: slot index < (+0xE44 - +0xE40) / 0x24 else false; a full slot
  is released first when `replace`; 0xC702B0, the socket attach 0xC120B0 (false when it fails), 0xD7D1A0 (physics
  component 0x116DD1B +0x268 = 0, +0x1F0 = 3), +0x71 = 1. ReleaseObject finds the slot and calls 0xC15D30(slot, a, b):
  vt 0xBC, the slot cleared, the physics added back (0xB3D410 / 0xB454A0 / 0xB542D0), 0xD7D240, vt 0xC0.
* ✔ (runtime) Both species of the creature fixture have one hold slot (sockets 9 and 17) and the cursor over the rock
  is 0x1C / 0 / 1 (walk / none): they cannot pick it up by a click; `HoldObject(avatar, rock, 0, true)` holds it (the
  rock follows the creature, also through a teleport). Released, the game's physics throws it off the hand: 14-23 m in
  the lab, and on the fixture's slope it rolled on for 25-30 s (lab 20261105-234535, 20261105-235138, 20261106-000521).
* ✔ (runtime) A copy teleported along a flying object, or put back again and again where a creature stands, hits that
  creature in its game: "hit by none" 91-348 damage, the guest's creature died (lab 20261105-235733, 20261106-001427,
  20261106-002414). Teleports of physics objects near creatures must be avoided.
* Fix `creature.carry_relay` (player_rules.cpp, MSG_PLAYER_RULE kinds 4 hold / 5 drop / 6 put): the own creature's hold
  of a rock or ornament is sent with the object's noun and the position both games last agreed on (where it lay, or its
  last sent rest point); the partner's game holds its nearest object of that noun within 6 m in the hold slot of the
  player's puppet. The release keeps the copy in the puppet's hand; the thrower's game sends each point where its object
  lies or creeps (slower than 1 m/s, 0.3 m from the last one sent) for up to 60 s; the partner releases its copy at the
  first and teleports it there for 3 s, never within 4 m of its own creature, and gives it a held object's physics
  state (component 0x116DD1B +0x268 = 0, +0x1F0 = 3, as 0xD7D1A0): teleported with its own physics on, the copy kept the
  speed of its release and rolled 5-115 m off (lab 20261106-002616, -002757); with it the two rocks lay 0.05-0.6 m apart
  (lab 20261106-003110, -003250, -003813, -003953). A creature picking the copy up and letting go of it gives it its
  physics back (0xC15D30). `coop.rules carry | hold | drop | findobj | clickobj` for tests.
* ~ Not covered: the flight itself (the partner sees the rock in the puppet's hand until it lands, then at the landing
  point), a throw at a creature (its damage is the thrower's game's), sticks (ornaments of sort 3: the worldobj
  `interactive_ornament` kind syncs their state, not checked together), fruit.

## The host's NPC set while its game stands still, and a guest's leave (2026-11-06, fix/w45-one-game-objects)
<a id="creature-npc-gaps"></a>

* ✔ Observed (lab 20261106-013312 / -014107, creature fixture): `coop.team die` on the host gives the death caption;
  about 8 s later the host's game updates no creature at all (`creatures_updated()` empty, coop.status `busy:
  cinematic,input_lock`, later `,paused`) and stays so until the caption's OK. npc.cpp host_tick returned at once in
  such frames (by design, so the guests keep their proxies), so it never ran the guests' relevance: a guest that went
  170 m away before the death and came back next to the host's body had 0 of the 4-5 host NPCs there for the whole
  freeze (out/lab/20261106-014526, `npc.frozen_send` off). The host's objects stay valid meanwhile (listed in the noun
  list, alive, in their herds); `npc.frozen_send` spawns the newly near ones from them every 0.5 s of a freeze.
  `coop.npc` (host) prints `frozen_s=` (how long no creature was updated), `frozen_spawns=`, `frozen_despawns=`.
* ✔ Observed: `coop.dna add 3000` on the creature fixture shows the brain upgrade caption after ~3 s (input_lock=1,
  busy=cinematic) with the host's simulation frozen the same way; the guest kept 5 of 5 host NPCs within 50 m for 14 s
  (lab 20261106-013649, -014107). The 13 -> 1 loss of BUG-0413's transition_net run (2026-11-01, after the live
  Cell -> Creature start) did not happen on the fixture. ~ Inferred: there the host's npc::on_world_change ran (it
  despawns every NPC at the guests and forgets them; the avatar object changed) and the frozen host sent no spawns
  again; the host logs `npc: world change (avatar ...): N NPC proxies despawned at the guests` now, so the next
  sighting says.
* ✔ Observed (coop_join_during --case editor, lab 20261106-015905 with `npc.leave_locals` off): a guest's
  `coop.leave` destroys its proxies (npc::on_session_end) and its game has no creature of its own then (the update
  filter removed them while it mirrored the host): 1 own creature within 70 m 10 s after its rejoin, where it had shown
  11. At a (re)join the update filter removed every local NPC from the first frame, although the host was in its
  editor: the away state needs 4 s (`host_away=0` at 0-2 s of the join). With the fix the leave hands the proxies over
  (away_locals, next frame, same world epoch) and a guest without proxies keeps its own NPCs until host_here():
  12 own creatures with their AI on (lab 20261106-015417). ~ They idle at their nests (none moved > 1 m in 16 s, small
  0.1-0.4 m moves): not checked whether creatures made by away_locals walk later.
* ✔ (BUG-0385) A creature of a save made while a partner's puppet existed (untuned +0xE84 = 0, a herd like ours) got
  an NPC id in host_tick until remove_saved_strays (once a second) removed it; now it gets none (`npc.stray_ids`).
### Carried rocks: the fast part of a roll (2026-11-06, fix/w45-creature-reports)
<a id="creature-carry-fast"></a>

* ✔ (runtime) A rock released by the nest mound of the creature fixture rolls on at over 1 m/s for up to 40 s: the
  rel/0.7.9 integration run's copy lay 8.47 m off (rel/0.7.9 out/lab/20261106-013035) because only the points slower
  than 1 m/s were sent; 100-143 samples a throw are faster (`coop.rules carry` fast=). The thrower now sends the
  position 10 times a second whenever it moved 0.3 m (`creature.carry_relay`), at any speed; with the copy's held
  physics and the 4 m guard no creature was hit: 16 of 16 PASS (out/lab/20261106-023016, -024443).
* ✔ A creature teleported onto a rock (the lab's warm reset, gopos) takes 47-195 "hit by none" damage from it in its
  game; seen with the relay off too (out/lab/20261106-025551): the test drops the rock 10 m from the start.

## The sea monster on the partner's screen: an effect of the death cinematic (2026-11-06, fix/w45-creature-reports)
<a id="creature-sea-monster-relay"></a>

BUG-0465 (fixed). GOG 2024, game data (read only), lab lanes 3/2.
* ✔ (game data) The cinematics are the script `Spore_Game.package` (and PatchData) type 0x024A0E52, group 0x40890200,
  instance 0x9EA3031A (32 KB of text, `state <name>` blocks). `state CRG_AvatarDied_SeaMonster` (its FNV-1 id 5E205931 is
  the "caption cinematic" of #creature-deep-water): letterbox, `addActor "TheAvatar" avatar`, camera; state
  `...SeaMonster0`: `startEffect "crg_seamonster_attack" crg_seamonster_attack -target "TheAvatar"` and
  `startEffect "splash1" monster_water_splash -target "TheAvatar"`; then the avatar is moved to its nest, a freeze frame,
  the text "Sea monster coming to get-cha.", CRG_Death_Restart. **The monster is the effect crg_seamonster_attack
  0x8A71529C** (FNV-1 of the name), the splash 0xB3965D1D; no model, no creature, no noun. The type 0xB1B104 props of
  group 0xF5F6FDE8 named like the cinematics (CRG_ClaimNest 63B7060A) are only their caption boxes.
* ✔ (runtime) The world effect relay (BUG-0451, since 0.7.4: an effect the game makes at the local player, sent with
  its position) sends both and the partner's game plays them (`creature.effects_relay off`: nothing on the guest's
  screen, out/lab/20261106-031408). The report was of 0.7.3.4, before that relay.
* ✔ (runtime) It was sent with the position only: identity rotation, scale 1 and transform flags 0 (the game then
  ignores the rotation). The game's own transform of an effect is at effect **+0x9C**: {u16 flags (6 here), u16 change
  counter, vec3 pos, f32 scale, 3x3 rotation row-major} (VisualEffect_SetTransform 0xA827B0 -> 0x537E10), the same layout
  as MSG_EFFECT's 0x44 bytes. The host's monster stands with its third row along the planet's up (cos 0.975-0.979); the
  guest's copy was 32 degrees off (cos 0.851, the world y axis) and drawn tilted, in pieces sideways out of the water
  (out/lab/20261106-031307, -030327).
* Fix `effects.world_xform` (effects.cpp): the effect's own transform is sent when it is readable and its position is
  the one read (else the position-only one). The guest's monster then has the host's rotation and scale exactly and
  rises upright out of the water on its screen (out/lab/20261106-031141, -030230). `coop.effects watch <id>` prints
  the last own / received effect of an id with its transform; `coop.nest look <target> 0 <from>` puts the own creature
  at a point facing a target (tests).
* ? Not changed: the sound of the cinematic (music_crg_cin_waterdeath) and the freeze frame stay the dying player's.

## Spice node scale: live-made 10, loaded 1.0 (2026-11-06, fix/w45-placement)
<a id="creature-node-scale"></a>

BUG-0485. GOG 2024. ✔ = observed, ~ = read in the code, ? = inferred.
* ✔ The BUG-0485 SetScale diagnostic (squadron.cpp h_SetScale, `scale 10 set on an object ...`) named the node's
  setter: return address 0xBC0641 in 0xBC02F0 (`SporeCoop::Planet_PlaceCommodityNodes`, a .sym entry), loco vtable
  0x14699E8 (the diagnostic logs the loco's vtable; the 0x1469A28 of the ticket's first note was not seen). It runs on
  the host at the live Cell -> Creature change (campaign_ct, lab rel/0.7.8 20261105-155603: host log only) and in
  space_play on both games.
* ~ 0xBC02F0 makes each node from a placement record: CreateInstance of noun 0x0403DF5C, SetScale(record +0x6C, or
  the prop 0x254CF89), SetPosition(record +0xC).
* ✔ A node read from a save has 1.0 (every node of the creature / tribe fixtures on both games): the scale is not
  saved, so the host's live-made node (10) and the guest's copy from the shared world (1.0) differ, and the host's own
  game shows 1.0 after any load. Not a mod bug at its origin; in co-op each player saw the node at another size.
* Fix `wobj.node_scale` (worldobj.cpp kind `node`, the whole planet, Creature / Tribe / Civ): the state is the host's
  scale; a guest's node that differs by more than 1 % takes it (cSpatialObject vt 0x40 SetScale). Test node_scale.
