#include "modes.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "cell_logic.h"
#include "game.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "world_name.h"

extern const SkHost* g_host_for_modes;
extern SkMod* g_mod_for_modes;

namespace modes {

void* (*g_species_for_player)(uint8_t player);
bool (*g_model_for_player)(uint8_t player, int kind, uint32_t key[3]);

// ---------------------------------------------------------------- creature / captain
static void* creature_local() { return game::avatar(); }

static void creature_fill(void* av, net::PlayerState& st)
{
    st.flags |= net::PF_IN_WORLD;
    st.pos = game::position(av);
    st.rot = game::orientation(av);
    st.vel = game::velocity(av);
    float sp = drive::speed(st.vel);
    if (sp > 0.3f)
        st.flags |= net::PF_MOVING;
    if (sp > 6.f)
        st.flags |= net::PF_RUNNING;
    if (game::is_dead(av))
        st.flags |= net::PF_DEAD;
    st.health = game::health(av);
    st.max_health = game::max_health(av);
    st.species = game::species_instance(av);
}

static void* creature_spawn(const net::PlayerState& s, void* av)
{
    void* own = g_species_for_player ? g_species_for_player(s.id) : nullptr;
    // one herd per species, reused (also one a save brought back): a new herd per spawn stayed in
    // the world after the puppet was gone
    void* herd = own ? game::own_herd_for(own) : nullptr;
    if (herd) {
        game::herd_home(herd, s.pos); // (a reused herd's home may be anywhere)
        game::herd_quiet(herd);
    } else if (own)
        herd = game::create_herd(s.pos, own);
    void* p = own && herd ? game::spawn_creature(s.pos, own, herd)
                          : game::spawn_creature(s.pos, game::species_profile(av), game::avatar_herd());
    if (!p)
        return nullptr;
    game::puppet_register(p);
    game::teleport(p, s.pos, s.rot);
    game::set_scale(p, game::scale(p) * 1.15f);
    return p;
}

static void creature_drive(void* p, const net::PlayerState& s, drive::State& ds, double now)
{
    // a partner's creature: lunges and charges move it faster than a puppet can walk, and attack
    // animations undo the nudges (~10-20 % of them in fights): it trailed by 5-8 m after a fight
    // move (play_creature.py 2026-10-23); snap at 4 m instead of 8
    drive::step(p, s.pos, s.rot, s.vel, ds, now, 0, true, 4.f);
    if (game::herd_of(p) != game::avatar_herd()) {
        game::herd_quiet(game::herd_of(p)); // (the game may enable a herd again)
        game::herd_home(game::herd_of(p), game::position(p));
    }
    float mh = game::max_health(p); // puppets mirror the remote health, never die locally
    if (mh > 0)
        game::set_health(p, mh);
}

static void creature_despawn(void* p) { game::destroy_creature(p); }

static float creature_tag(void* p) { return game::bounding_radius(p) * 1.3f + 0.3f; }

// adventure identity: cScenarioMode (*ptr) -> mpData (+0x74) -> mpResource (+0x10) -> resource key (+8)
static uint32_t S2(const char* n);
static uint32_t adventure_place()
{
    uintptr_t p = S2("App::cScenarioMode::ptr");
    uint8_t* sm = p ? *(uint8_t**)p : nullptr;
    if (!sm || !skg::readable(sm, 0xD0))
        return 0;
    uint8_t* data = *(uint8_t**)(sm + 0x74);
    uint8_t* res = data && skg::readable(data, 0x14) ? *(uint8_t**)(data + 0x10) : nullptr;
    if (!res || !skg::readable(res, 0x14))
        return 0;
    uint32_t inst = *(uint32_t*)(res + 8), group = *(uint32_t*)(res + 16);
    return (inst ^ (group * 0x9E3779B1u)) | 1u;
}

// ---------------------------------------------------------------- space: UFOs
// cGameDataUFO: cGameData 0x00, cLocomotiveObject 0x34 (pos +0x38, rot +0x44, vel +0x1FC),
// cCombatant 0x508 (hp +0x540, max +0x534, invincible +0x588)
constexpr uintptr_t kUfoLoco = 0x34, kUfoCombat = 0x508;

static uintptr_t S(const char* n) { return g_host_for_modes ? g_host_for_modes->sym(n) : 0; }
static uint32_t S2(const char* n) { return (uint32_t)S(n); }

static uint8_t* space_player_data()
{
    uintptr_t p = S("Simulator::SpacePlayerData::ptr");
    return p ? *(uint8_t**)p : nullptr;
}

static void* space_local()
{
    uintptr_t get = S("Simulator::cSimulatorPlayerUFO::Get");
    void* mgr = get ? skg::cdecl_call<void*>(get) : nullptr;
    return mgr ? skg::field<void*>(mgr, 0x40) : nullptr;
}

static uint32_t space_place()
{
    uint8_t* spd = space_player_data();
    if (!spd)
        return 0;
    int context = *(int*)(spd + 0x10); // 0 planet, 1 solar system, 2 galaxy
    void* planet = *(void**)(spd + 0x04);
    void* star = *(void**)(spd + 0x08);
    if (context == 0 && planet) {
        void* rec = skg::field<void*>(planet, 0x13C);                    // cPlanet::mpPlanetRecord
        return rec ? (skg::field<uint32_t>(rec, 0x184) | 0x40000000u) : 0; // PlanetID
    }
    if (context == 1 && star) {
        void* rec = skg::field<void*>(star, 0x48);                // cStar::mpStarRecord
        return rec ? (skg::field<uint32_t>(rec, 0x70) | 0x80000000u) : 0; // StarID
    }
    return 0; // galaxy view: not shown
}

static void space_fill(void* u, net::PlayerState& st)
{
    st.flags |= net::PF_IN_WORLD;
    auto& p = skg::field<skg::Vector3>(u, kUfoLoco + 0x04);
    auto& q = skg::field<skg::Quaternion>(u, kUfoLoco + 0x10);
    auto& v = skg::field<skg::Vector3>(u, kUfoLoco + 0x1C8);
    st.pos = {p.x, p.y, p.z};
    st.rot = {q.x, q.y, q.z, q.w};
    st.vel = {v.x, v.y, v.z};
    if (drive::speed(st.vel) > 0.3f)
        st.flags |= net::PF_MOVING;
    st.health = skg::field<float>(u, kUfoCombat + 0x38);
    st.max_health = skg::field<float>(u, kUfoCombat + 0x2C);
}

bool ufo_ident_fix = true;
COOP_FIX("fleet.type", "BUG-0111", ufo_ident_fix, "space: only our own puppets count as puppets, not any UfoType 6 ship (coop.fleet typefix)");
// our puppets: the model key they got and (BUG-0218) their object serial (cGameData mID)
struct UfoIdent {
    skg::ResourceKey key;
    uint32_t serial;
};
static std::unordered_map<void*, UfoIdent> g_ufo_ident;
// BUG-0218: the identity is the object serial, not the flags. Our puppet whose invincible byte the
// game cleared gets it back; our puppet that a game mission took (owner mission +0x800) is retired
// (held still, never sent or fired, destroyed once the mission lets go). Off: the 0.7.3.0 check
// (flags in the identity, such a ship forgotten and left in the game as an armed NPC).
bool puppet_id_fix = true;
COOP_FIX("fleet.puppet_id", "BUG-0218", puppet_id_fix,
         "space: a puppet is ours by its object serial; a cleared flag is restored, a mission's ship retired");
struct Retired {
    void* u;
    uint32_t serial;
};
static std::vector<Retired> g_retired;
static std::unordered_set<void*> g_fake_mission; // tests (coop.fleet impostor_mission): as if +0x800 were set
uint32_t g_puppet_flag_restored, g_puppet_retired, g_retired_destroyed, g_retired_gone, g_retired_left;
static void* owner_mission(void* u) { return g_fake_mission.count(u) ? u : skg::field<void*>(u, 0x800); }

static void* space_spawn(const net::PlayerState& s, void* local)
{
    uintptr_t spawn = S("Simulator::SpawnUFO"), empid = S("Simulator::GetPlayerEmpireID"),
              getemp = S("Simulator::GetPlayerEmpire");
    if (!spawn || !empid || !getemp)
        return nullptr;
    uint32_t pol = skg::cdecl_call<uint32_t>(empid);
    void* emp = skg::cdecl_call<void*>(getemp);
    if (!emp)
        return nullptr;
    skg::ResourceKey model = skg::field<skg::ResourceKey>(emp, 0xB8); // cEmpire::mUFOKey
    uint32_t own[3];
    if (g_model_for_player && g_model_for_player(s.id, 2, own)) // the player's own ship
        model = {own[0], own[1], own[2]};
    // UfoType 6 = Defender: friendly to its political id (ours)
    void* u = skg::cdecl_call<void*>(spawn, 6, &pol, &model);
    if (!u)
        return nullptr;
    skg::field<uint8_t>(u, kUfoCombat + 0x80) = 1; // mbInvincible
    g_ufo_ident[u] = {skg::field<skg::ResourceKey>(u, 0x780), game::object_serial(u)}; // mDesiredModelKey (BUG-0111), mID (BUG-0218)
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    skg::Quaternion q{s.rot.x, s.rot.y, s.rot.z, s.rot.w};
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x44 / 4, &p, &q); // Teleport
    (void)local;
    return u;
}

