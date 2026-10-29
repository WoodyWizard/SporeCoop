# Space stage RE: squadron co-op (GOG 2016 build, sha256 922af7c7…)

Static analysis only (llvm-objdump of `SporebinEP1/SporeApp.exe`, image base 0x400000, no runtime
checks). All conventions are MSVC x86: `thiscall` = `this` in ECX, callee pops `ret N` bytes.
Confidence: **V** = verified in this build's disassembly (prologue/ret/field use seen),
**P** = ported from ModAPI and consistent with code but not traced end to end,
**U** = unverified/hypothesis. Space code at 0x0102xxxx–0x0107xxxx has the *same* addresses as
ModAPI March2017 (port delta 0), 0x00Bxxxxx–0x00Cxxxxx is shifted by −0x10…−0x30.

Note: `overrides_922af7c7.sym` names `0x01020F30` "TimeOfDay_Unavailable" and `0x01021110`
"TimeOfDay_GetObject"; they are really `Simulator::GetCurrentContext()` (`mov eax,[0x16DEA6C];
mov eax,[eax+0x10]`) and `Simulator::GetActivePlanet()` (ModAPI names, same addresses).

## 0. Object layout recap (`cGameDataUFO`, size 0x818)

Vtables written by the ctor 0xC3C9B0 and the dtor 0xC3BAE0 (V):

| subobject | offset | vtable |
|---|---|---|
| cGameData (main) | +0x000 | 0x0146C7E0 (Cast +0x0C = 0xC36FC0, GetNounID +0x20 = 0xC37160 → 0x18EBADC, GetCastID +0x38 → 0xB033B403, SetPoliticalID +0x48 = 0x804880, GetPoliticalID +0x4C = 0x5AADB0 `mov eax,[ecx+0x30]`) |
| DefaultRefCounted | +0x004 | 0x0146C7CC |
| cLocomotiveObject | +0x034 | 0x0146C6D0 (SetPosition +0x38 = 0xC37B70, SetOrientation +0x3C = 0xC88850, Teleport +0x44 = 0xC888C0, GetModelKey +0x98) |
| ? (+0x108) | +0x108 | 0x0146C6C8 |
| cCombatant | +0x508 | 0x0146C660 (GetPoliticalID +0x10 = 0xC69020, GetWeapon +0x14 = 0xC37870 → [ufo+0x790], TakeDamage +0x18 = 0xC3CEA0, GetMaxHitPoints +0x58) |
| cBehaviorList | +0x5D0 | 0x0146C640 |
| IUnmanagedMessageListener | +0x5E8 | 0x0146C638 (HandleMessage = 0xC3B6A0, ret 8) |

Fields (V unless noted): cGameData `mbIsDestroyed` +0x21 (IsDestroyed 0xB182E0), `mID` +0x24
(= `++[0x167CE08]` in `cGameNounManager::CreateInstance` 0xB20B80 - per-process counter, unique
while the session lives), `mPoliticalID` +0x30; loco pos +0x38, rot +0x44, vel +0x1FC,
movement controller `loco+0x4CC` = ufo+0x500; combatant max HP +0x534, HP +0x540,
`mbInvincible` +0x588, `mpTarget` +0x550 (P); `mUFOType` +0x714 (0 = player, see UfoType below),
`mNextPosition` +0x718, `mNextVelocity` +0x724, `mNextOrientation` +0x730, `mOffsetFromPosition`
+0x740, `mbAtDestination` +0x74C, `mDestination` +0x750, `mZoomAltitude` +0x768 (planet: radius
of the flight sphere), `mDesiredModelKey` +0x780 (ResourceKey, printed by the debug dump 0xC3E150),
NPC weapons +0x790 (`mpNPCWeapon`), +0x794 ground, +0x798 near-air, +0x79C medium-air, +0x7A0
far-air, +0x7A4 abduct, current target refs +0x7A8 / +0x7AC (getters 0xC38C40 / 0xC38CC0),
`mEnergy` +0x7F4, `mMaxEnergy` +0x7F8.

UfoType (ModAPI SimulatorEnums): 0 Player, 1 UberTurret, 2 Raider, 3 ?, 4 Bomber, 5 Abductor,
6 Defender, 7 ?, 8 AmbushPirate, 9/10 ?, 11 SecurityDrone.

## 1. Enumerating UFOs in the current scene

| what | address | signature | conf |
|---|---|---|---|
| `GetData<cGameDataUFO>` helper | **0x00AD48B0** | `eastl::vector<cGameDataUFO*>* __thiscall(cGameNounManager*)`: pushes the 5 args below, calls GetData, returns `container+4` (the vector `{begin,end,cap}`), plain `ret` | V |
| `cGameNounManager::GetData` | 0x00B21260 | `tGameDataVectorT* __thiscall(mgr, create, clear, add, filter, nounID)`, `ret 0x14`; rebuilds the cached vector from the noun list (+0x78) only when the container's dirty byte is set | V |
| callbacks for UFOs | create 0xCD7E80, clear 0xD3D3D0, add 0xAD47B0 (casts to 0xB033B403, AddRef, push_back), filter 0xB1E410 (`obj->GetNounID()==noun`), noun 0x18EBADC | | V |
| `cGameNounManager::Get` | 0x00B3D3D0 | cdecl () | V (existing) |
| `cSimulatorSpaceGame::Get` | 0x01002A70 | cdecl (); `mNPC_UFOs` vector at +0x78 (offset from the attribute table 0x139D420) | P |
| `cStarManager::Get` / `GetEmpire(pol)` | 0x00B3D370 / 0x00BA94A0 | `cEmpire* __thiscall(starMgr, uint32 politicalID)` (used exactly so by SpawnUFO) | V |

Evidence: ~40 call sites use the exact sequence
`push 0x18EBADC; push 0xB1E410; push 0xAD47B0; push 0xD3D3D0; push 0xCD7E80; mov ecx,nounMgr;
call 0xB21260` and iterate `[ret+4]..[ret+8]` (e.g. 0x1000C60 kills all type-3 UFOs of the
player empire: `cmp [esi+0x714],3`, `GetPoliticalID`, then `TakeDamage`).

The list contains **every** live UFO noun: the player UFO (type 0, also
`cSimulatorPlayerUFO::Get()+0x40`; the singleton pointer is `[0x016DC9F4]`), our puppets, NPC
UFOs on the planet or in the system. Skip `mbIsDestroyed` (+0x21) and HP ≤ 0.

Per-UFO data to replicate:

| field | read |
|---|---|
| net identity | host keeps `map<cGameDataUFO*, net_id>`; `mID` +0x24 is a convenient stable key on the host only (counter differs per process) |
| owner | `mPoliticalID` +0x30 (or vtable +0x4C). Empire = `cStarManager::GetEmpire(pol)`; player = `GetPlayerEmpireID()` 0x1020F40 |
| type | `mUFOType` +0x714 |
| model | `mDesiredModelKey` +0x780 (12-byte ResourceKey) - pass to SpawnUFO on the mirror |
| pose | loco +0x38 / +0x44 / +0x1FC, plus `mDestination` +0x750, `mZoomAltitude` +0x768 |
| HP | +0x540 / +0x534; invincible +0x588 |
| place | same as players: `SpacePlayerData` context (planet id / star id) |

Recipe (host → guests, 10 Hz like NPC_STATE in the creature stage):
1. Each frame (or 10 Hz) in space: `vec = thiscall(0xAD48B0, cGameNounManager::Get())`; for each
   `u` with `u->mUFOType != 0` and not one of our puppets: assign/lookup net id, send
   `{net_id, type, pol, modelKey, pos, rot, vel, hp, maxhp, place}`; UFOs missing from the list
   → `UFO_DESPAWN`.
2. Guest: on first sight `SpawnUFO(type, &pol, &modelKey)` (0x102ACC0, cdecl, `ret`; model ptr may
   be null → default model; on a planet it calls `0xC3AB80` to put the UFO on the flight sphere),
   then drive it like a puppet (§4). Despawn with `cGameNounManager::DestroyInstance` 0xB224F0.
3. Guest must not have its own NPC UFOs: hook `SpawnUFO` 0x102ACC0 and `CreateUFO` 0x102AC60
   (cdecl `(UfoType, cEmpire*)`, calls `Initialize`) and return null for non-mod calls while a
   co-op guest is in space (keep a thread-local "mod is spawning" flag), and destroy any
   non-mirror UFO with type ≠ 0 found in the GetData list (callers of SpawnUFO such as raid
   code must tolerate null - **U**, check each caller before enabling; alternative is
   spawn-then-destroy next frame). SpawnUFO callers (functions): 0xC5D2C0, 0xD4C150, 0xFF8980,
   0xFF96B0, 0xFFA630, 0x1025200, 0x102B500, 0x102B790; CreateUFO callers at 0xFDAEDA,
   0x1008B3A, 0x1008FF2, 0x100923F, 0x10095C4, 0x100B004, 0x102B204, 0x10387F8, 0x1038B01.

## 2. Player weapons → damage

Every space weapon ends in the target combatant's virtual `TakeDamage` (combatant vtable +0x18).
For UFOs that is **0x00C3CEA0** - the single hook point for both directions.

### 2.1 `cGameDataUFO::TakeDamage` 0x00C3CEA0 (V)
`int __thiscall(cCombatant* this /*ufo+0x508*/, float dmg, uint32 attackerPol, int type,
const Vector3* dir, cCombatant* attacker)`, `ret 0x14`. **`dir` is a unit knock-back direction
(explosion → target), not a world position** (stored at combatant+0x74). Order:
1. `type==10` (heal) → base directly.
2. Return if invincible (combatant+0x80 = ufo+0x588, via 0xBFC5A0) or `0x1041D20()` (global flag
   0x23BA1FC, probably god-mode cheat - U); if dead (combatant+0x34 = ufo+0x53C == 2); if
   `ufo+0x76C==3 && type!=5`.
3. Player only (type 0): return if on a planet and `cSimulatorPlayerUFO::Get()+0x24 > 0`
   (invulnerability lock counter, incremented by 0xFFBF50).
4. Destroyed attacker → null. Attacker UFO of type 3 (posse/ally) → `attackerPol = player
   empire`. `attackerPol == own political id` → return (no friendly fire; turrets too).
5. `AddHostileUnit(attacker)` (vt+0x1C = 0xC37AE0, sets ufo+0x710, null-safe).
6. NPC-vs-NPC multiplier (target type ∉ {0,3} and attacker is a turret or a UFO of type ∉ {0,3}):
   `dmg *= 0x102A5F0(0x15B8510)` (tuning prop 0x679F157, default 1.0).
7. Knock-back on planets (not type 8): ufo+0x60C / +0x618, clears `mbAtDestination`.
8. Base **`cCombatant::TakeDamage` 0x00BFCEF0** (same args, `ret 0x14`): sets mpLastAttacker
   (+0x28), space damage-boost tool multiplier, stores mLastAttacker (+0x3C) and dir (+0x74);
   type 10 heals (clamped to vt+0x58), type 11 no-op, else `HP -= dmg`; if HP < 1.53e-5:
   HP = 0, state +0x34 = 2, posts **kMsgCombatantKilled 0x1622184** (combatant, attacker) via
   MessageManager 0x67DC80 vt+0x14, then `0xF249B0(gameData, type)`.
9. Target is the player: interrupts the active tool (+0x13C) and sets ufo+0x76C = 1;
   auto-retaliation through 0x102AF90 / 0xFFD5B0.
10. HP ≤ 0: player → death (§3); NPC → loot 0x102BA40, relationship hit 0x591F833 (both pols
    ≠ −1), event log, explosion effect, then **DestroyUFO 0x0102C350** (cdecl `(ufo, bool)`:
    removes from lists, stops its tool, `DestroyInstance`). Message 0x2A7C313D if the player
    was the killer.

### 2.2 Fire path (V unless noted)
* `cToolManager` player fire entry 0x01050BC0 (`thiscall(toolMgr, tool, ?, bool)`, `ret 0xC`,
  arg 2 U) → **FireTool 0x01050080**: `thiscall(cToolManager*, cSpaceToolData* tool,
  cSpatialObject* owner, const Vector3* aim, int targetDamagePointId, int arg5, Object* target,
  bool showErrors)`, `ret 0x1C`. Writes owner → tool+0x114, damage point → +0x11C, target →
  +0x118, calls strategy `WhileAiming` (vt+0x24) then `WhileFiring` (vt+0x34). Costs/UI only
  when owner is the player UFO (+0x34). Autofire variant 0x010504B0 (`ret 0x10`).
* `cDefaultProjectileWeapon` vtable 0x149C128: **WhileFiring 0x01056F10** (`thiscall(strategy,
  tool, const Vector3* aim, int)`, `ret 0xC`) creates noun 0x24270C5 (cDefaultToolProjectile) and
  calls `LaunchDefaultToolProjectile` 0x01054390 (cdecl `(tool, proj, &ownerPos, aim)`), which
  configures (vt+0x54 = 0xCB4EB0) min = `(int)(tool+0x1B4 * tool+0x144)`, max =
  `(int)(tool+0x1B8 * tool+0x144)`, radius tool+0x1AC → proj+0x52C/0x530/0x534/0x538.
* Projectile Update 0xCC2920 (cProjectile sub-object at +0x504), phase byte +0x544:
  phase 0 `0xCBB970`: on collision sets strike type/target (+0x5D0/+0x5D4) and calls strategy
  **OnHit** (vt+0x30; `cDefaultProjectileWeapon::OnHit` 0x0105B310 → `cToolStrategy::OnHit`
  0x010593B0, `ret 0x10`, only special tools/terraform/relationship effects, posts
  kMsgToolOnHit 0x56690BB - **no combatant damage here**); phase 1 **0x00CBBEF0** (`thiscall(proj)`,
  `ret`) = explosion: radius grows, damage tick every 250 ms, spatial query, each combatant hit
  once (+0x570); same political id skipped unless `kFlagDamagesFriendlies` (tool+0x174 bit 9);
  call `target->TakeDamage(min + rand(max-min), attackerPol, proj+0x538, &dir, attacker)` with
  attacker = owner (tool+0x114) cast to cCombatant, attackerPol = attacker vt+0x10 (or −1).
* Beams: AoE helper 0xCBA690 (usercall eax = centre) → 0xCB5B20 (usercall esi = target) →
  TakeDamage. Artillery-like 0xCB9940 calls TakeDamage with attacker = 0.
* Symbol fix: ported `cSpaceToolData::GetStrategy = 0x1050030` is wrong (it returns
  `strategy->ProcessCost(mUseCost)`); real GetStrategy = **0x0104F940** (`thiscall(tool)`,
  reads tool+0x178, default 0xB0DA49D9).

## 3. Enemy damage to the player, NPC fire, death/respawn

* NPC fire: **`cCombatSimulator::Update` 0x00AE7330** (vtable 0x145C838) iterates all
  cCombatants (GetData, cast 0x13F94D4); weapon = vt+0x14 (UFO: ufo+0x790), target =
  combatant+0x44 (pending +0x48); if `IsInWeaponRange` (vt+0x40) → FireTool 0x1050080
  `(weapon, self vt+8, &targetPos, aimIdx, delta, target vt+8, 1)` (autofire → 0x10504B0), then
  `target->AddHostileUnit(self)`. Hits reach the player through the same projectile →
  0xCBBEF0 → 0xC3CEA0 path with mUFOType 0.
* Player death (inside 0xC3CEA0, HP ≤ 0, type 0; P): `cSimulatorSpaceGame::Get()->0x1005020(10)`,
  sound, **0xC3C660(ufo)** (`thiscall`, `ret`): cinematic `SPG_PlayerDestroyed_PrePosse_Galaxy /
  _PrePosse_NotGalaxy / _PostPosse`, handle → ufo+0x7F0, hides ship (+0x77C=0, +0x77D=1),
  deactivates tool, event log 0x949ACE8, `(*0x16DD778)->0x100E580(attacker)`.
* Respawn (✔ runtime): UFO message handler 0xC3B6A0 (msg 0x44F1189 = a cinematic ended, or its own
  retry 0x67B2A95) with ufo+0x7F0 set → **0xC38420** (`thiscall(ufo)`, `ret`): travel to nearest owned
  star, clear +0x7F0, state +0x53C = 0, HP = max, energy = max (+0x7F4 = +0x7F8), message 0x6174B67.
  The death cinematic ends only when the player presses OK on its caption (§7.2.1).
* Sync state: `ufo+0x53C == 2` dead, `ufo+0x7F0 != 0` death explosion effect / caption running.

### Recipe for §2/§3 (hook 0xC3CEA0, thiscall 5 args, `ret 0x14`; flag `g_netApply` for replays)
Guest:
1. `this-0x508` is a mirror: if the shot is ours (`attacker == localUFO+0x508`, or
   `attackerPol == GetPlayerEmpireID()` and `type != 10`) send `HIT{npc_id, dmg, type, dir}`;
   in every case return without calling the original (host is authoritative).
2. `this` is the local UFO and the attacker is a mirror / NPC empire and `!g_netApply` → drop.
3. On `DAMAGE{dmg, type, dir, npc_id}`: `g_netApply=1; orig(localUFO+0x508, dmg, npcPol, type,
   &dir, mirror ? mirror+0x508 : nullptr); g_netApply=0` - local death/respawn then run natively.
4. On `NPC_DEATH`: clear mirror+0x588 and call orig with `(mirror+0x508, 1e6, 0xFFFFFFFF, 0,
   &dir, nullptr)` (explosion + 0x102C350; pol −1 skips relationship/event log) or call
   `0x102C350(mirror, 0)` directly.
5. Mirrors never fire: FireTool hook (§4).
Host:
1. `this-0x508` is a guest puppet (mod Defender UFO): send `DAMAGE` to that guest, then call the
   original (puppet stays invincible via +0x588; NPC aggression/AddHostileUnit keep working).
2. On `HIT` from a guest: if `npc+0x53C != 2`: `g_netApply=1; orig(npc+0x508, dmg,
   GetPlayerEmpireID(), type, &dir, puppet+0x508)`. With the puppet as attacker the NPC
   retaliates against the puppet, but the NPC-vs-NPC multiplier applies (default 1.0);
   `nullptr` avoids it but loses retaliation.
3. NPC deaths: kMsgCombatantKilled 0x1622184 or `+0x53C == 2` after the call → `NPC_DEATH`.
Open (U): whether host NPC AI targets Defender puppets at all (runtime check); meaning of
`0x1041D20`; arg 2 of 0x1050BC0.

## 4. NPC UFO per-frame update and direct pose control

Movement is done by a per-UFO *locomotion controller* object stored at `loco+0x4CC`
(= ufo+0x500), set by `cLocomotiveObject::SetController` **0x00C422F0** (`thiscall(loco, ctrl)`,
`ret 4`, AddRef new / Release old). Two UFO controllers exist:

| controller | vtable | ctor | Update (vtbl +0x0C) | installed by |
|---|---|---|---|---|
| default UFO controller | 0x0149CB20 | 0x105D1B0 | **0x01062CF0** `thiscall(ctrl, ctx*)`, `ret 4` | `cGameDataUFO` ctor **0x00C3C9B0** (every UFO) and Read 0xC37BD0 |
| "follow next pose" controller | 0x0149CB10 | 0x105D150 | 0x0105EBA0 `thiscall(ctrl, ctx*)`, `ret 4`: steers straight to mNextPosition/mNextOrientation | 0xC373B0 `thiscall(ufo)` (callers 0x1001807, 0x102C16B, 0x102C2D9, 0xDBEFE2) |

`ctx`: +0x00 Vector3 output velocity, +0x10 float dt (s; ×1000 → ms), +0x14 `cLocomotiveObject*`
(UFO = ptr − 0x34). 0x1062CF0 calls **0x1060E00** (`thiscall(ctrl, ctx, int dtMs)`, `ret 8`) when
`GetCurrentContext()==0` (planet) and **0x105F2E0** (same signature, `ret 8`) otherwise
(solar system), then integrates toward `mNextPosition + mOffsetFromPosition`.
0x1060E00 returns immediately if `byte [ufo+0x691] != 0` (flag set/cleared by AI behaviours,
meaning unknown - **U**, do not rely on it).

Decision AI ("attack", "patrol", "flee"…) is data-driven utility AI: a registry of 16-byte
entries `{hash, fnA, fnB, fnC}` at 0x0159E640… (e.g. hash 0x5872576 → 0xDC32C0/0xDC0AF0/0xDBFFD0;
functions get `cGameDataUFO*` and return float scores or fill a goal). They drive the UFO only
through `cGameDataUFO` commands: **MoveTo(dir)** 0x00C3BFB0 (`thiscall(ufo, const Vector3*)`,
`ret 4`: `mDestination = normalize(v)*mZoomAltitude`, `mbAtDestination = 0`, loco+0xDC request),
**SetZoomAltitude(f)** 0x00C3DBE0 (`ret 4`, then MoveTo(mDestination)), **MoveToPosition(pos)**
0x00C3DC20 (`ret 4`; planet → MoveTo + altitude = |pos|, system → 0xC3C120) and weapon use
(see §2). The dispatcher that iterates this registry was not found (**U**).

