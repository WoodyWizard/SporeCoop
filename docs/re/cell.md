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
