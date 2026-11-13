// Debug/RE helper commands for the coop mod (hot-reloadable experiments).
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <initializer_list>
#include <vector>
#include <windows.h>

#include "cell_logic.h"
#include "cellsync.h"
#include "dna_pace.h"
#include "settings.h"
#include "fixes.h"
#include "game.h"
#include "gamefixes.h"
#include "modes.h"
#include "world.h"
#include "terrain_diag.h"
#include "sporekit.h"
#include "sporekit_game.hpp"

static const SkHost* H;
static std::string msglisteners_text(uint32_t id); // BUG-0956 (RE)
namespace {
SkMod* g_self;
}
#define SAY(...) H->out_printf(out, __VA_ARGS__)

// BUG-1030: debug commands called H->sym() results without a check (a missing name: a call to address 0 on an unknown
// build). True when every name is known; else says which one is missing.
static bool need_syms(SkOut* out, std::initializer_list<const char*> names)
{
    for (const char* n : names)
        if (!H->sym(n)) {
            SAY("no symbol %s in this build\n", n);
            return false;
        }
    return true;
}

namespace {
int c_save(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "confirm")) { // tests: the held box is confirmed as usual again
        world::g_box_hold = false;
        SAY("box released");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "box")) {
        SAY("saved_box=%d hold=%d", world::saved_box_up() ? 1 : 0, world::g_box_hold ? 1 : 0);
        return 0;
    }
    world::g_box_hold = argc > 1 && !strcmp(argv[1], "hold"); // tests (BUG-0482): the box stays up
    std::string err;
    if (!world::save_game(err)) {
        SAY("%s", err.c_str());
        return 1;
    }
    SAY("saved (mode 0x%08X)", game::mode_id());
    return 0;
}

// tests (BUG-0313, BUG-0576): every pause reason the game counts now. cGameTimeManager: +0x48 flags (bit 0 paused),
// +0x4C a map reason -> index (EASTL rbtree, anchor +0x50: right +0, left +4, parent +8; node key +0x10, index
// +0x14), +0x68 an array of 8-byte rows, the count first (Pause 0xB32330 / Resume 0xB32360 add / remove one;
// docs/re/tribe.md#tribe-game-time-pauses)
static void pause_walk(const uint8_t* n, const uint8_t* anchor, const uint8_t* arr, std::string& out, int depth)
{
    if (!n || n == anchor || depth > 64 || !skg::readable(n, 0x18))
        return;
    pause_walk(*(const uint8_t* const*)(n + 4), anchor, arr, out, depth + 1);
    uint32_t key = *(const uint32_t*)(n + 0x10);
    int idx = *(const int*)(n + 0x14);
    int cnt = idx >= 0 && idx < 256 && skg::readable(arr + idx * 8, 4) ? *(const int*)(arr + idx * 8) : -999;
    char b[64];
    snprintf(b, sizeof b, " %08X=%d", key, cnt);
    if (cnt)
        out += b;
    pause_walk(*(const uint8_t* const*)n, anchor, arr, out, depth + 1);
}
int c_pauses(void*, int, const char* const*, SkOut* out)
{
    uintptr_t get = H->sym("Simulator::cGameTimeManager::Get");
    const uint8_t* tm = get ? skg::cdecl_call<const uint8_t*>(get) : nullptr;
    if (!tm || !skg::readable(tm, 0x6C)) {
        SAY("no time manager");
        return 1;
    }
    const uint8_t* anchor = tm + 0x50;
    const uint8_t* arr = *(const uint8_t* const*)(tm + 0x68);
    std::string r;
    pause_walk(*(const uint8_t* const*)(anchor + 8), anchor, arr, r, 0);
    SAY("pauses: flags=%X paused=%d reasons:%s", *(const uint32_t*)(tm + 0x48), (int)(tm[0x48] & 1), r.empty() ? " none" : r.c_str());
    return 0;
}

// the game's quit message (world.cpp load_slot): saves and leaves the stage for the galaxy menu
// BUG-0956 (RE): the listeners of one message id in the game's message managers (the one of the mode switch,
// [App::MessageManager_ptr], and IMessageManager::Get's): the entry's object, its vtable and slot +4 (the
// listener's HandleMessage), the function / user of a function entry; "BAD" = a vtable outside the exe image
static bool readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION m;
    if (!p || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return (uintptr_t)p + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}
