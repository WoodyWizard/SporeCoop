// World objects owned by the host: one generic L1 / L2 path for every kind (worldobj.h,
// docs/sync-architecture-plan.md, docs/re/creature.md#creature-world-objects).
#include "worldobj.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/protocol.h"
#include "sporekit_game.hpp"

namespace wobj {

using namespace skg;

static const SkHost* H;
static SkMod* M;
static Ctx C;

static uintptr_t a_FruitSetState, a_FloraGet, a_DropFruit, a_TreeVt, a_StrategyGet, a_ExecuteAction, a_OrnUsed, a_RewardFind, a_RewardHandlerVt, a_HerdSetExtinct, a_HerdSetExtinctFlag;

// ---------------------------------------------------------------- the kinds' common shape
// One object of a kind in this game: its pointer (valid in this frame only), the id the host's game
// gives it (the host's handle in snapshots and requests), its position and its replicated state.
struct Item {
    void* p;
    uint32_t id;
    net::Vec3 pos;
    uint32_t st;
};
using Items = std::vector<Item>;

enum : uint8_t { kStCreature = 1, kStTribe = 2 };

struct Kind {
    const char* name;  // as the divergence detector names the noun (tools/tests/worlddiff.py)
    bool* fix;         // its coop.fix switch (off: each game keeps its own, the old behaviour)
    uint8_t stages;    // kStCreature | kStTribe
    float tol;         // m: a guest's copy is the host's object within this distance
    float range;       // m around a guest: the objects the host sends
    int max;           // objects per snapshot (one packet)
    void (*collect)(Items& out);                          // every object of the kind in this game
    // guest: its copy lags the host's state (the host's object was taken / changed): make it so with the
    // game's own function, no reward for anybody
    bool (*behind)(uint32_t local, uint32_t host);
    void (*apply)(const Item& local, uint32_t host);
    // guest: its copy went from `was` to `now` while the host's has `host`: a player of this game took or
    // used it, the host must do the same (a request)
    bool (*acted)(uint32_t was, uint32_t now, uint32_t host);
    // host: a guest's request on its object (the game's own function, no reward: the guest had it)
    void (*act)(const Item& obj, uint32_t guest_now);
    bool gone_acted;  // a bound copy that vanished from the guest's game was taken there (eaten fruit)
    bool drop_own;    // guest: its own copies the host does not have are removed (apply(local, kGone))
};
constexpr uint32_t kGone = 0xFFFFFFFFu; // the state of an object the host's game no longer has

static void collect_noun(uint32_t noun, Items& out, uint32_t (*read)(void*), bool (*take)(void*) = nullptr)
{
    struct U {
        uint32_t noun;
        Items* out;
        uint32_t (*read)(void*);
        bool (*take)(void*);
    } u{noun, &out, read, take};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            if (noun != u->noun || !game::object_alive(o) || (u->take && !u->take(o)))
                return true;
            Item it{o, game::object_serial(o), {}, 0};
            if (!game::object_position(o, it.pos))
                return true;
            it.st = u->read(o);
            u->out->push_back(it);
            return true;
        },
        &u);
}

// ---------------------------------------------------------------- kind: fruit (BUG-0472)
// cFruit (noun 0x2C9CC91, size 0x170) of the flora: +0x16C state (cFruit_SetState 0xAFFFE0): 0 hanging in a
// tree, 1 falling, 2 on the ground (+0x168 its time there), 3 held by a creature that eats it, 5 / 6 eaten
// (the game takes it out of the list within a frame or two). Hanging fruit are drawn from the tree's mask
// (kind "tree" below) and are made and freed by the flora as it likes: only fruit that left the tree are
// objects of this kind. +0x158 the plant species, +0x160 the tree's index, +0x164 the slot (-1: fallen).
constexpr uint32_t kFruitNoun = 0x02C9CC91;
static bool g_fruit_fix = true;
COOP_FIX("wobj.fruit", "BUG-0472", g_fruit_fix, "creature / tribe: fallen fruit are the host's: one a player eats is gone for everybody");

