#include "squadron.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "game.h"
#include "modes.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/validate.h"
#include "space_rivalry.h"
#include "spacecoop.h"
#include "sporekit_game.hpp"
#include "fixes.h"

using skg::thiscall;
using skg::vcall;

namespace squadron {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;
static uintptr_t a_GetAllUFOs, a_TakeDamage, a_FireTool, a_SpawnUFO, a_PlayerUFOGet, a_PlayerEmpireID;

// ---- cGameDataUFO (docs/re/space.md) ----
constexpr uintptr_t kLoco = 0x34, kPos = 0x38, kRot = 0x44, kCombat = 0x508, kMaxHp = 0x534, kState = 0x53C,
                    kHp = 0x540, kInvincible = 0x588, kType = 0x714, kNextPos = 0x718, kNextVel = 0x724,
                    kNextRot = 0x730, kAtDest = 0x74C, kDest = 0x750, kModel = 0x780, kPol = 0x30;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
static bool in_space() { return g_ok && game::mode_id() == modes::kSpace; }

static void* player_ufo()
{
    void* mgr = a_PlayerUFOGet ? skg::cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    return mgr ? F<void*>(mgr, 0x40) : nullptr;
}

static std::vector<void*> all_ufos()
{
    std::vector<void*> r;
    void* nm = game::noun_manager();
    void* v = nm ? thiscall<void*>(a_GetAllUFOs, nm) : nullptr;
    if (!v)
        return r;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    for (void** it = b; it && it < e && r.size() < 256; ++it)
        if (*it && game::object_alive(*it) && F<int>(*it, kState) != 2)
            r.push_back(*it);
    return r;
}

static net::Vec3 pos_of(void* u)
{
    auto& p = F<float[3]>(u, kPos);
    return {p[0], p[1], p[2]};
}

// ---------------- host ----------------
static std::unordered_map<void*, uint16_t> g_hid;
static std::unordered_map<uint16_t, void*> g_hid_rev;
static uint16_t g_next_id = 1;
static double g_last_send;
static uint16_t g_cycle;
static uint8_t g_drop_chunk = 0xFF; // tests (coop.fleet drop <chunk> <n>): skip sending that chunk n times
static int g_drop_left;
static uint32_t g_hits_in, g_damage_out, g_puppet_shots;

// ---------------- guest ----------------
struct Mirror {
    void* u = nullptr;
    net::Vec3 pos;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    net::VelocityEstimate est; // ship snapshots carry no velocity
};
static std::unordered_map<uint16_t, Mirror> g_mirrors;
static std::unordered_map<void*, uint16_t> g_mirror_of;
static std::unordered_set<uint16_t> g_cycle_ids;
static uint16_t g_rx_cycle = 0xFFFF;
static bool g_rx_any;         // a cycle was seen in this session (g_rx_cycle is valid)
static uint32_t g_rx_mask;    // BUG-0037: the chunks of g_rx_cycle that arrived (chunks <= 15)
static bool g_rx_done;        // the removal pass of g_rx_cycle ran
// BUG-0037: the removal pass (explosion of every mirror missing from the cycle) ran on the last
// chunk even when an earlier chunk of the cycle was lost or late, and a late chunk of an older
// cycle reset the cycle. `coop.fleet chunkfix 0` restores that (tests).
static bool g_chunk_fix = true;
COOP_FIX("fleet.chunk", "BUG-0037", g_chunk_fix, "space: the mirror removal pass runs only on a complete cycle of ship chunks");
// BUG-0084: the guest's own escort (posse, type 3 ships) is not the host's to simulate.
// `coop.fleet keepposse 0` restores the old loop that destroyed it (tests).
static bool g_keep_posse = true;
COOP_FIX("fleet.posse", "BUG-0084", g_keep_posse, "space: the guest's own escort is not destroyed as a host mirror (coop.fleet keepposse)");
static uint32_t g_stale_chunks, g_partial_cycles, g_posse_kept;
static bool g_applying;
static uint32_t g_created, g_removed, g_hits_sent, g_damage_in, g_local_removed, g_mirror_hits;

// ---------------- hooks ----------------
using TakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const float*, void*);
static TakeDamage_t o_TakeDamage;
static int SK_THISCALL h_TakeDamage(void* combat, float dmg, uint32_t pol, int type, const float* dir, void* attacker)
{
    if (in_space() && (uint8_t*)combat - kCombat == player_ufo()) // rescue: a lethal hit may only disable us
        dmg = spacecoop::filter_own_damage((uint8_t*)combat - kCombat, dmg, type);
    if (!in_space() || g_applying || !C.sess || !C.sess->connected())
        return o_TakeDamage(combat, dmg, pol, type, dir, attacker);
    void* u = (uint8_t*)combat - kCombat;
    void* me = player_ufo();
    void* att = attacker ? (uint8_t*)attacker - kCombat : nullptr;
    if (rivalry::on_hit(u, att, dmg, type, dir))
        return 0; // our hit on the ship of a player at war with us: its owner takes it (space_rivalry.cpp)
    if (is_guest()) {
        auto it = g_mirror_of.find(u);
        if (it != g_mirror_of.end()) { // a mirror is hit: only our shots count, the host applies them
            g_mirror_hits++;
            if (me && att == me && type != 10) {
                net::Writer w;
                w.u16(it->second);
                w.f32(dmg);
                w.u32((uint32_t)type);
                w.f32(dir ? dir[0] : 0); w.f32(dir ? dir[1] : 0); w.f32(dir ? dir[2] : 0);
                C.sess->send_reliable_to(0, net::MSG_UFO_HIT, w);
                g_hits_sent++;
            }
            return 0;
        }
        if (u == me && att && g_mirror_of.count(att))
            return 0; // the host decides when its ships hit us (MSG_UFO_DAMAGE)
    }
    if (is_host() && C.puppet_owner) {
        int owner = C.puppet_owner(u);
        if (owner > 0 && att && C.puppet_owner(att) < 0 && att != me) {
            // an NPC ship hit a guest's ship: the guest takes it on its own ship
            net::Writer w;
            w.f32(dmg);
            w.u32((uint32_t)type);
            w.f32(dir ? dir[0] : 0); w.f32(dir ? dir[1] : 0); w.f32(dir ? dir[2] : 0);
            auto it = g_hid.find(att);
            w.u16(it == g_hid.end() ? 0xFFFF : it->second);
            C.sess->send_reliable_to((uint8_t)owner, net::MSG_UFO_DAMAGE, w);
            g_damage_out++;
        }
    }
    return o_TakeDamage(combat, dmg, pol, type, dir, attacker);
}

