// The host's shots copied on the guests (BUG-0477 and its class), the old Tribe-stage villages raided in Civ
// (BUG-0539) and the damage of a mirror's weapon dropped; moved out of civ.cpp (C8 of 0.7.15, BUG-1164).
#include "civ_internal.h"
#include "civ_nations.h"

namespace civ {

// ---------------- BUG-0477: the host's shots on the guests ----------------
// Every Civ weapon (vehicles, city turrets) fires through cToolManager::FireTool (0x01050080, called each
// update by cCombatSimulator::Update 0xAE7360 for a combatant whose target is in range; the tool's own
// refire time decides whether a projectile leaves). A guest's mirrors run no behaviour tree, so they never
// had a target and the guest saw the host's fights only as falling hit points. Now the host notes who
// fires where (squadron.cpp's FireTool hook calls fire_observed) and sends the shooters of the last 0.25 s
// every 0.2 s (MSG_CIV_STATE with chunk 0xFE / chunks 0: an older guest drops it at cycle_chunk_ok); the
// guest fires its mirror's own weapon at the same point with the same FireTool, so the game makes the
// projectile, its flight and its explosion. Its hits do nothing on the guest (cosmetic_hit: the damage
// of a mirror's weapon is dropped in the TakeDamage hooks; the host's state brings the real damage).
constexpr uint32_t kTurretNoun = 0x0436F342;
bool g_fix_creature_target = true;
COOP_FIX("civ.creature_target", "BUG-0970", g_fix_creature_target,
         "civ: a guest's attack of a wild creature takes the host's creature near that place, or drives there");
constexpr uintptr_t kVCombatant = 0x508, kBCombatant = 0x120;
// BUG-0688: a city turret is no building: its combatant is at +0x588 (vtable 0x1466C28 GOG 2024, the turret TakeDamage's
// `this`); +0x120 of a turret holds no object. guest_fire took every turret's combatant at kBCombatant and
// weapon_of called vt 0x14 of that word: the guest crashed (execute of 0xCCCCCCCC) at the first turret shot it
// copied. Now a turret's combatant is taken at kTCombatant, and weapon_of calls vt 0x14 only of a combatant
// whose vtable lies in the game's image and whose slot 0x14 points into the game's code (any shooter kind; a
// wrong one is skipped and logged once). `coop.fix civ.turret_weapon off`: the old offset, no check (0.7.10).
constexpr uintptr_t kTCombatant = 0x588;
bool g_fix_turret_weapon = true;
COOP_FIX("civ.turret_weapon", "BUG-0688", g_fix_turret_weapon,
         "civ: a guest copies a host turret's shot with the turret's own weapon (no call through a non-object)");
static uintptr_t shooter_comb_off(uint8_t kind) { return kind == 1 ? kVCombatant : g_fix_turret_weapon ? kTCombatant : kBCombatant; }
// BUG-0647: the vehicle's model key {instance, type, group} (its model object +0x34, +0x90: what cVehicle::Load
// 0xCA67C0 sets through vt 0x94 of +0x34; runtime: +0xC4 holds the design instance, coop.memfind)
constexpr double kShotSend = 0.2, kShotKeep = 0.25, kShotFireEvery = 0.1;
uintptr_t a_FireTool, a_ToolMgrGet;
std::map<std::pair<uint16_t, uint32_t>, Shot> g_shots;   // host: shooter -> last shot
std::map<std::pair<uint16_t, uint32_t>, Shot> g_shots_in; // guest: shooter -> its shots
static double g_shots_sent_at = -100;
uint32_t g_shots_seen, g_shots_tx, g_shots_rx, g_shots_fired, g_shots_nomirror, g_shots_notool,
    g_hits_dropped;
static bool g_fix_shot_target = true;
COOP_FIX("civ.shot_target", "BUG-0534", g_fix_shot_target,
         "civ: a guest fires a host's shot only at a target it has (the game faults on a shot without one)");
std::unordered_set<void*> g_turret_shooters; // guest: turrets fired from (their hits are dropped too)
// BUG-0530: cCombatSimulator::Update calls FireTool every update for a combatant with a target in range; FireTool
// returns true only when the weapon's refire time passed and its strategy (vt 0x34, WhileFiring) made the shot
// (GOG 2024 0x0105030B: `call [strategy+0x34]`, its al returned). The relay of every call let a guest's weapon
// fire on its own refire time for as long as the host's kept trying: religious (cultural) shots and AoE areas
// the host's game never made. Now the host counts the shots that left and the guest fires that many.
bool g_fix_shot_fired = true;
COOP_FIX("civ.shot_fired", "BUG-0530", g_fix_shot_fired,
         "civ: a guest fires the host's shots that left, not one per FireTool the host's game tried");
constexpr double kShotKeepFired = 1.0;     // host: a shooter stays in the lists (its count) for so long
// BUG-0530: a shot that left goes out at once (at most every 0.05 s), not with the next 0.2 s list: religious
// projectiles and AoE areas live about a second, and 0.2-0.4 s of relay delay showed them in one game only
static bool g_shot_new;
// BUG-0530: an AoE weapon (cDefaultAoETool) makes its area once when it starts firing (tool +0x120) and keeps it
// until cToolManager::OnMouseUp (0x0104FAA0, thiscall(mgr, tool) ret 4) stops the tool; cCombatSimulator::Update
// stops a combatant's tool when its target is gone. The area sends cultural projectiles at what is inside. A guest's
// copy of the weapon was never stopped: its area (and the area's cultural projectiles) stayed in the guest's game.
// Now the host notes the stop of a listed shooter's tool, the guest stops its copy (and at the latest when the
// shooter's list entry ends).
uintptr_t a_ToolMouseUp;

uint32_t pos_key(const net::Vec3& p)
{
    return (uint32_t)(int)std::lround(p.x) * 73856093u ^ (uint32_t)(int)std::lround(p.y) * 19349663u ^
           (uint32_t)(int)std::lround(p.z) * 83492791u;
}

// an object whose vtable lies in the game's image (no read through a vtable that is not one: a target of FireTool
// can be an object without a spatial at -kVSpatial, and IsBadReadPtr on its "vtable", a float, wrote a fault
// per shot to the host's log, BUG-0535)
bool vtable_in_game(const void* obj)
{
    static uintptr_t lo, hi;
    if (!hi)
        if (HMODULE exe = GetModuleHandleW(nullptr)) {
            auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
            lo = (uintptr_t)exe;
            hi = lo + nt->OptionalHeader.SizeOfImage;
        }
    uintptr_t vt = *(const uintptr_t*)obj;
    return vt >= lo && vt + 0x24 <= hi;
}

// BUG-0688: an address inside an executable section of the game's image (a virtual function of the game)
static bool code_in_game(uintptr_t a)
{
    static uintptr_t lo, hi;
    if (!hi)
        if (HMODULE exe = GetModuleHandleW(nullptr)) {
            auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
            auto* sec = IMAGE_FIRST_SECTION(nt);
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
                if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                    uintptr_t b = (uintptr_t)exe + sec[i].VirtualAddress, e = b + sec[i].Misc.VirtualSize;
                    lo = lo && lo < b ? lo : b;
                    hi = hi > e ? hi : e;
                }
        }
    return a >= lo && a < hi;
}

