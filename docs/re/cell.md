# Cell stage RE notes (GOG 2016 build, sha256 922af7c7…)

Static analysis only (objdump + Ghidra on a windowed copy 0xE40000–0xE90000). All addresses are VAs of
this build. "✔" = verified in this build's disassembly (convention checked at call sites / `ret`),
"~" = behaviour inferred from decompilation, "?" = unverified guess.

Globals: `G = *(uint8_t**)0x016B4BE4` (cCellGame, ✔). Serializable data `S = *(uint8_t**)(G+0x5190)`
(same object as `*(void**)0x016B4DC0` = cCellSerializableData::_ptr, ✔ - ModAPI m17 0x16B3DE0 + 0xFE0).
cCellUI `*(void**)0x016B4BEC`, cCellGFX `*(void**)0x016B4BE8` (already in overrides).

## 0. Object pool (cObjectPool, used for cells G+0x1C and the event pool G+0x54) ✔

Layout: `+0 data, +4 freeHead, +8 generation, +0xC liveCount, +0x10 highWater (slots ever used),
+0x14 objSize (0x398 for cells), +0x18 capacity`. Index = `slot | gen<<16`; a live object's first
dword == its index; free stores the old free-head slot in the first dword.

| addr | what | conv |
|---|---|---|
| 0xB72220 | `Allocate()` → index, memsets object, writes index to +0 | thiscall(pool) |
| 0xB72290 | `GetIfNotDeleted(idx)` → ptr/0 | thiscall, `ret 4` |
| 0xB722D0 | `Get(idx)` (no check) | thiscall, `ret 4` |
| 0xB72320 | `Free(idx)` (raw - does NOT clean GFX/query, never call for cells) | thiscall, `ret 4` |
| 0xB722F0 / 0xB721E0 | iterator next (alive test: `(first & 0xFFFF) == slot`) | thiscall |

## 1. Enumerate and classify cells

