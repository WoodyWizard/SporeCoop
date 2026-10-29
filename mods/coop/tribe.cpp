#include "tribe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "driver.h"
#include "game.h"
#include "modes.h"
#include "npc.h"
#include "space_ext.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;

// ---- layout (docs/re/tribe.md) ----
constexpr uint32_t kTribeNoun = 0x18C6D19;
constexpr uintptr_t kMembersOff = 0x340;   // cTribe: vector<cCreatureCitizen*>
constexpr uintptr_t kTribePosOff = 0x124;  // cTribe spatial position
constexpr uintptr_t kOwnerTribeOff = 0x1014; // cCreatureCitizen::mpOwnerTribe
constexpr uintptr_t kAgeOff = 0xB34;       // 0 baby, 1 adult
constexpr uintptr_t kSelectedOff = 0x12C;  // spatial+0x6C IsSelected
static uintptr_t a_CitizenVt; // Simulator::cCreatureCitizen::vftable
constexpr uint32_t kTreeNoun = 0x6ABEE7E;    // cTreeData (planet flora: fruit trees), not a cGameData
static uintptr_t a_TreeVt;    // Simulator::cTreeData::vftable (main vtable)

static uintptr_t a_GetPlayerTribe, a_DoAction, a_SetValueVector3, a_BTTick, a_DoActionForObjects, a_WalkSelected,
    a_SetSelected, a_CreateCitizen, a_AddMember;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
bool active() { return g_ok && C.sess && C.sess->connected() && game::mode_id() == modes::kTribe; }
// BUG-0151: while the host is away (main menu) nothing simulates or orders the guest's members (the
// host sends no states and drops orders outside Tribe): the guest's own game runs their AI and its
// orders until the host is back. `coop.tribe fix away 0`: the old frozen tribe (tests).
static bool g_fix_away = true;
COOP_FIX("tribe.away", "BUG-0151", g_fix_away, "tribe: while the host is away the guest's own game runs its members");
static uint32_t g_away_orders;
static bool local_play() { return g_fix_away && is_guest() && C.host_away && C.host_away(); }

static void* player_tribe()
{
    void* nm = game::noun_manager();
    return nm ? thiscall<void*>(a_GetPlayerTribe, nm) : nullptr;
}

static bool is_citizen(void* c) { return c && !IsBadReadPtr(c, 8) && *(uintptr_t*)c == a_CitizenVt; }

static std::vector<void*> members(void* t)
{
    std::vector<void*> v;
    void** b = F<void**>(t, kMembersOff);
    void** e = F<void**>(t, kMembersOff + 4);
    for (void** it = b; it && it < e && v.size() < 256; ++it)
        if (*it && game::object_alive(*it))
            v.push_back(*it);
    return v;
}

static std::vector<void*> all_tribes()
{
    std::vector<void*> v;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun == kTribeNoun && game::object_alive(o))
                ((std::vector<void*>*)u)->push_back(o);
            return true;
        },
        &v);
    return v;
}

static net::Vec3 tribe_pos(void* t)
{
    auto& p = F<float[3]>(t, kTribePosOff);
    return {p[0], p[1], p[2]};
}

// ---- planet trees (docs/re/tribe.md, "Trees"): the flora object keeps a sorted vector of patches,
// each patch has three fixed_vectors of intrusive_ptr<cTreeData>; a tree's position is at +0x10.
// Gather orders on a fruit tree name a cTreeData, which is no cGameData: it is found by position.
static bool is_tree(void* t) { return t && !IsBadReadPtr(t, 0x20) && *(uintptr_t*)t == a_TreeVt; }

template <typename Fn>
static void for_each_tree(Fn fn)
{
    static uintptr_t get = H->sym("SporeCoop::Flora_Get");
    uint8_t* fl = get ? skg::cdecl_call<uint8_t*>(get) : nullptr;
    if (!fl || IsBadReadPtr(fl + 0x1E5F0, 8))
        return;
    uint8_t* b = F<uint8_t*>(fl, 0x1E5F0);
    uint8_t* e = F<uint8_t*>(fl, 0x1E5F4);
    constexpr size_t kPatch = 0x19A8;
    if (!b || e < b || (size_t)(e - b) % kPatch || (size_t)(e - b) / kPatch > 20000)
        return;
    for (uint8_t* el = b; el < e; el += kPatch) {
        uint8_t* rec = el + 8;
        for (uintptr_t vo : {0x44u, 0x9Cu, 0xF4u}) {
            void** tb = F<void**>(rec, vo);
            void** te = F<void**>(rec, vo + 4);
            if (!tb || te < tb || te - tb > 64 || IsBadReadPtr(tb, (te - tb) * 4))
                continue;
            for (void** it = tb; it < te; ++it)
                if (is_tree(*it))
                    fn(*it, net::Vec3{F<float>(*it, 0x10), F<float>(*it, 0x14), F<float>(*it, 0x18)});
        }
    }
}

static void* find_tree(const net::Vec3& pos, float max_dist)
{
    void* best = nullptr;
    float bd = max_dist;
    for_each_tree([&](void* t, const net::Vec3& p) {
        float d = net::dist(p, pos);
        if (d < bd) {
            bd = d;
            best = t;
        }
    });
    return best;
}

void for_each_tribe_center(void (*fn)(const std::string& key, const net::Vec3& pos, void* user), void* user)
{
    if (!g_ok)
        return;
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t i = 0; i < tribes.size(); i++)
        fn("tribe" + std::to_string(i) + (tribes[i] == pt ? "p" : ""), tribe_pos(tribes[i]), user);
}

void for_each_tree_pos(void (*fn)(const net::Vec3& pos, bool fruit, void* user), void* user)
{
    if (g_ok) // +0x48: the fruit left (bits, +0x4C = all slots ~): a tree without fruit is no gather target
        for_each_tree([&](void* t, const net::Vec3& p) { fn(p, F<uint32_t>(t, 0x48) != 0, user); });
}

// ---------------- host ----------------
struct HMember {
    uint16_t id;
    uint8_t owner;
};
static std::unordered_map<void*, HMember> g_hm;
static std::unordered_map<uint16_t, void*> g_hm_by_id;
static uint16_t g_next_id = 1;
static uint32_t g_player_members_seen = 0;
static std::vector<uint8_t> g_split_for; // player ids the player tribe was split for
static double g_last_send;
static uint16_t g_cycle;
static uint32_t g_cmds_done, g_cmds_rejected;
static std::string g_last_cmd; // the last order (guest: sent, host: done or why it was rejected), tests
static std::string g_last_local; // the host's own last order (tests)
static uint32_t g_local_orders;
static uint32_t g_gifts;

// ---------------- guest ----------------
struct GMember {
    void* c = nullptr;
    uint8_t tribe = 0, owner = 0xFE, age = 1;
    net::Vec3 pos, vel;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    drive::State ds;
};
static std::unordered_map<uint16_t, GMember> g_gm;
static std::unordered_map<void*, uint16_t> g_gid_of;
static std::unordered_set<uint16_t> g_cycle_ids;
static std::unordered_set<uint8_t> g_cycle_chunks; // chunks of the current cycle received
static uint32_t g_incomplete;                      // cycles whose cleanup was skipped (a chunk missing)
static uint16_t g_rx_cycle = 0xFFFF;
static uint32_t g_spawned, g_removed, g_cmds_sent;
static std::vector<void*> g_local_tribe_of; // host tribe index -> local tribe

uint8_t owner_of(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm.find(c);
        return it == g_hm.end() ? 0xFF : it->second.owner;
    }
    auto it = g_gid_of.find(c);
    if (it == g_gid_of.end())
        return 0xFF;
    return g_gm[it->second].owner;
}

// ---------------- hooks ----------------
using BTTick_t = int(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static BTTick_t o_BTTick;
static uint32_t g_bt_skipped, g_bt_calls; // diagnostics (coop.tribe): mirrors' AI ticks skipped / all ticks
// behaviour tree tick (shared by every creature kind): skipped for citizen mirrors on guests
static int SK_CDECL h_BTTick(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t self, uint32_t a5, uint32_t a6,
                             uint32_t a7, uint32_t a8)
{
    g_bt_calls++;
    if (is_guest() && is_citizen((void*)self) && game::mode_id() == modes::kTribe && !local_play()) {
        g_bt_skipped++;
        return 0;
    }
    return o_BTTick(a0, a1, a2, a3, self, a5, a6, a7, a8);
}

// a player may select only their own members of the player tribe
using SetSelected_t = int(SK_THISCALL*)(void*, int);
static SetSelected_t o_SetSelected;
static int SK_THISCALL h_SetSelected(void* spatial, int sel)
{
    if ((sel & 0xFF) && active()) {
        void* c = (uint8_t*)spatial - 0xC0;
        uint8_t owner = owner_of(c);
        if (owner != 0xFF && owner != C.sess->my_id())
            sel = 0;
    }
    return o_SetSelected(spatial, sel);
}

struct Target {
    uint8_t kind = 0; // 0 none, 1 citizen id, 2 object by noun + position, 3 tree by position
    uint16_t id = 0;
    uint32_t noun = 0;
    net::Vec3 pos;
};

static Target encode_target(void* t)
{
    Target r;
    if (!t)
        return r;
    auto it = g_gid_of.find(t);
    if (it != g_gid_of.end()) {
        r.kind = 1;
        r.id = it->second;
        return r;
    }
    if (is_tree(t)) { // a planet tree: no cGameData, the host finds its own copy by position
        r.kind = 3;
        r.noun = kTreeNoun;
        r.pos = {F<float>(t, 0x10), F<float>(t, 0x14), F<float>(t, 0x18)};
        return r;
    }
    r.kind = 2;
    r.noun = game::noun_id(t);
    game::object_position(t, r.pos);
    return r;
}

static void write_target(net::Writer& w, const Target& t)
{
    w.u8(t.kind);
    w.u16(t.id);
    w.u32(t.noun);
    w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z);
}