static uint32_t fruit_state(void* o) { return IsBadReadPtr(o, 0x170) ? 6 : field<uint32_t>(o, 0x16C); }
static bool fruit_loose(void* o) { uint32_t s = fruit_state(o); return s >= 1 && s <= 4; }
static bool fruit_done(uint32_t s) { return s == kGone || s >= 5; }
static void fruit_collect(Items& out) { collect_noun(kFruitNoun, out, fruit_state, fruit_loose); }
static void fruit_eat(void* o)
{
    if (!fruit_done(fruit_state(o)))
        thiscall<void>(a_FruitSetState, o, 5, 0); // what the eat node 0xD87590 does when the meal ends
}
static bool fruit_behind(uint32_t local, uint32_t host) { return fruit_done(host) && !fruit_done(local); }
static void fruit_apply(const Item& l, uint32_t) { fruit_eat(l.p); }
static bool fruit_acted(uint32_t was, uint32_t now, uint32_t host) { return !fruit_done(was) && fruit_done(now) && !fruit_done(host); }
static void fruit_act(const Item& o, uint32_t) { fruit_eat(o.p); }

// ---------------------------------------------------------------- kind: tree (the fruit on the trees, BUG-0472)
// cTreeData (not in the noun list, the flora's patches: docs/re/tribe.md trees) +0x10 position (the planet's:
// the same on every game), +0x48 / +0x4C the fruit left (a bit per slot). A fruit falls with the flora's
// DropFruit 0xB09AE0 thiscall(flora, tree, slot, 1): the bit is cleared and a falling cFruit made.
static bool g_tree_fix = true;
COOP_FIX("wobj.tree", "BUG-0472", g_tree_fix, "creature / tribe: fruit falls from a tree in every game when it falls in one");

static bool is_tree(void* t) { return t && !IsBadReadPtr(t, 0x80) && *(uintptr_t*)t == a_TreeVt; }
static void tree_collect(Items& out)
{
    uint8_t* fl = a_FloraGet ? cdecl_call<uint8_t*>(a_FloraGet) : nullptr;
    if (!fl || !a_TreeVt || IsBadReadPtr(fl + 0x1E5F0, 8))
        return;
    uint8_t* b = field<uint8_t*>(fl, 0x1E5F0);
    uint8_t* e = field<uint8_t*>(fl, 0x1E5F4);
    constexpr size_t kPatch = 0x19A8;
    if (!b || e < b || (size_t)(e - b) % kPatch || (size_t)(e - b) / kPatch > 20000)
        return;
    for (uint8_t* el = b; el < e; el += kPatch) {
        uint8_t* rec = el + 8;
        for (uintptr_t vo : {0x44u, 0x9Cu, 0xF4u}) {
            void** tb = field<void**>(rec, vo);
            void** te = field<void**>(rec, vo + 4);
            if (!tb || te < tb || te - tb > 64 || IsBadReadPtr(tb, (te - tb) * 4))
                continue;
            for (void** it = tb; it < te; ++it)
                if (is_tree(*it)) { // every tree: one that runs out of fruit stays (mask 0)
                    void* t = *it;
                    net::Vec3 q{field<float>(t, 0x10), field<float>(t, 0x14), field<float>(t, 0x18)};
                    // no serial: the host's handle is its place (+0x50 patch key / +0x54 index are not unique)
                    uint32_t id = (uint32_t)(int)std::lround(q.x * 10) * 73856093u ^ (uint32_t)(int)std::lround(q.y * 10) * 19349663u ^
                                  (uint32_t)(int)std::lround(q.z * 10) * 83492791u;
                    out.push_back(Item{t, id, q, field<uint32_t>(t, 0x48)});
                }
        }
    }
}
static void tree_drop(void* t, uint32_t bits)
{
    void* fl = a_FloraGet ? cdecl_call<void*>(a_FloraGet) : nullptr;
    for (int s = 0; fl && s < 32 && is_tree(t); s++)
        if ((bits >> s & 1) && (field<uint32_t>(t, 0x48) >> s & 1))
            thiscall<int>(a_DropFruit, fl, t, s, 1);
}
// a tree that runs out of fruit leaves the list: kGone = no fruit left
static uint32_t tree_mask(uint32_t s) { return s == kGone ? 0 : s; }
static bool tree_behind(uint32_t local, uint32_t host) { return (tree_mask(local) & ~tree_mask(host)) != 0; }
static void tree_apply(const Item& l, uint32_t host) { tree_drop(l.p, tree_mask(l.st) & ~tree_mask(host)); }
static bool tree_acted(uint32_t was, uint32_t now, uint32_t host) { return (tree_mask(was) & ~tree_mask(now) & tree_mask(host)) != 0; }
static void tree_act(const Item& o, uint32_t now) { tree_drop(o.p, tree_mask(o.st) & ~tree_mask(now)); }