Direct pose control (V):
* `cGameDataUFO::SnapToDestination()` **0x00C37FA0** (`thiscall(ufo)`, plain `ret`):
  `mNextVelocity = 0; mNextPosition = mDestination; mZoomAltitude = |mNextPosition|;
  loco->SetPosition(mNextPosition + mOffsetFromPosition); loco->SetOrientation(mNextOrientation);
  mbAtDestination = 1`. So: write `mDestination` (+0x750) and `mNextOrientation` (+0x730), call
  0xC37FA0 - the controller then has nothing to do.
* Or the current puppet path: loco vtable SetPosition +0x38 (0xC37B70, also moves the abduct
  beam) / SetOrientation +0x3C / Teleport +0x44, and pin +0x718/+0x724/+0x730/+0x750/+0x74C.

Recipe for mirrors (guest):
1. Spawn with SpawnUFO (§1). Mark the pointer in a `mirrors` set.
2. Movement: hook **0x01062CF0** (controller Update; it also runs for the player UFO and for
   puppets, so filter strictly on the mirror set). If `ctx->loco − 0x34` is a mirror:
   write the received pose (`mDestination`, `mNextOrientation`, `mNextVelocity`) and call
   0xC37FA0, zero `ctx+0..8`, return without calling the original. Otherwise call original.
   (Cheaper alternative without a hook: keep doing what `space_drive` does for puppets.)
3. Firing / damage: mirrors must not shoot - hook FireTool **0x01050080** and return `al=0`
   when `owner == mirror+0x34` (NPC fire comes from `cCombatSimulator::Update` 0xAE7330, §3).
   Drop all local damage to mirrors in the TakeDamage hook (§2); real HP comes from the host
   and is written to +0x540.
4. Host side: nothing is suppressed; guest hits arrive as damage requests (§2).

## 5. Colonies

### 5.1 Placing a colony (V)
1. **`cPlaceColonyToolStrategy::OnHit` 0x0105B930** (`thiscall(strategy, tool, Vector3* pos,
   hitType, int)`, `ret 0x10`): calls `cDefaultProjectileWeapon::OnHit` 0x105B310, clears ground
   objects `0xFDC330(pos, radius)` (cdecl), then queues a delayed drop on a sequencer singleton
   (`*0x167B5EC`, lazy getter 0xB268A0 cdecl): `seq->0xAE4920(cb = 0x1058C60,
   GetPlayerEmpireID(), &pos, 0, 0xFD701F6F, 0, quat* or null, 0)` (`thiscall`, `ret 0x20`;
   null quat → `cPlanetModel::GetOrientation(pos)` 0xB7F320; node+0x04 pos, node+0x20 quat).
2. **Colony callback 0x01058C60** (cdecl `(node*)`): `activeStar->SetOwner(playerEmpire->mPoliticalID)`
   (`cStar` 0xC8B740, `ret 4` → starRec SetTechLevel(5) 0xBB9C10 + SetEmpireID 0xA16980 (+0x54));
   active planet's cGameData (planet+0xD4) vt+0x48 SetPoliticalID; then
   `cSimulatorSpaceGame::Get()+0x54` (mpHighLODPlanetSim) → **CreateColony**; city+0x2E8 =
   node+0x38 (U meaning); UI/badges.
3. **`cSimPlanetHighLOD::CreateColony` 0x00FF8FB0** (`thiscall(sim, Vector3* pos, Quaternion* rot)`
   → `cCity*`, `ret 8`, not in ModAPI): city via 0xBD9EA0(pos, 0); player cCivilization (0xB25F20,
   or new noun 0x18C816A); setup 0xBE87B0; city SetPoliticalID; star citizen species 0xBB9CD0
   (+0x74); `cPlanet::SetTechLevel(5)` 0xC70F60 → cdecl 0xC70850(record, level) (+0x194); finds
   the `cCivData` with `+0x14 == playerPol` → new cCityData (0xC4, ctor 0xFF2260) +
   `cCivData::AddCityData` 0xFF2B20, else new cCivData (0x50, ctor 0xFF3210) +
   `cPlanet::AddCivData` 0xC71230; `playerEmpire->AddPlanet(record)` 0xC34FB0; messages 0x4446533
   (first city) and 0x43F2583 (colony placed); 0xC70120(planet). Uses `GetActivePlanet()`
   internally → only for the planet the player is on.

### 5.2 Ownership / spice storage
There is no owner field in `cPlanetRecord`. Ownership = record tech level **+0x194 == 5**
(empire colony) + `mCivData` vector **+0x15C** of `cCivData` (`mPoliticalID` +0x14, `mCities`
+0x3C); star record `mEmpireID` **+0x54**, flags +0x5C (0x10 owned), tech level; `cPlanet`
cGameData base at +0xD4 (political id at planet+0x104), record ptr +0x13C; `cEmpire::mStars`
+0x88; player only: `SpacePlayerData` +0x20 `mPlayerColonies` (vector<cPlanet*>).

| function | address | conv | notes |
|---|---|---|---|
| `cEmpire::AddPlanet(cPlanetRecord*)` | 0xC34FB0 | thiscall `ret 4` | player → `SpacePlayerData::AddColony` 0x1022040 (thiscall on `*0x16DEA6C`, `ret 4`, via RecordToPlanet 0xBB5B20); then AddStarOwnership; ally → msg 0x43F2585 |
| `cEmpire::AddStarOwnership(cStarRecord*)` | 0xC342E0 | thiscall `ret 4` | mStars +0x88, mFlags +0x50 \|= 0x10, species key; each tech-5 planet → 0xC73920; msg 0x55BD8F7 |
| ConvertPlanetOwner(rec, pol) | 0xC73920 | cdecl | star SetEmpireID, rewrites every cCivData pol (0x97D0D0), recolour, live civ nouns if active |
| SetTechLevel(rec, level) | 0xC70850 | cdecl | +0x194 (getter 0xB8DB90), then star tech 0xC8B760(starRec) |
| FillPlanetDataForTechLevel(rec, level) | **0xB97040** | cdecl | (ported 0xB970A0 is wrong) level 5 → 0xB96E40(rec): cCivData with pol = star mEmpireID + a cCityData; then `rec->0xB8DA00(1)` (flag 0x40 in +0x2C) |
| cStarRecord SetEmpireID / SetTechLevel / SetFlag(mask,bool) | 0xA16980 / 0xBB9C10 / 0xBB9C40 | thiscall | +0x54 `ret 4`; tech level; +0x5C `ret 8` |
| `cPlanet::GetEmpire` | 0xC71FB0 | thiscall | empire only if record tech == 5; pol from planet cGameData |
| `cEmpire::IsAllyOf(pol)` | 0xC31D30 | thiscall `ret 4` | mAllies +0x70 vs ally +0x84 |

Spice: per city `cCityData` +0x1C `mSpiceProduction` (stored spice; Get 0xFD9420, Add (≥0)
0xFF0200, Set 0xFF0220, thiscall); `CalculateSpiceProduction(rec, remove)` 0xC70740 (cdecl) sums
cities and removes; spice type = record +0x198 `mSpiceGen` (getter 0xB8DBB0, setter 0xB8DBC0,
`cSpaceTrading::AssignPlanetSpice` 0x103CA50 `ret 8`).

Record-level colony sequence used by the game (0xFE9AF3–0xFE9B43 inside 0xFE9410, purpose U):
`empire->AddStarOwnership(star); star->SetEmpireID(id); star->SetTechLevel(5);
star->SetFlag(0x100,1); SetTechLevel(rec,5); FillPlanetDataForTechLevel(rec,5); rec->0xB8DA00(1)`.

### 5.3 Recipe: shared colonies
* Capture (any player): hook `CreateColony` 0xFF8FB0 (or callback 0x1058C60), send
  `{PlanetID (record+0x184), pos, quat}`; re-entrancy flag so replays are not re-sent.
* Apply, receiver on the same planet: `0xB268A0()->0xAE4920(0x1058C60, GetPlayerEmpireID(),
  &pos, 0, 0xFD701F6F, 0, &quat, 0)` - drop animation + full original path. Direct alternative:
  `cStar 0xC8B740(activeStar, pol)` then `GetPlanetHighLOD()->CreateColony(&pos, &quat)`.
* Apply, receiver elsewhere: `rec = cStarManager::GetPlanetRecord(planetID)` (0xBA6EF0), run the
  record-level sequence above, then `playerEmpire->AddPlanet(rec)` 0xC34FB0. Layout is then
  procedurally generated, not the host's. U: regeneration on later visit; idempotence of
  AddPlanet (it skips already-owned stars).
* Shared spice/money: one empire → treat host as authority for `cEmpire::mEmpireMoney` (+0xD0)
  and per-city `mSpiceProduction`, push periodic snapshots per PlanetID.

## 6. Raids

**ModAPI ids are swapped for this build**: `cRaidEvent` Cast compares **0x3960C0A**
(0xC5B340), GetNounID returns **0x3960C0E**. Plunder event: Cast 0x397BFF2, noun 0x397BFF3.

`cRaidEvent` (V): main vtable 0x1470350 (+4: 0x1470340, +0x34: 0x1470330), ctor 0xC5BEB0
(fields 0x1F0–0x238 match ModAPI), Cast 0xC5B340; vt +0x78 Initialize 0xC5B7B0, **+0x7C Update
0xC5E3E0** (`thiscall(raid, int dtMs)` → new state, `ret 4`), +0x80 Load 0xC5B430, +0x88
OnMissionAccept 0xC5C5E0, +0x94 OnMissionCompleted 0xC5BB70, +0x98 OnMissionFailed 0xC5BC60.
cMission fields confirmed: mMissionID +0x78, mElapsedTimeMS +0x7C, mState +0x84, owner empire
id/ptr +0x8C/+0x90, target empire id +0x94, source planet +0x9C, prop list +0x180,
mpTargetPlanet +0x1E8 (getter 0x970A90); base Update 0xC479F0.
`cMission::SetState(int)` 0xC47E00 (`ret 4`, writes +0x84, posts message); wrappers Accept(2)
0xC486F0, Complete(5) 0xC48720, Fail(6) 0xC48730, Abort(7) 0xC48710, Reject(8) 0xC48700.

Start: `cSimulatorUniverse` (singleton `*0x16DD778`, ctor 0x1015390) has an event-routine table
(0x14961A8; raid descriptor at +0x50). **Raid start 0x010125D0** (`ret 8`, `(cPlanetRecord*
colony, vector<cMission*>* out)`): `cMissionManager::Get()` 0xFEB880 →
**`CreateMission` 0xFEC420** (`thiscall(mgr, missionID, cPlanetRecord* src, cEmpire* owner,
cMission* parent)`, `ret 0x10`; class from prop 0x874B25B6, 0xA4B0A2DF → raid factory 0xFEBF70)
`(0xA4B0A2DF, colonyRec, empireOwningStar, 0)` → `SetAttackerEmpire(0)` 0xC5B520 (+0x230; 0
keeps −1 = pirates? U) → `SetTargetPlanet(rec)` 0xC472C0 → `SetNumBombers(rand)` 0xC5B540
(+0x1F8) → Accept. Variants: 0x100E620 (`ret 0xC`, `(0, rec, numBombers)`, used by the `raid`
cheat parser 0x1025200: `raid -start N | -stop | -plunder | -repair`: handy for tests);
plunder starter 0x10143E0 (class 0xC4956AA7, factory 0xFEBF90).

Update 0xC5E3E0 ("player present" = target planet is active and context 0):
* while `mWaitingForRaid` (+0x215): when arrival time passed or player present, ship list via
  0xC5C480; if present → **spawn 0xC5D2C0** (cdecl, 9 dwords: `(raid, &bgList +0x238,
  attackerPol, &pendingUFOKey +0x200, Vector3 spawnLoc +0x224 by value, bool summoned +0x234,
  bool leaveOnArrival +0x21C)`; per 0x20-byte entry (UFO type at +4) `SpawnUFO(type, &pol,
  &key)` + `ufo->SetOwnerMission(raid)` 0xC37810 (`ret 4`, ufo+0x800; getter 0xC37850)), clear +0x215.
* "still active" 0xC5DB90 (cdecl `(raid, &list)`): present → any UFO with +0x800 == raid, not
  dying (0xC373F0)/destroyed, HP +0x540 above threshold; else bgList non-empty.
* active: colony still tech 5, not owned by attacker (0xC8BAA0), owned by +0x94 → before timeout
  (prop 0x2B9E524) off-screen sim **0xC5C9E0** (cdecl 10 args `(list, planet, attackerID,
  &numBombers, &numFighters, propList, &dmgRem +0x20C, &dmgRemUFO +0x210, &destroy +0x214, dt)`)
  → return 3; else `0x102C0D0(planet)` → 6 (Failed).
* no ships left → 5 (Completed); event log 0x25594EA8.
* Completed/Failed handlers both call `0x102C0D0(planet)` (cdecl: raid UFOs leave) and
  `ApplyRelationship` 0xD06280.

### Recipe: shared raid defence
* Guests: no own universe events - hook 0x10125D0, 0x10143E0, 0x100E620 → return 0
  (alternative: the cSimulatorUniverse update, slot U: vtable 0x1496280 [4] = 0x1014CD0?).
* Host start: hook CreateMission (or end of 0x10125D0); if `Cast(0x3960C0A)` succeeds send
  `{missionID, PlanetID, attacker +0x230, bombers +0x1F8, fighters +0x1FC}`.
* Raid ships: UFOs with `+0x800 == raid` (or hook 0xC5D2C0) are ordinary hostile NPC UFOs →
  mirrored by §1/§4, guest hits go to the host by §2.
* Host end: hook Update 0xC5E3E0 post-call; return 5/6 → send result (+ city destruction /
  capture; `cSimPlanetHighLOD::DestroyCity` 0xFFA7B0 is ported, not checked).
* Optional guest UI: create a mirror raid with the 0x10125D0 steps, hook 0xC5D2C0 and 0xC5C9E0 to
  no-op, make Update return 3 until the host result, then 5/6 so the native handlers run.
* U: whether the manager applies Update's return through SetState; meaning of node+0x38.

## Open questions (all items)
* Dispatcher of the UFO utility-AI registry (0x159E640) - not needed if mirrors are pinned and
  FireTool is blocked.
* Runtime checks needed: SpawnUFO/CreateUFO null-tolerance of callers on guests; host NPC AI
  targeting Defender puppets; `+0x691` semantics; record-level colony replay on later visits.

## Co-op features (rescue, jump, raids, missions, terraform, cargo)

Static analysis only (llvm-objdump + Ghidra headless decompile, no runtime). Marks: ✔ verified in
this build's code (prologue / `ret N` / call sites / field use), ~ likely (behaviour inferred from
code, names are ours), ? unknown. [ModAPI] = name/meaning recalled from ModAPI, addresses differ.

### 7.1 Globals and getters used below (✔)

| what | address | notes |
|---|---|---|
| `SpacePlayerData*` | `[0x16DEA6C]` | +0x04 active `cPlanet*`, +0x08 active `cStar*`, +0x0C ?, +0x10 context (0 planet, 1 system, 2 galaxy, −1 transition), +0x18 player pol, +0x1C cached player `cEmpire*`, +0x20 colonies |
| GetCurrentContext | 0x01020F30 | cdecl () → int |
| GetActiveStar / GetActiveStarRecord | 0x010210E0 / 0x010210F0 | cdecl; record = `star+0x48` (null-safe) |
| GetActivePlanet / GetActivePlanetRecord | 0x01021110 / 0x01021150 | cdecl; record = `planet+0x13C` |
| GetPlayerEmpire | 0x010211B0 | cdecl → `cEmpire*` (re-cached from pol) |
| GetPlayerHomePlanetRecord | 0x01021220 | cdecl; `cEmpire::RequireHomePlanet` 0xC31870 or active planet |
| SetCurrentContext(int) | 0x01021850 | cdecl; only writer of +0x10 (called by the transitions below) |
| SetActiveStarAndPlanet(cStarRecord*, cPlanetRecord*) | 0x010218A0 | cdecl; RecordToStar 0xBB38C0 → +0x08, RecordToPlanet 0xBB5B20 → +0x04 (planet null if not in star). Low-level setter, no loading |
| `cAppModeSpace*` (space game mode, vtable 0x1494AF8, name u"cAppModeSpace") | `[0x16DA918]`, getter 0x00FD9C50 | set by ctor 0xFDC9F0; `+0x15C` = `cSimulatorSpaceGame*` |
| `cSimulatorSpaceGame*` | `[0x16DD0DC]`, getter 0x01002A70 | +0x40 = **`cPlayerInventory*`** (generic getter 0xA1AC50 = `mov eax,[ecx+0x40]`), +0x78 NPC UFO vector |
| `cSimulatorPlayerUFO*` | `[0x16DC9F4]`, getter 0x00FFBD00 | +0x24 planet-invulnerability counter (inc 0xFFBF50 / dec 0xFFBF60 / >0 test 0xFFBF40), +0x40 player `cGameDataUFO*` |
| `cStarManager::GetStarRecord(StarID)` | 0x00BA6EB0 | thiscall(starMgr 0xB3D370(), id) `ret 4`; 0 → `[+0x14C]`, −1 → null, else table `+0xC8`[(id>>12)*5][id&0xFFF] |
| `cStarManager::GetPlanetRecord(PlanetID)` | 0x00BA6EF0 | thiscall `ret 4`; star = id&0xFFFFFF, planet index = id>>24 |
| star id / planet id | starRecord+0x70 / planetRecord+0x184 | as used by `modes.cpp` (`space_place`) |
| `cEmpire::GetStars` / `GetHomeStarRecord` | 0x00C30A10 / 0x00C30DA0 | thiscall; `lea eax,[ecx+0x88]` (vector<cStarRecord*>) / home star id +0xB0 → GetStarRecord |

### 7.2 Player ship destruction → "disabled 20 s" (rescue)

Death path (✔, all inside `cGameDataUFO::TakeDamage` 0xC3CEA0, `ebp` = combatant = ufo+0x508):
1. base `cCombatant::TakeDamage` 0xBFCEF0: `HP(+0x540) -= dmg`; if `HP < 1.5259e-5` (const
   0x146A3B8): HP = 0, state +0x53C = 2, message 0x1622184. Damage boost multiplier only for the
   *attacker* side (player damage-boost tool 0x168E044), so a hit on the player loses exactly `dmg`.
   Early returns: `ufo+0x588` invincible, state already 2, planet + `cSimulatorPlayerUFO+0x24 > 0`.
2. back in 0xC3CEA0 at 0xC3D545: `if (HP <= 0 && mUFOType == 0)` → `cSimulatorSpaceGame->0x1005020(10)`,
   sound 0xD456D958, unless adventure/scenario checks (0x1015C90 → 0xA97F80 / 0xFF02D0) call
   **`cGameDataUFO::PlayerDestroyed` 0x00C3C660** (thiscall(ufo) plain `ret`: cinematic
   `SPG_PlayerDestroyed_*`, handle → +0x7F0, hides ship +0x77C=0/+0x77D=1), then event log
   0x949ACE8 and `cSimulatorUniverse(*0x16DD778)->0x100E580(attacker)`.
   Second caller of 0xC3C660: 0x1016948 (inside 0x10167F0, when the player UFO has HP ≤ 0 after an
   adventure/sequence ends) ~.