static void space_drive(void* u, const net::PlayerState& s, drive::State&, double)
{
    // UFOs fly freely: place the puppet exactly where the remote ship is, every frame
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    skg::Quaternion q{s.rot.x, s.rot.y, s.rot.z, s.rot.w};
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x38 / 4, &p); // SetPosition
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x3C / 4, &q); // SetOrientation
    skg::field<skg::Vector3>(u, kUfoLoco + 0x1C8) = {s.vel.x, s.vel.y, s.vel.z};
    // the UFO update steers towards mNextPosition/mDestination: pin them to the remote state
    skg::field<skg::Vector3>(u, 0x718) = p;    // mNextPosition
    skg::field<skg::Vector3>(u, 0x724) = {s.vel.x, s.vel.y, s.vel.z}; // mNextVelocity
    skg::field<skg::Quaternion>(u, 0x730) = q; // mNextOrientation
    skg::field<skg::Vector3>(u, 0x750) = p;    // mDestination
    skg::field<uint8_t>(u, 0x74C) = 1;         // mbAtDestination
    float mh = skg::field<float>(u, kUfoCombat + 0x2C);
    if (mh > 0)
        skg::field<float>(u, kUfoCombat + 0x38) = mh;
}

// DestroyInstance works for any cGameData; a puppet the game freed already (leaving a planet or a
// system destroys every NPC UFO, the puppets too) is only forgotten
static void space_despawn(void* u)
{
    g_ufo_ident.erase(u);
    if (game::ufo_listed(u)) {
        game::destroy_creature(u);
    } else {
        game::g_ufos_freed++;
        game::puppet_unregister(u);
    }
}

