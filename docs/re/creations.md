# Player creations: storage, runtime loading, avatar swap (GOG 2016 build, sha256 922af7c7…)

Goal: every player keeps their own cell / creature / spaceship, everybody sees it, and it plays
with its own parts. Today puppets are built from the *local* player's model (modes.cpp:
`creature_spawn` uses the local avatar species, `cell_spawn` the local `+0xFC` key, `space_spawn`
the local `cEmpire::mUFOKey`), and the guest's own avatar is whatever the host's save contains.

Static analysis only (llvm-objdump of `SporebinEP1/SporeApp.exe`, Ghidra via REA for a few
functions, a DBPF reader over the test profiles and the game packages). MSVC x86 conventions:
`thiscall` = `this` in ECX, callee pops `ret N`. Legend: **✔** verified in this build's
disassembly (prologue/`ret N`/call sites), **~** likely (behaviour inferred from code, not traced
end to end or not run), **?** unknown / hypothesis, **[ModAPI]** = from memory of Spore-ModAPI,
addresses differ.

ResourceKey in memory = `{u32 instance, u32 type, u32 group}` (✔, as in cell.md / FindRecord
0x8DFC70 reads `key+4` as the type). In save files (serialized nouns) a key is written
**big-endian as group, type, instance** (✔, seen in every .spo / stars.db record).

Scratch tools used (not part of the repo): `$CLAUDE_JOB_DIR/tmp/creations/dbpf.py` (DBPF 3.0 +
DBBF reader with RefPack), `keys.py` (finds serialized creation keys), `meta.py` (pollen metadata).

## 1. How creations are stored

### 1.1 Types and groups (✔ from the game packages; type ids are FNV-1 hashes of the extension)

| creation | resource type | group | example (Spore_Game / Spore_Content) |
|---|---|---|---|
| creature | `crt` 0x2B978C46 | 0x40626200 | 47 XML `.crt` in Spore_Game, ~760 `bem` in Spore_Content |
| cell | `cll` 0x3D97A8E4 | 0x40616200 (`*(u32*)0x15A8D0C`, used by GetModelKeyForCellResource) | premade player cell 0xE219E5D3 |
| building | `bld` 0x2399BE55 | 0x40636200 | |
| vehicle | `vcl` 0x24682294 | 0x40646200 | |
| UFO | `ufo` 0x476A98C7 | 0x40656200 | premade 0x3A948398, 0x0665F6CE |
| flora | `flr` 0x438F6347 | 0x40666200 | |
| (adventure assets?) | - | 0x406B6200 | bem only, not researched |

Record types that hold a creation:
- `bem` 0x1A99B06B - binary editor model (magic `D3 7E 65 48`), the normal storage form. 2.6–10 KB
  for the test creatures. **No self-reference to its own key inside** (✔ searched LE/BE) ⇒ a
  receiver may store it under a different instance id.
- `crt`/`cll`/`ufo`… as XML `<sporemodel>` (formatversion 18) - Maxis data in Spore_Game and
  creations *imported from PNG* (Pollination, tagged `tag:spore.com,2006:ImportedContent`).
- `creaturedata` 0x0F43029A - derived species data (abilities/stats cache). Read/written by
  `cCreatureDataResource` (Read 0x4BF920, Write 0x4BFC60, ported). `cEditor::LoadCreatureData`
  0x4BB4B0 (~) looks it up by `{instance, 0x0F43029A, group}` and **computes a new one when it is
  missing** (alloc 0x128 + 0x46AB00) ⇒ optional for a transfer.
- `png` 0x2F7D0004 in the model group (Sporepedia card 128×128, model data steganographic in the
  pixels, no extra chunks) and in group+1 (0x40626201, plain thumbnail ~). Not needed to play.
- `pollen_metadata` 0x030BDEE3 (big-endian: version 13, own key {type, group, instance} at +0x0C,
  parent key, timestamps, UTF-16BE author (the profile user name) and creation name). Useful for the UI.
- prop 0x00B1B104 in group 0x4062E500 (per-creation prop, not decoded).
- Baked meshes/textures of Maxis creations: 0x00E6BCE5 in groups +2/+3, 0x41626202/03,
  0x40627100/01 and 0x2F4E681C in 0x40622900/01. Baked data of player creations lives in
  `GraphicsCache.package` (607 MB in the test profile) ⇒ the game bakes on demand (~).

The resource factory for creations (✔ 0x4BC700 = GetSupportedTypes, 0x4BC780 = IsValid,
factory id 0x4FFF05D3 at 0x4BC6F0): supports crt, bld, vcl, ufo, flr, cll, creaturedata, bem, and
**a `bem` record may produce a crt/bld/vcl/ufo/flr/cll resource**. The resource manager maps a
requested type to record types through its type map (`cResourceManager+0x54`, used by FindRecord
0x8DFC70; SetTypeMapping 0x8E1BF0, GetTypeMapping 0x8DFC30, ported) ⇒ a key `{crt, 0x40626200, X}`
resolves to a `{bem, 0x40626200, X}` record in any registered database (~ - this is exactly how
EditorSaves.package, which contains only bem, serves crt keys).

### 1.2 Profile files (test profile `SporeKit-test/Spore`, ✔ listed with the reader)

| file | format | content |
|---|---|---|
| `EditorSaves.package` | DBPF 3.0 | **My Creations**: per creation `bem` + `png` (0x40626200 and 0x40626201) + `pollen_metadata` + prop (0x4062E500). Test profile: 7 creatures 0x29020CC4…0x290229B9 (Chitis, Ms Spoffit, Ciminerdo, Kirgle, Gorkbuddy, Judge, Tiger Horse); no cells/UFOs saved. |
| `Pollination.package` | **DBBF** (64-bit DBPF, index offset u64 @0x38, 32-byte entries with u64 offset) | Sporepedia cache: 2600 summaries `0x02D5C9AF` (groups 0x4061…0x4066 6200, 0x408A0000), `creaturedata` for the player's creatures, imported PNG creations as XML crt (+png, metadata). |
| `GraphicsCache.package` | DBBF | baked meshes/textures of creations |
| `Planets.package`, `RigblockInfo.package` | DBPF | planets / rigblock cache (not researched) |
| `Games/GameN/*.spo` | DBPF | one per planet; records keyed by **noun id** (type), group 0, instance 0x0179D304 (+ 0x01897C18 self-keyed record). Contains only *keys* of creations, never their bodies. |
| `Games/GameN/stars.db`, `planetRecords.pkp/.pkt`, `PlanetScripts.pld`, `GGEUserData.dat` | DBPF | galaxy (star records 0x03E4353C carry empire species keys; empire records 0x018EB9D2 carry bld/vcl designs), planet records, scripts, galaxy-game-entry user data (planet name) |

Records inside a .spo that reference the player's creations (✔ by scanning, names ~):
- **`cPlayer`** noun **0x02C21781** (GetNounID 0xC755D0 returns it ✔; instance kept at
  `cGameNounManager+0x74`, created lazily by 0xB20ED0 ✔). Holds the player's current designs:
  creature fixture → `crt 065CBE70` (the avatar species); space fixture → `bld 067C701A…`,
  `vcl 110771ED/A7278589/B40E95C5`, `ufo BDFB7DC5` + lists of encountered creations. Field
  offsets not decoded (?).
- `cCreatureAnimal` 0x018EB45E (species key per creature, mSpeciesKey +0xB28 in memory),
  herds 0x01BE418E (species per herd), citizens 0x018EB4B7, UFO 0x018EBADC (`mDesiredModelKey`
  +0x780 → `ufo BDFB7DC5` = the player ship).
- Cell stage record **0xABCDEF00** (+ 0x01897C18): `cll 40616200 E219E5D3` = the player cell
  (= cCellSerializableData+0x10, §2.2).

The fixtures were started from Maxis premades (0x065CBE70 creature, 0xE219E5D3 cell,
0x3A948398 UFO, all in Spore_Content) - those keys resolve on every machine. A real player's own
creations are 0x29xxxxxx keys from **their** EditorSaves.package and are unknown elsewhere.

**Instance collisions:** creation instance ids are allocated per profile and sequentially
(both test profiles contain the same 0x2902xxxx set with *different* history possible). Two
players can own different creations with the same key ⇒ a receiver must remap the instance
(e.g. `0x7C000000 | hash(peer, instance) & 0x00FFFFFF`; bem has no self reference ✔).
The same holds for the host's creations that populate its planet: the guest's game, its
`creaturedata` in Pollination.package and its baked models answer for the key with the guest's own
creation, also when the host's records are registered above My Creations (docs/re/creature.md 15).

### 1.3 Where the game opens My Creations (✔ 0x6B3930…0x6B39DE)

`0x6B33C0(dirId 0xA02151, L"EditorSaves", Database** out, saveArea 0x11AC199, bool)` → builds
`<AppData>/EditorSaves.package` and calls the package opener **0x6B2FE0** (§3), registered with
**priority 1000**; then `RegisterSaveArea` 0x6B36F0 maps save-area ids 0x11AC199, 0x11AC19A,
0x11AC19B, 0x90368EA3, 0x90368EA2, 0x90368EA0, 0x86CA01C9 to that database. If global
`0x16056E0 == 0` a directory database "EditorSaves" (folder) is used instead (~).
⇒ `Resource::GetDatabaseByID(0x11AC199)` (0x6B1F20, cdecl, already in overrides) should return the
EditorSaves database (~). `Database::GetKeyList` (vt+0x30, DBPF 0x8D8A20, `ret 8`:
`(vector<ResourceKey>& dst, IResourceFilter* filter)` ✔ ret) lists its keys.
The editor saves a creation with `cResourceManager::WriteResource` (vt+0x20, 0x8DED90) for the
`bem` key, then the `creaturedata` key, each followed by vt+0x6C (CacheResource) (✔ 0x577722…
0x5777E9 inside Editor_Save 0x577650).

## 2. Runtime: key → species profile / cell model

### 2.1 Species profile (creature, tribe, civ citizens)

| addr | what | conv |
|---|---|---|
| 0x401090 | `cSpeciesManager::Get()` | ✔ cdecl, returns `*(void**)0x15D1C24` |
| **0x4DF580** | `cSpeciesProfile* GetSpeciesProfile(const ResourceKey*)` | ✔ thiscall(mgr) `ret 4` |
| 0x4DF700 | map lookup (mgr+4 = key→profile map) | ✔ thiscall |
| 0x4DA3D0 | `bool KeyExists(const ResourceKey*)` = `RM->GetResource(key,0,0,0,0,0)` (vt+0xC) | ✔ cdecl |
| 0x4D3E00 | `cSpeciesProfile::ctor(this, const ResourceKey*, 0)` (object 0xA18 bytes, alloc 0xF47240) | ✔ call site |
| 0x4DF450 | `GetDefaultSpeciesProfile()` = `GetSpeciesProfile(mgr+0xA4)` | ✔ thiscall(mgr), plain `ret` |
| 0x4DF410 | thiscall(mgr, profile) - called after SetSpeciesProfile for avatar-flagged creatures | ? |

GetSpeciesProfile ✔: key.instance==0 → default key `mgr+0xA4`; cached? → return; else
**`KeyExists(key)` through the resource manager; false → return null *without caching*** ;
true → `new cSpeciesProfile(key)`, insert, return. ⇒ A profile for a remote creation appears as
soon as its record is reachable through any registered database; earlier failed lookups do not
poison it. (Abilities come from the profile; `creaturedata` is computed if absent, §1.1.)

On a creature: `cCreatureBase+0xB20 mpSpeciesProfile`, `+0xB24` profile version (`profile+0x53C`),
`+0xB28..+0xB30 mSpeciesKey` (= `profile+0x504`) (✔ SetSpeciesProfile below).

### 2.2 Cell model key (✔ 0xE65650)

`ResourceKey* __cdecl GetModelKeyForCellResource(ResourceKey* out, cellRef)` (hidden out first,
plain `ret`): walks the .cell resource's model entries (`res+0x14`, count `+0x18`, 0x28 bytes
each, kind at +4):
- kind 3 → `{entry+0x10, cll 0x3D97A8E4, *(u32*)0x15A8D0C = 0x40616200}` (fixed cell model);
- kind 4 → random creature model (0xE655D0);
- **kind 5 → the player's cell: `S+0x10..+0x18`** where `S = *(u8**)(*(u8**)0x16B4BE4 + 0x5190)`
  (= cCellSerializableData) - `{instance, type, group}`;
- none → `{0,0,0}` (globals 0x16B4DB4..BC).
The cell's current model is then `cCellObjectData+0xFC` (cell.md). For the avatar and for
puppets built from the avatar's resource, kind 5 applies ⇒ `S+0x10` is "the player's cell".