static Target read_target(net::Reader& r)
{
    Target t;
    t.kind = r.u8();
    t.id = r.u16();
    t.noun = r.u32();
    t.pos.x = r.f32(); t.pos.y = r.f32(); t.pos.z = r.f32();
    return t;
}

enum : uint8_t { CMD_ACTION = 0, CMD_WALK = 1, CMD_GIVE = 2 };

static void send_cmd(uint8_t kind, const std::vector<uint16_t>& ids, int action, const Target& tgt, const net::Vec3& pos)
{
    if (ids.empty())
        return;
    net::Writer w;
    w.u8(kind);
    w.u8((uint8_t)std::min<size_t>(ids.size(), 64));
    for (size_t i = 0; i < ids.size() && i < 64; i++)
        w.u16(ids[i]);
    w.u32((uint32_t)action);
    write_target(w, tgt);
    w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
    C.sess->send_reliable_to(0, net::MSG_TRIBE_CMD, w);
    g_cmds_sent++;
    char b[128];
    snprintf(b, sizeof b, "kind=%u n=%u action=%d target=%u/%u/%X (%.1f %.1f %.1f)", kind, (unsigned)ids.size(), action,
             tgt.kind, tgt.id, tgt.noun, tgt.pos.x, tgt.pos.y, tgt.pos.z);
    g_last_cmd = b;
}

// cTribeInputStrategy::DoActionForObjects(vector<cSpatialObject*>*, action, target, prop) - every
// order on an object goes through it; on guests the order goes to the host instead
using DoActionForObjects_t = int(SK_THISCALL*)(void*, void*, int, void*, void*);
static DoActionForObjects_t o_DoActionForObjects;
static int SK_THISCALL h_DoActionForObjects(void* self, void* vec, int action, void* target, void* prop)
{
    if (local_play()) {
        g_away_orders++;
        return o_DoActionForObjects(self, vec, action, target, prop);
    }
    if (!is_guest() || game::mode_id() != modes::kTribe) {
        if (is_host() && game::mode_id() == modes::kTribe) {
            char lb[96];
            snprintf(lb, sizeof lb, "action=%d n=%d obj=%p noun=%X", action,
                     vec ? (int)(F<void**>(vec, 4) - F<void**>(vec, 0)) : -1, target, target ? game::noun_id(target) : 0);
            g_last_local = lb;
            g_local_orders++;
        }
        return o_DoActionForObjects(self, vec, action, target, prop);
    }
    std::vector<uint16_t> ids;
    void** b = vec ? F<void**>(vec, 0) : nullptr;
    void** e = vec ? F<void**>(vec, 4) : nullptr;
    for (void** it = b; it && it < e; ++it) {
        auto f = g_gid_of.find((uint8_t*)*it - 0xC0);
        if (f != g_gid_of.end())
            ids.push_back(f->second);
    }
    send_cmd(CMD_ACTION, ids, action, encode_target(target), {});
    char ob[48];
    snprintf(ob, sizeof ob, " obj=%p", target);
    g_last_cmd += ob;
    return ids.empty() ? 1 : 0;
}

// cTribeInputStrategy::WalkSelectedTo(const Vector3*)
using WalkSelected_t = int(SK_THISCALL*)(void*, const float*);
static WalkSelected_t o_WalkSelected;
static int SK_THISCALL h_WalkSelected(void* self, const float* pos)
{
    if (local_play()) {
        g_away_orders++;
        return o_WalkSelected(self, pos);
    }
    if (!is_guest() || game::mode_id() != modes::kTribe || !pos) {
        if (is_host() && game::mode_id() == modes::kTribe && pos) {
            char lb[96];
            snprintf(lb, sizeof lb, "walk to (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);
            g_last_local = lb;
            g_local_orders++;
        }
        return o_WalkSelected(self, pos);
    }
    std::vector<uint16_t> ids;
    for (auto& [id, m] : g_gm)
        if (m.c && F<uint8_t>(m.c, kSelectedOff) && m.owner == C.sess->my_id())
            ids.push_back(id);
    send_cmd(CMD_WALK, ids, 4, {}, {pos[0], pos[1], pos[2]});
    char b[64];
    snprintf(b, sizeof b, " to (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);
    g_last_cmd += b;
    return 0;
}

static void coop_init();

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetPlayerTribe = H->sym("Simulator::cGameNounManager::GetPlayerTribe");
    a_DoAction = H->sym("Simulator::cCreatureCitizen::DoAction");
    a_SetValueVector3 = H->sym("App::Property::SetValueVector3");
    a_BTTick = H->sym("Simulator::BehaviorTreeTick");
    a_DoActionForObjects = H->sym("Simulator::cTribeInputStrategy::DoActionForObjects");
    a_WalkSelected = H->sym("Simulator::cTribeInputStrategy::WalkSelectedTo");
    a_SetSelected = H->sym("Simulator::cCreatureCitizen::SetSelected");
    a_CreateCitizen = H->sym("Simulator::cTribe::CreateCitizen");
    a_AddMember = H->sym("Simulator::cTribe::AddMember");
    a_CitizenVt = H->sym("Simulator::cCreatureCitizen::vftable");
    a_TreeVt = H->sym("Simulator::cTreeData::vftable");
    g_ok = a_GetPlayerTribe && a_DoAction && a_SetValueVector3 && a_BTTick && a_DoActionForObjects && a_WalkSelected &&
           a_SetSelected && a_CreateCitizen && a_AddMember && a_CitizenVt && a_TreeVt;
    if (!g_ok) {
        H->log(M, SK_WARN, "tribe: symbols missing, tribe co-op disabled");
        return false;
    }
    H->hook_create(M, (void*)a_BTTick, (void*)h_BTTick, (void**)&o_BTTick);
    H->hook_create(M, (void*)a_SetSelected, (void*)h_SetSelected, (void**)&o_SetSelected);
    H->hook_create(M, (void*)a_DoActionForObjects, (void*)h_DoActionForObjects, (void**)&o_DoActionForObjects);
    H->hook_create(M, (void*)a_WalkSelected, (void*)h_WalkSelected, (void**)&o_WalkSelected);
    coop_init();
    return true;
}

// ---------------- host: snapshots + orders ----------------
static void host_tick(double now)
{
    if (now - g_last_send < 0.12)
        return;
    g_last_send = now;
    void* pt = player_tribe();
    auto tribes = all_tribes();
    std::vector<uint8_t> players;
    for (auto& p : C.sess->players())
        players.push_back(p.id);
    std::sort(players.begin(), players.end());
    if (players != g_split_for && pt) {
        // the players changed: split the player tribe again, in member order
        g_split_for = players;
        g_player_members_seen = 0;
        for (void* c : members(pt)) {
            auto it = g_hm.find(c);
            if (it != g_hm.end())
                it->second.owner = players[g_player_members_seen++ % players.size()];
        }
    }
    std::unordered_set<void*> alive;
    struct Entry {
        uint16_t id;
        uint8_t tribe, owner, age;
        void* c;
    };
    std::vector<Entry> entries;
    for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
        bool player = tribes[ti] == pt;
        for (void* c : members(tribes[ti])) {
            alive.insert(c);
            auto it = g_hm.find(c);
            if (it == g_hm.end()) {
                // the player tribe is split between the players in turns; others belong to nobody
                uint8_t owner = player && !players.empty() ? players[g_player_members_seen++ % players.size()] : 0xFE;
                // BUG-0154: never a live id, never 0
                it = g_hm.emplace(c, HMember{net::alloc_id(g_next_id, g_hm_by_id), owner}).first;
                g_hm_by_id[it->second.id] = c;
            }
            entries.push_back({it->second.id, (uint8_t)ti, it->second.owner, F<uint8_t>(c, kAgeOff), c});
        }
    }
    for (auto it = g_hm.begin(); it != g_hm.end();) {
        if (!alive.count(it->first)) {
            g_hm_by_id.erase(it->second.id);
            it = g_hm.erase(it);
        } else
            ++it;
    }
    // chunks that fit one packet; every chunk carries the tribe table. A chunk larger than a packet
    // is dropped by the connection: with 24 members per chunk (fixed until 2026-10-23) the first
    // chunk (the player tribe first) never arrived once the planet had 24 citizens, and the guest
    // removed those mirrors at the end of every cycle (play_tribe.py: the guest's tribe emptied)
    g_cycle++;
    size_t nt = std::min<size_t>(tribes.size(), 32);
    constexpr size_t kEntry = 2 + 1 + 1 + 1 + 12 + 12 + 16 + 4;
    size_t head = 2 + 1 + 1 + 1 + nt * 13 + 1;
    size_t per = std::max<size_t>(1, (net::kMaxPacket - 64 - head) / kEntry);
    size_t chunks = std::max<size_t>(1, (entries.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        w.u8((uint8_t)std::min<size_t>(tribes.size(), 32));
        for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
            net::Vec3 p = tribe_pos(tribes[ti]);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.u8(tribes[ti] == pt ? 1 : 0);
        }
        size_t from = k * per, to = std::min(entries.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            const Entry& e = entries[i];
            net::Vec3 p = game::position(e.c), v = game::velocity(e.c);
            net::Quat q = game::orientation(e.c);
            w.u16(e.id); w.u8(e.tribe); w.u8(e.owner); w.u8(e.age);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(v.x); w.f32(v.y); w.f32(v.z);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.f32(game::health(e.c));
        }
        for (auto& p : C.sess->players())
            if (p.id != 0)
                C.sess->send_unreliable_to(p.id, net::MSG_TRIBE_STATE, w);
    }
}

