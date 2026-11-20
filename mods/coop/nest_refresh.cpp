#include "nest_refresh.h"

#include <algorithm>
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
static bool g_rotate_on = true;
COOP_FIX("creature.nest_rotate", "BUG-9286", g_rotate_on,
         "creature: on a galaxy planet a player's nest with no player of its species there for the server's rotation "
         "time gets a new species, picked as the game picks a planet's species (decision 7F)");

static std::string g_key;                     // the planet's save these times are of
static std::map<uint32_t, int64_t> g_since;   // species key instance -> extinct since (the server's clock)
static std::map<uint32_t, int64_t> g_left;    // BUG-9286: a player species' nests here -> no player of it since
static double g_next_scan;
static int64_t g_test_shift;                  // tests (`coop.refresh age`): seconds added to the server's clock
static uint32_t g_noted, g_revived, g_herds_revived, g_saves;
static uint32_t g_left_noted, g_rotated, g_herds_rotated, g_pick_failed;
static std::vector<uint32_t> g_rotate_due;    // BUG-9286: species to rotate (outside the frame)
static std::string g_last, g_last_rotation;

static const char* const kExtinct = ".duo-extinct";
static const char* const kLeft = ".duo-left";

// <save>.duo-extinct (BUG-9285) and <save>.duo-left (BUG-9286) next to the planet's save (they travel with the world
// on the server): text lines "<species instance> <server time>"
static std::wstring path(const char* ext)
{
    return g_key.empty() ? std::wstring() : slotfs::widen(g_key + ext);
}

static void load(const char* ext, std::map<uint32_t, int64_t>& m)
{
    m.clear();
    FILE* f = _wfopen(path(ext).c_str(), L"r");
    if (!f)
        return;
    char line[96];
    while (fgets(line, sizeof line, f)) {
        unsigned inst = 0;
        long long t = 0;
        if (sscanf(line, "%x %lld", &inst, &t) == 2 && inst && t > 0)
            m[inst] = t;
    }
    fclose(f);
}

static void save(const char* ext, const std::map<uint32_t, int64_t>& m)
{
    const std::wstring p = path(ext);
    if (p.empty())
        return;
    if (m.empty()) {
        DeleteFileW(p.c_str());
        return;
    }
    const std::wstring tmp = p + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"w");
    if (!f)
        return;
    for (auto& [inst, t] : m)
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

static bool game_herd(void* o)
{
    return game::object_alive(o) && skg::readable(o, 0x220) && !field<uint8_t>(o, 0x84) && game::herd_species(o);
}
// BUG-9286: a nest the game did not pick a species for (no slot in the planet's pick table, herd +0x8C 0): a player's
// (the game's own nests of a player's species and home_nests' nests); wild nests have their slot
static bool player_nest(void* o) { return game::herd_nest(o) && !field<uint32_t>(o, 0x8C); }
constexpr uint32_t kRotatedSlot = 0x7E000000; // the slot of a rotated nest: this | its new species' instance (24 bits)

struct Scan {
    std::set<void*> players;           // the players' species profiles here now (their own rule)
    std::map<uint32_t, bool> extinct;  // a wild species' instance -> any of its herds extinct
    std::set<uint32_t> left;           // BUG-9286: species with a player's nest and no player of them here
    std::map<uint32_t, float> nearest; // (tests) its nearest herd's home from `at`
    net::Vec3 at{};
};

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
            if (!inst)
                return true;
            if (player_nest(o))
                s.left.insert(inst);
            s.extinct[inst] = s.extinct[inst] || field<uint8_t>(o, 0x190) != 0;
            const float* h = &field<float>(o, 0x34);
            const float d = net::dist({h[0], h[1], h[2]}, s.at);
            auto it = s.nearest.find(inst);
            if (it == s.nearest.end() || d < it->second)
                s.nearest[inst] = d;
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