// mirrors never fire on their own (the host's ships shoot on the host)
using FireTool_t = int(SK_THISCALL*)(void*, void*, void*, const float*, int, int, void*, int);
static FireTool_t o_FireTool;
static int SK_THISCALL h_FireTool(void* mgr, void* tool, void* owner, const float* aim, int dp, int a5, void* target,
                                  int show)
{
    if (owner && is_guest() && in_space() && g_mirror_of.count((uint8_t*)owner - kLoco))
        return 0;
    if (owner && spacecoop::disabled() && (uint8_t*)owner - kLoco == player_ufo()) // a disabled ship can't shoot
        return 0;
    // the partners' ships here are Defender UFOs: their AI shot at every hostile ship in range on
    // its own (pirates near the guest's puppet died on the host while the guest did not fire);
    // only the partner's real shots count (MSG_UFO_HIT)
    if (owner && in_space() &&
        ((C.puppet_owner && C.puppet_owner((uint8_t*)owner - kLoco) >= 0) || modes::space_retired((uint8_t*)owner - kLoco))) {
        g_puppet_shots++;
        return 0;
    }
    return o_FireTool(mgr, tool, owner, aim, dp, a5, target, show);
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetAllUFOs = H->sym("Simulator::GetAllUFOs");
    a_TakeDamage = H->sym("Simulator::cGameDataUFO::TakeDamage");
    a_FireTool = H->sym("Simulator::cToolManager::FireTool");
    a_SpawnUFO = H->sym("Simulator::SpawnUFO");
    a_PlayerUFOGet = H->sym("Simulator::cSimulatorPlayerUFO::Get");
    a_PlayerEmpireID = H->sym("Simulator::GetPlayerEmpireID");
    g_ok = a_GetAllUFOs && a_TakeDamage && a_FireTool && a_SpawnUFO && a_PlayerUFOGet && a_PlayerEmpireID;
    if (!g_ok) {
        H->log(M, SK_WARN, "squadron: symbols missing, space co-op disabled");
        return false;
    }
    H->hook_create(M, (void*)a_TakeDamage, (void*)h_TakeDamage, (void**)&o_TakeDamage);
    H->hook_create(M, (void*)a_FireTool, (void*)h_FireTool, (void**)&o_FireTool);
    return true;
}