static void* resolve_target(const Target& t)
{
    if (t.kind == 1) {
        auto it = g_hm_by_id.find(t.id);
        return it == g_hm_by_id.end() ? nullptr : it->second;
    }
    if (t.kind == 2 && t.noun)
        return game::find_object(t.noun, t.pos, 8.f);
    if (t.kind == 3)
        return find_tree(t.pos, 1.f);
    return nullptr;
}

// host: members of `from` change hands (a gift); owners travel with the snapshots
static void give_on_host(uint8_t from, uint8_t to, const std::vector<uint16_t>& ids)
{
    void* pt = player_tribe();
    int n = 0;
    for (uint16_t id : ids) {
        auto it = g_hm_by_id.find(id);
        auto hm = it == g_hm_by_id.end() ? g_hm.end() : g_hm.find(it->second);
        if (hm == g_hm.end() || hm->second.owner != from || F<void*>(hm->first, kOwnerTribeOff) != pt)
            continue;
        hm->second.owner = to;
        n++;
    }
    g_gifts += n;
    if (n && C.notice)
        C.notice(C.sess->name_of(from) + " gave " + std::to_string(n) + (n == 1 ? " tribe member to " : " tribe members to ") +
                 C.sess->name_of(to));
}

static void host_command(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8(), n = r.u8();
    std::vector<uint16_t> ids;
    for (int i = 0; i < n && r.ok(); i++)
        ids.push_back(r.u16());
    int action = (int)r.u32();
    Target tgt = read_target(r);
    net::Vec3 pos;
    pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32();
    if (!r.ok())
        return;
    if (kind == CMD_GIVE) {
        if (action >= 0 && action <= 0xFF && net::is_player(*C.sess, (uint8_t)action))
            give_on_host(from, (uint8_t)action, ids);
        else
            g_cmds_rejected++;
        return;
    }
    // CitizenAction values of the game's own orders are 0..28 (docs/re/tribe.md 2)
    if (kind == CMD_ACTION && (action < 0 || action > 28)) {
        g_cmds_rejected++;
        return;
    }
    if (kind != CMD_ACTION && kind != CMD_WALK)
        return;
    void* pt = player_tribe();
    void* target = kind == CMD_ACTION ? resolve_target(tgt) : nullptr;
    char b[160];
    snprintf(b, sizeof b, "from=%u kind=%u n=%u action=%d target=%u/%u/%X (%.1f %.1f %.1f) -> %p", from, kind,
             (unsigned)ids.size(), action, tgt.kind, tgt.id, tgt.noun, tgt.pos.x, tgt.pos.y, tgt.pos.z, target);
    g_last_cmd = b;
    if (kind == CMD_ACTION && tgt.kind != 0 && !target) {
        g_cmds_rejected++;
        g_last_cmd += " rejected: no such target here";
        return; // the object is gone on the host
    }
    for (uint16_t id : ids) {
        auto it = g_hm_by_id.find(id);
        if (it == g_hm_by_id.end()) {
            g_cmds_rejected++;
            continue;
        }
        void* c = it->second;
        auto hm = g_hm.find(c);
        if (hm == g_hm.end() || hm->second.owner != from || F<void*>(c, kOwnerTribeOff) != pt ||
            !game::object_alive(c)) {
            g_cmds_rejected++;
            continue;
        }
        if (kind == CMD_WALK) {
            void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
            float p[3] = {pos.x, pos.y, pos.z};
            if (rec)
                thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)p);
        } else
            thiscall<void*>(a_DoAction, c, action, target, (void*)nullptr);
        g_cmds_done++;
    }
}

// ---------------- guest: mirrors ----------------
// The game-over caption ("Alas! Your population has fallen below the minimum...") comes when no
// member of the player tribe has health left. A member added while it is up has no species fields
// (+0xB20, +0xB54) and the OK (bounce back) faulted at 0xC0DAE3 on it, then the game froze
// (BUG-0172). No member is added to the player tribe then. `coop.tribe fix gameover 0`: the old way.
static bool g_fix_gameover = true;
COOP_FIX("tribe.gameover", "BUG-0172", g_fix_gameover, "tribe: no member is added during the game-over caption");
static uint32_t g_spawns_blocked;
static bool player_tribe_out(void* t)
{
    if (!g_fix_gameover || !t || t != player_tribe())
        return false;
    for (void* c : members(t))
        if (game::health(c) > 0)
            return false;
    return true;
}

static void* spawn_member(void* t, uint8_t age)
{
    if (player_tribe_out(t)) {
        g_spawns_blocked++;
        return nullptr;
    }
    // SpawnMember (vt 0x98) returns null at the population cap: then create + add directly
    void* c = vcall<void*>(t, 0x98 / 4, (int)age);
    if (!c) {
        c = thiscall<void*>(a_CreateCitizen, t, (int)age);
        if (c)
            thiscall<void>(a_AddMember, t, c, 1);
    }
    return c;
}

static void remove_member(void* c)
{
    void* t = F<void*>(c, kOwnerTribeOff);
    if (t)
        vcall<void>(t, 0x9C / 4, c, 1); // RemoveMember(c, destroy)
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), nt = r.u8();
    if (!r.ok() || !net::cycle_chunk_ok(chunk, chunks))
        return;
    std::vector<std::pair<net::Vec3, bool>> htribes;
    for (int i = 0; i < nt && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        htribes.push_back({p, r.u8() != 0});
    }
    if (!r.ok())
        return;
    // host tribe index -> local tribe (the player tribe directly, others by position)
    auto locals = all_tribes();
    void* pt = player_tribe();
    g_local_tribe_of.assign(htribes.size(), nullptr);
    for (size_t i = 0; i < htribes.size(); i++) {
        if (htribes[i].second) {
            g_local_tribe_of[i] = pt;
            continue;
        }
        float best = 40.f;
        for (void* t : locals) {
            float d = net::dist(tribe_pos(t), htribes[i].first);
            if (t != pt && d < best) {
                best = d;
                g_local_tribe_of[i] = t;
            }
        }
    }
    if (cycle != g_rx_cycle) {
        g_rx_cycle = cycle;
        g_cycle_ids.clear();
        g_cycle_chunks.clear();
    }
    double now = H->time_seconds();
    uint8_t n = r.u8();
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        GMember m;
        m.tribe = r.u8(); m.owner = r.u8(); m.age = r.u8();
        m.pos.x = r.f32(); m.pos.y = r.f32(); m.pos.z = r.f32();
        m.vel.x = r.f32(); m.vel.y = r.f32(); m.vel.z = r.f32();
        m.rot.x = r.f32(); m.rot.y = r.f32(); m.rot.z = r.f32(); m.rot.w = r.f32();
        m.hp = r.f32();
        if (!r.ok())
            return;
        g_cycle_ids.insert(id);
        auto& g = g_gm[id];
        void* c = g.c;
        g.tribe = m.tribe; g.owner = m.owner; g.age = m.age;
        g.pos = m.pos; g.vel = m.vel; g.rot = m.rot; g.hp = m.hp; g.seen = now;
        if (c && !game::object_alive(c)) {
            g_gid_of.erase(c);
            g.c = c = nullptr;
        }
        if (c)
            continue;
        void* t = m.tribe < g_local_tribe_of.size() ? g_local_tribe_of[m.tribe] : nullptr;
        if (!t)
            continue;
        // bind to the nearest unbound local member of that tribe, else spawn one
        float best = 6.f;
        for (void* lc : members(t)) {
            if (g_gid_of.count(lc))
                continue;
            float d = net::dist(game::position(lc), m.pos);
            if (d < best) {
                best = d;
                c = lc;
            }
        }
        if (!c) {
            c = spawn_member(t, m.age);
            if (!c)
                continue;
            game::teleport(c, m.pos, m.rot);
            g_spawned++;
        }
        g.c = c;
        g_gid_of[c] = id;
    }
    if (!r.ok()) // a cut chunk does not count: the cycle stays incomplete, nothing is removed
        return;
    g_cycle_chunks.insert(chunk);
    if (chunk + 1 < chunks)
        return;
    if (g_cycle_chunks.size() < chunks) { // a chunk of this cycle was lost or comes late: keep everything
        g_incomplete++;
        return;
    }
    // end of a full snapshot: members the host no longer has go away, and so do local members
    // the host does not know (the guest's tribes are only mirrors)
    for (auto it = g_gm.begin(); it != g_gm.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.c && game::object_alive(it->second.c)) {
                remove_member(it->second.c);
                g_removed++;
            }
            g_gid_of.erase(it->second.c);
            it = g_gm.erase(it);
        } else
            ++it;
    }
    for (void* t : g_local_tribe_of)
        if (t)
            for (void* lc : members(t))
                if (!g_gid_of.count(lc)) {
                    remove_member(lc);
                    g_removed++;
                }
}