// BUG-0530 (diagnostics): FireTool calls a guest's game makes itself in Civ (not guest_fire's copies), by caller
static bool g_in_guest_fire;
CALL_GUARD("civ.guest_fire", g_in_guest_fire); // BUG-1047
std::map<uintptr_t, LocalFire> g_local_fire;
// BUG-0530 (diagnostics): shots by the tool's strategy key (tool +0x178: which weapon made it), host and guest
std::map<uint32_t, uint32_t> g_shot_tools;
static void note_tool(void* tool)
{
    if (tool && !IsBadReadPtr(tool, 0x17C) && g_shot_tools.size() < 32)
        g_shot_tools[F<uint32_t>(tool, 0x178)]++;
}
static void shots_log(double now) // BUG-0530 (diagnostics): the shots by weapon every 120 s in the game log
{
    static double last = -1;
    if (last >= 0 && now - last < 120)
        return;
    last = now;
    if (g_shot_tools.empty())
        return;
    std::string t;
    for (auto& [k, n] : g_shot_tools) {
        char x[32];
        snprintf(x, sizeof x, " %08X=%u", k, n);
        t += x;
    }
    H->log(M, SK_INFO, "civ: shots by weapon (%s):%s", is_host() ? "host, fired" : "guest, copies fired", t.c_str());
}
void fire_local(void* owner, uintptr_t ret, bool fired)
{
    if (g_in_guest_fire || !is_guest() || game::mode_id() != modes::kCiv || !owner)
        return;
    auto& e = g_local_fire[ret];
    e.calls++;
    e.fired += fired;
    void* o = (uint8_t*)owner - kVSpatial;
    if (!e.owner_vt && !IsBadReadPtr(o, 4))
        e.owner_vt = *(uintptr_t*)o;
    if (fired && e.fired == 1 && g_local_fire.size() <= 16)
        H->log(M, SK_INFO, "civ: this guest's game fired a tool itself: FireTool from 0x%08X (owner vtable %08X, noun %08X)",
               (unsigned)ret, (unsigned)e.owner_vt, e.owner_vt && vtable_in_game(o) ? game::noun_id(o) : 0u);
}

