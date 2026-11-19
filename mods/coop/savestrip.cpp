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
static void* g_last_ser;      // the COMSerializer of the save being written
static double g_last_write;   // when Write ran last
// BUG-0255: one list per save: a new serializer or a new frame starts it (savestrip_rule.h), not a
// 0.5 s pause. `coop.fix savestrip.per_save off`: the 0.5 s rule of 0.7.3.0 (unsafe: a second
// save soon after can write a game object at a freed puppet's address as null).
static bool g_per_save_fix = true;
COOP_FIX_UNSAFE("savestrip.per_save", "BUG-0255", g_per_save_fix, "saves: every save builds its own list of our objects");
static uint32_t g_frame, g_save_first_frame, g_save_last_frame, g_max_span;
static SaveState g_save_state;
static std::unordered_set<void*> g_ours; // this save's objects of ours (bases and their Cast pointers)
static uint32_t g_saves, g_nulled, g_offthread, g_last_count;

constexpr uint32_t kCreatureNoun = 0x018EB45Eu;
constexpr uint32_t kSerializableIID = 0x0179C807u; // what WriteObjectPointer 0x69D950 casts to

void add_filter(Filter fn) { g_filters.push_back(fn); }
static void (*g_write_watch)();
static void* g_watch_ser;
static uint32_t g_watch_frame;
void set_write_watch(void (*f)()) { g_write_watch = f; }
static std::vector<std::pair<void (*)(), void (*)()>> g_save_hooks;
static bool g_hooks_ended = true;
void add_save_hooks(void (*start)(), void (*end)()) { g_save_hooks.push_back({start, end}); }
void set_enabled(bool on) { g_on = on; }

// ---- BUG-1315: keys of ours in the save streams (savestrip.h add_key_filter) ----
// `coop.fix savestrip.keys off`: every key goes into the save as the object holds it (0.7.16 before the fix: the guest's
// species reached the host's save through its members' and citizens' records, the herds and wild creatures of its
// species, and the main stream; lab 20261119-095414)
static bool g_key_fix = true;
COOP_FIX("savestrip.keys", "BUG-1315", g_key_fix,
         "saves: no key of a guest's species line goes into a save, whatever object holds it (the world's species instead)");
static std::vector<KeyFilter> g_key_filters;
static bool g_world_saving;     // inside SaveGame (main thread)
static bool g_save_any_write;   // the save of this frame has written its first object reference
static uint32_t g_key_raw, g_key_events;
static uint32_t g_key_seen, g_key_rewritten, g_key_early, g_key_off, g_key_save_n, g_key_save_early, g_key_save_off;
void add_key_filter(KeyFilter fn) { g_key_filters.push_back(fn); }
static bool saving_now() { return g_world_saving || !g_hooks_ended; }

struct Holder {
    const void* p;
    void* obj;
    uint32_t noun;
    uintptr_t off;
};
// (the log's first lines of a save) the game object whose memory holds the key, by the noun list
static Holder holder_of(const void* p)
{
    Holder h{p, nullptr, 0, 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            Holder* x = (Holder*)u;
            uintptr_t a = (uintptr_t)x->p, b = (uintptr_t)o;
            if (a >= b && a - b < 0x2000 && (!x->obj || a - b < x->off)) {
                x->obj = o;
                x->noun = noun;
                x->off = a - b;
            }
            return true;
        },
        &h);
    return h;
}

