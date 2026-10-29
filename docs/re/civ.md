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
* Test commands: `coop.civ tables` (sizes of every pointer table), `coop.civ fix boost|world 0|1`.

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
  `h_Capture` never picks a vehicle of an absent owner. `coop.civ fix orphans 0`.
* Co-op (BUG-0117): the superweapon vote captures civ, type and target by value; a request during
  another vote is refused with a notice to its sender. `coop.civ sw` shows the last request and the
  type the last guest vote fired. `coop.civ fix sw_vote 0`.
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
  player civilization object, another save name); switch `coop.fix civ.world_epoch`.
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
  change or session end. Switch `coop.fix civ.caption_only`. Not measured: the purchase chain
  (BuyCity, FinishBuyCity, Capture) and whether +0x2C drops between its cinematics; an NPC capture
  of an NPC city arms too (accepted: its caption would stop everybody the same way).