3. Cinematic end (✔ runtime: the player's OK on the death caption, §7.2.1) → UFO message handler
   0xC3B6A0 (msg 0x44F1189 / 0x67B2A95, `ufo+0x7F0` set) → **Respawn 0x00C38420** (thiscall(ufo), `ret`): picks the owned star (cEmpire::GetStars) nearest to
   the active star (fallback home star), travels there with the §7.3 sequence (+ landing 0xFE0E00),
   message 0x6174B67, clears +0x7F0, `state = 0`, **`cCombatant::SetHealth` 0x00F92480**
   (`thiscall(combatant, float)`, `ret 4`, writes combatant+0x38 = ufo+0x540) with
   GetMaxHitPoints (combatant vt+0x58, ST0), energy +0x7F4 = max +0x7F8.

Healing (✔):
* **`cCombatant::AddHealth(float)` 0x00BFD2C0** (thiscall(combatant), `ret 4`): if amount > 0:
  HP += amount clamped to max, **state +0x34 (ufo+0x53C) := 0** (posts 0x1622184 if it changed).
  Not blocked by the invincible flag → the right call to revive a disabled ship.
* `TakeDamage(..., type 10)` heals too but is blocked by invincible +0x588 (checked in the base).
* Repair tools: `cRepairToolStrategy::OnHit` 0x0105B140 (thiscall(strategy, tool, pos, hitType, int),
  `ret 0x10`) heals the **owner** (tool+0x114) by `maxHP * tool->0xCE6AB0() * [0x13EB950]` through
  AddHealth; `cEnergyRepairToolStrategy::OnHit` 0x0105B0B0 refills energy via 0xC38B30;
  **`cRepairAOEToolStrategy::WhileFiring` 0x01057FF0** heals the owner and every UFO in range that
  belongs to the player empire or an ally (0xD01FE0) with `combatant->TakeDamage(amount, playerPol, 10,
  &zero 0x16E2AF4, nullptr)` (vt+0x18) + effect 0xC38B00(2,1). So a partner's Repair-AoE already
  "hits" our puppet on the host with type 10 → a natural rescue trigger.

Recommended hook (extend the existing TakeDamage hook in `squadron.cpp`):
* `this == localUFO+0x508`, `type != 10`, partner in the same system, not disabled:
  if `HP - dmg < 1.5259e-5` → call the original with `dmg = HP - 1` (or skip it) so HP stays ≥ 1;
  set disabled (20 s timer), `ufo+0x588 = 1` (no further damage), block FireTool (owner = local)
  and the controller Update 0x01062CF0 for the local UFO (zero `ctx+0..8`, return) so it drifts dead.
* Rescue: on partner Repair-AoE (puppet hit with type 10 on the host → message to the owner) or
  proximity: `ufo+0x588 = 0; AddHealth(ufo+0x508, max*0.5)`.
* Timeout: `ufo+0x588 = 0`, `SetHealth(ufo+0x508, 0)`, `ufo+0x53C = 2`, then call
  `0xC3C660(ufo)` → native cinematic and respawn at the nearest colony (also call
  `cSimulatorSpaceGame->0x1005020(10)` / event log only if you want the full native side effects ~).
* Alternative single hook: `0xC3C660` itself (skip while rescue is possible). Simpler, but by then
  state = 2, the death sound/event log already ran ~.

### 7.2.1 Death and respawn captions (✔ runtime, lane 4, 2026-10-22; test `coop_space_death.py`)

Found while fixing "after a lone death the respawn did not come within 60 s" (branch
fix/space-lone-death). Not a hook problem: the debug hit (attacker null) and the rescue timeout
(`end_down(false)` → PlayerDestroyed) behave exactly like a native death.
* `PlayerDestroyed` 0xC3C660 starts the cinematic `SPG_PlayerDestroyed_PrePosse_NotGalaxy` (in a
  system; `_Galaxy` in the galaxy view, `_PostPosse` with a posse) through
  `cCinematicManager::PlayCinematic` 0xAE0880 (manager `[0x167FB30]`, getter 0xB3D5A0), after
  `0xAE0900(mgr, 0x14A4C5F5, &params, 0)`. ufo+0x7F0 is the handle of the explosion effect
  (`0x67DD90()` vt+0x2C, effect 0x4D42643), not the cinematic itself.
* The cinematic shows the caption "Your <ship> has taken so much damage that it has been destroyed.
  Thanks to advances in cloning technology you have not died. Instead, you return to your
  homeworld, a new spaceship at the ready." with a green check button and **waits for the player**:
  nothing ends it on its own (60 s+ in tests). The check button or Enter posts 0x44EE075 to the
  cinematic manager → EndCinematic 0xADFF20 → message 0x44F1189 (data+8 = the ended cinematic id)
  → 0xC3B6A0 → Respawn. The old test that never pressed OK waited forever (test artefact); a player
  presses OK. Tests press it with `key enter` (works in background mode).
* 0xC3B6A0 re-posts 0x67B2A95 to itself instead of respawning while `cSimulatorSpaceGame+0x50`
  has any bit set (0xB18D50). Posting 0x67B2A95 (MessagePost, IMessageManager vt 0x18, 4 args) is
  the native way to ask for the respawn again.
* Respawn lands on the colony planet (0xFE0E00) and at once plays the respawn caption
  **CVG2SPG_short** ("We've built you a new ship, try not to break it this time!", from 0xFF5F66;
  `CVG2SPG` in the other branch). Its text appears after ~15-30 s and waits for OK too. Cinematic
  manager +0x2C = 2 while any cinematic plays, 0 otherwise.
* **The game fault behind a ship stuck dead:** the end of CVG2SPG(_short) is also handled by
  `cSimulatorSpaceGame` 0x100B730 (at 0x100C513): it sets player UFO +0x6B4/+0x6B8 = 1.0 and calls
  `[cSimulatorSpaceGame+0x54]->0xFF59D0` (`+0x1E4` → 0x7CFCC0). +0x54 is set on a planet and null in
  a system (~, runtime reads). If the ship leaves the planet while the respawn caption still plays,
  the end of that caption faults there (AV at 0x7CFCC0, data 0x1E4, stack holds 0x100C57F and
  044F1189), the dispatch of 0x44F1189 stops, and a ship that died in the meantime never gets its end
  message: dead (state 2, +0x7F0 set), no cinematic running, stuck forever. A death started while
  another cinematic plays does not replace the running id (+0x148), so the OK on the death caption
  ends the respawn caption. The mod's jump (`travel_to`) did exactly that: it only checked +0x7F0.
* Fix in `spacecoop.cpp`: no jump while `cCinematicManager+0x2C != 0`, and a watchdog: own ship in
  state 2 with +0x7F0 set but no cinematic running for 5 s → MessagePost 0x67B2A95 → native
  Respawn (`respawns_forced` in `coop.status`; the old path is reproduced by
  `coop.space jumpto <star> force`).
* **The fault itself, fixed natively (✔ runtime, lane 7, 2026-10-23, branch fix/space-death-fault):**
  0x7CFCC0 is a generic `thiscall(holder)` "clear a set" (`mov ecx,[ecx]; jmp 0x7CFC10`, the impl
  has vectors at +4/+8, +0x1C, +0x34, +0x4C, +0x64). The caller is
  `cSimPlanetHighLOD_EndRespawnCaption` **0xFF59D0** (`add ecx,0x1E4; jmp 0x7CFCC0`), the only two
  callers of which are 0x100C57A (end of CVG2SPG_short) and 0x100C5E7 (end of CVG2SPG), both with
  `ecx = [cSimulatorSpaceGame+0x54]` (mpHighLODPlanetSim) and no check. The set at sim+0x1E4 is filled
  by 0xFF5D90 (only caller 0xFDBBF8, after the respawn landing): it adds 0x9C377C26 there (0xFF5EE2)
  and plays the caption. Off the planet the sim (and the set) is gone: nothing to clear.
  `gamefixes.cpp` hooks 0xFF59D0 and skips it when `this` is null ("respawn caption without a planet
  N" in `coop.status`). The handler then returns normally, the dispatch of 0x44F1189 goes on, and the
  UFO handler 0xC3B6A0 respawns the dead ship natively 2 s after the OK (the watchdog does not fire:
  `respawns_forced=0`). Crash stack of the old fault: `0100C57F 007E30B6 00C60660 00922514 0088564C
  ...` (message dispatch). Before the fix the forced-jump step of `coop_space_death.py` faulted in 4
  of 4 runs (it cleared the fault and passed); now the test fails on any fault of either game.
* Calling Respawn 0xC38420 directly on a stuck ship also works (console `call`), but the message
  keeps the game's own "busy" check.
* Not tested: a lethal hit by a real NPC while alone (the debug hit has no attacker; the attacker
  only reaches the event log and 0x100E580).

### 7.3 Travel / "jump to partner"

Rewritten 2026-10-23 (branch fix/space-jump, ✔ runtime on lane 7, GOG 2016, test
`coop_space_jump.py`). The 0.6 jump (respawn's sequence below) was wrong for any star whose planets
were never generated: see "Why TravelToStarSystem is not enough".

| function | address (both builds) | signature | notes |
|---|---|---|---|
| **`SpaceTeleportTo(cStarRecord*)`** | 0x010228C0 | cdecl, `ret` ✔ | the `space -flyto …` debug cheat target. **No-op unless context == 2 (galaxy)**. Adds the flight cost to the money first (`cEmpire::SetMoney(GetMoney() + cost)`, cost 0xFFC390), then `cSimulatorPlayerUFO::TravelToStar` and `SnapToDestination` 0xC37FA0: a free, instant galaxy flight |
| `cSimulatorPlayerUFO::TravelToStar(cStarRecord*)` | 0x01002040 | thiscall(playerUFOsingleton) ✔ | the player's galaxy flight. No-op when the game time is paused (`cGameTimeManager+0x48 & 1`) or dst is the active star. Cost 0xFFC390(cur, dst), "not enough money" 0x3E7E341, `AddMoney(-cost)`; relationship event 0x5B942D0 for an uncontacted empire star; old star +0x44 = 1 (visited); `SetActiveStarAndPlanet(dst, 0)`; energy: `ChangeEnergy(-x)`, **with no energy left it damages the ship** (combatant vt+0x18); **0xBB5920(starMgr, dst, 0) = RequirePlanetsForStar + per-planet setup**; relationship events 0x1014920(pol, 4/2) for empires nearby; posts 0x55BD8F7, 0x6174B67 |
| `cAppModeSpace::SystemToGalaxy()` (`SporeCoop::cAppModeSpace_SystemToGalaxy`) | 0x00FDA5D0 | thiscall(mode), `ret` ✔ | system → galaxy view: sim+0x50 \|= 4, DestroyNPCUFOs(1,1,0), Space_LeaveSolarSystem(active star), context 2 (0x1003330), 0x1007FA0, places the galaxy UFO, posts **0x5E37A3B** (handler 0xFDABE0: the tutorial's "entered the galaxy" caption SPG_EnteredGalaxy_201 once, hints). Callers: the zoom-out in the per-frame camera handler 0x1009C90 (0x1009EBC), message 0x6527EAF (0xC77EB0) |
| `cAppModeSpace::EnterSolarSystem()` | 0x00FDD590 | thiscall(mode), `ret` ✔ | galaxy → system of the active star: 0x10034C0 (leave the galaxy gfx), context 1 (0x10038F0), DestroyNPCUFOs(1,1,0), 0x1007680; for a star of tech level 5: `GetEmpireForStar`, empire+0x50 \|= 0x10 (0xC31D70), relationship event 0x1014920(pol, 3), preloads the empire's creations; **0xBB5CE0(starMgr, star, &vec): creates the `cPlanet` objects** (0xBAF2A0, into cStarManager +0x50) after 0xBB5920; picks the active planet (0xFDD1F0) when none; places the UFO at the system entry point; rare-star badge; posts **0x5E37A44** (cAppModeSpace handler 0xFE2EB0: no active planet → 0xFDADD0, else +0x188 = 1). Callers: Respawn (galaxy case), zoom-in handler 0x100CF70 |
| 0x00FDADD0 | | thiscall(mode) | after 0x5E37A44: for an NPC empire's star, once per game (flag 0xDAB99813): creates the empire's envoy UFO (type 8) next to the player and plays **SPG_FirstContact_Intercept** ("Drake's equation was right..."); SETI ambush; "first alliance possible" hint |
| **`cAppModeSpace::TravelToStarSystem(cStarRecord*, bool keepPosse)`** | 0x00FDD380 | thiscall(mode), `ret 8` ✔ | system → other system without the galaxy view: DestroyNPCUFOs(0, keepPosse, 0), Space_LeaveSolarSystem, `SetActiveStarAndPlanet(star, 0)`, picks a planet (0xFDD1F0 over the star's cSolarSystem +0x10..+0x14) and places the UFO there (0xC3C2D0 + 0xC38320), context 1. **No planet generation, no cPlanet creation, no 0x5E37A44.** Callers: Respawn (keepPosse 0, to an own star), Return-home tool 0x1051D98 (1), wormhole 0xFDD4E0 (1) |
| `cAppModeSpace::LeavePlanet(bool)` | 0x00FDE3D0 | thiscall, `ret 4` ✔ | planet → system (context 1 via 0x10038F0); respawn and Return-home pass 0 |
| `cStarManager::RequirePlanetsForStar(cStarRecord*, ?)` | 0x00BB4C60 / 2024 0x00BB4C90 | thiscall `ret 8` ✔ | generates the planet records when star+0xAC (count) is 0: the only caller of `GeneratePlanetsForStar` 0xBB4270, which draws from the global RNG **0x1602760** (2024 0x1601760; one dword LCG, `Math::RandomNumberGenerator::global_ptr`) |
| respawn landing | 0x00FE0E00 | thiscall, `ret` ~ | only caller is respawn: lands on the active planet |
| cSimulatorSpaceGame context switches | 0x01003330 → 2, 0x01003530 → 0, 0x010038F0 → 1 | thiscall ✔ | low level, called by the mode functions above |
| `cSimulatorSpaceGame::DestroyNPCUFOs(keepAmbush, keepPosse, bool)` | 0x01009310 | thiscall `ret 0xC` ✔ | iterates +0x78, DestroyUFO 0x102C350 |

**Why TravelToStarSystem is not enough (the root cause of the 0.6 bug, ✔ runtime).** A player's flight
is galaxy view → `TravelToStar` (generates the planet records of the target, 0xBB5920) → zoom in →
`EnterSolarSystem` (creates the cPlanet objects, 0xBB5CE0, and posts 0x5E37A44 for first contact).
`TravelToStarSystem` does neither: it only works for stars that are already prepared, which is all its
native callers need (respawn and Return-home go to own stars, whose planets are loaded with the save;
wormhole targets are prepared at load by 0xBB7770, which runs 0xBB5CE0 for the stars in cStarManager
+0xDC and +0xF0). The 0.6 jump into a star nobody visited arrived with star+0xAC = 0 (no records), active
planet 0, only the sun drawn and the old cPlanets in +0x50; the empire never saw the player. Zooming out
and back in afterwards generated the records (3) but the system stayed empty (active planet 0, no
targets) for the rest of the session ? (likely the cStar's cSolarSystem, created empty by 0x10038F0's
`cStar::GetSolarSystem` 0xC8B8F0, is never rebuilt). Reproduce with `coop.space jumpto <StarID> force old`.

**The co-op jump (`spacecoop.cpp travel_to`, ✔ runtime both from a system, a planet and the galaxy view,
into unvisited NPC and empty systems):**
```
guard: no cinematic (ufo+0x7F0 == 0 and cCinematicManager+0x2C == 0) unless "force"; context >= 0
mode = *(void**)0x16DA918;  dst = cStarManager::GetStarRecord(StarID) (checked against the table)
if dst == GetActiveStarRecord(): context 2 -> EnterSolarSystem(mode); else "already there"
case 0: PlanetModelCleanup(*0x167FADC, GetActivePlanet()); LeavePlanet(mode, 0)     // planet -> system
case 1: skip the next 0xFDABE0 (5 s); SystemToGalaxy(mode) 0xFDA5D0                // system -> galaxy
case 2: e = ufo+0x7F4; ufo+0x7F4 = max(e, ufo+0x7F8)   // TravelToStar spends energy / damages at 0
        SpaceTeleportTo(dst)                     0x10228C0  // free (cost refunded), instant, generates
        ufo+0x7F4 = e
        ok = GetActiveStarRecord() == dst       // TravelToStar refuses while the game time is paused
        EnterSolarSystem(mode)                   0xFDD590   // into dst (or back into the old star)
```
All of it runs in one call outside the frame (`coop.space` is SK_CMD_OUTSIDE_FRAME, the Space page
buttons use run_outside_frame); the posted messages are handled in the next frames, in the target system.
Effects seen: the target's planets in cStarManager +0x50 and drawn, an active planet of that star, the
empire's ships (envoy type 8, defenders type 6), the empire's contact entry gets flag 0x20 at once and
the comm card shows at the bottom left; the first NPC system of a game plays SPG_FirstContact_Intercept
(caption, OK). **The galaxy pass is hidden:** the 0x5E37A3B posted by SystemToGalaxy is handled one frame
later by 0xFDABE0 (`SporeCoop::cAppModeSpace_OnGalaxyEntered`, the player's first look at the galaxy): in
a tutorial save it played SPG_EnteredGalaxy_201 ("The interstellar drive from the wreck is responding to
a beacon...", waits for OK), also in the `space` fixture without the drive, and that caption then
blocked the next jump (coop_space_play.py and coop_space_page.py failed). `spacecoop.cpp` skips that
handler once within 5 s after its own SystemToGalaxy (`galaxy_msgs_skipped` in `coop.space`); a real
zoom-out later shows it as usual. The contact itself (cPlayer +0x11D8 flags 0x26, bit 1) comes when the player talks to the empire:
the comm tab above the empire's card (288, 914 in a 1440x1080 window), an answer, Goodbye. Money and
energy are unchanged. The planets of the player's colonies in other systems stay in +0x50 (the game keeps
them); `coop.targets all` lists only the active star's planets.

**Same planets in both games (✔ runtime):** `GeneratePlanetsForStar` 0xBB4270 (only caller
`RequirePlanetsForStar`) and its callees (planet kinds, orbits, scores, names) draw from the global RNG
`0x1602760` (2024 `0x1601760`), so the host and the guest generated different planets for the same
unvisited star (0046101B: 4 and 6, then 1 and 8 planets in two sessions). What did not work: seeding the
global RNG for the generation (mostly the same, once 6 and 10: other threads draw from the same object,
and the planet names `cSpaceNames::GenerateRandomName` 0x5ED000 draw a number of times that depends on
the names the game made before); only the generator's own five draws on a private generator (5, 6, 8,
10 planets: the callees decide the structure too). What mostly works (8 of 8 probe sessions the same 6 planets, both
games): `spacecoop.cpp` hooks `RequirePlanetsForStar` (generation when star+0xAC == 0), `RandomInt`
0xA68F30 / `RandomFloat` 0x936040 and `GenerateRandomName`: while the generation runs, every draw on the
global RNG **from the generating thread and outside the names** goes to a private generator seeded with
`StarID * 0x9E3779B1 ^ bits(x) ^ rot16(bits(z)) | 1` (star+0x3C/+0x44, the same in both games); the
global generator is untouched (`planets_seeded` in `coop.space`). Not complete: in one
`coop_space_jump.py` run the host still made 10 planets of 0046101B and the guest 6 (docs/known-issues.md).
The names may still differ (? not compared). Stars near home already have their planets in the save.
? spice kinds and the NPC colonies' data (FillPlanetDataForTechLevel 0xB97040) were not compared.
* `coop.targets all` only prints objects on the screen; `coop.space planets` prints every loaded planet
  of the active star (PlanetIDs, cStarManager +0x50).

Partner's star: send `starRecord+0x70` (StarID), resolve with `cStarManager::GetStarRecord` 0xBA6EB0
(same galaxy layout → same ids).

### 7.4 Raids and plunder (builds on §6)

* All missions/events are nouns 0x2AA5ADA. Enumerate (✔): `vec = thiscall(0xB21260, nounMgr,
  0xCD7E80, 0xD3D3D0, 0xFECEA0, 0xB1E430, 0x2AA5ADA)`, iterate `[ret+4]..[ret+8]`.
* Mission ids (+0x78) handled by cRaidEvent::Update 0xC5E3E0: 0xA4B0A2DF (pirate raid),
  0xC4956AA7 (raid plunder), 0xD646C543, 0x77184BAE (? other raid-type events).
* **Raid plunder** = separate class `cRaidPlunderEvent` (✔): noun 0x397BFF3, Cast 0x397BFF2
  (0xC5B0C0, chains to cMission Cast, *not* to cRaidEvent), vtable 0x1470168: Update **0xC5DC60**,
  Accept 0xC5B260, Completed 0xC5B2E0, Failed 0xC5B300. Starter 0x010143E0 (`ret 8`,
  `(cPlanetRecord* colony, vector<cMission*>* out)`, event-table routine, no direct callers):
  CreateMission(0xC4956AA7, colony, owner of star, 0), Cast, fills +0x200/+0x204, **event log
  0x31DF9AC0 (table 0x131A9F54) = "Raid Plundering occurred"** via ShowEvent 0xDD8640,
  SetTargetPlanet 0xC472C0, Accept. Cheat variant 0x01012940 (`ret 8`, `raid -plunder`).
  Update: waits for the player (spawns ships via 0xC5D2C0 like the raid), while ships alive and
  before timeout (prop 0x2B9E524) steals spice from the target planet (0xC71150 with a negative
  amount ~) → 3; no ships left → event 0x25594EA8 + 5 (Completed = defended); timeout →
  0x102C0D0 (ships leave), event 0xB681FBA2, → 6 (Failed = plundered).
* State machine `cMissionManager::Update` 0x00FEF240 (thiscall, `ret 8`, ✔): per mission by
  `GetState` (vt+0x60 = `[+0x84]`): 2 → OnAccept (vt+0x88), SetState(3); **3 → `r = Update(dt)`
  (vt+0x7C); if r ≠ 3 → SetState(r)**; 5 → OnCompleted (vt+0x94) → SetState(9 if +0x17C else 1);
  6 → OnFailed (vt+0x98) → 1; 7 → Abort (vt+0x90) → 1; 8 → Reject (vt+0x8C) → 1; 1 → removed (0xFEDB30).
* Detect a raid on planet P (✔): mission with Cast(0x3960C0A) (raid) or Cast(0x397BFF2) (plunder),
  `[+0x84] == 3`, target `cPlanet*` `[+0x1E8]` (getter 0x970A90; record = `planet+0x13C`,
  PlanetID = `record+0x184`). Generic: `cMissionManager::ThereIsEventInPlanet` 0x00FEE020
  (thiscall(mgr 0xFEB880()), `cPlanetRecord*`) `ret 4` → any active mission with flag `+0x130 & 1`
  (event) whose vt+0xF4 (0xC45EC0: `target->+0x13C == rec`) matches.
* Start/end hooks: start = `cMissionManager::CreateMission` 0xFEC420 post-call (`ret 0x10`) with id
  0xA4B0A2DF / 0xC4956AA7; end = **`cMission::SetState` 0xC47E00** (thiscall(int) `ret 4`) with 5
  (defended) / 6 (lost/plundered) on a raid/plunder mission; or the class handlers 0xC5BB70 /
  0xC5BC60 (raid) and 0xC5B2E0 / 0xC5B300 (plunder).
* Co-op: the guest at the raided colony sees host-mirrored raid UFOs (they are normal NPC UFOs with
  `ufo+0x800 == mission`); a guest's colony raid can't be mirrored from the host's save (different
  colonies) - to let the partner help, the owner of the raided planet stays authoritative and the
  helper "jumps" there (§7.3); helper hits on raid UFOs go through the §2 host path only if the owner
  is the host ?.

### 7.5 Missions

cMission fields (✔ unless noted): `mMissionID` +0x78 (template prop instance), elapsed ms +0x7C,
state +0x84, giver empire id +0x8C / cached `cEmpire*` +0x90 (`GetOwnerEmpire` 0xC45320), target
empire id +0x94, source planet +0x9C, **reward money (float) +0xA0**, reward tool ResourceKey +0xA4,
extra reward tool keys vector<ResourceKey> +0x11C..+0x120, flags +0x130 (bit0 = event ~),
+0x17C next step (non-zero → state 9), props +0x180, `mpTargetPlanet` +0x1E8.
`IsActive` 0xC44DC0 (state == 3). States: 0 ?, 1 finished/removed, 2 accepted, 3 active, 4 ?,
5 completed, 6 failed, 7 aborted, 8 rejected, 9 completed-with-follow-up ~.
Wrappers (thiscall, `ret`): Accept 0xC486F0 (2), Reject 0xC48700 (8), Abort 0xC48710 (7),
**Complete 0xC48720 (5)**, **Fail 0xC48730 (6)**. Base vtable `cMission` 0x146DCC8 (Update +0x7C
0xC479F0, OnCompleted +0x94 **0xC46CC0**, OnFailed +0x98 0xC46B30).
Base OnCompleted pays: reward tools via `cPlayerInventory::AddItemByKey` 0xFF4490 (§7.7),
`cEmpire::AddMoney((int)+0xA0)` on the player empire, relationship +/− with the giver (0xD06280,
pol 0x526E519), stats/sounds.
Player's missions: enumerate mission nouns (§7.4), `state == 3 && !(flags & 1)`; ? which one the UI
tracks as "current".
Feasibility of "a mission taken by one counts for both": missions exist only in the taker's save
(own galaxy save per player), so the partner cannot complete *the same object*. Feasible variant:
hook `SetState` 0xC47E00 (value 5/6) or OnCompleted, send `{missionID, money +0xA0, tool keys,
giver pol}` and pay the partner with AddMoney / AddItemByKey (relationship only if political ids
match across saves - ? same galaxy seed should give the same ids). Objective sharing (e.g. partner
kills/scans counting) would need per-mission-class hooks - not recommended.

### 7.6 Terraforming

Tool effect = `cToolStrategy::OnHit` 0x010593B0 (✔ `ret 0x10`, 4 stack args
`(cSpaceToolData* tool, const Vector3* pos, int hitType, int)`, ecx unused). Per hitType 0..3 it reads
three float deltas from the tool (hitType 1: +0x26C/+0x270/+0x274, 2: +0x278.., 0: +0x284.., 3: +0x290..;
getters 0x104C050..0x104C100), then only in planet context posts kMsgToolOnHit 0x56690BB and, with
`tm = cTerraformingManager::Get()` 0xB3D500 (`[0x167FB10]`) and `p = GetActivePlanet()`:

| function | address | effect |
|---|---|---|
| `tm->AddAtmosphereDelta(cPlanet*, float)` ~name | **0x00BBC480** thiscall `ret 8` ✔ | `p+0x154` (terrain, null if not loaded) `+0x1FD4 += d`; the terrain update 0xC7FC40 integrates it into **record+0xB0** over time |
| `tm->AddWaterDelta(cPlanet*, float)` ~name | **0x00BBC4B0** thiscall `ret 8` ✔ | only if p is active and context 0: `cPlanet::SetScoreB8` 0xC71F20(Get 0xC71EB0 + d) → record+0xB8 clamped to the range from 0xFC20A0(B0,B4); `tm+0x64 = 0` |
| `tm->AddTemperatureDelta(cPlanet*, float)` ~name | **0x00BBC500** thiscall `ret 8` ✔ | terrain `+0x1FD8 += d` → record+0xB4 |
| `CalculateTScore(float b0, float b4)` | 0x00FC2040 cdecl → int 0..3 ✔ | distance from the centre; so +0xB0/+0xB4 are the two T-score axes (atmosphere/temperature [ModAPI]) |
| `tm->GetTScore(cPlanetRecord* or null=active)` | 0x00BBC7B0 thiscall `ret 4` ✔ | |

Recommended: hook the three appliers (one place for all terraform tools). When the local player
calls them on the active planet, send `{PlanetID, which, d}`; a partner on the same planet replays
with the same function (its own copy of the planet) → both planets move at the sum of both
players' rates. Optional bonus multiplier `d *= 1.5` while a partner is on the planet. Partner
elsewhere: apply to `GetPlanetRecord(id)+0xB0/+0xB4` directly (not loaded → no terrain object).

### 7.7 Inventory: tools, charges, spice cargo, money

`inv = *(cPlayerInventory**)(*(uint8_t**)0x16DD0DC + 0x40)` ✔. `cPlayerInventory` vtable 0x14958A0
(ctor 0xFF5040) derives from `cSpaceInventory` (vtable 0x14740B8, ctor 0xC87CA0): items
`vector<cSpaceInventoryItem*>` +0x34..+0x3C, max stack +0x48 (−1 = unlimited, setter vt+0x58).

| vt | address (player / base) | signature | conf |
|---|---|---|---|
| +0x5C | 0xC873C0 | `int GetItemCount()` | ✔ |
| +0x60 | 0xC873D0 | `cSpaceInventoryItem* GetItem(int index)` `ret 4` | ✔ |
| +0x64 | 0xC873F0 | `int FindItem(int itemType, const ResourceKey* key)` `ret 8`, −1 if none | ✔ |
| +0x68 | 0xC87170 | `GetItemByKey(key)` = type 2 (tool) lookup `ret 4` | ✔ |
| +0x70 / +0x74 | 0xC874A0 / 0xC871A0 | `bool HasItem(type, key)` / `HasTool(key)` | ✔ |
| +0x78 | 0xFF3FD0 / 0xC87F00 | `bool RemoveItem(cSpaceInventoryItem*)` `ret 4` | ✔ |
| +0x7C | 0xFF40B0 / 0xC87F70 | `bool TakeItem(int index, cSpaceInventoryItemPtr* out, int count)` `ret 0xC`: count −1 or ≥ stack → removes the whole item into *out; less → splits (`item+0x10 -= count`); tools with ammo move charges (ConsumeAmmo/AddAmmo) | ✔ (split branch ~) |
| +0x80 | 0xFF4780 / 0xC881E0 | **`bool AddItem(cSpaceInventoryItem* item, cSpaceInventoryItem* cargoSlot /*null = find free*/, bool apply)`** `ret 0xC`: same key+type stacks (`count += item.count`, capped by +0x48), cargo types 0/1/4/5/6 need a free cargo slot (type 3) else returns 0; tools: ammo merge | ✔ |
| +0x88 / +0x8C / +0x90 / +0x94 | 0xC875A0 / 0xC87550 / 0xC875D0 / 0xFF39E0 | cargo slots: count / free count / `GetFreeSlot(out*)` / `AddCargoSlots(n)` | ✔ |

`cSpaceInventoryItem` (size 0x7C, ctor 0xC877E0): +0x0C tool sub-object, an embedded base, not a pointer (`(item+0xC)->vt[3]` = Cast(0x707459F0) →
`cSpaceToolData*`, no AddRef; 10.4), **+0x10 count**, **+0x14 item type** (2 tool, 3 empty cargo slot, 4 trading
object / spice; 0/1/5/6 other cargo - plant/animal/rare ~), +0x18 price, +0x1C non-stackable,
key via vt+0x18 (returns `ResourceKey*`), vt+0x14 SetKey.

Tools (✔): `cPlayerInventory::GetTool(const ResourceKey*)` **0xFF3BF0** (thiscall `ret 4` →
`cSpaceToolData*` or null); `cSpaceToolData` charges **+0x7C** current (getter 0xA02C10), +0x1A0
max, +0x19C use cost, uses-ammo flag `+0x174 bit 5` (0x104CBD0); **AddAmmo(int)** 0x0104CBE0
(`ret 4`, clamps to max), **ConsumeAmmo(int)** 0x0104CC10 (`ret 4`, −1 = use cost, no clamp).
New tool: **`cPlayerInventory::AddItemByKey(const ResourceKey*)` 0xFF4490** (`ret 4`, loads via
`cToolManager::LoadTool` 0x104E350, AddItem(item,0,1), event log 0x15E1A624) - used by mission rewards.

Spice (✔ from `space -addCargo` and the colony-collect code 0x103E6F0): spice is a trading object
(type 4) keyed by the planet's spice key `record+0x198` (getter 0xB8DBB0). Add N:
```
cSpaceInventoryItem* it = nullptr;
thiscall(0x103A490, cSpaceTrading::Get() /*0xB3D4A0*/, &it, &spiceKey, N, 0.0f /*price*/); // CreateTradingObject, ret 0x10
vcall(inv, 0x80/4, it, nullptr, true);   // stacks onto existing spice of that key, else needs a free slot
it->Release();                            // vt+0x08
```
Remove N: `i = vcall(inv, 0x64/4, 4, &spiceKey); vcall(inv, 0x7C/4, i, &out, N); out->Release()`.
Read amount: `GetItem(i)->+0x10`.

Money (✔): `cEmpire` +0xD0 (int, 0..99 999 999): GetMoney 0xC30DF0, **AddMoney(int)** 0xC31B40
(`ret 4`, clamps), **SetMoney(int)** 0xC31BA0 (`ret 4`). Player empire = GetPlayerEmpire 0x10211B0.
Sending money/spice/tools between players = subtract locally with the functions above, send, add
on the receiver; all safe in any context (no planet needed).

### 7.8 Open (?)
* Exact meaning of OnHit hitType 0..3 and of record+0xB8 (water [ModAPI] ?).
* Whether `TravelToStarSystem` is safe during comm screens / cinematics; FE0E00 details.
* Mission state 0/4 meaning; how the UI picks the tracked mission.
* ~~Political ids identical across two saves of the same galaxy~~: yes for the copies of one save (§12.3).

### 7.9 The galaxy menu intro left playing after a load: the guest "movie" (✔ runtime lanes 10 + 4, Ghidra both builds, 2026-10-23, branch fix/space-guest-movie)

Symptom (seen on GOG 2024 lanes 1, 9, 10): a guest that joined a host in Space sat in a HUD-less
state (only the starfield), `coop.space hp` showed `movie=1`, Enter did nothing, the co-op window
opened with F9 but real clicks on it did nothing, and its jumps answered "a cinematic is playing".
* **What plays:** `cCinematicManager` (`[0x167FB30]` 2016 / `[0x167EB50]` 2024, same layout) +0x2C = 2,
  +0x148 = **0x8915F9BB = FNV hash of "GGE_Intro"**: the galaxy menu's own intro cinematic (the camera
  flight over the galaxy behind the main menu), not anything of Space. `cGameInputManager`
  (0xB3D320 / 0xB3D350) +0x110 = 1: the input lock every cinematic holds while it plays, so the
  game drops the clicks (the co-op window opens on the F9 key, which goes through the mod's own hook).
* **How the intro runs (both builds, identical):** `Movie::MovieSystem::StopMovie` posts 0x49731AD
  when the start-up video ends (~16 s after the galaxy menu appears in the test lanes, muted and
  unwatched); the menu's handler `GalaxyGameEntry_IntroMessages` (0xDEA870 / 0xDEA820, state at
  `[0x16A2324]+0x1EC` / `[0x16A1344]+0x1EC`) then plays "GGE_intro" (state 5) unless the video was
  skipped with Escape (`CheckEscape` posts 0x61D72DE: state 3, no intro). The intro lasts ~10 s;
  its end message 0x44F1189 (data+8 = the GGE_Intro id, from EndCinematic) or 0x66F6B6F (skip)
  make the handler stop it (state 6, posts 0x66F8F25). Measured (a guest alone, sampling +0x2C/+0x148):
  menu at 12 s, intro 28.4-38.1 s (2024) / 28.0-37.6 s (2016).
* **The race:** world.cpp loads the received world from the menu (`LoadGameByName`) as soon as it
  arrived. When that happens while GGE_intro plays, the intro is never ended: in Space it stays
  "playing" forever (nothing advances it any more, the caption OK / Enter path 0x44EE075 only ends
  captions), with the HUD hidden and the input lock held. A host that loads its game before the video
  ends gets the intro *in Space* a few seconds later (seen in every run: ~17 s after its menu); that
  one ends on its own after ~10 s, but blocks the HUD and the clicks meanwhile (the first landing of
  coop_space_rescue.py and the "tutorial caption" the jump of coop_space_page.py had to force through).
* **Why GOG 2024 only (in the tests):** timing, not a symbol. The 2024 lanes run at ~66-69 fps, the
  2016 lanes at ~31 fps (the same `wait.frames 200` before the join takes 3 s instead of 6.5 s), and the
  host's load is faster: the 2024 guest joined ~16.7 s after its menu and loaded at ~20 s (inside the
  intro), the 2016 guest joined at ~22.8 s and loaded at ~26.3 s (just after it). The race is the
  same on GOG 2016: in one of three ordinary starts on lane 4 the 2016 guest's world also arrived
  during the intro (the fix ended it at the load). Real players on either build hit it when they
  join within ~16-26 s of the menu coming up (the video not skipped).
  Every Space symbol on that path was checked for 2024 (cinematic manager getter, EndCinematic);
  none was wrong.
* **Fix (world.cpp):** `load_now` ends a cinematic still playing in the menu with
  **EndCinematic** (0xADFF20 / 0xADFF50, `thiscall(mgr)`, `ret`: stop, input lock -1, +0x148 = 0,
  MessageSend 0x44F1189), so the menu handler stops its intro the native way before the load; and in
  any stage a running GGE_Intro (+0x148 == 0x8915F9BB) is ended outside the frame after 1 s (the
  host's late intro, or any load path that slipped past). Log: "world: cinematic 8915F9BB still
  playing at the load / in a stage: ended it"; `coop.status` prints "cinematic <id> playing (menu
  intro)" while one runs and "menu intro ended N". Calling EndCinematic on the stuck guest by hand
  (`call 0xADFF50 thiscall <mgr>`) brought the HUD and the clicks back at once, no faults.

* **Clicks on the menu itself (✔ runtime, lane 9 GOG 2024, 2026-10-24, branch fix/gog2024-tests):**
  while the intro plays the game drops every click on the galaxy menu (its own items and the mod's
  "Co-op" item: the UI never sees them), and also for **~2-3 s after it ended** (a click 1 s or 2 s
  after the end did nothing, 3 s after it worked; measured with `coop.status` + `coop.uistate`).
  A click does not skip the intro; **Escape does** (✔ runtime: gone at the next sample, 0.5 s; the
  key goes through the menu's key handler 0xDEA5B0, which passes it on to 0xE00620 ~). `coop_ui.py` clicked "Co-op" ~21 s after the menu on GOG
  2024 (~66 fps: `wait.frames 700` is ~10 s there, ~22 s on GOG 2016), inside the intro, so the
  host's window never opened (lab 20261023-225702, lane 1, twice). Real players clicking Co-op in
  the first ~30 s met the same dead button.
* **Fix (coop_ui.cpp tick):** the left button going down over the Co-op item while the intro plays
  (`world::menu_intro_playing()`: menu mode, cinematic manager +0x2C != 0, +0x148 = GGE_Intro) is
  seen through GetAsyncKeyState + GetCursorPos; the mod posts Escape (WM_KEYDOWN/UP VK_ESCAPE) to the
  game window, i.e. the game's own skip, and opens the co-op window 3 s after the intro is over (when
  the menu takes clicks again). Log: "ui: Co-op clicked during the menu intro: Escape skips it".
* **Negative result:** ending the intro with **EndCinematic in the menu** (the call `load_now` makes
  right before a load) and staying in the menu broke it: once an access violation at 0x9617A3
  (`UTFWin::Window::SetFlag+0xF3`, from `App::Canvas::Tick` 0x8483F6, data 0x0C0A0000) ~10 s later,
  once a game whose co-op window stopped reacting (no hosting) and whose console died. EndCinematic
  is only safe right before a load (the menu is torn down). The menu handler's end path (0x44F1189
  with the GGE_Intro id) also stops the cinematic again (0xAD7DC0) and releases its +0x1AC object.

### 7.10 Ship height on a planet and the repair distance (✔ runtime, lane 1 GOG 2024, 2026-10-24, branch fix/gog2024-tests)

* On a planet the scene's origin is the planet's centre and a UFO flies on a sphere whose radius is
  its **zoom** (`mZoomAltitude` +0x768, §0; `MoveTo` = normalize(dir) * altitude). On the home
  planet of the `space` fixture (✔ `coop.fleet list` positions while turning the mouse wheel at the
  ship): ~719 m from the centre right after a landing, ~15 m per single wheel notch on GOG 2024
  (6 notches at once: 120-180 m; GOG 2016: 6 notches ~270 m, 8 ~360 m), **~520 m fully zoomed
  in** (clamped), still on the planet at ~990 m (further out the ship leaves for the system). The
  two players' heights are their own zooms: nothing keeps them equal.
* **The bug (GOG 2024 lanes, lab 20261023-225702 lane 1):** the rescue measured the 3D distance
  (`net::dist`, < 60 m held for 2 s). With the retried landings of `coop_space_rescue.py` (zoom-ins
  that do not land in the first minute on GOG 2024, known-issues) the host flew at r 775 and the
  guest at r 978 (or 638 in round 2): the host circled 200-260 m from the disabled guest, right
  "over" it, and the guest was destroyed. Seen with `coop.fleet list` (me) positions on both games.
* **A player far above or below is not on the screen at all:** host zoomed in (r 536), guest at
  r 771-991: no `player` (or `far`) line in the host's `coop.targets all`, i.e. the partner is above
  the camera; with smaller differences it is seen with parallax.
* **Fix:** `spacecoop.cpp repair_distance`: on a planet the distance along the lower sphere (angle
  between the two directions times the smaller radius), elsewhere 3D. `coop.space dist` prints
  `<name> <3D> m; repair <that> m; alt <partner radius - own radius> m;` (alt on a planet only).
  The bot (`Player.match_height`, play_space.py) zooms toward the partner's height (alt/30 notches,
  at most 4, per step) while flying to a disabled partner, as a player zooms to find it, and stops
  once the ground distance (`repair`) is under 40 m (screen distance alone kept it flying past).

## 8. Loading a save while a stage runs (leaving space)

Found while fixing "loading a Cell save from the Space stage crashes" (branch fix/stage-switch-load,
2026-10-22; test `tools/tests/coop_stage_switch.py`).

### 8.1 The crash (✔ runtime + decompile)
* `cGamePersistenceManager::LoadGameByName` 0xB27FF0 → `SimulatorModeMgr_SetMode` 0xB1E320
  (`thiscall([0x167FACC], modeId)`, +0x20 current mode, +0x28 "switch pending") with kLoadGameMode
  0x1654C08 → the space app mode reacts in `cAppModeSpace_OnModeChange` 0xFDCED0
  (`thiscall(mode, oldMode, newMode) ret 8`; only does work when oldMode is Space 0x1654C05):
  * new = LoadGame → `cAppModeSpace_LeaveContext` 0xFDA9E0 then `cAppModeSpace_Teardown` 0xFDC140;
  * new = galaxy menu 0x2CCD1D2 → `cAppModeSpace_ExitToMenu` 0xFDC970 (LeaveContext, 0xB3D420,
    0xB8C1D0, Teardown), then the mode reset 0xB3D300(0xEBB801);
  * new = editor 0xDBDBA1 / 0x1654C06 / adventure 0x1654C10: own branches.
* LeaveContext switches on `GetCurrentContext` (SpacePlayerData+0x10): 0 planet → 0x100A800,
  1 system → 0x1009310(0,0,1) + `Space_LeaveSolarSystem` 0x1003860(`GetActiveStar()` 0x10210E0),
  2 galaxy → 0x1009310 + 0x10034C0, −1 → nothing; then `SetCurrentContext(-1)` + 0x10218A0(0,0).
  `Space_LeaveSolarSystem` (`thiscall(?, cStar*) ret 4`) calls `cStar::GetSolarSystem` 0xC8B8F0 on
  the star without a null check.
* On the way to kLoadGameMode SpacePlayerData+0x04 (planet) and +0x08 (star) are already 0 while
  +0x10 is still 1 → `GetSolarSystem(null)` AV at 0xC8B8F3 (data 0x40), then
  `cStar::IsStarOrBinaryStar` 0xC8B980 faults every frame (data 0x48) and the game hangs in
  LoadGame. The game's own `ReloadCurrentGame` 0xB27120 from Space crashes the same way (✔ runtime,
  no mod code involved). `ReadStarsFromSaveDB` while in space does **not** clear the active star
  (✔ runtime, negative result). Who clears +0x04/+0x08 before the handler: not found (?).