// the game removes NPC UFOs on its own: driving a freed puppet crashed the host (play_space.py,
// 2026-10-23: AV in space_drive, vtable overwritten)
// (a listed pointer may be a new UFO at a reused address: it must still be a Defender, type +0x714)
// BUG-0111: and it must be the UFO space_spawn made: a game Defender (an empire's or a colony's)
// allocated at the freed puppet's address is not ours (model key, invincible, no owner mission)
bool space_puppet_ok(void* u)
{
    if (!u || !game::ufo_listed(u) || !game::object_alive(u) || skg::field<int>(u, 0x714) != 6)
        return false;
    if (!ufo_ident_fix)
        return true;
    auto it = g_ufo_ident.find(u);
    if (it == g_ufo_ident.end())
        return false;
    const skg::ResourceKey& k = skg::field<skg::ResourceKey>(u, 0x780);
    if (puppet_id_fix) // BUG-0218: the same object (serial), not taken by a game mission
        return game::object_serial(u) == it->second.serial && !memcmp(&k, &it->second.key, sizeof k) &&
               !owner_mission(u);
    return !memcmp(&k, &it->second.key, sizeof k) && skg::field<uint8_t>(u, kUfoCombat + 0x80) == 1 &&
           !skg::field<void*>(u, 0x800);
}

static bool space_alive(void* u)
{
    if (space_puppet_ok(u)) {
        if (puppet_id_fix && ufo_ident_fix && skg::field<uint8_t>(u, kUfoCombat + 0x80) != 1) {
            // our own puppet lost its invincibility (the game? only the test command is known to do
            // it): give it back, count it, log the first one
            skg::field<uint8_t>(u, kUfoCombat + 0x80) = 1;
            if (!g_puppet_flag_restored++)
                g_host_for_modes->log(g_mod_for_modes, SK_WARN, "space: a puppet's invincible byte was cleared: restored (mode %08X)",
                       game::mode_id());
        }
        return true;
    }
    auto it = ufo_ident_fix ? g_ufo_ident.find(u) : g_ufo_ident.end();
    if (it != g_ufo_ident.end()) {
        uint32_t serial = it->second.serial;
        g_ufo_ident.erase(it);
        if (puppet_id_fix && game::ufo_listed(u) && game::object_alive(u) && game::object_serial(u) == serial &&
            owner_mission(u)) {
            // our ship, held by a game mission: never freed while the mission points to it; retired
            g_retired.push_back({u, serial});
            g_puppet_retired++;
            g_host_for_modes->log(g_mod_for_modes, SK_INFO, "space: a game mission took a puppet: retired until the mission lets go");
        }
        game::puppet_unregister(u); // (a different serial: another object at the address, forgotten, never destroyed)
    }
    game::g_ufos_freed++;
    return false;
}

bool space_retired(void* u)
{
    for (auto& r : g_retired)
        if (r.u == u)
            return true;
    return false;
}

void space_retired_tick()
{
    for (size_t i = 0; i < g_retired.size();) {
        void* u = g_retired[i].u;
        if (!game::ufo_listed(u) || !game::object_alive(u) || game::object_serial(u) != g_retired[i].serial) {
            g_retired_gone++; // the game freed it (another object may be at the address now)
            g_fake_mission.erase(u);
            g_retired.erase(g_retired.begin() + i);
            continue;
        }
        if (!owner_mission(u)) { // the mission let go: ours to destroy (the deferred path)
            g_retired_destroyed++;
            g_retired.erase(g_retired.begin() + i);
            game::destroy_creature(u);
            continue;
        }
        // held still where it is (its own controller would fly it)
        skg::Vector3 p = skg::field<skg::Vector3>(u, kUfoLoco + 0x04);
        skg::field<skg::Vector3>(u, 0x718) = p;
        skg::field<skg::Vector3>(u, 0x724) = {0, 0, 0};
        skg::field<skg::Vector3>(u, kUfoLoco + 0x1C8) = {0, 0, 0};
        skg::field<skg::Vector3>(u, 0x750) = p;
        i++;
    }
}

void space_retired_clear(const char* why)
{
    if (!g_retired.empty()) {
        g_retired_left += (uint32_t)g_retired.size();
        if (g_host_for_modes)
            g_host_for_modes->log(g_mod_for_modes, SK_WARN, "space: %u retired puppet(s) still held by a mission at %s: left to the game",
                   (unsigned)g_retired.size(), why);
    }
    g_retired.clear();
    g_fake_mission.clear();
}

std::string space_ident_test(void* u, const std::string& what, bool on)
{
    auto it = g_ufo_ident.find(u);
    if (what == "impostor_id") { // another object at the address: the recorded serial no longer matches
        if (it == g_ufo_ident.end())
            return "not a puppet";
        it->second.serial ^= 0x80000000u;
        return "puppet serial changed";
    }
    if (what == "impostor_mission") { // a game mission holds the ship (+0x800 seen as set), or lets go
        if (on)
            g_fake_mission.insert(u);
        else
            g_fake_mission.erase(u);
        return on ? "mission set" : "mission cleared";
    }
    char b[160];
    snprintf(b, sizeof b, "flag_restored=%u retired=%u (now %u) retired_destroyed=%u retired_gone=%u retired_left=%u",
             g_puppet_flag_restored, g_puppet_retired, (unsigned)g_retired.size(), g_retired_destroyed, g_retired_gone,
             g_retired_left);
    return b;
}

static float space_tag(void*) { return 4.f; }

// ---------------------------------------------------------------- cell stage
// cCellGame (*0x16B4BE4): mCells pool at +0x1C (mpData +0, object size +0x14), mpCellQuery +0x40FC,
// mAvatarCellIndex +0x411C. cCellObjectData (0x398 bytes): index +0, mIsIdle +4, mTargetPosition +8,
// mTransform +0x48 (offset +0x4C, scale +0x58, rotation 3x3 +0x5C), mCellResource +0x108,
// mIsInvulnerable +0x111, mChaseCellIndex +0x22C, mHealthPoints +0x244, mScale +0x358.
uint8_t* cell_game()
{
    uintptr_t p = S("Simulator::Cell::cCellGame::_ptr");
    return p ? *(uint8_t**)p : nullptr;
}