// ---------------- host ----------------
static void host_tick(double now)
{
    if (now - g_last_send < 0.1)
        return;
    g_last_send = now;
    void* me = player_ufo();
    std::vector<void*> ships;
    for (void* u : all_ufos())
        if (u != me && F<int>(u, kType) != 0 && (!C.puppet_owner || C.puppet_owner(u) < 0) &&
            !modes::space_retired(u)) // BUG-0218: a retired puppet is not one of our NPC ships
            ships.push_back(u);
    std::unordered_set<void*> alive(ships.begin(), ships.end());
    for (auto it = g_hid.begin(); it != g_hid.end();) {
        if (!alive.count(it->first)) {
            g_hid_rev.erase(it->second);
            it = g_hid.erase(it);
        } else
            ++it;
    }
    for (void* u : ships)
        if (!g_hid.count(u)) {
            uint16_t id = net::alloc_id(g_next_id, g_hid_rev); // BUG-0154: never a live id, never 0
            if (!id)
                continue;
            g_hid[u] = id;
            g_hid_rev[id] = u;
        }
    g_cycle++;
    size_t per = 18, chunks = std::max<size_t>(1, (ships.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        size_t from = k * per, to = std::min(ships.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            void* u = ships[i];
            net::Vec3 p = pos_of(u);
            auto& q = F<float[4]>(u, kRot);
            auto* key = &F<uint32_t>(u, kModel);
            w.u16(g_hid[u]);
            w.u8((uint8_t)F<int>(u, kType));
            w.u32(F<uint32_t>(u, kPol));
            w.u32(key[0]); w.u32(key[1]); w.u32(key[2]);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(q[0]); w.f32(q[1]); w.f32(q[2]); w.f32(q[3]);
            w.f32(F<float>(u, kHp));
            w.f32(F<float>(u, kMaxHp));
        }
        w.u32(net::now_ms()); // BUG-0122: the host's time of the snapshot (velocity on the guest)
        if (g_drop_left > 0 && k == g_drop_chunk) { // tests: a lost packet
            g_drop_left--;
            continue;
        }
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_UFO_STATE, w);
    }
}

// ---------------- guest ----------------
// the game frees NPC UFOs on its own (every context switch: DestroyNPCUFOs), the mirrors too:
// a pointer is only used while it is in the UFO noun list
static bool live(void* u)
{
    if (u && game::ufo_listed(u) && game::object_alive(u))
        return true;
    if (u)
        game::g_ufos_freed++;
    return false;
}

// BUG-0054: a ship handed to the game's TakeDamage as the attacker must still exist. The game frees
// mirrors and puppets on every context switch; a hit message handled in the frame after that (the
// messages run before squadron::tick drops the freed pointers) passed a freed object, which the
// game reads and AddRefs (AddHostileUnit). `coop.fleet hitfix 0` restores the old unchecked path.
static bool g_hit_fix = true;
COOP_FIX_UNSAFE("fleet.hit", "BUG-0054", g_hit_fix, "space: a hit message never passes a freed ship to TakeDamage");
static uint16_t g_hold_mirror = 0xFFFF; // tests (coop.fleet freemirror): this freed mirror stays listed
static void* g_stale_puppet;            // tests (coop.fleet freepuppet): the next MSG_UFO_HIT's attacker
static uint32_t g_att_dropped, g_att_freed;

