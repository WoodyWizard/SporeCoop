# Tribe stage - RE notes (GOG 2016 build, sha256 922af7c7…)

Static analysis only (llvm-objdump + local headless Ghidra on windowed PE copies).
All addresses are VAs in this build. "ret N" = callee pops N bytes (thiscall/stdcall).
Confidence: **V** = verified by disassembly in this build (signature + ret counted),
**P** = partly verified (address/role sure, some argument semantics inferred),
**U** = unverified / inferred, check at runtime.

ModAPI (March2017) names are used where they match. Several ported symbols in
`gog2016_922af7c7.sym` are wrong for this area - see "Symbol fixes" at the end.

---------------------------------------------------------------------------------------------
## 1. Player tribe, members, citizen fields

### Getting the tribe
| What | Address | Signature | Conf |
|---|---|---|---|
| `cGameNounManager::Get` | 0xB3D3D0 | cdecl `()` | V (known) |
| `cGameNounManager::GetPlayerTribe` | **0xBFC710** | thiscall `(nm)` → `cTribe*`; body is `mov eax,[ecx+0x70]; ret` | V |

`nm+0x70` = mpPlayerTribe (already used in shared.cpp). The tribe input code calls
`Get()` + `0xBFC710` everywhere.

### cTribe (noun 0x18C6D19, TYPE 0x4F396A66, main vtable **0x1474910**)
Layout matches ModAPI `cTribe.h` (checked by the getters):

| Off | Field | Evidence |
|---|---|---|
| +0x120 | cSpatialObject (pos +0x124) | known |
| +0x20C | cGameBundleContainer (food) | known |
| +0x334/+0x338/+0x33C | mbRoboTribe / mRoboPopulationCount / mbVisualized | SpawnMember/AddMember |
| **+0x340** | `vector<cCreatureCitizen*> mTribeMembers` (begin,+4 end) | vt 0x90 = 0xC8E990 `lea eax,[ecx+0x340]` |
| **+0x354** | `vector<cSpatialObject*> mSelectableMembers`: holds **citizen+0xC0** (spatial sub-object), adults only | vt 0x94 = 0xC8E710; AddMember pushes `citizen+0xC0` when `GetAge()==1` |
| +0x368 | `cTribeHut* mpHut` | vt 0xAC/0xB0 = 0xC8F850 |
| +0x36C | `vector<cTribeTool*> mTools` | vt 0xBC/0xC0 = 0xC8E880 (both return +0x36C here) |
| +0x380 | social tools vector (types 4,5,6 are also pushed here) | CreateTool |
| +0x550 | mTribeArchetype | 0xC8EC60 |
| +0x19D8 | mPurchasedTools bitmask | 0xC8ED60 set / 0xC8ED80 test |

cTribe vtable (0x1474910), verified slots:

| Slot | Addr | Meaning / signature | Conf |
|---|---|---|---|
| 0x8C | 0xC9A280 | SpawnTribe(...) (creates hut, food mat, initial members) | P |
| 0x90 | 0xC8E990 | `vector<cCreatureCitizen*>* GetTribeMembers()` | V |
| 0x94 | 0xC8E710 | `vector<cSpatialObject*>* GetSelectableMembers()` | V |
| 0x98 | 0xC983B0 | `cCreatureCitizen* SpawnMember(int age)` ret 4 | V |
| 0x9C | 0xC96060 | `void RemoveMember(cCreatureCitizen* c /*null=auto pick*/, bool destroy)` ret 8 | V |
| 0xA0 | 0xC8E800 | `RemoveMember(c)` → vt0x9C(c, true) ret 4 | V |
| 0xA4 | 0xC8E850 | GetTotalFood | V (known) |
| 0xAC | 0xC8F850 | `cTribeHut* GetHut()` | V |
| 0xB4 | 0xC96160 | `cTribeTool* CreateTool(int toolType)` ret 4 | V |
| 0xB8 | 0xC97010 | RemoveTool(cTribeTool*) | P (ModAPI, address matches) |
| 0xBC | 0xC8E880 | `vector<cTribeTool*>* GetTools()` | V |

Iterating members (C++ for the mod):
```cpp
void* tribe = thiscall<void*>(0xBFC710, cGameNounManager_Get());
void** b = *(void***)((char*)tribe + 0x340), **e = *(void***)((char*)tribe + 0x344);
for (void** it = b; it < e; ++it) { void* citizen = *it; ... }
```

### cCreatureCitizen (noun 0x18EB4B7, TYPE 0x4F176642, size 0x1038)
Main vtable **0x146B818** (ctor 0xC26F40 region writes sub-vtables:
+0x04 0x146B804, +0x34 0x146B7C0, +0x58 0x146B758 (behavior list),
**+0xC0 0x146B658 (cSpatialObject/locomotive)**, +0x194 0x146B64C, +0x590 0x146B630,
**+0x5A8 0x146B5C8 (cCombatant)**, +0x670 0x146B5B0).

Fields (verified in code unless noted):

| Off | Field | Evidence |
|---|---|---|
| +0x21 | destroyed flag (cGameData) | 0xB182E0 |
| +0x24 | mID (cGameData) | U (ModAPI) |
| +0x30 | political ID (cGameData) | U (ModAPI); Init sets it via vt0x48(tribe->vt0x4C()) |
| +0xC0 | cSpatialObject: GetPosition vt0x2C, SetPosition 0x38, SetOrientation 0x3C (same as animal, see 05-re-notes) | Init 0xC27310 uses 0x38/0x3C |
| +0xC0+0x6C = **+0x12C** | byte **IsSelected** | spatial vt0x50 = 0xAD26A0 `mov al,[ecx+0x6C]` |
| +0x5A8 | cCombatant; HP +0x5E0, MaxHP +0x5D4 | ctor vtable write; offsets as cCreatureAnimal |
| +0xB28 | species key | DoAction neighbour 0xC27810 |
| +0xB34 | age (0 = baby, 1 = adult) | 0xC0B880 GetAge, 0xC0B890 IsAdult |
| **+0xB4C** | AI/behavior-tree data (action queue list at AI+0x60C) | DoAction, Update |
| +0xB5E | byte flag (skip locomotion/anim when set) | Update |
| +0xFC0..+0x1002 | ModAPI fields (FC0=3, FF4 selection group, FF8, FFC handheld item, 1000, 1002) | reset in vt0xCC 0xC243D0 |
| +0xFF4 | selection group (Ctrl+N) - set 0xC228F0, get 0xC22900 | V |
| +0xFFC | mCurrentHandheldItem | V (writes) |
| +0x1010 | mpOwnerCity (null in tribe stage → AI runs) | V |
| **+0x1014** | **mpOwnerTribe** | V (DoAction, Init) |
| +0x102C | mSpecializedTool (tool type held, e.g. 9 = heal); getter **0xC22EE0** | V |

Current action ("what is the member doing"):
* `0xBCA410` thiscall(`ai` = citizen[+0xB4C]) → `ActionRec*` or null (first node of list at ai+0x60C, +8). **V**
* ActionRec: `+0x00 int actionId` (CitizenAction), `+0x28 cGameData* target` (intrusive),
  `+0x2C Vector3 targetPos` (copied from target), `+0x38 bool hasTarget`,
  `+0x39 App::Property param` (type word at +0x4B; 0x31 = Vector3 used by WalkTo). **V** (DoAction 0xC276F0, 0xBCB0C0, 0xCD0400)

Useful citizen helpers: `0xC233F0` IsChieftain (thiscall, P), `0xC0C4A0` cdecl
`cCreatureCitizen* CastToCitizen(cGameData*)` (V), `0xC22DD0` = vt0xF0 `PlayVoice(int code)`
(0 eat, 3 tool, 7/8/10 battle, 0x29A illegal, 0x29B select, 0x29C arrived; V).

---------------------------------------------------------------------------------------------
## 2. How the player issues orders (cTribeInputStrategy)

Getting the strategy: `TribeUI = *(void**)0x169C2A8` (getter **0xCD41E0**, cdecl, V);
`strategy = TribeUI->vt[0x70]()` (used by 0xB1B2F8 and ChooseInputActionID; P - class of
0x169C2A8 not identified, probably the tribe game-mode UI object).

Message handler: **0xCD1D10** = `cTribeInputStrategy::HandleMessage(uint32 msg, IMessage*)`
(IUnmanagedMessageListener sub-object at strategy+0x3C; listener vtable 0x1477CA0). Relevant
messages: `0x506D38C9` → DoActionGeneric(hovered); `0xFC6F3B` → 0xCD0790 click (walks via
0xCD0400); `0xE39174`/`0xE615BF` → selection clicks; `0xFC6D2C` → deselect; `0x2C0EDC1` →
`tribe->SpawnMember(1)`, `0x2C0EDC2` → `RemoveMember(0,1)` (debug/cheat-like commands).

### Call graph of every player order (all V)
```
right-click / action msg 0x506D38C9
  DoActionGeneric 0xCD0BD0  thiscall(strategy, cGameData* hovered) ret 4
    id = ChooseInputActionID 0xCCF0E0  cdecl(cGameData* obj, const Vector3* pos|0) -> TribeInputAction
    switch(id):
      1      -> WalkSelectedTo 0xCD0400(&groundPos)
      0x11..0x1A -> DoToolActionForAllSelected 0xCD0960(tool)
      0x28 (heal) -> builds vector of selected healers, calls DoActionForObjects 0xCCEAE0(vec,5,target,0)
      others -> DoActionForAllSelected 0xCD08F0(citizenAction, target, 0)
click msg 0xFC6F3B -> 0xCD0790 -> WalkSelectedTo 0xCD0400(pos)   (also called from UI 0xE0BBBD)

DoToolActionForAllSelected 0xCD0960  thiscall(strategy, cTribeTool*) ret 4, returns bool
   damaged tool -> DoActionForAllSelected(24 Repair); type 1..9 -> (3 GrabTool); type 10 firepit -> (12 Party)
DoActionForAllSelected 0xCD08F0  thiscall(strategy, int action, cGameData* target, App::Property* p) ret 0xC
   sel = GetTerrainCursor()->vt[0x5C](&fixed_vector)       // selected spatial objects
   0xCCFE20 thiscall(strategy, vec*, action, target, p) ret 0x10   // + feedback sound per action
     DoActionForObjects 0xCCEAE0 (ecx unused) (vector<cSpatialObject*>* vec, int action, cGameData* target, App::Property* p) ret 0x10
        for each o: c = o->vt[0xB8](0x4F176642) /*Cast to citizen*/; c->DoAction(action, target, p)
        returns 1 if vec empty else 0
WalkSelectedTo 0xCD0400 (ecx unused) (const Vector3* pos) ret 4
   rejects water/unreachable (cPlanetModel 0xB830A0 / 0xB88670 region check -> error sound 0xB062B167)
   for each selected citizen: if current rec is WalkTo with Vector3 param within 1.0 of pos: skip, else
        rec = c->DoAction(4, nullptr, nullptr); App::Property::SetValueVector3 0x6A3570 (rec+0x39, pos)
   then c->vt[0xF0](4)  // voice
```

### cCreatureCitizen::DoAction - **0xC276F0** (V; sym "medium" is correct)
`ActionRec* __thiscall DoAction(cCreatureCitizen* this, int action, cGameData* target, App::Property* param)` ret 0xC.
Body: `cur = 0xBCA410(ai)`; `rec = 0xBCB0C0(ai, action, target, 1 /*replace queue*/)` (thiscall ret 0xC);
copies `param` into rec+0x39 (0x542B80), copies target's position to rec+0x2C, then re-plans.
**No null check on `this`.** Also called by AI code (ac8abb…, ce1639…, d1efc5…), so hooking it
alone cannot tell player orders from AI.

### Action enums (from DoActionGeneric switch; TribeInputAction → CitizenAction)
| InputAction (ChooseInputActionID) | → CitizenAction / call |
|---|---|
| 1 | walk to ground (0xCD0400) |
| 7 | invalid (error sound) |
| 9 | 24 Repair |
| 0xA | 7 Attack (citizen) |
| 0xB | 10 Hunt (animal) |
| 0xC | 15 RaidHuts (hut/tool of other tribe) |
| 0xE, 0x10 | 9 GiftFood (with food-cost check) |
| 0x11–0x1A | tool click → DoToolActionForAllSelected (3 GrabTool / 24 Repair / 12 Party) |
| 0x1B | 14 Bundle |
| 0x1C, 0x1D | 27 GatherMeat |
| 0x1E–0x20 | 0 Eat |
| 0x21 | 8 RaidFood |
| 0x22 | 1 Fish (also sets fishing hotspot) |
| 0x23 | 13 CollectEgg |
| 0x24, 0x25 | 2 Gather |
| 0x27 | 26 Recruit (social) |
| 0x28 | 5 Heal (only members holding heal tool, 0xC22EE0()==9) |
| 0x29 | 20 FeedWild or 25 Tame |
CitizenAction 4 = WalkTo (param = Vector3 property), 6 Sleep, 28 ReturnToTribe (ModAPI enum).