* Stack signature: `GetSolarSystem+0x3` ← 0x10038A6 ← 0xFDAA2F ← 0xFDD0CF, with 01654C08 01654C05
  (new, old mode) on the stack.
* It does not depend on the stage of the save: Space → Space crashes the same way (nightly chaos
  run, lane 1: a guest in Space left, joined again and loaded the re-sent world in game; reproduced
  on lane 5 with the pre-fix build by `coop_stage_switch.py --steps space,rejoin`). Both runs were in
  a solar system (context 1). Host `coop.load` space → space in warm resets did not crash in the lab;
  why is open (?) - perhaps another context (planet/galaxy) at that moment; `ReloadCurrentGame` in a
  system crashed (✔).

### 8.2 The native way out: quit message 0x4715068 (✔)
* `Simulator_HandleMessage` 0xB63610 (`thiscall(this, msgId, data) ret 8`), message 0x4715068:
  if a mode switch is pending ([0x167FACC]+0x28) it re-posts itself; otherwise, in Space on a planet
  (context 0) it first leaves the planet (`LeavePlanet` 0xFDE3D0), posts 0x689C9B9, calls
  `IStateManager::Get()`→vt 0x40 and sets +0x28/+0x29. Result: the game **saves** (a `GameN.old`
  backup appears) and goes to the galaxy menu (GalaxyGameEntry) in ~1 s from any stage. Posted with
  `MessagePost` (IMessageManager vt 0x18, 4 arguments) like `kMsgSaveGame`.
