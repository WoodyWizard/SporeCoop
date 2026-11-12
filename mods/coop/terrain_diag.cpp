// BUG-0332: the planet terrain's stamps (docs/re/tribe.md#tribe-terrain-stamps): who applies them and what the
// terrain holds, on each game. `coop.terrain` lists the stamp vectors of the active planet's terrain and the last
// ApplyStamp / RemoveStamp / ClearAllStamps calls (caller, transform position, returned id).
//
// Finding (docs/re/tribe.md#tribe-terrain-stamps): the heights a game renders for stamps it applies live (the ~56 of
// the Tribe start) differ from the ones any load of the same stamp list gives; a rebuild started by the mod
// (`coop.terrain rebuild`, or the planet editor's reload sequence) gives a third result (lab 20261106-043944,
// 20261106-045023), so the fix (tribe.host_reload, world.cpp) is the game's load itself.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "terrain_diag.h"
#include "world.h"

namespace terrain_diag {
namespace {
const SkHost* H;
SkMod* M;
double g_last_stamp; // the time of the last live stamp call (apply, remove, clear)
uint32_t g_live_calls;

struct Call {
    double at;
    uint32_t kind; // 1 apply, 2 remove, 3 clear
    uint32_t caller;
    float x, y, z, scale;
    uint32_t id;
    void* prop;
};
Call g_calls[64];
uint32_t g_n;

void note(const Call& c)
{
    g_calls[g_n++ % 64] = c;
    if (H)
        H->log(M, SK_INFO, "terrain: %s from 0x%08X pos (%.2f %.2f %.2f) |r| %.2f scale %.2f id %u prop %p",
               c.kind == 1 ? "ApplyStamp" : c.kind == 2 ? "RemoveStamp" : "ClearAllStamps", c.caller, c.x, c.y, c.z,
               std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z), c.scale, c.id, c.prop);
}

// the return address as a GOG 2024 VA (rebase(0) is the base's shift from 0x400000). CORRECTION (2026-11-12, BUG-0947):
// it added 0x400000 once more, so every "from 0x0109DFBF / 0x01091981" in docs and logs before is 0x00C9DFBF / 0x00C91981
uint32_t caller_of(void* ra) { return (uint32_t)((uintptr_t)ra - H->rebase(0)); }

using ApplyFn = uint32_t(SK_THISCALL*)(void*, const uint8_t*, void*);
using RemoveFn = void(SK_THISCALL*)(void*, uint32_t, int);
using ClearFn = void(SK_THISCALL*)(void*);
ApplyFn o_Apply;
RemoveFn o_Remove;
ClearFn o_Clear;

uint32_t SK_THISCALL h_Apply(void* self, const uint8_t* xf, void* prop)
{
    uint32_t id = o_Apply(self, xf, prop);
    Call c{H->time_seconds(), 1, caller_of(__builtin_return_address(0)), 0, 0, 0, 0, id, prop};
    if (xf) {
        const float* f = (const float*)(xf + 4);
        c.x = f[0];
        c.y = f[1];
        c.z = f[2];
        c.scale = f[3];
    }
    note(c);
    g_last_stamp = H->time_seconds();
    g_live_calls++;
    return id;
}

void SK_THISCALL h_Remove(void* self, uint32_t id, int flag)
{
    note(Call{H->time_seconds(), 2, caller_of(__builtin_return_address(0)), 0, 0, 0, 0, id, (void*)(intptr_t)flag});
    o_Remove(self, id, flag);
    g_last_stamp = H->time_seconds();
    g_live_calls++;
}

void SK_THISCALL h_Clear(void* self)
{
    note(Call{H->time_seconds(), 3, caller_of(__builtin_return_address(0)), 0, 0, 0, 0, 0, nullptr});
    o_Clear(self);
    g_last_stamp = H->time_seconds();
    g_live_calls++;
}

void* terrain()
{
    uintptr_t get = H->sym("Simulator::cPlanetModel::Get");
    void* pm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!pm || !skg::readable((uint8_t*)pm + 0x24, 4))
        return nullptr;
    return *(void**)((uint8_t*)pm + 0x24);
}

