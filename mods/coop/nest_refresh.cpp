#include "nest_refresh.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>
#include <windows.h>

#include "fixes.h"
#include "galaxy_client.h"
#include "game.h"
#include "home_nests.h"
#include "net/interp.h"
#include "parties_net.h"
#include "slotfs.h"
#include "social.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "world.h"

namespace nest_refresh {

static const SkHost* H;
static SkMod* M;
using skg::field;

static bool g_on = true;
COOP_FIX("creature.nest_refresh", "BUG-9285", g_on,
         "creature: on a galaxy planet an extinct species comes back after the server's refresh time");
bool revive_on() { return g_on; }

static std::string g_key;                     // the planet's save these times are of
static std::map<uint32_t, int64_t> g_since;   // species key instance -> extinct since (the server's clock)
static double g_next_scan;
static int64_t g_test_shift;                  // tests (`coop.refresh age`): seconds added to the server's clock
static uint32_t g_noted, g_revived, g_herds_revived, g_saves;
static std::string g_last;

static std::wstring path()
{
    return g_key.empty() ? std::wstring() : slotfs::widen(g_key + ".duo-extinct");
}

static void load()
{
    g_since.clear();
    FILE* f = _wfopen(path().c_str(), L"r");
    if (!f)
        return;
    char line[96];
    while (fgets(line, sizeof line, f)) {
        unsigned inst = 0;
        long long t = 0;
        if (sscanf(line, "%x %lld", &inst, &t) == 2 && inst && t > 0)
            g_since[inst] = t;
    }
    fclose(f);
}

static void save()
{
    const std::wstring p = path();
    if (p.empty())
        return;
    if (g_since.empty()) {
        DeleteFileW(p.c_str());
        return;
    }
    const std::wstring tmp = p + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"w");
    if (!f)
        return;
    for (auto& [inst, t] : g_since)
        fprintf(f, "%08X %lld\n", inst, (long long)t);
    const bool ok = fclose(f) == 0;
    if (!ok || !MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return;
    }
    g_saves++;
}

static uint32_t profile_inst(void* sp) // the species' key instance (profile +0x504, nest.cpp)
{
    return sp && !IsBadReadPtr(sp, 0x510) ? field<uint32_t>(sp, 0x504) : 0;
}

struct Scan {
    std::set<void*> players;          // the players' species profiles (their own rule)
    std::map<uint32_t, bool> extinct; // a wild species' instance -> any of its herds extinct
    std::map<uint32_t, float> nearest; // (tests) its nearest herd's home from `at`
    net::Vec3 at{};
};
static bool game_herd(void* o)
{
    return game::object_alive(o) && skg::readable(o, 0x220) && !field<uint8_t>(o, 0x84) && game::herd_species(o);
}

static void scan(Scan& s)
{
    if (void* h = game::avatar_herd())
        s.players.insert(game::herd_species(h));
    if (void* av = game::avatar())
        if (void* h = game::herd_of(av))
            s.players.insert(game::herd_species(h));
    for (unsigned p = 0; p < net::kMaxPlayers; p++)
        if (void* h = home_nests::family_herd((uint8_t)p))
            s.players.insert(game::herd_species(h));
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto& s = *(Scan*)u;
            if (noun != game::kHerdNoun || !game_herd(o) || s.players.count(game::herd_species(o)))
                return true;
            const uint32_t inst = profile_inst(game::herd_species(o));
            if (inst) {
                s.extinct[inst] = s.extinct[inst] || field<uint8_t>(o, 0x190) != 0;
                const float* h = &field<float>(o, 0x34);
                const float d = net::dist({h[0], h[1], h[2]}, s.at);
                auto it = s.nearest.find(inst);
                if (it == s.nearest.end() || d < it->second)
                    s.nearest[inst] = d;
            }
            return true;
        },
        &s);
}

// the game's extinction undone for every herd of the species: +0x190 (cHerd_SetExtinction(false)), its nest's mark
static unsigned revive(uint32_t inst)
{
    static uintptr_t set_flag = H->sym("SporeCoop::cHerd_SetExtinction");
    struct U {
        uint32_t inst;
        uintptr_t set_flag;
        unsigned n;
    } u{inst, set_flag, 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* up) {
            auto& u = *(U*)up;
            if (noun != game::kHerdNoun || !game_herd(o) || profile_inst(game::herd_species(o)) != u.inst)
                return true;
            if (u.set_flag)
                skg::thiscall<void>(u.set_flag, o, false);
            else
                field<uint8_t>(o, 0x190) = 0;
            void* nest = field<void*>(o, 0x160);
            if (nest && !IsBadWritePtr((uint8_t*)nest + 0x120, 1))
                field<uint8_t>(nest, 0x120) = 0;
            u.n++;
            return true;
        },
        &u);
    social::clear_extinct(inst); // the relation record: not set again (the archetype decides), no kills counted
    return u.n;
}