// cObjectPoolIndex = slot (low 16 bits) | generation (high 16); the object's first field holds
// its own full index while it lives (cObjectPool::GetIfNotDeleted 0xB72290)
// particle arrays of the cell soft bodies: positions at +4, velocities at +0x4654 = 4 + 1500*12
constexpr uint32_t kCellParticles = 1500;

bool cell_at_fix = true;
COOP_FIX_UNSAFE("pool.cellat", "BUG-0083", cell_at_fix, "cell: modes::cell_at refuses the slot past the pool's used range");

// BUG-0271: on the way Cell -> Creature the game frees the cell pool's memory (3.7 MB, its own
// mapping) while mode_id() still reads Cell, and later points +0x1C at junk (docs/re/cell.md 13).
// Our console commands and the keepalive's network update also run between frames, from the game's
// message pump inside that teardown (the host's coop.status read a freed puppet there). So a cell
// is read only from pool memory that is still mapped: VirtualQuery over the whole block, checked
// once per frame scope of ours (no game code runs inside it) and on every call outside one. The
// local cell and the puppets go through the same pool check (cell_usable) before cell_fill reads
// them; coop.cpp's local_now() / puppet_now() do the same for every mode before a command or a
// network callback reads the entities our last frame found. `coop.fix core.stale_local off`: the old
// reads (tests).
bool stale_fix = true;
COOP_FIX("core.stale_local", "BUG-0271", stale_fix, "core: no cell, local entity or puppet read between frames once the game freed or changed the stage (Cell -> Creature)");
static int g_scope_depth;
static uint8_t* g_scope_data;   // the block found mapped in this scope
static uint32_t g_scope_bytes;
static CellGoneStats g_gone;
static uint8_t* g_gone_logged; // the freed block already logged

void cell_scope(bool enter)
{
    if (enter) {
        if (g_scope_depth++ == 0)
            g_scope_data = nullptr; // the game ran since the last scope: check again
    } else if (g_scope_depth > 0) {
        g_scope_depth--;
    }
}

// [p, p + n) is committed, readable memory (one or more regions)
static bool mapped(const uint8_t* p, size_t n)
{
    const uint8_t* end = p + n;
    while (p < end) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        p = (const uint8_t*)mi.BaseAddress + mi.RegionSize;
    }
    return true;
}

// the cell game's pool block is the game's live memory (with the fix off: always yes)
static bool pool_mapped(uint8_t* data, uint32_t bytes)
{
    if (!stale_fix)
        return true;
    if (g_scope_depth > 0 && data == g_scope_data && bytes == g_scope_bytes)
        return true;
    g_gone.checks++;
    if (g_scope_depth == 0)
        g_gone.outside++;
    bool ok = mapped(data, bytes);
    if (ok && g_scope_depth > 0) {
        g_scope_data = data;
        g_scope_bytes = bytes;
    }
    if (!ok) {
        g_gone.pool_gone++;
        if (data != g_gone_logged) {
            g_gone_logged = data;
            g_host_for_modes->log(g_mod_for_modes, SK_WARN,
                                  "cell: the pool block %p (%u bytes) is no longer mapped (mode 0x%08X, %s a frame): no cell read from it",
                                  data, bytes, game::mode_id(), g_scope_depth ? "inside" : "outside");
        }
    }
    return ok;
}

// once per frame (diagnostic, whatever the switch): in the Cell stage, is the pool block still
// mapped? Logs when it goes away while the mode still reads Cell and how long that lasts.
void cell_probe_tick()
{
    static bool gone = false;
    static double gone_at = 0;
    uint8_t* g = game::mode_id() == kCell ? cell_game() : nullptr;
    if (!g) {
        if (gone)
            g_host_for_modes->log(g_mod_for_modes, SK_INFO, "cell: the mode left Cell %.3f s after the pool block was freed (%u frames)",
                                  g_host_for_modes->time_seconds() - gone_at, g_gone.frames_gone);
        gone = false;
        return;
    }
    uint8_t* data = *(uint8_t**)(g + 0x1C);
    uint32_t cap = *(uint32_t*)(g + 0x1C + 0x18);
    bool ok = data && mapped(data, (cap && cap <= 4096 ? cap : 4096) * 0x398);
    if (!ok) {
        g_gone.frames_gone++;
        if (!gone) {
            gone = true;
            gone_at = g_host_for_modes->time_seconds();
            g_gone.windows++;
            g_host_for_modes->log(g_mod_for_modes, SK_WARN, "cell: frame with the pool block %p unmapped while the mode reads Cell",
                                  data);
        }
    } else if (gone) {
        gone = false; // a new block (the pool was rebuilt)
    }
}

uint8_t* cell_at(uint32_t idx)
{
    uint8_t* g = cell_game();
    uint32_t slot = idx & 0xFFFF;
    if (!g || !idx || slot >= 4096)
        return nullptr;
    uint8_t* data = *(uint8_t**)(g + 0x1C);
    uint32_t size = *(uint32_t*)(g + 0x1C + 0x14);
    uint32_t cap = *(uint32_t*)(g + 0x1C + 0x18);
    // live slots are [0, highWater) inside [0, capacity) (docs/re/cell.md 0, BUG-0083)
    if (!data || size != 0x398 ||
        !cell_logic::slot_in_pool(slot, *(uint32_t*)(g + 0x1C + 0x10), cap, cell_at_fix))
        return nullptr;
    if (!pool_mapped(data, (cap && cap <= 4096 ? cap : 4096) * size))
        return nullptr;
    uint8_t* c = data + (size_t)slot * size;
    return *(uint32_t*)c == idx ? c : nullptr;
}