* From the menu, `world::load_now` (Close/SetLocation/Open of DB 0x4729A47, `ReadStarsFromSaveDB`,
  `LoadGameByName`) works for every stage. SporeCoop loads every save requested while a stage runs
  this way (`world.cpp load_slot`): host `coop.load`, guest received world (also Space → Space).
  A host reload of the same stage sends no world (coop.cpp `on_mode_change`: not a new stage); the
  guest keeps its own copy until it rejoins. Timing: menu after
  ~1 s, 3 s settle, host in the target stage ~5–6 s after the request, guest together ~16–18 s.
* Other in-game loads seen to go wrong (all avoided by the menu route): a guest in Cell loading a
  world in game crashed in `modes::cell_fill` (cell pool freed under the cached local cell, coop.dll);
  a Space save loaded in game from Cell kept `CurrentGameName` = "Karke.spo", so the next save wrote
  `Karke.spo` into the space slot and the guest got the wrong planet; tribe → civ in game worked.
* A save of ours made while the game switches modes does not load at all: the host's share save
  written while its editor opened (end of an evolution vote) kept every game that loaded it in
  LoadGame 0x1654C08 forever, frames running, no fault (docs/re/creature.md 9). `save_game` now
  waits while an editor opens or is open, a load runs or [0x167FACC]+0x28 (switch pending) is set,
  and outside a stage. Two saves at once break the slot too: our share save and the quit message's
  own save (a guest joined as the host left) gave a slot whose load faulted at 0xBA7F87 (data
  0x1188, from 0xB27DA6) and stayed in LoadGame; the quit message now waits for our save (a hook on
  0xB63610 posts it again, docs/re/creature.md 9).
* Loading 3 s after the menu came up from on_frame (inside Present): the guest twice got a
  first-chance AV in `cTerrainStateMgr_GetTemperatureScore` 0xFB89D0 (ecx 0; caller 0xF9E5EC
  `[esi+0xC]->vt 0x10()` returns a null terrain state, a planet render), once at the very load call
  with 1 s settle, once 1 s into the load; the game survived it. Not seen in 14 switches since the
  load runs outside the frame (`run_outside_frame`). Why: 8.3.

### 8.3 Planet texture jobs and loads from inside Present (✔ runtime + disassembly, 2026-10-23)

Found on branch fix/menu-load-crash (test `tools/tests/coop_rejoin_loop.py`).

* A terrain sphere (`Terrain::cTerrainSphere`, the planet of a stage and every planet the galaxy menu
  shows) draws its textures in **render jobs**, one cube face per frame: the job object
  `cTerrainSphereTextureJob` (vtable 0x14911FC, size 0x28, kept at sphere+0x8A0; +0xC the sphere,
  AddRef'd; +0x10 pass 0..3; +0x14 face 0..5; +0x20 a render helper, null = nothing to draw; +0x24
  set up) is queued by `QueueTextureJob` 0xFA0520 (`thiscall(sphere, face, pass) ret 8`) through the
  render manager ([0x15FE8B8], vtable 0x140DCB0) `AddRenderJob` vt 0x78 = 0x75F6E0 (list +0x330 under
  the lock +0x28, returns an id from the counter 0x1630C5C, kept at sphere+0x8A4). After the draw its
  completion calls `OnTextureJobDone` 0xFA8800, which resets the job (0xF99F90) and queues the next
  face / pass: 6 faces x 3 passes, then pass 3, about 19 frames per planet.
* `DrawLayer` 0xF9E490 (job vt slot 3, `thiscall(job, flags, layerIndex, viewers, engine) ret 0x10`)
  sets sphere+0x8A4 = -1 and reads the sphere without checks: the terrain state sphere+0x20C (sphere
  vt 0x10 = 0xFA0D20) and the face render targets sphere+0x148/+0x160 [face]. The sphere's dispose
  0xFA4880 (vtable 0x14916D8 slot +0x110) releases the job (+0x8A0 = 0), calls `RemoveRenderJob`
  vt 0x7C = 0x75F9A0 (by id, no lock) when +0x8A4 != -1, then frees the targets and the state.
* The render manager's frame 0x7604F0 first **takes** the queued jobs into a local list (0x76062D),
  then calls Present of the previous frame (0x760727 -> 0x11F8150: swap chain vt 0xC / device vt 0x44),
  then draws the taken jobs (0x7607A4 -> 0x75E490 -> DrawLayer) and the layers, and puts undrawn
  jobs back. Present is where SporeKit's frame event runs (coop `on_frame`). Anything that disposes a
  terrain sphere from there (a load: `LoadGameByName` leaves the galaxy menu and disposes its planets)
  cannot remove a job that the frame already took; the job is drawn right after Present with the
  state and targets gone: AV at 0xFB89D0 (temperature, pass 0..2), 0xF9E660 / 0xF9E67E / 0xF9E6FE (the
  face targets), all on the main thread, call chain 0x75E547 <- 0x7607A9. Seen in co-op as "a planet
  render faults while a received world loads" (guests with 3-4 players, 2026-10-22/23).
* Repro (old build): the guest leaves its stage for the galaxy menu (`coop.menu`) and loads a save
  with `coop.load` (a main-thread command: runs inside Present) 0-1.5 s after the menu came up, in a
  freshly started game: 2 of 10 fresh games faulted (0xF9E67E, 0xF9E6FE), 4 of 17 first trips to
  the menu in all; later trips in the same game hardly ever fault (1 hard crash without a logged
  fault in about 150 later trips, cause not known). The first trip after the join textures the
  menu's planets, later ones seem to reuse them (~). With the guard of `gamefixes.cpp` and the old load path, 5 of 12
  fresh games had a skipped draw (`job .. sphere .. state 00000000 targets 00000000/00000000 sphere
  job 00000000 id -1 tid <main>`: the sphere was disposed, the job had already been taken).
* What SporeCoop does: every load goes through `world.cpp` `via_menu_tick`: from the galaxy menu
  only after it has been up `kMenuSettle` (3 s) and always outside the frame (`run_outside_frame`:
  a posted window message, pumped outside the render manager's frame). Before, a world that arrived
  while the guest was in the menu (join from the menu, a rejoin during a load) and the host's
  `coop.load` (a main-thread command) loaded at once from inside Present. As a safety net
  `gamefixes.cpp` puts a guard into the job's vtable slot 3 that skips the draw while the state or a
  target is null (`coop.status`: "planet texture job guard on, skipped N"); the guard replaced the
  first fix (only the temperature call at 0xF9E5EC redirected), after which the same draw faulted at
  0xF9E660.
* Leaving a stage: the stage mode begins at once when a load starts (LoadGame 0x1654C08 lasts about
  0.1 s), its loading screen and setup run in the stage mode. The tribe game's state machine
  0xCDE7D0 (state at +0x22C, previous mode +0x230, timer +0x238) runs its setup 0xCD78E0 one step
  after the load (state 3 -> 4); the setup points a camera-like object of the simulator
  ([0x167FAAC], getter 0xB3D350, vtable 0x14672A8, made by 0xBC3830; null in the galaxy menu) at the
  tribe through 0xB12CE0 (`thiscall(obj, Vector3*, ?, bool) ret 0xC`, reads obj+0x2B4). A guest got
  an AV there with the object null (0xB12D32, data 0x2B4; stack: Vector3 on the stack, ebx = the
  tribe+0x120, saved esi 0: the caller is 0xCD7D0D) during a 4-player churn. Not reproduced on 2
  players (? which order of events); likely a quit or load while the stage was still setting up.
  `via_menu_tick` now leaves a stage only when the co-op adapter has had something to control for
  3 s (`kStageSettle`; a stage without anything to control is left after 20 s).
* Also found with the same test: two world transfers to the same player (a rejoin while the first
  one was still being sent) sent their chunks in turn; the guest wrote the old chunks into the new
  world and dropped it ("world transfer failed: checksum mismatch", 3 of 60 rounds of
  `--reshare 0:2.5`). A newer transfer now replaces the running one (`share_now`).
* Seen as a desync: the same fault hit guests that join **from the galaxy menu** (an empty profile: the world
  arrives in `on_frame`, and `load_slot` called `load_now` right there, inside Present): 3 of 3 cold
  4-player sessions (2026-10-23, `coop_npc_multi.py`) had one lane-B guest with a first-chance AV in
  the terrain render pass of the menu's planet view 3 ms after `LoadGameByName`: 0xF9E67E or
  0xF9E6FE (`mov reg,[ecx]` with ecx = `terrainSphere+0x160+i*4`, a null RTT; same function as the
  0xF9E5EC call). The game survived, but that guest's planet came up **without its height map**: its
  ground lay ~6 m under the host's (its own avatar stood 6 m low), NPC proxies placed at the host's
  heights fell through to it (radial error only) and its screen kept showing the menu picture.
  Since `load_slot` sends this case through `run_outside_frame` too: 0 faults in 7 sessions.
  Check for a guest with a broken planet: compare `|pos|` of its own avatar with the host's copy
  (`coop.sync`), or `exc`.

## 9. Navigation, tools and what the game frees (✔ runtime, play_space.py, lane 7, 2026-10-23)

Found while writing the game bot `tools/tests/play_space.py` (players fly with real input).

### 9.1 Where things are on the screen
* **Planets of the current system**: `cStarManager` (`cStarManager::Get` 0xB3D370) **+0x50..+0x54** is
  the vector of the loaded `cPlanet*` (the list `RecordToPlanet` 0xBB5B20 searches, comparing
  `planet+0x13C` with the record). `cPlanet` **+0x04** = position (system coordinates; planets orbit,
  so it changes), +0x10 orientation, +0x13C record (+0x184 PlanetID, +0x194 tech level, 5 = colony),
  +0xD4 the cGameData base (vtable 0x1473038, GetNounID 0xC70100 returns **0x3275728**; the planets are
  not in the GetData noun list). In a system the list held 6 planets, one of them of another star
  (`P01B16013`).
* **Stars in the galaxy view**: `cStarManager` **+0xC8** = vector of 20-byte sectors whose first dword
  is an array of `cStarRecord*` (`GetStarRecord` 0xBA6EB0: StarID = sector << 12 | index).
  `cStarRecord` **+0x3C** position (galaxy coordinates, the player UFO's position in the galaxy view
  is in the same space; neighbouring stars are ~1 unit apart), +0x54 empire id (the player's political id for own stars), +0x70 StarID.
* `coop.targets all` prints them (`obj P/C<PlanetID>` in a system, `obj S/H<StarID>` in the galaxy
  view) and `far player <id> x y` for a partner off the screen (its projected point).

### 9.2 Input (1440x1080 test window)
* Planet view: W/A/S/D (or the arrows) fly the ship relative to the screen (W = up); a left click on
  the ground with no tool selected flies there; a click on the minimap flies to that place (a city on
  the minimap: the ship goes there).
* Solar system view: the keys barely move the ship; a **click on a planet** flies there and makes it
  the active planet (`coop.space star` prints `active <PlanetID>`; a moon next to a planet often
  takes the click). Zooming in (wheel) at the ship lands on the active planet (a short cinematic);
  zooming out of a planet goes back to the system. Clicks on empty space do nothing.
* Galaxy view: zooming out of a system (wheel, or the zoom buttons at the bottom left, "Zoom Out"
  at (18, H-60)) only works with the **interstellar drive**: without it the camera stops at the
  edge of the system. The space fixture's ship does not have it yet (a new player gets it in the
  first missions: fly to Dunoselka, scan the wreck).
* Galaxy view: a click on a star in the drive's range (the circle) flies there at once (no money
  spent in this save); zooming in enters its system. Neighbouring stars are ~1 galaxy unit apart.
* Tool palettes (bottom right, anchored to the right edge): category tabs at x = W-426 main, W-387
  combat, W-347 gear (scan, abduct), W-306 colony, then three terraforming tabs and cargo; y = H-51 while the palette is
  closed, H-151 while open (a click on a closed tab opens it). Keys 1..9 select the slot of the open
  palette. Esc deselects the tool; a second Esc opens the pause menu ("Resume Game").
* Clicking an own city (planet view) opens the city planner (buildings for money, done at the
  bottom right check button).
* Captions that wait for OK (6.9 in docs/06-testing.md): also the tutorial captions ("Something is
  showing up on the ship's radar...") and badges ("title of Commander"); Enter confirms them.

### 9.3 Tools and cheats
* Tool keys in the inventory are `{FNV-1 hash of the tool file name, 0, 0}`: scan 0x08EF6A62, abduct
  0xF58C1654, interplanetarydrive 0x4133FE05, **interstellardrive 0x2569FAE8** (not in the fixture).
  `cPlayerInventory::AddItemByKey` with such a key adds the tool (`coop.space tool add <name>`); the
  interstellar drive at once unlocks the galaxy view (a badge cinematic and a tutorial caption
  follow).
* The release build registers only some cheats: `moreMoney` (+1 000 000) works, the developer
  cheats `space`, `raid`, `tool` (with their help texts in the exe) do not exist (ProcessLine false).

### 9.4 The game frees NPC UFOs, puppets included
* Leaving a planet (zoom out to the system) frees the UFOs of the planet, also the mod's Defender
  puppets of the partners there and the guest's mirrors; it happened ~10 times in 8 minutes of play.
  Any later use of the pointer is a use after free (the host crashed in `modes::space_drive`, vtable
  overwritten). Only a pointer still in the UFO noun list (`GetAllUFOs` 0xAD48B0) may be touched
  (`game::ufo_listed`); a listed pointer can be a new UFO at a reused address (check the type).
* The partners' ships are Defender UFOs (type 6): their own AI fires at every hostile ship in range
  (`cCombatSimulator::Update` -> FireTool with the puppet as owner, ~10 calls per second with
  pirates around). Pirates near a guest's puppet died on the host while the guest never fired; the
  mod drops FireTool for puppets (`puppet_shots_blocked` in `coop.fleet list`). (~) The player's own
  ship may also shoot back at an attacker on its own (TakeDamage step 9, auto-retaliation).
* Pirates spawned with `SpawnUFO`: type 2 (raider) attack the player only around its colonies; on
  other planets they fly off and vanish after ~30 s without a shot. Type 8 (ambush pirate) attack
  the player anywhere (✔ runtime, `coop.fleet spawn <type>`; but not on the homeworld of the
  2026-10-23 fixture, where they flew off too: §10.2).
* Right after landing the player's ship takes no damage for a few seconds (the planet
  invulnerability counter `cSimulatorPlayerUFO` +0x24, §7.2) ~.

### 9.5 Rescue in real fights (✔, `coop_space_rescue.py`)
* NPC hits on a guest arrive as MSG_UFO_DAMAGE (the host's TakeDamage on the guest's puppet). The
  guest applied them with the original TakeDamage, past the rescue filter: a guest next to its
  partner was never disabled by pirates, it died. Now the hook applies them (`g_applying`: only the
  own-ship filter, then the game).
* The filter let a ship with HP <= 1 die at the next hit (small hits can leave 0.5 HP); now any
  lethal hit next to a partner disables the ship (60 s cooldown after a rescue as before).

### 9.6 Empires, contacts, colonies by record (✔ runtime, lane 8, 2026-10-23, making the `space_empire` fixture)

* **Tool names** (instance = FNV-1 of the lower-case name, group 0x30608F0B, type 0xB1B104 in
  Spore_Game.package / PatchData.package, 363 tools; names found by hashing guesses against that
  index): `interstellardrive_upgrade1`..`4`, `energyupgrade1`..`5`, `healthupgrade1`..`5`,
  `cargoupgrade1`..`3`, `laserlevel1`..`3`, `pulselevel1`..`3`, `placecolony` (the colony tool),
  `monolith`, `atmospheregenerator`, `cloak`, `shield`, `wormhole`, plus scan / abduct /
  interplanetarydrive / interstellardrive (9.3). `coop.space tool add <name>` adds any of them.
* **Placing a colony with the tool** needs a spice node near the spot: on the T1 tutorial planet
  Vulpica every spot was refused with "Colonies must be placed near spice." (red rings), the planet
  has no spice node object although its record has a spice kind. On the T2 planet Gabar (the tutorial's
  ruins) the colony tool was greyed out (? why).
* **Planet records of a star** are made on demand: star +0x84..+0x88 vector of `cPlanetRecord*`
  (`cStarRecord::GetPlanetRecords` 0xBBA8D0 generates them first, 0xBBA780); unvisited stars far
  from home have an empty vector. Record +0x198 spice kind is 0 on most planets of stars the
  player never got close to; the home system's and the beacon star's planets have one.
* **cStarRecord**: +0x3C position (galaxy units), **+0x4C tech level** (setter 0xBB9C10 is a
  plain store, getter 0xBB9C20), +0x54 empire id, **+0x5C flags** (SetFlag 0xBB9C40: or / and-not;
  0x4 on stars with an NPC empire, 0x8 on Grox stars, 0x10 / 0x20 set after the player visited,
  0x1000 ?, the player's home 0x1037), +0x70 StarID.
* **Empires**: `cStarManager` +0x150 is an eastl map pol -> `cEmpire*` (rbtree, anchor +0x154,
  root +0x15C, size +0x164; node +0x10 key, +0x14 value). In the `space` fixture's galaxy it held
  2121 empires: 2119 NPC empires with **one** star each (their home star, tech level 5,
  $100 000), the player's, and the Grox (800021C9, 2401 stars, ~320 units away at the centre). The
  nearest NPC empires were 5.4 and 5.9 units from home (neighbouring stars are ~0.5-1 apart).
  `cEmpire` +0x84 political id, +0x88 stars, +0xB0 home StarID, +0xD0 money.
  **`cStarManager::GetEmpireForStar` 0xBB14B0 creates an empire** for a star without one
  (new pol 0x80000000 | counter +0x1D4, SetEmpireID): only call it for stars with an empire id.
* **Contacts**: `cPlayer` = `cGameNounManager::Get()` +0x74 (getter 0xF67D40); +0x11D8..+0x11DC
  is a vector of `{u32 pol, u32 flags}` sorted by pol (lookup 0xD013B0). Getters 0xC7A840 (bit 0),
  **0xC7A890 `PlayerContactedEmpire(pol)` (bit 1)**, 0xC7A8F0 (bit 2, "EmpireIsAwareOfPlayer"),
  0xC7A950 (bit 3), ...; setters 0xC7BCE0 (bit 0), 0xC7BD00 (bit 1), 0xC7BD20 (bit 2),
  0xC7BD40 / 0xC7BD70 / 0xC7BDA0 (bits 3 / 4 / 5 with a bool). The player's own entry has 0x26.
  `cEmpire` 0xC30A50 = contacted by the player?, **0xC30A70 = SetContactedByPlayer** (bits 1 and
  2). The game's first contact (message 0x3AC86B5, handler at 0xFE3233) calls 0xC30A70, then
  0xC30AF0(0), counts a statistic and applies relationship event 0x5B6CF09 (scale 1.0) between
  the empire and the player. `coop.space empire contact <StarID>` does the same without the
  comm screen; the flags survive save and load.
* **A colony by record** (5.3 "receiver elsewhere") works: `SetTechLevel(rec, 5)` 0xC70850,
  `FillPlanetDataForTechLevel(rec, 5)` 0xB97040 (a cCivData with pol = star empire id and one or
  two cCityData), `cEmpire::AddPlanet(rec)` 0xC34FB0 (SpacePlayerData +0x20 colonies grows by
  one); for a star of no empire first `AddStarOwnership`, star +0x54 = player pol, +0x4C = 5,
  +0x5C |= 0x100. The colony shows in the system view (`C<PlanetID>` in `coop.targets all`), on a
  visit the planet has a generated colony (several cities and a spice geyser), and it survives a
  save and a load. `coop.space empire colony <PlanetID> [spice]`. Stored spice (`cCityData` +0x1C,
  float) set higher is clamped at the next load (one city: 5, two cities: 10).

## 10. Co-op contracts (feature 7) (✔ runtime, lane 3, GOG 2016, 2026-10-23; `space_contracts.cpp`)

### 10.1 Star records: name, type, planets
* `cStarRecord` **+0x60 `mName`** (eastl::string16: +0x60 begin, +0x64 end, UTF-16) ✔ runtime: the
  fixture's home star 0x460035 reads "Fibucius", its neighbours "Argis-2", "Gerbus", "Aspytory" (the
  names the galaxy view shows). `cPlanetRecord` +0x18 `mName` (string16, ✔ "Madas", the fixture's
  homeworld), +0x184 PlanetID, +0x194 tech level (5 on the homeworld). +0x48 `StarType` (ModAPI: 1 galactic core, 2 black hole, 3
  proto-planetary disk, 4.. real stars), +0xAC planet count (byte), +0x84 `vector<cPlanetRecord*>`
  (ModAPI layout, not needed so far). Ids from the network: `spacecoop::star_record` (the table
  lookup; `GetStarRecord` faults on an id that is not a star).
