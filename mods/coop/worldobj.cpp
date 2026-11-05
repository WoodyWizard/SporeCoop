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

#include "fields.h"
#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "realign.h"
#include "net/protocol.h"
#include "sporekit_game.hpp"
#include "tribe.h"

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
    uint32_t cls = 0; // BUG-0490: a kind with `classes`: a copy is bound only to a host object of its class
};
using Items = std::vector<Item>;

enum : uint8_t { kStCreature = 1, kStTribe = 2, kStCiv = 4, kStSpace = 8 }; // kStSpace: on a planet (BUG-0490)

// BUG-0476: a moving kind's guest copy follows the host's object: where the host's was (the last snapshot),
// its velocity (from the last two snapshots), and the follower's own memory (the last walk goal given)
struct Follow {
    net::Vec3 goal{};
    double at = -100;
};
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
    // BUG-0476: a kind that moves (city citizens): guest, on each snapshot, every bound copy is brought to the
    // host's object (nullptr: the kind does not move); a bind is kept while both exist, however far apart
    void (*follow)(const Item& local, const net::Vec3& host_pos, const net::Vec3& host_vel, double age, Follow& st,
                   double now) = nullptr;
    double every = 0; // s between snapshots (0: kSnapInterval)
    // guest: the host has an object this game has no copy of: make one (nullptr: the kind only binds)
    int (*make)(const net::Vec3& pos, uint32_t host) = nullptr; // 1 made, 0 failed, -1 not now (no try counted)
    // guest: an own copy the host has no partner of (each snapshot); true: keep it (not dropped)
    bool (*own)(const Item& local, double now) = nullptr;
    // host: a guest's request for an object this game has not (id 0): make it
    void (*buy)(uint8_t from, const net::Vec3& pos, uint32_t st) = nullptr;
    // BUG-0490: the objects carry a class (Item::cls, one more u32 on the wire): a copy binds only to its class
    bool classes = false;
    float vmax = 15.f; // m/s: a faster step between two snapshots is a jump (no velocity)
    // BUG-0490: guest, every frame, every bound copy is drawn where the host's is (nullptr: only `follow`)
    void (*drive)(const Item& local, const net::Vec3& host_pos, const net::Vec3& host_vel, double age, uint32_t host_st) = nullptr;
};
constexpr uint32_t kGone = 0xFFFFFFFFu; // the state of an object the host's game no longer has