// BUG-1322: FireTool's owner and target are spatials; fire_observed took the object at spatial - 0x34 (kVSpatial) and,
// when its first dword lay in SporeApp.exe's image (vtable_in_game, BUG-0535), called its vtable (Cast, the noun). A
// spatial that is not at +0x34 of a listed object (a target the combatant AI gives that is no game object of the list,
// or whatever memory precedes it) passed that check by chance: the host jumped into a vehicle's memory and then to
// 0xCCCCCCCC from this observer (lab 20261119-114030, civ_preach_far: the stack's module addresses are civ_shots.cpp
// fire_observed and game::object_position, called from squadron's FireTool hook). Now an object that is not one of our
// listed vehicles is used only when it is in the game's object list (game::listed: pointers compared, nothing called),
// once per object and frame. Off: the image check only (0.7.15).
static bool g_fix_shot_listed = true;
COOP_FIX("civ.shot_target_listed", "BUG-1322", g_fix_shot_listed,
         "civ: the host's shot observer calls nothing on a shooter or target that is not in the game's object list");
static bool g_shot_probe;          // tests (coop.civ shotprobe, below)
static int g_shot_probe_tkind = -1;
static uint32_t g_unlisted_shot_objs;
static std::string g_unlisted_last;
static bool shot_obj_ok(void* o, void* spatial)
{
    if (!g_fix_shot_listed)
        return vtable_in_game(o) && game::object_alive(o);
    static uint64_t frame = ~0ull;
    static std::unordered_map<void*, bool> seen;
    uint64_t f = H->frame_index();
    if (f != frame) {
        frame = f;
        seen.clear();
    }
    auto it = seen.find(o);
    if (it != seen.end())
        return it->second;
    bool ok = game::listed(o);
    seen[o] = ok;
    if (!ok) {
        char b[160];
        snprintf(b, sizeof b, "spatial %p (vtable %08X), object at -0x34 %p (first dword %08X, %s)", spatial,
                 *(const uint32_t*)spatial, o, *(const uint32_t*)o, vtable_in_game(o) ? "in the image" : "not in the image");
        g_unlisted_last = b;
        if (g_unlisted_shot_objs++ < 8)
            H->log(M, SK_INFO, "civ: a shot's shooter or target is no listed object: %s: nothing called (BUG-1322)", b);
    }
    return ok;
}
std::string shot_listed_status()
{
    char b[400];
    snprintf(b, sizeof b, "shot objects not listed %u (last: %s) fix=%d\n", g_unlisted_shot_objs,
             g_unlisted_last.empty() ? "-" : g_unlisted_last.c_str(), (int)g_fix_shot_listed);
    return b;
}

// tests (BUG-1322, `coop.civ shotprobe`): the observer gets a shot of a listed host vehicle at a target spatial that is no
// object (the memory before it starts with an address in SporeApp.exe's image whose dword +0xC is no code: the DOS
// header, 0x0000FFFF), as the preaching vehicles' target of lab 20261119-114030. With civ.shot_target_listed off the
// observer calls into that "vtable" and the game faults; on: the target is one the guests cannot find (kind 2).
std::string shot_probe()
{
    void* veh = nullptr;
    hv_each([&](const HvRef& x) {
        if (!veh && game::listed(x.v))
            veh = x.v;
    });
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!veh || !exe || !is_host())
        return "no listed host vehicle here (or not the host)\n";
    static uint8_t fake[0x80], tool[0x180];
    memset(fake, 0, sizeof fake);
    *(uintptr_t*)fake = (uintptr_t)exe; // the "object" at target - 0x34: a dword in the image, its +0xC = 0x0000FFFF
    float aim[3] = {0, 0, 0};
    if (net::Vec3 p; game::object_position(veh, p))
        aim[0] = p.x, aim[1] = p.y, aim[2] = p.z;
    g_shot_probe = true;
    g_shot_probe_tkind = -1;
    fire_observed(tool, (uint8_t*)veh + kVSpatial, aim, fake + kVSpatial, true);
    g_shot_probe = false;
    char b[200];
    snprintf(b, sizeof b, "shot probe: vehicle %p, target %p: target kind %d (2: none the guests can find) fix=%d\n", veh,
             fake + kVSpatial, g_shot_probe_tkind, (int)g_fix_shot_listed);
    return b + shot_listed_status();
}

