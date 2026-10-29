#include "savestrip.h"

#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>
#include <windows.h>

#include "game.h"
#include "npc.h"
#include "savestrip_rule.h"
#include "sporekit_game.hpp"
#include "fixes.h"

namespace savestrip {

static const SkHost* H;
static SkMod* M;
static bool g_on = true;
COOP_FIX("savestrip.on", "BUG-0020", g_on, "saves: our puppets, proxies and their herds are kept out of saves (coop.savestrip 0|1)");
static std::vector<Filter> g_filters;
static DWORD g_main_tid;
static void* g_last_ser;      // the COMSerializer of the save being written
static double g_last_write;   // when Write ran last
// BUG-0255: one list per save: a new serializer or a new frame starts it (savestrip_rule.h), not a
// 0.5 s pause. `coop.fix savestrip.per_save off`: the 0.5 s rule of 0.7.3.0 (unsafe: a second
// save soon after can write a game object at a freed puppet's address as null).
static bool g_per_save_fix = true;
COOP_FIX("savestrip.per_save", "BUG-0255", g_per_save_fix, "saves: every save builds its own list of our objects (UNSAFE off)");
static uint32_t g_frame, g_save_first_frame, g_save_last_frame, g_max_span;
static SaveState g_save_state;
static std::unordered_set<void*> g_ours; // this save's objects of ours (bases and their Cast pointers)
static uint32_t g_saves, g_nulled, g_offthread, g_last_count;

constexpr uint32_t kCreatureNoun = 0x018EB45Eu;
constexpr uint32_t kSerializableIID = 0x0179C807u; // what WriteObjectPointer 0x69D950 casts to

void add_filter(Filter fn) { g_filters.push_back(fn); }
void set_enabled(bool on) { g_on = on; }
bool enabled() { return g_on; }

// a herd made by CreateHerd(pos, species, 0, 0, 0, 0) for a puppet or a proxy: no archetype (+0x88),
// no nest (+0x160), not the avatar's (+0x84). Every herd of the game has an archetype
// (docs/re/creature.md 12).
static bool own_kind_herd(void* h)
{
    return h && !IsBadReadPtr(h, 0x21C) && game::object_alive(h) && game::noun_id(h) == game::kHerdNoun &&
           *(uint32_t*)((uint8_t*)h + 0x88) == 0 && !game::herd_nest(h) && !*(uint8_t*)((uint8_t*)h + 0x84);
}

// the creature stage: a puppet or a proxy of ours (never a member of the player's pack) whose herd
// is a herd made for it, and that herd when every creature in it is stripped. A creature of the
// game in such a herd (a console-spawned companion recruited into the pack, a test's NPC) keeps
// itself and its herd: a null member of the pack faults every frame after the load. Not stripped
// (not checked what a null member does on load): a puppet in the avatar's herd (the fallback for a
// partner whose creation is unknown), a guest's proxies in the nest herds of the game.
static bool g_owner_fix = true; // tests: `coop.savestrip owner 0` strips by herd kind alone (the first version)
COOP_FIX("savestrip.owner", "BUG-0020", g_owner_fix, "saves: only herds made for our objects are stripped, never a pack member");
static std::unordered_set<void*> g_strip_herds, g_keep_herds;
// BUG-0255: a herd made for a puppet stayed in the world empty after its partner left (herds are
// reused per species and never destroyed) and went into every save: the owner rule only stripped
// herds that still held a creature of ours. Now an own-kind herd with no members is stripped too,
// when it is surely empty (savestrip_rule.h). `coop.fix savestrip.empty_herds off`: kept (0.7.3.0).
static bool g_empty_fix = true;
COOP_FIX("savestrip.empty_herds", "BUG-0255", g_empty_fix, "saves: empty herds made for partners' puppets are kept out of saves too");
static std::unordered_set<void*> g_herd_seen; // herds some creature of this save points at
static bool g_unreadable;                     // a creature of this save could not be read
static uint32_t g_empty_stripped;

static bool herd_members(void* h, bool& empty)
{
    if (IsBadReadPtr((uint8_t*)h + 0x40, 8))
        return false;
    void** b = *(void***)((uint8_t*)h + 0x40);
    void** e = *(void***)((uint8_t*)h + 0x44);
    if (e < b || (b && e > b && IsBadReadPtr(b, (size_t)((uint8_t*)e - (uint8_t*)b))))
        return false;
    empty = e == b;
    return true;
}

static bool creature_filter(void* o, uint32_t noun)
{
    if (noun == game::kHerdNoun) {
        if (!own_kind_herd(o))
            return false;
        if (!g_owner_fix)
            return true;
        if (g_strip_herds.count(o) && !g_keep_herds.count(o))
            return true;
        if (!g_empty_fix || g_strip_herds.count(o) || g_keep_herds.count(o))
            return false;
        bool empty = false, readable = herd_members(o, empty);
        if (!strip_empty_herd(readable, empty, g_herd_seen.count(o) != 0, g_unreadable))
            return false;
        g_empty_stripped++;
        return true;
    }
    if (noun != kCreatureNoun)
        return false;
    if (IsBadReadPtr(o, 0x1680)) {
        g_unreadable = true;
        return false;
    }
    g_herd_seen.insert(game::herd_of(o)); // every creature, the avatar and the pack included
    if (o == game::avatar())
        return false;
    void* h = game::herd_of(o);
    if (!own_kind_herd(h))
        return false;
    if (!g_owner_fix)
        return true;
    bool ours = !game::pack_member(o) && (game::is_puppet(o) || npc::is_proxy(o));
    (ours ? g_strip_herds : g_keep_herds).insert(h);
    return ours;
}

static void rebuild()
{
    g_ours.clear();
    g_strip_herds.clear();
    g_keep_herds.clear();
    g_herd_seen.clear();
    g_unreadable = false;
    // creatures first (they decide their herds), then the herds
    for (int pass = 0; pass < 2; pass++)
        game::for_each_object(
        [](void* o, uint32_t noun, void* ctx) {
            if ((noun == game::kHerdNoun) != (*(int*)ctx == 1))
                return true;
            for (Filter f : g_filters)
                if (f(o, noun)) {
                    g_ours.insert(o);
                    if (void* c = game::cast(o, kSerializableIID))
                        g_ours.insert(c);
                    break;
                }
            return true;
        },
        &pass);
}

// COMSerializer::Write 0x6A05F0: thiscall(COMSerializer*, ISerializerStream*, ISPSerializable* obj)
// ret 8 -> bool. Writes the pointer as the reference id (0 for null), the noun id, and queues obj for
// SaveClassObjects 0x69FB90 (which writes its record).
using Write_t = bool(SK_THISCALL*)(void*, void*, void*);
static Write_t o_Write;
static bool SK_THISCALL h_Write(void* self, void* stream, void* obj)
{
    if (!g_on || !obj)
        return o_Write(self, stream, obj);
    if (GetCurrentThreadId() != g_main_tid) { // not a save of the world (those run on the main thread)
        g_offthread++;
        return o_Write(self, stream, obj);
    }
    double now = H->time_seconds();
    // a new serializer (each save opens its own) or a new frame (BUG-0255; before: a 0.5 s pause):
    // a new save. The set is made once per save, from the live noun list (never a pointer of an
    // older frame).
    bool fresh = new_save(g_save_state, self, g_frame);
    if (!g_per_save_fix)
        fresh = self != g_last_ser || now - g_last_write > 0.5;
    if (self != g_last_ser || now - g_last_write > 0.5) { // the measure: frames one save spans
        g_save_first_frame = g_frame;
    }
    g_save_last_frame = g_frame;
    if (g_save_last_frame - g_save_first_frame > g_max_span)
        g_max_span = g_save_last_frame - g_save_first_frame;
    if (fresh) {
        rebuild();
        g_saves++;
        g_last_count = (uint32_t)g_ours.size();
        g_last_ser = self;
        if (!g_ours.empty())
            H->log(M, SK_INFO, "save: %u objects of ours are kept out of it", g_last_count);
    }
    g_last_write = now;
    if (g_ours.count(obj)) {
        g_nulled++;
        return o_Write(self, stream, nullptr);
    }
    return o_Write(self, stream, obj);
}

// cGameBehaviorManager (simulator subsystem, vtable 0x0145CB68) keeps a vector of the objects it
// updates every frame at +0x20 / +0x24 (4-byte intrusive pointers); our creatures are in it. Its
// Read 0xB0E570 (slot +0x2C, thiscall(mgr, stream) ret 4) reads our null references back as null
// entries, and its update 0xB0EA40 calls vt+0x1C on each entry without a null check: a fault every
// frame (seen: 0xB0EC8C). After the read the null entries are taken out.
using BhvRead_t = int(SK_THISCALL*)(void*, void*);
static BhvRead_t o_BhvRead;
static uint32_t g_bhv_nulls;
static int SK_THISCALL h_BhvRead(void* mgr, void* stream)
{
    int r = o_BhvRead(mgr, stream);
    if (!g_on || IsBadReadPtr(mgr, 0x2C))
        return r;
    void**& b = *(void***)((uint8_t*)mgr + 0x20);
    void**& e = *(void***)((uint8_t*)mgr + 0x24);
    if (!b || e < b)
        return r;
    void** w = b;
    for (void** p = b; p != e; ++p)
        if (*p)
            *w++ = *p;
    g_bhv_nulls += (uint32_t)(e - w);
    for (void** p = w; p != e; ++p)
        *p = nullptr;
    e = w;
    return r;
}

void tick()
{
    g_main_tid = GetCurrentThreadId();
    g_frame++;
}

std::string status()
{
    char b[320];
    snprintf(b, sizeof b,
             "savestrip=%d saves=%u last_ours=%u nulled=%u offthread=%u behavior_nulls_dropped=%u empty_herds_stripped=%u "
             "max_save_frames=%u\n",
             (int)g_on, g_saves, g_last_count, g_nulled, g_offthread, g_bhv_nulls, g_empty_stripped, g_max_span);
    return b;
}

static int c_savestrip(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc >= 2 && (!strcmp(argv[1], "0") || !strcmp(argv[1], "1")))
        g_on = argv[1][0] == '1';
    if (argc >= 3 && !strcmp(argv[1], "owner"))
        g_owner_fix = argv[2][0] == '1';
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    g_main_tid = GetCurrentThreadId();
    add_filter(creature_filter);
    H->command_add(M, "coop.savestrip", "coop.savestrip [0|1] - keep our objects out of saves (tests: 0 = the old behaviour, BUG-0020)",
                   c_savestrip, nullptr, SK_CMD_MAIN_THREAD);
    uintptr_t br = H->sym("SporeCoop::cGameBehaviorManager_Read");
    if (!br || H->hook_create(M, (void*)br, (void*)&h_BhvRead, (void**)&o_BhvRead) != 0) {
        H->log(M, SK_WARN, "savestrip: no hook on the behavior manager's Read: saves keep our objects");
        return false; // a null in its vector faults every frame: without the cleanup, no nulls
    }
    uintptr_t a = H->sym("Simulator::COMSerializer::Write");
    if (!a || H->hook_create(M, (void*)a, (void*)&h_Write, (void**)&o_Write) != 0) {
        H->log(M, SK_WARN, "savestrip: no hook on COMSerializer::Write: our objects go into saves");
        return false;
    }
    return true;
}

} // namespace savestrip