static bool in_exe(uint32_t v)
{
    auto base = (uintptr_t)GetModuleHandleW(nullptr);
    auto nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    return v >= base && v < base + nt->OptionalHeader.SizeOfImage;
}
int c_msglisteners(void*, int argc, const char* const* argv, SkOut* out)
{
    SAY("%s", msglisteners_text(argc > 1 ? (uint32_t)strtoul(argv[1], nullptr, 16) : 0x212D3E7u).c_str());
    return 0;
}
} // namespace
std::string msglisteners_text(uint32_t id)
{
    std::string o;
    char b[200];
#undef SAY
#define SAY(...) (snprintf(b, sizeof b, __VA_ARGS__), o += b)
    const char* names[2] = {"App::MessageManager_ptr", "App::IMessageManager_ptr"};
    for (const char* nm : names) {
        uintptr_t g = H->sym(nm);
        const uint32_t* mgr = g && readable((void*)g, 4) ? *(const uint32_t* const*)g : nullptr;
        if (!mgr || !readable(mgr, 0x80)) {
            SAY("%s: none\n", nm);
            continue;
        }
        const uint32_t* buckets = (const uint32_t*)mgr[0x74 / 4];
        uint32_t n = mgr[0x78 / 4];
        SAY("%s = %08X, %u buckets\n", nm, (unsigned)(uintptr_t)mgr, n);
        if (!n || !readable(buckets, 4 * (n + 1)))
            continue;
        int k = 0;
        for (const uint32_t* node = (const uint32_t*)buckets[id % n]; node && readable(node, 12) && k < 64; node = (const uint32_t*)node[2], k++) {
            if (node[0] != id)
                continue;
            const uint32_t* list = (const uint32_t*)node[1];
            int i = 0;
            for (const uint32_t* e = list && readable(list, 8) ? (const uint32_t*)list[0] : nullptr; e && e != list && readable(e, 0x1C) && i < 200;
                 e = (const uint32_t*)e[0], i++) {
                uint32_t obj = e[2], vt = 0, h = 0;
                if (obj && readable((void*)(uintptr_t)obj, 4)) {
                    vt = *(const uint32_t*)(uintptr_t)obj;
                    if (readable((void*)(uintptr_t)vt, 8))
                        h = ((const uint32_t*)(uintptr_t)vt)[1];
                }
                SAY("  %08X: %u obj=%08X vt=%08X +4=%08X fn=%08X user=%08X flag=%u%s\n", id, i, obj, vt, h, e[5], e[6], e[4] & 0xFF,
                    obj && !(in_exe(vt) && in_exe(h)) ? " BAD" : "");
            }
        }
        break; // both globals hold the same manager (seen 2026-11-12)
    }
    return o;
}
#undef SAY
#define SAY(...) H->out_printf(out, __VA_ARGS__)
namespace {

int c_menu(void*, int, const char* const*, SkOut* out)
{
    std::string err;
    if (!world::post_quit_to_menu(err)) { // (world.cpp: the message, BUG-0264's stage_leaving; BUG-0854)
        SAY("%s", err.c_str());
        return 1;
    }
    SAY("leaving to the galaxy menu (mode 0x%08X)", game::mode_id());
    return 0;
}

// tests (BUG-0578): the player's way out of the game: WM_CLOSE to the game window (the window's close button / Alt+F4),
// so that the game runs its own exit (cSporeApp::Shutdown, the game mode manager and editors disposed); the lab's
// kill and `quit` end the process without it
// tests (BUG-0167): the galaxy menu intro starts in the first tick of the next stage (world::arm_intro_in_stage)
int c_introstage(void*, int, const char* const*, SkOut* out)
{
    world::arm_intro_in_stage();
    SAY("the menu intro starts in the first tick of the next stage");
    return 0;
}

int c_close(void*, int, const char* const*, SkOut* out)
{
    HWND w = (HWND)H->game_window();
    if (!w) {
        SAY("no game window");
        return 1;
    }
    PostMessageW(w, WM_CLOSE, 0, 0);
    SAY("WM_CLOSE posted to the game window (mode 0x%08X)", game::mode_id());
    return 0;
}

// tests (BUG-0270): the window manager's modal window becomes a destroyed object
int c_deadmodal(void*, int argc, const char* const* argv, SkOut* out)
{
    uintptr_t get = H->sym("UTFWin::IWindowManager::Get");
    void* wm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (argc > 1 && !strcmp(argv[1], "list")) { // BUG-0956
        SAY("%s", gamefixes::fake_dead_listed(wm).c_str());
        return 0;
    }
    SAY("%s", gamefixes::fake_dead_modal(wm).c_str());
    return 0;
}

// tests (BUG-0310): the next citizen the stage change sends walking is a cleaned-up one
int c_leavewalk(void*, int, const char* const*, SkOut* out)
{
    SAY("%s", gamefixes::arm_leave_walk().c_str());
    return 0;
}

// tests (BUG-0449): a physics body loses its owner (the state the guest's game faulted on in every frame)
int c_physorphan(void*, int, const char* const*, SkOut* out)
{
    SAY("%s", gamefixes::arm_physics_orphan().c_str());
    return 0;
}

// tests (BUG-0719): the game's fixed pools' fill
int c_pools(void*, int, const char* const*, SkOut* out)
{
    SAY("%s", gamefixes::pools_report().c_str());
    return 0;
}

// tests (BUG-0453): the next NPC in its attack animation loses its target (as when the target is destroyed)
int c_attackcut(void*, int, const char* const*, SkOut* out)
{
    SAY("%s", gamefixes::arm_attack_cut().c_str());
    return 0;
}

// tests (BUG-0582): the next attacker of a creature attack finds no spot record
int c_spotcut(void*, int, const char* const*, SkOut* out)
{
    SAY("%s", gamefixes::arm_spot_cut().c_str());
    return 0;
}

// tests (BUG-0686): game.ufo_model forgets the last known ship model
int c_ufoforget(void*, int, const char* const*, SkOut* out)
{
    SAY("%s", gamefixes::ufo_forget().c_str());
    return 0;
}

// tests (BUG-0329): coop.ground <x> <y> <z> - the terrain radius in that direction (GetHeightAt) and
// the point's own radius: how high a position is over this game's ground
int c_ground(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc < 4) {
        SAY("usage: coop.ground <x> <y> <z>");
        return 1;
    }
    net::Vec3 p{(float)atof(argv[1]), (float)atof(argv[2]), (float)atof(argv[3])};
    float g = game::ground_radius(p);
    float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    SAY("ground %.2f point %.2f above %.2f", g, r, r - g);
    // BUG-0562: the surface on the point's radial line as the game's snap finds it (cPlanetModel_SurfacePoint: the terrain
    // or a collision object over it, a nest mound or a rock, where the collision is loaded): the point's height over it
    uintptr_t sf = H->sym("SporeCoop::cPlanetModel_SurfacePoint"), pg = H->sym("Simulator::cPlanetModel::Get");
    void* pm = pg ? ((void*(__cdecl*)())pg)() : nullptr;
    if (pm && sf && r > 1.f) {
        float in[3] = {p.x, p.y, p.z}, o[3] = {0, 0, 0};
        skg::thiscall<float*>(sf, pm, o, (const float*)in, 0);
        float sr = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
        if (sr > 1.f)
            SAY(" surface %.2f over_surface %.2f", sr, r - sr);
    }
    return 0;
}

// tests (BUG-0917): `coop.abilities reach` takes only a live creature of the world's list for the avatar's target
static bool g_fix_reach_target = true;
COOP_FIX_UNSAFE("test.reach_target", "BUG-0917", g_fix_reach_target,
                "tests: coop.abilities reach calls the game's gap function only on a target that is a live creature of the object list");

// What the avatar's combat target (+0xE7C, a cCombatant*) is, by pointer compares with the game's object list
// only (safe on a freed target): `creature` when it is the combatant (+0x5A8) of a live creature; else `owner`,
// the listed object the pointer lies in (the nearest below it, within 0x1000), with its noun.
struct TargetKind {
    void* creature = nullptr;
    void* owner = nullptr;
    uint32_t noun = 0;
    bool alive = false;
};
TargetKind target_kind(uint8_t* tc)
{
    struct F {
        uint8_t* tc;
        TargetKind k;
    } f{tc, {}};
    if (!tc)
        return f.k;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* f = (F*)u;
            auto* b = (uint8_t*)o;
            if (b > f->tc || f->tc - b >= 0x1000 || (f->k.owner && b < (uint8_t*)f->k.owner))
                return true;
            f->k.owner = o;
            f->k.noun = noun;
            f->k.alive = game::object_alive(o);
            return true;
        },
        &f);
    if (f.k.owner && f.k.noun == 0x18EB45Eu && f.k.alive && (uint8_t*)f.k.owner + 0x5A8 == tc)
        f.k.creature = f.k.owner;
    return f.k;
}