void fire_observed(void* tool, void* owner, const float* aim, void* target, bool fired)
{
    if (!owner || !aim || !tool || !is_host() || !active())
        return;
    if (g_fix_shot_fired && !fired) {
        g_n.shots_tried++;
        return;
    }
    if (fired && !g_shot_probe)
        note_tool(tool);
    void* o = (uint8_t*)owner - kVSpatial;
    Shot s;
    if (HvRef it = hv_find(o)) {
        s.kind = 1;
        s.id = it.id;
    } else if (!IsBadReadPtr(o, 0x40) && (g_fix_shot_listed ? shot_obj_ok(o, owner) : vtable_in_game(o)) &&
               game::noun_id(o) == kTurretNoun && game::object_position(o, s.at)) {
        s.kind = 3;
        g_n.turret_shots_seen++;
    } else
        return;
    s.aim = {aim[0], aim[1], aim[2]};
    if (!std::isfinite(s.aim.x) || !std::isfinite(s.aim.y) || !std::isfinite(s.aim.z))
        return;
    if (target && target != owner && !IsBadReadPtr((uint8_t*)target - kVSpatial, 0x40)) {
        void* t = (uint8_t*)target - kVSpatial;
        if (HvRef tv = hv_find(t)) {
            s.tkind = 1;
            s.tid = tv.id;
        } else if (shot_obj_ok(t, target) && game::object_position(t, s.tpos)) {
            s.tkind = 3;
            s.tnoun = game::noun_id(t);
        } else
            s.tkind = 2; // a target the guest cannot find: it does not fire this one (BUG-0534)
    }
    if (g_shot_probe) { // tests (coop.civ shotprobe): the target's class only, no shot for the guests
        g_shot_probe_tkind = s.tkind;
        return;
    }
    s.seen = H->time_seconds();
    auto& e = g_shots[{s.id, s.kind == 3 ? pos_key(s.at) : 0u}];
    s.count = (uint8_t)(e.count + 1);
    g_shot_new = true;
    e = s;
    g_shots_seen++;
}

static void tribe_count_log(double now); // BUG-0539 diagnostics (below)
void host_send_shots(double now)
{
    shots_log(now);
    tribe_count_log(now);
    bool soon = g_fix_shot_fired && g_shot_new && now - g_shots_sent_at >= 0.05;
    if (now - g_shots_sent_at < kShotSend && !soon)
        return;
    g_shots_sent_at = now;
    g_shot_new = false;
    const bool counted = g_fix_shot_fired;
    const double keep = counted ? kShotKeepFired : kShotKeep;
    std::vector<const Shot*> list;
    for (auto it = g_shots.begin(); it != g_shots.end();) {
        if (now - it->second.seen > keep) {
            it = g_shots.erase(it);
            continue;
        }
        if (list.size() < 120)
            list.push_back(&it->second);
        ++it;
    }
    // one message holds at most 22 shooters (47 bytes each, kMaxPacket): 40 in one (up to 0.7.5) were 1845
    // bytes, a message the connection drops, so with more than 24 shooters a guest saw no shot at all
    constexpr size_t kPerMsg = 22;
    for (size_t at = 0; at < list.size(); at += kPerMsg) {
        size_t n = std::min(kPerMsg, list.size() - at);
        net::Writer w;
        w.u16(0); w.u8(counted ? kShotChunkFired : kShotChunk); w.u8(0);
        w.u8((uint8_t)n);
        for (size_t i = at; i < at + n; i++) {
            const Shot* s = list[i];
            w.u8(s->kind); w.u16(s->id);
            w.f32(s->at.x); w.f32(s->at.y); w.f32(s->at.z);
            w.f32(s->aim.x); w.f32(s->aim.y); w.f32(s->aim.z);
            w.u8(s->tkind); w.u16(s->tid); w.u32(s->tnoun);
            w.f32(s->tpos.x); w.f32(s->tpos.y); w.f32(s->tpos.z);
            if (counted) {
                w.u8(s->count);
                w.u8(s->stopped ? 1 : 0);
            }
        }
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
        g_shots_tx++;
    }
}