// Stream_WriteResourceKey 0xAE2DC0: cdecl(ISerializerStream*, const ResourceKey*) -> the stream; writes group, type,
// instance (big-endian in the file). Callers: the key attribute writer 0xAE33D0 (every class's key fields) and 11
// direct writers (Ghidra 2026-11-19).
using WriteKey_t = void*(SK_CDECL*)(void*, const uint32_t*);
static WriteKey_t o_WriteKey;
static void* SK_CDECL h_WriteKey(void* stream, const uint32_t* key)
{
    if (!key || g_key_filters.empty() || !game::on_main() || !saving_now() || IsBadReadPtr(key, 12))
        return o_WriteKey(stream, key);
    uint32_t k[3] = {key[0], key[1], key[2]};
    bool hit = false;
    for (KeyFilter f : g_key_filters)
        if (f(k)) {
            hit = true;
            break;
        }
    if (!hit)
        return o_WriteKey(stream, key);
    g_key_seen++;
    bool early = !g_save_any_write;
    if (!g_key_fix || !g_on) {
        g_key_off++;
        g_key_save_off++;
    } else {
        g_key_rewritten++;
        g_key_save_n++;
        if (early) {
            g_key_early++;
            g_key_save_early++;
        }
    }
    if (g_key_save_n + g_key_save_off <= 8) {
        Holder h = holder_of(key);
        H->log(M, SK_INFO,
               "save: key %08X.%08X of a species line %s (by %08X, in noun %08X +0x%X%s) (BUG-1315)", key[0], key[1],
               g_key_fix && g_on ? "written as the world's" : "written as it is (savestrip.keys off)",
               (uint32_t)((uintptr_t)__builtin_return_address(0) - H->rebase(0)), h.noun, (unsigned)h.off,
               early ? ", before the save's first object" : "");
    }
    return o_WriteKey(stream, g_key_fix && g_on ? k : key);
}
// IO::WriteInt32 0x93A9E0: cdecl bool(IStream*, const uint32_t* p, int count, int endian). A save's arrays of dwords
// (endian 1: as they are in memory) can hold keys as a block: {instance, type, group} (a ResourceKey) or {type,
// instance, group} (the event records of 0xE39890: a baby's 0x782E0768 named the guest's species twice in the host's
// Tribe save, lab 20261119-112418). Only blocks of 3 dwords or more while a save is written are looked at.
using WriteInt32_t = bool(SK_CDECL*)(void*, const uint32_t*, int, int);
static WriteInt32_t o_WriteInt32;
constexpr uint32_t kCreatureType = 0x2B978C46u;
// BUG-1274 (lab 20261119-141949 civ_own_designs): an event of the host's main stream named the guest's vehicle design
// {0x24682294, 7BE222FB, 0x40646200}: the dword blocks and the event lists are looked at for the design kinds of a line
// too (vehicle and building models), the filters decide
static bool line_key_type(uint32_t t) { return t == kCreatureType || t == 0x24682294u || t == 0x2399BE55u; }
static bool SK_CDECL h_WriteInt32(void* s, const uint32_t* p, int count, int endian)
{
    if (count < 3 || count > 0x10000 || !p || g_key_filters.empty() || !saving_now() || !game::on_main() ||
        IsBadReadPtr(p, (size_t)count * 4))
        return o_WriteInt32(s, p, count, endian);
    std::vector<uint32_t> copy;
    for (int i = 0; i < count; i++) {
        if (!line_key_type(p[i]))
            continue;
        // the type at i: {instance, type, group} = i-1, i, i+1; {type, instance, group} = i, i+1, i+2
        int at[2][3] = {{i - 1, i, i + 1}, {i + 1, i, i + 2}}; // instance, type, group
        for (auto& a : at) {
            if (a[0] < 0 || a[2] >= count || !p[a[0]])
                continue;
            uint32_t k[3] = {p[a[0]], p[a[1]], p[a[2]]};
            bool hit = false;
            for (KeyFilter f : g_key_filters)
                if (f(k)) {
                    hit = true;
                    break;
                }
            if (!hit)
                continue;
            g_key_seen++;
            if (!g_key_fix || !g_on) {
                g_key_off++;
                g_key_save_off++;
                break;
            }
            if (copy.empty())
                copy.assign(p, p + count);
            copy[a[0]] = k[0], copy[a[1]] = k[1], copy[a[2]] = k[2];
            g_key_rewritten++;
            g_key_save_n++;
            g_key_raw++;
            if (g_key_save_n <= 8)
                H->log(M, SK_INFO, "save: key %08X of a species line in a block of %d dwords written as the world's %08X "
                                   "(by %08X) (BUG-1315)",
                       p[a[0]], count, k[0], (uint32_t)((uintptr_t)__builtin_return_address(0) - H->rebase(0)));
            break;
        }
    }
    return o_WriteInt32(s, copy.empty() ? p : copy.data(), count, endian);
}