---------------------------------------------------------------------------------------------
## 3. Executing a command on a given member (host)

* Action on object: `DoAction(c, action, target, nullptr)`: **0xC276F0**, thiscall, 3 stack args.
  Equivalent to what the UI does (0xCCEAE0 just loops this). Alternatively build a vector
  `{c+0xC0}` and call 0xCCEAE0(vec, action, target, 0) (ecx can be anything).
* Walk: `rec = DoAction(c, 4, nullptr, nullptr); SetValueVector3(rec+0x39, &pos)`
  (`App::Property::SetValueVector3` 0x6A3570 thiscall(prop, const Vector3*) - sym high).
  Optional voice: `c->vt[0xF0](4)`. (Simpler alternative, U: pass an App::Property holding the
  Vector3 as DoAction's 4th arg - DoAction copies it to rec+0x39.)
* Grab tool: `DoAction(c, 3, tool, nullptr)`; party: `(12, firepit)`; repair `(24, tool|hut)`.
* WalkSelectedTo's reachability check (0xB830A0 on cPlanetModel 0xB3D420) is skipped this way:
  either run it on the guest before sending (call original logic) or accept.

---------------------------------------------------------------------------------------------
## 4. Per-frame update / AI (to suppress on mirrors)

`cCreatureCitizen::Update` = vt 0x60 = **0xC249F0**, thiscall `(this, int dtMs)` ret 4 (V; sym medium is right).
Order inside:
1. return if `!byte[+0x135]` or destroyed;
2. `this->vt[0x70](dtSec)`;
3. **AI**: if `ai=[+0xB4C]`, `*ai` and `city([+0x1010])==0`: build flags, then
   `BehaviorTreeTick` **0xD995C0** - cdecl, 9 dwords (`add esp,0x24`):
   `(float dt, double time, void* btRoot=*ai, cCreatureBase* self, void* ai+0x1D0, int ai[+0x5E8], uint flags, int x)`;
4. if `!byte[+0xB5E]`: `vt[0x64]()`, `vt[0x68](dt)`: locomotion/animation update (keep these on mirrors);
5. misc (vt 0x58 when swimming, hit-sphere at +0x1030, `0xC214F0`).

0xD995C0 is shared: called from cCreatureAnimal::NPCTickAI (0xC09AE9), AvatarTickAI (0xC0409F),
citizen Update (0xC24BAB) and 0xD5842C/0xD6073A/0xD60821/0xD60B40 (other creature kinds).
**Suppress mirrors' AI by hooking 0xD995C0 and skipping when `self` (4th arg) is a mirror.**
(Hooking 0xC249F0 and not calling the original would also kill locomotion/animation.)

**After a world load (GOG 2024 guest, 2026-10-24) ✔ runtime:** the guest's game time stays paused
(`cGameTimeManager::Get()` +0x48 = 3, bit 0 = paused) for ~7 s after the guest is in the world
(black screen, ~23 fps): no citizen `Update` runs (BehaviorTreeTick 0 calls), so mirrors neither walk
nor animate and only SetPosition (drive.h's nudge) moves them. The 2016 guest is already running at
that point. Counters: `coop.tribe` on a guest prints `ai_skipped=<skipped>/<all BT ticks>` and a
`drive err= nudges= kept= teleports=` line per mirror.

Positioning a member directly: spatial at `c+0xC0`: SetPosition vt 0x38 `(const Vector3*)`,
SetOrientation vt 0x3C `(const Quaternion*)`, GetPosition vt 0x2C - same as cCreatureAnimal
(docs/05-re-notes.md 5.4; MoveTo 0xE0 / StopMovement 0xEC on the locomotive object for smooth
driving, as npc.cpp does). Animations: `cCreatureBase::PlayAnimation` 0xC12430 (ret 0xC).
Pending actions are in the AI queue; without BT ticks a queued DoAction is not executed (U).

---------------------------------------------------------------------------------------------
## 5. Creating / removing members, buying tools

| Function | Address | Signature | Conf |
|---|---|---|---|
| cTribe::SpawnMember (vt 0x98) | 0xC983B0 | thiscall(tribe, int age) ret 4 → citizen or **0 if population cap** (`0xC8EC60` max pop) | V |
| cTribe::CreateCitizen | 0xC8E720 | thiscall(tribe, int age) ret 4: `CreateInstance(0x18EB4B7)`; `c->vt[0xE8](city=0, tribe, age, &zeroVec)`; `c[+0x5A8]->vt[0x2C](1)`; `cGameBehaviorManager::Get()(0xB3D330)->vt[0x38](c+0x58)`; age 0 → placed at tribe position | V |
| cTribe::AddMember | 0xC95EF0 | thiscall(tribe, citizen, bool) ret 8; pushes into +0x340, adults also into +0x354; names it | V |
| cCreatureCitizen::Init (vt 0xE8) | 0xC27310 | thiscall(c, cCity*, cTribe*, int age, const Vector3* pos /*zero vec 0x1696354 = auto*/) ret 0x10; sets +0x1010/+0x1014/+0xB34, political id, position | V |
| cTribe::RemoveMember (vt 0x9C) | 0xC96060 | thiscall(tribe, citizen, bool destroy) ret 8: deselects, removes from +0x340/+0x354, behavior-manager remove, `DestroyInstance` when destroy | V |
| "baby born" | 0xCD7490 | thiscall(TribeUI, Vector3 pos **by value**) ret 0xC: `playerTribe->SpawnMember(0)`, set pos/orient, anim 0x640358F, "vcode_baby_born_cry"; callers 0xCD7B11 (in 0xCD78E0), 0xCD8EBE (in 0xCD81E0) | P |
| cTribe::CreateTool (vt 0xB4) | 0xC96160 | thiscall(tribe, int toolType) ret 4: `CreateInstance(0x18C8F0C)`, political id, `SetToolType` (tool vt 0x60), model from `GetTribeToolData` 0xC9D040, push to +0x36C (and +0x380 for 4/5/6). Does **not** position/pay. | V |
| player tool placement start | 0xD130D0 (code at 0xD13C05) | palette item → `GetToolByType`, `tribe->CreateTool(type)`, `tool->vt[0x64](tribe)` (SetTribe), `0xC9D2C0(tool)`, starts drag via 0xD09FA0 | P |
| placement finish | 0xD10F90 | finalizes placed structure; for a cTribeTool sets purchased bit (0xC8ED60), fx; food charge path not located | P/U |
| GetToolByType | 0xC8F860 | thiscall(tribe, int type) ret 4 | V (sym high) |

Member purchase: the hatching path is 0xCD7490 (SpawnMember(0) → baby, becomes adult later,
age +0xB34 0→1). Where food is deducted for "buy baby" was not found (U). For the mod, buying
is best executed on the host by the original UI path, or by `SpawnMember(0)` + food via
`UpdateFoodVisuals`/bundles (shared.cpp).

---------------------------------------------------------------------------------------------
## 6. Selection

* Selection lives on the spatial objects: byte `spatial+0x6C` (`citizen+0x12C`).
  IsSelected = spatial vt 0x50 (0xAD26A0). **SetSelected = spatial vt 0x54; for citizens
  0xC227E0**: thiscall(`spatial = citizen+0xC0`, bool sel) ret 4; plays the "select" voice
  (vt0xF0(0x29B)) on a new selection (V).
* `cGameTerrainCursor::GetTerrainCursor` **0xB30E80** (cdecl; sym "medium" - V), cursor class
  vtable 0x1460A98. Candidate list pointer at **cursor+0x164** (`vector<cSpatialObject*>*`, set by
  vt 0x58 = 0xB2FC40 `SetObjects`). vt 0x5C = **0xB310C0 `GetSelectedObjects(vector* out)`**
  (AddRefs via spatial vt 0xBC), vt 0x64 = 0xB2FFA0 deselect all, vt 0x68 count candidates,
  vt 0x74 = 0xB300C0 `UpdateSelectedObjectCount` (+0x15C), vt 0x7C = 0xB31140 box select,
  vt 0x80 = 0xB308F0 apply set, vt 0x9C = 0xB30F90 / vt 0xA0 = 0xB314C0 click select.
  **Every selection change calls spatial vt 0x54 on objects of the candidate list** (V).
* The tribe candidate list is presumably `&playerTribe->mSelectableMembers` (+0x354) - **U**
  (check at runtime: `*(void**)(cursor+0x164) == tribe+0x354`).
* Restricting selection: hook 0xC227E0 and drop `sel=true` for citizens not owned by the local
  player (`citizen = this - 0xC0`). Covers click, box and group select.

---------------------------------------------------------------------------------------------
## 7. Other (NPC) tribes

* Same classes: NPC tribes are `cTribe` with `mpTribePlanner` (+0x290, AI) and members are
  `cCreatureCitizen` with `+0x1014` = their tribe. Spawn: `Simulator::SpawnNpcTribe` 0xC932E0
  (sym high; it calls `GetToolByType(10)`/`CreateTool(10)` at 0xC9353D). NPC tribe controller:
  `cNpcTribeController::Get` 0xCC8C40 (sym high).
* Planner AI behaviours are functions referenced from .data (e.g. 0xCE36D0, 0xCE38F0,
  0xCE3B00 - "fldz; ret" score functions, use CreateTool/SpawnMember) (P).
* Enumerate all tribes: `cGameNounManager::GetData` **0xB21260**, thiscall(nm, create, clear,
  add, filter, uint32 nounID) ret 0x14 (V by ret count; semantics = ModAPI template), with
  nounID 0x18C6D19 and a filter `obj->vt[0x20]() == nounID`. Or walk all citizens with noun
  0x18EB4B7 and group by `+0x1014`.

---------------------------------------------------------------------------------------------
## Implementation recipe (co-op "Chiefs")

**Ownership.** Host keeps `owner[netId]` for every player-tribe member; netIds assigned by the
host (pointer→id map like npc.cpp). Initial split: alternate by index in `tribe+0x340`.
New members (SpawnMember / AddMember 0xC95EF0 hook on host) go to whoever bought them (host:
default host).

**Guest mirrors.** Both players load the same tribe save, so the guest already has a local
player tribe. Recommended: reuse its members as mirrors (keeps UI, food mat, selection working):
1. On sync start the host sends the roster `{netId, age, specializedTool, pos, owner}`; guest maps
   by index, then reconciles counts with `SpawnMember(age)` / `RemoveMember(c, true)` on its
   local tribe (SpawnMember returns 0 at the population cap → use `CreateCitizen`+`AddMember`).
2. Hook **0xD995C0** (cdecl, 9 dwords): if `self` is a guest mirror → return without ticking.
   (Locomotion/animation in Update still run.)
3. Drive mirrors from host snapshots (10 Hz): position/orientation via spatial vt 0x38/0x3C or
   MoveTo (0xE0) like `drive::` in npc.cpp; HP `+0x5E0`; handheld item `+0xFFC`, tool `+0x102C`;
   current action id from `0xBCA410(c[+0xB4C])->actionId` to pick animations (U: needs a way
   to show tool models; not researched).
4. Guest local NPC tribes / animals: host-owned, mirrored by existing npc.cpp style (out of scope here).

**Order capture (guest).** Hook two functions; when on guest, send and do not call original:
* **0xCCEAE0** `DoActionForObjects(vec*, action, target, param)` (hook as thiscall, ecx ignored,
  ret 0x10): read `vec` (cSpatialObject* = citizen+0xC0), map to netIds, send
  `CmdAction{u16 ids[], u8 action, target}`; return 0 so the UI plays its confirmation sound.
* **0xCD0400** `WalkSelectedTo(const Vector3* pos)` (thiscall, ecx ignored, ret 4): collect
  selection via `GetTerrainCursor()->vt[0x5C](&vec)` (or remember last selection), send
  `CmdWalk{ids[], Vector3 pos}`.
  (Optionally call the original's reachability check first; skipping it is harmless.)
* Target encoding: `{u32 nounId (vt 0x20), Vector3 pos, u16 netId if it is a mirrored creature/
  citizen}`. Host resolves: netId → object, else nearest object with that noun within ~3 m
  (plants/huts/tools/fish spots exist on both sides from the same save).

**Order execution (host).** On `CmdAction`: for each id owned by the sender: `DoAction(c, action,
target, nullptr)` (0xC276F0). On `CmdWalk`: `rec = DoAction(c, 4, 0, 0); SetValueVector3(rec+0x39,
&pos)` (0x6A3570). Validate ownership and `c[+0x1014] == playerTribe`, alive (`!c[+0x21]`).

**Selection restriction (both sides).** Hook **0xC227E0** citizen SetSelected; if `sel` and
`owner[c] != me` → call original with `false` (or return). Host also uses it so the host cannot
command guest members.

**Purchases.** Food is already shared (shared.cpp). Tool/member purchases by a guest:
first version - forward `CmdBuyTool{type, pos}` → host `tool = tribe->vt[0xB4](type)`,
`tool->vt[0x64](tribe)`, set position via tool spatial (+0x34) vt 0x38, charge food; the
placement finalizer (0xD10F90: footprint, purchased bit 0xC8ED60) is skipped → **U, test**.
Guest gets the tool through a tool snapshot/rebuild. Alternatively keep building host-only.

---------------------------------------------------------------------------------------------
## Symbol fixes (proposed lines for overrides_922af7c7.sym - not applied)
```
Simulator::cGameNounManager::GetPlayerTribe=0x00BFC710 ; verified asm: mov eax,[ecx+0x70]; ret
Simulator::cTribeInputStrategy::DoActionGeneric=0x00CD0BD0 ; verified: thiscall(hovered) ret 4, switch on ChooseInputActionID (sym 0xCD0400 was wrong)
Simulator::cTribeInputStrategy::WalkSelectedTo=0x00CD0400 ; verified: (const Vector3*) ret 4, ecx unused, DoAction(4)+SetValueVector3(rec+0x39)
Simulator::cTribeInputStrategy::DoActionForObjects=0x00CCEAE0 ; verified: (vector<cSpatialObject*>*, action, target, prop) ret 0x10, ecx unused
Simulator::cTribeInputStrategy::DoActionForAllSelected=0x00CD08F0 ; verified ret 0xC
Simulator::cTribeInputStrategy::DoToolActionForAllSelected=0x00CD0960 ; verified ret 4
Simulator::cTribeInputStrategy::ChooseInputActionID=0x00CCF0E0 ; verified cdecl (obj, pos)
Simulator::cTribeInputStrategy::HandleMessage=0x00CD1D10 ; verified (listener at +0x3C)
Simulator::cCreatureCitizen::DoAction=0x00C276F0 ; verified thiscall(action,target,prop) ret 0xC, returns ActionRec*
Simulator::cCreatureCitizen::Update=0x00C249F0 ; verified vt 0x60, thiscall(int dtMs) ret 4
Simulator::cCreatureCitizen::Init=0x00C27310 ; verified vt 0xE8 (city,tribe,age,pos*) ret 0x10
Simulator::cCreatureCitizen::SetSelected=0x00C227E0 ; verified spatial vt 0x54, this=citizen+0xC0
Simulator::cCreatureCitizen::GetSpecializedTool=0x00C22EE0 ; verified [+0x102C]
Simulator::BehaviorTreeTick=0x00D995C0 ; verified cdecl 9 dwords, arg4 = creature
Simulator::cTribe::CreateCitizen=0x00C8E720 ; verified (age) ret 4
Simulator::cTribe::AddMember=0x00C95EF0 ; verified (citizen,bool) ret 8
Simulator::cTribe::RemoveMember=0x00C96060 ; verified vt 0x9C (citizen,bool destroy) ret 8
Simulator::cGameTerrainCursor::GetTerrainCursor=0x00B30E80 ; verified (creates noun 0x18C40BC)
Simulator::cGameTerrainCursor::GetSelectedObjects=0x00B310C0 ; verified vt 0x5C
Simulator::cTribeUI::Get=0x00CD41E0 ; verified returns [0x169C2A8], class name unverified
```

## Open questions
* Which object holds the tribe selection candidate list (cursor+0x164) - runtime check.
* Food cost / validation path for buying a tool or a baby (UI side) not found.
* How the held-tool model / gather/eat animations are shown - needed for good-looking mirrors.
* Whether mID (+0x24) is identical between host and guest after loading the same save.

---------------------------------------------------------------------------------------------
## Co-op features (pincer, duet, healers, chief, epic)

Static analysis only (llvm-objdump + REA/Ghidra on the full exe, plus game data from
`Data/PatchData.package`). Marks: **✔** verified in this build (prologue / `ret N` / call sites),
**~** likely (role sure, some semantics inferred), **?** unknown / check at runtime.
(✔ ≈ V, ~ ≈ P, ? ≈ U in the sections above.)

### A. Combat damage (goal: "pincer")

The damage path is a virtual call on the **cCombatant** sub-object (`creature+0x5A8`, slot
0x18 = TakeDamage). Every override has the same signature:
`int __thiscall TakeDamage(cCombatant* this, float dmg, uint32 attackerPolID, int type,
const Vector3* pos, cCombatant* attacker)` **ret 0x14** ✔ (the mod's `TakeDamage_t` matches).

| Class (victim) | combatant vtable | TakeDamage | Calls | Conf |
|---|---|---|---|---|
| cCombatant (base) | 0x1469070, … | **0xBFCEF0** | stores `attacker` at combatant **+0x28** (intrusive, = last attacker), HP −= dmg at +0x38, death at ≤0 | ✔ |
| cCreatureBase | 0x146AE58 | **0xC1A4E0** | invulnerability checks (avatar, space mode…), then 0xBFCEF0 (call at 0xC1A942) | ✔ |
| cCreatureAnimal | 0x146A948 | 0xC075A0 (the hook in game.cpp) | 0xC1A4E0 | ✔ |
| **cCreatureCitizen** | **0x146B5C8** | **0xC26920** | 0xC1A4E0, then tribe logic (below) | ✔ |
| cTribeHut (noun 0x1E4DAAE, main vt 0x1474FB0, combatant at +0x120) | 0x1474E50 | 0xC9CA20 | 0xBFCEF0; relationship events 0x530CF04/05 | ✔ |
| cTribeTool (noun 0x18C8F0C, main vt 0x1475210, combatant at +0x120) | 0x1475178 | 0xC9D7F0 | 0xBFCEF0 | ✔ |

**Answer: tribe combat does NOT go through the mod's damage filter for citizen victims.**
`game.cpp` hooks 0xC075A0 = cCreatureAnimal only, so it fires in the tribe stage only when an
*animal* is hit (hunting, epic creature, pets). Citizen-vs-citizen hits go
0xC26920 → 0xC1A4E0 → 0xBFCEF0. Hooking **0xC1A4E0** catches animals + citizens with one hook;
0xBFCEF0 additionally catches huts/tools/vehicles/UFOs.

Citizen TakeDamage 0xC26920 details ✔:
* `this` = citizen+0x5A8; victim citizen = `this − 0x5A8`; victim tribe = `this+0xA6C`
  (= citizen **+0x1014**, mpOwnerTribe).
* `attacker` (arg5) is a cCombatant*; `attacker->vt[0x0C]()` returns the owner object
  (`lea eax,[ecx−0x5A8]` for creatures), `attacker->vt[0x08]()` its spatial (owner+0xC0).
  The function itself casts the owner to a citizen (`0xAC7FD0(obj, 0x18EB4B7)` = cdecl
  object_cast by noun id via vt 0x20) and reads `+0x1014` → attacker tribe. Non-citizen
  attackers: cast with 0x18EB45E (animal) → herd.
* Survived + victim has a tribe + `attackerPolID != −1` → `ApplyRelationship(victimPol,
  attackerPol, 0x530CF00, 1.0)` (victim tribe gets angry at attacker) ✔.
* Killed: event-log entries (0x7C5B1068 "you killed …", 0xAF9432AB "member killed"),
  message 0x5C80207 {victim, attackerTribe, attackerHerd} ✔.
* Base 0xBFCEF0 keeps the last attacker at combatant+0x28 - handy to ask "who hit this enemy".

**Pincer recipe (host).** Hook **0xC26920** (thiscall, ret 0x14, same typedef as game.cpp):
```
victim = this-0x5A8; vt = victim[+0x1014];  att = attacker ? attacker->vt[0xC]() : 0
if att is citizen (vt 0x20 == 0x18EB4B7) and att[+0x1014] == playerTribe and vt != playerTribe:
    owner = coop owner of att;  remember (vt, owner, now)
    if the other player hit any member of vt within N s -> dmg *= k   (change arg1, call original)
```
Raids on buildings: same idea on 0xC9CA20 (hut, owner = this−0x120, tribe = hut+0x230 ✔ via
vt 0x58 = 0xC9C430) and 0xC9D7F0 (tool, tribe = tool+0x1F8 ✔ 0xC9DB60).

### B. Relationships / social (goal: "duet")

Relationships live in **cRelationshipManager**, keyed by political IDs; nothing per-tribe stores
the "feelings" value (only timers). Political ID of any cGameData = `vt[0x4C]()` =
**0x5AADB0** `mov eax,[ecx+0x30]` ✔ (cTribe and citizen; setter vt 0x48 = 0x804880).

| Function | Address | Signature | Conf |
|---|---|---|---|
| cRelationshipManager::Get | 0xB3D390 | cdecl() → mgr (mode-dependent switch) | ✔ |
| CalculateRelationship | **0xD00940** | thiscall(mgr, polA, polB, int 0) ret 0xC → float ST0, = 0xD05A60 clamped to [g 0x14797FC, g 0x1479800] | ✔ |
| CalculateRelationshipAbsolute | 0xD05A60 | thiscall(mgr, polA, polB, int) ret 0xC → float ST0 | ✔ |
| GetRelationshipLevel | 0xD008D0 | thiscall(mgr, float value) ret 4 → 0..4 (thresholds mgr+0x10..+0x1C; ≥2 = friendly side) | ✔ / meaning ~ |
| **ApplyRelationship** | **0xD06280** | thiscall(mgr, polA, polB, uint32 eventId, float scale) ret 0x10 → float ST0 (caller `fstp st0`) | ✔ |
| (event store) | 0xD05DD0 | thiscall(mgr, data, polA, polB, event, scale) ret 0x14; scale 0 → no-op; in tribe mode refreshes the tribe UI | ✔ |

Convention: `polA` = the side whose feelings change, `polB` = the other side. The tribe debug
text "This Tribe Feelings for Player" (0xC947CE) = `CalculateRelationship(npcTribePol,
playerTribePol, 0)` ✔.

Tribe events seen (ids are hashes, names unknown):
| Event | Where | Use |
|---|---|---|
| 0x530CF00 | citizen TakeDamage | attacked (scale 1) ✔ |
| 0x530CF02 | gift tick 0xDBC790 | gift food received (scale 1) ✔ |
| 0x530CF04 / 05 | hut/tool TakeDamage | building attacked / destroyed ~ |
| 0x530CF06 | 0xDBB830, 0xDBC790, 0xCC6E57 | scale = −current → reset to neutral ~ |
| **0x530CF07** | social tick 0xDA2FA0, 0xDAE970 | scale = (target − current) → **raw delta**; used to jump to the "friendly/allied" threshold (g 0x1582368 / 0x158236C, tuning, 0 in the file) ~ |
| 0x530CF0A / 0B | tribe HandleMessage 0xCD2D7E (msg 0x46AB559, cheat/debug) | ± by sign ~ |

cTribe fields (from its serializer table at 0x1355xxx, offsets ✔): `+0x2C4 mInitialRelationship`,
`+0x2C8 mChieftainRespawnTimer`, `+0x2CC mGiftRelationshipDecayTimer` (zeroed on social success),
`+0x260 mpFoodMat`, `+0x19CC mpDomesticatedAnimalsHerd`, `+0x19D0 mpTotemPole`.

**How the performance works** (✔ structure, ~ names). Citizens' current behaviour-tree node id is
`ai[+0x1D8]` (`ai = citizen[+0xB4C]`). BT action table (0x1594xxx, entries `{nodeId, enter,
tick, exit}`; tick = cdecl, **arg1 = the creature**, plain `ret`, returns bool):
| Node id | tick | Behaviour |
|---|---|---|
| **0xB46E8803** | **0xDA2FA0** | social performance with instruments (tool types 4/5/6 pick anims; chief has own anims) |
| 0x546E8804 | - | NPC side "being socialised" (chooser returns InputAction 0x2D) |
| 0x562C0FB | 0xDB9B20 | heal (section C) |
| 0x4623E0C | 0xDBC790 | gift food |
| 0x461300F | 0xDABDE0 | eat |
| 0x4611FF4 | 0xDAF810 | sleep (heals) |

The social tick looks up a **session record**: `idx = ai[+0x600][+0x1A8]`,
`s = SocialSession_Get(idx)` = **0xD9F500** cdecl(int) → record+4 or 0 (pool 0x15945A8/AC,
stride 0x210) ✔. Session: `+0x04 state` (6 = finished), `+0x08` player tribe, **`+0x0C` target
NPC tribe** ✔, `+0xE8` mini-game UI object (vtable 0x14823F8; **progress float at +0x68**,
success when ≥ 1.0 - 0xE31110), `+0xEC` result mode (1/2 → which threshold). On success:
ApplyRelationship(npcPol, playerPol, 0x530CF07, threshold − current), npcTribe+0x2CC = 0.
Player orders: social mode (`TribeUI[+0x390] == 0`) + click other tribe's hut → InputAction
0xE (level < 2) / 0xF (≥ 2, refused with a sound); attack mode (+0x390 == 1) → 0xC → CitizenAction
15 ✔. Which CitizenAction id starts the 0xB46E8803 node was not pinned down (?).

**Duet recipe (host, no hook needed):** each ~0.5 s, for every player-tribe member `c` with
`ai=c[+0xB4C]` and `ai[+0x1D8] == 0xB46E8803`: `s = 0xD9F500(ai[+0x600][+0x1A8])`,
`target = s[+0xC]`; group owners per target. If both players have performers on the same
target: `ApplyRelationship(Get(), target->vt4C(), playerTribe->vt4C(), 0x530CF07, +delta)`
(raw delta, ~), or speed up the mini-game: `ui = s[+0xE8]; *(float*)(ui+0x68) += x` (~).
Read back with CalculateRelationship to cap at the threshold. Optional hook instead of polling:
0xDA2FA0 (cdecl; declare ≥9 dword args and forward all - caller cleans).

### C. Healing (goal: healer bonus)

* Player order: InputAction 0x28 → CitizenAction **5** (only members with tool 9, see §2) ✔.
* Execution: BT node 0x562C0FB, tick **0xDB9B20** - cdecl, plain `ret`: **arg1 = healer**
  (citizen), **arg6 = cCreatureBase\*\* target** (patient = `0xD99440(*arg6)`, cdecl
  "valid target" → creature or 0), heal = rate × arg8 ✔ (arg8 ≈ dt, ~).
  Body: plays anim 0x562CFBF on the healer, `AddHealth(patient+0x5A8, amount)`; when the
  patient reaches max HP it increments `TribeUI+0x7C` map[patient mID] (mFullHealsPerPerTribeMember,
  achievement 0x90EC8983) and flags the healer's AI ✔.
* **cCombatant::AddHealth 0xBFD2C0** thiscall(combatant, float amount) ret 4, clamps to
  `vt[0x58]()` (max HP) ✔. Also used by eating (0xDABDE0) and sleeping (0xDAF810) - so hooking
  it alone can't name a healer.

**Healer recipe:** hook **0xDB9B20** (cdecl, forward 9+ args). Before: `pat = 0xD99440(*arg6)`,
`hp0 = pat[+0x5E0]`, `max = pat[+0x5D4]`. After: if `hp0 < max && pat[+0x5E0] >= max` (a full
heal just finished) and both are player-tribe citizens with different coop owners → food bonus
(section F).

### D. Chieftain (goal: chief / marker)

* The chief is **cCommunity::mpLeader at tribe+0x11C** (serializer `mpLeader 0x11C` ✔; shared
  with cCity). Exactly **one** leader per tribe; there is no per-member chief flag.
  | Function | Address | Signature | Conf |
  |---|---|---|---|
  | cCommunity::GetLeader | 0xC00770 | `mov eax,[ecx+0x11C]; ret` | ✔ |
  | cCommunity::SetLeader | 0xC00780 | thiscall(community, citizen) ret 4 (intrusive AddRef/Release) | ✔ |
  | cTribe::GetChieftain | 0xC8FEB0 | thiscall → leader cast to 0x4F176642 or 0 | ✔ |
  | **cTribe::SetChieftain** | **0xC8FED0** | thiscall(tribe, citizen /*0 = first adult not +0xB5E*/) ret 4 | ✔ |
  | cCreatureCitizen::IsChieftain | 0xC233F0 | thiscall → bool (`(city?city:tribe)->leader == this`) | ✔ |
  | cCreatureCitizen::SetSpecializedTool | **0xC27AE0** | thiscall(int toolType) ret 4; writes +0x102C, **0 on the leader becomes 0xB**, refreshes handheld (vt 0xAC(1)) and the tribe palette | ✔ |
  | cCreatureCitizen GetDesiredScale (vt 0x80) | 0xC23E10 | → float ST0; leader × g 0x1582214 (tuning), others random in [g 0x15DA92C, g 0x15DA930] | ✔ |
* SetChieftain does: SetLeader(c); `spatial(c+0xC0)->vt[0x40](c->vt[0x80](0))` (SetScale to the
  chief scale); `SetSpecializedTool(c, 0xB)` (**tool 11 = chief staff**); `combatant->vt[0x2C](1)`;
  0xC0BD00; `ai[+0x5FC] |= 0x20`; 0xC26450(−1). Respawn after death uses tribe+0x2C8 (~).
* Chief look = staff (tool 0xB) + bigger scale; no hat found (accessories are tribe-wide, ?).
* **Marker for any member** (~, cosmetic, test at runtime): scale up `spatial(c+0xC0)->vt[0x40]
  (c->vt[0x80]() * 1.15f)` (re-apply after age change / load), or give the staff with
  `SetSpecializedTool(c, 0xB)`: this replaces the member's tool/job and may confuse the UI
  palette counts, so prefer scale. Do **not** call SetChieftain on a second member (it moves the
  single leader slot).

### E. Epic creatures (goal: spawn an epic near the village)

* `cCreatureAnimal::IsEpic` = **0xC0C200** thiscall → `c[+0xE84] && c[+0xE84][+0x388]` ✔.
  `+0xE84` is the creature's **archetype tuning** (not the herd; mHerd is +0x1674, GetHerd
  0xC046B0). Tuning +0x388 is the prop **`isEpic`** (0xD2125BFE, loader 0x4DE8EF/0x4DE953 ✔),
  +0x389 is prop 0x299ABCFF (?).
* Create 0xC09B00 sets it ✔: `tuning = cSpeciesManager::GetArchetypeTuning(herd[+0x88]
  archetype, herd[+0xF0] generation)` = **0x4E0080** thiscall(specMgr 0x401090(), id, gen) ret 8;
  setter 0xC0C2A0. For an epic, Create also sets `c[+0x2A8]` and in tribe mode posts 0x4F84BD4 (~
  epic warning). The avatar species path uses 0x4E01E0 instead.
* Archetype prop files: group **0x02F98B67** (type 0x00B1B104). `isEpic = 1` in instances
  **0xFC811218** and **0x95511253** (both parent 0x2EA8FB98 = "default"; found in
  PatchData.package and Spore_Game.package) ✔ data; names unknown. 0xDADA0591 = fallback
  archetype, 0xD77DC235 = tribe domesticated animals (0xC90258).
* cHerd fields (serializer ✔): +0x34 pos, +0x84 mOwnedByAvatar, **+0x88 mArchetype**,
  +0x8C mArchetypeGroup, +0xA4 species, **+0xF0 mGeneration**, +0xF4 mTargetHerdSize,
  +0x108 mTerritoryRadius, +0x150 mScaleMultiplier, +0x154 mHitpointOverride, +0x158
  mDamageMultiplier, +0x15C mCreaturePersonality, +0x160 mpNest, **+0x218 mbEnabled**.
  `cHerd::SetEnabled` = **0xC6AE50** thiscall(bool) ret 4 ✔. CreateHerd (0xB238B0) calls
  SetEnabled(**false**) in creature/tribe mode - the scenario spawner (0xEEE0EC) re-enables it.

**Epic spawn recipe (host, ~: test at runtime):**
```
prof = cSpeciesManager::GetSpeciesProfile(0x401090(), &speciesKey)   // 0x4DF580, verified
herd = nm->CreateHerd(&pos, prof, 0, false, 0, false)                  // 0xB238B0
herd[+0x88] = 0xFC811218; herd[+0xF0] = 0;  0xC6AE50(herd, true)       // SetEnabled
c = cCreatureAnimal::Create(&pos, prof, 1, herd, false, false)         // 0xC09B00, cdecl
check 0xC0C200(c) == true; optionally herd[+0x154]/[+0x158] for HP/damage
```
Species: reuse a species already on the planet (any `cCreatureAnimal` with noun 0x18EB45E;
its profile is what Create got) so the guest has the model. Position: tribe spatial
`tribe+0x124` + offset outside the zoning radius (`tribe+0x330`).

### F. Food

* Food = bundles in `cGameBundleContainer` at **tribe+0x20C** (vector begin/end at +4/+8,
  amount float at bundle+0x134, type at +0x12C) ✔ - what shared.cpp reads.
  Total: container `0xCEE4D0` thiscall → float ST0 ✔; cTribe vt 0xA4 = 0xC8E850 → int ✔.
* **cTribe::AddFood = 0xC8F7E0** thiscall(tribe, **int delta**) ret 4 ✔: `delta>0` →
  `cBundleManager::CreateBundles(Get(), float delta, tribe+0x20C, 0xC8EB10(tribe) /*bundle type*/)`;
  `delta<0` → `RemoveBundles(Get(), float −delta, tribe+0x20C, 0)`. Used by NPC tribe AI
  (0xCE3836, 0xCE3EE8). Works on an empty container (creates bundles), unlike editing +0x134.
* cBundleManager: Get 0xB3D380 (cdecl → [0x167FABC]) ✔; CreateBundles **0xAC79B0**
  thiscall(bm, float, container*, int type) ret 0xC ✔; RemoveBundles **0xAC7C10**
  thiscall(bm, float, container*, int) ret 0xC → float ST0 ✔.
* **cTribe::UpdateFoodVisuals 0xC94CD0** thiscall(tribe, float **delta**) ret 4 ✔ only shows
  the floating "+N/−N food" text above the food mat (0xC94950) and only for the player tribe:
  it does **not** change food. Game callers pass the delta (gift: −amount at 0xDBCF0D; deposit:
  +bundle amount at 0xDBC1F4). ⚠ `shared.cpp s_food()` passes the new **total** → probably shows
  a wrong popup on every sync; pass `v − cur`.

**Bonus food recipe:** `thiscall(0xC8F7E0, playerTribe, n); thiscall(0xC94CD0, playerTribe,
(float)n);` on the host - shared.cpp then mirrors the total to the guest.

### Recommended hook points (summary)
| Goal | Hook / poll | Notes |
|---|---|---|
| Pincer | hook **0xC26920** (citizen TakeDamage, thiscall ret 0x14) (+ 0xC9CA20 huts) | attacker = arg5->vt[0xC](), tribes at +0x1014; scale arg1 |
| Duet | poll `ai[+0x1D8]==0xB46E8803` + session 0xD9F500 `+0xC` | bonus via ApplyRelationship 0xD06280 (event 0x530CF07) or minigame `+0xE8→+0x68` |
| Healers | hook **0xDB9B20** (cdecl heal tick) | arg1 healer, `*arg6` patient; detect HP reaching max |
| Chief marker | SetSpecializedTool 0xC27AE0 / spatial vt 0x40 scale | one real chief only (tribe+0x11C) |
| Epic | CreateHerd + herd+0x88 = 0xFC811218 + SetEnabled + Create | archetype tuning isEpic |
| Food | cTribe::AddFood 0xC8F7E0 + UpdateFoodVisuals(delta) | fix shared.cpp delta |

---------------------------------------------------------------------------------------------
## Trees (fruit trees, gather targets) - runtime RE 2026-10-23 (play_tribe)

What a right-click "gather" names depends on the plant: the small tribal food plants are
cGameData (noun **0x2C9CC91**, in the noun list, position via the spatial cast ✔), the planet's
fruit trees are **cTreeData** (noun **0x6ABEE7E** via vt 0x20 = 0xAFFEB0, main vtable
**0x145D160** / second base 0x145D150, size 0x80) which are **not** cGameData: not in the noun
list, no cSpatialObject cast, so `game::object_position` / `find_object` cannot see them ✔.
Before the fix a guest's gather on a tree reached the host as "noun 0x6ABEE7E at (0,0,0)" and was
rejected.

cTreeData (✔ runtime + asm): +0x00/+0x04 vtables, **+0x08 refcount**, +0x0C/+0x0E u16,
**+0x10 Vector3 position** (identical on host and guest: same planet data), +0x1C float,
+0x20 transform, +0x48/+0x4C u64 bit mask (fruit slots ~, set from random bits when planted in
0xB07490), +0x50..+0x58 ids, +0x5C..+0x64 vec3, +0x68 ref-counted model pointer, +0x6C..+0x74
scale. Ctors: 0xB016A0 (default), 0xB035B0 (copy), 0xB01750 scalar deleting dtor; allocation tags
"Simulator/cTreeData" (0xB07490, planting), "SP_Simulator/cTreeData/COM" (factory 0xB02020).
None of these ran while a guest loaded the world (probes: 0 hits): the live trees come another way (?).

Where they live (✔ found with `coop.memfind` from a picked tree up to a global):
* **flora object = [0x167FB14]**, getter **0xB3D510** (`SporeCoop::Flora_Get`; class name
  unknown, the getter sits between cTerraformingManager::Get 0xB3D500 and
  cAnimalSpeciesManager::Get 0xB3D520).
* flora **+0x1E5F0 / +0x1E5F4** = begin / end of a sorted vector of patches, element stride
  **0x19A8** = `{u32 key (patch index, sparse 0..~290), u32 0, patch}` (264 patches on Cohrach).
  0xB07490 compares a lookup result with `flora+0x1E5F4` (the end) ✔.
* patch: +0x00 Vector3 centre, +0x0C radius (26), three `fixed_vector<intrusive_ptr<cTreeData>,16>`
  at **+0x44, +0x9C, +0xF4** (begin, end, capacity; 0x58 bytes each) holding the live trees, three
  by-value `fixed_vector<cTreeData,16>` at +0x158, +0x970, +0x1188 (serializer table at 0x1318390,
  property ids 0x56690EB/EC/ED, reader 0xB0A240).
* The hover pick hands DoActionForObjects the live tree pointer (the same pointer as in the patch
  vector; refcount 3 while hovered and ordered).

Fields near the village of Cohrach (`coop.tribe trees`, ~ from 30 trees): `+0x0C` = 7 for all,
`+0x0E` 3..0x10, `+0x1C` size (0.7 small, 1.4-1.6 big), **`+0x48` fruit left** (bit per fruit; 0 =
picked empty: such a tree is no gather target), **`+0x4C` all fruit slots** (0x07 small, 0x1F big),
`+0x50` patch key (0x7B..0x89, = the patch's key), `+0x54` index in the patch, **`+0x58` plant
species hash** (0x6D60A1CC small fruit plant, 0x029C388A big fruit tree), `+0x6C` scale.

**Recipe (tribe.cpp):** the guest sends a tree target as `kind 3 + position (+0x10)`; the host walks
all patches (`for_each_tree`) and takes the tree within 1 m. `coop.targets objs` lists trees on
screen as `obj fruittree|tree <dist> x y` for game bots (with / without fruit left). A right-click
right after moving the cursor often hits the ground (a walk): hover ~0.4 s first (docs/06-testing.md 6.10).

---------------------------------------------------------------------------------------------
## What right-clicks become (runtime, play_tribe.py 2026-10-23, Cohrach fixture)

Recorded from `tribe last local order` / `tribe last order` (the CitizenAction the click gave):
fruit tree (cTreeData) -> **2** Gather; animal -> **10** Hunt; another tribe's member (attack
mode) -> **7** Attack; another tribe's hut (attack mode) -> **15** RaidHuts; another tribe's hut
(social mode, no instruments in this tribe) -> **9** GiftFood; the own food mat (noun 0x629BAFE;
clicks on 0x55CF865 / 0x18C431C next to it resolve to the mat) -> **0** Eat (hurt members heal
there, it costs food); the own fire pit (tool noun 0x18C8F0C) -> **12** Party; the own hut ->
no order. The small plants of noun 0x2C9CC91 near the village gave Gather once in early tries
but mostly took no order: decoration (~).

Game over: when the player tribe has no member with health left, the game shows "Alas! Your
population has fallen below the minimum needed to survive... Bounce back in time" with a green OK
(`key enter`); OK reloads the last save. With co-op the guest follows the host (both had the same
3 members again, 3 tries). Adding a member while the caption is up faults on OK:
docs/known-issues.md.

New NPC tribes form during play: Cohrach had 2 tribes at the start and 5 (33 citizens) after
~8 minutes, on host and guest alike (the guest's own game spawns them too; the positions match).

## Chiefs, epic and game over: object lifetime (2026-10-27, fix/w1-tribe-civ)
<a id="tribe-lifetime"></a>

* ✔ `RemoveMember(c, destroy=1)` (tribe vt 0x9C) on a chief: right after it `object_alive(c)` is
  already false (the destroyed flag +0x21 is set at once), so the old check skipped that case; the
  danger is a freed block reused by another object later (BUG-0081). Restores now go only to members
  of the player tribe's own vector (`kMembersOff`). Forced reuse in the test (`coop.tribe
  stale_chief <id>`): with the old check another tribe's citizen got the old chief's size (2.54 ->
  2.00), with the fix it keeps its own.
* ✔ Game over (GOG 2024, lane 5): the caption window `0x0445C9AB` reads "Alas! Your population has
  fallen below the minimum needed to survive. Will this tragic story repeat itself? Bounce back in
  time to find out!" (`coop.ui find "Alas"`); it comes several seconds (< 15 s) after the last member
  has no health left. A `coop.tribe spawn 2` while it was up, then OK: no fault, the bounce back
  reloaded the save (3 members with health) in 4 of 4 tries with the old code: the 0xC0DAE3 fault
  of 2026-10-23 (GOG 2016 era) is not reproduced on GOG 2024 (BUG-0172). The mod now adds no member
  to a player tribe without health (`coop.tribe fix gameover 0`: the old way; `spawns_blocked=` in
  `coop.status`).
* The epic (`g_epic`) is checked against the game's object list (`game::for_each_object`) before
  any read; `coop.tribe epic hit` stops at a dead victim (BUG-0171 hunt on main: 4 tribe_play + 4
  chaos_tribe runs without the freeze, lab run 20261027-183142; 14 tribe_play runs before).
* `coop.status` line `tribe lifetime:` (chief_restores, chief_gone, chief_bad_calls,
  spawns_blocked, chief_scale = game scale / own scale of every chief).

## Knocked-out members (2026-10-28, fix/w2-civ)
<a id="tribe-knocked-out-members"></a>

* A tribe member hit to 0 health on the host (NPC tribe member, `coop.tribe pincer <id> <own id>
  1000`) has the dead flag +0xB5E = 1 and behaviour node `ai[+0x1D8]` = **0x05CA7FA7**, and stays in
  its tribe's member list (still there 60 s later; it was moved once, ~6 s after the hit). ✔
* The guest's mirror runs no behaviour tree (h_BTTick skips citizen mirrors), so it never gets
  there by itself; the mod now calls `game::play_death` (Die vt 0x8C + death animation) when the
  host's health is 0 (BUG-0173). ✔ (dead flag on the guest, lane 7)
* `coop.tribe` lists `dead=` and `bt=` (the behaviour node) per member; `tribe lifetime` counts
  `knocked_out` / `ko_replaced`; switch `coop.tribe fix ko 0`.
* ? How the member gets up again (same citizen revived or a new one) was not seen within 60 s.

## The co-op epic: herd, saves, hitters (2026-10-28, fix/w3-civ)
<a id="tribe-epic-lifetime"></a>

* ✔ The fixture planet (tribe) has an epic of its own (IsEpic 0xC0C240 true, alive): counts of
  "live epics" must be relative to it. Its herd is not one with archetype 0xFC811218 (0 such herds
  before the first co-op epic).
* ✔ A herd made by `CreateHerd(.., 0, false, 0, false)` and given the epic archetype keeps
  `+0xF4` (target size) at whatever we write; `SetEnabled(false)` right when its epic died does not
  stick: the herd reads enabled (+0x218 = 1) again within seconds (lane 7, three kills). Disabled
  later (`coop.tribe epic quiet`, seconds after) it stayed disabled. Target size 0 is what keeps it
  from spawning.
* Co-op (BUG-0121): one epic herd of ours per species is reused (archetype 0xFC811218, no nest
  +0x160, not avatar-owned +0x84, target 0: "ours"; found in the object list, so also after a load);
  three epics leave 1 herd instead of 3. A live co-op epic is destroyed before every save
  (`tribe::before_save` through `spaceext::on_save`, the one hook on
  `cGamePersistenceManager_SaveGame`; a second MinHook on the same address fails) and at the end of
  the session. No function to destroy a herd is known: the quiet herd stays in saves.
* Co-op (BUG-0146): only players' members (owner < 0xFE, present) count as epic hitters.
* Chiefs: the size the game shows grows and shrinks over a few seconds after SetScale (ratio
  scale / desired 1.62 seen once right after a new chief, 1.27 4 s after the restore, 1.00 later):
  tests wait for it (`coop.tribe scales`).
* Diagnostics: `coop.tribe epic herds` (the game's epic herds, ours, enabled, live epics),
  `coop.tribe epic save` (as if the game saved), `coop.tribe scales`, switches
  `coop.tribe fix epic_players|epic_herd|away 0`.

## Which Tribe world is open: the stage counter (2026-10-28, fix/w5-world)
<a id="tribe-stage-epoch"></a>

BUG-0217. GOG 2024, lane 5.

* ✔ The Tribe stage has no adapter for the editors, so before the fix every editor frame (mode
  kEditor 0x00DBDBA1) made `avatar_change_check` see "no local entity" and call
  `tribe::on_world_change`: the tracked co-op epic was forgotten while it stayed alive, and the
  world share after the editor ("evolved the species") saved it (`coop.tribe epic herds`:
  `live_epics` 2 after the share save and after the session end, `tracked=00000000`).
* ✔ `coop.vote editor` in Tribe (a member's species key in the request, CreatureEditorExtraLarge)
  opens the creature editor within 1 s (mode kEditor). Its Done (1416, 1062) does nothing for the
  tribe species (a blocking error, ~ the part list bit 5 of docs/re/creations.md 7: not checked);
  the red X (1380, 1062) leaves without changes. Route seen: kTribe, kEditor, kTribe (no
  non-stage frame). The host's share save follows; the guest loads it about 10 s later through the
  galaxy menu (mode 0x02CCD1D2 seen by the guest's world check).
* ✔ Now (`game::stage_epoch`, mods/coop/stage_epoch.h): one counter per stage, raised when the
  stage is entered from anything but its editors (kEdit, kEditor; kScenario keeps Space only).
  `tribe::world_check` (top of on_frame, before messages) forgets the Tribe world when the mode
  leaves Tribe for a mode that is not an editor, and on the in-stage signals (another player tribe
  object, another save name). Switch `coop.fix tribe.editor_world`. Limit: a reload of the same
  save that reuses the player tribe's address and never shows a non-stage frame is not seen.
* ✔ On a guest `coop.tribe epic herds` lists `live_epics=0` before and after the host's world
  arrives, the planet's own epic included: the guest's game is no oracle for "the epic came with
  the save" (the host's game after the share save is).
* Not measured (issue #151 step 0): the pause-menu Load, the game-over bounce back and Civ to
  Space routes frame by frame. A route that reloads without any non-stage frame relies on the
  in-stage signals.

## The save name after the live Creature -> Tribe change: snapshot.spo (2026-10-29, fix/w5-snapshot)
<a id="tribe-snapshot-name"></a>

BUG-0258. GOG 2024.

* ✔ After the game's own Creature -> Tribe change (History, the fire cinematic, the Tribe intro)
  `CurrentGameName` (string16 0x0167DF60) reads `snapshot.spo`, the game's temporary file, and
  stays so until the game is saved again (four-player campaign runs 20261029-001119 and
  20261029-011243 on dev/campaign: 5+ minutes). The next save (pause menu, `coop.save`, the save
  before a world share) sets it back to the planet's file (`Cadian.spo`).
* ✔ When: 5 to 10 s after the mode turned Tribe (log line `world: the game's save name is now
  'snapshot.spo'`, lane 3: +4.9 s in a two-player run, +8.7 s in a four-player run), after the
  host's first share save. Whether a later save of the host follows decides the bug: in the
  four-player campaign run 20261029-061803 (lane 3, fix off) the "next phase"
  save came 10 s after the rename and set the name back (no bug); in the two-player lab run
  20261029-062544 the "next phase" save was posted 0.2 s after the rename, wrote nothing (no "save:
  objects kept out" line, the saved box 188 ms after it was the old one) and the name stayed
  `snapshot.spo` for 5 minutes: the bug. The ticket's four-player runs had both saves before the
  rename.
* ✔ The rename also made `tribe::world_check` report "the world changed" (its in-stage save-name
  signal) on the host 5 s into the stage.
* ✔ Effects before the fix: the place id of a player (crc32 of that name, `modes::current_world`)
  was crc32("snapshot.spo") = 0x6F3BE084 on the host and the planet's on every guest: `together N
  0`, no puppets, for good. The same name was the lobby text and the in-stage "another save name"
  signal of `tribe::world_check` (BUG-0217) and `civ::world_check`, so the rename and the next save
  each looked like a new world.
* ✔ Now (`mods/coop/world_name.h`): `modes::current_world_name` keeps the last planet file read
  while the game says `snapshot.spo`; read once per frame (`modes::world_name_tick`, top of
  on_frame), forgotten when the name is anything else ("SaveGame" in the galaxy menu), so a new
  game never inherits an old planet. `coop.avatar` prints the game's own name as `game_name`.
  Switch `coop.fix world.snapshot_name`.

## A share save right after the change to Tribe is not written (2026-10-29, fix/w7-vote)
<a id="tribe-share-save-dropped"></a>

* ~ (logs, lab 20261029-122937 campaign#2, four players, BUG-0276): the host entered Tribe at
  13:50:18.861, the shares for the three guests were queued again (the save before the change was
  of Creature: "the game went from Creature to Tribe during the save ... saving again") and one of
  them posted its save at 18.929, 68 ms into the stage. The game never wrote it: no "save: objects
  kept out" line, no "game saved" box, and 10 s later "no saved box ... sharing anyway" sent the
  slot's files of the Creature save of 13:50:17. All three guests loaded it and played Creature in
  the host's planet (`game.mode` 0x01654C01) while the host played Tribe, for 300 s, until a rejoin
  (which saved in a settled Tribe and worked).
* In the other games of the same lab the second save came 2 s after the change (the new shares
  had merged with the older save first and were moved by its "saving again" delay) and was
  written ~1.8 s later. Inferred: a save posted in the first frames of the new stage is dropped by
  the game. Not reproduced on purpose (a timing race of the four-player change).
* Fix (world.cpp, switch `world.share_settle`): a share save waits until the stage has been
  playable for 2 s; a share whose save showed no box is sent only when a file of the slot was
  written after the save was posted, else it saves again (at most three times). `coop.status`
  prints "share saves held while the stage settled N, shares saved again (nothing written) N".

## The share's settle clock did not restart at Creature -> Tribe (2026-10-29, fix/w8-stuck)
<a id="tribe-share-settle-reset"></a>

* CORRECTION (2026-10-29, fix/w8-stuck): the fix of [the dropped share save](#tribe-share-save-dropped)
  (`world.share_settle`) did not hold: lab 20261029-174717 campaign#1 (lane 2, main b5ef74d) had the
  same stuck finding. ✔ (logs) Its clock was `g_playable_since` of the via-menu load (world.cpp),
  reset only when the local player had nothing to control. The host's creature is the Tribe
  adapter's avatar from the first Tribe frame on (`avatar=yes` in the trace line of the change), so
  the clock still held the Creature value and the share saved at once: posted 19:08:38.767, 75 ms
  into Tribe (38.692); no box, "sharing anyway" after 10 s sent the slot's Creature files (written
  ~38.0, inside the stale check's 2 s slack); the guests played Creature in the host's planet.
* ✔ Reproduced with the new switch off: lab 20261029-192222 campaign_tribe#1 (lane 7), save
  posted 0.2 s into Tribe, no box, "sharing anyway", stuck finding. The other three games of the
  lab had their save 1.8-2 s after the change (the race of the older share's "saving again").
* Fix (`world.share_settle_reset`, BUG-0276): the share's clock (`g_share_settled_since`) restarts at
  every mode change and every place change (`world::set_place_probe`, the adapter's place); a save
  without its box is stale also when no file of the slot is newer than the newest one before the
  save was posted (`g_saved_base`). Log: "world: saving for the share of player N (Tribe, X s in
  this place)".

## The game-over bounce back and the guests' mirrors (2026-10-30, fix/w9-tribe)
<a id="tribe-bounce-mirrors"></a>

* ✔ (lab, lane 8) When the host's player tribe is knocked out, the game-over caption ("Alas! Your
  population has fallen below the minimum ...") comes on the HOST only, its input lock
  (cGameInputManager +0x110) is 1 then (once 0 when read). The guest's game shows no caption and its
  lock stays 0, although every mirror of its player tribe played its death (tribe.ko).
* ✔ The host's OK (bounce back) reloads the save: the host goes Tribe -> Loading -> Tribe (mode
  0x01654C08, same world), its members get new ids (t14.. in the fixture, t20.. in the campaign).
  The guests stay in Tribe and get the new ids in the next snapshot; the old ids are removed at the
  end of the cycle.
* ✔ Before the fix the guest's BUG-0172 guard (`player_tribe_out`: no member with health = out)
  refused every new mirror: first because every local mirror was dead, then because the tribe was
  empty after the dead ones were removed. `spawns_blocked` grew every snapshot (1950 in 2 min), the
  guest's game had 0 members in its player tribe for good (campaign 20261029-203821 campaign#2:
  "t: 16 missing on the guest" on all three guests after the host's bounce back at 22:08:05).
* Fix (`tribe.mirror_out`, BUG-0288): on a guest the guard holds only while its own game has the
  input lock up (a caption or a cinematic). The host keeps the guard as it was.
* Not explained: the campaign's first coverage finding (21:07:57, Bob and Carol) came 8 s BEFORE the
  host's bounce back, while its tribe was being knocked out (members 13 -> 5, the caption's
  cinematic); the logs do not say which kind was missing. coop_campaign.py now names the missing
  keys per kind. See also BUG-0291 (animals far from the tribe).

## A citizen's walk after its cleanup in the frame Tribe is left (2026-10-30, fix/w9-tribe)
<a id="tribe-leave-walk-fault"></a>

BUG-0285 (static reading of GOG 2024 `dc04aee5`, objdump; not reproduced):
* ✔ The fault 0xC0DB23 is in **0xC0DB10** (labelled CreatureHearsTarget+0x463 by the nearest
  symbol): `thiscall(cCreatureBase*)`, plain `ret`, reads `this+0xB54` (+0x3C) and `this+0xB20`
  (+0x554..+0x55C) without a null check, sets the spatial's move scale. Called from WalkTo
  (0xC1CEEB) and four more places (0xC09CEE, 0xC0ABD9, 0xC0E78D, 0xD410F8).
* ✔ `+0xB54` is set to 0 only by **0xC1CA40** (a cCreatureBase cleanup: releases +0xB54 through
  0xA051B0, then +0x5A8, +0x2C; reached from the citizen's override at 0xC27DE7 and four jmp thunks)
  and by the constructor (0xC1FB33). A citizen with +0xB54 = 0 has been cleaned up.
* ✔ The chain of the fault: 0xD673B0 (per citizen: 0xC1B030, then if `(this+0xC0)->vt58()` and
  0xC0EFC0(this) is set, a manager from 0xB3D580 runs **0xACA2A0** on it) -> 0xACA2A0 checks the
  object's noun (vt+0x20): for 0x18EB4B7 (the citizen noun, see above) it goes on only when the game mode is NOT Tribe (0x1654C02), looks for
  a point in range, else calls **0xD57680**(citizen) -> 0xC0D500, a timer on +0xB4C, then
  0xC27B00(citizen, 0xA68F70(3)+1, ~ a random 1..3) (sets +0x102C, then vt+0xAC(1)) -> DoAction -> WalkTo -> 0xC0DB10.
  So the branch only runs in the frame the mode is already the galaxy menu: the stage's teardown
  updates a citizen it had cleaned up. No frame of coop.dll is in the chain.
* ✔ (lab, lane 8, 4 leaves) Members made by `coop.tribe spawn 25` (SpawnMember / CreateCitizen +
  AddMember, 28 members) all have +0xB54 set, and leaving Tribe for the galaxy menu with them
  (`coop.load` of another slot) caught no fault: the mod's spawned members are not shown to be the
  cleaned-up citizen.
* The co-op epic destroyed by the quit's save (`before_save`, "the epic creature went away" 0.04 s
  after the quit) is an animal (noun 0x18EB45E): 0xACA2A0 takes another branch for it.
* ? Which citizen was cleaned up yet still updated, and why only once in ~100 leaves.

## Animal proxies trail a running herd on every guest (2026-10-30, fix/w12-tribe-npc)
<a id="tribe-animal-proxy-trail"></a>

How the guests' copies of the host's animals lagged 2-4 m in a hunt (BUG-0305). Measured with
`coop_sync_multi.py --fixture tribe` (4 players, play_tribe bots) and its `coop.npc list` probe of
every machine for each animal over 1.5 m off (`out/w12/probe*.jsonl` of the branch, not in git).

* ✔ The host's game updates every tracked animal every frame in Tribe (`coop.npc list` on the host:
  `upd=64/64`, the frames of the last 64 in which the creature ran its update). Not an update
  throttle.
* ✔ Every animal over 1.5 m off was running or had just stopped: host `velfr` (frames of the last 64
  with the game's velocity over 0.3 m/s) 16-64, `moved64` 4-8 m; often a wounded one (hp 12.5-20 of
  40-50) in short dashes, often a herd mate 0.3-3 m beside it (species 06808ACA, 0680AA69, 065CBCB4).
* ✔ The guest's proxy was 2-4 m from its own interpolation target (`err=`) while it walked at
  exactly the animal's speed (`own=` = the sample's `spd=` 5.5 / 6.5 / 7.5 m/s): the game's
  locomotion does not run a creature faster than its top speed, so `drive::step`'s catch-up speed
  never came, and a proxy thrown off once stayed behind for the whole run, the same way on every
  guest. After the animal stopped it caught up in 0.5-1.5 s (walking, or the stuck teleport).
* ✔ The host's herd mates stood 0.3-2 m apart while their proxies stood 0.7-3.4 m apart on the
  guests (probe2 r37 n81 / n82): on a guest the proxies push each other apart, the host's animals
  overlap. Which game code separates puppets (collision or the locomotion's avoidance): ? not
  looked up.
* ✔ Reproduced on two games (`coop.npc dash`, test command): two 06808ACA animals forced 1.2 m apart
  on a 12 m zigzag at 6.5 m/s, guest at 15 fps: p90 1.5-2.8 m off; one animal alone (or the two
  5-10 m apart) followed within p90 0.5-0.9 m. A 16DE4FEA pair 1.2 m apart overlapped so much that
  the guest flung the proxies apart at 12 m/s after every teleport (no game keeps a herd that close).
* ✔ The guests got 4-10 states per animal per second (`rx=`, sender seconds) at 10-30 fps guests;
  ages up to 1 s were seen on loaded lanes. Not the cause here: with 10 states/s the offsets stayed.
* Fix `npc.trail` (drive_plan.h, driver.h): a proxy more than 1.5 m off for 0.5 s without getting
  0.3 m closer is teleported onto its target and held there for 1 s (the per-frame nudge goes from
  15 % to all of the way), then 1 s before the next such teleport. The teleport alone was not
  enough: the proxy beside it pushed it off again at once.

## The citizen action whose exit sends a citizen walking off-stage (2026-10-30, fix/w13-tribe-civ)
<a id="tribe-offstage-action"></a>

BUG-0310 (the second sighting of BUG-0285's fault, lab 20261030-104149 campaign#3, the same
stack: Citizen_OffStageCheck+0x18A < Citizen_IdleWalk < 0xC27B00 < DoAction+0x496 < WalkTo <
CreatureBase_SetMoveScale+0x13). GOG 2024, static reading plus a probe of the guard's counters:
* ✔ 0xD673B0 has no direct caller: it is the 4th word of a descriptor in .data at 0x1583F40
  `{0x5218BB3, 0xD69540, 0xD69740, 0xD673B0}`: a citizen action (id 0x5218BB3) with its start
  (0xD69540, resets a 0x38-byte state), its tick (0xD69740, also calls Citizen_OffStageCheck at
  0xD69BB7) and its end (0xD673B0). The id is also used at 0xC2462D, 0xCCA672, 0xD9ADEA, 0xDA81D8,
  0xDA9C0C (? which action: not named yet).
* ✔ Citizen_OffStageCheck (0xACA2A0) has 14 callers (citizen actions, 0xC4CE00 ... 0x1032BD0);
  its citizen branch returns at once in the Tribe mode, so it only acts in the frame(s) after the
  mode changed (or in another mode).
* ✔ (lane 6, `coop_tribe_leave_walk.py`) 90 s of play_tribe bots made 11 calls, every one of them
  in the Tribe mode; two leaves of Tribe for the galaxy menu right after the play made none for a
  citizen outside the Tribe mode: the campaign's path needs a citizen in that action at the moment
  the mode changes, which the lab's short sessions do not give.
* Guard `tribe.leave_walk` (gamefixes.cpp): Citizen_OffStageCheck is skipped for a citizen whose
  +0xB54 is 0 (cleaned up by CreatureBase_Cleanup): there is nothing to walk, and in the Tribe mode
  the check does nothing for a citizen anyway. Its counters are in `coop.status` ("off-stage
  checks N (citizens while the stage is left M), cleaned-up citizen walks skipped K").

## The ground under a village differs between the host and the guests (2026-10-30, fix/w17-tribe)
<a id="tribe-ground-height"></a>

BUG-0329 / BUG-0332, from the campaign's probe lines (lab 20261030-210914):
* ✔ A tribe member's position on the host and its mirror's on every guest differed only along the
  planet's radius (4.9-5.3 m; sideways under 0.8 m); animals near the village the same (3.9-4.3 m).
  The sign differs between campaigns. All guests agree (they load the same save).
* ✔ A mirror driven to the host's absolute position under its own ground walks in place (5.5 m/s on
  the guest while the host's member stands) and NPC proxies are teleported again and again.
* ✔ `Simulator::cPlanetModel::GetHeightAt` (`coop.ground x y z`) gives each game's ground radius in a
  direction; a live Creature -> Tribe on flat ground (creature_cadian fixture) gave 503.68 m on both.
* ? Which game code places the village ground, and why a live Tribe and a loaded one differ.
* drive.own_height: in Tribe a mirror's target takes the mirror's own distance from the centre
  (when within 10 m of the host's).

## Where the players' chiefs stand at the arrival (2026-10-31, fix/w21-tribe)
<a id="tribe-chiefs-arrival"></a>

BUG-0340 (players' chiefs on the same spot), measured with `coop.tribe` (member lines end with
`radius=`, the spatial's bounding radius vt 0x70) on every machine:
* ✔ 2 players: fixture chiefs 26 m apart; live Creature -> Tribe 3.9 / 4.6 m apart (radii 2-3.9 m).
* ✔ 4 players, live Creature -> Tribe (campaign, lane 9): the chiefs of players 1 and 2 2.0 m apart
  (sum of the bounding radii 5.0-5.5 m) on all four machines, standing still, for 6+ s; 50 s later
  still 4.5 m (under 5.0). The others 6-20 m apart.
* ✔ Two chiefs put onto one spot on purpose are parted by the game's own avoidance within 1-2 s (to
  1.8-5 m), so the reported overlap is the arrival's placement, not a lock of the game.
* tribe.place_chiefs: at the arrival and after a rejoin (the host's split of the player tribe, then
  every 2 s for 20 s), a chief closer than the sum of the radii + 1 m to another chief goes to its
  ring spot: 12 m from the tribe's centre, a quarter turn per player, on the game's ground.
  tribe.spread_chiefs: later, two chiefs standing in each other (< 1.5 m, 1 s) are parted the same way.

## The flora's tree grid freed when Tribe begins (2026-10-31, fix/w27-heap)
<a id="tribe-flora-grid-free"></a>

Ghidra, GOG 2024 (BUG-0359); ✔ = read in the disassembly, ~ = role inferred.
* ✔ The flora object (`SporeCoop::Flora_Get` 0xB3D540, the patches at +0x1E5F0 / +0x1E5F4 of the
  "Trees" section above) is a message listener: its vtable at 0x0145C7C4 has the Gonzago subsystem
  slots, then the IMessageListener part {0xB0BEE0 dtor, **0xB081C0 HandleMessage**, AddRef, Release}
  at 0x0145C818. HandleMessage `thiscall(flora, uint32 msg, Msg* m)` ret 8 handles 0x02F3FC35 and
  0x025630B7 / 0x0477C00D / 0x02E9977E (names unknown); for the last three it calls 0xB04A00
  (patch lookup by m+0x20, species 0x29C388A / 0x3A8BE428 / 0x6D60A1CC: sets or clears a vec3 of the
  patch) and then **0xB07F00** (m+8 == 0) or 0xB078E0.
* ✔ 0xB07F00 `thiscall(flora, Msg*, char, void* flora+0x165A0)` ret 0xC: for the trees of the
  message's key it releases each tree's model (+0x1C4, vt 0xC then Release), resets its slots, and
  for each record of 0xB04B00(key, species) with a handle at +0x78 >= 0 calls **0x702E30** on the
  grid at flora+0x16500 and sets +0x78 = -1.
* ✔ 0x702E30 `thiscall(grid, uint handle)` ret 4 (~ a hierarchical grid remove; the string
  "Simulator/HierGrid" follows the flora vtable): handle slots in blocks of 128 x 16 bytes at
  *(grid+0x60)[h >> 7], free-slot list head at +0x7C, count at +0x20 -1, then **0x702B80**
  (unlink, ret 4): levels of 0x44 bytes at grid+0x24, the slot's list node is unlinked and freed with
  `GeneralAllocator::delete_` (0x702C3F, the return address 0x702C44 in a crash stack; the nearest
  ModAPI name, `Graphics::cFrustumCull::FrustumTestAABB+0x2A94`, is wrong).
* ✔ The return addresses 0xB78155 / 0xB78764 that the stack scan shows beside them are
  cObstacleManager's (vtable 0x01465068 after the string "Simulator/cObstacleManager"; its
  HandleMessage 0xB78500 switches on the same message ids): stale, it ran on the same message first ~.
* ✔ GeneralAllocator_FreeInternal 0x927260 (PPMalloc): chunk = ptr - 8, size = [ptr - 4] &
  0x7FFFFFF8; over the small-bin limit ([alloc+4], 0x48 here) it reads the next chunk's header at
  chunk + size + 4 (+0x64, 0x9272C4). A fault there with EDI (the size) a float bit pattern means
  the block's header was overwritten by a float: BUG-0359 (0x3F028F5C = 0.51f, a flora grid node,
  host at Creature -> Tribe), BUG-0308 / BUG-0348 (0x41FAECC0 = 31.37f, a Cell pool, guest leaving
  Cell). The site only says "corrupt header"; the block and its writer differ.

## The guests' mirrors of walking members (2026-11-01, fix/w30-tribe-walk)
<a id="tribe-mirror-walk"></a>

BUG-0365. GOG 2024, lanes 2-3. Measured with `coop.track` (in Tribe one row per member of the player
tribe, every frame) and `coop_tribe_walk.py` (members walked by box-select and right-click). Same
method as [the Creature puppets](creature.md#creature-puppet-walk).
* ✔ Under drive::step a mirror walks at a fixed speed: game speed exactly 9.0 m/s or 0 (the fixture's
  members), whatever SetDesiredSpeed (spatial vt 0xC4) asked just before (2.6-6.5 m/s). Under
  walk_place (SetDesiredSpeed every frame, MoveTo only every 0.3-1.5 s) its game speed is the one asked
  (4.75, 9.0, 12.0 m/s seen). ~ So a MoveTo (vt 0xE0) sets the citizen's speed back to its default and a
  SetDesiredSpeed has to come after it (inferred from the two drivings, not read in the code). The
  host's own member speeds up from 0 to 9 m/s in ~0.5-0.8 s and slows down the same way.
* ✔ The old driving (drive::step: MoveTo 0.25 s ahead of the target every 0.1 s, stop distance 0.3 m,
  SetPosition pulls over 0.5 m) follows at full speed (0.1 s windows matched 88-98 %, the walk gait on
  96-99 % on screen, no driver teleport), but while the host's member moves at 1-5 m/s (every start and
  end of a walk) the mirror at 9 m/s reaches the point ahead in one or two frames, stops, and walks
  again: standing in 9-25 % of those frames, faster than the member + 4 m/s in 1-17 %; its drawn speed
  is off its target path's by more than half of it + 1 m/s in 18-36 % of them (the host's own game ~1 %);
  it sets off 0.5-1.6 m late and catches up at 15-24 m/s; drawn error p50 0.46 m (the nudge only acts
  over 0.5 m). Stop-and-go at the ends of a walk, not the Creature puppet's "every half second" steps.
  With the fix: 5-6 % uneven, drawn error p50 0.01 m, p90 0.08-0.17 m.
* ✔ The tribe state (MSG_TRIBE_STATE, every 0.12 s, unreliable) had no sender time: the target was the
  latest snapshot moved on by its arrival age (`vel * min(age, 0.5)`), so jitter went into the path
  (the BUG-0122 class). Since BUG-0365 every chunk ends with an 8-byte trailer (magic 0x54435354,
  the host's ms); a guest before 0.7.3.4 stops reading after the entries and ignores it, a guest
  reading a chunk without it falls back to the arrival time.
* ✔ The fix: each mirror keeps a net::SnapshotBuffer (delay 0.2 s; `tribe.mirror_time`) and, while the
  interpolated member moves, the shared `drive::walk_place` (driver.h, BUG-0360's creature driving)
  walks it at the interpolated path's velocity (over the last 0.1 s; the newer state's speed made it
  walk ahead of its path at every start) and places it on the path at its own ground height
  (`tribe.mirror_walk`; the speed set after the MoveTo).
* ✔ The host's own members start and stop within one frame (0 -> 8 m/s); the drawn mirror is our
  placing plus one game update of its locomotion, so its walk speed has to match the path.
* ✔ The placing of a frame is drawn in the next one: after a hitch of the game (100-230 ms frames, a
  few per test while the camera jumps by the minimap) the next short frame shows the way made up
  (1.1-1.3 m in 13-14 ms). Not a teleport; a frame-step test has to allow the frame before's time.
* ✔ (after the merge with main) A guest at 3-4 fps (226-336 ms frames, lab load 31 on 12 cores) closed
  only 1.2 m per frame of its member's 2-3 m (the catch-up scaled by at most 0.1 s), fell 8 m behind and
  drive::step teleported it 8.1 m: the mirrors scale the catch-up by up to 0.4 s (`WalkPlace::max_dt`).
* ~ `WalkPlace::ground` (BUG-0366's on_own_ground) is not on for the mirrors: they keep their own height
  (`own_height`, BUG-0329); the `hd` column of coop.track showed them 0.00 m over the ground while they
  walk (fixture village, flat).
* ~ Members of NPC tribes (owner 0xFE) are mirrors too and get the same driving; not measured.

## NPC tribes formed during play, and the guests (2026-11-01, fix/w32-tribe-sync)
<a id="tribe-npc-forming"></a>

* ✔ `Simulator::SpawnNpcTribe` (GOG 2024 0xC932F0): cdecl `cTribe*(const Vector3* pos, int archetype,
  int members, int food, bool, cSpeciesProfile* species)`, plain ret. A null species takes one from
  the species manager (0x4DF490). It creates noun 0x18C6D19, sets +0x18CC = species, +0x550 = archetype
  (when < 0x20), then calls the tribe's vt 0x8C(pos, members, food, bool): the setup that puts the tribe on
  the ground at pos (terrain height, orientation, the hut) and makes `members` citizens; then the names.
* ✔ The game forms new NPC tribes during play in `cNpcTribeController` 0xCC6440 (vtable slot at
  .rdata 0x147689C): when the planet has fewer tribes than its property list allows, it takes the spawn
  marker (noun 0x36BE27E with +0x108 == 0x57F09E5E) nearest the player tribe, the next species of the
  controller's list (+0xD0, by the number of tribes) and the archetype of the list, calls SpawnNpcTribe,
  shows the event log entry (0xD82D8A26 / 0x182CD6CE), plays "TRG_PRE_FirstTribeAppears" for the first
  one, destroys the marker and the herds near the new village (noun 0x1BE418E within its radius) or hands
  them to it (0xCD48F0). The other caller, 0xB942A0, makes a tribe of n members with n*10 food and its tools.
* ✔ (BUG-0381) In co-op this ran on the host only: in 4-player campaign runs (out/lab/20261031-000328
  campaign#1, 20261031-185843 campaign_tribe#1) the host's tracked citizens went 22 -> 48 within 3 minutes
  after a cinematic on the host (new tribes) while every guest kept `local_tribes=2` (its own game's tribe
  objects) for the rest of the stage; in the other run two of three guests lacked the host's new second tribe (the third guest's own game had formed it at the same place). A guest maps the host's tribes
  to its own by position (40 m) and had no tribe to spawn the host's new members into: sync-check
  "t: N missing on the guest" (N up to 27), and the players on the guests never saw those tribes. Not
  known why the guests' controllers never formed them (the trigger is probably the player tribe's
  progress, which a guest's game does not have: its player tribe holds mirrors).
* ✔ Fix `tribe.npc_tribes`: the host appends each tribe's species key (from a member, citizen +0xB28)
  and archetype (+0x550) to its state chunks, after the BUG-0365 clock trailer (tag 0x49425254 "TRBI", a
  count, 13 bytes per tribe); a guest that has no tribe of its own within 40 m of a host NPC tribe calls
  SpawnNpcTribe(host position, archetype, 0 members, 0 food, false, the species: the host's own creations
  through creations::local_species / want_species) and binds the mirrors into it. A tribe made this way
  is remembered (a 2 m grid of the host position) and reused even if the setup put it farther off.
* ~ A guest's own controller could still form a tribe later at the same marker (a second local tribe
  near a mapped one, not mirrored): not seen in the runs.

## Animal proxies under the guest's ground (2026-11-01, fix/w33-proxy-ground)
<a id="tribe-proxy-ground"></a>

BUG-0399. GOG 2024, lane 7, tribe fixture (Zorispe), the monitor's `proxies` rule and the `npc: proxy N
... under this game's ground` line (which now also says when and how high over the ground the proxy was made).
* ✔ Only right after a cold join (the guest's trace lines at s 13-30), in about 1 of 5 cold tribe_coop /
  chaos_tribe runs (and 3 of 3 runs of feat/w32-monitor-diag): 1-4 proxies 0.75-1.7 m under the guest's
  ground for 1-30 s. 4 of the 5 named ones were of one herd (species 0680AA69 around (385 -70 318), ~100 m
  from the guest's tribe); the same herd read 0.00 m (proxy, the host's position here and the host's NPC on
  its own ground) in a warm game later.
* ✔ They were made ON the guest's ground (`made 1.8-5.9 s ago at 0.00 m over the ground`: the host's
  position was on this game's ground there) and were under it 0.8-6 s later, some while `placed on the
  host's path` (walk_place), some standing (`driven`, drive::step). So not a spawn at the host's height
  (the ticket's first inference).
* ✔ Negative results (warm games at 30-60 fps, fix off): a proxy put 1.2 m under its ground without the
  teleport mark (`coop.npc sink <id> 1.2`, as walk_place places) is back on the ground within 0.25-0.6 s,
  standing and walking, near the tribe and ~100 m from it, also at 4 fps (`frame.limit 250`); with the mark
  (`sink <id> 1.2 1`) at once. Host animals walked up and down 3-6 m slopes (`coop.npctrack walk`, 2.5-4 m/s,
  2 x 40 s) went at most 0.52 m under. The guest making all proxies again (`coop.npc off` / `on`, 4 times)
  left none under; the terrain radius at the proxies' places did not change after the join (sampled every
  0.5 s from the join on).
* ~ Inferred: in the seconds after a join the guest's game does not update its creatures (seen for the
  partner's puppet, [creature-walk-30fps](creature.md#creature-walk-30fps): 7 s), so nothing grounds them
  while the guest places them at their own distance from the centre (own_height, BUG-0329) on rising
  ground; why some stay under for 20-30 s afterwards is not known.
* Fix `npc.proxy_ground` (driver.h `out_of_ground`, npc.cpp): a Tribe proxy's walk is placed onto this
  game's ground (`WalkPlace::ground`, the on_own_ground of BUG-0366: 5 cm into it when within 3 m under to
  0.5 m over), `drive::step(.., ground)` teleports a standing proxy found more than 0.25 m (up to 15 m)
  under the ground onto it, and a new proxy is made on the ground (a host position under a higher guest
  ground, BUG-0329; inferred only). It does not need the game's grounding.
* ~ The member mirrors walk with `own_height` and without `ground` too (tribe-mirror-walk); their `agl` stays
  -0.01 to 0.00 m on the village's flat ground, so they are left as they are.
* Diagnostics: `coop.npc list` lines end with `agl=` (host: its NPC over its ground); guest lines carry
  `agl= want_agl= made_agl= grounded=` (the proxy, the host's position over this game's ground, at its
  making, and how often drive::step put it out of the ground).

## NPCs apart on the guests: not reproduced after npc.walk_tribe (2026-11-01, fix/w33-proxy-ground)
<a id="tribe-npc-apart"></a>

BUG-0402. GOG 2024, lane 7.
* ✔ Every recorded `n apart` (one NPC 8-25 m on every guest for minutes; herds 8.2-10.5 m; 510 m) is from
  2026-10-30/31 campaign runs, before npc.walk_tribe (BUG-0361). With it: none in campaign_tribe (4 players, live
  Creature -> Tribe), play_tribe 10 min (co-op epic at s 480, hit by both), tribe_epic and 30+ other Tribe runs of
  fix/w33-proxy-ground.
* ✔ A co-op epic on the tribe fixture hit for 1 hp every 12 s (walks and fights at up to 6.5 m/s): its proxy, compared
  at the proxy's distance from the centre, p50 0.13 / p90 0.99 / max 2.08 m with walk_place, p50 0.51 / p90 0.61 / max
  2.57 m with the old drive::step (`coop.fix npc.walk_tribe off`). The epic alone does not explain 8-25 m.
* ~ The sync-check compares absolute positions; a guest's Tribe proxies are at its own ground height (BUG-0329,
  3.9-5.3 m radial in live campaign worlds), so a running herd's lag plus that radial part can pass the 8 m limit
  (inferred, no current run showed it). The 510 m case is BUG-0339's NPC at the planet's
  centre.

## Mirrors under the guest's ground while its game is paused (2026-11-01, fix/w36-tribe-ground)
<a id="tribe-mirror-ground"></a>

BUG-0444. GOG 2024, lane 10, tribe fixture (Zorispe).
* ✔ After a cold join the guest's game time stays paused (`cGameTimeManager::Get()` +0x48 = 3) and no
  BehaviorTreeTick runs (`coop.tribe`'s `ai_skipped=0/0`): the ticks started by themselves ~15-30 s after the
  guest was in the world in this branch's cold runs; in recorded tribe_coop runs `ai_skipped=0/0` was still
  there at s 40 in two of them. A running game reads `00` there. Writing 3 (`mem.write`) pauses a running game
  the same way (no AI tick within 1 s), writing 0 resumes it.
* ✔ While it is paused, nothing but the mod moves or grounds the mirrors: walk_place's placing still moves them
  (game::place works while paused), the game's locomotion does not run. With own_height (BUG-0329: the mirror's
  own distance from the centre) a member the host walked 60 m up a 7-12 m rise went 9.3 m under the guest's
  ground (the host's member at 0.0 on the host's ground, also measured on the guest's terrain), walked back down
  it floated up to 10 m over it. Resumed, the game put it onto the ground and onto the host's position within a
  second.
* ✔ In the failing tribe_coop (lab 20261101-183414, feat/w33-tests-session) the guest's BT ticks were still 0
  at s 40.6 and only 2394 at s 80 (passing runs: 665-6705 at s 50, 11-23 k at s 90): its game was paused well
  into the hunt order, whose run took the guest's member [2] ~220 m away (pos s 70 -> s 80). ~ So the 2.6-3.3 m
  under (s 60-70) are this, inferred from the counters (no per-frame data of that run). 1 of 152 recorded Tribe
  guest logs with the `agl_hi` field had a member under -0.5 m.
* ~ A new mirror is teleported to the host's absolute position: under the guest's ground where that lies higher
  (BUG-0329 / BUG-0332, up to 5 m in live campaign worlds), and nothing grounds it while the game is paused.
  `coop.tribe lift -2` with a spawn on the host (tribe_mirror_ground): the new mirrors at -2.0 m (fix off).
* Fix `tribe.mirror_ground` (tribe.cpp mirror_drive / mirror_step / guest_state): the animal proxies' ground
  policy (npc.proxy_ground, [tribe-proxy-ground](#tribe-proxy-ground)): a new mirror on the ground
  (`out_of_ground`), the walk aim out of the ground and walk_place's `ground` (on_own_ground), a standing mirror
  more than 0.25 m under it teleported onto it (drive::step's `ground`, now the caller's switch); while the game
  time is paused also from above (`drive::onto_ground`, within 15 m both ways), since nothing else brings it
  down. With it: -0.12 to 0.02 m over the ground on the same walks, the new mirrors -0.11 m.
* `coop.tribe` (guest) ends with `mirror_grounded=` (standing mirrors put out of the ground), and the first three
  per mirror log `tribe: mirror N (owner O) was X m under this game's ground: put onto it`.

## The physics update and a body without an owner (2026-11-01, fix/w37-tribe-fault)
<a id="tribe-physics-orphan"></a>

GOG 2024 (Ghidra, `tools/ghidra/q.sh --build 2024`); GOG 2016 has the same code 0x10 lower (0xB4C360,
world pointer 0x167FB50). BUG-0449.

* ✔ The Simulator subsystem "Simulator/cPhysics" (vtable 0x01461160, ctor 0xB4D320, dtor 0xB4CDC0) keeps a
  physics world at `[0x167EB70]` (`SporeCoop::PhysicsWorld_ptr`, 0xBC bytes): +0x08 a contact pool
  (cObjectPool_, 0x3C x 0x800), +0x3C the body pool (0x138 x 0x1000), +0x58 a hash map owner -> {body
  handle, ?, broadphase id}. Its per-frame Update (`SporeCoop::PhysicsManager_Update` 0xB4C370, thiscall,
  ret 4; slot 0x38 through the thunk 0xB4C6E0, called from 0xB5D0F0) works in mode +0x20 == 1 in Tribe.
* ✔ cObjectPool_ layout: +0 base, +4 free head (-1 = none), +8 generation, +0xC count, +0x10 high water,
  +0x14 element size, +0x18 capacity. An element's first dword is its handle (generation << 16 | index);
  DeleteObject writes the free list's next index there. `Iterate` (0xB72340 over 0xB72230) takes every slot
  whose low 16 bits equal its index as live; `GetIfNotDeleted` compares the whole handle.
* ✔ A body (0xB47830 creates it): +4 kind (0 = with a 0x116DD1B-cast object, which is +0x40 then; 1 = the
  object itself in +0x40; 2 = static, no owner read), +0x34..+0x37 flags, +0x40 owner. Kinds 0/1 read
  the owner without a null check in Update (0xB4C5B1 `mov eax,[esi+0x40]`, 0xB4C5B4 `cmp byte [eax+0x75],0`),
  in 0xB46E10 and in the contact check 0xB3FD80; any kind but 0..2 is skipped by all three.
* ✔ Bodies are deleted by 0xB43810 (owner removed: hash lookup, broadphase 0xBBC380, DeleteObject) and
  0xB47FE0 (owner +0x75 or +0x70 cleared), both through the hash map.
* Observed once (tribe_walk, out/lab/20261101-204356, guest, lane 6): a live body of kind 0 with owner 0
  (eax 0, ecx = kind 0, esi = the body) at 0xB4C5B4 in every frame from s 90.4 to the end (1819 caught
  faults); the game's handler unwound the rest of the physics update each frame (a mirror 9.6 m off
  afterwards). The guest had just box-selected its own members; nothing was spawned or removed then.
* ? How the body lost its owner is not known. A body freed twice would turn the pool's free list onto
  itself (the slot then looks live to Iterate and the next two creates share it); the guard logs whether
  the pool's free head equals the slot. Not reproduced in 6+ tribe_walk runs on lanes 9/10.
* The guard (`game.physics_owner`, mods/coop/gamefixes.cpp) gives such bodies kind 3 before each Update;
  `coop.physorphan` (tests) makes a live body of kind 0 without an owner past the pool's high water, which no
  owner refers to (coop_physics_orphan.py). Taking the owner off a body that an owner still refers to faults
  elsewhere first: 0xB3DD60 (0xB3DCB0 via 0xB47FE0 from the dirty-object loops at 0xB4C55F).

## The size of the members' mirrors (2026-11-03, fix/w40-scale)
<a id="tribe-member-scale"></a>

* ✔ A member's size is its own game's pick (cCreatureCitizen GetDesiredScale vt 0x80: random per member,
  the chief's bigger, this file's chief section): a guest's mirror (bound to a local member or made by
  SpawnMember) had another size (the detector's `Tribe:scale:citizen` 70 %, 3.79 host / 4.47 guest).
  The members are a replicated-field kind now (`tribe_member`, docs/sync-architecture-plan.md#sync-fields,
  `tribe.scale`). The chiefs' marker (chiefs_tick: desired scale x 1.3, on every machine) ends on the
  host's size too: the host's chief already carries the marker.

## How late a guest shows the members, measured in play (2026-11-03, fix/w40-play)
<a id="tribe-mirror-lag"></a>

BUG-0481: play_tribe failed `sync t*: 90% within 1.5 m` on every run (citizens p90 1.78-1.89 m, p50 0.04 m).
Measured with play_tribe's probe (the host's coop.sync read back to back for 0.6 s before each guest read,
the guest's member compared with every point of that path, `out/lane<N>/track/play_tribe_probe_*.json`),
lab 20261103-150636, 298 samples:
* ✔ standing members (< 0.3 m/s over 0.4 s): raw p90 0.04 m. Nothing drifts or stands apart.
* ✔ walking members (p50 8.4 m/s, p90 9.0 m/s: the members run to their orders): raw distance to where the
  host has them now p90 2.14 m, but on the host's path 0.21 / 0.22 / 0.25 s back (best lag p10 / p50 / p90,
  max 0.37 s) and only 0.09 m p90 (0.70 m max) off it there. 0.22 s x 8.4 m/s = 1.85 m: the raw failures.
* So the mirrors are on the host's path, `kMirrorDelay` (0.2 s, tribe.cpp, BUG-0365's interpolation
  buffer) + ~0.02 s late, by design (the host sends every 0.12 s or more). No mirror lags behind that,
  walks its own path or gets snapped in play. The 1.5 m limit is coop_sync.py's, which compares with the
  host's path over its LAG_WINDOW (players: 0.10 s delay + one 20 Hz interval); play_tribe compared with
  the host's present position, delay included: a measurement error, not a desync.
* play_tribe now compares a citizen with the host's path over TRIBE_LAG = 0.32 s (0.2 s delay + one 0.12 s
  send interval) and prints the raw p90 next to it. `coop.tribe delay <s>` (guest, tests) sets the mirrors'
  interpolation delay: `PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6"` is a planted late mirror the check
  must fail.