void guest_shots(net::Reader& r, bool counted)
{
    uint8_t n = r.u8();
    double now = H->time_seconds();
    for (int i = 0; i < n && r.ok(); i++) {
        Shot s;
        s.kind = r.u8(); s.id = r.u16();
        s.at.x = r.f32(); s.at.y = r.f32(); s.at.z = r.f32();
        s.aim.x = r.f32(); s.aim.y = r.f32(); s.aim.z = r.f32();
        s.tkind = r.u8(); s.tid = r.u16(); s.tnoun = r.u32();
        s.tpos.x = r.f32(); s.tpos.y = r.f32(); s.tpos.z = r.f32();
        uint8_t flags = 0;
        if (counted) {
            s.count = r.u8();
            flags = r.u8();
        }
        s.stopped = (flags & 1) != 0;
        if (!r.ok() || (s.kind != 1 && s.kind != 3) || !std::isfinite(s.aim.x) || !std::isfinite(s.aim.y) ||
            !std::isfinite(s.aim.z) || !std::isfinite(s.at.x))
            return;
        auto key = std::make_pair(s.id, s.kind == 3 ? pos_key(s.at) : 0u);
        auto found = g_shots_in.find(key);
        bool fresh = found == g_shots_in.end() || found->second.counted != counted;
        auto& e = g_shots_in[key];
        double tried = e.tried;
        if (counted && !fresh && e.count != s.count)
            tried = -100; // a new shot of the host's: fired on this frame (the weapon's refire time still decides)
        uint8_t done = fresh ? (uint8_t)(s.count - 1) : e.done; // a shooter first heard of: its last shot
        bool released = !fresh && e.released && s.stopped;
        e = s;
        e.tried = tried;
        e.counted = counted;
        e.done = done;
        e.released = released;
        if (s.stopped)
            e.done = e.count; // fires no more
        if ((uint8_t)(e.count - e.done) > 3) // far behind (a hitch, lost lists): the last few only
            e.done = (uint8_t)(e.count - 3);
        // fires on until then (the next list keeps it going; not much past the host's last shot)
        e.seen = now + (counted ? kShotKeepFired + 0.2 : kShotKeep + 0.05);
        g_shots_rx++;
    }
}

// BUG-0539 (diagnostics): who destroys a Tribe stage tribe in Civ, and when the count of tribes changes
constexpr uint32_t kTribeNoun = 0x018C6D19;
static bool g_tribe_gone; // BUG-0539, host: a tribe was destroyed since the last list
static std::map<uintptr_t, uint32_t> g_tribe_destroys;
// BUG-0623: a listener of gamefixes' DestroyInstance hook (a second hook of the function failed and switched off
// the stale stimulus sender guard, BUG-0456)
void on_destroy_instance(void* obj, uintptr_t ra)
{
    if (obj && game::mode_id() == modes::kCiv && !IsBadReadPtr(obj, 4) && vtable_in_game(obj) &&
        game::noun_id(obj) == kTribeNoun) {
        if (is_host())
            g_tribe_gone = true; // BUG-0539: the guests hear of it with the next tribe list
        if (g_tribe_destroys[ra]++ == 0 && g_tribe_destroys.size() <= 8)
            H->log(M, SK_INFO, "civ: a tribe of the Tribe stage is destroyed here (%s), DestroyInstance from 0x%08X",
                   is_host() ? "host" : is_guest() ? "guest" : "alone", (unsigned)ra);
    }
}
// BUG-0539: the host's NPC vehicles raid the old Tribe stage villages (a behaviour tree action, 0xDCAAB0 "HR":
// money and an event, then DestroyInstance of the tribe at 0xDCB2B2, its huts, tools, food mat and bundles go with
// it); a guest's mirrors run no behaviour tree, so the guest's villages stood for good. The host sends the places of
// its tribes in Civ (MSG_CIV_STATE chunk 0xFA, every 5 s and when one is destroyed); a guest destroys a tribe of its
// own with no host tribe within 10 m (two looks in a row), the same DestroyInstance.
uintptr_t a_DestroyInst;
static double g_tribes_sent_at = -100;
std::unordered_map<void*, int> g_tribe_extra_seen; // guest: tribe -> looks without a host tribe
std::vector<std::pair<void*, net::Vec3>> tribes_here()
{
    std::vector<std::pair<void*, net::Vec3>> out;
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        net::Vec3 p;
        if (noun == kTribeNoun && game::object_alive(o) && game::object_position(o, p))
            ((std::vector<std::pair<void*, net::Vec3>>*)u)->push_back({o, p});
        return true;
    }, &out);
    return out;
}
// BUG-0659 (docs/re/civ.md#civ-tribe-ruins-fast): a guest removes a village on its second look without the host's
// tribe, and the second list came 5 s after the one sent 0.5 s after the raid: each raid left the village in the
// guest's world for 5-5.5 s (the detector's transient `Civilization:planet:village` / tribe_hut / tribe_tool /
// tribe_food_mat rows, one sample each). With civ.tribe_ruins_fast the host sends kQuickLists lists 0.5 s apart after
// a raid (the two looks then come within ~1 s; a lost one costs 0.5 s). Off: one quick list (0.7.10).
static bool g_tribe_ruins_fast = true;
COOP_FIX("civ.tribe_ruins_fast", "BUG-0659", g_tribe_ruins_fast,
         "civ: a raided old tribe village goes on the guests within about a second, not 5 s later");