// BUG-0173: a member knocked out on the host (health 0, the game's dead flag +0xB5E, behaviour
// node 0x05CA7FA7, it stays in the tribe until it respawns: the chieftain, NPC tribes) stood on the
// guest with its last health. The mirror's behaviour tree does not run on a guest, so the mod plays
// the death itself (Die + the death animation, game::play_death). When the host's member has health
// again, the dead mirror is replaced by a fresh one (the next snapshot binds or spawns it).
// `coop.tribe fix ko 0`: the code before (tests).
static bool g_fix_ko = true;
COOP_FIX("tribe.ko", "BUG-0173", g_fix_ko, "tribe: a guest's mirror of a member without health plays the death");
static uint32_t g_ko_shown, g_ko_replaced;

static void guest_tick(double now)
{
    if (local_play())
        return; // the last states would hold the members for 3 s
    for (auto& [id, g] : g_gm) {
        if (!g.c || !game::object_alive(g.c) || now - g.seen > 3.0)
            continue;
        if (g_fix_ko && g.hp <= 0) {
            if (!game::is_dead(g.c)) {
                game::set_health(g.c, 0);
                game::play_death(g.c);
                g_ko_shown++;
            }
            drive::step(g.c, g.pos, g.rot, g.vel, g.ds, now, now - g.seen, true);
            continue;
        }
        if (g_fix_ko && game::is_dead(g.c)) { // back on its feet on the host
            g_gid_of.erase(g.c);
            remove_member(g.c);
            g.c = nullptr;
            g_ko_replaced++;
            continue;
        }
        drive::step(g.c, g.pos, g.rot, g.vel, g.ds, now, now - g.seen, true);
        float hp = game::health(g.c);
        if (std::fabs(hp - g.hp) > 0.01f && g.hp > 0)
            game::set_health(g.c, g.hp);
    }
}

// ---------------- co-op mechanics (docs/re/tribe.md, co-op features) ----------------
// pincer: members of two players hitting the same tribe within 10 s deal 50% more damage
// chief: every player has a chief (its lowest-id adult member, bigger, "★" tag); members near their
//        chief hit 20% harder; a chief about to die next to a partner's member keeps 30% health
// duet: performances of both players for the same tribe fill the social mini-game faster
// healers: a full heal of a partner's member brings 5 food
// epic: now and then an epic creature comes to the village; killed by both players: 80 food
constexpr double kPincerWindow = 10.0;
static uintptr_t a_CitizenTD, a_HealTick, a_PatientOf, a_AddFood, a_FoodFx, a_SessionGet, a_CreateHerd,
    a_HerdEnable, a_CreatureCreate;
static std::unordered_map<void*, std::unordered_map<uint8_t, double>> g_pincer; // enemy tribe -> owner -> time
static std::unordered_map<void*, double> g_pincer_told;
static std::unordered_map<uint8_t, void*> g_chief;                              // owner -> chief citizen
static std::unordered_map<void*, float> g_chief_scale;                          // chief -> its own scale
static std::unordered_map<void*, double> g_chief_saved;                         // chief -> last rescue
static uint32_t g_pincer_hits, g_aura_hits, g_chief_rescues, g_duet_ticks, g_heals, g_epics, g_epic_kills;
static void* g_epic;
static void* g_epic_herd; // the herd g_epic lives in (ours: quieted when the epic is gone)
static std::unordered_set<uint8_t> g_epic_hitters;
// BUG-0146: only the players' members count as hitters (an NPC tribe's member is owner 0xFE).
// BUG-0121: the epic's herd is reused and quieted, a live epic never goes into a save.
// `coop.tribe fix epic_players|epic_herd 0`: the old behaviour (tests).
static bool g_fix_epic_players = true, g_fix_epic_herd = true;
COOP_FIX("tribe.epic_players", "BUG-0146", g_fix_epic_players, "tribe: only players' members count as epic hitters");
COOP_FIX("tribe.epic_herd", "BUG-0121", g_fix_epic_herd, "tribe: the epic's herd is reused and quieted, a live epic is not saved");
static uint32_t g_epic_herd_reused, g_epic_saved_out;
constexpr uint32_t kEpicArchetype = 0xFC811218u;
static double g_epic_next, g_last_duet, g_duet_told;

static uint16_t member_id(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm.find(c);
        return it == g_hm.end() ? 0 : it->second.id;
    }
    auto it = g_gid_of.find(c);
    return it == g_gid_of.end() ? 0 : it->second;
}

uint16_t id_of(void* c) { return member_id(c); }

// tests (game bots): a readable key for tribe buildings on screen: hut<tribe index>, "p" for the player's
std::string object_key(void* obj)
{
    if (!g_ok || !obj)
        return "";
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t i = 0; i < tribes.size(); i++)
        if (F<void*>(tribes[i], 0x368) == obj) // cTribe::mpHut
            return "hut" + std::to_string(i) + (tribes[i] == pt ? "p" : "");
    return "";
}

// the tribe UI's interaction mode (TribeUI+0x390: 0 social, 1 attack; docs/re/tribe.md B), -1 unknown
static int ui_mode()
{
    uintptr_t get = H->sym("SporeCoop::cTribeUI_Get");
    void* ui = get ? skg::cdecl_call<void*>(get) : nullptr;
    return ui && !IsBadReadPtr((uint8_t*)ui + 0x390, 4) ? F<uint8_t>(ui, 0x390) : -1;
}

bool is_chief(void* c)
{
    for (auto& [o, ch] : g_chief)
        if (ch == c)
            return true;
    return false;
}

// health as the host has it: guests keep the last snapshot value (a mirror is never set to 0, so a
// knocked-out member would still look alive there and the machines would pick different chiefs)
static float host_health(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host)
        return game::health(c);
    auto it = g_gid_of.find(c);
    if (it == g_gid_of.end())
        return game::health(c);
    auto g = g_gm.find(it->second);
    return g == g_gm.end() ? game::health(c) : g->second.hp;
}

// A former chief gets its own size back (spatial vt 0x40 SetScale). BUG-0081: it is called only
// for a citizen that is still a member of the player tribe in the game's own list; before, the only
// check was object_alive, which reads the citizen's own memory and cannot tell a freed one (a guest
// mirror removed by guest_state, a load): the call went through a vtable read from freed memory.
// keep: the chiefs that stay (nullptr: none, the session ends). `coop.tribe fix chiefs 0`: the old check.
static bool g_fix_chiefs = true;
COOP_FIX_UNSAFE("tribe.chiefs", "BUG-0081", g_fix_chiefs, "tribe: chiefs checked against the player tribe's own list, not freed memory");
static uint32_t g_chief_restores, g_chief_gone, g_chief_bad_calls;
static void restore_chief_scales(const std::unordered_map<uint8_t, void*>* keep)
{
    std::unordered_set<void*> mem;
    if (void* pt = g_ok && game::mode_id() == modes::kTribe ? player_tribe() : nullptr) {
        void** b = F<void**>(pt, kMembersOff);
        void** e = F<void**>(pt, kMembersOff + 4);
        for (void** it = b; it && it < e && mem.size() < 256; ++it)
            if (*it)
                mem.insert(*it);
    }
    for (auto it = g_chief_scale.begin(); it != g_chief_scale.end();) {
        bool still = false;
        if (keep)
            for (auto& [o, c] : *keep)
                if (c == it->first)
                    still = true;
        if (still) {
            ++it;
            continue;
        }
        bool member = mem.count(it->first) != 0;
        if (g_fix_chiefs ? member : game::object_alive(it->first)) {
            if (!member)
                g_chief_bad_calls++; // the old check let a call into a non-member through (test)
            vcall<void>((uint8_t*)it->first + 0xC0, 0x40 / 4, it->second);
            g_chief_restores++;
        } else
            g_chief_gone++; // gone from the tribe: nothing to restore
        it = g_chief_scale.erase(it);
    }
}

// every machine picks the same chiefs (ids, owners and health come from the host)
static void chiefs_tick()
{
    void* pt = player_tribe();
    std::unordered_map<uint8_t, void*> want;
    std::unordered_map<uint8_t, uint16_t> best;
    if (pt)
        for (void* c : members(pt)) {
            uint8_t o = owner_of(c);
            uint16_t id = member_id(c);
            // a knocked-out member (the game's chieftain waits for its respawn with 0 health) leads nobody
            if (o >= 0xFE || !id || F<uint8_t>(c, kAgeOff) != 1 || !game::object_alive(c) || host_health(c) <= 0)
                continue;
            if (!best.count(o) || id < best[o]) {
                best[o] = id;
                want[o] = c;
            }
        }
    // scale: chiefs stand out (spatial vt 0x40 SetScale; citizen vt 0x80 its desired scale)
    restore_chief_scales(&want);
    for (auto& [o, c] : want)
        if (!g_chief_scale.count(c)) {
            float sc = vcall<float>(c, 0x80 / 4, 0); // GetDesiredScale(int), ret 4
            if (sc <= 0.01f || sc > 10.f)
                sc = 1.f;
            g_chief_scale[c] = sc;
            vcall<void>((uint8_t*)c + 0xC0, 0x40 / 4, sc * 1.3f);
        }
    g_chief = want;
}