// tests (BUG-0366): coop.abilities [play <i>] - the own avatar's abilities (vt 0xB0 count, vt 0xB4
// get(i): +8 type, +0x10) and its height over the ground; `play` uses one (PlayAbility)
int c_abilities(void*, int argc, const char* const* argv, SkOut* out)
{
    void* av = game::avatar();
    if (!av) {
        SAY("no avatar");
        return 1;
    }
    if (argc > 2 && !strcmp(argv[1], "play")) {
        uintptr_t pa = H->sym("Simulator::cCreatureBase::PlayAbility");
        int r = pa ? skg::thiscall<int>(pa, av, atoi(argv[2]), (void*)nullptr) : -2;
        SAY("play %d -> %d\n", atoi(argv[2]), r);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "target")) { // tests (BUG-0934): the avatar's combat target through the game's own
        // SetTarget (creature vt 0x84, thiscall(target combatant = creature + 0x5A8, 1, 0), as the click's
        // CreatureClickObject does): `none` clears it, <address> aims at that puppet of ours
        void* p = nullptr;
        if (strcmp(argv[2], "none")) {
            void* want = (void*)(uintptr_t)strtoul(argv[2], nullptr, 16);
            for (void* q : game::puppets())
                if (q == want)
                    p = q;
            // `any` (BUG-0964): any live creature of the world's list (an animal, an NPC proxy)
            if (!p && argc > 3 && !strcmp(argv[3], "any") && game::listed(want, 0x18EB45Eu))
                p = want;
            if (!p || !skg::readable(p, 0x5B0) || game::is_dead(p)) {
                SAY("%s is no live puppet of ours\n", argv[2]);
                return 1;
            }
        }
        skg::vcall<void>(av, 0x84 / 4, p ? (void*)((uint8_t*)p + 0x5A8) : (void*)nullptr, 1, 0);
        SAY("target %p (puppet %p)\n", *(void**)((uint8_t*)av + 0xE7C), p);
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "ground")) { // BUG-0377: the ground radius in the direction of x y z, no move
        net::Vec3 p{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        SAY("ground %.2f\n", game::ground_radius(p));
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "goto")) { // onto the ground in the direction of x y z
        net::Vec3 p{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), g = game::ground_radius(p);
        if (r < 1.f || g <= 0) {
            SAY("no ground there");
            return 1;
        }
        float k = (g + 0.05f) / r;
        game::teleport(av, {p.x * k, p.y * k, p.z * k}, game::orientation(av));
        SAY("at ground %.2f\n", g);
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "put")) { // tests (BUG-0592): to x y z as given, no grounding [1: SetPosition]
        net::Vec3 p{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        if (argc > 5 && atoi(argv[5]) == 1)
            game::set_position(av, p); // (the game puts a standing creature moved so back onto its ground)
        else
            game::teleport(av, p, game::orientation(av));
        p = game::position(av);
        SAY("at (%.2f %.2f %.2f)\n", p.x, p.y, p.z);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "reach")) { // tests (BUG-0618): the game's own reach of each ability
        // The ability use 0xD1E930 for melee / ranged attacks: gap = Ability_TargetGap(avatar spatial, target
        // spatial, 1) (centre distance minus both radii), in reach when gap <= (ability +0xF4 + +0xA8) *
        // Ability_RangeScale(0); else the game posts a walk-and-attack goal (0x20). Status 0 = usable (0xD49220).
        uintptr_t gap_f = H->sym("SporeCoop::Ability_TargetGap"), scale_f = H->sym("SporeCoop::Ability_RangeScale"),
                  st_f = H->sym("SporeCoop::AbilityBar_Status");
        uint8_t* tc = *(uint8_t**)((uint8_t*)av + 0xE7C); // the target's combatant (a creature's is at +0x5A8)
        float gap = -1.f;
        if (!g_fix_reach_target) { // before BUG-0917: every target taken for a creature after a `readable` check
            if (tc && gap_f && skg::readable(tc - 0x5A8, 0x5B0)) {
                uint8_t* t = tc - 0x5A8;
                gap = ((float(__cdecl*)(void*, void*, bool))gap_f)((uint8_t*)av + 0xC0, t + 0xC0, true);
                SAY("target %p dead=%d gap=%.2f\n", (void*)t, *(uint8_t*)(t + 0xB5E), gap);
            } else
                SAY("target none\n");
        } else {
            // BUG-0917: +0xE7C is a cCombatant of any class (the game casts it to its spatial with the combatant's
            // vt 0x5C before Ability_TargetGap), and a creature can be gone from the world while the field still
            // names it. Only a live creature of the game's object list is a target here; nothing is called or read
            // on anything else (pointer compares only).
            TargetKind k = target_kind(tc);
            if (k.creature && gap_f) {
                uint8_t* t = (uint8_t*)k.creature;
                gap = ((float(__cdecl*)(void*, void*, bool))gap_f)((uint8_t*)av + 0xC0, t + 0xC0, true);
                SAY("target %p dead=%d gap=%.2f\n", (void*)t, *(uint8_t*)(t + 0xB5E), gap);
            } else if (!tc)
                SAY("target none\n");
            else if (k.owner)
                SAY("target none (the game's target %p is no live creature: object %p noun 0x%X alive=%d, its "
                    "combatant at +0x%X)\n",
                    (void*)tc, k.owner, k.noun, k.alive ? 1 : 0, (unsigned)(tc - (uint8_t*)k.owner));
            else
                SAY("target none (the game's target %p is no object of the world's list)\n", (void*)tc);
            static uint8_t* s_noted; // one log line per such target: what the avatar's target field named
            if (tc && !k.creature && tc != s_noted) {
                s_noted = tc;
                H->log(g_self, SK_INFO,
                       "abilities: the avatar's target %p is no live creature of the list (object %p noun 0x%X "
                       "alive=%d, +0x%X): reach reports no target (BUG-0917)",
                       (void*)tc, k.owner, k.noun, k.alive ? 1 : 0,
                       k.owner ? (unsigned)(tc - (uint8_t*)k.owner) : 0u);
            }
        }
        // BUG-0957: cdecl float(int kind, creature*): the creature is the second argument (its +0xE84 object's +0x358
        // scale); called with the kind alone, the function read the caller's stack as the creature and faulted
        float sc = scale_f ? ((float(__cdecl*)(int, void*))scale_f)(0, av) : 1.f;
        int n = skg::vcall<int>(av, 0xB0 / 4);
        for (int i = 0; i < n && i < 64; i++) {
            uint8_t* a = skg::vcall<uint8_t*>(av, 0xB4 / 4, i);
            if (!a || !skg::readable(a, 0x13C))
                continue;
            float reach = (*(float*)(a + 0xF4) + *(float*)(a + 0xA8)) * sc;
            int st = st_f ? ((int(__cdecl*)(int))st_f)(i) : -1;
            SAY("ab %d type=0x%X row=%d key=0x%X reach=%.2f min=%.2f/%.2f status=%d\n", i, *(uint32_t*)(a + 8),
                *(int*)(a + 0xC), *(uint32_t*)(a + 0x38), reach, *(float*)(a + 0x134), *(float*)(a + 0x138), st);
        }
        // the HUD bar (AbilityBar_ptr): the keys 1..5 use the slots of the stance's row (row*0x28 + col*8)
        if (uintptr_t bp = H->sym("SporeCoop::AbilityBar_ptr"))
            if (uint8_t* bar = *(uint8_t**)bp; bar && skg::readable(bar, 0xD8))
                for (int row = 0; row < 5; row++) {
                    SAY("bar %d:", row);
                    for (int col = 0; col < 5; col++)
                        SAY(" %d", *(int*)(bar + row * 0x28 + col * 8));
                    SAY("\n");
                }
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "walkto")) { // tests (BUG-0964): the game's own walk to x y z (as the BT's
        // walk-and-use asks it: cCreatureBase::WalkTo(2 = walk, &pos, &facing, stop 0.1, acceptable 0.2))
        uintptr_t wt = H->sym("Simulator::cCreatureBase::WalkTo");
        net::Vec3 p = game::position(av), q{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        net::Vec3 f{q.x - p.x, q.y - p.y, q.z - p.z};
        if (!wt) {
            SAY("no WalkTo\n");
            return 1;
        }
        skg::thiscall<void>(wt, av, 2, &q, &f, 0.1f, 0.2f);
        SAY("walking to (%.2f %.2f %.2f)\n", q.x, q.y, q.z);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "walk")) { // tests (BUG-0964): what the avatar's walk-and-use is doing
        // the behaviour tree's node (AI +0x1D8), its stimuli (AI +8: 8 slots of 0x38, masks +0/+4, sender +0x2C),
        // the timed goal slots (+0xB48 -> 8 x 0x44, template +0x24), the locomotion: desired / standard speed
        // (spatial +0x1E0 / +0x1E4), the movement request (spatial +0x1F4: destination +0x14, goal-stop +0x20, mode
        // +0x5C, acceptable-stop +0x60) and the velocity (creature +0x288)
        uint8_t* c = (uint8_t*)av;
        uint8_t* ai = *(uint8_t**)(c + 0xB4C);
        if (ai && skg::readable(ai, 0x200)) {
            SAY("node=%08X ai+1D0:", *(uint32_t*)(ai + 0x1D8));
            for (int o = 0x1C0; o < 0x200; o += 4)
                SAY(" %X", *(uint32_t*)(ai + o));
            SAY("\nstim:");
            for (int i = 0; i < 8; i++) {
                uint8_t* s = ai + 8 + i * 0x38;
                if (*(uint32_t*)s || *(uint32_t*)(s + 4))
                    SAY(" [%X:%X v=%g c=%X t=%X snd=%p]", *(uint32_t*)s, *(uint32_t*)(s + 4), *(float*)(s + 8),
                        *(uint32_t*)(s + 0xC), *(uint32_t*)(s + 0x10), *(void**)(s + 0x2C));
            }
            SAY("\n");
        }
        if (uint8_t* g = *(uint8_t**)(c + 0xB48); g && skg::readable(g, 0x230)) {
            SAY("goals mask=%X:", *(uint32_t*)(g + 0x22C));
            for (int i = 0; i < 8; i++)
                if (*(int*)(g + i * 0x44 + 0x24) != -1)
                    SAY(" [%d t=%d grp=%d s=%.1f tg=%p]", i, *(int*)(g + i * 0x44 + 0x24), *(int*)(g + i * 0x44 + 0x28),
                        *(float*)(g + i * 0x44 + 0x2C), *(void**)(g + i * 0x44 + 0x40));
            SAY("\n");
        }
        uint8_t* sp = c + 0xC0;
        uint8_t* rq = sp + 0x1F4;
        net::Vec3 p = game::position(av), v = *(net::Vec3*)(c + game::kVelOff);
        SAY("speed desired=%.2f standard=%.2f vel=%.2f pos=(%.2f %.2f %.2f) req dest=(%.2f %.2f %.2f) stop=%.2f mode=%d acc=%.2f\n",
            *(float*)(sp + 0x1E0), *(float*)(sp + 0x1E4), std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), p.x, p.y, p.z,
            *(float*)(rq + 0x14), *(float*)(rq + 0x18), *(float*)(rq + 0x1C), *(float*)(rq + 0x20), *(int*)(rq + 0x5C),
            *(float*)(rq + 0x60));
        { // the facing (spatial vt 0x5C, as the walk step 0xB46EF0 reads it) against the way to the request's goal:
            // the step's speed is max(0, cos) of that angle; it turns by slerp, set only when the result is finite
            float fw[3] = {};
            skg::thiscall<void*>((uintptr_t)skg::vtable(sp)[0x5C / 4], sp, fw);
            net::Quat q = game::orientation(av);
            float dx = *(float*)(rq + 0x14) - p.x, dy = *(float*)(rq + 0x18) - p.y, dz = *(float*)(rq + 0x1C) - p.z;
            float dl = std::sqrt(dx * dx + dy * dy + dz * dz) + 1e-6f;
            SAY("facing=(%.3f %.3f %.3f) quat=(%.4f %.4f %.4f %.4f) |q|=%.5f cos_to_goal=%.3f\n", fw[0], fw[1], fw[2], q.x,
                q.y, q.z, q.w, std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w),
                (fw[0] * dx + fw[1] * dy + fw[2] * dz) / dl);
            // the steering's sum this frame (Locomotion_Steer: the goal pull, the obstacle and flocking forces of
            // the list at spatial +0x490): the walk step goes along it at max(0, cos to the facing) of the speed
            if (uintptr_t st = H->sym("SporeCoop::Locomotion_Steer"); st && argc > 2) {
                float s[3] = {};
                skg::thiscall<void>(st, sp, s);
                float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]) + 1e-9f;
                SAY("steer=(%.3f %.3f %.3f) |s|=%.3f cos_facing=%.3f cos_goal=%.3f forces=%d:", s[0], s[1], s[2], sl,
                    (s[0] * fw[0] + s[1] * fw[1] + s[2] * fw[2]) / sl, (s[0] * dx + s[1] * dy + s[2] * dz) / sl / dl,
                    (int)((*(uintptr_t*)(sp + 0x494) - *(uintptr_t*)(sp + 0x490)) / 4));
                for (uintptr_t* f = *(uintptr_t**)(sp + 0x490); f && f != *(uintptr_t**)(sp + 0x494); f++)
                    SAY(" %X", (unsigned)*f);
                SAY("\n");
                // the world's objects within 5 m: noun@distance/cos of their bearing to the way to the goal
                struct U {
                    net::Vec3 p, g;
                    void* self;
                    std::string s;
                    int n;
                } u{p, {dx / dl, dy / dl, dz / dl}, av, "", 0};
                game::for_each_object(
                    [](void* o, uint32_t noun, void* up) {
                        auto* u = (U*)up;
                        net::Vec3 q;
                        if (o == u->self || u->n >= 24 || !game::object_position(o, q))
                            return true;
                        float ex = q.x - u->p.x, ey = q.y - u->p.y, ez = q.z - u->p.z, d = std::sqrt(ex * ex + ey * ey + ez * ez);
                        if (d > 5.f)
                            return true;
                        char b[64];
                        snprintf(b, sizeof b, " %08X@%.1f/%.2f", noun, d, d > 0.01f ? (ex * u->g.x + ey * u->g.y + ez * u->g.z) / d : 0.f);
                        u->s += b;
                        u->n++;
                        return true;
                    },
                    &u);
                SAY("near:%s\n", u.s.c_str());
                struct N {
                    net::Vec3 p;
                    float d;
                    net::Vec3 q;
                } nn{p, 1e9f, {}};
                game::for_each_object(
                    [](void* o, uint32_t noun, void* up) {
                        auto* n = (N*)up;
                        net::Vec3 q;
                        if (noun != 0x52AA6122u || !game::object_position(o, q))
                            return true;
                        float d = std::sqrt((q.x - n->p.x) * (q.x - n->p.x) + (q.y - n->p.y) * (q.y - n->p.y) +
                                            (q.z - n->p.z) * (q.z - n->p.z));
                        if (d < n->d)
                            n->d = d, n->q = q;
                        return true;
                    },
                    &nn);
                float pr = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
                SAY("nest=%.1f m (its radius %.2f, ground %.2f) me: radius %.2f ground %.2f surface %.2f\n", nn.d,
                    std::sqrt(nn.q.x * nn.q.x + nn.q.y * nn.q.y + nn.q.z * nn.q.z), game::ground_radius(nn.q), pr,
                    game::ground_radius(p), game::surface_radius(p));
            }
        }
        SAY("req:");
        for (int o = 0; o < 0x70; o += 4)
            SAY(" %X", *(uint32_t*)(rq + o));
        if (argc > 2 && skg::readable(c, 0x1700)) { // `walk raw`: the creature's dwords +0..+0x1700 (spatial at +0xC0)
            for (int o = 0; o < 0x1700; o += 4) {
                if (o % 0x40 == 0)
                    SAY("\nc+%X:", o);
                SAY(" %X", *(uint32_t*)(c + o));
            }
        }
        uint32_t an = 0;
        game::drawn_state(av, nullptr, &an);
        SAY("\nanim=%08X flags b58=%X mark b5d=%d f98=%d\n", an, *(uint32_t*)(c + 0xB58), *(uint8_t*)(c + 0xB5D),
            *(int*)(c + 0xF98));
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "halt")) { // tests (BUG-0408): it stops at once (a stop without slowing down)
        game::stop(av);
        game::teleport(av, game::position(av), game::orientation(av));
        *(net::Vec3*)((uint8_t*)av + game::kVelOff) = net::Vec3{};
        SAY("halted\n");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "lift")) { // put it <m> higher (its fall is the second half of a jump)
        net::Vec3 p = game::position(av);
        float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (!(r > 1e-3f)) { // BUG-1030: no direction "up" at the planet's centre (k was a division by 0)
            SAY("no lift: the position is at the planet's centre\n");
            return 1;
        }
        float k = (r + (float)atof(argv[2])) / r;
        game::teleport(av, {p.x * k, p.y * k, p.z * k}, game::orientation(av));
        SAY("lifted %.2f\n", (float)atof(argv[2]));
        return 0;
    }
    int n = skg::vcall<int>(av, 0xB0 / 4);
    for (int i = 0; i < n && i < 64; i++) {
        uint8_t* a = skg::vcall<uint8_t*>(av, 0xB4 / 4, i);
        if (a && skg::readable(a, 0x14))
            SAY("%d type=0x%X +0x10=%u\n", i, *(uint32_t*)(a + 8), *(uint32_t*)(a + 0x10));
    }
    net::Vec3 p = game::position(av);
    float g = game::ground_radius(p);
    SAY("above=%.2f", std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) - g);
    return 0;
}