// ---------------------------------------------------------------- kind: interactive ornament (BUG-0459 / BUG-0473)
// cInteractiveOrnament (noun 0x3A2511E, size 0x260): bones, skeletons, carcasses, rocks the avatar can use. They
// come from the save: the same objects at the same places (and with the same serials) on every game. The interact
// node 0xD869B0 walks the avatar there, plays the animation, calls 0xC40360 (the ornament's next model, none for
// the fixture's bones), clears +0xA3 and posts 0xD3353638 {creature, ornament, 0} to cCreatureModeStrategy;
// its listener CreatureStageRewardHandler (vt 0x147AD48, slot 0x18 = 0xD47860) looks the ornament's reward up
// (0xD475E0, made from the model's property 0x3D03B03 at the first lookup) and pays it (0xD46BE0). The reward
// {+0x04 kind (6 a part), +0x08 avatar only, +0x0C trigger message (0xA07DDE8F any), +0x10 uses left, +0x14 level,
// +0x24 used up}: the ornament itself does not change (runtime, lane 9: a bone of kind +0x110 = 2 gave a part,
// +0x10 1 -> 0, +0x24 0 -> 1; a second use paid nothing). So the state of an ornament is its reward's +0x24.
// Only ornaments with a reward are of this kind (+0x110 = 2 on the fixture: bones and skeletons).
constexpr uint32_t kOrnNoun = 0x03A2511E;
constexpr uint32_t kOrnMsg = 0xD3353638;
static bool g_orn_fix = true;
COOP_FIX("wobj.ornament", "BUG-0459 BUG-0473", g_orn_fix, "creature: a bone or skeleton one player used is used for everybody (one part, not two)");

static void* reward_handler() // the listener of cCreatureModeStrategy that pays rewards
{
    void* st = a_StrategyGet ? cdecl_call<void*>(a_StrategyGet) : nullptr;
    if (!st || IsBadReadPtr(st, 0xC4) || !a_RewardHandlerVt)
        return nullptr;
    void** b = field<void**>(st, 0xBC);
    void** e = field<void**>(st, 0xC0);
    if (!b || e < b || e - b > 64 || IsBadReadPtr(b, (e - b) * 4))
        return nullptr;
    for (void** it = b; it < e; ++it)
        if (*it && !IsBadReadPtr(*it, 0x20) && *(uintptr_t*)*it == a_RewardHandlerVt)
            return *it;
    return nullptr;
}
static uint8_t* orn_reward(void* o)
{
    void* h = reward_handler();
    if (!h || !a_RewardFind || IsBadReadPtr(o, 0x260))
        return nullptr;
    uint8_t* r = thiscall<uint8_t*>(a_RewardFind, h, kOrnMsg, o);
    return r && !IsBadReadPtr(r, 0x2C) ? r : nullptr;
}
static bool orn_take(void* o) { return orn_reward(o) != nullptr; }
static uint32_t orn_state(void* o)
{
    uint8_t* r = orn_reward(o);
    return r && field<uint8_t>(r, 0x24) ? 1u : 0u;
}
static bool orn_used(uint32_t s) { return s != kGone && (s & 1); }
static void orn_collect(Items& out) { collect_noun(kOrnNoun, out, orn_state, orn_take); }
static void orn_use(void* o) // used up without the reward, as 0xD46BE0 leaves the last use (and the interact node the ornament)
{
    uint8_t* r = orn_reward(o);
    if (!r || field<uint8_t>(r, 0x24))
        return;
    field<int>(r, 0x10) = 0;
    field<uint8_t>(r, 0x24) = 1;
    field<uint8_t>(o, 0xA3) = 0;
}
static bool orn_behind(uint32_t local, uint32_t host) { return orn_used(host) && !orn_used(local); }
static void orn_apply(const Item& l, uint32_t) { orn_use(l.p); }
static bool orn_acted(uint32_t was, uint32_t now, uint32_t host) { return !orn_used(was) && orn_used(now) && !orn_used(host); }
static void orn_act(const Item& o, uint32_t) { orn_use(o.p); }