// the terrain manager (SporeCoop::TerrainManager_ptr): +0x1CD set by a fresh planet load (0xB7D960), cleared at teardown
uint8_t* tmgr()
{
    uintptr_t a = H->sym("SporeCoop::TerrainManager_ptr");
    uint8_t* tm = a && skg::readable((void*)a, 4) ? *(uint8_t**)a : nullptr;
    return tm && skg::readable(tm + 0x1C0, 0x10) ? tm : nullptr;
}

int c_terrain(void*, int argc, const char* const* argv, SkOut* out)
{
    void* t = terrain();
    std::string s;
    char b[256];
    // regen [clear]: RE (BUG-0332), [clear: the terrain manager's fresh-load flag +0x1CD to 0, as the planet teardown
    // 0xB7DBDD does], then the stamp version +0x814 one up so the heights are generated again
    // rebuild: RE (BUG-0332), ask the terrain for one height rebuild from its stamp lists the way the game does: its update
    // (vt 0x9C 0xFAAD30) counts sphere+0xA48 down and at 0 writes the stamp lists (vt 0x80) and starts the generator
    // (0xF9B860); only while no generator runs (sphere+0xFC == 0)
    if (argc > 1 && !strcmp(argv[1], "rebuild")) {
        if (!t || !skg::readable((uint8_t*)t + 0xA48, 4)) {
            H->out_printf(out, "no terrain");
            return 1;
        }
        uint32_t gen = *(uint32_t*)((uint8_t*)t + 0xFC);
        if (gen) {
            H->out_printf(out, "a generator runs (+0xFC %08X): not now", gen);
            return 1;
        }
        *(int32_t*)((uint8_t*)t + 0xA48) = 1;
        H->out_printf(out, "rebuild asked (versions %u %u)", *(uint32_t*)((uint8_t*)t + 0x814), *(uint32_t*)((uint8_t*)t + 0x818));
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "regen")) {
        uint8_t* tm = tmgr();
        if (argc > 2 && !strcmp(argv[2], "clear") && tm)
            tm[0x1CD] = 0;
        if (t && skg::readable((uint8_t*)t + 0x814, 8))
            (*(uint32_t*)((uint8_t*)t + 0x814))++;
        H->out_printf(out, "regen: flag 1CD=%d 1CE=%d, versions %u %u", tm ? tm[0x1CD] : -1, tm ? tm[0x1CE] : -1,
                      t ? *(uint32_t*)((uint8_t*)t + 0x814) : 0, t ? *(uint32_t*)((uint8_t*)t + 0x818) : 0);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "state")) { // RE: the terrain's counters round the stamp ones, IsReady (vt 0x50)
        if (!t || !skg::readable((uint8_t*)t + 0x7E0, 0x80)) {
            H->out_printf(out, "no terrain");
            return 1;
        }
        s = "state";
        for (int k = 0x7E0; k < 0x860; k += 4) {
            snprintf(b, sizeof b, " %X=%X", k, *(uint32_t*)((uint8_t*)t + k));
            s += b;
        }
        void* hm = skg::vcall<void*>(t, 0xC / 4);
        uint8_t* tm = tmgr();
        uintptr_t gen = skg::readable((uint8_t*)t + 0xFC, 4) ? *(uintptr_t*)((uint8_t*)t + 0xFC) : 0;
        snprintf(b, sizeof b, " flag1CD=%d flag1CE=%d generator=+0x%X heights=%p ready=%d", tm ? tm[0x1CD] : -1, tm ? tm[0x1CE] : -1,
                 gen ? (unsigned)(gen - (uintptr_t)t) : 0u, hm, (int)skg::vcall<char>(t, 0x50 / 4));
        s += b;
        if (hm && skg::readable((uint8_t*)hm, 0x60)) { // the planet's height parameters (ApplyStamp's fixed radius: +0x34 + +0x38 * +0x3C)
            const float* f = (const float*)hm;
            snprintf(b, sizeof b, " h+20..5C=%.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f", f[8], f[9],
                     f[10], f[11], f[12], f[13], f[14], f[15], f[16], f[17], f[18], f[19], f[20], f[21], f[22], f[23]);
            s += b;
        }
        void* ts = skg::readable((uint8_t*)t + 0x20C, 4) ? *(void**)((uint8_t*)t + 0x20C) : nullptr;
        if (ts && skg::readable((uint8_t*)ts + 0x590, 0x20)) { // the terrain state 0xFA3BC0 scales brushes with (0x3ABC385)
            const float* f = (const float*)((uint8_t*)ts + 0x590);
            snprintf(b, sizeof b, " ts+590..5AC=%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f", f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
            s += b;
        }
        H->out_printf(out, "%s", s.c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "dirty")) { // RE: mark the stamp lists changed (+0x814, +0x818 ++) as ApplyStamp does
        if (!t || !skg::readable((uint8_t*)t + 0x814, 8)) {
            H->out_printf(out, "no terrain");
            return 1;
        }
        uint32_t* d = (uint32_t*)((uint8_t*)t + 0x814);
        d[0]++; // +0x814 the stamp version; +0x818 is the version the heights were generated for
        H->out_printf(out, "terrain %p dirty counters now %u %u", t, d[0], d[1]);
        return 0;
    }
    // reapply <from> [to]: RE (BUG-0332), the stamps of the +0x770 list from index <from> to <to> are taken off and put
    // on again in the list's order with ApplyStamp (their own transform, the prop list of the key at entry+0x98), as a
    // live game applies them; prints the ground at each stamp before and after
    if (argc > 2 && !strcmp(argv[1], "reapply") && o_Apply && o_Remove) {
        uintptr_t pmg = H->sym("App::IPropManager::Get");
        void* pm = pmg ? skg::cdecl_call<void*>(pmg) : nullptr;
        if (!t || !pm || !skg::readable((uint8_t*)t + 0x770, 8)) {
            H->out_printf(out, "no terrain or prop manager");
            return 1;
        }
        size_t from = (size_t)atoi(argv[2]);
        uint8_t* bg = *(uint8_t**)((uint8_t*)t + 0x770);
        uint8_t* en = *(uint8_t**)((uint8_t*)t + 0x774);
        size_t n = (bg && en > bg) ? (size_t)(en - bg) / 0xAC : 0;
        size_t to = argc > 3 ? std::min(n, (size_t)atoi(argv[3]) + 1) : n;
        struct One {
            uint8_t xf[0x38];
            uint32_t key[3], id;
        };
        std::vector<One> v;
        for (size_t i = from; i < to; i++) {
            One o;
            memcpy(o.xf, bg + i * 0xAC, 0x38);
            memcpy(o.key, bg + i * 0xAC + 0x98, 12);
            o.id = *(uint32_t*)(bg + i * 0xAC + 0xA8);
            v.push_back(o);
        }
        for (auto& o : v)
            o_Remove(t, o.id, 1);
        int ok = 0;
        for (auto& o : v) {
            void* pl = nullptr;
            if (!skg::vcall<bool>(pm, 0x2C / 4, o.key[0], o.key[2], &pl) || !pl) {
                snprintf(b, sizeof b, "no prop list %08X/%08X for stamp %u\n", o.key[0], o.key[2], o.id);
                s += b;
                continue;
            }
            const float* f = (const float*)(o.xf + 4);
            net::Vec3 q{f[0], f[1], f[2]};
            uint32_t nid = o_Apply(t, o.xf, pl);
            skg::vcall<int>(pl, 0x04 / 4); // Release
            ok++;
            snprintf(b, sizeof b, "stamp %u -> %u at |r| %.2f: ground %.2f\n", o.id, nid,
                     std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), game::ground_radius(q));
            s += b;
        }
        snprintf(b, sizeof b, "reapplied %d of %u (list %u..%u of %u)\n", ok, (unsigned)v.size(), (unsigned)from, (unsigned)to,
                 (unsigned)n);
        H->out_printf(out, "%s%s", b, s.c_str());
        return 0;
    }
    // stamp <template id> <key instance> <key group>: RE (BUG-0947), ApplyStamp now with the transform of stamp <template
    // id> (its +0x770 entry) and the prop list of that key (the stamp a village's load adds, applied live)
    if (argc > 4 && !strcmp(argv[1], "stamp") && o_Apply) {
        uintptr_t pmg = H->sym("App::IPropManager::Get");
        void* pm = pmg ? skg::cdecl_call<void*>(pmg) : nullptr;
        uint32_t want = (uint32_t)strtoul(argv[2], nullptr, 10);
        uint32_t ki = (uint32_t)strtoul(argv[3], nullptr, 16), kg = (uint32_t)strtoul(argv[4], nullptr, 16);
        uint8_t xf[0x38];
        bool found = false;
        if (t && pm && skg::readable((uint8_t*)t + 0x770, 8)) {
            uint8_t* bg = *(uint8_t**)((uint8_t*)t + 0x770);
            uint8_t* en = *(uint8_t**)((uint8_t*)t + 0x774);
            for (uint8_t* e = bg; bg && e + 0xAC <= en && !found; e += 0xAC)
                if (*(uint32_t*)(e + 0xA8) == want) {
                    memcpy(xf, e, 0x38);
                    found = true;
                }
        }
        void* pl = nullptr;
        if (!found || !skg::vcall<bool>(pm, 0x2C / 4, ki, kg, &pl) || !pl) {
            H->out_printf(out, "no template stamp %u or no prop list %08X/%08X", want, ki, kg);
            return 1;
        }
        uint32_t nid = o_Apply(t, xf, pl);
        skg::vcall<int>(pl, 0x04 / 4);
        H->out_printf(out, "stamped %u", nid);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "raw")) { // RE: the whole 0xAC-byte entry of stamp <id>
        uint32_t want = (uint32_t)strtoul(argv[2], nullptr, 10);
        if (t && skg::readable((uint8_t*)t + 0x770, 0x20))
            for (uint32_t off : {0x770u, 0x784u}) {
                uint8_t* bg = *(uint8_t**)((uint8_t*)t + off);
                uint8_t* en = *(uint8_t**)((uint8_t*)t + off + 4);
                for (uint8_t* e = bg; bg && e + 0xAC <= en; e += 0xAC)
                    if (skg::readable(e, 0xAC) && *(uint32_t*)(e + 0xA8) == want) {
                        for (int k = 0; k < 0xAC; k += 4) {
                            float fv;
                            memcpy(&fv, e + k, 4);
                            snprintf(b, sizeof b, "+%02X %08X %.4g\n", k, *(uint32_t*)(e + k), fv);
                            s += b;
                        }
                    }
            }
        H->out_printf(out, "%s", s.empty() ? "no such stamp" : s.c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "list")) { // one line per stamp: vector, id, transform position, |r|, prop key words
        if (t && skg::readable((uint8_t*)t + 0x770, 0x20))
            for (uint32_t off : {0x770u, 0x784u}) {
                uint8_t* bg = *(uint8_t**)((uint8_t*)t + off);
                uint8_t* en = *(uint8_t**)((uint8_t*)t + off + 4);
                size_t n = (bg && en > bg) ? (size_t)(en - bg) / 0xAC : 0;
                for (size_t i = 0; i < n; i++) {
                    uint8_t* e = bg + i * 0xAC;
                    if (!skg::readable(e, 0xAC))
                        break;
                    const float* f = (const float*)(e + 4);
                    snprintf(b, sizeof b, "s %X %u %u %.2f %.2f %.2f %.2f w %08X %08X %08X %08X %08X %08X\n", off, (unsigned)i,
                             *(uint32_t*)(e + 0xA8), f[0], f[1], f[2], std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]),
                             *(uint32_t*)(e + 0x44), *(uint32_t*)(e + 0x48), *(uint32_t*)(e + 0x4C), *(uint32_t*)(e + 0x50),
                             *(uint32_t*)(e + 0x54), *(uint32_t*)(e + 0x58));
                    s += b;
                }
            }
        H->out_printf(out, "%s", s.c_str());
        return 0;
    }
    snprintf(b, sizeof b, "terrain %p hooks %d%d%d %s\n", t, o_Apply != nullptr, o_Remove != nullptr, o_Clear != nullptr,
             stats().c_str());
    s += b;
    if (t && skg::readable((uint8_t*)t + 0x770, 0xB0)) {
        for (uint32_t off : {0x770u, 0x784u}) {
            uint8_t* bg = *(uint8_t**)((uint8_t*)t + off);
            uint8_t* en = *(uint8_t**)((uint8_t*)t + off + 4);
            size_t n = (bg && en > bg) ? (size_t)(en - bg) / 0xAC : 0;
            snprintf(b, sizeof b, "stamps +0x%X: %u\n", off, (unsigned)n);
            s += b;
            for (size_t i = 0; i < n && i < 40; i++) {
                uint8_t* e = bg + i * 0xAC;
                if (!skg::readable(e, 0xAC))
                    break;
                const float* f = (const float*)e;
                snprintf(b, sizeof b, "  #%u id %u f0-11 %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
                         (unsigned)i, *(uint32_t*)(e + 0xA8), f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9],
                         f[10], f[11]);
                s += b;
                std::string hex = "    raw";
                for (int k = 0; k < 0xA8; k += 4) {
                    snprintf(b, sizeof b, " %08X", *(uint32_t*)(e + k));
                    hex += b;
                }
                s += hex + "\n";
            }
        }
    }
    for (uint32_t i = g_n > 64 ? g_n - 64 : 0; i < g_n; i++) {
        const Call& c = g_calls[i % 64];
        snprintf(b, sizeof b, "call %u t=%.1f %s from 0x%08X pos (%.2f %.2f %.2f) |r| %.2f scale %.2f id %u\n", i, c.at,
                 c.kind == 1 ? "apply" : c.kind == 2 ? "remove" : "clear", c.caller, c.x, c.y, c.z,
                 std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z), c.scale, c.id);
        s += b;
    }
    H->out_printf(out, "%s", s.c_str());
    return 0;
}
} // namespace

std::string stats()
{
    return "live stamp calls=" + std::to_string(g_live_calls) + " host_reloads=" + std::to_string(world::host_reloads());
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    // optional symbols (docs/re/tribe.md#tribe-village-plateaus): log-only hooks, nothing changes
    if (uintptr_t a = H->sym("Terrain::cTerrainSphere::ApplyStamp"))
        H->hook_create(M, (void*)a, (void*)h_Apply, (void**)&o_Apply);
    if (uintptr_t a = H->sym("Terrain::cTerrainSphere::RemoveStamp"))
        H->hook_create(M, (void*)a, (void*)h_Remove, (void**)&o_Remove);
    if (uintptr_t a = H->sym("Terrain::cTerrainSphere::ClearAllStamps"))
        H->hook_create(M, (void*)a, (void*)h_Clear, (void**)&o_Clear);
    H->command_add(M, "coop.terrain", "coop.terrain - the active planet terrain's stamps and the last stamp calls (tests, BUG-0332)",
                   c_terrain, nullptr, SK_CMD_MAIN_THREAD);
}
} // namespace terrain_diag