static void* hit_attacker(void* a)
{
    if (a && g_hit_fix && (!live(a) || F<int>(a, kState) == 2)) {
        g_att_dropped++;
        a = nullptr;
    }
    if (a && !game::ufo_listed(a)) // what the game gets: counted for the tests, with the fix off
        g_att_freed++;
    return a;
}

static void* hit_puppet(uint8_t from)
{
    void* p = C.puppet_of ? C.puppet_of(from) : nullptr;
    if (g_stale_puppet) {
        p = g_stale_puppet;
        g_stale_puppet = nullptr;
    }
    if (p && g_hit_fix && live(p) && !modes::space_puppet_ok(p)) // only the mod's player puppets (BUG-0111)
        p = nullptr, g_att_dropped++;
    return hit_attacker(p);
}

static void destroy_mirror(void* u, bool explode)
{
    if (!live(u))
        return;
    g_applying = true;
    if (explode) { // the game's own death: explosion, removal
        F<uint8_t>(u, kInvincible) = 0;
        float dir[3] = {0, 0, 1};
        o_TakeDamage((uint8_t*)u + kCombat, 1e6f, 0xFFFFFFFFu, 0, dir, nullptr);
    } else
        game::destroy_creature(u); // DestroyInstance works for any cGameData
    g_applying = false;
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), n = r.u8();
    if (!r.ok() || !C.with_host || !C.with_host())
        return; // the host's ships are mirrored only where the host is
    if (g_chunk_fix) {
        if (!chunks || chunks > 31 || chunk >= chunks)
            return;
        int16_t age = (int16_t)(uint16_t)(cycle - g_rx_cycle);
        if (g_rx_any && age < 0 && age > -100) { // a late chunk of an older cycle (> -100: not a restart)
            g_stale_chunks++;
            return;
        }
        if (!g_rx_any || cycle != g_rx_cycle) {
            if (g_rx_any && !g_rx_done)
                g_partial_cycles++; // the previous cycle never completed: nothing was removed for it
            g_rx_any = true;
            g_rx_cycle = cycle;
            g_rx_mask = 0;
            g_rx_done = false;
            g_cycle_ids.clear();
        }
    } else if (cycle != g_rx_cycle) {
        g_rx_cycle = cycle;
        g_cycle_ids.clear();
    }
    double now = H->time_seconds();
    double sent = net::snapshot_time(r, (size_t)n * 55); // 55 bytes per ship
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        int type = r.u8();
        uint32_t pol = r.u32(), key[3];
        key[0] = r.u32(); key[1] = r.u32(); key[2] = r.u32();
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        net::Quat q;
        q.x = r.f32(); q.y = r.f32(); q.z = r.f32(); q.w = r.f32();
        float hp = r.f32(), mx = r.f32();
        (void)mx;
        if (!r.ok())
            return;
        g_cycle_ids.insert(id);
        auto& m = g_mirrors[id];
        m.pos = p; m.rot = q; m.hp = hp; m.seen = now;
        m.est.update(p, now, sent);
        if (id == g_hold_mirror)
            continue; // tests: keep the freed pointer until the next MSG_UFO_DAMAGE
        if (m.u && (!live(m.u) || F<int>(m.u, kState) == 2)) {
            g_mirror_of.erase(m.u);
            m.u = nullptr;
        }
        if (!m.u) {
            void* u = skg::cdecl_call<void*>(a_SpawnUFO, type, &pol, (void*)key);
            if (!u)
                continue;
            m.u = u;
            g_mirror_of[u] = id;
            float pp[3] = {p.x, p.y, p.z}, qq[4] = {q.x, q.y, q.z, q.w};
            vcall<void>((uint8_t*)u + kLoco, 0x44 / 4, (const float*)pp, (const float*)qq); // Teleport
            g_created++;
        }
    }
    if (g_chunk_fix) { // the removal pass needs every chunk of the cycle, in any order, once
        g_rx_mask |= 1u << chunk;
        if (g_rx_done || g_rx_mask != (chunks >= 32 ? 0xFFFFFFFFu : (1u << chunks) - 1))
            return;
        g_rx_done = true;
    } else if (chunk + 1 < chunks)
        return;
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.u) {
                g_mirror_of.erase(it->second.u);
                destroy_mirror(it->second.u, true);
                g_removed++;
            }
            it = g_mirrors.erase(it);
        } else
            ++it;
    }
    // our own NPC ships in the host's place: the host's are the real ones
    void* me = player_ufo();
    for (void* u : all_ufos()) {
        if (u == me || F<int>(u, kType) == 0 || g_mirror_of.count(u))
            continue;
        // the mod's player puppets (Defenders) stay; the game's own Defenders (an empire's system, a
        // colony's) go like the other NPC ships: the host's come as mirrors (BUG-0111: every type 6
        // was kept, and the guest saw each defender twice). coop.fleet typefix 0: the old check
        if (modes::ufo_ident_fix ? (C.puppet_owner && C.puppet_owner(u) >= 0) : F<int>(u, kType) == 6)
            continue;
        if (modes::space_retired(u)) // BUG-0218: a game mission holds it, never freed here
            continue;
        if (g_keep_posse && F<int>(u, kType) == 3) {
            // type 3 is the posse (DestroyNPCUFOs keeps exactly type 3 when keepPosse is set): our
            // own escort, whatever empire its ships came from, not one of the host's NPCs
            g_posse_kept++;
            continue;
        }
        destroy_mirror(u, false);
        g_local_removed++;
    }
}