## 3. Registering an extra package at runtime (✔)

The game's own opener for save-area packages, **0x6B2FE0** (✔ 0x6B307C…0x6B311F), does:

```
db = operator_new(0x388, "name", 0,0,0,0)          // 0x9262B0 cdecl (6 args, add esp,0x18)
DatabasePackedFile::ctor(db, const char16_t* path, ICoreAllocator* 0)   // 0x8DA290 thiscall ret 8
(db+4)->vt[0]()                                     // AddRef (0x8DD4B0, refcount at db+8)
0x8D8840(db, 0x12, float)                           // thiscall ret 8: hole/compaction params (+0x334/+0x338), optional
0x8D8870(db, 0)                                     // thiscall ret 4: byte +0x2E0, optional
db->vt[1]()                                         // Initialize 0x6BBDC0 (sets byte +0xC; Open fails without it)
db->vt[6](AccessFlags 3, CD 6, false)               // Open 0x8D8C90 thiscall ret 0xC
RM = *(void**)0x15FE88C  (getter 0x67DC90)
RM->vt[0x50/4](true, db, 1000)                      // RegisterDatabase 0x8E0FF0 thiscall ret 0xC
```

Details:
- **DatabasePackedFile** object 0x388 bytes; vtable 0x1437290 at +0, secondary (refcount) vtable
  0x143727C at +4 (AddRef 0x8DD4B0, Release 0x69D3C0 → on 0 calls vt[0](1) = scalar deleting dtor
  0x8D9CA0 which frees with 0x9262F0). Ctor: non-null path → SetLocation(path) ✔.
- Primary vtable (✔ addresses = ported names): +0x00 dtor 0x8D9CA0, +0x04 Initialize 0x6BBDC0,
  +0x08 Shutdown 0x8D8960, +0x0C GetDatabaseType, +0x10 GetRefCount, +0x14 Lock, **+0x18 Open**,
  +0x1C Close 0x8D9CC0, +0x20 GetAccessFlags, +0x24 Flush, +0x28 GetLocation 0xA426C0,
  **+0x2C SetLocation 0x8D9CD0** (`(const char16_t*) ret 4`, fails if already open),
  +0x30 GetKeyList 0x8D8A20, +0x34 OpenRecord 0x8DA6E0 (`ret 0x18`), +0x38 GetOpenCount,
  +0x3C CloseRecord, +0x40 DeleteRecord, +0x44 Attach 0x8D88B0, …
- **Open** ✔ `(AccessFlags acc, CD cd, bool lazy)`: acc 1 = read, 2 = write, 3 = RW; cd 6 =
  default → 3 OpenExisting for read-only, 4 OpenAlways when writing; `lazy=true` only stores the
  flags. Read-only `Open(1, 6, 0)` for a received package. The file stream (0x931900…) opens with
  share mode READ for read access and READ|WRITE when writing (✔) - so the mod can read the
  live EditorSaves.package from disk with `FILE_SHARE_READ|FILE_SHARE_WRITE`.
- **cResourceManager** (vtable 0x14375F8, ✔): +0x00 dtor 0x8E2560, +0x04 Initialize, +0x08 Dispose,
  **+0x0C GetResource 0x8DF6F0** (`(const ResourceKey*, ResourceObject** out, int, Database*,
  IResourceFactory*, const ResourceKey* cacheName)` ret 0x18), +0x10 GetResourceAsync,
  +0x14 GetLoadedResource, +0x18 ReloadResource, +0x1C GetPrivateResource, +0x20 WriteResource,
  +0x24 SetTypeMapping, +0x28 GetRecordTypesFromResourceType, +0x2C GetTypeMapping,
  **+0x30 FindRecord 0x8DFC70** (`Database* (const ResourceKey*, ResourceKey* outRealKey,
  Database* in)` ret 0xC), +0x34 GetResourceKeyList, +0x38/+0x3C GetRecordKeyList,
  +0x40 RegisterChangeNotification, +0x44 RegisterFactory, +0x48 FindFactory, +0x4C GetFactoryList,
  **+0x50 RegisterDatabase 0x8E0FF0** (`bool (bool add, Database*, int priority)` ret 0xC),
  +0x54 IsDatabaseRegistered 0x8DE5D0, **+0x58 FindDatabase 0x8DE610**, +0x5C GetDatabaseList,
  +0x60 DoDatabaseChanged, +0x64 RegisterCache, +0x68 FindCache, +0x6C CacheResource, +0x70
  FlushCache, …, +0x78 GetKeyFromName, +0x7C GetNameFromKey.
- RegisterDatabase(add) ✔: refuses an already registered db, inserts into the priority-sorted list
  `RM+0x2C` (0x8DEC00), AddRef, `db->vt[0x44/4](1, RM, 1)` (Attach). Remove = `(false, db, x)`:
  unlink, Attach(0, RM, 1), Release.
- **Lookup has no global key index** ✔: FindDatabase walks the list and probes
  `db->OpenRecord(key, 0, 1, 6, true, 0)` ⇒ a newly registered package is visible at once.
- The App global `0x15FE88C` (getter 0x67DC90, setter 0x67DE80) is what the game uses for
  RegisterDatabase; `Resource::IResourceManager::Get` 0x8DE4A0 returns `0x1668A80` (~ same object).
- Other DBPF creators (ctor callers, for reference): 0x41C1C0, 0x580DF0 (GGEUserData: wrapper
  0x69FA60 + SetLocation + `Open(1,0,0)`), 0x685730, 0x686D50, 0x6872B0, 0x688F90, 0xB276F3,
  0xB27990 (persistence manager), 0xBB8C60 (ReadStarsFromSaveDB), 0xF25E00, 0xF93BE0.

[ModAPI] does the same: `new Resource::DatabasePackedFile(u"path")`,
`dbpf->Open(IO::AccessFlags::Read, IO::CD::OpenExisting)`, `ResourceManager.RegisterDatabase(true,
dbpf, priority)`; newer ModAPI wraps it in a DatabaseManager - not present in this build.

Alternatives (not recommended): writing records into the guest's EditorSaves db (pollutes My
Creations, RW db is flushed by the game); building `cEditorResource` objects and injecting them
with CacheResource (needs the factory's parse path); importing a Sporepedia PNG (writes an XML
crt into Pollination as "ImportedContent" - import function not located, ?).

**Package to write on the receiver** (DBPF 3.0, uncompressed is fine): header `"DBPF"`, u32 3,
u32 0, …, `+0x24` entry count, `+0x2C` index size, `+0x3C` index minor 3, `+0x40` index offset
(u32). Index: `u32 flags = 4`, `u32 0` (constant instance-high), then per entry `type, group,
instance, offset, size | 0x80000000, usize, u16 compression (0 = none, 0xFFFF = RefPack),
u16 1`. Content per creation: `bem` (required), optional `creaturedata`, `pollen_metadata`, png.

## 4. Swapping a body at runtime

### 4.1 Creature stage (and tribe/civ citizens)

| addr | what | conv |
|---|---|---|
| **0xC22410** | `cCreatureBase::SetSpeciesProfile(cSpeciesProfile*)` | ✔ thiscall `ret 4` |
| **0xC038E0** | `cCreatureAnimal::SetSpecies(cSpeciesProfile*, int age, bool keepState)` → bool | ✔ thiscall `ret 0xC` |
| 0xC09B00 | `cCreatureAnimal::Create(pos*, profile, age, herd, isAvatar, bool)` | ✔ (overrides) |
| 0xC05E60 | cCreatureAnimal Read: `GetSpeciesProfile(+0xB28)` → `SetSpecies(p, 0, 1)` | ✔ |
| 0xC22590 | cCreatureBase Read part: re-resolves `+0xB28`, SetSpeciesProfile if changed | ✔ |
| 0xD5BFA0 | tribe: after the editor every member gets `SetSpeciesProfile(tribeSpecies)` when it differs | ✔ (decompiled) |

SetSpeciesProfile ✔: writes `+0xB20`, `+0xB24 = p+0x53C`, `+0xB28..+0xB30 = p+0x504` (key);
null → default profile 0x4DF450; then (unless global byte 0x168E8E4) 0xC21D10(changed, same)
(model rebuild ~), `vt+0xAC(changed)`, and re-picks ability indices via
`GetAbilityIndexByType` 0xC0C1A0 into `+0xE74`/`+0xE70` ⇒ **abilities follow the new species**.
SetSpecies 0xC038E0 adds the cCreatureAnimal part: health/limits from the profile
(`+0x590/+0x594`), 0xC13FD0/0xC211B0, locomotion setup 0xCEE410, scale (`+0xC0` vt+0x40), flags
`+0xB58`, `+0xB5E` (cleared when keepState=0).

Avatar creation on entering the stage (✔ 0xD44040…0xD44187): a herd is built from the start
species (0xB99020/0xB90A20), `herd+0x84 = 1` (owned by avatar), `Create(nestPos 0xC6AE30(herd),
herd+0xA4 species, age 1, herd, isAvatar 1, 0)`; fallback 0xD444DE uses the default profile.
After the creature editor the planet is re-saved/reloaded and the avatar re-reads its species key
(0xC05E60) (~). The new design keys are stored in cPlayer (§1.2).

To give the local avatar another body (~, mirrors what Read does):
`p = GetSpeciesProfile(&key); if (p) SetSpecies(avatar, p, *(int*)(avatar+0xB34) /*age*/, 1);`
optionally also `herd+0xA4 = p` for the avatar herd (nest respawn / babies use the herd
species ~). Heavier alternative: destroy the avatar and `Create(..., isAvatar=1)` (+SetAvatar
0xB1FC70).

### 4.2 Cell stage

Leaving the cell editor ✔ (handler **0xE809A0**, registered for message 0x30C11C7 at 0xE81B6A):
reads the editor result (0xE82C80), result code 0x1100000, then
`S+0x10..+0x18 = new cell model key`, **0xE624C0(key by value)** (cdecl, 3 dwords: adds the key to
the preload set at `G+0x70`, ~), unlocked parts `S+0x34` refresh 0xE4FBA0, then **RebuildLevel
0xE7FD00** (destroys and recreates every cell, avatar gets the kind-5 model `S+0x10`) and posts
message 0x6579712.

To give the local avatar another cell (~): `S+0x10 = key; 0xE624C0(key)`; then either
RebuildLevel (wipes all mirrors/puppets on this machine - acceptable right after joining, before
mirrors exist) or an in-place swap: write avatar `+0xFC`, free its GFX (0xE66020, `+0x248 = 0`)
so 0xE78C00 recreates it lazily (?, soft-body particles `+0x250` come from the GFX - needs a
runtime test). Remote puppets: already spawned with the forced-model hook on
GetModelKeyForCellResource; just use the remote key (and call 0xE624C0 first).
How cell abilities (mouth/weapons/movement) derive from the model parts was not traced (?).

## 5. Space stage

- Player ship model: `cEmpire::mUFOKey` +0xB8 (known), the live ship `cGameDataUFO+0x780
  mDesiredModelKey` (space.md), cPlayer record holds the same key (§1.2). Type ufo, group
  0x40656200.
- Spawn with a model: `SpawnUFO(type, &politicalId, &modelKey)` 0x102ACC0 cdecl (space.md ✔);
  `CreateUFO` 0x102AC60. Puppets: register the remote package first, then pass the remote key
  instead of the local `mUFOKey`. Changing the local player's ship = the editor flow (not traced, ?);
  setting `mUFOKey` alone does not rebuild the live ship (~).

## 6. Recommended implementation path

1. **Identify own creations** (each machine, before/independent of the host world):
   creature = local avatar `+0xB28` in the player's own game, cell = `S+0x10`, UFO =
   `cEmpire+0xB8`; or let the player pick from My Creations (`GetDatabaseByID(0x11AC199)` →
   GetKeyList, group 0x40626200 / 0x40616200 / 0x40656200, names from pollen_metadata). Keys
   whose record is in Spore_Content/Spore_Game (Maxis) need no transfer - test with
   `RM->FindRecord(&key, &real, 0)` and the database's location (EditorSaves/Pollination ⇒ send).
2. **Extract** the records: `bem` (+`creaturedata`, metadata, png optional) - either parse
   `EditorSaves.package` from disk (share R|W) or `FindRecord` → `db->OpenRecord` (vt+0x34) and
   read the stream. A creature is ~3–10 KB (+ up to 13 KB creaturedata): send in chunks over the
   existing reliable channel together with `{owner, full ResourceKey, content hash}`.
3. **Receive**: remap the instance to a collision-free id, write
   `<profile>/Temp/coop_<peer>.package` (DBPF 3.0, uncompressed), create a DatabasePackedFile with
   that path, Initialize, `Open(1, 6, 0)`, `RegisterDatabase(true, db, 1000)` (one package per peer;
   unregister/re-register when a peer re-sends after an editor).
