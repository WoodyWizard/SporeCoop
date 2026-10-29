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