// A cell with a soft body (GFX +0x248 set, particle count +0x250 > 1) gets +0x4C and +0x90 back
// from its particles every cell step (0xE7C080 -> 0xE553B0): particle i = *(u32*)(c+0x254+4i),
// position at parts+4+12i, velocity at parts+0x4654+12i (parts = *(G+0x4108)). Moving the cell
// moves the particle positions; `still` also stops them (a teleport). Writing the velocities
// instead (as this did until 2026-10-22) made the cell glide there and overshoot by metres.
void cell_move(uint8_t* c, const net::Vec3& p, bool still)
{
    auto* pos = (float*)(c + 0x4C);
    float d[3] = {p.x - pos[0], p.y - pos[1], p.z - pos[2]};
    int n = *(int*)(c + 0x250);
    uint8_t* g = cell_game();
    uint8_t* parts = g ? *(uint8_t**)(g + 0x4108) : nullptr;
    if (*(uint32_t*)(c + 0x248) && parts && n > 1 && n <= 64) // +0x254..+0x353: at most 64 indices
        for (int i = 0; i < n; i++) {
            uint32_t k = *(uint32_t*)(c + 0x254 + 4 * i);
            if (k >= kCellParticles)
                continue;
            auto* q = (float*)(parts + 4 + 12 * (size_t)k);
            q[0] += d[0]; q[1] += d[1]; q[2] += d[2];
            if (still) {
                auto* v = (float*)(parts + 0x4654 + 12 * (size_t)k);
                v[0] = v[1] = v[2] = 0;
            }
        }
    pos[0] = p.x; pos[1] = p.y; pos[2] = p.z;
    auto* prev = (float*)(c + 0x84);
    prev[0] = p.x; prev[1] = p.y; prev[2] = p.z;
    *(uint16_t*)(c + 0x48) |= 4; // offset dirty
    (*(uint16_t*)(c + 0x4A))++;
}

static std::unordered_map<void*, uint32_t> g_cell_ids; // puppet -> its pool index

// The cell step moves and turns every cell (a puppet turns towards where it swims); poses written
// at the end of the frame were overridden before the next drawing. The last pose of each puppet is
// applied again right after the step (SimUpdate), so what is drawn is the remote pose.
struct CellPose {
    net::Vec3 pos;
    float rot[9];
};
static std::unordered_map<void*, CellPose> g_cell_pose;
typedef void(__cdecl* SimUpdate_t)(float);
static SimUpdate_t o_SimUpdate;
static uint8_t* cell_at_checked(void* cp);

static void __cdecl h_SimUpdate(float dt)
{
    o_SimUpdate(dt);
    for (auto it = g_cell_pose.begin(); it != g_cell_pose.end();) {
        uint8_t* c = cell_at_checked(it->first);
        if (!c) {
            it = g_cell_pose.erase(it);
            continue;
        }
        cell_move(c, it->second.pos);
        memcpy(c + 0x5C, it->second.rot, sizeof it->second.rot);
        *(uint16_t*)(c + 0x48) |= 2; // rotation dirty
        ++it;
    }
}

static void* cell_local()
{
    uint8_t* g = cell_game();
    return g ? cell_at(*(uint32_t*)(g + 0x411C)) : nullptr;
}

void mat_to_quat(const float* m, net::Quat& q) // row-major 3x3; unit length, identity for a zero matrix (BUG-0160)
{
    cell_logic::Quat r = cell_logic::mat_to_quat(m);
    q.x = r.x, q.y = r.y, q.z = r.z, q.w = r.w;
}

void quat_to_mat(const net::Quat& q, float* m)
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z,
          wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    m[0] = 1 - 2 * (yy + zz); m[1] = 2 * (xy - wz);     m[2] = 2 * (xz + wy);
    m[3] = 2 * (xy + wz);     m[4] = 1 - 2 * (xx + zz); m[5] = 2 * (yz - wx);
    m[6] = 2 * (xz - wy);     m[7] = 2 * (yz + wx);     m[8] = 1 - 2 * (xx + yy);
}

// BUG-0271: the local cell or a puppet is still a live cell of a mapped pool (the caller may hold it
// from an earlier frame: g_local, a puppet, read by a command between frames)
bool cell_usable(void* cp)
{
    if (!cp)
        return false;
    if (!stale_fix)
        return true;
    return cp == cell_local() || cell_at_checked(cp);
}

const CellGoneStats& cell_gone_stats() { return g_gone; }

static void cell_fill(void* cp, net::PlayerState& st)
{
    uint8_t* c = (uint8_t*)cp;
    if (!cell_usable(cp)) { // BUG-0271: nothing is read, the state says "not in the world"
        g_gone.refused++;
        return;
    }
    auto* pos = (float*)(c + 0x4C);
    st.flags |= net::PF_IN_WORLD;
    st.pos = {pos[0], pos[1], pos[2]};
    mat_to_quat((float*)(c + 0x5C), st.rot);
    auto* tgt = (float*)(c + 0x08);
    net::Vec3 d{tgt[0] - pos[0], tgt[1] - pos[1], tgt[2] - pos[2]};
    if (!*(bool*)(c + 4) && drive::speed(d) > 0.05f)
        st.flags |= net::PF_MOVING;
    auto* vel = (float*)(c + 0x90);
    st.vel = {vel[0], vel[1], vel[2]};
    st.health = (float)*(int*)(c + 0x244);
    st.max_health = 6;
    if (st.health <= 0)
        st.flags |= net::PF_DEAD;
    st.species = *(uint32_t*)(c + 0x58); // transform scale (cell size) travels in the species slot
}

// CreateCellObject asks GetModelKeyForCellResource for the model; the avatar's resource
// ("PlayerCreature") maps to the species picked in the creator, not to the evolved cell the
// player has now. While spawning a puppet the hook answers with the avatar's current model.
typedef skg::ResourceKey*(__cdecl* GetModelKey_t)(skg::ResourceKey*, void*);
static GetModelKey_t o_GetModelKey;
static bool g_force_key;
static skg::ResourceKey g_key;