static void guest_tick(double now)
{
    bool here = C.with_host && C.with_host();
    for (auto& [id, m] : g_mirrors) {
        if (id == g_hold_mirror)
            continue; // tests: the one frame between the free and this tick, held open
        if (m.u && !live(m.u)) { // freed by the game: spawned again with the next state
            g_mirror_of.erase(m.u);
            m.u = nullptr;
        }
        if (!m.u)
            continue;
        if (!here || now - m.seen > 3.0)
            continue;
        net::Vec3 at = m.est.at(now); // moved on with the estimated velocity (fast ships)
        float p[3] = {at.x, at.y, at.z}, q[4] = {m.rot.x, m.rot.y, m.rot.z, m.rot.w};
        vcall<void>((uint8_t*)m.u + kLoco, 0x38 / 4, (const float*)p); // SetPosition
        vcall<void>((uint8_t*)m.u + kLoco, 0x3C / 4, (const float*)q); // SetOrientation
        // the UFO's own controller steers to mNextPosition/mDestination: pin them
        auto set3 = [&](uintptr_t off) {
            auto& d = F<float[3]>(m.u, off);
            d[0] = p[0]; d[1] = p[1]; d[2] = p[2];
        };
        set3(kNextPos);
        set3(kDest);
        auto& nv = F<float[3]>(m.u, kNextVel);
        nv[0] = nv[1] = nv[2] = 0;
        auto& nr = F<float[4]>(m.u, kNextRot);
        for (int i = 0; i < 4; i++)
            nr[i] = q[i];
        F<uint8_t>(m.u, kAtDest) = 1;
        F<float>(m.u, kHp) = m.hp > 0 ? m.hp : 1;
    }
    if (!here && !g_mirrors.empty()) { // we left the host's place: drop the mirrors
        for (auto& [id, m] : g_mirrors)
            if (m.u) {
                g_mirror_of.erase(m.u);
                destroy_mirror(m.u, false);
            }
        g_mirrors.clear();
    }
}