Enumeration (✔, same test as the game's iterator 0xB721E0):
```
pool = G+0x1C; for slot in [0, *(u32*)(pool+0x10)): c = data + slot*0x398;
  alive = (*(u32*)c & 0xFFFF) == slot; idx = *(u32*)c
```
Radius query: `int __cdecl FindCellsInRadius(query, const Vec3* pos, float r, u32* outIdx, int max, void* 0)`
0xE87210 ✔ (used so by 0xE71F00).

Per-cell fields (cCellObjectData, 0x398) confirmed in this build:

| off | field | evidence |
|---|---|---|
| +0x000 | own pool index | pool |
| +0x004 | u8 mIsIdle (steering off) | 0xE6EEC0 |
| +0x008 | Vec3 mTargetPosition | 0xE6EEC0, 0xE6F5B0 |
| +0x014 | Quat mTargetOrientation | CreateCellObject |
| +0x048 | Transform: u16 flags (\|4 = offset dirty, \|2 = rot dirty), u16 change counter +0x4A, offset +0x4C, scale +0x58, rot 3x3 +0x5C | 0xE7C080, 0xE5A450, 0xE74A20 |
| +0x080 | float elevation (CreateCellObject arg3) | 0xE74A20 |
| +0x084 | Vec3 previous position | 0xE7C080 |
| +0x090 | Vec3 velocity (integrated into pos each frame) | 0xE7C080 |
| +0x0A0/+0x0A4 | opacity / target opacity (opacity 0 only hides) | 0xE7BFB0 |
| +0x0AC/+0x0B4 | size smoothing rate / mTargetSize; **size→0 auto-destroys the cell** | 0xE7BFB0 |
| +0x0FC | ResourceKey mModelKey {instance, type, group}; instance≠0 ⇒ creature model | 0xE74A20 (GetModelKey) |
| +0x108 | cCellDataReference* mCellResource (its +0 = .cell instanceID) | many |
| +0x110 | u8 "player-owned": kills by this attacker count as player kills | 0xE7A7C0, 0xE7A8AF |
| +0x111 | u8 mIsInvulnerable (debug key toggles it) | 0xE7A7F5, 0xE8173B |
| +0x112 | u8 dead/consumed (set by kill event 6 → 0xE59170; ShouldNotAttack/IsDamageable test it) | ✔ |
| +0x113 | u8 dying (set by KillCell/PlayerDeath, blocks second kill) | 0xE7A4A0 |
| +0x16C | u8 **AI lock**: when ≠0 the AI dispatcher skips all behaviour (see §2) | 0xE7A218 |
| +0x178..+0x189 | u8 animation/interaction locks (set together with +0x16C by death/mating/cinematics) | 0xE59190… |
| +0x18C | int animation/state id (7 = dying, 10 = dead-anim) | 0xE59190, 0xE7A209 |
| +0x1B0 | current animation | PlayAnimation callers |
| +0x1C4 | Vec3 acceleration (accumulated by steering, cleared each frame) | 0xE6EEC0, 0xE7C8C0 |
| +0x220/+0x21C | flee target idx / flee time | 0xE71F00 |
| +0x22C | chase target idx | ModAPI, used by modes.cpp |
| +0x244 | int hp 0..6 | ✔ |
| +0x248 | GFX object index (0 = not created yet; created lazily when visible) | 0xE78C00 |
| +0x354 | u8 keep-alive: not destroyed when outside the active tiles | 0xE7C080, 0xE771D0 |
| +0x358 | int mScale level (-1 = player/puppet; never culled) | 0xE5F8D0 |
| +0x35C | query: `G+0x40FC` = foreground (gameplay), `G+0x4100` = background decoration | 0xE74A20, 0xE7C8C0 |
| +0x360/+0x364 | query handle / collision-pair handle (G+0x4104) | 0xE780A0 |

Resource data (`cCellCellResource`, ModAPI layout matches): get it with
`void* __cdecl GetData(ref, void** holder)` = **0xE823A0** (✔; 0xE4CC30 is a jmp thunk to it). It
writes `*holder = ref` and increments `ref+0xC`; release with `--*(int*)(ref+0xC)` (game uses 0xE82130,
thiscall on &holder). NOTE: symbol `Simulator::Cell::GetData=0xE4CC00` in gog2016 sym is wrong - that
is a stdcall string→hash helper.
Used fields: `+0xB0 flags, +0xB4 cellType, +0xB8 unlockType, +0x2FC friendGroup, +0x300/+0x301
wontAttackPlayer(/WhenSmall), +0x30C eat.foodValue, +0x310 eat.hpValue`. AI block by difficulty
`d = *(int*)(S+0x7C)`: d==0 → `res+0x248` (easy) if its type ≠ -1, d==2 → `res+0x194` (hard) if ≠ -1,
else `res+0xE0`; `ai.type` is its first int (0 = none/player, 0x1001.. behaviours).

Classification recipe:
- skip if `*(void**)(c+0x35C) == *(void**)(G+0x4100)` (background layer);
- avatar: `idx == *(u32*)(G+0x411C)`;
- cellType (`res+0xB4`): 2 = "solid" (creatures and meat; only type 2 is damageable, 0xE52950),
  3 = plant food (counts as plant food in eating), 1 rock, 4 air/bubble, 5 poison, 6 unlockable part
  (`res+0xB8` = part id, auto-removed once unlocked), 7 nanite;
- NPC creature: cellType 2 and `ai.type != 0` (and normally modelKey.instance ≠ 0);
- meat chunk / food: no AI (`ai.type == 0`), cellType 2 or 3.

Network identity of a species: `instanceID = *(u32*)*(void**)(c+0x108)` (the .cell file) **plus**
`modelKey` (+0xFC, 3×u32 - RandomCreature cells pick a random model per spawn) plus level `+0x358`.
Host pool index (`*(u32*)c`) is a stable net id for the cell's lifetime.

## 2. Per-frame update and how to make a cell passive

Frame: `cCellModeStrategy::Update` 0xE80980 → `CellFrame(dt…)` 0xE806B0 (✔): MovePlayerToMousePosition
0xE5B7B0 → `SimUpdate` 0xE7F3A0 → (if `G+0x51D8`) RebuildLevel 0xE7FD00 → GFX 0xE73F60 → UI 0xE600B0.
SimUpdate 0xE7F3A0 (~): … `PopulateTiles` 0xE7D370(level, G+0x4128) → events 0xE7E7A0 →
**`UpdateCells` 0xE7C8C0** → 0xE5AF50 → 0xE7AD70 → `Collisions` 0xE7F260 → …

UpdateCells 0xE7C8C0 per foreground cell: GFX/visibility 0xE78C00(cell,res) → **AI 0xE7A190** →
(if still alive) anim 0xE6D340, soft-body 0xE5A450, size/opacity 0xE7BFB0 (destroys when size→0),
physics 0xE7C080 (vel += accel·dt, pos += vel·dt, re-insert into query, destroy if outside active
tiles and `+0x354==0`), clear accel.

**AI dispatcher 0xE7A190** ✔ custom convention: `ECX = cell, EAX = resource data, [esp+4] = float dt`,
plain `ret` (caller `add esp,4`). Body: run if `ai.type != 0 || cell is avatar`; timers + AoE
0xE76A80; if `+0x112` → state 10; **if `byte[cell+0x16C] != 0` → return** (no steering, no chase/flee,
no behaviour); avatar → 0xE6F5B0 (click-target + steering); else switch ai.type → behaviour
(0x1001 0xE71300, 0x1003 0xE70650, 0x1004 0xE704B0, 0x1005 0xE702D0, 0x1006 0xE7A0A0, 0x1007 0xE6F990,
0x1008 0xE78FC0, 0x1009 0xE707D0, 0x100B 0xE6FBB0, 0x100C 0xE6FD70, 0x100D 0xE6FCE0, 0x100E 0xE7A0D0).
Steering used by behaviours and the avatar: `SwimTowardsTarget` 0xE6EEC0 (`ESI = cell`, stack:
`float dt, float maxSpeed, float accel(1.0), int, int 0`, caller pops 0x14) - reads +4/+8.

Passive mirror (two options):
- **A (no code patch, ✔ by static reading):** `byte[c+0x16C] = 1`. Only reader in cell code is
  0xE7A218; no code that clears it was found (all writers set 1). Still runs 0xE76A80 (timers and
  electric/poison AoE 0xE71980 → 0xE71520), anim, physics, collisions.
- **B (strict):** hook 0xE7A190 with a naked stub (`push eax; push ecx; call is_mirror; …; pop eax`
  - EAX must be preserved) and `ret` for mirrors.
Collisions (0xE7F260 → 0xE7F140 → interaction 0xE7E7F0) still run for passive cells: a mirror can
still bite/zap the local avatar on contact and the local avatar can eat/bite it - handled in §3/§5.

Moving a mirror: set velocity `+0x90` (dead reckoning happens in 0xE7C080), add a correction
`(target-pos)*k` to it, snap `+0x4C` (and `+0x84`) when far, then `*(u16*)(c+0x48) |= 4;
++*(u16*)(c+0x4A)`. Rotation: write 3×3 at +0x5C, `|= 2`, counter++ (as 0xE5A450 does). Keep
`+0xB4`/`+0x58` = host size, `+0xA4` opacity, `+0x354 = 1` (we own the lifetime).

## 3. Damage, eating, death

| addr | signature (all cdecl unless noted) | notes |
|---|---|---|
| 0xE57360 | `int GetScaleDifferenceWithPlayer(cell*)` 0..4 | ✔ |
| 0xE57480 | `bool ShouldNotAttack(cell* a, cell* b)` | ✔ same .cell ref, +0x112, wontAttackPlayer, friendGroup |
| 0xE589A0 | `int GetDamageAmount(cell* attacker, cell* victim)` | ✔ depends only on scale diff/avatar/difficulty; NPC↔NPC table 5×5 |
| 0xE52950 | `bool IsDamageable()` **ESI = cell** | ✔ ai.type≠0 or avatar, hp≠0, +0x112≠1, cellType==2 |
| **0xE7A7C0** | `bool DamageCell(cell* victim, cell* attacker, const float* dir, int dmgType, int, float dt)` | ✔ checks IsDamageable, ShouldNotAttack, +0x17C, +0x111; `hp -= GetDamageAmount(attacker,victim)` (clamped ≥0); health ring 0xE62350 if avatar involved; AI reactions 0xE57930/0xE57710; hp==0 → KillCell; else hurt anim (types 1/3/5 → anim 0xE/0xF/0x11, else 0x10) |
| 0xE7A770 | `void KillIfDead(u32 victimIdx, u32 killerIdx, ref* killerRes, bool byPlayer, int dmgType, float)` | ✔ |
| **0xE7A4A0** | `bool KillCell(cell*, u32 killerIdx, ref* killerRes, bool byPlayer, int dmgType, float dt)` | ✔ 6 args. NPC: +0x113=1, death effect via InstanceEffectOnCell, alert same species 0xE71F00, if byPlayer telemetry + `S+0x6C` killCount++, loot (0xE52A30/0xE771D0) if level within ±1 of player, anims 10/7, event 6 (→ +0x112), locks 0xE59190. Avatar → PlayerDeath (unless `G+0x5169` or state `G+0x5158`∈{3,5}) |
| 0xE72060 | `bool PlayerDeath(cell*, u32 killerIdx, ref*, float)` | ✔ +0x113, UI `cCellUI+0x936=1`, telemetry 0xE5C900, anims 10/7, event 0x1D in pool G+0x54 (index stored at `G+0x5194`), locks +0x16C/+0x178/+0x17C, state 7 |
| 0xE7AEE0 | `void BreakOrSwallow(cell* victim, cell* attacker, float)` | ✔ effect "cell_death_break"; avatar victim: `S+0x70` deathCount++; event 5 → 0xE7ADD0 (loot; avatar → 0xE72200 respawn request; NPC → DestroyCell(idx,1,…)) |
| 0xE7B0A0 | Bite: see §10 (signature) and §11 | ✔ DamageCell type 4 while the damage is below the victim's hp; **the lethal bite goes to BreakOrSwallow** (not KillCell) |
| 0xE7E7F0 | collision interaction executor (switch on kind) | ~ |
| 0xE62350 | `void ShowHealthRollover(cell*, int oldHp)` | ✔ needs GFX (+0x248≠0) |
| 0xE66850 | `IVisualEffect* InstanceEffectOnCell(cell*, u32 effectId)` | ✔ ids: `0x932E50(name, 0x811C9DC5, 1)` (FNV) e.g. "cell_death_oneshot(_small/_large)" |

Eating (events posted on mouth contact, processed by 0xE7E130):
- event 0x12 → `EatFood(eaterIdx, foodIdx, foodRef, float)` 0xE7DF40 ✔: OnEat, then the food shrinks one
  level or is destroyed (`DestroyCell(food,1,0,0)`).
- events 0x13/0x14 → 0xE7DD90 / 0xE7DBD0: eat a chunk of a creature: OnEat + damage + KillIfDead(type 6).
- `OnEat` 0xE7D760: **ESI = eater, EDI = foodType, stack (ref* food, float)** - avatar → AvatarEat,
  NPC → growth 0xE79320.
- `AvatarEat` 0xE7D070 ✔: **EAX = ref* food**, stack `(int foodType, float)`: heals
  `hp += eat.hpValue` (×2 on easy; +2 if no resource), clamp 6, rollover; points
  `AddFoodPoints(eat.foodValue or 10, isPlant)`; isPlant = cellType==3 (or foodType 3 when flags&1);
  mission/mate-button bookkeeping (`S+0x69`, `S+0x80/0x84`).

Health: max 6. **There is no passive regeneration** - the only writers that raise +0x244 are
AvatarEat, CreateCellObject (=6) and the cheat `RefillAvatarHealth` 0xE5C580 (void, sets avatar hp 6).

Avatar death outcome (✔ flow): both PlayerDeath and 0xE72200 end in `RequestRespawn` 0xE50930
(`G+0x51D8 = 1`, `G+0x51D9 = flag`, UI reset) → next CellFrame calls **RebuildLevel 0xE7FD00**
`(foodProg, plantProg, overPlant, overAnimal, byte G+0x410C, G+0x4110, 0)`: destroys every cell,
clears the tile list, recreates the world and the avatar (0xE78B80/0xE78B20) at the **same food
progression** (no DNA lost), then starts the intro/hatch sequence 0xE6ECB0 / 0xE6EB60 (? egg).
In co-op this wipes all mirrors/puppets on that machine.

## 4. Progress (DNA/food bar)

`S` (cCellSerializableData, ModAPI layout ✔ for these): `+0x1C foodProgression 0..1000`,
`+0x20 plant food`, `+0x24 over-plant`, `+0x28 over-animal` (points above the 1000 cap),
`+0x34 unlockedParts[13]`, `+0x68/+0x69` cinematic/mate-button flags, `+0x6C killCount`,
`+0x70 deathCount`, `+0x74 gameTime ms`, `+0x7C difficulty (0 easy,1 normal,2 hard)`.

**`void __cdecl AddFoodPoints(int amount, bool isPlant)` 0xE7CE10** ✔ (debug keys call it with ±50 at
0xE81762/0xE8178D; negative works). Clamps to 1000 (0x14846AC), updates plant/over counters, and on
change runs level-up/down (zoom 0xE72B40 / 0xE57190, RemoveCellsOfOldScale 0xE7CD10, see §8) and the 1000 end trigger
0xE6E8C0. Level table: entries of 0x1C bytes at 0x14846B0 `{playerLevel, foodThreshold, …, float size}`;
thresholds every 50 points (0,50,100,…).

## 5. Removing / creating cells

- **`void __cdecl DestroyCell(u32 idx, bool spawnLoot, float, int)` 0xE780A0** ✔ - the game's own
  removal: clears avatar index if it was the avatar, removes from query (0xE86930) and collision pairs,
  frees GFX (0xE66020), detaches links (+0x370), optional loot/remains (spawnLoot=1 → 0xE67890 + 0xE771D0),
  then pool Free. Use `DestroyCell(idx, 0, 0.f, 0)` for mirrors. Never on the avatar.
  (Setting hp=0 does NOT remove a cell; `mTargetSize(+0xB4)=0` makes it shrink and auto-destroy.)
- **CreateCellObject 0xE74A20** ✔ cdecl, returns index:
  `(query*, const Vec3* pos, float elevation, ref* res, int level, float sizeFactor(→+0xB4),
  float cellSize (0 = auto from level/res), bool applySize, const Quat* orient (null = random),
  int collisionGroup (-1 = auto: own index, -5 for poison/nanite))`. Natural spawns pass
  `(…, 1.0, 0.0, 1, &identity|null, -1)`. Sets hp 6, GFX is created later by 0xE78C00.
- Resource ref by .cell instance id: **`ref* __cdecl cCellDataReference_::Create(u32 instanceId,
  CellSerializer* ser)` 0xE82420** ✔ (dedups: returns the existing ref). Cell serializer =
  **0x016AE490** ✔ (from the static ref table 0x15A8584; others: globals 0x016B1B3C, world 0x016AE3B0,
  loot table 0x016B2000). Model: force `modelKey` via the existing GetModelKeyForCellResource hook
  (0xE65650) while calling CreateCellObject.
- Population: `PopulateTiles` 0xE7D370 → `PopulateTile` 0xE7C6C0(tile*, …) → **`PopulateMarker`
  0xE7C500** ✔ cdecl `(float prob, int layer, int level, query*, bbox*, markerRef, int)` (recursive).
  Skipping it when `query == *(void**)(G+0x40FC)` suppresses all gameplay-layer spawns (creatures,
  food, parts) while keeping the background. Other spawn sources: loot on death/break
  (**SpawnLoot 0xE771D0**, §11), NPC growth/spawnOutput (behaviours - off for passive cells).
- Active area: tiles stream around the **local** player's view (`G+0x4128`, bbox 0x16B4C68..7C);
  cells outside it are destroyed unless `+0x354` (0xE7C080) or level −1. ⇒ on the host, NPCs near a
  guest far from the host do not exist.

## 6. Avatar move / respawn, regen

- **The avatar has a soft body** ✔ (disassembly + runtime 2026-10-22): when `+0x248` (GFX) ≠ 0
  and `+0x250` (particle count) > 1, 0xE7C080 adds `accel(+0x1C4)·dt` to each particle velocity
  and then `0xE553B0` (custom: `ECX = &outPos, EAX = &cell+0x90`, stack `(int n, u32* idx)`)
  averages the particles: `+0x4C` = mean **position** `parts + 4 + 12·idx`, `+0x90` = mean
  **velocity** `parts + 0x4654 + 12·idx` (`parts = *(G+0x4108)`, indices `+0x254[i]`, at most 64
  of them up to +0x353; 0x4650 = 1500 × 12 ⇒ 1500 particle slots). Writing `+0x4C` alone is undone
  the next frame - translate the particle positions by the same delta, and zero the particle
  velocities to stop it (`modes::cell_move(c, p, still)`). Until 2026-10-22 the mod added the
  delta to the velocity array (the old note named +0x4654 as positions): a "teleport" then
  glided there in ~2 s and overshot by 4–6 m, the knockout respawn landed 7–40 m from the
  partner. The particle count changes with the size (4 at size 0.55, 1 at 0.28: then +0x4C is
  written by plain physics). NPC cells/puppets seen with n = 1 or 4.
- Size ✔: `SetCellSize` 0xE57190 (EAX = cell index, stack float factor): `+0xB4` target =
  factor·√c·G+0x514C/G+0x5150, `+0xB8` = size²·k (mass), `+0xBC` = c/`+0xB8`; AddFoodPoints calls
  it for the avatar on a level change (factor from the level table 0x14846B0 `+0x0C`); the
  smoother moves `+0x58` towards `+0xB4`. Level table: `{worldLevel, threshold, parts, avatarSize,
  worldScale, ?, ?}`, avatarSize 1, 1.73, 3, 5.2, 10 … 52000.
- **Poses of remote cells** ✔: the cell step `SimUpdate` 0xE7F3A0 (cdecl `(float dt)`) moves and
  turns every cell (a cell turns towards where it swims) and the GFX update draws after it. A
  pose written at the end of the frame is drawn only after the next step changed it (facing off by
  up to 170°): hook SimUpdate and write position (through the particles) and the rotation matrix
  `+0x5C` (flag 2 in `+0x48`) after the original. Rotating the particles instead blows the body up.
- Teleport (✔ fields): write `+0x4C` pos (z = 0 on the gameplay layer), `+0x08` target = pos,
  `+0x84` = pos, `+0x90` vel = 0, `+0x04` idle = 1, `flags(+0x48) |= 4; ++counter(+0x4A)`; query
  position is refreshed by 0xE7C080 the same frame. Camera follows the avatar (?) - check for a jump.
- Respawn-next-to-partner: avoid the vanilla path (RebuildLevel wipes the world):
  hook **KillCell 0xE7A4A0** and **BreakOrSwallow 0xE7AEE0**; when the victim is the avatar and the
  partner is alive → do not call the original; set hp 6, clear `+0x112/+0x113`, teleport next to the
  partner, `+0x111 = 1` for ~3 s, optional effect. Safety net: hook PlayerDeath 0xE72060 and
  0xE72200/RequestRespawn 0xE50930 (if reached, also clear `+0x16C,+0x178..+0x189`, `+0x18C = 0`,
  `cCellUI+0x936 = 0`, `G+0x5194 = 0`).
- Regen near partner: own timer (no vanilla regen exists): if `dist(avatar, partner) < R` and hp < 6
  every T s: `ShowHealthRollover(avatar, hp)`; `hp++`.

## 7. Implementation recipe (host-authoritative tide pool)

Rule: each machine is authoritative for its own avatar; the host for NPCs, food and progress.

Host
1. Each tick enumerate foreground cells (§1) near any player; send per cell: net id (pool index),
   instanceID, modelKey, level, size(+0x58)/targetSize(+0xB4), pos, vel, rot, hp, flags
   (dying +0x113). Creatures at 10–15 Hz, food/parts as spawn/despawn + low-rate position.
2. Guest puppets (created as now): keep `+0x111 = 1` (NPC bites on them are not applied - the guest
   applies them locally), set `+0x110 = 1` so damage/kills they cause count as player kills.
3. On `Damage{netId, amount, type}` from a guest: victim = pool[netId]; set a "forced damage" global,
   call DamageCell(victim, guestPuppet, &dir, type, 0, dt) with a GetDamageAmount hook returning the
   forced amount (or replicate: `hp -= amount`, clamp, KillCell when 0). Damage from several players
   sums up naturally.
4. On `Consumed{netId}`: run EatFood-like logic for food (shrink a level or `DestroyCell(idx,0,0,0)`).
5. AddFoodPoints hook: every local or forwarded gain is applied with the original, then broadcast the
   authoritative `S+0x1C/+0x20` values.

Guest
1. Hook PopulateMarker 0xE7C500 → skip gameplay layer (no local NPCs/food).
2. Mirrors: `ref = Create(instanceID, 0x16AE490)`; force modelKey; `idx = CreateCellObject(G+0x40FC,
   &pos, 0, ref, level, 1.f, 0.f, 1, &q, -1)`; then `+0x16C = 1` (or hook 0xE7A190), `+0x354 = 1`,
   copy size/hp. Drive by velocity + correction (§2). Remove with `DestroyCell(idx,0,0,0)`
   (play "cell_death_oneshot" via InstanceEffectOnCell first when the host reports a kill).
3. Hook DamageCell 0xE7A7C0: if victim is a mirror → send `Damage{netId,
   GetDamageAmount(attacker, victim), type}` and return 0 (no local hp change/kill/loot); if victim is
   the local avatar → let it run (mirrors can bite the guest on contact).
4. Mirror eaten locally (EatFood 0xE7DF40 / 0xE7DBD0 / 0xE7DD90 with a mirror as food): let the local
   heal/points happen, send `Consumed{netId}`; detect it by hooking DestroyCell 0xE780A0 for mirrors
   not removed by the sync code (and BreakOrSwallow for swallowed mirrors).
5. AddFoodPoints hook: forward `{amount, isPlant}` to the host; on host total T vs local L call the
   original `AddFoodPoints(T-L, isPlant)` under a re-entrancy guard (keeps level-ups consistent).

Both: co-op respawn and regen near partner as in §6.

## 8. Runtime findings from two players playing (2026-10-23, `tools/tests/play_cell.py`)

Who calls `DestroyCell` 0xE780A0 (return addresses counted on a guest over 10 minutes of play; the
mod's `coop.pool why` lists them):

| return addr | caller | what | ✔/~ |
|---|---|---|---|
| 0xE7CDA1 | **RemoveCellsOfOldScale 0xE7CD10** (cdecl `void(void)`, plain `ret`) | on a level change (AddFoodPoints 0xE7CF5E, avatar body swap 0xE7D908) every pool cell with level ≠ −1 for which 0xE5CE00 is true (and the head of its `+0x36C` chain) is destroyed (~600 per level change). Not a repopulate | ✔ |
| 0xE78C37 | 0xE78C00 (per-cell visibility step, UpdateCells) | a **poison** cell (cellType 5) outside the active area is destroyed (no keep-alive check) | ✔ |
| 0xE78B6E | 0xE78B20 (custom: ECX/EDX + stack float radius, `FindCellsInRadius` 0xE87210) | clears every cell in a radius except one (the hatching / respawning avatar) | ~ |
| 0xE7C03C | size/opacity step 0xE7BFB0 | size reached 0 | ✔ |
| 0xE791DE | 0xE791A0 (custom: ESI = eater, EAX = victim) | eater swallows the victim (if the eater is the avatar 0xE514B0 first), DestroyCell(victim, loot 1) when both have GFX | ~ |
| 0xE7DA2E | 0xE7D880 cdecl(eater cell*, victim cell*) from event callback 0xE7DB80 | swallow whole: victim ≠ avatar → `+0x114 = 1`, effect, DestroyCell; victim = avatar → the avatar index moves to the eater and food +150 | ~ |
| 0xE7AEC3 | event 5 handler 0xE7ADD0 | loot of a broken/swallowed cell, NPC → DestroyCell | ✔ (§3) |
| 0xE7A14E, 0xE7B798, 0xE7C3E6 | behaviour 0x100E area, event handler 0xE7B410, physics | rare | ? |

* **0xE5CE00** (custom: EAX = cell, returns al): true when the cell is outside the active area
  (bbox 0x16B4C68 test for the foreground query), false for the avatar and `G+0x51D4`.
* **HandleCollisionPair 0xE7F140** ✔ cdecl `void(cell* a, cell* b, float dt)`: the only caller is
  Collisions 0xE7F260 (`add esp,0xC`); it asks the interaction kind both ways (0xE73DD0) and runs the
  interaction executor 0xE7E7F0. Every bite, eat and swallow between two cells starts here, so
  skipping a pair removes all their interactions (used by the guest for pairs without its avatar
  and by both for pairs with a partner's cell). ~500 000 pairs in 10 minutes.
* Event dispatcher 0xE7E130 (custom: EDI = event, stack float dt, …): an event's callback at
  `+4` runs first (`+8` = done), then a switch on the type `+0x24` (1..0x32) with the cell indices
  at `+0x28/+0x2C/+0x30`: 0x12 → EatFood 0xE7DF40(+0x28 eater, +0x2C food, +0x3C), chunk eats
  0xE7DBD0 / 0xE7DD90, kill 0xE7A770, …
* Soft bodies of NPC cells ✔ (runtime): a mirror whose `+0x4C` is written without its particles
  (`+0x250` > 1 and GFX) drifts away exponentially: 2e6 m, 4e18 m within a minute (the centre is
  pulled back to the particles and the particle velocities grow). Move such cells with
  `modes::cell_move` (particles too) and zero the particle velocities.
* Captions in the cell stage: the cinematic manager `+0x2C` (`coop.pool` prints `cine=`) is 2 during
  the level-up zoom (ends by itself, ~1-2 s, letterbox, no text) and during part/tutorial captions
  ("When you see the new part appear, swim over the token...", "Other cells and meteor bits are the
  key to finding new parts...") that wait for the green check (bottom right) or Enter.
* Cell kinds a player sees (`coop.pool see`): GetData(+0x108) `+0xB4` cellType and the AI block
  (§1): rock 1, creature (2 with ai.type ≠ 0), meat (2, no AI), plant 3, bubble 4, poison 5, part
  token 6, nanite 7.
* The Karke fixture's cell has no weapon (~): a bot that chases smaller creatures never bites (no
  DamageCell from its avatar with a mirror as victim in 40 minutes of play).

## 9. How an NPC picks its prey and its threat (2026-10-23, the host-hunts-host bug)

Chase target `+0x22C` / flee target `+0x220` (cell indices, 0 = none), chase timer `+0x230`, flee
timer `+0x21C`, re-pick timer `+0x238`.

| addr | what | ✔/~ |
|---|---|---|
| 0xE70E30 | per-frame target timers of a behaviour (behaviour 0x1006 0xE7A0A0 calls it): clears `+0x220`/`+0x22C` when the cell is gone, counts `+0x238` down and then calls PickTargets | ✔ |
| **0xE70CF0** PickTargets | cdecl `bool(cell*, resData*)`: a cell in a group (`+0x38C`, pool G+0xA8) copies its leader's targets; else GetTargetSearchParams + FindPreyAndThreat into `+0x220`/`+0x22C`; a new target starts a flee (0xE70950) or chase (0xE70BC0) with the timer from the AI block (`+0x7C` / `+0x84`) | ✔ |
| **0xE68B30** GetTargetSearchParams | cdecl `void(cell*, resData*, u8 params[16])`: `+0` float prey radius, `+4` float threat radius (0xE686C0: from the AI block scaled by the level table, constant for the avatar), bytes `+8..+0xD` = AI block `+0x92, +0x8D, +0x8E, +0x8F, +0x90, +0x91` | ✔ |
| **0xE68990** FindPreyAndThreat | cdecl `void(cell* self, params*, u32* outFlee, u32* outChase, u32* outOther)`: FindCellsInRadius(self+0x35C query, max radius, 512); for each other cell: IsThreat → nearest within `+4` → *outFlee, else IsPrey → nearest within `+0` → *outChase | ✔ |
| **0xE68870** IsThreat | cdecl `bool(self, other, params)`: other not dead, !ShouldNotAttack, GetScaleDifference(self, other) ≥ 2, and other is a carnivore / herbivore / spiked / ... as the params flags ask | ✔ |
| **0xE68910** IsPrey | cdecl `bool(self, other, params)`: !ShouldNotAttack(self, other), CanEat 0xE68470(self, other), scale difference: 0 never, 1 only when `params+8` is 0, then herbivore/carnivore flags, and `other+0x189 == 0` | ✔ |
| 0xE68470 CanEat | cdecl `bool(self, other)`: by the self's creature model (0xE679E0(modelKey): `+0x1250` carnivore, `+0x1251` herbivore), other's cellType (2 meat/creature, 3 plant) and the scale difference | ~ |
| **0xE57400** GetScaleDifference | cdecl `int(self, other)` = clamp(other.level − self.level + 2, 0, 4), level `+0x358`, −1 (the avatar) read as the level of the food progress (0xE4EE50, EDX = food: the level table 0x14846B0) | ✔ |
| 0xE57480 ShouldNotAttack | cdecl `bool(a, b)`: same resource, dead, `wontAttackPlayer(WhenSmall)` (res `+0x300/+0x301`) **only when b is the avatar** (`G+0x411C`), friendGroup `+0x2FC` | ✔ |
| 0xE57610 | cdecl `void(cell* self, u32 attackerIdx)`: reaction to an attack: flee (`+0x220`) or chase (`+0x22C`) the attacker by the AI block flags `+0x91`/`+0x8C`; reached through 0xE57710 (custom: ESI = victim, stack attackerIdx) from DamageCell and the chunk eats | ✔ |
| 0xE71CE0 | the species alert of DamageCell (called at 0xE7A80B): cells of the victim's species around (FindCellsInRadius, ShouldNotAttack true = same kind) chase (`+0x22C`, 0xE71EC0) or flee (`+0x220`) the attacker by distance; KillCell has its own alert 0xE71F00 | ✔ |
| 0xE58F10 GetCurrentAdvectInfo | cdecl `(int* level, float*, float*, float*)`: its level is **not** the players' level everywhere (it said 8 where GetScaleDifference reads the avatar as 7, food 600) | ✔ runtime |

The prey radius is small: 2-8 m for most species, 20 m for the biggest (seen with `coop.pool prey`),
so an NPC hunts whoever is nearest within a few metres.

**The bug (fixed 2026-10-23):** a partner's cell on the host (the puppet) is an ordinary pool cell
with the level `+0x358` it got when it was created (GetCurrentAdvectInfo at spawn) and never
updated. After a few level-ups the host's NPCs read it as 2+ levels smaller than they are
(GetScaleDifference 0): IsPrey false for the normal hunters (and prey only for the big eaters that
take tiny cells), IsThreat false. So they hunted the host's avatar (−1, always the current level).
Fix: `modes::cell_drive` keeps the puppet's level at the players' level, asking the game
(GetScaleDifference(avatar, puppet) must be 2), and the ShouldNotAttack hook answers for a puppet
victim as for the avatar (swaps `G+0x411C` around the original). After it `coop.pool prey` shows
the same prey / threat answers for the avatar and the partner's cell for every NPC.

Damage stays with the bitten player's own machine: on the host no collision pair with a partner's
cell runs (HandleCollisionPair hook, §8), so a host NPC that catches the puppet does nothing there;
its mirror on the guest touches the guest's real cell and the guest's own collision step bites it
(`hurt_local`; part-to-part geometry in 0xE6AD80, no AI state needed, the mirror's AI lock does not
matter). The bite then goes through the guest's DamageCell / KillCell hooks (knockout, revive).
The host drops any DamageCell on a partner's cell (`puppet_hits_dropped`; none seen: AoE damage
0xE71520 asks IsDamageable first, which a puppet fails), so nothing is applied twice.

What DamageCell does around a bite besides the hp, and how the co-op path replays it:

* **Attacker rests** (0xE57930, ESI = attacker → RestAfterBite 0xE578B0 for AI type 0x1006): its
  chase ends and it picks no target for AI block `+0x88` seconds. On the host an NPC that caught
  the guest's puppet never bit (no pair), so it went on chasing the guest without a pause: in the
  first run with the level fix the host's NPCs chased the guest 892 samples vs the host 80, the
  guest was knocked out 3 times. The guest now sends `MSG_CELL_BITTEN {host index}` for every bite
  a mirror gives it and the host calls RestAfterBite on that NPC.
* **Species alert** (AlertSpeciesOfAttack 0xE71CE0) and the **victim's reaction**
  (0xE57710 → ReactToAttacker 0xE57610, AI type 0x1006): run for the host's bites inside
  DamageCell; a guest's bite arrives as `MSG_CELL_HIT` and the host applies the hp itself, so it
  now calls both with the guest's puppet as the attacker (`hits_aggro`): the bitten NPC and its
  kind turn on the guest as they would on the host.
* The jump tables of 0xE57710 and 0xE57930 (bytes at 0xE577AC / 0xE579C8, index ai.type − 0x1003)
  are `00 00 00 01 00 00 00 02 00 00 00 00`: only ai.type 0x1006 reaches the call.

## 10. Soft-body facing of mirrors; bites that do not go through DamageCell (2026-10-24, GOG 2024)

Player report (0.7.x): "enemies spun uncontrollably for the guest, switching between facing the host
and the guest, so they were almost impossible to kill even with both attacking".
`tools/tests/coop_cell_spin.py` (both players swim at a creature, its rotation `+0x5C` sampled on
both machines with `coop.sync`; then each player bites one to death through the game's Bite).
Addresses of the GOG 2024 build (`dc04aee5`); the cell code around 0xE7xxxx is at the same address
as in GOG 2016; 0xE5A430 is likely 0xE5A450 there (its neighbours moved by -0x20, not checked).

**Facing comes from the particles** ✔ (disassembly + runtime). `SoftBodyStep` **0xE5A430**, cdecl
`void(cell*, resData*, float dt)`, runs in UpdateCells after the AI dispatcher and the animation
step (call at 0xE7CA72), for every foreground cell with GFX and more than one particle:

* `FUN_00E85D70` fits the cell's rest shape (0x16AD6C0 + `+0x24C`·0x300, at most 64 Vec3) to the
  current particle positions (`parts + 4 + 12k`) and returns a rotation (polar decomposition
  0xE85170 of the covariance) and the goal positions `goal = rest·scale · M + centre` (row
  vectors: the image of the x axis is row 0 of M, so the heading is `atan2(M[1], M[0])`);
* writes M to `+0x5C` (flag 2, counter++) and to `+0x24` (the shape matrix, a blend of M and the
  linear fit with weight 0 = M), the angular speed (0xE5A2A0) to `+0x1D0`, and the goal offsets
  `goal - pos` per particle to **`parts + 0x11944 + 12k`** (a 5th particle array after positions
  +4, velocities +0x4654 and two more);
* with one particle it only copies a constant 3x3 to `+0x24`; `+0x5C` is then whatever was written.

Part geometry for collisions (0xE6AD80, both cells) places every part at `+0x4C + scale·(part · M)`
with M = `+0x5C` (only when flag 2 is set), so the facing decides which parts touch: a mouth, a
spike, the body.

A guest moves a mirror's particles (translation, velocities zeroed: `modes::cell_move`) and wrote
the host's rotation to `+0x5C` once a frame, outside the cell step. The next SoftBodyStep replaced it
with the fit of a particle cloud that nobody turned: it kept the orientation it had at its spawn plus
whatever the guest's collisions and the shape matching did to it. Measured before the fix (two
players chasing a creature for 25 s, 10 samples/s): facing difference host vs guest median 63-79°,
p90 144-154°, the guest's copy turned 4900-5400° while the host's turned 560-1200°; `coop.sync`
sometimes caught the host's value (read right after the write) and the next sample 150° off.
Fix (cellsync.cpp `h_Soft`): after the original, for a mirror, turn the particle positions about
their mean, their velocities and goal offsets by `heading(host) - heading(fit)` and write the host's
matrix to `+0x5C` and `+0x24`: the next fit returns the host's facing, so drawing and the collision
step both use it. After: p90 2-10°, turned within 2% of the host. Rotating the particles this way
(a rigid turn about the centre, every frame) did not blow the body up (cf. §6 for the puppets).

**Bites that bypass DamageCell** ✔ (disassembly; the Bite path also at runtime):

| addr | what |
|---|---|
| 0xE7E7F0 | interaction executor, cdecl `void(interaction*, float dt)` (`add esp,8` at 0xE7F208). Interaction: `+0` cell A idx, `+4` cell B idx, `+8` kind, `+0xC/+0x10` part indices, `+0x14` depth, `+0x18` contact point, `+0x24` normal. HandleCollisionPair asks 0xE73DD0 both ways; 0xE59CC0 keeps the highest kind (ties: deeper). Kinds seen for a player's cell vs a creature in 20 s chases: 1 (push, ~hundreds), 3, 8 (weapon contact, ~1/s at most) |
| 0xE6A3F0 | the kind of one part pair (attacker part type, victim cellType `+0xB4`, victim part type, GetScaleDifference(attacker, victim), victim res flags `+0xB0`). Part type 7 (a weapon) on a creature of the same level: kind 8 unless the victim's part is a weapon too (0xE) |
| 0xE7B540 | timed callback of an interaction event: kind 10 → **Bite 0xE7B0A0** |
| **0xE7B0A0** Bite | cdecl `bool(u32 attacker, u32 victim, void* ev+0x48, u32* dir ev+0x54, int ev+0x38, float dt)`. `+0x178 = 1` on both; GetScaleDifferenceWithPlayer(victim) < 3: **DamageCell(type 4) only when the damage is below the victim's hp, else BreakOrSwallow(victim, attacker)**; bigger victims: DamageCell and a push 0xE76AF0 |
| **0xE7DD90 / 0xE7DBD0** EatChunkA/B | events 0x13 / 0x14 (dispatcher 0xE7E130), cdecl `void(u32 eater, u32 victim, u32 ev+0x30, u32 ev+0x3C, float dt)`: OnEat for the eater, victim hp -= GetDamageAmount directly (no DamageCell), attacker rest 0xE576F0, KillIfDead(type 6), pushes |

So in co-op the guest's bites on a mirror went to the host only when they were non-lethal DamageCell
calls: the lethal bite (BreakOrSwallow) broke the mirror on the guest only (and `h_Destroy` kept it,
untouched), and chunk bites lowered the mirror's hp until the next state. With 6 hp and 3 per bite
(a creature of the players' level) the guest could take a creature to 3 hp and never kill it
(`coop_cell_spin.py --without-fix`: hp on the host `[6, 3, 3, 3, 3, 3]`). Fix: the guest's
`h_Break` and `h_ChunkA/B` send `MSG_CELL_HIT` (type 4 with at least the mirror's hp / type 6) and
keep the mirror (a chunk bite still shows: food, health ring, push; the mirror keeps 1 hp at least).
The host's handler has no distance or facing test: it applies the hp, the species alert and the
victim's reaction, and KillCell when hp reaches 0 (`hits_in`, `hits_aggro`).
A mirror that bites a chunk off the guest now also sends `MSG_CELL_BITTEN` (the NPC's rest).

Changed since by fix/cell-meat (§11): the lethal bite goes to the host as a break (`MSG_CELL_EAT`
kind 2, the host runs BreakOrSwallow with its loot) rather than a hit (KillCell drops no loot at the
players' level), and a creature the host killed dies on the guest too (KillCell replayed on the
mirror). Not changed: how often a player's weapon gets to
a creature in the bots' chase (`cell_contacts:` `creature_pairs`, `bite_events`): host 1-3 bites per
20 s, guest 0-3, with or without the fix; the Karke fixture cell has no weapon (§8), premade cell
`BAF1AD40` (`coop.creations choose cell BAF1AD40`) has one.

## 11. Kills and their meat; eating once (2026-10-24, GOG 2024, branch fix/cell-meat)

Player report (0.7.x): "when one of us killed an enemy, the other did not see the meat that should
drop". `tools/tests/coop_cell_meat.py`.

**Where the meat of a kill comes from** ✔ (Ghidra + runtime, `coop.pool loot`):

| addr | what |
|---|---|
| **0xE771D0 SpawnLoot** | cdecl `void(ref* res, ResourceKey* model, query*, Vec3* pos, float size, int level, ref* lootTable, float dt, u32* outIdx, int* outCount)`, plain ret. Picks one entry of the loot table (`res+0xD4` passed by the callers) by weight: type 1 = RandomInt(`+0x14`) + `+0x10` cells of the entry's `.cell` (`+4`) via 0xE76480 with an outward velocity (`+0x18` added to the level), type 2 = repeat, 0 = nothing. Appends every new cell to `outIdx` when given (no bound check), so a hook can list them |
| KillCell 0xE7A4A0 | loot only when the victim's level `+0x358` ≠ 0 and within (player level − 1, player level + 2) (0xE4EE40, the level of the food progress); loot level = victim level − 1 (0xE52A20) |
| BreakOrSwallow 0xE7AEE0 | effect `cell_death_break`, anim 8, event 5 → **0xE7ADD0**: SpawnLoot (level − 1), then (NPC) DestroyCell(idx, 1) → **DestroyCell's own SpawnLoot** when the level is not the player's − 1. A lethal Bite of a creature of the players' level left 6 loot cells (3 meat chunks still there 3 s later) in every one of 24 kills in the test |
| 0xE7D880 / 0xE791A0 | swallowing whole: DestroyCell(victim, 1) (loot as above) |
| EatFood 0xE7DF40 | event 0x12: OnEat (avatar: AvatarEat, AddFoodPoints), then the food shrinks one level or DestroyCell(food, 1, 0, 0). A meat chunk gave 12 points |

KillCell at the players' level dropped nothing in the test (4 of 4 kills through DamageCell); the
dead creature stays (hp 0, `+0x113`, later `+0x112`) until something removes it.

**What went wrong in co-op (0.7.x)**: the host's kills were fine (loot cells are ordinary pool
cells, the state carries them; measured: every loot cell of a host kill was on the guest within
0.1-0.3 s). The guest's lethal bite on a mirror ran BreakOrSwallow **on the guest**: effect, loot
(local cells the host does not have, purged as strays by the next state 0.1 s later: `purged`),
DestroyCell → `MSG_CELL_EAT` → the host removed its creature with `DestroyCell(idx, 0)`: no death,
no meat on the host, and the guest's meat vanished at once (test before the fix: 4 of 4 guest kills
not killed by the host's game, 24 loot cells spawned on the guest, none on the host for them).
Eating: the guest sent its food gain (`MSG_CELL_FOOD`) and the eaten cell separately, so a chunk
both players ate at the same moment counted twice. And a creature the host killed never died on the
guest (the state's dying flag was not applied).

**Fix** (cellsync.cpp): the guest's game spawns no gameplay loot (`h_SpawnLoot`: foreground query →
skipped, `loot_skipped`); a mirror the guest's avatar breaks or kills is not broken / killed there:
`MSG_CELL_EAT {u32 host index, u8 kind 0 gone / 1 kill / 2 break, u8 arg (loot flag / damage type),
i32 food, u8 plant}` and the host runs BreakOrSwallow / KillCell / DestroyCell(idx, loot) on its cell
(attacker: the guest's puppet), so death and loot happen once, on the host, and come back with the
state; a dying/dead flag in the state replays KillCell on the mirror (`death_replays`, with
SpawnLoot off); the guest's AddFoodPoints is held until the end of the frame and goes with the eaten
cell: the host adds it only if the cell was still there, else (`eats_late`) it resends its total and
the guest's AddFoodPoints(diff) takes the points back. Older guests (index only) still work.

Debug (`coop.pool`): `kill [c<key>|-] [hit]` one bite of the own cell through Bite (or DamageCell
only with `hit`), `meat` every meat cell, `loot` (host) the cells of the last loot drops, `eat <key>`
the game's eat handler on a cell, `swimto <key> [tp]`.

## 12. The mating call and the cell editor (2026-10-24, GOG 2024 addresses, Ghidra `out/ghidra-2024`)

How a player gets into the cell editor (the real path, driven by `tools/tests/coop_editor_desync.py`;
addresses of the **GOG 2024 build dc04aee5**, not the GOG 2016 ones above):

1. **The button**: collecting a part token runs **0xE5D7B0** (thiscall, from the collision code
   0xE7B630; also a debug key in 0xE81120): `S+0x34+4*part = 2` (unlocked), mission bookkeeping
   (`S+0x90..+0x9C`), the first time the callout `Callout_CLG_Mate`, and **`S+0x68 = S+0x69 = 1`**.
   `S+0x69` = the mating-call button is shown (a heart button at the bottom centre of the HUD,
   1440x1080: 721, 1018). `coop.pool mate button` sets it for tests (✔ runtime: the button appears).
2. **The call**: the button spawns the mate, **0xE760F0** ✔: if `G+0x51D4` (the mate's pool index)
   holds no live cell, `CreateCellObject` at `mateSpawnDistance` (cell globals `+0x110`, from
   GetGlobalsData; the mate came 12-16 m away at runtime) with resource 0xE4CC90(0x1A, ...), index into
   `G+0x51D4`; the avatar plays animation 0x43. Effects `cell_call_avatar` / `cell_call_mate`
   (0xE63BF0). Runtime: the mate swims towards the avatar; the HUD shows a ring with a heart towards it.
3. **The meeting**: SimUpdate 0xE7F3A0 calls **0xE78D10** every step: avatar and mate alive and closer
   than `mateTriggerDistance` (globals `+0x10C`, 4.0) → **0xE75EA0**: both not dying (`+0x112`,
   `+0x113`), not locked (`+0x188`), **the same level** (0xE51670 on both; a co-op respawn as a small
   cell, or food added by hand, can leave them at different levels and nothing happens) → mating
   animations 0x41 / 0x42 and a timed event (pool object 0x2D) → ... → event **0x1E** in the event
   dispatcher 0xE7E130 → **0xE72AC0** → **0xE59800(0)**.
4. **The request**: 0xE59800(int debug) computes the editor id from the level table
   (`DAT_01483b54 + 7*i`, by `S+0x1C`), the DNA to spend (0xE51410() - `S+0x2C`) and calls
   **0xE84600**: `new EditorRequest` (0x9C bytes), `+0xC` editor id, `+0x10..+0x18` = `S+0x10` (the
   player's cell model), `+0x90 = 0x1100000`, `+0x60` a float from 0xE51700, then
   `Editors::EditorRequest::Submit` 0x5A92C0 and message 0x90A03FDF. With `G+0x410C` set and
   debug = 0 nothing is submitted.
5. **Asked twice**: while the mate is still within reach the game asks again: with the co-op vote
   holding the first request, a second Submit came 13 ms after the vote ended (before the held
   request reached the game) and started a second vote that ran on in the editor. The mod drops
   requests while one is held or within 5 s of one reaching the game (`coop.vote` prints "repeated
   requests dropped").
6. **Back from the editor**: handler of message 0x30C11C7 (GOG 2016 0xE809A0, §4.2 of creations.md):
   `S+0x10` = the new cell, RebuildLevel (the cell hatches again; a guest's mirrors are rebuilt by the
   sync). The new cell is a new creation in My Creations (instance ids per profile: the host's and the
   guest's first own cells were both 0x290742F6 in the test profiles).

On a guest the mate is a local cell the host does not have: the sync's stray-cell purge removed it
within a second, so a guest never reached its mate (report 2026-10-24: "the guest could not call a
mate himself"). The purge now spares `G+0x51D4`. A guest's own cell editor opens without a vote
(9.3: evolving inside a stage does not move the others) and its new cell goes to the others as its
own creation.

## 13. After the Cell stage: cCellGame stays, its pool does not (2026-10-25, GOG 2024)

`*cCellGame::_ptr` (0x016B3C04 on dc04aee5) is **not cleared** when the Cell stage is left (the
Creature stage after the evolution, ✔ runtime and in a player's minidump). The object stays
readable, `+0x1C+0x14` still says 0x398 and `+0x1C+0x10` the old high-water mark, but the pool data
`+0x1C` is gone: the player's dump had `0x0000BAAD` there, lane 5 faulted reading `0x0000F42D`. So
`cell_game() != nullptr` does **not** mean "in the Cell stage": every reader must also check
`game::mode_id() == kCell` (cellsync's `in_cell()`). What the mod did with that (player report
2026-10-25, crash 2, `coop_cell_leave.py`):

* cellsync kept the guest's mirrors (pool indices) after the stage and destroyed them on session
  end: `modes::cell_at` <- `cell_destroy` <- `cellsync::destroy_local` <- `on_session_end` (the
  disconnected callback, also the host's BYE) faulted. Now the mirrors, `g_host_of`, the touched and
  loot lists and the puppets' indices (`modes::cell_forget`) are forgotten in the first tick outside
  the stage (the cell editor too: RebuildLevel rebuilds the pool after it anyway, the sync recreates
  the mirrors), and on_session_end destroys mirrors only inside the stage.
* `coop.pool`, `coop.cell` and `cellsync::S()` (read by `coop.status`) read the pool or
  `G+0x5190` without a mode check; they now answer only in the Cell stage.
* A guest's own `coop.leave` does not run cellsync's on_session_end (only the disconnected
  callback does), so its mirrors stay passive cells in its pool until it leaves the stage or rejoins
  (not changed: a rejoin reuses them).
  ~ superseded (2026-10-27): see the correction below.

CORRECTION (2026-10-27, fix/cell-guest-leave): Voluntary leave now calls Cell
cleanup while the pool is valid. A manual test used two Steam Galactic Adventures processes on
Windows with SporeCoop 0.7.2.4: four retained baseline creatures had `+0x16C=0` and nonzero
velocity several seconds after leave. Their indices included generations. Retained mirrors do not
always stay passive; this observation does not establish who cleared the lock. See [BUG-0028](../tickets/BUG-0028-cell-guest-leave-keeps-passive-mirrors.md)
for verification and its limits.

## Open questions
- Who clears `+0x16C`/`+0x178…` locks (none found statically) - confirm at runtime that a live NPC
  with `+0x16C=1` stays passive and nothing resets it. The retained-cell observation in the
  correction above shows unlocked, moving cells after leave, but does not identify the writer.
- `DamageCell` arg3 (float*, negated "dir") and arg5 meaning; damage type numbers beyond 3 (periodic
  poison/electric), 4 (bite), 5 (0xE7B280), 6 (eat chunk).
- Effects of `+0x110` beyond kill credit; meaning of `G+0x5169`, state `G+0x5158`.
- Whether RandomCreature models must be preloaded on the guest (cCellGFX::PreloadCreature 0xE64990)
  before CreateCellObject with a forced model key.
- Camera behaviour after an avatar teleport; whether 0xE6ECB0/0xE6EB60 is the egg/hatch intro.
- Existing `cell_despawn` in mods/coop/modes.cpp sets hp=0 and opacity 0: that only hides the puppet
  (cells are removed by DestroyCell or size→0), so puppets may leak - use DestroyCell 0xE780A0.

## Sync robustness: state rounds, meal numbers, knockout lifetime (2026-10-28, fix/w2-cell)
<a id="cell-sync-robustness"></a>

* ✔ `MSG_CELL_STATE` rounds are unreliable chunks of 14 cells `{u16 cycle, u8 chunk, u8 chunks}`.
  The guest removes the mirrors a round did not list only when every chunk of that round arrived
  (`cell_logic::Rounds`); a chunk of an older round (within 64 rounds) is dropped. A mirror not
  listed for 1 s goes anyway, so a cell the host lost is still removed on a lossy link
  (BUG-0032). `coop.status` guest: `rounds_partial=`, `late_chunks=`.
* ✔ A recreated mirror erases its old local index from `g_host_of` (`host_of=` equals `mirrors=`,
  BUG-0088).
* ✔ Food totals carry meal numbers: the guest appends `u32 meal` to `MSG_CELL_FOOD` and
  `MSG_CELL_EAT`; the host sends each guest `MSG_CELL_FOOD {i32 food, i32 plant, u32 last meal
  applied}` and the guest applies `host total + meals still on the way - own total`
  (`cell_logic::Meals`, BUG-0051). Older peers ignore or lack the field: the old difference.
  A refused meal (eaten first, `eats_late`) is acknowledged without points, so it is still taken back.
* ✔ The host sends the food total to each guest again every 2 s and at once to a newcomer; the
  DNA total (coop.cpp) also every 2 s (BUG-0128). A guest drops both without an avatar.
* ✔ The avatar's `+0x358` is -1 (not its level); the level line is every 50 food points. A
  meal over a line resizes the avatar inside AddFoodPoints (target `+0xB4` changes during the
  call): after a small respawn that size is the full one, `g_body` becomes 1 (BUG-0041, measured:
  target 0.55 -> 0.82 on host and guest alike over the line from 10 to 51 points).
* ✔ Knockout lifetime: `MSG_CELL_KO` is stored and relayed outside the Cell stage; a remote
  `KO_OUT` not refreshed for 2 s (its owner sends it every 0.25 s) ends; `cb.player_leave` calls
  `cellsync::on_player_leave` (knockout, puppet and food bookkeeping of that id, BUG-0061). The
  local knockout (`+0x111` invulnerability, `+0xA4` opacity, hp) ends with the session and with
  the stage (`reset_knockout`, BUG-0070).
* ✔ `modes::cell_at` accepts slots in `[0, highWater)` within `[0, capacity)` only (BUG-0083).
* Old behaviour of each: `coop.pool fix <purge|hostof|foodseq|resend|kostale|koreset|bodylevel|cellat|all> 0|1`.
  Test helpers: `coop.pool zap <n>` (the game removes n mirror cells), `mirrormap`, `small`,
  `foodlocal <n>` (our total only), `invuln 0|1`; `coop.pool` lists `puppet <id> op=`.

## DamageCell's gate and a guest's bite (2026-10-28, fix/w3-cell)
<a id="cell-damage-gate"></a>

* ✔ (GOG 2024 decompile of 0xE7A7C0) `DamageCell(victim, attacker, ...)` does nothing and returns
  false when IsDamageable 0xE52940 (ESI = victim: AI type or the avatar, hp != 0, +0x112 != 1, the
  resource's cell type +0xB4 == 2) fails, when `ShouldNotAttack(victim, attacker)` 0xE57460 is true,
  or when the victim's +0x17C or +0x111 is set: no hp, no alert, no reaction. ShouldNotAttack(a, b):
  same resource (+0x108) -> true; either dead (+0x112) -> false; the flags `+0x301`
  (wontAttackPlayerWhenSmall, scale difference 1) and `+0x300` (wontAttackPlayer) of a's resource
  data count only when b is the avatar (`G+0x411C`); same friend group (+0x2FC) -> true. So a
  species with "won't attack the player" cannot be hurt by the player's bites either.
* The host applies a guest's bite (MSG_CELL_HIT) with the same gate since BUG-0078: the victim's
  hp / dead flag, +0x17C, +0x111 and `ShouldNotAttack(victim, the guest's puppet)` through the
  hook that lends the avatar index to the puppet; a mouth's chunk (type 6, EatChunk) skips the gate
  as the game's does. Before, the arguments were swapped and nothing was checked. `coop.pool fix
  guestbite 0|1`; test helper `coop.pool npcflag c<key> spare|invuln 0|1 | hp <n>` (host).
* Knockout helpers (BUG-0131): `nearest_partner` skips partners whose knockout state is KO_OUT
  (`coop.pool fix rescue 0|1`); `coop.pool selfhit <dmg>` bites our own cell (tests).

## Knockout grace, meals while the host is away, IsDamageable's resource part (2026-10-28, fix/w5-cell)
<a id="cell-ko-grace"></a>

* ✔ (BUG-0238) A remote knockout (`g_ko`, MSG_CELL_KO every 0.25 s from its owner) is stale only
  after `max(2 s, left + 2 s)` since its last refresh (`cell_logic::ko_stale`, `left` = the
  owner's seconds still to run, a byte of tenths, at most 25.5 s). The 2 s of BUG-0061 ended it at
  one late refresh. A refresh after an expiry (`Ko::expired`) restores the translucency without
  `cell_death_oneshot`. `reset_knockout` sends KO_UP when it ends a running knockout of ours while
  the session is open (a stage leave). `coop.pool kodrop <s>` (tests, host) ignores MSG_CELL_KO for
  s seconds; `ko[N]=` lines of `coop.pool` end with `fx=<death effects played on that puppet>`.
  Switch `pool.ko_grace`.
* ✔ (BUG-0239) The host records the meal number of a guest's MSG_CELL_EAT / MSG_CELL_FOOD also
  outside the Cell stage or without an avatar (no points, no game call): the ack means "counted or
  never will be", as for a meal eaten first. `read_eat` / `read_food` are the only readers of the two
  messages. `coop.pool away 1|0` (tests, host) handles the Cell messages as in an editor. Switch
  `pool.food_ack_away`.
* ✔ (BUG-0240) IsDamageable, GOG 2024 **0xE52940** (GOG 2016 0xE52950, the address in the table of
  section 6), disassembled: `push ecx`; GetData(victim+0x108) into a holder; the AI block by the
  difficulty `*(*(0x16B3C04)+0x5190)+0x7C` (0: +0x248, 2: +0x194, each only when its first int is
  not -1; else +0xE0); returns true only when `(ai.type != 0 || victim.index == G+0x411C) && hp
  (+0x244) != 0 && +0x112 != 1 && res+0xB4 == 2`. Seen in the game (lane 2): the host's own bite
  (DamageCell) on a rock (cell type 1) and on meat (type 2, AI type 0), each given hp 60, takes
  nothing. Rocks and plants have hp 6 in play, so before the fix a guest could bite them. The
  host's guest-bite gate now copies this part (`res_types`, fail open when the resource cannot be
  read). Switch `pool.guestbite_type`; `coop.pool kill <key|-> anyhit [kind]` bites any kind.
* ? The hurt lock after a host bite (the tests wait 2 s between bites; Bite sets +0x178 on both
  cells, DamageCell checks +0x17C): which byte, set by whom and for how long is not measured. A
  guest's bite applied by the host sets none, so a guest may bite one NPC faster than the host
  (docs/known-issues.md, BUG-0240).

## Rebuilds in a session (2026-10-28, fix/w5-cell)
<a id="cell-rebuilds-in-session"></a>

Measured on GOG 2024 (lane 2) with `coop.cell pool` (tests: the pool header G+0x1C..+0x18 as
`data free gen live high size cap`, the avatar index G+0x411C, `rebuild=` G+0x51D8, the avatar's
pointer, +0x111 and +0xFC). Tests: `coop_cell_respawn.py [--swap]`, `coop_cell_editor_invuln.py`.

* ✔ The game's own death in a session (BUG-0221: the only partner knocked out, then a lethal hit,
  BUG-0131's rule) rebuilds the pool of the dying player's machine: the avatar gets a new index
  (host: 7F010000 -> 80C501C2; guest with --swap: 7F010000 -> 80820180), the generation keeps
  counting (32964 -> 33419, 32897 -> 33261): **generations do not restart**, so an old index never
  matches a new cell (`cell_at` checks the full index).
* ✔ The rebuild's own DestroyCell calls are not refused by `h_Destroy`: `puppet_saves=0`, `kept=0`
  and `coop.pool why` empty after the host's and after the guest's rebuild. The guest recreates
  every mirror from the next state rounds (created 420 -> 863), every mirror maps to a live host
  cell afterwards, and each game shows the other player's cell where that player's game has it
  (0.0 m off). The partner's puppet is recreated once (`puppet_new=1` on both).
* ✔ The death UI and caption are gone within the 6 s the test waits (`cine=0`, hp > 0, not
  dying), and the knocked-out partner respawns after its 10 s. No fault in either game.
* ~ G+0x51D8 was never seen set by a console poll every 50 ms (6 s after the lethal hit): it is set
  and cleared inside one frame (RequestRespawn during SimUpdate, RebuildLevel in the same
  CellFrame), so a detector polling it from `tick` would miss it; the avatar index change is the
  signal to use if one is ever needed. None is needed today.
* ✔ The own cell editor (guest's mating call, cancel) also rebuilds the pool: the avatar comes back
  as a new object with a new index (7F010000 -> 808C0126, pointer changed) and **+0x111 clear**,
  also when it was set (with our timer running) as the editor opened. So BUG-0237's flag left set
  after the editor does not happen; `coop.pool invulnfor <s>` (tests) sets the flag and the timer
  as a respawn does. The pool after the editor return starts smaller (live 383 -> 215) and fills
  up again.

## The pool freed between our frames on the way Cell -> Creature (2026-10-29, fix/w6-cell)
<a id="cell-pool-freed-between-frames"></a>

BUG-0271. GOG 2024, lanes 4 and 5, `tools/tests/coop_cell_evolve_poll.py`.

* ✔ After History's check the game changes the mode to Creature and frees the cell pool (its own
  3.7 MB mapping, `0x398 * 4096`) inside one step of its loop: when the mod first saw the freed block,
  `G+0x1C` already read `0x0000BAAD` and `mode_id()` already read Creature. Our frame probe (every
  frame in Cell, VirtualQuery of the block) never saw a frame in Cell with the block gone (0 windows
  in 6 two-player runs): the teardown and the planet load run between two of our frames.
* ✔ In that gap the game's loading code pumps window messages; SporeKit runs queued main-thread
  console commands from that pump (WM_SK_PUMP), and the keepalive's network update runs outside the
  frame when frames stop for 0.5 s. Our state of the last frame (g_mode Cell, g_local = the local
  cell, the partners' puppet cells) is stale there. The crash stacks show it: the game frames above
  the fault are the creature planet's terrain (`cTerrainMapSet::GetHeight`, `cPlanetModel::ToSurface`).
  Faults seen with the fix off: modes::cell_fill reading `+0x54` of a freed cell from `coop.status`
  (a puppet) or `coop.avatar` / `coop.sync` (the local cell), on the host and on the guest (it
  leaves its Cell stage the same way when it follows), and `game::species_profile` reading
  `cell+0xB20` from `coop.avatar` (it asked the creature function on a cell).
* `*cCellGame::_ptr` stays set and the cCellGame object stays readable (as in section 13), so
  `cell_game() != nullptr` is still no proof of anything.
* The fix (`core.stale_local`): `modes::cell_at` reads a cell only from a pool block that VirtualQuery
  still finds committed (cached for our frame scope, where no game code runs; every call outside
  it); `cell_fill` reads only the live local cell or a live puppet; commands use the local entity
  and the puppets only while `mode_id()` is still the frame's mode, the stage is not being left
  (BUG-0264) and the adapter still finds them (`local_now()` / `puppet_now()` in coop.cpp).
* `coop.cellgone` (any thread): pool checks, refusals, and the frame probe's counters.

## New players, diets and the food a guest finds (2026-10-29, fix/w7-eat)
<a id="cell-new-players-food"></a>

BUG-0279 (Discord: "even at stage 1 only 1 of us could eat"). GOG 2024, lane 9,
`tools/tests/coop_cell_new_game.py` (both profiles without saves or creations, a new game from the
stage wizard, the guest joins from its menu).

* ✔ The Cell wizard's diet card picks the premade player cell: herbivore (leaf, left card)
  `E219E5D2`, carnivore (meat, right card) `252A132F` (`cll` 0x3D97A8E4, group 0x40616200). There is
  no omnivore card; an omnivore mouth comes only from the cell editor.
* ✔ A guest without creations of its own plays the host world's cell (`S+0x10` from the save the
  host shares, `coop.creations`: "mine cell" = the host's key, no swap, no rebuild): host and guest
  have the same mouth. Its puppet on the host has the same model.
* ✔ **GetCellModelData 0xE679E0** (same address in both builds) cdecl `u8*(ResourceKey by value)`,
  only the instance is read: the model data of a cell model from the map at `*(0x16B3C04)+0x70`,
  built on first use (0xE67940 / 0xE83E30, the 0xE83EC8 fault path of BUG-0004 for an unknown key).
  `+0x1250` carnivore, `+0x1251` herbivore: CanEat 0xE68470 reads both (`coop.pool diet` prints them
  for the own cell and the partners').
* ✔ Runtime, both diets, both players: a herbivore eats plants, a carnivore meat chunks (AvatarEat,
  +12 / +27 points); a carnivore touching a plant leaves it there with no points; small cells the
  avatar touches are sometimes swallowed without points (the guest reports them, `eats_sent` >
  `own_eats`), as in the solo game. With the release 0.7.3.0 the same (lane 9, run below).
* ✔ Populating ("tiles"): `PopulateTiles` 0xE7D370 (cdecl `(area, tileList*, bool all)`, callers
  SimUpdate 0xE7F4BA and RebuildLevel 0xE803FE): first every tile in the list that 0xE5F810 finds
  outside the active set is deleted (its object `+0x300` released, pool DeleteObject), then the
  active tiles (0xE5FA80(area) fills a stack array of 20-byte entries) that are not in the sorted list
  are created and populated (`PopulateTile` 0xE7C6C0 -> PopulateMarker per marker of the tile's
  layer), at most 0x32 per call unless `all`. A tile that leaves the active set and comes back is
  populated again.
* ✔ Culling: 0xE5CDE0 (EAX = cell; GOG 2016 0xE5CE00) is true for a gameplay cell (query `G+0x40FC`)
  whose sphere (`+0x58` * 1.7) is outside the box at 0x16B3C88 (GOG 2024), never for the avatar or
  the mate `G+0x51D4`. The visibility step 0xE78C00 then destroys only poison (type 5) and part
  tokens already collected; other cells outside lose their graphics (0xE66010(0)).
* ✔ Runtime: the area the host's game keeps populated is about one screen around the host's cell,
  not the 120 m the host sends. A guest holding W away from the host (carnivore, level 2) saw 41-51
  plants / meat on its screen up to 42 m from the host and **0 from 44 m on** (55, 68, 81, 93, 104,
  115 m), while the host saw 6-9 all the time. A second run right after the start of a new
  game (level 1, `--apart 70`): 9-18 up to 13 m, 0 from 23 m to 160 m (the host 2-11). CORRECTION (2026-10-29, fix/w7-eat): docs/known-issues
  said "more than ~100 m away" (BUG-0174); the empty pool starts at 23-45 m.
* Fix ideas (not done, BUG-0174): (a) the host populates around each partner too: a second tile
  list per partner (the list is a sorted vector with an allocator byte at `+0x1018`, layout not
  traced) passed to PopulateTiles with an area around the puppet, and cells there kept by `+0x354`
  (0xE7C080) while a partner is near; (b) a guest far from the host spawns its own food (plants and
  meat only) through PopulateMarker outside the host's area, eaten locally and sent as MSG_CELL_FOOD,
  removed when the host's area reaches it.

## Populating around every player: tile lists, view boxes, the host's view centres (2026-10-30, fix/w9-cell-food)
<a id="cell-partner-tiles"></a>

BUG-0174 / BUG-0279 (a guest far from the host swam in an empty pool). GOG 2024, Ghidra
(`out/ghidra-2024`), names in `overrides_dc04aee5.sym`.

* ✔ **View boxes** `Simulator::Cell::ViewBoxes` 0x16B3C88, 12 floats, written at the start of every
  SimUpdate by 0xE507F0 from the camera point: `[0..5]` the visible box (x +-10, y +-7.5 around the
  camera, z 0), `[6..11]` the background box (the same times `1 + globals+0x68 / 15`). Fixed size in
  cell coordinates at every level (the world scale is `G+0x514C`). Readers: the visibility cull
  0xE5CDE0, RemoveCellsOfOldScale, the populate range 0xE5F610 (layer 0: the visible box doubled
  around its centre, so +-20 x +-15; layer 1: the background box), spawn helpers 0xE4xxxx.
* ✔ **Tile lists**: PopulateTiles 0xE7D370 cdecl `(int levelBase, TileList*, bool all)`. A TileList is
  an eastl fixed_vector of `{int key, u32 tileIdx}` sorted by key: `+0 begin, +4 end, +8 capacity,
  +0x10 the fixed buffer` (a growth allocates with GeneralAllocator and frees the old buffer only when
  it is not the fixed one), `+0x1018` a byte passed to lower_bound 0xB41FA0. The host's list is
  `cCellGame+0x4128`; a list of our own (same layout, our buffer as the fixed one) works with the
  same function. Key = `layer<<24 | level<<16 | x<<8 | y` (bytes, compared signed;
  `cell_logic::tile_key`).
* ✔ Tile objects live in the pool `cCellGame+0x38`: `{+0 own index, +4 layer, +8 level, +0xC x,
  +0x10 y, +0x14 flag, +0x300 a released object}`. RebuildLevel (0xE7FD00) clears that pool
  (`cObjectPool_::Clear`: highWater 0) and empties the host's list; a list of ours then holds dead
  indices (check: `pool slot < highWater` and the object's first dword == the index).
* ✔ **IsTileActive** 0xE5F810 cdecl `bool(const int* {layer, level, x, y})`: the level within
  `[table(level of the food progress), +5]` and (x, y) in the range of the current view boxes.
  Callers: the delete loop of PopulateTiles and the cell cull **0xE5F8C0** (called by the cell update
  0xE7C080): a gameplay cell with level != -1 whose `+0x36C` parent is gone is destroyed when the tile
  under it is not active, unless `+0x354`. So "the active area" for cells is the tile range of the view
  box, read again for every cell every frame.
* ✔ **PopulateTile** 0xE7C6C0 cdecl `void(tile*, bool all)`: PopulateMarker for every marker of the
  tile's layer. Also called by RemoveCellsOfOldScale 0xE7CD10, which after a level change destroys
  every cell outside the visible box and then repopulates EVERY tile object of the pool (also the
  ones in our lists).
* ✔ **Two traps** (both crashed the first runs of the fix, lab 20261030-001703 / -002902): (1) the
  delete loop of PopulateTiles erases with an inlined loop bound to `cCellGame+0x412C` (the host
  list's end), not to the list it was given: deleting from any other list shortens the host's list and
  runs past ours (fault at PopulateTiles+0xC3). Our lists' deletes are done by our own code
  (`tiles_delete`: release `+0x300`, `cObjectPool_::DeleteObject` 0xB72370 by hand) and the game's
  loop is told every tile of ours is active. (2) The tile pool `G+0x38` has a fixed capacity (512 seen
  at level 1; `+0x18`); `cObjectPool_::CreateObject` 0xB72270 returns 0 when it is full and
  PopulateTiles then fills slot 0 of a live tile (a double free later, the free list broken, faults at
  CreateObject+0x37). A partner pass runs only with room for 4 x 0x40 tiles and creates no
  background (layer 1) tiles (its background box is all 0); seen: 71 of 512 with two players 150 m
  apart. On a level change (the level base argument differs), a rebuild (`all`) or a short pool every
  tile of ours goes back to the pool before the host's own pass (`coop.pool food 1000` faulted there
  without it, lab 20261030-004900 cell_evolve_poll). RebuildLevel clears the pool `G+0x38` at
  0xE7FFFF and calls PopulateTiles(all 1) at 0xE803FE.
* ✔ **Third trap, the world scale step** `WorldScaleStep` 0xE73E50 (cdecl `void(float dt)`, first in
  SimUpdate): while the scale `G+0x514C` moves towards `G+0x5150` (a level change takes several frames)
  it calls 0xE6DB70 (EAX = `0x16B3C0C+0x48`, a float ratio on the stack: rescales the cells, the camera
  and the boxes), which **empties the host's tile list and fills it again from every tile object of
  the pool**, one entry per key (the last object of a key wins). With our tiles in the pool the host's
  list then held them too (both lists freed them: the double free behind the faults at
  CreateObject+0x37, seen with a consistency check: right after our deletes the host list had exactly
  our 15 freed tiles, lab runs of 2026-10-30 on lane 2), or one of ours replaced the host's own tile
  of a shared key (that one leaked). The only deleter of tile objects is PopulateTiles' loop (every
  other `cObjectPool_::DeleteObject` call is on other pools). Fix: `h_WorldScale` gives all our tiles
  back to the pool before a moving-scale step, and no partner pass runs while the scale moves.
* ✔ **Shared tiles hand over**: a tile two lists hold is populated once; the other one is a
  "shadow" (no cells of its own; RemoveCellsOfOldScale skips it too). When the populated one is ours
  and goes (a level change flushes all our tiles), the shadow is populated right then (`hand_over`):
  without it the host's own area stayed empty after a level-up (`cell_new_game`, the host found no
  meat on its screen, lab 20261030-022912).
* The fix (`coop.fix cell.partner_tiles`, cellsync.cpp `h_PopTiles`): on the host, after the game's
  own pass, one more PopulateTiles call per partner with a list of our own and the 12 view-box floats
  moved by (partner cell - host box centre), restored right after. `h_PopTile` populates a tile that
  another list (the host's or another partner's) already holds only once (and remembers the skipped
  tile objects, so RemoveCellsOfOldScale does not populate them either) and skips a partner's
  background tiles; `h_TileActive` answers true for a gameplay tile inside ANY player's box, so the
  host's tiles and cells near a partner stay and a partner's tiles near the host stay. A partner
  without a cell for 2 s: our code deletes all its tiles and frees the list. Our lists are forgotten (not drained) when
  the tile pool was cleared or the stage is left. Each guest gets the cells within 120 m of its own
  cell (before: within 120 m of the host's).
* **The host's view centres**: ONE module for every stage, `mods/coop/view_centres.{h,cpp}`
  (namespace `views`, merged 2026-10-30 with fix/w9-creature's BUG-0280 registry,
  docs/re/creature.md#creature-view-centres). coop.cpp notes each partner it shows in its own place
  (`views::note`, any stage; a position that is not finite is no centre, BUG-0292); the Creature and
  Tribe relevance hook reads them (`npc.view_centres`, `npc.view_centres_tribe`), the Cell stage reads
  the same ones (`views::partners(kCell)`) into a `views::Set` (the host's view first, then every
  partner, at most `views::kMaxCentres` = 8; `near_any` / `nearest`), rebuilt every SimUpdate and
  readable as `cellsync::view_centres()`. The cost in Cell is one PopulateTiles call per partner (at
  most 0x32 new tiles each), IsTileActive up to once per centre for tiles and cells the own box
  refuses, and the pool holds the cells of up to N screens.
* Rivals (docs/11-rivals.md, 0.8): every player plays on its own, far from the host. The same module
  serves it: the registry already holds every partner in every stage; each stage turns it into its
  own keep-alive (Cell: tile passes + per-guest state; Creature / Tribe: relevance spheres) and asks
  `views::Set::near_any` / `nearest` instead of "near the host's avatar".
  Not done for Cell: a per-centre level (a Rivals player far ahead of the host would need its own
  tile level; the level here is the shared food progress).
* Not verified: the pool's capacity with four players far apart (the cell pool holds 4096 slots in
  for_each_cell's bound; seen sizes up to ~650 with 4 players together); a host level-up wipes and
  repopulates the cells around partners too (vanilla behaviour for anything outside the view).
## The guest's own pool while the host is away (2026-10-30, fix/w9-cell-away)
<a id="cell-host-away"></a>

BUG-0205 / BUG-0151 (GitHub #130). GOG 2024, lane 3, `tools/tests/coop_cell_away.py [--editor]`.

* ✔ With the host in the main menu (no state rounds, no totals) the guest's pool stood still: 0 of 14
  creatures around the guest moved more than 1 m in 6 s (`pool.away` off). The mirrors are held at
  the host's last pose by `guest_tick` (the extrapolation is capped at 0.5 s), `h_Populate` and
  `h_SpawnLoot` are off on a guest and `h_Pair` skips every pair without the guest's own cell.
* ~ Between two frames the AI lock +0x16C read 0 on the mirrors too (`coop.pool npcs`, a console
  read): the mod writes it every frame (`guest_tick`, `guest_state`), something in the game's step
  seems to clear it again. The mirrors do not move all the same (the pose is written every frame).
  Not analysed further; the test checks the movement, not the byte.
* ✔ Clearing +0x16C and +0x354 (keep-alive) of the former mirrors and forgetting them as mirrors is
  enough for the guest's game to run them: 11 of 12 creatures moved within 6 s. 570 cells were
  released (the whole mirrored pool, not only the ones on the screen). `h_Populate` was not called
  in the ~10 s the host was away (`solo_spawns=0`): the tiles around the guest were already populated.
* ✔ The return: the host's first complete state round after its reload purges every cell the guest
  had as a stray (the purge spares the puppets and the mate) and recreates the mirrors: 315 mirrors,
  each a live host cell, no own creature left within 30 s. No fault on either side.
* ✔ The guest's food while the host was away (`coop.pool food 10`, the hooked AddFoodPoints): kept
  on the guest and sent as one numbered meal just before the first state round is applied; the
  host's total grew 0 -> 10 and the guest's stayed 10. Sent while the host is in the menu, the same
  meal was acknowledged without its points (BUG-0239) and the guest's total fell back to 0.
* ✔ The cell editor through the mate: the Cell stage is left ~10 s after the cell touches its mate
  (9.86 s and 10.02 s, `coop_cell_editor_timed.py`; the mating cinematic). The host's editor
  therefore counts as away (4 s) only well after the stage was left.
* ~ The game's death count S+0x70 stayed 0 after a death through KillCell / PlayerDeath (the
  `selfhit` death of `coop_cell_respawn.py`, both machines); the table above names only
  BreakOrSwallow 0xE7AEE0 as its writer. Consistent, not checked with a break death.
## The cell broadphase holds ~2048 cells (2026-10-30, fix/w10-cell-4p)
<a id="cell-broadphase-cap"></a>

BUG-0295 / BUG-0296 / BUG-0297 / BUG-0298. GOG 2024, Ghidra (`tools/ghidra/q.sh --build 2024`) and
lanes 2 / 3 with four players (`tools/tests/coop_cell_4p.py`, suite `cell_4p`).

* ✔ `Simulator::Cell::SimUpdate+0x131` (0xE7F4D1) calls 0xBBBF60, a thunk to **SweepPrune::UpdatePairs
  0xBBB450** (cdecl, one argument): for each of two axes it merges the new endpoints into the sorted
  endpoint array (**SweepPrune::MergeAxis 0xBBAF10**), then sweeps axis 0 with an active list of 1024
  ints on its stack and writes the overlapping pairs. An endpoint is 8 bytes `{short type (0 min),
  short object id, float value}`; object records are 16 bytes at `sap+0x14` (+8 / +0xC: its endpoint
  index per axis, a short).
* ✔ MergeAxis keeps the displaced endpoints in a **stack buffer of 0x8000 bytes = 4096 endpoints =
  2048 objects**, with no bound check. A game holding more cells overruns it: the saved arguments
  are overwritten and the loop faults at **0x00BBB07B** (MergeAxis+0x16B, `add esi,[ebx+0x14]` with a
  garbage EBX; EAX, the merge index, read 0x1021..0x1084 in the four logs), which the crash tools
  symbolise as `Simulator::cStarRecord::GetPlanetRecord+0x4AB`. SporeKit catches it, the frame goes
  on with a corrupted stack and heap: seen right after it on the same games, a crash in DestroyCell
  of a mirror (0xA81FE6, cCellGFX, BUG-0296), a null read on a worker thread (0xA479B7, BUG-0297),
  faults at small addresses (0x9BB) and games that stop answering (lab 20261030-035318 campaign#2
  and #3: every game faulted at 0xBBB07B within 2 s at level 4, with 1950-2450 live cells).
* ✔ The cell pool's capacity (4096 slots, `G+0x1C+0x18`) is not the limit: the broadphase is.
  Measured with the budget off (lab 20261030-052851, four players, food progress at level 4): the
  host's pool reached 4096 (full) and 8203 faults, the guests 3140 / 2211 / 2334.
* ✔ Where the cells came from: at level 4 the host alone with three partners next to it held ~1400
  to 2200 cells; with the partners 200 m apart (BUG-0174's partner passes, one screen of tiles each)
  it grew past 4000; and a guest got every host cell within 120 m of itself (up to 3140 mirrors).
* The bound (`coop.fix cell.budget`, cellsync.cpp): (1) a partner's tile is populated only while the
  host has fewer than 1100 live cells (kPopCap; first 1400: host peak 1714, lab 20261030-053258; then 1250: peaks 1759-1776, lab 20261030-063326) (`G+0x1C+0xC`); the others are kept as empty tiles ("starved")
  and populated, 4 per frame, when the pool has room; a tile on the partner's own screen waits
  only above 1300 (kPopCapScreen; `partner_core`: IsTileActive reads the gameplay range as the visible box
  doubled, so asked with the partner's visible box halved it answers for the screen itself; without
  it a far guest's screen stayed empty, lab 20261030-054633); the partner passes take turns being first;
  a starved tile does not count as "populated elsewhere" for the other lists; (2) each guest gets
  at most its 900 nearest cells (nth_element by distance to its centre); (3) a guest creates no new
  mirror at 1500 live cells. Measured with the 1400 cap (lab 20261030-052856): host peak 1557, guests 1282 to
  1394, no fault, the far guests had 185-322 host cells within 60 m. The host's own pass is never
  limited (vanilla; ~ superseded 2026-10-30, see the host tiles below), so the host's own area keeps all its food; with four players at level 4 the
  partners' areas are thinner than the host's (starved tiles waited, 542 starvations, 99 fed later).
* ✔ `coop.pool` prints `cell_tiles: ... budget= starved=<count>/<now> fed= send_capped= mirror_capped=
  cells=<live> peak=<highest seen> level=<tile level base>` on both roles.
* ? Not measured: whether the active list of 1024 (UpdatePairs) can overflow with fewer cells (it
  holds the cells overlapping one x position; would need ~1000 cells in one column).
  ~ superseded (2026-10-31): see #cell-sap-active-list. CORRECTION (2026-10-31, fix/w20-cell): it
  overflows (1100 cells in one column, and a guest with ~1024 objects left open in the campaign):
  the return address is overwritten, BUG-0337.
* The sync-check (`net/diagmsg.h` compare) and the campaign's coverage (`coop_multi.measure`) now
  count only host cells within 60 m of the guest (BUG-0298): each guest is sent the cells around
  itself, not the ones around the other players (it reported "1033 missing on the guest").
* ✔ The live count lags: a populated tile's cells reach `G+0x1C+0xC` over the next frames, so many
  tiles in one frame overshot the caps; from kPopCap - 250 at most 2 partner tiles (passes and
  feeding together) per frame. A starved tile remembers its partner and is fed by the same rule
  (screen tiles between the caps: without it a far guest's screen stayed empty, lab
  20261030-060124). Even so the host's count jumps by ~250 now and then with no partner tile
  (loot, the game's own spawns; not traced): the caps keep ~450 below the broadphase's limit.
* ✔ While partners play, the host's own gameplay tiles follow the same rule (its screen up to 1300,
  the ring of its doubled box only below 1100): at level 4 the host's own box alone held ~1000
  cells, so with partner-only rules a far guest's screen got nothing (cell_4p, lab
  20261030-064009). The background layer (layer 1) and a host without partners are never limited.
  With it: cell_4p 3/3, host peaks 1386-1430, guests <= 1500 (lab 20261030-065411).
* With caps 1250 / 1500 `cell_4p` x2 (lab 20261030-055041): host peak 1625-1626, guests
  1135-1339, no fault; the final caps 1100 / 1300: see BUG-0295's Verification.
* ✔ BUG-0299 (a guest's own cell NaN with no NaN mirror, 4-player campaign_short, lab
  20261030-053709 i14): the order seen was the own position NaN, then a fault in CreateObject+0x56
  under PopulateTiles+0x1EF (garbage tiles from NaN view boxes: the pool's live count grew by one
  per frame while the list held 12), then 90 s later the level-up's RemoveCellsOfOldScale crashed in
  PopulateTile+0x8A. `coop.pool nanself` (tests) makes the same state: without the guard the
  guest's game hung after the level-ups (hang report, lab 20261030-055502). The guard
  (`coop.fix cell.nan_guard`): no PopulateTiles / PopulateTile while the own view boxes are not
  finite, and the own cell is written back to its last finite position (`+0x4C`, `+0x84`, the
  soft-body particles `G+0x4108` on a ring of 0.4 x size around it, at rest). ? Where the NaN came
  from is still not known (the guest held 900 mirrors, none NaN).

## A group member copies a dead chase target from its leader (2026-10-30, fix/w12-cell-fault)
<a id="cell-pick-stale"></a>

BUG-0306. GOG 2024, Ghidra (REA), lab 20261030-104149 campaign#2 (host, 4 players, level 4).

* ✔ The fault `0xC0000005 at 0x00E52949` (data 0x108, ESI=0), which the crash tools name
  `App::cCellModeStrategy::OnMouseMove+0x1939` and whose chain they name `RestAfterBite+0x479 <
  DestroyCell+... < PopulateTile+0x25A` (nearest-symbol names, all wrong), is **IsDamageable
  0xE52940 + 9** (`mov ecx,[esi+0x108]`, ESI = the cell asked about). The real chain from the
  stack words: `SimUpdate 0xE7F3A0 -> 0xE7E7A0 -> UpdateCells 0xE7C8C0 (ret 0xE7C91A / 0xE7CA4C) ->
  AI dispatcher 0xE7A190 (ret 0xE7A394) -> behaviour 0x1006 0xE7A0A0 (ret 0xE7A0CA) -> chase step
  0xE79C30 (ret 0xE79F1E) -> 0xE57CE0 (ret 0xE57D09) -> IsDamageable`. 0xE719F8 in the list is a
  stale word (the AoE 0xE71980 run earlier by 0xE76A80). The stack held `0xE57CE0(cell
  0x1C80C870, NULL)`.
* ✔ **0xE57CE0** cdecl `bool(cell* a, cell* b)`, plain ret: `GetScaleDifferenceWithPlayer 0xE57340(a)
  != 1 && a+0xFC != 0 && IsDamageable(ESI = b)`; b is not checked for NULL.
* ✔ **Chase step 0xE79C30** cdecl `void(cell*, resData*, float dt)`: (G+0x51DC and the avatar alive:
  swim away from the avatar); `+0x224 > 0` rest: count down; `+0x228 > 0`: count down, `+0x178 = 1`;
  `+0x21C > 0` flee `+0x220`; `+0x230 > 0` chase: `b = GetIfNotDeleted(+0x22C)` (0xB722E0 on G+0x1C,
  thiscall ret 4: NULL for 0, a slot above the high-water mark or another generation) -> 0xE57CE0(cell,
  b) -> steering 0xE6F800 (raw Get 0xB72320 of +0x22C, no check); at the end of the chase
  `+0x224 = res+0x88`; else idle (+0x220 = +0x22C = 0, wander 0xE6FF60). All float tests are COMISS
  (NaN takes the "<= 0" branch), the same as in 0xE70E30.
* ✔ **Behaviour 0x1006 0xE7A0A0** = target timers 0xE70E30(cell, res, dt) then 0xE79C30(cell, res,
  dt); E79C30 has no other caller. 0xE70E30 returns at once when `+0x224 > 0` or `+0x228 > 0`, clears a
  dead flee target (`+0x21C > 0`, a live one returns), clears a dead chase target (`+0x230 > 0`), and
  only **then** counts `+0x238` down and calls PickTargets (`+0x238 = 0.25`).
* ✔ **PickTargets 0xE70CF0** (same address in both builds): a cell whose group (`+0x38C`, pool
  **G+0xA8**, `GetIfNotDeleted` on it) has a leader (`group+8`) other than itself that is alive copies
  the leader's `+0x21C, +0x220, +0x230, +0x22C, +0x208` **without checking them** and returns 0. So a
  leader still holding a dead target (its target died after its last check: it comes later in the
  pool, or it rests / is dead and skips the behaviours) hands it to the follower, and 0xE79C30 passes
  NULL to 0xE57CE0 in the same call. A vanilla bug (a crash on Windows), made likely by many deaths
  per second (four players eating, tiles going inactive) and low frame rates.
* ✔ Why it repeats at every frame: Wine swallows the access violation and the game's frame is
  aborted, the rest of UpdateCells included. The leader (later in the pool) never runs its check, the
  follower's 0xE70E30 clears the copy, sets `+0x238 = 0`, calls PickTargets at once, copies the dead
  target again and faults again: 8616 faults in 5 min at the host's frame rate, every cell after
  the follower frozen on the host, until the stage was left.
* Fix (`coop.fix cell.pick_guard`, cellsync.cpp h_PickTargets, both roles, puppets left alone):
  after PickTargets a flee / chase target that is not a live cell is dropped with its timer, as
  0xE70E30 would do one frame later. `coop.pool` prints `stale_targets=` (cell_contacts line);
  `coop.pool staletarget [n]` (tests, host) makes n group leaders chase a dead cell and rest 60 s.
  Test `coop_cell_stale.py` (suite `cell_stale`).
* ? Not measured: how often the vanilla game reaches the copy with a dead target; which leader
  held the dead target in the lab run (the follower's index is 0x1C80C870's first dword, not logged).

## The own cell's NaN repeats: fields the heal must repair (2026-10-30, fix/w12-cell-fault)
<a id="cell-nan-fields"></a>

BUG-0307 (follow-up of BUG-0299, #cell-broadphase-cap end).

* ✔ (lab 20261030-121742 / -122306) A soft-body cell's position comes back from its particles and
  the shape matching at every cell step: with the rotation (+0x5C), the shape matrix (+0x24), the
  size (+0x58 / +0xB4), the mass (+0xB8, inverse +0xBC) or a particle's rest offset
  (`parts + 0x11944 + 12*k`, 1500 particle slots, k from +0x254) NaN, a position written back finite
  is NaN again after the next step. Repairing position, particles' positions and velocities alone
  (BUG-0299) does not hold.
* ✔ Identity matrices, the last finite size and mass and rest offsets on the particles' ring are
  accepted by the game: the cell swims on and the next fit rebuilds the shape (cell_nan_all,
  lab 20261030-121908).
* ✔ Negative: re-creating 900 mirrors 40 times (28 049 CreateCellObject + DestroyCell on the guest)
  does not use address space (lab 20261030-121142); neither do 15 s of a NaN own cell.
* ? The first NaN's source: not caught yet. The probes (cellsync.cpp own_probe) name the step where
  it appears.

## Background cells left behind by jumps, and what did not reproduce (2026-10-30, fix/w13-cell)
<a id="cell-bg-leftovers"></a>

* ✔ **Background cells are not freed when the camera moves away.** The decoration of the background
  tiles (cells with query `+0x35C` = `G+0x4100`, levels above the player's, 6 and 7 at level 4) stays
  in the pool after the own cell has jumped far: 40 jumps of 150-250 m (`coop.pool tp`, 3 s apart)
  left 1099 of them on a guest and 1075 on the host (37 / 35 before, 807 / 781 over 150 m from the own
  cell; out/hunt of fix/w13-cell, lanes 2 and 3). Both roles, at 3 fps and at 65 fps, so a game
  behaviour that our jumps expose: the game frees them only on a level change
  (RemoveCellsOfOldScale). The foreground cull (0xE5F8C0, per foreground cell in UpdateCells) never
  sees them. On a guest the pool then reaches the mirror cap (1500 live cells, BUG-0295) and no
  mirror is created any more: the guest has 0 of the host's cells (BUG-0314, the coverage 1% of
  BUG-0307's campaign run, whose NaN heals made the own cell jump 100-200 m at a time).
* ✔ `cell.bg_cull` (cellsync.cpp `bg_cull`): every 2 s, outside a level change, background cells
  (not `+0x354` keep-alive) farther than 150 m from the own cell are destroyed with DestroyCell (as
  RemoveCellsOfOldScale destroys cells outside the view), at most 300 a pass. `coop.pool census`
  lists every used pool slot by kind (foreground, background with distance and levels, mirrors,
  puppets, strays, culled).
* ✔ Negative (BUG-0307's address space drain): 3 minutes of a NaN own cell (every field, the heal off,
  level-ups, swimming) kept the guest at 3632 MB free (lab 20261030-133256); 4 players with Carol at
  3 fps jumping 80 times (28 538 mirrors created) kept 3613-3620 MB. The drain is neither the NaN
  state nor the jumps nor the mirror churn. Still unknown.
* ✔ Negative (BUG-0282): on 5d7f20e the low-frame-rate trail did not come back: 2 players, guest at
  15 fps, 30 checks, 0 of 16 616 cells over 1.2 m (worst 0.08 m, lab 20261030-133013); 4 players
  (Carol 15 fps, Dave 30 fps) after the food went to 200: p90 median 0.05 / 0.04 m, largest 0.39 /
  0.45 m, 0 checks over the limit.
* ✔ The own_probe lines (BUG-0307) never fired in these runs (4 players, level-ups, 3 fps, jumps):
  the natural first NaN was not caught.

## Our pool work while the Cell stage is torn down (2026-10-30, fix/w13-cell)
<a id="cell-leave-guard"></a>

* ✔ Static: between the posted quit (world.cpp `load_slot`: "quit to the galaxy menu posted",
  `game::stage_leaving`) and the mode change (~0.8 s, three frames in lab 20261030-104149
  campaign_short#3) `game::mode_id()` still reads Cell and the cell game is still there, so
  cellsync's `in_cell()` is true. Its `avatar()` reads the pool directly (not `game::avatar`, which is
  null while leaving, BUG-0264), so `tick()` kept running `guest_tick` / `host_tick` / `bg_cull` /
  `symbiosis_tick`, `on_message` kept applying MSG_CELL_STATE (`guest_state`: CreateCellObject for
  new mirrors, field writes, DestroyCell for dropped ones), and modes.cpp `h_SimUpdate` kept writing
  the puppets' poses, all on the pool the game frees in SimUpdate (the BUG-0308 fault was in
  `cObjectPool::_dtor` under SimUpdate+0x6ED). A rejoin in Cell is exactly this case: the guest
  creates ~900 mirrors at the join and the host's world arrives ~3 s later.
* `cell.leave_guard` (modes.cpp `cell_leaving()`): none of that runs while `game::leaving_stage()`.
  `coop.pool` prints `leave_writes=` (states / ticks applied while leaving, the off side) and
  `leave_dropped=` (skipped). Whether this work corrupted the heap is not shown.

## The broadphase sweep's open list holds 1024 objects (2026-10-31, fix/w20-cell)
<a id="cell-sap-active-list"></a>

BUG-0337. GOG 2024, Ghidra (`tools/ghidra/q.sh --build 2024`), lab 20261031-054758 campaign#1 (the
report) and `tools/tests/coop_cell_sap.py` (suite `cell_sap`). Answers the "? Not measured" of
#cell-broadphase-cap: yes, it overflows, and then every frame faults.

* ✔ The cell broadphase is the sweep-and-prune at `cCellGame+0x4104` (SAP, built by 0xBBB570:
  s+0 pair capacity, +4 object capacity (4096), +8 pair count, +0xC pairs, +0x10 live objects,
  +0x14 object records (16 bytes), +0x18 high water, +0x1C free list head, +0x20 endpoint count,
  +0x24 / +0x28 sorted endpoints per axis, +0x2C pending count, +0x30 / +0x34 pending endpoints
  per axis, each 2 x capacity x 8 bytes). Object record: +0 the pair key, +4 a group (same nonzero:
  never a pair; -1 after RemoveObject), +8 / +0xA min endpoint index per axis, +0xC / +0xE max
  (short, bit 15: still in the pending array). Endpoint `{short type (0 min, 1 max, -2 removed),
  short object, float value}`.
* ✔ A cell's object id is cell `+0x364` (-1: none). CreateCellObject (0xE74FF6) adds it through
  0xBBBF40 -> AddObject 0xBBB290 (pending entries, merged at the next sweep); DestroyCell (0xE78116)
  and the world rebuild 0xE7FD00 remove it through 0xBBBF50 -> RemoveObject 0xBBB380 (its four
  entries marked -2, the record to the free list; the cell's `+0x364` is not reset). Every frame
  0xE6DAE0 (and the world scale step 0xE6DB70) moves its bounds, `pos +- r` with r = model radius
  (model data +0x1244) x cell `+0x58` (0xE6D790), through 0xBBC460 -> UpdateObject 0xBBBFD0, which
  moves each endpoint by insertion with strict `<` (0xBBB770 / 0xBBB880 / 0xBBB990 / 0xBBBA70). The
  pending sort's comparator 0xBBAE50 never answers "equal" (equal values: either order).
* ✔ `SweepPrune::UpdatePairs` 0xBBB450 (SimUpdate+0x131, every frame): MergeAxis for both axes,
  then the sweep along x keeps the objects whose min it has passed and whose max it has not in
  **an int[1024] at esp+0x1C of a 0x100C-byte frame, no bound check**. open[1024] is the return
  address, open[1025] the argument, then SimUpdate's frame. With more than 1024 objects open at
  once `ret` jumps to an object id: faults at 0x196, 0x4D6, 0x144, ... with **esp=0012D394**,
  edi=01639470, ebx=034CF4E8, ebp=034CF480 on the main thread (the crash tools name the stale
  return addresses above it "UIMissionCardPanel_HandleMessage+0x775 < cCellModeStrategy::OnExit+0xC4C",
  really FUN_00e18aa0 0xE18AD5 and SimUpdate's caller 0xE8084C). EDX at the fault is the open
  count at the end of the sweep. The game catches it; the rest of SimUpdate is skipped every frame
  (the own cell stops, 500-750 ms per fault on Wine, lab: 2642 faults in 40 s), the stack above is
  overwritten.
* ✔ Reproduced: `coop.pool column 1100` on a host with ~1300 cells (food 300) puts 1100 cells on one
  40 m line: `coop.fix cell.sap_sweep off` -> the same fault signature at once (esp=0012D394, small
  EIPs, edi=01639470), the host's cell stops, a guest lost its mirrors (lab 20261031-075501).
* The fix `cell.sap_sweep` (cellsync.cpp `h_UpdatePairs`, hook on 0xBBB450): the same two
  MergeAxis calls, the same sweep with the open list in a heap vector, the same pair test (the
  game's `SweepPrune::OverlapOther` 0xBBB110) and the same pairs in the same order. `coop.pool sap`
  replays the sweep (peak_open, open_at_end, max-before-min, duplicate / lone min entries, which
  cells are left open) and prints our counters (`sweeps`, `over_1024`, `sweep_peak`,
  `sweep_left`); more than 64 objects left open at the end of a sweep logs that report (30 s apart).
* ? Why the campaign's guest (Carol, 927 cells, 900 mirrors) had over 1024 open objects: EDX at its
  faults was 0x3F9-0x407, i.e. ~1024 objects **still open at the end of the sweep** (a max endpoint
  before its min, or min entries left behind), more than its live cells. In a healthy game none is
  left (three replays of the campaign's Cell stage with `coop.pool sap` every 10 s on lanes 5 / 6:
  peak 13-43 open, 0 left, 0 duplicate or lone entries). The same frame (esp=0012D394, EDX
  0x614 / 0x7F0 with 2000-2450 cells) is in lab 20261030-035318 after BUG-0295's MergeAxis overrun.
  Inferred, not seen: the endpoint arrays were corrupted earlier (entries of a removed object left
  behind by a RemoveObject with stale indices, or a broken order); not reproduced. The warning
  above records it the next time.

## The broadphase's pending list overflows while the simulation is paused (2026-10-31, fix/w22-cell)
<a id="cell-sap-pending"></a>

BUG-0348 (and the cause of BUG-0337's objects left open). GOG 2024; lab 20261031-120025
campaign#1 (guest Bob) and `tools/tests/coop_cell_pause.py` (suite `cell_pause`).

* ✔ AddObject 0xBBB290 (through 0xBBBF40, from CreateCellObject) appends the new object's two
  endpoints per axis at `s+0x30[axis] + 8 * s+0x2C` and adds 2 to `s+0x2C`, **no bound check**; the
  pending arrays hold 2 x capacity = 8192 entries (64 KB, allocated by 0xBBB570 right after the
  sorted array of the same axis). Only UpdatePairs (the sweep, inside SimUpdate) merges and resets
  them. RemoveObject marks pending entries -2 but `s+0x2C` does not shrink, so every create counts.
* ✔ In the pause menu SimUpdate does not run, but a guest keeps applying the host's states: mirrors
  dropped and created again (or new ones as the host moves). Measured on lane 8 (`coop.pool zap 5000`
  every 2.5 s while paused): pending 1774, 3532, ... **20376** after 12 rounds, 2.5 x the arrays,
  i.e. ~97 KB written past pending[0] (over the next blocks: the axis-1 sorted endpoints and
  whatever the heap put after pending[1]). After Resume the sweep left 477 objects open (entries
  with stale or foreign ids, a broken order: BUG-0337's "~1024 left open").
* ✔ The campaign case: Bob's position did not change from 13:07:36 to 13:08:36 (game clock) while
  his mirrors went 354 -> 502 -> 1 -> 95 -> 248 -> 330 (a pause menu whose Resume was lost, BUG-0289);
  right after it the first warning of BUG-0337's diagnostics: 198 objects left open, 194 of them
  with his avatar's object id; 30 s later 782 (endpoints 2597 for 904 live objects); then on the
  way to Creature `GeneralAllocator_FreeInternal+0x13D` freeing the event pool `G+0x54` in the
  Cell teardown 0xE7F9B0 (return 0xE7FAF0; the crash tools say SimUpdate+0x750).
* ✔ Reproduced (lane 8, `cell.sap_pending` off): pending 17424 while paused, Resume, then the guest
  leaves Cell for the galaxy menu (`coop.menu`): 88 faults, first `GeneralAllocator_FreeInternal+0x64`
  freeing pool `G+0x8C` (return 0xE7FA8D, BUG-0308's exact signature), then 0x00DDA358 (data 0x18) as
  in both campaign reports. The heap blocks after the pending arrays are what the teardown frees.
* The guard `cell.sap_pending` (modes.cpp `cell_sap_room()`, used by `cell_create` (mirrors) and
  `cell_spawn` (puppets)): no cell of ours while `s+0x2C + 2 + 512 > 2 x s+4`; the mirror is made
  by a later state once the simulation runs again. `coop.pool sap` prints `held=` (creations held).
  The game itself creates cells only inside SimUpdate, before the sweep of the same frame.
* ~ BUG-0308 (a rejoined guest, the same fault freeing pool `G+0x8C`, return 0xE7FA8D): the same
  signature is what the pending overflow gives; whether that guest's game was not simulating while
  it made mirrors (the join, a pause) is not shown.
## A guest's bite and the health ring; the editor's DNA budget; joining after a load (2026-10-31, fix/w22-reports2)
<a id="cell-guest-bite-ring"></a>

* ✔ (Ghidra GOG 2024, DamageCell 0xE7A7C0) the health ring a bite shows is `ShowHealthRollover`
  0xE62340 (cdecl `void(cell*, int hpBefore)`, plain ret), called by DamageCell before the hp change
  when the avatar is the victim or the attacker (or the attacker's +0x110). It returns at once without
  GFX (+0x248 = 0) and otherwise calls 0xE62200(cell index, hp/6): an entry of the cell UI's
  rollover pool `*cCellUI::_ptr (0x016B3C0C) + 0x9C` (cObjectPool, docs §0): `+4` the cell's pool
  index (an existing entry for that cell is reused), `+8/+0x14/+0x1C` hp/6, `+0x24` a timer set to
  0.5 (runtime: 0.42-0.50 0.1 s later), `+0x28` 0.25, `+0x30` a flag. The same ring is made by
  EatChunkA/B (0xE7DD90 / 0xE7DBD0) and 0xE62500, 0xE71520.
* ✔ After the hp change DamageCell plays the victim's hurt animation through
  `Simulator::Cell::PlayAnimation` 0xE6D200 (`(cell*, 0, anim, cell+0x1B0)`: anim 0xE type 1, 0xF
  type 3, 0x11 type 5, else 0x10; it writes the animation to +0x1B0 and its length to +0x1B8) and
  schedules event 2 (0xE51EC0) with that length, whose end runs the after-hurt reaction; with a
  zero length it calls 0xE7A770 at once.
* ✔ (runtime, lab 20261031-125228..130728, BUG-0350) on a guest the bite on a mirror never ran the
  original (the host applies it), so no ring: `ring=-1` in the guest's game while the host's own bite
  on the same creature showed `ring=0.50` in the host's game. The fix `cell.bite_ring` calls
  ShowHealthRollover itself under DamageCell's conditions (damage > 0, GFX, hp > 0, not dead or
  dying, +0x17C and +0x111 clear, ShouldNotAttack false): ring 0.50 on each of 3 bites. The hurt
  animation is not played on the mirror: its event 2 would run the after-hurt AI reaction on a
  passive mirror (not tried).
* ✔ A host creature that flees after a bite often has **no GFX on the guest** (`gfx=0 particles=0`,
  same pool index) for seconds right after the guest was teleported next to it (tests'
  `coop.pool swimto <key> tp`): it is not drawn there yet, and the game would show no ring either.
  With the fix off the same. `coop.pool cell <key>` now prints `ring=`, `anim=` (+0x1B0), `idx=` and
  `onscreen=`; `coop.pool kill` prints `ring=`, `rings=` and `skip=` (why the last guest bite showed
  none). The `onscreen=` test (world_to_screen) said 0 for cells 3-5 m from the guest's cell on the
  headless lanes now and then: not a reliable "drawn" test, GFX is.
<a id="cell-editor-dna"></a>
* ✔ (Ghidra) the cell editor's DNA budget: the request 0xE59800 computes
  `DNAForFood(S+0x1C) - S+0x2C` and sends it with message 0x90A03FDF after
  `Editors::EditorRequest::Submit`. `DNAForFood` 0xE51410 is fastcall (ECX = food), interpolated in
  the level table 0x01483B54 (7 ints: level, food threshold, DNA, ...): 0 -> 0, 50 -> 5, 100 -> 10,
  150 -> 13, ... 350 -> 25, 400 -> 28, 450 -> 31, 500 -> 34; food 1000 uses the entry 0x01483DA0.
  `S+0x2C` is the DNA spent so far. The mod writes neither. Since BUG-0351 the log has
  `cell: DNA at the editor request: food= plant= spent= dna_for_food= budget=` (each request, also
  after the vote) and `cell: DNA on entering Cell: ...` (a load, an editor's return).
<a id="cell-join-after-load"></a>
* ✔ (player logs 2026-10-31, BUG-0352) a loaded Cell world starts the player's cell at the pool's
  start, also on a guest; only an own-cell rebuild moved a guest to the host (`join_partner`). The
  fix `cell.join_after_load`: every new avatar index of a guest (a load, an editor, a rebuild) goes
  2.5 m next to the host once the host's state is here (2 s to 30 s after the avatar appeared, not
  while knocked out), when it is 15 m or more away; log `cell: our new cell joined the host`.

## The soft-body particle pool holds 1500 (2026-10-31, fix/w23-cell)
<a id="cell-particle-pool"></a>

BUG-0338. GOG 2024, Ghidra and `tools/tests/coop_cell_particles.py` (suite `cell_particles`).

* ✔ Particle pool `cCellGame+0x4108` (the `parts` the mod already uses: positions +4 + 12i,
  velocities +0x4654 + 12i, goal offsets +0x11944 + 12i): owner +0x18E74[i], next +0x1A5E4[i], count
  +0x1BD54, high water +0x1BD58, free-list head +0x1BD5C; **1500 slots** (0x5DC).
  Simulator::Cell::ParticleAlloc 0xE4DBA0 returns -1 when 1499 are used (so it never hands out slot
  1499); Simulator::Cell::ParticleFree 0xE4EC10 has no bound check.
  ~ superseded (2026-10-31): "never hands out slot 1499", see the CORRECTION below.
  CORRECTION (2026-10-31, fix/w26-cell-spare): with no free slot below the high water,
  ParticleAlloc's index is the new count (slots 1..1499, slot 0 never used), so it does hand out
  1499 at 1498 used; BUG-0338's spare could then be a real cell's particle and that cell's free of
  it was dropped (one slot lost, `coop.pool orphans`). With `cell.spare` the pool counts as full at
  1498 used and 1499 is the spare alone (docs/re/cell.md#cell-gfx-pool).
* ✔ A cell's soft body (cell +0x248 object, +0x24C tier, +0x250 count, +0x254 indices):
  CreateSoftBody 0xE6D8F0 picks the tier from size +0x58 (>=0.3: 1, >=1: 2, >=2: 3) and takes one
  particle per node of the quadtree 0x16AE800 at that depth (4 / 16 / 64); tier 0, another query or
  model type 0 / 5 is the "no particles" form (+0x250 = 1, +0x254 = -1). The re-tier 0xE51A20
  (from 0xE5A1A0, when a cell grows or shrinks) frees the old particles and takes the new tier's.
  FreeSoftBody 0xE66010 (DestroyCell) frees every entry when +0x250 > 1.
* ✔ A full pool's -1 is kept in the list. The game then reads and writes particle -1 (8 bytes
  before the block: the heap header; a cell's centre computed from it jumped 4000-8400 m) and frees
  it, which makes -1 the free-list head; the next allocation reads `next[-1]` (another array's
  last value) as the head and the one after faults: **0x00E4DBD9** (ParticleAlloc+0x39, the host in
  cell_sap, 1332 faults) and **0x00E4E384** (the advect query 0xE4E320, the guest in cell_sap and in
  coop_cell_particles with the fix off: 360 faults, own cell 8385 m away). The re-tier from a cell
  without particles frees its -1 too (the count went to -25 once).
* How full the pool gets: ordinary 2-player play at level 4 (food 300) peaked at 391 on the host;
  `coop.pool column 1100` (1100 cells next to the host's cell, cell_sap) filled it (1480-1499 right
  after). Inferred: real play needs some hundreds of cells crowded near the camera to reach it, not
  seen outside these tests.
* The fix `cell.particles` (cellsync.cpp, hooks on ParticleAlloc / ParticleFree): an allocation the
  game would refuse gets the spare slot 1499 (set to the asking cell's position, at rest; shared by
  all such cells), and a free of 1499 or of anything outside the pool does nothing. `coop.pool sap`
  prints `particles=` (the game's count), `peak=`, `spare=`, `bad_frees=`; `coop.pool particles <n>`
  (tests) fills the pool to n used with real allocations.

## The "ghost" after the own cell's rebuild: our avatar taken for the partner's puppet (2026-10-31, fix/w25-reports)
<a id="cell-ghost-puppet-address"></a>

BUG-0342. GOG 2024, lanes 5/6, `tools/tests/coop_cell_own_load.py` (34 + 18 runs) with
`coop.pool collide` / `sapme` / `own_pairs`.

* ✔ Not the cause (the failing runs had the same as the good ones): the cell model data (16 parts,
  radius 1.259), the broadphase (object, endpoints, bounds, pair list 60-120 of 4096), scale +0x58,
  matrix +0x5C, flags +0x48, the level. The broadphase held 1-3 pairs of our cell with the plants it
  touched, both cells live in the pool (GetIfNotDeleted) and Collisions' sphere test (0xE6D790:
  model data centre +0x1238 and radius +0x1244 times +0x58, turned by +0x5C when +0x48 bit 2) passed,
  yet HandleCollisionPair (hook h_Pair) saw no pair of our cell (`own_pairs` stayed 0; good runs: 15-70
  per plant).
* ✔ The cause: h_Pair drops every pair with a partner's puppet (`modes::cell_is_puppet`), and that
  asked only a map keyed by the cell's address. RebuildLevel 0xE7FD00 frees every cell and makes the
  avatar first, often at the address the puppet had; the puppet is then made again at another
  address, and the old entry is never looked at again (the remote's alive check asks for its new
  puppet). With the old check: `puppet=1` for our own cell in both ghost runs of 12 (lab of the
  branch); a game whose avatar is "a puppet" does not eat, bite or get hit until the next rebuild.
* Fix `cell.puppet_index` (modes.cpp): a puppet is the cell at the address AND its pool index
  (index + generation, `cell_at`); entries whose index no longer leads to their address are dropped
  when a puppet is made. With it: see BUG-0342's Verification.

## The cell graphics pool (256) loses soft-body particles when full (2026-10-31, fix/w26-cell-spare)
<a id="cell-gfx-pool"></a>

BUG-0355. GOG 2024, Ghidra and `tools/tests/coop_cell_spare.py` (suite `cell_spare`), lanes 5/6/8,
`coop.pool parts` / `coop.pool orphans` / `coop.sample`.

* ✔ Soft-body LOD: every frame UpdateCells 0xE7C8C0 calls Simulator::Cell::SoftBodyLod 0xE78C00 per
  cell: 0xE5CDE0 asks whether a foreground cell's sphere (pos +0x4C, 1.7 x size +0x58) touches the
  active area Simulator::Cell::ActiveBox 0x016B3C88 (min xyz, max xyz around the camera; 20 x 15 m
  at level 4, it also spans the 20 x 15 bucket grid of the advect step 0xE4E8B0); outside it frees the
  soft body (FreeSoftBody 0xE66010), inside it makes a missing one (+0x248 == 0: CreateSoftBody
  0xE6D8F0). So only cells near the camera hold particles; the rest of the 900 mirrors have none.
  Negative: the per-frame tier check 0xE5A1A0 skips cells of the background query (+0x35C ==
  G+0x4100); a mirror's size changed by us does not re-tier a cell that has no +0x248.
* ✔ CreateSoftBody allocates the particles (tree walk 0xE5CB60, call site 0xE5CC88) and writes the
  count +0x250 first, then takes the graphics object (+0x248) from the cObjectPool at
  `*Simulator::Cell::cCellGFX::_ptr + 0x168` (live +0xC, capacity +0x18 = **256**).
  cObjectPool_::CreateObject 0xB72270 returns 0 when live == capacity; the 0 is kept, the object
  set up is cObjectPool_::Get(0). DestroyCell 0xE780A0 calls FreeSoftBody only when +0x248 != 0, and
  the next frame's LOD sees +0x248 == 0 and calls CreateSoftBody again over the old list. Every cell
  in the active area beyond the 256th loses its 4-64 particles every frame.
* ✔ Runtime (2 players, `coop.pool crowd 1000 8` on the host, fix off): within 12 s the pool was full
  in both games; 611-1491 used slots (guest) and 195-1491 (host) held by no cell, all allocated at
  0xE5CC88, 1195-1391 of their owners gone; 67 DestroyCell of cells with 616 particles gave back 40
  (63 of them without +0x248). Then BUG-0338's spare slot is handed to every cell asking: its
  position was NaN in every such run within seconds (each holder's soft-body step pulls it, up to 64
  times per cell), the NaN spreads to every holder (the centre is the mean of the particles;
  guest_tick moves a mirror's particles by the NaN difference): 26-31 NaN cells; the guest's own cell
  grew in the end-of-Cell cinematic with 56-60 of its 64 particles the spare and went non-finite
  (BUG-0292 / BUG-0307 lines every frame).
* ✔ With `cell.gfx_full` (no CreateSoftBody while the graphics pool is full): gfx peak 256, 891
  calls refused in 12 s, 20 DestroyCell with 164 particles gave back 164, no lost slot.
* ~ Inferred (no graphics pool numbers in that log): ordinary 4-player play reaches it, the
  campaign guest's 900 mirrors crowding its screen; its particle pool was full at 18:02:47 (lab 20261031-151552 campaign_short#1 extra1), the other three games and
  two further fix-off campaign_short runs (lab 20261031-174551 of the branch) did not get there (1 of 5 campaign_short runs so far).
* ~ Inferred, not reproduced: the campaign guest's 3 fps for good (frames of 300-400 ms) and its
  address space falling 3526 -> 1899 MB in two minutes. A 2-player crowd with the fix off kept
  60+ fps (`coop.sample`: no hot spot). Likely the per-frame CreateSoftBody calls of many cells on a
  full graphics pool (each sets up Get(0)'s object again and plays an animation) once the
  cinematic's zoom put more cells in the active area, and the own cell flying around with the NaN
  spare. Both are gone with the fix.
* A full particle pool can still come legitimately (256 cells x 16 particles at tier 2 is 4096):
  `cell.spare` frees the soft bodies of the cells farthest from our own (not ours, no puppet, not the
  asking cell) down to 1300 used and hands out a real slot; such a cell keeps its tier and becomes
  the game's own "no particles" form (+0x250 = 1, +0x254 = -1, as a model type 0 / 5 cell), moved and
  drawn without a soft body until the LOD or a re-tier gives it particles again. A cell left with
  the spare is freed the same way once a frame; the spare is put back to a finite place.
* Follow-up (2026-10-31, after the merge 55da78b): cell_particles (the pool filled with particles no
  cell holds, `coop.pool particles 1499`, then the mirrors made again 10-16 times) showed steps of
  the guest's own cell of 13-18 m in one round on main (2 of 5 runs). Not introduced by BUG-0355: with
  `cell.spare`, `cell.gfx_full` (and `cell.bare_still`) all off it stepped in 6 of 12 runs, 64 m to
  1e7 m in 4 of them (the shared NaN spare); with them on 2-4 of 12 at 5-16 m; without a full pool at
  all (`--no-fill`) 1 of 8 (18.5 m: the own cell was knocked out by the crowd and respawned, its
  soft body 4 -> 1). With the own cell invulnerable (`coop.pool invuln 1`) the most in a round was
  4.3 m in 6 runs and 10.8 m in 1 of 6 more (fixes on), 4.8 m in 6 runs (off; one of them drifted 34
  m in all). ✔ observed: a knock-out's respawn moves the passive own cell; ~ inferred: bites and the
  mirrors made again on top of it shove it the rest.
* `cell.bare_still` (with this follow-up): a mirror whose soft body `cell.spare` freed is placed at
  the host's pose at rest every frame (as a soft mirror is), not steered by velocity like a tier-0
  mirror. ~ Not shown to matter: steered 4 of 12 runs stepped over 5 m, at rest 2 of 12 (one a
  knock-out); kept because a bare mirror steered into ours is the one way left for the fix to push it.
  Tried and dropped: keeping the soft bodies of cells within 8 m of ours (they then share the spare
  again): 3 of 12 stepped, no better.
* `cell.gfx_full`, corrected (2026-10-31, follow-up): a cell CreateCellObject makes has count +0x250
  = 0 (the object pool's memset) until its first CreateSoftBody; refused, it kept 0 for good. In
  cell_sap (1100 host cells in a column, the host's graphics pool full in every run) 3 of 5 runs
  failed after the merge: first-interval jumps of the host's cell of 62 and 73 m (limit 60) and
  once a host cell with a pose that was not finite and the host's own cell at (-9934064 -5482405).
  Now a refused cell gets the game's "no particles" form (count 1, +0x254 = -1, `count0=` in
  `coop.pool parts`): 4/4 passed (first intervals 4-37 m); with `cell.gfx_full` off 3/4 (46 m once,
  one failure on a guest's view). ~ Inferred: some game code divides by or walks the count (the
  NaN cell); the 62-73 m first intervals are the column pushing itself apart (up to 46 m with the
  guard off too), not this.

## A negative "spent DNA" after the cell editor (2026-11-01, fix/w30-progress)
<a id="cell-editor-negative-spent"></a>

* ✔ (player logs 0.7.3.3, BUG-0351) `S+0x2C` (DNA spent) can be negative after the game's cell editor:
  the host's first editor (budget 20, the starting carnivore cell E3B1B67B) came back with spent=-40
  (budget 60 on the next request), then -19 and -20. The mod writes neither field
  (#cell-editor-dna). ~ Inferred: the editor charges the difference of the cells' worth, so removing the
  starting cell's parts pays DNA back (vanilla sell-back; not checked in a solo game).

## A knocked-out cell still ate what touched it (2026-11-01, fix/w31-cell-gate)
<a id="cell-ko-eat"></a>

* ✔ observed (BUG-0364, lab 20261101-023131 cell_coop; reproduced by cell_ko_food with
  `coop.fix cell.ko_no_eat off`, lab 20261101-093513 and 20261101-094736): the knockout
  (`go_down` / `downed_tick`: hp held at 1, invulnerable, `stop_at` its spot every frame) does not
  stop the game's contact events. EatFood 0xE7DF40 (event 0x12) still ran for the avatar: OnEat ->
  AvatarEat -> AddFoodPoints, +12 per plant, shared with the partner, the plant shrank or was
  destroyed. The respawn's "small again" body follows the food total (regrow, +0.01 per point), so
  the cell came back bigger (cell_coop target 0.41 instead of 0.28).
* Fix `cell.ko_no_eat`: EatFood is hooked (GOG 2024 symbol; the GOG 2016 file has none, the hook is
  then skipped); while our cell is knocked out a call with our avatar as the eater returns at once
  (the food stays where it is), and our mouth's chunk bites (events 0x13 / 0x14, `chunk`) are left
  out too. `coop.pool` counts them as `ko_eats_blocked=`. A partner's cell and the NPCs eat as before.
* ~ inferred, not seen: the knocked-out cell's own soft body still collides and pushes; only eating
  is stopped.

## The mating-call button and the hatch after a load (2026-11-01, fix/w34-cell-mate)
<a id="cell-mate-button"></a>

* ✔ (Ghidra GOG 2024, BUG-0411) Two callers reach the mating call 0xE760F0
  (docs/re/cell.md, section 12): the HUD's message handler **0xE78E70** (registered in data at
  0x01485338; window message id `+8 == 0x287259F6`, command `+0xC == 0x44837C0`) calls it with no
  condition, and **0xE76320** (from 0xE7F630, probably the key binding) only when
  `*(DAT_016B3C0C + 0x936) == 0`, `G+0x51CC <= 0` and `S+0x69 != 0`. 0xE760F0 creates the mate only
  when `G+0x51D4` holds no live cell; `G+0x51D4 == 0` (`coop.pool mate` prints `mate=00000000`) means
  the call never ran in this Cell game (a load starts a new one with 0).
* ✔ runtime (lane 6, 2026-11-01): a Cell world loaded from the galaxy menu often starts with the
  cell's hatch, the game's input lock on (`cGameInputManager +0x110`, `coop.uistate input_lock=1`)
  and a cinematic playing (`coop.pool cine=1`) for about 5-10 s; a click on the heart button then
  does nothing (no mate, `G+0x51D4` stays 0) and the press is not queued. Some loads show no hatch
  at all (input_lock=0 at once): which ones is not known.
* ✔ runtime: after the host's cell editor the host shares its world again (saved 3 s after the
  stage settled); the guest leaves the Cell stage about 4-5 s after it got the host's new cell
  record and is back in Cell another ~5 s later. A mate the guest called before that load is gone.

## The Cell parts a player collects, and the party's shared set (2026-11-03, fix/w40-editor)
<a id="cell-parts-shared"></a>

BUG-0467. GOG 2024, objdump of `SporeApp.exe` (dc04aee5).

* ✔ (disassembly) The Cell stage's collected parts are **13 ints** at cCellSerializableData (`S`,
  cCellGame+0x5190) **+0x34 + 4*part**, not bytes: 0 locked, 1 unlocked, 2 collected and not seen in
  the editor yet. 0xE4FB90 turns every 2 into 1 (the editor's return); 0xE4FAA0(int* states) builds
  the cCollectableItems of the cell editor (`G+0x5164`) from them: state 1 -> UnlockPart, 0 ->
  LockPart, then state 2 -> UnlockPart, over the table **0x1483DD8** ({part index, item instance} x 12,
  type 0x150D6D8).
  CORRECTION (2026-11-03, fix/w40-editor): `coop.pool`'s `parts=` read 13 bytes at `S+0x34` (only the
  first 3-4 parts' low bytes); it reads the 13 ints now.
* ✔ (disassembly) **0xE5D7B0 `Simulator::Cell::CollectPart`**, cdecl void(int part), plain ret: a part
  token collected (from the collision code 0xE7B630; a debug key 0xE81120). The first time
  (`S+0x69 == 0`) timers `G+0x51D0` / `G+0x51CC` and a callout; then the parts mission (`S+0x90`
  1 -> 3, counter `S+0x94` up to 6), `S+0x69 = S+0x68 = 1` (the mating-call button), `state[part] = 2`,
  the part's item key from the table, 0xE83430 (the part pop-up), 0xE4FAA0 over `S+0x34` (the
  palette's items rebuilt). It runs in the collecting player's game only: a part a guest picked up
  never reached the host (BUG-0467) and the other way round.
* The co-op rule (as BUG-0401 for the creature parts): the party has one set, the union, held by the
  host's game. `parts.shared_cell` (parts.cpp, MSG_PARTS kinds 3 and 4, keys `{part, 0}`): every game
  polls the 13 states in a live Cell game (1 Hz); a guest that plays the host's world sends the parts
  it has and the host lacks (kind 4, resent every 3 s, at most 5 times a part), the host collects them
  through CollectPart (outside the frame: it shows the pop-up) and sends its set (kind 3, at once on a
  change, else every 10 s); a guest collects what it lacks of the host's set the same way. The notice
  "Bob found a new cell part: it is in your editor too". Nothing is locked. Older SporeCoop ignores
  the new kinds. `coop.parts cell [collect <i>]` prints the states (tests: the game's own collect).
* ~ inferred, not seen: the cell editor opened while a part arrives keeps its palette until the next
  editor (0xE4FAA0 builds it from the states at the collect and the editor request).
## A mirror of a creation this game does not have (2026-11-03, fix/w40-crashes)
<a id="cell-unknown-model"></a>

BUG-0455 (a guest's crash at 0xE83EC8, read 0x9C, "when the host left the evolution editor"), the
signature of BUG-0004 on another path.

* ✔ The player's dump: CreateCellObject+0x4B9 (0xE74ED9) <- coop.dll `cellsync::guest_state`
  (cellsync.cpp, `modes::cell_create` with the model key the host sent) -> 0xE6CAF7 -> 0xE69726 ->
  0xE67A41 (GetCellModelData 0xE679E0: the model data of a key, built on first use) -> 0xE83E30, which
  calls `Editors::cEditor::LoadCreatureData` (0x4BB500) and reads `[result+0x9C]` at **0xE83EC8**: ecx
  = 0, the creation's file was not found. Symbolized with a v0.7.3.4 build (`git archive v0.7.3.4`).
* ~ After the host's Cell editor its cells of the player's species carry the new creation's key; the
  guest mirrored them before the creation's records reached it (the creation bundle comes later, docs/re
  /creations.md 8). Lab: a host cell sent with an unknown model instance (`coop.cell spawn <slot> dx dy
  level <instance>`, cellsync::set_fake_model) faulted the guest at 0xE83EC8 with the players' chain
  (0xE67A41 0xE69726 0xE6CAF7 0xE4F9E4 0xE74ED9) and the guest made no mirror after it.
* Fix (`cell.unknown_model`, mods/coop/modes.cpp): every forced model key is checked with
  `creations::key_known` (RM FindRecord) first; `cell_create` makes no cell of an unknown creation
  (cellsync tries again with the next state round; `unknown_models=` in coop.status), `cell_spawn`
  (puppets) falls back to the own avatar's model.

## Does a guest's mirror die when the host's cell is gone? (2026-11-03, fix/w40-cell-evo)
<a id="cell-stale-mirrors"></a>

BUG-0480. Lane 4, `tools/tests/coop_cell_gone.py` (docs/06-testing.md#testing-world-lod).

* ✔ (runtime) The purge of guest_state: a mirror not listed in a complete round of the host's MSG_CELL_STATE
  goes at the round's last chunk; after a lost chunk one not listed for 1 s. A 4 s dead link
  (`coop.pool statehold 4`) while the host destroyed 8 cells near the guest left no mirror of them 2.5 s after
  the state came back (6 of 6 rounds).
* ✔ (runtime) The detector's `Cell:guest_only:cell` / `host_only:cell` came mostly from its single host dump:
  Cell cells live for seconds and a host level change replaces hundreds between two dumps. Bracketing the guest's
  dump by two host dumps 1.5 s apart: 0 where one dump gave 2 (10 rounds).
* ? (runtime, 3 of 9 cold runs) 28-49 mirrors at once (meat / rock / bubble, level 0-1) of host indices the host
  had in neither dump and not in `coop.pool cell` (cell_at) a moment later, though the guest's entry had been
  refreshed 0.1-0.2 s before; the host never sent an index cell_at did not find at the send. Inferred: cells that
  exist in the host's pool during part of its frame only (the partner tiles populated for the guest's view);
  not verified.
## A knocked-out cell's attacks (2026-11-03, fix/w40-guest-combat)
<a id="cell-ko-attacks"></a>

* ✔ The knockout (cellsync go_down / downed_tick: hp 1, dead/dying 0, invulnerable +0x111, translucent, held in
  place) did not stop the avatar's attacks: the game's Bite 0xE7B0A0 for the avatar still ran and killed (lab
  lane 2, `coop.pool realbite` while downed: a guest's creature 6 -> 3 -> 0 on the host through MSG_CELL_HIT; the
  host's own bites the same in its game). Only the mouth's chunks and food were blocked (cell.ko_no_eat, BUG-0364).
* `cell.ko_no_bite` (BUG-0509): while downed, h_Bite (the avatar as attacker) returns false, h_DamageCell drops a
  hit whose attacker is the avatar (spikes, poison) and h_Break drops a lethal one; the host refuses MSG_CELL_HIT
  from a guest whose last MSG_CELL_KO state is KO_OUT (an old guest, a hit in flight).
* ? "got damaged" while knocked out (the report) is not looked at: the cell is invulnerable and its hp forced to 1
  every frame; inferred: the health ring moving.

## The world scale of a level change on two games (2026-11-03, fix/w40-misc)
<a id="cell-world-scale"></a>

* ✔ `WorldScaleStep` 0xE73E50: while the world scale `G+0x514C` differs from its target `G+0x5150` it moves
  it (log-space smoothing 0xE509F0, dt * 0.75) and calls 0xE6DB70 with ratio = new / old. 0xE6DB70 divides by
  the ratio, about a centre `C`: every soft-body particle, every cell's `+0x4C` pos, `+0x08` target, `+0x58`
  size, `+0x90` velocity, `+0xB4` target size (and mass), `+0x1C4`, the camera points (0x16B3C0C +0x48/+0x54/
  +0x60) and `G+4`. `C` is 0x16B3C28 (read (0 0 0) at runtime) while the camera point is within 1000 m of the
  origin, else 0xE4F900 picks one (not looked at). So in the usual case `pos * G+0x514C` is invariant.
* ✔ Runtime (lab, cell fixture, lanes 9): world scale 0.5 at level 1, 1.5 at level 2, 5.0 at level 3; one change
  takes ~3.5 s. The guest's change starts 1-4 frames after the host's (its level follows the shared food total,
  MSG_CELL_FOOD), so for ~3 s the two scales differ (host 0.964 / guest 0.702 at one sample).
* ✔ BUG-0511: the guest applied the host's cell poses (MSG_CELL_STATE, in the host's scale) unchanged: the
  mirrors were 14-18 m (p90, guest metres) from where the host's cells were during each level change and glided
  back over seconds ("cells float around very fast"). Fix `cell.world_scale` (cellsync.cpp): the host appends its
  world scale to each state chunk (after the cells: older guests stop reading before it); the guest keeps each
  mirror's pose in the host's scale and draws it times host scale / own scale every frame (size at receive).
  p90 0.1 m during the change with it (`cell_grow`, docs/06-testing.md#testing-cell-grow).
* ? Not done: the players' own cells (puppets, MSG_STATE through `modes::cell_drive`) carry no scale; a partner's
  puppet may be off in the same way for those ~3 s (BUG-0519).