constexpr int kQuickLists = 3;
static int g_quick_left;
void host_send_tribes(double now)
{
    if (g_tribe_gone)
        g_quick_left = g_tribe_ruins_fast ? kQuickLists : 1;
    g_tribe_gone = false;
    if (!(now - g_tribes_sent_at >= 5.0 || (g_quick_left > 0 && now - g_tribes_sent_at >= 0.5)))
        return;
    if (now - g_tribes_sent_at < 5.0)
        g_quick_left--;
    g_tribes_sent_at = now;
    auto ts = tribes_here();
    if (ts.size() > 80)
        ts.resize(80); // 80 x 12 bytes: one packet
    net::Writer w;
    w.u16(0); w.u8(kTribeChunk); w.u8(0);
    w.u8((uint8_t)ts.size());
    for (auto& [t, p] : ts) {
        w.f32(p.x); w.f32(p.y); w.f32(p.z);
    }
    for (auto& pl : C.sess->players())
        if (pl.id != 0)
            C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    g_n.tribes_tx++;
}
void guest_tribes(net::Reader& r)
{
    std::vector<net::Vec3> hp(r.u8());
    for (auto& p : hp) {
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
    }
    if (!r.ok())
        return;
    g_n.tribes_rx++;
    if (!a_DestroyInst || local_play() || game::mode_id() != modes::kCiv)
        return;
    auto here = tribes_here();
    // a list without any tribe while this game has several: the host's world is not there yet (a load, a
    // stage change), not a raid of all of them; and never more than two per list (a raid takes one at a time)
    if (hp.empty() && here.size() > 2)
        return;
    int removed = 0;
    std::unordered_map<void*, int> seen;
    for (auto& [t, p] : here) {
        bool host_has = false;
        for (auto& q : hp)
            if (net::dist(p, q) < 10.f) {
                host_has = true;
                break;
            }
        if (host_has)
            continue;
        int n = g_tribe_extra_seen.count(t) ? g_tribe_extra_seen[t] + 1 : 1;
        if (n < 2 || removed >= 2) {
            seen[t] = n;
            continue;
        }
        removed++;
        {
            callguard::Hold applying(g_applying);
            thiscall<void>(a_DestroyInst, nm(), t);
        }
        g_n.tribes_removed++;
        H->log(M, SK_INFO, "civ: the old tribe village at (%.0f %.0f %.0f) is gone on the host: removed here (BUG-0539)", p.x,
               p.y, p.z);
    }
    g_tribe_extra_seen = std::move(seen);
}

static void tribe_count_log(double now)
{
    static double last = -100;
    static int known = -1;
    if (now - last < 5)
        return;
    last = now;
    int n = 0;
    game::for_each_object([](void*, uint32_t noun, void* u) {
        if (noun == kTribeNoun)
            ++*(int*)u;
        return true;
    }, &n);
    if (n != known)
        H->log(M, SK_INFO, "civ: tribes of the Tribe stage in this game: %d (was %d, cities %u)", n, known,
               (unsigned)all_cities().size());
    known = n;
}

// host: a tool stops (cToolManager::OnMouseUp): a listed shooter's entry says so (BUG-0530)
ToolMouseUp_t o_ToolMouseUp;
int SK_THISCALL h_ToolMouseUp(void* mgr, void* tool)
{
    if (g_fix_shot_fired && tool && is_host() && active() && !IsBadReadPtr(tool, 0x118)) {
        void* owner = *(void**)((uint8_t*)tool + 0x114);
        void* o = owner ? (uint8_t*)owner - kVSpatial : nullptr;
        HvRef it = hv_find(o);
        std::pair<uint16_t, uint32_t> key{0, 0};
        bool known = false;
        if (it) {
            key = {it.id, 0u};
            known = true;
        } else if (o && !IsBadReadPtr(o, 0x40) && vtable_in_game(o) && game::noun_id(o) == kTurretNoun) {
            net::Vec3 at;
            if (game::object_position(o, at)) {
                key = {0, pos_key(at)};
                known = true;
            }
        }
        auto e = known ? g_shots.find(key) : g_shots.end();
        if (e != g_shots.end() && !e->second.stopped) {
            e->second.stopped = true;
            e->second.seen = H->time_seconds(); // in the lists a while longer, so the guests hear of it
            g_shot_new = true;
            g_n.shot_stops_seen++;
        }
    }
    return o_ToolMouseUp(mgr, tool);
}