int c_load(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc < 3) {
        SAY("usage: coop.load <GameN> <Planet[.spo]>");
        return 1;
    }
    std::string err;
    if (!world::load_local(argv[1], argv[2], err)) {
        SAY("%s", err.c_str());
        return 1;
    }
    SAY("loading %s/%s", argv[1], argv[2]);
    return 0;
}

// coop.cell list | spawn <slot> [dx dy] : experiment with cell-stage objects
int c_cell(void*, int argc, const char* const* argv, SkOut* out)
{
    using namespace skg;
    // cCellGame stays allocated after the Cell stage, its pool does not: only in the stage
    uint8_t* g = game::mode_id() == modes::kCell ? modes::cell_game() : nullptr;
    if (!g) {
        SAY("not in the cell stage");
        return 1;
    }
    uint8_t* data = *(uint8_t**)(g + 0x1C);
    uint32_t n = *(uint32_t*)(g + 0x1C + 0x10);
    uint32_t av = *(uint32_t*)(g + 0x411C);
    if (argc >= 2 && !strcmp(argv[1], "pool")) {
        // the pool header G+0x1C .. +0x18, the avatar index, the rebuild flag G+0x51D8 and the
        // avatar's +0x111 / +0xFC (tests: what a rebuild keeps, BUG-0221, BUG-0237)
        const uint32_t* h = (const uint32_t*)(g + 0x1C);
        uint32_t slot = av & 0xFFFF;
        uint8_t* a = data && av && cell_logic::slot_in_pool(slot, n, h[6], true) &&
                             *(uint32_t*)(data + slot * 0x398) == av ? data + slot * 0x398 : nullptr;
        SAY("pool data=%08X free=%08X gen=%u live=%u high=%u size=%X cap=%u avatar=%08X rebuild=%u "
            "av_ptr=%p av_invuln=%d av_fc=%08X\n", h[0], h[1], h[2], h[3], h[4], h[5], h[6], av,
            (unsigned)*(uint8_t*)(g + 0x51D8), (void*)a, a ? *(uint8_t*)(a + 0x111) : -1, a ? *(uint32_t*)(a + 0xFC) : 0);
        return 0;
    }
    if (argc < 2 || !strcmp(argv[1], "list")) {
        for (uint32_t i = 1; i < n && i < 4096; i++) { // [1, highWater): BUG-0083, BUG-0093
            uint8_t* c = data + i * 0x398;
            if ((*(uint32_t*)c & 0xFFFF) != i)
                continue;
            float* p = (float*)(c + 0x4C);
            SAY("%s#%u idx=%08X pos=(%.1f %.1f %.1f) size=%.2f res=%p model=%08X scale=%d hp=%d op=%.2f\n",
                *(uint32_t*)c == av ? ">" : " ", i, *(uint32_t*)c, p[0], p[1], p[2], *(float*)(c + 0x58),
                *(void**)(c + 0x108), *(uint32_t*)(c + 0xFC), *(int*)(c + 0x358), *(int*)(c + 0x244),
                *(float*)(c + 0xA0));
        }
        return 0;
    }
    if (!strcmp(argv[1], "spawn") && argc >= 3) {
        // BUG-0093: only a live cell of the pool (atoi let "99999" / "-1" read far outside it)
        uint32_t slot = 0;
        if (!data || !cell_logic::parse_cell_slot(argv[2], n, &slot) || (*(uint32_t*)(data + slot * 0x398) & 0xFFFF) != slot) {
            SAY("no such cell: %s (coop.cell list shows the live ones)", argv[2]);
            return 1;
        }
        uint32_t me_slot = av & 0xFFFF;
        if (!cell_logic::slot_in_pool(me_slot, n, 4096, true) || (*(uint32_t*)(data + me_slot * 0x398) & 0xFFFF) != me_slot) {
            SAY("no avatar cell");
            return 1;
        }
        uint8_t* src = data + slot * 0x398;
        uint8_t* me = data + me_slot * 0x398;
        float* mp = (float*)(me + 0x4C);
        Vector3 pos{mp[0] + (argc > 3 ? (float)atof(argv[3]) : 4.f), mp[1] + (argc > 4 ? (float)atof(argv[4]) : 0.f), mp[2]};
        if (!need_syms(out, {"Simulator::Cell::CreateCellObject"}))
            return 1;
        uint32_t idx = cdecl_call<uint32_t>(H->sym("Simulator::Cell::CreateCellObject"), *(void**)(g + 0x40FC), &pos, 0.f,
                                            *(void**)(src + 0x108), argc > 5 ? atoi(argv[5]) : *(int*)(src + 0x358), 1.f, 1.f, 1, (void*)nullptr, 0);
        if (argc > 6) // tests (BUG-0455): the guests get this cell with a model instance they do not have
            cellsync::set_fake_model(idx, strtoul(argv[6], nullptr, 16));
        SAY("created %08X from #%u", idx, slot);
        return 0;
    }
    SAY("usage: coop.cell list | spawn <slot> [dx dy [level [model instance hex sent to the guests]]]");
    return 1;
}