* `cEmpire` **+0x88** = `vector<cStarRecord*>` of the empire's stars: `GetStars` is `lea eax,[ecx+0x88];
  ret` at 0x00C30A10 (GOG 2016) and 0x00C30A30 (GOG 2024) ✔ objdump of both exes. The mod reads the
  field directly (no symbol).
* Neighbour stars: the sector table of §9.1 (`cStarManager` +0xC8), sorted by the distance of +0x3C.
  Neighbours were 0.3 to 1 galaxy units away; `TravelToStarSystem` (§7.3, `coop.space jumpto`)
  reaches any of them without the interstellar drive and without money.

### 10.2 The armada: the game's own raids (✔ runtime, GOG 2016, lane 3)
* **Raid on demand**: `SporeCoop::Space_StartRaid` = **0x0100E620** (the `raid -start N` cheat routine,
  caller 0x102527E; the same address and body in GOG 2024 ✔ objdump): thiscall, `this` not used,
  `(int unused, cPlanetRecord* colony, int numBombers)`, `ret 0xC`, returns bool. Body: mission
  manager 0xFEB880, `CreateMission(0xA4B0A2DF, colony, owner of the star, 0)` 0xFEC420, Cast
  0x3960C0A, SetAttackerEmpire(0) 0xC5B520, SetTargetPlanet 0xC472C0, SetNumBombers 0xC5B540, Accept
  0xC486F0. On the homeworld with the player there: `numBombers` bombers (type 4, 250 HP) and 4
  raiders (type 2, 50 HP) per bomber spawn at once, far from the ship, and attack the colony; the
  colony's turrets alone shot down 4 + 16 of them in ~25 s; the mission ends with state 5 (defended,
  spacecoop pays its +2000 with a partner there). Each ship's `+0x800` is the raid mission (id +0x78
  = 0xA4B0A2DF, state +0x84, target cPlanet +0x1E8 -> record +0x13C -> PlanetID +0x184).
* A raid needs a planet of the raided empire: the armada targets a colony of the host (record
  +0x194 == 5, the homeworld counts). The guests' colonies are not in the host's save.
* What did not work (✔ runtime, so nobody tries again):
  * `SpawnUFO(8 /*AmbushPirate*/, &pol 0xFFFFFFFF, key)` pirates do not fight: in the system view
    they stand still, on a planet they fly straight up and away and vanish after ~30 s (also with
    `coop.fleet spawn 8`, on the homeworld of the new fixture; docs 9.4's "type 8 attacks anywhere"
    did not hold there). Not changed by `cSimulatorSpaceGame::AddNPCUFO` **0x010098E0** (thiscall(sg,
    ufo) `ret 4`: push_back into the NPC UFO vector +0x78 with AddRef; the ambush spawner 0xFF8BC8
    calls it), by `ufo+0x7B0` = a star record (the ambush spawner writes one there), by the
    combatant's "attacked by" vt +0x1C **0xC37AE0** (thiscall(combatant, attacker combatant) `ret 4`,
    stores the attacker UFO at combatant +0x208 = ufo +0x710 with AddRef), nor by writing the pending
    target (combatant +0x48 = ufo +0x550) and mDestination every 0.3 s.
  * SpawnUFO's ships are not freed by a context switch: they came along from the system view to a
    planet (not in the NPC list), so the mod would have to remove them itself.
  * **Null model key**: SpawnUFO then takes the default key {0xCB89C3D6, 0x476A98C7, [0x15B8504]}
    (0x102AD33..0x102AD56) and every such ship, and every guest mirror of it, costs one caught access
    violation at 0x00E98DD0 (`cPlanetGfx_Create+0x8F0` by the nearest symbol, data 0x4). The game's
    pirate model: a raid without an attacker empire copies its UFO key from **`(*(cGameNounManager +
    0x74)) + 0x1280`** (0xC5C8B3: 0xF67D40 = `mov eax,[ecx+0x74]; ret`, 0xC758F0 = `lea eax,[ecx+0x1280];
    ret`; GOG 2024 0xC5C8F3 / 0xF67D40 / 0xC75910, the same fields); an empire's ships take
    `cEmpire` 0xC327F0 (the cached key at empire +0xB8), as the ambush spawner 0xFF8B89 does.
  * With the pirate key a Teleport (loco vt +0x44) right after SpawnUFO did not hold in the system
    view (the ship stayed at the spawn spot (0, 0, 700)); writing mNextPosition +0x718 and
    mDestination +0x750 too made it hold.
* The waves (space_contracts.cpp): `crew + wave - 1` bombers (two players: 2 bombers + 8 raiders,
  then 3 + 12), started when the host and another member of the crew are on the colony for 3 s,
  10 s between the waves. Each raid ship gets double max HP / HP on first sight (the guests' mirrors
  show the host's HP). The wave's outcome is the raid's own end state, seen by spacecoop's
  `cMission::SetState` hook (`spacecoop::set_raid_listener`): 5 = won, 6 = the pirates won (the
  contract fails). Ships killed by one big hit leave the UFO list before a state 2 is seen, so ship
  counting cannot tell destroyed from flown off. If the host leaves the planet during a wave, the
  game runs the raid on off-screen; the mod starts the wave again when the crew is back.
* Captions in the test: landing on the planet shows tutorial captions that wait for OK ("Something
  is showing up on the ship's radar...", "You have discovered a wild planet..."), and the defended
  raid gives badge points: the panel "2 badge points" + OK takes the mouse, the co-op window's
  buttons get no clicks until it is closed (Enter).

### 10.3 Rewards
* Money: `cEmpire::AddMoney` on the host only (shared budget, shared.cpp gives the total to every
  guest within a second ✔ runtime: 100 000 -> 115 000 -> 140 000 on both games); in rivals mode
  every member of the crew adds it locally.
* Tools: `cPlayerInventory::AddItemByKey` with {FNV-1 of the file name, type, group of a tool in the
  inventory} (§9.3), on every member of the crew; `HasTool` (vt +0x74) after it is true ✔ runtime.
  File names in `spacetools~` (0x30608F0B), hashes checked against the index of the game's packages
  (Spore_Game / EP1 .package): laserlevel1..3 0x6DB01899.., pulselevel1..3 0x7B76E38F / 0x7B76E38C /
  0x7B76E38D, protonmissilelevel1..3 0xFC220278 / 0xFC22027B / 0xFC22027A, antimattermissile
  0xF06A8152, shield 0x4542380A, cloak 0x44808DE7, atmospheregenerator 0x931426A0, gravitationwave
  0xA43B6F38, monolith 0x14A7D231, allylaser1..3 0x47E65D39.., allymissile1..3 0x0068DF06 /
  0x0068DF05 / 0x0068DF04 (378 resources in the group; the other names are not known).

## 11. Rivals mode: players as rival empires, PvP (✔ runtime, lane 6, GOG 2016, 2026-10-23; `space_rivalry.cpp`, `coop_space_rivalry.py`)

### 11.1 Why a partner's ship cannot be hurt, and how war changes that (✔)
* A partner's ship is a Defender puppet (UfoType 6) spawned with **our own** political id
  (`modes.cpp` space_spawn: `SpawnUFO(6, &GetPlayerEmpireID(), model)`). The game's weapons never hurt
  their own empire: the projectile explosion 0x00CBBEF0 compares the attacker's political id (its
  local at esp+0x10, from the owner's combatant vt+0x10) with `target->GetPoliticalID()` (combatant
  vt+0x10 = 0xC69020: `mov eax,[ecx-0x508]; jmp [eax+0x4C]` = cGameData vt+0x4C 0x5AADB0 `mov eax,
  [ecx+0x30]`) at 0xCBC198..0xCBC1AC and skips equal ids (unless the tool has kFlagDamagesFriendlies
  or the attacker's id is -1); TakeDamage itself returns on `attackerPol == own pol` (§2.1 step 4).
* **The political id is just `cGameData+0x30`**: `SetPoliticalID` (vt+0x48) 0x00804880 is `mov eax,
  [esp+4]; mov [ecx+0x30],eax; ret 4`. Writing it on a live UFO works at once.
* GOG 2024 (dc04aee5, static check by byte pattern): the same friendly-fire test sits at 0x00CBC1EC
  (`cmp ebx,-1; je; mov edx,[edi]; mov eax,[edx+0x10]; call eax; cmp ebx,eax`) and SetPoliticalID is
  the same store at 0x00804910, so +0x30 is the political id there too (not run on that build).
* The puppet of a player we are at war with gets political id **-1** (0xFFFFFFFF, "no empire", like
  the pirates of `coop.fleet spawn`): the laser (palette "combat", key 1, a click on the ship) then
  hits it, TakeDamage 0xC3CEA0 is called on it with `type 1`, attacker = our UFO's combatant, about
  1 damage per hit and 3 hits per 0.7 s with the starting laser (guest ship 1500 HP). The puppet stays
  invincible (+0x588), so the game itself does nothing; squadron's TakeDamage hook sends the hit to the
  puppet's owner.
* Political ids of the player empire are the same in both games of the session (the same galaxy:
  0x800021CA on host and guest in the `space` fixture), which partly answers §7.8.

### 11.2 Applying a PvP hit on the victim's game (✔)
* The owner calls the hooked TakeDamage on its own ship: `thiscall(ufo+0x508, dmg, 0xFFFFFFFF, type,
  dir, puppetOfShooter+0x508)`. The hook runs the rescue filter first (spacecoop.cpp: a partner at
  war never counts as a helper, so an enemy's lethal hit is a real death), then the game: HP goes
  down; a lethal hit plays the native death caption ("...cloning technology..."), OK respawns the
  ship at the home colony (§7.2.1). `cSimulatorUniverse 0x100E580(attacker)` only stores the attacker
  pointer (+0x110) and a constant (+0x114).
* Not applied: the attacker side's damage-boost tool multiplier (inside the base TakeDamage of the
  shooter's game, which is not called for the puppet).

### 11.3 Money (✔)
* `cEmpire::mEmpireMoney` +0xD0 of `GetPlayerEmpire()`, changed with `cEmpire::AddMoney` (clamps to
  0..99 999 999). shared.cpp pauses the Space money (resource id 3) while `spaceext::rivals()`:
  no totals, no guest changes, the guest's unsent change is dropped from the queue (it stays in the
  guest's own money).
* A guest that rejoins reloads the host's save, so its money becomes the save's value: the guest asks
  the host (K_ASK) for its last reported budget (kept by player name) after every world load in rivals
  mode and reports nothing until the answer (K_SET).

## 12. Diplomacy: relations with NPC empires (✔ disassembly of both builds + runtime, lane 4, 2026-10-23; `space_diplomacy.cpp`, test `coop_space_diplomacy.py`)

### 12.1 Where the state lives
* `cRelationshipManager::Get` (0xB3D390 / GOG 2024 0xB3D3C0) is a mode switch; in the Space mode
  (0x1654C05) it is the star manager's manager, in tribe/civ it is `SpacePlayerData+0x0C`.
* **+0x24 `map<pair<pol, pol>, cRelationshipData>`** (EASTL rbtree: root +0x30, size +0x38; node:
  right +0, left +4, parent +8, key +0x10/+0x14, data **+0x18**). The pair is normalized by
  0xD00B50: in Space a pair with the player is stored as **(other, player)**. `0xD01AE0(a, b)`
  returns this map for two empire ids (high bit set, `IsEmpirePol` 0xBA6780 = `pol >> 31`) and a
  per-planet map (mgr +0x9C) on a planet when one side is not an empire.
* `cRelationshipData`: +0x00 cached value, **+0x04 flags (bit 0 war, bit 1 alliance)** (IsAtWar
  0xD01F50/0xD01F80 `flags & 1`, IsAllied 0xD01FE0/0xD02020 `flags >> 1 & 1`), **+0x08
  `map<eventId, float>`** (root +0x14 of the record, node key +0x10, value +0x14). The relation
  (CalculateRelationship 0xD00940, clamped) is the sum of the event values.
* **Every NPC empire has a record with the player from the start** (2120 empires and 2120 records
  in the new-game fixture), with no events. A record with no events is not a contact.
* War / alliance also live in `cEmpire` enemies +0x5C / allies +0x70 (AddEnemy 0xC34730, AddAlly
  0xC347C0, remove ally 0xC32970), kept by the four state functions below.
* **Contact** has two levels in the `cPlayer` flags of §9.6 (`cGameNounManager::Get()+0x74`
  **+0x11D8..+0x11DC**, sorted `{u32 pol, u32 flags}`): **0x20 = met** (0xC7AA10 / 0xC7AA30, set
  0xC7BDA0 / 0xC7BDC0 `(pol, bool)` by MakeContact below, on arriving at the empire's star) and
  **0x02 = contacted** (PlayerContactedEmpire 0xC7A890, set with 0x04 by
  `cEmpire_SetContactedByPlayer` 0xC30A70 / 0xC30A90 from the first-contact talk, §9.6). In the
  `space_empire` fixture the two contacted empires have 0x02 (and 0x20).

### 12.2 Functions (both builds, overrides files)
| function | GOG 2016 / GOG 2024 | notes |
|---|---|---|
| `ApplyRelationship(polA, polB_cause, eventId, scale)` | 0xD06280 / 0xD06280 | thiscall `ret 0x10`, float ST0; = 0xD01AE0 + ApplyEvent |
| **ApplyEvent** `(map, polA, polB, eventId, scale)` | 0xD05DD0 / 0xD05DD0 | `ret 0x14`; the core of every event (also DeclareWar's and the FoughtEnemy cascade 0xD062B0's); creates the record (0xD05770), adds the tuning value (0xD01720, `space_npc_relationship_effects` 0x568DE14 per archetype) into the event map, erases it when ~0 |
| DeclareWar / DeclareAlliance / DeclarePeace / BreakAlliance `(cEmpire* e1, cEmpire* e2 = 0: player)` | 0xD065E0, 0xD06960, 0xD03920, 0xD01E60 / 0xD065E0, 0xD06960, 0xD03920, 0xD01E70 | `ret 8`; flags in both orders of the pair (only if the record exists), cEmpire vectors; DeclareWar with e2 != player adds StartedWar 0x5F8A1AD, DeclareAlliance calls DeclarePeace first |
| ResetRelationship(a, b) | 0xD05870 / 0xD05870 | `ret 8`: creates the record if missing, clears its events (flags kept) |
| map<u32,float>::operator[] | 0xC8C6D0 / 0xC8C6E0 | `ret 4` → float* (used by ApplyEvent on the record's event map) |
| **cEmpire_MakeContact(int)** | 0xC32430 / 0xC32450 | thiscall(empire) `ret 4`: first contact: returns if met, else sets flag 0x20 and applies the personality events 0x5B6FCC9 / 0x5B6FCD4 (+ archetype, super power, Grox ones); called on arriving at the empire's star (0xC7A234), landing on its planet (0xC75660), by the universe sim (0x1014A59) |
| cEmpire_SetContactedByPlayer() | 0xC30A70 / 0xC30A90 | thiscall plain ret; flags 0x02 and 0x04; one caller, the first-contact message handler 0xFE3233 (which then applies 0x5B6CF09 by ApplyRelationship) |
| cStarManager::GetEmpire(pol) | 0xBA94A0 / 0xBA94C0 | `ret 4`; `map<pol, cEmpire*>` at star manager +0x150 (node value +0x14); cEmpire pol +0x84, name string16 +0x3C, home StarID +0xB0 |

Event ids (ModAPI `RelationshipEvents`) found at the call sites of ApplyRelationship: gift (comm
screen) 0x526E51F at 0x102CBC9 (after `AddMoney(-amount)`), break alliance 0x526E521, mission
complete / failed / rejected / aborted 0x526E519 / 51C / 51D / 51E (cMission 0xC46xxx, raids
0xC5BC08), destroy UFO 0x591F833 (0xC3D92D), destroy ally UFO 0x526E542, destroy building 0x526E53C,
trade 0x667AF08 (spice sale 0x1038CF2), cheat good / bad 0x526E5DC / 0x526E5F3. Computed by every
copy on its own: personality 0x5B6FCC9 / 0x5B6FCD4 and archetype 0x5FF85B2..B4 (MakeContact),
super power 0x601DF2A, allied with Grox 0x68B2938, embassy 0x580E23B, the empire AI's be nice /
be nasty 0x577909A / B (0xFE9xxx), avoided contact 0x5B942D0. CommunicatedMean 0x5F62736 applied
by hand changed the relation once but left no event in the map (✔ runtime).

### 12.3 Findings at runtime (lane 4, GOG 2016, new-game fixture `space`)
* **Political ids are identical in both copies**: the player 0x800021CA and the 2120 NPC empires
  (same ids and home stars) on host and guest, because the guest loads the host's save. The empires
  exist from the start of the Space game, so empires contacted after the join have the same ids
  too (checked for two contacts made after the join). ? Empires born later (an uplifted civilization, a colony split)
  would take `NextPoliticalID` 0xBA6760 on each copy and could differ: not seen.
* **Empire names and species are chosen per copy**: until contact every NPC empire is named
  "unknown"; after the contact each copy picks its own (the same empire was "Chitis Empire" on the
  guest, "Mustachio Empire" on the host): the species comes from each player's own creations.
  Notices therefore show both names: "Alice formed an alliance with Kirgle Empire (Alice: Judge
  Empire)".
* Arriving at an NPC empire's home star (`coop.space jumpto <home StarID>`) is enough for the first
  contact (MakeContact from the star visit); a badge cinematic may block the jump first (Enter).
* The game's debug cheats (`space -addAllies`, `ari -war`, `raid ...`) are not registered in the
  retail build (`cheat` returns false); `moreMoney` is.

### 12.4 Recipe: shared diplomacy (implemented)
Hook ApplyEvent, the four state functions, MakeContact and SetContactedByPlayer. A call that is not a replay
(`g_applying`) and not nested in another hooked call (`g_depth`) and pairs the player with an NPC
empire is sent: an event as `{npc, player first, id, scale}` (not the computed ones above), a state
change only when the flags really changed, a contact (level met or talk) when its flag went on. The partner replays
through ApplyRelationship / the state function / MakeContact with its own player. The host sends
its table (records with events or a state, plus the met flag) every 20 s and in answer to a guest's
request (on entering Space and the "Resync" button); a guest sets war/alliance through the state
functions, then, if the events differ, `ResetRelationship` + operator[] for each of the host's
events, and clears its own records the host does not have. A periodic table does not touch an
empire the guest itself changed in the last 8 s; an answer to a request does (the host handles the
request after the guest's earlier messages).

### 12.5 The game ends an alliance below a relation of 3 (✔ Ghidra both builds + runtime lane 2, 2026-10-24, branch fix/gog2024-tests)
* `FUN_01013370(cEmpire*)` (same address in both builds; called per empire from 0x1013650, the
  periodic pass over the empires with a relation to the player) reads
  `CalculateRelationship(npc, player)`; if allied (IsAllied) and the relation is **below the tuning
  value at +0x54 = 3.0**, it shows event 0x4CA76FBD and calls **BreakAlliance** (posts 0x43F2585);
  below +0x50 = 7.0 (and falling) it only warns (event 0x2194609D). The tuning object is the
  address `FUN_010407d0` returns: 0x15B9450 (GOG 2016, read at runtime: +0x40 1.0, +0x44 100, +0x48
  150, +0x50 7.0, +0x54 3.0, +0x58 3.0) / 0x15B8450 (GOG 2024, the same values at runtime, lane 9). Then war and peace are decided the
  same way (DeclareWar below FUN_00cbcc70's value, DeclarePeace above FUN_00b7e1f0's).
* So an alliance with a relation under 3 is not stable in any copy: `coop_space_diplomacy.py` made
  one gift (rel -0.69) and an alliance (event 0x526E524 +1, rel 2.44); a minute later one copy's pass
  broke it and the shared hooks broke it in the other (the "after the rejoin the guest is still
  allied" failure on lane 9, GOG 2024, and on lane 2, GOG 2016). The mod was right to share it. The
  test now gives a gift worth 3. Each DeclareAlliance adds 0x526E524 (+1.0 in the event map).

## 13. Galaxy map, roster, colonies of partners (✔ runtime, `space_map.cpp`, lane 2 GOG 2016, 2026-10-23)

Found while writing feature #1/#2 of 0.7 (partners and their colonies on the galaxy map) and the
roster every Space feature reads (`mods/coop/space_map.h`, test `coop_space_map.py`).

### 13.1 The own empire, read from memory (✔ runtime, ModAPI layout confirmed)
* `cEmpire` (GetPlayerEmpire 0x10211B0): cIdentityColorable at +0x0C (mIDColorID +0x10, cached
  colour ColorRGB floats **+0x30** (the space fixture: 0.69 0.35 0.48 = FFB1587B)), **mEmpireName**
  string16 **+0x3C** ("Fishtistic Empire"), mPoliticalID +0x84, mStars vector<cStarRecord*> +0x88,
  mHomeStar +0xB0 (StarID), mHomePlanet +0xB4 (PlanetID), money +0xD0.
* `cStarRecord`: position +0x3C, mEmpireID +0x54, **mName string16 +0x60**, StarID +0x70,
  **mPlanets vector<cPlanetRecord*> +0x84** (empty for a star whose system was never generated).
* `cPlanetRecord`: **mName string16 +0x18**, type +0x28 (1 rock/T0, 2-4 terrestrial, 5 the home
  planet in the fixture ~), scores +0xB0/+0xB4/+0xB8, **mCommodityNodes +0xE4** (spice geysers; 0 on
  every planet of the fixture but the home planet, 13 there), mCivData +0x15C (cCivData pol +0x14,
  mCities vector +0x3C), PlanetID +0x184, tech level +0x194 (5 = empire colony), spice key +0x198.
* The colonies of the own empire: every planet of `mStars` with tech level 5 and a cCivData of the
  player's political id (`SpacePlayerData+0x20` holds the same as cPlanet*, the records need no
  loaded planet).
* **Spice keys** `{FNV-1("spiceN"), 0x00B1B104 prop, 0x034D97FA SpaceTrading}`: 0x1CA01562 spice1 Red,
  0x1CA01561 spice2 Blue, 0x1CA01560 spice3 Yellow, 0x1CA01567 spice4 Green, 0x1CA01566 spice5 Pink,
  0x1CA01565 spice6 Purple (the prop files' texts "Red Spice" ..., Spore_Game.package and
  PatchData.package; no spice7+).
* **Star names are not stable:** the names of stars nobody named are made up when the galaxy loads:
  star 4587540 was "Indentia", "Tatiar", "Ipheb" in three loads of the same save; two players see
  different names for the same star. The roster carries the owner's names (Colony::star_name).
* **One empire:** a guest plays the host's save, so at the start both players have the same empire
  (same political id, home, colonies, colour). A colony founded later exists only in the founder's
  copy; a rejoining guest loads the host's world again and its own colonies are gone
  (`own_colony()` in space_map.h: the partner's colonies that are not mine).

### 13.2 The galaxy view on the screen (✔ runtime)
* `SkHost::world_to_screen` with the star record's position +0x3C gives the star's pixel in the
  galaxy view exactly (the test compares the drawn tag with the star: equal). It returns 0 also for
  points in front of the camera but outside the view (a star 8 units away at the default zoom), so
  off-screen directions are found by walking the segment from the own ship (on screen) to the target.
* In the galaxy view a partner's `PlayerState.pos` is its ship in galaxy coordinates (the same space
  as the stars; parked at a star it is within 0.15 of the star's position).
* `SpaceTeleportTo(cStarRecord*)` 0x10228C0 (galaxy view only) puts the own ship at the star at once,
  the view stays the galaxy view (✔ runtime: the tag click next to a partner in flight).
* Galaxy flight price `cSimulatorPlayerUFO_TravelCost` **0xFFC390** (thiscall(playerUFO, from, to)
  ret 8 → int; own stars and allies' stars cheaper): **0 for every star** in this game (the fixture,
  6 stars tried), so TravelToStar and SpaceTeleportTo cost nothing; a mod that sets a price would be
  charged by space_map's colony trips too.
* Hiding: `cCommManager::IsCommScreenActive` 0xAE92E0 (thiscall(`cCommManager::Get` 0xB3D570), bool)
  for comm screens; `cCinematicManager+0x2C` for cinematics; coop.cpp already skips the world labels
  under the pause menu (`menu_up`) and the co-op window. (A native mark, the star's own ownership
  ring, would need a fake owner in `cStarRecord+0x54`: that is feature #4's allied empire, not this
  one's; the map draws overlay text: a dotted ring of 10 `•` at 14 px around a colony star and a
  `▼ Name` tag 36 px above the partner's star.)

### 13.3 The colony tool (✔ runtime)
* Tool key `{0x733B280C (FNV-1 "placecolony"), 0, 0}` (`coop.space tool add placecolony`), the
  "Colony Incredi-Pak" in the colony palette (tab W-306, row 2 slot 1).
* Aiming refuses: "Can't use in this system" (star 4587540 of the fixture, empire FFFFFFFF, reason ?)
  and "Colonies must be placed near spice" (Vulpica: no commodity nodes) with red placement rings.
* `cPlaceColonyToolStrategy::OnHit` **0x105B930** (thiscall, `this` unused, (cSpaceToolData* tool,
  Vector3* pos, int hitType, int) ret 0x10) is the tool's effect without the aiming check: charge,
  ground clear, the delayed drop (callback 0x1058C60 → CreateColony). The drop's caption waits for
  OK; after it the planet has tech level 5 and 1 city (~14 s in the test). `coop.space map colonize`
  fires it at the ground under the ship: `cPlanetModel::GetHeightAt` 0xB7F040 (thiscall(model
  `cPlanetModel::Get` 0xB3D420, Vector3* dir) ret 4 → float: the terrain's distance from the planet
  centre; planet coordinates are centred on the planet).
* **A colony at the planet's centre faults in a loop** (0xC0000005 x4559 at 0xB7ED00 =
  GetGravity+0x7A0, calls 0xFF90A2 CreateColony / 0x1058CE6 the drop callback; the guest froze).
  The terrain cursor object (noun 0x18C40BC) sits at 0,0,0 while the mouse is not over the ground,
  and the planet's animals (0x18EB45E) read 0,0,0 through cSpatialObject in Space: neither gives a
  ground point.
* SpaceToolHit (ModAPI SimulatorEnums): 0 combatant, 1 ground, 2 water, 3 air (the hitType of the
  OnHit functions of §7.6 and 7.8).

### 13.4 The roster on the wire (`space_map.cpp`)
* MSG_SPACE kind 10 `K_ROSTER`: u32 version, u8 part, u8 parts; part 0 starts with the head (home
  star, home planet, place, galaxy position, empire name, star name, colour, political id, money),
  every part then has u8 n colonies (PlanetID, planet name, star name, spice key, spice name, cities,
  flags). Parts are cut at 1000 bytes (an empire of 250 colonies fits in ~20 parts). A roster is
  taken when all parts of one version arrived.
* Kind 11 `K_ROSTER_REQ`: a player who (re)joined or entered Space asks everybody for theirs.
* Sent on a change (read every second; the ship's galaxy position and the money do not count as a
  change), every 5 s anyway, and when a new player is in the session. Rosters of players who left
  are dropped (the session's player list).

## 14. Tool gifts and spice trade (`space_trade.cpp`, Ghidra both builds + offline props, 2026-10-23)

### 14.1 Tools in the inventory (✔ decompile; ~ runtime marked)
* `cSpaceToolData` **is** a `cSpaceInventoryItem` (first base, size 0x2A0, ModAPI layout holds):
  the item pointer of `GetItem` / `GetTool` (vt+0x68) is the tool; the safe cast is the
  ISimulatorSerializable sub-object at +0x0C, vt+0x0C `Cast(0x707459F0)`. Item vt+0x18 key
  (`&item+0x5C`, tools `{instance, 0, 0}`, spice too), **vt+0x28 = the localized name**
  (`LocalizedString(+0x34)::GetText`, char16_t*, 0xC870F0), vt+0x2C the detail text (+0x48).
* `cToolManager::LoadTool` 0x104E350 (both builds): new tool, `+0x18` price = prop 0xF0609ECE,
  `+0x7C` charges = prop **0xF0609ED9 (initial charges)**; max charges +0x1A0 come from prop
  0x02B24D49 (99 for every consumable), "uses ammo" (flag 0x20 of +0x174) from prop 0xF0609ED3.
* Base TakeItem 0xC87F70 (vt+0x7C under the player's 0xFF40B0): a tool with "uses ammo" and a
  count other than -1 is **split**: `ConsumeAmmo(count)` on the tool (only if it has that many,
  else nothing happens but it still returns true), `AddAmmo(count)` on `*out` if any; the player
  wrapper then posts 0x635AE7E (item changed) / 0x5E793AD (removed) and **0x57F3749** (the
  palettes refresh). AddItem 0xFF4780 merges a tool into the same tool with AddAmmo (clamped) and
  refuses (returns 0) a tool whose prerequisite tool key (0x104BE90) the inventory lacks: an
  upgrade replaces its older version (item position copied, old one removed).
* AddAmmo 0x104CBE0 clamps at +0x1A0 and does nothing at or above the max; ConsumeAmmo 0x104CC10
  does not clamp. Same addresses and bytes in the GOG 2024 build.
* The 363 tool props (`spacetools~` 0x30608F0B in PatchData.package / Spore_Game.package, binary
  props: big-endian header, string16 values little-endian, keys 16 bytes) have the internal name in
  prop 0x00B2CCCA. The 37 with charges ("uses ammo"): Antimatter Bomb 144DF50A, Planet Buster Bomb
  05475298, Genesis Device 72E076A7, terraforming Asteroid BBE8451A, Ice Comet A1C9D86E, Atmosphere
  Freezer 07ADE670, Atmosphere Generator 931426A0, Drought Generator 04A8669D, Ice Storm 138722CD,
  Meteor Shower A86778EC, Volcano 4FC5AB9D, colony and city tools Place Colony 733B280C, Monolith
  14A7D231, Bio Protector F55D419A, Bio Stabilizer B7D22F27, Happiness Booster 4A1E2E7B, Loyalty
  Booster 44B420E4, Spice Storage B4150FEB, Uber Turret E4EC3F34, Fireworks F4ED3F95; and
  Galactic Adventures / NPC / archetype ones (Create/Edit Creature, Energy/Repair Potion, Epicize,
  Eradicate, AOE Repair, Defense Missile, Place Embassy, Zoo Planet, Global Happiness, Planet Scan).
* **Giftable** (space_trade.cpp `gift_denied`): every ammo tool except the second group (adventure
  captain tools, NPC ship weapons, archetype abilities that recharge by themselves and belong to
  one play style). Tools without charges (weapons, drives, scan, abduct, sensors) are permanent
  unlocks and rank rewards: giving one would strip the giver and skip the partner's progression.
* Gift recipe: giver `i = IndexOf(2, key)` (vt+0x64), `TakeItem(i, &null, n)`; receiver with the
  tool: `AddAmmo(n)` (kept = after - before), else `AddItemByKey(key)` (the "new tool" event, the
  tool comes with its initial charges) then set exactly n with ConsumeAmmo/AddAmmo; post
  0x57F3749 through `IMessageManager` vt+0x18. What is not kept goes back to the giver.

### 14.2 Spice prices (✔ decompile both builds)
* `cSpaceTrading` (`Get` 0xB3D4A0 / 2024 0xB3D4D0): +0x20 `mSpices` (vector<ResourceKey>, 6
  spices, keys `{1CA01560..1CA01567, 0, 0}`), +0x34 every `spacetrading~` key, +0x48 NPC stores.
  A commodity's `spaceTradingType` (prop 0x34F1A4F) is 0x082AF541 for spice.
* Value of one unit = spacetrading~ prop **0x34F1A4A** (float: red 1CA01560 4500, 1CA01561 5230,
  1CA01562 3375, 1CA01565 14625, 1CA01566 11242, 1CA01567 10125) x the difficulty multiplier
  `tuning->GetFloat(0x1C2E590D)` with `tuning = [cGameNounManager+0x74]` (0xC75DB0, 2024 0xC75DD0:
  array prop [+0x11F4] indexed by the difficulty [+0x1248]).
* NPC store (GenerateNPCStore 0x103E8F0): a new store (0x103CD10) picks the 3 spices it wants
  (not the planet's own) at value x one of {0.6, 0.75, 0.85, 1.0, 1.2, 1.4, 1.65, 2.0, 2.5, 3.0}
  (cNPCStore +0x60 keys, +0x9C prices); what it pays for the player's spice (0x103D9C0, list
  +0x88): a wanted spice at that price, any other spice at **5% of the value** (0x1487954); it sells
  its planet's own spice (0x103E6F0) at the value (0 on the player's own planets). Limit: the store
  stops buying after `spiceBought` (+0x30) reaches a tuning value (0x102FDF0).
* So "twice the market value" (space_trade.cpp default sale) beats 8 of the 10 random store
  factors and every unwanted-spice price; in rivals mode the offer is 1.5x.

### 14.3 Trade between players (design, ✔ two-game test `coop_space_trade.py`)
* Spice cannot go into a colony's own storage (`cCityData` +0x1C holds the planet's own spice
  type only), so a partner colony's spice lands in the partner's **cargo hold** (a trading object,
  stacks or takes a free slot). A colony does not buy the spice its planet makes.
* Default mode (one shared budget): the seller's game adds n x price (shared.cpp carries the
  change to everybody): an export bonus paid by the galaxy, zero-sum otherwise. A colony buys at
  most 30 units per 10 minutes (against selling the same spice back and forth).
* Rivals mode (`spaceext::rivals()`): the seller's spice is held back, the partner's page shows the
  offer (Accept / Decline, declined by itself after 2 minutes), the partner pays from its own
  budget and gets the spice, then the seller is paid; a decline, no money or no room returns it.
* The receiver of a gift / sale / offer always decides and answers; the sender gives back what it
  held when the answer is "no", when the partner leaves before answering, or when its own session
  ends (then the partner may have got it too: a known edge).

## 15. A partner's colonies as an allied empire (`space_allies.cpp`, ✔ runtime, lane 9 GOG 2024, 2026-10-23)

How a planet of my galaxy is made to belong to another (made up) empire, and how to undo it so that
no save keeps it. GOG 2016 addresses (decompile with tools/ghidra/q.sh); the GOG 2024 ones are in
`overrides_dc04aee5.sym`.

### 15.1 Empires and political ids (✔)
* `cStarManager` +0x150 = `eastl::map<uint32 pol, intrusive_ptr<cEmpire>>` (find 0xE5C7A0, header
  +0x154; `GetEmpire(pol)` 0xBA94A0), +0x12C = map pol -> home `cStarRecord` (same map type),
  +0x1D4 = the political id counter (`NextPoliticalID(bool high)` 0xBA6760: `[+0x1D4]++`, | 0x80000000
  with high; empires and the player use high = 1), +0x1D8 = the player's pol.
* **The game makes an empire lazily** in `GetEmpireForStar(star)` 0xBB14B0: a star with `+0x54 == -1`
  gets `NextPoliticalID | 0x80000000`; when the map has no empire for that pol: home star map
  (0xBAF7A0), `CreateEmpire(pol)` 0xBAF220 (new cEmpire 0x158, ctor 0xC34970, +0x84 = pol, into the
  map), `cEmpire_Init(pol, isPlayer)` 0xC35AE0 (isPlayer = home star flags & 3: player-owned home),
  `AddStarOwnership(star)`, `SetHomeStar(star)` 0xC30D50, money from props. A civilization planet
  that grows to space (0x100A000, case tech 4 -> 5) uses exactly this plus `SetContacted` 0xC30AF0,
  `SetSpeciesProfile`, `AddPlanet` 0xC34FB0, `SetHomePlanet` 0xC31800 and a relationship event.
* cEmpire fields: +0x10 colour id (0x53DBCF3 + 0..11 = the 12 palette colours, returned at once by
  `UpdateAndGetColor` 0xC32E10; 0x53DBCF1 = the player's species colour, 0x53DBCF2 unset), +0x30 RGB
  floats, +0x3C name (eastl::string16, ctor sets u"unknown"; `SetSpeciesProfile` 0xC337D0 rewrites it
  from the species name via `cIdentityColorable::AssignNames`), +0x50 flags (0x10 owns stars, 0x40|0x04
  player), +0x54/+0x58 archetype, +0x5C enemies vector (add 0xC34730, remove 0xC328E0), +0x70 allies
  vector (add 0xC347C0, remove 0xC32970; `IsAllyOf` 0xC31D30), +0x84 pol, +0x88 stars, +0xB0 home
  StarID, +0xB4 home PlanetID, +0xD0 money, +0x104 controller (NPC: 0x28-byte object 0xEAB3E0(3),
  grox 4).
* `cEmpire::Destroy` 0xC34460 is loud (event log 0x88426556 "empire destroyed", message 0x4B33711,
  every star released): not for undoing. Quiet removal: erase the pol from the map +0x150
  (`MapUintPtr_Erase` 0xBAA280 = map<uint32, intrusive_ptr>::erase(&key)) and the home star map
  (0xBAF7A0(pol, null)); the mod keeps its own reference and puts the same object back on the next
  apply (`MapUintPtr_At` 0xDD85C0 = operator[]), so nothing that still holds it sees it freed.
* NPC empires grow by themselves: 0xFEA3A0 (periodic, for empires near the active star whose
  stars lack flag 0x100 and whose +0x50 bit 0x40 is clear) -> `cEmpireGrowth_Expand(pol)` 0xFE9410
  (colonises or upgrades a star). The mirror is skipped there (hook), or it would leave stars of its
  own behind.

### 15.2 Stars and planets (✔)
* cStarRecord: +0x4C tech level (`SetTechLevel` 0xBB9C10; the max of its planets, 0xC8B760), +0x54
  empire id (0xA16980), +0x5C flags (0xBB9C40: 0x1/0x2 player home / owned, 0x4 home star of an
  empire, 0x8 ?, 0x40 the filtered planet copy at +0x98, 0x100 small colony: 1 city), +0x60 name
  (the names of unnamed stars are made up anew in every game), +0x70 StarID, +0x74 species key
  (12 bytes), +0x84 vector<cPlanetRecord*>, +0xAC planet count.
* **Planet records are resources** {PlanetID, 0x5220CB8, 1} in the save's planet DB
  (`planetRecords.pkp`). The game empties a star's vector +0x84 for systems far away;
  `cStarRecord::GetPlanetRecords` 0xBBA8D0 (-> 0xBBA780) loads them again (from the resource cache:
  the same object while somebody holds a reference). A star never visited has count 0:
  `RequirePlanetsForStar(star, 0)` 0xBB4C60 generates them (same PlanetIDs in every copy).
* **The save writes every cached planet record**: `SaveGame` 0xB28FD0 ends in 0xBB4D10 -> 0xBAC510,
  which takes every cached resource of type 0x5220CB8 and writes it to the planet DB, also those of
  systems the player left long ago. Undoing a change through the star's vector therefore misses
  records the game dropped from it (seen: a mirrored planet stayed a colony in the save while its star
  was restored). The mod holds a reference to each record it changes and restores it by pointer.
* Record-level colony (what a mirror colony is): star `+0x54 = pol` (+ `AddStarOwnership`), then
  `cPlanetRecord_SetTechLevel(rec, 5)` 0xC70850 (cdecl), `FillPlanetDataForTechLevel(rec, 5)`
  0xB97040 (cdecl; -> 0xB96E40: a new cCivData 0x50 with pol = the star's empire and
  `num_cities_in_colony` cCityData 0xC4 of type 0xFD701F6F, pushed with `AddCivData` 0xB8EFA0 to the
  raw-pointer vector +0x15C; flag 0x40 of +0x2C), `cEmpire_AddPlanet(rec)` 0xC34FB0. The cCivData ctor
  0xFF32F0 takes a political id of its own from the counter too.
* Landing there (✔ screenshot `allies_planet.png`): the planet panel shows the empire's name and
  species portrait, the planet has one colony city of that empire (cCity +0x30 = its pol; the 10
  cCityData make one city on a fresh colony), its turrets shoot animals, not an ally's ship. The first
  star I tried (the nearest free one of the fixture) is the tutorial's ruined colony: there the city is
  a scripted ruin.
* A loaded cPlanet (cStarManager +0x50 list) carries the pol in its cGameData (planet+0xD4 vt+0x48
  SetPoliticalID; read at planet+0x104): set with the record, restored with it.

### 15.3 Contact and relationship (✔)
* cPlayer = `cGameNounManager +0x74` (0xF67D40); +0x11D8 = sorted vector map pol -> contact flags
  (operator[] 0xF41940; setters 0xC7BCE0 |1, 0xC7BD00 |2 contacted (`PlayerContactedEmpire`
  0xC7A890 reads it), 0xC7BD20 |4 aware (`EmpireIsAwareOfPlayer` 0xC7A8F0), 0xC7BD40 8, 0xC7BD70 0x10,
  0xC7BDA0 0x20).
* An empire the player has not contacted greets him when he enters its territory: cEmpire 0xC31690
  sets "aware" and, not contacted, starts a first contact comm event (0xAEB330): an envoy ship and
  (the first time in a game) the tutorial caption "Drake's equation was right... a living alien race!".
  That first contact **reset the alliance** ("Your alliance with the Alice's empire has been
  severed."). The mirror is marked contacted + aware (0x6) up front: no envoy, no reset.
* Relationship data: `GetRelationshipMap(polA, polB)` 0xD01AE0 (thiscall on the manager, ret 8: the
  galaxy map relMgr+0x24 or the active planet's), `GetRelationshipData(map, a, b)` 0xD01560 (ret 0xC,
  null when there is none), flags at data+4: bit 0 war, bit 1 ally (`DeclareWar` 0xD065E0 /
  `DeclareAlliance` 0xD06960 set them plus the empires' vectors, and post UI messages: 0x4445D43
  when the player is one side). `ResetRelationship(a, b)` 0xD05870 makes a missing entry (0xD05770).
  The mod writes the flags and the vectors directly. With the alliance the panel shows the happy face
  and "We like new acquaintances +30".
* There is no erase for relationship entries; `cEmpire::Destroy` does not remove them either (the
  game keeps the entries of destroyed empires). The mirror's entry (flags cleared) stays in a save.

### 15.4 The save: never with the mirror (✔)
* kMsgSaveGame 0x1CD20F0 -> handler 0xB29930 posts 0x685DBFA -> after a delay
  `cGamePersistenceManager_SaveGame(name, bool)` 0xB28FD0 (thiscall, ret 8); the quit save 0xB295D0
  calls the same function. Everything (stars 0xBAEE30, planet records 0xBB4D10, the serializer) is
  written inside the call.
* The mod hooks 0xB28FD0: before the original every mirror is undone (planets by pointer, stars, the
  empire out of the maps, contact flags and relationship flags back); 3 s after it the mirror is
  applied again (the same empire object and pol, the same cCivData: the cities keep their state).
* Space saves only off-planet (the handler refuses on a planet with event 0x6251278C), so a mirror
  planet is never the high-LOD planet when it is undone for a save.

### 15.5 Raids on a mirror colony (✔)
* The game's raid code works on any planet: `StartRaidCheat` 0x100E620 (`raid -start`; stdcall-like
  `(unused, cPlanetRecord*, bombers)` ret 0xC, ecx unused) -> CreateMission(0xA4B0A2DF, rec, empire
  owning the star). On a mirror colony in my galaxy with me on the planet: pirate raiders (type 4,
  pol -1) and the colony's own defenders (type 6, pol of the mirror empire) spawn and fight.
* The manager takes a finished raid through 5 or 6 to 1 within one update; won vs lost is told by its
  ships (`ufo+0x800 == mission`): none left alive = won (a failed raid sends them away alive).
* Mission list: `cGameNounManager::GetData(cb1, cb2, cb3, cb4, 0x2AA5ADA)` (the callbacks as pushed by
  `ThereIsEventInPlanet` 0xFEE020), vector at result+4.

## 16. The galactic core (✔ static analysis + runtime, lane 5, GOG 2016, 2026-10-23; feature `space_finale.cpp`)

How the game finds, opens and rewards the core, and what the co-op finale builds on.

### 16.1 The core star
* The core is an ordinary `cStarRecord` of **type 1** (`+0x48`, StarType::GalacticCore) at the
  galaxy centre (0, 0) (✔ runtime: StarID 8519680 = 0x820000, sector 2080 of 4096, index 0; the
  galaxy centre global `0x16DC9F8` (GOG 2024 `0x16DBA18`) is (0, 0, 0)). There is one; find it by a
  scan of the star table `cStarManager +0xC8` (vector of 20-byte sectors = vector<cStarRecord*> +
  8 bytes; all sectors are filled after the load, ~2000 of them non-empty).
* The "approaching the core" zone: `cStarManager +0x1D0` = 100 (radius, 2D distance from the
  centre), `+0x1CC` = 70, `+0x1C8` = 3 (✔ runtime). Inside it the travel range shrinks
  (`0xFFC6A0`: range lerps down to +0x1C8 near the centre) **unless the player has flag
  0x61F1454** (Steve's gift), and the first jump inside plays `SPG_TravelNearCore` with the event
  0x5F776D6 "As you approach the galactic core, it gets harder to plot a course..." (TravelToStar
  0x10022BF). The Grox live around it (squadron.cpp mirrors their ships like any NPC UFO).
* Travel: `cSimulatorPlayerUFO::TravelToStar` has no range check (the range is a UI limit), so
  `SpaceTeleportTo(coreRecord)` 0x10228C0 (galaxy view only) puts the ship at the core star:
  active star = the core, `ufo+0x74C` (arrived) set. That is "at the gate".

### 16.2 Entering the core
* Two steps, like a black hole: (1) in the galaxy view at the core star, zooming in enters the
  core's **"solar system"** like any star (galaxy update 0x100CF70: zoom <= threshold 2 at
  `0x16DE364`, ship arrived `ufo+0x74C` -> `EnterSolarSystem` 0xFDD590; for a star of type 1 that
  adds the progress of achievement "42", 16.3; the view shows the bright core, the system name
  "Galactic Core", no planets). (2) In that system the system update **0x01009C90** (every frame)
  opens the core when the camera is zoomed in (threshold 1 at `0x16DE3B0`, controller +0xD5) with
  the ship in the centre and `0x10422F0` allows it: ship arrived and the active star is a black hole
  with the wormhole tool, or **the core and `cPlayer::HasFlag(0x681C28E)` is false**, no comm
  screen, no pause. Black hole -> wormhole 0xFDD4E0; **core -> `0x01002DD0`**
  (`SporeCoop::Space_EnterGalacticCore`, cdecl, no args): `SetFlag(0x681C28E)`, full health and
  energy, chronicle event 0x57BAFFD3, then `PlayCinematic("SPG_CenterOfGalaxy")`. While the
  conditions hold 0x1009C90 calls it **every frame** (332 calls in ~10 s with the hook blocking it),
  after the flag it does nothing: the core opens once per save. The same update zooms out to the
  galaxy (0xFDA5D0) and lands on planets (0xFDF5A0).
  Runtime (lane 5): galaxy view at the core -> mouse wheel in (ctx 1, "Galactic Core") -> a click
  at the screen centre flies the ship into the centre -> wheel in -> 0x1002DD0.
* The flag is also the comm condition "EnteredGalacticCore" (0x8B, table at 0x1402C88): aliens
  talk about the trip afterwards.
* `cPlayer` = `cGameNounManager::Get()+0x74` (0xF67D40 is `mov eax,[ecx+0x74]`). Its flags:
  hash set at +0x1120 (buckets) / +0x1124 (count): **`HasFlag` 0xC77440 / `SetFlag` 0xC77D70**
  (GOG 2024 0xC77460 / 0xC77D90), thiscall(player, id) `ret 4`. The flags live in the save: a guest
  joins with a copy of the host's save and so has the host's flags.

### 16.3 The cinematic SPG_CenterOfGalaxy and Steve's gift
* The script is the cinematics ArgScript file (Spore_Game.package group 0x40890200 instance
  0xF83E63C9, type 0x24A0E52, text): states SPG_CenterOfGalaxy .. _TransitionToGalaxy. It sends
  messages handled by **`cPlayer` 0xC77EB0** (GOG 2024 0xC77ED0, listener = player+0x34):
  0x6667038 / 0x6666683 place the UFO, 0x6524498 journey effect, **0x6526395 "player gets tools"**,
  0x6527231 reverse travel effect, **0x6527EAF** -> `cAppModeSpace::LeaveSolarSystemToGalaxy`
  0xFDA5D0 (back to the galaxy view, the session goes on there).
* The texts (table 0x974299BE "cinematics_spg"): 0x067B778C "Greetings my friend! You have
  traveled very far...", 0x067B7833 "I'm Steve. You are now to be given the power...", 0x06526580
  "You have been awarded the Staff of Life." (this build calls him Steve, not Steko). The player
  has to confirm three times: `wait -forContinue` after each scrolling text and `wait -forNext`
  after the award text (Enter works). The whole cinematic takes ~2 minutes (runtime: 1 min 50 s with an
  Enter every 8 s); it ends in the galaxy view at the core (context 2).
* **0x6526395**: if `!HasFlag(0x61F1454)`: `cPlayerInventory` 0xFF3CA0 adds every tool of the
  list at **inventory +0x60..+0x64** (5 tool objects in the tested save, prepared at load) with its
  charges, `SetRareAsFound(0xA9E151E4)`, event 0xDE5EDDE2, `SetFlag(0x61F1454)`. The Staff of Life
  is the tool **genesisdevice** (key {0x72E076A7, 0, 0}; text 0x0BA5C1BF!0x0C7C683C "Staff of
  Life"; runtime: after the cinematic the Staff of Life sits in the cargo palette with **42 charges**, the
  "42 more"). Sending this message with `IMessageManager` MessageSend (vt 0x14, (id, nullptr, nullptr))
  gives a partner the gift natively, once (the game's own flag).
* Achievement "42" ("Find the center of the galaxy", spg-42.prop, id 0xF88C1342):
  `cAchievementsManager::Get()->AddProgress(0xF88C1342, 1)` 0x676E50 (`ret 8`); the game calls it
  in EnterSolarSystem (0xFDD8CB) for a star of type 1.
* While the cinematic plays: `cCinematicManager +0x2C` != 0 and `+0x148` = 0x2F7C7BDA (the id of
  SPG_CenterOfGalaxy; the UFO controller 0x105F2E0 checks it).

### 16.4 What the co-op finale does with it (`space_finale.cpp`)
* Zones per player (0.5 s): far / near (inside +0x1D0) / gate (active star = the core: in the
  galaxy view at it or in its system), flags entered (0x681C28E), staff (0x61F1454), waiting; sent
  as MSG_SPACE 40 every 3 s and on a change, with the active star and the last star before the core.
* Hook 0x01002DD0: while partners in space are not at the gate the call is dropped (the core
  stays closed, the player is told once, the partners get MSG_SPACE 41 "waits for you"); the Space
  page has "Enter now". When everybody is at the gate (or after "Enter now") the mod calls the
  original outside the frame (only in the core's system, ctx 1). After the original set the flag:
  MSG_SPACE 42 "entered".
* On 42: a partner at the gate enters too (from the galaxy view first `EnterSolarSystem`, then the
  original: its own cinematic). Everybody else gets Steve's gift: message 0x6526395 by MessageSend
  + `AddProgress(0xF88C1342, 1)`, checked every 3 s while the gift is missing (a world load on a
  rejoin replaces the game state, the gift of the old world is gone), and can still visit the core
  (the flag 0x681C28E stays unset).
* "Fly to <name>" (only to a partner in the near zone or at the gate; needs the interstellar
  drive): in the galaxy view `SpaceTeleportTo(partner's star)` (+ `EnterSolarSystem` when the
  partner is in that system or at the gate); in a system or on a planet the jump of the Space page
  (`spacecoop::travel_to_star`, the respawn's native sequence) into the partner's system, for a
  partner at the gate into the system it came from; from there the player zooms out and flies on
  (a second "Fly to" in the galaxy view goes to the gate).
* Not done by the mod: a system -> galaxy switch. Calling 0xFDA5D0 outside the game's zoom update
  worked, but a later Enter crashed the game in UI code (AV at 0xCCCCCCCC from App::Canvas::Tick);
  (it turned out that the crashes came from the next bullet, so 0xFDA5D0 itself may be fine ?).
* Pitfall found here: the tool part of an inventory item is an **embedded base at item+0x0C**
  (`Cast` = `(item+0xC)->vt[3](0x707459F0)`, no AddRef). Reading `*(item+0xC)` as a pointer and
  calling Release on the "tool" corrupted the inventory: AV at 0xCCCCCCCC from App::Canvas::Tick
  (stack 0x847EFA, 0x84930F, 0x84935E) seconds later. `cPlayerInventory::GetTool` 0xFF3BF0 is the
  safe way; it also finds tools that are not owned yet (a second list at +0x4C..+0x50): check
  `HasTool` (vt 0x74) first.

## Freed attackers and star ids past the table (2026-10-27, fix/w1-space)
<a id="space-stale-lookups"></a>

* ✔ A mirror freed through `DestroyInstance` (`game::destroy_creature`) leaves the UFO noun list at
  once: `game::ufo_listed` is false in the same frame. A puppet freed the same way on the host is
  gone from the list too; `drive_puppet` spawns a new one on the next tick. Hit messages are handled
  before `squadron::tick`, so the handlers check the attacker themselves (BUG-0054).
* ✔ The memory of such a freed ship stays mapped: the game's TakeDamage read it without a fault
  in the test (`coop.fleet hitfix 0`). The bug is silent until the block is reused.
* ✔ cStarManager +0xC8 sectors: `{begin, end, ...}` (20 bytes). The word right after a sector's
  last record (sector 0x046 of the `space` fixture, 61 stars) was 0x00000131, not a pointer;
  `IsBadReadPtr(0x131, 0x74)` raised a caught access violation in kernel32 under Wine. Always
  check the index against `end` before reading (BUG-0092).
* ✔ The rescue's repair timer: the first `down_tick` after a disable must start from a zero step;
  the old function-local `last` kept the previous disable's time (repair=0.6 after 0.15 s on the
  second disable, BUG-0147).