static bool near_own_chief(void* c, uint8_t owner)
{
    auto it = g_chief.find(owner);
    return it != g_chief.end() && it->second != c && net::dist(game::position(it->second), game::position(c)) < 15.f;
}

// cCreatureCitizen TakeDamage 0xC26920 (this = citizen+0x5A8): every hit on a citizen
using CTD_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static CTD_t o_CitizenTD;
static int SK_THISCALL h_CitizenTD(void* comb, float dmg, uint32_t pol, int type, const void* pos, void* att)
{
    if (is_host() && active()) {
        void* victim = (uint8_t*)comb - 0x5A8;
        void* pt = player_tribe();
        void* vt = F<void*>(victim, kOwnerTribeOff);
        void* ac = att && !IsBadReadPtr(att, 4) ? vcall<void*>(att, 0xC / 4) : nullptr;
        uint8_t ao = ac && g_hm.count(ac) ? g_hm[ac].owner : 0xFF;
        double now = H->time_seconds();
        if (ao < 0xFE && vt && vt != pt) { // our member hits another tribe
            auto& hits = g_pincer[vt];
            hits[ao] = now;
            int players = 0;
            for (auto& [o, t] : hits)
                if (now - t < kPincerWindow)
                    players++;
            if (players >= 2) {
                dmg *= 1.5f;
                g_pincer_hits++;
                if (now - g_pincer_told[vt] > 60 && C.notice) {
                    g_pincer_told[vt] = now;
                    C.notice("pincer attack: +50% damage");
                }
            }
            if (near_own_chief(ac, ao)) {
                dmg *= 1.2f;
                g_aura_hits++;
            }
        }
        // a chief about to fall next to a partner's member: the tribe protects it
        if (vt == pt && is_chief(victim)) {
            float hp = F<float>(victim, 0x5E0), mx = F<float>(victim, 0x5D4);
            uint8_t o = owner_of(victim);
            if (dmg >= hp && mx > 0 && now - g_chief_saved[victim] > 60) {
                for (void* m : members(pt)) {
                    uint8_t mo = owner_of(m);
                    if (mo < 0xFE && mo != o && net::dist(game::position(m), game::position(victim)) < 15.f) {
                        dmg = std::max(0.f, hp - mx * 0.3f);
                        g_chief_saved[victim] = now;
                        g_chief_rescues++;
                        if (C.notice)
                            C.notice(C.sess->name_of(mo) + " saved " + C.sess->name_of(o) + "'s chief");
                        break;
                    }
                }
            }
        }
    }
    return o_CitizenTD(comb, dmg, pol, type, pos, att);
}

static void bonus_food(int n)
{
    void* pt = player_tribe();
    if (!pt || !a_AddFood)
        return;
    thiscall<void>(a_AddFood, pt, n);
    if (a_FoodFx)
        thiscall<void>(a_FoodFx, pt, (float)n);
}

// heal tick 0xDB9B20 (cdecl: arg1 healer, *arg6 patient): a full heal of a partner's member
using HealTick_t = bool(SK_CDECL*)(void*, void*, void*, void*, void*, void**, void*, float, void*, void*, void*, void*);
static HealTick_t o_HealTick;
static bool SK_CDECL h_HealTick(void* healer, void* a2, void* a3, void* a4, void* a5, void** target, void* a7, float a8,
                                void* a9, void* a10, void* a11, void* a12)
{
    void* pat = nullptr;
    float hp0 = 0, mx = 0;
    if (is_host() && active() && target && a_PatientOf) {
        pat = skg::cdecl_call<void*>(a_PatientOf, *target);
        if (pat) {
            hp0 = F<float>(pat, 0x5E0);
            mx = F<float>(pat, 0x5D4);
        }
    }
    bool r = o_HealTick(healer, a2, a3, a4, a5, target, a7, a8, a9, a10, a11, a12);
    if (pat && mx > 0 && hp0 < mx && F<float>(pat, 0x5E0) >= mx) {
        uint8_t ho = owner_of(healer), po = owner_of(pat);
        if (ho < 0xFE && po < 0xFE && ho != po) {
            bonus_food(5);
            if (g_heals++ % 4 == 0 && C.notice)
                C.notice(C.sess->name_of(ho) + " healed " + C.sess->name_of(po) + "'s tribe member: +5 food");
        }
    }
    return r;
}

// duet: performances (BT node 0xB46E8803) of both players for the same tribe
static void duet_tick(double now)
{
    if (now - g_last_duet < 0.5 || !a_SessionGet)
        return;
    g_last_duet = now;
    void* pt = player_tribe();
    if (!pt)
        return;
    std::unordered_map<void*, std::unordered_set<uint8_t>> perf; // target tribe -> owners
    std::unordered_map<void*, void*> ui_of;
    for (void* c : members(pt)) {
        uint8_t o = owner_of(c);
        uint8_t* ai = o < 0xFE ? F<uint8_t*>(c, 0xB4C) : nullptr;
        if (!ai || IsBadReadPtr(ai, 0x604) || F<uint32_t>(ai, 0x1D8) != 0xB46E8803u)
            continue;
        uint8_t* blk = F<uint8_t*>(ai, 0x600);
        if (!blk || IsBadReadPtr(blk, 0x1AC))
            continue;
        uint8_t* ss = skg::cdecl_call<uint8_t*>(a_SessionGet, F<int>(blk, 0x1A8));
        if (!ss || IsBadReadPtr(ss, 0xF0))
            continue;
        void* target = F<void*>(ss, 0xC);
        if (!target)
            continue;
        perf[target].insert(o);
        ui_of[target] = F<void*>(ss, 0xE8);
    }
    for (auto& [target, owners] : perf) {
        if (owners.size() < 2)
            continue;
        void* ui = ui_of[target];
        if (ui && !IsBadReadPtr(ui, 0x6C)) {
            float& p = F<float>(ui, 0x68);
            if (p < 0.97f)
                p = std::min(0.97f, p + 0.04f); // +8% a second on top of the performance
            g_duet_ticks++;
        }
        if (now - g_duet_told > 60 && C.notice) {
            g_duet_told = now;
            C.notice("duet: friendship grows faster");
        }
    }
}

// a herd of ours for epics: the epic archetype, no nest, not the avatar's, target size 0 (the
// game's own herds have a target size). prof: of this species (null: any). Checked in the object list.
static bool is_epic_herd_of_ours(void* o)
{
    return game::object_alive(o) && skg::readable(o, 0x21C) && F<uint32_t>(o, 0x88) == kEpicArchetype &&
           !game::herd_nest(o) && !F<uint8_t>(o, 0x84) && F<int32_t>(o, 0xF4) == 0;
}

static void* epic_herd_find(void* prof)
{
    struct Q {
        void* prof;
        void* found = nullptr;
    } q{prof};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (noun != game::kHerdNoun || !is_epic_herd_of_ours(o) || (q->prof && game::herd_species(o) != q->prof))
                return true;
            q->found = o;
            return false;
        },
        &q);
    return q.found;
}

static bool herd_listed(void* h)
{
    struct Q {
        void* want;
        bool found = false;
    } q{h};
    if (!h)
        return false;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (o == q->want && noun == game::kHerdNoun)
                q->found = true;
            return !q->found;
        },
        &q);
    return q.found;
}

// the epic is gone (killed, removed before a save, the session ended): its herd spawns nothing.
// Target size 0 holds; the game enables the herd again after its member died (lane 7: disabled at
// the kill, enabled again within seconds; a disable later stayed), so `enabled` is best effort.
static void epic_herd_release()
{
    if (g_fix_epic_herd && g_epic_herd && herd_listed(g_epic_herd))
        game::herd_quiet(g_epic_herd);
    g_epic_herd = nullptr;
}