int c_cam(void*, int argc, const char* const* argv, SkOut* out)
{
    using namespace skg;
    if (!need_syms(out, {"App::IGameModeManager::Get", "App::cViewer::GetCameraToPoint"}))
        return 1;
    uintptr_t mmget = H->sym("App::IGameModeManager::Get");
    void* mm = cdecl_call<void*>(mmget);
    void* viewer = mm ? vcall<void*>(mm, 0x58 / 4) : nullptr;
    if (!viewer) {
        SAY("no viewer");
        return 1;
    }
    float* cam = field<float*>(viewer, 0x170);
    SAY("viewer=%p camera=%p\n", viewer, cam);
    if (!cam || !readable(cam, 0x98))
        return 1;
    for (int r = 0; r < 4; r++)
        SAY("  m[%d] = %8.3f %8.3f %8.3f %8.3f\n", r, cam[r * 4], cam[r * 4 + 1], cam[r * 4 + 2], cam[r * 4 + 3]);
    SAY("viewOffset=(%.3f %.3f) viewWindow=(%.3f %.3f) recip=(%.3f %.3f) proj=%d near=%.3f far=%.1f\n",
        cam[0x54 / 4], cam[0x58 / 4], cam[0x5C / 4], cam[0x60 / 4], cam[0x64 / 4], cam[0x68 / 4],
        *(int*)&cam[0x6C / 4], cam[0x70 / 4], cam[0x74 / 4]);
    uint32_t* vp = (uint32_t*)&cam[0x78 / 4];
    SAY("viewport x=%u y=%u w=%u h=%u\n", vp[0], vp[1], vp[2], vp[3]);
    float* vm = (float*)viewer;
    for (int r = 0; r < 4; r++)
        SAY("  view[%d] = %8.3f %8.3f %8.3f %8.3f\n", r, vm[r * 4], vm[r * 4 + 1], vm[r * 4 + 2], vm[r * 4 + 3]);
    if (argc >= 3) {
        Vector3 pos{}, dir{};
        uintptr_t f = H->sym("App::cViewer::GetCameraToPoint");
        bool ok = thiscall<bool>(f, viewer, (float)atof(argv[1]), (float)atof(argv[2]), &pos, &dir);
        SAY("ray(%s,%s) ok=%d pos=(%.2f %.2f %.2f) dir=(%.3f %.3f %.3f)\n", argv[1], argv[2], ok, pos.x, pos.y,
            pos.z, dir.x, dir.y, dir.z);
    }
    void* av = game::avatar();
    if (av) {
        auto p = game::position(av);
        SAY("avatar=(%.2f %.2f %.2f)\n", p.x, p.y, p.z);
        // camera space with both matrix conventions
        float a[3], b[3];
        for (int i = 0; i < 3; i++) {
            a[i] = cam[i * 4 + 0] * p.x + cam[i * 4 + 1] * p.y + cam[i * 4 + 2] * p.z + cam[i * 4 + 3];
            b[i] = cam[0 * 4 + i] * p.x + cam[1 * 4 + i] * p.y + cam[2 * 4 + i] * p.z + cam[3 * 4 + i];
        }
        SAY("rows: (%.2f %.2f %.2f)  cols: (%.2f %.2f %.2f)\n", a[0], a[1], a[2], b[0], b[1], b[2]);
    }
    return 0;
}
} // namespace

