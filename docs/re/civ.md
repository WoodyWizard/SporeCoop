# Civilization stage - RE notes (GOG 2016 build, sha256 922af7c7…)

Static analysis only (llvm-objdump on the full exe + local headless Ghidra on a windowed PE copy
0xBD0000–0xD20000). All addresses are VAs in this build. "ret N" = callee pops N bytes.
Confidence: **V** = verified by disassembly in this build (role + args + ret counted),
**P** = partly verified (address/role sure, some argument semantics inferred),
**U** = unverified / inferred from ModAPI headers or a single use, check at runtime.

Game-mode ids: kGameCiv = 0x1654C04, kGameSpace = 0x1654C05 (`0xB5B8D0()` returns the
current mode id; civ code branches on it everywhere).

---------------------------------------------------------------------------------------------
## 1. Civilizations, cities, city fields

### Getting the objects (all thiscall on `nm = cGameNounManager::Get()` 0xB3D3D0)
| What | Address | Signature | Conf |
|---|---|---|---|
| GetPlayerCivilization | 0xB25F20 | `(nm)` → `cCivilization*` | V (known) |
| **GetAllCivilizations** | 0xB25C10 | `(nm)` → `eastl::vector<cCivilization*>*` (begin,+4 end) | V |
| **GetCivilizationByPoliticalID** | 0xB25EB0 | `(nm, uint32 polID)` → civ or null, ret 4 | V |
| **GetAllCities** | 0xACE1C0 | `(nm)` → `vector<cCity*>*` (every city on the planet, all civs) | V |
| **GetAllVehicles** | 0xAE7300 | `(nm)` → `vector<cVehicle*>*` | V |
| first player city ("home") | 0xB25BA0 | `(nm)` → first city with `city+0x120 vt0x58 IsPlayerOwned()` | V |
| GetData (generic) | 0xB21260 | `(nm, create, clear, add, filter, nounID)`; the 3 getters above are wrappers that return `GetData(...)+4` | V |

Noun ids: civ 0x18C816A, city 0x18C43E8, vehicle 0x18C6DE8 (all wrappers push these).

### cCivilization (TYPE 0x901F1362) - matches ModAPI `cCivilization.h`
| Off | Field | Evidence | Conf |
|---|---|---|---|
| +0x30 | mPoliticalID (cGameData); vt 0x4C GetPoliticalID | used everywhere | V |
| +0x6C | cCultureSet (design keys) - `0xBF9890` = `lea ecx,[ecx+0x6C]; jmp 0xBF9820` | V |
| +0x89 | mIsPlayerOwned | GetMoney/SpendMoney | V |
| +0x98 | mWealth | GetMoney | V |
| +0x9C | `vector<cCity*> mCities` | AddCity 0xBF4490 | V |
| +0xB0 | `vector<cVehicle*> mVehicles` (max 36 total, see 0xBF2220) | AddVehicle 0xBF5750 | V |
| +0xD4 | `vector<cSpatialObject*> mSelectableObjectVector` (vehicle+0x34 pushed) | AddVehicle | V |
| +0x4AC/+0x4B0 | domestic/expansion budget (NPC civs only, added to money) | GetMoney | V |
| +0x4C0/+0x4C4/+0x4C8 | military/religious/economic capture counters | ChangeOwner | V |

Civ functions (thiscall on civ):
| Function | Address | Signature | Conf |
|---|---|---|---|
| GetMoney | 0xBEF800 | `()` → float in ST0 (player civ: just mWealth) | V |
| **SpendMoney** | 0xBEF840 | `(float amount)` ret 4; `mWealth -= amount` | V |
| AddCity | 0xBF4490 | `(cCity*)` ret 4 | V |
| RemoveCity | 0xBF4710 | `(cCity*, int)` ret 8 | P |
| AddVehicle | 0xBF5750 | `(cVehicle*)` ret 4: push to mVehicles, `SetPoliticalID(civ)`, push vehicle+0x34 to +0xD4 | V |
| **CreateVehicle** | 0xBF58D0 | `(int purpose, int locomotion, ResourceKey key /*12 B by value*/, const Vector3* pos, bool isSpace)` → `cVehicle*`, ret 0x1C. CreateInstance(0x18C6DE8) → AddVehicle → `cVehicle::Load` 0xCA67B0 `(loco, purpose, key)` → SetPosition(pos) | V |
| VehicleSlotsLeft | 0xBF2220 | `(int purpose)` ret 4 → int (0 if ≥36 vehicles) | V |
| **GetVehicleCost** | 0xBF2290 | `(int purpose, int locomotion)` ret 8 → float ST0 | V |
| GetDesignKey | 0xBF9890 | `(uint32 designId)` ret 4 → `ResourceKey*` (from culture set) | V |
| BuyCity | 0xBF0500 | `(cCity*, float price)` (pie command BuyCity) | P |
| AddMoney (refund) | 0xBEFEB0 | `(float)` (used by CitySellBuilding) | P |

`int VehicleDesignId(purpose, locomotion)` = **0xC9E850** cdecl → id for `GetDesignKey`
(space mode uses another table). Enums: purpose 0 military, 1 cultural(religious),
2 economic, 3 colony; locomotion 0 land, 1 water, 2 air.

### cCity (TYPE 0xEE9B2232, noun 0x18C43E8) - matches ModAPI `cCity.h`
| Off | Field | Evidence | Conf |
|---|---|---|---|
| +0x24 | mID (cGameData) | header only | U |
| +0x30 | mPoliticalID; set from civ in SetCivilization | 0xBE1280 | V |
| +0x120 | cSpatialObject; **+0x124 position**; vt 0x58 IsPlayerOwned | known, purchase UI | V |
| +0x29C | capture type of the last capture (0 mil, 1 rel, 2 econ) | Capture 0xBE9410 writes it | V |
| +0x320 | mpCityHall (`0xBD9C70` getter) | V |
| +0x324 / +0x328 | mpCityWalls / mpCityTerritory | dispose | P |
| +0x33C | mbIsPlayerCity (= new civ is player civ) | ChangeOwner | V |
| +0x340 | `vector<cBuilding*> mBuildings` (begin,+4 end) | AddBuilding / AI growth | V |
| +0x354 | `fixed_vector<cTurret*,32> mTurrets` (begin,+4 end) | dispose | P |
| +0x3EC | `cCommunityLayout mBuildingsLayout`; slot vector at **+0x43C** (layout+0x50), stride 0x18: `{+0 u8 flag, +4 occupant obj, +8 int, +0xC Vector3 pos}`; 14 building slots | `0xAF9F40` GetSlot(i), `0xAFADA0` RemoveObj, `0xFCC1D0` slot→occupant | V |
| +0x4B4 | mTurretsLayout | header | U |
| +0x518 | mbSmallCity | V |
| +0x540 | **mVehicleSpecialty** (= purpose of vehicles this city builds) | `0xBD8300` getter | V |
| +0x590 | **mpCivilization** (`0xBD9D20` getter) | SpawnVehicle | V |
| +0x664..0x678 | money/happy/unhappy/new buildings/final income | header (mNewBuildings +0x670 used by AI growth) | U |
| +0x748/+0x74C | mCapturePercent / mCaptureID (military) | 0xBE9AB0 | V |
| +0x750/+0x754 | mConvertPercent / mConvertID (religious) | 0xBEAB60 | V |
| +0x758/+0x75C | mBuyPercent / mBuyID | header | U |

City functions (thiscall on city):
| Function | Address | Signature | Conf |
|---|---|---|---|
| SetCivilization | 0xBE1280 | `(cCivilization*)` ret 4 (sets +0x590 and political id; low level, no bookkeeping) | V |
| **SpawnVehicle** | 0xBDDED0 | `(int purpose, int locomotion, ResourceKey key, bool isSpace)` → cVehicle*, ret 0x18. Spawn point = `0xBDCB60(city, Vector3* out, loco)`, then civ CreateVehicle, faces the walls. **No money** | V |
| AddBuilding | 0xBE2020 | `(cBuilding*, int noUpdate)` ret 8 | V |
| RemoveBuilding | 0xBE3480 | `(cBuilding*)` | P (ported) |
| CreateBuilding (auto slot) | 0xBE54E0 | `(uint32 noun, uint32 typeId)` ret 8 → cBuilding*; picks the best free slot (`0xBDB6C0`), used by AI growth | V |
| CreateBuildingByType | 0xBE74A0 | `(uint32 typeId)` ret 4 (type→noun: 0x1A55E4D→0x1A56ABA ent., 0xECADE42→0x18EA2CC factory, else house 0x18EB106) | V |
| SetBuildingModel | 0xBDB920 | `(cBuilding*, uint32 typeId)` ret 8 - sets model key from civ culture set | V |
| **SnapBuildingToSlot** | 0xBE3630 | `(cBuilding*, bool update)` ret 8 → bool; finds the free slot nearest to the building's current position (`0xAFACC0`), assigns it, sets slot position + orientation facing the city | V |
| ChangeOwner | 0xBE8A00 | see §6 | V |

### Network id for a city
`mID` is not proven stable between two processes. Cities never move, so use the position:
key = index in `GetAllCities()` sorted by (x,y,z) of `city+0x124`, or send the position and
resolve to the nearest city (`cPlanetModel::GetNearestCity` 0xB89580 is ported, unverified).
Buildings: (city key, slot index in +0x43C). Vehicles: host-assigned net id (like npc.cpp).

---------------------------------------------------------------------------------------------
## 2. Building placement

The player builds in the **city planner** = `Simulator/cCommunityEditor` (size 0x2FC, ctor
0xD0D200, main vtable 0x147A0BC, created by cCityInputStrategy when the city hall button
0x62D5618 in the RolloverCivCity window is clicked: input strategy +0x110 = city hall,
message 0xB2699148). Palette items are dragged onto the 14 plots.

* Commit of a placement: `cCommunityEditor` vt[1] **0xD130D0** `HandleMessage(msgId, msg)`
  (thiscall, ret 8). On drop it: reads the palette item's noun (property 0x456B66A),
  `CreateInstance(noun)`, `vt0x54 SetOwnerCity(city)`, sets model key from
  `civ->GetDesignKey(...)`, **`AddBuilding(city, b, 0)` at 0xD14197 (return address 0xD1419C)**,
  then cost `0xD0AC10(obj, refund=false, ...)` (cdecl, building → `vt0x5C GetCost()`,
  turret → 0xBD0240) is taken from the editor budget (`[editor+0x68] vt0x18(0, -cost)`). **P**
* There is no simple "place(noun, slot)" API; the host should rebuild the sequence:

```
// host: build `noun` in `city` at slot `k` (or at a guest-supplied position)
b   = CreateInstance(nm, noun)            // 0xB20B80
b   = b->vt0x0C Cast(0x0E9CB8BA)          // cBuilding
SetBuildingModel(city, b, typeId)         // 0xBDB920 (typeId: house 0xFF10521, factory 0xECADE42, ent 0x1A55E4D)
b->vt0x54(city)                           // SetOwnerCity (+0x1F0)
pos = *(Vector3*)(*(uint8**)(city+0x43C) + k*0x18 + 0xC)
(b+0x34)->vt0x38 SetPosition(&pos)
if (SnapBuildingToSlot(city, b, 1))       // 0xBE3630, picks nearest free slot = k
    AddBuilding(city, b, 0);              // 0xBE2020
else DestroyInstance(nm, b);              // 0xB224F0
civ->SpendMoney(b->vt0x5C GetCost())      // 0xBEF840 (GetCost returns float in ST0)
```
Building vtable (house 0x1468220, verified): 0x20 GetNounID, 0x54/0x58 SetOwnerCity, 0x5C GetCost,
0x84 GetOwnerCity (+0x1F0). Building nouns: house 0x18EB106, factory 0x18EA2CC,
entertainment 0x1A56ABA, city hall 0x18EA1EB, turret 0x436F342 (turrets use mTurretsLayout,
turret placement path not traced - **U**).

Sell/destroy: pie command CitySellBuilding (0x37EAF27) in 0xCF3340: refund
`cf7ba0()*b->GetCost()` via 0xBEFEB0, then `0xBE3510(city, b)` removes it. **P**

---------------------------------------------------------------------------------------------
## 3. Vehicle purchase

UI: `RolloverCivCity` window proc (vtable 0x1482190), **HandleUIMessage 0xE2ADE0**
`thiscall(this, IWindow* win, const Message* msg)` ret 8. `msg+8` = 0x287259F6 (button click),
`msg+0xC` = control id; `this+0xA8` = the cCity. **V**

| Button id | locomotion |
|---|---|
| 0x62D6902 | 0 land |
| 0x62D6908 | 1 water |
| 0x62D690F | 2 air |
| 0x62D5618 | open city planner (city hall) |

Purchase sequence (identical for the 3 buttons; host can call it for any city):
```
civ = GetPlayerCivilization(); if (!city->+0x120 vt0x58 IsPlayerOwned()) return;
purpose = *(int*)(city+0x540);                         // 0xBD8300
if (VehicleSlotsLeft(civ, purpose) <= 0) return;       // 0xBF2220
cost = GetVehicleCost(civ, purpose, loco);             // 0xBF2290 (float ST0)
if (GetMoney(civ) < cost) return;                      // 0xBEF800
key  = *GetDesignKey(civ, VehicleDesignId(purpose, loco));   // 0xBF9890(0xC9E850(p,l))
v    = SpawnVehicle(city, purpose, loco, key, mode == 0x1654C05);  // 0xBDDED0
if (v) PushEvent(v, 10);                               // 0xCA8260 thiscall(v, int) ret 4
SpendMoney(civ, cost);                                 // 0xBEF840
// cosmetic: 0xCF85F0 / 0xCF9010(v) on cCivModeStrategy (hint + "vehicle built" event)
```
Vehicles are stored in `civ+0xB0` (and their spatial in `civ+0xD4`). The pie/cheat command
ids 0x321..0x32F in 0xCF3340 also spawn vehicles (`purpose = id-0x321 / 5`, `loco = % 5`)
**without** charging money. Civ start (0xCF72A0) spawns the free first vehicle at
`0xB25BA0()` city and charges it.

### cVehicle (TYPE 0x137E8E0, noun 0x18C6DE8, size 0xD98, ctor 0xCA7A20)
Vtables: main 0x14756D8, +0x34 locomotive/spatial 0x1475730, +0x508 cCombatant 0x1475830,
+0x5D0 behavior agent 0x1475658, +0x638 0x1475638, +0x650 0x1475620.
| Off | Field | Conf |
|---|---|---|
| +0x30 | mPoliticalID (set by AddVehicle) | V |
| +0x34 | cLocomotiveObject/cSpatialObject; **+0x38 position, +0x44 orientation (quat)** (GetPosition = spatial+4, GetOrientation = spatial+0x10) | V |
| +0xA0 | selected flag (spatial+0x6C) | V |
| +0x508 | cCombatant; **+0x540 HP**, +0x534 base max HP (vt0x58 GetMaxHitPoints applies modifiers) | V |
| +0xAF0 | behavior tree data (AI) | V |
| +0xB1C / +0xB20 | mLocomotion / mPurpose | V |
| +0xB2C | event deque (PushEvent 0xCA8260) | V |
| +0xB5C | mpNearestCity | P |
| +0xB68 | `vector<tVehicleOrder>` mOrders, element 0x18 B: `{+0 target obj (ref-counted), +4 Vector3 pos, +0x10 int type, +0x14 issuer politicalID}` | V |
| +0xBC8 | order flag (1, or 2 when ≥10 units ordered) | V |
| +0xBCD | mbDead | V |
| +0xD24/+0xD28 | order stamp (`0xC9EFB0(v, x)`) | P |

Spatial vtable (+0x34): 0x2C GetPosition, 0x30 GetOrientation, 0x38 SetPosition (vehicle
override 0xC9FDE0), 0x3C SetOrientation, 0x44 Teleport, 0x50 IsSelected, 0x54 SetIsSelected
(0xC9F8D0), 0x58 IsPlayerOwned, 0xE0 MoveTo (0xCA3590), 0xEC StopMovement. Combatant vt[6]
TakeDamage = **0xCAB6D0** (this = vehicle+0x508, ret 0x14, same args as the creature one).
`cVehicle::Die` **0xCAA4E0** thiscall(v, bool effects, uint32 killerPolID) ret 8
(TakeDamage calls it with (1, attacker); silent removal path uses (0, -1)).

---------------------------------------------------------------------------------------------
## 4. Vehicle orders

Input: `Simulator/cCityInputStrategy` (size 0x148, ctor 0xCF4DF0, main vtable 0x147925C,
message-listener vtable 0x147924C → HandleMessage **0xCF5690**). Owned by
`cCivModeStrategy` (global ptr **0x169E2A8**, getter 0xCF7610) at +0xE8.

| Function | Address | Signature | Conf |
|---|---|---|---|
| **Right-click on object** | 0xCF3D40 | thiscall(input, cGameData* target, ?) ret 8; classifies target (creature 0xD0036E08, building ≠ city hall, turret 0x436F315, tribe hut 0xEE02C7, spice node cCommodityNode 0x403DF5F, vehicle) and orders all selected vehicles | V |
| **Move selected to ground** | 0xCF2670 | thiscall(input) - formation positions, `SetMoveOrder` per vehicle | P |
| **Pie/command handler** | 0xCF3340 | thiscall(input, uint32 cmdId) ret 4; target object at input+0x10C (city via vt0x84), spice node at +0x11C, turret at +0x118 | V |
| **Group order** | 0xCF3200 | thiscall(input, cGameData* target, Vector3 pos /*by value*/, int type) ret 0x14; uses the vehicles collected in input+0xE8 (begin,+4 end); type 6 → per-vehicle SetMoveOrder, else SetOrder; then `0xC9EFB0(v,0)` | V |
| **cVehicle::SetOrder** | **0xCAC180** | thiscall(v, cGameData* target, int type, int flag) ret 0xC: clears orders (0xCAAA80), pushes `{target,_,type,polID}`, +0xBC8=flag, 0xC9F940 | V |
| **cVehicle::SetMoveOrder** | **0xCAC260** | thiscall(v, Vector3 pos /*by value*/, int flag) ret 0x10: order type 6 | V |
| GetCurrentOrder | 0xCA7350 | thiscall(v) → tVehicleOrder* | V |
| CanReach/CanTarget city | 0xCA84C0 | thiscall(v, cCity*) → bool | P |
| Can reach position | 0xC9EAE0 | thiscall(v, const Vector3*) → bool | P |
| Can claim node | 0xC9FFC0 | thiscall(v, obj) → bool | P |

Order types (from the call sites): **1** attack target (unit/building/turret/creature, enemy
spice node), **2** military attack city (cmd VehicleAttack 0x37E95B9, military vehicles),
**3** religious attack city (cmd CityCultureAttack 0x37E95BA, purpose 1), **5** trade with city
(cmd CityTradeWithCity 0x37E95BD, economic), **6** move to position, **8** claim free spice
node / act on tribe hut, **9** create trade route (cmd 0x37E95BC), **11** bribe spice node
(cmd BribeNode 0x51B40A4). 0, 4, 7, 10 are used only by the civ AI (0xBF5DD0, 0xBF8830…) - **U**.
Command name table (id → name) is registered in 0xCF47B0 (e.g. 0x37E8514 CityBuyVehicle,
0x37E95BB BuyCity, 0x37EAF27 CitySellBuilding, 0x406D0D8 CityPosseSelect).

Programmatic order (host, for vehicle `v`):
`SetOrder(v, target, type, 1); 0xC9EFB0(v, 0)` or `SetMoveOrder(v, pos, 1)`.
Target must be the cGameData* of the object (city, building, vehicle, cCommodityNode…).

---------------------------------------------------------------------------------------------
## 5. Vehicle per-frame update (AI) and direct pose

Civ vehicles are ticked by a simulator strategy, **0xDCEDB0** (vtable 0x147CF44, slot 13
`Update(uint64 dtMs)`): for every vehicle of `GetAllVehicles()`:
`0xCAC890` (one-time init) → `0xC9F450(v, dt)` weapon timers → `0xCA9830` timers/events →
`0xC9E880` → **behavior tree tick `0xBC9140`** (only if `v+0x5D0 vt0x20()==0` and
`[v+0xAF0]` valid) → `0xC9F740` UI state → `0xCA3370` visual update. **V**

* **AI = behavior tree**: `0xBC9140` cdecl `(float dt, double time, treeDef, agent, …)`,
  9 dwords, **agent (the cVehicle*) at [esp+0x14] on entry** (ret addr at [esp]). Shared with
  creatures/scenario (callers 0xC38880, 0xC69810, 0xCE4A60, 0xD995C0, 0xF39DF0), so filter by
  agent. Hooking it and returning for mirror vehicles stops orders, movement goals and
  weapon use. **V** (layout), runtime test needed.
* Alternative: hook 0xDCEDB0 is too coarse (all vehicles).
* Pose: `(v+0x34)->vt0x44 Teleport(&pos, &quat)` or vt0x38/0x3C; call vt0xEC StopMovement
  first so the locomotion does not pull it back. Read pose from v+0x38 / v+0x44.
* Death on mirrors: `cVehicle::Die(v, 1, killerPol)`; damage: TakeDamage 0xCAB6D0.

---------------------------------------------------------------------------------------------
## 6. City capture / ownership change

| Function | Address | Signature | Conf |
|---|---|---|---|
| **Capture** | 0xBE9410 | thiscall(city, uint32 newPolID, int captureType, a3, a4, a5) ret 0x14; writes +0x29C, returns if already owned, then ChangeOwner (+cinematics) | P (first 2 args V) |
| **ChangeOwner** | 0xBE8A00 | thiscall(city, uint32 newPolID, bool, bool) ret 0xC: civ = GetCivilizationByPoliticalID; `RemoveCity(old)`, `AddCity(new)` (sets +0x590 + political id of city), capture counters, +0x33C mbIsPlayerCity, notifications | V |
| military capture check | 0xBE9AB0 | thiscall(city): if `+0x748 ≥ 1.0` and `+0x74C` ≠ owner → Capture(+0x74C, 0) | V |
| religious conversion check | 0xBEAB60 | `+0x750 ≥ 1.0` → Capture(+0x754, 1) | V |
| economic buy | 0xBE9980 | thiscall(city, uint32 buyerPolID, float price) ret 8 → Capture(…, 2) | V |
| other capture caller | 0xBE9DE0 | (called from 0xBEAA60, 0xBEACE0, 0xBEB130, 0xCFF000) | U |
| cheat path | 0xCFDEA0 | `ChangeOwner(city, playerCiv->GetPoliticalID(), 0, 0)` | V |

What changes: city+0x590 (civ), city+0x30 political id, civ mCities lists, +0x29C,
+0x33C, civ capture counters. The buildings keep their objects (owner city unchanged).

---------------------------------------------------------------------------------------------
## 7. Selection

* Terrain cursor = selection holder: `cGameTerrainCursor::GetTerrainCursor` **0xB30E80**
  (cdecl, global 0x167F784). Main vtable 0x1460A98: **vt0x5C GetSelected(vector<cSpatialObject*>*)**
  (0xB310C0: copies objects of `cursor+0x164` list whose `vt0x50 IsSelected()` is true),
  **vt0x60 GetSelectedOfType(vector*, typeId)**. `cursor+0x164` = pointer to the selectable
  object vector (in civ = civ+0xD4). **V**
* Selection state is the per-object flag set through spatial vt0x54. For vehicles:
  **`cVehicle::SetIsSelected` 0xC9F8D0** thiscall(v+0x34, bool) ret 4, flag at v+0xA0. **V**
* Selected → vehicle: `0xBD8570` cdecl(spatial) → cVehicle* (used on every selection entry).

---------------------------------------------------------------------------------------------
## 8. Implementation recipe (co-op "governors")

Host simulates everything; guests run mirrors. Mod-side tables (host-authoritative):
`city_owner[cityKey] = playerId` (capital = host), `vehicle_owner[netId] = playerId`.

**Hooks (both peers unless noted)**
1. `cVehicle::SetIsSelected` 0xC9F8D0: if `bool==true` and the vehicle is not owned by the local
   player → ignore (return). Keeps foreign vehicles out of every selection/order path.
2. RolloverCivCity `HandleUIMessage` 0xE2ADE0: on click 0x62D6902/08/0F
   * city not owned by the local player → consume (return 1) + hint "city of Alice";
   * guest → send `CIV_BUY{cityKey, loco}` and consume; host → let it run, then tag the new
     vehicle (hook SpawnVehicle with return address 0xE2B05B/0xE2B265/0xE2B3E7 (land/water/air) or diff
     `civ+0xB0`) with owner = local host player.
   Same check for button 0x62D5618 (city planner) on foreign cities.
3. City planner placement: hook `AddBuilding` 0xBE2020 with return address **0xD1419C**:
   guest → read `b->GetNounID()` and position (b+0x38), send `CIV_BUILD{cityKey, noun, pos}`
   (the local building is replaced by the host's result; or let it stay and reconcile by
   slot). Host executes the §2 sequence.
4. Orders, guest: hook `SetOrder` 0xCAC180 and `SetMoveOrder` 0xCAC260; when the return
   address is inside the input strategy (0xCF1000–0xCF7000) send
   `CIV_ORDER{netId, type, targetRef, pos, flag}` and skip the original. Host: owner check,
   then call the same function on the real vehicle (+ `0xC9EFB0(v,0)`).
   targetRef = {kind, key}: city / building(city,slot) / vehicle netId / spice node or other
   object by position (nearest object of that TYPE).
5. Mirrors (guest): create with `CreateVehicle(civ, purpose, loco, key, &pos, 0)` 0xBF58D0
   on the guest's player civ; hook `0xBC9140` and return when `[esp+0x14]` is a mirror;
   apply `CIV_VEH_STATE{netId, pos, quat, hp, flags}` at 10 Hz via StopMovement + Teleport
   and write HP at v+0x540; `CIV_VEH_DESPAWN` → `Die(v, 1, -1)`.
   Guest must not create vehicles of its own: block `SpawnVehicle` 0xBDDED0 on guests
   (catches the free start vehicle 0xCF72A0 and the cheat ids) unless the mod spawns a mirror.
6. Capture (guest): hook `Capture` 0xBE9410 and return unless applying a host message;
   host: hook it (post) and broadcast `CIV_CITY_OWNER{cityKey, newPolID, type}` → guest calls
   `ChangeOwner(city, pol, 0, 0)` 0xBE8A00. New owner player of a captured city = owner of
   the vehicle whose political attack captured it (host knows from `+0x74C` / the attacking
   vehicles near the city).
7. Money is already shared (civ+0x98); host-side purchases call SpendMoney themselves.

**Messages**: CIV_BUY, CIV_BUILD, CIV_ORDER, CIV_VEH_SPAWN{netId, owner, purpose, loco, key,
pos}, CIV_VEH_STATE, CIV_VEH_DESPAWN, CIV_CITY_OWNER, CIV_BUILDING_ADDED/REMOVED{cityKey,
slot, noun}, CIV_OWNERSHIP{city→player table}.

---------------------------------------------------------------------------------------------
## Open questions / unverified
* Whole-planet consistency on guests: NPC civs, their vehicles and city growth still run on
  guests. Done (2026-10-23): AI growth (`CreateBuilding` from 0xBE5BAE / 0xBE6169) and new cities
  (0xAE3A80) are blocked on guests, buildings / spice nodes / capture meter come from the host
  (§9). Still local: the NPC AI's vehicle spawns (killed by the next snapshot; `SpawnVehicle`
  callers 0xBE7643 / 0xBFBCF7 do not check for null, so it cannot simply be blocked) and its
  budgets (civ AI entry points 0xBF5DD0, 0xBF6930, 0xBF8830, 0xBFB140).
* Exact meaning of SetOrder `flag` (1/2) and of order types 0/4/7/10.
* Planner commit: where the slot index lives at commit time (Ghidra shows a local slot index
  ≠ -1) and how the editor budget `[editor+0x68]` is settled with civ money.
* Turret placement path and mTurretsLayout slots.
* Whether cGameData mID (+0x24) is persisted in the save (would be a better network key).
* Capture() args 3–5, RemoveCity 2nd arg, BuyCity 0xBF0500 args.
* Civ vehicle-list removal on death (Die only marks dead/effects; list cleanup not traced).

---------------------------------------------------------------------------------------------
## Co-op features (siege, roles, trade, alarm, superweapon)

Static analysis (llvm-objdump of the full exe), 2026-10-22. Marks in this section:
**✔** verified (prologue, `ret N`, call sites), **~** likely (role clear, details inferred),
**?** unknown. All political ids below are civ political ids - in co-op every player shares
the one player civ, so the game itself can never tell *which player* acted: the mod must take
that from the attacking vehicle (TakeDamage arg 5) or from its own owner tables.

**Correction to §6:** the capture/convert/buy percentages run **0..100** (constant 100.0 at
0x13EC4A8), not 0..1. `0xBE9AB0` and `0xBEAB60` capture at `>= 100`.

### A. Military siege (capture progress)

City fields: **+0x748 float mCapturePercent (0..100)**, **+0x74C mCaptureID** (attacker pol,
-1 = none), +0x2F0 capture-meter object (UI, `0xBD8320` getter), **+0x98 + 8·kind uint64
last-attack time** (kind 0 military, 1 religious, 2 economic), +0xB0 "under attack" event
cooldown timer, +0x200/+0x238 hit-effect timers on buildings.

Damage flow (vehicle shot → building combatant, `this` = building+0x120, args
`(float dmg, uint32 attackerPol, int dmgType, const Vector3* dir, cCombatant* attacker)`
ret 0x14 - same as the creature/UFO TakeDamage):

| Function | Address | Signature / role | Conf |
|---|---|---|---|
| building TakeDamage (house, factory, ent., …) | 0xBCEE30 | combatant vt[6] in vtables 0x1467A10/E80/80C0/82F8/8588. Generic HP damage via `0xBFCEF0` (same args, ret 0x14); when the building **becomes destroyed** (`0x8E8220(comb)` = comb+0x34 state == 2) it adds `0xBD80E0(attackerPol,0) × k` capture damage through AddCaptureDamage | ✔ |
| **city hall TakeDamage** | 0xBCF840 | combatant vt[6] at 0x1467C60 (vtable 0x1467C48). In civ mode: relationship hit `0xD062B0(owner, attacker, 0x526E501, 1.0)`, then **only if `AreAllBuildingsDestroyed(city)` (or dmgType 0xD)** → `AddCaptureDamage(…, dmg, attackerPol, type==0xD)`. dmgType 0xC → `0xBE2CE0(city, dmg, pol)` (queued city event 0x5F601F4, 2 s), 0xB → only restarts timer city+0x230 | ✔ |
| turret TakeDamage | 0xBD0640 | vt[6] at 0x1467758 (`this` = turret+0x588), ret 0x14; HP only, no capture progress | ✔ (ret/vtable) |
| building "attacked by" | 0xBCD9E0 | combatant vt[7] thiscall(cCombatant* attacker) ret 4: attacker → pol (military vehicle `+0xB20==0`, UFO 0xB033B403, creature 0xD0036E08), `civ->0xBF03D0(pol,pos)`, **`city->OnAttacked(0, pol)`** (turret version 0xBCE7B0 at 0x146775C does the same) | ✔ |
| `AreAllBuildingsDestroyed` | 0xBD9E50 | thiscall(city) → bool: every building except the city hall (+0x320) has state 2 | ✔ |
| **AddCaptureDamage** | **0xBE9C50** | **cdecl** `(planetCtx /*0x1021150()*/, cCity*, meter /*city+0x2F0*/, uint32 ownerPol, float dmg, uint32 attackerPol, bool flag)` (caller `add esp,0x1C`). `p = +0x748; id = +0x74C (-1→attacker); p += dmg·0.1 if id==attacker else p -= dmg·0.1 (negative → flips to attacker); clamp 100` → `SetCaptureProgress`; civ mode → `CheckMilitaryCapture(city, flag)` | ✔ |
| SetCaptureProgress | 0xBD7E40 | thiscall(city, uint32 pol, float pct) ret 8: writes +0x74C/+0x748, updates the meter | ✔ |
| CheckMilitaryCapture | 0xBE9AB0 | thiscall(city, bool flag) ret 4: `+0x748 >= 100` and `+0x74C` ≠ owner → event (0xEDD4A52F lost / 0x43D2843C won, group 0x2BB7AFF), clears the per-pol maps +0x5A0/+0x5BC/+0x5D8 and +0x98, `Capture(pol, 0, 0, flag, 0)` | ✔ |
| UpdateCaptureDecay | 0xBE2240 | thiscall(city) ret (per city tick from 0xAE57F8): if **not** `IsUnderAttack(0)` → `+0x748 -= 0.1` per tick (→0 resets +0x74C); recomputes religious +0x750 (see B); drops ids of dead civs | ✔ |