// epic: a giant of a species on the planet comes to the village (host)
static bool spawn_epic()
{
    void* pt = player_tribe();
    if (!pt || !a_CreateHerd || !a_HerdEnable || !a_CreatureCreate || g_epic)
        return false;
    net::Vec3 tp = tribe_pos(pt);
    // a species already on the planet (the guests have its model): the nearest animal
    void* prof = nullptr;
    struct Pick {
        net::Vec3 tp;
        void* prof = nullptr;
        float bd = 1e9f;
    } pick{tp};
    game::for_each_object(
        [](void* a, uint32_t noun, void* u) {
            auto* pk = (Pick*)u;
            if (noun != 0x18EB45E || !game::object_alive(a))
                return true;
            float d = net::dist(game::position(a), pk->tp);
            if (d < pk->bd && game::species_profile(a)) {
                pk->bd = d;
                pk->prof = game::species_profile(a);
            }
            return true;
        },
        &pick);
    prof = pick.prof;
    if (!prof)
        return false;
    float len = std::sqrt(tp.x * tp.x + tp.y * tp.y + tp.z * tp.z);
    // 60 m from the village, along a tangent of the planet
    net::Vec3 up = len > 1 ? net::Vec3{tp.x / len, tp.y / len, tp.z / len} : net::Vec3{0, 0, 1};
    net::Vec3 side = std::fabs(up.z) < 0.9f ? net::Vec3{up.y, -up.x, 0} : net::Vec3{0, up.z, -up.y};
    float sl = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
    net::Vec3 p{tp.x + side.x / sl * 60.f, tp.y + side.y / sl * 60.f, tp.z + side.z / sl * 60.f};
    float pp[3] = {p.x, p.y, p.z};
    void* nmgr = game::noun_manager();
    // BUG-0121: one quiet epic herd of the species is reused (also one a save brought back), not a
    // new herd per epic that stayed in the world and in every save
    void* herd = g_fix_epic_herd ? epic_herd_find(prof) : nullptr;
    if (herd)
        g_epic_herd_reused++;
    else
        herd = nmgr ? thiscall<void*>(a_CreateHerd, nmgr, pp, prof, 0, false, 0, false) : nullptr;
    if (!herd)
        return false;
    F<uint32_t>(herd, 0x88) = kEpicArchetype; // an archetype with isEpic = 1
    F<uint32_t>(herd, 0xF0) = 0;
    if (g_fix_epic_herd)
        F<float>(herd, 0x34) = pp[0], F<float>(herd, 0x38) = pp[1], F<float>(herd, 0x3C) = pp[2]; // its home
    thiscall<void>(a_HerdEnable, herd, true);
    void* c = skg::cdecl_call<void*>(a_CreatureCreate, pp, prof, 1, herd, false, false);
    if (g_fix_epic_herd)
        F<int32_t>(herd, 0xF4) = 0; // mTargetHerdSize: the game adds no more epics to it
    if (!c) {
        if (g_fix_epic_herd)
            game::herd_quiet(herd);
        return false;
    }
    g_epic = c;
    g_epic_herd = herd;
    g_epic_hitters.clear();
    g_epics++;
    if (C.notice)
        C.notice("an epic creature comes! together: 80 food");
    return true;
}

// the epic is still an animal of the game's object list (BUG-0171: object_alive reads the
// creature's own memory and cannot tell a freed one; g_epic is a raw pointer kept for minutes)
static bool epic_listed()
{
    struct Q {
        void* want;
        bool found = false;
    } q{g_epic};
    if (!g_epic)
        return false;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (o == q->want)
                q->found = true;
            return !q->found;
        },
        &q);
    return q.found;
}

static void epic_tick(double now)
{
    if (g_epic && (!epic_listed() || !game::object_alive(g_epic) || game::is_dead(g_epic))) {
        int food = g_epic_hitters.size() >= 2 ? 80 : 30;
        bonus_food(food);
        g_epic_kills++;
        if (C.notice)
            C.notice(std::string("epic creature down: +") + std::to_string(food) + " food");
        g_epic = nullptr;
        epic_herd_release();
        g_epic_next = now + 600;
    }
    if (!g_epic_next)
        g_epic_next = now + 480; // the first one after 8 minutes together
    if (!g_epic && now > g_epic_next && C.sess->players().size() >= 2) {
        g_epic_next = now + 600;
        spawn_epic();
    }
}

// the epic's hitters (animals go through the game's creature damage filter)
void note_animal_hit(void* animal, void* attacker_creature)
{
    if (animal && animal == g_epic && attacker_creature && g_hm.count(attacker_creature)) {
        uint8_t o = g_hm[attacker_creature].owner;
        if (!g_fix_epic_players || (o < 0xFE && net::is_player(*C.sess, o)))
            g_epic_hitters.insert(o);
    }
}

static void coop_tick(double now)
{
    chiefs_tick();
    if (is_host()) {
        duet_tick(now);
        epic_tick(now);
    }
}

static void coop_init()
{
    a_CitizenTD = H->sym("Simulator::cCreatureCitizen::TakeDamage");
    a_HealTick = H->sym("SporeCoop::BT_HealTick");
    a_PatientOf = H->sym("SporeCoop::BT_ValidTarget");
    a_AddFood = H->sym("SporeCoop::cTribe_AddFood");
    a_FoodFx = H->sym("Simulator::cTribe::UpdateFoodVisuals");
    a_SessionGet = H->sym("SporeCoop::SocialSession_Get");
    a_CreateHerd = H->sym("Simulator::cGameNounManager::CreateHerd");
    a_HerdEnable = H->sym("Simulator::cHerd::SetEnabled");
    a_CreatureCreate = H->sym("Simulator::cCreatureAnimal::Create");
    spaceext::on_save(before_save); // space_allies' save hook (the one hook on SaveGame)
    if (a_CitizenTD)
        H->hook_create(M, (void*)a_CitizenTD, (void*)h_CitizenTD, (void**)&o_CitizenTD);
    if (a_HealTick && a_PatientOf)
        H->hook_create(M, (void*)a_HealTick, (void*)h_HealTick, (void**)&o_HealTick);
    H->log(M, SK_INFO, "tribe co-op: citizen damage %s, heal %s, duet %s, epic %s", a_CitizenTD ? "on" : "off",
           a_HealTick && a_PatientOf ? "on" : "off", a_SessionGet ? "on" : "off",
           a_CreateHerd && a_HerdEnable && a_CreatureCreate ? "on" : "off");
}

// a member that changed hands (a gift) stayed selected on the giver's machine, so the giver could
// still order the partner's member (play_tribe.py): drop members of others from the own selection
static uint32_t g_unselected;
static void drop_foreign_selection()
{
    void* pt = player_tribe();
    if (!pt)
        return;
    uint8_t me = C.sess->my_id();
    for (void* c : members(pt)) {
        uint8_t o = owner_of(c);
        if (o < 0xFE && o != me && F<uint8_t>(c, kSelectedOff)) {
            vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, 0); // SetSelected(false)
            g_unselected++;
        }
    }
}

void tick(double now)
{
    if (!active())
        return;
    drop_foreign_selection();
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
    coop_tick(now);
}

static uint32_t g_rx, g_rx_skipped, g_resets;

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!g_ok)
        return;
    if (type == net::MSG_TRIBE_STATE) {
        g_rx++;
        if (!is_guest() || game::mode_id() != modes::kTribe)
            g_rx_skipped++;
    }
    if (type == net::MSG_TRIBE_STATE && is_guest() && game::mode_id() == modes::kTribe)
        guest_state(r);
    else if (type == net::MSG_TRIBE_CMD && is_host() && game::mode_id() == modes::kTribe)
        host_command(from, r);
}

// the co-op mechanics' tables of game pointers (chiefs, pincer, epic): a new session or world
// must not inherit them (BUG-0081: a former chief's size was restored through a freed pointer).
// tribe.chiefs gates only the chief and pincer part; the epic part has its own switch
// (tribe.epic_herd) and runs whatever tribe.chiefs says (BUG-0214: one bug per switch)
static void clear_coop_tables(bool restore)
{
    if (g_fix_chiefs) {
        if (restore)
            restore_chief_scales(nullptr); // the chiefs are plain members again after the session
        g_chief_scale.clear();
        g_chief.clear();
        g_chief_saved.clear();
        g_pincer.clear();
        g_pincer_told.clear();
    }
    // the co-op epic ends with the session (BUG-0121: not left in the solo game and its saves);
    // after a world change (restore false) the old world's objects are gone: only forgotten
    if (restore && g_fix_epic_herd && g_epic && game::mode_id() == modes::kTribe && epic_listed())
        game::destroy_creature(g_epic);
    g_epic = nullptr;
    if (restore)
        epic_herd_release();
    g_epic_herd = nullptr;
    g_epic_hitters.clear();
    if (g_fix_epic_herd)
        g_epic_next = 0; // BUG-0067: the first epic of the next session comes after 8 minutes again
}

// BUG-0121: the host saves (pause menu, quit, the world sent to a joining guest): a live co-op epic
// and its herd's spawning must not go into the save, where a load brought it back as an untracked
// epic. The epic leaves without a reward; the next one comes a minute later.
void before_save()
{
    if (!g_ok || !g_fix_epic_herd || !g_epic || game::mode_id() != modes::kTribe)
        return;
    if (epic_listed())
        game::destroy_creature(g_epic);
    g_epic = nullptr;
    g_epic_hitters.clear();
    epic_herd_release();
    g_epic_saved_out++;
    double now = H->time_seconds();
    if (g_epic_next < now + 60)
        g_epic_next = now + 60;
    if (C.notice && is_host())
        C.notice("the epic creature went away");
}

void on_session_end()
{
    clear_coop_tables(true);
    g_split_for.clear();
    g_hm.clear();
    g_hm_by_id.clear();
    g_gm.clear();
    g_gid_of.clear();
    g_player_members_seen = 0;
}

void on_world_change()
{
    g_resets++;
    clear_coop_tables(false); // every citizen of the old world is freed: nothing to restore
    on_session_end();
}

bool center(net::Vec3& pos)
{
    void* t = g_ok ? player_tribe() : nullptr;
    if (!t)
        return false;
    pos = tribe_pos(t);
    return true;
}

void for_each_owned(void (*fn)(void* citizen, uint8_t owner, void* user), void* user)
{
    void* t = active() ? player_tribe() : nullptr;
    if (!t)
        return;
    for (void* c : members(t)) {
        uint8_t o = owner_of(c);
        if (o != 0xFF && o != 0xFE)
            fn(c, o, user);
    }
}