static void collect_noun(uint32_t noun, Items& out, uint32_t (*read)(void*), bool (*take)(void*) = nullptr,
                         uint32_t (*cls)(void*) = nullptr)
{
    struct U {
        uint32_t noun;
        Items* out;
        uint32_t (*read)(void*);
        bool (*take)(void*);
        uint32_t (*cls)(void*);
    } u{noun, &out, read, take, cls};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            if (noun != u->noun || !game::object_alive(o) || (u->take && !u->take(o)))
                return true;
            Item it{o, game::object_serial(o), {}, 0};
            if (!game::object_position(o, it.pos))
                return true;
            it.st = u->read(o);
            if (u->cls)
                it.cls = u->cls(o);
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

// ---------------------------------------------------------------- kind: tribe tool (BUG-0514, BUG-0523, BUG-0524)
// cTribeTool (noun 0x18C8F0C) of the tribes (tribe +0x36C): the weapon racks, instrument stands, the healer's
// hut, the fire pit. The palette's placement makes one in the game of the player who bought it, an NPC tribe's
// in each game on its own (docs/re/tribe.md 5, #tribe-tools-all): a rack the host bought was in the host's game
// only, so a guest could not click it and its members never got the weapon (the guest's orders run on the
// host, on the host's rack, found by position). The tools are the host's: a guest gets a copy of every host tool
// it has none of (the same type at the host's place, upright), so its click on the rack becomes the host's
// GrabTool there.
// State: the tool type (vt 0x58) | the host's index of its tribe << 8 | 1 << 16 for the player tribe.
// BUG-0523 (`wobj.tribe_tools_all`): every tribe's tools, not the player tribe's only; a tool the palette drags
// is not sent (it is not placed yet); a guest's own tool the host has not (another spot, a tribe the host has
// not) goes with the game's RemoveTool, the host's is made in the mapped tribe.
// BUG-0524 (`wobj.tribe_tool_buy`): a tool a guest bought for the player tribe (its own copy without a host
// partner) is a request to the host, which places the same there (the old one of the type goes, as the
// palette does); the guest's copy is bound to it at the next snapshot. The food was paid in the guest's game
// (the shared food ledger takes its change to the host).
static bool g_tool_fix = true, g_tool_all_fix = true, g_tool_buy_fix = true;
COOP_FIX("wobj.tribe_tool", "BUG-0514", g_tool_fix, "tribe: a tool building (weapon rack, ...) the host built is in every player's game");
COOP_FIX("wobj.tribe_tools_all", "BUG-0523", g_tool_all_fix,
         "tribe: every tribe's tools stand where the host's stand (NPC tribes too); a guest's own copy elsewhere goes");
COOP_FIX("wobj.tribe_tool_buy", "BUG-0524", g_tool_buy_fix, "tribe: a tool a guest buys and places is placed in the host's game too");
constexpr uint32_t kToolPlayer = 1u << 16;
static uint32_t tool_state(int type, int tribe, bool player)
{
    return (uint32_t)(type & 0xFF) | (uint32_t)(tribe & 0xFF) << 8 | (player ? kToolPlayer : 0);
}
static bool is_guest();
// guest: the player tribe's tools this game had when the host's first snapshot came, and the copies it made: never a
// purchase (a copy left from an earlier session or world is not bought again on the host)
static std::set<std::pair<void*, uint32_t>> g_tool_base;
static bool g_tool_base_done;
static void tool_collect(Items& out)
{
    if (!g_tool_base_done && is_guest()) { // the first look after a reset (the first snapshot's reconcile)
        g_tool_base_done = true;
        tribe::for_each_tool(
            false, [](void* t, int, int, bool, const net::Vec3&, void*) { g_tool_base.insert({t, game::object_serial(t)}); }, nullptr);
    }
    tribe::for_each_tool(
        g_tool_all_fix,
        [](void* t, int type, int ix, bool player, const net::Vec3& pos, void* u) {
            ((Items*)u)->push_back(Item{t, game::object_serial(t), pos, tool_state(type, ix, player)});
        },
        &out);
}
static uint32_t g_tools_removed, g_tool_buys_tx, g_tool_buys_rx, g_tool_buys_done;
static bool tool_behind(uint32_t local, uint32_t host) { return g_tool_all_fix && host == kGone && local != kGone; }
static void tool_apply(const Item& l, uint32_t host)
{
    if (host != kGone || !tribe::remove_tool(l.p))
        return;
    g_tools_removed++;
    H->log(M, SK_INFO, "wobj: removed a tribe tool type %u the host has not at (%.1f %.1f %.1f)", l.st & 0xFF, l.pos.x, l.pos.y, l.pos.z);
}
static bool tool_acted(uint32_t, uint32_t, uint32_t) { return false; }
static void tool_act(const Item&, uint32_t) {}
// 1 made, 0 failed (a try), -1 not now (no try counted)
static int tool_make(const net::Vec3& pos, uint32_t host)
{
    int type = (int)(host & 0xFF), ix = (int)(host >> 8 & 0xFF);
    if (host == kGone || type < 1 || type > 10)
        return 0;
    void* t = nullptr;
    if (!g_tool_all_fix) // BUG-0514 alone: the player tribe's
        t = (host & kToolPlayer) || host <= 10 ? tribe::make_player_tool(type, pos) : nullptr;
    else {
        // this game's tribe has one of the type elsewhere (its own, to be removed, or one the palette drags:
        // a guest's purchase on its way to the host): one per type, as the palette keeps it
        int has = tribe::has_tool_type(ix, type);
        if (has != 0)
            return -1; // (-1: no such tribe here yet)
        t = tribe::make_tool(ix, type, pos);
    }
    if (t)
        g_tool_base.insert({t, game::object_serial(t)});
    if (t)
        H->log(M, SK_INFO, "wobj: made the host's tribe tool type %d (tribe %d) at (%.1f %.1f %.1f)", type, ix, pos.x, pos.y, pos.z);
    return t ? 1 : 0;
}
static void send_act(int k, uint32_t id, const net::Vec3& pos, uint32_t now);
static int tool_kind();
// guest: its own tool without a host partner; true: keep it (a purchase on its way to the host)
struct ToolBuy {
    double first = 0, sent = -100;
};
static std::map<std::pair<void*, uint32_t>, ToolBuy> g_tool_buys;
static bool tool_own(const Item& l, double now)
{
    if (!(l.st & kToolPlayer))
        return false;
    if (!g_tool_buy_fix)
        return true; // the old behaviour: it stays in this game only
    if (g_tool_base.count({l.p, l.id}))
        return false; // not bought in this session: an own copy as any other
    ToolBuy& b = g_tool_buys[{l.p, l.id}];
    if (b.first == 0)
        b.first = now;
    if (now - b.first > 20.0)
        return false; // the host never placed it (refused): this game drops it as any own copy
    if (now - b.sent >= 2.0) {
        b.sent = now;
        send_act(tool_kind(), 0, l.pos, l.st);
        g_tool_buys_tx++;
        H->log(M, SK_INFO, "wobj: bought a tribe tool type %u at (%.1f %.1f %.1f): asking the host", l.st & 0xFF, l.pos.x, l.pos.y, l.pos.z);
    }
    return true;
}
// host: a guest's purchase
static void tool_buy(uint8_t from, const net::Vec3& pos, uint32_t st)
{
    g_tool_buys_rx++;
    if (!g_tool_buy_fix || !(st & kToolPlayer))
        return;
    if (tribe::buy_player_tool((int)(st & 0xFF), pos)) {
        g_tool_buys_done++;
        H->log(M, SK_INFO, "wobj: player %u bought a tribe tool type %u at (%.1f %.1f %.1f): placed here too", from, st & 0xFF, pos.x, pos.y, pos.z);
    }
}

// ---------------------------------------------------------------- the table
// ---------------------------------------------------------------- kind: citizen (Civ city citizens, BUG-0476)
// cCreatureCitizen (noun 0x18EB4B7) of the cities: each game spawns and walks its own (an idle walk 0xD57680
// picked by Citizen_OffStageCheck outside Tribe, docs/re/civ.md#civ-roads-citizens), so a fifth to a half of
// them were elsewhere on the guest. The host's citizens near a guest (its pointer, else the host's capital)
// are sent like the other kinds; a guest binds its own citizens to them by position (both games spawn the
// same number per city) and walks each bound one to the host's (the game's own MoveTo, so it animates), or
// puts it there when it is far. Their size is the fields kind `civ_citizen` (below, BUG-0475's Civ half).
// `coop.fix civ.citizens off`: each game walks its own (0.7.4).
constexpr uint32_t kCitizenNoun = 0x018EB4B7;
static bool g_citizen_fix = true;
COOP_FIX("civ.citizens", "BUG-0476", g_citizen_fix, "civ: city citizens walk where the host's walk (the host's citizens near a guest)");
static uint32_t citizen_state(void*) { return 0; }
static void citizen_collect(Items& out) { collect_noun(kCitizenNoun, out, citizen_state); }
static bool citizen_behind(uint32_t, uint32_t) { return false; }
static void citizen_apply(const Item&, uint32_t) {}
static bool citizen_acted(uint32_t, uint32_t, uint32_t) { return false; }
static void citizen_act(const Item&, uint32_t) {}
// m: walked when farther than kCitizenWalk from where the host's is now (its last place moved on with its
// velocity), put there over kCitizenPut (the detector's 15 m). A walk goes kCitizenLead s ahead of the host's
// and is given again only when that goal moved 2 m or after 1.5 s (a MoveTo every tick hardly walks: BUG-0281)
constexpr float kCitizenWalk = 1.5f, kCitizenPut = 12.f, kCitizenLead = 1.5f;
static uint32_t g_cit_walks, g_cit_puts, g_cit_far;
// BUG-0553: each game keeps a pool of citizens per planet and moves it to the city near its own camera
// (BUG-0526). A bound copy far from the host's (kCitizenFar, no walk lags that much) was moved by the guest's
// game to the city the guest looks at: left there (it was put back at the host's city, the guest's city empty).
// `coop.fix civ.citizen_far off`: put back however far (0.7.5).
constexpr float kCitizenFar = 60.f;
static bool g_cit_far_fix = true;
COOP_FIX("civ.citizen_far", "BUG-0553", g_cit_far_fix, "civ: a guest's citizens the game moved to the city its camera is at stay there");

static void citizen_follow(const Item& l, const net::Vec3& hp, const net::Vec3& v, double age, Follow& st, double now)
{
    float t = (float)std::min(age, 1.5);
    net::Vec3 at{hp.x + v.x * t, hp.y + v.y * t, hp.z + v.z * t};
    float d = net::dist(l.pos, at);
    if (g_cit_far_fix && d > kCitizenFar) {
        g_cit_far++;
        return;
    }
    if (d > kCitizenPut) {
        game::stop(l.p);
        game::place(l.p, at, game::orientation(l.p));
        st.at = -100;
        g_cit_puts++;
        return;
    }
    float sp = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (d <= kCitizenWalk && sp < 0.3f)
        return; // there, and the host's stands: the game's own idle
    net::Vec3 goal = sp >= 0.3f ? net::Vec3{at.x + v.x * kCitizenLead, at.y + v.y * kCitizenLead, at.z + v.z * kCitizenLead} : at;
    if (net::dist(goal, st.goal) < 2.f && now - st.at < 1.5)
        return;
    game::move_to(l.p, goal, 0.5f);
    st.goal = goal;
    st.at = now;
    g_cit_walks++;
}

// ---------------------------------------------------------------- kinds on a planet in Space (BUG-0490)
// docs/re/space.md#space-planet-life. A colony planet's citizens (the same cCreatureCitizen noun) are a pool
// of 150 that each game puts at the cities near its own camera (measured: city 4's citizens moved to city 0
// when only the guest's ship went there); most stand, some run about (~5 m/s); 123 of 150 were within 3 m of
// a partner, the rest elsewhere, each of its own size. Citizens near a guest the host's
// game also has: bound and put where the host's are every frame (space_citizen_drive), their size by the
// fields kind `space_citizen`. Citizens at a city the host's camera never came near exist in the guest's
// game only: the host has none to bind (level of detail, not compared by the detector).
// The colony's vehicles (cVehicle, noun 0x18C6DE8, 40 on the fixture's planet on both games, ~20 m/s)
// are driven by each game's own planner: 25 of 40 were more than 15 m from any partner. The host's are
// sent (the whole planet, every 0.4 s) with their orientation; a guest binds its own of the same class
// (political id, locomotion, purpose) and draws each where the host's is every frame (StopMovement +
// Teleport, as civ.cpp moves its vehicle mirrors). `coop.fix space.citizens off` / `space.vehicles off`:
// each game keeps its own (0.7.5).
// Space citizens: most stand, some run about (~5 m/s): a MoveTo hardly moved them (500 walks in 25 s, 4-10 of 19
// still 3+ m off), a put every 0.25 s lagged 4 m behind the runners. So, like the vehicles, every frame a bound one
// is put where the host's is now (its last place moved on with its velocity)
static uint32_t g_space_cit_puts;
static void space_citizen_drive(const Item& l, const net::Vec3& hp, const net::Vec3& v, double age, uint32_t)
{
    float t = (float)std::min(age, 1.0);
    net::Vec3 want{hp.x + v.x * t, hp.y + v.y * t, hp.z + v.z * t};
    if (net::dist(l.pos, want) <= 0.3f)
        return;
    game::stop(l.p);
    game::place(l.p, want, game::orientation(l.p));
    g_space_cit_puts++;
}
static bool g_space_cit_fix = true;
COOP_FIX("space.citizens", "BUG-0490", g_space_cit_fix, "space: a colony's citizens stand where the host's stand (the host's citizens near a guest on its planet)");
static bool g_space_veh_fix = true;
COOP_FIX("space.vehicles", "BUG-0490", g_space_veh_fix, "space: a colony's vehicles drive where the host's drive (on the planet both players are on)");
constexpr uint32_t kVehicleNoun = 0x018C6DE8;
constexpr uintptr_t kVehSpatial = 0x34, kVehRot = 0x44, kVehLoco = 0xB1C, kVehPurpose = 0xB20, kVehPol = 0x30; // civ.cpp
static uint32_t g_veh_drives;

// a unit quaternion in 32 bits: the index of the largest component (2 bits), the other three (10 bits each)
static uint32_t quat_pack(const float q[4])
{
    int big = 0;
    for (int i = 1; i < 4; i++)
        if (std::fabs(q[i]) > std::fabs(q[big]))
            big = i;
    float sg = q[big] < 0 ? -1.f : 1.f;
    uint32_t v = (uint32_t)big;
    for (int i = 0, k = 0; i < 4; i++) {
        if (i == big)
            continue;
        float c = std::clamp(q[i] * sg * 1.41421356f, -1.f, 1.f); // |c| <= 1/sqrt(2) before the scale
        v |= (uint32_t)std::lround((c + 1.f) * 511.5f) << (2 + 10 * k++);
    }
    return v;
}
static void quat_unpack(uint32_t v, float q[4])
{
    int big = (int)(v & 3);
    float sum = 0;
    for (int i = 0, k = 0; i < 4; i++) {
        if (i == big)
            continue;
        float c = (float)((v >> (2 + 10 * k++)) & 1023) / 511.5f - 1.f;
        q[i] = c / 1.41421356f;
        sum += q[i] * q[i];
    }
    q[big] = std::sqrt(std::max(0.f, 1.f - sum));
}
static bool veh_take(void* v) { return !IsBadReadPtr(v, kVehPurpose + 4); }
static uint32_t veh_state(void* v) { return quat_pack(&field<float>(v, kVehRot)); }
static uint32_t veh_class(void* v)
{
    return field<uint32_t>(v, kVehPol) * 2654435761u ^ (uint32_t)field<int>(v, kVehLoco) * 40503u ^ (uint32_t)field<int>(v, kVehPurpose);
}
static void space_vehicle_collect(Items& out) { collect_noun(kVehicleNoun, out, veh_state, veh_take, veh_class); }
static bool none_behind(uint32_t, uint32_t) { return false; }
static void none_apply(const Item&, uint32_t) {}
static bool none_acted(uint32_t, uint32_t, uint32_t) { return false; }
static void none_act(const Item&, uint32_t) {}
static void space_vehicle_drive(const Item& l, const net::Vec3& hp, const net::Vec3& v, double age, uint32_t host_st)
{
    if (host_st == kGone)
        return;
    float t = (float)std::min(age, 0.5);
    net::Vec3 want{hp.x + v.x * t, hp.y + v.y * t, hp.z + v.z * t};
    float d = net::dist(l.pos, want);
    net::Vec3 p = want;
    if (d < 30.f && d > 0.01f) // glide small corrections out (civ.cpp's mirrors)
        p = {l.pos.x + (want.x - l.pos.x) * 0.5f, l.pos.y + (want.y - l.pos.y) * 0.5f, l.pos.z + (want.z - l.pos.z) * 0.5f};
    float pp[3] = {p.x, p.y, p.z}, q[4];
    quat_unpack(host_st, q);
    void* sp = (uint8_t*)l.p + kVehSpatial;
    vcall<void>(sp, 0xEC / 4);                                   // StopMovement: its planner does not pull it back
    vcall<void>(sp, 0x44 / 4, (const float*)pp, (const float*)q); // Teleport
    g_veh_drives++;
}

static Kind g_kinds[] = {
    {"fruit", &g_fruit_fix, kStCreature | kStTribe, 3.0f, 70.f, 48, fruit_collect, fruit_behind, fruit_apply, fruit_acted, fruit_act, true, true},
    {"tree", &g_tree_fix, kStCreature | kStTribe, 0.5f, 70.f, 48, tree_collect, tree_behind, tree_apply, tree_acted, tree_act, false, false},
    {"herd", &g_herd_fix, kStCreature, 1.0f, 3000.f, 48, herd_collect, herd_behind, herd_apply, herd_acted, herd_act, false, false},
    {"interactive_ornament", &g_orn_fix, kStCreature, 1.5f, 90.f, 40, orn_collect, orn_behind, orn_apply, orn_acted, orn_act, false, false},
    // the bind radius 30 m: 15 of 19 citizens were within 15 m of a partner without the fix (out/detector/20261103-final.md)
    {"citizen", &g_citizen_fix, kStCiv, 30.f, 200.f, 56, citizen_collect, citizen_behind, citizen_apply, citizen_acted, citizen_act, false, false,
     citizen_follow, 0.5},
    {"tribe_tool", &g_tool_fix, kStTribe, 2.0f, 150.f, 48, tool_collect, tool_behind, tool_apply, tool_acted, tool_act, false, true, nullptr, 0, tool_make,
     tool_own, tool_buy},
    // BUG-0490 (new kinds at the end: an older guest drops a kind index it does not know)
    {.name = "space_citizen", .fix = &g_space_cit_fix, .stages = kStSpace, .tol = 30.f, .range = 400.f, .max = 56,
     .collect = citizen_collect, .behind = none_behind, .apply = none_apply, .acted = none_acted, .act = none_act,
     .gone_acted = false, .drop_own = false, .every = 0.5, .drive = space_citizen_drive},
    // the whole planet (the fixture's is ~510 m in radius): every vehicle binds to the nearest host vehicle of its class
    // (sea vehicles were 400+ m from their partner: the bind radius is the planet's size)
    {.name = "space_vehicle", .fix = &g_space_veh_fix, .stages = kStSpace, .tol = 1500.f, .range = 2500.f, .max = 44,
     .collect = space_vehicle_collect, .behind = none_behind, .apply = none_apply, .acted = none_acted, .act = none_act,
     .gone_acted = false, .drop_own = false, .every = 0.4, .classes = true, .vmax = 60.f, .drive = space_vehicle_drive},
};
constexpr int kKinds = (int)(sizeof g_kinds / sizeof g_kinds[0]);
// a snapshot is one unreliable message: 19 bytes + 20 per object within net::Connection::kMaxSingle (1163)
constexpr int kMaxPerSnap = 56;

// ---------------------------------------------------------------- engine
constexpr uint8_t kMsgSnap = 0, kMsgAct = 1;
constexpr double kSnapInterval = 1.0, kGuestInterval = 0.25, kStale = 3.5, kReqRetry = 2.0;
constexpr int kOwnGrace = 3; // snapshots a guest's own copy is seen without a host partner before it goes

static int kind_of_name(const char* n)
{
    for (int k = 0; k < kKinds; k++)
        if (!strcmp(g_kinds[k].name, n))
            return k;
    return -1;
}
static int tool_kind() { return kind_of_name("tribe_tool"); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

static uint8_t stage_bit()
{
    uint32_t m = game::mode_id();
    if (m == modes::kSpace)
        return C.space_on_planet && C.space_on_planet() ? kStSpace : 0;
    return m == modes::kCreature ? kStCreature : m == modes::kTribe ? kStTribe : m == modes::kCiv ? kStCiv : 0;
}

struct Bind {               // guest: a host object and this game's copy of it
    void* p = nullptr;      // the copy (compared with this frame's collect only, never dereferenced)
    uint32_t serial = 0;    // the copy's id in this game
    uint32_t host_st = 0;   // the host's state in the last snapshot
    uint32_t local_st = 0;  // the copy's state when last looked at
    net::Vec3 host_pos{};
    double req_at = -100;   // the last request sent for it
    double host_at = -100;  // BUG-0476: when host_pos came, the host object's velocity, the follower's state
    net::Vec3 host_vel{};
    Follow fol;
};
struct Snap {
    net::Vec3 c{};
    float r = 0;
    double at = -100;
    struct Obj {
        net::Vec3 first; // position
        uint32_t second; // state
        uint32_t cls;    // class (kinds with `classes`)
    };
    std::map<uint32_t, Obj> objs; // host id -> object
};
struct GuestKind {
    std::map<uint32_t, Bind> binds;                   // host id -> copy
    std::map<uint32_t, std::pair<double, int>> made;  // host id -> (last make, tries): K.make at most every 3 s, 3 times
    std::map<std::pair<void*, uint32_t>, int> own;    // copies without a host partner -> snapshots seen so
    Snap snap;
};
static GuestKind g_g[kKinds];
static uint32_t g_epoch = 0;
static double g_last_tx[16], g_last_guest = 0;
struct Stat {
    uint32_t tx = 0, rx = 0, bound = 0, applied = 0, dropped = 0, req_tx = 0, req_rx = 0, acted = 0, missing = 0, made = 0;
};
static Stat g_st[kKinds];

static void guest_reset()
{
    g_tool_buys.clear();
    g_tool_base.clear();
    g_tool_base_done = false;
    for (auto& g : g_g) {
        g.binds.clear();
        g.own.clear();
        g.made.clear();
        g.snap = Snap{};
    }
}

// W2 (BUG-0488): a tool of a far village was never the host's (the snapshot covered 150 m around a guest): after a
// rejoin the guest's planet lacked 8 of the host's tools (coop_realign_planet.py, lab 20261104-212408) and each
// game's far tribes kept the tools they bought themselves. The tools' snapshot covers the whole planet (the 48
// nearest a guest; the tribe fixture's planet has 10-20). `coop.fix wobj.tools_planet off`: 150 m (0.7.6).
static bool g_tools_planet_fix = true;
COOP_FIX("wobj.tools_planet", "BUG-0488", g_tools_planet_fix, "tribe: every village's tools on the planet are the host's in a guest's game, not only those near it");
static float kind_range(int k) { return k == tool_kind() && g_tools_planet_fix ? 3000.f : g_kinds[k].range; }

static void host_tick(double now)
{
    uint8_t sb = stage_bit();
    for (int k = 0; k < kKinds && k < 16; k++) {
        Kind& K = g_kinds[k];
        if (!*K.fix || !(K.stages & sb) || K.max > kMaxPerSnap || now - g_last_tx[k] < (K.every > 0 ? K.every : kSnapInterval))
            continue;
        g_last_tx[k] = now;
        Items all;
        K.collect(all);
        for (auto& p : C.sess->players()) {
            if (p.id == 0 || (C.in_place && !C.in_place(p.id)))
                continue;
            net::Vec3 c;
            if (sb == kStSpace) { // BUG-0490: a guest's ship on our planet, over the ground the objects stand on
                if (!C.space_partner || !C.space_partner(p.id, c) || all.empty())
                    continue;
                float rc = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
                const net::Vec3& o = all[0].pos;
                float ro = std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z);
                if (rc > 1.f && ro > 1.f && ro < rc)
                    c = {c.x * ro / rc, c.y * ro / rc, c.z * ro / rc};
            } else if (!C.player_pos || !C.player_pos(p.id, c) || (c.x == 0 && c.y == 0 && c.z == 0)) {
                if (!(sb == kStTribe && C.stage_center && C.stage_center(c)) && !(sb == kStCiv && C.civ_center && C.civ_center(c)))
                    continue;
            }
            std::vector<std::pair<float, const Item*>> close;
            float range = kind_range(k);
            for (auto& it : all) {
                float d = net::dist(it.pos, c);
                if (d < range)
                    close.push_back({d, &it});
            }
            std::sort(close.begin(), close.end(), [](auto& a, auto& b) { return a.first < b.first; });
            float r = range;
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
                if (K.classes)
                    w.u32(it->cls);
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
        if (K.follow && b.host_at > 0)
            K.follow(l, b.host_pos, b.host_vel, now - b.host_at, b.fol, now);
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
                if (d < bd && !used.count({l.p, l.id}) && (!K.classes || l.cls == ps.cls)) {
                    bd = d;
                    best = &l;
                }
            }
            if (!best) {
                g_st[k].missing++;
                auto& m = G.made[id];
                if (K.make && m.second < 3 && now - m.first > 3.0) {
                    int r = K.make(ps.first, ps.second);
                    if (r >= 0) {
                        m.first = now;
                        m.second++;
                    }
                    if (r > 0)
                        g_st[k].made++; // bound at the next snapshot (this frame's list is older)
                }
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
        double dt = now - b.host_at;
        if (b.host_at > 0 && dt > 0.3 && dt < 3.0) {
            net::Vec3 v{(ps.first.x - b.host_pos.x) / (float)dt, (ps.first.y - b.host_pos.y) / (float)dt,
                        (ps.first.z - b.host_pos.z) / (float)dt};
            float sp = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            b.host_vel = sp < K.vmax ? v : net::Vec3{}; // (a jump: no velocity)
        } else
            b.host_vel = {};
        b.host_pos = ps.first;
        b.host_at = now;
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
        if (K.own && K.own(l, now)) {
            own[key] = n;
            continue;
        }
        if (K.drop_own && n >= kOwnGrace && K.behind(l.st, kGone)) {
            K.apply(l, kGone);
            g_st[k].dropped++;
            continue;
        }
        own[key] = n;
    }
    G.own.swap(own);
}

// BUG-0490: guest, every frame: the bound copies of a kind with `drive` are drawn where the host's objects are
// (this frame's list only: a copy the game freed is not in it)
static void drive_frame(double now, uint8_t sb, bool away)
{
    for (int k = 0; k < kKinds; k++) {
        Kind& K = g_kinds[k];
        GuestKind& G = g_g[k];
        if (!K.drive || !*K.fix || !(K.stages & sb) || away || G.binds.empty() || now - G.snap.at > kStale)
            continue;
        Items local;
        K.collect(local);
        std::map<std::pair<void*, uint32_t>, const Item*> by_key;
        for (auto& it : local)
            by_key[{it.p, it.id}] = &it;
        for (auto& [id, b] : G.binds) {
            auto f = by_key.find({b.p, b.serial});
            if (f != by_key.end() && b.host_at > 0)
                K.drive(*f->second, b.host_pos, b.host_vel, now - b.host_at, b.host_st);
        }
    }
}

static void guest_tick(double now)
{
    if (g_epoch != game::world_epoch()) {
        guest_reset();
        g_epoch = game::world_epoch();
    }
    drive_frame(now, stage_bit(), C.host_away && C.host_away());
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
    if (!r.ok() || k >= kKinds || n > g_kinds[k].max || !std::isfinite(rad) || rad < 0 || rad > 5000.f) // (Space vehicles: the whole planet, 2500 m)
        return;
    Snap s;
    s.c = c;
    s.r = rad;
    s.at = now;
    for (int i = 0; i < n; i++) {
        uint32_t id = r.u32();
        net::Vec3 p{r.f32(), r.f32(), r.f32()};
        uint32_t st = r.u32();
        uint32_t cls = k < kKinds && g_kinds[k].classes ? r.u32() : 0;
        if (!r.ok())
            return;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || p.x * p.x + p.y * p.y + p.z * p.z > 1e8f)
            continue;
        s.objs[id] = {p, st, cls};
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
    if (!hit) {
        if (id == 0 && K.buy && (!C.in_place || C.in_place(from)))
            K.buy(from, p, now); // a guest made one this game has not (BUG-0524)
        return; // gone already (eaten here too, or out of the game)
    }
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
    else if (kind == 2) // the whole planet's list after transitions (realign.cpp, W2: BUG-0564)
        realign::on_planet(from, r);
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
        snprintf(b, sizeof b, "kind %s fix=%d binds=%zu own=%zu snap_n=%zu snap_r=%.0f tx=%u rx=%u bound=%u applied=%u dropped=%u missing=%u made=%u req_tx=%u req_rx=%u acted=%u\n",
                 g_kinds[k].name, (int)*g_kinds[k].fix, g_g[k].binds.size(), g_g[k].own.size(), g_g[k].snap.objs.size(), g_g[k].snap.r,
                 t.tx, t.rx, t.bound, t.applied, t.dropped, t.missing, t.made, t.req_tx, t.req_rx, t.acted);
        s += b;
    }
    snprintf(b, sizeof b, "citizens walks=%u puts=%u far=%u; space citizens puts=%u; space vehicles drives=%u\n", g_cit_walks, g_cit_puts, g_cit_far,
             g_space_cit_puts, g_veh_drives);
    s += b;
    snprintf(b, sizeof b, "tools all=%d buy=%d removed=%u buys_tx=%u buys_rx=%u buys_done=%u\n", (int)g_tool_all_fix, (int)g_tool_buy_fix,
             g_tools_removed, g_tool_buys_tx, g_tool_buys_rx, g_tool_buys_done);
    s += b;
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

// ---------------------------------------------------------------- fields kind civ_citizen (BUG-0475's Civ half)
// The fields engine's ids are u16: a citizen's id is the low half of the host's object serial (a second
// citizen with the same low half is left out on the host: rare, 1 in ~65000 per pair)
static int citizen_kind() { return kind_of_name("citizen"); }
static bool g_cit_scale_fix = true;
COOP_FIX("civ.citizen_scale", "BUG-0475", g_cit_scale_fix, "civ: a guest's bound city citizen gets the host citizen's size");
static const fields::Field kCitizenFields[] = {fields::kScale};
static void citizen_each(void (*cb)(uint16_t, void*, void*), void* u)
{
    Items all;
    citizen_collect(all);
    std::set<uint16_t> seen;
    for (auto& it : all)
        if (seen.insert((uint16_t)(it.id & 0xFFFF)).second)
            cb((uint16_t)(it.id & 0xFFFF), it.p, u);
}
// guest: the bound copy of the host's citizen `id`, only when it is still a live citizen of this frame's list
static uint64_t g_cit_live_frame = ~0ull;
static std::set<void*> g_cit_live;
static void* citizen_mirror(uint16_t id)
{
    int k = citizen_kind();
    if (k < 0 || !*g_kinds[k].fix)
        return nullptr;
    for (auto& [hid, b] : g_g[k].binds) {
        if ((uint16_t)(hid & 0xFFFF) != id)
            continue;
        if (H->frame_index() != g_cit_live_frame) {
            g_cit_live_frame = H->frame_index();
            g_cit_live.clear();
            Items all;
            citizen_collect(all);
            for (auto& it : all)
                g_cit_live.insert(it.p);
        }
        return g_cit_live.count(b.p) && game::object_serial(b.p) == b.serial ? b.p : nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------- fields kinds space_citizen / space_vehicle (BUG-0490)
// The same shape as civ_citizen: ids are the low half of the host's serial, the guest's mirror is its bound copy.
static bool g_space_scale_fix = true;
COOP_FIX("space.planet_scale", "BUG-0490", g_space_scale_fix, "space: a guest's bound colony citizen or vehicle gets the host's size");
struct LiveSet {
    uint64_t frame = ~0ull;
    std::set<void*> live;
};
static void* bound_mirror(const char* kind, uint16_t id, LiveSet& ls)
{
    int k = kind_of_name(kind);
    if (k < 0 || !*g_kinds[k].fix)
        return nullptr;
    for (auto& [hid, b] : g_g[k].binds) {
        if ((uint16_t)(hid & 0xFFFF) != id)
            continue;
        if (H->frame_index() != ls.frame) {
            ls.frame = H->frame_index();
            ls.live.clear();
            Items all;
            g_kinds[k].collect(all);
            for (auto& it : all)
                ls.live.insert(it.p);
        }
        return ls.live.count(b.p) && game::object_serial(b.p) == b.serial ? b.p : nullptr;
    }
    return nullptr;
}
static void each_of(void (*collect)(Items&), void (*cb)(uint16_t, void*, void*), void* u)
{
    Items all;
    collect(all);
    std::set<uint16_t> seen;
    for (auto& it : all)
        if (seen.insert((uint16_t)(it.id & 0xFFFF)).second)
            cb((uint16_t)(it.id & 0xFFFF), it.p, u);
}
static LiveSet g_sc_live, g_sv_live;
static void space_citizen_each(void (*cb)(uint16_t, void*, void*), void* u) { each_of(citizen_collect, cb, u); }
static void space_vehicle_each(void (*cb)(uint16_t, void*, void*), void* u) { each_of(space_vehicle_collect, cb, u); }
static void* space_citizen_mirror(uint16_t id) { return bound_mirror("space_citizen", id, g_sc_live); }
static void* space_vehicle_mirror(uint16_t id) { return bound_mirror("space_vehicle", id, g_sv_live); }
static bool space_planet_active() { return stage_bit() == kStSpace; }

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
    fields::add_kind({fields::K_CIV_CITIZEN, "civ_citizen", &g_cit_scale_fix, kCitizenFields, 1, citizen_each, citizen_mirror,
                      [] { return game::mode_id() == modes::kCiv; }});
    fields::add_kind({fields::K_SPACE_CITIZEN, "space_citizen", &g_space_scale_fix, kCitizenFields, 1, space_citizen_each,
                      space_citizen_mirror, space_planet_active});
    fields::add_kind({fields::K_SPACE_VEHICLE, "space_vehicle", &g_space_scale_fix, kCitizenFields, 1, space_vehicle_each,
                      space_vehicle_mirror, space_planet_active});
    if (!a_FruitSetState || !a_DropFruit || !a_OrnUsed)
        H->log(M, SK_WARN, "wobj: symbols missing, world objects stay per game");
    H->command_add(M, "coop.wobj",
                   "coop.wobj [list <kind> [r] [x y z] | use <ornament> | eat <fruit> | drop <tree> <slot>] - the host's world objects (fruit, trees, ornaments) on every game (BUG-0472, BUG-0459)",
                   c_wobj, nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace wobj