void tick(const net::Session& s, double now)
{
    if (!g_on || s.role() != net::Role::Host || now < g_next_scan)
        return;
    g_next_scan = now + 10;
    const uint32_t every = galaxy::refresh_seconds();
    const int64_t clock = galaxy::server_time();
    if (!every || clock <= 0 || !parties::galaxy() || !game::in_creature_stage() || !game::avatar())
        return;
    const std::string key = world::current_game_key();
    if (key.empty())
        return;
    if (key != g_key) {
        g_key = key;
        load();
    }
    const int64_t t = clock + g_test_shift;
    Scan sc;
    scan(sc);
    bool changed = false;
    for (auto& [inst, ext] : sc.extinct) {
        auto it = g_since.find(inst);
        if (ext && it == g_since.end()) { // died out since the last look
            g_since[inst] = t;
            g_noted++;
            changed = true;
            H->log(M, SK_INFO, "nest refresh: species %08X is extinct; back in %u min (BUG-9285)", inst, every / 60);
        } else if (!ext && it != g_since.end()) { // back another way (a load of an older world, a test)
            g_since.erase(it);
            changed = true;
        }
    }
    for (auto it = g_since.begin(); it != g_since.end();) {
        if (!sc.extinct.count(it->first)) { // no herd of it on the planet any more: nothing to bring back here
            it = g_since.erase(it);
            changed = true;
            continue;
        }
        if (t - it->second >= (int64_t)every) {
            const unsigned n = revive(it->first);
            g_revived++;
            g_herds_revived += n;
            char b[160];
            snprintf(b, sizeof b, "species %08X back after %lld min (%u herds)", it->first, (long long)((t - it->second) / 60), n);
            g_last = b;
            H->log(M, SK_INFO, "nest refresh: %s (BUG-9285)", b);
            it = g_since.erase(it);
            changed = true;
            continue;
        }
        ++it;
    }
    if (changed)
        save();
}

std::string status()
{
    char b[300];
    const int64_t clock = galaxy::server_time();
    snprintf(b, sizeof b,
             "nest refresh (BUG-9285): on=%d every=%u s server_clock=%lld test_shift=%lld extinct=%u noted=%u revived=%u "
             "herds=%u saves=%u last='%s'\n",
             (int)g_on, galaxy::refresh_seconds(), (long long)clock, (long long)g_test_shift, (unsigned)g_since.size(),
             g_noted, g_revived, g_herds_revived, g_saves, g_last.c_str());
    std::string s = b;
    for (auto& [inst, t] : g_since) {
        snprintf(b, sizeof b, "  %08X extinct for %lld s\n", inst, (long long)(clock + g_test_shift - t));
        s += b;
    }
    return s;
}

static int c_refresh(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "age")) { // tests: the clock this many seconds later (the refresh due sooner)
        g_test_shift = atoll(argv[2]);
        g_next_scan = 0;
    } else if (argc > 2 && !strcmp(argv[1], "extinct")) { // tests: species <hex> dies out the game's way (every herd)
        static uintptr_t extinct = H->sym("SporeCoop::cHerd_SetExtinct");
        const uint32_t inst = (uint32_t)strtoul(argv[2], nullptr, 16);
        struct U {
            uint32_t inst;
            uintptr_t f;
            unsigned n;
        } u{inst, extinct, 0};
        if (extinct)
            game::for_each_object(
                [](void* o, uint32_t noun, void* up) {
                    auto& u = *(U*)up;
                    if (noun == game::kHerdNoun && game_herd(o) && profile_inst(game::herd_species(o)) == u.inst) {
                        skg::thiscall<void>(u.f, o);
                        u.n++;
                    }
                    return true;
                },
                &u);
        H->out_printf(out, "%u herds of %08X extinct\n", u.n, inst);
        g_next_scan = 0;
        return 0;
    } else if (argc > 1 && !strcmp(argv[1], "species")) { // tests: the wild species here and whether extinct
        Scan sc;
        if (void* av = game::avatar())
            sc.at = game::position(av);
        scan(sc);
        for (auto& [inst, ext] : sc.extinct)
            H->out_printf(out, "%08X%s nearest %.0f m\n", inst, ext ? " extinct" : "", sc.nearest[inst]);
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    H->command_add(M, "coop.refresh",
                   "coop.refresh [species | extinct <hex> | age <seconds>] - the nest refresh of a galaxy planet (BUG-9285)",
                   c_refresh, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace nest_refresh