// ---- coop.sample: where the game's main thread spends its time (BUG-0355, a 3 fps guest) ----
// Suspends the main thread every ~1 ms for <ms>, reads its EIP and the return-address-like words of
// the top of its stack (inside SporeApp.exe's image), and prints the most frequent ones at their
// 0x400000 addresses: "self" is where the thread was, "stack" counts each address once per sample
// (an inclusive profile of the callers). Runs on the console thread; the game goes on running.
namespace {
std::atomic<DWORD> g_main_tid{0};
int c_sample(void*, int argc, const char* const* argv, SkOut* out)
{
    int ms = argc > 1 ? atoi(argv[1]) : 2000, top = argc > 2 ? atoi(argv[2]) : 25;
    ms = std::clamp(ms, 100, 30000);
    DWORD tid = g_main_tid.load();
    if (tid == GetCurrentThreadId()) {
        SAY("not on the main thread");
        return 1;
    }
    HANDLE th = tid ? OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid) : nullptr;
    if (!th) {
        SAY("no main thread (tid %lu)", tid);
        return 1;
    }
    uintptr_t base = H->image_base();
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    uintptr_t lo = base + 0x1000, hi = base + nt->OptionalHeader.SizeOfImage;
    auto va = [&](uintptr_t a) { return (unsigned)(a - base + 0x400000); };
    // the main thread's stack top (its TEB's StackBase): the copy below stays inside its stack
    struct { LONG status; void* teb; uintptr_t pid, tid, mask; LONG prio, base; } tbi{};
    using NtQIT = LONG(WINAPI*)(HANDLE, int, void*, ULONG, ULONG*);
    auto qit = (NtQIT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    uintptr_t stack_top = 0;
    if (qit && qit(th, 0 /* ThreadBasicInformation */, &tbi, sizeof tbi, nullptr) == 0 && tbi.teb)
        stack_top = ((uintptr_t*)tbi.teb)[1];
    std::unordered_map<uintptr_t, uint32_t> self, stack;
    self.reserve(4096);
    stack.reserve(16384);
    uint32_t n = 0, outside = 0;
    DWORD t0 = GetTickCount();
    std::vector<uintptr_t> seen;
    seen.reserve(512);
    static uint32_t words[512];
    while (GetTickCount() - t0 < (DWORD)ms) {
        // nothing that takes a lock while the thread is suspended (it may hold the heap's or
        // Wine's virtual memory lock): no allocation, no ReadProcessMemory, only a plain copy
        if (SuspendThread(th) == (DWORD)-1)
            break;
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        size_t got = 0;
        bool ok = GetThreadContext(th, &ctx);
        if (ok && stack_top > ctx.Esp && stack_top - ctx.Esp >= 4) {
            got = std::min<size_t>(sizeof words, (stack_top - ctx.Esp) & ~(size_t)3);
            memcpy(words, (void*)(uintptr_t)ctx.Esp, got);
        }
        ResumeThread(th);
        if (ok) {
            n++;
            uintptr_t eip = ctx.Eip;
            if (eip >= lo && eip < hi)
                self[eip & ~(uintptr_t)0xF]++;
            else
                outside++;
            seen.clear();
            for (size_t i = 0; i < got / 4; i++)
                if (words[i] >= lo && words[i] < hi && std::find(seen.begin(), seen.end(), words[i]) == seen.end()) {
                    seen.push_back(words[i]);
                    stack[words[i]]++;
                }
        }
        Sleep(1);
    }
    CloseHandle(th);
    auto print = [&](const char* what, std::unordered_map<uintptr_t, uint32_t>& m) {
        std::vector<std::pair<uint32_t, uintptr_t>> v;
        for (auto& [a, c] : m)
            v.push_back({c, a});
        std::sort(v.rbegin(), v.rend());
        SAY("%s:", what);
        for (int i = 0; i < top && i < (int)v.size(); i++)
            SAY(" %08X=%u", va(v[i].second), v[i].first);
        SAY("\n");
    };
    SAY("samples=%u outside_exe=%u ms=%d\n", n, outside, ms);
    print("self", self);
    print("stack", stack);
    return 0;
}
} // namespace
// "module+offset" of an address (RE, BUG-0355)
void coop_module_of(uintptr_t a, char* out, size_t cap)
{
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, name, sizeof name);
        const char* b = strrchr(name, '\\');
        snprintf(out, cap, "%s+0x%X", b ? b + 1 : name, (unsigned)(a - (uintptr_t)m));
    } else
        snprintf(out, cap, "%08X", (unsigned)a);
}
namespace testview {
void init(const SkHost*, SkMod*); // test_view.cpp: coop.view
}