static skg::ResourceKey* __cdecl h_GetModelKey(skg::ResourceKey* ret, void* res)
{
    if (g_force_key) {
        *ret = g_key;
        return ret;
    }
    return o_GetModelKey(ret, res);
}

bool cell_is_puppet(void* c) { return g_cell_ids.count(c) != 0; }

// The Cell stage was left (an editor, the Creature stage, a load): the pool the puppets lived in is
// rebuilt or gone. Stale entries could match a new cell at the same address and index in the next
// pool (h_SimUpdate would then pose an NPC as the partner); coop.cpp forgets the puppets themselves.
void cell_forget()
{
    g_cell_ids.clear();
    g_cell_pose.clear();
}

void cell_destroy(uint32_t idx)
{
    // DestroyCell(idx, spawnLoot, float, int): the game's own removal (setting hp 0 does not remove)
    if (uintptr_t a = S("Simulator::Cell::DestroyCell"))
        if (cell_at(idx))
            skg::cdecl_call<void>(a, idx, 0, 0.f, 0);
}

static uint8_t* cell_at_checked(void* cp)
{
    auto it = g_cell_ids.find(cp);
    return it != g_cell_ids.end() && cell_at(it->second) == cp ? (uint8_t*)cp : nullptr;
}

static void ensure_model_hook()
{
    if (!o_SimUpdate) {
        uintptr_t a = S("Simulator::Cell::SimUpdate");
        if (a)
            g_host_for_modes->hook_create(g_mod_for_modes, (void*)a, (void*)h_SimUpdate, (void**)&o_SimUpdate);
    }
    if (!o_GetModelKey) {
        uintptr_t a = S("Simulator::Cell::GetModelKeyForCellResource");
        if (a)
            g_host_for_modes->hook_create(g_mod_for_modes, (void*)a, (void*)h_GetModelKey, (void**)&o_GetModelKey);
    }
}

uint32_t cell_create(const net::Vec3& pos, void* res_ref, int level, const uint32_t key[3], const net::Quat* rot)
{
    ensure_model_hook();
    uint8_t* g = cell_game();
    uintptr_t create = S("Simulator::Cell::CreateCellObject");
    if (!g || !res_ref || !create)
        return 0;
    skg::Vector3 p{pos.x, pos.y, pos.z};
    skg::Quaternion q{0, 0, 0, 1};
    if (rot)
        q = {rot->x, rot->y, rot->z, rot->w};
    g_key = {key[0], key[1], key[2]};
    g_force_key = o_GetModelKey && key[0] != 0;
    // (query, pos, elevation, ref, level, sizeFactor, cellSize 0 = auto, applySize, orientation, collision group -1)
    uint32_t idx = skg::cdecl_call<uint32_t>(create, *(void**)(g + 0x40FC), &p, 0.f, res_ref, level, 1.f, 0.f, 1,
                                             (void*)&q, -1);
    g_force_key = false;
    return cell_at(idx) ? idx : 0;
}

static void* cell_spawn(const net::PlayerState& s, void* local)
{
    ensure_model_hook();
    uint8_t* g = cell_game();
    uint8_t* me = (uint8_t*)local;
    uintptr_t create = S("Simulator::Cell::CreateCellObject");
    if (!g || !me || !create)
        return nullptr;
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    void* query = *(void**)(g + 0x40FC);
    void* res = *(void**)(me + 0x108);
    // the avatar's own mScale is -1 ("player"); other cells with -1 are not drawn: use the level the
    // player is at (GetCurrentAdvectInfo reports the current CellStageScale; cell_drive corrects it)
    int scale = 1;
    if (uintptr_t adv = S("Simulator::Cell::GetCurrentAdvectInfo")) {
        int lvl = -1;
        float a, b, c;
        skg::cdecl_call<uint32_t>(adv, &lvl, &a, &b, &c);
        if (lvl >= 0 && lvl < 8)
            scale = lvl;
    }
    g_key = skg::field<skg::ResourceKey>(me, 0xFC);
    uint32_t own[3];
    if (g_model_for_player && g_model_for_player(s.id, 1, own)) // the player's own cell
        g_key = {own[0], own[1], own[2]};
    g_force_key = o_GetModelKey && g_key.instance != 0;
    uint32_t idx = skg::cdecl_call<uint32_t>(create, query, &p, 0.f, res, scale, 1.f, 1.f, 1, (void*)nullptr, 0);
    g_force_key = false;
    uint8_t* c = cell_at(idx);
    if (!c)
        return nullptr;
    g_cell_ids[c] = idx;
    *(bool*)(c + 0x111) = true; // invulnerable: nobody eats a friend
    *(uint8_t*)(c + 0x354) = 1; // we own the lifetime: not removed outside the local active area
    return c;
}