// ---------------------------------------------------------------- kind: herd (a nest's species extinct, BUG-0460)
// cHerd (noun 0x1BE418E, game::kHerdNoun): +0x34 its home (the save's: the same on every game), +0x84 the avatar's
// own, +0xA4 its species, +0x160 its nest, +0x190 mbExtinction. When a species dies out the host's game runs
// 0xBA4750 from cSpeciesRelationshipManager::ApplyEvent: the relation 6, the "extinct" card, and cHerd_SetExtinct
// 0xC6AED0 on every herd of the species (+0x190 = 1, nest +0x120 = 1: the extinct nest of the minimap). A guest
// gets the host's relationship table (social.cpp, MSG_SPECIES) but writes it, it never runs 0xBA4750: its herds
// and nests stayed alive. Extinction is the host's: a guest only applies it (no request).
static bool g_herd_fix = true;
COOP_FIX("wobj.herd", "BUG-0460", g_herd_fix, "creature: a nest whose species died out in the host's game is extinct in every game");

static void herd_collect(Items& out)
{
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            if (noun != game::kHerdNoun || !game::object_alive(o) || IsBadReadPtr(o, 0x220) || field<uint8_t>(o, 0x84))
                return true;
            void* nest = field<void*>(o, 0x160);
            if (!nest || IsBadReadPtr(nest, 0x130))
                return true; // only nests are on the map
            const float* hp = &field<float>(o, 0x34);
            uint32_t st = (field<uint8_t>(o, 0x190) ? 1u : 0u) | (field<uint8_t>(nest, 0x120) ? 2u : 0u);
            ((Items*)p)->push_back(Item{o, game::object_serial(o), {hp[0], hp[1], hp[2]}, st});
            return true;
        },
        &out);
}
// host state: bit 0 herd +0x190, bit 1 nest +0x120 (runtime, lane 9: a herd whose members the host's avatar killed had
// +0x190 = 1 and its nest +0x120 = 0: +0x190 came first another way, so cHerd_SetExtinct did not touch the nest)
static bool herd_behind(uint32_t local, uint32_t host) { return host != kGone && (host & ~local & 3) != 0; }
static void herd_apply(const Item& l, uint32_t host)
{
    if ((host & 2) && !(l.st & 1))
        thiscall<void>(a_HerdSetExtinct, l.p); // both, as the species' extinction does
    else if (host & 1)
        thiscall<void>(a_HerdSetExtinctFlag, l.p, true);
    void* nest = field<void*>(l.p, 0x160);
    if ((host & 2) && nest && !IsBadReadPtr(nest, 0x130))
        field<uint8_t>(nest, 0x120) = 1; // what cHerd_SetExtinct writes
}
static bool herd_acted(uint32_t, uint32_t, uint32_t) { return false; }
static void herd_act(const Item&, uint32_t) {}

// ---------------------------------------------------------------- the table
static Kind g_kinds[] = {
    {"fruit", &g_fruit_fix, kStCreature | kStTribe, 3.0f, 70.f, 48, fruit_collect, fruit_behind, fruit_apply, fruit_acted, fruit_act, true, true},
    {"tree", &g_tree_fix, kStCreature | kStTribe, 0.5f, 70.f, 48, tree_collect, tree_behind, tree_apply, tree_acted, tree_act, false, false},
    {"herd", &g_herd_fix, kStCreature, 1.0f, 3000.f, 48, herd_collect, herd_behind, herd_apply, herd_acted, herd_act, false, false},
    {"interactive_ornament", &g_orn_fix, kStCreature, 1.5f, 90.f, 40, orn_collect, orn_behind, orn_apply, orn_acted, orn_act, false, false},
};
constexpr int kKinds = (int)(sizeof g_kinds / sizeof g_kinds[0]);
// a snapshot is one unreliable message: 19 bytes + 20 per object within net::Connection::kMaxSingle (1163)
constexpr int kMaxPerSnap = 56;

// ---------------------------------------------------------------- engine
constexpr uint8_t kMsgSnap = 0, kMsgAct = 1;
constexpr double kSnapInterval = 1.0, kGuestInterval = 0.25, kStale = 3.5, kReqRetry = 2.0;
constexpr int kOwnGrace = 3; // snapshots a guest's own copy is seen without a host partner before it goes

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

static uint8_t stage_bit()
{
    uint32_t m = game::mode_id();
    return m == modes::kCreature ? kStCreature : m == modes::kTribe ? kStTribe : 0;
}

