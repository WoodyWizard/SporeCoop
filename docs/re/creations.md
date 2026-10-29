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