// GameEventLog_Write 0x7EB210 (thiscall(log, stream) ret 4): the game's event lists (kinds 7..12, each event a vector of
// dwords written raw, little-endian); events of 0xE39890 name species {type, instance, group} (a tribe baby's 0x782E0768
// the guest's line key twice: the main stream of the host's Tribe save, lab 20261119-113320). While a save is written
// every line key in them is the world's for the call (memory put back after it).
using EventLogWrite_t = bool(SK_THISCALL*)(void*, void*);
static EventLogWrite_t o_EventLogWrite;
static bool SK_THISCALL h_EventLogWrite(void* log, void* stream)
{
    struct Kept {
        uint32_t* p;
        uint32_t v[3];
    };
    std::vector<Kept> kept;
    if (!g_key_filters.empty() && saving_now() && game::on_main() && !IsBadReadPtr(log, 0x30)) {
        uint32_t** buckets = *(uint32_t***)((uint8_t*)log + 8);
        uint32_t n = *(uint32_t*)((uint8_t*)log + 0xC);
        for (uint32_t kind = 7; kind <= 12 && buckets && n && n < 0x10000 && !IsBadReadPtr(buckets, n * 4); kind++) {
            uint32_t* node = buckets[kind % n];
            for (int guard = 0; node && guard < 64 && !IsBadReadPtr(node, 0x14) && node[0] != kind; guard++)
                node = (uint32_t*)node[4];
            if (!node || IsBadReadPtr(node, 0x14) || node[0] != kind)
                continue;
            uint32_t* sent = node + 1;
            int guard = 0;
            for (uint32_t* e = (uint32_t*)*sent; e && e != sent && guard < 0x4000 && !IsBadReadPtr(e, 0x1C);
                 e = (uint32_t*)*e, guard++) {
                uint32_t* b = (uint32_t*)e[5];
                uint32_t* end = (uint32_t*)e[6];
                if (!b || end < b + 3 || end - b > 0x400 || IsBadWritePtr(b, (end - b) * 4))
                    continue;
                int cnt = (int)(end - b);
                for (int i = 0; i + 2 < cnt; i++) {
                    if (!line_key_type(b[i]) || !b[i + 1])
                        continue;
                    uint32_t k[3] = {b[i + 1], b[i], b[i + 2]}; // {instance, type, group}
                    bool hit = false;
                    for (KeyFilter f : g_key_filters)
                        if (f(k)) {
                            hit = true;
                            break;
                        }
                    if (!hit)
                        continue;
                    g_key_seen++;
                    if (!g_key_fix || !g_on) {
                        g_key_off++;
                        g_key_save_off++;
                        continue;
                    }
                    kept.push_back({b + i, {b[i], b[i + 1], b[i + 2]}});
                    if (g_key_save_n < 8)
                        H->log(M, SK_INFO, "save: key %08X of a species line in the event log (kind %u, event %08X) "
                                           "written as the world's %08X (BUG-1315)",
                               b[i + 1], kind, b[0], k[0]);
                    b[i] = k[1], b[i + 1] = k[0], b[i + 2] = k[2];
                    g_key_rewritten++;
                    g_key_save_n++;
                    g_key_events++;
                    i += 2;
                }
            }
        }
    }
    bool r = o_EventLogWrite(log, stream);
    for (auto it = kept.rbegin(); it != kept.rend(); ++it)
        memcpy(it->p, it->v, 12);
    return r;
}

static void key_save_done()
{
    if (g_key_save_n || g_key_save_off)
        H->log(M, SK_INFO, "save: %u keys of a species line written as the world's (%u before the first object), %u as "
                           "they are (BUG-1315)",
               g_key_save_n, g_key_save_early, g_key_save_off);
    g_key_save_n = g_key_save_early = g_key_save_off = 0;
    g_save_any_write = false;
}

static void start_hooks()
{
    if (!g_hooks_ended) // a second save in the same frame: the first one's objects back first
        for (auto& h : g_save_hooks)
            h.second();
    for (auto& h : g_save_hooks)
        h.first();
    g_hooks_ended = false;
}