struct Bind {               // guest: a host object and this game's copy of it
    void* p = nullptr;      // the copy (compared with this frame's collect only, never dereferenced)
    uint32_t serial = 0;    // the copy's id in this game
    uint32_t host_st = 0;   // the host's state in the last snapshot
    uint32_t local_st = 0;  // the copy's state when last looked at
    net::Vec3 host_pos{};
    double req_at = -100;   // the last request sent for it
};
struct Snap {
    net::Vec3 c{};
    float r = 0;
    double at = -100;
    std::map<uint32_t, std::pair<net::Vec3, uint32_t>> objs; // host id -> (pos, state)
};
struct GuestKind {
    std::map<uint32_t, Bind> binds;                   // host id -> copy
    std::map<std::pair<void*, uint32_t>, int> own;    // copies without a host partner -> snapshots seen so
    Snap snap;
};
static GuestKind g_g[kKinds];
static uint32_t g_epoch = 0;
static double g_last_tx = 0, g_last_guest = 0;
struct Stat {
    uint32_t tx = 0, rx = 0, bound = 0, applied = 0, dropped = 0, req_tx = 0, req_rx = 0, acted = 0, missing = 0;
};
static Stat g_st[kKinds];

static void guest_reset()
{
    for (auto& g : g_g) {
        g.binds.clear();
        g.own.clear();
        g.snap = Snap{};
    }
}

static void host_tick(double now)
{
    if (now - g_last_tx < kSnapInterval)
        return;
    g_last_tx = now;
    uint8_t sb = stage_bit();
    for (int k = 0; k < kKinds; k++) {
        Kind& K = g_kinds[k];
        if (!*K.fix || !(K.stages & sb) || K.max > kMaxPerSnap)
            continue;
        Items all;
        K.collect(all);
        for (auto& p : C.sess->players()) {
            if (p.id == 0 || (C.in_place && !C.in_place(p.id)))
                continue;
            net::Vec3 c;
            if (!C.player_pos || !C.player_pos(p.id, c) || (c.x == 0 && c.y == 0 && c.z == 0)) {
                if (!(sb == kStTribe && C.stage_center && C.stage_center(c)))
                    continue;
            }
            std::vector<std::pair<float, const Item*>> close;
            for (auto& it : all) {
                float d = net::dist(it.pos, c);
                if (d < K.range)
                    close.push_back({d, &it});
            }
            std::sort(close.begin(), close.end(), [](auto& a, auto& b) { return a.first < b.first; });
            float r = K.range;
            if (close.size() > (size_t)K.max) { // the guest knows which area the snapshot covers
                r = close[K.max].first;
                close.resize(K.max);
            }
            net::Writer w;
            w.u8(kMsgSnap);
            w.u8((uint8_t)k);
            w.f32(c.x);
            w.f32(c.y);
            w.f32(c.z);
            w.f32(r);
            w.u8((uint8_t)close.size());
            for (auto& [d, it] : close) {
                w.u32(it->id);
                w.f32(it->pos.x);
                w.f32(it->pos.y);
                w.f32(it->pos.z);
                w.u32(it->st);
            }
            C.sess->send_unreliable_to(p.id, net::MSG_WOBJ, w);
            g_st[k].tx++;
        }
    }
}

static void send_act(int k, uint32_t id, const net::Vec3& pos, uint32_t now)
{
    net::Writer w;
    w.u8(kMsgAct);
    w.u8((uint8_t)k);
    w.u32(id);
    w.f32(pos.x);
    w.f32(pos.y);
    w.f32(pos.z);
    w.u32(now);
    C.sess->send_reliable_to(0, net::MSG_WOBJ, w);
    g_st[k].req_tx++;
}