// guest: the weapon of a local shooter (combatant vt 0x14, as cCombatSimulator::Update gets it)
static void* weapon_of(void* comb)
{
    if (!comb || IsBadReadPtr(comb, 8) || IsBadReadPtr(*(void**)comb, 0x18))
        return nullptr;
    if (g_fix_turret_weapon && !(vtable_in_game(comb) && code_in_game((*(uintptr_t**)comb)[0x14 / 4]))) {
        // BUG-0688: no game combatant (a shooter read at the wrong offset): skipped, logged once per word
        static std::unordered_set<uintptr_t> logged;
        g_n.bad_comb++;
        uintptr_t vt = *(uintptr_t*)comb;
        if (logged.size() < 8 && logged.insert(vt).second)
            H->log(M, SK_WARN, "civ: a shooter's combatant %p is no game object (vtable %08X): its shot is not copied (BUG-0688)",
                   comb, (unsigned)vt);
        return nullptr;
    }
    void* t = vcall<void*>(comb, 0x14 / 4);
    return t && !IsBadReadPtr(t, 0x120) ? t : nullptr;
}

// guest: objects by noun near a place, all found in one walk of the noun list (turret shooters, building targets)
struct Want {
    uint32_t noun;
    net::Vec3 pos;
    float best;
    void* found;
};
static void find_wanted(std::vector<Want>& w)
{
    if (w.empty())
        return;
    game::for_each_object(
        [](void* o, uint32_t n, void* u) {
            auto& w = *(std::vector<Want>*)u;
            net::Vec3 p;
            bool have = false;
            for (auto& x : w) {
                if (x.noun != n)
                    continue;
                if (!have && !(game::object_alive(o) && game::object_position(o, p)))
                    return true;
                have = true;
                float d = net::dist(p, x.pos);
                if (d < x.best) {
                    x.best = d;
                    x.found = o;
                }
            }
            return true;
        },
        &w);
}

// BUG-0530: this game's copy of a host's weapon stops (vehicles; a turret's copy stops when its entry ends only
// if found again, which needs the noun walk: turrets are military, no AoE)
static void release_copy(Shot& s)
{
    s.released = true;
    if (!a_ToolMouseUp || s.kind != 1)
        return;
    void* o = gv_live(s.id); // (BUG-0951)
    if (!o || !exists(o) || F<uint8_t>(o, kVDead))
        return;
    void* tool = weapon_of((uint8_t*)o + kVCombatant);
    if (!tool || !F<uint8_t>(tool, 0x80)) // +0x80: firing (FireTool sets it, OnMouseUp clears it)
        return;
    void* mgr = skg::cdecl_call<void*>(a_ToolMgrGet);
    if (!mgr)
        return;
    {
        callguard::Hold in_guest_fire(g_in_guest_fire);
        thiscall<int>(a_ToolMouseUp, mgr, tool);
    }
    g_n.shot_released++;
}