static void cell_drive(void* cp, const net::PlayerState& s, drive::State&, double)
{
    uint8_t* c = (uint8_t*)cp;
    // exactly where the remote cell is (interpolated), every frame; the target ahead along its
    // velocity keeps the swimming animation going between our writes
    cell_move(c, s.pos);
    bool moving = (s.flags & net::PF_MOVING) != 0;
    auto* tgt = (float*)(c + 0x08);
    tgt[0] = s.pos.x + (moving ? s.vel.x * 0.3f : 0.f);
    tgt[1] = s.pos.y + (moving ? s.vel.y * 0.3f : 0.f);
    tgt[2] = s.pos.z;
    *(bool*)(c + 4) = !moving;
    // facing: the rotation itself, not only the target the game turns towards; applied again
    // after the next cell step (h_SimUpdate), before the frame is drawn
    quat_to_mat(s.rot, (float*)(c + 0x5C));
    *(uint16_t*)(c + 0x48) |= 2; // rotation dirty
    CellPose& pose = g_cell_pose[cp];
    pose.pos = s.pos;
    memcpy(pose.rot, c + 0x5C, sizeof pose.rot);
    skg::field<skg::Quaternion>(c, 0x14) = {s.rot.x, s.rot.y, s.rot.z, s.rot.w}; // mTargetOrientation
    *(uint8_t*)(c + 0x354) = 1;
    float size;
    memcpy(&size, &s.species, 4);
    if (size > 0.01f && size < 1000.f) {
        *(float*)(c + 0x58) = size;
        *(float*)(c + 0xB4) = size; // mTargetSize
    }
    // Keep the puppet at the players' level (+0x358). It used to keep the level of the moment it
    // was created: after a few level-ups the host's NPCs judged the partner several levels smaller
    // than them (scale difference 0, IsPrey 0xE68910 says "too small to hunt") and hunted only the
    // host's own cell (docs/re/cell.md 9, docs/known-issues.md history).
    // The game reads the avatar's -1 as the level of the food progress (GetScaleDifference
    // 0xE57400 -> 0xE4EE50); GetCurrentAdvectInfo, which the spawn uses, does not always agree (8
    // for 7 at food 600). Ask the game: the scale difference avatar -> puppet is 2 at the same level.
    uint8_t* g = cell_game();
    uint8_t* av = g ? cell_at(*(uint32_t*)(g + 0x411C)) : nullptr;
    if (uintptr_t diff = S("Simulator::Cell::GetScaleDifference"); av && av != c && diff) {
        int d = skg::cdecl_call<int>(diff, av, c); // clamped to 0..4: converges within a few frames
        int lvl = *(int*)(c + 0x358) + 2 - d;
        if (d != 2 && lvl >= 0 && lvl < 32)
            *(int*)(c + 0x358) = lvl;
    }
    *(int*)(c + 0x22C) = -1; // never chase anything
    *(int*)(c + 0x220) = -1; // nor flee
    *(int*)(c + 0x244) = 6;
    *(bool*)(c + 0x111) = true;
}

static bool cell_alive(void* cp)
{
    auto it = g_cell_ids.find(cp);
    bool ok = it != g_cell_ids.end() && cell_at(it->second) == cp;
    if (!ok && it != g_cell_ids.end())
        g_cell_ids.erase(it);
    return ok;
}

static void cell_despawn(void* cp)
{
    if (!cell_alive(cp))
        return;
    uint32_t idx = g_cell_ids[cp];
    g_cell_ids.erase(cp);
    g_cell_pose.erase(cp);
    *(bool*)((uint8_t*)cp + 0x111) = false;
    cell_destroy(idx); // hp 0 or opacity 0 would only hide it (docs/re/cell.md §5)
}

static float cell_tag(void* cp) { return *(float*)((uint8_t*)cp + 0x58) * 1.2f + 0.5f; }

// ---------------------------------------------------------------- tribe / civilization
// RTS stages have no single avatar: a player is shown as a pointer - the point of the planet
// under their mouse - with the name tag. Both play the same (host) world, so the tribe or the
// cities themselves already exist on every machine.
struct Marker {
    net::Vec3 pos;
};
static Marker g_local_marker;