void coop_register_debug_commands(const SkHost* host, SkMod* self)
{
    H = host;
    host->subscribe(self, SK_EV_FRAME, [](void*, int, void*) { g_main_tid.store(GetCurrentThreadId()); }, nullptr);
    host->command_add(self, "coop.sample", "coop.sample [ms] [top] - sample the main thread's EIP and stack (RE, BUG-0355)", c_sample,
                      nullptr, SK_CMD_ANY_THREAD | SK_CMD_NOT_ON_MAIN);
    void coop_register_debug_commands2(const SkHost*, SkMod*);
    coop_register_debug_commands2(host, self);
    host->command_add(self, "coop.cam", "coop.cam [x y] - dump camera, optional screen ray", c_cam, nullptr,
                      SK_CMD_MAIN_THREAD);
    testview::init(host, self);
}


namespace {
int c_w2s(void*, int, const char* const*, SkOut* out)
{
    void* av = game::avatar();
    if (!av) {
        SAY("no avatar");
        return 1;
    }
    auto p = game::position(av);
    float w[3] = {p.x, p.y, p.z}, sc[2] = {0, 0};
    int ok = H->world_to_screen(w, sc);
    SAY("avatar (%.2f %.2f %.2f) -> ok=%d screen=(%.1f %.1f)\n", p.x, p.y, p.z, ok, sc[0], sc[1]);
    // check with the game's own inverse: ray through that pixel should pass near the avatar
    using namespace skg;
    if (!need_syms(out, {"App::IGameModeManager::Get", "App::cViewer::GetCameraToPoint"}))
        return 1;
    void* mm = cdecl_call<void*>(H->sym("App::IGameModeManager::Get"));
    void* viewer = mm ? vcall<void*>(mm, 0x58 / 4) : nullptr;
    if (!viewer) {
        SAY("no viewer\n");
        return 1;
    }
    Vector3 o{}, d{};
    thiscall<bool>(H->sym("App::cViewer::GetCameraToPoint"), viewer, sc[0], sc[1], &o, &d);
    float vx = p.x - o.x, vy = p.y - o.y, vz = p.z - o.z;
    float t = vx * d.x + vy * d.y + vz * d.z;
    float cx = o.x + d.x * t - p.x, cy = o.y + d.y * t - p.y, cz = o.z + d.z * t - p.z;
    SAY("ray miss distance %.3f m (t=%.1f)\n", std::sqrt(cx * cx + cy * cy + cz * cz), t);
    return 0;
}
} // namespace

namespace {
int c_dna(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && (!strcmp(argv[1], "add") || !strcmp(argv[1], "gain")) &&
        !need_syms(out, {"Simulator::cCreatureGameData::AddEvolutionPoints"}))
        return 1;
    if (argc > 2 && !strcmp(argv[1], "add")) {
        // simulate a gain as the game would report it (goes through the coop hook), exactly N: test
        // setups (not scaled by the party's pace, BUG-0510)
        dna_pace::g_bypass = true;
        skg::cdecl_call<void>(H->sym("Simulator::cCreatureGameData::AddEvolutionPoints"), (float)atof(argv[2]));
        dna_pace::g_bypass = false;
    }
    if (argc > 2 && !strcmp(argv[1], "gain")) // a reward of the game (a kill, a friend): the party's pace applies (BUG-0510)
        skg::cdecl_call<void>(H->sym("Simulator::cCreatureGameData::AddEvolutionPoints"), (float)atof(argv[2]));
    if (argc > 2 && !strcmp(argv[1], "pace")) // tests: the session setting all.dna_pace (scope not checked)
        settings::set(settings::kDnaPace, atoi(argv[2]), true);
    if (argc > 2 && !strcmp(argv[1], "local")) // tests: our total only, as a host total we missed (BUG-0128)
        game::dna_add_raw((float)atof(argv[2]));
    if (argc > 2 && !strcmp(argv[1], "spend")) // tests (BUG-0630): a spend the game's way (its editor's result:
        game::dna_set_raw(game::dna() - (float)atof(argv[2])); // SetEvolutionPoints, the DNA only)
    if (argc > 1 && !strcmp(argv[1], "bar")) { // tests (BUG-0630): also the evolution bar, cPlayer +0x10F0
        SAY("dna=%.1f bar=%.1f", game::dna(), game::evolution_bar());
        return 0;
    }
    SAY("dna=%.1f", game::dna()); // (the plain form: tests parse "dna=<n>" alone)
    return 0;
}
} // namespace

namespace {
typedef bool(SK_THISCALL* LoadGame_t)(void*, const void*);
LoadGame_t o_LoadGame;

std::string str16(const void* s)
{
    const char16_t* b = skg::field<const char16_t*>((void*)s, 0);
    const char16_t* e = skg::field<const char16_t*>((void*)s, 4);
    std::string out;
    if (!b || e < b || e - b > 260 || !skg::readable(b, (e - b) * 2 + 2))
        return "<?>";
    for (; b < e; b++)
        out += (*b < 128) ? (char)*b : '?';
    return out;
}

bool SK_THISCALL h_LoadGame(void* self, const void* params)
{
    H->log(g_self, SK_INFO, "LoadGame name='%s' star='%s' f20='%s'", str16(params).c_str(),
           str16((const uint8_t*)params + 0x10).c_str(), str16((const uint8_t*)params + 0x20).c_str());
    const uint32_t* p = (const uint32_t*)params;
    for (int i = 0; i < 0xB4 / 4; i += 8)
        H->log(g_self, SK_INFO, "  +%02X: %08X %08X %08X %08X %08X %08X %08X %08X", i * 4, p[i], p[i + 1], p[i + 2],
               p[i + 3], p[i + 4], p[i + 5], p[i + 6], p[i + 7]);
    return o_LoadGame(self, params);
}

int c_loadworld(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc < 3) {
        SAY("usage: coop.loadworld <windows slot dir ending with \\> <Planet.spo>");
        return 1;
    }
    auto w = [](const char* s) { std::u16string r; for (; *s; s++) r += (char16_t)(unsigned char)*s; return r; };
    std::u16string dir = w(argv[1]), name = w(argv[2]);
    void* db = skg::cdecl_call<void*>(H->sym("Resource::GetDatabaseByID"), 0x4729A47u);
    if (!db) { SAY("no save DB"); return 1; }
    const char16_t* old = skg::vcall<const char16_t*>(db, 0x28 / 4);
    std::string olds; for (const char16_t* p = old; p && *p; p++) olds += (char)*p;
    int acc = skg::vcall<int>(db, 0x20 / 4);              // GetAccessFlags
    bool closed = skg::vcall<bool>(db, 0x1C / 4);         // Close
    bool ok = skg::vcall<bool>(db, 0x2C / 4, dir.c_str()); // SetLocation
    bool opened = skg::vcall<bool>(db, 0x18 / 4, acc ? acc : 3, 6, 0); // Open(access, CD::Default, autoOpen)
    if (void* sm = skg::cdecl_call<void*>(H->sym("Simulator::cStarManager::Get")))
        skg::thiscall<void>(H->sym("Simulator::cStarManager::ReadStarsFromSaveDB"), sm);
    SAY("access=%d close=%d open=%d\n", acc, closed, opened);
    void* mgr = skg::cdecl_call<void*>(H->sym("Simulator::cGamePersistenceManager::Get"));
    skg::thiscall<void>(H->sym("Simulator::cGamePersistenceManager::LoadGameByName"), mgr, name.c_str());
    SAY("save DB %s -> %s (SetLocation %d), loading %s", olds.c_str(), argv[1], ok, argv[2]);
    return 0;
}

