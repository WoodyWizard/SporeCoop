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