// guest: bind the host's objects to this game's copies, apply the host's state, remove own copies,
// and ask the host for what a player of this game did (every kGuestInterval and on each snapshot)
static void reconcile(int k, double now, bool fresh)
{
    Kind& K = g_kinds[k];
    GuestKind& G = g_g[k];
    Items local;
    K.collect(local);
    const Snap& S = G.snap;
    float inner = S.r - K.tol; // copies here must have a host partner
    std::map<std::pair<void*, uint32_t>, Item*> by_key;
    for (auto& it : local)
        by_key[{it.p, it.id}] = &it;
    std::set<std::pair<void*, uint32_t>> used;
    // bound copies: still here? what did this game do with them?
    for (auto bi = G.binds.begin(); bi != G.binds.end();) {
        Bind& b = bi->second;
        auto f = by_key.find({b.p, b.serial});
        if (f == by_key.end()) { // gone from this game
            if (K.gone_acted && K.acted(b.local_st, kGone, b.host_st) && now - b.req_at > kReqRetry) {
                send_act(k, bi->first, b.host_pos, kGone);
                b.req_at = now;
            }
            bi = G.binds.erase(bi);
            continue;
        }
        Item& l = *f->second;
        used.insert(f->first);
        if (K.acted(b.local_st, l.st, b.host_st) && now - b.req_at > kReqRetry) {
            send_act(k, bi->first, b.host_pos, l.st);
            b.req_at = now;
        }
        b.local_st = l.st;
        ++bi;
    }
    if (!fresh)
        return;
    // the snapshot: new binds, the host's state, the host's gone objects
    for (auto& [id, ps] : S.objs) {
        auto bi = G.binds.find(id);
        if (bi == G.binds.end()) {
            Item* best = nullptr;
            float bd = K.tol;
            for (auto& l : local) {
                float d = net::dist(l.pos, ps.first);
                if (d < bd && !used.count({l.p, l.id})) {
                    bd = d;
                    best = &l;
                }
            }
            if (!best) {
                g_st[k].missing++;
                continue;
            }
            used.insert({best->p, best->id});
            Bind b;
            b.p = best->p;
            b.serial = best->id;
            b.local_st = best->st;
            bi = G.binds.emplace(id, b).first;
            g_st[k].bound++;
        }
        Bind& b = bi->second;
        b.host_st = ps.second;
        b.host_pos = ps.first;
        auto f = by_key.find({b.p, b.serial});
        if (f != by_key.end() && K.behind(f->second->st, ps.second)) {
            K.apply(*f->second, ps.second);
            g_st[k].applied++;
        }
    }
    for (auto bi = G.binds.begin(); bi != G.binds.end();) {
        auto f = by_key.find({bi->second.p, bi->second.serial});
        if (S.objs.count(bi->first) || f == by_key.end()) {
            ++bi;
            continue;
        }
        if (net::dist(f->second->pos, S.c) < inner) { // the host's game no longer has it
            if (K.behind(f->second->st, kGone)) {
                K.apply(*f->second, kGone);
                g_st[k].applied++;
            }
        }
        bi = G.binds.erase(bi); // (or out of the area)
    }
    std::map<std::pair<void*, uint32_t>, int> own;
    for (auto& l : local) {
        std::pair<void*, uint32_t> key{l.p, l.id};
        if (used.count(key) || net::dist(l.pos, S.c) >= inner)
            continue;
        int n = 1;
        auto o = G.own.find(key);
        if (o != G.own.end())
            n = o->second + 1;
        if (K.drop_own && n >= kOwnGrace && K.behind(l.st, kGone)) {
            K.apply(l, kGone);
            g_st[k].dropped++;
            continue;
        }
        own[key] = n;
    }
    G.own.swap(own);
}

static void guest_tick(double now)
{
    if (g_epoch != game::world_epoch()) {
        guest_reset();
        g_epoch = game::world_epoch();
    }
    if (now - g_last_guest < kGuestInterval)
        return;
    g_last_guest = now;
    uint8_t sb = stage_bit();
    bool away = C.host_away && C.host_away();
    for (int k = 0; k < kKinds; k++) {
        GuestKind& G = g_g[k];
        if (!*g_kinds[k].fix || !(g_kinds[k].stages & sb) || away || now - G.snap.at > kStale) {
            G.binds.clear();
            G.own.clear();
            continue;
        }
        reconcile(k, now, false);
    }
}

static void on_snap(net::Reader& r, double now)
{
    uint8_t k = r.u8();
    net::Vec3 c{r.f32(), r.f32(), r.f32()};
    float rad = r.f32();
    uint8_t n = r.u8();
    if (!r.ok() || k >= kKinds || n > g_kinds[k].max || !std::isfinite(rad) || rad < 0 || rad > 1000.f)
        return;
    Snap s;
    s.c = c;
    s.r = rad;
    s.at = now;
    for (int i = 0; i < n; i++) {
        uint32_t id = r.u32();
        net::Vec3 p{r.f32(), r.f32(), r.f32()};
        uint32_t st = r.u32();
        if (!r.ok())
            return;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || p.x * p.x + p.y * p.y + p.z * p.z > 1e8f)
            continue;
        s.objs[id] = {p, st};
    }
    Kind& K = g_kinds[k];
    if (!*K.fix || !(K.stages & stage_bit()) || (C.host_away && C.host_away()) || g_epoch != game::world_epoch())
        return;
    g_st[k].rx++;
    g_g[k].snap = std::move(s);
    reconcile(k, now, true);
}

