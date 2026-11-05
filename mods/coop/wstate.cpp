// World state of this game for the divergence detector (census.h, tools/tests/worlddiff.py,
// docs/06-testing.md#testing-detector-state). Read-only: every value is read from the game, never from the
// mod's own bookkeeping, so a host's and a guest's dumps compare what each player's game shows.
//
// `coop.wstate` prints (one line each; parsers ignore what they do not know):
//   wstate v1 role=<host|guest|none> mode=<name> modeid=<hex> world=<hex> t=<s>
//   st tod q=(x y z w) day=<s>             the planet context's orientation (the sky, what MSG_WORLD_TIME sends)
//                                          and cTimeOfDay +0x24 (the day's length, s); every planet stage
//   st player traits=<cell>,<creature>,<tribe>,<civ>,<space> progress=<f> total=<f> social=<f> combat=<f>
//                                          cPlayer (noun manager +0x74): +0x10FC..+0x110C the consequence trait of
//                                          each stage (0 = the stage not done), +0x10F0 / +0x10F4 the stage's
//                                          goal progress bar, +0x1278 / +0x127C the social / combat trait progress
//   st res <name>=<v> ...                  the shared resource of the mode (shared::brief: food / money)
//   st cell food=<n> plant=<n> spent=<n>   Cell: cCellSerializableData +0x1C / +0x20 / +0x2C (the DNA shown is
//                                          DNAForFood(food) - spent, docs/re/cell.md#cell-editor-dna)
//   st dna v=<f>                           Creature: cCreatureGameData::GetEvolutionPoints
//   species <key>/<key>/<key> relation= x= impressed= killed=   Creature: the species relationship table
//   st rel lv=<f>,<f>,<f>,<f>              cRelationshipManager +0x10..+0x1C (the level thresholds, ~)
//   tribe i=<n> player=<0|1> pol=<hex> pos=(x y z) members=<n> food=<f> rel=<f>
//                                          Tribe: every cTribe (food = its bundles +0x20C, rel = how the tribe feels
//                                          about the player tribe, CalculateRelationship)
//   goal tribe=<i|-1> state=<n> pos=(x y z)  Tribe: the tribe mission manager's current mission +0x74, its
//                                          mpAssociatedTribe +0x80 and cMission state +0x84
//   civ pol=<hex> player=<0|1> money=<f> rel=<f> cities=<n>   Civ: every cCivilization (+0x98 wealth)
//   emp pol=<hex> rel=<f> flags=<n>         Space: every NPC empire the player has a relation record with
//   pl <noun> pos=(x y z) pol=<hex>         every placed village / hut / tool / nest / city / building on the planet
//   end wstate rows=<n>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "census.h"
#include "game.h"
#include "modes.h"
#include "shared.h"
#include "social.h"
#include "space_ext.h"
#include "sporekit_game.hpp"

namespace census {

static const SkHost* H;
static const Ctx* WC;

template <typename T>
static T F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }
static bool R(void* p, size_t n) { return p && skg::readable(p, n); }
static uintptr_t S(const char* n) { return H ? H->sym(n) : 0; }

constexpr uint32_t kTribeNoun = 0x018C6D19, kCivNoun = 0x018C816A, kCityNoun = 0x018C43E8;
// the planet's lists the detector compares whole (worlddiff.PLANET)
static const uint32_t kPlanetNouns[] = {
    0x018C6D19, // tribe (a village)
    0x01E4DAAE, // tribe_hut
    0x018C8F0C, // tribe_tool
    0x055CF865, // totem_pole
    0x0629BAFE, // tribe_food_mat
    0x52AA6122, // nest
    0x018C43E8, // city
    0x018EA1EB, // building_city_hall
    0x018EB106, // building_house
    0x018EA2CC, // building_industry
    0x01A56ABA, // building_entertainment
    0x0436F342, // turret
};

static void* noun_mgr()
{
    uintptr_t f = S("Simulator::cGameNounManager::Get");
    return f ? skg::cdecl_call<void*>(f) : nullptr;
}
static void* rel_mgr()
{
    uintptr_t f = S("Simulator::cRelationshipManager::Get");
    return f ? skg::cdecl_call<void*>(f) : nullptr;
}
static float relation(void* mgr, uint32_t a, uint32_t b)
{
    uintptr_t f = S("Simulator::cRelationshipManager::CalculateRelationship");
    return mgr && f && a != 0xFFFFFFFFu && b != 0xFFFFFFFFu ? skg::thiscall<float>(f, mgr, a, b, 0) : NAN;
}
static uint32_t pol(void* o) { return o ? skg::vcall<uint32_t>(o, 0x4C / 4) : 0xFFFFFFFFu; }