void tick(double now)
{
    modes::space_retired_tick(); // BUG-0218 (reads the UFO list only, before it touches a ship)
    if (!in_space() || !C.sess || !C.sess->connected() || !player_ufo())
        return;
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!in_space() || !player_ufo())
        return;
    if (type == net::MSG_UFO_STATE && is_guest()) {
        guest_state(r);
    } else if (type == net::MSG_UFO_HIT && is_host()) {
        uint16_t id = r.u16();
        float dmg = r.f32();
        int t = (int)r.u32();
        float dir[3] = {r.f32(), r.f32(), r.f32()};
        auto it = g_hid_rev.find(id);
        if (!r.ok() || it == g_hid_rev.end() || !live(it->second) || F<int>(it->second, kState) == 2)
            return;
        dmg = net::sane_damage(dmg, 100000.f); // a guest's report: finite and not negative
        if (!net::finite3(dir))
            dir[0] = dir[1] = dir[2] = 0;
        void* puppet = hit_puppet(from);
        g_applying = true;
        o_TakeDamage((uint8_t*)it->second + kCombat, dmg, skg::cdecl_call<uint32_t>(a_PlayerEmpireID), t, dir,
                     puppet ? (uint8_t*)puppet + kCombat : nullptr);
        g_applying = false;
        g_hits_in++;
    } else if (type == net::MSG_UFO_DAMAGE && is_guest()) {
        float dmg = r.f32();
        int t = (int)r.u32();
        float dir[3] = {r.f32(), r.f32(), r.f32()};
        uint16_t att = r.u16();
        void* me = player_ufo();
        if (!r.ok() || !me)
            return;
        auto it = g_mirrors.find(att);
        void* a = hit_attacker(it != g_mirrors.end() ? it->second.u : nullptr);
        g_hold_mirror = 0xFFFF;
        // through the hook (g_applying: straight to the game after the own-ship filter): a lethal hit
        // next to a partner only disables the ship (spacecoop rescue). Calling the original here
        // skipped that, and the guest's ship died in real fights with the partner next to it.
        g_applying = true;
        h_TakeDamage((uint8_t*)me + kCombat, dmg, a ? F<uint32_t>(a, kPol) : 0xFFFFFFFFu, t, dir,
                     a ? (uint8_t*)a + kCombat : nullptr);
        g_applying = false;
        g_damage_in++;
    }
}

void on_session_end()
{
    for (auto& [id, m] : g_mirrors)
        if (m.u)
            destroy_mirror(m.u, false);
    g_mirrors.clear();
    g_mirror_of.clear();
    g_hid.clear();
    g_hid_rev.clear();
    g_hold_mirror = 0xFFFF;
    g_stale_puppet = nullptr;
    modes::space_retired_clear("session end");
    g_cycle_ids.clear();
    g_rx_any = false;
    g_rx_mask = 0;
    g_rx_done = false;
    g_drop_left = 0;
}

std::string status()
{
    char b[420];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b,
                 "space(host): ships=%u hits_in=%u damage_out=%u freed_by_game=%u puppet_shots_blocked=%u "
                 "att_dropped=%u att_freed=%u\n",
                 (unsigned)g_hid.size(), g_hits_in, g_damage_out, game::g_ufos_freed, g_puppet_shots, g_att_dropped,
                 g_att_freed);
    else
        snprintf(b, sizeof b,
                 "space(guest): mirrors=%u created=%u removed=%u local_removed=%u hits_sent=%u damage_in=%u "
                 "freed_by_game=%u puppet_shots_blocked=%u mirror_hits=%u att_dropped=%u att_freed=%u stale_chunks=%u "
                 "partial_cycles=%u posse_kept=%u\n",
                 (unsigned)g_mirrors.size(), g_created, g_removed, g_local_removed, g_hits_sent, g_damage_in,
                 game::g_ufos_freed, g_puppet_shots, g_mirror_hits, g_att_dropped, g_att_freed, g_stale_chunks,
                 g_partial_cycles, g_posse_kept);
    return b;
}