// BUG-9286: the wild nest (a slot and an archetype) nearest to `herd`: its archetype and generation are the planet's
static void* nearest_wild(void* herd)
{
    struct F {
        net::Vec3 at;
        void* best = nullptr;
        float d = 1e30f;
    } f;
    const float* a = &field<float>(herd, 0x34);
    f.at = {a[0], a[1], a[2]};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto& f = *(F*)u;
            if (noun != game::kHerdNoun || !game_herd(o) || player_nest(o) || !game::herd_nest(o) ||
                !field<uint32_t>(o, 0x88))
                return true;
            const float* h = &field<float>(o, 0x34);
            const float d = net::dist({h[0], h[1], h[2]}, f.at);
            if (d < f.d)
                f.best = o, f.d = d;
            return true;
        },
        &f);
    return f.best;
}

// BUG-9286 (decision 7F): every player's nest of species `inst` gets a new species the way the game picks one for a
// planet's herd: PlanetPickSpecies 0xB9A8A0 with the archetype and generation of the nearest wild nest (no slot: a
// fresh pick from the creations the game knows, Sporepedia downloads included). Then as the game's own evolution step
// does (CreatureGame_Update 0xD45E2E): +0xF0, +0xA4, cHerd_ApplyProfile(tuning, 1), the members' +0x1004. No longer
// the party's best friends; no evolution of its own (+0x85 = 0: the chain is the player's).
static unsigned rotate(uint32_t inst)
{
    static uintptr_t pick = H->sym("SporeCoop::PlanetPickSpecies"), forget = H->sym("SporeCoop::PlanetForgetSpeciesPick"),
                     spm = H->sym("Editors::cSpeciesManager::Get"), tuning = H->sym("SporeCoop::cSpeciesManager_GetTuning"),
                     apply = H->sym("SporeCoop::cHerd_ApplyProfile"), friends = H->sym("SporeCoop::cHerd_SetBestFriends"),
                     set_flag = H->sym("SporeCoop::cHerd_SetExtinction");
    if (!pick || !forget || !spm || !tuning || !apply)
        return 0;
    struct U {
        uint32_t inst;
        std::vector<void*> herds;
    } u{inst, {}};
    game::for_each_object(
        [](void* o, uint32_t noun, void* up) {
            auto& u = *(U*)up;
            if (noun == game::kHerdNoun && game_herd(o) && player_nest(o) && profile_inst(game::herd_species(o)) == u.inst)
                u.herds.push_back(o);
            return true;
        },
        &u);
    void* avatar = game::avatar();
    unsigned n = 0;
    for (void* h : u.herds) {
        void* w = nearest_wild(h);
        void* mgr = skg::cdecl_call<void*>(spm);
        if (!w || !mgr) {
            g_pick_failed++;
            continue;
        }
        const uint32_t arch = field<uint32_t>(w, 0x88);
        int gen = field<int32_t>(w, 0xF0);
        const uintptr_t kb = field<uintptr_t>(h, 0xA8), ke = field<uintptr_t>(h, 0xAC);
        const int nk = kb && ke > kb ? (int)((ke - kb) / 12) : 0;
        if (nk > 0) // the herd's own chain (the game's nest of a player's species): the key goes into it (guests follow)
            gen = std::min(gen, nk - 1);
        bool picked_flag = false;
        void* sp = skg::cdecl_call<void*>(pick, 0u, arch, gen, (void*)nullptr, &picked_flag);
        skg::cdecl_call<void>(forget, 0u, arch, gen, (void*)nullptr); // the table entry the pick added (slot 0 is never looked up)
        void* tun = skg::thiscall<void*>(tuning, mgr, arch, gen);
        if (!sp || !tun || profile_inst(sp) == inst || IsBadReadPtr(sp, 0x510)) {
            g_pick_failed++;
            H->log(M, SK_WARN, "nest rotation: no new species for the nest of %08X (arch %08X gen %d) (BUG-9286)", inst,
                   arch, gen);
            continue;
        }
        const uint32_t k0 = field<uint32_t>(sp, 0x504), k1 = field<uint32_t>(sp, 0x508), k2 = field<uint32_t>(sp, 0x50C);
        if (nk > 0) {
            uint32_t* k = (uint32_t*)(kb + gen * 12);
            k[0] = k0, k[1] = k1, k[2] = k2;
        }
        // a slot of its own (the game's slots are small numbers): a wild nest now, not a player's (player_nest), and
        // the game's own re-pick for it (an extinct species' replacement, 0xD45DB9) finds no table entry: a fresh pick
        field<uint32_t>(h, 0x8C) = kRotatedSlot | (k0 & 0xFFFFFF);
        field<uint32_t>(h, 0x88) = arch;
        field<int32_t>(h, 0xF0) = gen;
        field<void*>(h, 0xA4) = sp;
        field<uint32_t>(h, 0x98) = k0, field<uint32_t>(h, 0x9C) = k1, field<uint32_t>(h, 0xA0) = k2;
        field<uint8_t>(h, 0x85) = 0;
        skg::thiscall<void>(apply, h, tun, 1);
        if (friends)
            skg::thiscall<void>(friends, h, false);
        if (field<uint8_t>(h, 0x190)) {
            if (set_flag)
                skg::thiscall<void>(set_flag, h, false);
            void* nest = game::herd_nest(h);
            if (nest && !IsBadWritePtr((uint8_t*)nest + 0x120, 1))
                field<uint8_t>(nest, 0x120) = 0;
        }
        for (uintptr_t m = field<uintptr_t>(h, 0x40), e = field<uintptr_t>(h, 0x44); m && m < e; m += 4) {
            void* c = *(void**)m;
            if (c && c != avatar && skg::readable(c, 0x1010))
                field<uint32_t>(c, 0x1004) = k0, field<uint32_t>(c, 0x1008) = k1, field<uint32_t>(c, 0x100C) = k2;
        }
        const float* at = &field<float>(h, 0x34);
        char b[200];
        snprintf(b, sizeof b, "the nest of %08X at (%.0f %.0f %.0f) is now %08X (arch %08X gen %d, picked %d)", inst, at[0],
                 at[1], at[2], k0, arch, gen, (int)picked_flag);
        g_last_rotation = b;
        H->log(M, SK_INFO, "nest rotation: %s (BUG-9286)", b);
        n++;
    }
    return n;
}