void world_save(bool begin)
{
    if (!game::on_main())
        return;
    g_world_saving = begin;
    // the save hooks before the main stream (its first keys come before the first object reference; BUG-1315). The
    // first Write of the save runs them again (a new serializer: savestrip_rule.h); the end is the next frame's tick.
    if (begin && g_on && g_key_fix)
        start_hooks();
}
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
static bool g_pending_fix = true;
COOP_FIX("savestrip.pending", "BUG-0256", g_pending_fix, "saves: creatures waiting for a deferred destroy are kept out of saves like puppets");
// BUG-0255: a herd made for a puppet stayed in the world empty after its partner left (herds are
// reused per species and never destroyed) and went into every save: the owner rule only stripped
// herds that still held a creature of ours. Now an own-kind herd with no members is stripped too,
// when it is surely empty (savestrip_rule.h).
// BUG-1067: the empty-herd rule asks whether the herd is one the mod made (game::our_herd), not only whether it
// looks like one: in Civ the game's own herds have no archetype and no nest either, and every empty one of them was
// left out of every Civ save (77 in a guest's save, the host's too). `coop.fix savestrip.our_herds off`: by the
// look alone (0.7.13).
static bool g_our_herds_fix = true;
COOP_FIX("savestrip.our_herds", "BUG-1067", g_our_herds_fix, "saves: only empty herds the mod made are kept out of saves, never the game's own (Civ's look like ours)");
static uint32_t g_empty_kept_game; // empty own-kind herds kept because they are the game's (BUG-1067)
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
        if (g_strip_herds.count(o) || g_keep_herds.count(o))
            return false;
        bool empty = false, readable = herd_members(o, empty);
        if (!strip_empty_herd(readable, empty, g_herd_seen.count(o) != 0, g_unreadable))
            return false;
        if (g_our_herds_fix && !game::our_herd(o)) { // BUG-1067: an empty herd of the game's
            g_empty_kept_game++;
            return false;
        }
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
    if (o == game::avatar_field()) // (avatar() is null during the quit message's save, BUG-0264)
        return false;
    void* h = game::herd_of(o);
    if (!own_kind_herd(h))
        return false;
    if (!g_owner_fix)
        return true;
    // BUG-0256: a creature waiting for its deferred destroy (a stray removed during the evolution
    // cinematic) is ours too: herds are reused per species, and the herd it shared with the
    // partner's new puppet was kept for it with a null where the puppet was stripped; the guest that
    // loaded that save faulted every frame in Herd_Update
    bool ours = !game::pack_member(o) &&
                (game::is_puppet(o) || npc::is_proxy(o) || (g_pending_fix && game::destroy_pending(o)));
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
    if (g_write_watch && (self != g_watch_ser || g_frame != g_watch_frame) && game::on_main()) {
        g_watch_ser = self;
        g_watch_frame = g_frame;
        g_write_watch();
    }
    if (!g_on || !obj)
        return o_Write(self, stream, obj);
    if (!game::on_main()) { // not a save of the world (those run on the main thread)
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
        start_hooks();
        rebuild();
        g_saves++;
        g_last_count = (uint32_t)g_ours.size();
        g_last_ser = self;
        if (!g_ours.empty())
            H->log(M, SK_INFO, "save: %u objects of ours are kept out of it", g_last_count);
    }
    g_last_write = now;
    g_save_any_write = true;
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
    g_frame++;
    if (!g_hooks_ended && !g_world_saving) {
        g_hooks_ended = true;
        for (auto& h : g_save_hooks)
            h.second();
        key_save_done();
    }
}

std::string status()
{
    char b[420];
    snprintf(b, sizeof b,
             "savestrip=%d saves=%u last_ours=%u nulled=%u offthread=%u behavior_nulls_dropped=%u empty_herds_stripped=%u "
             "empty_game_herds_kept=%u max_save_frames=%u keys=%d line_keys=%u rewritten=%u early=%u raw=%u events=%u kept_off=%u\n",
             (int)g_on, g_saves, g_last_count, g_nulled, g_offthread, g_bhv_nulls, g_empty_stripped, g_empty_kept_game,
             g_max_span, (int)g_key_fix, g_key_seen, g_key_rewritten, g_key_early, g_key_raw, g_key_events, g_key_off);
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
    add_filter(creature_filter);
    H->command_add(M, "coop.savestrip", "coop.savestrip [0|1] - keep our objects out of saves (tests: 0 = the old behaviour, BUG-0020)",
                   c_savestrip, nullptr, SK_CMD_MAIN_THREAD);
    uintptr_t br = H->sym("SporeCoop::cGameBehaviorManager_Read");
    if (!br || H->hook_create(M, (void*)br, (void*)&h_BhvRead, (void**)&o_BhvRead) != 0) {
        H->log(M, SK_WARN, "savestrip: no hook on the behavior manager's Read: saves keep our objects");
        return false; // a null in its vector faults every frame: without the cleanup, no nulls
    }
    if (uintptr_t wk = H->sym("SporeCoop::Stream_WriteResourceKey")) // BUG-1315
        if (H->hook_create(M, (void*)wk, (void*)&h_WriteKey, (void**)&o_WriteKey) != 0)
            H->log(M, SK_WARN, "savestrip: no hook on the save stream's key writer: keys of a species line may reach saves");
    if (uintptr_t ev = H->sym("SporeCoop::GameEventLog_Write")) // BUG-1315: keys in the game's event lists
        if (H->hook_create(M, (void*)ev, (void*)&h_EventLogWrite, (void**)&o_EventLogWrite) != 0)
            H->log(M, SK_WARN, "savestrip: no hook on the event log's Write: keys of a species line in events may reach saves");
    if (uintptr_t wi = H->sym("IO::WriteInt32")) // BUG-1315: keys in raw arrays
        if (H->hook_create(M, (void*)wi, (void*)&h_WriteInt32, (void**)&o_WriteInt32) != 0)
            H->log(M, SK_WARN, "savestrip: no hook on IO::WriteInt32: keys of a species line in raw arrays may reach saves");
    uintptr_t a = H->sym("Simulator::COMSerializer::Write");
    if (!a || H->hook_create(M, (void*)a, (void*)&h_Write, (void**)&o_Write) != 0) {
        H->log(M, SK_WARN, "savestrip: no hook on COMSerializer::Write: our objects go into saves");
        return false;
    }
    return true;
}

} // namespace savestrip