static void* noun_manager()
{
    uintptr_t get = S("Simulator::cGameNounManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

// a point on the planet surface of the player's community (sets the ray-cast sphere radius)
static bool rts_anchor(net::Vec3& p)
{
    void* nm = noun_manager();
    if (!nm)
        return false;
    void* tribe = skg::field<void*>(nm, 0x70);
    if (tribe && skg::readable(tribe, 0x130)) {
        auto v = skg::field<skg::Vector3>(tribe, 0x124);
        p = {v.x, v.y, v.z};
        return drive::speed(p) > 1;
    }
    uintptr_t f = S("Simulator::cGameNounManager::GetPlayerCivilization");
    void* civ = f ? skg::thiscall<void*>(f, nm) : nullptr;
    if (civ && skg::readable(civ, 0xA8)) {
        void** b = skg::field<void**>(civ, 0x9C); // mCities (vector begin)
        void** e = skg::field<void**>(civ, 0xA0);
        if (b && e > b && skg::readable(b, 4) && *b && skg::readable(*b, 0x130)) {
            auto v = skg::field<skg::Vector3>(*b, 0x124);
            p = {v.x, v.y, v.z};
            return drive::speed(p) > 1;
        }
    }
    return false;
}

// tests (BUG-0223): frames in which Tribe's / Civ's local entity is null, as when the anchor city
// cannot be read for a moment
static int g_test_null_frames;
static bool g_test_null_now;
void test_null_local(int frames) { g_test_null_frames = frames; }
void test_tick()
{
    g_test_null_now = g_test_null_frames > 0;
    if (g_test_null_now)
        g_test_null_frames--;
}

static void* rts_local()
{
    if (g_test_null_now)
        return nullptr;
    net::Vec3 a;
    return rts_anchor(a) ? &g_local_marker : nullptr;
}

// planet point under the mouse (ray from the game camera against the planet sphere)
static bool mouse_on_planet(net::Vec3& out)
{
    net::Vec3 anchor;
    if (!rts_anchor(anchor))
        return false;
    HWND w = (HWND)g_host_for_modes->game_window();
    POINT pt;
    if (!w || !GetCursorPos(&pt) || !ScreenToClient(w, &pt))
        return false;
    uintptr_t mmget = S("App::IGameModeManager::Get"), ray = S("App::cViewer::GetCameraToPoint");
    void* mm = mmget ? skg::cdecl_call<void*>(mmget) : nullptr;
    void* viewer = mm ? skg::vcall<void*>(mm, 0x58 / 4) : nullptr;
    if (!viewer || !ray)
        return false;
    skg::Vector3 o{}, d{};
    if (!skg::thiscall<bool>(ray, viewer, (float)pt.x, (float)pt.y, &o, &d))
        return false;
    float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dl < 1e-6f)
        return false;
    d.x /= dl; d.y /= dl; d.z /= dl;
    float R = drive::speed(anchor);
    float b = o.x * d.x + o.y * d.y + o.z * d.z;
    float c = o.x * o.x + o.y * o.y + o.z * o.z - R * R;
    float disc = b * b - c;
    if (disc < 0)
        return false;
    float t = -b - std::sqrt(disc);
    if (t < 0)
        t = -b + std::sqrt(disc);
    if (t < 0)
        return false;
    out = {o.x + d.x * t, o.y + d.y * t, o.z + d.z * t};
    return true;
}

static void rts_fill(void* m, net::PlayerState& st)
{
    Marker* mk = (Marker*)m;
    net::Vec3 p;
    if (mk == &g_local_marker && mouse_on_planet(p))
        mk->pos = p;
    st.flags |= net::PF_IN_WORLD;
    st.pos = mk->pos;
}

static void* rts_spawn(const net::PlayerState& s, void*)
{
    auto* m = new Marker;
    m->pos = s.pos;
    return m;
}

static void rts_drive(void* m, const net::PlayerState& s, drive::State&, double) { ((Marker*)m)->pos = s.pos; }
static void rts_despawn(void* m) { delete (Marker*)m; }
static float rts_tag(void*) { return 0.5f; }

static const Adapter kAdapters[] = {
    {"Tribe", kTribe, nullptr, false, rts_local, rts_fill, rts_spawn, rts_drive, rts_despawn, rts_tag},
    {"Civilization", kCiv, nullptr, false, rts_local, rts_fill, rts_spawn, rts_drive, rts_despawn, rts_tag},
    {"Cell", kCell, nullptr, false, cell_local, cell_fill, cell_spawn, cell_drive, cell_despawn, cell_tag, cell_alive, true},
    {"Creature", kCreature, nullptr, false, creature_local, creature_fill, creature_spawn, creature_drive, creature_despawn, creature_tag},
    // adventures are not saves: every player starts the same adventure, puppets meet inside it
    {"Adventure", kScenario, adventure_place, true, creature_local, creature_fill, creature_spawn, creature_drive, creature_despawn, creature_tag},
    {"Space", kSpace, space_place, false, space_local, space_fill, space_spawn, space_drive, space_despawn, space_tag, space_alive},
};

net::Vec3 entity_pos(const Adapter* a, void* e)
{
    net::PlayerState st;
    if (a && e)
        a->fill(e, st);
    return st.pos;
}

const Adapter* for_mode(uint32_t mode)
{
    for (auto& a : kAdapters)
        if (a.mode == mode)
            return &a;
    return nullptr;
}

const char* mode_name(uint32_t m)
{
    switch (m) {
    case kCell: return "Cell";
    case kCreature: return "Creature";
    case kTribe: return "Tribe";
    case kCiv: return "Civilization";
    case kSpace: return "Space";
    case kEdit: case kEditor: return "Editor";
    case kLoad: return "Loading";
    case kScenario: return "Adventure";
    case kMenu: return "Galaxy menu";
    case 0: return "-";
    default: return "other";
    }
}

// BUG-0258: after the live Creature -> Tribe change the game's name reads "snapshot.spo" until the
// next save; the planet before it is still the one played (world_name.h)
static bool g_snapshot_name_fix = true;
COOP_FIX("world.snapshot_name", "BUG-0258", g_snapshot_name_fix, "world: a host whose save name reads snapshot.spo after the Tribe intro keeps its planet's place");
static std::mutex g_world_name_mx;
static world_name::State g_world_name;

std::string raw_world_name()
{
    uintptr_t a = g_host_for_modes ? g_host_for_modes->sym("SporeCoop::CurrentGameName") : 0;
    if (!a)
        return {};
    const char16_t* b = skg::field<const char16_t*>((void*)a, 0);
    const char16_t* e = skg::field<const char16_t*>((void*)a, 4);
    if (!b || e < b || e - b > 260 || !skg::readable(b, (e - b) * 2))
        return {};
    // UTF-8, the way world.cpp names the files it sends: a player may type any name for a new planet
    // ("Земля"), and the save file has that name. Non-ASCII letters used to become '_', so the host did
    // not find the file of the planet it played and sent another one (docs/07-modes.md 7.2).
    int n = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)b, (int)(e - b), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n : 0, 0);
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)b, (int)(e - b), out.data(), n, nullptr, nullptr);
    return out;
}

std::string current_world_name()
{
    std::string raw = raw_world_name();
    std::lock_guard<std::mutex> lk(g_world_name_mx);
    return world_name::effective(g_world_name, raw, g_snapshot_name_fix);
}

// once per frame: the memory follows the game; every change of the game's own name is logged
// (BUG-0258: when "snapshot.spo" comes and goes)
void world_name_tick()
{
    static std::string seen;
    std::string raw = raw_world_name();
    std::string eff;
    {
        std::lock_guard<std::mutex> lk(g_world_name_mx);
        eff = world_name::effective(g_world_name, raw, g_snapshot_name_fix);
    }
    if (raw != seen) {
        if (g_host_for_modes && g_mod_for_modes)
            g_host_for_modes->log(g_mod_for_modes, SK_INFO, "world: the game's save name is now '%s' (mode %08X; the place uses '%s')",
                                  raw.c_str(), game::mode_id(), eff.c_str());
        seen = raw;
    }
}

uint32_t current_place(const Adapter* a)
{
    if (a && a->place)
        return a->place();
    return current_world();
}

uint32_t current_world()
{
    std::string n = current_world_name();
    return n.empty() ? 0 : net::crc32_update(0, (const uint8_t*)n.data(), n.size());
}

} // namespace modes

const SkHost* g_host_for_modes;
SkMod* g_mod_for_modes;