static void on_act(uint8_t from, net::Reader& r)
{
    uint8_t k = r.u8();
    uint32_t id = r.u32();
    net::Vec3 p{r.f32(), r.f32(), r.f32()};
    uint32_t now = r.u32();
    if (!r.ok() || k >= kKinds)
        return;
    Kind& K = g_kinds[k];
    if (!*K.fix || !(K.stages & stage_bit()))
        return;
    g_st[k].req_rx++;
    Items all;
    K.collect(all);
    const Item* hit = nullptr;
    for (auto& it : all)
        if (it.id == id && net::dist(it.pos, p) < K.tol * 4 + 5.f)
            hit = &it;
    if (!hit)
        return; // gone already (eaten here too, or out of the game)
    if (K.acted(hit->st, now, hit->st) || K.behind(hit->st, now)) {
        K.act(*hit, now);
        g_st[k].acted++;
        H->log(M, SK_INFO, "wobj: player %u took %s %u: done here too", from, K.name, id);
    }
}

void tick(double now)
{
    if (!C.sess)
        return;
    if (is_host()) {
        if (stage_bit())
            host_tick(now);
    } else if (is_guest())
        guest_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_WOBJ)
        return;
    uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (kind == kMsgSnap && !is_host() && from == 0)
        on_snap(r, H->time_seconds());
    else if (kind == kMsgAct && is_host())
        on_act(from, r);
}

void on_session_end() { guest_reset(); }
void on_world_change() { guest_reset(); }

std::string status()
{
    std::string s = "wobj role=";
    s += is_host() ? "host" : is_guest() ? "guest" : "none";
    s += "\n";
    char b[320];
    for (int k = 0; k < kKinds; k++) {
        const Stat& t = g_st[k];
        snprintf(b, sizeof b, "kind %s fix=%d binds=%zu own=%zu snap_n=%zu snap_r=%.0f tx=%u rx=%u bound=%u applied=%u dropped=%u missing=%u req_tx=%u req_rx=%u acted=%u\n",
                 g_kinds[k].name, (int)*g_kinds[k].fix, g_g[k].binds.size(), g_g[k].own.size(), g_g[k].snap.objs.size(), g_g[k].snap.r,
                 t.tx, t.rx, t.bound, t.applied, t.dropped, t.missing, t.req_tx, t.req_rx, t.acted);
        s += b;
    }
    return s;
}

// ---------------------------------------------------------------- tests
static int kind_of(const char* n)
{
    for (int k = 0; k < kKinds; k++)
        if (!strcmp(g_kinds[k].name, n) || !strncmp(g_kinds[k].name, n, strlen(n)))
            return k;
    return -1;
}