static void rotate_due(void*)
{
    std::vector<uint32_t> due;
    due.swap(g_rotate_due);
    for (uint32_t inst : due) {
        g_rotated++;
        g_herds_rotated += rotate(inst);
    }
}

void tick(const net::Session& s, double now)
{
    if ((!g_on && !g_rotate_on) || s.role() != net::Role::Host || now < g_next_scan)
        return;
    g_next_scan = now + 10;
    const uint32_t every = g_on ? galaxy::refresh_seconds() : 0;
    const uint32_t rot = g_rotate_on ? galaxy::rotate_seconds() : 0;
    const int64_t clock = galaxy::server_time();
    if ((!every && !rot) || clock <= 0 || !parties::galaxy() || !game::in_creature_stage() || !game::avatar())
        return;
    const std::string key = world::current_game_key();
    if (key.empty())
        return;
    if (key != g_key) {
        g_key = key;
        load(kExtinct, g_since);
        load(kLeft, g_left);
    }
    const int64_t t = clock + g_test_shift;
    Scan sc;
    scan(sc);
    if (every) {
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
            save(kExtinct, g_since);
    }
    if (rot) { // BUG-9286: players' nests with no player of their species here
        bool changed = false;
        for (uint32_t inst : sc.left)
            if (!g_left.count(inst)) {
                g_left[inst] = t;
                g_left_noted++;
                changed = true;
                H->log(M, SK_INFO, "nest rotation: no player of %08X here; its nests get a new species in %u h (BUG-9286)",
                       inst, rot / 3600);
            }
        for (auto it = g_left.begin(); it != g_left.end();) {
            if (!sc.left.count(it->first)) { // a player of it is back, or its nests are gone
                it = g_left.erase(it);
                changed = true;
            } else if (t - it->second >= (int64_t)rot) {
                g_rotate_due.push_back(it->first);
                it = g_left.erase(it);
                changed = true;
            } else
                ++it;
        }
        if (!g_rotate_due.empty())
            H->run_outside_frame(rotate_due, nullptr);
        if (changed)
            save(kLeft, g_left);
    }
}