**Goal: faster capture when 2+ players attack.** Hook the two TakeDamage functions
(0xBCEE30, 0xBCF840; `thiscall`, ret 0x14): vehicle = `attacker->vt[0x5C](0x137E8E0)`
(the combatant's cast, as 0xBCD9E0 does; or `attacker - 0x508`), record
`city → {player → last hit time}`; when ≥2 players hit the city in the last N s, scale arg 1
`dmg` (this speeds both building destruction and the city-hall capture), **or** hook
`AddCaptureDamage` 0xBE9C50 (cdecl, arg 5 at [esp+0x14]) and scale only the capture part
(the TakeDamage hook sets a "current attacker" for it, the call is synchronous). Host only;
the guests get the capture via the existing CIV_CITY_OWNER path. A manual top-up is also
possible: `SetCaptureProgress(city, pol, p)` then `CheckMilitaryCapture(city, 0)`.

### B. Religious conversion

City fields: **+0x5A0 `map<uint32 pol, int>` followers** (citizens per religion; owner's
own count included), +0x5BC / +0x5D8 per-pol maps (converted-from-owner / converted-from-rival
counters, cleared each 0xBEB800 pass), **+0x750 mConvertPercent**, **+0x754 mConvertID**,
+0x618 vector of 0x80-byte "preacher slots", +0x62C their objects (+0x1DC = pol, slot+0x70 =
the religious cVehicle).

* `+0x750` is **recomputed every tick** in 0xBE2240:
  `200·followers[convertID] / (followers[convertID] + followers[owner])`, clamped 0..100:
  so writing +0x750 has no lasting effect; the state is the followers map. ✔
* **ConvertCitizens 0xBEB1C0** - thiscall(city, uint32 converterPol, int count) ret 8: moves
  up to `count` followers from the owner (or from a rival converter) to `converterPol`, sets
  +0x754, notifies (0xB3D550→0xAC8BD0) and calls `CheckReligiousConversion` 0xBEAB60 ✔.
  Called only from the per-slot preach tick 0xBEB2F0 (thiscall(city, int slot)), which first
  calls `OnAttacked(1, pol)` and computes `count` from tuning 0x169D235 (0xCEEA30…0xCEEB70) ~.
  ~ superseded (2026-11-19): see the CORRECTION below and [civ-influence-relay](#civ-influence-relay).
* CORRECTION (2026-11-19, fix/w56-sync): on GOG 2024 the preach tick is **0xBEB310** thiscall(city, int slot, int
  activePreachers) with stack purge 8 (ret 8 at 0xBEB708; 0xBEB2F0 / one argument was the GOG 2016 reading): its only
  caller 0xBEB710 (call at 0xBEB7EA) passes the slot index and the count of the city's preaching objects (+0x62C) whose
  +0x1D8 == 2. Ghidra (tools/ghidra/q.sh, info / dec / callers). ConvertCitizens on GOG 2024 is 0xBEB1E0 (.sym). The
  Lineages fork had it right (fork BUG-9086).
* CONFLICT (2026-11-19, docs/lineages-re): ours (✔ above) says ConvertCitizens moves followers from the owner or
  from a rival converter to `converterPol`; the Lineages fork (GOG 2024 disassembly, fork BUG-9078) says that when the
  caller differs from the current converter +0x754 (compare at 0xBEB216) the code at 0xBEB244..0xBEB268 first takes
  followers from the current converter and gives them back to the city's owner, and only a contribution left over
  after that reversal switches +0x754 to the new converter; a nation preaching in a city another nation converts
  gains nothing at first (its runtime fixture saw 0 followers gained). See
  [civ-lineages-religion](#civ-lineages-religion); not settled.
* **Hook point (religious role bonus): 0xBEB1C0**, scale `count` ([esp+8]) when the slot's
  vehicle (find via +0x62C/+0x618, or simply the religious vehicle of the bonus player nearest
  the city) belongs to a "religious" player. ~ (the slot→vehicle mapping is not fully traced)

### C. Economic: buy progress, buying, trade routes, trade income

City fields: **+0x55C `vector<{cCity* partner, route object*, int type}>` trade routes**
(stride 0xC), +0x570 `map<uint32 pol, float>` (entry created when a route with that civ
exists; GetBuyPrice returns -1 without it), **+0x758 mBuyPercent / +0x75C mBuyID**.

| Function | Address | Signature / role | Conf |
|---|---|---|---|
| **TradeDelivery** | 0xBE1990 | thiscall(city, uint32 traderPol, float cargo, cVehicle*) ret 0xC → float ST0 (next cargo, tuning 0xCEEC20). `x = cargo/6` (×1/3 if traderPol == city owner, i.e. own-civ trade); city's civ and trader's civ get `AddMoney` (×8 / ×4, with difficulty) + money popups | ✔ sig, ~ formula |
| AddMoney | 0xBEFEB0 | thiscall(civ, float, bool applyDifficulty) ret 8; mWealth clamp 999999 (fixes §1 "(float)") | ✔ |
| GetEconomicInfluence | 0xBD9570 | thiscall(city, float vehiclePower /*v+0xC50*/) ret 4 → ST0 (tuning 0xCEEC70(#buildings, +0x64C, x)) | ✔ |
| **AddBuyProgress** | 0xBDD250 | thiscall(city, float amount, uint32 pol) ret 8: returns if pol==owner; `+0x758 ±= amount·8/(buyerCityCount+6)`, clamp 100; at 100 for the player: message 0x53039B6 + event 0x927DCA97 | ✔ |
| economic vehicle trade behaviour | 0xDCC830 | cdecl(cVehicle*, state*) → bool: shuttles between home city `0xC9F550(v)` (= v+0xB5C) and the order target city; requires `HasTradeRouteWith(target, vehiclePol)`; at target: `TradeDelivery` + `AddBuyProgress(target, GetEconomicInfluence(v+0xC50), pol)`; at home: `TradeDelivery` | ✔ |
| HasTradeRouteWith | 0xBDB330 | thiscall(city, uint32 pol) ret 4 → bool (any route partner of civ `pol`) | ✔ |
| **AddTradeRoute** | 0xBE1470 | thiscall(city, cCity* other, int routeType) ret 8: pushes the entry on **both** cities, `routeType 0` creates the ground route object (noun 0x2B8A4E7, cast 0x2BCA14B, init 0xC41DA0), space computes 1/2; event 0x3F5A88F8 for the player. No same-civ check | ✔ sig, ~ type |
| AddTradeRouteWithCiv | 0xBE1950 | thiscall(city, uint32 pol) ret 4: `0xBD9400(city, civ)` picks that civ's city → AddTradeRoute(it, 0). Used by the economic vehicle "create route" behaviour 0xDCA100 (order 9) | ✔ |
| RemoveTradeRoute | 0xBDFA50 | thiscall(city, cCity*) ret 4 (one side, destroys the route object) | ✔ |
| RemoveTradeRoutesWithCiv | 0xBDFB10 | thiscall(city, uint32 pol) ret 4 → partner; called by ChangeOwner (+0xBE8EE8) | ✔ |
| GetBuyPrice | 0xBE9380 | thiscall(city, uint32 buyerPol) ret 4 → ST0; -1 if no +0x570 entry (no trade route), else tuning 0xCEECD0(entry, 0xBE6EC0(city, cityCount)) | ✔ |
| cCivilization::BuyCity | 0xBF0500 | thiscall(civ, cCity*, float price) ret 8: only starts it - civ+0x440 pending city, +0x448 price, cinematic; **FinishBuyCity 0xBF05E0** thiscall(civ, bool ok) ret 4 → `cCity 0xBE9980(city, civPol, price)` | ✔ |
| cCity buy | 0xBE9980 | thiscall(city, uint32 buyerPol, float price /*0 = GetBuyPrice*/) ret 8: SpendMoney if affordable, `Capture(pol, 2, 0, 0, 0)` | ✔ |
| GetCityCount | 0xBF0300 | thiscall(civ) → `(+0xA0 - +0x9C) / 4` | ✔ |

Pie command BuyCity (0x37E95BB) → `BuyCity(playerCiv, city, GetBuyPrice(city, playerPol))`
(0xCF3600). The co-op players never buy each other's cities (same civ).

**Trade route between two players' cities.** Both cities are in the same civ, so the native
"trade with civ" path does not apply. Options:
1. ~ `AddTradeRoute(cityA, cityB, 0)` 0xBE1470 (host; guests replay it - visual route) and
   order an economic vehicle `SetOrder(v, cityB, 5, 1)`: HasTradeRouteWith(cityB, ownPol) is
   then true, TradeDelivery pays ×1/3 (own-civ), AddBuyProgress returns early. Untested: the
   input strategy may refuse order 5 on an own city, and ChangeOwner removes routes.
2. **Recommended:** keep the co-op route in the mod (visual via AddTradeRoute or a toast only)
   and pay income from the host: every N s `AddMoney(civ, k·f(cities), true)` 0xBEFEB0.
   Economic role bonus: hook `TradeDelivery` 0xBE1990 (scale `cargo`, [esp+8]) or AddMoney
   calls with return address inside 0xBE1990.

### D. Vehicle speed

* **v+0x218 float = max speed** (= locomotive(v+0x34)+0x1E4, mStandardSpeed). Written by
  `cVehicle::RecalcStats` 0xCA40A0 (thiscall, plain ret; tuning 0x169D9A8 `0xCEED60(loco,
  power +0xC50, speed +0xC54, health +0xC58)` → +0xC5C damage, +0x218 speed, +0x534 max HP)
  and by Load 0xCA67B0. ✔
* `cVehicle::GetMaxSpeed` 0xC9F500 thiscall(v) → ST0 `v+0x218 × (adventure mult.)`, used by
  the movement code (10 callers 0xCA9ADF…0xCB3299); MoveTo 0xCA3590 (spatial vt 0xE0) reads
  v+0x218 directly and passes it to spatial **vt 0xC4 SetDesiredSpeed(float, int)** ret 8
  (0xC9EE60 → 0xC41E60: locomotive+0x1E0 = v+0x214). Getters: vt 0xC8 → +0x1E0, vt 0xCC → +0x1E4. ✔
* **Temporary boost:** save `v+0x218`, write `old·k`, and for an already moving vehicle
  also `(v+0x34)->vt0xC4(newSpeed, 0)` (or re-issue its move order); restore later. RecalcStats
  (model reload/upgrade) overwrites it - re-apply after. Mirrors on guests do not need it
  (pose comes from the host). ~ (effect on the per-tick locomotion not run-time tested)

### E. "Your city is under attack"

* **cCity::OnAttacked 0xC00C20** - thiscall(city, int kind, uint32 attackerPol) ret 8. ✔
  * if the city was **not** attacked (that kind) in the last 20 s (or mode is 0x1654C02):
    sends message **0x30AA01B** through the message manager `0x883910()` (= [0x16524AC],
    vt 0x14 `MessageSend(id, msg, 0)`); payload: `+8 cCity*`, `+0x18 kind`, `+0x20 attackerPol`.
    Handled e.g. by the UI proc 0xE11AB0. ✔ (`0x883910` vs `App::IMessageManager::Get`
    0x67DC80 ([0x15FE888]) - different globals, which one AddListener must use is **?**)
  * stores `now` at city+0x98+8·kind (IsUnderAttack reads it);
  * civ mode, kind ≠ 2, city owner == player pol, cooldown city+0xB0 > 15 s →
    `cCivModeStrategy 0xCF9190(city, 0x38320B95, 0xAA9A8ED7, &pos, …)` = the event-log entry
    "Your city under attack" (+ tutorial/music side effects), restarts +0xB0. ✔
  * callers: building/turret vt[7] 0xBCD9E0/0xBCE7B0 (kind 0, military hits), preach tick
    0xBEB2F0 (kind 1), city-hall space path 0xBCFFD1, tribe 0xC8E8A7.
* `cCity::IsUnderAttack` **0xC005E0** thiscall(city, int kind) ret 4 → bool
  (`now - stamp[kind] < 20000 ms`). ✔
* The event is hidden by `cUIEventLog 0xDD7620(log, 0x38320B95, 0, nameHash)` in ChangeOwner
  (0xBE8B3D) and in city tick 0xBEB800 ~15 s after the last attack. ~
* **Hook point:** `OnAttacked` 0xC00C20 (host): when `kind ∈ {0,1}` and
  `!IsUnderAttack(city, kind)` (edge) → look up the city's co-op owner and send a toast /
  CIV_CITY_ALARM to that player (attackerPol tells friend/foe; NPC civs attack with their own
  pol). Polling alternative without hooks: `IsUnderAttack(city, 0/1)` for every city each 1 s.

### F. Superweapons

Civ fields: +0x484 `bool unlocked[12]`, +0x490 per-type cooldown timers (stride 0x20),
+0x4CC "global weapon used" flag; global 0x168CD4C = cheat unlockSuperWeapons (free, always
usable).

| Function | Address | Signature / role | Conf |
|---|---|---|---|
| CanUseSuperweapon | 0xBF8B20 | thiscall(civ, int type) ret 4 → bool: unlocked && affordable (`0xBF7850`: mWealth ≥ cost) && no cooldown (`0xBF18B0`); types 2/5/8 only once (+0x4CC) | ✔ |
| **GetSuperweaponCost** | 0xBF6DD0 | thiscall(civ, int type) ret 4 → ST0. type 0/6/11: 4000; 1/4/7: 16000; 3/9/10: 2000; **2/5/8: 12000 × max(1, cities not owned by this civ)** (the planet-wide ones) | ✔ |
| **FireSuperweapon** | 0xBF8B90 | thiscall(civ, int type, const Vector3* target) ret 8: CanUse → picks the own city that can launch (`0xBDA1B0(city, type)`) nearest to target (target = zero vector 0x169DC6C for untargeted) → `city 0xBE5D10(type, target)` launches; `mWealth -= cost`; restarts cooldown; sets +0x4CC for 2/5/8; player event (table 0x1469DA8) | ✔ |
| SelectSuperweapon (UI) | 0xCF1D30 | cCityInputStrategy thiscall(input, int type) ret 4: types 2/5/8 fire at once (`FireSuperweapon(civ, type, &zero)`, return addr **0xCF1DC4**); others arm the targeting cursor (command 0x58B3913 "SuperweaponActivate", input+0x140) | ✔ |
| targeted fire on click | 0xCF5230 | `0xBDA420(type, &pos)` valid → `FireSuperweapon(playerCiv, input+0x104, &pos)` (return addr **0xCF68C5**) | ✔ |
| UI button | 0xE05568 | superweapon panel calls SelectSuperweapon; 0xE0615F checks CanUse for the button state | ~ |

Type → name mapping is **?** (12 types; 2/5/8 are the "global" ones).

**Hook point (vote):** hook `FireSuperweapon` 0xBF8B90. When the call comes from the local
UI (return address 0xCF1DC4 or 0xCF68C5) and the mod is not replaying: consume it (return),
send `CIV_SW_PROPOSE{type, target}` (guest → host), host opens the vote (cost check via
`CanUseSuperweapon`/`GetSuperweaponCost`). On a pass the host calls the original
`FireSuperweapon(playerCiv, type, &target)` (host simulates; money is shared). Guests must
always consume local fires (they only mirror the results).

### G. Giving a vehicle to another player

* The only native "control" test is `IsPlayerOwned` (vehicle spatial vt 0x58 = **0xC9E810**:
  `v->GetPoliticalID() == cGameNounManager 0xB1F8E0 GetPlayerPoliticalID()`), plus membership
  in `civ+0xB0` / `civ+0xD4` (selectable list read by the terrain cursor). ✔
* All co-op players share the player civ, so **nothing in the game blocks** a guest-owned
  vehicle: re-assigning is purely the mod's `vehicle_owner` table (+ the SetIsSelected hook
  of §8.1). Orders store the issuer pol (order +0x14) but it is not checked against an owner. ~
* Moving a vehicle to a *different* civ would need `AddVehicle` 0xBF5750 on the new civ and
  removal from the old `civ+0xB0`/`+0xD4` (removal function not found - **?**).

### H. Summary of recommended hook points (host side)

| Goal | Hook | Notes |
|---|---|---|
| Faster siege with 2+ players | 0xBCEE30 + 0xBCF840 (attacker id, scale dmg) and/or 0xBE9C50 (scale capture dmg) | building TakeDamage `this`=building+0x120, ret 0x14 |
| Military role bonus | same, per attacker player | |
| Religious role bonus | 0xBEB1C0 ConvertCitizens (scale count) | |
| Economic role bonus | 0xBE1990 TradeDelivery (scale cargo) / 0xBDD250 | |
| Co-op trade route | mod-side route + periodic `AddMoney` 0xBEFEB0 (option 2), visual via 0xBE1470 | |
| Speed buff | write v+0x218 (+ spatial vt 0xC4) | restore after; RecalcStats 0xCA40A0 resets |
| City alarm | 0xC00C20 OnAttacked (edge via 0xC005E0) | kinds 0 mil, 1 rel |
| Superweapon vote | 0xBF8B90 FireSuperweapon (ret addrs 0xCF1DC4 / 0xCF68C5) | |

## NPC cities: how the other nations' cities appear (2026-10-23)

A Civilization game started from the stage chooser has only the player city ("soon other cities
will arise"); `GetAllCities` lists the others only once they appeared. Static analysis
(disassembly) plus runtime checks on lane 5:

* `cCitySimulator` singleton in **0x167B5EC**, getter **0xB268A0** cdecl() (creates it) ✔.
  Vtable 0x145C578, slot +0x10 = Update **0xAE6190** (thiscall ret 8), runs once per 1 s of game
  time. Fields (reflection names): +0x14 `mCityAppearanceInfo` (vector, stride 0x64: +0x2C order,
  +0x30 hasAppeared, +0x38 owner object, +0x40 instant), +0x28 pending appearances, +0x58
  `mCityAppearanceTimer`, **+0xAC `mNumCitiesTotal`, +0xB0 `mNumCitiesNotAppearedYet`** (runtime:
  10 and 9 at the start of a civ game, the player city is entry 0), +0xB4 "blocked" byte ~.
  Filled at civ start by 0xB9D950 (from 0xBA1D90 for mode 0x1654C04) ~.
* **ShouldNextCityAppear 0xAE2EC0** thiscall ret 0 ~: false when +0xB4 or no player civ; needs the
  player flag `{0x521F703, 0, 0xAA9A8ED7}` in `player+0x38` (set when the player claims a spice node,
  0xBFF3F0, or buys enough vehicles, 0xCF9010: more than 5/3/1 on easy/normal/hard; setter 0xC785A0
  thiscall(player, const uint32 key[3]) ret 4) and net worth (0xBF2C00) > 9000/7000/0 or income
  (0xBF70C0) > 2400/1600/0; then the timer: tuning 0x4CDAD7D (20.0 s in prop 0xB6A5C63D) x3 on easy,
  /24 on hard, x1.5 / /2 by how many appeared. Difficulty = `*(int*)(*(nm+0x74)+0x1248)` ~.
  Runtime: with an idle vehicle and $3000 nothing appeared in 20 minutes; after claiming a spice
  geyser the first NPC city came about 5 minutes later, when the money had grown to about $8000 ✔.
* **AppearNextCity 0xAE3A80** thiscall(cs, **bool instant, void* who**) ret 8 -> bool (al) ✔ runtime:
  `who` = 0 takes the next city by appearance order, a non-zero `who` is dereferenced (who=1 crashed
  the game: the argument order is instant first). Decrements +0xB0; the city object (cCity, only
  factory 0xBD9EA0 cdecl(ptr, int)) is created by the next Update tick; instant=1 skips the visual
  effect and the camera cinematic "CVG_PRE_NewCityAppears". Three calls gave three NPC cities
  (political ids 0x21CB..0x21CD) at 400 to 800 m from the player city on a planet of radius ~500, even
  while the intro text still paused the game. `new_stage_game.py civ --npc-cities N` uses it.
* No cheat creates or hurries these cities (negative result, cheat strings checked).

---------------------------------------------------------------------------------------------
## 9. Found while two bots played the stage (tools/tests/play_civ.py, 2026-10-23)

Runtime findings (✔ = seen working in the game), with what the co-op code does with them.

### 9.1 UI a player (and a bot) uses
* **Own city menu** (click the city hall): buttons are windows with these control ids (`coop.ui where
  <id>` gives their screen rect, `enabled=0` when greyed): planner 0x062D5618, land 0x062D6902,
  sea 0x062D6908, air 0x062D690F. Greyed buy buttons = no vehicle slot left (or no harbour). ✔
* **Another nation's city menu** (left click on its city): the window ids are the command ids of
  §4: attack 0x037E95B9 (the crosshair, order type 2 for the selected military vehicles), and a
  top button 0x051B4174 (gift); religious / trade buttons show for those vehicles. **A right click
  on a foreign city gives no order at all**: attacks go through the menu. ✔
* **City planner**: open = window 0x272EB68E (city name, bottom) visible; the green check at
  (1412, 1054) of a 1440x1080 window closes it. In a new game (civ_npc) the house / factory /
  entertainment tiles have no design yet (a blue badge): a click on the tile opens the building
  editor (an editor request: the co-op vote), so placing a building through the planner needs a
  design first. Not automated yet. ✔
* **Pause menu**: Esc on the planet opens it (window 0x064C23F8 visible), Esc again closes it. ✔
* **Camera**: W/A/S/D pan (the camera glides on ~0.5 s after the key is released), the wheel zooms
  (down to the ground, where the view tilts to the horizon). `coop.cam` rows: view[0] right,
  view[1] forward, view[2] up, view[3] position; viewWindow = half extents at distance 1, so
  `x = 720 + (d·right)/(d·fwd)/vw.x·720`, `y = 540 − (d·up)/(d·fwd)/vw.y·540`. ✔
* **Captions**: a captured city plays a cinematic with a caption ("You've captured this military
  city by military means...") and a green check; **the host's whole world stands still until OK**
  (cCinematicManager +0x2C ≠ 0; vehicles keep order 6/12 and do not move). Enter = OK. ✔

### 9.2 Spice nodes (cCommodityNode)
* Noun 0x403DF5C, TYPE 0x403DF5F, main vtable 0x146A5D8 (vt 0x48 SetPoliticalID 0xBFE080: writes
  +0x30 and the mine object at +0x288), +0x30 owner political id (-1 free). The serializer names
  that follow the vtable: mConstructingVehicle, mBuyPoliticalID, mConvertPoliticalID,
  mCapturePoliticalID, mConstructingPoliticalID, mMineStateTime. ✔
* SetOwner 0xBFF3F0 thiscall(node, pol, reason) ret 8; **Claim 0xBFF800** thiscall(node, pol) ret 4
  (SetOwner + model 0xBFE650 + mine state +0x1EC = 3); **Unclaim 0xBFF870** thiscall(node). Claims
  happen in the vehicle claim behaviour (0xBC012F/0xBC0244). Calling Claim on a guest shows the
  derrick and costs nothing. ✔ (applied from the host snapshot)
* A claimed node earns money (cCivilization::AddMoney 0xBEFEB0, +70..+100 every few seconds). The
  guest's copy of the derricks (and of the save's claimed nodes) earned it too and shared.cpp sent
  every local gain to the host: double income (~8000 extra in 10 minutes, found 2026-10-23).
  Guests now ignore AddMoney for the player civ; `coop.res` on the host shows `from_guests=+X/-Y`. ✔

### 9.3 New cities of other nations
* The planet has fixed city sites; a manager (singleton 0x167B5EC, `0xB268A0` Get, tick 0xAE6190)
  calls **SpawnNextCity 0xAE3A80** thiscall(mgr, 0, 0) ret 8 → picks the free site with the lowest
  order (+0x14 records, stride 0x64, +0x30 used), and the city appears a few seconds later
  (0xB9FAE0 → timed callback 0xB9C6E0 → 0xB999C0 makes the cCity from the record, AddCity
  0xBF4490). Seen at runtime: 4-6 new cities in 10 minutes on Taldon. ✔
* Without co-op code the guest's tick founded cities on its own: sometimes the same as the host's,
  once a city the host never had. Now only the host founds; guests replay SpawnNextCity in the
  same order (same free site, same new political id). ✔

### 9.4 Buildings in a siege
* Building combatant at +0x120: **state +0x154 (2 = destroyed / ruin), hp +0x158**. Destroyed
  buildings stay as ruins while the city is besieged; a capture removes them on the host.
* **DestroyBuilding 0xBE3510** thiscall(city, building) ret 4 (AddRef, DestroyInstance, RemoveBuilding
  0xBE3480, Release; the sell path) removes one on a guest. ✔
* The generic combatant damage 0xBFCEF0 (ret 0x14) is what turns a building into a ruin (hp → 0,
  state 2, message 0x1622184 with the attacker); not called on guests yet (docs/known-issues.md).

### 9.5 Vehicle orders seen at runtime
* Order list at v+0xB68 (`coop.civ` prints `order=` = type of the first one): 6 move, 8 claim node,
  2 attack city, 1 attack unit, **12** seen on the vehicles that took a city while its capture
  caption was up (meaning **?**).
* A land vehicle sent onto water or across the sea gets no order (the game ignores the click):
  ~20-30 % of the bots' random move clicks, on host and guest alike.

## Pointer tables across a load and a leave (2026-10-27, fix/w1-tribe-civ)
<a id="civ-pointer-tables"></a>

* ✔ A host `coop.load` of the same civ save while connected (civ_npc, GOG 2024): coop.cpp sees
  "no local" (the RTS marker of `modes.cpp rts_local` is null while the world loads), so
  `civ::on_world_change` runs once per load (`coop.civ tables`: `world_resets=1`). The host is back
  in Civ about 7 s after the command; the guest stays in its world (same stage: nothing is sent).
* ✔ Before BUG-0059 nothing in civ reset on a load: `g_alarm_at` (and `g_boost` until its 20 s ran
  out) kept the old world's pointers, and `on_session_end` did not clear `g_boost` / `g_alarm_at`
  (after a `coop.leave`: `boost=3 alarm_at=2`, coop_civ_reload.py --nofix).
* ~ Vehicle addresses after a reload of the same save: 4 of 4 vehicles got other addresses than
  before (1 try). The inherited-owner case of BUG-0059 needs address reuse: not seen, still possible.
* The vehicle and building editors also have no co-op adapter (no local): `on_world_change` skips
  them (`modes::kEdit`, `kEditor`) so the owners survive an editor visit.
* `HV.pol`: the political id a vehicle had when the host listed it; an entry whose `pol_of(v)`
  changed is listed anew (a new owner from the nearest player city, a new net id).
* Test commands: `coop.civ tables` (sizes of every pointer table), (the switches civ.boost and civ.world were retired in 0.7.15).

## Shared money and food across sessions and crossing spends (2026-10-28, fix/w2-civ)
<a id="civ-shared-pool-sessions"></a>

How `mods/coop/shared.cpp` keeps one total (tribe food, civ money, space money), after BUG-0049
and BUG-0106:
* The sync state ends with the session (`shared::on_session_end`, also every frame while the
  role is None or a guest has no WELCOME yet). Before, a guest that left, earned money alone and
  joined again sent the whole difference as one `MSG_RES_ADD` right after HELLO (reliable order
  HELLO, RES_ADD, then WELCOME): the host added 40000 at once (seen on lane 7). ✔
* `MSG_RES_ADD` = u8 id, f64 change, **u32 sequence** (new, trailing). `MSG_RES` = u8 id, f64
  total, **u8 n, n x (u8 player id, u32 last counted sequence)** (new, trailing). Older peers stop
  reading before the new fields. The guest shows total + changes the host has not counted + the
  unsent change (`net::SharedLedger`, unit-tested in `make test`). ✔
* The host floors a guest's change at 0; `s_civ` / `s_space` clamp to 0..99 999 999 like the
  game's AddMoney (`mWealth` +0x98 and `cEmpire` +0xD0 are written raw). Two spends of 100 out
  of 150 sent in the same step ended at -50 on both games before, now at 0 (lane 7). ✔
* Food: `s_food` floors every bundle at 0; the guest now takes what the game holds after a total
  as its base, so a floored amount is not reported as an earning on the next tick. (~, by reading)
* Test switches: `coop.res fix reset|floor 0|1`; roles: `coop.role fix 0|1` (BUG-0064, BUG-0079).

## A guest catches up with the host's city count (2026-10-28, fix/w2-civ)
<a id="civ-city-catch-up"></a>

* `Simulator::cCivExpansion::Get` / `SpawnNextCity` (GOG 2024 names) are the functions the GOG
  2016 file calls `cCitySimulator::Get` / `AppearNextCity` (same addresses in GOG 2016: 0xB268A0,
  0xAE3A80). `tools/tests/new_stage_game.py --npc-cities` used the 2016 names and failed on GOG
  2024 ("bad address"); it uses the 2024 names now. ✔
* A founding broadcast (`MSG_CIV_BUILD` kNextCitySite) that reaches a guest while it is about to
  load the host's world is applied to the old copy (or dropped outside Civ), and the host's save
  was written before the city: the guest stayed one city behind for the stage (lane 7: host 5,
  guest 4, `cities_in=1`). ✔
* Now the guest compares the host's city count (`MSG_CIV_STATE` header) with `all_cities()`; short
  for 6 s and no founding here for 15 s: it replays `SpawnNextCity(ex, 0, 0)` (the free site with
  the lowest order, the same one the host took). The new city matched the host's position within
  60 m (lane 7). Counter `cities_caught_up`; switch `coop.civ fix cities 0`. ✔
* `call Simulator::cCivExpansion::SpawnNextCity thiscall <ex> 0 0` from the console goes through
  the co-op hook and broadcasts the founding (instant 0, who 0); instant 1 does not broadcast. ✔

## The other nations' vehicle purchases (2026-10-28, fix/w2-civ)
<a id="civ-ai-vehicle-spawns"></a>

GOG 2024 addresses. Callers of `cCity::SpawnVehicle` 0xBDDEF0 (Ghidra `callers`): 0xBD72B0,
0xBDE5F0, 0xBE5890, 0xBE75C0, 0xBF9960, 0xBF9FB0, 0xBFA7A0, 0xBFB160, 0xBFBA70, 0xBFBD30,
0xCF3350, 0xCF72B0, 0xCFB7C0, 0xD09200 (9 sites), UI::RolloverCivCity::HandleUIMessage.
* **FUN_00BFBD30** fastcall(civ) = the civilization AI update (called from 0xAE61C0). Player civ:
  1 in 10 ticks, outside Space, a free vehicle when it has none and less than $1000 (spawn at
  0xBFBE5C). Other nations: per city FUN_00BE5890 and FUN_00BDE5F0, then FUN_00BF9960,
  FUN_00BF9FB0, FUN_00BFA7A0(0), FUN_00BFA7A0(1), FUN_00BF8850, FUN_00BFB160, FUN_00BF5DF0, ...;
  in Space also FUN_00BFBA70 (military spawn, uses the result: `mov ecx,eax` at 0xBFBD1E). ✔
* **FUN_00BE5890** fastcall(city) = NPC city tick: CreateBuilding growth (blocked on guests
  since 0f5d5c6) and, when `city+0x760` is 0 and a vehicle slot is left, the city's first vehicle
  (`SpawnVehicle` at 0xBE5A6B, result ignored, then `city+0x760 = 1`). ✔
* Result use after the call (disassembly): 0xBE5A6B none; 0xBDEA1C `test eax,eax / jz`;
  0xBF9F53 none (SpendMoney follows); 0xBFA746, 0xBFB0B1, 0xBFB692 none (budget `civ+0x4B0 -=
  cost`). These six get null on a guest (BUG-0188, `.sym` `SporeCoop::CivAI_*_SpawnVehicle_Ret`).
  0xBE7663 is in the city message handler FUN_00BE75C0 (message 0x5416D55; result ignored). ✔
* Runtime (lane 7, `play_civ.py --seed 9`, 10 min): the guest's game spawned 11 vehicles by
  itself: 6 x 0xBE5A70 (one per new NPC city), 3 x 0xBF9F58, 2 x 0xBFB0B6. In 4 idle minutes on
  civ_npc without new cities: none. ✔
* Diagnostics: `coop.civ spawns` (the guest's spawns by caller, refused ones), switch
  `coop.civ fix aispawn 0`.

## Leavers' vehicles, superweapon votes, captions, host away (2026-10-28, fix/w3-civ)
<a id="civ-wave3"></a>

* Co-op (BUG-0031): when the player set changes the host gives every vehicle of an absent owner
  to the owner of the nearest player city (after the new city split; shared capital: the host);
  `h_Capture` never picks a vehicle of an absent owner. Switch civ.orphans (retired in 0.7.15).
* Co-op (BUG-0117): the superweapon vote captures civ, type and target by value; a request during
  another vote is refused with a notice to its sender. `coop.civ sw` shows the last request and the
  type the last guest vote fired. Switch civ.sw_vote (retired in 0.7.15).
* ✔ `cCinematicManager::EndCinematic` (0xADFF50, GOG 2024) ends a capture caption cleanly: called
  8 s into the caption of a capture made through `Capture` (`coop.civ capture <city>`, return 6), the
  cinematic state +0x2C went to 0, the HUD came back without the caption, the city belonged to the
  player civ on host and guest, and the vehicles drove again (lane 7). Co-op (BUG-0187): with 2+
  players the host ends its cinematic after 8 s. `coop.civ movie`, `coop.civ fix caption 0`.
* ✔ Guest vehicle mirrors get StopMovement + Teleport every frame for 3 s after the last state
  (`guest_tick`): an order given in that time is cancelled. Co-op (BUG-0151): while the host is in
  the main menu the guest skips `guest_tick`, runs the vehicles' behaviour trees and keeps its
  orders local (`SetOrder`, `SetMoveOrder`); a vehicle the guest's game created as a mirror of a
  bought one drives too (39.7 m in 10 s). The same in Tribe (`DoActionForObjects`,
  `WalkSelectedTo`). `coop.civ fix away 0`, `coop.tribe fix away 0`.

## SpawnNextCity: one argument order (2026-10-28, fix/w4-tools)
<a id="civ-spawn-next-city-args"></a>

* CORRECTION (2026-10-28, fix/w4-tools, BUG-0134): `overrides_922af7c7.sym` had two entries for
  0xAE3A80: `cCitySimulator::AppearNextCity` "thiscall(cs, bool instant, void* who)" (verified at
  runtime: a non-zero `who` is dereferenced, who=1 crashed) and `cCivExpansion::SpawnNextCity`
  "likely thiscall(obj, bool)". The runtime-checked order holds: `(ex, bool instant, void* who)`.
  The co-op detour named them `(who, flag)`, the other way round; it is `(ex, instant, who)` now.
  No behaviour changed: every caller (the planet tick, the cheat, civ.cpp's replay) passes (0, 0),
  and the broadcast condition `!instant && !who` is symmetric. The GOG 2024 entry
  (`overrides_dc04aee5.sym`, 0xAE3AB0) names the arguments too. ✔ by reading

## Civ world changes and cinematics (2026-10-28, fix/w5-world)
<a id="civ-world-epoch-cinematics"></a>

BUG-0223, BUG-0224. GOG 2024, lane 5.

* ✔ One frame with Civ's local entity null (the anchor built from the player civ's first city;
  forced with `coop.civ nulllocal 1`) made the host forget every table before the fix: all vehicles
  got new ids (`next_id` 3 -> 5), the guest's game destroyed and rebuilt every vehicle (new object
  addresses), the guest's own vehicle lost its owner. Now `civ::world_check` decides from
  `game::stage_epoch(kCiv)`, leaving Civ for a non-editor mode, and the in-stage signals (another
  player civilization object, another save name); switch `coop.fix civ.world_epoch` (retired in 0.7.15).
  `host_tick` prunes `g_hv` against the game's vehicle list before it reads a vehicle (`pol`, the
  orphan loop).
* ✔ `Simulator::cCivExpansion::SpawnNextCity(ex, 0, 0)` on civ_npc founds a city with no cinematic:
  +0x2C of the cinematic manager stayed 0 for 25 s.
* ✔ `cCinematicManager::PlayCinematic` GOG 2024 0xAE08B0 (port, overrides_dc04aee5.sym):
  `call Simulator::cCinematicManager::PlayCinematic thiscall <cm> s:CVG_PRE_NewCityAppears 1 1 0 0 0`
  plays a camera cinematic that keeps +0x2C set for more than 30 s (Enter does not end it,
  `SporeCoop::cCinematicManager_EndCinematic` does). Other names that start in Civ (non-zero id):
  CVG_UnlockUFO, CVG_UnlockAir, CVG_PlayerCityAttacked; CVG-CamRotate returns 0.
* ✔ `coop.civ capture <city>`: +0x2C is set within 0.3 s after `Capture` returns. BUG-0224: only a
  capture (`h_Capture`, host, any nation) arms the 8 s close of BUG-0187; the 8 s count from the
  arm, the arm ends when the cinematic ends, when none came within 3 s, after the close, at a world
  change or session end. Switch `coop.fix civ.caption_only` (retired in 0.7.15). Not measured: the purchase chain
  (BuyCity, FinishBuyCity, Capture) and whether +0x2C drops between its cinematics; an NPC capture
  of an NPC city arms too (accepted: its caption would stop everybody the same way).
## Superweapon fields and the boost speed on GOG 2024 (2026-10-30, fix/w9-tests)
<a id="civ-superweapon-2024"></a>

Measured for BUG-0222 (the civ_reload / civ_sw_vote tests read these now):
* ✔ `cCivilization::FireSuperweapon` GOG 2024 0xBF8BB0 (thiscall civ, int type, const Vector3* target,
  ret 8): CanUse 0xBF8B40 → the own city that can launch nearest to `target` (equal to the zero vector
  0x168C514 = the first one) → launch `city 0xBE5D30(type, target)` → unless the free cheat byte
  0x168BD6C is set: `mWealth (+0x98) -= cost(type)` (0xBF6DF0), +0x4CC = 1 for types 2/5/8. It reads
  `target` without a null check: a null target crashes when the weapon is usable (the game's own
  callers always pass one; `coop.civ super <type> [x y z]` passes the zero vector or a place now).
* ✔ CanUse 0xBF8B40 (thiscall civ, type, ret 4): free cheat → 1; types 2/5/8 only while +0x4CC is 0;
  `unlocked[type]` at civ +0x484 (bool[12]); affordable 0xBF7870 (`cost <= mWealth +0x98`); no
  cooldown 0xBF18D0. Runtime: on the `civ` fixture `unlocked[]` is all 0, money 3000; after
  `mem.write` of `unlocked[3]` / `[9]` / `[1]` and 20000 money the game fired them (money -2000 /
  -2000 / -16000). A fired type is not usable again within the test's minutes (cooldown): the same
  type twice in one test fires once.
* ✔ Costs 0xBF6DF0: types 0, 6, 11 = 4000; 1, 4, 7 = 16000; 2, 5, 8 = 12000 x 1 while fewer than two
  cities of the planet are not this civ's, else 12000 x (the type number, as decompiled: ~ not checked
  at runtime); every other type (3, 9, 10) = 2000.
* ✔ Fired at the zero vector, type 4 crashed the game in `cPlanetModel::GetNearestCity+0x1B7`
  (0xB897A7, read of 0x8F04CAB0, lab 20261030-001517; via launch 0xBE5D30 → 0xBDC2F0). Types 1, 3
  and 9 at the zero vector worked. Tests use 1, 3, 9 only.
* ✔ The alarm boost writes the vehicle's speed +0x218 (x1.5). The game does NOT bring it back by
  itself: with `coop.fix civ.boost off` (retired in 0.7.15) the boosted vehicles kept 1.5x for 30 s after `coop.leave`
  (lab 20261030-002050); with the fix the session end restores them within 2 s. So the speed is a
  FAIL criterion of BUG-0040, read with `mem.read <vehicle+0x218> 4` (`coop.civ boosts` lists the
  boosted vehicles and their own speed).
* ✔ The order list element (v+0xB68) holds the order's place at +4: `coop.civ` prints it as
  `opos=(x y z)`. A guest's move order that the host executed shows the guest's target there
  (BUG-0059 with `civ.world off`: lab 20261030-001854).

## A vehicle the host's game puts somewhere: the guest's velocity estimate (2026-10-30, fix/w13-tribe-civ)
<a id="civ-vehicle-jump-velocity"></a>

BUG-0186 (single vehicle sync outliers of 10-27 m in `play_civ.py --seed 9`):
* ✔ (lane 7, `play_civ.py --minutes 10 --seed 9`, out/w13/play_civ9_a.log of the branch, not in
  git) two of the player's vehicles were 16 m off at the same moment, and the guest's `coop.civ`
  line showed `age=0.0` / `0.2` (fresh states), `pushed` 0.0 / 0.3 (the guest's game did not move
  them) and `err=27.0` for both: the mirror stood 27 m from the target guest_tick wanted while it was
  only 16 m from the host's vehicle. The target itself was 11+ m from the host's vehicle with a
  fresh state, so it came from the velocity estimate (`VelocityEstimate::at`, up to 0.4 s of
  extrapolation), not from a missing or old state, nor from a push.
* Inferred: one step of the host's vehicle between two snapshots that the host's game made by
  putting it somewhere (a step the vehicle cannot drive: tens of m in 0.1 s) became a speed of
  100+ u/s, and the mirror was put up to 0.4 s of that speed past the vehicle. The earlier
  hypotheses (an old state, a push out of a building on the guest) are ruled out for this sighting.
  Which game action puts a vehicle (the `defend` order of the bots came right before) is not known.
* ✔ unit (tests/net_tests.cpp, test_velocity_jump_cap): a standing vehicle put 20 units further in
  one snapshot: 15.6 units of overshoot without a cap, 0.0 with it.
* Fix `civ.vel_cap`: the guest gives each vehicle's estimate a top speed of 2 x its own speed
  (+0x218) + 10; a step faster than that is taken as a jump (no speed, like the 200-unit jump).
  `coop.civ` on the guest shows `vel=` (the estimate's speed), `top=` (+0x218) and `jumps=`.

## The guest's game throws a mirror off "invalid" ground (2026-10-30, fix/w14-tribe-civ)
<a id="civ-mirror-valid-ground"></a>

BUG-0186, the 13-17 m outliers of `play_civ.py` seeds 9 and 11 (always NPC vehicle #2 of nation
000021CB at its spice node near (-336, 132, 354) of civ_npc):
* ✔ (runtime probe `coop.civ pushprobe`, lane 7) every frame the guest's game called the mirror's
  spatial SetPosition (vt 0x38) from 0x00CA75B8 and moved it 13 m to (-345.8, 133.5, 344.0); then
  guest_tick's Teleport put it back. The caller is **Vehicle_KeepOnValidGround 0xCA7430**
  thiscall(v): 0xC9EA70(locomotion +0xB1C, pos) tests the planet cell flags under the vehicle
  (land: none of 0x98000000; sea: none of 0x58000000; air: bit 29 clear); when invalid it searches
  nearby spots (cCommunityLayout, radius 128) and sets the vehicle on the first valid one.
  Callers: vehicle updates 0xCA8360, 0xCA84D0 (x2), 0xCA8CB0, 0xCADA20 (spatial-side thunk).
* ✔ The host's game keeps its own vehicle on that spot (claiming the node); the guest's planet finds
  it invalid for a land vehicle. ? why the cell flags differ (the node's claim / derrick, or the
  host vehicle reached it while the ground was valid).
* ~ superseded (2026-10-30): the guess of fix/w13-tribe-civ (docs/re/civ.md#civ-vehicle-jump-velocity)
  that the guest's game pushed the mirror because of an order of its own: CORRECTION (2026-10-30,
  fix/w14-tribe-civ): the mirror had no order (`order=-1`) while it was thrown off; clearing its
  orders every frame (tried) changed nothing: `coop_civ_mirror_order` with the clearing: 12.9-13.6 m.
* Fix `civ.mirror_place`: the check is skipped for mirrors on a guest in Civ (the host's vehicle is
  where it is). Test `coop_civ_mirror_place.py`: off 17.3 m, on 0.8-1.2 m.
* Also `civ.extrap` (VelocityEstimate::max_ahead 0.15 s for vehicles, was 0.4): a fast air vehicle
  (25 u/s) whose host hitched was put up to 10 m past the host's (unit test test_velocity_hitch:
  10.0 -> 3.8 units; a 150 ms late packet costs 0.8 units either way).
## A guest's catch-up founds every missed city at once (2026-11-01, fix/w31-civ-gate)
<a id="civ-city-burst"></a>

* ✔ (runtime, lane 9, civ_npc, GOG 2024) `cCivExpansion` (`Simulator::cCivExpansion::Get`) +0xAC =
  all city sites (10), +0xB0 = sites not founded yet (6 with 4 cities). `SpawnNextCity` lowers +0xB0
  in the same call; the city appears ~4 s later. A loaded save has the same values as the host's
  (host and guest 10 / 6 after the guest loaded the host's world). So `+0xAC - +0xB0` = the cities
  this game founded, appeared or still on their way (the player city included).
* ✔ The campaign's shortcut `SpawnNextCity(ex, 1, 0)` (instant) is not broadcast by the co-op hook
  (only `!instant && !who`): the guests got those cities only from the catch-up, one per 15 s
  (6 s short + 15 s since the last founding). 6 cities took 75 s on a campaign guest; meanwhile
  every vehicle of those nations was missing there (`CivByPol` gives no civilization, no mirror):
  sync-check `v: 6 missing on the guest` in every campaign_space (BUG-0380). In the exploration run
  2 missed cities took 26 s, the guest's vehicles 4 -> 6 only then.
* Now (`civ.city_burst`): a guest whose founded count is under the host's city count (2 s short)
  founds the difference in one go; when its founded count already reaches the host's, it waits
  (a paused game holds the appearance, BUG-0331) and never founds a second time. coop_civ_city_burst:
  3 missed cities and their vehicles on the guest in 7 s (was more than 30 s).
* The single-vehicle `v: 1 missing` lines of the campaigns came in the same advance phase (after
  the shortcut), not from another cause seen in the logs.

## The model check of mirror vehicles and buildings (2026-11-02, fix/w39-content-all)
<a id="civ-content-all-kinds"></a>

BUG-0469, GOG 2024, Ghidra and lanes 4/5 (`tools/tests/coop_content_civ.py`). The civ paths of
docs/re/creations.md#creations-content-all-kinds.

* ✔ A mirror vehicle is created from the design key the guest's own copy of that civilization returns:
  `cCivilization::GetDesignKey(civ, VehicleDesignId(purpose, loco))` -> `CreateVehicle(civ, purpose, loco,
  key, &pos, 0)` (`civ.cpp` `guest_state`). That design's model is checked with its parts and paints
  (`creations::ensure_model`, kind vehicle) and the mirror is created from the returned key (the design's
  own key, whose record a package of ours answers above the game's own, with base game parts if this game
  lacks some: no key of the mod's reaches the save, BUG-0469 review / BUG-0470). A design whose model
  cannot be built at all (refused) is
  created from a **zero key**: `cVehicle::Load` (`Simulator::cVehicle::Load` 0x00CA67C0, 0xCA68E9) builds
  the design of `VehicleDesignId(purpose, loco)` itself from the object template DB (prop 0x2DC9D1E) for
  a key instance of 0 or -1 and sets that key, i.e. the game's own design of the same purpose and
  locomotion. Logged once per design.
* ✔ A building's model is the design `cCity::SetBuildingModel` (0xBDB940) asks the city's civilization for:
  a design id per building type (house 0xFF10521 -> 0x4E3F7777, entertainment 0x1A55E4D -> 0x72C49181,
  factory 0xECADE42 -> 0x47C10953, every other type -> 0x4E3F7777; `civ.cpp` `building_design_id`), then
  `GetDesignKey(city+0x590, designId)`; a key instance of 0 makes it build the design first
  (FUN_00bf98c0 -> FUN_00bf9440, the design generator from the object template DB). The mod cannot pass
  another key here, so on a guest a design whose model names parts or paints this game lacks is left out
  (the building stays missing on that guest, logged once per design; `build_in_city` returns nothing) and
  a design that this game has is built as it is. The host builds from its own designs (its own game
  baked them).
* ✔ Runtime (lane 4 + guest lane 5, PASS run `out/lab/20261102-202217`): the guest's house design key is
  `0679F5DE/2399BE55/40636200` (`coop.civ design <city> house`) and its model is complete (`model ok`);
  with the guest's design forced to a vehicle model that names a part of a pack the guest lacks
  (`coop.civ fakeveh`, test only) the guest builds a substitute copy and shows the mirror
  (`coop.creations content`: `substituted=1`, `refused=0`). With `SPORE_FIX_OFF=content.all_kinds` the
  same guest faults at 0x0046C82A (`out/lab/20261102-202807`).
* ✔ The guest saves its world after the mirror was built (`coop.save`, the game's SaveGame), its game is
  restarted without a co-op session and loads that save: the saved mirror vehicles load with no bake
  fault, the save slot names the design's own key (0x29030001) and no key of the mod's
  (`out/lab/20261102-213135`; with `coop.fix content.model_key off` (retired in 0.7.15), the first version's 0x7D72C793:
  `out/lab/20261102-213354` FAIL). The mirror vehicles themselves stay in the save, which is BUG-0470.

## The city planner, discovery cards and vehicle removal on a guest (2026-11-03, fix/w40-civ-crashes)
<a id="civ-planner-lifetime"></a>

GOG 2024 addresses (dc04aee5; the Steam build 3010a413 has the same code). Found from the three guest
crashes of BUG-0491 / BUG-0492 / BUG-0493 (0.7.3.6) and the detector's BUG-0478.

**City planner (cCommunityEditor, size 0x2FC, ctor 0xD0D200, main vtable 0x147956C in GOG 2024).**
One editor object lives in the city input strategy (+0x80, made in 0xCF47C0) and serves the Tribe,
Civ and Space planners (update callers 0xCDFA5D, 0xCFE56B, 0xFE126E).
* ✔ +0x40: the community being edited (Civ: the cCity, cast 0xEE9B2232), 0 while closed. The update
  0xD124E0 (`SporeCoop::CommunityEditor_Update`, thiscall(ed, int) ret 4) runs every frame and returns
  at once when +0x40 is 0; it runs while the planner pauses the game (runtime: `coop.civ planner` sees
  the editor and the city while the planner is open, nothing after the green check).
* ✔ +0xC0: the object in the hand (a spatial interface, `building+0x34`); +0xF4: the layout it is placed
  in (0xD09B10 picks it: buildings city+0x3EC, turrets 0xBD80B0, others); +0xF8: the slot index.
* ✔ +0x118: an eastl map (anchor right +0, left +4, root +8, size +0x128) of objects the planner moves:
  node +0x10 the spatial interface, +0x14 the target position. The update lerps them
  (0xD0C5F0: vt 0x2C GetPosition, vt 0x38 SetPosition), the exit 0xD100B0 -> 0xD0C7A0 puts them at
  their target (vehicles get PushEvent 10). A placed building is entered here while it slides into its
  slot (0xD10720 via 0xD0FEE0). No check that an object is still alive: a freed one is read through
  a null vtable (BUG-0493: read of 0x2C at 0xD0C662).
* ✔ Pick-up 0xD0FCC0 (`SporeCoop::CommunityEditor_PickUp`, thiscall(ed, spatial) ret 4 -> 0 / 2):
  slot = the slot of layout +0xF4 nearest to the object (0xAFAAF0, occupied or not); when that slot's
  occupant (0xFCC1D0, `slot+4`) is not the object (cast 0x17F243B), +0xF8 = -1, and then
  `cLayoutSlot::RemoveObject` and `SetObject` run on `slots + (-1)*0x18`: the 0x18 bytes before the
  slot vector (BUG-0491: RemoveObject+0x13 reading a float of heap memory as a pointer).
  A direct call on a building out of its slot returned 2 before that point in the lab (the editor
  state of a click was not there); the guard `civ.planner_pick` refuses such a pick-up itself.
* ✔ The planner's adjacency links (0xD11F80) read the occupant of every linked slot at +0x289 with no
  null check (139 first-chance faults at 0xD122A8 in the lab when a slot was emptied under it).
* Rule (`civ.planner_hold`): while the local planner edits a city, the mod changes nothing of it:
  no reconciliation, no host building, ruin or capture for that city, no removal of a vehicle in the
  +0x118 map (removed when the planner lets it go); the host holds a guest's build for the city its own
  planner edits (a shared capital) until the planner closes. The snapshots after the planner bring the
  city up to date (runtime: within 2 s, `civ_planner`).
* ✔ `cLayoutSlot` {+0 u8 occupied, +4 occupant (ref-counted), +8 int, +0xC Vector3}: SetObject
  0xAF9FB0 sets the flag and swaps the occupant, RemoveObject 0xAFA020 clears all three. The nearest
  *free* slot is 0xAFA550 (SnapBuildingToSlot uses it through 0xAFACF0, returns 0 when no slot is free).
* ✔ `build_in_city` with no free slot left the building it had made in the world (in no city list, no
  slot, owner city set, at the host's spot over another building): the next save kept it and a pick-up
  of it is the -1 case above. It is destroyed now (`civ.build_orphan`). `coop.civ planner` lists such
  buildings as `orphan`.

**Discovery cards (BUG-0492).**
* ✔ `SporeCoop::AssetDiscovery_AddCard` 0xE30CF0 thiscall(cUIAssetDiscoveryManager*, data) ret 4 makes a
  "UI/AssetDiscoveryCard" and fills it in 0xE2F6B0, which reads `data+0xC` at once: data null -> read
  of 0xC at 0xE2F7AF (crash reports name it SetImageForWindow+0x21F).
* ✔ `SporeCoop::CivDiscovery_Meet` 0xC746F0 thiscall(planet context = `TimeOfDay_GetObject()`, const
  uint32* pol) ret 4: not met yet -> data 0xBA5B60(GetCivilizationByPoliticalID(pol), 1), null when
  that civilization does not exist, then the card. Called by the city click handler 0xCE8C30 (message
  6 on a city hall: the city's political id) and cCommManager::CreateCivCommEvent. Reproduced:
  `coop.civ discover 7EADBEEF` on a guest crashes at 0xE2F7AF with `civ.discovery_card` off.
* ~ How a guest got a city whose owner has no civilization in its game is not observed. Inferred: the
  host's snapshot changed the city's owner to a political id the guest's game does not have
  (ChangeOwner takes any id). In the lab the political ids of the first cities and of three cities the
  host founded were the same on both games. The guest now keeps such a city's owner (logged once).

**Vehicle removal (BUG-0478).**
* ✔ `cVehicle::Die(v, effects, killer)` alone only marks the vehicle dead (+0xBCD): it stays in the
  noun list and the civilization's lists, and its AI drives the wreck on (runtime: a dead vehicle of the
  host moved 300 m in 90 s). The game removes a vehicle with `SporeCoop::Vehicle_Remove` 0xCAA7B0
  thiscall(v, bool refund) ret 4: refund, a recycle effect, Die(0, -1), cGameBehaviorManager vt 0x3C,
  then cCivilization 0xBF3D90 (destroys it) or DestroyInstance (callers: disband 0xCF5BB9, the
  planner 0xD118D3, 0xBF75D2). An NPC vehicle that dies in the host's combat is gone from the host's
  noun list at once; the guest's Die(0, -1) left a wreck for good (the detector's extra guest vehicle,
  growing with the play time). The guest removes its mirrors with 0xCAA7B0 now (`civ.mirror_remove`).
* ? Roads (`inter_city_road`, noun 0x02B8A4E7, vtable 0x146CE08) are not mirrored: where the host's game
  makes them (the input commands "CityBuildRoad" 4000 and "CityCreateTradeRoute" 0x37E95BC are
  registered in 0xCF47C0) is not traced. Open in BUG-0478.
  ~ superseded (2026-11-04): the road is the trade route's object (cCity::AddTradeRoute), see [civ-roads-citizens](#civ-roads-citizens).

**Saves (BUG-0470).**
* ✔ A guest's mirror vehicle stripped from a save (a null reference) is read back as a null entry in the
  civilization's vehicle vector (+0xB0) and selectable vector (+0xD4): 4 + 4 nulls for 4 mirrors and a
  fault at 0xBF2BF2 on load (`coop.civ civlists`, lab 2026-11-03). The mirrors now leave both vectors
  while the save is written (savestrip's save hooks, the save is written within one frame) and are put
  back on the next frame: the reloaded world has no mirror, no null and no fault. A world loaded with no
  vehicle at all gets the game's free first vehicle at its city.

## Roads and city citizens between the games (2026-11-04, fix/w41-civ-citizens)
<a id="civ-roads-citizens"></a>

GOG 2024 addresses (dc04aee5).

**Roads (BUG-0494).**
* ✔ The road between two cities (`inter_city_road`, noun 0x02B8A4E7, vtable 0x146CE08) is the ground object of a
  trade route: `cCity::AddTradeRoute` 0xBE1490 (GOG 2016 0xBE1470, same prologue +0x20, `ret 8`) thiscall(city,
  cCity* other, int type) pushes `{partner, route object, type}` (stride 0xC) on +0x55C of **both** cities and,
  for type 0, makes the road. Runtime (lab 20261103-234403): after `AddTradeRoute(c0, c1, 0)` both cities list
  the route with the same object (`route 0 1 type=0 obj=1F275758 noun=02B8A4E7`, `route 1 0 ... obj=1F275758`).
  The object's own position (`coop.civ nouns 2B8A4E7`) is (0 0 0): roads are counted, not placed.
* ✔ The host's game makes routes in its own AI (economic vehicles' "create route", 0xBE1950 AddTradeRouteWithCiv);
  a guest's mirrors run none of it, so the host's roads never appeared there. Now the host sends its route list
  (MSG_CIV_BUILD, city index 0xFE: u8 n, n x {u8 host city a, u8 b, u8 type}) when it changes and every 10 s;
  a guest adds the routes it lacks with AddTradeRoute (`civ.roads`, `coop.civ roads` lists them).
* ? Removal is left to the game: `RemoveTradeRoute` 0xBDFA70 (GOG 2016 0xBDFA50, `ret 4`) works on one side and
  destroys the route object, which the other city's entry shares: calling it for both ends would free the object
  twice (not tried). A capture removes routes on both games itself (ChangeOwner -> RemoveTradeRoutesWithCiv).

**City citizens (BUG-0476, BUG-0475's Civ half).**
* ✔ Both games spawn the same number of citizens (noun 0x18EB4B7, cCreatureCitizen) per city (19 / 19 around
  the host's capital in every comparison of lab 20261103-233836) and walk them on their own: 9 of 19 had no
  partner within 15 m (100 % of the comparisons), 88 % of the comparisons had a pair of another size.
* ✔ `Citizen_OffStageCheck` 0xACA2A0 is not what walks Civ citizens: a filter that skipped it for the guest's
  bound citizens skipped 0 calls in a 90 s Civ session (lab 20261104-000150). Where the Civ citizens' walks
  come from (their behaviour tree?) is not traced.
* ✔ The fix is a moving kind of the world-object table (worldobj.cpp, `citizen`, stage Civ): the host sends the
  56 citizens nearest each guest's pointer (its capital when the guest has none: a background game's pointer
  is (0 0 0)) every 0.5 s; the guest binds its own citizens to them by position (30 m) and walks each bound one
  with the game's MoveTo to where the host's is now (its last place moved on with its velocity from the last two
  snapshots, 1.5 s ahead), a MoveTo again only when that goal moved 2 m or after 1.5 s, and puts it there (place,
  no teleport mark) when it is 12 m off. 1 s snapshots left a median 2-5 m lag and 4-19 % unpaired; 0.5 s:
  0 of 95 unpaired, median pair distance 0.5-4.9 m (lab 20261104-000358).
* ✔ Their size is the fields kind `civ_citizen` (field `scale`): the host sends every citizen (id = the low 16
  bits of its object serial, a duplicate left out), the guest applies it to the bound copy (applied 17-18, differ
  0 at the end of each run).
* ~ The detector's `Civilization:scale:citizen` stays in part: its greedy nearest pairs cross in a crowd (pairs
  0.3 m apart of two different citizens, one bound elsewhere). `coop_civ_citizens.py` compares sizes on
  unambiguous pairs only (each the other's nearest within 3 m, the next one 2 m farther).
* Cost: host one walk of the noun list per 0.5 s for the snapshot and one for the fields; the net 19 + 20 bytes
  per citizen per guest every 0.5 s (19 citizens: 0.8 KB/s, at most 56: 2.3 KB/s), the fields about 6 bytes per
  citizen of the planet every 4 s.

## Shots, turrets and cultural targets between the games (2026-11-04, fix/w41-civ-combat)
<a id="civ-combat-shots"></a>

GOG 2024 addresses (dc04aee5).

**How Civ weapons fire (BUG-0477).**
* ✔ Every Civ weapon goes through `cToolManager::FireTool` 0x01050080 (thiscall(mgr, tool, owner spatial, const
  Vector3* aim, int dmgPoint, int, target spatial, bool) ret 0x1C). Callers: `cCombatSimulator::Update` 0xAE7360
  (every combatant of the noun list with a target in range: weapon = combatant vt 0x14, owner / target = the
  objects' spatials, i.e. object + 0x34; it calls FireTool **every update**, the tool's refire time decides
  whether a projectile leaves: lab 20261104-014831, 5120 calls in ~70 s for 3 vehicles, about 100 projectiles),
  the city functions 0xBDC2F0 / 0xBE4300 / 0xBE4550 (from 0xBE5D30: city-wide strikes), 0xD56BE0, 0xDC1360.
* ✔ Runtime: FireTool's target argument is a spatial (vtable 0x1474BE8 = a vehicle's +0x34, 0x14673F0 = a
  building's), never the owner. `cDefaultProjectileWeapon::WhileFiring` 0x01056F10 makes noun 0x24270C5 and
  calls `LaunchDefaultToolProjectile` 0x01054390 (homing on the tool's +0x118 target when there is one, else
  straight at the aim). Projectile nouns: 0x24270C5 default tool (vtable 0x1476110), 0x5776A2C resource (made by
  0xBFE8E0), 0x4F76F0D cultural (0xCB7030), 0x4167186 AoE area (cDefaultAoETool). A projectile keeps its tool at
  +0x518 (+0x504 the cProjectile part); 0xBE4700 walks the default projectiles of a civilization's vehicles.
* ✔ A guest's mirrors (no behaviour tree) never get a target: no FireTool, no projectile ever in a guest's
  game (the detector's `noun_only_host:*_projectile`, 81 % of the play_civ comparisons in lab 20261104-013631).
* ✔ Fix `civ.shots`: the host notes each FireTool of a listed vehicle or a turret (shooter, aim, target: a
  vehicle id or noun + position) and sends the shooters of the last 0.25 s every 0.2 s; the guest calls FireTool
  for its mirror (or its turret at that place) with the mirror's own weapon and the target of its own game, at
  most every 0.1 s (turret shooters and building targets are found in one walk of the noun list per 0.1 s for all
  shots due). Without the target (aim only) the guest's projectiles flew straight, hit nothing and
  lived longer: 186 guest / 99 host projectiles in the samples (lab 20261104-014352); with it 93 / 95
  (lab 20261104-015048), and they hit.
* ✔ The guest's copies hit and would do the host's damage a second time (with `civ.shot_damage` off the guest's
  city lost 5200 -> 4452 hit points, the host's 4452: lab 20261104-015254). `cosmetic_hit` drops a hit whose
  attacker is a mirror's combatant (vt 0x5C cast to 0x137E8E0, attacker vtable 0x1474CE8) or a turret the guest
  fired from, in the vehicle (`Simulator::cVehicle::TakeDamage` 0xCAB6F0), building, city hall and citizen
  TakeDamage hooks. Before the fix no hit of the guest's own reached those hooks in a fight (0 calls).
* Cost: host, one map write per FireTool call (every update of every combatant in range) and, while something
  fires, 5 + 46 bytes per shooter every 0.2 s to each guest (10 shooters: about 2.3 KB/s); nothing when quiet.
  Guest: one FireTool per shooter per 0.1 s and at most one noun-list walk per 0.1 s.
* ~ A guest's buildings keep their own hit points: only the host's ruins reach it (`reconcile_buildings`), so a
  city the host hit stands at full hit points on the guest (5200 / 4452 above). Not fixed here (BUG-0529).

**Turrets (BUG-0484, not fixed).**
* ✔ `cCity` +0x354 / +0x358 / +0x35C: the vector of the city's turrets (noun 0x436F342, cast 0x436F315), their
  layout at city+0x4B4 (0xBD80B0). 0xBE2100 (`CreateTurret`, fastcall city): CreateInstance(0x436F342), push on
  +0x354, put at the city. 0xBE39A0 (fastcall city): the next free turret slot (0xBD9A10), CreateTurret,
  cLayoutSlot::SetObject, position and orientation (callers 0xBE5890 x2, 0xBE5F20, 0xBE87D0, 0xFF55D0,
  0x1025200); 0xBE3AE0 (thiscall city, slot) the same in a given slot (caller 0xFF6900); the planner calls
  CreateTurret itself (0xD13CC7). 0xBE5F20 rebuilds a city's turrets (removes them with 0xBE21C0 and adds
  as many with 0xBE39A0).
* ✔ The detector's turret rows (play_civ, lab 20261104-013631): `noun_only_guest:turret` in 23 % of the
  comparisons (the host's game had no turret at all, the guest's one): the guest's game added a turret the
  host's did not. Which caller (its own AI for an NPC city, a rebuild 0xBE5F20, the planner) is not traced.
  Turrets are not in MSG_CIV_BUILD (`h_AddBuilding` sees buildings only).

**Cultural targets and bundles (BUG-0484, not fixed).**
* ✔ The city setup 0xBE7D40 makes 4 cultural targets (noun 0x3D5C325) per city (a loop of 4, stored in the city
  record +0x618 table at +0x78 + 0x80 * i). The detector saw them unmatched in campaign_space only (3 / 3, 2 of
  3 without a partner within 3 m): their places are the game's own per city.
* ✔ The Civ `game_bundle` rows of play_civ are at the old tribe's village place (-234 -34 482), next to the
  guest-only `tribe`, `tribe_hut`, `tribe_food_mat` rows (BUG-0483): leftovers of the Tribe stage in the guest's
  world, not bundles of a Civ fight.

## Building hit points, fired shots and turrets between the games (2026-11-04, fix/w42-civ-left)
<a id="civ-hp-shots-turrets"></a>

GOG 2024 addresses (dc04aee5).

**Building hit points (BUG-0529).**
* ✔ A building's combatant is at +0x120: state +0x154 (2 = ruin), hit points +0x158 (9.4). The guest's own game
  never hurts its buildings in a fight (the shots it copies are cosmetic, `civ.shot_damage`), so before the fix a
  city the host's vehicles hit stood at 5200 on the guest while the host's fell to 4462 (civ_shots, lab
  20261104-095130).
* ✔ Fix `civ.building_hp`: the host sends the hit points of the snapshot cycle's city (noun, place, hp; the buildings
  of type house / factory / entertainment and the city hall 0x18EA1EB, at most 40) in MSG_CIV_STATE chunk 0xFD
  (chunks 0) when they changed and every 3 s. The guest pairs each row with its own building (same noun, within
  4 m), lowers its hit points with `cCombatant::TakeDamage` 0xBFCEF0 (no attacker, direction (0 0 0): the damage look
  of the game, no capture damage) and raises them (the host's repair) by writing +0x158. Rows at 0 hp and local
  ruins are left to `reconcile_buildings`. Runtime: guest 4976 / host 4972 at 30 s, 4470 / 4462 at the end
  (lab 20261104-094742). Cost: about 20 bytes per building of one city per change (a city of 20 buildings in a
  fight: 400 bytes per snapshot cycle of that city), nothing for a quiet city but one list per 3 s.

**FireTool's result (BUG-0530).**
* ✔ `cToolManager::FireTool` 0x01050080 returns a bool in al: 0 unless the tool's refire time (+0x1D4 * 1000 ms)
  passed and the strategy's WhileFiring (vt 0x34, called at 0x0105030B, its al stored at [esp+0x13]) made the shot.
  cCombatSimulator::Update calls it every update: in civ_shots 35 calls of 3474 made a shot on the host
  (`tried=3439`).
* ✔ Fix `civ.shot_fired`: the host counts only the calls that made a shot (u8 count per shooter) and sends the
  shooters of the last 1 s with their counts (MSG_CIV_STATE chunk 0xFC; 0xFE is the 0.7.5 list, still read); the
  guest fires until it made as many (its own FireTool true), at most every 0.1 s, at most 3 behind.
* ✔ The shot lists are split into messages of 22 shooters (47 bytes each): the 0.7.5 list of up to 40 was 1845
  bytes, over kMaxPacket (1200), and a connection drops such a message: with more than 24 shooters a guest saw no
  shot at all (not seen in a test; found reading the code).
* ~ A single religious vehicle against an NPC city gave the same religious shots on both games with the switch on
  and off (126 / 126, lab 20261104-095320): the guest's weapon refused 1099 of the relayed calls on its own refire
  time. The extra religious shots and AoE areas of the detector (play_civ) are therefore not explained by the
  every-call relay alone.
* ✔ What the rows are (play_civ x3, lab 20261104-100146 / 101322 / 102449): a guest's game fires no tool of its own
  in Civ (a FireTool hook for calls outside the relay logged none), and the guest's copies made as many shots of the
  same weapon as the host's (`civ: shots by weapon`: 397 host, 397 guest at 600 s). Religious projectiles and AoE
  areas live about a second; the relay showed them 0.3-0.4 s later on the guest (civ_religious, the best shift of
  the two count series), so a detector sample often saw them in one game only (both ways: `noun_only_host` rows
  too once the counts matched). Fix `civ.shot_now`: a shot that left goes out at once (at most every 0.05 s) and
  the guest fires a new count on the frame it arrives: 0.10-0.20 s (lab 20261104-104140).
* ~ The delay was not all of it: after civ.shot_now one play_civ run still had guest-only AoE areas in 26 % and
  cultural projectiles in 13 % of the comparisons (lab 20261104-113023; another 3 % / 6 %, lab 20261104-110843).
  Inferred: those objects live longer on the guest (a copy that misses). Open in BUG-0530.

**Turrets (BUG-0484).**
* ✔ `City_AddTurret` 0xBE39A0 fastcall(city) -> 1 / 0: the next free turret slot, CreateTurret 0xBE2100,
  cLayoutSlot::SetObject, place and orientation. `City_RemoveTurret` 0xBE21C0 thiscall(city, turret or 0, bool)
  ret 8. Callers of AddTurret: the city growth 0xBE5890 (0xBE59B8 when the city was attacked in the last 20 s,
  0xBE5CFF at random), the turret rebuild 0xBE5F20 (removes all, adds as many), the city setup 0xBE87D0, 0xFF55D0,
  0x1025200 (a vtable slot). The growth pays from the civilization's budget (+0x4AC, float).
* ✔ `0xC00600` thiscall(city, int k) ret 4: true when city +0x98 + 8k (a game time, u64) lies within the last 20 s.
* ✔ Fix `civ.turrets`: a guest's growth calls (return addresses 0xBE59BD, 0xBE5D04) make no turret; the host sends
  the places of the snapshot city's turrets (chunk 0xFB) when they changed and every 3 s, the guest adds the ones it
  lacks with City_AddTurret and removes extra ones (two looks in a row). Runtime: a turret the host's game added
  stood on the guest within 25 s (lab 20261104-095903), not without the fix (lab 20261104-100015).
* ? A guest's own growth buying a turret was not seen in the test: with a 100000 budget and the city marked attacked
  the guest's growth spent the budget on other things (blocked buildings / vehicles) with the switch off too.
* The Civ `game_bundle` rows are Tribe leftovers (BUG-0483, fixed in 0.7.5). Cultural targets were unmatched only
  in campaign_space (BUG-0484 history): not part of the Civ fight.

## AoE weapons and their stop between the games (2026-11-04, fix/w42-civ-left)
<a id="civ-aoe-stop"></a>

GOG 2024 addresses (dc04aee5).

* ✔ `cDefaultAoETool` (strategy vtable 0x149B830): WhileFiring 0x01056B20 (vt 0x34) returns 1 and, when the tool has
  no area yet (tool +0x120 == 0), calls vt 0x48 (0x01054FF0): CreateInstance(0x4167186, the AoE area), placed at the
  owner (tool +0x114), kept at tool +0x120. vt 0x4C (0x01053ED0) moves the area with the owner. OnMouseUp vt 0x38
  (0x01053E90) releases the area.
* ✔ `cToolManager::OnMouseUp` 0x0104FAA0 thiscall(mgr, tool) ret 4: the strategy's vt 0x38, tool +0x80 (firing) = 0,
  the tool's 3 effects at +0x108 stopped. cCombatSimulator::Update 0xAE7360 calls it (three sites) when a combatant's
  target is gone or changes; FireTool sets +0x80.
* ✔ The cultural projectile (noun 0x4F76F0D) is made by 0xCB7030; its only caller 0xCBEBA0 (a vtable slot at
  0x1475150, next to the AoE area's vtable 0x1475240) makes three: the area sends them.
* ✔ A guest's copies of a host's AoE weapon were never stopped: the guest's area lived 30 s after a 5 s area on the
  host, its cultural projectiles 21 s against 2 s (civ_aoe, lab 20261104-124527). Fix `civ.shot_release`: the host's
  OnMouseUp of a listed shooter is sent with the shot list (stopped flag), the guest calls OnMouseUp for its copy
  (lab 20261104-124742: the guest's area ended 0.2 s after the host's).
* Supersedes the "late, not extra" reading of [civ-hp-shots-turrets](#civ-hp-shots-turrets) for the AoE rows: the
  delay explained part of the cultural rows, the never-stopped areas the rest.

## Old tribe raids and the city citizens' pool (2026-11-04, fix/w42-civ-left)
<a id="civ-tribe-raids"></a>

GOG 2024 addresses (dc04aee5).

* ✔ A Civ world keeps the Tribe stage's tribes (civ_npc: 17, each with a hut, a tool, a food mat and 2 bundles) on both
  games. The NPC vehicles raid them: behaviour tree action 0xDCAAB0 ("HR: no spice target found", "HR: path
  failure"): drive there, stimulus 0x8000 to its members, then AddMoney (random 400-1000) or an event log entry and
  `cGameNounManager::DestroyInstance` of the tribe at 0xDCB2B2 (return 0xDCB2B7); the huts, tools, food mat and
  bundles go with the tribe. A founding at a tribe's site destroys it too (return 0xB9BCE6, both games). A capture
  removes none. Runtime: the host's count 17 -> 5 in 10 min of play_civ (lab 20261104-125332), the guest's stayed.
* ✔ Fix `civ.tribe_ruins`: the host's tribe places (chunk 0xFA, every 5 s and after a destroy), the guest destroys a
  tribe with no host tribe within 10 m on two lists in a row (lab 20261104-130500: 3 removed, 14 / 14).
* ✔ City citizens (noun 0x18EB4B7) are a pool of 150 per planet on each game, 19 put at the city nearest the own
  camera (coop_civ_citizen_lod, lab 20261104-130500): level of detail, as in Space; the detector skips a city whose
  citizens only one game has (BUG-0526).


## A captured city's buildings, the nations' relations and money on a guest (2026-11-05, fix/w44-world-state2)
<a id="civ-wsync-owners"></a>

BUG-0593. GOG 2024, Ghidra (tools/ghidra/q.sh --build 2024) and lab lane 7 (civ_npc fixture, coop_wsync_civ.py).

* ✔ `cCity::Capture` 0xBE9430 sets the city's flags **+0x2E0..+0x2E6 to 1** before it calls `ChangeOwner`
  0xBE8A20 (directly when the captor is no player civ or `+0x762` / arg 4 is set; else after its caption cinematic
  CVG_Capture). ChangeOwner acts on the flags: **+0x2E2** -> `City_SetPartsOwner` 0xBE4C80 (thiscall(city, newCiv)
  ret 4: SetPoliticalID vt 0x48 of every building +0x340, turret +0x354 and +0x308 part, the new owner's building
  models from its culture set (0xBE4B60: 0x99E92F05 City Hall, 0x4E3F7777, 0x47C10953, 0x72C49181), an update
  request 0xBD1C00(1)); +0x2E3 -> 0xBD91B0 (the new owner's colour, a view effect); +0x2E1 the capture
  cinematic; +0x2E6 -> 0xBDB200 (an effect at the City Hall). It also asks the relationship manager (0xD00BD0, a
  level query) and, in Space, 0xBD9130.
* ✔ (observed, lab 20261105-122815 with the fix off) A guest gets a capture from MSG_CIV_STATE and calls
  `ChangeOwner(city, pol, 0, 0)` itself (civ.cpp guest_cities) with the flags 0: the city became the player's, its
  three buildings kept the NPC's political id 000021CB (the detector's `planet_owner:building_*` rows of the 0.7.7
  final check, campaign_space). Fixed by the wsync row `city_owner` (wsync.city_owner): a guest's city whose
  buildings or turrets have another owner than the city gets them through City_SetPartsOwner (not while the open
  city planner holds the city, BUG-0493).
* ✔ The nations' feelings about the player civilization (`CalculateRelationship(civ, playerCiv)`, the same manager
  as Tribe) change on the host by its game's events (a capture: -1.12 in the lab, the guest's own ChangeOwner gave
  the same -1.12 but none of the host's other events); a guest's never followed (0.7.7 final: 000021CB -3.21 on the
  host, 3.00 on the guest). Row `civ_rel` (wsync.civ_rel): ApplyRelationship by the difference, as tribe_rel.
* ✔ An NPC civilization's money (+0x98; GetMoney adds the NPC budgets +0x4AC / +0x4B0) moves with its own AI: 1500
  added on the host was 0 again 25 s later (lab 20261105-122514). Row `civ_money` (wsync.civ_money): +0x98 written
  (AddMoney applies the difficulty, SpendMoney refills an NPC's budget: no setter fits).
* ~ civ_coop's `Civilization:state:money` row ("-800 on the host, 0 on the guest"): the test's `coop.civ build`
  on the host spends without the game's price check (the host's wealth went 400 -> -800); a guest's shared total
  is floored at 0 (res.floor, BUG-0106). Not a player's case (the game refuses a building it cannot pay); not changed.

## Cultural targets at the host's places (2026-11-06, fix/w45-world-objects)
<a id="civ-host-cultural-targets"></a>

GOG 2024 addresses (dc04aee5). Kind `cultural_target` of mods/coop/worldobj.cpp (BUG-0540), switch
`wobj.cultural_target`.

* ✔ The 4 cultural targets (noun 0x3D5C325) of a city stood at the same places on both games after a fresh load of
  `civ_npc` (and `civ`), but 3.3-8.5 m apart after the guest left and joined again 60 s later (lab 20261106-043659):
  the guest's game sets its cities up again from the host's world of then and puts them by its own city layout. This
  is the campaign_space row: it came after Carol's rejoin (out/lab/20261103-012624, `extra1`).
* ✔ Fix: a guest's target binds to the host's within 3 m, else its nearest unbound one within 120 m is moved there
  (SetPosition of the spatial object); no make or removal (4 per city in both games). PASS lab 20261106-044559-2
  (relocated=8 placed=8), FAIL with the switch off lab 20261106-043659.
* ? Whether the city record's own copy of a target's place (+0x618, +0x78 + 0x80 * i) is used by the AI is not checked:
  only the object is moved.
## Mirror vehicle scale 10: the vehicle's model-ready handler (2026-11-06, fix/w45-placement)
<a id="civ-vehicle-scale"></a>

BUG-0549. GOG 2024. ✔ = observed, ~ = read in the code, ? = inferred.
* ✔ The SetScale diagnostic logged `scale 10 set on an object with vtable 01474BE8 from 00CA41DA` in play_civ on the
  guest only (lab fix/w42-civ-left 20261104-131223, the run of the detector's `Civilization:scale:vehicle` row, 0.81
  host / 10.00 guest); never in a host log of the November labs.
* ~ 0xCA41DA is in `SporeCoop::Vehicle_GroundSetup` 0xCA40B0, called only from the vehicle's model message handler
  0xCA76B0 (this = vehicle +0x34) on message 0x2B978C46, which first sets vehicle +0xD04 from the message. With
  +0xD04 set, it asks the manager 0xB34050 (vt 0x30) for an object at the vehicle (+0xD00) and, when there is one,
  sets the loco's scale to 10.0, copies colours to that object and sets the bounding box (vt 0x64). ? Which object
  that is and why only the guest's mirrors find it was not traced; the scale it leaves is what the player saw.
* Fix `civ.vehicle_scale`: Civ vehicles are a replicated-fields kind (`fields::K_CIV_VEHICLE`, civ.cpp: the host's
  vehicles by net id, a guest's mirrors from g_gv): every mirror whose scale differs by more than the field's
  tolerance takes the host vehicle's, also when the game sets it again. `coop.civ` vehicle lines print `scale=`;
  `coop.civ vscale <id> <s>` (tests). Test civ_vehicle_scale.

## City terrain stamps: the same ground on both games (2026-11-06, fix/w45-placement)
<a id="civ-city-ground"></a>

The BUG-0332 class (docs/re/tribe.md#tribe-terrain-stamps on fix/w45-village-ground: stamps applied live render
another ground than the same list after a load) checked in Civ, where cities stamp the terrain too. ✔ = observed.
* ✔ civ_npc (the save's cities, loaded on both games): `coop.ground` on a 10 m grid up to 80 m round every city,
  892 points, the largest difference 0.00 m (civ_city_ground, lab 20261106-062301).
* ✔ After a live founding on both games (the host's SpawnNextCity, the guest's replay) and a capture: 1115 points,
  0.00 m (same run).
* ✔ After the live Tribe -> Civ change (the host makes its first city live, the guest loads the host's Civ save):
  223 points round the cities, 0.00 m (tribe_civ_ground = coop_tribe_civ.py --ground, lab 20261106-062912).
* So no Civ ticket: the two grounds do not differ round cities in these paths. Not measured: a city the guest's game
  founded while the host's was away (local play), and a Space colony's city.

## A captured city's new layout and the guest's rebuild loop (2026-11-06, fix/w45-placement)
<a id="civ-capture-layout"></a>

BUG-0602. GOG 2024. ✔ = observed, ~ = read in the code, ? = inferred.
* ✔ campaign_space (lab 20261106-064018, `SPORE_DETECTOR=20`): the campaign founds 6 cities (SpawnNextCity
  instant=1, the guests found theirs) and captures one per 30 s for the player's civ (cCity::Capture). Right after the
  capture of city 4 (06:45:25) every guest logged every 2 s `the host's building 018EA2CC of city 4 at (-319.0 -310.6
  -239.6) went to (-317.0 -316.8 -234.0) here, 8.6 m off` (buildings_fixed=+224/-224 at the end); the detector's
  `Civilization:planet:building_industry` in 27 samples.
* ✔ Live `coop.civ slots 4` / `blist 4` on both games in that run: the host's city 4 had a new layout (all 12 slots
  elsewhere, the industry in slot 5 at (-319 -311 -240), the City Hall 3.9 m and the house 3.2 m from the guest's),
  the guests kept the layout of the founding (the same 12 slots the host had before the capture, lab 20261106-062912).
  The host's building stood in its slot (no `host building ... nearest slot` line): the layouts differ, not a building.
* ✔ Not by the capture alone: `coop.civ capture` of a new city in a two-game test left the host's layout as it was
  (civ_city_layout before its --move step was added, lab 20261106-062912). ? The relayout came with the captured city's new models
  (City_SetPartsOwner 0xBE4C80 -> 0xBE4B60, the owner's City Hall, then an update request 0xBD1C00(1)), which a guest's
  ChangeOwner without Capture's +0x2E2 flag does not run, and may wait for the city to be updated in view (the
  campaign's capture caption shows it); not traced.
* ✔ Why it looped: reconcile_buildings matches a host building with a local one of the same noun within 4 m; the
  guest's was 8.6 m off, so it was destroyed (extra) and the host's built again (missing) with SnapBuildingToSlot,
  which took the free slot nearest the host's spot: the guest's own slot of that building, 8.6 m off again.
* Fix `civ.city_layout`: a building a guest makes for the host's (reconcile, MSG_CIV_BUILD), and one it matched 1-4 m
  off, is put at the host's spot with its layout slot (cLayoutSlot +0xC) moved there too, so the slot nearest the
  building stays its own (the planner's pick, BUG-0491) and the next snapshot matches it. `coop.civ` counts
  `off_slot=` (rebuilt more than 4 m from the host's spot) and `moved=`. `coop.civ blist <i>` lists a city's buildings
  with their slot, `coop.civ bmove <i> <k> <x y z>` (tests) moves one.

## Instant city foundings reach the guests at once (2026-11-06, fix/w45-int3)
<a id="civ-instant-found"></a>

* ✔ (lab 20261106-122407, 20261106-123450) civ_city_layout founds three NPC cities with
  `SpawnNextCity(ex, 1, 0)` and `wait.frames 30` between them. At ~25 fps (a loaded PC) the three host cities
  appeared 2 s apart (host log: their terrain stamps 0x00F99E0E at 37.2, 39.3, 41.3 s), and the guest's catch-up
  (city_catch_up: 2 s short, then SpawnNextCity with the appearance timer) had each of them 4-5 s after the host
  (guest stamps at 42.3 and 44.5 s): the vehicle of the first new nation was missing on the guest in two sync-checks
  3 s apart (`v: 1 missing on the guest (v5)`), 3 of 3 runs on this tree, 1 of 3 on 46b4e1d5 under the same load;
  at 60-69 fps (fix/w45-placement runs) the three foundings fell in one snapshot and it passed.
* Fix `civ.instant_found` (BUG-0624): the host's hook broadcasts an instant founding too (MSG_CIV_BUILD
  kNextCitySite, noun 1 = instant; 0 = with the timer as before), a guest founds it with instant=1 at once (the city
  object comes with the city simulator's next Update tick, at most 1 s of game time, docs/re/civ.md 8); the catch-up
  founds instantly as well (the city is late already, the guest skips the new-city cinematic of a city it missed).
  Off: no broadcast for instant foundings, the catch-up with the timer (0.7.8).
* ~ A founding with the timer (the game's own AI, `instant` 0) still runs each game's own appearance timer: a guest
  whose game time runs slower (hitches) has the city seconds after the host (play_civ 20261106-105201: founded on
  both at 11:18:04-05, the host's city at 11:18:10, the guest's at 11:18:17). Not changed: the guest's new-city
  cinematic belongs to that path.

## The host's own vehicle designs on a guest (2026-11-07, fix/w46-tribe-civ)
<a id="civ-host-designs"></a>

BUG-0647. Lab lane 4.
* ✔ (runtime, `coop.memfind`) a vehicle's model key is at **+0xC4** (instance, type 0x24682294, group 0x40646200: the model
  object at +0x34, its +0x90; cVehicle::Load 0xCA67C0 sets it with vt 0x94 of +0x34, SetModelKey).
* ✔ (runtime) The host's civilization names its vehicle designs by keys of the host's own My Creations; the guest's copy of
  that civilization names the same keys (`coop.civ vdesign`). With another creation under the same id in the guest's My
  Creations, the guest's mirror was built from the guest's own model (lab 20261107-074609: the mirror's bem 76023C9D, the
  host's DD673F46): the player's "monsters" (two big creature-like models) at the host's vehicles.
* Fix `creations.host_designs`: a guest asks the host for each vehicle design key a mirror needs (MSG_CREATION kind 6); the
  host answers with the records when the key is one of its own My Creations (kind 7, a bundle) or "not mine" (kind 8); the
  guest registers them under a key of its own (0x7D...... from the content) and creates the mirror from it, or re-keys a bound
  vehicle of its loaded world (SetModelKey). No mirror is made while the answer is out (at most 20 s, then the key as it is).
  Mirrors are kept out of saves (BUG-0470), so the key of ours never reaches one. Test civ_host_design (lab
  20261107-074609 FAIL, 20261107-074448 and 20261107-074935 PASS). The buildings have the same collision: BUG-0670.

## A guest's attack of an NPC city (2026-11-07, fix/w46-tribe-civ)
<a id="civ-guest-attack"></a>

BUG-0648. Lab lane 4.
* ✔ (runtime) A guest's real attack: a right-click on an NPC city opens its menu, the crosshair (926, 392 with the city at the
  centre) makes the guest's game call SetOrder(vehicle, city, 2): civ.cpp sends kind 2 (the host's city index); the host's
  vehicles took order 2, the city's hit points fell (5200 -> 3971) and its capture meter rose for the player civilization
  (5/800021CA) on both games. `coop.civ tables` prints the guest's last order (`last_order=type/kind/index/noun`).
* ~ The report (0.7.8) had the guest's vehicles up to 147 m from the host's (sync-check `v: 5 apart`) and NPC cities laid out
  anew after a capture (BUG-0602, fixed in 0.7.9): the guest saw its vehicles at a city hall the host's vehicles had not
  reached. Not reproduced on 0.7.10.

## The partner's pointer tag in Civ (2026-11-07, fix/w46-tribe-civ)
<a id="civ-partner-tag"></a>

BUG-0649.
* ✔ (code, coop.cpp on_overlay) every remote player in Tribe and Civ is a Marker at the planet point under that player's mouse
  (modes.cpp rts_fill); its name tag is drawn in green on every game the same way, while the local entity exists (rts_anchor:
  the player tribe or the player civilization's first city).
* ✔ (the report's own screenshot, guest s 5930) the host's green name tag is drawn on the guest's screen, at the host's
  vehicles (where the host's mouse was), next to the blue owner tags of the vehicles with the same name. The tag is at the
  partner's mouse, not at its camera; it jumps with the mouse (up to 154 m/s in the guest's log).
## A vehicle mirror drawn behind its own target, and the state ages of a loaded host (2026-11-07, fix/w46-k3-bodies)
<a id="civ-place-now"></a>

BUG-0438 (K3). Measured with `coop.bodyrec start diag` (SPORE_BODYREC_DIAG=1 for coop_body_trail.py): the guest records
per vehicle mirror `w<id>` (the target guest_tick aims at: the velocity estimate moved on by the state's age and the
one-way latency, at most kVehLead 0.3 s) and `e<id>` (x: the state's age, y: the estimate's speed, z: the error before
the glide) next to the drawn vehicle; tools: the decomposition in docs/06-testing.md#testing-bodyrec-diag.

* ✔ observed (body_trail_civ_loss, lab 20261107-074040 and the twin runs below): the drawn vehicle was 0.4-1.8 m (p50)
  from its own target. guest_tick moved it half way from where it stood to the target every frame (StopMovement +
  Teleport, the game does not move it), so at 15-20 m/s it was a frame's glide plus the frame the placing shows up in
  behind the target.
* ✔ fix `coop.fix civ.place_now`: the mirror goes on with the estimate's velocity for the last frame's time and only the
  rest (a new state's correction, a push) is glided half per frame; the target is taken one frame ahead (the placing is
  drawn in the next frame). Drawn vs target p50 0.98-0.99 / p90 2.24-2.99 m off (SPORE_FIX_OFF=civ.place_now, lab
  20261107-082632, lane 7) against 0.25-0.46 / 0.94-1.49 m on (lab 20261107-082432, lane 6), at the same time and load.
* ✔ observed: the target's own error is the state's age. At a lab load of 11-16 on 6 cores both Civ games ran at 16 fps
  (frame p50 60 ms, p90 140 ms): the host's 0.1 s send gate gave states every 0.14 s (p50) to 0.39 s (p90, max 1.2 s),
  ages p50 0.12-0.15 s, and the target's error grew with them (aim p50 0.5-1.2 m under 0.15 s, 1.5-1.9 m at 0.15-0.25 s,
  2.1-2.6 m at 0.25-0.35 s, 2.8-3.5 m over). The vehicles of the test turn all the time (coop.civ move to a new point of
  the city every second): replayed on the host's own path, a straight extrapolation from the last two states is p90
  0.9 / 1.9 / 3.1 m off at 0.1 / 0.2 / 0.3 s, an arc (the yaw rate of the last three states) 0.8 / 1.7 / 2.9 m: no model
  of the path makes a 0.3 s old state good at 15-20 m/s. What is left of BUG-0438 at that load is the host's frame rate.

## The host's own building designs on a guest (2026-11-07, fix/w46-misc)
<a id="civ-host-buildings"></a>

BUG-0670, the buildings of docs/re/civ.md#civ-host-designs. Lab lane 9 (`tools/tests/coop_civ_host_building.py`).
* ✔ (Ghidra) `cCity::SetBuildingModel` 0xBDB940: the design id per building type (house 0xFF10521 -> 0x4E3F7777,
  entertainment 0x1A55E4D -> 0x72C49181, factory 0xECADE42 -> 0x47C10953, **0x1007AE63 -> 0x99E92F05**, every other
  type 0x4E3F7777), `GetDesignKey(civ +0x590, id)` (instance 0: FUN_00bf98c0 generates the design first), then the
  building's model object (+0x34) vt 0x94 SetModelKey(key). The 0x1007AE63 case is missing in civ.cpp
  `building_design_id` (BUG-0469's content check skips that type).
* ✔ (runtime) A building's model key is at **+0x34 +0x90 = +0xC4** (type 0x2399BE55, group 0x40636200), as for
  vehicles: the host's house of the test's design shows it (`coop.civ bmodels`).
* ✔ (runtime, lab 20261107-120600) With another creation under the same id in the guest's My Creations, the guest's
  copy of the host's house was the guest's model (bem 9A907DC4, the host's 0B25F69B), for a house built while both
  played and for the house of the loaded world after a rejoin.
* Fix `civ.host_buildings`: once a second a guest checks every building of every city; a key the host answers as its own
  design (creations::host_design, MSG_CREATION kind 6/7/8) gets the host's records under a key of ours (0x7D......) and
  the building SetModelKey with it. The building's own key is written back into +0xC4 while the game saves (savestrip
  save hooks) and with SetModelKey when the session ends. Test civ_host_building (FAIL 20261107-120600, PASS
  20261107-120446: the new house and the loaded one show the host's model; after `coop.leave` the guest's own again).

## A turret's combatant and the guest's copies of turret shots (2026-11-07, fix/w46-issue198)
<a id="civ-turret-combatant"></a>

BUG-0688 (GitHub #198), GOG 2024 addresses. Lab lane 2 (`tools/tests/coop_civ_turret_shot.py`).
* ✔ (runtime, lab 20261107-204140, 2 host and 2 guest turrets made by City_AddTurret) a city turret object (noun
  0x436F342) has the vtable **0x1466E30**; its combatant is at **turret + 0x588**, vtable **0x1466C28** (the GOG 2016
  0x1467758 of the turret TakeDamage row above). +0x120 (a building's combatant) holds no object in a turret: seen
  0x00000000, 0x3DA4944E, 0xAA670100, 0x2134B200 (floats / flags); the player's minidump had 0x6DB2C23E.
* ✔ The attacker of a turret's hits (the TakeDamage hooks' `att`) is that combatant: att_vt 0x1466C28 in every hit
  of the test; `cosmetic_hit` finds the turret at att - 0x588 (640 of 640 guest hits dropped, lab 20261107-204140).
* ✔ Cause of the crash: guest_fire took every turret shooter's combatant at +0x120 and weapon_of called vt 0x14 of
  that word: with `SPORE_FIX_OFF=civ.turret_weapon` 284 execute faults on the guest's main thread in 60 s (one per
  copied turret shot, eip = the garbage "slot", here 0x8080FF00; the lab's frame guard, BUG-0667, caught them, a
  player's game died: #198, eip 0xCCCCCCCC), no copy fired (lab 20261107-204522). Turret shot relay (BUG-0477) had
  never run in a test before: the fixtures' NPC cities had no turret near a fight.
* ✔ Fix `civ.turret_weapon`: a turret's combatant at +0x588; weapon_of calls vt 0x14 only when the combatant's
  vtable lies in SporeApp.exe's image and its slot 0x14 in an executable section of it (every shooter kind; a wrong
  one is skipped, counted as `bad_combatant` in the `turret_shots:` line of `coop.civ` and logged once per vtable).
  With it the guest fired 264 copies of 264 host turret shots, no fault.
## SpendMoney goes below 0; the mod's paid build checks the money (2026-11-07, fix/w46-v2-civ-space)
<a id="civ-spend-below-zero"></a>

BUG-0613. Lab lanes 9/10 (`tools/tests/coop_civ_money_short.py`).
* ✔ (runtime, lab 20261107-205315 with `civ.build_money` off) `cCivilization::SpendMoney` 0xBEF860 subtracts from
  mWealth (+0x98) without a floor: a factory (1200) built by `coop.civ build` with ~500 (+ the city income) left the host's game at -660.
  The game's own planner only offers what the money covers, `host_buy` checks GetMoney first; the mod's paid build
  (`build_in_city(.., pay)`, the console's `coop.civ build` on either game) did not.
* ✔ A negative host total is broadcast as it is (MSG_RES) and every guest floors it at 0 (shared.cpp `money_clamp`,
  BUG-0106): host -800, guest 0 was the detector's `Civilization:state:money` row in every civ_coop run (the fixture's
  3000 went to a vehicle 1000 and the guest's house 1600, then the host's factory 1200).
* A building's cost is its vt 0x5C (float) once SetBuildingModel gave it its type; the fix reads it before the slot and
  destroys the instance (cGameNounManager::DestroyInstance) when GetMoney is below it: "not enough money".
* Not this cause: the campaign's `Civilization:state:money` rows (host 3530, guest 3000) in civ/advance, where every
  guest's game reloads the Civ world every ~22 s (mode 01654C08) and shows the save's money for a moment (BUG-0311 run
  20261107-141346 of rel/0.7.10).
## A raided old village on the guests within a second (2026-11-08, fix/w46-k8-open)
<a id="civ-tribe-ruins-fast"></a>

BUG-0659 (follow-up of [#civ-tribe-raids](#civ-tribe-raids)). GOG 2024, lanes 4 / 5.

* ✔ (runtime) With civ.tribe_ruins a guest removes a village 5.1-5.3 s after the host's game raided it (the second look
  came with the next 5 s list); the detector's one-sample `Civilization:planet:village` / `tribe_hut` / `tribe_tool` /
  `tribe_food_mat` rows of play_civ were that window (the next sample always had equal counts; the host's count falls
  17 -> 4 over a 10 min play_civ from its vehicles' raids).
* ✔ `civ.tribe_ruins_fast`: 3 lists 0.5 s apart after a raid: the guest removes it after 0.6-0.9 s.


## The city planner's editor after the stage teardown (2026-11-08, fix/w46-k7-findings)
<a id="civ-planner-stale-editor"></a>

BUG-0682, GOG 2024, lab lane 10. Follow-up of [the planner's lifetime](#civ-planner-lifetime).
* ✔ The editor update 0xD124E0 runs every frame in Space too (on a planet and in flight: `coop.civ planner_probe` saw
  the editor on every Space frame of the space fixture, +0x40 = 0 while no planner is open).
* ✔ The quit to the galaxy menu (`coop.menu`) frees the editor with the city input strategy while the stage leaves:
  the memory stays readable and +0x40 holds garbage (0x4394C5EB, 0xC3BCCD6F). With the editor trusted for 3 frames
  after its last update, planner_city() cast that garbage 2-3 times per quit (8 caught faults in 4 quits, game::cast
  reading the "vtable", stack word 0xEE9B2232 = the cCity cast type: lab 20261108-003304); the campaign_space fault
  (coop.dll+0x1071C8 from wsync's city row) was one of them.
* Rule (`civ.planner_stale`): the editor is trusted only in the mode of its last update and not while the stage is
  leaving (game::leaving_stage), and its +0x40 must be readable with a vtable in the game's image before the cast.

## End of the Civilization stage (2026-11-08, fix/w46-g4-tests)
<a id="civ-stage-end"></a>

BUG-0311. GOG 2024, Ghidra (tools/ghidra/q.sh --build 2024), static; lab logs 2026-10-30..11-08.

* ✔ `cCivilization::AddCity` 0xBF44B0 (thiscall(civ, cCity*) ret 4) pushes the city to +0x9C and sets the civ's
  level +0x298 through 0xBEF8C0 (only raises it; ctor 0xBF7A10 sets 1): 4 when the city count >= `cCivExpansion`
  +0xAC (0xBEFE80, the planet's city total), else 3 when > 3 cities, else 2. The player civ at 4 = owns the planet.
* ✔ `GameCiv_IsPlanetOwned` 0xCF7730 = player civ +0x298 > 3. The end is not checked periodically, only at:
  (a) `cCity::Capture` instant branch (arg3 != 0 or city +0x762): ChangeOwner, then CF7730 -> 0xCF8F60 + 0xCF7680;
  (b) `cGameModeCiv::HandleMessage` 0xCFD500, msg 0x44F1189 (cinematic ended, posted by EndCinematic 0xADFF50, so
  the mod's 8 s caption close delivers it) for CVG_Capture / CVG_Capture_Nuked: ChangeOwner of player civ +0x44C (the
  last city the player civ Capture()d) if +0x2E0 is pending, then 0xCFA220 (level >= 3 and hint 0x50A2E65 unseen:
  plays CVG_UnlockAir, no check), 0xCF7A30 (capture type +0x29C != specialty +0x540: 0xBD9330 specialty box +
  Pause 0x4BF38A7, no check), else the check;
  (c) the specialty box answer: listener 0xBDC950 posts 0x40FAFDF, the city listener 0xBE75C0 acts only when player
  civ +0x44C is that city: SetSpecialty 0xBE7520 (data 0x5107B1A), Resume 0x4BF38A7, 0xCFA280, then the check.
  ChangeOwner also calls CF7730, for achievements only (0xCF7790).
* ✔ Capture arguments (stack offsets): (newPol, type, instant, nuked caption, a5 -> ChangeOwner). Military 0xBE9AD0,
  religious 0xBEAB80 and buy 0xBE99A0 pass instant 0 (cinematic path).
* ✔ A nation's surrender (comm action 0x3EF09234 in 0xCFF010, answer "We happily accept your surrender.") takes all
  its cities through 0xBE9E00 -> Capture(pol, type, 1, 0, 1): the instant path, the end comes at once.
* ~ CF8F60 -> 0xE042A0(1) shows windows 0x6453746 / 0x652A628 and hides 0x44827B8; 0xCF7680 pauses (0x4BF38A7).
  Likely the planet-won UI that leads to the spaceship editor (not watched at runtime).
* ~ Consequence (BUG-0311, inferred from 2 failing lab runs): after the last capture by the cinematic path the end
  waits for the specialty box of exactly that city; a box hidden under a talk, closed by Esc, or a press on an older
  city's box leaves the planet owned (level 4) with no end. The campaign shortcut now captures with instant = 1, as the
  surrender talk does (coop_campaign.py kick_civ).
  CORRECTION (2026-11-08, fix/w46-g4-tests): in lab 20261030-104149 campaign_space#1 the 10th city was the
  shortcut's own Capture (host log 12:00:15 "Alice captured a city", owned_cities=10 after the caption), not a talk
  (BUG-0311's first root cause).

## The planner's palette: drag places, click opens the Sporepedia (2026-11-08, fix/w46-g4-tests)
<a id="civ-planner-palette"></a>

BUG-0189. GOG 2024, Ghidra (static only):
* ~ A tile always has a design: the planner setup 0xD0E170 asks GetDesignKey (0xBF98B0, culture set lookup
  cCultureSet::PickCreation 0xBF9840) for each design id (City Hall 0x99E92F05, house 0x4E3F7777, factory
  0x47C10953, entertainment 0x72C49181, ...) and hands the key to the tile (0xD0C860: msg 0x4A344E9 {id, key,
  png key}). A missing entry is generated by 0xBF9440: a Sporepedia template DB query (prop 0x2DC9D1E = design
  id), the first match stored (this is how civ_npc has `0679F5DE/2399BE55/40636200`).
* ~ Placement is a mouse-down + drag: msg 0xB2E18705 on a palette item of kind 0x81C74DBC (building) goes
  straight to CreateInstance(prop 0x456B66A) -> SetOwnerCity vt 0x54 -> GetDesignKey -> model vt 0x94 ->
  AddBuilding (0xD14197) -> drag 0xD09FA0; no design check. A **click** (msg 0x133B269E) calls 0xD08B90:
  Sporepedia::ShopperRequest::Show for that design id (shopper ids: house 0x8049B6DC, factory 0x47F2EBB2,
  entertainment 0xED90D2E6, City Hall 0x695CBB72 for 0x99E92F05); the editor comes from the shopper (msg 0x4A314E6
  -> SubmitEditorRequest). A pick in the shopper is msg 0x5132389 -> cCivilization 0xBF4C50(designId, key)
  (culture set entry, then every city's buildings of that design re-modelled through 0xBE3870).
* ? This revises 9.1 ("a click on the tile opens the building editor"): the click opens the shopper / editor; the
  drag places. Runtime: see BUG-0189.

## The planner palette at runtime: no design, no drag (2026-11-08, fix/w46-g4b-coverage)
<a id="civ-planner-palette-runtime"></a>

BUG-0189, lane 3 (GOG 2024), `civ_npc`, runtime of docs/re/civ.md#civ-planner-palette:
* CORRECTION (2026-11-08, fix/w46-g4b-coverage): "a tile always has a design" does not hold for the player civ
  of a new Civ game. ✔ `coop.civ design 0 house|factory|fun` prints key 00000000 for the player civilization
  (800021CA) on both games while the NPC cities' buildings carry template models (`coop.civ bmodels`: house
  067CEDDC, factory 067C7029, fun 067CEDE3, City Hall 067CEDE1). The planner then shows the house / factory /
  fun tiles with a blue badge and the tooltip "Design a new house!"; a mouse-down + slow drag of such a tile onto
  a plot does nothing (no building, no money taken): no generation by 0xBF9440 at planner setup was seen.
  The static reading of the drag path may still be right for a tile with a design; the badge tile is another
  state (? which window kind it is).
* ✔ With a design key set (`coop.civ bdesign 800021CA house 067CEDDC`, the culture set entry the game's
  GetDesignKey returns) the next planner shows the house model on the tile with its price ($1,600 in civ_npc),
  and a hover, mouse-down on the tile (123, 385 of 1440x1080), 30 small moves and a mouse-up on a free plot's
  screen point (`coop.civ slots <city>`, world_to_screen in the planner camera) place a house: the price is
  taken at the drop (shared money 3000 -> 1400 on the guest at once), the building is snapped into a plot of
  the planner's choice (here slot 3, not the plot under the cursor), and it shows in the city's building list
  (`coop.civ buildings`, cCity +0x340) only after the planner is closed with the green check.
* ✔ A guest's placement goes to the host through the AddBuilding return address 0xD1419C (civ.cpp h_AddBuilding,
  CMD_BUILD): the host's game had the house at the same position 1 s after the planner closed, money equal on
  both games (`civ_planner_build`). With the send off (`coop.fix civ.planner_send off`) the guest's house is
  removed by the guest's reconcile within 2 s (the host does not have it) while the price stays paid.
* Palette tiles of the Civ planner (1440x1080): City Hall (123, 215), house (123, 385), factory (123, 558),
  entertainment (123, 730), turret (123, 905); the green check (1412, 1054).

## Fork notes: the shared message box and the Civ -> Space editor request (2026-11-10, plan/w47-pr196)
<a id="civ-shared-message-box"></a>

From the fork PandizeLive/SporeCoop-Lineages PR #196 (BUG-9057, BUG-9070), not verified by us:

* ~ The captured-city specialty question is a window 0x06318E78, the same control id as the "game saved" box
  (docs/re/creature.md, CORRECTION 2026-11-10); its choices are 0x05107B19 (Religious) and 0x05107B1A (Military), it has
  no OK 0xFFFFFFF1. An unanswered specialty question also holds the fork's capture checkpoint. Our watcher: BUG-0846.
* ~ The native Civ -> Space spacecraft EditorRequest: editor +0xC = 0x96B24187, model +0x10 = 0, return mode +0x1C =
  Civ (0x01654C04 in the fork's notation), context +0x90 = 0x0116D51D; after the vote the host showed input_lock and
  paused with the request held (BUG-0855). Which pause reason it uses is not known (our BUG-0576 rule masks the pause
  only for reason 0x4BF38A6).
## The city-alarm boost and its base speed (2026-11-10, plan/w47-reports)
<a id="civ-boost-stacking"></a>

* ~ (code audit, GitHub #199) The alarm boost (h_OnAttacked, civ.cpp) re-reads a vehicle's `+0x218` (top speed) as
  its base whenever the last boost has run out, and the alarm of a city repeats every 20 s, the boost's own length.
  A hit in the game update after the boost's end and before that frame's boost_tick reads the boosted value: the
  base grows x1.5 per cycle of a continuous siege (BUG-0771). An entry erased while its vehicle lives (out of the
  vehicle table, on_world_change) is not restored (BUG-0773). Not yet seen in the lab.
* ? Whether and when the game's RecalcStats rewrites `+0x218` from the vehicle's design (it would heal a stuck
  value): not known.

## The alarm boost measured, and the guest's jump limit (2026-11-12, fix/w48-civ-speed)
<a id="civ-boost-measured"></a>

Closes the "not yet seen in the lab" of docs/re/civ.md#civ-boost-stacking (GitHub #199, BUG-0771 / 0773 / 0876 / 0787;
test `civ_boost_stack`, `civ_fast_mirror`).
* ✔ The stacking is real once the order of the hit is the one the audit described: an alarm in the moment the last
  boost has run out and before boost_tick restored it (`coop.civ alarm <city> expire` puts every boost's `until` just
  behind now, then runs h_OnAttacked) re-read the boosted `+0x218` as the vehicle's own speed: 19.12 -> 28.68 -> ...
  -> 217.8 (11.39x after six alarms), and after the boosts ran out the vehicle kept 145.17 for good (lab
  20261112-003101, `civ.boost_base off`). ~ That this order happens in a real siege is inferred (alarm cooldown and
  boost both 20 s, OnAttacked on every hit inside the game's update, boost_tick once per frame in host_tick): not
  measured with an NPC army.
* ✔ A vehicle drives at the `+0x218` it has when it gets its order (cVehicle::SetMoveOrder -> MoveTo reads it): the
  host's free vehicle with 145 written was seen on the guest at 225 m/s (1 s windows); 28.7 written: 29-30 m/s; 55
  written: 58-72 m/s. So the stacked value is what the players see, on both games.
* ✔ civ_npc has a single player city (the shared capital, owner 0xFD): its alarm boosts every player's vehicle (owner
  0..3), here the host's own free vehicle. With `civ.boost_players off` it also boosted the three NPC vehicles
  within 600 m (12.27 -> 18.41, lab 20261112-003106).
* ✔ In the host's tick a living vehicle never misses from `g_hv` at boost_tick time in normal play: the table is
  pruned and re-listed in the same 0.1 s step that runs after boost_tick (a pol change gives a new id at once). The
  BUG-0773 leak needs the tables cleared while the vehicles live (on_world_change on a false world change: the
  BUG-0223 class) or a test drop (`coop.civ hvdrop <id>`). Both left 28.68 for good before the fix (lab
  20261112-003336). The fix writes the own speed back only while the vehicle is in the game's list, of the same nation
  and still at exactly `orig x 1.5` (the value the mod wrote), so a real load writes nothing into a new vehicle at a
  reused address.
* ✔ The guest's jump limit (BUG-0186 `2 x top + 10`) used the copy's own `+0x218`. A host vehicle at 55 m/s against a
  copy whose own top is 19.1 (limit 48): every snapshot step was a "jump", the estimated velocity 0, the copy only
  glided half way per frame: it trailed 4.8-4.9 m (p50) and 6.8 m (p90) behind the host's vehicle at the same moment,
  `jumps=130` in 20 s (lab 20261112-003654, -003833). It did not stand still and teleport as the ticket guessed (the
  glide catches up below 30 m). With the host's top speed in the snapshot: 0.7-0.9 / 1.8-2.3 m (lab 20261112-003659,
  -003838).
* ✔ Wire format (from 0.7.13): MSG_CIV_STATE vehicle chunk = n x 41 bytes, u32 host time, then n x f32 top speed
  (+0x218) in the same order. A 0.7.12 guest sees more than the time after the vehicles and drops only the sender time
  (velocity from arrival times, as with a host before BUG-0122); a 0.7.13 guest of an older host uses its own top.
* ? Whether the game's RecalcStats rewrites `+0x218` by itself during play (it would heal a stuck value): still not
  seen; with the boost off the value stayed for minutes (BUG-0040 and here: 145.17 22 s after).
## Captures and purchases on one game only (2026-11-12, fix/w48-captures)
<a id="civ-capture-keeps"></a>

BUG-0875, BUG-0775, BUG-0877, BUG-0878 (Discord 2026-11-09: "their settlement is captured, the host's is not"). GOG 2024,
lanes 4 / 5, fixture civ_npc.
* ✔ (runtime) A capture through the capture meter (`coop.civ siege <city> <player> <hits> pc`: AddCaptureDamage with
  the player civilization's political id, the game's own CheckMilitaryCapture at 100) does **not** change the city's
  owner at once: `Capture` plays the capture cinematic and the city is the player civilization's 8 s later (the
  caption closed by civ.caption), from the cinematic-end message (GameCiv_HandleMessage, CVG_Capture: the pending
  ChangeOwner). So when h_Capture names the captor the city is not in the player civilization's list yet, and the
  drop loop of host_assign_cities ("cities the player lost") erased the owner it had just written: every such capture
  was dealt out like a new city. (`coop.civ capture` through Capture directly showed the same; lab 20261112-003012.)
* ✔ (code + runtime) host_assign_cities' resplit: with 2 players and 1 city the capital is shared (`cities < players`);
  the second city crosses the threshold, every city but the gifts is dealt again: the capital to the host, the
  captured city to `players[1]` = the guest. The host's own capture became the guest's (lab 20261112-004217, fix off:
  owner 1 on both games, the guest's game lets the guest buy there).
* Fix `civ.capture_keeps`: the captor waits in `g_capture_by` until the city is the player civilization's (120 s at
  most); a city a player captured or was given keeps its owner at a resplit while that player is in the session; the
  free cities go first to the players without a city, and the capital stays shared while a player would have none.
* ✔ (code) The captor was the player whose vehicle was nearest the city (BUG-0775). The capture runs inside the hit
  that took it (building / hall TakeDamage -> AddCaptureDamage -> CheckMilitaryCapture -> Capture), so `g_hit_by`
  (b_damage) is the player whose vehicle hit last. Fix `civ.capture_credit`: that player, else the last player hit in
  the joint-siege window (`g_siege`), else the nearest vehicle; `coop.civ tables` prints `captures: credit_cmd=
  credit_hit= credit_siege= credit_nearest=`.
* ✔ (Ghidra, GOG 2024) The purchase of a city: pie command 0xCF3350 -> `cCivilization_BuyCity` 0xBF0520 (the talk
  with the city's nation, price offers) -> the talk's answer 0xCFF010 -> `cCivilization_FinishBuyCity` 0xBF0600 ->
  `cCity_Buy` 0xBE99A0 (GetMoney, SpendMoney, Capture type 2). On a guest Capture is refused (h_Capture), the
  spending reaches the shared money: money gone, no city (BUG-0877, lab 20261112-003450 with civ.buy_city off: the city
  the NPC's in both games). Fix `civ.buy_city`: the guest's cCity_Buy sends CMD_BUYCITY (6: city index, price) and
  returns; the host's game runs cCity_Buy itself, credited to that guest (`g_capture_for`); the host's own purchase
  is credited to the host. A refused one tells the guest ("not enough money or no trade route").
* ✔ (runtime) While the host is away (local play, civ.away) a guest's game besieges and captures by itself: its
  CheckMilitaryCapture fires the "won" event and calls Capture, which a guest refuses; nothing went to the host (lab
  20261112-003556, civ.away_capture off: the city the NPC's on both games after the host's return). Fix
  `civ.away_capture`: that Capture sends CMD_CAPTURE (7: city index, type) once per city and absence; the host holds
  it (civ.cmd_hold) and runs h_Capture for that guest when it is back. The host's city indices survived its reload of
  the same save in the test (the city by its place).
* ? Not checked: a religious conversion's capture (Convert, no hit: credited by g_siege or the nearest vehicle); the
  owners after a host's load (on_world_change clears g_city_owner, every city is dealt anew: captures are not kept
  across loads, BUG-0950).
## Mirror vehicles: a freed vehicle's block (2026-11-12, fix/w48-freed2)
<a id="civ-mirror-vehicle-reuse"></a>

BUG-0951 (class BUG-0426). GOG 2024, runtime on lane 6 (`coop.civ reusehunt`, docs/06-testing.md#testing-freed2-w48).

* ✔ A vehicle removed by the disband path (0xCAA7B0, `kill_local`) is freed later and its block goes to the next
  vehicle the game makes: `cCivilization::CreateVehicle` of the same purpose and locomotion for another civilization
  (the guest's own, the game's generated design, key 0) got it on the 2nd try in 3 of 4 hunts (lab 20261112-050123,
  20261112-050242).
* ✔ A guest's mirror does not follow it even with object_alive: `prune_gone` (every Civ frame, the game's vehicle
  list, pointers only) drops a mirror whose vehicle left the list, which happens at the removal, before the free; the
  new vehicle at the block is then a vehicle the host has not and the guest's cycle removes it (both with
  `civ.mirror_lifetime` on and off). prune_gone runs only while the Civ mode is active.

## City owners across a host's load (2026-11-12, fix/w48-new)
<a id="civ-owners-kept"></a>

BUG-0950. ✔ (runtime, lane 5, civ_npc) A host that saves (`coop.menu`) and loads the same slot again gets the same
cities back at the same positions (within a metre); the mod's owner tables were cleared by on_world_change and
host_assign_cities dealt every city anew: the host's bought city became the guest's (lab 20261112-072252, fix off).
Fix `civ.owners_kept`: the host writes the owners of the player civilization's cities every 2 s per world
(world::current_game_key: `<slot dir>\<planet>.spo`) by position and owner name (with the gift / capture marks) and
gives them back at the first split after a world change (a city within 5 m, the owner must be in the session); when
every city got its owner back nothing is dealt anew. Kept in memory only: a restart of the host's game loses them, and a
new session deals the cities anew. `coop.civ tables` prints `kept: fix= worlds= this_world= saved= restored= key=`.
## A guest's build the host cannot make (2026-11-12, fix/w48-tribe-civ)
<a id="civ-build-refund"></a>

BUG-0807. GOG 2024, lab lane 2 (civ fixture).
* ✔ (runtime) The players' city of the civ fixture has 11 building plots (`coop.civ slots 0`); a house costs 1600 at
  every count (10 houses in a row, each 1600).
* ✔ (runtime, a 300 ms link) A guest's build of the last plot that reaches the host after the host's own build of it:
  the guest's game built it and paid (its copy of the city still had the plot), the host's build_in_city failed with
  "no free slot" (SnapToSlot), and the guest's copy was demolished by the reconcile: 3200 spent for one house before
  `civ.build_refund` (lab 20261112-085815), 1600 with it (20261112-085612).
* CMD_BUILD carries the paid price as a float after the place since 0.7.13 (the host reads it only when present).
## The Civ -> Space launch's pause and the spacecraft editor (2026-11-12, fix/w48-tribe-civ)
<a id="civ-launch-pause"></a>

BUG-0855. GOG 2024, the host's game logs of campaign_space runs (four players).
* ✔ The spacecraft editor request of the Civ -> Space change is editor 0x96B24187, submitted through the co-op vote
  ("wants to edit a design (Civilization)"); the game submits it under its own pause and input lock, and the pause
  reason is **0x4BF38A6** (cGameTimeManager GetPauseCount 1 at the gate), the same reason as the City Hall editor at
  the end of Tribe (#tribe-graduation-save, BUG-0576). With `core.editor_own_pause` the editor opens 1.3-2 s after the
  vote (rel/0.7.13 lab 20261112-050529 and five more runs). Answers "Unknown: which pause reason the Civ launch uses".
* ✔ With `core.editor_own_pause` off the spacecraft editor waited at the busy gate ("input_lock,paused") and came up
  61 s after its vote (lab 20261112-100520, 20261112-102046): the hold the fork saw (BUG-0855).

## Wild creatures and a guest's orders on them (2026-11-12, fix/w48-civcmd)
<a id="civ-wild-creature-orders"></a>

BUG-0970. Lab lane 4, GOG 2024.
* ✔ (runtime) Wild creatures in Civ (noun 0x18EB45E, cCreatureAnimal) are not synced: each game runs its own. Right
  after the load both games have the same ones within 0.0-3.8 m (civ_npc: a herd of 10 near (-285 246 353), still
  within 3.8 m after 5 min without play; civ: 4). Moved, fled or killed on one game, they differ for good.
* ✔ (runtime) Every game also has a wild creature object without a species at (0 0 0) (not placed in the world;
  `coop.census creatures` skips it). A nearest-creature search without a small radius finds it from the surface
  (~500 m off).
* ✔ (runtime) A guest's right-click on a creature sends CMD_ORDER type 1 with target kind 3 (noun + position); the
  host now takes its own creature within 40 m of that place, else a move order there (`civ.creature_target`).

## A guest's order on a vehicle the host's game just removed (2026-11-13, fix/w49-live)
<a id="civ-cmd-listed"></a>

* ✔ (lab 20261113-022453, BUG-1053) Vehicle_Remove (0xCAA7B0, `coop.civ remove`) leaves the game's object list in the
  same call (`listed=0`), the host's table g_hv keeps the entry until the next host tick (0.1 s). SetOrder(v, <removed
  vehicle>, 1, 1) is accepted: v's mOrders (+0xB68) element 0 names the removed block as its target.
* ✔ The freed block of a removed vehicle is given to the next vehicle the game makes: three spawnveh calls ~5 s apart
  after three removals got the same address 1A7719A0 each time (lab 20261113-022453). A stale table entry is therefore
  quickly another vehicle of the same nation (BUG-0951's serial check covers host_tick; the commands use
  game::alive with the serial, civ.cmd_listed).

## The ground check of mirror vehicles while the host is away (2026-11-13, fix/w49-small)
<a id="civ-away-ground"></a>

BUG-1056 (owner decision 8 of plan-0.7.14). ✔ runtime (civ_npc fixture, lane 7):
- While the host is in the menu (local_play), a land mirror put on the spice node near (-336, 132, 354) stays there
  (0.2-0.4 m after 0.3 s) when Vehicle_KeepOnValidGround is skipped (civ.mirror_place, BUG-0186); with the game's check
  it is moved about 13 m off within 0.3 s (the same 13 m as BUG-0186's outliers). So the bypass is only right while the
  host's states place the mirror; in local play the game's own check runs (`civ.away_ground`).
- A guest's SetPosition (spatial vt 0x38) of a mirror its own game drives is undone by the locomotion within 0.3 s (it
  ended 630 m away, where it was driving): StopMovement (spatial vt 0xEC) first, as guest_tick does before Teleport.

## Wild creatures owned by the host (2026-11-13, fix/w49-civ)
<a id="civ-wild-sync"></a>

BUG-0973. GOG 2024, lane 4, fixtures civ_npc / civ.
* ✔ (runtime) civ_npc has 10 wild creatures (noun 0x18EB45E): a herd of 7 (species 06538C80, hp 10/5) near
  (-285 246 353) and three epics (hp 3000, about 506 m from the centre, one each). They run the same
  `cCreatureAnimal::Update` as the Creature stage's (game.cpp h_Update), so npc.cpp's update filter and host_tick see
  them. Census rows of noun 0x18EB4B7 next to them are not wild creatures (not updated through that hook).
* ✔ (runtime) The herd members have no archetype tuning (+0xE84) and their herd looks like the herds the mod makes
  (no archetype +0x88, no nest): npc.stray_ids (BUG-0385, `saved_stray`) gave them no NPC id, only the epics were
  tracked. Civ skips that rule (no partner's puppet exists in Civ to be saved).
* ✔ (runtime) The Creature stage's relevance pass (Relevance_Update, view_centres.cpp) runs in Civ too: one game
  sphere at the camera's point, enter 150 m, leave 200 m. A herd set 60 m aside (about 245 m from the host's camera
  point) stopped updating and lost its herd within a second (npc `pooled=7` on the host); the epics 500 m away kept
  updating (not checked why: no herd relevance for epics?).
* Fix `npc.civ_wild`: npc.cpp runs in Civ like the Tribe stage (rts_stage: proxies walked at their own ground height)
  without a centre: every wild creature the host's game updates goes to every guest in the world (kCivRelevance).
  The guest's own wild creatures are removed by the update filter; proxies take the nest herd of their species.
  `npc.view_centres_civ`: the partners' pointers (rts_fill) are spheres of the host's relevance pass in Civ too.
* Bandwidth: 10 creatures x 47 B (NpcState) + 2 B (agl trailer) every 0.1 s per guest = about 4.9 KB/s per guest,
  one or two unreliable packets per batch; spawns are reliable, once per creature. Measured (lab, civ_npc, 24-25
  creatures once the cameras moved, host -> guest in all): 7.6 KB/s with the sync off, 20.1 KB/s with it, 12.3 KB/s
  with `npc.civ_quiet` (a standing creature's state once a second).
* ? Not checked: the guest's pointer as a relevance centre in play (in the tests the guest's mouse stays off the
  planet, its centre is (0 0 0)); a guest whose host is away (local play) shows no wild creatures until the host is
  back (the Tribe stage's rule: proxies dropped, the own ones were removed).

## The specialty question goes to the buyer (2026-11-13, fix/w49-civ)
<a id="civ-specialty-buyer"></a>

BUG-0982. GOG 2024, Ghidra + runtime (lane 4, civ_npc).
* ✔ (Ghidra) `GameCiv_CaptureNeedsSpecialty` 0xCF7A30: cdecl, no arguments, returns bool in AL; reads player civ
  +0x44C (the last city it captured), compares its capture type +0x29C (0 military, 1 religious, 2 economic; a
  purchase is 2) with the specialty (0xBD8320 = `mov eax,[ecx+0x540]`) and calls `cCity_ShowSpecialtyBox` 0xBD9330
  (thiscall, ECX = city: the text by +0x540 / +0x29C, `cSPUIMessageBox::ShowDialog`, Pause 0x4BF38A7) when they
  differ. Its only caller is GameCiv_HandleMessage (the capture cinematic's end); returning 0 lets the handler go on
  to the planet-won check at once.
* ✔ (Ghidra) The box's answer: 0xBDC950 posts message 0x40FAFDF with the pressed button at data +8; every city's
  listener `cCity_MessageListener` 0xBE75C0 (this = city +0x20C) acts only for the player civ's +0x44C city: button
  0x5107B1A = take the capture type (`cCity_SetSpecialty(city, +0x29C, 1)` + effects), else keep; then Resume
  0x4BF38A7, 0xCFA280 (an effect and hint flags when the civ has more cities than before) and the planet-won check.
* ✔ (runtime) Fix `civ.specialty_buyer`: the host's hook of 0xCF7A30 sends the question of a city whose owner is a
  guest (owner table, else the pending captor) to that guest (MSG_CIV_CMD kind 8: city index, capture type) and
  returns 0 (no box, no pause on the host). The guest writes the type into its copy's +0x29C and calls
  ShowSpecialtyBox outside the frame (the game's own box and pause there); its listener hook takes the answer
  message once (the same data pointer reaches every city's listener), sets its copy, resumes the pause and sends
  kind 8 (city, take, type) back; the host's game calls SetSpecialty when the guest took the new one. civ_buy_city:
  question on the guest only, Economic taken, specialty 2 in both games (`coop.civ` city lines print `spec=`).

## The game's empty herds in a Civ save (2026-11-13, fix/w49-tests)
<a id="civ-save-herds"></a>

BUG-1067. GOG 2024, lane 5, civ fixture (Arigine), host + guest.
* ✔ (runtime) The host's Civ world: 82 herds (noun 0x1BE418E), 78 without an archetype (+0x88 = 0) and a nest, 71 of
  those empty, 25 members in all (`coop.npc archless all`); many have their home at (0 0 0). The guest's game: 90 / 86 /
  79. Wild creatures out of the camera's relevance are pooled and leave their herd empty (#civ-wild-sync), so most of
  the game's Civ herds are empty at any time.
* ✔ (runtime) Such a herd looks exactly like a herd the mod makes for a puppet or a proxy (CreateHerd(.., 0, 0, 0, 0)):
  savestrip's empty-herd rule of BUG-0255 left 72 of them out of the host's own save. After loading that save the game
  has 82 herds again (it makes the missing ones at load), but 52 of the 72 were not back at their home (the same
  species within 2 m): the herds' homes moved. No fault in 20 s after the load.
* Fix `savestrip.our_herds`: the mod's herds are known by their serial (`game::our_herd`, cGameData::mID +0x24), only
  those are stripped when empty.
* ? Not checked: what a moved herd home changes in play (where the wild creatures roam, their nests); a guest's save
  behaves the same (its 77 stripped in civ_save_strip, BUG-0973).

## The cinematic manager's enabled flag and loads (2026-11-13, fix/w49-camp)
<a id="civ-cinematics-enabled"></a>

BUG-1073, BUG-1079. GOG 2024, Ghidra (tools/ghidra/q.sh --build 2024) and runtime (lab lanes 4 and 5, a watcher
reading `[0x167EB50]` +0x15F / +0x2C / +0x148 every 5 s; `coop.cine` shows the same).

* ✔ `cCinematicManager::PlayCinematic` 0xAE08B0 plays nothing and returns 0 while the manager's byte **+0x15F** is 0
  (its first check). The only writer besides a table init is `SporeCoop::cCinematicManager_SetEnabled` 0xADF310
  (thiscall(mgr, bool) ret 4; 0 also empties the queue at +0x118..+0x128). Call sites: **0** right after
  `PlayCinematic("CRG2TRG")` (0xD3FFD2, the live Creature -> Tribe change) and after `TRG2CVG_PreEditor` (0xCDC6E8,
  TribeGame_AdvanceProc, the Tribe advance button); **1** in the Tribe start (0xCD4410, 0xCD8341, 0xCDD5ED), in
  Creature (0xD4538E), in the Civ start (0xCFD0CF) and in the galaxy menu's enter (0xEF24D4, vtable 0x148AFC0 +0x30).
* ✔ (runtime) Creature -> Tribe: 0 from CRG2TRG (13:36:28) until the Tribe start; the advance button: 0 from the
  click (13:22:34) through the City Hall and vehicle editors until the Civ start (13:25:22: 1).
* ✔ (runtime, campaign_tribe_end lab 20261113-133534 lane 5, from 13:40:52) The mod's reload of the host's Tribe right after the live
  change (tribe.host_reload, BUG-0332: Tribe -> galaxy menu -> Loading -> Tribe 6.6 s after the change) came before the
  Tribe start's SetEnabled(1): the flag stayed 0 in the galaxy menu (its enter did not run SetEnabled(1) on this
  path: another run of the same path showed 1 there, 13:38:02) and through the whole Tribe. A load does not set it.
  Seen in the full campaigns too: the host had no cinematic after the Creature -> Tribe change in all four jump
  campaigns of fix/w49-tests (`busy=cinematic` in its trace lines stops at 10:39 in lab 20261113-101808; 0.7.13's
  final check had them in Tribe and Civ).
* ✔ Consequence in Civ: `cCity::Capture` with instant 0 (sieges, conversions, purchases, the campaign's shortcut)
  sets the city's pending flags +0x2E0.. and leaves the ChangeOwner to the end of CVG_Capture (msg 0x44F1189); with no
  cinematic the city never changes hands. With instant 1 (a surrender) it does (the ChangeOwner is direct). In Space
  a destroyed ship's death cinematic does not play (spacecoop's respawn_tick: "no death cinematic runs: respawn").
* ~ (runtime, same run) In Tribe with the flag 0 the host's bot never box-selected its own members (32 tries, "no
  member selected") and the full campaigns' minimap calibration read the same view for three clicks: inferred, the
  view moves of a minimap or card click go through the cinematic manager and do nothing while it is off.
* Fix (coop.dll, gamefixes `game.cinematics_on_load`): when a load ends in Cell / Creature / Tribe / Civ / Space with
  +0x15F 0, SetEnabled(1). A boot + load starts with 1, so a loaded stage never needs 0.

## Wild creatures parked while the host's game stands still (2026-11-13, fix/w49-proxy)
<a id="civ-wild-parked"></a>

BUG-1089. GOG 2024, lab lanes 4 and 5, civ_npc.
* ✔ (runtime) The host NPCs at (0 0 0) of the full campaign's sync-check MISMATCH (lab 20261113-160426, 14 of
  species 067A147A, hp 8-16) are wild creatures the game took back into its pool (the planet's centre, no herd) while
  the host's simulation stood (the host's trace says `busy=paused` at 17:00:21; the BUG-0339 diagnostic "is updated at
  the planet's centre" never fired for them, so host_tick never saw them updated there: they were parked in frames
  without creature updates). The sync-check's entries are the last running frame's table (npc.entries_listed), the
  positions are read now: (0 0 0) on the host, the proxies on Bob's ground.
* ✔ (runtime, civ_wild_pooled with npc.frozen_pooled_rts off, lab 20261113-174015) With the host in its pause menu
  and its camera panned to the other side of the planet, the game parked a herd of 14 (species 066742CD) by itself:
  the same MISMATCH "n: 14 apart" (519-537 m). So the Civ relevance pass (#civ-wild-sync) runs during the pause menu
  and parks creatures although no creature is updated.
* Mod side: host_tick returned at once in Tribe / Civ frames without creature updates (the Creature stage's frozen
  pass, BUG-0413 / BUG-0339, is relevance per guest and was skipped for the RTS stages), so nothing was despawned until
  the game went on. Fix `npc.frozen_pooled_rts`: every 0.5 s of such a freeze the tracked NPCs that are no creature of
  the world any more or are within 50 m of the centre are retired (despawned at every guest).
* ~ Seen, not changed: dead creatures the game keeps updating at the centre (BUG-0339's corpse case, 4 lines at
  17:01:03 in the same campaign) are not despawned in Civ by distance (every guest gets every wild creature); not
  compared by the sync-check (dead), not looked at here.


## A pooled wild creature given out again during the host's stand (2026-11-14, fix/w49-fcgates)
<a id="civ-wild-reused-frozen"></a>

BUG-1100. GOG 2024, lab lane 7, civ_npc. Follows #civ-wild-parked.
* ✔ (logs, civ_wild_pooled of the 0.7.14 final check, lab 20261114-005006) While the host stood in its pause menu
  (`busy=paused` at s 40 and 50), its object 10E5C360 of NPC #129 read species 065CBF12 at 517 m from the centre with
  `moved64=507.1` (it went through the planet's centre within its last 64 updates); the guest's proxy #129 was species
  06538C80, last news 15.4 s old, at (-387 -328 0): 690.9 m apart (sync-check MISMATCH n129). So the game's pool took
  a creature back and gave the same object out again as another species within one stand of the host's game, between
  two 0.5 s frozen passes (npc.frozen_pooled_rts saw neither "gone" nor "at the centre").
* Inferred: the Civ relevance pass during the pause (the camera moved) both parks and brings out herds; it reuses the
  pooled objects for the new ones. A natural reuse could not be forced in the lab (as for BUG-0044): the test makes the
  same state with `coop.npc kill <id>` (the death flag) and `coop.npc reuse <id>` (alive again) during the pause.
* Fix `npc.frozen_reused`: the frozen pass retires a tracked object that npc_object_reused calls another creature
  (another species, or alive after dead), as host_tick does while the game runs.
* ✔ (runtime, civ_wild_pooled 20261114-033051 with the BUG-1100 log line) In the host's stand right after the civ_npc
  fixture's start the game rebuilt 14 tracked objects of species 06689607 as 065276AC / 066742CD within 2 s, several
  at unplaced points with z = 0 ((0 524.8 0), (-368.7 -347.0 0)), the rest at their new herds: the reuse in a stand is
  common, and the guest's #129 at (-387.0 -327.7 0.0) of the final check was such an unplaced object.

## City and vehicle owners across sessions: claims by position (2026-11-15, feat/w51-store)
<a id="civ-owner-claims"></a>

BUG-1153 (E1 of 0.7.15), lab lanes 9 / 10 (coop_civ_owners_session.py).
* The mod's owner tables (g_city_owner, the vehicle table g_hv) are rebuilt by every world change and every session:
  the save holds no owner. civ.owners_kept (BUG-0950) kept them only in memory, by the owner's name.
* ✔ Now each player city and vehicle has a claim (civ.cpp g_city_claim / g_veh_claim: its owner's stable key, the host
  "host", the shared capital "shared"), written with every save of the game into the host's per-player store
  (docs/01-architecture.md#arch-player-store, world record `civ.owners` v1: city spots then vehicle spots, each
  {x, y, z, key, flags 1 gifted / 2 captured}; owner_claims.h). After the load the first host tick with cities pairs
  them with the spots (one to one, nearest first, 5 m; vehicles 6 m): the saved positions come back exactly (lab: 3 of
  3 cities, 2 of 2 vehicles, from_disk=3/2).
* A claimed city (not the capital) is dealt to its player when it is here; while it is away the host holds it (never
  another guest); its join gives it back (the deal on a changed roster). A claimed vehicle the same, on every change
  of the roster. The claims follow the owners every 2 s for players that are here (a gift, a capture, a deal), and at
  once for a gift (CMD_GIVE) and a capture (h_Capture). A world with claims from the disk skips BUG-0950's name-keyed
  restore.
* ? A vehicle that drove more than 6 m between the save and the load is not found (the save keeps its position, so
  only a load of an older save). A city that changed hands in the host's game while its claimed player was away (an
  NPC capture) keeps its claim only while it is a player city.
## New designs between the games: the culture set (2026-11-15, fix/w51-editors)
<a id="civ-culture-sync"></a>

E4, BUG-1157, BUG-1158: cCivilization +0x6C is an EASTL map design id -> key, set by `Civ_SetDesign` 0xBF4C50 (the
planner's pick, the editors' return), which re-models the civilization's buildings and vehicles of that design.
Before 0.7.15 the host's new designs never reached a guest kept in place (BUG-0508) and a guest's own pick or editor
changed only its own game. Now the wsync row `civ_design` carries the host's entries (the guest calls the same setter),
a guest's own design editor is refused and its own pick goes back. Details and lab runs:
docs/re/creations.md#creations-stage-designs.

## The guest's specialty box stopped its game time (2026-11-15, fix/w51-civveh)
<a id="civ-specialty-runs"></a>

BUG-1175. GOG 2024, runtime, lanes 6 and 7, civ_npc.
* ✔ (runtime) `cCity_ShowSpecialtyBox` (#civ-specialty-buyer) on the guest takes one Pause 0x4BF38A7 at once
  (`coop.pauses` right after the box: `flags=3 paused=1 reasons: 04BF38A7=1`); the game time stands until the answer.
  The host's game goes on (BUG-0982 does not pause it).
* ✔ (runtime) A city the host's game founds while the guest's game stands is replayed on the guest (SpawnNextCity,
  "here founded") but does not appear: the appearance is the timed callback of 9.3 and waits for the game time. The
  guest then has no civilization of that political id, so `CivByPol` gives nothing and the new nation's vehicles get
  no mirror (`v: N missing on the guest`). They appear 1-3 s after the answer (the city, then the design request of
  BUG-0647 for its vehicle). civ_owners_reload left the box up 16 s and the game founded the next site after each
  purchase (the host: "a tribe of the Tribe stage is destroyed", ApplyStamp of the new city ~20 s after the buy, held
  by the capture cinematics): 2 of 2 MISMATCH on rel/0.7.15 9362bcdd.
* ✔ (runtime) Resuming 0x4BF38A7 right after the box opened keeps the box up and working: its buttons answer (the
  listener's message 0x40FAFDF), the specialty is set in both games, no pause is left counted; the game time runs
  behind it (the new city and its vehicle on the guest within 5 s). Fix `civ.specialty_runs`: the guest counts the
  box's pauses before and after ShowSpecialtyBox, resumes the difference and does not resume again at the answer.

## The city citizens' binds after a load (2026-11-17, fix/w51-citizens)
<a id="civ-citizens-reload"></a>

BUG-1221, BUG-1227. GOG 2024, runtime, lane 4, civ_npc; the guest's `coop.wobj list citizen` (which host citizen each
of its citizens is bound to) next to both games' census (coop_civ_citizens.py prints it for every miss).
* ✔ (runtime) After the host loads a save in running games (the lab's warm reset, `--reload`), the two games' 19 city
  citizens near the capital stand at other spots: the guest's first snapshot bound them first come in the host's id
  order within the kind's 30 m, and 2-5 host citizens were left without a free guest citizen within 30 m (`kind citizen
  binds=14 own=5` after a load, lab 20261117-123109). Their free guest citizens stood 32-76 m away (`unbound host
  citizens [2, 9]: the nearest free guest citizen 71 m, 37 m`, 20261117-121708 #3) and walked on their own; some came
  within 30 m by themselves after 20-40 s, others not for the whole test (the final check's 5 of 19).
* ✔ (runtime) The citizen serials (cGameData +0x24) after a load are small numbers again (host 2..20, guest 2..20): the
  game numbers the new pool from the start. The fields kind's low-16 ids do not collide (150 distinct per planet).
* ✔ Fix `civ.citizen_rebind` (worldobj.cpp): a snapshot's new binds go by nearest pairs over all its host citizens
  without a copy (`pair_nearest`), then a host citizen still without one takes the guest's nearest free citizen within
  120 m (also by nearest pairs), which is stopped and put at the host's place (game::stop + game::place, as
  citizen_follow's put) and bound. 120 m: a city's citizens stand within ~80 m of each other, the cities are hundreds of
  metres apart (civ_npc's far city 855 m), so a citizen of the guest's other city (its camera's, BUG-0553) is not taken.
  With it on, 5-18 relocations per load and `binds=19 own=0` after every load (lab 20261117-120957, -123516).
* ✔ (runtime) The "another size" pairs of civ_citizens (the triage's relocation try, 2 of 6 runs) are no wrong sizes:
  the guest citizen at a host citizen's place is the copy of a neighbour 3-6 m away with that neighbour's size, and the
  host citizen's own copy stands 3-6 m away with its size (walking copies trail and cross, BUG-1227). 0 of 172 standing
  pairs (both moved under 0.5 m in a second) had another size; `fields civ_citizen differ=0`.
* `coop.wobj loose <kind> <n> <m>` (tests): the guest drops the binds of its n bound copies nearest the snapshot's centre
  and puts each m metres farther out along the ground: the state a load leaves, made at will.
## Wild creatures' copies, the ground and the sync-check (2026-11-17, fix/w51-civsync)
<a id="civ-npc-ground"></a>

BUG-1216 (the campaign's Civ segment failed the sync-check gate `n apart`). GOG 2024, lanes 5 / 9.
* ✔ (runtime, final check lab 20261116-025154 campaign_tc2) The host's live Civ world and the guests' copy of its save
  can differ in terrain height: at a standing herd (species 067A147A) the host's creatures stood at agl 0.00 on the
  host's ground, every guest's ground at that point was 9.2-10.5 m higher (`want_agl=-10.47`) and every copy stood on
  its own guest's ground (`agl=0.00`). A player sees the herd on the ground; only the sync-check's absolute positions
  were "8.0-10.2 apart". No terrain stamp of either game lies within 250 m of the point; why the heights differ is not
  found (the segment runs, where every game loads the same save, never showed it). There the guest's ground is also a
  steep slope: the copies slid and were trail-teleported back every ~2.5 s (BUG-1228).
* ✔ Fix `sync.npc_agl`: every digest entry of an NPC carries its height over the sender's own terrain (an appended
  trailer, older readers stop before it, an older sender's entries are compared absolutely), and the host compares a
  copy at its height over the guest's ground put over the host's ground (net::diag::agl_dist: the distance along the
  ground and the difference of the two heights). The final check's n406 is 1.7 m apart that way (net_tests
  test_diag_npc_agl); a copy in the air, under the ground or off to the side is as far apart as before.
* ✔ (runtime, civ_wild_sync / civ_proxy_air / the campaign segment, labs 20261117-122701, -124343, -125242) The Civ
  game's pool (#civ-wild-sync, #civ-wild-reused-frozen) moves the host's wild creatures hundreds of metres at once
  (often to and from an unplaced point with z = 0, e.g. (0 524.8 0)), and that happens all the time: 120-160 copies
  are made in 70 s of civ_npc with the cameras panned twice, 57-132 copies a run "drawn 2-84 m over the guest's
  ground" (the guest's `npc: proxy N ... drawn X m over this game's ground ... (BUG-1216)` line, its creature on the
  host's ground). Two ways a guest showed that jump: (1) the proxy buffer blended across it (jump_speed 0): the target
  slid along the chord through the planet, 59-115 m under the ground and 80-205 m from the host's creature for seconds
  (the campaign's `n: 7 apart (n1030 205.2 ...)`, `want_agl=-96.42`); (2) the jump went into the proxy's path velocity
  (drive::path_update's v_ema, 1000+ m/s for 0.2 s), a standing creature's proxy is walked at its path's speed
  (BUG-0394), and walk_place took it the whole way in a few frames on the straight line from where it stood: over a
  valley 10-80 m in the air ("before this frame 0.00 m up", after the walk 57 m up). Inferred: the campaign's two
  walking copies 8.2 m over the ground (lab 20261116-085558, `tp=1`, made while the host's game refilled its herds in
  a pause) were such a flight's end, kept at that height by own_height.
* ✔ Fix `npc.civ_jump`: a Civ proxy's buffer does not blend across a step faster than 40 m/s (the partners' puppets'
  jump speed, BUG-0499), and a target that jumps more than 10 m in one frame puts the proxy there (onto this game's
  ground, a fresh path, no walk). civ_proxy_air: off 59 "drawn high" lines and a copy 2 samples in a row 58-62 m up
  (FAIL 20261117-125242), on none, 105 jumps (PASS 20261117-125257).
* ✔ (runtime, campaign segment lab 20261117-130736) The pool also moves a tracked herd of the same species while the
  host's game stands: during a 24 s pause of the host a herd of 7 was 392-398 m from where it had stood; host_tick
  sends nothing in frames without creature updates and the frozen pass (#civ-wild-parked, #civ-wild-reused-frozen)
  only retired objects at the centre or reused as another creature, so both guests kept it (states 20 s old,
  `n: 7 apart`) until the game went on. Fix `npc.frozen_moved`: in Civ the frozen pass retires a tracked object more
  than 20 m from where the game last updated it (no creature moves while none is updated). civ_wild_moved (the herd
  set 60 m aside during the host's pause menu): off FAIL 20261117-131957, on PASS 20261117-132012.
* ✔ (runtime, labs 20261117-121304, -121752, -122049, a lane 5 probe) Negative: a copy the game moved in one piece is
  grounded by the Civ game itself, like Tribe's (docs/re/tribe.md#tribe-animal-height): a herd the host's game set
  25 m aside onto ground 7.3 m lower, a walking creature shoved onto 3.2-4.6 m lower ground, and a walking copy put 8 m
  up on the guest (`coop.npctrack`-style probes) were at agl 0.00 within 50 ms, also with no NF_AIR from the host for
  30 s. So own_height alone does not hold a Civ copy in the air (a switch that put walked Civ copies at the sender's
  height over this game's ground had no failing twin and was dropped).
* ✔ (runtime) civ_npc's walking creatures are sent with NF_AIR often (`air_catches` 450-1900 per test): their game's
  grounded byte is 0 and they are more than kAirOver (0.15 m) off GetHeightAt at times.

## Walking city citizens on a guest: the lag and the glide (2026-11-17, fix/w51-trail)
<a id="civ-citizens-trail"></a>

BUG-1227. GOG 2024, runtime, lanes 4 and 6, civ_npc; measured with both games' own positions at the same moment
(`coop.bodyrec start kinds zvny`: z<host id> the host's citizens and the guest's bound copies, keyed by the host's id,
so each copy is compared with its own host citizen; tools/tests/coop_civ_citizen_trail.py, suite `civ_citizen_trail`).
* ✔ (runtime) The host's city citizens near the capital mostly stand; when they move they run at a steady 8.0 m/s
  (7.4 % of the host's samples move), in straight legs with sharp turns and stops (not a slow walk).
* ✔ (runtime) Before the fix (`civ.citizen_lead off`, labs 20261117-133529, -133940, -135246): a walking copy was p50
  4.7-5.2 m, p90 7.3-7.9 m behind its host citizen (lag p50 0.59-0.65 s, p90 0.92 s), stretches over 2 m of 16-35 s; in
  42-56 % of the moving samples the copy stood over 2 m off with another copy nearer the host citizen (the crossings of
  civ_citizens' size pairs). In the same sessions NPC proxies were p90 0.27-0.54 m off (lag p50 0.008-0.023 s) and Civ
  vehicles p90 0.08-0.13 m (lag p50 0.003-0.007 s): the citizens lagged 25-80 times more than the mod's other mirrored
  walkers, so not the relay's normal lag.
* ✔ Cause (observed): citizen_follow's MoveTo goal was right (1.5 s ahead, 10-12 m in front of the host's citizen), but
  the copy runs at the citizen's own top speed, the host's 8 m/s, and keeps the distance it lost at the start of each
  leg (a start is known a snapshot later and the velocity after two snapshots, 0.5-1 s, i.e. 4-8 m): the copy-to-goal
  distance stayed constant for seconds (`zf` 7.3 m behind for 5 s). The same class as the Tribe animal proxies
  (docs/re/tribe.md#tribe-animal-proxy-trail): the game's locomotion does not run a creature faster than its top
  speed, so a MoveTo alone never catches up.
* ✔ Fix `civ.citizen_lead` (worldobj.cpp `citizen_drive`, a `drive` of the citizen kind, every frame on the guest): while
  the host's citizen moves (0.3 m/s or more), the copy (still walked by MoveTo, so it plays its run animation) is
  placed without the teleport mark (game::place) a part of the way to where the host's citizen is now (its last
  place moved on with its velocity for the snapshot's age, at most 1 s): the error closes with a 0.12 s time constant,
  at most 4 m/s over the host's speed. The host sends the kind every 0.25 s instead of 0.5 s to guests of 0.7.15 on (an
  older guest takes a velocity only from snapshots over 0.3 s apart, so it still gets every 0.5 s: `Kind::fast_every`,
  the peer's HELLO families), and such a guest takes the velocity from snapshots over 0.15 s apart. Result (labs
  20261117-135241 and the ticket's PASS runs): p50 0.28-0.31 m, p90 1.38-1.70 m, lag p50 0.035-0.039 s, crossed
  samples 2.9-4.5 %. The glide alone at 0.5 s snapshots: p50 0.82, p90 3.43 m (-133935); 0.125 s snapshots gave p90
  1.68-1.86 m against 1.93-2.00 m at 0.25 s with the slower glide (0.25 s): the cadence is not the limit, 0.25 s kept.
* ✔ (runtime) What is left is the sharp turns at 8 m/s: samples within 0.6 s after a turn over 60 degrees p90 2.7-3.5 m,
  straight legs p90 1.1-1.6 m (the turn is known up to a snapshot late).
* ✔ (runtime) Animations (bodyrec rows a<id>, game::drawn_state): a host citizen that moves plays 0x02481DB0 (its run,
  every moving sample), a standing one 0x02481DE5 or an action (0x050A3F95, 0x04A1DD75, ...). A guest's copy of a
  running host citizen plays the run in only 8-28 % of the samples with `civ.citizen_lead off` and 26-98 % with it; the
  rest of the time it moves while it plays the standing 0x02481DE5 (key frames: the host's citizen in a run pose, the
  copy crouched). Older than the glide, BUG-1229 (fixed: a copy keeps the animation it was bound with,
  [civ-citizen-run](#civ-citizen-run)). game::gait_state's gait word is 0 for every citizen (both
  games, moving or not): not a walk indicator for them.
* Bandwidth: 19-56 citizens x 20 B every 0.25 s, about 1.6-4.6 KB/s per guest (was half).

## City citizens' run animation, and why a guest's copies slid (2026-11-17, fix/w51-citanim)
<a id="civ-citizen-run"></a>

BUG-1229. GOG 2024, lanes 4 and 6, civ_npc; civ_citizen_trail's a<id> rows (each citizen's current animation,
game::drawn_state, on both games at the same moment) and Ghidra.
* ✔ (static) A city citizen's run 0x02481DB0 is played by its behaviour tree: about 30 sites in 0xD90E30..0xD97E98 (node
  enters, ticks and exits of the citizens' node table, rows [id, enter, tick, exit] of 16 bytes at .data 0x15913xx-0x15916xx)
  and 0xD57F30, each `cCreatureBase::PlayAnimation(0x2481DB0, -1, 0)`. Several nodes play it at their exit
  (0x05DE40D5 exit 0xD90E30, 0x048F79DA exit 0xD91160, 0x04C0492C exit 0xD90FE0, overrides_dc04aee5.sym): between actions
  a citizen goes back to its run. Nothing in the locomotion (MoveTo, cLocomotiveObject) plays it, and the idle refresh
  (Creature_IdleRefresh 0xC148B0, only when no main animation is left) picks the plain idle 0x02481DE5 for a citizen
  (Creature_ChooseIdleAnim needs a walk gait; a citizen's gait word is always 0, docs/re/civ.md#civ-citizens-trail).
* ✔ (runtime) On the host every moving citizen sample (over 2 m/s) plays 0x02481DB0 (98-100 %); a standing one the idle
  0x02481DE5 or an action (0x04A1DD75, 0x04A3442D, 0x050B5AB8, 0x050B3FA5, 0x050A3F95, 0x049C8AE2, ...), the run in
  5-13 % of the standing samples (stops).
* ✔ (runtime, observed) On a guest a bound copy keeps one animation for the whole 60 s recording while the mod walks it
  (per copy, 4 recordings of 15 copies: every copy either 100 % 0x02481DB0 or 100 % 0x02481DE5, moving or not; the
  copies that never moved stood in the idle): its own behaviour plays nothing on it there. So a copy bound in the idle
  slid in its standing pose whenever it ran, a copy bound in the run ran in place while it stood (guest 37-59 % of the
  standing samples in the run, host 5-13 %). The share of copies bound in the run varies by session: the copies of
  running host citizens played the run in 8-98 % of the samples (BUG-1227's glide is not the cause: off and on alike).
  The cause is neither the MoveTo goal (10-12 m ahead) nor the per-frame place (the same without civ.citizen_lead) nor
  the speed (the copies run at the host's 8 m/s).
* ? Why the guest's own behaviour of a bound citizen plays no animation for a minute (its tree held by the mod's MoveTo,
  or the guest's game not running its citizens' behaviour near the host's capital) is not known; the copies of standing
  host citizens never gesture either (BUG-1231).
* ✔ Fix `civ.citizen_run` (worldobj.cpp `citizen_run`, from the citizen kind's per-frame `drive` on a guest): while the
  host's citizen or the copy itself moves (1.5 m/s on, under 0.3 m/s off; the copy's own speed from its places, smoothed
  over 0.15 s), the copy plays the run (`game::play_animation(c, 0x02481DB0, -1, false)`, as the nodes do) when another
  animation is on it; a standing copy in the run gets the game's idle refresh (forced), at most every 0.2 s each. About
  200 plays and 170 idle refreshes a minute for 15 copies; what the run replaced: 0x02481DE5 almost always, a few
  0x04866DAB / 0x0490C8A6 / 0x0490C8B4. Moving copies in the run 97-98 % (host 98-100 %), standing ones 13-14 % (host
  7-10 %). Off: the copy's animation is left alone (0.7.14).
* ✔ (runtime) Found on the way: when the host's citizen stops, its copy runs on 8-10 m and comes back (over 3 m off in
  5-24 % of the standing samples, max 10-12 m, with and without civ.citizen_lead): BUG-1230 (open; inferred: the walk goal
  12 m ahead is not replaced when the copy is within 1.5 m of the stop point).
## A standing copy on steep ground: slid by its game, held by the mod (2026-11-17, fix/w51-slope)
<a id="civ-npc-slope"></a>

BUG-1228 (the final check's copies on a guest's steep ground, #civ-npc-ground). GOG 2024, lanes 5 / 9, civ_npc;
test civ_npc_slope (docs/06-testing.md#testing-npc-slope).
* ✔ (runtime, labs 20261117-142058, -142930) A guest's NPC copy (a puppet: no AI) put onto ground of 56-68 degrees
  (the rise over 3 m to each side) is slid down by its game: its velocity reads 19-28 m/s while its remote stands
  (rv 0), the model (AnimatedCreature +0x04) 0.2-0.5 m under the terrain, the grounded byte 0. The mod's driving of a
  standing copy then fought it: over 0.8 m off it walked it (MoveTo) and nudged it every frame, the trail rule
  (BUG-0305) teleported it back after 0.5 s and the nudge put it onto the target every frame for 1 s (kTrailQuiet,
  k = 1): drawn 0.4-1.1 m off in every other frame, then a 2-3.5 m slide, then the next trail teleport, every
  ~1.5 s, for as long as the remote stood (7 trail teleports and 45-53 m of movement in 12 s). The final check's
  copies (own=19-22, trail every ~2.5 s, nudges in 97 % of the frames) were the same cycle.
* ✔ The same copy on 39-53 degrees stays where it is put, or slides 0.4-0.8 m once and rests (labs 20261117-140524,
  -140718, -141408): below the walk's 0.8 m nothing drives it again.
* ✔ The host's game holding its own AI creature on such a spot (put there every frame) reads 5.5 m/s most of the time
  (its AI walks it back to its herd) and 25-26 m/s only at times: not a usable sign of the slope. A host creature put
  on 70 degrees left the host's object list within seconds (`no such host npc`, twice).
* Inferred (BUG-1228's campaign): there the copies' target lay on the guest's ground, 10 m higher than the host's
  (#civ-npc-ground), on a slope steep enough for this slide; the host's creature stood on its own, flatter, ground.
  Why the two terrains differ there is still not known.
* ✔ Fix `npc.slope_hold` (npc.cpp, every NPC proxy driven by drive::step, every stage): a standing copy (its remote
  under 0.3 m/s) that its game moved more than 0.1 m between two frames (kSlideM: where the last frame's driving left
  it vs where the game drew it) is held from then on while its remote stands: game::place_hold with `still` (the
  game's next creature update and physics update may move it towards its target, drive::State::step_to, not away;
  put back with game::place, no teleport mark, velocity cleared) and its velocity cleared each frame. The hold ends
  (game::place_release) when the remote walks, the copy is placed, put elsewhere (npc.civ_jump) or dies.
  `coop.npc status`: `slope_slides=` (copies found moved so, counted with the switch off too), `still_kept=` (updates
  undone). civ_npc_slope off: FAIL 20261117-142930, -150616 (63 / 68 degrees: 7 trail teleports each); on: PASS
  20261117-150636, -150855 (the same spots: 0 teleports, at most 0.1 m).
* Creature / Tribe (the same NPC proxy driving, so the same fix): the test's state could not be made there. Creature
  (lab 20261117-145748, `SLOPE_TRACK=1`): a host animal held on a 57 degree spot 8 m from both avatars targeted the
  partner (`on_partner`) and both games held it thrown (loco mode +0x2B0 = 3) 10-13 m over the spot: the guest's copy
  was snapped onto the target every frame (800 teleports in 12 s) and drawn back up there: not the slide, and with the
  switch on the same (labs 20261117-144954 / -145014). Held 30 m from the avatars it left the host's list
  (20261117-145900). Tribe: no bare ground of 55-68 degrees within 100 m of the fixture's village; animals held 150 m and
  more away left the host's list within 3 s (20261117-145014, -145553). Not seen in Creature / Tribe play either; the
  Creature slope tests (npc_walk_slope) run with the fix.
* ✔ (runtime) The detection also finds copies in normal play: 8-17 standing copies a run of civ_npc_slope outside the
  measured one (herd mates the game moved over 0.1 m in a frame); a hold only stops moves away from the target.
  ~ superseded (2026-11-17): see [civ-citizen-hold](#civ-citizen-hold) (the guest's own walks of a standing copy, not the stop).

## A guest's copies of standing city citizens: their own walks (2026-11-17, fix/w51-overrun)
<a id="civ-citizen-hold"></a>

BUG-1230. GOG 2024, lanes 4 and 6, civ_npc; civ_citizen_trail's rows (z<id> both games' citizens, y<id> the guest's
walk goals, a<id> animations), 6 recordings of fix/w51-trail and the labs below.
* CORRECTION (2026-11-17, fix/w51-overrun): the inferred cause in [civ-citizen-run](#civ-citizen-run) ("the walk goal
  12 m ahead is not replaced when the copy is within 1.5 m of the stop point") is not what happens. The far samples are
  not at the stops: of a copy's standing samples over 3 m off, most come 3 s to more than 10 s after its host citizen
  stopped (z3 of 20261117-141249: 12 within 1 s of the stop, 1214 later), and the walk goal y<id> is the host
  citizen's place all the time (0.0 m from it).
* ✔ (runtime, observed) While the host's citizen stands, the guest's own behaviour still walks its bound copy: every
  2-3 s the copy, standing within 1 m, runs off at the citizens' 8 m/s, and citizen_follow (every 0.25 s and at each
  snapshot) walks it back only when it is over kCitizenWalk (1.5 m) off and its last MoveTo to the same point is 1.5 s
  old: 8-12 m out and back. Standing copies over 3 m off in 7-22 % of the standing samples, p90 2.3-6.4 m.
* ✔ (runtime) StopMovement (cLocomotiveObject vt 0xEC) on a copy that starts such a walk, or a MoveTo back at once,
  does not end it: the behaviour walks it again within a second (stop: 2-3 m out and back every 1-2 s, 1100-1600
  StopMovements a minute, labs 20261117-152554, -153309) and plays the run again each time (the copies of standing host
  citizens in the run 23-36 % of the samples against the host's 2-12 %).
* ✔ (runtime) Placed every frame (no teleport mark) toward the host's citizen, a copy whose behaviour still walks it
  rests about 0.5 m off it (its walk against the place); its behaviour then plays nothing new, so one idle refresh
  ends its run. Some copies' walks end by themselves (6 000 against 24 000 places a minute in two sessions).
* ✔ Fix `civ.citizen_hold` (worldobj.cpp `citizen_hold`, from the citizen kind's per-frame `drive` on a guest): while
  the host's citizen stands (under 0.3 m/s), the copy is placed a part of the way to it (at the copy's own distance
  from the planet's centre, time constant 0.06 s, at most 10 m/s; nothing within 0.15 m); one over 1.5 m off that moves
  away gets a MoveTo back at once (at most every 0.3 s); within 1.5 m citizen_run takes it as standing (its own speed
  from its walk against the place kept it in the run). Result: standing samples p90 0.54-0.55 m (off 3.41-4.50 m), max
  2.4-2.8 m (off 11 m), standing copies in the run 0-1 % (host 4-5 %). Off: only citizen_follow's walk.
* ? What in the citizens' behaviour tree keeps walking a bound copy (and whether it can be told to stand) is not traced.

## The species of a city's citizens and per-object designs (2026-11-18, plan/0.7.16)
<a id="civ-own-species"></a>

Research for docs/plan-0.7.16.md item 2 (a guest's cities and vehicles of its own species and designs). GOG 2024,
Ghidra (`tools/ghidra/q.sh`), no runtime probe of these paths yet.

* ✔ (Ghidra) **`City_Species` 0xBD9F90**: thiscall(cCity*) -> cSpeciesProfile*, 11 bytes: `mov ecx,[ecx+0x590]` (the
  city's civilization) then `jmp 0xBEFAA0` = `Civ_Species`: `GetSpeciesProfile(&civ +0x268)`. So a city's citizens are of
  its **civilization's species key cCivilization +0x268** (12 bytes); null falls back to `cSpeciesManager_PlayerSpecies`
  in the callers. Its five callers are all citizen code: the citizen init vt 0xE8 0xC27330 (a citizen of a city),
  0xD5BF50, 0xD5C6D0, 0xD5CCA0 and the city citizen manager's update 0xD5FB10. One hook of 0xBD9F90 answers per city.
* ✔ (Ghidra) `CityCitizens_Sync` 0xD5BF50 (thiscall(mgr, bool) ret 4; mgr +0xAF0 the city it runs, +0xA64 vector of its
  citizens, the manager of the city near the camera, docs/re/civ.md#civ-roads-citizens): takes the city's species
  (City_Species, else PlayerSpecies), makes or drops pool citizens (0xAD1130 noun 0x18EB4B7) until the count matches,
  **and sets every citizen whose species (+0xB20, getter 0xC0BD30) differs to the city's species**
  (SetSpeciesProfile 0xC22450). It runs from the update 0xD5FB10 when the count differs, from 0xAC8C10 (the manager of
  that city) and 0xD60EB0. So a citizen's species can not be set one by one: the next sync puts it back; the city's
  answer is the lever. All citizens of one city are one species.
* ✔ (Ghidra) `Civ_SetSpeciesKey` 0xBEBF20: thiscall(civ, const ResourceKey*) ret 4, writes +0x268..+0x270 only.
  `cSpeciesManager_SetPlayerSpecies` 0x4DF380: thiscall(mgr, const ResourceKey*) ret 4, writes mgr +0xA4..+0xAC (the
  world's player species), tells the OTDB and posts a message. The Civ editor return 0xCFBD70 (a creature result, type
  0x2B978C46: the Civ outfitter) calls SetPlayerSpecies, Civ_SetSpeciesKey on the civilizations of its loop (~ the
  condition inside the loop not read), 0xAC8C10 for their cities (the citizens re-synced) and puts the new profile
  into every cTribe's species slot (0xC8E9C0(profile, 0), docs/re/tribe.md#tribe-two-species).
* ✔ (Ghidra) The Tribe -> Civ end (`TribeGame_AdvanceProc` 0xCDBEA0, the advance button with the bar full): it records the player
  tribe's species slot key (`cTribe_SpeciesKey` 0xC8ECE0 = slot +0x504) in the stage history (0xE39890 with the
  consequence trait, property 0x8FD1273C), gives it to the window +0x1104 (0x555F70) and posts 0x73E46F6 with it,
  then TRG2CVG_PreEditor. The Civ stage's species is the tribe's slot: **the game carries no member's own species
  into Civ**; a guest's line has to be carried by the mod.
* ✔ (Ghidra) `Civ_SetDesign` 0xBF4C50 (#civ-culture-sync): for a vehicle design id it calls only the model object's
  vt 0x94 SetModelKey on each vehicle of the civilization (+0xB0) whose locomotion +0xB1C and purpose +0xB20 match; for a
  building id 0xBE3870 per city. The vehicle's hit points that change with the design (362 -> 314 in BUG-1157's run)
  therefore follow SetModelKey alone: **a per-vehicle SetModelKey gives that vehicle the design's model and stats**
  (~ inferred from that run; the per-vehicle stats were not read field by field).
* Consequences for an own design per owner (the culture set is one per civilization, #civ-culture-sync, BUG-1158):
  * citizens: hook City_Species: a city owned by a guest answers the guest's species profile; every game knows the
    city owners (civ.cpp owner labels), each game's citizens are its own pool, so nothing is sent per citizen and
    nothing is saved (the pool is rebuilt; +0x268 is never written).
  * vehicles and buildings: SetModelKey per object (the path of civ.host_buildings / civ::redesign_vehicles), with the
    object's culture key written back into +0xC4 while a save is written (the savestrip pattern of
    civ.host_buildings, #civ-host-buildings). `Civ_SetDesign` on the host's civilization re-models every vehicle of
    a locomotion and purpose: a guest's vehicles must be re-keyed after it (hook its end).
  * ? Not checked: whether a vehicle's or building's SetModelKey with a key the game does not know on load faults or
    leaves an empty model (the reason a save must never keep a key of ours); whether captured cities (cCity::ChangeOwner
    0xBE8A20 reads Civ_Species) need more.

## A player's own vehicle and building designs (2026-11-18, feat/w54-designs)
<a id="civ-own-designs"></a>

WP-C2 of docs/plan-0.7.16.md (BUG-1274), built on #civ-own-species. GOG 2024. Code: mods/coop/civ_own_designs.cpp,
creations.cpp (`share_design`, MSG_CREATION kind 9). Runtime not yet run (the night lab: civ_own_designs and its twin).
* Design: every game re-keys its own objects, nothing per object is sent. A vehicle of the player civilization (its
  list +0xB0) whose owner (host: the vehicle table; guest: the snapshot's owner of the mirror) has an own design for
  its design id (`VehicleDesignId(purpose +0xB20, locomotion +0xB1C)`), and a building of a city whose owner has one for
  the building's design id (noun -> id: house 0x18EB106 -> 0x4E3F7777, factory 0x18EA2CC -> 0x47C10953, entertainment
  0x1A56ABA -> 0x72C49181, City Hall 0x18EA1EB -> 0x99E92F05, as cCity::SetBuildingModel), gets that design's key
  by SetModelKey (vt 0x94 of +0x34; the key at +0xC4 for both). Checked four times a second, at once after a guest's
  purchase (CMD_BUY) and at the end of every `Civ_SetDesign` 0xBF4C50 (hooked: the setter re-modelled the kind with
  the culture's key; our keys go back on in the same call, so no frame shows the culture's model on the host).
* The culture key of each re-keyed object is kept (what the game gave it; on a guest a building's BUG-0670 key of ours
  is replaced by that entry's own culture key): written raw into +0xC4 while a save is written (savestrip hooks, as
  #civ-host-buildings), SetModelKey with it when the owner has no design any more (a gift to the host, creature.mode
  host, the switch off) and at the session's end. An object whose cGameData serial changed is another object (the
  record goes).
* The design's records: the guest reads its creation from its own My Creations file (not the game's lookup: the host's
  world packages answer the same id with the host's creation at priority 2000) and sends them to the host (MSG_CREATION
  kind 9, owner = the player, id = the design id); the host keeps them for later joiners and sends them to every
  guest, the sender included, so a design applies only when the host has it. Every game registers them under
  0x7D000000 | fnv(content) (the same key everywhere, the BUG-0647 package path) after `ensure_model`. Older versions
  drop kind 9 (creations family minor 1.1): a 0.7.15.2 player sees the culture's design.
* The guest's mirrors: `bound_design` (BUG-0647) and `host_buildings_tick` (BUG-0670) leave an object alone while it
  shows its owner's design (`own_designs_holds`).
* ? Not checked at runtime (night lab): that the vehicle's hit points follow SetModelKey exactly as a vehicle made with
  that key (the test compares with a reference vehicle spawned with the key, `coop.civ owndesign hpref`); that the raw
  culture key in +0xC4 is what the vehicle record of a save holds (the test parses the save); captured cities.
* Tests: `coop.civ owndesign` (lists `ownveh` / `ownbld` rows with the model and culture keys, the player designs),
  `owndesign set <hex design id> <hex creation>` (the WP-0 shim setter on a guest), `owndesign did <city> <loco>
  [purpose]`, `owndesign hpref <purpose> <loco> <hex key>` / `owndesign hpref` (host: a reference vehicle and its hit
  points, then removed). Test tools/tests/coop_civ_own_designs.py.
## The citizens of a city of its owner's species: runtime (2026-11-18, feat/w54-citizens)
<a id="civ-own-citizens"></a>

WP-C1 of docs/plan-0.7.16.md (BUG-1273), on top of [civ-own-species](#civ-own-species). GOG 2024.

* ✔ (runtime, lab 20261118-180539 lane 6, civ_npc) **a hook of `City_Species` 0xBD9F90 changes the citizens' species
  on both games**: after the guest's capture of the nearest NPC city, the host's and the guest's game each held 19
  citizens of that city, 15 of them of the guest's creature key (+0xB28 = 7CBC3253, the creation's shared key) and 4
  of the civilization's (067C7743); the capital kept 19 of the civilization's. The hook was called ~2600 times in 90 s
  per game (every citizen update asks it), the answers per game cost one map lookup. No fault on either game.
* ✔ (runtime) cCreatureCitizen **+0x1010 is the cCity pointer** of a city citizen (the census of `coop.civ citizens`
  matched every one of 19 citizens to its city by it).
* ✔ (runtime + Ghidra) **the citizen manager's four special citizens** are not re-synced: `0xD5C6D0` (from the city
  switch 0xD60EB0, once per visit: flag mgr +0xAF5) makes one citizen into each of four vectors at mgr +0xA78 + i*0x14
  (begin, end, capacity, ...) through `0xD577B0` (Citizen_Init vt 0xE8 with the city: City_Species at that moment).
  `CityCitizens_Sync` walks only +0xA64, so after an owner change these 4 kept the old species (the 4 of 15 + 4 above).
  The mod sets them with `CreatureBase_SetSpeciesProfile` 0xC22450 (thiscall(creature, profile) ret 4: +0xB20, +0xB24
  = profile +0x53C, +0xB28..+0xB30 = profile +0x504).
* ✔ (runtime) the manager: `PlanetEco_HerdSpawner()` (= [0x167EB3C]) +0xA4 is the city citizen manager; its +0xAF0 the
  city near the camera. 0xAC8C10 thiscall(spawner, city, bool) ret 8 runs `CityCitizens_Sync(mgr, bool)` only when
  that city is the manager's; ChangeOwner calls it with 1, ConvertCitizens with 0. The bool (Ghidra): 1 also re-sets
  every pool citizen's political id (vt 0x48) and its behaviour (vt 0xE4, 0xC0DAB0); the species loop runs either way.
  The mod calls the sync with 0.
* ✔ (runtime) **a Civ save writes the city citizens near the camera with their species key**: the host's save
  (`coop.save` with the camera at the guest's city, hook on, no strip) had the guest's key 7CBC3253 in its citizen
  records (record type 0x018EB4B7). So the key is put back while a save is written (savestrip save hooks, below).
* ~ (Ghidra) `0xD5CCA0` (from Citizen_OffStageCheck 0xACA2A0) compares a citizen's species with City_Species: equal
  -> back into the city's list; else it is handled as a foreign visitor (+0xACC list, relationship checks). A stable
  hook answer keeps a guest's city's own citizens "own".
* The NPC cities of civ_npc (made by new_stage_game.py) have the player civilization's species (067C7743) too.
* Design (civ_species.cpp, `coop.fix civ.own_species`, the save part `civ.own_species_save`): City_Species answers the owner's species when the city's
  owner (g_city_owner, known on every game) is a guest with an own line (until WP-0: its own creature,
  `creations::player_key(pid, kCreature)`, only when it differs from the host's); the host's cities, the shared capital
  (its founder's: the host's), NPC cities answer the game's own. `creature.mode` host: no answer. Every 0.5 s, outside
  the frame, the manager's city is checked: a pool citizen or a special citizen of another species than the city's
  answer -> `CityCitizens_Sync(mgr, 0)` once (at most every 3 s per city) and SetSpeciesProfile for the special ones.
  Nothing is sent and nothing is written into the civilization. While a save is written every citizen with a guest
  line's key gets the player civilization's key in +0xB28..+0xB30 (no rebuild) and its own back at the end.
* CORRECTION (2026-11-18, feat/w54-shims): #civ-own-designs' "MSG_CREATION kind 9, registered under 0x7D......" is
  ~ superseded: the designs go as line creations of the species line (species::set_design, MSG_SPECIES_LINE kBundle,
  registered under 0x7B...... on every game, docs/01-architecture.md#arch-species-line-packages); kind 9 was removed
  before any release. The re-keying, the save strip and the hooks are unchanged.
## A guest's own Civ editors: where the game writes the civilization (2026-11-18, feat/w54-civedit)
<a id="civ-own-editors"></a>

WP-C3 of docs/plan-0.7.16.md (BUG-1275), on top of [civ-own-designs](#civ-own-designs) and the species line
(docs/01-architecture.md#arch-species-line). GOG 2024, Ghidra only (no lab run yet: the night lab runs
`civ_guest_editors` and its twin). Code: mods/coop/civ_editors.cpp, designs.cpp (refusal, own pick).

* ✔ (Ghidra) **The design editors' result does not pass CivGame_EditorReturn 0xCFBD70**: the editor's exit
  (`Editors::cEditor::OnExit`, 0x587C51) posts 0x30C11C7 with a 0x48-byte result: +0xC the request's tag (+0x90),
  +0x10 the request's +0x94, **+0x14 the editor model's +0x58 (the model type = the culture's design id: house
  4E3F7777, a vehicle's VehicleDesignId ...)**, +0x18..+0x20 the key (the request's +0x10 after the save), +0x44
  cancelled (1 from OnExit, 0 after a save). The planner's listener `CommunityEditor_HandleMessage` 0xD130D0 copies them
  into planner +0x4C (design id), +0x50..+0x58 (key), +0x5C (cancelled); the Civ re-entry re-opens the planner
  (0xCFBD70 state 1 -> 0xCF20B0 -> message 0xB2699148 -> 0xCF56A0 -> `CommunityEditor_Open` 0xD0E170), which calls
  **`Planner_ApplyDesign` 0xD095A0(+0x4C, &+0x50)** when +0x4C is set, +0x204 set and +0x50 non-zero (not for the
  creature model types 0x372E2C04, 0xCCC35C46, 0x65672ADE, 0x4178B8E8). 0xD095A0 is the one writer: `Civ_SetDesign`
  on the planner city's civilization (city +0x590) and, for the City Hall, cPlayer +0x1150..+0x1158 (the City Hall the
  Civ start reads, 0xCFBD70 state 0). Then (not cancelled) a stage-history record per kind (0x7B27DBD1 vehicles,
  0xEF6D08A3 buildings, with the civilization's species +0x504) and the palette's tiles refreshed from the culture
  set (0xD0C860 for 16 design ids; it generates no entry itself, GetDesignKey 0xBF98B0 may).
* ✔ (Ghidra) **The Sporepedia pick** has two paths: the shopper's accept 0xD0CC00 (the IShopperListener at planner
  +4, vtable 0x1479568: design id planner +0x60, set by `Planner_ShowShopper`) -> 0xD0C860 + 0xD095A0; and planner
  message 0x5132389 (a vector of keys, each `cEditorResource` +0x18 its design id) -> `Civ_SetDesign` directly +
  0xD0C860.
* ✔ (Ghidra) **The palette's buttons** (`Palettes` item window proc 0x5F1750, command 0x287259F6 with the control's
  command id): 0x4A1CB6D the tile -> message 0x133B269E (the shopper); 0x4A1CB6B / 0x4A1CB71 "new design" ->
  0x4A314E6 {+0 the item's design id (+0x30), +8 0}; 0x4A1CB6C "edit" -> 0x4A314E6 {.., +8 1}; 0x6443438 the drag
  -> 0xB2E18705. 0xD130D0 on 0x4A314E6 (only with a city planner open: +0x40, cast to a city): +0x4C = id, a new
  EditorRequest: +0xC `GetEditorForAssetType(id)`, +0x10 the current design's key when +8 == 1 (0xD09640), +0x1C the
  game mode, +0x90 tag **0x91D4AF08**, flags +0x36..+0x3D, +0x64, +0x6D, +0x70 the cPlayer's five stage traits, then
  `cGameModeManager::SubmitEditorRequest` 0xB1DE20 (keeps it at +0x2C, pauses 0x4BF38A6 and submits from 0xB1DF60, or
  `EditorRequest::Submit` at once). The data object is only read (the handler's later `intrusive_ptr` is a new one).
* ✔ (Ghidra) **The Civ planner's tabs**: the Civ UI's commands post 0xB2699148 {+8 kind}: CommunityEditorActivate
  (0x37EA1BD) kind 0, CityBuyVehicle (0x37E8514) kind 1, AccessoryEditorActivate (0x4D2B5FD) kind 4; 0xCF56A0 opens the
  planner on the city (0xD0E170) and `0xD096D0(kind)` (the tab; -1: the planner's +0x2F4). So vehicle designs are a
  planner tab (the planner's 0xD0C860 list has 12 vehicle ids: 7D433FAD, 441CD3E6, 8F963DCB, 9AD7D4AA, 449C040F,
  1F2A25B6, BC1041E6, 2090A11B, C15695DA, F670AA43, 1A4E0708, 2A5147A9) and the Civ outfitter is tab 4. ? The tabs'
  screen layout is not known (the test opens the vehicle editor through the planner's own message).
* ✔ (Ghidra) **The Civ outfitter**: a species card's edit (`Palettes::OutfitterItemUI`, 0x5EFD30) posts 0x53850BAE;
  0xD130D0 builds the request (editor data +0xC, key data +0x10..+0x18, in Civ tag **0x66787BB**, Tribe 0x116DD1B,
  Space 0x66787BC). Its result reaches `GameCiv_HandleMessage` 0xCFD500: tag 0x66787BB -> 0x572620 keeps the result
  (null when cancelled) in the game mode (+0x100); `CivGame_EditorReturn` 0xCFBD70 is the Civ mode's state machine
  (+0xB4; +0xB8 the mode it came from): state 1 from the editor mode with +0x28 == 0 re-opens the planner and, for a
  creature result (+0x1C type 0x2B978C46), calls `cSpeciesManager_SetPlayerSpecies`, `Civ_SetSpeciesKey` on every
  civilization (no condition: the loop calls it for each) with 0xAC8C10 for each of their cities, and
  `cTribe_SetSpeciesSlot(profile, 0)` on every tribe (cGameNounManager data 0x18C6D19). State 2 with +0x28 == 1 is the
  spaceship editor (0xCF4620: to Space).
* Design (`coop.fix civ.guest_editors`, own mode: creature.mode own, `species.line` on, a guest in the host's world):
  BUG-1158's Civ refusal (designs::refuse_own_editor) only when it is off or in host mode; 0xD095A0 is skipped for the
  guest (no culture entry, no cPlayer City Hall) and a changed key goes to `species::set_design(design id, key)`; a
  0x5132389 pick's changed entries go back (Civ_SetDesign with the old key, a design-less entry emptied again) and to
  the line; the outfitter's result goes to `species::set_civ_outfit` and is handed to cGameModeCiv as cancelled (+0x44
  = 1 for the call: nothing kept, so none of the outfitter writes of 0xCFBD70 runs); the guest's requests start from
  its line (SubmitEditorRequest: the outfitter from `species::own_key(Civ)`, an edit from `own_design(id)`), and its
  palette tiles show its own designs (0xD0C860 with our key). A guest's own culture entry that changed by another
  path is taken into the line by the wsync row's undo (designs::civ_sync) instead of the "went back" line. The line
  applies from `civedit::tick` (the creation packed from the game's records, retried for 15 s). The host and host mode
  run the game's own code.
* ? Not checked at runtime: the result layout above (+0x14 the design id) on a real editor's exit; that the tiles'
  "new design" buttons are visible windows `coop.ui cmd 4A1CB6B` finds; the Civ outfitter's route on the screen and
  its editor id; whether the guest's palette shows its own design's picture from our key with no png key.
## A guest's own City Hall and vehicle editors at its Civ arrival (2026-11-18, feat/w54-x1)
<a id="civ-own-arrival"></a>

WP-X1 of docs/plan-0.7.16.md (BUG-1276), on top of [civ-own-editors](#civ-own-editors). GOG 2024, Ghidra only (no lab
run yet: the night lab runs `civ_own_arrival` and its twin). Code: mods/coop/civ_arrival.cpp, civ_editors.cpp
(`take_editor`, the result's call in h_CivMsg), session_glue.cpp (the vote's end).

* ✔ (Ghidra, asm 0xCD2308..0xCD233B) **The end of Tribe's vehicle editor** (the Tribe listener 0xCD1D60 on the end of
  TRG2CVG_ShowCityHall) is chosen by cPlayer +0x1104 (the consequence archetype): 0xC2CA9495 -> **F670AA43**,
  0x60A78928 -> **9AD7D4AA**, anything else -> 9AD7D4AA + E26B6B03 = **7D433FAD**.
  CORRECTION (2026-11-18, feat/w54-x1): docs/re/tribe.md#tribe-civ-transition's "7CB21BAD" is wrong (no dword 7CB21BAD
  exists in the exe, `scan`); it is 7D433FAD. `Editors::cEditor::GetEditorForAssetType` maps each of these ids and
  the City Hall 0x99E92F05 to itself: the editor id is the asset type (the culture's design id).
* ✔ (Ghidra) Both Tribe requests: EditorRequest (new 0x9C + `EditorRequest_ctor`) +0xC the editor, +0x10 empty (a new
  design), +0x90 tag **0x116DD1B**, +0x34 0, +0x36 1, +0x37 1, +0x38 1, +0x39 0, +0x3B 0 (no red cross), +0x3C 1, +0x3D 1,
  +0x64 1; then `Editors::EditorRequest::Submit` directly (the listener paused 0x4BF38A6 and raised the input lock first,
  its own return lifts them) and the byte 0x1686AF0 = 1.
* ✔ (Ghidra) `EditorRequest_ctor` 0x5A9140 leaves the reference count (+8) at **0**, +0xC -1, +0x1C the current game
  mode (IGameModeManager vt 0x38), +0x39 1, +0x3B 1, +0x3D 1, +0x6D 1, +0x6E 1, the rest 0. The game holds a request
  with an intrusive pointer (0x61DF80: store, AddRef vt+4), submits, then Releases (vt+8). (vote.cpp's
  build_editor_request says "one reference held": it is 0; open_own_editor never Releases, so nothing is freed early.)
* ✔ (Ghidra) The Civ planner's own design request (0xD130D0 on 0x4A314E6): +0xC GetEditorForAssetType(id), +0x10 the
  current design's key for an edit, +0x1C the game mode, +0x90 0x91D4AF08, +0x36 / +0x37 / +0x38 / +0x3C / +0x3D 1,
  +0x39 0, +0x3B the constructor's 1 (the cross), +0x24..+0x30 four dwords of 0x15DA7C4, +0x64 1, +0x6D 1, +0x70 the
  five traits cPlayer +0x10FC..+0x110C; `cGameModeManager::SubmitEditorRequest`.
* ✔ (Ghidra) **The way back into Civ from any editor** (`CivGame_EditorReturn` 0xCFBD70, the mode it came from
  0xDBDBA1): state 0 skips the stage-start parts for the editor mode; state 1 with cGameModeCiv +0x28 == 0 calls
  0xCF20B0(+0xF8) and sets +0xF8 = -1; 0xCF20B0 posts 0xB2699148 (the planner opens on the picked city +0xD4, tab
  +0xF8) only when +0x80 != 0 and +0xF8 != -1. **+0xF8 is -1 from the constructor** (GameCiv_ctor +0xCE) and after
  each return, so an editor that did not come from the planner re-opens nothing. Then only an outfitter result
  (+0x100, type 0x2B978C46) is applied. ? Where +0xF8 is set to a tab was not found (no `mov [reg+0xF8]` in the
  cGameModeCiv code but the ctor's, 0xCF7F20's and 0xCFBD70's -1).
* ✔ (Ghidra) `GameCiv_HandleMessage` 0xCFD500 on 0x30C11C7 acts on the tags **0x66787BB** (the Civ outfitter) and
  **0x116D51D** (the spaceship editor, model type 0x98E03C0D) only: a result with the Tribe tag 0x116DD1B in Civ is
  ignored by the game. The planner's listener 0xD130D0 copies **every** 0x30C11C7 into planner +0x4C (model type),
  +0x50..+0x58 (key), +0x5C (cancelled) whatever the tag; its next open (0xD0E170) applies it through 0xD095A0 when +0x4C
  != -1, +0x204 != 0 and +0x50 != 0, then sets +0x4C = -1.
* Design (`coop.fix core.own_civ_arrival`; own mode, the species line, `civ.guest_editors`, a host with creations 1.1):
  a guest whose game sees the host's vote for one of the end-of-Tribe editors end while it plays the host's Tribe keeps
  its own Civ editors due; in the host's Civ world (`designs::civ_player`, not elsewhere) after 20 s without a
  cinematic, pause, load, vote or pause menu (240 s at most, pauses not counted; 900 s from the vote to the arrival)
  the City Hall editor opens on its game (outside the frame, the request built as the Tribe's with +0x3B = 1, Submit
  under vote.cpp's bypass: no vote, no BUG-1158 refusal), then 5 s after its return the vehicle editor of the
  archetype. The result: from 0x30C11C7 with tag 0x116DD1B in cGameModeCiv's listener (civ_editors.cpp's hook), else
  the planner's copy; it goes to `civedit::take_editor(the editor id, key)` -> `species::set_design`; the planner's
  copy is emptied (+0x4C = -1) so a later planner open never applies the guest's design to the civilization. A
  cancelled City Hall editor (the red cross) skips the vehicle one. Nothing is sent by this file and the host's game
  runs unchanged: it plays Civ while the guest designs.
* ✔ (Ghidra) `SporeCoop::Planner_Instance` 0x169D580 is set by the planner's open 0xD0E170 and set to **0** by its
  close 0xD100B0 (0xD106EE `mov [0x169D580], ebp` with ebp 0): it names a planner only while one is open. So the
  planner's copy of a result is read only when the guest's planner is open at the editor's return (not after the
  arrival editors); a copy left in a closed planner's +0x4C would be applied at its next open, which WP-C3's h_Apply
  takes into the guest's line in own mode instead of the civilization (the third way the result reaches the line).
* ? Not checked at runtime: that the building / vehicle editor opened by a request in the Civ mode (not the Tribe's)
  draws and saves as in Tribe; that cGameModeCiv's listener sees the result while the editor closes (else the
  planner's copy is used, if the planner's listener is registered then); that the result's +0x14 is the editor id
  (the log prints it; the code uses the editor id).

## Fork notes: founder nations, species keys and control entry points (2026-11-19, docs/lineages-re)
<a id="civ-lineages-founders-control"></a>

Source: the Lineages fork by PandizeLive (PR #196); not yet verified on our lab.
GOG 2024 / Steam 3010a413 code (the fork read the Steam code from a running game, ASLR undone; the Steam exe is
packed on disk, so on-disk disassembly is no evidence). Fork tickets: fork BUG-9046, 9053, 9054, 9135, 9155, 9170.

* ~ (runtime, fork) Spore forms a second nation with its own treasury and **empty** country and city species keys
  (cCivilization +0x268, cCity +0x658 mSpeciesKey). Writing both keys reads back at once; after a save the nation
  keeps its key and the **city clears its own** (+0x658). Citizens are made with the **nation's** species even while
  the city key is empty (19 citizens of each city with the expected species on host and guest after a cold start;
  our [civ-own-species](#civ-own-species) section reads `City_Species` -> civ +0x268, which agrees).
  The citizen noun 0x018EB4B7 has its owning city at **+0x1010**; many registered citizens of a save have no live
  owning city and say nothing about either city.
* ~ (runtime, fork) Once NPC nations exist, "the only blank nation" does not identify a new capital (every new nation
  starts blank); the fork snapshots the live cities before `SpawnNextCity(ex, 0, 0)` and takes the one new city.
  The tutorial captions of Civ stop the game time: a founding requested then completes only after the captions
  are answered (3 minutes in one run). `cCity +0x38` (a "city level" candidate, as cCommunity +0x38 in Tribe) read 0
  on a fresh and on a grown capital: **no city level found there** (negative result).
* ~ (runtime, fork) `GetDesignKey(city+0x590, design)` with 0x99E92F05 (City Hall) or 0xF670AA43 can return a stock
  fallback (0x068044ED hall, vehicle 0) when the custom model is not in the resource manager: entering Civ does not
  prove a guest has the host's designs. A custom building and a custom vehicle can share one instance id
  (0x29160CE8 in the fork's save), so group and type are part of a design's identity.
* ✔ (Steam 3010a413, fork: vtable dispatch, ret checked) Control entry points:
  * City spatial (city+0x120) vt 0x58 -> **0xBD8300**, bool thiscall(spatial), plain ret: reads byte spatial+0x21C =
    city+0x33C (mbIsPlayerCity). **0xBD8310** = SetIsPlayerCity(bool), ret 4. (Our section 1 table's 0xBD8300, the
    specialty getter, is a GOG 2016 address.)
  * Vehicle spatial (vehicle+0x34) vt 0x58 -> **0xC9E820**, bool thiscall(spatial), plain ret: compares the native
    player political id with the vehicle's.
  * **0xBFBD30** the nation AI update: ECX = nation, no stack arguments, plain ret at 0xBFC4B7; the city simulator
    calls it at 0xAE62E6 and ignores the result. 0xBFBD68 calls GetPlayerCivilization, 0xBFBD6D compares ESI with
    EAX: the player branch is chosen by **pointer**, not by the nation's +0x89 mIsPlayerOwned flag; the common economy
    updates run before the branch.
  * Terrain cursor (GOG 2024): `GetTerrainCursor` 0xB30EB0 cdecl, ret at +0x107; vt 0x58 SetObjects 0xB2FC70 only
    stores the borrowed vector pointer at cursor+0x164, ret 4; vt 0x5C GetSelected 0xB310F0, vt 0x7C box selection
    0xB31170, vt 0x80 ApplySelection 0xB30920 all read that list (a nation's +0xD4 vector). An empty ground click or
    box need not deselect. A guest mouse drag selects its own nation's vehicles only when the cursor holds that
    nation's +0xD4 vector.
* ✔ (fork, ret checked) **MoveSelectedToGround 0xCF2680 is not argument-free**: it ends with `ret 0xC` at 0xCF2A48;
  live input passes the target, mouse flags and a position pointer (our `.sym` names it only as a caller of
  OrderStamp).
* ✔ (live, fork BUG-9155) Refusing cCity::ChangeOwner (not calling the original) for a city keeps it with its nation on
  host and guests; the capture flags +0x2E0..+0x2E6 (set by Capture before ChangeOwner) must be cleared then. A city
  given back with ChangeOwner(city, pol, 0, 0) and then City_SetPartsOwner(city, nation) outside the game's
  ChangeOwner (the host's tick) followed on the guest.
* ✔ (live, fork) A nation that loses its only city is gone at once: CivByPol of its political id returned null right
  after the capture (UpdateCaptureDecay drops dead nations), so a capture cannot be given back to it.
* ? (fork) The native game over of the player nation (the host's last city) is not known.
* ✔ (runtime, fork BUG-9170) The creature editor (EditorRequest CreatureEditorExtraLarge, no creation) opens from Civ
  as from Tribe and returns to Civ; the saved creature is then usable. The native Tribe -> Civ graduation runs the
  City Hall editor (0x99E92F05) and then the Military Land vehicle editor (0x7D433FAD).
* ~ (runtime, fork) An incoming ChangeOwner while that city's planner (CommunityEditor) is open leaves buildings of
  both factions in the city; the fork holds a city whose editor updated within the last three frames.
* ? (fork) The position field of a cCivExpansion site record (+0x14, stride 0x64; [civ-spawn-next-city-args](#civ-spawn-next-city-args))
  is still not known; SpawnNextCity's non-zero `who` argument is not understood.

## Fork notes: campaign credit, Space readiness and the telemetry manager (2026-11-19, docs/lineages-re)
<a id="civ-lineages-campaign-credit"></a>

Source: the Lineages fork by PandizeLive (PR #196); not yet verified on our lab.
GOG 2024 / Steam code, fork BUG-9056 / 9058; adds to [civ-stage-end](#civ-stage-end).

* ✔ (fork, ret sites) **GetCityCount 0xBF0320** thiscall(nation), plain ret at +0xF: (nation+0xA0 - nation+0x9C) / 4.
  Only the unlock tooltip (return 0xE05ECF) and the tech tooltip (return 0xE06345) use it as a pure count without
  indexing the city vector.
* ✔ (fork) AddCity 0xBF44B0 calls SetVehicleTech **0xBEF8C0** at 0xBF4611: one int, `ret 4` at +0x66, raises
  nation+0x298 only upward and refreshes the player feedback (it can start the air-vehicle caption). **0xBEFE80** the
  final-count getter: cdecl, no arguments, returns at +0x35 / +0x43, reads Civ tuning +0xAC. **Highest owned city
  count: nation+0x4D0.**
* ✔ (fork) **RefreshUnlocks 0xBF2510** thiscall(nation), plain ret at +0x229: clears the twelve unlock bytes at +0x484,
  maps the player history traits, compares city counts with the native thresholds, reads the first city's specialty
  for the fallback global weapons and disables them at the final count. It reads only the city vector's length and
  first city and never changes the vector.
* ✔ (fork) ChangeOwner 0xBE8A20 calls GetPlayerCivilization at 0xBE8A85 (return 0xBE8A8A) to pick the campaign
  history and goals; ChangeOwner resizes the event payload to ten words at 0xBE8D97 and appends the specialty
  (+0x24). Capture counters +0x4C0 / +0x4C4 / +0x4C8 as in section 1.
* ✔ (fork) **0xCF7730** (Space readiness) ignores ECX, takes no stack arguments, returns AL, plain ret at +0x1E / +0x21:
  player nation +0x298 >= 4; ChangeOwner calls it at 0xBE8E5D. At ten team cities it returned true on both games.
* ✔ (fork) **0xCF8F80** the campaign fraction: no stack arguments, float in x87 ST0, ret at +0x8F / +0x95; home city
  vector length / tuning +0xAC, 0 without a nation or denominator. Do not call it with an integer-only console call
  (ST0 is left live). The Civ HUD calls it at 0xCEAB7F (return 0xCEAB84), stores ST0 at 0xCEAB87 and passes it to the
  widget setter 0xE04510.
* ~ (fork, superseded inside the fork) **0xAE3740** thiscall(tuning, int kind, Vector3 position by value), `ret 0x10`
  at +0x12D, ECX unused, builds a 9-word (36-byte) event (kinds 3 / 4 / 5 = military / religious / economic, stage 3;
  payload +4 = the home nation's own city count). The manager behind it, **0x67DE50** (cdecl, plain ret +5, loads the
  global 0x165D910), is **telemetry** (telemetry.dat, App/Telemetry, writer 0x7EB6D0), not the player-visible history:
  its records prove nothing about the timeline. AddEvent 0x7EC230 (stage, kind) `ret 8` at +0xB2; the stage-map lookup
  0x7EC0A0 (key pointer, ret 4) **inserts on a miss**. Manager result +0x10 bucket array, +0x14 bucket count; a stage
  node: key +0, list sentinel +4, hash-chain link **+0x10** (the serializer 0x7EBC70; +0xC was wrong); a list node:
  next +0, prev +4, event +8, kind node+0x10, payload vector node+0x14 / +0x18. B26910 before it returns the Civ
  tuning, not a history manager.
* ✔ (runtime, fork BUG-9088) A nation's second city opens the native Communications Panel lesson (title 0x01510D07,
  box 0x06318E78, checkmark 0xFFFFFFF1); a captured religious city's specialty question uses the same window 0x06318E78
  with choices 0x05107B19 / 0x05107B1A ([civ-shared-message-box](#civ-shared-message-box)).
* ~ (runtime, fork BUG-9052) The camera lesson of the Civ tutorial advances with repeated key-down messages followed by
  key-up, not with one held message (centered cursor in both variants); GetKeyboardState was called 0 times during the
  lesson. The vehicle-selection lesson, the geyser caption and the Goals tooltip follow it.

## Fork notes: military production, vehicle, turret and building combat (2026-11-19, docs/lineages-re)
<a id="civ-lineages-combat"></a>

Source: the Lineages fork by PandizeLive (PR #196); not yet verified on our lab.
GOG 2024 / Steam code, fork BUG-9072, 9076; adds to [civ-ai-vehicle-spawns](#civ-ai-vehicle-spawns).

* ~ (fork) `VehicleSlotsLeft` 0xBF2240 is 0 at 36 vehicles; else the **purpose allowance 0xBF2110** minus the count of
  vehicles of that purpose. Runtime: a rival with two cities held 32 economic and 7 military vehicles and the military
  query returned 0.
* ~ (fork) The military branch of 0xBFA7A0 calls GetElapsed on nation+0x148 at 0xBFA974 and needs more than 40,000 ms;
  the religious branch reads nation+0x168 and needs 50,000. The game can skip the timer when it changes targets. The
  purchase loop checks the expansion budget against the price at 0xBFB058, the slots at 0xBFB067, spawns at 0xBFB0B1
  and subtracts the price at 0xBFB0CB. Runtime: eight vehicles for 24,000 after 28,000 ms.
* ✔ (fork, vtable and runtime) The vehicle combatant (vehicle+0x508) vtable 0x01474CE8 +0x18 -> 0xCAB6F0; its +0x58
  getter **0xBFCC50** reads combatant+0x2C in Civ (the max HP, = vehicle+0x534); current HP combatant+0x38 (=
  vehicle+0x540, as in section 1).
* ✔ (fork, prologue) The turret combatant vtable 0x01466C28 +0x18 -> **0xBD0660**: the prologue subtracts 0x588 from
  the receiver and checks incoming attackers for the turret type 0x0436F315. The turret's city pointer is at
  turret+0x688. 0xBE3AE0 (placement at a given slot) rejects occupied or missing slots, then calls the constructor
  0xBE2100. Runtime: the native weapon of a turret loaded cold targeted a nearby vehicle after an attack order.
* ~ (runtime, fork) Houses, factories and entertainment buildings: combatant at +0x120, current HP +0x158, **max HP
  +0x14C** (800 for a rival building). City Hall's native damage path adds capture progress only when the other
  buildings are destroyed (or for special damage types): hitting City Hall first lost 0 HP.

## Fork notes: buying a city and trade (2026-11-19, docs/lineages-re)
<a id="civ-lineages-trade"></a>

Source: the Lineages fork by PandizeLive (PR #196); not yet verified on our lab.
GOG 2024 / Steam code, fork BUG-9079, 9080, 9081, 9082; adds to section C and the city purchase notes of BUG-0877.

* ✔ (fork, disassembly) `cCity_Buy` 0xBE99A0: GetMoney at 0xBE9A04, SpendMoney at 0xBE9A1E, Capture type 2.
  FinishBuyCity 0xBF0600 reads the pending city at nation+0x440, converts the signed int price at **+0x444** to float
  and calls cCity_Buy at 0xBF063B. BuyCity 0xBF0520: pending target +0x440, selected int offer +0x444, float quote
  +0x448; five offers 32,000 / 24,000 / 16,000 / 8,000 / 4,000. The offer handler 0xCFFC7F reads the player nation's
  scratch fields and wealth; 0xCFFCC9 subtracts the fair-offer tolerance 120 (float at 0x013F1EF4) from the quote: a
  fair or higher offer is accepted, a lower one refused (not random). FinishBuyCity(false) zeroes the meter +0x758 and
  sets the buyer +0x75C to 0xFFFFFFFF; "Nevermind..." closes the talk through its cancel path and keeps the meter.
* ✔ (fork) The command handler 0xCF3350's Buy City branch calls **CanBuyCity 0xBD8110** at 0xCF35F1 and GetBuyPrice
  0xBE93A0 at 0xCF361F, both with the native player political id. CanBuyCity needs city+0x75C == buyer and city+0x758
  >= 100; an enabled menu item alone does not prove it.
* ✔ (fork, ret site) AddBuyProgress (GOG 2024) **0xBDD270** thiscall(city, float amount, uint32 buyerPol), `ret 8` at
  0xBDD31E (same body as section C). The economic vehicle calls it at 0xDCCBFB after computing its influence at
  0xBD9590. TradeDelivery and GetBuyPrice are separate functions.
* ✔ (fork, prologue, callers, live reads) **0xD00BD0** thiscall(relationship manager, polA, polB, bool) `ret 0xC`
  returns the relationship category 0..4 (the "level query" of ChangeOwner).
* ✔ (fork, live entry probe) Route acceptance **0xCEEF90** (cCity*, requesting cCivilization*) on the stack, `ret 8`,
  returns a float; the route behaviour calls it at 0xDCA391. For a requesting nation whose +0x89 mIsPlayerOwned is
  false it returns 0 at category <= 1, then needs category >= 3 from the target; later branches tell player, AI and
  neutral cities apart. No route in a hostile fixture is no evidence of broken routes. Test setup only: one scalar
  diplomacy event per direction set to the cheat event 0x0526E5DC with value 100 gave category 4.
* ✔ (fork, ret sites) AddTradeRoute 0xBE1490 thiscall(city, partner, type) `ret 8` at 0xBE196A appends to **both**
  cities' +0x55C vectors but creates the +0x570 price-map entry **only on the first city**: replaying routes in an
  arbitrary endpoint order does not reproduce both cities' pricing maps. GetBuyPrice 0xBE93A0 reads +0x570 before its
  price tuning.
* ~ (fork) RemoveTradeRoute 0xBDFA70 thiscall(city, partner) `ret 4` removes only the selected city's entry: remove
  both ends before rebuilding a route. (Our section C note keeps "?" on the shared route object, which calling it for
  both ends might free twice; the fork does not say how it handled the object.)
* ✔ (fork, callers 0xBE14E9, 0xBE93E0) **CityTradeMap_Index 0xBE08E0** thiscall(map, key pointer) `ret 4` -> node+0x14
  float*, keys compared as **signed** ints, a missing value inserted as 0 (city+0x570).
* ✔ (fork, call site 0xD05F27) **FloatMap_EraseIterator 0x8DB190** thiscall(map, output iterator*, node iterator by
  value) `ret 8`: decrements map+0x14, rebalances, frees the node without a value destructor, returns the next node
  (also used for the integer follower map: map+4 anchor).
* ~ (fork) DeclareAlliance / DeclarePeace with Civ political ids instead of cEmpire pointers crashed at IsAtWar+0x22
  reading 0x2232+0x84 (fork BUG-9080); the signatures are in docs/re/space.md.

## Fork notes: religious conversion and preaching slots (2026-11-19, docs/lineages-re)
<a id="civ-lineages-religion"></a>

Source: the Lineages fork by PandizeLive (PR #196); not yet verified on our lab.
GOG 2024 / Steam code, fork BUG-9078, 9084, 9086. Competing converters: see the CONFLICT line in section B.

* ✔ (fork, runtime reads) Follower map city+0x5A0: root +0xC, count +0x14, node right +0, left +4, key +0x10, int
  followers +0x14. **CityFollowersMap_Index 0x643A00** thiscall(map, const uint32* key) `ret 4` compares **unsigned**
  (jb / jae, checked at the ConvertCitizens calls 0xBEB235 and 0xBEB2A1) and returns node+0x14 int* (our `.sym`
  `MapU32Int_StageMap_At`, same address); do not use the signed trade map index 0xBE08E0 for it.
* ~ (fork) Purpose 1 is religious, 2 economic (as in section 1); one ConvertCitizens call moved followers without a
  capture; repeated preaching across frames with city updates between completed it.
* ✔ (runtime, fork BUG-9086) The four 128-byte preaching slots of a rival city never seen by the host's camera
  (city+0x618 / +0x61C) have **capacities 0 at slot +0 / +4**: the preacher attaches and takes retaliation (from
  0xBEB6F8) but cannot convert. Moving the host's camera there sets them to 10 / 2 and the same AI captures the city.
  * 0xD57B00 updates the city crowd controller's formations (controller+0xAF0 = the city), called from 0xD616DF (bind)
    and 0xD61A55 (refresh); the manager 0xAC8C80 has one controller at +0xA4 (it owns the visible crowd: do not bind it
    to remote cities).
  * The formations: cCommunityLayout::SetParameters 0xAF9C70 `ret 0x10` and 0xAFDF60 `ret 4`, spacing 1.5 (0x13EED94),
    inner radius 7 (0x1486054), outer radius 3 (0x13EB950); 0xAFDF60 makes trunc(r^2 / spacing^2 * 0.5) 24-byte slots.
    0xD57D16 passes both counts to **0xBD9FF0** thiscall(city, slot, innerCount, outerCount) `ret 0xC`, which writes only
    slot+0 / +4.
  * **0xBEB310** thiscall(city, int slot, int activePreachers) `ret 8` at 0xBEB708; its only caller 0xBEB7EA passes the
    slot and the number of active preaching slot objects (not a time step). A slot's spatial object is at slot+0x78.
    (Our section B note of the per-slot preach tick 0xBEB2F0 has one argument; that is a GOG 2016 address.)
* ~ (fork) The +0x5BC / +0x5D8 per-pol maps are transient deltas.

## The cities' religious and economic standing on the guests, preaching places (2026-11-19, fix/w56-sync)
<a id="civ-influence-relay"></a>

BUG-1284..1287 (found by the Lineages fork (PR #196), its BUG-9078, 9084, 9086, 9081). GOG 2024, Ghidra
(tools/ghidra/q.sh); mods/coop/civ_influence.cpp. Checks the fork notes of [civ-lineages-trade](#civ-lineages-trade)
and [civ-lineages-religion](#civ-lineages-religion) on our build: the addresses and signatures below are ours now.

* ✔ The city update **0xBE2260** (fastcall(city); GOG 2016 0xBE2240) recomputes the conversion meter +0x750 from the
  follower map +0x5A0 indexed with 0x643A00 (unsigned keys): 200 * f[+0x754] / (f[+0x754] + f[owner]), clamped 0..100;
  0 or less sets +0x754 = -1. It also lets the capture meter +0x748 fall by 0.1 per call when the city is not attacked
  (0xC00600(0) false; +0x74C = -1 at 0), and clears a converter / capturer / buyer (+0x75C, with +0x758 = 0) whose
  civilization is gone or is the owner. So on a guest the map and +0x754 are the state; +0x750 follows.
* ✔ **0x643A00** (`SporeCoop::MapU32Int_StageMap_At`) is operator[] of eastl::map<uint32, int>: unsigned compare, a
  missing key inserted with 0 (0x643290). **0xBE08E0** (`SporeCoop::CityTradeMap_Index`) is the same for the price map
  +0x570 with a signed compare (insert 0xDE6A10). Node: right +0, left +4, key +0x10, value +0x14; map: anchor +4,
  root +0xC, size +0x14.
* ✔ **CanBuyCity 0xBD8110** thiscall(city, buyer) ret 4: buyer != owner (vt+0x4C), +0x75C == buyer, +0x758 >= 100
  (COMISS on a float: +0x758 is a float). **GetBuyPrice 0xBE93A0** thiscall(city, buyer) ret 4, float in ST0: -1.0
  when the price map has no entry for the buyer (find 0xE39F60), else the tuned price of that entry.
* ✔ **RemoveTradeRoute 0xBDFA70** thiscall(city, partner) ret 4: the +0x55C entry of that partner: its partner ref
  released, the planet model told (0xB868E0, 0xB7E4B0), the route object destroyed (DestroyInstance) unless its vt+0x2C
  says it already is, its ref released, the entry erased (0xBDF580). The game's own removal **0xBDFB30** thiscall(city,
  pol) ret 4 (callers: ChangeOwner at 0xBE8F08, 0xBE5144, 0xBFBF4B, 0xCFF392 / 0xCFF4EC) removes every route of the city
  to a city of that pol the same way inline and calls 0xBDFA70 on the partner: a route is removed at both ends.
* ✔ **The preaching places**: City_SetPreachingCapacity **0xBD9FF0** thiscall(city, uint slot, a, b) ret 12 writes
  (+0x618)[slot * 0x80] +0 = a, +4 = b for slot < 4; its only caller is the crowd controller 0xD57B00 (0xD57D16), which
  the camera's city has. The preach tick reads capacity slot+0 minus used slot+8 to bound the converts (decompiled).
  ~ Not seen at runtime by us: a city the host's camera never looked at keeps 0 / 0 (the fork's runtime finding);
  civ.remote_preaching gives such a slot 10 / 2 in the host's preach tick (coop_civ_influence.py --case preach checks it).
* Decision (BUG-1285 / BUG-1287): the host sends every city's follower map, converter, buy meter, buyer and price map
  once a second in MSG_CIV_STATE chunk 0xF9 (chunks 0; an older guest drops it as a bad header); the guest writes them
  with the game's own index functions (no node erased: a value the host has not is set to 0) and its own ConvertCitizens
  returns while the host's maps come. A host route the guest has and the host's list has not is removed on the guest at
  both ends (guest_roads).
* Decision (BUG-1284): the religious governor's bonus in ConvertCitizens counts a religious vehicle (purpose 1) of the
  converting nation within 200 m; it counted economic vehicles (2) of any nation.

## A guest that missed the end of Tribe: its own Civ editors at its first arrival (2026-11-19, feat/w56-late)
<a id="civ-late-arrival"></a>

BUG-1290 (WP-S1b, `core.own_civ_late`, mods/coop/civ_arrival.cpp; #civ-own-arrival for the editors themselves).
* ✔ (code) BUG-1276 made the own City Hall and vehicle editors due only on the host's end-of-Tribe vote seen by a
  guest in the host's Tribe; that state is a session state. A guest that joins in Civ, reconnects or restarts its game
  after the change never had them due.
* The rule now: the first arrival of a session in the host's Civ world (mode Civ, the player civilization there, not
  elsewhere, no load queued) is looked at once: due when the arrival path allows it (own mode, a host with the line,
  WP-C3's editors), the vote path is not due, the guest plays an own creature here (`creations::plays_own`), it has no
  own City Hall (nor any own Civ design) in its line, and its SporeCoop\worlds.ini has no `[civ_arrival] <world crc>` note. Then the same
  settle and editors as #civ-own-arrival; a stored row that brings an own City Hall before the settle ends drops it.
  The note is written when the City Hall editor opens (the red cross counts as offered), on the vote path too.
* A guest that plays the host's species at its arrival gets none (the civ fixtures' guests: no editor pops up in the
  lab's Civ tests); it picks its creature with the co-op window's row (#creations-late-species) and gets them at its
  next arrival. `coop.civarrival` prints `late: ... checked why='...'`; `coop.civarrival forget` removes the note.
* Not checked: the world crc of a world reached by the live Tribe -> Civ change equals the one of the planet's save
  loaded later (both are the save's name; BUG-0258's "snapshot.spo" is the host's side).
## The Civ return's own pause after an editor without the planner (2026-11-19, fix/w57-own-editors)
<a id="civ-editor-return-pause"></a>

BUG-1323. GOG 2024, Ghidra (0xCFE2C0, 0xCFBD70) and a runtime pause trace (`coop.pauses trace`, lane 2: `coop.civarrival
due` on a guest in civ_npc, its own City Hall editor, done):
* ✔ `GameCiv_Update` 0xCFE2C0: while cGameModeCiv +0xB4 (the state machine `CivGame_EditorReturn` 0xCFBD70) is not 10
  it calls `cGameTimeManager::Pause(0x4BF38A7)` when the time does not stand yet (trace: "Pause(04BF38A7) -> 1 from
  00CFE36E" the frame the guest came back) and steps the machine.
* ✔ The machine resumes 0x4BF38A7 only in its state 7, and only when the mode before (+0xB8) was **not** the editor
  0xDBDBA1 (and +0xBC is Civ). After an editor its state 1 re-opens the planner (0xCF20B0 with +0xF8) and that planner
  carries the pause (its close lifts it): the planner's design editors come back to a running game.
* ✔ An editor that came from no planner (+0xF8 = -1: the mod's own City Hall and vehicle editors at a guest's Civ
  arrival, civ_arrival.cpp) leaves 0x4BF38A7 counted once: the game stands (busy=paused, no HUD) for good; the arrival
  path then waited for its vehicle editor forever (lab 20261119-113002: civ_own_arrival, civ_late_arrival,
  line_stages_editors; also why that guest bought no city and no vehicle afterwards).
* Fix `core.own_civ_unpause`: when our editor came back to Civ and +0xB4 reached 10, 0x4BF38A7 counted more than when
  the editor was requested is resumed once (probe: "resumed=1" after the City Hall editor, the vehicle editor opened
  5 s later, "resumed=2" after it, `busy` empty).
* ? Not looked at: whether the game's own Civ outfitter and spaceship editor paths (from the planner / the launch)
  ever come back without a planner; the Civ outfitter's result is applied in state 1 too.

## The civ_npc fixture's vehicle slots and a full religious conversion (2026-11-19, fix/w57-sync)
<a id="civ-npc-fixture-slots"></a>

BUG-1284..1286 tests (coop_civ_influence.py). ✔ (Ghidra) `VehicleSlotsLeft` 0xBF2240: 0 at 36 entries of +0xB0, else
the purpose allowance **0xBF2110** minus the civ's vehicles of that purpose (vehicle +0xB20). 0xBF2110(purpose): 1 for
purpose 4; 3 when the purpose is the nation's own type (0xBF1080: 0 military, 1 economic, 2 religious; +0x8A clear),
plus a tech term (0xBF0330 x 0x169CB64), plus per city of that specialization (0xBD8320) its size term (0xBD8AB0 / 0x169C088,
at most 6, or 3 for a city with +0x519), capped at 36. ✔ (runtime, lab 20261119-104136, 20261119-114425) the civ_npc
fixture's civilization (one city, military by its vehicles: inferred; four vehicles) has no religious or economic allowance: "no vehicle slot left"
even after all four vehicles were removed (`coop.civ remove`). `coop.civ spawnveh <ci> <purpose> <loco> force` spawns
past the allowance (the game's SpawnVehicle does not check it; hpref of #civ-own-designs did the same; the 36 cap
stays). Of three forced preachers at a far city the host often had only one left later (the city's retaliation
0xBEB6F8, or a disband: not told apart), without a fault in the other runs; in one run with them 30 m in the air the host
faulted (BUG-1322; CORRECTION (2026-11-19, fix/w57-crashes): not the height: the mod's shot observer called through a
preacher's cCulturalTarget spatial, see [civ-shot-observer-targets](#civ-shot-observer-targets)). ✔ (runtime, lab 20261119-114030) a vehicle `vput` 30 m over the ground stays there on the host; the
guest's game puts its copy on the ground (sync-check "v6 34.0"): tests put vehicles on the city's sphere.
✔ (runtime, lab 20261119-104136 civ_religion_state) ConvertCitizens until every citizen of an NPC city follows the
player (15 of 15, the meter 200 * 15 / 15 clamped to 100) captures the city at once: "Alice captured a city", its
cinematic, a share save; the host's meter +0x750 reads 0 right after while the guest's game (the capture not taken yet) showed
100. A test of the meter converts part of a city (2 of 15: 26.7 on both games, lab 20261119-111559).
* (fix/w57-sync) The BUG-1286 hook (civ_influence.cpp h_PreachTick) sets a slot's places only when the slot has its live
  spatial object (+0x78 non-null), the fork's verified condition; before, any 0 / 0 slot ticked got 10 / 2.

## A vehicle of a kind the culture has no design for (2026-11-19, fix/w57-civlook)
<a id="civ-vehicle-no-design"></a>

BUG-1274. GOG 2024, runtime (line_stages).
* ✔ (runtime, lab 20261119-133307 line_stages, both games) A Civ game started from Tribe has a land vehicle design only
  for the capital's specialty; a vehicle bought in a captured city of another specialty (military 7D433FAD here, the
  capital economic) through `coop.civ buy` is made with the model key **{0, 0, 0}** at +0xC4: no model (only the
  spawn and heal effects are drawn), base max hit points 10 on the host (249 in the guest's mirror). The game's own
  purchase UI asks for a design first; the lab's buy and a guest's own editor path do not.
* civ_own_designs.cpp skipped every object with +0xC4 0 ("no model yet"), so the guest's own design never applied.
  Fix `civ.own_designs_nodesign`: a vehicle with the zero key whose owner has an own design for its id gets it; its
  culture key is the zero key (written back raw for a save, SetModelKey with it at the end, as cVehicle::Load did).
* ✔ (runtime, lab 20261119-140235 / 20261119-141949 civ_own_designs, 20261119-140215 line_stages) With every vehicle
  and building stripped (+0xC4 the culture key while saving), the host's save still named the guest's design keys:
  the vehicle design in an event of the main stream 0x01897C18 (`{0x24682294, key, 0x40646200}`, little-endian) and in
  the cPlayer record 0x02C21781, the house design in the house record 0x018EB106 at the object's **+0x224** (a second
  key field of a building, written by the attribute writer 0xAE33DF). So a design key can not be kept out of a save
  per object field: civ_own_designs.cpp adds a savestrip key filter (docs/re/creations.md#creations-save-keys: every key of
  Stream_WriteResourceKey, dword blocks and event lists, now also of the vehicle and building model types) that writes
  the culture set's key of the design id the line names it for (zero for a kind without a culture design). Lab
  20261119-142443 civ_own_designs: no key of ours in the save.
## The host's shot observer and targets that are no listed object (2026-11-19, fix/w57-crashes)
<a id="civ-shot-observer-targets"></a>

BUG-1322. Follows "How Civ weapons fire (BUG-0477)" above and BUG-0535.

* ✔ (runtime, lab 20261119-114030 civ_preach_far, host) The fault with EIP inside a preaching vehicle (+9) and the next at
  0xCCCCCCCC came from the mod, not from the game's FUN_00bfd750 (that address was a stale stack entry): the module
  return addresses on the stack, mapped on a build of the run's commit (dee4405f), are `civ::fire_observed`
  (civ_shots.cpp, the target branch) and `game::object_position` (the Cast vcall, slot 0xC), called from squadron's
  `cToolManager::FireTool` hook; esi held the Cast id 0x1186577. fire_observed took the object at target spatial - 0x34
  and, when its first dword lay in SporeApp.exe's image (BUG-0535's check), called its vtable: for a spatial that is not
  +0x34 of a game object the "vtable" is whatever memory precedes it.
* `cCombatSimulator::Update` 0xAE7360 passes as the target the target combatant's (+0x44) vt+8 result. ✔ (runtime, lab
  20261119-141549 civ_shot_objects, host, preachers on the ground at an NPC city; Ghidra) a preacher's target spatial has
  vtable 0x1468478, which sits at **+0x100** of `Simulator/cCulturalTarget` (noun factory 0xB1EB40, 0x218 bytes, ctor
  0xBDACB0: cGameData, combatant 0xBFD620, cModelObject; vtables 0x14685B8 at +0, 0x14685A8 +4, 0x1468540 +0x38,
  0x1468478 +0x100; dtor 0xBDAE30). Target - 0x34 is then cCulturalTarget +0xCC, a field (0x00000018 in that run): when
  it happens to hold a value in the image's range the old check took it for a vtable. So every religious preaching in a
  co-op host's game went through this; the crash needed that field's value, which explains 1 fault in 4 runs.
  Vehicles and buildings keep their spatial at +0x34 (BUG-0477).
* The mod (BUG-1322): `civ.shot_target_listed`: a shooter or target that is not one of the host's listed vehicles is
  used only when the object at spatial - 0x34 is in the game's object list (`game::listed`, pointers compared, once
  per object and frame); else nothing is called and the shot has no target the guests could find (kind 2).
  `coop.civ shotobjs` prints the unlisted ones seen, `coop.civ shotprobe` (tests) one observed shot at a target that is
  no object. Test `coop_civ_influence.py --case shotobjs` (docs/06-testing.md#testing-civ-shot-objects).
  no object. Test `coop_civ_influence.py --case shotobjs` (docs/06-testing.md#testing-civ-shot-objects; on the 0.7.15
  line `coop_civ_shot_objects.py`, fix/w58-hotfix).

## The Tribe stage's listener and the results of a guest's own arrival editors (2026-11-19, fix/w58-assignnames)
<a id="civ-arrival-tribe-listener"></a>

BUG-1329, on top of [civ-own-arrival](#civ-own-arrival). Code: mods/coop/civ_arrival.cpp (`core.own_civ_tag`).

* ✔ (lab 20261119-113002, -130049 civ_late_arrival; 20261119-200557, -201729 creation_props_key0: the guest's log)
  The result message 0x30C11C7 of a guest's own City Hall editor (tag 0x116DD1B, saved or left with the red cross)
  faulted on that guest: `exception 0xC0000005 at 0x00645E4D hash_map__int_int__Find+0xD (data 0x00000030)`, ebp
  0x030C11C7, esi 0x5312DA01. The stack names (AssignNames+0x1443, TribeStage_RemoveMemberCard+0x1D8F) are the nearest
  symbols only: the real chain is 0xCD81F0+0x163 -> 0xCD6B00 -> 0xB708A0 -> hash_map Find. Both routes had it (the
  restarted guest of civ_late_arrival and the live end of Tribe of creation_props_key0); Wine's game went on.
* ✔ (Ghidra, asm 0xCD831C..0xCD83A5) The Tribe stage's message listener 0xCD81F0 (`TribeGame_GoalListener`, this = the
  stage + 8) is still registered in Civ. On 0x30C11C7 with result +0xC == 0x116DD1B it calls `cCinematicManager::Get(1)`
  -> 0xADF310, then **0xCD6B00(stage, 0x5312DA01)** (a Tribe hint), the intrusive assign of the result (null when
  cancelled) into stage +0x2F8 (0x572620), and with stage +0x28 set and a City Hall result (+0x14 0x99E92F05, not
  cancelled) 0xCD3F80, which copies the result's key +0x18..+0x20 into cPlayer +0x1150..+0x1158; with stage +0x29 the
  same for the vehicle ids (0xCD3FB0).
* ✔ (Ghidra) 0xCD6B00 (thiscall(stage, uint32 hint) ret 4) for hint 0x5312DA01 calls **0xB708A0** with ECX = **stage
  +0x394** (`MOV ECX,[EDI+0x394]` at 0xCD6F74): the stage's hint list; 0xB708A0 (thiscall(list, id) ret 4) finds the id
  in its hash map (+0x2C / +0x30) and runs the hint's vt 0x78. In Civ that list is gone (null): the read of +0x30.
* ✔ (Ghidra `scan`) 0x116DD1B is also a common component id (casts at 0xDB4D22, 0xF207A2 and ~40 more sites); the tag
  check of the Tribe listener at 0xCD832B is the only listener test of it that matters here. `GameCiv_HandleMessage`
  acts on the tags 0x66787BB and 0x116D51D only, the planner's listener 0xD130D0 copies every result whatever the tag
  (0x91D4AF08 is the planner's own request tag, tested at 0xD08D85 / 0xD135A5).
* Fix `core.own_civ_tag`: our arrival requests carry the tag **0x0C0B1329** (no dword of the exe has it), so no game
  listener acts on their results; `civ_arrival_result` takes the result by that tag (cGameModeCiv's listener hook), the
  planner's copy stays the second way. Off: the Tribe tag as before (the twin civ_late_arrival_tag_off).
* Negative: the result of a guest's own Tribal Outfitter (designs.cpp, in Tribe) must keep the Tribe tag: there the
  listener's +0x2F8 copy is the way its result comes back (0xCDCC90).

## Citizens exist near the camera only; NPC city specialties (2026-11-20, fix/w60-tribe)
<a id="civ-citizens-camera"></a>

BUG-1343. GOG 2024, lab 20261120-041304 civ_own_arrival (logs, key frames), no new disassembly.
* ✔ (runtime) A game has citizen objects of one city only, the citizen manager's (mgr +0xAF0, #civ-own-species): with
  the guest's camera showing the bought city at the horizon (key frame screen point (768, 207)) the guest's game had
  none of its citizens for 31 s while the host's (camera close) had 19. A test counting citizens must first bring each
  game's camera onto the city until `coop.civ citizens` says `mgr city=<that city>`.
* ✔ (runtime) The NPC cities' specialties (+0x540) are the planet's and differ from run to run (the same fixture planet:
  the city nearest the capital military in some runs, religious in others). A vehicle bought in a city is of its
  specialty's design id, and the guest's vehicle editor at its Civ arrival is the capital's specialty's: a test that
  needs a city where that design applies sets one with the game's own `cCity_SetSpecialty` 0xBE7520 when none has it.

## A wild creature killed out of the cameras' relevance goes to the pool with its body (2026-11-21, fix/w62-flake-tcs)
<a id="civ-wild-kill-pooled"></a>

BUG-1401. GOG 2024, logs of lab 20261121-003202 (civ_wild_sync, lane 2).
* ✔ (logs) Two vehicles of the guest killed a wild creature about 180 m from both cameras' point: the host's npc pass
  logged `npc 140 ... is updated at the planet's centre: herd 00000000, 507 m from where it was a frame before, dead=1`
  (the frame before it stood on the ground), and the census then listed the same object (same serial) alive at
  (0 0 0): the game took the body into its pool in the frame of the death and gave the object out again. Neither game
  showed a body. A kill near the camera (every other civ_wild_sync run) keeps the body.
* ✔ (logs) The census lists pool objects given out at unplaced points ((0 524.8 0)) as live creatures
  (#civ-wild-reused-frozen); a test that picks "the densest place of live creatures" can pick one of them alone.
  Tests pick herds by placed creatures (no coordinate exactly 0, not at the centre) seen in two censuses.