std::string status()
{
    char b[384];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "tribe(host): members=%u cmds_done=%u rejected=%u gifts=%u unselected=%u\n",
                 (unsigned)g_hm.size(), g_cmds_done, g_cmds_rejected, g_gifts, g_unselected);
    else
        snprintf(b, sizeof b,
                 "tribe(guest): mirrors=%u spawned=%u removed=%u cmds_sent=%u rx=%u skipped=%u resets=%u "
                 "local_tribes=%u player_tribe=%p unselected=%u incomplete=%u ai_skipped=%u/%u away_orders=%u local=%d\n",
                 (unsigned)g_gm.size(), g_spawned, g_removed, g_cmds_sent, g_rx, g_rx_skipped, g_resets,
                 (unsigned)all_tribes().size(), player_tribe(), g_unselected, g_incomplete, g_bt_skipped, g_bt_calls, g_away_orders, (int)local_play());
    std::string out = b;
    if (!g_last_cmd.empty())
        out += "tribe last order: " + g_last_cmd + "\n";
    out += "tribe ui mode=" + std::to_string(ui_mode()) + " (0 social, 1 attack)\n";
    if (!g_last_local.empty())
        out += "tribe last local order: #" + std::to_string(g_local_orders) + " " + g_last_local + "\n";
    std::string chiefs;
    for (auto& [o, c] : g_chief)
        chiefs += " " + std::to_string(o) + ":#" + std::to_string(member_id(c));
    snprintf(b, sizeof b,
             "tribe co-op: pincer=%u aura=%u chief_rescues=%u duet=%u heals=%u epics=%u epic_kills=%u epic=%p "
             "chiefs=%s\n",
             g_pincer_hits, g_aura_hits, g_chief_rescues, g_duet_ticks, g_heals, g_epics, g_epic_kills, g_epic,
             chiefs.c_str());
    out += b;
    // the chiefs' size in the game (spatial GetScale / their own scale) and the lifetime checks
    std::string scales;
    for (auto& [o, c] : g_chief) {
        auto it = g_chief_scale.find(c);
        char sb[32];
        snprintf(sb, sizeof sb, " %u:%.2f", o, it != g_chief_scale.end() && it->second > 0 ? game::scale(c) / it->second : 0.f);
        scales += sb;
    }
    snprintf(b, sizeof b,
             "tribe lifetime: chief_restores=%u chief_gone=%u chief_bad_calls=%u spawns_blocked=%u fix_chiefs=%d "
             "fix_gameover=%d knocked_out=%u ko_replaced=%u epic_herd_reused=%u epic_saved_out=%u chief_scale=%s\n",
             g_chief_restores, g_chief_gone, g_chief_bad_calls, g_spawns_blocked, (int)g_fix_chiefs, (int)g_fix_gameover,
             g_ko_shown, g_ko_replaced, g_epic_herd_reused, g_epic_saved_out, scales.c_str());
    return out + b;
}

// ---- debug (tests): orders through the same hooked functions the UI uses ----
static void* local_of(uint16_t id)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm_by_id.find(id);
        return it == g_hm_by_id.end() ? nullptr : it->second;
    }
    auto it = g_gm.find(id);
    return it == g_gm.end() ? nullptr : it->second.c;
}

std::string debug(const std::vector<std::string>& a)
{
    // tests of the co-op mechanics through the game's damage path (host): ids as in coop.tribe
    auto hit = [](void* victim, void* attacker, float dmg) {
        float before = F<float>(victim, 0x5E0);
        // a dead victim takes no more blows: the test loop went on hitting the dying epic (BUG-0171)
        if (before <= 0 || game::is_dead(victim))
            return std::string("hp 0.00 -> 0.00 (dead)");
        // the combatant's TakeDamage (vt 0x18): the same path as a real blow, with our hooks
        // (the push vector: (0,0,0) as the game's own bites and strikes, docs/re/creature.md 3)
        static const float zero[3] = {0, 0, 0}; // the hit direction must not be null
        vcall<int>((uint8_t*)victim + 0x5A8, 0x18 / 4, dmg, 0xFFFFFFFFu, 0, zero,
                   attacker ? (uint8_t*)attacker + 0x5A8 : nullptr);
        char b[64];
        snprintf(b, sizeof b, "hp %.2f -> %.2f", before, F<float>(victim, 0x5E0));
        return std::string(b);
    };
    if (a.size() >= 3 && a[0] == "fix") { // fix chiefs|gameover 0|1: the code before BUG-0081 / BUG-0172 (tests)
        bool on = atoi(a[2].c_str()) != 0;
        if (a[1] == "chiefs")
            g_fix_chiefs = on;
        else if (a[1] == "gameover")
            g_fix_gameover = on;
        else if (a[1] == "ko")
            g_fix_ko = on;
        else if (a[1] == "epic_players")
            g_fix_epic_players = on;
        else if (a[1] == "epic_herd")
            g_fix_epic_herd = on;
        else if (a[1] == "away")
            g_fix_away = on;
        else
            return "usage: coop.tribe fix chiefs|gameover|ko|epic_players|epic_herd|away 0|1";
        return "fix " + a[1] + " " + (on ? "on" : "off");
    }
    if (a.size() >= 2 && a[0] == "drop_chief") { // drop_chief <owner>: the chief leaves the tribe at once
        // (RemoveMember(c, destroy) as guest_state does for a mirror the host no longer sends):
        // BUG-0081, the next chiefs_tick must not call into it
        auto it = g_chief.find((uint8_t)atoi(a[1].c_str()));
        if (it == g_chief.end() || !it->second)
            return "no chief";
        void* c = it->second;
        uint16_t id = member_id(c);
        remove_member(c);
        return "dropped chief #" + std::to_string(id);
    }
    if (a.size() >= 2 && a[0] == "stale_chief") { // stale_chief <id>: as if a former chief's freed block
        // now held this member (another tribe's citizen): the next chiefs_tick must leave it alone
        // (BUG-0081; before the fix it got the old chief's size, 2.0 here)
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        g_chief_scale[c] = 2.f;
        return "stale chief entry for #" + a[1];
    }
    if (a.size() >= 2 && a[0] == "scale") { // scale <id>: the member's size in the game (spatial GetScale)
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        char b[64];
        snprintf(b, sizeof b, "scale %.3f", c ? game::scale(c) : 0.f);
        return c ? std::string(b) : std::string("no such member");
    }
    if (a.size() >= 4 && a[0] == "pincer" && is_host()) { // pincer <victim id> <attacker id> <dmg>
        void* v = local_of((uint16_t)atoi(a[1].c_str())), *at = local_of((uint16_t)atoi(a[2].c_str()));
        if (!v || !at)
            return "no such member";
        return hit(v, at, (float)atof(a[3].c_str()));
    }
    if (!a.empty() && a[0] == "scales") { // the player tribe's members: size in the game / own size (BUG-0067)
        void* pt = game::mode_id() == modes::kTribe ? player_tribe() : nullptr;
        int n = 0, big = 0;
        float mx = 0;
        for (void* c : pt ? members(pt) : std::vector<void*>{}) {
            float own = vcall<float>(c, 0x80 / 4, 0); // GetDesiredScale(int)
            if (own <= 0.01f || own > 10.f)
                continue;
            float r = game::scale(c) / own;
            n++;
            big += r > 1.15f;
            mx = std::max(mx, r);
        }
        char b[96];
        snprintf(b, sizeof b, "scales members=%d big=%d max_ratio=%.2f", n, big, mx);
        return b;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "herds") { // the game's epic herds and epic creatures (BUG-0121)
        struct Cnt {
            uintptr_t is_epic;
            int herds = 0, ours = 0, enabled = 0, epics = 0;
        } n{H->sym("SporeCoop::cCreatureAnimal_IsEpic")};
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* n = (Cnt*)u;
                if (!game::object_alive(o))
                    return true;
                if (noun == game::kHerdNoun && skg::readable(o, 0x21C) && F<uint32_t>(o, 0x88) == kEpicArchetype) {
                    n->herds++;
                    n->ours += is_epic_herd_of_ours(o);
                    n->enabled += F<uint8_t>(o, 0x218) != 0;
                } else if (noun == 0x18EB45E && n->is_epic && !game::is_dead(o) && thiscall<bool>(n->is_epic, o))
                    n->epics++;
                return true;
            },
            &n);
        char b[128];
        snprintf(b, sizeof b, "epic herds=%d quiet_ours=%d enabled=%d live_epics=%d tracked=%p", n.herds, n.ours,
                 n.enabled, n.epics, g_epic);
        return b;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "quiet") { // RE: quiet every epic herd of ours now
        std::string out;
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                if (noun == game::kHerdNoun && is_epic_herd_of_ours(o)) {
                    char b[96];
                    uint8_t e0 = F<uint8_t>(o, 0x218);
                    game::herd_quiet(o);
                    snprintf(b, sizeof b, "herd %p enabled %d -> %d (ours %p)\n", o, e0, F<uint8_t>(o, 0x218), g_epic_herd);
                    *(std::string*)u += b;
                }
                return true;
            },
            &out);
        return out.empty() ? "no herd of ours" : out;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "save") { // as if the game saved now (BUG-0121)
        before_save();
        return "before_save done";
    }
    if (!a.empty() && a[0] == "epic" && is_host()) {
        if (a.size() >= 3 && a[1] == "hit") { // epic hit <attacker id> <dmg>
            void* at = local_of((uint16_t)atoi(a[2].c_str()));
            if (!g_epic || !at || !epic_listed())
                return "no epic / member";
            return hit(g_epic, at, a.size() > 3 ? (float)atof(a[3].c_str()) : 50.f);
        }
        bool ok = spawn_epic();
        uintptr_t is_epic = H->sym("SporeCoop::cCreatureAnimal_IsEpic");
        char b[96];
        snprintf(b, sizeof b, "epic %s %p is_epic=%d hp=%.0f", ok ? "spawned" : "not spawned", g_epic,
                 g_epic && is_epic ? (int)thiscall<bool>(is_epic, g_epic) : -1, g_epic ? F<float>(g_epic, 0x5E0) : 0.f);
        return b;
    }
    if (a.size() >= 3 && a[0] == "select") { // select <id> <0|1>
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, atoi(a[2].c_str()));
        return std::string("selected=") + std::to_string(F<uint8_t>(c, kSelectedOff));
    }
    if (a.size() >= 4 && a[0] == "walk") { // walk <x> <y> <z> : the selected members walk there
        float p[3] = {(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        thiscall<int>(a_WalkSelected, (void*)nullptr, (const float*)p);
        return "walk ordered";
    }
    if (a.size() >= 3 && (a[0] == "act" || a[0] == "act_npc")) { // act <action> <member id | npc id>
        void* t = a[0] == "act" ? local_of((uint16_t)atoi(a[2].c_str())) : npc::proxy((uint16_t)atoi(a[2].c_str()));
        std::vector<void*> sel;
        for (auto& [id, g] : g_gm)
            if (g.c && F<uint8_t>(g.c, kSelectedOff))
                sel.push_back((uint8_t*)g.c + 0xC0);
        for (auto& [c, hm] : g_hm)
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, atoi(a[1].c_str()), t, (void*)nullptr);
        return "action ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ")";
    }
    if (a.size() >= 2 && a[0] == "spawn" && is_host()) { // spawn <n>: test setup, n more adults in the player tribe
        void* pt = player_tribe();
        int n = 0;
        for (int i = 0; pt && i < atoi(a[1].c_str()) && i < 40; i++) {
            void* c = spawn_member(pt, 1);
            if (!c)
                break;
            n++;
        }
        return "spawned " + std::to_string(n);
    }
    if (!a.empty() && a[0] == "trees") { // trees near the village with their raw fields (RE)
        net::Vec3 at;
        center(at);
        std::vector<std::pair<float, std::string>> v;
        for_each_tree([&](void* t, const net::Vec3& p) {
            char b[256];
            float d = net::dist(p, at);
            snprintf(b, sizeof b, "%p d=%.0f pos=(%.1f %.1f %.1f) c=%04X e=%04X 1c=%.2f 48=%08X 4c=%08X 50=%08X 54=%08X 58=%08X 6c=%.2f 78=%08X 7c=%02X\n",
                     t, d, p.x, p.y, p.z, F<uint16_t>(t, 0xC), F<uint16_t>(t, 0xE), F<float>(t, 0x1C), F<uint32_t>(t, 0x48),
                     F<uint32_t>(t, 0x4C), F<uint32_t>(t, 0x50), F<uint32_t>(t, 0x54), F<uint32_t>(t, 0x58), F<float>(t, 0x6C),
                     F<uint32_t>(t, 0x78), F<uint8_t>(t, 0x7C));
            v.push_back({d, b});
        });
        std::sort(v.begin(), v.end());
        std::string out;
        for (size_t i = 0; i < v.size() && i < (a.size() > 1 ? (size_t)atoi(a[1].c_str()) : 25); i++)
            out += v[i].second;
        return out;
    }
    if (a.size() >= 2 && a[0] == "objs") { // objs <noun hex>: objects of a kind near the village, world positions
        struct Q {
            uint32_t noun;
            net::Vec3 at;
            std::vector<std::pair<float, std::string>> v;
        } q{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), {}, {}};
        center(q.at);
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto* q = (Q*)u;
                net::Vec3 p;
                if (n == q->noun && game::object_position(o, p)) {
                    char b[128];
                    float d = net::dist(p, q->at);
                    snprintf(b, sizeof b, "%p alive=%d pos=(%.1f %.1f %.1f) dist=%.1f\n", o, (int)game::object_alive(o), p.x,
                             p.y, p.z, d);
                    q->v.push_back({d, b});
                }
                return true;
            },
            &q);
        std::sort(q.v.begin(), q.v.end());
        std::string out;
        for (size_t i = 0; i < q.v.size() && i < 40; i++)
            out += q.v[i].second;
        return out + std::to_string(q.v.size()) + " objects";
    }
    return "usage: coop.tribe select <id> <0|1> | walk <x> <y> <z> | act <action> <target id> | objs <noun hex>";
}