4. **Use**: creature puppets `GetSpeciesProfile(&remoteKey)` → CreateHerd → `Create(...,
   isAvatar 0)` (npc.cpp path); local avatar `SetSpecies(avatar, ownProfile, age, 1)` after the
   host world is loaded. Cell puppets: forced model key = remote key; local avatar: `S+0x10` +
   RebuildLevel right after the world arrives. Space: `SpawnUFO(6, &pol, &remoteUfoKey)`.
   Tribe/civ citizens: SetSpeciesProfile per owned citizen (as 0xD5BFA0 does).
5. `net::PlayerState.species` (u32 instance) must become a full key per stage.

Runtime checks still needed: baking of a never-seen creation on first spawn (IBakeManager::Get
0x401010, `*(void**)0x15D1C04`, is used by spawn paths; expect a delay/placeholder ?), GetDatabaseByID(0x11AC199) =
EditorSaves, SetSpecies on the live avatar (animations, hit points, UI ability bar refresh),
cell in-place model swap.

## 7. Editor "done" and the part list (✔ disassembly + runtime 2026-10-22)

Why the guest's "done" did nothing for its own creature (cancel worked): the creature comes from
the Creature Creator (My Creations) and holds parts that are not in the creature-stage editor's
part list. The stage editor refuses to finish such a creation.

The chain, from the button to leaving the editor:

| addr | what | conv |
|---|---|---|
| 0x5DF4B0 | `EditorUI::OnExitButton(int command, int)`: command 0x102 = done (0x101/0x103/0x107 other exits) | ✔ thiscall `ret 8` |
| **0x57AAA0** | `cEditor::GetFinishState()`: 3 = unchanged, may leave; 4 = unchanged but an error of `editor+0x48` is still there; 0/1/2 the same after edits (2 = a load-time error is still there) | ✔ thiscall plain `ret` |
| 0x5DEC50 | `EditorUI::RunExitCommand(command)`: switch on command-0x100, 0x102 → `cEditor::Exit(1, 2, 1)` (or a confirmation dialog when `ui+0xEC`) | ✔ thiscall `ret 4` |
| 0x5DCA40 | state 4 (and 2): `0x4F3B80(+0x48, current)` = first error bit in both, `0x4F3C80(bit)` = its message id (table 0x150D920, 8 bytes per bit), shown through 0x67CA80; the editor stays | ✔ |
| 0x58A350 | `cEditor::Exit(int how, int, int)`: 0 = commit (0x5802F0), 1 = accept and leave (0x57E8A0), 2 = cancel (0x57C1E0) | ✔ thiscall `ret 0xC` |
| 0x585D40 | save state machine (`editor+0x38C`: 1 → 2 → 4 thumbnail 256 → 5 thumbnail 128 → `Editor_Save` 0x577650), runs per frame from 0x58BE50 after an edited creature is accepted | ✔ runtime (136 calls, then one Editor_Save) |

Errors are 128-bit sets (`u32[4]`); `0x4F3DA0(a, b)` (cdecl, two sets by value) returns 1 when
they share no bit. `editor+0x1CC` is the live validation object (flags at +0x24..+0x30, masked by
the globals 0x15DB7EC..F8, read by 0x57A960); **`editor+0x48`** = the blocking set computed when
the creation is loaded: `cEditor::GetBlockingErrors` **0x57AC00** (thiscall `(u32 out[4],
cEditorResource*, model, bool) ret 0x10`, returns `out`; called from the load 0x57F6C0 for +0x48
and +0x58 and from 5 other places). It returns an empty set when app property 0x55D7CA1 is true
(a debug switch, ~), else the live errors plus:
- bit 10 (0x400): creature/cell without a part with property 0xB00F0FEC or 0x11B79301 (?);
- bit 18 (0x40000): check 0x4EFB60 against editor config prop 0x7A926123 (?);
- **bit 5 (0x20): a rigblock of the creation is not in the editor's part list**: editor config
  (`editor+0x24`) prop **0xF5CBE065** = list id, list from the global map 0x15DB960 (filled by
  0x4EB2B0 from the editor's palette resources; empty list = everything allowed), check
  **0x4EF8C0** (cdecl `(cEditorResource*, u32 listId, u32* flags)`: walks the rigblocks at
  `res+0x98`, 0x1D8 bytes each); message id 0x74E889BF;
- bit 11 (0x800): `editor+0x4B1 && !+0x4B2`;
- then filtered by 0x4EDF80.

Runtime (lane 3, guest Kirgle 0x290225CD in CreatureEditorExtraLarge 0x465C50BA): `editor+0x48`
= `{0x20, 0, 0, 0}`, GetFinishState = 4, the message for bit 5 is posted and the editor stays.
The host's species (Maxis 0x065CBE70, made in the stage) has an empty blocking set: state 3,
`Exit(1, 2, 1)`. Clearing bit 5 at `editor+0x48` by hand made the guest's done work; an edited
creature is then saved to My Creations under a new key (0x2904B8xx) and the editor returns to
the creature stage.

The mod (coop.cpp `h_BlockingErrors`) drops bit 5 from GetBlockingErrors while the player's own
editor (`open_own_editor`) is open: in the own editor the player's own parts are allowed. Other
blocking errors still stop "done". Not done: native evolution data of the request (`+0x90 =
0x64AA168`, `+0x94` = object from `0xF67D40()+0x10E8`, `+0x70` vector of 4 values from
`+0x10FC..+0x1108`, flags +0x34..+0x3D, see 0xD42F2C) is not filled by the mod's request; done and
cancel work without it.

## 8. After an editor: the new creation reaches the others (✔ runtime 2026-10-24, GOG 2024, lane 5)

What an editor leaves behind, seen with two games (`tools/tests/coop_editor_desync.py`):

* Every editor that saves makes a **new key** in My Creations (cell 0x4DA56CA5 premade → 0x29073D26,
  creature 0x2907406F → 0x29074073). The record is in the game's EditorSaves database at once, but
  **our copy of `EditorSaves.package` (read from disk every 10 s) lagged**: the first bundle of the
  new creation went out with **0 records** ("creations: player 0 cell = 29073D26 (0 records)"), the
  one with records 4 s later. A guest that got the key alone:
  * Cell: forced the unknown model key into `CreateCellObject` for the host's puppet and **faulted at
    0xE83EC8** (data 0x9C, from CreateCellObject+0x4B9 → ... → 0xE67A41), then the main thread hung
    (watchdog after 30 s). Twice of twice on lane 5 with the old code.
  * Creature: no species for the key, so the host's puppet took the guest's own avatar body until the
    next bundle.
  Since then a creation the game finds in the EditorSaves database (`FindRecord` returns that
  database) is sent only with its records: the mod reads the file again at once and otherwise waits
  for the next round (`coop.creations` prints `waits_on_disk`). A receiver does not use a bundle
  without records whose key it does not know (`unusable`).
* At the **start of the Creature stage** the game is in Creature mode (0x1654C01) with the creature
  made from the cell (a creation saved already, legless) **before** the first creature editor: the
  host's world goes to the guests with that creature, the editor request comes from inside the stage
  (the vote), and the world goes again when the host is back ("evolved the species").
* A guest **without own creations** plays the host world's species (its own creature = its avatar's
  key, sent without records: everybody knows the host's creations). The vote opens its own editor on
  the host's pre-editor creature; cancel keeps the world's species, which after the reload is the
  host's evolved one.
* The host's creations are registered under their original key on guests at priority 2000. A newer
  version under the same key replaces the older package now (two packages at the same priority could
  keep answering with the old one; not seen, editors make new keys); `world_swaps` counts it.
* A bundle applied again (the same version after a rejoin) reuses the package registered for it:
  writing the file again fails while the game has it open, and the creation was then not applied at
  all.
* After the player's **own cell editor** the new cell becomes the own one (as creatures since 7):
  a guest with other cells in My Creations otherwise got its newest older cell back with a level
  rebuild.
* **The guest's own editor right after a world load came up without its interface**: at the start
  of the Creature stage the host's world reaches the guest a few seconds before the vote ends; with
  a quick "I'm ready" the guest's own editor opened 0.5-5 s after the load: the creature and the
  background showed, but no palette and no buttons (`coop.ui tree 4`: 2-15 windows instead of 34),
  cancel did nothing and the guest stayed in it (2 of 2 test runs). After that load the planet showed
  6-15 s after the avatar appeared (black screen before). The mod now opens the own editor once the
  avatar has played for 15 s and no cinematic or caption is up (`world::cinematic_playing`,
  cCinematicManager +0x2C), at most 90 s after the vote. An editor opened by `coop.vote editor` while
  the host's stage-intro caption ("It is the beginning of a new day...") waited for its check stayed
  black after cancel too.

## The guest's own editor saves, the avatar keeps the world's species (2026-10-29, fix/w7-legless)
<a id="creations-own-editor-save"></a>

Seen on lane 8 (GOG 2024, `tools/tests/coop_legless_guest.py`, a fresh guest profile without
creations, the first creature editor after the Cell stage). BUG-0278.

* ✔ The own editor the vote opens (`open_own_editor`: an `EditorRequest` with the editor id and the
  avatar's species key only, none of the native evolution data of section 7) is the "edit a
  creation" editor, not the evolution one: it has a red cross (the host's first creature editor has
  only the check), "done" after an edit asks "Save 'X' as a new creation or replace 'X' before
  exiting?" (Don't Save / Cancel / Replace / Save New, at 510/612/830/926, 604 in a 1440x1080
  window), and the red cross after an edit asks "Exiting will cause your changes to be lost" (Cancel /
  Quit at 944, 604). "done" without a change leaves at once (GetFinishState 3).
* ✔ Whatever it saves goes into the guest's My Creations, but **the avatar keeps the species it had**
  (the world's: the host's legless pre-editor creature); the game does not put the saved creation on
  the avatar as the evolution editor does.