static int c_wobj(void*, int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "";
    void* av = game::avatar();
    net::Vec3 me{};
    if (av)
        me = game::position(av);
    else if (C.stage_center)
        C.stage_center(me);
    if (!strcmp(a, "list") && argc > 2) { // list <kind> [r] [x y z]: this game's objects of a kind
        int k = kind_of(argv[2]);
        if (k < 0) {
            H->out_printf(out, "no such kind\n");
            return 1;
        }
        float r = argc > 3 ? (float)atof(argv[3]) : 40.f;
        if (argc > 6)
            me = {(float)atof(argv[4]), (float)atof(argv[5]), (float)atof(argv[6])};
        Items all;
        g_kinds[k].collect(all);
        std::sort(all.begin(), all.end(), [&](const Item& x, const Item& y) { return net::dist(x.pos, me) < net::dist(y.pos, me); });
        int n = 0;
        for (auto& it : all) {
            float d = net::dist(it.pos, me);
            if (d > r || n >= 200)
                break;
            uint32_t host = 0;
            for (auto& [id, b] : g_g[k].binds)
                if (b.p == it.p && b.serial == it.id)
                    host = id;
            H->out_printf(out, "%s %p id=%u pos=(%.2f %.2f %.2f) st=%X dist=%.1f bound_to=%u\n", g_kinds[k].name, it.p, it.id, it.pos.x, it.pos.y,
                          it.pos.z, it.st, d, host);
            n++;
        }
        H->out_printf(out, "end n=%d\n", n);
        return 0;
    }
    if (!strcmp(a, "use") && argc > 2) { // use <ornament>: the avatar uses it the game's way (OrnamentUsed + 0xD3353638)
        void* o = (void*)strtoul(argv[2], nullptr, 16);
        if (!av || IsBadReadPtr(o, 0x260) || game::noun_id(o) != kOrnNoun || !a_StrategyGet || !a_ExecuteAction || !a_OrnUsed) {
            H->out_printf(out, "no avatar or no such ornament\n");
            return 1;
        }
        void* st = cdecl_call<void*>(a_StrategyGet);
        int r = thiscall<int>(a_OrnUsed, o);
        field<uint8_t>(o, 0xA3) = 0;
        uint32_t msg[3] = {(uint32_t)(uintptr_t)av, (uint32_t)(uintptr_t)o, 0};
        if (st)
            thiscall<void>(a_ExecuteAction, st, kOrnMsg, (void*)msg);
        H->out_printf(out, "used (next model %d) state=%X reward=%p\n", r, orn_state(o), (void*)orn_reward(o));
        return 0;
    }
    if (!strcmp(a, "eat") && argc > 2) { // eat <fruit>: eaten here (cFruit_SetState 5, as the eat node ends)
        void* o = (void*)strtoul(argv[2], nullptr, 16);
        if (IsBadReadPtr(o, 0x170) || game::noun_id(o) != kFruitNoun || !a_FruitSetState) {
            H->out_printf(out, "no such fruit\n");
            return 1;
        }
        fruit_eat(o);
        H->out_printf(out, "eaten state=%u\n", fruit_state(o));
        return 0;
    }
    if (!strcmp(a, "drop") && argc > 3) { // drop <tree> <slot>: a fruit falls (the flora's DropFruit)
        void* t = (void*)strtoul(argv[2], nullptr, 16);
        if (!is_tree(t) || !a_DropFruit || !a_FloraGet) {
            H->out_printf(out, "no such tree\n");
            return 1;
        }
        uint32_t before = field<uint32_t>(t, 0x48);
        tree_drop(t, 1u << atoi(argv[3]));
        H->out_printf(out, "mask %X -> %X\n", before, field<uint32_t>(t, 0x48));
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    a_FruitSetState = H->sym("SporeCoop::cFruit_SetState");
    a_FloraGet = H->sym("SporeCoop::Flora_Get");
    a_DropFruit = H->sym("SporeCoop::Flora_DropFruit");
    a_TreeVt = H->sym("Simulator::cTreeData::vftable");
    a_StrategyGet = H->sym("App::cCreatureModeStrategy::Get");
    a_ExecuteAction = H->sym("App::cCreatureModeStrategy::ExecuteAction");
    a_OrnUsed = H->sym("SporeCoop::cInteractiveOrnament_NextModel");
    a_RewardFind = H->sym("SporeCoop::CreatureReward_Find");
    a_RewardHandlerVt = H->sym("SporeCoop::CreatureStageRewardHandler_vftable");
    a_HerdSetExtinct = H->sym("SporeCoop::cHerd_SetExtinct");
    a_HerdSetExtinctFlag = H->sym("SporeCoop::cHerd_SetExtinction");
    if (!a_FruitSetState || !a_FloraGet || !a_DropFruit || !a_TreeVt || !a_OrnUsed || !a_RewardFind || !a_RewardHandlerVt || !a_HerdSetExtinct || !a_HerdSetExtinctFlag)
        for (auto& k : g_kinds)
            k.stages = 0; // a symbol missing on this build: every game keeps its own (logged below)
    if (!a_FruitSetState || !a_DropFruit || !a_OrnUsed)
        H->log(M, SK_WARN, "wobj: symbols missing, world objects stay per game");
    H->command_add(M, "coop.wobj",
                   "coop.wobj [list <kind> [r] [x y z] | use <ornament> | eat <fruit> | drop <tree> <slot>] - the host's world objects (fruit, trees, ornaments) on every game (BUG-0472, BUG-0459)",
                   c_wobj, nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace wobj
