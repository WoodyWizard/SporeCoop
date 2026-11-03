// Census of the shared objects (census.h, docs/06-testing.md#testing-census).
#include "census.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "game.h"
#include "modes.h"
#include "npc.h"

namespace census {

static const SkHost* H;
static SkMod* M;
static Ctx C;

#define SAY(...) H->out_printf(out, __VA_ARGS__)

constexpr uint32_t kCreatureNoun = 0x18EB45Eu; // cCreatureAnimal (docs: SporeCoop::NounCast)

static void creature_rows(std::vector<Row>& rows)
{
    struct U {
        std::vector<Row>* rows;
        void* av;
    } u{&rows, game::avatar()};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            if (noun != kCreatureNoun || !game::object_alive(o) || u->rows->size() >= 2000)
                return true;
            Row r;
            r.kind = 'c';
            r.key = game::species_key(o).instance;
            if (!r.key && o != u->av) // a creature object without a species: not placed in the world
                return true;
            r.alive = game::is_dead(o) ? 0 : 1;
            r.hp = game::health(o);
            r.ai = game::is_puppet(o) ? 0 : 1;
            r.pos = game::position(o);
            r.noun = noun;
            r.serial = game::object_serial(o);
            r.scale = game::scale(o);
            int pid = npc::proxy_id(o), hid = npc::host_id(o);
            if (o == u->av)
                r.own = "avatar";
            else if (pid >= 0) {
                r.own = "proxy";
                r.id = pid;
            } else if (hid >= 0) {
                r.own = "npc";
                r.id = hid;
            } else
                r.own = r.ai ? "local" : "puppet";
            u->rows->push_back(r);
            return true;
        },
        &u);
}

// every placed game object other than the creatures (they have their c rows) within r m of `c`,
// and the count of every noun in the list
struct AllQ {
    std::vector<Row>* rows;
    std::map<uint32_t, int>* nouns;
    std::map<uint32_t, uintptr_t>* vts; // a vtable of each noun (Ghidra address: the class in the exe)
    net::Vec3 c;
    bool have_c;
    float r2;
};

static void all_rows(AllQ& q)
{
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* q = (AllQ*)p;
            if (!game::object_alive(o))
                return true;
            (*q->nouns)[noun]++;
            if (!q->vts->count(noun))
                (*q->vts)[noun] = *(uintptr_t*)o - H->image_base() + 0x400000;
            if (noun == kCreatureNoun || q->rows->size() >= 6000)
                return true;
            // a fruit hanging in a tree (cFruit +0x16C state 0) is made by each game's flora near its own camera:
            // both games hold the same tree masks (cTreeData +0x48) while only one has the fruit objects there
            // (lane 9, 2026-11-03: 23-41 m from any fruit of the other game, the trees' masks equal). Their state
            // is the tree's (worldobj.cpp kind "tree"); only fruit that left the tree are rows (BUG-0472).
            if (noun == 0x02C9CC91 && !IsBadReadPtr(o, 0x170) && *(uint32_t*)((uint8_t*)o + 0x16C) == 0)
                return true;
            net::Vec3 pos;
            if (!game::object_position(o, pos))
                return true;
            if (q->have_c) {
                float dx = pos.x - q->c.x, dy = pos.y - q->c.y, dz = pos.z - q->c.z;
                if (dx * dx + dy * dy + dz * dz > q->r2)
                    return true;
            }
            Row r;
            r.kind = 'g';
            r.pos = pos;
            r.noun = noun;
            r.serial = game::object_serial(o);
            r.scale = game::object_scale(o);
            r.own = "world";
            q->rows->push_back(r);
            return true;
        },
        &q);
}

static int c_census(void*, int argc, const char* const* argv, SkOut* out)
{
    bool all = argc >= 2 && !strcmp(argv[1], "all");
    float radius = argc >= 3 ? (float)atof(argv[2]) : 250.f;
    // `all <r> <x> <y> <z>`: the area around a given point (the detector centres the host's dump on
    // the guest's player, worlddiff.pair_dump), not around the own player
    bool at = argc >= 6;
    net::Vec3 centre{at ? (float)atof(argv[3]) : 0.f, at ? (float)atof(argv[4]) : 0.f, at ? (float)atof(argv[5]) : 0.f};
    uint32_t mode = 0, world = 0;
    if (C.place)
        C.place(mode, world);
    bool conn = C.sess && C.sess->connected();
    const char* role = !conn ? "none" : C.sess->role() == net::Role::Host ? "host" : "guest";
    SAY("census v1 role=%s me=%u mode=%s modeid=%08X world=%08X t=%.1f\n", role, conn ? C.sess->my_id() : 0,
        modes::mode_name(mode), mode, world, H->time_seconds());
    std::vector<Row> rows;
    if (mode == modes::kCreature)
        creature_rows(rows);
    else if (mode == modes::kCiv)
        civ::census_rows(rows);
    std::map<uint32_t, int> nouns;
    std::map<uint32_t, uintptr_t> vts;
    net::Vec3 me{};
    bool have_me = C.self_pos && C.self_pos(me);
    // Tribe / Civ: the own position is the mouse pointer, (0 0 0) when there is none (a game in the
    // background): no radius then (the compare picks its centre, worlddiff.py)
    bool centred = have_me && (std::fabs(me.x) > 0.01f || std::fabs(me.y) > 0.01f || std::fabs(me.z) > 0.01f);
    if (all) {
        if (mode == modes::kCell)
            cellsync::census_rows(rows);
        AllQ q{&rows, &nouns, &vts, at ? centre : me, at || centred, radius > 0 ? radius * radius : 1e30f};
        all_rows(q);
    }
    std::string buf;
    for (auto& r : rows) { // a console answer is built from pieces of at most 8 KB
        char b[256];
        int n = snprintf(b, sizeof b, "obj %c%d key=%08X alive=%d hp=%.1f ai=%d own=%s owner=%d pos=(%.1f %.1f %.1f)", r.kind,
                         r.id, r.key, r.alive, r.hp, r.ai, r.own, r.owner, r.pos.x, r.pos.y, r.pos.z);
        if (all && n > 0 && n < (int)sizeof b - 64)
            snprintf(b + n, sizeof b - n, " noun=%08X ser=%u scale=%.3f\n", r.noun, r.serial, r.scale);
        else if (n > 0 && n < (int)sizeof b - 2)
            strcat(b, "\n");
        if (buf.size() + strlen(b) > 7000) {
            SAY("%s", buf.c_str());
            buf.clear();
        }
        buf += b;
    }
    if (!buf.empty())
        SAY("%s", buf.c_str());
    if (all) {
        SAY("me pos=(%.1f %.1f %.1f) ok=%d\n", me.x, me.y, me.z, have_me ? 1 : 0);
        std::string ln = "nouns";
        for (auto& [k, v] : nouns) {
            char b[48];
            snprintf(b, sizeof b, " %08X=%d@%X", k, v, (unsigned)vts[k]);
            if (ln.size() > 7000) {
                SAY("%s\n", ln.c_str());
                ln = "nouns";
            }
            ln += b;
        }
        SAY("%s\n", ln.c_str());
    }
    SAY("end objects=%u\n", (unsigned)rows.size());
    return 0;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    H->command_add(M, "coop.census",
                   "coop.census [all [r [x y z]]] - the shared objects of this mode as the game has them, with net ids (tests: "
                   "tools/tests/census.py); all: every placed game object within r m and the noun counts (worlddiff.py)",
                   c_census, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace census
