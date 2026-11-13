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
~ superseded (2026-11-08): `max = pat[+0x5D4]` is wrong, see docs/re/tribe.md#tribe-healer-runtime.
CORRECTION (2026-11-08, fix/w46-g4b-coverage): pat+0x5D4 (combatant +0x2C) reads 1.0 on a tribe member whose
maximum is 112 (mem.read, lane 3); the maximum is the combatant's vt 0x58 (game::max_health). With +0x5D4 the
bonus never fired (BUG-0745).

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
  | cCreatureCitizen::SetSpecializedTool | **0xC27AE0** | thiscall(int toolType) ret 4; writes +0x102C, **0 on the leader becomes 0xB**, refreshes handheld (vt 0xAC(1)) and the tribe palette (~ superseded 2026-11-10: vt 0xAC rebuilds the abilities, the hand is not touched, see [tribe-held-item](#tribe-held-item)) | ✔ |
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
+0x20 transform, +0x48/+0x4C u64 bit mask (~ superseded (2026-11-13): see the CORRECTION below; fruit slots ~, set from random bits when planted in
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

CORRECTION (2026-11-13, fix/w49-small, plan-0.7.14 WLD-22): +0x48 / +0x4C are two u32 masks, not one u64: +0x48 the
fruit left (a bit per fruit), +0x4C every fruit slot (0x07 on a small plant, 0x1F on a big tree: at most 5 slots).
Evidence: the runtime values of `coop.tribe trees` above (30 trees: +0x4C only 0x07 / 0x1F, +0x48 a subset of it) and
the audit's check of the planting (WLD-22: at most 5 fruit slots). The 32-bit reads in tribe.cpp (fruit left),
census.cpp and worldobj.cpp are right; the "u64 bit mask" reading of the planting code was a guess (~).

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

## Tools of the player tribe and the guests (2026-11-03, fix/w41-tribe-actions)
<a id="tribe-tools-guest"></a>

BUG-0514 (players: a guest's members cannot take weapons from the racks the host built). GOG 2024 (Ghidra
`--build 2024`, runtime lane 2, `coop_tribe_tools.py`):
* ✔ A bought tool is made only in the buyer's game: the palette's placement (0xD13C05, the same address in
  both builds) does `GetToolByType`, `tribe->CreateTool(type)` (vt 0xB4 = 0xC96170), `tool+0x84 |= 8`,
  `tool->vt[0x64](tribe)` (SetTribe), 0xC9D2D0(tool) (a clock reset, 0xBC32A0), then the drag; the finish
  0xD10F90 puts it into a cLayoutSlot of the village and, for a type not bought before, sets the purchased bit
  (0xC8ED70 `tribe+0x19D8 |= 1 << type`, the test 0xC8ED90). Nothing of it reached a guest: the fixture's
  village has one tool (type 10, the fire pit) on both games; a rack the host built (`coop.tribe build 1`) was
  in the host's tool list only (lab 20261103-234416).
* ✔ The tool's spatial object is at tool+0x34 (the placement reads its position with vt 0x2C there);
  `game::cast(tool, 0x1186577)` gives it; SetPosition vt 0x38 / SetOrientation vt 0x3C place a tool made by
  CreateTool + SetTribe where wanted; it is drawn upright with the rotation of +Z onto the planet's up.
* ✔ DoToolActionForAllSelected (GOG 2024 0xCD09B0, ret 4): a damaged tool (+0x158 < combatant +0x120 vt 0x58)
  -> DoActionForAllSelected(0x18 Repair); type (tool vt 0x58) 1..9 -> (3 GrabTool); 10 (fire pit) -> (0xC Party).
  The order goes through DoActionForObjects (hooked): a guest sends it with the tool as target kind 2 (noun
  0x18C8F0C + position); the host's `game::find_object` finds its own tool within 8 m and its member takes it
  (+0x102C = 1 in 3-37 s).
* ✔ cCreatureCitizen::SetSpecializedTool GOG 2024 **0xC27B00** (2016 0xC27AE0), thiscall(int) ret 4: callers
  0xDABA9A (in 0xDAB4A0, the grab behaviour), 0xC8FF67 (0xC8FEE0: making a chief, 0xB), 0xDA75D9 (0xDA7590:
  `SetSpecializedTool(0)`, a drop), 0xD576EC. The guest's mirror never runs these (its AI ticks are skipped):
  it showed no weapon while the host's member had one (lab 20261103-234532, `tribe.tool_hold` off).
* Fix: the player tribe's tools are host-owned world objects (worldobj.cpp kind `tribe_tool`, switch
  `wobj.tribe_tool`): a guest makes a copy of a host tool it has none of within 2 m (the same type, upright,
  purchased bit); the held tool is a replicated field of the members (fields.h kind `tribe_member_tool`, field
  `tool`, switch `tribe.tool_hold`), applied with SetSpecializedTool (the leader's 0 -> 0xB left alone).
* ? Not done: a tool a GUEST buys is still made in its own game only (the host has no rack there, the guest's
  grab order is rejected "no such target here"): BUG-0524. A raided (damaged) rack's state is not replicated.

## The co-op epic's blows (2026-11-03, fix/w41-tribe-actions)
<a id="tribe-epic-damage"></a>

BUG-0515 (players: "in Tribe giants only deal 8 damage"). Runtime, GOG 2024, 'tribe' fixture, lane 2
(`coop_tribe_epic_damage.py`, `coop.tribe epic stats`: every live epic with its herd and its blows on members, read
in the host's citizen TakeDamage hook):
* ✔ The fixture planet's own epic: herd archetype 0xEC4C89F8, generation 0, mScaleMultiplier (+0x150) 1.0,
  mHitpointOverride (+0x154) 0, **mDamageMultiplier (+0x158) 15.0**, size 11.4, 1000 HP.
* ✔ The co-op epic (spawn_epic: CreateHerd + archetype 0xFC811218, isEpic): +0x158 = **1.0**, size 7.7-9.6,
  1000/1250 HP. Its blows on members: 1.5 and **8.0** (lab 20261104-000238): an ordinary animal's, exactly the
  players' "8". With +0x158 = 15 set on the herd before cCreatureAnimal::Create: 120 per blow (lab 20261104-000059).
  So the multiplier is read at the blow (or at Create) from the herd; the archetype's isEpic does not set it.
* The blows are the host's (it simulates every tribe); the guests see the members' health of the host.
* ? The co-op epic is smaller than the game's (7.7-9.6 vs 11.4, the same scale multiplier): its size comes from
  elsewhere (the archetype 0xEC4C89F8's tuning?), not changed.
## A guest's own tribes and saves (2026-11-03, fix/w41-tribe-world)
<a id="tribe-guest-world"></a>

* ✔ The objects the mod makes in a Tribe guest's game: the members `guest_state` spawns as mirrors
  (`spawn_member`: cTribe vt 0x98 SpawnMember, else CreateCitizen + AddMember) and the NPC tribes it makes
  for the host's (SpawnNpcTribe, tribe.npc_tribes, BUG-0381) with their hut (+0x368), tools (+0x36C, +0x380)
  and food mat (noun 0x629BAFE; ~ it holds its tribe's pointer at +0x2C: found by a scan at the first save,
  lane 5 2026-11-03, "a food mat holds its tribe at +0x2C"). Members of the guest's own game bound as
  mirrors (the loaded world's) are the player's.
* ✔ (BUG-0495, tribe.save_strip, lab 20261103-234419) A save on the guest writes none of them: the savestrip
  filter nulls their references, and while the save is written the members leave the tribes' member vector
  +0x340 and selectable vector +0x354 (citizen+0xC0). The reloaded world (alone, no session) had the player
  tribe with the guest's own 2 members (7 before the save: 5 spawned mirrors), no tribe where the made ones
  stood, no fault; the mirrors were back in the session after the save.
* ✔ `SporeCoop::NpcTribeController_FormTribe` (GOG 2024 0xCC6440): thiscall(controller, int) ret 4, slot 0x24
  of the controller's vtable 0x1476878 (no direct caller), the singleton from `cNpcTribeController::Get`
  0xCC8C90 (global 0x169B054, ctor 0xCC5BF0). Each call forms the next tribe when the planet has fewer than its
  property list allows (the spawn marker nearest the player tribe, docs above).
* ~ (BUG-0483) The detector's guest-only villages came after the live Creature -> Tribe change (campaign_ct)
  only. Inferred: the guest's own controller formed a tribe at another time or marker than the host's (the
  guests' games form tribes, the host's tribe there came later or elsewhere), or a host tribe the guest's was
  mapped to was gone. tribe.guest_tribes: on a guest in a session the controller's forming does not run (the
  host's tribes come through tribe.npc_tribes), and a local NPC tribe no host tribe maps to (40 m, or the one
  made for it) for 5 s of complete cycles loses its members (RemoveMember with destroy). Its hut, tools and
  food mat stay: no game function to remove a whole tribe is known (? which one the game uses when a tribe is
  conquered).
* ? Tools (BUG-0523): the mod syncs no tool; each game makes the tools its tribes buy at its own spots.
## A guest's orders on the host and the game's own: the grab spots (2026-11-04, fix/w41-tribe-orders)
<a id="tribe-orders"></a>

BUG-0505 (a guest's orders taken by the host, its members standing still). GOG 2024, Ghidra + runtime, lane 9
(`coop.tribe ai <id>`: the order record, the behaviour node with its memory, the walk; `coop.tribe layout <type>`:
the tribe's grab spots of a tool type).
* ✔ The game's own order and host_command run the same code: a click goes DoActionGeneric ->
  DoActionForAllSelected 0xCD0940 (the terrain cursor's selected objects, vt 0x5C) -> 0xCCFE70 ->
  DoActionForObjects 0xCCEB30 (per object: cast to citizen, `DoAction(action, target, prop)`), then 0xCCFDA0 (the
  selected members' voice, vt 0xF0) and a feedback sound per action. host_command calls the same DoAction with
  prop 0. Nothing else (no flag, no queue, no priority) is set by the UI path: the only difference is that the
  host's members of a guest are not selected on the host (+0x12C); the BT flags do not carry it (? the
  behaviour code was not searched for other reads of IsSelected, spatial vt 0x50 = 0xAD26E0) (the BT flags built in citizen Update 0xC24A30: `ai[+0x5FC] | 4 if the list at c+0x38 is not empty | 8
  unless c+0x110 & 0x100 | 0x40 if an order record exists | 0x400/0x800/0x1000 by c+0xEAC`).
* ✔ DoAction 0xC27710: copies the current record, `0xBCB0E0(ai, action, target, 1)` clears the queue at
  ai+0x60C and appends one record, copies prop and the target's position, 0xC265B0 posts the action's stimulus
  (the switch action -> mask: 0 -> 8, 2 -> 0x20, 3 -> 0x40, 10 -> 0x8000, ...), and **only when the new record
  differs from the old one** (0xBCA2F0 compares action, target and the four properties) posts stimulus 0x4000
  (re-plan). So a second identical order (the same tool again) does not restart a member stuck in its node.
* ✔ Behaviour nodes (the action table at 0x1593A80, entries `{id, enter, tick, exit}`): **0x0462854D = GrabTool**
  (enter 0xDAFC10, tick 0xDAB4A0, exit 0xDAFD20), 0x0462854E next to it, 0x0562C0FB heal, 0x04611FF4 sleep,
  0x0461300F eat, 0x04623E0C gift food, **0x05F79546** (enter 0xDA7850: needs stimulus (0, 0x4000000), plays
  anim 0x5F73E48 and the tick 0xD7C730 waits for its end: a reaction to an area blow, posted by 0xC20390 to every
  creature within its radius; ~ name), 0x05CA7FA7 knocked out. **0x0569236E** is in another table (0x158426C,
  enter 0xD75A80, tick 0xD75F70: the gather behaviour, a 20-state machine with the tree, walk, pick and bundle
  steps). 0xCCA550 maps a member's node to the card's activity icon.
* ✔ The grab node's memory (the leaf's, ai+0x1D0 + 4 * (0xF + 0x2B * (depth - 1)), depth ai+0x1DC):
  `+0 state, +4 the tool, +8 the tool's position at the enter (moved more than 1 m: the node fails), +0x14
  took-it flag, +0x18 claimed spot, +0x1C wait counter`. States: 2 walk near the tool (chiefs: to the hut), 3 wait
  until within reach, 4 claim a spot (0xC98860: cTribe, the free cLayoutSlot of the type's layout
  (SporeCoop::cTribe_ToolLayout) nearest to the member; none: walk 1-2 m, wait 45 ticks, again), then WalkTo the
  point on the line tool -> spot at the radii + 0.1 from the tool facing it, 1 wait for the walk's arrival
  (0xC42F80 on c+0xC0: goal mode +0x250 1-4, radius check, modes 3/4 also the facing; **no time limit**), 6 / 7
  the grab animation and its event 0x7D7502FC -> SetSpecializedTool(type), 8 done.
* ✔ The grab spots of the 'tribe' fixture's rack built 9 m off the centre (`coop.tribe build 1 9`): 6 spots on a
  ring ~6.8 m round the rack; the one toward the village is at (406.1 -7.1 302.7), 2.2 m from the fire pit's
  centre, the member's radius 2.87. Observed in a failing tribe_tools run (lab 20261104-014156 #2): the guest's
  member in node 0x0462854D, state 1, spot 0, walk mode 3, goal that spot, standing 1.5 m short of it for 90 s
  (speed 0, the 40 s re-order changed nothing). The order was the guest's and taken by the host as its own; the
  member stopped because the spot cannot be stood on. Three of the six spots were used (`used=1`) before the order.
* ~ Why the walk stops short is not pinned down: a member put 5 m from the rack toward the fire pit, also with
  another member put onto that spot, reached it (or took another spot) in 6 s with the fix off; the stop in the
  failing runs was 1.5 m short with walk flags 01 (+0x270) and mode 3 (the arrival also checks the facing).
* Fix (`tribe.grab_spot`, host): a player tribe member in the grab node's state 1 standing still 4 s within 3 m of
  its walk goal is set onto the goal facing the tool (game::teleport); the node then arrives and grabs.
* ~ The play_tribe idle orders of the earlier runs were not reproduced in 2 runs on lane 9 (lab 20261104-014832):
  the host's own idle orders there (ok 9 of 14 in 20261104-010027) show the same game behaviour for host orders.
* ✔ (play_tribe, out/lab/20261104-030728 #3) A member stuck in the gather node 0x0569236E (state 1, walk mode 1,
  its goal the old tree 100 m off, not moving) keeps that node when a new order comes (rec=4 walk, rec=10 hunt with
  its target set): the record is replaced, the node is not left. ? what ends the gather node's state 1 (tick
  0xD75F70 case 1 runs while the list at c+0x38 is empty or +0xFC8 != +0xFC4, unless 0xC26DF0(0x30000)); BUG-0505.
## Every tribe's tools, the palette's drag and a guest's purchase (2026-11-04, fix/w42-tribe-left)
<a id="tribe-tools-all"></a>

BUG-0523 (each game places the tools its tribes buy at its own spots), BUG-0524 (a guest's purchase only in its
game). GOG 2024, Ghidra (`tools/ghidra/q.sh --build 2024`):
* ✔ One tool per type and tribe: the palette's placement start (0xD130D0, the tool branch at 0xD13C05, message
  property 0x4294750 = the tool type) calls `GetToolByType(type)`, makes the new tool (CreateTool vt 0xB4), sets
  `tool+0x84 |= 8`, SetTribe (vt 0x64), 0xC9D2D0, starts the drag 0xD09FA0(tool+0x34), sets `tool+0xA2 = 1`, and
  when the tribe had a tool of the type: `cLayoutSlot::RemoveObject` and **RemoveTool (vt 0xB8) of the old one at
  once** (a move of a tool is a new tool, the old one destroyed).
* ✔ The drag: 0xD09FA0 thiscall(placement, spatial) ret 4 keeps the dragged spatial at placement+0xC0, clears
  `+0x72` of the last one and sets **`spatial+0x72 = 1`** on the new one (a tool's spatial is tool+0x34: the byte
  tool+0xA6 is 1 while the cursor drags it). The finish 0xD10F90 (thiscall(placement), ret 8, only caller
  0xD12910) puts the tool into the village's layout slot nearest (0xD0B750, `cLayoutSlot::SetObject`), sets the
  purchased bit for a new type (0xC8ED90 test, 0xC8ED70 set) and posts message 0x53850BAF {type, tribe +0x504};
  0xD09FA0(0) ends the drag. A cancel / sell is 0xD09D70: message 0x53850BAF, RemoveTool, then 0xC8F600(0,
  0x800000, FLT_MAX, 0) = SporeCoop::BT_PostStimulus 0x800000 on every member (vt 0x90).
* ✔ cTribe::RemoveTool GOG 2024 0xC97020, thiscall(tool) ret 4: the village's layout slots holding it
  (tribe+0x404..+0x408, 0x18 each) `RemoveObject`, out of +0x380 and +0x36C, tool vt 0x44, DestroyInstance,
  0xC96B90(&type).
* So a tool the host was dragging was in its tool list (and in BUG-0514's snapshots) at the cursor's places: a
  guest could copy it mid-drag. Tools with spatial+0x72 set are not world objects now.
* Fix (worldobj kind `tribe_tool`, state = type | host tribe index << 8 | 1 << 16 for the player tribe):
  `wobj.tribe_tools_all` every tribe's tools (host index = the MSG_TRIBE_STATE table order, a guest's local tribe
  by tribe.cpp's mapping g_local_tribe_of), a guest's own tool the host has not goes (RemoveTool, kind drop_own),
  the host's is made in the mapped tribe only when that tribe has no tool of the type (one per type: a dragged
  purchase or an own one about to go waits); `wobj.tribe_tool_buy` a guest's own player-tribe tool without a host
  partner is a request (MSG_WOBJ kind 1 with id 0, state as above, every 2 s for 20 s): the host removes its tool
  of the type elsewhere and makes the same at the guest's place (tribe.cpp buy_player_tool); the next snapshot
  binds the guest's copy. The food: the guest's palette paid in its game, the shared food ledger (shared.cpp
  MSG_RES_ADD) takes the change to the host; the host pays nothing more.
* ? Not done: a tool made by the mod (a guest's copy, the host's copy of a guest's purchase) is not put into a
  village layout slot (the finish's 0xD0B750 / SetObject): the slot looks free to that game's palette. A guest
  selling a tool (0xD09D70) is not sent: the host keeps it and the guest gets its copy back.
## The gather behaviour and a new order (2026-11-04, fix/w42-tribe-left)
<a id="tribe-gather-orders"></a>

BUG-0531 (a member in the gather node does not act on a new order). GOG 2024, Ghidra + runtime lane 6
(`coop_tribe_gather_stall.py`, `coop.tribe ai` now also prints `carry=` the food the member carries and `tool=`):
* ✔ The gather node's table entry 0x1584260: `{0x42464100, 0x2B8CBCCC, 100.0, 0x0569236E, enter 0xD75A80, tick
  0xD75F70, exit 0xD75520, 6, 0, 1.0, 0, FLT_MAX, 4.3125, ...}` (100.0: the range the ticket saw next to the id).
* ✔ Its memory (the leaf's): `+0 state, +4 an object (refcounted, its +0x108 released at state 0), +8 the tree,
  +0xC, +0x10 a timer, +0x18 a counter (state 0 fails when it passes a tuning value 0x78F4963A), +0x1C byte, +0x1D
  byte "ordered" (from the enter's argument +0x18), +0x20 a claimed food-mat slot (0xDBA5C0(c, 0xD)), +0x24 an
  object`. The enter zeroes it, state 1 when it starts with a walk.
* ✔ 0xCEE490 / 0xCEE4E0 on the member's own cGameBundleContainer (c+0x38: bundle vector +4..+8): the first bundle /
  the sum of the bundles' +0x134 (the food it carries). 0xC26DF0 thiscall(c, mask, x) ret 8: the handheld item of
  the member's tool (+0x102C; 0 on the tribe's leader = item 0x11) tested against `mask` (0x30000 in the gather).
* ✔ The tick's state 1 returns "running" while (it carries nothing, or c+0xFC8 != c+0xFC4) and 0xC26DF0(0x30000)
  is false; then the walk's arrival (0xC42F80) -> state 0; otherwise, unless +0x1D, "running" again: **no time
  limit**. State 0xF with +4 = 0 returns 0 ("done") at once; the failure path (LAB 0xD76512) is 0xDA61D0(c, 2) then
  0. States 0x12 -> 0x13 (claim a food-mat slot, 0xDBA5C0(c, 0xD)) -> 0x14 (walk there; arrived and 0xAF0B50 ->
  state 0, else the failure path).
* ✔ The exit 0xD75520 releases +0x24 and +4, drops or keeps the carried bundles (0xC23460 / cBundleManager), frees
  the food-mat slot (0xC988E0 / 0xC97ED0 with 0xD) and 0xD753C0. It does not touch the order queue.
* ✔ Runtime (lab 20261104-095422, -100046, -100245, -100604; 'tribe' fixture, a guest's member sent by
  `coop.tribe order <id> 2 <tree>` to a fruit tree 90-130 m off): the member walks there in state 1 (walk mode 1,
  goal the tree, carry 0), then states 5, 2 (at the tree), 19 = 0x13 (back home to a food-mat slot, goal near the
  village), then the delivery node 0x04623E0C and 0x05524115. A walk order (the guest's own: select + walk, run
  by host_command's DoAction(4) + SetValueVector3) in state 1 at the tree or in state 19 carrying the food home
  took the member out of the gather node every time (7 runs): rec=-1 and node 0x04626360 (an idle / walk-end
  node) within 3 s, the member at the goal on both games. After a walk that ends, the game's own behaviour goes on
  (a member carrying food delivers it).
* ~ So the play_tribe stall of BUG-0531 (state 1, walk mode 1, goal the old tree, standing) was not reproduced:
  inferred, it needs the state-1 wait with the walk itself stopped (the member standing short of the tree, as the
  grab spots of BUG-0505), which the gather's state 1 never leaves without arriving. Not fixed.
* ✔ A test pitfall: a walk goal 12 m from the village toward some trees ended at the hut's edge (the game's walk
  stopped at (413.8 -22.6 291.4) in three runs, the member 8 m short): check that a member walks, not that it
  reaches a point near the village.
## Chiefs put in each other after the arrival (2026-11-04, fix/w42-tribe-left)
<a id="tribe-chiefs-overlap"></a>

BUG-0340, 2 players, 'tribe' fixture, lane 6 (`coop.tribe near <ratio>`: the later chief put that part of the sum
of the two chiefs' bounding radii from the first, standing):
* ✔ The chiefs' bounding radii (spatial vt 0x70) are ~8 m in this fixture: 15 m apart is 0.92 of the sum. The
  bounding sphere is much larger than the body: "closer than the sum" is not a visible overlap.
* ✔ Put at 0.46 of the sum (~7 m, more than spread_chiefs' 1.5 m) with no mod parting: 0.92 within 14 s on both
  games (the game's avoidance; lab 20261104-102641).
* ✔ At the fixture's arrival (session s 5) the two chiefs stood closer than 0.6 of the sum for 2 s before
  place_chiefs or the game parted them (a watchdog's log in lab 20261104-102249 and -102641; the watchdog,
  `tribe.chiefs_overlap`, was removed again for want of a failing run).
* ✔ 4 players (campaign_tribe, out/lab/20261104-102948): the three chiefs place_chiefs put on ring spots at the
  arrival were back within 14-28 s at 5.0-5.3 m from the host's chief (bounding radii sum 5.8-6.2 m) on all four
  machines: the idle behaviour gathers idle members round the host's chief / the centre. ? which node and target.

## The player tribe's food after the game spends the last of it (2026-11-04, fix/w43-replay)
<a id="tribe-food-zero"></a>

* ✔ (lab, tribe fixture, GOG 2024): `cTribe::AddFood(tribe, -n)` (`SporeCoop::cTribe_AddFood`, the game's own
  spending path -> `cBundleManager::RemoveBundles`) that spends the whole food removes the emptied bundle: the
  container's vector at tribe+0x210 is empty afterwards (begin == end). Editing the amounts at bundle+0x134 down
  to 0 (shared.cpp s_food, `coop.res add food -n`) keeps the bundles.
* ✔ consequence (BUG-0572): shared.cpp s_food returns without a change on an empty vector, so a guest whose game
  spent its last food (a purchase, its members eating) keeps 0 food for good while the host's total goes up
  (tribe_food_zero, out/lab/20261104-201453). A total written there needs AddFood (creates bundles) for the
  positive part.

## A model's attached-effect list and the BUG-0575 crash site (2026-11-04, fix/w43-replay)
<a id="tribe-model-effects-crash"></a>

* ~ (disassembly, GOG 2024 = Steam 3010a413 code): 0xC8AC40.. walks a model's attached effects (vector at
  +0xC0 / +0xC4, 0x3C bytes per entry, the effect at entry+0x38, a distance threshold at entry-0x30 compared with
  the camera's): an effect that comes into range gets vt+8(0), one that leaves vt+0xC(0).
* ~ 0xA82660 (thiscall, the effect's vt+8 with one int): checks +0x28, flags +0x2C, then calls vt+8(arg) of each
  component (vector +0x144..+0x148, 0x5C bytes each, object at +0x38). BUG-0575 crashed in that loop with
  edi = 0x5C, esi = 1 (a component's call returned with the callee-saved registers broken: a freed component).

## The red ring under an attack order's target (2026-11-04, fix/w43-guest-player)
<a id="tribe-target-ring"></a>

BUG-0570. GOG 2024, Ghidra. See also docs/re/creature.md#creature-player-rules.

* ✔ The attack order (DoActionGeneric 0xCD0C20 input action 0xA -> DoActionForAllSelected 0xCD0940(7, target)) draws
  nothing. The ring is the Tribe HUD's: `TribeTargetRings_Update` 0xCCC7C0 (thiscall(owner), owner =
  [`TribeUI_ptr` 0x169B2C8] + 0x240, from 0xCCDDB0 <- TribeUI vt 0x34) draws, every frame while owner +0x78, a ring
  with `TribeTargetRing_Draw` 0xCCBE90 for every **selected** citizen with a live combat target +0xE7C (kind =
  citizen +0xB74, flag = target spatial +0x77), plus the eat target and social members, then stops every ring not
  touched in that pass. The rings are a map keyed by the target's +0x24 at owner +0x2C0 (buckets +0x2C4, count +0x2C8,
  node {key, effect id, IVisualEffect*, touched +0xC, next +0x10}).
* ✔ +0xE7C is set by SetTarget vt 0x84 0xC0EEC0 (thiscall(c, combatant, bool, int kind) ret 0xC) from the AI attack
  node's enter 0xD67980 only; a guest's mirrors run no AI, so its game never had a ring.
* `tribe.target_ring` (tribe.cpp): the guest remembers its own attack order (target, members) and, after the HUD's
  pass, draws the ring under that target while one of those members is selected, the target lives and no other
  order came (120 s at most). `coop.tribe ring <id>` says whether the HUD's map holds a live ring under creature
  <id>. ~ Alternative not done: the host sends each member's +0xE7C and the guest sets it on the mirrors
  (SetTarget on a mirror makes a cTribeToTribeMission, side effects not audited).
## The goal card, village relations and the sky on a guest (2026-11-04, fix/w43-world-state)
<a id="tribe-wsync"></a>

What the host-owned world state rows (docs/sync-architecture-plan.md#sync-wsync) use in Tribe:

* ✔ (Ghidra, GOG 2024) the Tribe goal card ("attack / befriend the <colour> village") is the **current
  cTribeToTribeMission** of the Tribe Mission Manager: `SporeCoop::TribeMissionManager_Get` 0xCD35C0 (cdecl, `mov
  eax,[0x169B27C]`) +0x74. Manager: +0x14..+0x1C every mission (the generic goal manager base 0xB6F5xx, shared with
  the Creature goal manager 0xD38150), +0x70 dirty byte, +0x74 current mission, +0x78..+0x84 map pol -> mission.
  cTribeToTribeMission (noun 0x5436411A, ctor 0xC2F5D0): **+0x7C state** (the ctor sets 3, 5 = done; vt 0x60
  0xA02C70 reads it), **+0x80 the village** (cTribe, intrusive), +0x84 / +0x88 the card's property lists, +0x8C /
  +0x90 cached from the two tribes' +0x230.
* CORRECTION (2026-11-04, fix/w43-world-state): #tribe-detector-state says the cMission state is +0x84 (cMission::SetState
  0xC47E20); for cTribeToTribeMission the state is +0x7C (vt 0x60 reads it), +0x84 is a property list pointer written
  by 0xC30000. Evidence: decompiles of 0xA02C70, 0xC30000, 0xC2F7F0.
* ✔ `SporeCoop::TribeMission_SetTargetTribe` 0xCD3D70 thiscall(mgr, cTribe or citizen) ret 4: the mission of its
  political id (made when new), mgr+0x74 = it, the old card removed from the HUD panel and the new one added and
  focused unless its state is 5, then mgr->vt 0x28(0). Callers: the input actions 0xC / 0xE / 0x10 (0xCD0C20: attack,
  gift on another tribe's object), SetTarget 0xC0EEC0, tribe messages 0xCD1D60, BT 0xDBB7D0. **The card follows the
  last village the player acted on**; no "next village" pick was found. ✔ (runtime, lab 20261104-205242) called on
  the guest outside the frame it moved the guest's card (`MissionCardPanel_HasCard` true for the new mission).
* ~ (Ghidra) card text 0xC30000 (fastcall(mission), vt 0x80 via 0xC301E0): relation level > 1 -> prop 0x3D61DF6C
  (befriend), else 0x57E2F080 (attack). Completion 0xC2F7F0 (vt 0x78): +0x7C = 5, 0xCDB520(kind 1 conquered / 2
  allied, tribe), TRG_TotemPole_Conquer; ? its caller.
* ✔ the goal progress bar: `SporeCoop::TribeStage_AddGoalProgress` 0xCDB520 thiscall([0x169B2C8] = cTribeUI_Get,
  kind, tribe) ret 8 adds a tTribeGoalData to +0x40 (mGoalProgressHistory: the owner of table 0x135DB90 is that object)
  and cPlayer +0x10F0 += 1; 0xCDE470 sets +0x10F0 = 0, +0x10F4 = min(5, villages - 1 + ...) at a new game; the HUD
  (0xCCDDB0) reads the two floats every frame. 0xB3D500 on GOG 2024 returns [0x167EB60], the mission card panel
  (`SporeCoop::MissionCardPanel_Get`), not cTerraformingManager (a GOG 2016 name).
* ✔ (runtime, lab 20261104-204941) `ApplyRelationship(mgr, npc pol, player tribe pol, 0x530CF07, d)` moves
  `CalculateRelationship` by d at once and it stays (0.00 -> -7.00, 20 s later -7.00); with d from
  `CalculateRelationshipAbsolute` 0xD05A60 (ret 0xC, unclamped) the result is the host's value. Nothing in a guest's
  game moved its villages' relations before (BUG-0565): the host's raids and gifts are events in the host's game.
* ✔ (code + lab 20261104-205648) **BUG-0579**: MSG_WORLD_TIME was applied only when `game::avatar()` is set, and
  game::avatar() is null outside Creature and adventures: a Tribe or Civ guest never took the host's sky; both start
  from the save's time at their own load, so they look the same until a load or a reload of one of them (0.097 rad,
  about 15 s of a 960 s day, apart in a plain session of the FAIL run; the host's game over and reload moved its sky
  0.2 rad). The tod row applies while the guest has its own entity in any of the three stages.
* ✔ (lab 20261104-205648, every fix off) a guest's rejoin loads the host's current world: relations, the goal, the
  stages done and the food were the host's after the rejoin without any row (the world transfer carries them).
* ~ (lab 20261104-205651) the host's game over and reload kept the village's relation, the goal card and a
  consequence trait written before it (the reload does not restore cPlayer or the relationship manager from the save,
  or the game saved them before the reload: ? which).
## Whole-planet re-alignment after transitions (2026-11-04, fix/w43-realign)
<a id="tribe-realign"></a>

W2 of 0.7.7 (BUG-0564, BUG-0566, BUG-0567, BUG-0563; classes BUG-0421, BUG-0427). GOG 2024, lab lanes 6 / 7.

* ✔ The player reports' sessions: the host's game went Tribe -> Loading -> Tribe three times (the game over's
  reload of its last save) while the guest's stayed in its world. Lab (tribe_bounce_world): after the host's reload
  its world had 2 tribes (the player's and the save's NPC tribe), the guest's 5: BUG-0483 emptied the 3 extra
  tribes to 0 members, their tribe objects, huts, tools, food mats (and map marks) stayed (out/lab/20261104-204705).
* Decision: the guest re-aligns in place, it does not load the host's world again: a load costs a loading screen
  at every host game over and the guest's own game's changes since, and does not cover a rejoin, an editor return
  or a death (the same mechanism must).
* ✔ `cGameNounManager::DestroyInstance` of a cTribe in the Tribe stage on a guest (realign.villages,
  `tribe::remove_village`, members first with RemoveMember destroy): the tribe, its hut, its tools and its food
  mat leave the noun list together (coop.realign planet: village 5 -> 2, hut 5 -> 2, food_mat 5 -> 2 in one
  look, lab 20261104-211648), no fault in the 40 s after. ? The map mark: not read by a test (the planet map
  lists tribes from the noun list, inferred).
* ✔ A guest's own Drama Manager event kind 7 (creature attack) in Tribe faulted 15 s after such a removal, in its
  start (0xD521BA, data 0x120, null + 0x120; lab 20261104-211318): the event had picked a village. On a guest in
  Tribe the kind does not start any more (drama.cpp, tribe.guest_attacks).
* ✔ Huts (noun 0x1E4DAAE) and tools (0x18C8F0C): cCombatant at +0x120, health +0x158 (combatant +0x38), maximum
  +0x14C (+0x2C). The fixture's NPC huts read 1000/1000 on both games. Written on a guest
  (`coop.realign hp hut x y z <permille>`) the guest's own value changes, nothing goes to the host. ? Whether the
  hut's damage effects (smoke, the broken model of BUG-0567) follow the health field or only TakeDamage: not seen
  (no screenshot compared).
* ✔ The whole planet's lists in the tribe fixture (coop.realign planet): 5 villages, 5 huts, 5 food mats, 6-10
  tools, 49 nests. Tools of far tribes differ between the games (the guest 6, the host 8-10: tools within 150 m of
  the guest only are synced, wobj.tribe_tools_all): BUG-0488's tool rows on the whole planet, not changed here.
* ✔ (BUG-0563) The co-op epic (tribe.cpp spawn_epic) took the species of the animal nearest the player village.
  Player logs: the guest's puppet of the Creature stage (species of the guest's creation) stood next to the host
  until the stage change; the epic came 8 min after it (its timer), the host fought "the guest's creature" as a
  monster. tribe.epic_species: puppets, proxies and the players' creatures' species are never the epic's.
## Tribe state the detector reads (2026-11-04, fix/w43-detector2)
<a id="tribe-detector-state"></a>

What `coop.wstate` (mods/coop/wstate.cpp, docs/06-testing.md#testing-detector-state) reads in Tribe, all from the game:

* ✔ (Ghidra, GOG 2024) cTribe serializer table (0x13551E0 -> 0x157ABB0), field offsets: +0x260 mpFoodMat, +0x264
  mbMotiveCheatOn, +0x268 mRSquareSize, +0x270 mVignetteTimer, +0x2C0 mEggPenFoodValue, +0x2C4 mInitialRelationship,
  +0x2C8 mChieftainRespawnTimer, +0x2CC mGiftRelationshipDecayTimer, +0x300 / +0x301 mbCheckedForWater / Forests, +0x304
  mClosestWater, +0x314 mClosestForests, +0x32C mUpgradeLevel, +0x330 mZoningRadius, +0x334 mbRoboTribe, +0x338
  mRoboPopulationCount, +0x33C mbVisualized, **+0x340 mTribeMembers**, +0x354 mSelectableMembers, +0x368 mpHut, +0x36C
  mTools, +0x380 mSocialTools, +0x510 mPopulationTimer, +0x530 mTimer, +0x550 mTribeArchetype, +0x557 mGoodyPopped,
  +0x1904 mSpeciesKeys, +0x19C4 mpDomesticatedAnimalsPen, +0x19C8 mpEggPen, +0x19CC mpDomesticatedAnimalsHerd, +0x19D0
  mpTotemPole, +0x19D4 mhFootprint, +0x19D8 mPurchasedTools.
* ✔ (runtime, lane 2, tribe fixture) every tribe's food = the sum of its bundles (+0x20C, as for the player tribe): the
  NPC tribe had 70 on the host and 80 on the guest at the same time (each game's own NPC tribe gathers on its own);
  its relation to the player tribe `CalculateRelationship(mgr, tribe pol, player tribe pol, 0)` 0.00 on both; the player
  tribe's political id 0x800021CA, the NPC tribe's 0x000021CB, the same on both games (the guest loads the host's save).
* ✔ (runtime) cRelationshipManager +0x10..+0x1C in Tribe: -7, -3, 3, 7 (~ the level thresholds GetRelationshipLevel
  uses: the UI's aggressive / slightly aggressive / neutral / friendly / allied, not checked against the UI).
* ✔ (Ghidra) **the Tribe Mission Manager** (UTF-16 name "Tribe Mission Manager Simulator" before its vtables): instance
  global `SporeCoop::TribeMissionManager_ptr` 0x169B27C (ctor 0xCD3CE0, dtor 0xCD3C30, vtables 0x14772CC / 0x14772A8 /
  0x1477298). Serializer table 0x157EE90 (0x135D720): **+0x74 mpTribeToTribeMission** (an intrusive pointer; 0xCD3600
  checks its vt 0x60 == 5 before a save), +0x78 mTribeToTribeMissionMap (hash map: buckets +0x7C, bucket count +0x80,
  count +0x84). cTribeToTribeMission (noun 0x5436411A, table 0x1572A18 by 0x1336CD0): **+0x80 mpAssociatedTribe**; the
  cMission state is +0x84 (cMission::SetState 0xC47E20; state 3 posts 0x38CF2FD).
* ~ (runtime, tribe fixture) +0x74 was null and the map empty on both games: the fixture's tribe has no tribe-to-tribe
  mission. ? Whether this mission is the goal card players see ("attack the X village's centre", BUG-0564) is not
  shown: `goal tribe=-1` in every sample so far. Another tribe-stage table (0x135DB90): +0x40 mGoalProgressHistory
  (a vector of tTribeGoalData), +0x78 mTotalTimeInTribe, +0x7C mFullHealsPerPerTribeMember, +0x94 mEnchantedAnimals,
  +0xA8 mUnlockedTools, +0x19C mpAccessories, +0x258 / +0x2A8 player / NPC spear ornaments, +0x3A3 mbMatingAllowed
  (the owner object of that table not found yet).
* ✗ `cRelationshipManager::IsAllied` 0xD02030 / `IsAtWar` 0xD01F90 take two **cEmpire** pointers (their pol at +0x84,
  stack purge 8), not political ids: not usable for tribes.
## What the NPC tribe controller needs before it forms a tribe (2026-11-04, fix/w43-tests)
<a id="tribe-form-needs"></a>

* ✔ (Ghidra, GOG 2024 0xCC6440, NpcTribeController_FormTribe) Before forming, besides "fewer tribe nouns (0x18C6D19,
  counted from the noun list) x 2 than the property list (controller +0xFC) holds" and a spawn marker (noun
  0x36BE27E with +0x108 == 0x57F09E5E): the next species (list +0xD0, index min(tribes - 1, size - 1)) must have its
  model baked: `Editors::IBakeManager::Get()` vt 0x1C(species + 0x504, 0); when 0 it asks for the bake (vt 0x3C,
  else vt 0x4C with key 0x609B763 / 0x40000) and returns without forming. The game calls it again later.
* ✔ (runtime, lane 2) The tribe fixture has 4 spawn markers; each formed tribe uses one (4 -> 1 after three).
* ✔ The pause bit (cGameTimeManager +0x48 = 3, held with mem.write) does not stop the forming (`tribes 2 -> 3`).
* ✔ With tribe.guest_tribes on, a guest's controller never reaches the bake request (the hook blocks the call), so the
  first call with the fix off may only start the bake (BUG-0587).

## The end of Tribe: from the goal bar to the Civ stage (2026-11-04, fix/w43-transitions)
<a id="tribe-civ-transition"></a>

BUG-0576, BUG-0585. GOG 2024, Ghidra (tools/ghidra/q.sh) and lab runs on lane 4 (tribe fixture, coop_tribe_civ.py).

* ✔ The goal bar full (cPlayer +0x10F0 >= +0x10F4) shows the HUD's **advance button** (window id 0x6454B47) in place of
  the History button (0x44827B8): 0xCC8C20 thiscall(display strategy, bool full) toggles the three windows (0x6454B47,
  0x652A628 shown, 0x44827B8 hidden). Called at the stage's entry (0xCD78F0) and by the stage's message listener
  0xCD81F0 on message **0x5CC2589** (sent after a goal; with the bar full it also posts hint 0x566CDF0 and, with
  tutorials on, pauses with reason 0x4BF38A7). `coop.tribe advance` (tests) fills the bar and posts 0x5CC2589: the
  button appears within 3 s (lab, lane 4).
* ✔ The button's click is handled by the stage's window proc 0xCDBEA0 (vtable 0x14775C0 +0x10, control id
  0x6454B47): achievements, the stage card (History screen with the consequence trait; its check at (1415, 1055)),
  then `PlayCinematic("TRG2CVG_PreEditor")` (id 11E9E5AF; its caption "Your tribe now dominates this continent ..."
  waits for the check (1410, 1048) with the game paused).
* ✔ **The game opens the City Hall editor from a paused game**: the listener 0xCD1D60 takes the cinematic's end
  (message 0x44F1189 with the cinematic's id), `cGameTimeManager::Pause(0x4BF38A6)`, cGameInputManager +0x110 += 1
  (the input lock), cTribeUI +0x28 = 1, a new EditorRequest (0x9C bytes) with editor **0x99E92F05** (the building
  editor, City Hall), +0x90 = 0x116DD1B, flags +0x34..+0x3D, +0x64 = 1, then `Editors::EditorRequest::Submit`.
  After the City Hall (back in Tribe) TRG2CVG_ShowCityHall (id 10792A72) plays, its caption waits, and its end
  submits the same way the **vehicle editor F670AA43** (or 9AD7D4AA / 7CB21BAD by cPlayer +0x1104: the consequence
  archetype), +0x29 = 1. After the vehicle editor the game goes to Civ (0x1654C04) by itself.
* ✔ `cGameModeManager::SubmitEditorRequest` 0xB1DE20 (thiscall(mgr, req) ret 4) keeps the request at +0x2C; when
  the mode object +0x30 vt+8(0x4CF52822) says so it pauses with the same reason 0x4BF38A6, listens for 0x462C656
  and its handler 0xB1DF60 submits the request and only then resumes 0x4BF38A6. The pause reason 0x4BF38A6 is "an
  editor is coming": the editor (and the stage after it) lifts it.
* ✔ The editors save only with **at least 3 parts** ("You need at least 3 parts to Save") and a name ("You must give
  your creation a name before you can save"; the name panel's dice at (614, 826)); done at (1416, 1058). The building
  editor's palette first row (88 / 220 / 350, 130), the plot around (900, 850); the vehicle editor the same.
* ✔ **BUG-0576** (observed, lab 20261104-2213xx on 0.7.6 code): the vote's busy gate (BUG-0404, busy::kEditorGate)
  held the held request while the game time stood or the input lock was up: both were the game's own for this
  editor, so the editor opened only when the gate's limit ran out: 60 s with the input lock, 300 s with the pause
  only (the players' log: `busy=paused` for 50 s, then they gave up on a black screen). Fix `core.editor_own_pause`:
  the busy bits the game had when it submitted the request (kPaused while `GetPauseCount(0x4BF38A6)` > 0,
  kInputLock) are not waited for; a cinematic or a later pause still are.
* ✔ **BUG-0585** (observed, lab, twice): after the City Hall editor the player tribe's food mat pointer +0x260 is 0.
  `Simulator::cTribe::UpdateFoodVisuals` 0xC94CE0 (thiscall(tribe, float change) ret 4) reads `[+0x260]+0x34` and
  calls its vt+0x2C without a check: a guest's food change relayed to the host then (shared.cpp `s_food`) faulted
  at UpdateFoodVisuals+0x28 (data 0x34) inside the message pump and the host's game froze in the vehicle editor
  (the log line "shared food ... from player 1" comes after the call, so it never printed). Alone the change to Civ
  ran without a fault (lab, guest left). Fix `tribe.food_fx_mat` (game::tribe_food_visuals): no visual without the
  food mat; the food itself still changes.
* ~ (BUG-0588) Seen in the same runs, not analysed: at the vote for the vehicle editor the host logs "evolved in place: Bob keeps
  its world (host species 00000000 -> 00000000 ...)" (the Creature evolve-in-place path runs for a Tribe editor; no
  harm seen), and a sync-check MISMATCH right after the City Hall: the host's own NPCs read at (0, 0, 0) for 2 checks.

## The game's pause reasons (2026-11-04, fix/w43-transitions)
<a id="tribe-game-time-pauses"></a>

* ✔ `cGameTimeManager` (0xB3D480 Get): +0x48 flags (bit 0 = paused), +0x4C a map reason -> index (EASTL rbtree,
  anchor +0x50; node: right +0, left +4, parent +8, key +0x10, index +0x14), +0x68 an array of 8-byte rows whose
  first int is the count. Pause 0xB32330 / Resume 0xB32360 (thiscall(tm, reason) ret 4) add / remove one;
  `GetPauseCount` 0xB321E0 (thiscall(tm, reason) ret 4) reads one. `coop.pauses` lists every reason counted now
  (runtime, lane 4/5: the stage card 0x4BF38A4, a stage entry / hints 0x4BF38A7, the editor 0x4BF38A6).
* ✔ Reasons seen: **0x4BF38A6** an editor is coming (above); **0x4BF38A7** a UI modal / tutorial hint / the stage
  entry (90 call sites); **0x4BF38A4** the stage card (History screen) of the end of Tribe.
* ✔ **BUG-0313** (observed, lane 5, GOG 2024, 2 runs): a guest's game after its world load in Tribe stands on a
  black screen with the single pause reason **0x4BF38A7** for 7.6 / 8.7 s, the host none. It is the Tribe stage's
  own entry: the mode-change handler 0xCDEE00 (vtable 0x1477578) pauses 0x4BF38A7 on entering Tribe and the entry
  state machine 0xCDE7E0 (cTribeGame +0x22C: 2 planet / flora, 3 0xCD78F0 stage UI, 4 planet model ready, 5 the
  simulator, 6 the renderer's layer 0x16 off until `IBakeManager` has nothing queued (vt+0x28 < 1) and the model
  world nothing pending (vt+0x10 < 1), 7 Resume(0x4BF38A7)) resumes only when every creature model is baked. A
  guest that just received the host's world bakes the host's creations then (the host's own game had them baked).
  Inferred: GOG 2024 bakes slower on the lanes than GOG 2016 (the 2016 guest was running when start() returned).
  Not a mod bug: the guest's mirrors are kept on the ground meanwhile by `tribe.mirror_ground` (BUG-0444).
## The creature attack's spots round a village (2026-11-05, fix/w43-crashes)
<a id="tribe-attack-spot"></a>

BUG-0582. GOG 2024, Ghidra and lab lane 2 (`tools/tests/coop_tribe_attack_spot.py`).

* ✔ (Ghidra) The creature attack (Drama Manager kind 7) runs its animals through a behaviour whose start 0xD73930
  stores the attacked place's position (+0xB0) and its planet cell id (+0xC0, `SporeCoop::SpotRecord_CellId` 0xC9B140
  with make = 0) and whose update 0xD73FD0 walks them through states; in state 3 it looks the cell's spot record up
  (`SporeCoop::SpotRecord_Find` 0xC945C0: a global sorted fixed vector 0x157ABB0, 0x60-byte entries, null when the
  cell has none) and takes the nearest free spot with `SporeCoop::TribeAttack_TakeSpot` 0xD72720, which reads the
  record's spots (+0x40 / +0x44, 0x1C bytes: used flag, position) **without a check** (null + 0x44 at 0xD72745). Its
  sibling 0xD72690 (a spot given back) checks for null; a false answer of 0xD72720 is the game's own "no free spot"
  branch (0xDA61D0 + the behaviour's vt 0x44: the attack ends).
* ✔ Records are made for every cTribe (0xC9B8D0 over the noun list, 0xC9B860 from a tribe's creation 0xCD4F10, make
  = 1); the table is emptied by 0xC9A210 on the stage modes' enter / exit (0xCDEE00, 0xCFA770, 0xFFA170).
* ✔ (lab) The host's own creature attack (`coop.drama start 7`) after a tribe formed during play (`coop.tribe form`,
  the controller's own forming): the attackers reached state 3 with no record within ~55 s in 3 of 3 runs (431 and
  357 caught faults with the guard off, lab 20261105-000150, 20261104-235754; 2 attackers ended by the guard, lab
  20261104-235955); without the formed tribe none in 1 run. ~ Inferred: the formed tribe's cell has no record (a
  game bug that co-op tests hit because they form tribes; the NPC controller forms them in long sessions too).
* Fix `tribe.attack_spot` (gamefixes.cpp): with no record 0xD72720 answers "no free spot" (out = -1, false).
## A tribe leader's abilities and a tool (2026-11-05, fix/w43-crashes)
<a id="tribe-leader-tool"></a>

BUG-0583. GOG 2024, Ghidra and the lab's fault record.

* ✔ (Ghidra) A citizen's abilities: count vt 0xB0 = `SporeCoop::Citizen_AbilityCount` 0xC24DB0: its species' (+0xB20
  profile, vector +0x6D4 / +0x6D8) + 2 for a tool (+0x102C) of type 1..6, 1 for 9 or 0xB, + 6 when it leads its
  tribe (`SporeCoop::Citizen_IsLeader` 0xC23430: owner +0x1010 city, else +0x1014 tribe, owner +0x11C == it). The
  getter vt 0xB4 = 0xC24E40 serves n (the species' count) entries, then index n = GetTribeToolData(tool) +0x24, then
  for a leader n+1..n+6 the table 0x169DE30 +0x1C.., for others n+1 = the table's +0x34..+0x48 by tool 1..6; null
  past that. **A leader with a tool of type 1..6 counts n + 8 and the getter serves n + 7**: the last is null and
  `GetAbilityIndexByType` 0xC0C1E0 (from every citizen's Update through 0xC1CF30 when +0xB54 is set) reads null + 8.
* ✔ (lab 20261104-212803, fix/w43-realign) the guest's fault: EBX = 21 (count), ESI = 20 (the last index), EAX = 0,
  **ECX = 1**: 0xC23430 returns with ECX = its result, only the leader branch keeps it (the others leave 0): a leader
  holding a tool of type 1..6.
* The game never makes that state: `SetSpecializedTool(0)` on a leader gives 0xB (the staff) and the leader takes
  no rack's tool. The mirrors' tool field (tribe.tool_hold, BUG-0514) copies the host's member's tool to the guest's
  mirror, which can be its own tribe's leader there (? how the leaders came to differ after the rejoin: not seen
  again; in 3 lab runs the guest's leaders were a subset of the host's, lab 20261105-002001, -002140, -002402).
* Fix `tribe.leader_tool` (tribe.cpp apply_held_tool): no tool of type 1..6 on this game's leader.
* ✔ (lab 20261105-002402, fix off) a leader mirror set to 1 by the field's apply did not fault before the host's next
  field gave it 11 again (seconds): the state must last for the fault (+0xB54 updates).

## A model's attached effects and the guard for a broken one (2026-11-05, fix/w44-crashes)
<a id="tribe-model-fx-guard"></a>

BUG-0575. GOG 2024 (= the Steam 3010a413 code), Ghidra and asm. Extends #tribe-model-effects-crash.

* CORRECTION (2026-11-05, fix/w44-crashes): the crash report's names (cSpatialObject::SetModelKey <- QuaternionToMatrix
  <- cModelObject <- cPlanetaryArtifact::SetLocomotion) are the nearest .sym names of stale return addresses found by
  the stack scan, not the calls: nothing set a model key or a locomotion. The real chain (the Steam exe at 0x4E0000,
  GOG 2024 = Steam - 0xE0000): VisualEffect_Start 0xA82660 (fault 0xA826F3) <- SpatialEffects_UpdateByDistance
  0xC8ABF0 (return 0xC8ACC4) <- CreatureView_Update 0xE8C100 (return 0xE8C96E) <- the view list walk 0xB3CB6F
  (return 0xB3CB71, a linked list of views, vt 4(a, b) each). 0xC69BC6, 0xE8E5D3 / 0xE8EA45, 0xE8CD3B are stale.
* ✔ The creature view (vtable 0x1486328, 0x1C bytes, made by 0xE8BCF0 from an object cast to 0xCE9F6639): +0xC the
  creature (a counted reference), +0x10 its AnimatedCreature (creature +0xB54, counted with 0xA02B70 / 0xA051B0),
  +0x14 a flag. Its update (vt 1, `SporeCoop::CreatureView_Update`) returns at once when creature +0x135 is clear or
  +0xB54 is null; else camera distance (creature +0x128), LOD (0xC0C460), the creature's pool-2 effects by key
  (0xC20300 / 0xC14FD0: 0x73E4CDF, 0x77492D8, 0x7E2FEDF, 0x7A16140, 0x6452CA4), then when +0x134 is set
  SpatialEffects_SetTransform 0xC89EF0 and always SpatialEffects_UpdateByDistance 0xC8ABF0 on the creature's spatial
  (creature +0xC0): its attached-effect vector is at creature +0x180 / +0x184.
* ✔ Attached effects (`SporeCoop::SpatialEffects_Add` 0xC8AFB0 thiscall(owner, id, group, key, bool restart) ret
  0x10): entry 0x3C bytes, +0 key, +4 effect id, +8 enabled, +9 restart (the bool), +0xA a second restart flag,
  +0xC threshold (256 m when made), +0x14 1.0, +0x38 the effect (a counted reference). The range check stops (vt
  0xC(0)) a running effect at or past the threshold and starts (vt 8(0)) a stopped one inside it when +8 and +9 or
  +0xA are set; a stopped entry with neither is erased with its Release. Removal by key 0xC8AEC0. Views add theirs
  through the wrappers 0xC8B1D0 / 0xC8B1F0 / 0xC8B210 / 0xC8B2C0 / 0xC8B340 (cDefaultBeamProjectileView 0xCC2EF0,
  0xE87xxx, 0xE96xxx).
* ✔ VisualEffect (`SporeCoop::VisualEffect_vftable` 0x1457DE8, the only vtable with Start 0xA82660 in slot 2): Start
  calls vt 8(arg) of each component (+0x144..+0x148, 0x5C bytes, object +0x38) without a check. The crash's
  registers (edi = 0x5C, esi = 1, ebp = 0, ebx = the vector's end) mean the first component's call went into a
  function through a vtable that is not one (a freed component whose memory was reused) and came back with
  esi / edi destroyed.
* ? Not found: what freed the component while the effect stayed in a creature's attached list. Not the relay of
  effects.cpp: it keeps and releases only the world effects it made itself (pool 5, g_live), the creature pools
  +0xCC8 / +0xD68 are other lists, and its references balance (read 2026-11-05). Not reproduced (two runs of
  fix/w43-*, see the ticket).
* Guard `game.model_fx` (gamefixes.cpp): hooks on 0xC8ABF0 and 0xC89EF0 check every listed effect first (its vtable
  in the game's image with slots 2, 3, 4, 6 in its code; for a VisualEffect the component vector and each component
  object's vtable and slots 2, 3); a broken one leaves its entry (+0x38 = 0, no Release, the reference is lost) and the
  game makes it again when it wants it. Arithmetic checks only (no VirtualQuery): the loops run for every view every
  frame. `coop.fxlist [creature|all]` lists attached effects; `coop.fxcut` makes the crash state (tests).

## Animals over the ground in Tribe (2026-11-05, fix/w44-height)
<a id="tribe-animal-height"></a>

* ✔ The host's Tribe game puts an animal lifted 4 m over its ground (teleported there at every mod frame, +0x137 = 0,
  `coop.npc lift`) back onto its terrain in its own update: the host's npctrack rows say 0.00 m in every frame (lab
  20261105-112236, 112346). So the game itself keeps Tribe animals on the ground (inferred: the animal's AI walk
  grounds it; a guest's proxy, which has no AI, stayed at 4 m when placed there). The height rule's air side
  (docs/re/creature.md#creature-height-rule) has no Tribe case found; its ground side was already in place
  (own_height, out_of_ground).
* ✔ BUG-0402's fourth kind (campaign_ct, lab 20261105-015350 in the rel/0.7.7 worktree) is a terrain difference, not a
  placement: the host's NPC n146 stood at agl 0.00 on the host's terrain (its own `sync-check: n146 here:` line), the
  guest's terrain at that point is 8.39 m higher (`want_agl=-8.39`), and the proxy stood on the guest's ground
  (`agl=0.00`): what the rule asks. The sync-check compares absolute positions, so the terrain difference showed as
  `n apart 8.4` (BUG-0329's kind, larger than the 5 m seen there).
## Music, member animations and the village pace on a guest (2026-11-05, fix/w44-tribe-guest)
<a id="tribe-guest-a1"></a>

A1 of 0.7.8 (BUG-0568, BUG-0569, BUG-0571). GOG 2024, lab lane 9 / 10.

**Audio (BUG-0568).**
* ✔ (Ghidra) Audio::AudioSystem (singleton 0x166D9F4; vtables 0x1452CE8 base / 0x1453FC8 derived, slot 0 Init 0xA25200) is
  a command builder: vt 0x38 begin(cmd) `SporeCoop::AudioSystem_BeginCommand` 0xA20A80 ret 4, vt 0x3C float parameter
  0xA20A90 ret 8, vt 0x40 int parameter `_SetInt` 0xA20AB0 ret 8, vt 0x58 submit `_Submit` 0xA20B10; all write the buffer at
  +0x214. Commands: 0x3475365 play (0x3475381 sound, 0x3475385 track), 0x347536B stop (track, 0x34753A0), 0x3475376 set
  property, 0x3CDD1A9 (key 0x34753A7: a state hash), 0x3991984 the listener (every frame). vt 0x20 / 0x24 hand out tracks
  (two pools: 1.. and 1001..).
* ✔ `coop.audio [mark | list [n]]` (mods/coop/audio.cpp) records them with the first 4 return addresses on the stack:
  the lab games are muted, the commands are still submitted.
* ✔ (runtime + Ghidra) the stage's sounds come in groups: 0xEA9DA0 is a message listener that activates / deactivates
  groups (0xEA9A10 / 0xEA9250; 0x22D1ADC = the mode changed: the group of the new mode by a map). The Tribe mode's group
  (0x34D61238, 0x931E43A7, 0x014B6A97, 0xE02B7355, 0x7B470F0F, 0x55AA10F0, 0x03AF053F, 0x9A3C482A, 0x4421F8E8, 0xD6C4FC3B)
  plays on the host and the guest alike after a load from the galaxy menu and after a guest's own editor visit. The
  editor's group (0x02A8D194, 0xF8EFEA3C, 0x38EB66F8 x12) stays playing after the red X on both (the game's own).
  The galaxy menu: 0xF390C020 / 0xCCF3D9C6 / 0xCD785B24 and the music 0xBD7F5BC9; after the menu intro GGE_Intro ends
  0x54AB9C8A / 0x896ED613 (on a lab host they play into Tribe when world.cpp ends the intro there).
* ✔ (Ghidra + runtime) **the Tribe stage's theme music manager** TribeStage (= [0x169B2C8], `cTribeUI_Get`) **+0x1C8**
  (made by the stage UI setup 0xCD78F0, stopped at the stage's exit 0xCD5FC0 by 0xBC2330): 23 slots {track, squared
  distance}. `SporeCoop::TribeTheme_Play` 0xBC23B0 (sound, pos) -> slot, `_Move` 0xBC2540, `_Stop` 0xBC22F0. Callers are
  behaviours of citizens only: 0xD69740 (hunt / fight: **0xAD1A17D**, while its member is the player tribe's and
  0xC238B0), 0xDB1060, 0xDB7120, 0xDBAAF0, 0xDBB7D0. A 40 s fight: host 7-11 plays, guest 0 (mirrors have no tree).
  Fix `tribe.theme_music` relays them (BUG-0568).
* ? The hit and weapon sounds of a fight (0xA7E3E1FB, 0xD05B58ED, 0xD967193C, 0xCE5F2ED0; callers 0xA3749B / 0xA83B18) and
  the creature voices 0xC881120E / 0x529FEC69 are host-only too: BUG-0600, not analysed.

**Member animations (BUG-0569).**
* ✔ (runtime) The host's members play 24 kinds of behaviour animations in 45 s of a fight and the villages' own work
  (0x591B056, 0x2C39003, 0x67378DD, 0x2C39001, 0x62FF867, ...), all started by their trees through
  SporeCoop::CreaturePlayAnim (game.cpp's listener); a guest's mirror plays none of them (0 of 646 samples): only its
  locomotion's idle / walk. Fix `tribe.mirror_anims`: the npc.anims relay (BUG-0464) for the members (MSG_NPC_ANIM kinds
  2 / 3, member ids), 94 % of the samples the same on the guest. `coop.tribe anims` lists `cur <id> anim= bhv=`.

**The NPC tribe controller's pace (BUG-0571).**
* ✔ (Ghidra) `cNpcTribeController` update 0xCC6A90 (vt 0x10, thiscall(ctrl, u64 dt ms) ret 8): returns while a cinematic
  plays or the game is paused; a 1 s timer 0x157E940 (FormTribe sets it to 1); then by the tribe nouns: **1**: FormTribe
  after 25 s (properties 0x3D346A77 / 0x1A09D79E, or a tutorial flag); **2**: when TribeStage +0x40 (mGoalProgressHistory)
  is not empty, or the player tribe's vt 0xA4 (food) > property 0x4C87E711 (default 100, `SporeCoop::App_GetPropInt` 0x4E1CA0
  on `SporeCoop::TribeProps_ptr` 0x1581288); **3, 4**: every second; **5**: with 3 goals done; more: never. FormTribe
  0xCC6440 then needs tribes x 2 <= its property list's length (the list +0xFC is picked by the player tribe's diet and
  the difficulty, 0xCC6160) and a spawn marker.
* ~ the controller's vt 0xA4 on the player tribe behaves as the food (food 15: no tribe in 5 min, 135: 3 tribes in 2 s):
  GetTotalFood as in the cTribe table above (that table's addresses are GOG 2016; 0xC8E850 on GOG 2024 is another
  function, `add ecx,0x510; jmp 0xBC3260`). The GOG 2024 address of the slot was not read.
* ✔ (runtime) food 135 with 2 tribes: villages 3, 4, 5 within 2 s, alone (no session) and in a session alike; the guest
  makes the host's within 2-4 s, its own controller forms none (tribe.guest_tribes). Fix `tribe.form_pace`: in a session
  the food path waits for the threshold x the players (BUG-0571).

## Villages formed during play: other political ids on a guest (2026-11-05, fix/w44-world-state2)
<a id="tribe-new-village-ids"></a>

BUG-0591. Lab lane 6 (tribe fixture, coop_wsync_tribe.py villages).

* ✔ (observed, lab 20261105-123323 / 123225) Three `coop.tribe form` on the host made its NPC tribe controller form
  4 villages (1 -> 5 NPC tribes); the guest made each of them (tribe.npc_tribes, SpawnNpcTribe at the host's centre)
  with its own next political ids: host 000021CE / CF / D0 / D1 -> guest 000021D0 / D1 / D2 / D3 at the same places.
  The host's ids 000021D0 and 000021D1 were other villages on the guest. The save's village keeps its id (000021CB on
  both). So political ids of villages formed during play are not shared; positions are (0.0 m).
* ✔ wsync's match() took the same political id first: the host's relation and food of village 000021D0 went to the
  guest's 000021D0 (another village), the two right villages got none (relation 0, food 0): the 0.7.7 final
  check's `Tribe:state:relation` / `tribe_food` rows from the host's forming at 418 s on (play_tribe, lab
  20261105-015320). Fixed by `wsync.tribe_pos`: the closest village within 40 m first, the id only without one
  (also the goal card's village).

## The end of Tribe on a guest: the taken-down village and animals (2026-11-05, fix/w44-world-state2)
<a id="tribe-stage-end"></a>

BUG-0588, BUG-0594. Lab lanes 6 / 7 (tribe fixture, coop_tribe_civ.py).

* ✔ (observed, the 0.7.7 final tribe_civ 20261105-015320 and lab 20261105-123325) Back from the City Hall editor
  the host's game has no totem pole (none in its noun list) and no food mat of the player tribe (+0x260 = 0,
  BUG-0585); a guest's game keeps both at the village (404 8 305) until the Civ world comes. ✔ (observed, lab
  20261104-224338) The host's animals then read at (0, 0, 0) (46 NPC rows "here" at (0 0 0) with their health)
  while the guests' proxies stay: the sync-check's "n: 46 apart" MISMATCH 18 s after the City Hall.
* ✔ `cTribeUI +0x28` / `+0x29` (what the listener 0xCD1D60 sets when it submits the City Hall / vehicle editor, see
  [tribe-civ-transition](#tribe-civ-transition)) read 0 on the object cTribeUI_Get 0xCD4230 returns, back in Tribe
  after the City Hall (lab 20261105-123325): ~ the listener's object is another one, or the flags are cleared. The
  mod's sign of the phase is the player tribe without its food mat (`tribe::stage_end`).
* Decision: a guest runs no such phase (its game never opens these editors); neither player sees the other's
  world then (the host is in the cinematic and the editors), and the Civ world replaces both. So it is not
  re-aligned: the sync-check compares nothing while the host's game ends the stage (`diag.stage_end`), the
  detector skips the sample as `apart` (`st phase stage_end=1` of coop.wstate).
* ✔ The vote / notice path after these editors took them for an evolution ("Alice evolved the species: everybody
  gets the new body", the Creature evolve-in-place path): the notice now says a design for the Civilization stage
  was made. The in-place path itself (no world sent between the two editors) stays: the Civ stage sends the world.

## The fight's effects, sprints and voices on a guest's mirrors (2026-11-05, fix/w44-guest-player3)
<a id="tribe-mirror-fx"></a>

P2b of 0.7.8 (BUG-0600). GOG 2024, lab lanes 4 / 5, `coop.audio` and `coop.effects log on` (the log lines now carry
the first four return addresses into the game's code: the creator of every effect).
* ✔ (runtime, out/lab/20261105-134329) In a 30 s fight of the host's members the host's game played, besides the
  theme (#tribe-guest-a1), sounds from an effect's sound component (FUN_00a37430, return address 0xA3749B) and the
  creature voices; the guest's game none of them. Matched by the intervals between the plays and the effect creations
  of the same log:
  * 0xD967193C = the sound of pool-1 effect **0xE55DC391 key 0x35052B7 (the sprint)**: `cCreatureBase::Update`
    0xC21530 calls StartOrStopEffectFromPool1(bit 0x20 of +0xFA4, 0xE55DC391, 0x35052B7) every frame when the
    creature's +0xC0 vt 0x58 is true (the player tribe's members: the enemy's had none). The bit is the sprint:
    `SporeCoop::Creature_SetSprint` 0xC1AE70, called by ~30 behaviour nodes (run to the target).
  * 0xC881120E (into a fight) / 0x529FEC69 (out of it) = `SporeCoop::Creature_CombatVoice` 0xC1B030 (+0xBB1 the
    in-combat byte), called by the fight node 0xD69540 and the TakeDamage of animals and citizens.
    CORRECTION (2026-11-10, plan/w47-cellcreature, BUG-0837): 0xC1B030 is CreatureSetSneaking (docs/re/creature.md#creature-c1b030-two-names); +0xBB1 is the sneak byte, force = 1 forces the sneak.
    CORRECTION (2026-11-12, fix/w48-small, BUG-0837): decided: the name is `SporeCoop::CreatureSetSneaking` (the only one in the .sym now; mirror_fx.cpp hooks it by that name); "combat voice", "into a fight" and "in-combat byte" above are superseded: the two sounds are the sneak's start and end, +0xBB1 is the sneak flag (docs/re/creature.md#creature-c1b030-decided).
  * 0xCE5F2ED0 = effect 0xA231DA2F of 0xCCBE90 (~ the tribe UI's order marker at the target: the effect id is picked by
    the order kind and the camera distance band): the ordering player's own feedback, not a member's.
  * other pool effects of the members (no sound in these runs): 0x8ADA6617 (pool 1 on the NPC chief, from 0xC2103D),
    0xF8122731 / 0x759B0E13 (one-shots at bone 20 / 22, cCreatureBase::CreateEffect from 0xC25419 / 0xCCAA2F: the
    tools), 0x21C934A7 / 0x85A02492 (pool 2 key 0x55559EF from the behaviour 0xDAB4A0).
* ✔ The animation-event effects (0x9FBF00: 0x2D4ADB36 footsteps, 0xE6D770B8..BD) play on the guest's mirrors too
  (the relayed animations, BUG-0569); 0xDCAE75D5 (the foot step's second effect, made by 0x9A6910 only while the foot
  event's +0x2D5 is set) plays on the host only: not followed further.
* ? The A1 run (out/lab/20261105-115203) also had 0xA7E3E1FB (18) and 0xD05B58ED (17) through the same sound
  component path; none of the three runs of this branch had them, their effect is not known.
* ✔ Fix `mirror.fx` (mods/coop/mirror_fx.cpp, MSG_MIRROR_FX 64): the host sends, for a tribe member or an npc.cpp NPC,
  the pool effect starts / stops and one-shots the game's pool hooks report (effects.cpp passes every creature but the
  own avatar and the puppets on), and the changes of CombatVoice and SetSprint; a guest makes the same call on its
  mirror (start_effect / create_creature_effect with the bone check / stop_effect, the original CombatVoice with
  force, the original SetSprint: its Update then starts the sprint effect itself). Lab (coop_tribe_fight_sounds.py,
  the order marker and the per-target impacts left out): the guest's game 9 of the host's 9 such sounds, the voices
  4 of 4 (out/lab/20261105-135946, 2 runs); off 4 of 15 (only the animation effects' C656EAF8) and 0 of 4
  (out/lab/20261105-140001).
* ~ NPCs of the Creature stage go through the same path (npc::id_of / npc::proxy); not tested there.

## The flora grid's handle slots, and what BUG-0359's float was not (2026-11-05, fix/w45-crashes)
<a id="tribe-flora-grid-slots"></a>

Ghidra, GOG 2024 (BUG-0359, continuing [the flora grid free](#tribe-flora-grid-free)).
* ✔ HierGrid handle slot (16 bytes at `*(grid+0x60)[h >> 7] + (h & 0x7F) * 0x10`, HierGrid_Unlink
  0x702B80): +0 the free-list word (bit 31 set = free, low 30 bits the next free handle; HierGrid_Remove
  0x702E30 writes it), +4 the level (index into the 0x44-byte levels at grid+0x24), +8 the cell key
  (-1 = the overflow list), +0xC a pointer to the handle's record R: `*(R+0) + 4` is the 16-byte list
  node that Unlink unhooks (node+0 next, node+4 prev) and frees with GeneralAllocator::delete_
  (0xF47220), R+8 the user value Unlink returns. So a walker over the live slots (bit 31 clear) can
  reach every list node of the grid; its block count was not found (not needed for the analysis).
* ✔ 0.51f (0x3F028F5C, the value over the node's chunk header in BUG-0359) is a literal of the exe
  only once, .rdata 0x13FE148, read by FUN_00629E10 (0x629F2D) alone; not followed (the float may as
  well be a computed or data value).
* Negative (lab history, 2026-11-05): the signature (FreeInternal+0x64 < 0x702C44 < 0x702E88 <
  0xB0818E) is in no other game log of out/lab of the main checkout and every worktree (searched every
  `*.game.log` with GeneralAllocator_FreeInternal: 24 files, the others are BUG-0308 / 0348 (Cell
  pools), Tribe / Civ effect creation, the editor's play-mode pictures and BUG-0605).
## The hit impacts of the members' attacks on a guest (2026-11-05, fix/w45-tribe)
<a id="tribe-hit-impacts"></a>

BUG-0603. GOG 2024, Ghidra + runtime lane 9 (coop_tribe_hit_impacts.py, `coop.effects log on`: `fxanim:` lines with
the effect's position, `fxhit:` lines).
* ✔ (Ghidra) `SporeCoop::AnimEvent_Handle` 0x9A6D70 (cdecl, 21 arguments) has one caller, 0x9A7720, which resolves
  the event's object specs (0x9A5560, up to three lists) and calls the handler once per resolved object: param 2 the
  animating instance, param 3 the resolved object's instance (+0x18 its position, +0x2E4 its bones, 700 bytes each).
  A spec that resolves to nothing makes no call (no effect).
* CORRECTION (2026-11-05, fix/w45-tribe): the BUG-0603 inference "the per-target loop over param_18" (#tribe-mirror-fx)
  is wrong: AnimEffect_SetTarget 0x9FBC10, the loop's only effect call, ran 0 times in a 40 s fight with 58 impact
  sounds (lab 20261105-230956). The impacts come through the param_18 == 0 path (AnimEffect_Create from 0x9A72E3).
* ✔ (runtime, lab 20261105-231448) In a 40 s fight 127 of the host's 477 animation-event effects were made for
  another object than the animating one: 0x682E8D4B (25, sound 0xD05B58ED), 0x594BDFF9 (24), 0x7B5A2E62 (24),
  0xF0D5A994 (18), 0xCFECC623 (18), 0x1EA65782, 0x46C9F053, 0xE19059E2 (5 each), 0x6532F5C6 (3), at the victim's
  hit spot (1-3 m from the victim, the attacker next to it). The guest's game: 0 of 454 (every one on the animating
  mirror: footsteps, the swing 0xE6D770B8..BF). So these specs resolve to the attack's target, which the guest's
  mirror (its animation relayed by tribe.mirror_anims) does not have. ? How the game hands the attacker's animation
  its target (not followed: the relay below does not need it).
* Fix `effects.hit_targets`: game.cpp hooks AnimEvent_Handle (notes the two instances, thread local) and reports an
  effect of AnimEffect_Create made inside it for another object; effects.cpp sends it as a world effect (pool 5, id
  and position) to the players in the same place. Lab: the guest 52 of the host's 56 impact sounds (out/lab/
  20261105-231808), off 0 of 53 (out/lab/20261105-231635).
* Test pitfall: the fight of coop_tribe_fight_sounds.py's setup (attack an NPC tribe's member in its village) had
  0-3 impacts in 40 s in 5 of 7 runs (the members still walking there); the new test puts the victim 9 m from them.

## The gather's walk after an interruption (2026-11-05, fix/w45-tribe)
<a id="tribe-gather-walk"></a>

BUG-0531 (not reproduced, no fix). GOG 2024, lab lane 9, 'tribe' fixture.
* ✔ (runtime) A gather order (`coop.tribe order <id> 2 <tree>`) to a fruit tree 60 m off: node 0x0569236E state 1,
  walk mode 1, the walk's goal the tree itself with an arrival radius of 5.1-6.2 m (`coop.tribe ai`: r=); the
  node memory's +8 (the tree) read 0 then.
* ✔ A teleport of the walking member (game::teleport through `coop.tribe put`) does not end the walk: put 2.5 m short
  of the goal (inside r) the gather went on at once (states 5, 2 at the tree: lab 20261105-232113); put 7.6 m short
  (outside r) it walked on and reached the tree in 10 s (lab 20261105-232555). So a stall of state 1 needs the game's
  own walk to stop with the member outside the goal's radius, which a test cannot make by moving the member.
* ~ The play_tribe stall (goal 2.8 m off, standing) therefore had a goal with a radius under 2.8 m: not the tree goal
  of this path (inferred).

## The ground of a village made live, compared on both games (2026-11-05, fix/w45-tribe)
<a id="tribe-village-ground"></a>

BUG-0332 (not reproduced). GOG 2024, lab lane 9, coop_tribe_village_ground.py (the players' way from the `creature`
fixture to Tribe, as coop_tribe_snapshot.py).
* ✔ (runtime, out/lab/20261105-232737) After the live Creature -> Tribe change the game's own ground
  (`coop.ground`, Simulator::cPlanetModel::GetHeightAt) at the new village (-175.3 -465.8 -85.4) and on rings of 10, 25,
  50 and 100 m round it is the same on the host and the guest within 0.20 m (0.11 m at 100 m), twice 20 s apart. So
  on this planet the village the host's game makes live and the one the guest loads from its save stand on the same
  ground.
* ? The 3.9-5.3 m of the campaign (#tribe-ground-height) were on planets the campaign made itself (Cell -> Creature
  -> Tribe); not measured with this probe there yet.
* ✔ (runtime, 2026-11-06) In 4-player Cell -> Creature -> Tribe campaigns (campaign_ct) the guests' ground under the
  host's members was higher in 2 of 4 runs: 5.1 m (out/lab/20261105-233903) and 5.9 m (out/lab/20261106-000530),
  the same on all three guests; 0 in the two others. So the planets the campaign makes show it, the fixtures do not.
  `CAMPAIGN_GROUND=1` makes the campaign sample `coop.ground` on every machine at a member / player and 15-200 m off
  (noted above 0.5 m); `CAMPAIGN_SEED=<n>` replays a seed through the lab (the planet pick still differs).
* ? Leads: the strings `mTerrainStampsToRemove` (0x14655A8, registered in 0x1322A30) and `mAppliedStamp`
  (0x1469174): terrain stamps that placed objects apply and a list of stamps to remove, saved with the planet; not
  followed (BUG-0332 stays open).

## Members and animals shown where the host has them now (2026-11-06, fix/w45-remote-bodies)
<a id="tribe-remote-lead"></a>

The guest's mirrors of the player tribe's members (tribe.mirror_time, 0.2 s delay) and the animals' proxies (0.15 s)
were 0.17-0.32 s behind the host's bodies, p90 2.04 m (members) and 1.17 m (animals) while they walk; with
`drive.lead` 0.64 m and 0.37 m. Details: docs/re/creature.md#creature-remote-lead (BUG-0410, BUG-0425).

## The arrival's chief placing teleported walking chiefs (2026-11-06, fix/w45-remote-bodies-2)
<a id="tribe-place-chiefs-walking"></a>

BUG-0610. CORRECTION (2026-11-06, fix/w45-remote-bodies-2): the ticket said "the host's own game put its member 21 m
away"; it was the mod's own `place_chiefs` (tribe.cpp, `tribe.place_chiefs`, BUG-0340), not the game.
* ✔ (observed) Every one-frame jump of a walking member on the host in M1's 13 body_trail_tribe tracks (9-22 m, always
  t2, the guest's chief) fell within ~1 s of the host's log line "tribe: Bob's chief stood next to another one at the
  arrival: put on its own spot round the village"; one line and one jump per run. The guest's copy followed 0.25 s
  later (the next state). No game-side teleport of a walking citizen was found (41 indirect vt+0x44 calls in
  0xC00000-0xDB0000 do not resolve to the creature Teleport 0xC0C040).
* ✔ `place_chiefs` looked at every chief for 60 s after an arrival (every 2 s) and teleported one closer to another chief
  than their radii + 1 m, with no speed check (spread_chiefs has one): a chief walking past the host's chief on its
  player's order, or one that walked there and stopped (13.8 m, lab 20261106-041542 with the first fix that only left
  walking chiefs alone), was put on its ring spot.
* Fix `tribe.place_still`: a chief is placed only while it stands AND has not moved more than 1.5 m since the first
  look of this arrival's window (a chief stacked at the arrival stands where it came in). The log line carries the
  game's time (`t <H->time_seconds()>`, the clock of `coop.bodyrec`).
## Food bundles of the host on every game (2026-11-06, fix/w45-world-objects)
<a id="tribe-host-bundles"></a>

GOG 2024 addresses (dc04aee5). Kind `game_bundle` of mods/coop/worldobj.cpp (BUG-0548), switch `wobj.bundle`.

* ✔ cGameBundle (noun 0x18C431C): +0x128 its container's interface, +0x12C type, +0x134 amount (float). A container
  (cGameBundleContainer interface): vector +4..+8, max count +0x1C, vt +0x3C one bundle's size.
* ✔ cBundleManager::Get 0xB3D3B0 (cdecl, [0x167EADC]); +0x1C the one ground container (a
  cGameBundleGroundContainer, noun 0x1906183, its interface at +0x34). CreateBundles 0xAC79F0 thiscall(mgr, float
  amount, container, int type) ret 0xC -> the bundle; RemoveBundles 0xAC7C50 thiscall(mgr, amount, 0, bundle) ret 0xC
  takes that bundle alone.
* ✔ A carried bundle sits in the creature's own container at creature +0x34 (max 1): `coop.wobj bundle carry` gave
  holder = member + 0x34 (lab 2026-11-06). A tribe's mat holds the whole food in one bundle (no per-bundle cap).
* ✔ The holder of a mat's bundle is no creature: reading noun_id at holder - 0x34 faulted on a null vtable (the host,
  at its first snapshot, lab 20261106-042445); the carrier is now looked up in the game's creature list.
* ✔ A member given food walks to the mat at ~7 m/s and puts it down within ~8 s; the guest's copy of the member carries
  the copy 1-3.5 m behind it (coop.wobj list on both every 0.7 s), and the copy is removed when the host's is.
* ✔ host_bundle: the host's ground food lies at its place on the guest, the guest's copy of the member carries the
  food, the food a guest's member takes from the ground is gone on the host (PASS lab 20261106-044500, FAIL with
  `SPORE_FIX_OFF=wobj.bundle` lab 20261106-044559).

## The host's extra ornament in Tribe: not reproduced (2026-11-06, fix/w45-world-objects)
<a id="tribe-ornament-not-reproduced"></a>

BUG-0528. GOG 2024 (dc04aee5).

* ✔ The `tribe` fixture has 5 ornaments of noun 0x18C88E4 in both games, the same models (+0x228: 0x356EB8A x3,
  0x5B9A06E, 0x5D97F762 the tribe's decoration of 0xC90080) at the same places; none changed in 80 s after the
  guest's members were ordered to hunt (`coop.tribe act_npc 10`, lane 7). 0xCCEB90 releases a held object of model
  0x356EB8A (the creature's +0xC0): these can be held.
* ✔ No Tribe `ornament` detector row in play_tribe (lab 20261106-035845, 20261106-051335).
* ? The makers of the noun (CreateInstance(0x18C88E4) call sites): 0xC90080, 0xCCEB90's caller 0xCD1D60, 0xD130D0,
  0xD2FFD0, 0xD99F50, 0xC60440, 0xB41FE0, 0xBE6A50 (city decorations, docs/re/space.md#space-city-ornaments),
  0xEC0F00, 0xF30B60, 0xF41A50, 0x1051860, 0xB25FC0. `coop.wobj raw 18C88E4 3000 576` prints the model of each.
## The terrain stamps of the Tribe start, live and loaded (2026-11-06, fix/w45-village-ground)
<a id="tribe-terrain-stamps"></a>

BUG-0332 (and BUG-0608). GOG 2024; runtime on lane 4/5 (creature_cadian fixture, the campaign's own planet), statics
in Ghidra / objdump. ✔ = observed, ~ = read in the code but not seen running, ? = inferred.
* ✔ The planet terrain (cPlanetModel+0x24, vtable 0x1490BA8) keeps lists of stamps: +0x770 (+0x784 when the stamp prop's
  bool 0xC5AE63ED is set; a second family at +0x798), 0xAC-byte entries: +0x00 the Transform (u16 flags, u16, position
  +4..+0xC = the stamp's level as a distance from the centre, scale +0x10, matrix +0x14), +0x38..+0x97 brush data,
  +0x98 the stamp prop's key (instance, type 0x00B1B104, group at +0xA0), +0xA4 flags, +0xA8 the id. The save holds the
  lists (prop lists 0x43B29E1/2 etc., vt 0x84 0xF9B790 -> vt 0x80 0xFAA570), no height map; a load rebuilds the heights.
* ✔ `ApplyStamp` 0xFA6C10 (thiscall(terrain, Transform*, cPropList*) ret 8 -> id) is called at the live Creature ->
  Tribe change ~56 times within 0.5 s: from 0x0109DFBF and 0x01091981 (the player's village), 0x010DDDEF (one per tribe
  spawn marker, 4-6), 0x0106AD74 (~50, all of one prop list, key {E960A03F, 00B1B104, 0}); later from 0x0109DFBF for
  every NPC village made (the controller's forming, the guests' copies: SpawnNpcTribe). They cut / fill plateaus:
  e.g. 530 m -> 508.2 m over 60 m. The host's heights are final 0.9 s into the stage (`coop.terrain`, `--watch`).
* ✔ The guest loads the host's Tribe save: its +0x770 list is the host's, entry for entry and byte for byte; its
  heights still differ round some of the stamps by 0.4-14 m (patches near the spawn markers and NPC villages; e.g.
  Creature 533.19 m, Tribe host 518.59, guest 532.49). Lab 20261106-033400 (3.08 m), 20261106-034853, 20261106-040512
  (13.9 m), 20261106-042556 (13.48 m).
* ✔ **The host's own load of its Tribe save gives exactly the guests' ground** (lab 20261106-043244, 12 of 12 points:
  host live 508.06 / after its load 508.83 / guest 508.83, ...). So the game itself renders the stamps it applies live
  differently from the full rebuild of the same list that every load makes; in co-op the host keeps its live ground
  and every guest has the loaded one. That is BUG-0332 (5.1-5.9 m in the campaigns, where the members walked onto
  such a patch: a new NPC village, "made it ... at 5.1 m").
* ✔ Not the causes: the stamp lists (equal), the terrain manager's fresh-load flag [[0x16C8EB0]+0x1CD] (0 on the
  guest), the height parameters (equal), RemoveStamp + ApplyStamp of all 169 entries on the guest and bumping the
  version +0x814 (no rebuild starts: nothing changes), a late share save (the host's terrain is final before it).
* ~ The rebuild: `0xF9B860` thiscall(sphere, a1, a2, bool a3, u8 a4, float a5) ret 0x14 picks a generator (+0xB8 while
  0x1CD is set: no stamp steps; +0x30 with a3; else +0x74), whose step functions 0xFC1250 / 0xFC1480 load (0xFA9B00),
  rebuild (0xFA60E0, 0xFA3BC0) and render (0xFA8670, 0xFA5E90) the stamps, then +0x818 = +0x814. The sphere update vt
  0x9C 0xFAAD30 counts sphere+0xA48 down and at 0 calls vt 0x80 and 0xF9B860; the planet editor's reload 0xF37390 calls
  vt 0x114 (0xFA9CD0, thiscall(sphere, cPropList*) ret 4: the lists read back) and then 0xF9B860(sphere, 0, 0, 0, 1, f).
  ApplyStamp itself writes no height.
* ✔ A rebuild started by the mod does not give the loaded ground either: sphere+0xA48 = 1 after the burst (lab
  20261106-043944: 387 of 1499 points off, up to 4.25 m) or vt 0x80 + vt 0x114 + 0xF9B860 as 0xF37390 (lab
  20261106-045023: 445 points, up to 20.8 m). ? The generator seems to render the stamps onto the heights it has, so
  every rebuild moves the ground again; only a load starts from the planet's base.
* ✔ Fix `tribe.host_reload` (world.cpp): after a live Creature -> Tribe change the host loads its own share save once
  it has gone out to every guest (the game's own load through the galaxy menu, `load_slot`, as the guests do), so every
  game has the ground any later load of that world gives. `coop.terrain [list | raw <id> | state | rebuild | dirty |
  regen | reapply <from>]` (terrain_diag.cpp, log-only hooks on ApplyStamp / RemoveStamp / ClearAllStamps) are the
  probes.
* ? The same split may exist in Civ (cities stamp live through cCityData / cCity, 0xB99DFA ... 0xBECF7D): not
  measured, the fix is limited to the Tribe stage.
* BUG-0608 (a relayed world effect at the sender's absolute position) came only from the two grounds differing; with
  one ground on every game the absolute position is on the receiver's ground too.

## A member mirror's lead target without jumps (2026-11-06, fix/w45-remote-bodies-2)
<a id="tribe-lead-smooth"></a>

BUG-0619, `coop.fix drive.lead_smooth` (net/interp.h SnapshotBuffer::smooth and the latency rule, tribe.cpp
mirror_target / mirror_drive, driver.h WalkPlace::lead_speed). Measured on tribe_walk_mobile (both games at 30 fps,
netem mobile: 120 ms one-way, +-60 ms jitter, 3 % loss) from the per-frame track JSON (out/laneN/track/tribe_walk_*.json:
drawn position `d`, aim `tgt`, aim speed `rv`).

* ✔ observed: with drive.lead (docs/re/tribe.md#tribe-remote-lead) every "jump" of a member mirror (0.88-0.97 m in
  a 32 ms frame, over the test's 3 x 9 m/s x dt) comes 1-3 frames after the lead target itself jumped 2.7-4.8 m in one
  frame at the start of a walk (target steps: 0.0, then 2.7-4.8 m, then 0.29 m = 9 m/s), 12 of 12 decomposed jumps
  (lab 20261106-130123, -064239): the first walking state arrives a latency + jitter late and the lead moves it on by
  its velocity x (its age + the one-way latency), up to 0.45 s x 9 m/s. The target is steady in the jump frames; the
  jump is catch_prop's catch-up (from where the game's walk had put the body) on top of the walk.
* ✔ observed: what the player sees in a frame is the placing's step from where the game's walk had the body plus
  the game's walk of the NEXT frame. A bound on the placing from the last placing (M2's v1; here (speed + 3) x dt)
  still drew 0.9-1.3 m frames when the walk's speed rose that frame (20261106-145657, -145700). Held instead to
  (2 x lead speed + 3 - walk speed) x dt from the walked position: no jump in 24 of 24 views (20261106-151828,
  -151831).
* ✔ observed: a mirror walked at its target's closing speed (18-21 m/s) does not reach it (fell 0.5-3 m behind and
  closed in ~1 m frames, 20261106-141403, -142519).
* ✔ observed: with the jumps gone a smoothed lead still fails the stop-and-go check (1-5 m/s frames whose drawn step
  is off the target's by more than half + 1 m/s: 11-22 %, limit 10 %) in about half the runs over the mobile network,
  where 0.7.8 passes at the same time (20261106-154457 0 of 3 vs SPORE_FIX_OFF=drive.lead -154500 3 of 3, lanes
  swapped -155349 / -155352 the same): the lead moves each state on by 0.2-0.3 s there, so every change of speed between
  two states moves the target (speeding up: the target runs v + a x L), and whatever the closing (exponential 0.1-0.15
  s, at most 1-1.25 x the speed + 3 m/s, eased 60 m/s2, the target's velocity blended over 0.1 s, closing only while
  over 5 m/s, interpolated while under 5 m/s) the mirror lags and surges in those frames.
* ✔ observed: the arrival lateness of the states does not tell a mobile link from a local one at the lab's load (6-15
  on 6 cores): median 51-62 ms mobile vs 39-51 ms local, IQR 45-103 vs 34-106 ms (the guest's own frame times dominate
  it; 20261106-164638, -165647, -170617). The one-way latency (the session's half round trip, median of 64 states) does:
  mobile 123-149 ms, the player network and a local link under 100 ms (20261106-171543, -171546).
* ✔ the fix, Tribe member mirrors only: (1) over a one-way latency over 100 ms (until under 85 ms) no lead:
  interpolated and driven as 0.7.8 (smooth, ~0.2 s + latency late); (2) under it M1's lead as merged (raw, catch_prop);
  (3) a switch between the two goes through a decaying offset (closes at d / 0.12 s, at most the speed + 3 m/s; the
  mirror walks at the target's velocity eased up at 40 m/s2, at most 3 m/s over the lead; the placing's step held to
  (2 x lead speed + 3 - walk speed) x dt). Smoothing every lead step instead (no jumps) trailed more over the player
  network (body_trail_tribe_player members p90 1.47-1.59 m and 1.1-1.8 s over 2 m, M1 1.14-1.27 m; 20261106-172428 /
  -172431, -184547 / -184550). Final runs: tribe_walk_mobile 4 of 4, body_trail_tribe members p90 0.56-0.59 m,
  body_trail_tribe_player 1.05-1.37 m, tribe_walk 2 of 2 (20261106-185808, -185811).
* ~ over the player network (45 ms, spikes of +250 ms) M1's lead keeps its start steps when a walk starts in a spike
  (3.6-3.8 m, then 0.87-0.95 m frames: tribe_walk_player 20261106-185808); smoothing them (over 1.5 m) made that test
  stop-and-go and body_trail_tribe_player trail (-191334, -191337): not solved here. Solved since by bringing a late
  state in over time (BUG-0650): docs/re/tribe.md#tribe-lead-late.
* ~ NPC proxies (npc.cpp, the other SnapshotBuffer::lead user) keep M1's lead and catch_prop (smooth_max 0): no test
  showed jumps there; not measured over the mobile network.
* ~ under the lab load of the day (6-20 on 6 cores) body_trail_* fail for NPC animals too, which no part of this
  touches (p90 1.0-1.8 m against M1's quiet 0.37 m): judge those only A/B under one load.
## The galaxy menu intro in a stage leaves the host's view stuck (2026-11-06, fix/w45-int3)
<a id="tribe-intro-camera"></a>

* ✔ (lab logs, 72 play_tribe host logs of all worktrees) play_tribe failed "host: the minimap takes the view where it
  points" (o == a == b in the calibration, busy=0, game time running; then "host: gave orders through real clicks
  (0)") in 3 of the 12 runs whose host log has `world: cinematic 8915F9BB still playing in a stage: ended it` before
  `the game saved box is up`, and in none of the 48 runs with the other order (fix/w43-transitions 20261104-225940,
  fix/w45-village-ground 20261106-050921, rel/0.7.9 20261106-105201; the stuck pick was the same point
  (0.0, -471.29, 185.84) in all three). Not caused by 0.7.9: the first sighting is from 2026-11-04.
* ✔ The intro is the galaxy menu's "GGE_intro" (docs/re/space.md 7.9): it starts ~16 s after the menu came up even
  when the lab's quick load left the menu after 4-5 s, so it starts in the Tribe stage, here during the share's save
  for the joining guest and its "game saved" box.
* ✔ Ending it in the same PlayCinematic call (a first fix) was not enough: lab 20261106-124414 (the intro started
  0.05 s before the box was seen, ended at once) had the host's view stuck at the first calibration. ~ Inferred: the
  box, open while the intro owns the camera, gives that camera back on its OK.
* Fix `world.intro_at_once` (BUG-0626): a hook on `cCinematicManager::PlayCinematic` (0xAE08B0) does not start
  "GGE_intro" outside the galaxy menu mode (its only caller, GalaxyGameEntry_IntroMessages 0xDEA870, ignores the
  result; its state stays 5, "intro playing", until the menu comes up again). Lab 20261106-124712: the cold start's
  own intro under the box was "not started", the view followed the minimap. `coop.status`: "menu intro ended N (not
  started M)". Test tribe_intro_camera (starts the intro by hand under a held box).
* Related: BUG-0607 (a Creature guest's view from space after the same intro ended in the stage). Confirmed and covered by
  this fix: docs/re/creature.md#creature-intro-camera (the intro starts in the Loading mode after a menu load).
## A dead member's body: out of the member list, still in the world (2026-11-06, fix/w45-last)
<a id="tribe-member-body"></a>

BUG-0620. GOG 2024, lab lane 5 (play_tribe + `coop_tribe_member_place.py --attach`, tribe_dead_player).
* ✔ (runtime) A **player** tribe's member that dies leaves its tribe's member list (cTribe +0x340): its +0x1014
  (mpOwnerTribe) reads 0, it is in no tribe's list, and the game keeps the body (dead flag +0xB5E = 1, adult, its last
  order record still there, `coop.tribe who`) for 10-24 s more. An NPC tribe's member killed the same way stays in its
  tribe, knocked out (BUG-0488's tribe_dead_member).
* ✔ The mod's member set (host_tick) comes from the member lists only: the body left the snapshot at once and the
  guest removed its mirror, so the detector saw a lone host citizen (`Tribe:unmatched:citizen`). Fixed by
  `tribe.member_body`: such a body is sent with its last tribe and health 0 while the game keeps it (60 s at most).
* ✔ BT node **0x05218BB3** = the creature fight node (node table .data 0x1583F40: enter 0xD69540, tick 0xD69740, exit
  0xD673B0): play_tribe's "no movement" orders of members in it (a gather order to the leader, a social order) are
  members in a fight that the game keeps fighting; not the gather stall of BUG-0531.
* ~ `coop.tribe ai`'s order target check (tgt_listed) reads 0 for trees: a fruit tree (cTreeData, noun 0x6ABEE7E) is
  no object of the game's object list.

## The gather walk round an obstacle; play_tribe's idle orders (2026-11-06, fix/w45-last)
<a id="tribe-gather-block"></a>

BUG-0531. GOG 2024, lane 5, coop_tribe_gather_block.py and play_tribe (`coop.tribe ai`).
* ✔ The gather node 0x0569236E's states as `coop.tribe ai` shows them on one gather (a guest's member put 6 m behind a
  hut, the tree 21 m beyond it): 1 walking to the tree (walk mode 1, goal = the tree, r 6.7 m), 2 the last metres
  (walk mode 3, a goal by the tree, r 0.1), 4 and 6 at the tree (walk mode 0), 2 again, 19 carrying home (walk mode 1,
  goal the village, r 6.6; `carry=` read 0.0 there). The way round the hut was found by the game.
* ✔ play_tribe's "no movement" orders (2 runs) were members in the fight node 0x05218BB3 (#tribe-member-body) or
  already carrying food; none in 0x0569236E. The state 1 stall of 2026-11-05 was not seen again.

## Carried food on a guest's screen: unbound copies (2026-11-06, fix/w45-last)
<a id="tribe-carried-bundles"></a>

BUG-0617 (after BUG-0548). GOG 2024, lane 5, coop_world_host.py --case bundle_carry, play_tribe with the detector.
* ✔ A guest's copy of a carried bundle (wobj game_bundle, class bit 0x100) sits in the container of the guest's copy of
  the carrier (creature +0x34) and moves with it; with the carrier walking it was 9.5-21.5 m from the host's bundle at
  the next snapshot (the copy of the member lags, and the made copy starts at the host's place). Bound copies stay
  bound by id however far; an unbound one stayed the guest's own for good (bundle_own) and kept being carried after
  the host's member put its food down. Fix `wobj.bundle_carried` (bind within 8 m, no own carried copies).
* ✔ The guest's tribe mirrors run no behaviour tree (`coop.tribe` ai_skipped = every tick), so a guest's game makes no
  carried food of its own while the host plays.
* ✔ The food mats' bundles (census own=held, `game_bundle_held`) are the tribes' food (tribe +0x20C), each game's own
  container; compared through the state's food values, not as objects.
* ✔ (2026-11-06, fix/w45-last, addition to #tribe-member-body) On the guest the body's mirror is taken out of its
  tribe's member (+0x340) and selectable (+0x354, citizen +0xC0) vectors and its +0x1014 cleared, as the host's game
  does: left in, play_tribe saw one member more on the guest and a guest's order to the body was rejected on the host
  (lab 20261106-143257). The detached mirror is destroyed with DestroyInstance when the host's body goes.

## A late state brought in over time: the member lead at a walk start in a latency spike (2026-11-06, fix/w45-lead-player)
<a id="tribe-lead-late"></a>

BUG-0650, `coop.fix drive.lead_late` (net/interp.h SnapshotBuffer::late_time / lead_at; Tribe member mirrors only,
smooth_max > 0, over a link under drive.lead_smooth's 100 ms one-way rule). Measured on tribe_walk_player (both games at
30 fps, SPORE_NET=player: 45 ms, +-15 ms, +250 ms for 0.7 s every 7 s) from the per-frame track JSON.

* ✔ observed (M3, docs/re/tribe.md#tribe-lead-smooth): a walk that starts in a spike is known 0.3-0.4 s late; M1's lead
  shows the newest state moved on to the sender's now, so the target stepped 3.6-3.8 m in one frame from the stand and
  catch_prop drew 0.87-0.95 m frames (the test's limit 3 x 9 m/s x dt = 0.86 m at 32 ms).
* ✔ observed: bounding catch_prop's drawn step instead (`drive.lead_draw`, tried, reverted: walk + placing <= 2.5 x the
  speed + 1 m/s) left a short walk known late 3 m behind its target when it stopped; the stand driving (drive::step) then
  closed it in a 1.08 m frame as the next walk started (lab 20261106-200630 #2, member 2 at 10.47 s). Closing a late start
  at any speed the body can be drawn at needs the time the walk lasts: the target itself has to start where the body is.
* ✔ observed (net_tests test_lead_late): when a spike ends, the states sent in it arrive after newer ones and are dropped
  as old (netem orders by send time + delay), so the first walking state that is kept can be 2.7 m from the stand. A
  shown time that steps back only to the newest state's own time still jumped 2.7 m there; back to the state before it
  (the path between the two interpolated) 0.45-0.55 m per 1/30 s frame (M1 3.3-3.6 m).
* ✔ the fix: the lead target has its own sender time. It advances at the real time's rate; when a new state arrives it
  may step back (never forward) to as early as the state before the newest: of that span (17 candidates) the time whose
  target is nearest where the shown target was going (a state as expected: the lead's own time, nothing changes; the
  first state of a start known late: the path from the last standing state). Then it catches up with the lead at 1.5x
  the real time but at most 3 m/s over the body's speed, and the velocity the mirror walks at is scaled by the same rate
  meanwhile (walked at the state's speed the mirror fell behind a faster target and catch_prop surged after it). At
  1.5x without the 3 m/s cap an 11 m/s member's target ran 16.5 m/s and walk + catch_prop drew 0.94-0.96 m frames
  (lab 20261106-212040 #2, member 7 at 34.8 s). Catching up takes lateness x speed / 3 m/s (0.35 s late at 9 m/s: ~1 s;
  ~0.1 s late on a calm link: 0.3 s).
* ✔ observed (guest logs): under a lab load over ~10 on 6 cores the one-way latency drive.lead_smooth reads is 110-150
  ms on a local link too and the member lead goes off (BUG-0651); body_trail_tribe(_player) then fail for members on
  the old and the new code alike (p90 2.4-3.2 m, lag p50 0.4-0.7 s; 20261106-210300, -210310).
* ~ the remaining target steps over 1.5 m in tribe_walk_player are at stops (a stop state known late: the lead had gone
  on past it) and at members the host's own game put somewhere (rv 0): not walk starts; no drawn jump in those runs.
* ~ under lab loads of 9-19 on 6 cores tribe_walk_player fails now and then for 0.7.8 too (windows 63-71 %, uneven
  11 %): judge it only A/B at the same time on the other lane pair.

## Bodies and the sync-check after a join (2026-11-07, fix/w46-tribe-missing)
<a id="tribe-sync-bodies"></a>

BUG-0664. GOG 2024, lab lanes 4-5 (tribe_dead_rejoin), long_tribe out/lab/20261107-004042 of rel/0.7.9.
* ✔ (logs) long_tribe on rel/0.7.9 failed the sync-check gate twice, 12 s after the guest's restart and 6 s after its
  rejoin had loaded the host's world: "t: 1 missing on the guest (t10)" / "(t8)"; on the host, too, `t10 here: not
  listed` (in no tribe's member list, `coop.tribe`), and the host's `bodies_sent` counter grew across both joins
  (1778 -> 1903 -> 1979 around s 1850-1900, 1979 -> 2055 -> 2212 around s 2300-2330, 0 in between): the key was a body
  sent by tribe.member_body (#tribe-member-body).
* ✔ (code) the host's `sync_entries` listed every live object of `g_hm`, bodies included; a guest spawns no mirror for
  a body entry (only lets a mirror it already had lie there), so a guest that joined or reloaded the world while a body
  lay on the host had none. 0.7.8 dropped the body from `g_hm` at once: no such key, no mismatch (long_tribe on 0.7.8
  and 0.7.7: 0 MISMATCH lines).
* ✔ tribe.sync_bodies: both sides leave bodies (host: `body_since != 0`, guest: the entry's body bit) out of the
  sync-check; the check compares members. `coop.fix tribe.sync_bodies off`: the host lists them again.
* ? a guest that joins while a body lies on the host does not show that body (at most the 10-20 s the game keeps it);
  not checked whether the shared save holds it.

## A body's mirror destroyed without its tribe: the eat node's exit (2026-11-07, fix/w46-tribe-fault)
<a id="tribe-body-teardown"></a>

BUG-0667. GOG 2024, disassembly + lab lane 4 (`coop_tribe_dead_member.py --eat`, suite tribe_body_teardown).
* ✔ (disassembly) `cTribe_ReleaseSlot` 0xC988E0, thiscall(tribe, layout type, slot) ret 8: calls
  cTribe_ToolLayout(tribe+0x394, &type) **before** it checks the slot, so it faults with tribe = null whatever the slot
  (0xAC9C7D `mov edi,[ecx+8]`, ecx 0x394, data 0x39C). The claim's twin of 0xC98860.
* ✔ (disassembly) BT action table entry at .data 0x1593B20: node **0x0461300F** (eat) enter 0xDB0B10, tick 0xDABD80,
  exit **0xDB0C40**. The exit reads the tribe by 0xC230B0 (`mov eax,[ecx+0x1014]; ret`) and releases spot
  `memory+0x10` of layout type **0xD** (the food mat's spots) with 0xC988E0. (The table in section 4 lists tick 0xDABDE0
  for eat: not checked which one is right; the entry read here has 0xDABD80.)
* ✔ (disassembly) A citizen's teardown 0xC1FFE0 (DestroyInstance 0xB22560 -> ... -> 0xC1CA40 cleanup) clears its
  container (+0x34), calls vt 0xBC and 0xC0D500 on it (the behaviour tree's shutdown: the active nodes' exits run), then
  frees the tree (+0xB4C). Neither 0xC1FFE0 nor the destructor 0xC1BD30 reads +0x1014 itself.
* ✔ (disassembly) cTribe::RemoveMember (vt 0x9C, GOG 2024 0xC96070) destroys the member (0xB22560 at 0xC96150) with
  its +0x1014 still set: the game's own path never tears a citizen down without its tribe.
* ✔ (runtime, lab 20261107-031255) A guest's mirror runs its tree only while the host is away (local_play, or
  `coop.tribe localbt`); an eat order then puts it in 0x0461300F within 1 s and the node stays active when the tree is
  skipped again. The host's member dies (tribe.member_body), the guest detaches the mirror (+0x1014 = 0, #tribe-member-body),
  and when the host's body goes the guest's DestroyInstance runs the eat exit: the fault of long_tribe
  (20261107-021641: s 1013, after the editor visit at 14.9 min, so the mirror's eat came from the guest's own play then).
* ✔ Fix `tribe.body_teardown`: the detached body gets its tribe (kept by detach_body) back right before it is
  destroyed when the tribe is still one of the world's; at session end the bodies left in the world get it back too
  (the game tears them down later); a cTribe_ReleaseSlot with this = null is skipped (detour, warned 8 times).
* ✔ (runtime) Why one caught fault "froze" the guest: SporeKit's per-frame work (d3d9hook.cpp on_frame_begin_present ->
  SK_EV_FRAME -> coop's tick -> tribe on_message -> remove_member) runs under `g_in_frame`. The game's own handler
  caught the fault and unwound past SporeKit's frames, so `g_in_frame` stayed true: every later Present and game loop pass
  returned at once (no mod tick, no frame counter). The game itself went on (the watchdog's stack 30 s later: the main
  thread in App::Canvas::Tick's message pump), but the lab saw no frames and the host saw the guest's state age grow.
  Fixed in SporeKit (`kit.fix frameunwind`): an entry on the frame's thread at or above the cut-off frame's stack
  address clears the flags (a nested call is always deeper) and logs "the frame N was cut off by a fault".
* ? Any other mod state a cut-off frame leaves half done (a loop over g_gm stopped in the middle) is not reset; the
  next snapshot cycle rebuilds the tribe's.
* ✔ (disassembly) citizen+0x1014 is an intrusive pointer: SetTribe 0xC23040 AddRefs the new tribe (vt 0) and Releases
  the old (vt 4); the citizen teardown vt 0xC4 = 0xC27CD0 runs 0xC1FFE0 first and only then releases and zeroes +0x1010
  and +0x1014. detach_body's raw clear keeps the reference (the raw restore before the destroy is balanced).
* ✔ (runtime, long_tribe_short 20261107-032137, with the slot guard) when the guest left the world for the menu one
  citizen was torn down with no tribe: the eat exit's release was skipped, then the exit **0xDA7FB0** of node
  **0x0551759F** (enter 0xDAE230, tick 0xDA7F20; reads tribe+0x26C) faulted. Not one of the detached bodies (the one of
  that run had been destroyed with its tribe). ? which citizen: a member whose tribe the guest's own game cleared
  (SetTribe(null), e.g. a death in its local play during the editor visit) is the inferred candidate.
* ✔ (runtime, lab 20261107-035000, negative) a mirror in the eat node torn down by the guest's Exit to Main Menu does not
  fault with the fix off: the game's world teardown destroys members with their tribe set.
* ✔ Hardening (tribe.body_teardown, guest): SetTribe(null) is recorded (citizen -> old tribe); the citizen teardown
  0xC27CD0 with +0x1014 null puts back the detached body's tribe or the recorded one (AddRef first) when it is still a
  world's tribe, else it empties the tree's stack (ai+0x1DC = 0: no exit runs) and logs it (8 times).
* CORRECTION (2026-11-07, fix/w46-tribe-fault): the teardown fallback above set the depth (ai+0x1DC) to 0, which does
  not stop the exits: long_tribe_short on rel/0.7.9 (20261107-043030) logged it for 3 citizens and still faulted in
  0xDA7FB0. 0xBC8850 skips the exits only when the stack's first word ai+0x1D0 is -1; the fallback now fills the
  0x418-byte stack with 0xFF as 0xBC8850 leaves it. 0xC0D500 (SporeCoop::Citizen_BTTerminate, cdecl(creature)) is the
  tree's end; ~ superseded: "0xC0D500(citizen) (a timer on +0xB4C)" in Citizen_IdleWalk's .sym line.
* ✔ (runtime, diagnostics lab 20261107-052358) the citizens left without a tribe at long_tribe_short's menu exit are
  detached bodies (bodies of members killed in the session's play, still lying when the guest leaves): `detached 1,
  left tribe 0, tribes listed 0`. The world's teardown destroys the tribes (their members through RemoveMember with the
  tribe set) before the objects outside the lists, so a body's tribe is no world's tribe any more by then.
  No SetTribe(null) of the guest's game was seen in that run.
* ✔ Source fix (tribe.body_teardown): detach_body ends the body's tree with Citizen_BTTerminate while +0x1014 is still
  set (the exits release their spots as the host's dying member's did), and the BT tick stays skipped for detached
  bodies (also while the host is away). Repro `coop_tribe_dead_member.py --eat --menu` (suite tribe_body_menu): the
  eating mirror's member dies on the host, the guest leaves to the main menu while the body lies: with the fix off the
  fault at 0xAC9C7D and the two space-gfx faults of the following world load (lab 20261107-053541), as in
  long_tribe_short; with it no fault and no fallback line (20261107-053655).
* ✔ A/B: long_tribe_short on v0.7.8 passed (rel/0.7.8 worktree, lab 20261107-052252): new in 0.7.9 (detach_body).
* ? (runtime, long_tribe_short x3 lab 20261107-053815, all PASS, 0 faults) at the later menu exit (after the guest's
  restart and reload, s ~600) 1-2 citizens were still torn down with no tribe in node 0x04861814 (depth 2) that were
  neither detached nor taken out by a SetTribe(null) of this game: the fallback ended their trees without exits. Inferred
  origin: bodies that came with the received world without a tribe (the only other writer of +0x1014 is the reader
  0xC03370 that copies +0x1010 / +0x1014 from a record); not checked. Whether 0x04861814's exit reads the tribe: not checked.

## The player tribe's level and population cap on a guest (2026-11-07, fix/w46-tribe-civ)
<a id="tribe-level"></a>

BUG-0645. GOG 2024, Ghidra and lab lanes 4/5.
* ✔ (asm) `SporeCoop::cTribe_MaxPopulation` 0xC8EC70 (thiscall, plain ret): an NPC archetype 1..31 (+0x550) reads a table
  0x1695460; the player tribe (spatial +0x120 vt 0x58 true) returns `0xC00C10(tribe +0x38)`, a table 0x168D228 of 8 ints
  (runtime: **6, 9, 12, 15, 30, 45, 60**, 0), 0 at level 8 or more. cTribe::SpawnMember 0xC983C0 refuses a member at this cap.
* ✔ (Ghidra) +0x38 is the player tribe's **level**; its only setter is 0xFF0260 (`mov [ecx+0x38], arg`). The hut upgrade
  `SporeCoop::TribeHut_Upgrade` 0xCDA4F0 computes it from the goal progress (cPlayer +0x10F0) against the int list property
  0x16D19C8A of TribeProps_ptr, sets it, plays the cinematic "TRG_Upgrade" with the members near the hut and writes the new
  cap into the Tribe HUD (`SporeCoop::TribeHud_ptr` 0x169B41C, +0x10: ~ the "members / cap" panel).
* ✔ (runtime) A guest gets the host's goal progress (wsync.tribe_goal) but its game never runs the upgrade: the host at level
  2 (cap 12) with 10 members, the guest at level 0, cap 6, HUD cap 6 (lab 20261107-075004). This is the player's "12/6".
  Fix `wsync.tribe_level`: a wsync row with the host's player tribe level; the guest stores it (the game's own one store) and
  the HUD cap of that level. ? The hut's model on the guest (the upgrade's look) was not compared.
* ~ A new member the guest makes in its own game is removed by the mirror reconcile and never reaches the host (BUG-0671).

## The roster portraits are the species' picture (2026-11-07, fix/w46-tribe-civ)
<a id="tribe-portraits"></a>

BUG-0643. Lab lane 5.
* ✔ (runtime) The Tribe roster's member portraits (right side of the screen) are the png {0x2F7D0004, 0x40626200, species
  instance} the game finds for the species: a broken png registered above every package before the guest's first load
  (`coop.creations fakepng 067B3E4A`) gave the failed-image square (white with a red cross) in every member slot. The game
  keeps a picture it drew for the whole process: registering the broken png after the first load changed nothing.
* ✔ (code) The host's own creations went to the guests with their bem, metadata and creaturedata only (records_from_game
  took no png): after the host's Creature -> Tribe editor (a new creation of the host's My Creations) the guest's game had no
  picture for the tribe's species: white squares. Fix `creations.png`: a creation's records include its pngs (model group and
  thumbnail group +1). Test tribe_portraits: the guest's game answers the png of the host's species (lab 20261107-081042 FAIL,
  20261107-081214 PASS).
* (2026-11-11: the players' case was not this one, see #tribe-portraits-session-species, BUG-0921.)
* ? In the lab the roster kept the fixture species' portraits after the members' species and the tribe's first species key
  (+0x1920, the inline first element of mSpeciesKeys +0x1904, runtime) were set to the host's creation before the world share:
  which key the roster reads was not found, so the test checks the resource, not the screen.

## A village's identity colour (2026-11-07, fix/w46-tribe-civ)
<a id="tribe-id-colour"></a>

BUG-0644. Ghidra and lab lane 4.
* ✔ (Ghidra) serializer 0x1320F20 (class key 0x053DBCF2): `mIDColorID` at +4 and `mCachedColor` (RGB) at +0x24 of an identity
  colour block. The colour list is a global list at 0x156B0BC of {id 0x053DBCxx at +0x10, RGB at +0x14}; 0xB6E4D0 finds the id
  nearest an RGB (default 0x053DBCF3).
* ✔ (runtime) cTribe **+0x230** is that block: **+0x234 mIDColorID**, +0x254 mCachedColor (0 for NPC tribes until drawn; the
  player tribe's is set). Five tribes of a host game: 0x053DBCF1 (player), F4, F5, F6, F7 in forming order; the guest's copies
  had the same ids in this run because it made them in the same order. A tribe takes the next colour, so a guest that made the
  host's villages in another order, or had one of its own, had other colours (the player report).
* Fix `wsync.tribe_color`: a wsync row of every village's colour id by place; the guest stores the id and clears the cached
  RGB. Test tribe_colors (lab 20261107-081057 FAIL, 20261107-080948 PASS). ? Whether the village's look changes at once
  (the hut and flags are drawn from the cached colour) was not checked on the screen.

## The dance at the fire and the guest's sounds (2026-11-07, fix/w46-tribe-civ)
<a id="tribe-dance-music"></a>

BUG-0646, BUG-0669. Lab lane 4, `coop.audio`.
* ✔ (runtime) 3 min of the tribe fixture with a 30 s day (SetDayDuration on the host), 9 host members at their village work
  and at the fire (anims 0x2C39001 / 0x2C39003 on both games): no theme music play on the host at all (tribe.theme_music sent
  0), so the BUG-0568 relay had nothing to send. The host-only sounds were footsteps: 0x4A776923 436 vs 36 (FUN_009A6BD0, a
  sound per animation channel while the creature's velocity +0x1660 is above 0.1: BUG-0669).
* ? The dance with music the host saw at night (report s 5218, a conquer goal open, an epic coming) was not seen in the lab;
  its trigger and its sound path are not known (not the theme manager in these 3 min, not an animation of the members: the
  guest's mirrors played the same animations).
## A tool made in the old world's tribe right after a load (2026-11-07, fix/w46-tribe-load)
<a id="tribe-stale-map-tool"></a>

BUG-0672. GOG 2024, disassembly + lab lane 9 (`coop_tribe_world_map.py`, suite tribe_world_map).
* ✔ (disassembly) the fault 0xCE6AE7 is in 0xCE6AE0, called by **Species_FootprintScale 0xCE6BC0** (thiscall(tuning
  .data 0x1581208, cSpeciesProfile*) ret 4): it reads the species' size +0x554 / +0x558 / +0x55C with no null check. The
  label "cPlanetRecord_GetPlanetID+0x17" is only the function before it (0xCE6AD0).
* ✔ (disassembly) the return 0xC98A71 is in **cTribe_LayoutToolSpots 0xC98940** (thiscall(tribe, tool type) ret 4),
  not in cTribe_ReleaseSlot 0xC988E0 ("+0x191" was the nearest symbol): for a type other than 0xA / 0xC / 0xD it calls
  GetTribeToolData(type) and Species_FootprintScale(tribe +0x18CC, the tribe's species). So with eax = 0 the tribe's
  species was null. The tribe's own callers pass 0xC / 0xD only; type 6 comes from **cTribeTool_SetPosition 0xC9DB80**
  (spatial vtable slot .rdata 0x1474778) and 0xC9D700 (slot 0x14747A4), which re-lay the tool's tribe (+0x1C4) for the
  tool's type (+0x1C8). Not the BUG-0667 detour and not Citizen_BTTerminate.
* ✔ (code) the guest's wobj tool kind (BUG-0523) makes the host's tool in `tribe_of_index(i)`, the mapping host tribe
  index -> this game's tribe, which only guest_state (each tribe state) rebuilt. on_world_change did not clear it, so
  between a load and the new world's first tribe state it named the old world's tribes, freed by the load (their memory
  still readable, the species reference already released: +0x18CC = 0). make_tool's distance check (150 m from the
  tribe's place) passes on a warm reset: the same save, the same places. CreateTool and SetTribe on the freed tribe ran,
  the tool's SetPosition faulted.
* ✔ (runtime, lab 20261107-094944) forced (`coop.tribe statehold 1`: the guest ignores tribe states; leave, join, load;
  `coop.tribe frametool 1 6`): the same fault, esi = the old tribe 1, edi = 6, the same calls 00CE6BCA 00C98A71.
  After the load the mapping's old player tribe was no longer the player tribe (no "(p)").
* ✔ (runtime, 20261107-095148, kit.outpump on) the tool stays in the world with the freed tribe: the game faults on it
  in every pass (1221 times in 40 s, Present no longer reached SporeKit, frames from the game loop).
* ✔ Fix `tribe.world_map`: on_world_change clears the mapping; tribe_of_index uses an entry only while it is one of the
  world's tribes (all_tribes), else logs it (8 times) and returns null.
* ✔ (runtime + disassembly) **why kit.fix frameunwind did not restart the frames**: the fault is inside SporeKit's frame
  (coop's tick from SK_EV_FRAME, the frame was cut off), but a task for outside the frame was queued: its WM_SK_PUMP_OUT
  reached h_WndProc -> pump_outside_tasks, which saw in_frame() (the flag still set) and posted the message again at
  once. App::Canvas::Tick's message loop (0x848362 .. 0x848404: PeekMessage, Translate, DispatchMessage while a message
  is there) never ran dry, so h_CanvasTick never got to frame_from_game_loop, where frameunwind clears the flag (the
  watchdog stack: Canvas::Tick+0x106 < DispatchMessage < h_WndProc < pump_outside_tasks < PostMessage). BUG-0667's
  runs had no outside task pending at the fault. Fix `kit.fix outpump` (SporeKit): inside a frame the task waits; the
  frame's end, Present's end or the cut-off posts the message again. Off: the old repost (since the first co-op UI).

## The fire pit party and its music (2026-11-07, fix/w46-tribe-dance)
<a id="tribe-party-music"></a>

BUG-0646. GOG 2024, Ghidra and lab lane 4. CORRECTION (2026-11-07, fix/w46-tribe-dance) of
docs/re/tribe.md#tribe-dance-music: the dance music is neither the theme manager nor an animation sound; it is the party
behaviour's own procedural sound, and a party order does start it in the lab (it needs no night or goal).
* ✔ (Ghidra) A party at the own fire pit (CitizenAction 12 Party, a click on the fire pit "Firepit - Throw Party $5", the
  tool name 0xC9D220 of type 10) is the game behaviour **cTribeGameBehaviorParty** (factory 0xD791C0, 0x110 bytes, ctor
  0xD79120, vtables 0x147BEB0 / 0x147BE9C, type id 0xF09B40E0). Tick `SporeCoop::TribeParty_Tick` 0xD793F0 (vt 0x18): lays the
  members out round the fire pit (cCommunityLayout::SetParameters), walks each to its slot (per-member state 1 walking, 3
  there), then dances them (states 4 / 5: anims of the table 0x158441C 0x2C39904, 0x2C39801, 0x2C39812, 0x2C39812,
  0x2C39813, 0x2C39830, 0x2C39521; 7 done -> vt 0x44 leaves). When every member stands at its slot: +0x94 = 2 and, once,
  +0x90 = a new track (AudioSystem vt 0x20) and **Audio::PlayProceduralAudio(0x19DC06DF, +0x90, the fire pit's position)**
  (call 0xD79E7C, return 0xD79E81: the only use of 0x19DC06DF in the exe). Exit 0xD78F80 (vt 0x24): tribe +0x554
  (partying, 0xC8E950 / 0xC8E960) = 0, Audio::StopAudio(+0x90, 0). CanJoin 0xD78E10 (vt 0x30): a player tribe citizen, the
  target a type 10 tool of its tribe (tool +0x1F8, 0xC9DB70), the tribe not partying.
* ✔ (runtime, lab 20261107-102952) `coop.tribe party` with the host's own members selected (DoActionForObjects(sel, 12,
  fire pit)): tribe +0x554 = 1 for ~25 s, the host's game played 0x19DC06DF (caller 0xD79E81), the guest's none; the
  guest's mirrors played the dance animations (tribe.mirror_anims). The "DoActionForObjects returned 0" of the earlier
  attempt is its normal answer (1 only for an empty selection).
* Fix `tribe.party_music`: Audio::PlayProceduralAudio and Audio::StopAudio hooked; the host relays a play of 0x19DC06DF and
  the stop of its track (MSG_NPC_ANIM kinds 7 / 8); a guest plays it on its own new track and stops it. Test
  tribe_party_music (lab 20261107-103745 FAIL off, 20261107-103611 PASS: the guest's game played it 3 s after the host's
  and stopped when the host's stopped, 21 s later).
* ? A guest that joins during a party does not hear it until the next party (the play is sent once).

## A member the guest buys at its hut (2026-11-07, fix/w46-tribe-dance)
<a id="tribe-guest-baby"></a>

BUG-0671. GOG 2024, Ghidra and lab lane 5.
* ✔ (runtime, lab 20261107-103440) A left click on the own hut costs food 10 at once (15 -> 5) and a baby is born 5-8 s
  later; with 5 food the click does nothing. Right clicks on the hut give no baby.
* ✔ (Ghidra) The birth is `SporeCoop::TribeStage_BabyBorn` 0xCD74A0, thiscall(TribeStage = cTribeUI_Get, x, y, z) ret 0xC
  (SpawnMember(0), the cry "vcode_baby_born_cry", HUD +0x14, stage +0x3A3 = 1). Callers: the stage enter 0xCD78F0 (the first
  baby, +0x3A3 0) and the stage message handler 0xCD81F0 for message **0x63BBC42** (place at data +0x28 -> +4). No code
  pushes the id (only the handler's compare and the stage's listener list 0x1477470): it comes from data. The cursor
  "cursor-mate" (0x5498863) is loaded (0xCCE790) but never set by code. Where the click charges the food was not found (?).
* ✔ (runtime) Both games called it from the message path (return 0xCD8ED3) after the guest's click (lab 20261107-104338):
  the guest's game made its own baby (removed by the mirror reconcile), and the host's game made one because the world
  effects relay (effects.other_stages, BUG-0451) replays the guest's hut effect there, whose data sends the message. With
  that relay off the guest paid 10 food and the host got nothing (lab 20261107-104533): the BUG-0671 symptom. Its owner was
  the next in the turn split (the host's with an even member count, lab 20261107-104918).
* Fix `tribe.guest_baby`: a guest that mirrors the host's tribe makes no baby: it sends the place (MSG_TRIBE_CMD kind 3);
  the host pairs it with a baby its own game makes within 4 s within 6 m (the relayed effect's) or makes it itself with
  0xCD74A0, and the member is the buyer's. Test tribe_guest_baby (FAIL 20261107-104918, PASS 20261107-104809) and
  tribe_guest_baby_noeffects (the relay off: see the ticket).
## The member lead kept on, and a mirror the guest's game holds back (2026-11-07, fix/w46-k3-bodies)
<a id="tribe-lead-keep"></a>

BUG-0410, BUG-0651 (`coop.fix drive.lead_keep`, net/interp.h) and BUG-0386 (`coop.fix tribe.mirror_keep`, tribe.cpp
mirror_drive). Measured with `coop.bodyrec start diag` (SPORE_BODYREC_DIAG=1 for coop_body_trail.py): the guest records
per member mirror `a<id>` (the driving's aim), `r<id>` (M1's raw lead: the newest state moved on to the sender's now) and
`l<id>` (x: how far the shown lead's sender time is behind M1's, plus one guest frame; y: 1 = no lead; z: walk error).

* ✔ observed (body_trail_tribe_player, rel/0.7.9 code, lab 20261107-073841, load 8-16 on 6 cores): in 2 of 3 runs the
  member lead was off for the whole run (`l` y = 1): the one-way latency BUG-0619's rule reads (half the round trip of
  the acks, both games' frame times included) was over 100 ms; members shown interpolated, 0.2 s + latency late, p90
  3.2-3.4 m (aim 3.5 m, the raw lead would have been 2.3 m). The 0.7.9 final check run 20261107-012856 #2 had it off
  for 25 s of 60. The player network alone reads 80-101 ms: on the rule's edge (BUG-0651).
* ✔ observed (the third run, lead on 83 % of the time): with the lead on the aim was p50 0.60 / p90 2.36 m off the host's
  member against the raw lead's 0.25 / 1.20 m: drive.lead_late's catch-up at 1.33x (at most +3 m/s) kept the shown time
  0.25-0.3 s behind for ~0.75 s after every start known late (a spike, a loss burst).
* ✔ observed (net_tests, a catch-up at 2x): drive.lead_late's "where the shown target was going" used the shown target's
  own velocity; after a frame that stepped back (velocity 0) the next state stepped it back again and the target stood
  every other frame 0.07-0.1 s behind. Going on at the lead's velocity instead: no stall.
* ✔ fix (drive.lead_keep), final: BUG-0619's no-lead rule stays for the mobile network but its line is 130 ms (back
  under 115 ms) instead of 100 / 85 ms; the late target catches up at 1 + lag / 0.25 s (at most 2x), going on at the
  lead's velocity; a new state steps the shown time back only when its news was late (the lead had to go over kLateNews
  0.05 s further than the median of the last 32 horizons past the state before it) and not at all when no point of the
  new path is within 1.5 m of where the shown target was going (a turn round inside a spike: the lead's time at once);
  a mirror's drawn step is always held to 2 x its own speed + catch_up (WalkPlace::lead_speed). net_tests
  test_lead_keep: over a 110 ms one-way link 0.7.9 2.79 m mean target error, the fix 0.00 m; a start known 0.3 s late
  within 0.3 m after 0.53 s (0.7.9 1.10 s); a turn round in a spike over 2 m 0.00 s after the news (0.7.9 0.67 s).
* ✔ observed: with the lead kept on over any link (a first version) tribe_walk_mobile went stop-and-go and drew 0.9 m
  jumps (1 of 3, 1 of 3 against 3 of 3 with the rule, labs 20261107-094647, -095652-2 / -095652); the latency the rule
  reads does not part the mobile network (109-126 ms at the switch) from a loaded guest (100-150 ms): BUG-0651 stays for
  a heavily loaded guest.
* ✔ result: body_trail_tribe_player 3 of 3 with the fix (p90 1.32-1.77 m) against 0 of 3 off (2.24 / 2.00 / 1.81 m,
  over 2 m 1.06-1.13 s) at the same time on the other lane (labs 20261107-114838 / -114838-2, lab load 25-35).
* ✔ observed (lab 20261107-081649, lead kept on): aim p90 1.62-2.34 m against the raw lead's 1.36-2.15 m: the lead's
  cost is gone; what is left at a load of 12 on 6 cores is the raw lead itself (states every 0.1-0.2 s from a host at
  16-20 fps) and the walk behind the aim (p90 0.7-0.9 m).
* ✔ observed (BUG-0386, the track of fix/w45-remote-bodies-2 lab 20261106-172428 #2, tribe_walk_guest_walks_173129,
  the guest's own member 2): from the frame its game's walk took over (game speed 0 -> 11-12 m/s) every placing of
  0.38-0.40 m was undone and the game moved it 0.03-0.04 m a frame for 0.9 s while its target walked on at 9 m/s; the
  error grew to 8 m and drive::step teleported it. walk_place never counted the placing as lost (under its 0.5 m a
  frame) and keep_lost (BUG-0361's rule for NPC proxies) was off for mirrors. ? what holds the body there is not known
  (no object within 3 m in the probe lines; static RE: creature pairs are not in the gameplay contact handler).
* ✔ fix (tribe.mirror_keep): keep_lost for walked member mirrors: an error over 0.75 m that grows for 0.15 s moves it by
  teleports with the mark at the catch-up speed until it is within 0.2 m. Test: body_trail_tribe_hold (`coop.tribe undo
  <id> <s>` plants the hold: the game's walk and every unmarked placing undone, 0.04 m a frame; marked teleports kept).
* ✔ observed (BUG-0445, npc_walk_tribe at 30 fps over SPORE_NET=player, labs 20261107-103414, -112016 / -112016-2,
  tracks out/lane6|7/track/npc_walk_tribe_*.json): one-frame jumps of 2.5-5.6 m of walked animal proxies, each the frame
  after the proxy's lead target stepped as far (the host's NPC moving smoothly at 0.2 m a frame) and walk_place's snap
  (2.5 m) teleported it there. Some are a walking state known late (BUG-0650's case), most the lead's extrapolation
  through a spike or loss burst (up to kMaxLead 0.6 s at the old speed) while the NPC slowed or turned: 4-5 m off when
  the next state came. ✔ tried and reverted: drive.lead_late for the proxies' buffers (`drive.lead_late_npc`): it failed
  on and off alike at the same time (-112016 lane 6 on, -112016-2 lane 7 off). Open as BUG-0674 (the lead through a gap).
* ✔ (BUG-0410) with drive.lead_keep a frame of a catching-up mirror is held to 2 x its own speed + catch_up
  (WalkPlace::lead_speed): unbounded, the game's walk at 13-17 m/s plus catch_prop drew 1.0-1.4 m frames
  (tribe_walk_player lab 20261107-102038 #3); with catch_prop off instead a 6-9 m error took 0.6 s to close.
* ~ BUG-0674: a member that turns round inside a latency spike is extrapolated the old way for up to kMaxLead (8-9 m
  off for ~1 s); a limit of the lead itself, open.

## Each tribe's leader in a guest's game (2026-11-07, fix/w46-s1-sweep)
<a id="tribe-leader-sync"></a>

* ✔ `cTribe::SetChieftain` GOG 2024 **0xC8FEE0** (GOG 2016 0xC8FED0; `SporeCoop::cTribe_SetChieftain`): thiscall(tribe,
  citizen) ret 4; citizen 0 = the first member of +0x340..+0x344 for which 0xC0B8D0 is true and +0xB5E is 0. It calls
  SetLeader 0xC007A0 (+0x11C), sets the chief scale (spatial vt 0x40 of citizen vt 0x80(0)) and SetSpecializedTool(0xB).
  The old leader keeps its staff and size (nothing resets it). GetLeader 0xC00790 (`mov eax,[ecx+0x11C]`),
  cTribe::GetChieftain 0xC8FEC0, cCreatureCitizen +0x1010 city / +0x1014 tribe getter 0xC233F0.
* ✔ (lab) The leader was each game's own: a guest's mirror of an NPC tribe's musician (the host's member held tool 6)
  led its tribe in the guest's game (BUG-0652: "fields: tribe_member_tool N tool: applied 6.000, the game reads
  11.000" in 55 runs, tribe_walk_player in 22 of 66). It kept the chief's staff (tribe.leader_tool, BUG-0583), the
  mirror of the host's leader had a staff too. ✔ (lab 20261107-114312) right after a guest's load an NPC tribe of its
  game had no leader at all ("the leader was none") until the field set one.
* Fix `tribe.leader_sync`: the members' field kind (tribe_member, kid 2) has a `leader` field (2 = leads, 1 = not:
  values under 0.001 are not sent, fields_wire::sane); the guest calls SetChieftain on the mirror of the host's
  leader. The kind is applied before the tools' (kid 5) in the same pass, so the old leader takes its tool at once.
  ? Side note: a tool 0 is never applied either (the same sanity range): a mirror keeps a tool its host member put
  down (not seen as a problem; the members change tools, they do not drop them).
* Test tribe_leader_sync (`coop.tribe chief <id>` on the guest: SetChieftain on a mirror, the state the lab saw):
  FAIL with the fix off 20261107-114534 (the guest's #5 kept leading 15 s), PASS 20261107-114426 (1.1 s).

## The footsteps of a creature and the epic flag (2026-11-07, fix/w46-misc)
<a id="tribe-epic-steps"></a>

BUG-0669, GOG 2024, Ghidra and lab lane 9 (`tools/tests/coop_tribe_footsteps.py`).
* ✔ (Ghidra) The animation update 0xA01000 (this = the animation's owner, its cid at +0xFBC) calls FUN_009A6BD0 (the
  footsteps) when `+0xFAC` (a gait blend, set in 0xA01E80) or the cid's **epic flag +0x2D5** is set, and the cid's
  +0x260 is set. FUN_009A6BD0 calls FUN_009A6910 per foot channel while the cid's velocity +0x1660..+0x1668 (squared
  over 0.1) or turn +0x166C (over 0.2) moves; 0x9B7C70 writes that velocity per update from the model's own move
  (+0x18 minus the last position +0x1644, in its local frame) or, with +0x263, from +0x264. FUN_009A6910 plays the
  puff 0x2D4ADB36 (with the epic flag also 0xDCAE75D5) and 0x9A4FA0("foot_step"): an audio play with sound id 0, the
  foot's sound type in parameter 0x39FA0AB (the part's +0x424: 0x4A776923, 0x76BDAADD, 0x5C0D552C, ...) and the epic
  flag in 0x48DEF6D. The "sound 0x4A776923" counts of the ticket were that parameter, not a sound id.
* ✔ (Ghidra) The cid is `AnimatedCreature +0x17C`; 0xA04C00 (`SporeCoop::AnimatedCreature_SetEpicFlag`) writes its
  +0x2D5; the creature's model setup 0xC21D50 calls it with the tuning's isEpic (+0xE84 -> +0x388).
* ✔ (runtime, tribe fixture) The host's tribe members have no tuning (+0xE84 = 0) and flag 0: their footsteps play
  only while the gait blend runs (rarely: none to a few a minute on both games). The fixture's planet has an epic
  (species 0654E5CB, tuning +0x388 = 1, flag 1) near the villages: every footstep the host's game played in a minute
  (35-52) was the epic's. The guest's proxy of it had the host's tuning (BUG-0462, SetTuning 0xC0C2E0 after Create)
  but flag 0 in 4 of 5 runs (made in a herd of ours, model setup without tuning): no footsteps. In one run the proxy
  had the flag from the start (74 vs 42).
* ✔ Fix `npc.epic_steps`: a proxy's flag follows its tuning's isEpic while the host's NPC walks (reported speed over
  0.3 m/s). Without the walking rule a standing proxy, turned a little each frame by the mod's facing (+0x166C up to
  1 rad/s), played 256 against the host's 38. Test tribe_footsteps (FAIL 20261107-120238, PASS 20261107-120034).
* `coop.audio list` shows the float parameters too now (hook of AudioSystem vt 0x3C, `SporeCoop::AudioSystem_SetFloat`
  0xA20A90): the position 0x3475391/95/98 of a play.
## The galaxy menu's rollover planet across a planet rebuild (2026-11-07, fix/w46-rejoin-crash)
<a id="tribe-menu-rollover"></a>

BUG-0676. GOG 2024, Ghidra + lab lane 6. Not Tribe code: the guest's galaxy menu while it loads a received Tribe world
(chaos_tribe_awful 20261107-113600 #1). Not the BUG-0672 class (no state of an old world): it was the guest's first
load after a cold start; the "rejoin" of the ticket's title came minutes later, with the guest already stuck.
* ✔ (Ghidra) the fault 0xDF6D93 is the first read of **GGE_PanelSetState 0xDF6D80** (thiscall(panel, state) ret 4:
  `cmp [esi+0x2C], ebp`), panel = null. The call chain 0xDF7C2C < 0xDE825C < 0x7D86D6 < 0x81530F < 0x7D8D6A: the planet
  set's rollover (vt 0x5C 0xDF7BF0) called with 0 by the galaxy menu's exit 0xDE8160 (old mode vt 0x1C from the mode
  switch 0x7D8670 < cGameModeManager::SetActiveModeAt, i.e. LoadGameByName leaving the menu). 0xDF7BF0 rolls the old
  planet out with `0xDF6D80(1)` on old->+0xB8 without a null check.
* ✔ (Ghidra) the old planet is `[0x16A1344]+0xF8` (GalaxyGameEntry_ptr), a pointer into the planet set's vector
  (`[0x16A15FC]` +0x08..+0x10, 0xBC-byte entries, +0xB8 the panel). Only 0xDF7BF0 writes it. **GGE_RebuildPlanets
  0xDF95A0** clears every entry (0xDF94E0: panels freed, +0xB8 = 0), resizes (0xDE9DD0: a larger count reallocates and
  frees the old storage) and refills, and leaves +0xF8 / +0xFC alone. It runs on 0x625D27D, which
  cStarManager::ReadStarsFromSaveDB sends at 0xBB98F6: the mod's load_now calls ReadStarsFromSaveDB right before
  LoadGameByName (world.cpp, the star manager kept the old slot's stars).
* ✔ (runtime, 20261107-125914, fix off) a cold guest's menu shows its empty slot Game0 (6 planets, room for 6); the
  rollover is put on planet 5 (`coop.menuhover arm 5`), the received world's slot has 7: the rebuild reallocates, the
  rollover points into the freed block ("no planet of the list; panel 00015E08"), the exit faults in 0xDF6D80
  (0x8102B2 here: the freed block held a stale value, not 0 as in the lab run; the same calls 00DF7C2C 00DE825C 007D86D6
  0081530F). With the old storage kept (the same count) the rollover stays valid and nothing faults (20261107-125449).
* ✔ (runtime + Ghidra) **why 22939 faults**: the exit 0xDE8160 had released +0x184 (= 0) before the fault and never
  finished; the mode switch never set the new mode (0x7D8670 writes +0x28 after the exit), so the half torn-down menu
  stayed the active mode and its planet set's frame 0xDF8700 called 0xE01320 on [GGE+0x184] = 0 in every frame: a
  fault at 0xE01323 (data 8) each frame that the game's handler catches (16540 in 5 min in the forced run). The guest
  stays "Galaxy menu ... busy=loading" with frames from the game loop, never a crash, never a report.
* In the game alone the mouse rests on a planet whenever the camera of the menu (the intro flight) brings one under it;
  the game's own 0x625D27C path (0xDE8A80: ReadStarsFromSaveDB + rebuild) has the same flaw.
* Fix `game.menu_hover` (gamefixes.cpp, hook on GGE_RebuildPlanets): before the rebuild the rollover planet is rolled
  out the game's way (vt 0x5C(0)); after it a +0xF8 / +0xFC that names no entry of the new vector (with a panel) is
  dropped, each with a log line. Off: the game's code. SporeKit `kit.fix faultstorm` (0.3.12: nothing): the same
  fault 300 times in 10 s logs `fault storm: ...` once per place and writes one report per run (mark "kept faulting").

## A guest's own game over from a blow on its last mirror (2026-11-07, fix/w46-tribe-leader)
<a id="tribe-guest-game-over"></a>

BUG-0685. GOG 2024, lab lanes 4 and 5.
* ✔ (lab, tribe_leader_ko) a guest's game runs its own game-over rule on its player tribe of mirrors: with the host's
  tribe down to its chief (the others knocked out, bodies taken out of the list) a blow of the guest's game on the
  chief's mirror (the citizen's TakeDamage 0xC26920, `coop.tribe hurt <id> 1000`: 168 -> 0) raised the guest's input
  lock (cGameInputManager +0x110) within 0.1 s and the caption "Alas! Your population has fallen below the minimum ..."
  about 12-19 s later, while the host's chief stood with full health. The mod's set_health put the mirror back to the
  host's 168 at once (dead flag 0): the game over stays anyway. Its OK would reload the guest's own last save.
* ✔ The same with `tribe.leader_sync` and `wsync.tribe_level` off (lab 20261107-164852): not a 0.7.10 change; the
  damage hook and the BUG-0172/0288 guard are as in 0.7.9.
* ✔ (lab) the mod's own death of a mirror (tribe.ko: set_health 0 + Die, the host's chief knocked out with no other
  member alive) brings no game over to the guest (its lock stays 0, no caption: tribe_leader_ko exploration runs
  20261107-162217 / -162222), as BUG-0288 saw. Only the game's own blow does.
* ✔ (lab) a chief that still has other members with health is not knocked out by a blow of 1000 on the host: its
  health stops at 0.3 and comes back at ~9 per second (the game's own rule, `coop.tribe pincer`); alone, the same
  blow takes it to 0 (dead flag) and the host's game over follows.
* ✔ (lab, campaign_tc2 with the new log lines) the host's chief knocked out during the raids and up again: every guest
  logs `mirror #1 goes: dead here, back on its feet on the host (made again)` and then leader_sync's `(the leader
  was none)`: the "leader was none" lines of the campaign are these replacements, not a leader the guest's game lost.
* Inferred chain of the campaign (Carol, s 2950-3007): a blow of Carol's game on her last mirror with health (#4, the
  chief) brought up her game over (`busy=input_lock,paused` from s 2960, only on her); the mirror then went (the KO
  replacement or the game's own removal) and tribe.gameover's guard (a guest with its input lock up and no member with
  health: "the player tribe is out") refused the new one while the host kept sending #4.
* Fix `tribe.mirror_blow` (h_CitizenTD): on a guest (not while the host is away, local_play) a blow of its own game
  never takes a member mirror below 0.3 health; the host's game decides when it falls. Off: the game's blow.
* Diagnostics: a guest logs (60 lines at most) `tribe: mirror #N goes: ...` (gone from the host's snapshot, a body,
  dead here and up on the host), `a member of this game the host has not goes`, `mirror #N: its object is gone`,
  `mirror #N is not made: <why>` (no local tribe for its tribe, tribe.gameover's guard, the game made no citizen)
  and `mirror #N is made again`; `coop.tribe` lifetime line `mirror_blows_kept=`.
## The tribe setup without a simulator camera: reproduced (2026-11-07, fix/w46-n1-crashes)
<a id="tribe-camera-null"></a>

BUG-0167 (GOG 2024, lab lanes 9 / 10, test tribe_camera_null).
* ✔ (runtime) The null simulator camera at TribeGame_Setup comes from the galaxy menu's own intro timer firing in the
  first frames of the Tribe stage: a guest joins 12.5 s after its menu came up (twoinst.start(join_at=12.5), the
  BUG-0607 timing), its load runs ~3-3.5 s later, the menu's timer (~15.85 s) starts "GGE_intro" in the mode
  01654C02 (Tribe, "the galaxy menu intro starts (mode 01654C02)"), and 0.07 s later the setup's LookAt meets the null
  camera object [0x167FAAC]. With `tribe.camera_guard` off: `0xC0000005 at 0x00B12D62 SporeCoop::SimCamera_LookAt+0x52
  data=0x000002B4` (calls 0xCD7D22 = TribeGame_Setup), the report's fault (GOG 2016 0xB12D32, data 0x2B4); with the
  guard: "skipped the tribe camera set-up", the guest plays on. Hit on the first join in 4 of 4 cold starts (lab
  20261107-231113, 231118, 231252, 231257).
* ✔ Only that order: the intro started by hand (PlayCinematic as the menu's handler calls it) right after the mod's
  load request in the menu, in the first tick of the Loading mode 01654C08, or on a host reloading its own Tribe
  played into the stage ("still playing in a stage: ended it") with the camera there (lab 20261107-225810 ..
  230854, 15 loads). ~ Inferred: the camera object is made by the stage's own setup path a frame after the stage
  begins, and the intro that starts in that frame takes the step that makes it.
* Both layers hold on 0.7.9+: `world.intro_at_once` (BUG-0626) does not start the intro outside the menu (the test
  switches it off to make the race; in the 4-player churns of lab 20261107-223550 the guests' logs show "would start
  outside the menu (mode 01654C02): not started" 1-2 times each), and `tribe.camera_guard` (d605e96, a lab switch since
  0.7.11, COOP_FIX_UNSAFE) skips LookAt / SimCamera_Update while the camera is null.
## The game's own exit at the end of Tribe, the City Hall editor held or open (2026-11-07, fix/w46-n1-crashes)
<a id="tribe-exit-editor"></a>

BUG-0578 (GOG 2024, lab lanes 9 / 10, tests exit_editor_held / exit_editor_open; `coop.close` posts WM_CLOSE to the game
window, the player's close button / Alt+F4; the lab's kill and `quit` end the process without the game's exit).
* ✔ (runtime) WM_CLOSE runs the game's own exit: `App::cSporeApp::Shutdown`, the mod's summary and unload, then
  "the game exits (ExitProcess 0)" 0.3-0.5 s later; no crash report.
* ✔ With the City Hall editor request held (core.editor_own_pause off: "the editor waits", mode Tribe, busy
  input_lock,paused, the 0.7.3.6 state of the report) the exit is clean too (lab 20261107-231415, 20261107-233741),
  and from inside the City Hall editor (mode 00DBDBA1; lab 20261107-231415, 20261107-232504 x2).
* ~ Not covered: Windows (the report's Steam build ran on Windows), a guest that timed out rather than left, and the
  report's 81 s in the held state (today's old gate opens the editor ~60 s after the vote, so the test closes at 40 s).
## A carried bundle put on the ground is the same object (2026-11-08, fix/w46-k8-open)
<a id="tribe-bundle-reclass"></a>

BUG-0661, follow-up of [#tribe-host-bundles](#tribe-host-bundles) and [#tribe-carried-bundles](#tribe-carried-bundles).
GOG 2024, lane 5.

* ✔ (runtime, host_bundle_drop) when a member carrying food dies, its game moves the same cGameBundle (same pointer,
  same census id) from the member's container (+0x34) into the ground container (bundle manager +0x1C): the object's
  holder +0x128 changes, nothing is made or destroyed. A copy bound on the guest kept its class (carried) and stayed
  in the hand of the member's copy.
* ✔ `wobj.bundle_reclass`: a bound copy of another class than its host object is removed and made again in the
  right container in the same snapshot (lab 20261108-011630-2: the ground copy within 3 m 6 s after each drop).
* Tribe only (`Kind::reclass_stages`): in Civ the old villages' members run their own behaviour and a bundle taken
  out of a member's hand faults its gather node (BUG-0684, 0xC23469); a play_civ with the reclass on in Civ had that
  fault storm on the guest (lab 20261108-015213; inferred to be the BUG-0684 class, not proven to be the reclass).

## Remote bodies under latency spikes: the lead's fall, throws, the link's own latency (2026-11-08, fix/w46-k4-bodies)
<a id="tribe-lead-fall"></a>

BUG-0674, BUG-0675, BUG-0651, BUG-0680 (K4; net/interp.h, net/connection.cpp; switches `drive.lead_fall`,
`drive.lead_throw`, `drive.lead_low`, `drive.puppet_fall`). One class: a remote body moved on past its newest state
while no newer state comes, and the rules that decide how far and whether.
* ✔ (bodyrec rows, lab 20261107-102038 / -110506, K3) A member that turns round or stops inside a latency spike of the
  player network was moved on the old way for up to kMaxLead 0.6 s at its full speed: 2 x speed x gap off (8-9 m at
  9-12 m/s). The same in a Creature puppet's interpolated extrapolation (max_extrap 0.25 s at the state's velocity):
  puppet_stops' failing stop of lab 20261107-132459 #1 (the rel/0.7.9 track rows): the target walked on at 4.75 m/s
  through the stop and stepped back 0.72 m when the stop's state came, the puppet 0.33 m (bodyrec) / 0.86 m (target)
  past where the walker stopped. And a Space ship's VelocityEstimate::at (max_ahead 0.4 s): 11.4 m past a ship that
  had stopped (space_fast_sync, lab 20261107-215251 #1, V2).
* ✔ The rule now (`net::fall_time`): at the full speed up to the usual horizon (the median of how far past the newest
  state the body was when the next state came, last 32 states) + kFallMargin 0.04 s, then falling exponentially
  (kFallTau 0.12 s; the velocity handed to the driving falls with it). Puppets (Creature only): from kPuppetFallMargin
  0.02 s past the render time's usual horizon, kPuppetFallTau 0.05 s, so the velocity falls under 0.3 m/s and
  creature.puppet_stop holds the puppet where the target slows. Unit tests (net_tests test_lead_fall): a turn round
  inside a 0.35 s spike 6.0 -> 4.5 m worst, a stop 3.0 -> 1.5 m, a straight walk 0 -> 1.7 m (the cost: behind for the
  rest of the gap, caught up by drive.lead_late), a 28 m/s ship stopping in a 0.4 s spike 8.4 -> 5.1 m, a puppet's
  target past a stop 0.71 -> 0.17 m.
* ~ The cost is real in the lab: a gap of 0.5-0.7 s with no state at all (seen under a lab load of 30-35, all members
  at once, body_trail_tribe_player lab 20261108-010745-2 #1, 6.5-7.2 s) stands the shown target after ~0.3 s while the
  member walks straight on (8.6 m off before the news). The extrapolation can not know which way the body went; the
  fall bounds the turn round and the stop, full extrapolation the straight walk.
* ✔ (coop.npctrack rows of lab 20261107-103414 #2, n19 at 44.0 s) A pushed NPC thrown at ~20 m/s reports that speed in
  its state (path 19.6-20.4 m/s, reported 19.4-20.4 m/s): lead_velocity took any path over 15 m/s for the body put
  somewhere and returned 0, so the copy stood at each newest state (3-6 frames) and stepped 2 m when the next came.
  Now (`drive.lead_throw`) the state's velocity within 35 % of the path and the same way is a throw; and a step under
  1 m is never a put: under a lab load of ~30 the host's state times are off its positions by up to a frame and a 9-10
  m/s member read 12-18 m/s, its lead stood 0.4 s (lab 20261108-005832, t3 at 3.2-3.6 s).
* ✔ (guest log lines `net: round trip p10 .. p90`, new) The mean round trip (Connection::rtt_ms, ~10 samples) follows
  every spike of the player network and both games' frames: 115-462 ms on the player network at a lab load of 25-35
  (the no-lead rule then read 139 ms and turned the member lead off: body_trail_tribe_player lab 20261108-003907-2,
  p90 2.11 m). The lower quartile of the last 128 samples: player network 84-151 ms (one-way 42-75), mobile 186-234
  ms (93-117) (lab 20261108-005832, -005832-2); with half this game's own wait between two socket reads taken off each
  sample (a loaded guest's frame, Connection::poll_prev_ms) the player network at a load of 25-35 reads 80-152 ms
  (lab 20261108-010745 / -2). The rule (`drive.lead_low`) reads half of it, lines 80 / 70 ms: the mobile network still
  goes off (tribe_walk_mobile OK, `lead off (one-way latency 101 ms)`), the player network under load keeps the lead.
  ~ The margin at the top of the player network's range (76 ms against 80) is thin; a heavier load than the lab's 35
  may still turn it off.
* ✔ (npc_walk_tribe over the player network, lab 20261108-011449 / -012638-2) The walked NPC proxies' remaining jumps
  were late news without drive.lead_late (a walk that started in a spike: the target jumped 2.6 m at the start) and a
  turn known late (the target jumped 2.6-4 m and walk_place's snap teleported the proxy onto it). Now the proxies
  have late_lead (late_time) and smooth_max 8 m (every lead jump up to 8 m through the smoothing offset); the members'
  rules (the no-lead rule, the smoothing only after a switch) stay theirs.
* ✔ (2026-11-08, fix/w46-n1-camera-test) The same null camera made on purpose: `coop.introstage` starts the intro (as
  the menu's handler does, through the BUG-0626 hook) in the mod's first tick in the next stage; the tribe setup's
  LookAt comes 0.10-0.15 s later and meets the null camera on every start (lab 20261108-013439, 20261108-013444). The
  join-time race alone depends on the guest's console answering before ~12.5 s after its menu came up, which a slow
  start misses (lab 20261108-012203: ~30 s).

## Carried food bundles outside Tribe (2026-11-08, fix/w46-k7-findings)
<a id="tribe-bundles-outside-tribe"></a>

BUG-0684, GOG 2024. Follow-up of [the carried bundles](#tribe-carried-bundles).
* ✔ In Civ the old tribe villages (and the cities' citizens, noun 0x18EB4B7) run their own behaviour in each game: in
  `civ_village_bundle` a creature of the guest's game given food (`coop.wobj bundle carry`) still carried it 10 s later
  with the bundle sync kept off its hand; the host's carriers' food was copied into the hands of 3 creatures of the
  guest's own village within 1 s (the nearest empty hand within 8 m) and removed again 3 s later (lab 20261108-004130-2,
  `wobj.bundle_civ_carried` off: `carried=3 removed=6`).
* ~ The fault of play_civ (132 caught faults at 0xC23469, `SporeCoop::Citizen_HoldBundle` 0xC23460 with a null bundle,
  from the gather node sub-state 0xD74D40) follows when such a bundle is removed from the container while the member's
  gather node holds it: inferred from the disassembly and the play_civ log (the host's carried bundle made on the guest
  6 s before, 25-35 m from the old village); the test shows the removal, not the fault (0 faults in its FAIL runs).
* Rule (`wobj.bundle_civ_carried`): outside Tribe a carried bundle is left to the game that carries it: no copy of the
  host's carried food is made on a guest, a guest's own carried bundle is kept (bundle_own), and a bound one whose host
  bundle is gone is not removed. Ground bundles are synced as before.
* ~ Tried and reverted (2026-11-08): moving the fall's knee on by half of a newest state's extra age (a spike whose states
  flow late, so a straight walk is not shown behind): the proxies got worse (npc_walk_tribe 0 of 2 with 2 jumps each,
  body_trail_hits 1 of 2 at 7.9 m after a push, lab 20261108-015610 / -2). The plain knee's cost stays: NPCs walking
  straight on through a spike, body_trail_hits NPC p90 0.56-0.58 m against 0.33-0.37 m without the fall (limit 0.6).
## The creature attack's marker: an ornament of model 0x356EB8A (2026-11-08, fix/w46-n2-desyncs)
<a id="tribe-attack-marker"></a>

BUG-0528, BUG-0740. GOG 2024 (dc04aee5), Ghidra and lab lanes 9/10.

* ✔ (Ghidra) 0xC911C0 (thiscall(this, const Vector3* pos, float 10.0) ret 8) runs only in Tribe (mode 0x1654C02): it makes
  once (this+0x28) an ornament of noun 0x18C88E4 and sets its model 0x356EB8A (0xC6F910), puts it at pos, makes once
  (this+0x24) a noun 0x2E72CAE object, and adds a minimap blip through cTribeUI vt 0x6C and 0xCC98C0 (flags 0x10000 /
  0x8000 / 0x4000). Its only caller is `SporeCoop::SpotRecord_CellId` 0xC9B140 (+0x66F, return 0xC9B7AF).
* ✔ (runtime, `probe 0x00C911C0`, lab 20261108-050749 campaign_tribe#3) the call during Tribe play came from the creature
  attack behaviour's start 0xD73930 (make = 0, return 0xD739E0), with this = the global 0x157ADB0: one marker for the
  attack, made at its first attack and reused.
  ~ superseded (2026-11-08): "the global 0x157ADB0" is a record of the spot table, one marker per record: see
  #tribe-spot-markers. The detector then sees it as a host-only ornament (`Tribe:*:ornament`,
  mapped to BUG-0740 in tools/detector_report.py): the attack runs only in the host's game.
* ✔ (runtime) the tribe fixture's three ornaments of model 0x356EB8A are the same in both games; a share save, a guest's
  rejoin (2 players) and three extra guests' rejoins (4 players) made none (lane 9, 2026-11-08).
* ~ `SpotRecord_CellId` 0xC9B140 also arms a game timer (0x1695368 = now + 240 s); its periodic caller 0xC9B8D0 (from
  0xCDF080 / 0xCFE2C0) makes a record for each cTribe's cached spot (tribe +0x300 flag, +0x304 position from 0xC8FFB0),
  0xCDDE70 (the Tribe entry state machine 0xCDE7E0) for the spawn markers (noun 0x36BE27E, +0x108 == 0x57F09E5E).
* ? Why `coop.drama start 7` on the fixture (lab 20261108-052511, -052727: the attack started and ended within 150 s)
  made no marker: not known (no formed tribe, the pace rule held `coop.tribe form`; or the attack never reached its
  start behaviour).
## The spot records and their markers in both games (2026-11-08, fix/w46-attack-marker)
<a id="tribe-spot-markers"></a>

BUG-0740. GOG 2024 (dc04aee5), Ghidra and lab lane 9 (`tools/tests/coop_tribe_attack_marker.py`). Extends
#tribe-attack-marker and #tribe-attack-spot.

* CORRECTION (2026-11-08, fix/w46-attack-marker): 0x157ADB0 is not a global marker holder: `SporeCoop::SpotRecord_Table`
  0x157ABB0 is a fixed vector with inline storage (entries from 0x157ABC8, 0x60 bytes, the record at +8), and
  0x157ADB0 is the record of its sixth entry. TribeMarker_Place 0xC911C0 runs on the record SpotRecord_CellId has just
  made (asm 0xC9B79E..0xC9B7AA: ECX = 0xC9ABA0(table, &id), the table's get-or-insert): **every spot record has its own
  marker** (+0x28 ornament of model 0x356EB8A, +0x24 a noun 0x2E72CAE object, a minimap blip). The tribe fixture's three
  ornaments of the model are the three records of its villages, in both games (`coop.wsync spots`).
* CORRECTION (2026-11-08): SpotRecord_CellId 0xC9B140 does not read its second argument (`make`): it scans the cells
  +-4 round the asked place (a face grid of 0x80) for an existing record (returns its id) and, with none, makes the
  record at the first cell of terrain kind 3, then places its marker; -1 when neither.
* ✔ (Ghidra) 0xB83110 thiscall(planet, pos) is the cell's terrain kind: 3 = shallow water (the water surface 0.01..2 m
  above the ground, slope under 5), 2 = deeper water, 1 = steep land, 0 = land. **Records are made only at shore cells**:
  the creature attack's animals come out of the water there.
* ✔ (Ghidra) Records are taken away only by the table's clear 0xC9A210 (the Tribe / Civ / Space modes' enter and exit),
  which calls `SporeCoop::SpotRecord_Release` 0xC8F150 (fastcall(record): DestroyInstance of +0x24 and +0x28, the effect
  +0x3C stopped) for each. A marker stays as long as the stage's table: the "removal when the attack ends" the ticket
  asked about does not exist in the game.
* ✔ (lab, lane 9) Why `coop.drama start 7` on the fixture made none: its attack hits the player village, whose record
  exists (the asked place has a record in the window: no new record, no marker). Asking the host's game for places
  round the village (`coop.wsync spot x y z`, SpotRecord_CellId as the attack's start does) made a record 121 m from it
  at the 33rd place (a shore cell, cell 00760046); places at 60-150 m in one direction answered -1 (no shore).
* ✔ (lab) The guest's game asked for the host's asked place makes the same cell's record at the same position (0.00 m
  apart, lab 20261108-061051): the cell scan depends only on the planet's terrain, which both games have.
* Fix `wsync.tribe_marker` (wsync.cpp row 15 `tribe_marker`): the host's row lists its records with a marker {cell id,
  record position, the place SpotRecord_CellId was asked for (a hook remembers it per new record)}; a guest that lacks a
  record asks its own game for that place outside the frame (the minimap blip is UI), or places the marker on a record
  of that cell without one (TribeMarker_Place(rec, pos, 10.0)); markers the guest made for a host record that is gone
  (the host's stage table cleared, the guest's not) are released with SpotRecord_Release. A rejoined guest gets them
  from the row (the host sends all rows at a join). `coop.wsync spots` lists a game's table, `coop.wsync dump
  tribe_marker` the row.
* ✔ (lab 20261108-063825, the switch off) The host's share save carries its spot records: a guest that rejoined after
  the host's record was made had it (with its marker, the same place) right after loading the host's world, without a
  SpotRecord_CellId call of its own. What a guest misses is only a record made after its load.
* ✔ (lab 20261108-063947 campaign_tribe, 4 players, the fix on) The game's own attack: at the Tribe arrival the host's
  game made 5 records from 0xCDDE70 (the spawn markers; each guest's game makes them itself), and 112 s later one from
  the creature attack's start (return 0xD739E0) for (-196.8 -441.8 -127.7): cell A0540026, its marker at (-177.4 -445.2
  -142.6), the ticket's place. All three guests' games made the same cell's record and marker within 0.7 s; the
  detector had no Tribe ornament row.

## The healer bonus at runtime (2026-11-08, fix/w46-g4b-coverage)
<a id="tribe-healer-runtime"></a>

BUG-0199 / BUG-0745, lane 3 (GOG 2024), `tribe` fixture (`coop_tribe_healer.py`):
* ✔ The heal tool (type 9) through the palette's placement on the host (`coop.tribe buy 9 9 200` + `buy_end`), a
  non-chief member's GrabTool order (`coop.tribe grab`): the member holds tool 9 within a few seconds. The chief (tool
  11) does not take it (three orders, no change).
* ✔ A blow on a guest's member in the host's game (`coop.tribe pincer <id> <id> 56`: the citizen's TakeDamage)
  shows on the guest's mirror at once. `coop.tribe hurt` on the guest only hits the guest's own mirror (the host's
  member keeps its health, the next snapshot restores the mirror): it is no way to hurt a member in co-op.
* ✔ The heal order `coop.tribe act 5 <patient>` with only the healer selected (DoActionForObjects, action 5): the
  patient goes 56 -> 112 in ~55 s (the heal tick 0xDB9AC0 in GOG 2024), the guest's mirror follows.
* ✔ The member's maximum health is not at +0x5D4 (1.0 there); with game::max_health the hook sees the full heal and the
  shared food rises by 5 on both games within a second of the patient reaching its maximum (15 -> 20; the test's
  1 s poll read 111.5 just before).


## The conquered village's caption holds every member (2026-11-08, fix/w46-member-stall)
<a id="tribe-conquer-caption"></a>

BUG-0531 (members standing in a gather or delivery node, new orders change only their order record). GOG 2024,
lane 9, `tools/tests/coop_tribe_conquer_orders.py` (`coop.tribe npc_tribe`, a raid by `coop.tribe order <id> 15 <hut>
1E4DAAE`, `coop.tribe huthp` to shorten it; `coop.tribe ai` now also prints `path=` the walk's waypoints left, `bt=`
the member's behaviour-tree ticks so far on the host, `anim=`, `upd=` +0x135, `tree=` *ai set, `city=` +0x1010).
* ✔ `cCreatureCitizen::Update` 0xC24A30 returns at once when +0x135 is 0 or vt 0x2C says so, and ticks the tree
  (`BehaviorTreeTick`) only when `*ai` (c+0xB4C) is set and the city c+0x1010 is null.
* ✔ A raid that takes an NPC tribe's hut to health 0 plays **TRG_TotemPole_Conquer (0x5418B038)** on the host
  (`coop.status`: "cinematic 5418B038 playing", `busy: cinematic,input_lock`, then `,paused`, `coop.uistate`
  modal=1): its caption waits for the player's OK with the game time standing. Every member's tree stops ticking
  (`bt=` unchanged for minutes, the guest's raider and the host's alike), so a new order (DoAction) changes the order
  record (rec=4) but no node, no walk: the members stand in whatever node they were (the blow reaction 0x05F79546,
  the gather node 0x0569236E state 1, 0x04623E0C). The guest's own game runs on (busy -), so to the guest its
  members simply ignore its orders. Fix off: lab 20261108-100229 and -100728 (both raiders, 0.0 m in 2 x 10 s).
* ✔ The play_tribe stalls of BUG-0531 were this: lab 20261108-020506-2 (fix/w46-k4-bodies) host `input_lock=1` from
  s 320 (the raided tribe's tools at health 0 at s 318) to the end; 20261105-115848 (fix/w44-tribe-guest) host
  `busy=cinematic,input_lock,paused` from s ~370 with the host's view at that village. The play bot never presses
  the caption's OK; a host player away from the keyboard does the same to every guest.
* ✔ Right after it the game plays **TRG_Upgrade (0x94D17D9F)** (the tribe's new level), whose caption holds the game
  the same way (lab 20261108-101943: still up 40 s after the conquest). The names are the game's strings (Ghidra
  `str TRG_`: TRG_Begin, TRG_FirstBaby, TRG_PRE_FirstTribeAppears, TRG_PRE_HutDestroyed, TRG_PRE_PopDestroyed,
  TRG_TotemPole, TRG_TotemPole_Conquer, TRG_TotemPole_Social, TRG_Upgrade); the ids are their FNV-1 hashes of the
  lower-case name (the registry id cCinematicManager +0x148 shows).
* ~ TRG_TotemPole_Social (0x6A4BB900, an alliance) is expected to hold the game the same way (not run in the lab).
* Fix `tribe.conquer_caption` (host, 2 or more players): each of these three cinematics playing 8 s is ended through
  `cCinematicManager_EndCinematic` (world::end_cinematic, outside the frame, as the OK does), with the notices
  "<host> watches a cinematic: the tribe waits for the OK (or 8 s)" and "the caption closed by itself", as
  civ.caption (BUG-0187) does for a city capture.

## The minimap: blips and the map transform (2026-11-08, feat/w46-partner-arrows)
<a id="tribe-minimap"></a>

F2. GOG 2024 (dc04aee5), Ghidra static only. The class and its API: [creature-minimap](creature.md#creature-minimap).

* ✔ (Ghidra) The Tribe cMinimap is `TribeGameUI +0x70`; TribeGameUI = vt 0x6C of the object `cTribeUI_Get` 0xCD4230 returns
  (`TribeStage_GetGameUI` 0x104BFF0: `[this+0x240]`). It is made by the Tribe UI load 0xCCDEF0 as a child of HUD window
  0x190722E (as in Creature). TribeMarker_Place checks vt 0x6C for 0 before use; check +0x70 too.
* ✔ `TribeGameUI_MinimapBlip` 0xCC98C0: thiscall(TribeGameUI*, u32 key, float x, float y, float z, int kind, bool show)
  ret 0x18. show 0 removes the blip; else MoveBlip, and when the key is new AddBlip with kind 0 -> flags 0x10000
  (icon 0x5E51BB6), 1 -> 0x8000 (0x5E51BC1, plants), other -> 0x4000 (0x5E51AD8) tinted 0xFFFFFF00. Callers:
  TribeMarker_Place (kind 0, key = the ornament) and the rebuild.
* ✔ `TribeGameUI_MinimapRebuild` 0xCCDA20 (fastcall) **clears every blip** (Minimap_ClearBlips), centres on the player
  tribe, then re-adds plants, spot records, nests (0xCC97C0, flags 0x20) and tribes (0xCC95E0: flags 2, |0x100000,
  tint = the tribe colour `GetCachedColorFromId(tribe +0x234)`). It runs at the Tribe UI load and from the stage
  message handler 0xCD1D60: blips the mod adds must be re-added when `Minimap_FindBlip` says they are gone.
* ✔ The own marker is the camera: 0xCCDDB0 (Tribe UI update) calls Minimap_SetAvatar with the camera's position /
  direction (0xB10160 / 0xB13840). In Tribe, `Minimap_WorldToMap`'s caller 0xE0BF20 hides blips outside the map rect
  instead of leaving them on the edge.
## The guest's copy of a host-formed tribe uses up its spawn marker (2026-11-08, fix/w46-b1-tribe)
<a id="tribe-copy-marker"></a>

BUG-0741. GOG 2024, lane 7, `coop_tribe_copy_marker.py`.
* ✔ (runtime) The tribe fixture has 4 tribe spawn markers (cGameplayMarker +0x108 == 0x57F09E5E, position +0x1A8) at
  the same places in both games. With `tribe.form_pace` off, one `coop.tribe form` on the host formed **two** tribes
  within a second (2 -> 3 -> 4 tribes: with 3-4 tribes the controller forms every second, #tribe-form-needs), each
  exactly at a marker, and the host's markers went 4 -> 2.
* ✔ The guest's copies (tribe.npc_tribes) are made ~1 s after the host's forming at the host's place (0.0 m from the
  marker); without `tribe.copy_marker` the guest kept all 4 markers.
* Fix `tribe.copy_marker`: the copy destroys the guest's marker nearest it within 30 m (cGameNounManager::DestroyInstance,
  as FormTribe 0xCC6440 does). Not done: FormTribe's other steps after the spawn (the terraforming call 0xBBEDD0 on
  marker +0x11C, the herds near the new village destroyed or moved) are not repeated on the guest.

## A guest's gather order on a fruit the host lacks (2026-11-08, fix/w46-b1-tribe)
<a id="tribe-fruit-order"></a>

BUG-0744. GOG 2024, lane 7, `coop_tribe_fruit_order.py`.
* ✔ (runtime) A guest's right-click gather on a loose fruit sends CitizenAction 2 with target kind 2 (noun 0x2C9CC91
  and the fruit's position); the host resolves it with find_object within 8 m. Destroying the host's fruit there
  (`coop.tribe unfruit`) and ordering at once reproduces the reject of play_tribe ("no such target here").
* ✔ A Gather (2) on a planet tree (cTreeData) with fruit left is accepted by DoAction on the host as on a fruit: the
  member's order record shows rec=2 with the tree, and the member walks there (the guest's mirror follows).
* Fix `tribe.fruit_order`: such an order takes the nearest tree with fruit left within 12 m, else the host's nearest
  fruit within 15 m.

## A member's jump and a velocity left at 120 m/s on the guest's mirror (2026-11-08, fix/w46-b1-tribe)
<a id="tribe-mirror-jump"></a>

BUG-0759 (not reproduced). GOG 2024, lane 7.
* ✔ (Ghidra) The sleep tick 0xDAF7B0 (node 0x04611FF4): state 0 walks the member into its hut's radius (hut vt 0x60) or
  to a point at its edge and stops it (spatial vt 0xEC); 1-3 play 0x2796B22 / 0x2796B25 and add health per tick (rate
  from a property, 0xF3FAAC05 with a heal tool, else 0xA1B01538); 4 ends with 0x2796B28. A member at full health leaves
  at once (node 0x05524115 next). No teleport.
* ✔ (runtime) On the way into the hut a member jumps ~5 m in one frame (its velocity reads 128.7 m/s for one state);
  the guest's mirror is at most 0.8 m off 0.5 s later. A `coop.tribe put` teleport of 10-11.5 m is followed within
  0.4 s (drive::step snaps over 8 m).
* ✔ (runtime) The velocity field (cLocomotiveObject +0x1C8 of the citizen) held at 113-120 m/s for 6-8 s on a standing
  member (`coop.tribe setvel`): the game does not move it (once 3 m, the frame the hold started on a member that had
  just stood); the guest's mirror stays at its place (0.0-0.7 m, also over a 100 ms link): the lead does not carry it
  ahead.

## The duet's preconditions: instruments, the social order, the performance node (2026-11-08, fix/w46-b1-tribe)
<a id="tribe-duet-test"></a>

BUG-0748 (no test yet). GOG 2024, lane 7, out/b1tribe/exp_duet.py (not in git).
* ✔ (runtime) Instruments bought the game's way (`coop.tribe buy <type> <m> <deg>` + `buy_end`) are taken by any
  member the host orders to grab them (`coop.tribe grab <id> <tool index>` on the host, also a guest's member): held
  4 s later. A tool from `coop.tribe build` (make_player_tool) was not taken in 45 s; `settool` changes the held type
  only.
* ✔ (Ghidra) A social click on another tribe's hut (DoActionGeneric 0xCD0C20, InputAction 0xE) costs food (property
  0xFD444753, default 10) unless a selected member's first tool has +0x12C == 5; it sets the mission card
  (0xCD3D70) and orders CitizenAction **9** on the tribe (0xF only shows hint 0x30D20036).
* ✔ (runtime) CitizenAction 9 on an NPC tribe with instrument holders, from the host or from the guest (taken by the
  host: rec=9 with the tribe), gives node **0x0462855E** (enter 0xDB00C0, tick 0xDBB7D0, exit 0xDB0950: the group walk
  and the food-bundle gift, ApplyRelationship 0x530CF06) for ~15 s, then idle nodes; never the performance 0xB46E8803.
* ✔ (Ghidra) The performance's session is made by the enter of node **0xF473F46A** (0xDA59A0): a "Social" group
  (0x18EB4B7) whose index goes to ai+0x600 +0x1A8 (what SocialSession_Get reads), stimulus 0x200000 to the member and
  0x100000 / 0x10000 to the members in cTribeUI +0x94..+0x98 (the UI's list). The node id is only in the BT action
  table (0x15933EC); its chooser is BT data. ? what enters it (a step of the social mini-game's UI is likely).
* Test commands: `coop.tribe duet` (performers: id, owner, the session's target, state, ui, progress `ui+0x68`;
  `ticks=`), `coop.tribe act_obj <action> <noun hex> <x y z>`, switch `coop.fix tribe.duet off`.
* ✔ CORRECTION (2026-11-08, fix/w46-b1-tribe): what starts the performance is found. In social mode with instrument
  holders selected, a right-click on a **member** of the other village (the game's hint "Right click on a tribe member
  to perform for them") gives **CitizenAction 26** on that member; the members enter 0xB46E8803 within 2 s, with a
  session on the village (state 0 -> 1 -> 3/5, without notes it ends at 6 after ~20 s) and the host's HUD shows the
  social meter. A guest's member ordered with 26 (sent to the host) joins the same session; duet_tick then fills the
  progress 0.08 a second (labs 20261108-161904, -163746). The host's right-clicks gave the order only with the chief in
  the selection (2 runs). The "? what enters it" above is answered by this; 26 was listed as "Recruit (social)" in §2.

## When a raided hut falls, and the conquest after it (2026-11-08, fix/w46-b1-tribe)
<a id="tribe-hut-fall"></a>

BUG-0761. GOG 2024, lane 7, `coop_tribe_conquer_orders.py`.
* ✔ (Ghidra) cCombatant::TakeDamage 0xBFCF10 (a hut's combatant at hut +0x120): health +0x38 -= damage; under 1.5e-5
  it sets health 0, **state +0x34 = 2** and posts the death message (0x1622184 block); +0x28 the last attacker. Damage
  type 10 adds instead (repairs), type 0xB nothing.
* ✔ (runtime) Raid blows on an NPC hut take 3.75-4.0, so from a written 40 the health can end at 0.25-0.5 with state 1:
  the hut stands until one more blow; the conquest cinematic TRG_TotemPole_Conquer (0x5418B038) plays 0-4 s after
  state 2 (4 runs). `coop.tribe huthp` prints `exact=` / `state=` / `last=`.
* ✔ (runtime) The raiders (`coop.tribe order <id> 15 <hut>`) can leave a hut that still stands (26/400 for 60 s, no
  raider within 14 m): the order again sends them back.
* (Ghidra, not finished) The conquest's completion is cTribeToTribeMission vt 0x78 0xC2F7F0 (state 5, the goal,
  the cinematic); a probe on it counted ~12 calls a second (it returns at once when the state is already 5), its
  caller was not found.

## Fork notes: graduation pen, growth, totem, level (2026-11-10, plan/w47-pr196)
<a id="tribe-fork-graduation-notes"></a>

From the fork PandizeLive/SporeCoop-Lineages PR #196 (addresses in its "canonical" notation, which it maps to the
GOG 2024 / Steam code; check against overrides_dc04aee5.sym before use), not verified by us:

* ~ Graduation (after the City Hall editor) hides village assets at 0xCDD461: it calls 0xC90060, which returns tribe
  +0x19C4 (mpDomesticatedAnimalsPen, ModAPI cTribe.h), then that object's vt 0x54 with argument 0 and no null check.
  A tribe with a null pen faults at 0xCDD466 (stack through SimCamera_Update+806). All 7 tribes of the fork's restored
  fixture had null pen and egg pen (+0x19C8). Ticket BUG-0856. Their guard matches the 11 bytes
  8B 10 8B C8 8B 42 54 6A 00 FF D0 at that site.
* ~ cCreatureCitizen GrowUp = vt +0x78 (0xC248C0, plain ret): base growth, adds the citizen's spatial to the
  selectable members, updates model, scale and adult AI flags, clears the baby hit sphere; pointer and species stay
  (BUG-0857).
* ~ Totem segment add 0xC8DDA0 thiscall(totem, int type, ColorRGB*) ret 8: appends to the type vector +0x11C and the
  colour vector +0x130, updates the attachments when the model is loaded; conquest 0xCDADC0+0xAF calls it with type 0;
  the factory 0xC992D0 makes a placed totem for a new tribe.
* ~ The tribe's level: the max-population method 0xC8EC70 calls 0xA1AC90, which reads community +0x38 (as our code
  uses, tribe.cpp `F<int>(pt, 0x38)`). The fork says ModAPI's +0x32C "upgrade" is not established as the live level:
  our +0x32C mUpgradeLevel above is the serializer's field name only, do not read it as the level.
* ~ The social mini-game UI's primary vtable is 0x14818B8 on the current build (the fork); the 0x14823F8 in the social
  section above was written before the GOG 2024 switch (build not named there). Our code reads only +0x68.
* ~ Native Tribal performance rounds (length, count, requests) come from AudioSystem events: a game started with
  -nosound never gets rounds (session state 4). Mute through the profile's volume instead (our lab does).
* ~ The cTribe constructor is 0xC98430 (not a stage cleanup); the native layout 0xC98940 reads the species at
  +0x18CC and faults at 0xCE6AE7 when it was cleared at unload (same site as BUG-0672, other trigger in the fork).

## What a member holds in its hand: the handheld item and its model (2026-11-10, fix/w47-tribe-looks)
<a id="tribe-held-item"></a>

* ✔ (Ghidra GOG 2024 + runtime lane 6) cCreatureCitizen **+0xFFC mCurrentHandheldItem** is what the game draws in the
  member's hand, not the tool type +0x102C. Items (the model switch of 0xC250A0): 0 nothing, 1 / 2 / 3 the racks'
  weapons of tool 1 / 2 / 3, 0xD tool 5, 0xE tool 4, 0xF tool 6, 0x10 tool 7, **0x11 the chief's staff (tool 0xB)**,
  0x12 tool 9 (the healer's rod); ~ 4 / 5 / 6 have model variants by leader and tool 8, 7..0xC and 0x13..0x17 are
  other models by id without GetTribeToolData (? what: carried food, fish). `GetHandheldItemForTool` 0xC23740 thiscall(citizen, tool) ret 4 maps a tool
  to its item (for the tribe's leader only tools 1, 2, 7, 8, 0xB give 0x11, the rest 0).
* ✔ **0xC250A0** (`SporeCoop::Citizen_SetHandheldItem`) thiscall(citizen, int item, int hand) ret 8: hand -1 = the
  member's own hand +0xFC4; detaches what that hand holds (0xC241A0), writes +0xFFC when the hand is the own one, and
  attaches the item's model to the attachment key `0x2735300 + hand` (0xC17EC0 -> 0xC150D0; the model part only for a
  member with a tribe, +0x1014). 46 call sites, most of them behaviour nodes (0xD7.., 0xD9.., 0xDA.., 0xDB..).
* ✔ 0xC26470 thiscall(citizen, int hand) ret 4: "the hand shows the tool": item = 0x11 for a leader with tool 0,
  else GetHandheldItemForTool(+0x102C); calls 0xC250A0 when the hand or the item differs. Callers: SetChieftain
  0xC8FEE0 (after SetSpecializedTool(0xB)), the grab behaviour 0xDAB4A0 (right after its SetSpecializedTool), five
  other nodes. 0xC255E0 thiscall(citizen, item, bool) ret 8 is the animated change (plays 0x2C39200 and attaches at
  the animation's event, through 0xC26540); 0xC26DF0 thiscall(citizen, mask, bool) ret 8 the gather node's.
* ✔ **SetSpecializedTool 0xC27B00 does not touch the hand**: it writes +0x102C, resets the ability indices (+0xE8C..)
  and calls vt 0xAC(1) = 0xC1CDE0, which rebuilds the abilities (not "refreshes the handheld").
  ~ superseded (2026-11-10): the lines above that say SetSpecializedTool "refreshes handheld (vt 0xAC(1))".
  CORRECTION (2026-11-10, fix/w47-tribe-looks): decompiled 0xC27B00 and 0xC1CDE0; at runtime a guest's mirror with
  tool 7 set by the tool field had +0xFFC = 0 and no model while the host's member had 0x10 (lane 6).
* ✔ `0xC0E110` (`SporeCoop::Creature_AttachmentOf`) thiscall(creature, key) ret 4: the attachment record of the
  animated body (creature +0xB54 -> +0x17C, 0x9CAA90), 0 without a body; record +0x8C = the attached model. With the
  hand's key it tells whether the hand has a model (`model=` of `coop.tribe`): 1 on the host's chief, 0 on the
  guest's copy before the fix.
* ✔ (lab 20261110-201619 key frames, 20261110-210216 with the fix off) A guest's mirrors run no behaviours, so
  nothing set their hands: the mirror of the host's chief (bound to the guest's own save's member: tool 0xB, leader
  1, **item 0**) stood without its staff; the mirrors with tool 6 / 7 (BUG-0514's tool field) were empty-handed. A
  mirror that leader_sync made the leader (SetChieftain -> 0xC26470) did get its staff: an NPC chief had one, the
  player tribe's had none. The host's members have item 0 at times in the host's own game (seen: the chief, away from
  the village after a rejoin, for more than 30 s; musicians with tool 6; ~ the gather and idle nodes put it away).
* ✔ **How the item is held: the timed modifier of kind 5.** A creature has 8 timed modifiers (creature +0xB48 -> a
  table of 8 entries of 0x44 bytes: +0x00 8 dwords of data, +0x20 u16 serial, +0x24 kind (-1 free), +0x28 a sub-kind,
  +0x2C a time, +0x30 / +0x34 / +0x38 functions (+0x34 the remove, +0x38 the tick), +0x3C the owner, +0x40; made by
  0xBCA980, ended by **0xBCA1B0** thiscall(table, u16 serial | index << 16) ret 4, `SporeCoop::CreatureModifiers_Remove`).
  The attach (0xC240E0, for an item with GetTribeToolData +0x30) adds one of kind 5 with the data {the attachment's
  index in creature +0xE40 (0x24-byte entries), the handle of the animation playing now, started 0}. Its tick 0xC11240
  starts the taking of the item on its first run (attachment +0xC count up, 0xC0FCA0, started = 1) and keeps the
  modifier while that animation is in state 1 or 2; its remove 0xC14A00 (count down, 0xC141D0 at 0) puts the item into
  the resting grip (the chief: the arm raised, the staff upright). The detach 0xC241A0 ends the hand's modifier too.
* ✔ (runtime lane 6, shots) In the host's game the attach comes inside a short animation (the change animation
  0x2C39200 or a behaviour's), so the modifier ends by itself: a standing chief has none (`hold=0`), one waving its
  staff has one for that animation. A mirror's animation is its idle loop: the modifier never ended and the staff
  stuck out forward with the arm down for as long as the mirror stood (seen in the first sheets with the field alone,
  lab 20261110-211339). The same on the HOST when a test called 0xC250A0 on its standing chief
  (`coop.tribe hand <id> <item>`), until `coop.tribe hand <id> end` ended the modifier: then upright at once. Ended
  before its first tick (started 0) the remove does nothing and the item stays without a grip (tried).
* ✘ Tried and dropped: the game's animated change 0xC255E0 (`ChangeHandheldItem`, thiscall(citizen, item, bool) ret 8)
  plus its second half 0xC26540 (thiscall(citizen, tool) ret 4, called by behaviour nodes every tick: attaches the
  tool's item to the hand the animation's channel 0x7D7502FC names): on a mirror the event attach came once in 15
  tries (the change animation is not the mirror's current one for long).
* Fix (BUG-0906, switch `tribe.held_item`): the item is a replicated field of the members (fields.h kind
  `tribe_member_item` 9, field `item` 5, on the wire item + 1), applied with 0xC250A0(item, -1) whenever the mirror's
  +0xFFC differs; four times a second a mirror's started kind 5 modifier that has lasted 1.5 s is ended with the
  game's remove (tribe.cpp held_items_tick). Off puts the mirror's own item back. `coop.tribe` rows print `item=`,
  `model=` and `hold=` (1: a started kind 5 modifier, the item still held as in the middle of taking it).
* ? Not done: a mirror does not play the host member's staff or instrument animations through this (those are the
  animation relay's); during them the host has `hold=1` and the mirror the resting grip.
* ? Not checked: the look of carried things (items 0x13..0x17: fish, fruit) on the mirrors, which the field now
  copies too; the hand of the guest's own members in the host's game is the host's own state (nothing to copy back).

## The size of a held item (2026-11-10, fix/w47-tribe-looks-2)
<a id="tribe-item-size"></a>

* ✔ (Ghidra GOG 2024) The attach 0xC250A0 sizes the item's model once: `s = (bbox[+0x14] - bbox[+8]) * *(float*)
  0x1581334` with the bbox of the member's spatial vt 0x68 (0xC250F9), then model vt 0x20(Transform*) reads the
  model's transform, the scale (+0x10 of the 0x38-byte Transform: u16 flags, u16 change count, offset, scale, 3x3)
  is set to s, the count raised, model vt 0x18(Transform*) writes it (0xC2552F..). Model = the hand's attachment
  record +0x8C. Citizen vt 0xE0 (0xC23360) returns an id for model vt 0x44(5, id, 3), not a size.
* ✔ (lab 20261110-221944) The model keeps that size when the member's scale changes later: the host's chief
  (size 2.844 after the mod's 1.3x) holds a staff of scale 2.188, its own size at the attach. A mirror that got the
  staff after it had the host's size had 2.845 (BUG-0912).
* Fix `tribe.item_scale`: the model's scale is a replicated field (kind `tribe_member_item_scale` 10, field 6),
  written the same way; `coop.tribe` rows print `iscale=` and `scale=`.
* ✔ A player's chief's `chief_scale` ratio (coop.status) is its size over a NEW GetDesiredScale pick noted by
  chiefs_tick: 1.30 on the host by construction, no measure on a guest, whose mirror has the host's size
  (BUG-0911).

## A partner's name in Tribe: the chief and the members, not the pointer (2026-11-10, fix/w47-tribe-looks)
<a id="tribe-name-tags"></a>

* ✔ The Tribe (and Civ) adapter's "puppet" of a partner is no object: a marker at the planet point under the
  partner's mouse (modes.cpp rts_fill / mouse_on_planet; with input.window the last point the pointer moved over in
  the game window). coop.cpp's overlay drew the partner's bare name 0.5 m over that point: a name over empty ground
  wherever the mouse rested (BUG-0907: at the fire pit in same_view_tribe, on a slope in play_tribe's start frame, lab
  20261110-201619). The members' own labels (draw_owner_tags: the owner's name, "<name>'s chief" for its chief) were
  right all along.
* Fix (switch `tribe.pointer_tag`): in Tribe the pointer gets no name; the partner's labels are its chief's and
  members', and the arrow of F2 when the chief is off screen. Civ keeps the name at the pointer (there it is the
  "where the partner plays" mark the arrow also points at). `coop.find labels` lists the labels of the last overlay
  pass (kind player / chief / member / city / vehicle, the player, the screen point, the world point, the text).

## The roster portraits of a species the host made in the session (2026-11-11, fix/w47-roster-portraits)
<a id="tribe-portraits-session-species"></a>

BUG-0921 (BUG-0643 again, the case players have). Lab lanes 7 and 8, `coop_roster_portraits.py`.
* ✔ (runtime, lane 7, Cell -> Creature -> Tribe with 2 players) The guest's player tribe has the host's species key
  (291E2509 on both; `coop.tribe`), and the roster asks the png {0x2F7D0004, 0x40626200, that key}: the host's game
  answered it (`coop.creations png 291E2509` known=1), the guest's did not (known=0) and drew every member slot as a
  plain white square (no red cross: that is a png that is there and broken, #tribe-portraits). The guest did have the
  picture, under the content key of the host's own species only (7D573565, own_species_*.package).
* ✔ (code + runtime) Why the BUG-0643 fix missed it: the species of the host's tribe is normally the creature the
  host plays, and that one reaches a guest twice. First as the host's creation bundle (share_own -> make_bundle,
  "player 0 creature = ... (2 records)": bem and metadata), which the guest registers under the host's own key at
  priority 2000 (world_*.package). Then as an own species of the host's world (host_species_out, 4 records with the
  png, `creations.png`): apply_own_species puts it under the content key 0x7D...... and under the host's key only
  when nothing answers for that key (`!key_exists`), and the bundle's package already does. So the host's key had
  a model and no picture. tribe_portraits (BUG-0643) passed because its species is another creation of the host
  (`coop.tribe setspecies`), never sent as a bundle.
* Fix `creations.bundle_png`: make_bundle takes the creation's pngs too (the model group and the thumbnail group
  +1, up to 1 MB each; a bundle of pictures without a bem is sent without them). The world package of the host's key
  then answers the picture, for every version of the host's creature. Off: as 0.7.11.
* CORRECTION (2026-11-11, fix/w47-roster-portraits) to #tribe-portraits "the game keeps a picture it drew for the
  whole process": that holds for a png that was found (also a broken one). A png that was NOT found is asked again:
  in the same guest process, after the picture was registered and the world loaded again (a rejoin), the white
  squares became portraits (lane 7, hot reload of the fixed coop.dll into the running games, out/lane7/shots
  probe_g / probe2_g by eye). ? Whether the slots of a roster that is already on the screen pick the picture up
  without a load was not tried.
* ✔ (runtime) The host's species made by the Cell -> Creature change has one png in the host's My Creations (group
  0x40626200; `coop.creations png <key> 40626201` known=0 on the host) before the first editor is over: the 4
  records of host_species_out are sent 1 s before the host's editor opens.
* The picture check of the test: a roster slot is found on a full-size 1440x1080 shot by its frame, a gold row
  (about 253 224 163) across x 1403..1429; slots are 36.4 px apart from y 646 on (the chief's icon first) or lower
  with fewer members; the picture is the 21 rows below the frame's 2 rows, x 1407..1428. A white square is 100 %
  pure white there, a portrait 0 %. On the half-size shots of `twoinst.shot` the gold row blurs: one slot of three
  was missed.
## A destroyed village and its goal mission: the fault storm at 0x8E8230 (2026-11-11, fix/w47-getfilecount)
<a id="tribe-gone-village-mission"></a>

BUG-0942. GOG 2024, Ghidra (private project) and lab lanes 2 / 3. Adds to #tribe-wsync and #tribe-realign.

* ✔ `0x008E8230` is **not resource code**: it is the 4-byte getter `mov eax,[ecx+0x34]; ret`, shared by the linker
  with every class that has such a getter; the ported name `Resource::PFIndexModifiable::GetFileCount` is one of its
  owners (docs/re/creature.md 14.1: the cCombatant state). A fault there with `read 0x154` and ecx 0x120 is
  `((cCombatant*)(null + 0x120))->state`: the combatant of a null cTribeHut. The nearest-name line of the fault
  report ("cTribe_SetChieftain+0x861 < SpeciesGoal_Refresh+0x4B9 < ...") is far past those functions and misleads.
* ✔ The chain `00C90741 00C30069 00CD3770 00BA266E 00BA1EE0 00B5D229 00CDF46C`:
  0xC90730 `cTribe::HutIsDead`: `[tribe+0x368]+0x120 -> 0x8E8230 == 2`, **no null check of the hut**
  < 0xC30000 the goal card's text (`SporeCoop::TribeMission_RefreshCard`, thiscall(cTribeToTribeMission), mission vt
  0x80 via the thunk 0xC301E0): relation of mission+0x80 (the village) to the player picks prop 0x57E2F080 (attack)
  or 0x3D61DF6C (befriend); then `SporeCoop::cTribe_PopulationCount` 0xC8F530 == 0 and HutIsDead -> prop 0xF1E0DD24
  (the conquered text); the prop lists go to mission +0x84 / +0x88
  < 0xCD3740 the Tribe Mission Manager's update (vt 0x10 of vtable 0x14772CC): every mission's vt 0xB8, then **while
  the dirty byte +0x70 is set**: vt 0x80 of +0x68 and of +0x74 (the current mission), 0xB6F750, and only then
  `+0x70 = 0` < 0xBA2550 the simulator's subsystem loop (vt 0x10 of each) < 0xBA1ED0 < 0xB5D0F0 < 0xCDF080.
* ✔ So one fault becomes a storm: the game's handler unwinds the update before the dirty byte is cleared, the next
  frame runs the same refresh again (813 faults in 14 s on the campaign guest, 1302 in 20 s in the lab), until the
  current mission changes (the campaign: the goal row moved the card to a village of the host's world).
* ✔ The dirty byte: `SporeCoop::TribeMissionManager_Refresh` 0xCD38E0 (manager vt 0x28, thiscall(mgr, pol or 0) ret
  4) sets it (0xB6F5E0) and, with a pol, calls vt 0x80 of the map's mission of that pol at once. SetTargetTribe ends
  with it; the game's relation and mission events call it. ? Which event set it 12 s after the realign in the
  campaign was not traced (the test asks for it with `coop.wsync goal dirty`).
* ✔ `SporeCoop::TribeMissionManager_RemoveMission` 0xCD37F0 (manager vt 0x30, thiscall(mgr, mission) ret 4): when
  the mission is +0x68 or +0x74, its card leaves the HUD panel (0xE17F90) and the pointer is released; the vector
  +0x14 and the map +0x78 keep the mission.
* ✔ (runtime, lab 20261111-131344) `cGameNounManager::DestroyInstance` of a cTribe in the Tribe stage
  (`tribe::remove_village`, the guest's realign) leaves its cTribeToTribeMission as it was: the mission holds the
  tribe (intrusive, the block stays readable), `coop.wsync dump tribe_goal` still shows it as the target with
  `card=1`, the tribe's hut pointer +0x368 is null and it has no members. The game itself never destroys a tribe in
  the Tribe stage (a conquered village stays, with a dead hut: that is what HutIsDead is for), so nothing in the game
  removes such a mission. CORRECTION (2026-11-11, fix/w47-getfilecount) to #tribe-realign "no fault in the 40 s
  after": true only while the goal card is not on the removed village or no goal event comes.
* Fix `realign.village_goal` (wsync.cpp): `wsync::village_gone(tribe)` from remove_village removes the current
  mission of that village with RemoveMission outside the frame (the HUD card goes; the goal row puts the host's goal
  there when the host has one), and a hook of 0xC30000 skips the text of a mission whose village has no hut and no
  members (the missions left in the vector and the map, reached by Refresh(pol) or a later SetTargetTribe).

## A blow throws a member up; what else holds a member over the ground (2026-11-11, fix/w47-final12c)
<a id="tribe-member-toss"></a>

BUG-0945, BUG-0946. GOG 2024, tribe fixture, lanes 2 and 5 (probes, then tribe_member_air: lab 20261111-182334).
✔ = observed at runtime. Heights are over the terrain (cPlanetModel::GetHeightAt), read 4 times a second.
* ✔ A blow of 60, 100 or 140 hp on a player-tribe member that lives through it (`coop.tribe pincer <victim>
  <attacker> <dmg>`: the combatant's TakeDamage vt 0x18 with a zero push vector, the attacker another member) throws
  the victim up while its node is the fight node 0x05218BB3: 3.1-3.7 m a quarter second after the blow, moving up at
  5.1-6.2 m/s, 4.96-5.04 m at the top, over 3 m for 1.3-1.5 s, on the ground again within 3 s. A blow of 20 hp does
  not lift it over 3 m. A killing blow (5000 hp) lifts it 3.4-3.7 m before it lies down (node 0x05CA7FA7).
* ✔ Only on the game that runs the member: the guest's mirror stays within 0.06 m of its own ground during the
  throw (the mirrors keep their own height, BUG-0329 / BUG-0444). BUG-0946.
* ✔ A wounded member (`coop.tribe sethp <id> 1`) in the sleep node 0x04611FF4 heals at its hut on the ground
  (0.02 m, +6 to +9 hp a second), then node 0x05524115. A knocked-out leader (dead flag, node 0x05CA7FA7) lies on
  the ground and stands up there after about 95 s with full health.
* ✔ A member put onto deep water (`coop.tribe put` to a point whose sea bed is 25 m under the village's ground)
  rises to the water's surface within 3 s and floats there, 15.2 m over the sea bed, standing in the idle node
  0x04601D70 for 45 s and more; the guest's mirror floats the same way (its own game's water). A member that was hit
  just before (fight node) swims back to the land at 12 m/s at once. The tribe fixture's water surface is at radius
  496.9 m; the campaign planet Cadian's at about 499.7 m (a swimming creature's radius).
* ✔ In 30 min of play_tribe (three lanes) and the Tribe plays of two four-player campaigns no member was over 3 m
  once: the throws need blows harder than the fixture's animals deal.
* The log lines for it: `tribe: member #<id> of player <p> is <m> m over the ground at (...): bt=<node> hp= dead=
  age= tool= item= leader= speed= (up <m/s>) over the game's surface <m>` and `tribe: member #<id> of player <p> is
  down again (or gone) after <s> s over 3 m, highest <m> m` (coop.cpp tribe_air_watch); the trace's `agl_held=`.
* ? Not checked: which of the game's own attacks reach 60 hp on a member (an epic's and a rogue's are likely), and
  whether the throw is the fight node's own reaction or the locomotion's (mode +0x2B0 = 3, docs/re/creature.md
  "thrown").

## A conquest on one game only: the conquered flag, the hut's fall and an away conquest (2026-11-12, fix/w48-captures)
<a id="tribe-conquest-sync"></a>

BUG-0777, BUG-0879 (Discord 2026-11-09). GOG 2024, Ghidra (private project) and lane 5, tribe fixture. Adds to
#tribe-hut-fall and #tribe-wsync.

* ✔ (Ghidra) **cTribe +0x556 is the village's "conquered" flag.** Its only writer besides the ctor (0xC98430 clears
  +0x555..+0x557) is `SporeCoop::cTribe_SetConquered` 0xC8E8C0 (view effect 0x1046F5EF, the village's bundles go).
  Its caller is the Tribe stage's message handler 0xCD81F0 on message **0x58BADDD** (a village defeated: +8 the
  village, +0x10 the conqueror's tribe), which also plays effects at the hut and updates the HUD.
* ✔ (Ghidra) 0x58BADDD is posted by the hut combatant's own TakeDamage `SporeCoop::cTribeHut_TakeDamage` 0xC9CA30
  (vt 0x18 of the hut's combatant at +0x120) when a blow takes the hut's health from above 0 to 0 in Tribe; members
  standing or not. With an attackerPol other than -1 it first applies relationship 0x530CF05.
* ✔ (Ghidra) The goal mission's update `TribeToTribeMission_Update` 0xC2F750 (vt 0xB8) ends the mission
  (`TribeToTribeMission_Complete` 0xC2F7F0, vt 0x78) when the relation is 4 (allied) or +0x556 is set: kind 2 allied,
  kind 1 conquered (AddGoalProgress, the history event 0xBA59E0(village, 6), TRG_TotemPole_Conquer). This answers
  "? its caller" of #tribe-wsync and #tribe-hut-fall.
* ✔ (code + runtime) realign kept a guest's hut at 1 hp at least (`max(1, host's)`: "the guest's game runs no
  defeat"), and the rows had no dead state: a village the host conquered stood unconquered on the guest (hut whole,
  +0x556 0). Fix `realign.hut_fall`: the rows carry Obj.fl bit 1 (`kFlDown`: a hut's combatant state 2, a village's
  +0x556); a guest whose village (or hut) is down on the host and not here takes the game's own killing blow on its
  hut (`tribe::fell_hut`: vt 0x18 with the hut's health + 10, attackerPol -1, the attacker a member of the player
  tribe), or cTribe_SetConquered when no standing hut is left; a dead hut here takes no health write.
  Lab 20261112-004329: on the guest the hut fell (state 2) and +0x556 was set in the same second as on the host, the
  hut burns on both screens; the old matching ignores the new bit (match() compares kFlPlayer only).
* ✔ (code) While the host is away the guest's own game raids and conquers (tribe.away). Fix `tribe.away_conquest`:
  the guest watches every NPC village's +0x556 twice a second; one set while the host is away goes to the host as
  MSG_TRIBE_CMD kind 4 (CMD_CONQUER, the village's position), held by the host until it is back in Tribe (BUG-0693),
  which fells that village's hut with a blow by a member of that player (`coop.tribe conquests` counts).
* ? Alliances (relation 4) were not run: the guest gets the relation through wsync.tribe_rel (ApplyRelationship), so
  its own mission update should end the mission as allied by itself; not checked.
## A removed citizen's block and the mirrors (2026-11-12, fix/w48-freed)
<a id="tribe-mirror-citizen-reuse"></a>

BUG-0844 (class BUG-0426). GOG 2024, runtime on lanes 6/7 (`coop.tribe reusehunt`, docs/06-testing.md#testing-freed-block-reuse).

* ✔ A citizen removed with its tribe's RemoveMember (vt 0x9C, destroy = 1) is freed later, like a creature
  (docs/re/creature.md#creature-freed-block-reuse); a member made afterwards in **another** tribe (the guest's player
  tribe, SpawnMember vt 0x98) got its block in 1 of 8 hunts of 150 frames (12 tries) in one run, 1 of up to 16 in
  others: rarer than for animals, but real.
* ✔ object_alive (+0x21) and the object list both accept that new member for the old mirror; only the serial
  (+0x24) tells it apart. With the old check the guest drove its own new member onto the host's member of the other
  tribe once the host's states came again (lab 20261112-010419).
* ✔ The member vector (+0x340) of the live tribes is the safe way to find the tribe that has a citizen
  (`tribe_holding`); the citizen's owner field (+0x1014) is read only after the citizen is a listed object.
## A save in the end of Tribe faults in the serializer (2026-11-12, fix/w48-saves)
<a id="tribe-graduation-save"></a>

* ✔ (runtime, lab 20261112-021437, `tribe.graduation_save` off) Back from the City Hall editor (the player tribe
  without its food mat, `tribe::stage_end`), a guest that joins makes the host post a share save 5.5 s into the
  stage; 1.2 s later the host's game faulted: `0xC0000005 at 0x00BCAB07` (data 0x60C), the address the fork
  PandizeLive/SporeCoop-Lineages saw (its canonical 0xBCAB07, BUG-0849). The game's exception handler kept the game
  running, no saved box came ("no saved box 10013 ms after the save: sharing anyway") and the slot got one more file
  (11 instead of 10): a half-written save went to the guest. In Civ after it, four share saves in a row wrote nothing
  ("no saved box and no file written", the game's save name 'snapshot.spo'); the guest got the world only by
  "sharing anyway" 43 s later.
* Rule (coop.cpp `tribe_graduating`): no share and no share save while the game is in Tribe and either
  `tribe::stage_end()` or the last editor request was one of the end of Tribe (City Hall 0x99E92F05, vehicles
  0xF670AA43 / 0x9AD7D4AA / 0x7CB21BAD); the return from those editors is not an evolution. The request id is
  forgotten on a load, the menu or another stage.
* Without a join our code made no share save in that time in the earlier runs: since BUG-0508 the return from the City
  Hall editor goes "in place" for a guest in the same Tribe world (tribe_civ_ground 0.7.9 lab 20261106-231404:
  "evolved in place: Bob keeps its world" after the 120 s cinematic hold).
* ✔ A second FAIL run (lab 20261112-023150): the same fault 0xBCAB07 1.2 s after the join's share save. With the fix
  the share for the joined guest waited (refused saves) and, before 138a239e, held the vehicle editor's request 15 s
  (`share_save_due`); now the end of Tribe does not count a share that cannot save (the editor 0.4 s after its vote,
  lab 20261112-022822).
## Mirror citizens: the other users of a kept mirror, and the host's member table (2026-11-12, fix/w48-freed2)
<a id="tribe-mirror-other-users"></a>

BUG-0955, BUG-0951 (class BUG-0426). GOG 2024, runtime on lane 7 (tribe_mirror_lifetime) and code read. Follows
#tribe-mirror-citizen-reuse.

* ✔ BUG-0844's fix checked the mirror's citizen in guest_state, guest_tick and the snapshot's end only. The other
  users of `g_gm[id].c` still asked `object_alive`: the field replication (`member_mirror`: scale and leader), the
  behaviour animation relay (MSG_NPC_ANIM kinds 2 / 3), the held items, `mirror_of` (mirror_fx's effects), the ring's
  selected member, `for_each_tracked` / `for_each_diag` and the sync-check. In the test's statehold (the host's states
  paused, as in its editors) a freed mirror citizen kept getting fields and animations: the guest's game caught
  `0xC0000005 at coop+0x13E88F` (`game::object_scale` -> `game::cast` vcall on the freed block) and in the same frame
  run `SporeCoop::CreaturePlayAnim+0x4F` with the same object (esi 1E1684C0), then 4 more cut-off frames 0.25 s
  apart (lab rel/0.7.13 20261112-041619-2 #3, 6 faults); another run faulted executing the freed block's vtable from
  UTFWin paint (0x24D2F858, 10 faults, 20261112-031338). Now all of them ask `mirror_ok` (listed + serial).
* ✔ (code read) The host's member table `g_hm` is keyed by the citizen pointer and rebuilt from the live tribes'
  member vectors every cycle (0.12 s); a member the game removes leaves its vector in that call and is destroyed
  (+0x21) 10-40 frames before it is freed, so the next cycle drops it. Only a new citizen at a freed member's address
  before that (the host paused in between) took the old id and owner; the entry now keeps the serial and such a
  citizen is a new member (`tribe.kept_refs`). The dead bodies' branch (BUG-0620) asks the same.
* ✔ (code read) `g_detached` (the bodies taken out of their tribe on a guest) is exact: the citizen teardown hook
  erases an entry before the game frees it. The target ring (`g_ring`, up to 120 s), the epic (`g_epic`) and the
  babies the host's game made (`g_baby_made`) are asked in every frame (the HUD's update, epic_tick) or once: the
  destroyed flag is seen before the free; they keep the serial now anyway (`tribe.kept_refs`).
* ✔ The HUD's target ring map is keyed by the target's serial (+0x24, `ring_shown`): a ring drawn for a new object
  at the old address would be keyed by the new one's.
## Whose purchase a baby is: the hut effect in every game (2026-11-12, fix/w48-tribe-civ)
<a id="tribe-baby-buyer"></a>

BUG-0824. GOG 2024, lab lane 2 (tribe fixture), follows #tribe-guest-baby.
* ✔ (runtime, `coop.effects log on`, lab 20261112-084158) A left click on the own hut starts the world effect
  **0xDB6F2233** at the hut (the "sent world id" of the clicking game); the world effects relay plays it in every
  partner's game. The stage's message 0x63BBC42 (TribeStage_BabyBorn) follows **5.16-5.20 s** after the effect's start
  in every game that plays it, the own and the relayed copy alike: every game makes one baby per purchase of any
  player. The baby is born ~10 m from the effect's place (effect (408.7 -23.3 298.5), baby (407.9 -14.0 300.9)).
* ✔ (Ghidra asm) The message handler 0xCD81F0 is thiscall(stage, uint32 id, void* data) ret 8; for 0x63BBC42 it
  copies the place from `[data+0x28]+4..+0x10` and calls 0xCD74A0 at 0xCD8ECE (return 0xCD8ED3,
  `SporeCoop::TribeStage_BabyBornMsg_Ret`). `data` is a message on the main thread's stack (0x12D1C4 in every call);
  `[data+0x28]` is a heap Transform (first dword 0x00010006: flags 6, counter 1), not inside the effect object
  (it is not within 0x400 bytes of any effect the relay created): the message does not say which effect sent it.
* ✔ (runtime) The game's effect creators report one hut effect twice (the same object in two hooks in one frame).
* Consequence before the fix (lab 20261112-083858): a guest's game took the host's relayed purchase for its own
  (tribe.guest_baby sent CMD_BABY) and the host paired its own baby with that ask: every member the host bought was
  the guest's. Fix `tribe.baby_pair`: each game notes the hut effects that start in it with their player (effects'
  world-start observer) and gives each baby of the message the oldest noted purchase.
## A mirror made in the middle of a baby's growth (2026-11-12, fix/w48-tribe-civ)
<a id="tribe-mirror-grow"></a>

BUG-0857. GOG 2024, Ghidra and lab lane 2. Verifies the fork's note in #tribe-fork-graduation-notes.
* ✔ (Ghidra) cCreatureCitizen vtable 0x146AD00 slot 0x78 = **0xC248C0 GrowUp** (fastcall/thiscall, plain ret). It
  calls the base 0xC223A0 (sets **+0xB34 = 1**, the adult's model and scale, health scaled to the new maximum,
  +0xBD = 1), then for a citizen of a tribe (+0x1014) adds it to the tribe's selectable members (0xC95E50), its colour,
  the TribeUI's member list (vt 0x6C), for the player tribe the event log line 0x11C9AD29/0x182CD6CE, a BT stimulus
  0x200; destroys the baby's extra object +0x1030 and plays effect 0x3A616FEE/0x715FB4A7. No caller in the code: it
  is reached through the vtable only.
* ✔ (runtime, lab 20261112-090138-2) A baby grows up 57-59 s after SpawnMember(0). A guest that rejoins 12 s after the
  birth has its mirror made again as a baby whose own timer starts then: it grew up 6 s after the host's. With
  `tribe.mirror_grow` the bound mirror is grown through vt 0x78 in the snapshot that says adult (the same second).
## Members never change tribes (2026-11-12, fix/w48-tribe-civ)
<a id="tribe-member-moves"></a>

BUG-0841. GOG 2024, Ghidra (private project) and lab lane 2/3.
* ✔ (Ghidra) `cTribe::AddMember` 0xC95F00 thiscall(tribe, citizen) ret 8 (null citizen: +0x338 robo population + 1;
  a citizen already in +0x340 returns) is called by SpawnMember 0xC983C0, by the population set-up 0xC9A290 (vtable
  0x1473E64, vt 0x8C: CreateInstance(0x18EB4B7), vt 0xE8 (0, tribe, 1, ...), then AddMember up to the count) and by
  the thunk 0xC97010 = AddMember(c, 1), whose callers make or place new citizens: 0xACA2A0 (the noun manager's
  placement of a citizen made outside Tribe mode: a nearby tribe within twice its radius takes it), 0xAD11E0,
  0xAD2260. `RemoveMember` (vtable 0x1473E74, vt 0x9C) 0xC96070 thiscall(tribe, citizen, bool destroy) ret 8 detaches
  (SetTribe(0)) or destroys; no caller hands the citizen to another tribe.
* ✔ (runtime, `tribe_member_moves`) An alliance (relation 10) and a conquest (+0x556) leave the player tribe's
  member vector as it was; the host's table saw no member in another tribe (`coop.tribe` `member_moves=0/0`).
## The party music and the editors (2026-11-12, fix/w48-tribe-civ)
<a id="tribe-party-stop"></a>

BUG-0890. GOG 2024, lab lane 3, follows the BUG-0646 notes (tribe.cpp, the party music relay).
* ✔ (runtime, lab 20261112-085745) The host's game keeps playing the fire-pit party music (0x19DC06DF) through its own
  editor (60 s in the editor, still playing); the party's exit (StopAudio 0xD78F80) runs only after the host is back
  in Tribe. So the host's stop is sent from Tribe in practice.
* ✔ (runtime, lab 20261112-090930) A guest in its own editor (`coop.vote editor` on the guest opens it at once; the
  host stays in Tribe) misses that stop when MSG_NPC_ANIM is dropped outside Tribe: its track played 35 s on after it
  was back. `tribe.party_stop` takes kind 8 in every mode.
* `coop.vote editor` on the host in Tribe opens the host's editor at once and gives the guest no ready button and no
  offer; on the guest it opens the guest's own editor at once, the host plays on.
## Graduation's pen call and the villages' pens (2026-11-12, fix/w48-tribe-civ)
<a id="tribe-graduation-pen"></a>

BUG-0856. GOG 2024, Ghidra and lab lane 3. Verifies the fork's note in #tribe-fork-graduation-notes.
* ✔ (Ghidra) The sequence at 0xCDD461 is in 0xCDD0F0 (fastcall, its only caller 0xCDE7E0), which starts with
  `GetPlayerTribe`, destroys the tribe's food mat (+0x260), calls 0xC90600, then 0xC90060 (`return tribe+0x19C4`)
  and that object's vt 0x54(0) without a null check: **the player tribe's pen only**.
* ✔ (runtime, `tribe_pens`) The player tribe has a pen and an egg pen (+0x19C4, +0x19C8) in both games, on the tribe
  fixture, after the game forms a village and after a guest's rejoin. **Every computer village has both null** in the
  game itself (the fixture's and newly formed ones): null pens there are normal.
## Mirrors that look like the host's members: hunger, throws, idle poses, the hold of an item (2026-11-12, fix/w48-tribe-mirrors)
<a id="tribe-mirror-looks"></a>

BUG-0935, BUG-0946, BUG-0936, BUG-0914. GOG 2024, Ghidra (private project) and lanes 4 / 5, tribe fixture. Follows
#tribe-held-item and #tribe-member-toss. `coop.tribe` rows print `maxhp= hunger=<+0xBBC>/<+0xBB8>/<+0xB90>
idle=<+0xE3C> anim=<current>`; `coop.tribe anims` rows `idle=` (the host's last idle-refresh play) and `hold=`, and
the counters `air`, `idle`, `hold`, `hunger_sets`.

* ✔ (Ghidra) **cCreatureBase +0xBBC is mHunger** (0..100; the serializer's names: "mHunger" +0xBBC, "mHungerDelta"
  +0xBB8, "mHungerDelayTimer" +0xB90). Getter 0xC0BB20, clamped setter `SporeCoop::Creature_SetHunger` 0xC0BB30.
  The update `SporeCoop::Creature_HungerUpdate` 0xC0D5A0 (a vtable slot, every frame) adds the delta (-0.28 a second
  at runtime) times dt; below 1 the creature starves: TakeDamage of its combatant (+0x5A8 vt 0x18) every frame. The
  eat 0xC22C40 (the behaviour tree's, from the tribe's food) raises it with health and takes the tribe's bundles.
* ✔ (runtime, lane 4, 5 min) The host's members eat (99.8 after 4 min), a guest's mirror never does (30.3 then; 0 in
  about 6 min): the roster card of a starving member is red with an empty bar (BUG-0935: the guest's cards red after
  minutes of play, the host's well). The mirror's own starvation blows are undone by the snapshot's health.
* Fix `tribe.mirror_hunger`: hunger is a replicated field (kind `tribe_member_hunger` 11, field 7), applied with the
  setter when the mirror differs by 2 %. Lab: the guest's own update had its mirrors at 65-67 when the host's ate
  (100): "member #1's hunger 66.1 -> 99.9 as in the host's game".
* ✔ (runtime) A thrown member (#tribe-member-toss) can keep the grounded byte +0x137 for part of its flight, so the
  host sends every member more than 0.5 m over its ground (or `drive::body_airborne`) with that height (MSG_NPC_ANIM
  kind 9 {u16 id, f32 agl} once a cycle). Fix `tribe.mirror_air`: a guest places the mirror at that height over its
  own ground (drive::air_place, as npc.air) while the news is under 0.35 s old. A member floating on deep water
  (15.2 m over the sea bed) is sent too and lands on the guest's water as before.
* ✔ (runtime, lane 4/5, `coop.tribe anims` of both games twice a second) The animations of members at rest: the
  relay (BUG-0569) starts the host's behaviour animations on the mirrors, the idle refresh (Creature_IdleRefresh,
  plays its idle / walk choice; never relayed) is each game's own. A behaviour animation the host's tree cuts short
  (the idle variants 0x02C39001..3, the gestures 0x04C6CB02 / 05, the work 0x03CDACC0) went on to its end on the
  mirror while the host's member already stood (0x02481DE5) or walked (0x04866DAB / 0x04866D8E): with the host in its
  own idle choice, the mirror played the same or its own idle / walk in 30 % of the samples (probe, 3 min). The BT's
  idle override +0xE3C was -1 on every member sampled.
* Fix `tribe.mirror_idle`: the host notes the idle refresh's plays per member (game.cpp's idle listener) and once a
  second sends what each member plays that is not its behaviour animation (kind 10 {u16 id, u32 anim, u8 1 idle
  choice | 2 another start}); a guest mirror that plays something else ends it (not within 1.5 s of a relayed start,
  at most every 2 s): its own idle refresh (forced) for an idle choice, else the host's animation; the locomotion's
  idle / walk ids are never played as main animations. Same samples: 96 % (tribe_mirror_idle).
* ✔ (runtime) The host's members take their item into the hand inside behaviour animations (`hold=1`: the chief's
  gestures 0x04C6CB02 / 05, work 0x03CDACC0, 0x02DF4B12, 0x066E50FC, the change 0x02C39200), each for 0.5-2 s. A
  mirror plays the animation through the relay, but only some take the item themselves (the change animation's
  event); for the chief's gestures the mirror's staff stayed in the resting grip (BUG-0914).
* Fix `tribe.mirror_hold`: the host sends each new hold of a member (a new kind 5 modifier handle) with the animation
  playing (kind 11 {u16 id, u32 anim}, unreliable, every hold again once a second) and each end (anim 0, reliable);
  the guest attaches the item again (Citizen_SetHandheldItem) while its mirror plays that animation (also right after
  the relay starts it, and four times a second while a hold is pending), so the modifier is bound to that animation
  and ends with it or with the host's end; held_items_tick (BUG-0906's 1.5 s end) leaves such a hold alone for up to
  8 s while the animation plays.
* ✔ (lab) The rate of natural holds the mirror shares is no measure: with the fix off 60-70 % of the host's holds had
  the mirror holding in the same animation (the work 0x03CDACC0 and the change 0x02C39200 take the item at their own
  events on the mirror too), 78-80 % on. The chief's staff in its gestures: 0 of 3 off, 3 of 3 on (`coop.tribe hand`
  rounds); key frames: upright in the resting grip on the guest off, in the hand on both on.
* ? The mirror's staff angle in the hand is the mirror's animation phase, not always the host's (the relay starts the
  same animation 0.1-0.3 s later).
## A village's second ground stamp, and the game over with a chief standing up (2026-11-12, fix/w48-tribe-mirrors)
<a id="tribe-village-load-stamp"></a>

BUG-0947, BUG-0922. GOG 2024, Ghidra (private project) and lane 5, tribe fixture (tribe_form_ground_rejoin,
tribe_gameover_partner).

* CORRECTION (2026-11-12, fix/w48-tribe-mirrors) to #tribe-terrain-stamps and the BUG-0332 / BUG-0947 notes: the
  `from 0x...` callers that `coop.terrain` and the `terrain:` log lines printed were 0x400000 too high (terrain_diag's
  caller_of added the image base's shift once more). "0x0109DFBF" is 0x00C9DFBF, "0x01091981" 0x00C91981, "0x010DDDEF"
  0x00CDDDEF, "0x0106AD74" 0x00C6AD74 (Ghidra put the old values inside Havok code). Fixed; the stamp lists and ids
  of those notes stand.
* ✔ (Ghidra) A village has two ground stamps: `SporeCoop::TribeVillage_FormStamp` 0xC9DEE0 (obj +0x248 the id, the prop
  list of obj+0x34's vt 0xAC +0x90: the "0x0109DFBF" one, made with every village) and the tribe's own
  `SporeCoop::cTribe_ApplyGroundStamp` 0xC91890 (cTribe +0x19D4 the id, the prop list +0x2D4, key 535DC193 / 2BE764E2
  at runtime: about 50 m wide), applied only when +0x19D4 is 0, from 0xC996E0 (a load) and 0xC9A290 (the live Creature
  -> Tribe change). The NPC tribe controller's forming and SpawnNpcTribe never call it.
* ✔ (lab 20261112-094334, -100542) So the games that saw a village made had one stamp there, a game that loaded the
  world later two: a guest's rejoin had the ground 50 m round a village the host formed 7.95 m off (10 and 25 m: 0.00
  / 0.09), its list one entry longer (the tribe's stamp, id 1888182615). The same stamp applied live by hand on the host
  (`coop.terrain stamp <the village's first stamp> 535DC193 2BE764E2`) got the same id and made both grounds equal at
  every point (0.00 m): unlike BUG-0332's stamps, this one renders live as a load renders it. `coop.tribe` tribe lines
  print `stamp=` (+0x19D4).
* Fix `tribe.village_load_stamp`: every game in Tribe calls 0xC91890 for a tribe whose +0x19D4 is 0 a second after it
  appeared (the live change makes it itself right after the village). A host's own later load then adds nothing either.
* ✔ (lab, tribe_gameover_partner) The game-over caption needs every member of the player tribe down: the host's own
  members and chief knocked out while the guest's live brought none in 80 s. A knocked-out chief stands up again about
  95 s after its fall with full health (hp 168, dead 0), also while the caption is up; the caption stays until OK. In
  play_tribe the chief had fallen first, the rest 90 s later: the chief stood up 3 s into the caption, and the play's
  "no member with health" rule never pressed OK (BUG-0922, a test rule; the guest playing on meanwhile: BUG-0960).

## A guest's member eats in the host's game (2026-11-12, fix/w48-small)
<a id="tribe-guest-eat"></a>

BUG-0889 (the report behind BUG-0573). Lab lane 3, tribe fixture (`tools/tests/coop_tribe_guest_eat.py`).
* ✔ (runtime) The host's game feeds a member the guest owns (members are dealt round-robin: owner = the guest's id in
  the host's `coop.tribe` rows) by itself: set to hunger 15 with the game's setter and given no order, its own tree ate
  within 15.6-17.3 s (hunger 15 -> 100; the fixture's tribe has food 70). The meal's animations seen on the host's
  member: 02796B38, 060212DC / 060212CE (its `bhv=`).
* ✔ A guest's mirror runs no tree (h_BTTick), so it eats only in the host's game; the guest sees the meal through
  tribe.mirror_anims (the animation) and tribe.mirror_hunger (the value). With both off the guest's mirror played none
  of the meal's animations and kept its own hunger (lab 20261112-122726).
* CORRECTION (2026-11-12, fix/w48-small, BUG-0889): BUG-0573's cause ("the guest's tribe has the host's food bundles
  again, so its members' hunger AI finds food") is superseded: no hunger AI runs on a guest; the report's members that
  "do not eat on their own" were the guest's view of them (no eating animation before BUG-0569, a hunger running down
  in the guest's own game before BUG-0935).
## The guest waits while the host's game-over caption is up (2026-11-12, fix/w48-rest)
<a id="tribe-gameover-wait"></a>

BUG-0960. GOG 2024, Ghidra and lab lanes 4 / 5 (tools/tests/coop_tribe_gameover_wait.py). Follows #tribe-bounce-mirrors
and #tribe-guest-game-over.

* ✔ (lab) Every member of the player tribe knocked out with `coop.tribe pincer` (5000 a blow; a chief with others
  still up stops at 0.3, the last ones go to 0): the host's caption came 14 s later, the host `busy:
  cinematic,input_lock,paused`, the guest `busy: -` (it played on) until the host's OK (Enter) bounced it back.
* ✔ `Simulator::cGameTimeManager::Pause` 0xB32330 / `Resume` 0xB32360: thiscall(tm, uint32 reason id) ret 4; a count per
  reason (+0x68 table through the id map 0x643A00), then 0xB320B0 sets the stood flag +0x48 bit 0 (what `busy: paused`
  reads). A guest paused with the reason 0x4BF38A7 stood (`busy: paused`) and ran again on Resume.
* Fix `tribe.gameover_wait` (tribe.cpp): the host sends its caption state (MSG_NPC_ANIM kind 20 {u8 up}, reliable, every
  3 s while up): up = in Tribe with the input lock and the stood time, entered with the player tribe out; a guest
  pauses its game with the UI-modal reason 0x4BF38A7 (#tribe-game-time-pauses) and a co-op note, and resumes on "down", 10 s without news, the stage's or the
  session's end. ✔ Lab: the guest stood 0.0 s after the host's caption was seen and went on 0.5 s after the host was
  back (20261112-140917).
## The roster's cards: the game UI's map, taken off by the stage's listener (2026-11-12, fix/w48-rest)
<a id="tribe-roster-cards"></a>

BUG-0961. GOG 2024, Ghidra and lab lanes 4 / 5 (tools/tests/coop_tribe_roster_cards.py). Follows #tribe-member-body.

* ✔ The roster (the member cards on the right edge) is the stage game UI's map ([cTribeUI_Get 0x169B2C8]+0x240 = the
  TribeGameUI, +0x98 an EASTL map citizen -> card window, begin +0x9C, **size +0xA8**); its update 0xCCC7C0 walks it
  every frame (health bar 0x3D99184, hunger 0x3D99162, the activity icon from the node 0xCCA550). ✔ Runtime: the size
  equals the player tribe's members at the start (5 / 5 on both).
* ✔ A card leaves through `SporeCoop::TribeStage_RemoveMemberCard` 0xCD51F0 (thiscall(stage, obj) ret 4: cast to a
  citizen 0x4F176642, then `TribeRoster_RemoveCard` 0xCCBCE0 on [stage+0x240]), called by the stage's message listener
  0xCD81F0 when a member goes, and from the BT node 0xDA78C0. cTribe_RemoveMember 0xC96070 itself does not touch it;
  `TribeHud_ptr` 0x169B41C is a text helper (+0xC a text, +0x14 a citizen for the event log), not the roster.
* ✔ (lab) Two members killed on the host: the host's roster 3 cards / 3 members, the guest's 5 / 3 for 20 s and more:
  detach_body (BUG-0620) edits the tribe's lists and never told the roster. ✔ detach_body's own list edit releases no
  reference while the game's removal (0xC952E0 on +0x340: vt 4, 0xC95280 on +0x354: vt 0xC0) does.
* Fix `tribe.body_card`: detach_body calls 0xCD51F0 for the mirror first (lab: 3 / 3 on both).

## A mirror's own hold of its item (2026-11-12, fix/w48-rest)
<a id="tribe-hold-only"></a>

BUG-0962. Lab lane 5 (tools/tests/coop_tribe_hold_only.py; the host's chief in `coop.tribe anims` on both games at
~20 Hz). Follows #tribe-mirror-looks.

* ✔ With tribe.mirror_hold the host's holds reach the mirror 0.0-0.07 s after the host's start and end within 0.02 s
  (3 of 3 in 120 s); the hold state of the chief agrees in 1967 of 1990 samples, the animation in 1795.
* ✔ The difference: in the gesture 0x04C6CB02, played by both, the mirror's own animation event took the staff while
  the host's member did not (1.66 s, BUG-0906's 1.5 s settle and the 0.25 s tick); after a gesture the host ended
  (it stood, 0x02481DE5) the mirror played 0x04C6CB05 again for 0.2 s and held for 0.15 s. Phase is under 0.1 s.
* Fix `tribe.mirror_hold_only`: a hold of the mirror not bound to a host's hold, with none of the host's pending,
  ends after 0.35 s.

## A host's hold the mirror missed: the change animation and another animation (2026-11-12, fix/w48-last)
<a id="tribe-hold-missed"></a>

BUG-0971, BUG-0968. Lab lanes 2 / 3, tribe fixture (tools/tests/coop_tribe_hold_only.py; `coop.tribe holdtrace [id]`
on the guest: the last 4096 hold events of the mirrors: S / E the host's start / end received, T bound, t a miss
(anim, the mirror's current animation or FFFFFFFF for no item), O / C / M the mirror's own hold seen / cut after 0.35 s /
ended after 1.5 s, R / r a relayed start played / refused (anim, the animation right after), I the idle fix, P
tribe.hold_anim's play, D a relayed start skipped after P). Follows #tribe-hold-only.

* ✔ (lab, 4 misses of 0.43-0.68 s in 6 runs, all of them holds in the change animation 0x02C39200) Two ways:
  (a) the change is where a member takes an item into its hand (0xC255E0, the item attached at the animation's event):
  when the host's start came (0.1-0.4 s after the relayed start of 0x02C39200), the mirror played 0x02C39200 but its
  hand item +0xFFC was still 0 (the replicated item field comes later): `t 02C39200 FFFFFFFF` at each start until the
  host's end. (b) the host went from its gesture 0x04C6CB02 straight into the change: no relayed start of 0x02C39200
  reached the mirror before the hold (once one came and the mirror stayed in the gesture), so every start found the
  mirror in 0x04C6CB02 (`t 02C39200 04C6CB02`) until the host's end.
* ✔ The host's chief and the guest's chief (both simulated by the host) behave the same; the guest's chief's gestures
  0x04C6CB02 hold the torch / staff raised for 2-46 s on the host (shared by the mirror in every run sampled).
* Fix `tribe.hold_item` (BUG-0971): starts go as MSG_NPC_ANIM kind 12 (kind 11's entries + u8 the host's item, +0xFFC);
  hold_try attaches that item when the mirror's differs or is 0.
* Fix `tribe.hold_anim` (BUG-0968): a fresh start (under 0.5 s) whose animation the mirror does not play starts it on
  the mirror (at most once a second, as a relayed start for the idle fix) and binds the hold to it.
* ✔ (lab 20261112-170115) A relayed start of the same animation arriving with the host's start (same moment: S, P, T,
  R in the trace) started it a second time: the hold bound to the first start ended and every later start (T each
  second) attached no started modifier until the host's end (2.7 s missed). The relay skips a start of the animation
  hold_try played under 0.5 s before while the mirror still plays it (trace D).
* The BUG-0962 cut of the mirror's own holds is quantised by the 0.25 s tick: up to 0.85 s by design (0.8 s measured).

## The guest's game plays the village captions too (2026-11-12, fix/w48-fmisc)
<a id="tribe-guest-village-caption"></a>

* ✔ (runtime, lab 20261112-212337-2 / 20261112-212603) When the host's raid conquers a village and the realign
  conquers it in the guest's game too (BUG-0777), the guest's game plays the same captions as the host's
  (docs/re/tribe.md#tribe-conquer-caption): TRG_TotemPole_Conquer 5418B038 ("You have destroyed the ... Village! Your
  totem grows ..."), then TRG_Upgrade 94D17D9F, with the input lock and the game time standing until its own player
  clicks. The host's closes after 8 s (BUG-0531); the guest's stood 40 s+ (test end) with
  `busy cinematic,input_lock,paused`.
* Fix (BUG-0983, `tribe.conquer_caption_guest`): a guest's village caption ends itself after the same 8 s through
  `world::end_cinematic` (no notice: nobody else waits for it). Measured: busy 16-20 s after the conquest (two
  captions of 8 s), then free.
## A start that waits behind the running animation (2026-11-12, fix/w48-ftribe)
<a id="tribe-anim-queued-start"></a>

BUG-0992. Follows #tribe-hold-missed (the 0.7.13 final check still missed holds of 0.35-0.43 s in the change 0x02C39200;
this branch's runs also one of 4.2 s and one of 2.7 s in the gesture 0x04C6CB02).

* ✔ (Ghidra GOG 2024) `SporeCoop::CreaturePlayAnim` 0xC122F0 thiscall(creature, anim, a, b) ret 0xC: maps the id
  (0xC0C710: in Tribe 0xEBFCE0), reads the current main animation (AnimatedCreature vt 0x58 = 0xA05090, main track
  0x9FF690), returns 0 when that is the same id and loops (track +0xA0 == -1.0, 0xA048D0); `a` != 0 ends the pending
  tracks (0xA02B00 -> 0xA008C0, `SporeCoop::AnimatedCreature_ClearPending`); then a new track (vt 8), vt 0xC(track,
  current loops ? 1 : 0) (0xA00130: track +0x80), vt 0x18 start (0xA01A90 -> 0xA01750), vt 0x40 the id, vt 0x3C,
  0xC103B0, +0xE88 = the new track. `cCreatureBase::PlayAnimation` 0xC12470: the same with vt 0xC(track, 1) always.
* ✔ (Ghidra) The animation state (AnimatedCreature +0x184): tracks of 0xF0 bytes from +8 (id at +0xD8, state +0xC0),
  the playing ones' indices at +0xF0C (the main one +0xF0C[+0xFA4]), the pending ones' at +0xF4C (16 each, -1 free).
* ✔ (lab 20261112-175447, 20261112-205503 #2, holdtrace) A relayed start of the change (`R 02C39200 04C6CB02`) and
  hold_try's own start (`P`) returned a track, but the mirror went on in its gesture 0x04C6CB02 until the host's end.
  The host's member did the same: its tree started the change at ~32.0 s and its current animation stayed the gesture
  until 33.1 s. So a start over a non-looping current animation waits until that one ends (a looping one, idle or
  walk, is cut at once: `R 06426C10 06426C10`). The mirror's gesture began later than the host's, so it ended later and
  the host's 0.4 s hold was over first.
* ✔ (Ghidra) The attach 0xC240E0 (from Citizen_SetHandheldItem 0xC250A0, its last argument the tool's data +0x30)
  adds the hold modifier (0xBCA980 kind 5) with the track found by 0x9FF700(state, current id): it looks in the
  **pending** tracks first, then the playing ones. The modifier's tick 0xC11240 keeps it only while that track's state
  +0xC0 is 1 or 2.
* ✔ (lab 20261112-213048 #1, 20261112-214601 #2) The host's chief started 0x04C6CB02 again right after its last one;
  the mirror, still in its own 0x04C6CB02, got the relayed restart as a pending track of the same id. Every attach
  (`T 04C6CB02 1100` once a second for the host's 2.7-4.2 s) bound the hold to that pending track and no hold showed.
  (Starting the animation again with PlayAnimation, tried first, added one more pending track: same miss.)
* Fix `tribe.hold_queued` (BUG-0992): before the attach, a mirror with a pending track of its current id has its
  pending tracks ended (the game's 0xA02B00; trace `W`): it goes on in the running instance and the hold binds to it.
  A fresh host hold whose animation the mirror still does not play 0.2 s after hold_try started it is held in the
  animation the mirror plays (trace `Q`), ended by the host's end as any bound hold.

## Snapshot cycles after a restart, and effects a mirror keeps (2026-11-13, fix/w49-life)
<a id="tribe-cycle-restart"></a>

BUG-1051 / BUG-1049, GOG 2024, lab lane 5.
* ✔ (runtime, out/lab/20261113-010644) The host's Tribe snapshot counter (tribe.cpp g_cycle, u16) set from ~100 to
  40000 is an "older" cycle for the guest (40000 - 100 >= 0x8000, net::cycle_order): the guest dropped 49 chunks until
  the counter came round. A test that wants a "long session" sets 20000 (newer). Back to 1 after that (a restarted
  host) every chunk is older: without the reset the guest made no mirror at all (out/lab/20261113-011952).
* ✔ (runtime, out/lab/20261113-012450) Pool-2 effect 0x21C934A7 key 0x55559EF (a member behaviour's,
  #tribe-mirror-fx) started on a host member by SporeCoop's pool start stays active on the guest's mirror (no
  update of the mirror stops it); `game::stop_effect` of the key stops it there.
* Negative (out/lab/20261113-010644): pool-1 effect 0x8ADA6617 (the NPC chief's) started the same way never showed
  as active on a mirror of a player member (three members tried); not followed further (inferred: the creature's own
  update stops it on a non-chief).
* ✔ (runtime, lane 4 probe 2026-11-13) Started on a mirror by the relay and read each 0.5 s for 6 s
  (`coop.mirrorfx has`): 0x21C934A7 and 0x85A02492 (pool 2, key 0x55559EF) are active for under 1 s and end by
  themselves; 0xE55DC391 key 0x35052B7 (pool 1, the sprint's dust) stays active (the mirror is not sprinting, its
  Update does not stop it); 0x8ADA6617 is never active (pool 2 too). The keyed effects a 25 s fight of the host's
  members made: those three only (`coop.effects log on`, fx1k / fx2k lines).

## A guest's order on a member the host's game just removed (2026-11-13, fix/w49-live)
<a id="tribe-cmd-listed"></a>

* ✔ (lab 20261113-022421, BUG-1053) RemoveMember(c, destroy) (cTribe vt 0x9C, `coop.tribe drop_member`) takes the
  citizen out of the game's object list at once (`listed_now=0` in the same console call), but the host's table g_hm
  keeps it until the next host_tick (0.12 s). DoAction(member, 7 Attack, <that destroyed citizen>) is accepted by the
  game: the member's order record (ai+0x60C, rec+0 action 7, rec+0x28 target) names the destroyed block. No fault was
  seen in the 50 s after (3 rounds); the block is freed later and the record would then point into freed memory.
* The host resolves member and target ids only through game::alive (tribe.cmd_listed).

## Mirror poses: a gesture that comes back, the moment of an animation, a mirror's own hold (2026-11-13, fix/w49-mirror)
<a id="tribe-mirror-pose-phase"></a>

BUG-0972, BUG-0988, BUG-0993. GOG 2024, lab lanes 6 / 7, tribe fixture. Follows #tribe-mirror-looks,
#tribe-hold-missed and #tribe-anim-queued-start.

* ✔ (probe, both games' `coop.tribe anims` at 5 Hz for 150 s) The host's chief (#1) and its mirror played the same
  animation in 59 % of the samples (the other members 89-98 %); 257 of the rest were the host's chief in its idle
  0x02481DE5 and the mirror in the gesture 0x04C6CB02. Second probe with the guest's hold trace: the host restarted
  the gesture at 80.9 s (a pending track of the same id on the mirror, as in BUG-0992 (b)), changed its item at
  90.3 s (0x02C39200) and then walked and stood; the mirror played the change, then the waiting gesture again (91.4 s)
  until the end (37 s). The idle fix (kind 10) skipped it: the host sent its idle (how 1) and the mirror's animation
  was not the last relayed one (0x02C39200), which the fix took for the mirror's own idle or walk.
* Fix `tribe.mirror_stale_anim` (BUG-0972): a mirror in an animation that is not its locomotion's idle / walk
  (0x02481DE5, 0x04866D8E, 0x04866DAB, 0x04866DD4), while the host's member is in its idle or walk, has its pending
  tracks ended (AnimatedCreature_ClearPending 0xA02B00) and its idle refreshed (trace `Z`; `stale_cut=` in
  `coop.tribe anims`). Lab: the mirror back in its idle / walk 0.7-2.1 s after such a gesture (off: never in 9 s).
* ✔ (Ghidra) The main animation track as GetCurrentAnimation 0xA05090 finds it (0x9FF690): the animation state
  (AnimatedCreature +0x184), the main index +0xFA4 into the playing list +0xF0C (index <= 0xF), the track at state +
  8 + index * 0xF0 (its first dword not 0); the track's id +0xD8, **its time in the animation +0xA8 (double, seconds;
  GetCurrentAnimation's float* time)**, +0xB0 an int (its 4th out-argument), +0xA0 = -1.0 when it loops.
* ✔ (lab, tribe_mirror_phase off) A relayed behaviour animation runs on the mirror 0.05-0.16 s behind the host's
  member (0x04C6CB02 0.16 s, 0x02C39002 0.15 s, 0x06426C10 0.05 s; median of all 0.10 s), more after a returned or
  late start (up to 2 s). Writing the mirror's track time +0xA8 moves the animation there: the time goes on from the
  value written (the next samples). ~ The pose follows the time (inferred from the key frames, not measured on bones).
* Fix `tribe.mirror_phase` (BUG-0988): once a second the host sends every member's time in its behaviour animation
  (MSG_NPC_ANIM kind 14: u8 n, n x {u16 id, u32 anim, f32 time}; not for loops); a guest whose mirror plays that
  animation more than 0.1 s from time + RTT / 2 writes the track's time (`phase fix ... set=` in `coop.tribe anims`;
  `t=` and `loop=` in its rows). Lab: median 0.046 s, 98 % under 0.15 s (off 0.099 s, 77 %). The mirror still runs
  0.03-0.1 s behind (the RTT / 2 estimate is below the real delay of a once-a-second message).
* Fix `tribe.hold_cut_frame` (BUG-0993): held_items_tick looks at the mirrors' own holds every frame (hold_try keeps
  its 0.25 s tick): the BUG-0962 cut comes 0.41-0.43 s after the hold began at 25 fps (before 0.59-0.78 s there, up to
  1.05 s at 26 fps with hitches). Trace `H`: a hand set by `coop.tribe hand` (tests).

## A member's thought bubble: the chat effect and its pictogram (2026-11-13, fix/w49-mirror)
<a id="tribe-bubble-icons"></a>

BUG-0987 (wontfix). GOG 2024, lab lanes 6 / 7, `coop.effects log`, shots of both games, probes with a forced seed.

* ✔ (runtime) The bubble over two chatting members (a cloud with a pictogram: a fish, a spear, a horn, a ball, a
  cake, a planet, ...) is the **animation event effect 0x883816C2** of the chat animations 0x04FFA012..0x04FFA019
  (AnimEffect_Create from AnimEvent_Handle, call path 0x9A72E3 -> 0x9A61D7), about 2 m over the member. The relay plays
  the animation on the guest's mirror and its game makes the same effect at the same moment (60-90 ms after the
  host's); the pictogram differs (5 of 6 bubbles in a probe). Not related to BUG-1043 (no field is involved).
* ✔ AnimEvent_Handle's param 2 (the animating instance) is the creature's animated model: AnimatedCreature
  (creature +0xB54) +0x17C (matched every bubble to its member on both games).
* ✔ (Ghidra) The visual effect (vtable 0x1457DE8; a second interface at +8, vtable 0x1457DA8, the group interface,
  slot 0x18 = 0xA838A0 its update(dt, float, stats*)): vt 0x3C 0xA81D00 writes **+0x180, the seed**; Start 0xA82660
  seeds the effects' generator SporeCoop::EffectsRandom 0x16778DC with it (Random_Seed 0x936000: 0 or -1 takes the
  time stamp counter) before its components' Start; vt 0x0C 0xA81FB0 Stop(hard) clears the started flags 0x200 /
  0x400. The bubble has one component (+0x144..+0x148, 0x5C bytes, +0x38 a nested effect of the same class). The
  generator 0x16778DC is read by the random float 0x936240 from 100+ places (UI, terrain, the effects' updates).
* ✘ Tried, no effect on the pictogram: (1) vt 0x3C(seed) + Stop(1) + Start(0) right after the effect was made (the
  same pictogram stayed; later bubbles changed anyway); (2) the generator set to one seed right before
  AnimEffect_Create built the effect, on both games (forced 12345 on every bubble: flower, axe, horn, ghost on one
  game, others on the other); (3) the same also before every update of the bubble (a hook on 0xA838A0 for the
  effect, its +8 interface and the nested effect; 1780 updates seeded): still other pictograms. So the pick is
  neither in the build nor in the group update's draws from 0x16778DC; it is made deeper (a particle component's own
  random or another generator). Not followed further: cosmetic chat (known-issues#issue-tribe-bubble-icons).

## The chief's gesture from the load, a restart over a bound hold, members without a behaviour start (2026-11-13, fix/w49-tribe3)
<a id="tribe-chief-load-gesture"></a>

BUG-1069, BUG-1071. GOG 2024, lab lanes 6 / 7, tribe fixture. Follows #tribe-mirror-pose-phase.

* ✔ (lab 20261113-060215 #6, same_view_tribe with the chief's rows at each still) The host's chief of the fixture
  stands in its staff gesture 0x04C6CB02 begun at the save's load, before the session: on the host `bhv=00000000`
  (no behaviour start seen since the host began tracking: SporeCoop's behaviour listener never saw it) and the track
  time t 16.7 -> 22.2 s over the four views (the gesture runs 20 s and more); the guest's mirror played the same id
  from its own start (t 0.6 -> 6.1 s; started by the idle news kind 10 how 2 or hold_try's P), the hold 0 at the
  first view. The moment (kind 14, BUG-0988) went only for members whose current animation equals bhv_anim: none for
  this one, so the staff pointed elsewhere on the two screens.
* ✔ (lab 20261113-060910, tribe_chief_still) The same gesture on both games 5.6 s apart (host restarted it: t 2.96,
  mirror 8.53): the phase fix only set differences under 5 s. The host's chief restarts 0x04C6CB02 with a hold
  (holdtrace `S T R` at one moment): the relayed restart started a new instance on the mirror and the hold bound to the
  running one ended (`O` only at the next resend, 1.1 s later). Once the mirror's instance ended before the host's
  (host still in the gesture, mirror idle 1.4 s, holdtrace `M`).
* ✔ (same run, tribe_mirror_stale round 1) The host's chief in its idle with `bhv=00000000` and the guest's mirror in a
  returned gesture (relayed 0): no idle news (kind 10) was sent for it at all, because the host read the member's
  animation only when bhv_anim was set; the gesture ran to its own end (6.4 s; 9 s in 20261113-054031-2).
* ✔ (lab probe 2026-11-13 lane 6, both games' chief rows at 10 Hz for 100 s, `a0=` / `b0=` added to `coop.tribe
  anims`) The track's **+0xA0 float: -1.0 loops, 1.0 plays once**; **+0xB0 int: the passes done** (the idle 0x02481DE5
  counts 0, 1, 2 every ~6 s). The host's behaviour starts of 0x04C6CB02 play once (a0 1.0), but a mirror's start of the
  same id was sometimes a looping track (a0 -1.0; 1 of 6 starts in the probe, after the idle at t 15 s): the phase fix
  skipped loops, so the mirror stayed in the gesture 8 s after the host's had ended (b0 1). ~ Which start makes the
  looping track is not known (CreaturePlayAnim's vt 0xC(track, current loops ? 1 : 0), #tribe-anim-queued-start).
* ✔ (second probe, 150 s) The other way round too: the host's chief's own 0x04C6CB02 track can loop (a0 -1.0 from its
  start at 116.6 s to the end of the probe; the stills of 060215 #6 had it at t 16-22 s): its mirror played single
  passes of ~6-7 s, went to its idle, was started again by the hold / idle news, and so on (the hand empty 1.3-1.5 s at
  each gap in tribe_chief_still). The moment relay never covered loops.
* Fixes: `tribe.mirror_phase_any` (once a second the host sends every member's current animation but the
  locomotion's idle / walk, loops too, as MSG_NPC_ANIM kind 15: u8 n, n x {u16 id, u32 anim, f32 t, f32 +0xA0, u16
  +0xB0}; a guest whose mirror plays that id gives its track the host's +0xA0 / +0xB0 when they differ (`unlooped=` in
  `coop.tribe anims`) and the host's time + RTT / 2 when 0.1 s or more off), `tribe.hold_restart`
  (a relayed restart of the animation a host's hold bound to under 0.5 s before is skipped while the mirror plays it,
  trace `D`), `tribe.mirror_idle_loco` (the host reads every member's animation for the idle news, and a member whose
  behaviour start is its locomotion's idle / walk goes as idle news, not as kind 3).

## The goal mission of a village only guests act on (2026-11-13, fix/w49-raid)
<a id="tribe-guest-mission"></a>

BUG-1068. GOG 2024, lanes 6 / 7, tribe fixture (`coop_tribe_guest_raid_goal.py`). Adds to #tribe-wsync and
#tribe-conquest-sync.

* ✔ (runtime, lab 20261113-072615, `tribe.guest_mission` off) The fixture's NPC village (pol 0x21CB) raided by a
  guest's member only (its order through DoActionForObjects -> MSG_TRIBE_CMD -> the host's DoAction 15 on the hut):
  the hut fell (state 2), the host's Tribe Mission Manager had no current mission before or after (`tribe_goal`
  target FFFFFFFF, state 0) and the goal bar stayed 0.0/5.0 for 30 s. A guest's gift (DoAction 9 on the cTribe) did
  not move the card either. So nothing on the host's DoAction path makes the village's cTribeToTribeMission; the
  game's own click makes it in DoActionGeneric 0xCD0C20 (SetTargetTribe 0xCD3D70) before DoActionForAllSelected.
  The host's own `coop.tribe order` / `act_obj` skip that function too (they call DoAction / DoActionForObjects).
* ✔ (runtime, lab 20261113-072313, fix off) A village the host's game formed during play (`coop.tribe npc_tribe 3
  45`, SpawnNpcTribe) raided the same way **did** count: no current mission before the fall, then 4 s after it the
  current mission was that village's (state 5, card shown) and the bar went 0 -> 1. ? What made that mission at the
  fall (likely the tribe message handler 0xCD1D60, a caller of SetTargetTribe, on the defeat message 0x58BADDD for
  a village formed during play) and why it does not for the save's villages: not traced.
* ✔ (runtime, labs 20261113-072313-2, 20261113-073140, 20261113-073140-2) SetTargetTribe(mgr, the village's cTribe) called
  on the host outside the frame when its order from a guest runs (`wsync::goal_target`, fix `tribe.guest_mission`):
  the gift moved the host's card at once (state 3, card=1), the raided village's fall counted 0 -> 1 within 2 s.
  The mission's update ends it from the village's conquered flag alone, so making it at the order is enough.
* ~ On the guest, right after such a fall, `tribe_goal` read progress 2.0 once while the host's was 1.0 (the guest's
  own game also ends a mission it has for the village through realign.hut_fall's blow, then the row writes the
  host's value): seen in labs 20261113-072313-2 and 20261113-073140; the guest applies the held row every frame (guest_tick), so it is a moment, not checked further.
## The end of Tribe by raids: missions, huts, new villages (2026-11-13, fix/w49-tests)
<a id="tribe-raid-end"></a>

Q1 of 0.7.14 (the campaign's Tribe end), GOG 2024, lane 4, tribe fixture (Zorispe), 2 and 4 players.
* ✔ (runtime) The tribe fixture has the players' village and one NPC village; the goal bar reads 0/5
  (`coop.wstate` progress / total). Right after the first conquest the game formed three more villages at once
  (`coop.realign planet village`: 5 villages, the conquered one fl=2); a fifth NPC village came while the fourth was
  raided (338 m from the players). Five conquests filled the bar.
* ✔ (runtime) NPC huts read 400/400 (the fixture's village) and 750/750 (the villages formed later). Raiders hit a hut
  from 20-30 m (its health falls while none of them is within 14 m of its centre).
* ✔ (runtime) **Only a conquest of a village the host's own player attacked counts.** Raided with `coop.tribe order <id>
  15 <hut>` alone (the DoAction a guest's order becomes on the host) the hut fell (state 2), the village got its
  conquered flag (fl=2), but the bar stayed 0/5 and no conquest cinematic played; with one right-click of the host's
  player on the hut first, each fall moved the bar by one (0 -> 1 -> 2). In a 4-player campaign a village only Bob and
  Carol had clicked fell with the bar at 0 (BUG-1068). Inferred: the click's input action makes the village's
  cTribeToTribeMission (#tribe-wsync), whose completion is the goal.
* ✔ (runtime) The full bar shows the advance button (0x6454B47); after it the game asked for two votes and two empty
  editors (City Hall 99E92F05, then the vehicle editor 7D433FAD), each saved with three parts and the dice's name,
  then Civ (campaign_tc2 lab 20261113-054114: 1152 s from the first raid to Civ, 3 min of it the editors).
* ~ (runtime, lab 20261113-051634, twice) While the farthest village was raided with every member, the host's game
  played a cinematic with the input lock and then reloaded its save (`Tribe -> Loading -> Tribe` in 0.1 s, members
  24 -> 19, every conquest gone): a game over. Not seen which one (no screenshot); inferred: the NPC village raided back
  the players' empty village. The campaign keeps the players' hut above 80 % of its health since (a noted shortcut).

## The creature attack event's attacked tribe (2026-11-13, fix/w49-drama)
<a id="tribe-attack-event-target"></a>

BUG-1078. GOG 2024, Ghidra (tools/ghidra/q.sh --build 2024). Follows docs/re/creature.md#creature-drama-events and the
BUG-0564 lines above (the guest's own kind 7 faulting at 0xD521BA after a village removal).

* ✔ cCreatureAttackEvent (kind 7, cast id 0x5ED29D3, vtable .rdata 0x147B320): +0x28 0xD509A0 setup (int property
  0x5F10CE1 = RandomInt + (int) event +0x20: the pick), +0x30 0xD51BE0 start (`SporeCoop::CreatureAttackEvent_Start`),
  +0x38 0xD50B30 can start, +0x3C 0xD50BE0 done?, +0x44 0xD50DF0 pre-check. Int properties in a map at event +0x44
  (0x5649E0 thiscall(map, &key) ret 4 -> int*: find or insert), object properties (counted references) in a map at
  +0x5C / +0x60 / +0x70: `SporeCoop::DramaEvent_GetObject` 0xD4CEC0 thiscall(event, key, type) ret 8 (no AddRef),
  `SporeCoop::DramaEvent_SetObject` 0xD4ED80 thiscall(event, key, obj) ret 8 (AddRef new, Release old; null erases
  through 0xD4E890 thiscall(map, &key) ret 4).
* ✔ The attacked tribe is object property 0x5F11333. `SporeCoop::DramaManager_NoteNpcTribe` 0xD516B0 (thiscall(mgr,
  tribe) ret 4, its only caller SpawnNpcTribe at 0xC933F7) counts the NPC tribes the game makes in Tribe (int property
  0x5F10CDD) and makes the one where the count reaches the pick (0x5F10CE1) the attacked tribe. Without it the start
  takes the tribe noun list (0x18C6D19) at the index 0x5F10CE1 (none when the index is past the list's end).
* ✔ The start finds a herd (0x1BE418E) of the event's species near the tribe or makes one, and posts stimulus 0x4000
  to each member at `[place +0x120]` vt 0xC, where place = the tribe's tools (vt 0xBC) [1] when it has more than one
  tool, else its hut (vt 0xAC, +0x368). No check: a tribe without its hut (and at most one tool) faults at
  0xD521BA (`mov edx,[ecx]`, ecx = 0 + 0x120). Registers of lab 20261113-103507 (guest, alone after `coop.leave`):
  ecx 0x120, ebp 0 (no second tool), esi the herd.
* ✔ (code) How a guest gets there: its game makes an NPC tribe for each new host tribe (SpawnNpcTribe, so it counts
  for the pick); after the host's game over and reload the re-alignment destroys the ones the host's world lost
  (tribe::remove_village, cGameNounManager::DestroyInstance: the hut goes, the tribe object stays allocated while the
  event holds its reference). The guest's game holds the event back (tribe.guest_attacks); alone it starts.
  `tribe.attack_target` (drama.cpp): before a kind 7 start in Tribe an attacked tribe whose place is null is set to
  null with the game's setter. `coop.drama target [<tribe obj>]` reads / sets the property (tests).
* ? Whether the vanilla game ever leaves a tribe without its hut in the Tribe stage (no tribe is destroyed there
  outside the mod, inferred): not looked for.

## The caption's tribe name: the Tribe token provider (2026-11-13, fix/w49-fault)
<a id="tribe-caption-tribe-token"></a>

BUG-1076. GOG 2024, Ghidra (tools/ghidra/q.sh --build 2024) and the campaign's guest log (lab 20261113-082316, lane 4).
* ✔ (Ghidra) `SporeCoop::TribeHud_ptr` 0x169B41C is the Tribe stage's **text token provider** (vtable
  `SporeCoop::TribeTokens_vftable` 0x14779D0, 0x20 bytes, made once by the Tribe UI ctor 0xCCCDD0 and registered with
  the locale manager (cLocaleManager vt 0x20 -> vt 0x1C); never freed or cleared). CORRECTION (2026-11-13,
  fix/w49-fault): the BUG-0645 line called it a HUD object; its +0x10 (the population cap of the hut upgrade) is one
  of its tokens. Fields: +0xC / +0x10 ints, +0x14 a creature (token 0xCB37547D reads +0xB38), +0x1C a cTribe* (token
  0xF7A4AD93 = the tribe's name through vt 0x7C), stored without a reference.
* ✔ `SporeCoop::TribeTokens_Resolve` 0xCE67E0 (vt 0x10) checks +0x1C for null only. A freed tribe there faults at
  0xCE69F9 (`mov edx,[ecx]`, the dead object's first dword) or 0xCE69FB (`mov eax,[edx+0x7C]`, vtable 0): the
  BUG-1076 registers (eax = 0xF7A4AD93 the token hash, ecx = 0x3F400000 / edx = 0). The stack scan's names
  (App::Property::SetValueFloat+0x185D, FNV_hash+0x126, GlobalUI_FindWindowByID+0x28E4) are stale stack words;
  LocalizedString ctor frames show a caption text being built.
* ✔ Writers of +0x1C: `SporeCoop::TribeVillage_Defeated` 0xCDAC90 (the conquered or allied village's tribe, else the
  player tribe; then TRG_Upgrade through TribeHut_Upgrade 0xCDA4F0, or TRG_TotemPole) and the relation change events
  of 0xCDF080 (each set right before cUIEventLog::ShowEvent). The conquest captions TRG_TotemPole_Conquer and
  TRG_Upgrade read it while they play.
* ✔ (runtime, lab 20261113-082316) How it went stale: the players' hut fell in the host's game (an NPC village raided
  it back, the campaign's repair came 19 s later); the guest's realign (BUG-0777) gave the guest's own hut the
  killing blow too. The guest's game then ran its own defeat: `busy cinematic,input_lock`, then paused, and 20 s later
  `Tribe -> Loading -> Tribe` (its own save reloaded, every conquest gone, the tribes freed). The realign conquered the
  village again and felled the hut again after each reload: 11 reloads in 6 min (epochs 2..13). A conquest set
  +0x1C just before such a reload; its captions played after it and read the freed tribe (9 faults, 3-4 s into
  TRG_Upgrade).
* ? Whether the host's own game always ends the stage when its players' hut falls: in that run the host's hut stood at
  state 2 with 0/400 for ~19 s without a defeat (input_lock 0) until the campaign set its health back (the hut stays
  "dead" with full health: `coop.realign planet hut` fl 3 on the host). In lab 20261113-051634 the host's game did
  reload after such a raid.
* Fix `realign.own_hut` (BUG-1076): a guest's own players' hut never takes the realign's blow; its health follows the
  host's down to 1 hp, the host's game decides the defeat. Test `tribe_own_hut_fall` (docs/06-testing.md#testing-own-hut-fall).

## The hut-destroyed defeat on the host (2026-11-13, fix/w49-hutwait)
<a id="tribe-hut-defeat-caption"></a>

BUG-1081. ✔ (runtime, lab 20261113-140055) The killing blow on the player tribe's own hut (`coop.tribe hutfall`,
combatant vt 0x18) sets its combatant state (+0x120 +0x34) to 2 at once; the player tribe's `mpHut` (+0x368) keeps
pointing at the dead hut (as `cTribe::HutIsDead` 0xC90730 reads it). 7.4 s later (a cinematic of the burning hut
first) the caption "Oh, the humanity! Your tribal hut has been destroyed! Perhaps a fresh start is just the thing you
need." is up (`coop.ui find "humanity"`), and the game stands with `busy: cinematic,input_lock,paused`, as for the
population game over (BUG-0960). Its OK (Enter) reloads the save: back in Tribe play with the hut at 1000/1000.