std::string status()
{
    char b[400];
    const int64_t clock = galaxy::server_time();
    snprintf(b, sizeof b,
             "nest refresh (BUG-9285): on=%d every=%u s server_clock=%lld test_shift=%lld extinct=%u noted=%u revived=%u "
             "herds=%u saves=%u last='%s'\n"
             "nest rotation (BUG-9286): on=%d after=%u s left=%u noted=%u rotated=%u herds=%u pick_failed=%u last='%s'\n",
             (int)g_on, galaxy::refresh_seconds(), (long long)clock, (long long)g_test_shift, (unsigned)g_since.size(),
             g_noted, g_revived, g_herds_revived, g_saves, g_last.c_str(), (int)g_rotate_on, galaxy::rotate_seconds(),
             (unsigned)g_left.size(), g_left_noted, g_rotated, g_herds_rotated, g_pick_failed, g_last_rotation.c_str());
    std::string s = b;
    for (auto& [inst, t] : g_since) {
        snprintf(b, sizeof b, "  %08X extinct for %lld s\n", inst, (long long)(clock + g_test_shift - t));
        s += b;
    }
    for (auto& [inst, t] : g_left) {
        snprintf(b, sizeof b, "  %08X no player for %lld s\n", inst, (long long)(clock + g_test_shift - t));
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
    } else if (argc > 1 && !strcmp(argv[1], "herds")) { // tests: every nest herd's species and pick fields
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                if (noun != game::kHerdNoun || !game::object_alive(o) || !skg::readable(o, 0x220) || !game::herd_nest(o))
                    return true;
                const auto chain = (field<uintptr_t>(o, 0xAC) - field<uintptr_t>(o, 0xA8)) / 12;
                const float* h = &field<float>(o, 0x34);
                unsigned members = 0, of_species = 0;
                for (uintptr_t m = field<uintptr_t>(o, 0x40), e = field<uintptr_t>(o, 0x44); m && m < e; m += 4)
                    if (void* c = *(void**)m) {
                        members++;
                        of_species += game::species_profile(c) == game::herd_species(o);
                    }
                H->out_printf((SkOut*)u,
                              "herd %p sp %08X arch %08X slot %08X gen %u chain %u evo %u own %u ext %u bf %u members %u "
                              "of_species %u at %.0f %.0f %.0f\n",
                              o, profile_inst(game::herd_species(o)), field<uint32_t>(o, 0x88), field<uint32_t>(o, 0x8C),
                              field<uint32_t>(o, 0xF0), (unsigned)chain, field<uint8_t>(o, 0x85), field<uint8_t>(o, 0x84),
                              field<uint8_t>(o, 0x190), field<uint8_t>(o, 0x191), members, of_species, h[0], h[1], h[2]);
                return true;
            },
            out);
        return 0;
    } else if (argc > 2 && !strcmp(argv[1], "rotate")) { // tests: the nests of species <hex> rotate now (outside the frame)
        g_rotate_due.push_back((uint32_t)strtoul(argv[2], nullptr, 16));
        H->run_outside_frame(rotate_due, nullptr);
        H->out_printf(out, "queued\n");
        return 0;
    } else if (argc > 1 && !strcmp(argv[1], "species")) { // tests: the wild species here and whether extinct
        Scan sc;
        if (void* av = game::avatar())
            sc.at = game::position(av);
        scan(sc);
        for (auto& [inst, ext] : sc.extinct)
            H->out_printf(out, "%08X%s%s nearest %.0f m\n", inst, ext ? " extinct" : "", sc.left.count(inst) ? " left" : "",
                          sc.nearest[inst]);
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
                   "coop.refresh [species | herds | extinct <hex> | rotate <hex> | age <seconds>] - the nest refresh "
                   "(BUG-9285) and the nest rotation (BUG-9286) of a galaxy planet",
                   c_refresh, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace nest_refresh