void guest_fire(double now)
{
    shots_log(now);
    tribe_count_log(now);
    if (!a_FireTool || !a_ToolMgrGet || local_play()) {
        g_shots_in.clear();
        return;
    }
    std::vector<Shot*> due;
    std::vector<Want> want; // per due shot: [2i] its turret (kind 3), [2i + 1] its target (tkind 3)
    for (auto it = g_shots_in.begin(); it != g_shots_in.end();) {
        Shot& s = it->second;
        if (now > s.seen) {
            if (!s.released)
                release_copy(s); // the host's list no longer has it: its weapon stopped there
            it = g_shots_in.erase(it);
            continue;
        }
        ++it;
        if (s.stopped && !s.released) {
            g_n.shot_stops_seen++;
            release_copy(s);
        }
        if (now - s.tried < kShotFireEvery || (s.counted && s.done == s.count))
            continue;
        s.tried = now;
        due.push_back(&s);
        want.push_back({s.kind == 3 ? kTurretNoun : 0u, s.at, 6.f, nullptr});
        want.push_back({s.tkind == 3 ? s.tnoun : 0u, s.tpos, 4.f, nullptr});
    }
    bool any = false;
    for (auto& w : want)
        any |= w.noun != 0;
    if (any)
        find_wanted(want);
    for (size_t i = 0; i < due.size(); i++) {
        Shot& s = *due[i];
        void* o = nullptr;
        void* comb = nullptr;
        if (s.kind == 1) {
            o = gv_live(s.id); // (BUG-0951)
            if (o && (!exists(o) || F<uint8_t>(o, kVDead)))
                o = nullptr;
            comb = o ? (uint8_t*)o + kVCombatant : nullptr;
        } else {
            o = want[2 * i].found;
            comb = o ? (uint8_t*)o + shooter_comb_off(3) : nullptr;
        }
        if (!o) {
            g_shots_nomirror++;
            continue;
        }
        void* tool = weapon_of(comb);
        void* mgr = tool ? skg::cdecl_call<void*>(a_ToolMgrGet) : nullptr;
        if (!mgr) {
            g_shots_notool++;
            continue;
        }
        if (s.kind == 3)
            g_turret_shooters.insert(o);
        // the target in this game (the projectile homes on it as the host's does); its place is the aim
        void* t = nullptr;
        if (s.tkind == 1) {
            t = gv_live(s.tid); // (BUG-0951)
            if (t && (!exists(t) || F<uint8_t>(t, kVDead)))
                t = nullptr;
        } else if (s.tkind == 3)
            t = want[2 * i + 1].found;
        // BUG-0534: the host's shot had a target and this game has none for it: no FireTool. The game's
        // weapon update (0xCB5FA0, GOG 2024) reads the target of a tool's shot without a null check
        // (`mov eax,[edi+0x50]` with edi = 0) and faulted every frame for a beam with no target.
        if (s.tkind && !t && g_fix_shot_target) {
            g_n.shots_notarget++;
            continue;
        }
        net::Vec3 ap = s.aim;
        if (s.tkind)
            g_n.shots_tgt_wanted++;
        if (t) {
            g_n.shots_tgt_found++;
            net::Vec3 tp;
            if (game::object_position(t, tp)) // the host aimed at its own copy's place: the same offset here
                ap = {s.aim.x + tp.x - (s.tkind == 3 ? s.tpos.x : tp.x), s.aim.y + tp.y - (s.tkind == 3 ? s.tpos.y : tp.y),
                      s.aim.z + tp.z - (s.tkind == 3 ? s.tpos.z : tp.z)};
        }
        float aim[3] = {ap.x, ap.y, ap.z};
        int fired{};
        {
            callguard::Hold in_guest_fire(g_in_guest_fire);
            fired = thiscall<int>(a_FireTool, mgr, tool, (uint8_t*)o + kVSpatial, (const float*)aim, -1, 0,
                                      t ? (void*)((uint8_t*)t + kVSpatial) : nullptr, 0);
        }
        if (fired & 0xFF) { // FireTool returns a bool in al (BUG-0530)
            note_tool(tool);
            s.done++;
            g_shots_fired++;
            g_n.turret_fired += s.kind == 3;
        } else
            g_n.shots_refused++;
    }
}

// guest: a hit from a mirror's (or a fired-from turret's) weapon: the shot is the host's, its damage too
uintptr_t g_hit_att_vt;               // the vtable of the last attacker (diagnostics)
bool cosmetic_hit(void* att)
{
    if (game::mode_id() == modes::kSpace) // BUG-0677: a copy of a host ship's shot (a mirror's) hurts no building here
        return squadron::shot_is_cosmetic(att);
    if (is_guest() && game::mode_id() == modes::kCiv) {
        g_n.hits_guest++;
        if (!att || IsBadReadPtr(att, 4))
            g_n.hits_noatt++;
        else
            g_hit_att_vt = *(uintptr_t*)att;
    }
    if (!att || !is_guest() || game::mode_id() != modes::kCiv || local_play() || IsBadReadPtr(att, 4) ||
        IsBadReadPtr(*(void**)att, 0x60))
        return false;
    void* v = vcall<void*>(att, 0x5C / 4, 0x137E8E0u); // the vehicle behind a combatant
    bool ours = gv_bound(v) || g_turret_shooters.count((uint8_t*)att - shooter_comb_off(3));
    if (ours)
        g_hits_dropped++;
    return ours;
}
bool hit_is_cosmetic(void* attacker) { return cosmetic_hit(attacker); }

VTakeDamage_t o_VTakeDamage;
int SK_THISCALL h_VTakeDamage(void* s, float d, uint32_t pol, int t, const void* dir, void* a)
{
    if (cosmetic_hit(a))
        return 0;
    if (nations::friendly(pol_of((uint8_t*)s - 0x508), pol)) // BUG-9274: the players' nations are allies
        return 0;
    return o_VTakeDamage(s, d, pol, t, dir, a);
}



} // namespace civ