* ~ Key of the saved creation: "Save New" kept the key of the creation it edited, the host's
  (290D780E in My Creations of the guest, the host's pre-editor creature was 290D780E); "Replace"
  made a new one (290D79C9 for the host's 290D79C6). One run each. The next own editor's "Save New"
  gave 290D7813 (the host's evolved creature was 290D7812). ? how the game picks the instance.
* ✔ On a guest a key of the host's world answers with the host's package (priority 2000 above My
  Creations at 1000): an own creation under such a key is the host's creature in the guest's game.
* Before the fix (0.7.3.0): the return from the editor took the avatar's key as the own creature
  (`adopt_after_editor`, choices.ini) when it was not the newest own one: after Replace the host's
  legless key 290D79C6 was pinned ("back from the editor, own creature 290D79C6"); after Save New the
  newest own creation was that very key. From then on `apply_own_avatar` put the legless creature on
  every avatar the host's worlds brought (also after a rejoin and a restart: choices.ini), the host
  showed the guest with its own old creature, and the co-op window's arrows offered only the guest's
  My Creations (no way back to the world's species). The guest's own screen and the host's both
  showed the legless creature after Replace; after Save New only the guest's own screen did (the host
  got the guest's records under a remapped key).
* The fix (`creations.pre_editor`): a guest takes over after an editor only the newest own creation
  that was not in My Creations when the editor opened (read again every 2 s for 30 s; the host keeps
  the old way: right after its evolution editor its avatar still had the pre-editor key, which is
  one of its own creations, and a first version of the fix pinned it); a pinned key of the host's world that is not ours is
  ignored; an own creation under a host key is used under its remapped key (`player_key`, the same
  records the host shows); the co-op window's arrows include "the world's own" (the host's current
  creature, sent by key).
* ? Cancel run (one of one): while the guest's own editor was open the creature it showed got the
  host's new legs and the DNA dropped by their price (115 -> 65) without any input of the guest, so
  the red cross asked to confirm (BUG-0283).

## An editor during the avatar's body swap stays black (2026-10-30, fix/w14-creature)
<a id="creations-editor-after-swap"></a>

BUG-0318. GOG 2024, lane 4, guest at 6.7 fps (`frame.limit 150`).

* ✔ An editor request (`Editors::EditorRequest::Submit`) 0.3 s after `cCreatureAnimal::SetSpecies`
  on the avatar (creations.cpp `apply_own_avatar`, after a rejoin): the editor's windows exist (as
  many as in a working editor) but nothing is drawn, for minutes and also after the frame rate goes
  back up; the red cross, Quit and Esc do nothing. 60 s after the swap, or the swap done at normal
  frame rate first, or the editor entered at normal frame rate: the editor draws. At 15 fps 3 s
  after the swap it drew.
* ~ Inferred: the new body of SetSpecies is built over the following frames and an editor that takes
  the avatar's creature before that never finishes its setup.
* ✔ The same right after a load without a swap (the new avatar of the loaded world), 1.2-1.7 s
  after the load.
* Fix `creations.editor_after_swap`: editor requests wait until the last swap and the current avatar
  are 5 s and 300 frames old (`creations::avatar_swap_settling`; coop.cpp `h_Submit` holds the request, `submit_tick`
  sends it, `own_editor_tick` waits too).
* CORRECTION (2026-10-30, fix/w13-creature, recorded on fix/w14-creature): the "? Cancel run" bullet
  of [the own editor save section](#creations-own-editor-save) (the host's legs and a DNA drop in a
  guest's open editor, BUG-0283) was the test's own edit: `coop_legless_guest.py --guest cancel`
  added legs and pressed the save dialog's Cancel (BUG-0317).

## Content packs: Creepy & Cute parts and the part ids in a creature model (2026-10-31, fix/w22-reports)
<a id="creations-content-packs"></a>

* ✔ Creepy & Cute (GOG: `bp1content/BoosterPack_01.package`, Steam: a DLC of its own) holds 119
  creature parts (rigblock props, type 0x00B1B104, group 0x40626000) that no package of `Data\` or
  `DataEP1\` has. List: `mods/coop/content_packs.h` (from the game files' index). It holds no premade
  creatures.
* ✔ A creature model (`.bem`, type 0x1A99B06B, group 0x40626200 in My Creations) names each part as
  its group 0x40626000 (bytes `00 60 62 40`) followed by the instance. Byte runs are packed: `FE n`
  = the byte before, n more times; a 0xFE byte is written `FE 00` (Tiger Horse's part 0x4BF3FE6A is
  stored `6A FE 00 F3 4B`). `creations::model_parts` reads them (ids with a run inside are skipped).
  ~ Not every id after the group is a part prop: Kirgle (fixture) names 0x05D58995, which no game
  package holds, and builds fine: only ids of the pack list count.
* ✔ runtime (GOG lanes 5/6, `coop_guest_missing_pack.py`): a game without the pack loading a world
  whose creature has one C&C part faults at **0x441F3B** (FUN_00441910+0x62B, the model bake reads a
  null property list `this+0xC`, BUG-0341), 0.5 s into the load, from the save reader. Under Wine the
  fault is caught and the load hangs; on Windows the game crashes. The same creature on a game with
  the pack: no fault. A creature from My Creations chosen by the host (choices.ini) alone does not
  crash a guest that has all its parts.
* The guest decides with one key: `have_creepy_cute()` = the resource manager finds the first part of
  the list (logged once: `creations: content: Creepy & Cute parts pack present / NOT present`).
* The world shared by the host does not hold creation records: the guest's game builds the host's
  creatures from what co-op registers (`world_*`, `own_species_*`, `species_*` packages), so checking
  the bundles before the world covers the world's creatures.
* ~ superseded in part (2026-10-31): a creature with pack parts is no longer refused but shown with
  base parts, see #creations-part-substitutes (the refusal stays for a part without a substitute).

## The own cell's rebuild right after a Cell load (2026-10-31, fix/w22-reports)
<a id="creations-own-cell-rebuild-timing"></a>

* ✔ runtime (lanes 5/6, `coop_cell_own_load.py`): RebuildLevel asked for (`G+0x51D8 = 1`) by
  `apply_own_cell` on the first frames after a reload of the host's Cell world leaves the guest's new
  avatar unable to eat plants it swims into (AvatarEat never runs: 0 of 3 in 2 of 2 runs); the same
  rebuild 5 s / 150 frames later: 3 of 3 (BUG-0342, `cell.own_rebuild`).
* ~ After the first load of a join the delay is not enough: 2 of 4 runs did not eat. Unknown why.
* ✔ `g_cell_set` must be forgotten when `game::stage_epoch(Cell)` changes, or a reload keeps the
  world's cell (BUG-0349, `creations.cell_reapply`).

## Pack parts shown as base parts on a game without the pack (2026-10-31, fix/w24-parts)
<a id="creations-part-substitutes"></a>

BUG-0354 (the owner's choice for BUG-0341: substitute instead of refusing). Files read with
`tools/part_substitutes.py` (the game install, read only); runtime on GOG lanes 8 / 9
(`coop_guest_missing_pack.py`).

* ✔ A part prop (type 0x00B1B104, group 0x40626000) is a property list (big endian count; each:
  id u32, type u16, flags u16; keys 16 bytes little endian instance/type/group + pad; arrays: count u32,
  item size u32). Ids that mark a part's role are a family `0xB00F0FXX` (one per role):
  **0x0FE9 eye**, **0x0FEC mouth**, **0x0FE2 grasper / arm**, **0x0FEF foot**, 0x0FDF bone / leg,
  0x0FE5 ear, 0x0FF4 horn, 0x0FF7 mace, 0x0FF2 spike; details have none (names checked against the
  en-us locale of the base parts, e.g. 0x0FE9: Grumpeye, Optic Orb; 0x0FEC: Whalephant, Leeyotch).
  The ability levels are int properties `0x??C3E5XX` (1..5; e.g. a mouth's diet is one of them:
  0xB1C3E5B5 with the herbivore mouths, 0xB1C3E5B6 with the carnivore ones); 0x02166464 is the cost
  (10 / 25 / 75 / 150 / 250), 0x3C652302 / 0x51FA9DFA / 0x03A289AC small editor ranks. The part's name
  is a text property 0x8F6FC401 (table 0x325440A2 in Data/Locale/en-us/Text.package for the base
  parts; the pack's table 0xB90C8C60 is not in that package).
* ✔ The pack's 119 parts: 12 eyes, 12 mouths, 12 graspers, 12 feet, 71 details. The table
  `kCreepyCuteSubstitutes` (content_packs.h) gives each a base part of `Spore_Graphics` / `PatchData`
  (not a cell part) with the same role set, the nearest abilities (each level apart 1, a missing or
  extra ability 3), the same ranks and cost, and the most properties in common; its id has as many
  0xFE bytes (a model writes 0xFE as `FE 00`, so the substitute fits in place).
* The guest rewrites the ids in its copy of the creature's model records (`substitute_parts`, before
  the records are registered: a player's creature, a species of the host's world, the host's own
  creation of its world); nothing of the pack is sent and no save file is touched. A part without a
  substitute, or whose substitute this game lacks (`key_exists`), keeps BUG-0341's refusal.
* ✔ Runtime (lanes 8 / 9): Carol without the pack loads the host's Creature world whose creature
  (Tiger Horse with the pack part 0x17A60A7F, substituted by 0x0330D834) she plays with: the model her
  game reads has 33 parts, none of the pack, the species profile is built, 0 faults; Bob with the pack
  reads the original part (`coop.creations parts 290229B9`). Log: `creations: player 0's creature
  'Tiger Horse': 1 Creepy & Cute part(s) shown as similar base game parts`, and once per session the
  notice "Some Creepy & Cute parts are shown as similar base game parts".
* Gameplay: the guest's own avatar has its own parts (unchanged); the host's creatures and NPC species
  are run by the host (its abilities count), the guest only shows them, so a substitute changes their
  look on that guest, not what they do. ? Not checked: an animation that only the pack part has (the
  base part's own is played on that guest).

## The guest's own editor and the parts the species has collected (2026-11-01, fix/w30-progress)
<a id="creations-own-editor-items"></a>

BUG-0368. GOG 2024, Ghidra and lane 10 (`tools/tests/coop_guest_editor_locks.py`).

* ✔ (Ghidra) The creature stage's own evolution request is built in the creature game's message
  handler 0xD42E50 (message 0x4471C60, only with `this+0xDA` set): `new EditorRequest`, `+0xC`
  0x465C50BA, `+0x10..+0x18` the avatar's species (0xC0BD60), `+0x90 = 0x64AA168` (the tag the
  return message 0x30C11C7 carries back at its +0xC: then 0x572620 applies the evolution), **`+0x94` =
  `cPlayer+0x10E8`** set with the intrusive pointer assignment 0xB5FAB0
  (`SporeCoop::IntrusivePtr_Assign`), flags +0x34/+0x36/+0x37/+0x38/+0x39/+0x3B = 0 and +0x3C/+0x3D = 1,
  `+0x70` a vector of the four dwords `cPlayer+0x10FC..+0x1108`; after Submit it posts 0x5D02A72 and
  sets byte 0x1686AF0. `cPlayer` = `cGameNounManager +0x74` (0xF67D40).
* ✔ `cPlayer+0x10E8` is the species' collected creature parts, a `Simulator::cCollectableItems`
  (UnlockPart 0x596DA0 / LockPart 0x596E10 work on it). `IsUnlocked` 0x595110 (fastcall, ECX = items,
  8-byte key on the stack, ret 8): the item's state (hash map +0x4D04) bit 0; an unlockable item
  (hash map +0x1490: buckets +0x1490, count +0x1494, node: key +0/+4, next +0x28) without a state is
  locked; anything else answers `this+0xC`.
* ✔ (Ghidra) The palette asks 0x5C6820(item, items) "available" = `items == null` or
  `IsUnlocked(item+0xC, item+0x14)` (0x5C6720 the opposite, "locked"); `cEditor::OnEnter` takes the
  items from its request (`cEditor+0x1CC`) +0x94. **A request without +0x94 offers every part.**
* ✔ runtime: the editor game mode is in the mode manager's list (`IGameModeManager::Get()` +0x14 ..
  +0x18, entries of 0x18 bytes `{mode, id, name string}`) under id 0xDBDBA1 (0x57F3E0 registers it);
  its vtable 0x13F5800 (+0x18 OnEnter). `IGameModeManager` vt 0x50 is not the active mode (another
  object on every call).
* ✔ runtime (lane 10, creature fixture): the guest's own editor (`open_own_editor`) had `+0x94 = 0`:
  0 of the 312 unlockable parts locked, every part in the palette (the player report). With the
  fix (`creations.own_editor_items`: the request gets the guest's `cPlayer+0x10E8`, Creature only)
  258 of 312 are locked, the palette shows the locked silhouettes, "done" without an edit still
  leaves at once. The guest plays the host's world, so these are the pack's collected parts (the
  host's save).
* Not done: the other native fields (+0x90 evolution tag, +0x70, the flags): +0x90 would make the
  guest's editor apply an evolution to the world's species. The test command `coop.vote editor` on
  the host builds a bare request too (its palette is unlocked in tests; the real host's editor is the
  game's own).
* Updates section 7's "Not done" line: +0x94 is now filled for the own editor (Creature).

## Content packs: Creepy & Cute paints (2026-11-01, fix/w30-progress)
<a id="creations-paint-substitutes"></a>

BUG-0369. GOG 2024 game files and lane 10 (`tools/tests/coop_guest_pack_paints.py`).

* ✔ A creature model (bem) names its three paint layers (base, coat, detail) by **skin effect ids**:
  three dwords at byte 26 of the (unpacked) model header, right before the three layer seeds (1234
  each) and the colours (`EditorModel::Save` 0x4AF780 writes `model+0x8C..+0x94` effects, `+0x98..`
  seeds, `+0xA4..` colours into the resource's +0x18 block). In the fixtures the ids are written byte
  by byte (no 0xFE run in the header).
* ✔ A paint prop (type 0x00B1B104, group 0x406A0200; name 0x00B2CCCA) lists its layers' effects in
  0xB0E066A7 (an array of three keys, 0 for a layer it does not paint) and default colours in
  0xB0E066A8. The base game's effect sources are type 0x497767B9, group 0xEA5118B1
  (Spore_Graphics.package; some, e.g. 0x103691CE, elsewhere).
* ✔ Creepy & Cute holds 48 paint props (themes, base, coat, detail) whose 36 effects are compiled
  into the pack's own effect directories (type 0xEA5118B0, groups 0x40604300..02): no base package
  has them.
* ✔ runtime (lane 10, both games without the pack, the host's Tiger Horse with the pack's "BP1
  Dottie" base and coat): the guest showed the host's creature **black** (the layers it cannot find are
  not painted). The player's "different colour".
* Fix `creations.paint_substitute` (creations.cpp `substitute_paints`, on a game without the pack
  only, where the part substitutes run): each pack effect in this game's copy of a creature's model
  is rewritten in place to the base game effect of the same layer whose paint has the nearest
  default colour of that layer (content_packs.h `kCreepyCutePaintSubstitutes`, 36 entries from
  `tools/paint_substitutes.py`; the same number of 0xFE bytes, so it fits). The model's own colours
  stay: the creature keeps the host's colours on a base game pattern. `coop.creations paints <key>`
  prints the three effects of the model the game reads and how many are the pack's.
* ? Not reproduced: "cubes". The black creature above is the only effect seen of a missing paint; the
  pack parts are substituted since BUG-0354 (10 on the player's guest, logged). Not checked: a pack
  part whose substitute has another parent block (eye children map to children of other eyes).

## A player's replaced creature in the host's world (2026-11-01, fix/w31-creature-gate)
<a id="creations-old-species"></a>

* ✔ A player's creature is registered on every machine as `0x7C000000 | fnv(owner, bundle id)`. A new
  bundle (an editor, `coop.creations next creature`) registers the new key and unregisters the old
  package at once, on the host and on the owner's own game (BUG-0370).
* ✔ The host's game keeps the species profile of the old key: `coop.npc spawn <old key>` still makes a
  creature of it after the drop, and the NPC sync sends such a creature with the old key. Its census
  row shows no species (the key reads 0).
* ✔ A creature of it in a herd like a puppet's (no archetype, no nest) is removed by the host's stray
  pass (npc.strays) within a second; the players' NPC (BUG-0370) stayed, so its herd was not one.
* Since creations.old_species the host keeps the records of the last 24 player creatures applied and
  answers a guest's `kAskSpecies` from them (`old_sent=` in `coop.creations`).

## Collected parts: UnlockPart, LockPart, a count for the trace (2026-11-01, feat/w32-monitor-diag)
<a id="creations-unlock-part"></a>

GOG 2024, objdump of `SporeApp.exe` (dc04aee5) and lane 6.
* ✔ `cCollectableItems::UnlockPart` 0x596DA0: thiscall(items, key_lo, key_hi, int cost) ret 0xC. False
  when `IsUnlocked` 0x595110 says so already; a cost != 0 is taken from `this+0x6D70` (false when that
  goes negative); then the state map entry (`+0x4D00`, find-or-insert 0x595EB0) gets bits 0-1 set and
  the key is added to the set at `+0x6D80` (0x594B10); returns true.
* ✔ `LockPart` 0x596E10: thiscall(items, key_lo, key_hi) ret 8: the same entry's bits 0-1 cleared, the
  key added to the set at `+0x6D80` (0x5945B0); returns true.
* The trace's `parts=<count>:<hash>` (own line, Creature) counts the unlockable items (the map at
  `+0x1490`) that `IsUnlocked` answers true for, with an order-free hash of their keys
  ([06-testing.md#testing-monitor](../06-testing.md#testing-monitor)). Lane 6, creature fixture: 54
  of 312 on host and guest (the same hash). `coop.parts unlock <n>` unlocks n locked ones with cost 0
  in that game only (the plant of the monitor's `unlocks` rule).

## How a player gets a creature part, and the party's shared set (2026-11-01, fix/w33-unlocks)
<a id="creations-part-pickup"></a>

BUG-0401. GOG 2024, Ghidra (out/ghidra-2024) and lane 6 (`tools/tests/coop_parts_shared.py`).

* ✔ (Ghidra) Every unlock in play goes into the cPlayer's collected parts (`cPlayer+0x10E8`), but **not
  always through UnlockPart 0x596DA0**. The creature stage's finds and rewards:
  `CreatureStageEventHandler` 0xD47FC0 (a listener of `cCreatureModeStrategy`, its messages) for
  0xD335362C (a creature's drop after a hit / kill, any attacker; once per creature: `+0xB58 |= 2`),
  0x49B56D7 (several rewards at once, posted by 0xD3FCA0) and **0x5371F11 (a part found: msg
  `{creature, 0, level}`, the creature must be the avatar)** call 0xD47E50 (pick a locked part of the
  level) -> cCollectableItems 0x5976D0 -> **0x597410**, which sets the state (`|= 3`) and appends the key
  to the list at `+0x6D80` itself; then 0xD3D4A0 shows the UI (goal card `CRG_FirstPartUnlock`, the
  part pop-up). 0x61B1320 (0xD3B640) marks a category's items state 4 (seen), not unlocked.
* ✔ (Ghidra) 0x5371F11 is posted by 0xD46BE0, the reward of an interactable (kind +4: 0 health,
  2 DNA, 3 / 5 messages, 4 death, 6 a part of level `round(+0x14)`), to every listener of the
  strategy (`+0xBC..+0xC0`, vt 0x18), as `cCreatureModeStrategy::ExecuteAction` does.
* ✔ (Ghidra) UnlockPart's callers are loads and set-ups: the start from Cell (0xC7A390 over the
  creature's blocks, 0xDEB720 / 0xE83910), the editor's own items (0x5BF2E0, a new
  "Editor/cCollectableItems" for some editors), the Cell game (0xE4FAA0), Space scans (0x1058430),
  `cStarManager::ReadStarsFromSaveDB`, the outfitter UI (0x5EF4F0).
* ✔ runtime (lane 6, creature fixture): `cCreatureModeStrategy::ExecuteAction(Get(), 0x5371F11,
  {avatar, 0, 1})` (`coop.parts find 1`) unlocks one part (54 -> 55), on host and guest alike. The
  trace's `parts=` hash moves with it.
* So a part sync cannot hook UnlockPart: parts.cpp polls the unlocked unlockable items (the
  `parts=` census, 1 Hz in Creature) and sends the difference.
* The co-op rule (docs/07-modes.md 7.1: shared progress like DNA): the party has one set, the union,
  held by the host's game. `parts.shared` (BUG-0401): a guest sends the parts the host's set lacks
  (MSG_PARTS kind 2, resent every 3 s, at most 5 times a key), the host unlocks them (UnlockPart,
  cost 0) and sends its whole set to every guest (kind 0, at once on a change, else every 10 s;
  kind 1 the news with the finder, a notice "Bob found a new creature part"), a guest that plays the
  host's world (same world id) unlocks what it lacks, also after a reload. Nothing is locked. The
  host's world saved at a join holds the union, so a rejoin keeps the guests' finds.

## The Cell twins: creature parts a live Cell -> Creature start adds (2026-11-01, fix/w36-parts-transition)
<a id="creations-cell-twins"></a>

BUG-0441. GOG 2024, Ghidra (out/ghidra-2024) and lane 9 (`coop_transition_net.py --to creature`,
`coop.parts keys|states`).

* ✔ (Ghidra) The live start of Creature from Cell, 0xE83910, after 0xC7A390 (the creature's blocks) calls
  0x598540(0x465C50BA) and then, over the table **0x1485FE8** (12 pairs `{cell instance, creature
  instance}`, ends at 0x148604C), for each pair: the cell key (type 0x40616000 = DAT 0x150D6D8) and the
  creature key (type 0x40626000 = DAT 0x150D6DC) through 0x593980; `AddUnlockableItem` 0x598DB0
  (creature key, all other arguments 0: level -1) into the unlockable map `+0x1490`; 0x596D70 (state
  = 4, seen); `UnlockPart` (cost 0) when the cell key is unlocked (the cell had that part). Then
  `sub_594010` 0x5942E0 clears bit 1 (new) of every state.
* Creature twins (instance, type 0x40626000): 02B3EDDB 7E262CF0 BE830BDE 4908E576 B70894B6 C5744537
  1610AD94 30E66572 DEF1C7FF 32AC9620 FE534766 3A3C49A5 (cell keys 3824091B 026D3C30 5683C01E
  2FC35736 BAAFF976 2A889977 12599FD4 7DD30832 35916FD9 64028FE0 2494A726 A1C47665).
* ✔ runtime (lane 9, `cell` fixture, Cell -> Creature the players' way): the host's map has 324 items
  (55 unlocked: 49 + the 6 twins of its cell's parts, state 5), the guest that loaded the host's world
  has 312 in the map (the fixture's 312 too) but **the same 324 item states** (`coop.parts states`
  identical on both games, the 6 twins state 5 and IsUnlocked). So a load rebuilds the map from the
  game's data without the twins and keeps their states from the save.
* ✔ (Ghidra) `IsUnlocked` 0x595110 reads the state map `+0x4D04` first (bit 0), then 0 for an item of
  the map `+0x1490`, else `+0xC` (1 on both games: an item the game knows nothing about is offered).
  The palette's check 0x5C6820 is IsUnlocked: a loaded game offers the unlocked twins too, so the
  guest's editor was right; only a census over `+0x1490` (BUG-0401's first version) counted 6 fewer.
* The state map: buckets `+0x4D04`, count `+0x4D08`, node `{u32 lo, u32 hi, u8 state, ..., next
  +0x10}` (as `sub_594010` walks it). In Creature it held only creature parts (type 0x40626000).
* So parts.cpp's census (`parts.census_states`) goes over the map and the states together (each key
  once): every game counts what its palette offers, and the locked twins are in the locked list that
  parts.shared unlocks from.
* Not a reward of the host only: the guest's game has the host's twins. Not checked: whether a guest's
  own Cell parts (collected in its game during co-op Cell) should count for the twins; the guest
  loads the host's world, so the host's cell decides (BUG-0446).

## Unknown parts check (2026-11-02, fix/w38-content)
<a id="creations-unknown-parts"></a>

BUG-0452. GOG 2024, lanes 4/5 (`tools/tests/coop_unknown_parts.py`), the game files' index.

* ✔ A part key in a model is the group's last three bytes (`60 62 40` for the creature parts group
  0x40626000, `60 61 40` for the cell group 0x40616000, `60 64 40` for the UFO group 0x40646000) and the
  instance right after them. The group's least significant byte (`0x00`) is usually written as a byte run
  (the byte before, repeated), so a scan for the literal four bytes finds only the keys whose `0x00` is
  literal: the 33-part Tiger Horse model yielded 1 reference, not 33. `model_part_refs` matches the three
  bytes and reads the instance after them; the slot and parent that follow must be plausible (a small
  index or -1), which keeps the group bytes that occur in other data out.
* ✔ The 0.7.3.4 checks only know the Creepy & Cute part and paint lists. A guest without
  `Data/Spore_Pack_03.package` and a host creation with one part of that pack (`0x75A5D2D4`) faults at
  `0x00441F3B` (data 0) in the bake, exactly as BUG-0341 (`coop_unknown_parts.py`, FAIL run
  `out/lab/20261102-122525`).
* ✔ Every part id of every received creature, cell and UFO model is now checked with `key_exists` (cached
  per session). An unknown part is shown as a base game detail of the same group and the same number of
  `0xFE` bytes (`content_packs.h` `kBasePartFallbacks`); the Creepy & Cute table is the first choice. An
  unknown paint is shown as a base game paint of the same layer and length (`kBasePaintFallbacks`); the
  base game's 111 skin paint effects are `kBasePaintEffects` (14 of them have no effect record in any
  package, the game defines them itself).
* ✔ An id that cannot be replaced refuses the creation: a puppet falls back, an NPC species is not
  registered, the host's own world creation keeps the world from loading (the message in `world.cpp`).
  Each unknown id is logged (`creations: ... part <id> unknown here: this game does not have it ...`).
* ✔ Runtime (lanes 4/5, PASS run `out/lab/20261102-122224`): Carol without Spore_Pack_03 plays the host's
  Creature world, no fault, logs the unknown part id; the model her game reads has 33 parts, none unknown,
  the species profile built (`parts 290229B9 n=33 cc=0 unknown=0 species=1 ... substituted=2`).
* Covered kinds: creature, cell, UFO (the only `Kind` values; the mod has no vehicle or building creation
  path). Paints are checked for creature models only.
* ? Not verified: a part of another pack whose kind is not a detail (the kind is in the prop record the
  guest lacks, so it is shown as a detail, not the same kind); the three paint effect dwords of cell and
  UFO models; a paint effect that has no base game substitute (the creation is refused, never baked).

## Content check for every model kind (2026-11-02, fix/w39-content-all)
<a id="creations-content-all-kinds"></a>

BUG-0469, GOG 2024, lanes 4/5-6 (`tools/tests/coop_content_civ.py`, `tools/tests/coop_content_space.py`),
the game files' index and Ghidra. The rest of BUG-0424 after BUG-0452.

* ✔ `creations::ensure_model(model, kind)` (`mods/coop/creations.cpp`) answers for any model key the mod
  makes this game build: this game has the key AND every part / paint id the model names (the
  `model_part_refs` / `part_known` / `paint_known` of BUG-0452). An id this game lacks is shown as a
  substitute in a copy of the model's records (`content_packs.h` tables, as for a received creation),
  registered at once **under the model's own key** (see the next bullet); a model with an id that has no
  substitute, or whose key cannot be shadowed, is refused (the caller uses the game's own default or
  leaves the object out). Each unknown id is logged once per session:
  `creations: content: <kind> model <id> part <id> unknown here: this game does not have it`.
  Switch `content.all_kinds` off: 0.7.3.5, the new call sites pass the key as they get it.
* ✔ The copy goes under the model's own key, never under a key of the mod's (BUG-0469 review,
  2026-11-02): the first version registered it under an instance of our own (0x7D......, as a received
  creation is), and a civ mirror vehicle is created from that key: mirror vehicles are not kept out of
  saves (BUG-0470), so the guest's save named a model key that no package answers after a restart (or
  without the mod). The package is written into `SporeCoop\` as before but registered at priority
  **3000**, above the game's own packages, the host's world packages (2000) and My Creations (1000):
  `RegisterDatabase` 0x8E1000 keeps the databases in a priority-sorted list (+0x2C) and
  `FindDatabase` 0x8DE620 walks it highest first, probing `OpenRecord` (no global key index), so the
  key now answers from our package. `ensure_model` checks that after registering (`FindRecord` 0x8DFC80
  then the database's `GetLocation` 0xA426C0 = our file) and **refuses** when another package still
  answers, so the caller never builds a model that would fault and never sees a key of ours.
  Nothing of the game was found that resolves a model key once and keeps it: the model bake re-reads the
  parts on every build and the earlier FAIL run faults on every attempt (456 faults in one civ run), and
  the runtime runs below show the copy in use at once. `coop.fix content.model_key off` keeps the first
  version (a key of our own) for the lab. Runtime (civ, lane 4 + guest lane 5): the guest's save names
  the design's own key 0x29030001 and no 0x7D instance, and the save loads in a game restarted without a
  session with the mirror vehicles (`out/lab/20261102-213135`); with `content.model_key` off the save
  names 0x7D72C793 instead (`out/lab/20261102-213354`, FAIL).
* ✔ Model kinds and their keys: creature `crt` 0x2B978C46 / 0x40626200 (parts 0x40626000), cell `cll`
  0x3D97A8E4 / 0x40616200 (parts 0x40616000), UFO `ufo` 0x476A98C7 / 0x40656200, **vehicle** `vcl`
  0x24682294 / 0x40646200 and **building** `bld` 0x2399BE55 / 0x40636200.
* ✔ Vehicles and UFOs name their parts in the *same* group 0x40646000 (`Data/Spore_Content.package`: the
  4 sampled models of each kind name 13 to 31 part references with `model_part_refs`; the count of literal
  `60 64 40` bytes is lower, the group's 0x00 is usually a byte run); a vehicle design key
  {vcl, 0x40646200, X} resolves to a {bem, 0x40646200, X} record (the game's type mapping, as for `crt`).
  Building models name their parts in 0x40636000 (the base game has 224 parts of that group, none with a
  0xFE byte: a building part with one would be refused).
* ✔ The bem header of a vehicle, UFO and building model carries the three paint effect ids at byte 26 as
  a creature model does: 42 of the 285 vehicle, 32 of the 91 UFO and 17 of the 310 building models of
  `Data/Spore_Content.package` name a nonzero id there (the others are all 0: no layer painted); those
  ids are base game skin paints of `kBasePaintEffects` or have an effect record (0x00628C255 of a
  building model), except the 3 of the next point, so `ensure_model` checks and substitutes paints for
  these kinds too.
* ~ 3 paint ids that shipped vehicle, UFO and building models (and 14 shipped creature models) name in
  their base layer are in no paint prop and in no effect record: 0x791C8201, 0x7A2E9EB3, 0xDCAD3E09.
  `paint_known` takes them for unknown and shows a base game base paint instead (as 0.7.3.5 does for the
  creature models): a colour difference, never a refusal (all are base layers without a 0xFE byte, so
  `kBasePaintFallbacks` has an entry).
* ✔ A *cell* model's header is not the creature one: of the 44 cell models of `Data/Spore_Content.package`
  43 name an id at byte 26 that is neither a base game skin paint nor an effect record (19 distinct ids;
  the ids look like cell paints, which the game defines elsewhere), and the second and third dwords are 0.
  `paint_known` would take almost every cell model for one with an unknown paint, so cell paints are not
  checked.
* ✔ A model that names a part this game does not have faults in the *model bake*, at a different place per
  kind: the creature bake at 0x00441F3B (BUG-0452) and the vehicle/UFO bake at 0x0046C82A
  (`SporeCoop::ModelBake_PartProp_Null`, `mov eax,[edx]` where edx is the part's property object, prop
  0xAFFF3A14). Runtime: the guest of the two tests with `SPORE_FIX_OFF=content.all_kinds` faults 456 and
  467 times (civ, every frame the mirror is tried) and twice (space, once per mirror) in one run
  (`out/lab/20261102-202807`).
* ✔ Call sites (`content.all_kinds`): a civ mirror vehicle (`civ.cpp` `guest_state`: the civ's
  `GetDesignKey(VehicleDesignId(purpose, loco))`, then `CreateVehicle` with the checked copy's key; a
  design whose model cannot be built at all is created from a zero key, which the game fills with its own
  design of that purpose and locomotion: docs/re/civ.md#civ-content-all-kinds), a building of a city on a
  guest (`civ.cpp` `build_in_city`: `SetBuildingModel` reads the civ's design key itself, so a design
  whose model names ids this game lacks is left out and logged, nothing can be substituted for it), a
  host ship's mirror (`squadron.cpp`: the model key AND its parts, a substituted copy, else the local
  model of a ship of that empire as BUG-0323: docs/re/space.md#space-mirror-content).
* ✔ Not built by the mod: tribe huts, tools and outfits (a tribe's members are creatures and their
  outfits are creature parts of the species model, covered by the creature check of BUG-0452), a player's
  own puppet in Space (its key is the local key of an applied creation, already checked), the host's own
  vehicles and buildings (the host's game bakes its own designs itself).
* ? Not verified: the refusal path of a model whose id has no substitute (a part with 2 or more 0xFE
  bytes cannot be written into a fixture model in place); a paint of a pack in a ship or vehicle model
  (no pack in this install has ship or vehicle paints: the Creepy & Cute pack has creature parts and
  paints only); a key whose records are not in a package the mod can read (a baked resource): the model is
  built as it is and a warning is logged once.

## The own editor's price, the evolution request, the result message (2026-11-03, fix/w40-editor)
<a id="creations-own-editor-dna"></a>

BUG-0466, BUG-0457. GOG 2024, objdump of `SporeApp.exe` (dc04aee5) and lanes 6 / 7
(`tools/tests/coop_editor_sync.py`).

* ✔ (runtime) The guest's own editor (`open_own_editor`, no evolution tag) shows the DNA budget and the
  price of new parts (a mouth 25: 40 -> 15), but **the game never takes the price**: back in the stage
  GetEvolutionPoints was 40 again, no mod code wrote it (lab 20261103-141812).
* ✔ (disassembly) `SporeCoop::CreatureGame_HandleMessage` 0xD42E50, thiscall bool(this, u32 msg, void* data)
  ret 8: message 0x30C11C7 is an editor's result; data +0xC carries the request's +0x90, +0x18 the
  species key, **+0x34 the remaining DNA (int)**, +0x44 cancelled. Only the tags 0x12191CA (the Cell ->
  Creature editor) and 0x64AA168 (an evolution, not cancelled) go to 0x572620(this+0x5C, data), which
  keeps the result for the stage's return (state machine 0xD43DE0): 0xD44545 the species from +0x18,
  0xD445AA `SetEvolutionPoints` 0xD2E480 = (float) +0x34. ✔ runtime: the own editor's result had tag 0,
  remaining 15, cancelled 0 (lab 20261103-143345).
* ✔ The DNA value is the float 0x169E398: GetEvolutionPoints 0xD2E350 reads it, AddEvolutionPoints 0xD2E8A0
  adds, **SetEvolutionPoints 0xD2E480** (cdecl(float), plain ret) writes it (callers 0xD402CB, 0xD44222
  from cPlayer +0x10EC, 0xD445AA). After Submit the creature stage posts **0x90A03FDF** with +8 = (int)
  GetEvolutionPoints (0xD455FD: the editor's budget, the same message as the Cell editor's).
* Fix `creations.own_editor_dna` (coop.cpp h_CreatureGameMsg): the own editor's result (no evolution tag,
  not cancelled) sets the DNA to its +0x34 through the game; `core.dna_spend` sends the spend to the host.
* ✔ (disassembly) 0xD42E50 message 0x4471C60 (Creature mode, `this+0xDA` set) builds the evolution request
  (docs above, #creations-own-editor-items) and after Submit posts 0x5D02A72 {+8 the collected parts' count
  object} and sets **0x1686AF0 = 1** (`SporeCoop::CreatureEvolutionPending_flag`; read by 0xB5EB94 and
  0xCD00C0, cleared at 0xB5F19D). Push into the request's +0x70 vector: `SporeCoop::VectorU32_PushBack`
  0x454D30, thiscall(vector*, const u32*) ret 4.
* ? (runtime, lab 20261103-142443) A request built like that by the mod (`coop.vote evolve`) opened the
  host's editor through the vote; after an edit and done the host's DNA stayed 40 and its avatar key
  065CBE70 for 60+ s, with hearts over the nest: the evolution was not applied. `coop.vote editor` (all
  older editor tests) is the "edit a creation" editor for the host too: its result never changes the
  host's species (BUG-0457 not reproduced through it).

## The host's editor without a world reload: what the guest's game needs (2026-11-03, fix/w40-no-reload)
<a id="creations-evolve-in-place"></a>

BUG-0508. GOG 2024, lane 6, `tools/tests/coop_evolve_in_place.py --case cell|creature` (a guest without own
creations: it plays the host world's species, the case where the reload gave the guest's own avatar its body).

Until 0.7.3.6 every host editor inside a stage (`on_mode_change` "evolved") marked every guest `needs_world`:
`follow_phase` saved the host's game (MessagePost 0x1CD20F0) and sent it, the guest went stage -> galaxy menu
(GalaxyGameEntry 0x02CCD1D2) -> LoadGame -> stage (lab 20261103-173049 Cell: 3 s in the menu;
20261103-173315 Creature: menu + LoadGame, 4 s). What that reload carried, checked one by one:

* ✔ (runtime) **The host's puppet**: not the reload. The host's creation bundle (share_own, every 2 s) reaches the
  guest before the world, is registered under a content key (0x7C......) and under the host's key at priority 2000,
  and rebuilds the puppet (Cell 7C76F89F 'Inplacecell', Creature 7C530FCB 'Burpee') with or without the reload.
* ✔ (runtime) **The guest's own avatar** (a guest that plays the world's species): the reload gave it the host's
  new species (the save's avatar). Without it the guest kept the pre-editor body (Cell 4DA56CA5, Creature 065CBE70).
  Now `creations.follow_evolution`: when the host's bundle changes from the key the guest plays (or played before
  the vote's editor) to a new one, the guest's `mine()` follows it in this stage world (Cell: player model and
  the level rebuild of apply_own_cell, 4DA56CA5 -> 291431B5; Creature: SetSpecies, 065CBE70 -> 29143508, the
  ability bar rebuilt by bar_tick). A guest with own creations plays its own and is not touched.
* ✔ (runtime) **The world's player species** (cSpeciesManager +0xA4, a 12-byte ResourceKey read by
  `cSpeciesManager_PlayerSpecies` 0x4DF490 -> GetSpeciesProfile 0x4DF5C0 on every call, the key used when the
  asked key's instance is 0): the host's game has the new species there right after the editor (291433C0 while its
  avatar was still 065CBE70); a reloaded guest had it from the save; in place it stayed 065CBE70. It is read by
  BaseMaxHitPoints (the brain level's HP), IsPlayerSpecies (relations) and the ability bar's slots past the
  avatar's own (docs/re/creature.md). The guest now writes the host's new key there when it still holds the
  host's previous one (in a Creature frame, the key resolvable): 29143508 on both (lab 20261103-174221).
* ✔ (runtime) **The host's own avatar** takes the evolved species only with a save of the game: without the
  share's save the host played 065CBE70 for 2+ minutes after its editor although +0xA4 was 291433C0 and the
  new creation was in its My Creations (lab 20261103-173652); with the share's save (0.7.3.6) or the save the host
  now makes in place (`world::save_game`, not sent) the avatar became the new species 0.6-1.7 s after it
  (20261103-173315, 20261103-174221). ? Why the save applies it (the stage's return 0xD43DE0 applying the stored
  result, a mode round trip inside the save, the game's own autosave after the editor) was not looked up.
  ~ Inferred: a host whose share save failed or was skipped would play its old creature (BUG-0457's report?).
* ✔ (runtime) **A cinematic after every Creature evolution**: ~10 s (`busy=cinematic,input_lock,paused`, share
  wait `cinematic`), the evolved avatar is a hatchling (age 0, half HP: 5/5) for a few seconds after it, then
  age 1 (10/10). So neither "a cinematic played" nor "the age changed" marks a migration.
* ✔ (BUG-0019) **The migration** (player property 0x514A219 = 1 set by CreatureGame_AfterEditor 0xD40FA0 when the
  stage's DNA crossed a brain threshold, CRG_SpeciesMigrate, age 0 until its caption): the guest's game only gets
  it from the host's save made after the caption. Kept: the host reads the property after the BUG-0019 wait; a
  value above 0 that was not there before the editor sends the world as before ("evolved: X gets the world (a
  migration started)"). Not run in place.
* ✔ (BUG-0232) The per-guest cinematic wait is unchanged: the in-place decision is taken where the share would go
  out (after `share_wait::hold`), so it waits as long as the share did.
* ✔ (runtime, detector) NPC proxies, nests, eggs: the host's NPCs do not change species with its evolution;
  the detector (SPORE_DETECTOR=20, 4 comparisons in 20261103-174221) found only the known `gameplay_marker`,
  `game_plant`, `fruit` signatures, no `model:creature`; Cell (5 comparisons, 20261103-172813): one known
  `guest_only:cell` (BUG-0480).
* Cell: the host's own cell follows its editor without a save (its player model 4DA56CA5 -> 291431B5 in place).
* Kept as before (world sent): a guest in another world or stage (its latest state), a guest of an older
  SporeCoop (no CAP_VERSION), a Creature guest whose reported avatar species is still the host's pre-editor one
  30 s after the decision (the apply failed or `creations.follow_evolution` is off there), a migration.
  The decision is taken with the stage's adapter: `on_mode_change` runs before `g_adapter` / `g_world` follow the
  new mode (the first version read the editor's null adapter and kept every guest on the reload).

### Why the guest must be ready for the host's editor (the vote)

`h_Submit` holds the host's editor request until every player pressed "I'm ready", 60 s, or "Start now"
(docs/09-coop-design.md). It does three things: (1) every guest's own editor opens with the host's ("evolving
together": the guest's only editor turn in the stage, its DNA spent there, BUG-0466); (2) the host's game, which
simulates every NPC, stops while it is in its editor: guests in the stage then play on alone (BUG-0205 away
state) and a guest in combat would lose its NPCs' state; (3) before this change the reload after the editor
would have thrown a guest out of whatever it did, so it had to be at a safe point. With the reload gone only (1)
and (2) remain. Not changed here: dropping the wait would open the host's editor while a guest fights, and the
guest's own editor would open alone later (BUG-0318 timing, BUG-0205 away, the same vote); "Start now" and the
60 s limit already let the host go without a slow guest. A possible relaxation (the host's editor at once, the
guest's own editor offered as a button while the host is away) is a design change, not clearly safe: BUG-0516.
## The host's evolution in the Creature stage reaches the guest (2026-11-03, fix/w40-cell-evo)
<a id="creations-host-evolution"></a>

BUG-0457. GOG 2024, lane 4 (`tools/tests/coop_editor_sync.py --case host|host2`).

* CORRECTION (2026-11-03, fix/w40-cell-evo): the `?` of #creations-own-editor-dna ("a request built like
  that by the mod: the evolution was not applied") was the test's drag: the part at the palette's first
  column, row y 170, is locked in the host's evolution editor (uncollected parts are locked there too), so the
  host pressed done on an unchanged creature. Dragging the unlocked mouth (y 780) the evolution is applied:
  GetEvolutionPoints 40 -> 15 after the editor, the avatar's species 065CBE70 -> 29141F13 'Burpee' (lab
  20261103-160241, 20261103-160839), the stage's return applying the result as 0xD44545 / 0xD445AA describe.
  `coop.vote evolve` is a working evolution request.
* ✔ (runtime) Every evolution gives the host's species a **new key** (065CBE70 -> 29141FE7 -> 29141FEC in two
  evolutions in a row, lab 20261103-161211), saved in its My Creations: share_own sends a bundle with its
  records, the guest registers it under a content key (0x7C...... from owner + bundle id, new per version) and
  the host's world key 29141FEC as 0x7D...... (world package), rebuilds the puppet (appearance_changed ->
  despawn -> spawn with the new profile). The guest's puppet species = the bundle key with the host's creature
  name, in every run. ~ The screenshots (out/lane4/shots/editor_sync_host_on_guest.png, _self.png) show a red biped
  for the host on both games (by eye only).
* ✔ (disassembly) 0xD42E50 message 0x4471C60 (posted by 0xE45E0F at the end of the mating scene) builds the
  request only when `this+0xDA` (creature game base +0xDE) is set; it is set at 0xD2C709 on message
  0x287259F6 with data+0xC == 0x6455B55 (the mating-call command), cleared in the constructor 0xD3BA3F.
  The stage's return 0xD43DE0 applies a stored result (this+0x60) only when it comes back from the editor
  modes 0x1654C06 / 0xDBDBA1 (0xD44442).
* ? Not reproduced: the report's "previous version". The same-key path (a key-only bundle of a stage species
  edited in place, the guest's species manager keeping its cached profile of that key, §2.1) did not happen
  here: the evolution never kept the key.

## Save areas of My Spore Creations and the settings window (2026-11-03, fix/w40-misc)
<a id="creations-save-areas"></a>

* ✔ The app's init (`App::cAppSystem::Init` -> 0x6B3760, GOG 2024) makes the save areas with
  `Resource::Paths::CreateSaveAreaDirectoryDatabase(dirID, name, &db, areaID)` + `RegisterSaveArea(areaID, db, 0)`:
  dir id 0xA0214B = Documents\My Spore Creations: Pictures 0x11AC196, Movies 0x11AC197, Animations 0x11AC198
  (names localized, LocalizedString 0x19F76D11 / 0x5CA8D99, 0x5CA8D98, 0x5CA8D9A); dir id 0xA02151 = AppData\Spore:
  Games\Game0 0x4729A47, Preferences 0x11AC192, EditorSaves (packed) 0x11AC199..0x11AC19B, 0x90368EA0/A2/A3,
  0x86CA01C9, CityMusic 0x500EFC6, Cache / Server (Pollination.package) 0x11AC19C / 0x11AC19D, Planets 0x31389B5,
  RigblockInfo 0x49A3D83, GraphicsCache 0x11AC1AC. A failed create leaves the area unregistered:
  `Resource::GetDatabaseByID(id)` (= Paths::GetSaveArea) returns 0.
* ✔ The game settings window's builder (`SporeCoop::GameSettings_Build` 0x602070) calls
  `GetDatabaseByID(0x11AC197)->vt 0x28` (the folder path shown in the window) without a null check unless App
  property 0x61B67B6 is true; other users of 0x11AC197 (0x63B940, 0x61B730 the video upload) check for null.
* ✔ Runtime (lab, 2026-11-03): with the profile's `Documents\My Spore Creations` replaced by a plain file, the
  three areas are 0 and Options -> Settings in the galaxy menu faults at 0x6025A9 reading 0, the players' BUG-0506
  signature. Vanilla behaviour: without a `-sporekit-profile` SporeKit does not touch the Documents path (the players
  ran with "profile own"), so the cause is the players' Documents folder (missing, redirected or blocked by
  Windows' controlled folder access: inferred, not seen on their PCs).
* Fix (gamefixes.cpp, `game.save_areas`): before the builder runs, a missing area of the three gets a fallback
  directory database in AppData\Spore\<name> (dir id 0xA02151), made and registered as the init does.

## Save areas of a ru-ru install, the editor's backgrounds (2026-11-03, fix/w40-misc)
<a id="creations-save-areas-locale"></a>

GitHub issue #190 (BUG-0506), a follow-up of #creations-save-areas.

* ✔ Static: `App::cSporeApp::Init` reads `HKLM\Software\Electronic Arts\SPORE\PlayerDir` (default
  "My Spore Creations") and `AppDir` (default "Spore") and gives them to cAppSystem vt 0xC; dir id 0xA0214B (the
  Pictures / Movies / Animations save areas) is Documents\<PlayerDir>. The ru-ru Steam install writes PlayerDir
  "??? ????????" (the issue's InstallScript.vdf: the Russian name lost in a code page).
* ✔ Runtime (lane 9, 2026-11-03): with the bottle's `playerdir` set to "??? ????????" the game made none of the three
  areas (GetDatabaseByID 0x11AC196 / 0x11AC197 / 0x11AC198 all 0 in the galaxy menu) and no folder of that name.
  So the localized subfolder names are not the cause; the PlayerDir that cannot be a folder name is. Inferred: the
  ru-ru folders under My Spore Creations (localized names) that the reporter saw were made by other code (creation
  PNGs), not by these areas; switching the game to English fixed it because the installer then writes a valid
  PlayerDir. Not checked: which code makes those localized folders.
* ✔ The editor's play-mode backgrounds (`SporeCoop::PlayModeBackgrounds_LoadPictures` 0x633750, caller 0x633890)
  call GetDatabaseByID(0x11AC196 Pictures)->vt 0x5C without a null check: a read of 0 at 0x633765 when a Cell editor
  opens (the issue's 0x513765 is the same instruction in an image loaded 0x120000 lower; GOG 2024 0x633765 here).
  Reproduced in the lab with the save areas missing (coop_save_areas_editor.py, fix off).
* Fix: the fallback (`game.save_areas`) now runs right after the app's init made the areas (hook on
  `SporeCoop::SaveAreas_Init` 0x6B3760, called once by cAppSystem::Init, after coop.dll is loaded), with a log line of
  the three areas (`save areas after the game's startup: Pictures=... Movies=... Animations=...`); the first frame
  checks once more when the hook did not run (hot reload), and the settings builder keeps its check. Pictures,
  Movies and Animations are the only areas of dir 0xA0214B (the folder AnimatedAvatars on disk is made elsewhere,
  not looked at).

## The own creature's species key after an evolution editor (2026-11-03, fix/w41-creature-left)
<a id="creations-evolve-key"></a>

BUG-0517. GOG 2024, Ghidra and lane 9 (`coop_evolve_in_place.py --case creature --nosave`, suite `evolve_key`).

* ✔ (Ghidra) The stage's return 0xD43DE0, back from the editor modes 0x1654C06 / 0xDBDBA1 with a stored result
  (this+0x60, set by the result message 0x30C11C7 for the tag 0x64AA168), calls `0x4DFBC0(cSpeciesManager, avatar
  +0xB20 profile, result +0x18 new key)`: the avatar's species profile object is reloaded IN PLACE with the new
  species (0x4D4F70), both keys map to it, and the world's player species +0xA4 becomes the new key when it was the
  old one (0x4DF380). Then avatar vt 0xAC, the normalizing scale, SetEvolutionPoints(result +0x34), and vt 0xAC on
  every creature of the avatar's profile (its family). Nothing writes the creatures' own key +0xB28.
* ✔ (runtime, lab 20261104-000929, switch off, no save) After the editor the host's avatar had the new profile
  but +0xB28 = 065CBE70 (the pre-editor species) and kept it: every reader of +0xB28 (the mod's state, trace,
  evolve_inplace's check: the guest got the world after 30 s after all) saw the old species. A save writes the
  key (0.6-1.7 s, #creations-evolve-in-place); a host alone never saves then.
* `creature.evolve_key` (coop.cpp evo_key_tick): within 60 s after an evolution editor, when the avatar's profile
  key (+0xB20 -> +0x504) differs from its +0xB28 and +0xB28 is the pre-editor species, every creature with the
  avatar's profile and that key gets the profile's key (6 creatures in the lab: the avatar and its family); the
  host's in-place save of BUG-0508 is then skipped. Any role, also without a session (~ not run alone in the lab).
  `coop.evokey [nosave|save]` prints `keys_set=` and turns the host's in-place save off for a test.

## A guest's own creation in a new world; a guest's Cell parts and their twins (2026-11-04, fix/w41-cell-left)
<a id="creations-guest-other-world"></a>

BUG-0345 ("the guest spawns in the Cell stage with its previous cell creature"), BUG-0446.

* ✔ (code) Where it is chosen: `creations::mine(kind)` (creations.cpp). An explicit choice (choices.ini, the co-op
  window) wins; without one **a guest plays `g_opts[kind].front()`, the newest own creation of its My Creations
  (EditorSaves.package)**, in any world, also one it never played; only a guest without own creations plays the
  world's species (and follows the host's evolution, BUG-0508). `apply_own_cell` then swaps the guest's avatar
  5 s after each Cell load (RebuildLevel, "own cell is now ... (level rebuilt)"), `apply_own_avatar` the
  creature. The host has had the opposite rule since BUG-0344 (`creations.host_world_species`: the world's
  species unless chosen in this stage world). So "the old one comes back" is the design (every player keeps its
  own creature, coop_creations.py), not a lost choice: the previous game's cell is the newest Cell creation in
  the player's My Creations.
* ✔ (code, logs) The DNA: the Cell parts states are the world's (`S+0x34`, docs/re/cell.md#cell-parts-shared),
  not the creation's: a guest's own cell does not bring its parts, and CollectPart adds no food. The guest's
  jump on joining (`dna: food 24 -> 170 cause=meal:255+146`) is the host's shared total applied to the guest;
  the host's "+68 / 100 DNA" were the guest's meals (BUG-0510, fixed in 0.7.4 by the 1/N pace). Not seen: any
  DNA from the parts of an own creation.
* ~ superseded (2026-11-04): see the decision below. Not changed (a design decision for the owner, BUG-0345 stays open): (a) a notice on both sides that a guest
  plays its own creation from My Creations; (b) a guest's default is the world's species and its own creation
  only by choice (as the host since BUG-0344): this flips `coop_creations.py`'s feature; (c) a host setting.
* ✔ (runtime, BUG-0446, `coop_parts_shared.py --from-cell --guest-cell-part`, lane 4) A Cell part the guest
  collects reaches the host's game (`parts.shared_cell`, BUG-0467) and so the host's live Cell -> Creature start
  unlocks its creature twin; the guest that loads the host's world has it too (part 4, twin 32AC9620, both
  IsUnlocked: lab 20261104-022114). With `parts.shared_cell` off neither game has the twin (lab
  20261104-022323). Part index -> twin: table 0x1483DD8 `{part, cell item}` then 0x1485FE8 (#creations-cell-twins):
  2 FE534766, 3 3A3C49A5, 4 32AC9620, 5 B70894B6, 6 4908E576, 7 C5744537, 8 1610AD94, 9 DEF1C7FF, 10 30E66572,
  11 02B3EDDB 7E262CF0 BE830BDE; parts 0, 1 and 12 have none.
* Decision (orchestrator for the owner, 2026-11-04) and fix `creations.guest_world_species` (BUG-0345): a guest plays
  its own creation by default only when it chose it (the co-op window, choices.ini: holds in every world) or
  already played it in THIS world; else the world's species, as the host. "Played in this world": our folder's
  `SporeCoop\worlds.ini`, `[creature]` / `[cell]` `<world crc> = <creation>` (the world crc is modes::current_world,
  the crc32 of the planet file name), written when the guest's avatar has its own creation in that world, and by
  the guest's own editor (its result is recorded for the world instead of becoming a choice for every world).
  The first time per session and world a guest plays its own creation, every game gets the notice "<name> plays
  its own creature" (or cell). Off: the newest own creation in any world (0.7.4). Test commands: `coop.creations
  unchoose <kind>` (no choice), `coop.creations worlds [forget]` (the record of this world).
  Lab pitfall: worlds.ini lives in each lane's guest profile; a test that needs the guest's own creature
  chooses it and restores the profile (tools/tests/creations_state.py), so no test depends on another's order.

## cPlayer fields by name, the stages done, the time of day (2026-11-04, fix/w43-detector2)
<a id="creations-cplayer-fields"></a>

* ✔ (Ghidra, GOG 2024) the cPlayer serializer table (0x1350CA0; cPlayer = cGameNounManager +0x74) names its fields:
  +0x38 mOneTimeEventFlags, +0x698 mFlags, +0x10E8 mpCRGItems, **+0x10F0 mCurrentGoalProgress, +0x10F4
  mGoalProgressTotal** (the stage's progress bar), +0x10F8 mUniqueGameID, **+0x10FC mCellConsequenceTrait, +0x1100
  mCreatureConsequenceTrait, +0x1104 mTribeConsequenceTrait, +0x1108 mCivConsequenceTrait, +0x110C
  mSpaceConsequenceTrait** (the card each finished stage gave: 0 = the stage not done), +0x111C mGameEventRecord,
  +0x113C mSelectionGroups, +0x1150 mSelectedCityHallKey, +0x115C mSelectedVehicleKey, +0x1168 mSelectedUFOKey, +0x1178
  mCapitalCityPos, +0x1184 mStarsVisited, +0x1198 mStarsKnown, +0x11AC mWormholesUsed, +0x11C0 mPlanetData, +0x11D8
  mPlayerSpecificEmpireData, +0x11F0 mHasCheated, +0x11F8 mSpacePlayerWarData, +0x1214 mPlayerCaptureProgressStars,
  +0x1228 mEmbassies, +0x1244 mThemeID, +0x1248 mDifficultyLevel, +0x124C mMaxPosseSize, +0x1250 mTempPosseCount, +0x1258
  mSoothingSongTimer, +0x1278 mSocialTraitProgress, +0x127C mCombatTraitProgress, +0x1280 mPirateUFOModelKey, +0x128C
  mTotalTimeInCVG, +0x1290 mInitCaptainKey, +0x129C mAdventuresCompleted. (The four dwords +0x10FC..+0x1108 the editor
  request copies, #creations section 1.2 area, are these traits.)
* ~ (runtime, lane 2, tribe fixture, both games) traits 0,0,0,0,0, progress 0 of 5: the fixture was started at the Tribe
  stage (no earlier stage played); ? the trait ids of played stages are not read yet. The detector's `All:state:stage_done`
  (BUG-0577) compares done / not done per stage.
* ✔ (Ghidra) `Simulator::cTimeOfDay::Get` 0xBC3220: the singleton [0x168AA6C]; +0x24 the day's length (s,
  SetDayDuration 0xBC29A0; 960 s on the tribe fixture at runtime). The sky the mod shares (MSG_WORLD_TIME) is the planet
  context's orientation (`TimeOfDay_GetObject` +0x10, docs/re/space.md): the detector compares both (`All:state:time_of_day`,
  `All:state:day_length`, BUG-0579).
## A part or paint the other game lacks: the blocks, the shade, the lookalike part (2026-11-04, fix/w43-content)
<a id="creations-lookalike"></a>

BUG-0559 (class BUG-0424). GOG 2024, lanes 9 / 10 (`tools/tests/coop_content_lookalike.py`: Carol's lane without
`bp1content/BoosterPack_01.package` and `Data/Spore_Pack_03.package`), Ghidra, the game files' index.

* ✔ **The blocks are the species data, not the model.** A species' `creaturedata` record (0x0F43029A, the game's
  cache of what it derived from the model, §1.1) is a second copy of the model: its header names the three paint
  effects (the same 12 bytes as the bem header's byte 26, here at byte 36) and its body names each part as
  `00 60 62 40` + the part id **without byte-run packing** (33 entries for the 33 parts of Tiger Horse) followed by
  the part's data with float boxes. The host sends its own creaturedata with each species of its world that is one
  of its own creations (`kOwnSpecies`, BUG-0017: the 0xD2C361 desync). The 0.7.6 guest rewrote the part ids of the
  bem only: its game built the host's species from a creaturedata naming parts it lacks and **drew each such part as
  a box** (runtime, lane 10: the host's family and pack members made of orange boxes of each part's size, the
  model the game reads (`coop.creations partlist`) had no unknown part; the same substitute parts put into the
  host's model directly (no pack part anywhere) are drawn normally: run `CONTENT_SWAPS=08504962=CE3E4186,...`).
  No fault: the bake survives a part id it cannot find in creaturedata (the bem's unknown id faults at 0x41F3B).
* ✔ **The shade is a paint check that did not know the base game's cell paints.** The host's creature kept its
  Cell stage paint `skinpaint_cell_stripes` (0xE4C02CCE, paint prop 0xD0654338 group 0x406A0200 of
  `Spore_Game` / `PatchData`). That effect is compiled into the base game's effect directory (type 0xEA5118B0
  0xBAFC5CA1, groups 0x40450200..02) and has no 0x497767B9 record, and `kBasePaintEffects` (111 creature skin
  paints) did not list it: every game, also one WITH all packs (Bob and the host itself, `paints_fb=1`), took it
  for unknown and painted the base layer `skinpaint_base_blankShiny` (shiny plain orange in the test). Observed
  in the player's guest log too (paint E4C02CCE "unknown here", "a base game paint" fallback).
* ✔ `Swarm::cEffectsManager::GetDirectoryAndEffectIndex` 0xA6D750 (thiscall(manager, id, group) ret 8) is the
  lookup the game's effect creator makes (EffectWorld::CreateEffect 0xA71300 gives up below 0): the effect
  directories at manager+0x2C..+0x30, each with a hash map at +0x68 keyed (id, group), the answer
  `(directory << 24) | index` or -1. With (id, 0) it finds 0xE4C02CCE on every game and the pack's 0xC78AAED4 only on
  a game with the pack (runtime lane 10). `paint_known` asks it first (`content.lookalike`).
* ✔ **The resource manager's database list**: `RM+0x2C` points to the head pointer of a singly linked list of
  nodes `{next, Database*}`, highest priority first (GetDatabaseList 0x8E0380 and FindDatabase 0x8DE620 walk it,
  probing `OpenRecord` vt 0x34(key, 0, 1, 6, 1, 0)). A `DatabasePackedFile`'s GetLocation (vt 0x28) is its file.
  The mod reads the indexes of these files itself to list every part prop this game has (`catalogue`; the first
  package with an id answers, as FindRecord does), once per session at the first part it lacks: runtime (lane 10,
  without Creepy & Cute) `this game has 503 parts of group 40626000 (33 packages, 0 unreadable, 16 ms)`.
* ✔ **A part prop's property list** (type 0x00B1B104): an array iff `flags & 0x10` (count, item size, items);
  strings (type 0x12 / 0x13, flags 0x0D) are a length in characters and the characters; fixed types 0x01 1,
  0x09/0x0A/0x0D 4, 0x20 16, 0x22 0x208, 0x30 8, 0x31..0x34 16, 0x38 56, 0x39 24 bytes. All 2839 part props of
  the four part groups of the GOG 2024 install parse with this rule. CORRECTION (2026-11-04, fix/w43-content):
  `tools/part_substitutes.py` treats `flags & 0x1C` as an array, so a string with flags 0x0D was read as an
  array and the record dropped (6 of the 622 creature part props of the three packages it reads; the Creepy &
  Cute table was generated without them). The bounding box is 0x1CBB2F62 (type 0x39, six little endian floats).
* ✔ **The lookalike part** (`content.lookalike`, `mods/coop/net/partprops.*`): a sending game adds the property
  list of each part its creation's models name to the bundle (records {id, 0x00B1B104, 0x7C0DE5C1}: a group no
  game resource has, taken out before anything is registered; a 0.7.6 receiver would register them under that
  group, which nothing asks for). The receiving game, for a part it lacks, picks among the parts it has the one
  with the same role set (0xB00F0FXX) and as many 0xFE bytes (it is written in place), the nearest ability levels,
  ranks and cost (the weights of the Creepy & Cute table) and the nearest size (3 per doubling of the box
  diagonal); none of that role: the 0.7.6 chain (the Creepy & Cute table, then a base detail). Runtime: the
  host's foot of Spore_Pack_03 (0xF54F0B90, on no table) became the base foot 0x7F815C91 on Carol; with the switch
  off it became the fixed detail 0x0330D834 (the creature lost its feet on that game). The parts of Creepy & Cute
  mostly get the table's choice; one detail differs (0xC61D94A9: 0xE788026D instead of 0x0330D834).
* Fix `content.species_data`: the ids substituted in the bem (parts and the three paints) are substituted in the
  creaturedata of the same bundle; a creaturedata that still names a part this game lacks is left out (the game
  computes one). `coop.creations partlist <key>` prints each part of the model the game reads with its role here
  and `species_data=<parts> species_data_unknown=<lacking>` of the creaturedata the game finds for that key.
* ? Not verified: the lookalike choice for cell and UFO parts (the same code, no test); vehicles and buildings
  (`ensure_model`, no bundle and so no description: the 0.7.6 tables); an animation only the missing part has.
## The Creature evolution editor's DNA budget after a held request (2026-11-05, fix/w43-crashes)
<a id="creations-creature-editor-budget"></a>

BUG-0581 (not a bug). The `?` of docs/re/cell.md#cell-editor-budget (a held Creature request may lose its budget
message 0x90A03FDF the way BUG-0560 lost the Cell one): ✔ checked with recorded runs of `coop_editor_sync.py --case
host|host2`, where the host's evolution request waits for the vote (`vote active: ... wants to evolve`) before its
editor opens: a mouth (25 DNA) took the host's DNA from 40 to 15 and, in the second evolution, from 55 to 30 (lab
fix/w40-cell-evo 20261103-161211, rel/0.7.4 20261103-204638), so the editor started from the stage's DNA, not from
a config start budget, whatever the held post did. GetEvolutionPoints reads the same value inside the editor (40 -> 40
while the part is bought: the editor keeps its own count until done, the result's +0x34 then sets it).
? Not looked at: where the Creature editor takes its start budget (OnEnter or the stage's DNA); no re-post needed.
