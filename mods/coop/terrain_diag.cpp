// BUG-0332: the planet terrain's stamps (docs/re/tribe.md#tribe-village-plateaus): who applies them and what the
// terrain holds, on each game. `coop.terrain` lists the stamp vectors of the active planet's terrain and the last
// ApplyStamp / RemoveStamp / ClearAllStamps calls (caller, transform position, returned id).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "game.h"
#include "sporekit.h"
#include "sporekit_game.hpp"

namespace terrain_diag {
namespace {
const SkHost* H;
SkMod* M;

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

uint32_t caller_of(void* ra) { return (uint32_t)((uintptr_t)ra - H->rebase(0) + 0x400000); }

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
    return id;
}

void SK_THISCALL h_Remove(void* self, uint32_t id, int flag)
{
    note(Call{H->time_seconds(), 2, caller_of(__builtin_return_address(0)), 0, 0, 0, 0, id, (void*)(intptr_t)flag});
    o_Remove(self, id, flag);
}

void SK_THISCALL h_Clear(void* self)
{
    note(Call{H->time_seconds(), 3, caller_of(__builtin_return_address(0)), 0, 0, 0, 0, 0, nullptr});
    o_Clear(self);
}

void* terrain()
{
    uintptr_t get = H->sym("Simulator::cPlanetModel::Get");
    void* pm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!pm || !skg::readable((uint8_t*)pm + 0x24, 4))
        return nullptr;
    return *(void**)((uint8_t*)pm + 0x24);
}

int c_terrain(void*, int argc, const char* const* argv, SkOut* out)
{
    void* t = terrain();
    std::string s;
    char b[256];
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
    snprintf(b, sizeof b, "terrain %p hooks %d%d%d\n", t, o_Apply != nullptr, o_Remove != nullptr, o_Clear != nullptr);
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