// the citizen's current behaviour-tree node (ai = c+0xB4C, node id at ai+0x1D8; docs/re/tribe.md B)
static uint32_t bt_node(void* c)
{
    uint8_t* ai = F<uint8_t*>(c, 0xB4C);
    return ai && skg::readable(ai + 0x1D8, 4) ? *(uint32_t*)(ai + 0x1D8) : 0;
}

std::string list()
{
    std::string out;
    char b[240];
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t ti = 0; ti < tribes.size(); ti++) {
        net::Vec3 p = tribe_pos(tribes[ti]);
        auto ms = members(tribes[ti]);
        snprintf(b, sizeof b, "tribe %u%s at (%.0f %.0f %.0f): %u members\n", (unsigned)ti,
                 tribes[ti] == pt ? " (player)" : "", p.x, p.y, p.z, (unsigned)ms.size());
        out += b;
        for (void* c : ms) {
            net::Vec3 cp = game::position(c);
            uint8_t ow = owner_of(c);
            uint16_t id = 0;
            if (C.sess && C.sess->role() == net::Role::Host) {
                auto it = g_hm.find(c);
                id = it == g_hm.end() ? 0 : it->second.id;
            } else {
                auto it = g_gid_of.find(c);
                id = it == g_gid_of.end() ? 0 : it->second;
            }
            net::Vec3 v = game::velocity(c), sv{};
            const drive::State* ds = nullptr;
            if (C.sess && C.sess->role() != net::Role::Host) { // the host's velocity in the snapshot
                auto it = g_gm.find(id);
                if (it != g_gm.end()) {
                    sv = it->second.vel;
                    ds = &it->second.ds;
                }
            }
            snprintf(b, sizeof b,
                     "  #%u %p owner=%d age=%u sel=%u hp=%.1f pos=(%.1f %.1f %.1f) speed=%.1f host_speed=%.1f dead=%d bt=%08X\n",
                     id, c, ow == 0xFF ? -1 : (int)ow, F<uint8_t>(c, kAgeOff), F<uint8_t>(c, kSelectedOff),
                     game::health(c), cp.x, cp.y, cp.z, drive::speed(v), drive::speed(sv), (int)game::is_dead(c),
                     bt_node(c));
            out += b;
            if (ds) { // guests: how the mirror is driven (drive::step)
                snprintf(b, sizeof b, "     drive err=%.2f nudges=%u kept=%u teleports=%u\n", ds->last_err, ds->nudges,
                         ds->nudges_kept, ds->teleports);
                out += b;
            }
        }
    }
    return out;
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [c, h] : g_hm)
            if (game::object_alive(c))
                v.push_back({"t" + std::to_string(h.id), game::position(c), game::orientation(c)});
    } else {
        for (auto& [id, g] : g_gm)
            if (g.c && game::object_alive(g.c))
                v.push_back({"t" + std::to_string(id), game::position(g.c), game::orientation(g.c)});
    }
    return v;
}

int own_selected()
{
    if (!C.sess || !C.sess->connected() || game::mode_id() != modes::kTribe)
        return 0;
    int n = 0;
    uint8_t me = C.sess->my_id();
    if (C.sess->role() == net::Role::Host) {
        for (auto& [c, hm] : g_hm)
            if (hm.owner == me && F<uint8_t>(c, kSelectedOff))
                n++;
    } else
        for (auto& [id, m] : g_gm)
            if (m.c && m.owner == me && F<uint8_t>(m.c, kSelectedOff))
                n++;
    return n;
}

int give_selected(uint8_t to)
{
    if (!C.sess || !C.sess->connected() || game::mode_id() != modes::kTribe || to == C.sess->my_id())
        return 0;
    uint8_t me = C.sess->my_id();
    std::vector<uint16_t> ids;
    if (C.sess->role() == net::Role::Host) {
        for (auto& [c, hm] : g_hm)
            if (hm.owner == me && F<uint8_t>(c, kSelectedOff))
                ids.push_back(hm.id);
        give_on_host(me, to, ids);
    } else {
        for (auto& [id, m] : g_gm)
            if (m.c && m.owner == me && F<uint8_t>(m.c, kSelectedOff))
                ids.push_back(id);
        send_cmd(CMD_GIVE, ids, to, {}, {});
    }
    return (int)ids.size();
}

} // namespace tribe