std::string debug(const std::vector<std::string>& a)
{
    void* me = player_ufo();
    if (!me || !in_space())
        return "not in space";
    if (!a.empty() && a[0] == "list") {
        std::string out;
        char b[200];
        for (void* u : all_ufos()) {
            net::Vec3 p = pos_of(u);
            int id = -1;
            if (g_hid.count(u))
                id = g_hid[u];
            else if (g_mirror_of.count(u))
                id = g_mirror_of[u];
            snprintf(b, sizeof b, "  ship #%d %p type=%d pol=%08X hp=%.0f/%.0f%s pos=(%.0f %.0f %.0f)\n", id, u,
                     F<int>(u, kType), F<uint32_t>(u, kPol), F<float>(u, kHp), F<float>(u, kMaxHp),
                     u == me ? " (me)" : "", p.x, p.y, p.z);
            out += b;
        }
        return status() + out;
    }
    if (a.size() >= 3 && a[0] == "shoot") { // shoot <id> <dmg>: our ship hits ship #id (through the hook)
        void* t = nullptr;
        uint16_t id = (uint16_t)atoi(a[1].c_str());
        if (is_guest()) {
            auto it = g_mirrors.find(id);
            t = it == g_mirrors.end() ? nullptr : it->second.u;
        } else {
            auto it = g_hid_rev.find(id);
            t = it == g_hid_rev.end() ? nullptr : it->second;
        }
        if (!t || !live(t))
            return "no such ship";
        float dir[3] = {0, 0, 1};
        h_TakeDamage((uint8_t*)t + kCombat, (float)atof(a[2].c_str()), skg::cdecl_call<uint32_t>(a_PlayerEmpireID), 0,
                     dir, (uint8_t*)me + kCombat);
        return "shot";
    }
    if (a.size() >= 2 && a[0] == "spawn") { // spawn <type> [own]: a ship without an empire (pirate) next to
        // us, or with our own empire's political id (`own`: type 3 = an escort ship of ours, BUG-0084)
        uint32_t pol = a.size() >= 3 && a[2] == "own" ? skg::cdecl_call<uint32_t>(a_PlayerEmpireID) : 0xFFFFFFFFu;
        void* u = skg::cdecl_call<void*>(a_SpawnUFO, atoi(a[1].c_str()), &pol, (void*)&F<uint32_t>(me, kModel));
        if (!u)
            return "spawn failed";
        net::Vec3 p = pos_of(me);
        float pp[3] = {p.x + 30.f, p.y, p.z + 10.f};
        auto& q = F<float[4]>(me, kRot);
        vcall<void>((uint8_t*)u + kLoco, 0x44 / 4, (const float*)pp, (const float*)q);
        char b[64];
        snprintf(b, sizeof b, "spawned %p type=%d pol=%08X", u, F<int>(u, kType), F<uint32_t>(u, kPol));
        return b;
    }
    if (a.size() >= 3 && a[0] == "hurt") { // host: ship #id hits player <p>'s puppet [with dmg]
        auto it = g_hid_rev.find((uint16_t)atoi(a[1].c_str()));
        void* pup = C.puppet_of ? C.puppet_of((uint8_t)atoi(a[2].c_str())) : nullptr;
        if (it == g_hid_rev.end() || !pup || !live(it->second) || !live(pup))
            return "no such ship or puppet";
        float dir[3] = {0, 0, 1};
        h_TakeDamage((uint8_t*)pup + kCombat, a.size() > 3 ? (float)atof(a[3].c_str()) : 5.f,
                     F<uint32_t>(it->second, kPol), 0, dir, (uint8_t*)it->second + kCombat);
        return "hit sent";
    }
    if (a.size() >= 2 && a[0] == "chunkfix") { // BUG-0037: 0 = the removal pass on the last chunk (tests)
        g_chunk_fix = a[1] != "0";
        return std::string("chunkfix ") + (g_chunk_fix ? "1" : "0");
    }
    if (a.size() >= 2 && a[0] == "keepposse") { // BUG-0084: 0 = the guest destroys its own escort (tests)
        g_keep_posse = a[1] != "0";
        return std::string("keepposse ") + (g_keep_posse ? "1" : "0");
    }
    if (a.size() >= 3 && a[0] == "drop") { // tests (BUG-0037, host): skip the next n sends of chunk k
        g_drop_chunk = (uint8_t)atoi(a[1].c_str());
        g_drop_left = atoi(a[2].c_str());
        return "dropping";
    }
    if (a.size() >= 2 && (a[0] == "impostor_id" || a[0] == "impostor_mission")) { // tests (BUG-0218)
        void* pup = C.puppet_of ? C.puppet_of((uint8_t)atoi(a[1].c_str())) : nullptr;
        if (a[0] == "impostor_mission" && a.size() >= 4) // impostor_mission <p> 0 <ptr>: a retired ship lets go
            pup = (void*)(uintptr_t)strtoull(a[3].c_str(), nullptr, 16);
        if (!pup)
            return "no puppet";
        std::string r = modes::space_ident_test(pup, a[0], a.size() < 3 || a[2] != "0");
        char b[64];
        snprintf(b, sizeof b, " (%p)", pup);
        return r + b;
    }
    if (a.size() >= 1 && a[0] == "ident") // tests (BUG-0218): the identity counters
        return modes::space_ident_test(nullptr, "", false);
    if (a.size() >= 2 && (a[0] == "puppet" || a[0] == "impostor")) { // tests (BUG-0111): player <p>'s puppet;
        // impostor: make it vulnerable, as a game Defender made at the freed puppet's address is (another
        // model key would do too, but an invalid one costs every mirror of the ship a caught fault)
        void* pup = C.puppet_of ? C.puppet_of((uint8_t)atoi(a[1].c_str())) : nullptr;
        if (!pup || !live(pup))
            return "no puppet";
        if (a[0] == "impostor")
            F<uint8_t>(pup, kInvincible) = 0;
        char b[120];
        snprintf(b, sizeof b, "puppet %p type=%d pol=%08X model=%08X invincible=%d", pup, F<int>(pup, kType),
                 F<uint32_t>(pup, kPol), F<uint32_t>(pup, kModel), (int)F<uint8_t>(pup, kInvincible));
        return b;
    }
    if (a.size() >= 2 && a[0] == "typefix") { // BUG-0111: 0 = any listed Defender is a partner's puppet (tests)
        modes::ufo_ident_fix = a[1] != "0";
        return std::string("typefix ") + (modes::ufo_ident_fix ? "1" : "0");
    }
    if (a.size() >= 2 && a[0] == "hitfix") { // BUG-0054: 0 = the old unchecked attacker (tests)
        g_hit_fix = a[1] != "0";
        return std::string("hitfix ") + (g_hit_fix ? "1" : "0");
    }
    if (a.size() >= 2 && a[0] == "freemirror") { // tests (BUG-0054): free mirror #id as a context switch
        // does, and keep its pointer listed until the next MSG_UFO_DAMAGE (the one-frame window)
        auto it = g_mirrors.find((uint16_t)atoi(a[1].c_str()));
        if (!is_guest() || it == g_mirrors.end() || !it->second.u || !live(it->second.u))
            return "no such mirror";
        g_hold_mirror = it->first;
        game::destroy_creature(it->second.u);
        return live(it->second.u) ? "still listed" : "freed";
    }
    if (a.size() >= 2 && a[0] == "freepuppet") { // tests (BUG-0054, host): free player <p>'s puppet; the
        // next MSG_UFO_HIT gets that pointer as its attacker (the frame before drive_puppet notices)
        void* pup = C.puppet_of ? C.puppet_of((uint8_t)atoi(a[1].c_str())) : nullptr;
        if (!is_host() || !pup || !live(pup))
            return "no such puppet";
        g_stale_puppet = pup;
        game::destroy_creature(pup);
        return live(pup) ? "still listed" : "freed";
    }
    return "usage: coop.fleet list | spawn <type> [own] | shoot <id> <dmg> | hurt <ship id> <player> | hitfix 0|1 | "
           "chunkfix 0|1 | keepposse 0|1 | drop <chunk> <n> | freemirror <id> | freepuppet <player>";
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    auto rot = [](void* u) {
        auto& r = F<float[4]>(u, kRot);
        return net::Quat{r[0], r[1], r[2], r[3]};
    };
    auto ufos = all_ufos();
    auto live = [&](void* x) { return std::find(ufos.begin(), ufos.end(), x) != ufos.end(); };
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [u, id] : g_hid)
            if (live(u))
                v.push_back({"s" + std::to_string(id), pos_of(u), rot(u)});
    } else {
        for (auto& [id, m] : g_mirrors)
            if (m.u && live(m.u))
                v.push_back({"s" + std::to_string(id), pos_of(m.u), rot(m.u)});
    }
    return v;
}

} // namespace squadron