## Ship state chunks, the posse, trade refunds, mirror empires (2026-10-28, fix/w2-space)
<a id="space-ship-state-chunks"></a>

* ✔ `cSimulatorSpaceGame::DestroyNPCUFOs(keepAmbush, keepPosse, bool)` 0x1009310 (GOG 2024: the
  same address as GOG 2016) walks +0x78 and keeps exactly `mUFOType` 3 when keepPosse is set and
  type 8 when keepAmbush is set (disassembly: `cmp eax,3 / je` to the keepPosse test, `cmp eax,8`).
  So type 3 is the posse (escort) and the mod never removes it as a host NPC (BUG-0084).
* ✔ A type 3 UFO made with `SpawnUFO(3, &ownPol, key)` (the player's own political id) is freed by
  the game at once (not in the UFO list in the next call, its address reused); with pol -1 it stays.
  The real posse is made another way (comm screen recruitment, not traced).
* ✔ The host's ship state (MSG_UFO_STATE, 10 Hz) is 18 ships per unreliable chunk; with 20 host
  ships and one chunk lost the old guest code exploded 18 mirrors and spawned them again. The guest
  now needs every chunk of a cycle before it removes anything (BUG-0037, `coop.fleet list`:
  `partial_cycles`, `stale_chunks`).
* ✔ In the galaxy menu (GalaxyGameEntry) `cSimulatorSpaceGame +0x40` (the player's inventory) is
  null: `coop.space trade spices` prints "free cargo slots -1". A refund there cannot be added
  (BUG-0058: now owed until back in Space).
* ✔ Each allies mirror made with `ensure_empire` (NextPoliticalID + CreateEmpire) advanced the
  political id counter by 2 in the tests (mirrors 800021CD, 800021CF, ...).
* ✔ Going to the menu with an applied mirror: the game saves on the way, the allies save hook
  unmirrors (applied = 0), and after the load the mirror's cEmpire is no longer in the map; the old
  code put the old object back at the old id (BUG-0038). After the load the counter
  `cStarManager +0x1D4` was above the mirror's id (21CF before, 21D0 after), so the collision with
  an NPC empire of the loaded galaxy was not seen on this path.
* ✔ After a save (the allies save hook unmirrors first) the mirror's cEmpire is no longer in the
  star manager's map (`GetEmpire(pol)` != the object) although the galaxy is the same; the player's
  cEmpire pointer (`GetPlayerEmpire`) does change with a load, so the mod uses it to tell a reload.
* ✔ `cStarManager::GetEmpire` does not return an unapplied mirror's cEmpire after `allies drop`
  (the "no empire" answer); `allies detach` (tests) keeps the empire with its star instead.

## Trips, clicks, Defenders, seeding threads, armada waves (2026-10-28, fix/w3-space)
<a id="space-w3-trips-clicks-defenders"></a>

Runtime findings of the wave 3 Space fixes (lane 6, GOG 2024):

* ✔ `cRelationshipManager::ApplyRelationship` called as `void` leaves its ST0 result on the x87
  stack: three `coop.space empire contact` calls moved TOP from 0 to 5 while the tag word still
  read FFFF ("empty"). A check of the x87 state has to look at TOP (`coop.space fpu`), not only the
  tag word (BUG-0126).
* ~ `SpaceTeleportTo` in the galaxy view to the star next to a partner did not spend energy or
  damage the ship with energy 0 (energy 0 -> 0, HP unchanged): the cost of section 7.3's
  TravelToStar was not seen on this short path. The co-op teleport (`spacecoop::teleport_in_galaxy`)
  still refunds as `travel_to` does, and refuses a disabled ship (BUG-0063).
* ✔ Clicks: SporeKit's background mode (on for test runs) hooks GetForegroundWindow / GetActiveWindow
  / GetFocus to return the game window, so a focus check in the mod proves nothing in the lab. The
  mod takes primary clicks from the game window's WM_LBUTTONDOWN through a thread message hook
  (WH_GETMESSAGE on the main thread, `mods/coop/mouse.cpp`): the logical primary button (swapped
  mice), only for clicks on the game window; the tests' `click` posts the same message
  (BUG-0090, BUG-0145).
* ✔ A Defender the guest's own game makes (SpawnUFO type 6 with the player's political id, as a
  colony's defence) in the host's place is now removed like the other local NPC ships. Our puppets
  are told apart by what space_spawn gave them: model key +0x780, mbInvincible (combatant +0x80 =
  ufo +0x588) = 1, no owner mission +0x800 (`modes::space_puppet_ok`, BUG-0111).
* ✔ The planet generation's RNG redirection is per thread (`thread_local`, native TLS in the
  llvm-mingw DLL works under Wine). With the old shared state, another thread "inside
  GenerateRandomName" made the guest generate 5 planets of 00460024 where the host made 6, and 4 of
  0046002F against 1 (BUG-0123). No off-main-thread call of GenerateRandomName or
  RequirePlanetsForStar was logged in these runs; the mod warns once if it sees one.
* ✔ Armada: the game keeps a raid active (state 3) while nobody is on the planet; a host who left
  for the system view and landed again got a second raid on the same colony (the old wave start),
  30 ships counted for the wave. The wave now keeps its mission and resumes it (`contracts raids`,
  `resumed=`). During a wave the mouse wheel at the ship did not take the host off the planet
  (`coop.space takeoff` in the test) (BUG-0046).

## Object serial, the Space world epoch, disabled ships, gifts, puppets, armada returns (2026-10-28, fix/w5-space)
<a id="space-object-serial"></a>

* ✔ `cGameData::mID` (+0x24) is written in `cGameNounManager::CreateInstance` (GOG 2024 0xB20BF0):
  `mov eax,[0x167BE28]; inc eax; mov [0x167BE28],eax; mov [esi+0x24],eax` (0xB20C83..0xB20C8E).
  CreateInstance is the only writer of the counter 0x167BE28 in the code. `game::object_serial(u)`
  reads +0x24 (only after `ufo_listed` / `object_alive` in the same frame). Seen at runtime: a
  puppet's serial stays the same over a whole test; the identity tests below rely on it.
* ? The counter's address is also stored as data in two static tables (0x131AD5A and 0x131B076:
  `mov [esp+0x18],0x167BE28` next to other function pointers, then `rep movs` into a global): most
  likely a serializer's field list, so a load may set the counter back and objects read from a save
  may keep saved ids. Not measured. Until it is, an identity kept across frames also compares the
  Space world epoch (below); serials are never compared across a load or between machines.