// a tribe's food: the bundles of its cGameBundleContainer (+0x20C, vector +4/+8, amount +0x134), as
// cTribe::GetTotalFood sums them (docs/re/tribe.md F)
static float tribe_food(void* t)
{
    if (!R(t, 0x220))
        return NAN;
    uint8_t* cont = (uint8_t*)t + 0x20C;
    void** b = F<void**>(cont, 4);
    void** e = F<void**>(cont, 8);
    if (!b || e < b || e - b > 256 || !R(b, (e - b) * 4 + 1))
        return b == e ? 0.f : NAN;
    float v = 0;
    for (void** it = b; it < e; it++)
        if (*it && R(*it, 0x138))
            v += F<float>(*it, 0x134);
    return v;
}

static int tribe_members(void* t)
{
    void** b = F<void**>(t, 0x340); // cTribe::mTribeMembers
    void** e = F<void**>(t, 0x344);
    if (!b || e < b || e - b > 512 || !R(b, (e - b) * 4 + 1))
        return b == e ? 0 : -1;
    int n = 0;
    for (void** it = b; it < e; it++)
        if (*it && game::object_alive(*it))
            n++;
    return n;
}

static int c_wstate(void*, int, const char* const*, SkOut* out)
{
    uint32_t mode = 0, world = 0;
    if (WC->place)
        WC->place(mode, world);
    bool conn = WC->sess && WC->sess->connected();
    const char* role = !conn ? "none" : WC->sess->role() == net::Role::Host ? "host" : "guest";
    std::string o;
    char b[400];
    int rows = 0;
    auto add = [&](const char* s) {
        o += s;
        rows++;
        if (o.size() > 7000) { // a console answer is built from pieces of at most 8 KB
            H->out_printf(out, "%s", o.c_str());
            o.clear();
        }
    };
    snprintf(b, sizeof b, "wstate v1 role=%s mode=%s modeid=%08X world=%08X t=%.1f\n", role, modes::mode_name(mode), mode,
             world, H->time_seconds());
    o += b;
    bool planet = mode == modes::kCell || mode == modes::kCreature || mode == modes::kTribe || mode == modes::kCiv ||
                  mode == modes::kSpace;
    if (!planet) {
        H->out_printf(out, "%send wstate rows=0\n", o.c_str());
        return 0;
    }
    net::Quat q;
    if (mode != modes::kCell && game::time_of_day(q)) {
        uintptr_t tg = S("Simulator::cTimeOfDay::Get");
        void* tod = tg ? skg::cdecl_call<void*>(tg) : nullptr;
        float day = R(tod, 0x28) ? F<float>(tod, 0x24) : NAN;
        snprintf(b, sizeof b, "st tod q=(%.4f %.4f %.4f %.4f) day=%.1f\n", q.x, q.y, q.z, q.w, day);
        add(b);
    }
    void* nm = noun_mgr();
    void* pl = nm && R(nm, 0x78) ? F<void*>(nm, 0x74) : nullptr; // cPlayer
    if (R(pl, 0x1280)) {
        snprintf(b, sizeof b, "st player traits=%u,%u,%u,%u,%u progress=%.1f total=%.1f social=%.2f combat=%.2f\n",
                 F<uint32_t>(pl, 0x10FC), F<uint32_t>(pl, 0x1100), F<uint32_t>(pl, 0x1104), F<uint32_t>(pl, 0x1108),
                 F<uint32_t>(pl, 0x110C), F<float>(pl, 0x10F0), F<float>(pl, 0x10F4), F<float>(pl, 0x1278),
                 F<float>(pl, 0x127C));
        add(b);
    }
    std::string res = shared::brief();
    if (!res.empty()) {
        snprintf(b, sizeof b, "st res %s\n", res.c_str());
        add(b);
    }
    if (mode == modes::kCell) {
        uintptr_t sp = S("Simulator::Cell::cCellSerializableData::_ptr");
        void* s = sp ? *(void**)sp : nullptr;
        if (R(s, 0x30)) {
            snprintf(b, sizeof b, "st cell food=%d plant=%d spent=%d\n", F<int32_t>(s, 0x1C), F<int32_t>(s, 0x20),
                     F<int32_t>(s, 0x2C));
            add(b);
        }
    }
    if (mode == modes::kCreature) {
        snprintf(b, sizeof b, "st dna v=%.1f\n", game::dna());
        add(b);
        std::string sp = social::species_dump();
        size_t p = 0;
        while (p < sp.size()) {
            size_t e = sp.find('\n', p);
            std::string ln = sp.substr(p, e == std::string::npos ? std::string::npos : e - p + 1);
            if (ln.rfind("species ", 0) == 0)
                add(ln.c_str());
            if (e == std::string::npos)
                break;
            p = e + 1;
        }
    }
    void* mgr = mode == modes::kTribe || mode == modes::kCiv || mode == modes::kSpace ? rel_mgr() : nullptr;
    if (R(mgr, 0x20)) {
        snprintf(b, sizeof b, "st rel lv=%.2f,%.2f,%.2f,%.2f\n", F<float>(mgr, 0x10), F<float>(mgr, 0x14),
                 F<float>(mgr, 0x18), F<float>(mgr, 0x1C));
        add(b);
    }
    // the objects of the stage's tables and the planet's lists, in one pass over the noun list
    struct Q {
        std::vector<void*> tribes, civs;
        std::vector<std::pair<uint32_t, void*>> placed;
        std::map<uint32_t, int> cities; // pol -> cities
    } qq;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (!game::object_alive(o))
                return true;
            if (noun == kTribeNoun)
                q->tribes.push_back(o);
            else if (noun == kCivNoun)
                q->civs.push_back(o);
            if (noun == kCityNoun)
                q->cities[pol(o)]++;
            for (uint32_t n : kPlanetNouns)
                if (n == noun && q->placed.size() < 4000)
                    q->placed.push_back({noun, o});
            return true;
        },
        &qq);
    if (mode == modes::kTribe) {
        void* pt = nm && R(nm, 0x74) ? F<void*>(nm, 0x70) : nullptr; // cGameNounManager::mpPlayerTribe
        uint32_t ppol = pt ? pol(pt) : 0xFFFFFFFFu;
        for (size_t i = 0; i < qq.tribes.size(); i++) {
            void* t = qq.tribes[i];
            float* p = (float*)((uint8_t*)t + 0x124);
            bool mine = t == pt;
            uint32_t tp = pol(t);
            snprintf(b, sizeof b,
                     "tribe i=%u player=%d pol=%08X pos=(%.1f %.1f %.1f) members=%d food=%.1f rel=%.2f\n",
                     (unsigned)i, mine ? 1 : 0, tp, p[0], p[1], p[2], tribe_members(t), tribe_food(t),
                     mine ? NAN : relation(mgr, tp, ppol));
            add(b);
        }
        // the current goal ("ally with or conquer the X tribe"): the tribe mission manager's mission
        uintptr_t mp = S("SporeCoop::TribeMissionManager_ptr");
        void* mm = mp ? *(void**)mp : nullptr;
        void* mis = R(mm, 0x78) ? F<void*>(mm, 0x74) : nullptr;
        if (R(mis, 0x88)) {
            void* at = F<void*>(mis, 0x80);
            int idx = -1;
            for (size_t i = 0; i < qq.tribes.size(); i++)
                if (qq.tribes[i] == at)
                    idx = (int)i;
            float* p = idx >= 0 ? (float*)((uint8_t*)at + 0x124) : nullptr;
            snprintf(b, sizeof b, "goal tribe=%d state=%d pos=(%.1f %.1f %.1f)\n", idx, F<int>(mis, 0x84), p ? p[0] : 0.f,
                     p ? p[1] : 0.f, p ? p[2] : 0.f);
        } else
            snprintf(b, sizeof b, "goal tribe=-1 state=-1 pos=(0 0 0)\n");
        add(b);
    }
    if (mode == modes::kCiv) {
        uintptr_t gf = S("Simulator::cGameNounManager::GetPlayerCivilization");
        void* pc = nm && gf ? skg::thiscall<void*>(gf, nm) : nullptr;
        uint32_t ppol = pc ? pol(pc) : 0xFFFFFFFFu;
        for (void* c : qq.civs) {
            if (!R(c, 0x9C))
                continue;
            uint32_t cp = pol(c);
            snprintf(b, sizeof b, "civ pol=%08X player=%d money=%.0f rel=%.2f cities=%d\n", cp, c == pc ? 1 : 0,
                     F<float>(c, 0x98), c == pc ? NAN : relation(mgr, cp, ppol), qq.cities.count(cp) ? qq.cities[cp] : 0);
            add(b);
        }
    }
    if (mode == modes::kSpace) {
        std::string em = diplo::wstate();
        size_t p = 0;
        while (p < em.size()) {
            size_t e = em.find('\n', p);
            add(em.substr(p, e == std::string::npos ? std::string::npos : e - p + 1).c_str());
            if (e == std::string::npos)
                break;
            p = e + 1;
        }
    }
    for (auto& [noun, obj] : qq.placed) {
        net::Vec3 pos;
        if (!game::object_position(obj, pos) || (std::fabs(pos.x) < 0.01f && std::fabs(pos.y) < 0.01f && std::fabs(pos.z) < 0.01f))
            continue; // not placed (pooled)
        snprintf(b, sizeof b, "pl %08X pos=(%.1f %.1f %.1f) pol=%08X\n", noun, pos.x, pos.y, pos.z, pol(obj));
        add(b);
    }
    snprintf(b, sizeof b, "end wstate rows=%d\n", rows);
    o += b;
    H->out_printf(out, "%s", o.c_str());
    return 0;
}

void wstate_init(const SkHost* host, SkMod* mod, const Ctx* ctx)
{
    H = host;
    WC = ctx;
    H->command_add(mod, "coop.wstate",
                   "coop.wstate - this game's world state for the detector (worlddiff.py): time of day, stages done, "
                   "DNA, food, relations per tribe / species / civ / empire, the current goal, the planet's villages, "
                   "nests, cities and buildings",
                   c_wstate, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace census