int c_savedb(void*, int, const char* const*, SkOut* out)
{
    if (!need_syms(out, {"Resource::GetDatabaseByID"}))
        return 1;
    void* db = skg::cdecl_call<void*>(H->sym("Resource::GetDatabaseByID"), 0x4729A47u);
    if (!db) { SAY("no save DB"); return 1; }
    const char16_t* loc = skg::vcall<const char16_t*>(db, 0x28 / 4);
    std::string l; for (const char16_t* p = loc; p && *p; p++) l += (char)*p;
    SAY("save DB %p location=%s access=%d refs=%d", db, l.c_str(), skg::vcall<int>(db, 0x20 / 4),
        skg::vcall<int>(db, 0x10 / 4));
    return 0;
}

int c_loadhook(void*, int, const char* const*, SkOut* out)
{
    if (!need_syms(out, {"Simulator::cGamePersistenceManager::LoadGame"}))
        return 1;
    if (!o_LoadGame)
        H->hook_create(g_self, (void*)H->sym("Simulator::cGamePersistenceManager::LoadGame"), (void*)h_LoadGame, (void**)&o_LoadGame);
    SAY("LoadGame logging hook installed");
    return 0;
}
// memfind <hex dword> [max]: every aligned dword in committed read-write memory equal to the value
// (who points at an object: RE of containers at runtime)
int c_memfind(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc < 2) {
        SAY("usage: coop.memfind <hex dword> [max]");
        return 1;
    }
    uint32_t v = (uint32_t)strtoul(argv[1], nullptr, 16);
    int max = argc > 2 ? atoi(argv[2]) : 20, n = 0;
    MEMORY_BASIC_INFORMATION mi;
    for (uint8_t* a = (uint8_t*)0x10000; a < (uint8_t*)0x7FFF0000 && VirtualQuery(a, &mi, sizeof mi) == sizeof mi;
         a = (uint8_t*)mi.BaseAddress + mi.RegionSize) {
        if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)))
            continue;
        uint32_t* p = (uint32_t*)mi.BaseAddress;
        uint32_t* e = (uint32_t*)((uint8_t*)mi.BaseAddress + mi.RegionSize);
        for (; p < e && n < max; p++)
            if (*p == v) {
                SAY("%p\n", (void*)p);
                n++;
            }
        if (n >= max)
            break;
    }
    SAY("%d found", n);
    return 0;
}
} // namespace

void coop_register_debug_commands2(const SkHost* host, SkMod* self)
{
    g_self = self;
    terrain_diag::init(host, self); // BUG-0332
    host->command_add(self, "coop.loadworld", "debug: point the save DB to a slot dir and load a planet", c_loadworld,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.savedb", "debug: current save folder database", c_savedb, nullptr,
                      SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.loadhook", "debug: log cGamePersistenceManager::LoadGame parameters", c_loadhook,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.load", "coop.load <GameN> <Planet> - load a local save (also from the menu)",
                      c_load, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.ground", "coop.ground <x> <y> <z> - terrain radius under a point (tests, BUG-0329)", c_ground,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.abilities", "coop.abilities [play <i> | lift <m> | halt | put <x> <y> <z> | goto <x> <y> <z> | ground <x> <y> <z> | target none|<puppet address> [any] | walk [raw] | walkto <x> <y> <z>] - the own avatar's abilities (tests, BUG-0366)", c_abilities,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.pauses", "coop.pauses - the game's pause reasons counted now (tests, BUG-0313)", c_pauses,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.msglisteners", "coop.msglisteners [hex id] - the game's listeners of a message (RE, BUG-0956; default 0x212D3E7, the mode change)",
                      c_msglisteners, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.menu", "coop.menu - save and leave the stage for the galaxy menu (tests)", c_menu,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.cell", "coop.cell list | pool | spawn <slot> [dx dy [scale]] - cell stage experiments", c_cell,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.introstage", "coop.introstage - the menu intro starts in the first tick of the next stage (tests, BUG-0167)",
                      c_introstage, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.close", "coop.close - WM_CLOSE to the game window: the game's own exit (tests, BUG-0578)", c_close,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.deadmodal", "coop.deadmodal [list] - tests (BUG-0270): the window manager's modal window becomes a destroyed object; list (BUG-0956): a destroyed window listed under a modal one that is popped",
                      c_deadmodal, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.leavewalk", "tests (BUG-0310): the next citizen walked off-stage is a cleaned-up one",
                      c_leavewalk, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.physorphan", "tests (BUG-0449): the next physics update gets a body of kind 0 without an owner",
                      c_physorphan, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.pools", "tests (BUG-0719): the fill of the game's fixed object pools (physics bodies, contacts) and the world's objects",
                      c_pools, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.attackcut", "tests (BUG-0453): the next NPC in its attack animation loses its target (cleared as on the target's destroy)",
                      c_attackcut, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.spotcut", "tests (BUG-0582): the next attacker of a creature attack finds no spot record (a cell without one)",
                      c_spotcut, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.ufoforget", "tests (BUG-0686): the ship model guard forgets the last known ship model (as a Creature game that has none)",
                      c_ufoforget, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.save", "save the game (like the pause menu Save)", c_save, nullptr,
                      SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.dna", "coop.dna [add|gain|local|spend <n> | pace 0|1 | bar] - shared DNA debug (bar: also the evolution bar, cPlayer +0x10F0)", c_dna, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.memfind", "coop.memfind <hex dword> [max] - addresses holding the value (RE)",
                      c_memfind, nullptr, 0);
    host->command_add(self, "coop.w2s", "check world_to_screen against the game's screen ray", c_w2s, nullptr,
                      SK_CMD_MAIN_THREAD);
}