* ✔ The Space world epoch (`spaceext::world_epoch`, a Space-only stand-in for the stage epoch of
  BUG-0217 / #151): +1 on the first kSpace frame after a frame in any mode other than kSpace, kEdit,
  kEditor or kScenario, and when the player's cEmpire changes between two non-null values in
  kSpace. A load of the same game from the galaxy menu (`coop.menu`, `coop.load`) went 1 -> 2
  (coop_space_down_away.py part b). Not verified: that the captain outfitter, the ship editor and an
  adventure from Space show only kEdit / kEditor / kScenario frames (the log line "space: world
  epoch N (Space entered)" shows a false change).
* ✔ `coop.space away 1|0` (test hook, cleared at session end): spaceext::in_space() and spacecoop's
  in_space() / inventory() act as outside Space while the Space world stays; trade's
  `coop.space trade away` stays its own hook.
* ✔ Disabled ship (BUG-0228, BUG-0229): the state records the ship (pointer, serial, world epoch).
  Outside Space in the same world its 20 s stand still (25 s away: 19.0 -> 16.9 s left); another
  world ends it without a death; its invincible byte is cleared at once in Space, or on the first
  Space frame back on the same ship (`coop.space downstate`: `owed`, `owed_cleared`).
* ✔ Spice gift (BUG-0226): K_GIFT has no id and only a failure is answered (K_GIFT_BACK). The
  sender keeps its unanswered gifts (16 at most, cleared at session end); a K_GIFT_BACK matches
  the oldest with the same receiver, key and count once. Outside Space it is owed until the first
  Space frame of the same world (`coop.space gift`). `add_spice` refuses outside Space.
* ✔ Puppet identity (BUG-0218): our puppet is the object with the serial recorded at its spawn
  (and the model key). The invincible byte is not part of it any more: when it is found cleared it
  is set back (`coop.fleet ident`: `flag_restored`). A puppet whose owner mission (+0x800) is set
  is retired: held still, not sent to the guests, its shots dropped, destroyed through the deferred
  path once +0x800 is null again, left to the game at session end. Whether the game ever sets
  +0x800 or clears +0x588 on a Defender with our political id is still not known.
* ✔ Armada returns (BUG-0227): the "HP raised once" set is keyed by serial; a resumed wave waits for
  its ships again and, if none come in 30 s, its raid is aborted with `cMission::SetState(7)`
  (spacecoop's raid hook sends no K_RAID and pays no bonus for 7) before a new one starts; a raid
  that ended with 5 / 6 while the host was away counts.
