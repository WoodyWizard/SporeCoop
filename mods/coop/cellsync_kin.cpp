// Cell: the wild kin of every player's species (BUG-1257, WP-L1 of docs/plan-0.7.16.md, docs/re/cell.md#cell-kin-species).
//
// The world's kin are the cells of a resource with a kind-5 model entry (the player's cell, S+0x10) that the
// population makes (PopulateMarker 0xE7C500, called by PopulateTile from PopulateTiles and from the level change's
// repopulation in RemoveCellsOfOldScale): 6-8 per level, all the host's cell, and every guest mirrors the host's pool.
// Owner decision (2026-11-18): each player's kin swim near that player, a mix where the players are together. So,
// while the population runs (a depth counter in the PopulateMarker hook) and makes a gameplay cell (CreateCellObject,
// whose position we keep for the call), the game's own answer of GetModelKeyForCellResource that equals S+0x10 is
// replaced by the cell of the player whose view centre is nearest the new cell, when that is a partner, it has a cell
// of its own (species::key(player, Stage::Cell) of the species line, docs/01-architecture.md#arch-species-line: its
// creation as this game has it, not the host's) and the key is known here (a guest still lacking it does not make the mirror
// until the creation arrives: BUG-0455's check in modes.cpp). The guests draw those cells from the key the host's
// state carries (no protocol change). The same runs in a guest's own pool while the host is away (BUG-0205, g_solo):
// the partners there are the puppets in that game.
// "One creature for all" (creature.mode host, species::own_mode) changes nothing, nor does `coop.fix species.line off`.
// Off (`coop.fix cell.kin_species off`): every kin is the host's cell (0.7.15).
#include "cellsync_internal.h"

#include "creations.h"
#include "species_line.h"

namespace cellsync {

static bool g_fix_kin = true;
COOP_FIX("cell.kin_species", "BUG-1257", g_fix_kin,
         "cell: the wild kin near each player are of that player's own cell (a mix where the players are together)");

// BUG-1333: a guest that plays the world's own cell (no cell of its own) follows the host's own-cell change
// (BUG-0508), but its new key reaches the host ~0.5 s after the host's level rebuild has populated the level: the
// rebuild made the kin near that guest of the cell it was leaving, and they stayed (cells of a species nobody plays,
// final check 20261119-214823 cell_own_level10: 4DA56CA5 x5 after the host's change to BAF1AD40). So for
// kFollowWindow s after this game's own cell changed, a partner whose cell is the previous one counts as following
// (its kin are the world's). A guest that chose that cell itself gets its own kin again after the window.
// Off (`coop.fix cell.kin_follow off`): the partner's key as this game has it at once (0.7.16 before this fix).
static bool g_fix_follow = true;
COOP_FIX("cell.kin_follow", "BUG-1333", g_fix_follow,
         "cell: after a player's own cell changes, a partner still on the previous cell (on its way to follow) gets no kin of it");
constexpr double kFollowWindow = 15.0;
static cell_logic::OwnCellFollow g_follow; // this game's own cell (S+0x10) at the last population, the one before

static int g_pop_depth;           // inside PopulateMarker (recursive)
static const float* g_new_pos;    // the position of the gameplay cell the population is making now (CreateCellObject)
static struct {
    uint32_t own = 0, partner = 0, no_key = 0, unknown = 0, one_mode = 0, follow = 0;
    uint32_t per_player[8] = {};
} g_kin;

void kin_pop_enter() { g_pop_depth++; }
void kin_pop_leave() { g_pop_depth--; }

// the creation key of player `id`'s own cell as this game knows it (species::key(id, Stage::Cell): the same key every
// game uses, the one its puppet is built from, known here), {0} when it has none. The host has no line (its cell is
// the world's own): in a guest's own pool its cell is what its puppet is drawn with here.
static creations::Key own_cell_key(uint8_t id)
{
    creations::Key k = species::key(id, species::Stage::Cell);
    if (!k.instance && id == 0 && is_guest())
        if (uint8_t* pc = puppet_cell(id))
            k = {F<uint32_t>(pc, kModelKey), F<uint32_t>(pc, kModelKey + 4), F<uint32_t>(pc, kModelKey + 8)};
    return k;
}

struct KinCentre {
    uint8_t player;
    float x, y;
};

// the players whose kin may swim here and where they are in this game: the host's view centres (itself first), or a
// guest's own cell and the puppets of its partners while it plays its own pool
static size_t kin_centres(KinCentre* out, size_t max)
{
    size_t n = 0;
    if (is_host()) {
        for (auto& c : view_centres().list())
            if (n < max)
                out[n++] = {c.player, c.pos.x, c.pos.y};
        return n;
    }
    if (!is_guest() || !g_solo || !C.sess || !C.puppets)
        return 0;
    if (uint8_t* av = avatar()) {
        net::Vec3 p = pos_of(av);
        out[n++] = {C.sess->my_id(), p.x, p.y};
    }
    for (auto& p : C.puppets())
        if (n < max && p.cell && modes::cell_is_puppet(p.cell)) {
            net::Vec3 q = pos_of((uint8_t*)p.cell);
            if (views::finite_pos(q))
                out[n++] = {(uint8_t)p.id, q.x, q.y};
        }
    return n;
}

static void kin_filter(uint32_t key[3], void*)
{
    if (!g_new_pos || !in_cell())
        return;
    uint8_t* s = S();
    if (!s || key[0] != F<uint32_t>(s, 0x10) || key[1] != F<uint32_t>(s, 0x14) || key[2] != F<uint32_t>(s, 0x18) || !key[0])
        return; // not a kind-5 answer (the player's cell): the level's own species
    g_follow.seen(key, H->time_seconds()); // BUG-1333
    if (!g_fix_kin || !C.sess)
        return;
    if (!species::own_mode()) {
        g_kin.one_mode++;
        return;
    }
    KinCentre cs[views::kMaxCentres + 1];
    size_t n = kin_centres(cs, views::kMaxCentres + 1);
    if (n < 2)
        return;
    uint8_t me = C.sess->my_id(), best = me;
    float bd = 3.0e38f;
    for (size_t i = 0; i < n; i++) {
        float d = std::hypot(g_new_pos[0] - cs[i].x, g_new_pos[1] - cs[i].y);
        if (d < bd || (d == bd && cs[i].player == me)) // (a tie stays ours)
            bd = d, best = cs[i].player;
    }
    if (best == me) {
        g_kin.own++;
        return;
    }
    creations::Key k = own_cell_key(best);
    if (!k.instance) {
        creations::Key c = creations::player_key(best, creations::kCell);
        if (c.instance && !creations::key_known(c))
            g_kin.unknown++; // BUG-0455: a model this game cannot build yet (the line answers it once it can)
        else
            g_kin.no_key++;
        return;
    }
    if (k.instance == key[0] && k.type == key[1] && k.group == key[2]) {
        g_kin.own++;
        return;
    }
    const uint32_t kk[3] = {k.instance, k.type, k.group};
    if (g_fix_follow && g_follow.follows(kk, H->time_seconds(), kFollowWindow)) {
        g_kin.follow++; // BUG-1333: on its way to follow our change
        return;
    }
    if (!creations::key_known(k)) { // BUG-0455: a model this game cannot build yet
        g_kin.unknown++;
        return;
    }
    key[0] = k.instance, key[1] = k.type, key[2] = k.group;
    g_kin.partner++;
    if (best < 8)
        g_kin.per_player[best]++;
}

// CreateCellObject 0xE74A20 (cdecl, 10 dwords: query, Vector3* pos, elevation, res, level, sizeFactor, cellSize,
// applySize, orientation, collision group; returns the index): passed on as raw dwords, the position kept for the
// model filter while the population makes a gameplay cell
using CreateCell_t = uint32_t(SK_CDECL*)(void*, const float*, uint32_t, void*, uint32_t, uint32_t, uint32_t, uint32_t,
                                         void*, uint32_t);
static CreateCell_t o_CreateCell;
static uint32_t SK_CDECL h_CreateCell(void* q, const float* pos, uint32_t elev, void* res, uint32_t level, uint32_t size,
                                      uint32_t csize, uint32_t apply, void* orient, uint32_t group)
{
    if (g_pop_depth <= 0 || !pos || q != fg_query())
        return o_CreateCell(q, pos, elev, res, level, size, csize, apply, orient, group);
    const float* keep = g_new_pos;
    g_new_pos = pos;
    uint32_t r = o_CreateCell(q, pos, elev, res, level, size, csize, apply, orient, group);
    g_new_pos = keep;
    return r;
}

void kin_init()
{
    if (uintptr_t a = H->sym("Simulator::Cell::CreateCellObject"))
        H->hook_create(M, (void*)a, (void*)h_CreateCell, (void**)&o_CreateCell);
    modes::set_cell_model_filter(kin_filter);
}

// the population of this game may make a partner's kin (the model hook must be in before its pass)
void kin_before_pass()
{
    if (g_fix_kin)
        modes::ensure_cell_model_hook();
}

// tests (`coop.pool kin [r]`): the counters, each player's cell as this game knows it and where it is, then every
// live foreground creature within r m of a player whose model is one of the players' cells: its model, whether it is
// a mirror, the nearest player, its resource (the AI's data), the AI's targets (flee +0x220, chase +0x22C), its speed,
// level and swim target, and `key=` the host's pool index (the same cell on every game)
std::string kin_report(float r)
{
    char b[300];
    std::string out;
    // box: this game's visible box of the gameplay layer (Simulator::Cell::ViewBoxes, x0 y0 z0 x1 y1 z1): the tests
    // measure distances in it, since positions do not scale with G+0x514C over many levels (BUG-1257)
    float* vb = view_boxes();
    snprintf(b, sizeof b, "kin fix=%d own=%u partner=%u no_key=%u unknown=%u one_mode=%u follow=%u depth=%d box=%.3f %.3f\n",
             (int)g_fix_kin, g_kin.own, g_kin.partner, g_kin.no_key, g_kin.unknown, g_kin.one_mode, g_kin.follow, g_pop_depth,
             vb ? vb[3] - vb[0] : 0.f, vb ? vb[4] - vb[1] : 0.f);
    out = b;
    if (!in_cell() || !C.sess)
        return out + "not in Cell\n";
    struct P {
        uint8_t id;
        uint32_t model;
        net::Vec3 pos;
    };
    std::vector<P> ps;
    uint8_t me = C.sess->my_id();
    if (uint8_t* av = avatar())
        ps.push_back({me, F<uint32_t>(av, kModelKey), pos_of(av)});
    if (C.puppets)
        for (auto& p : C.puppets())
            if (p.cell && modes::cell_is_puppet(p.cell))
                ps.push_back({(uint8_t)p.id, F<uint32_t>((uint8_t*)p.cell, kModelKey), pos_of((uint8_t*)p.cell)});
    std::set<uint32_t> models;
    for (auto& p : ps) {
        creations::Key k = creations::player_key(p.id, creations::kCell);
        snprintf(b, sizeof b, "kin player %u%s cell=%08X drawn=%08X made=%u pos=(%.3f %.3f) line=%08X\n", p.id,
                 p.id == me ? " (me)" : "", k.instance, p.model, p.id < 8 ? g_kin.per_player[p.id] : 0u, p.pos.x, p.pos.y,
                 species::key(p.id, species::Stage::Cell).instance);
        out += b;
        models.insert(p.model);
        if (k.instance)
            models.insert(k.instance);
    }
    if (uint8_t* s = S())
        models.insert(F<uint32_t>(s, 0x10));
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) || strcmp(kind_of(c), "creature") ||
            !models.count(F<uint32_t>(c, kModelKey)))
            return;
        net::Vec3 q = pos_of(c);
        int who = -1;
        float nd = 3.0e38f;
        for (auto& p : ps) {
            float d = std::hypot(q.x - p.pos.x, q.y - p.pos.y);
            if (d < nd)
                nd = d, who = p.id;
        }
        if (nd > r)
            return;
        auto hk = g_host_of.find(idx); // key: the host's pool index of the cell (a mirror's), the same cell on every game
        uint32_t host_key = hk != g_host_of.end() ? hk->second : idx;
        snprintf(b, sizeof b, "kin cell %08X model=%08X mirror=%d near=%d d=%.3f res=%08X flee=%08X chase=%08X pos=(%.3f %.3f) "
                 "v=%.3f level=%d target=(%.3f %.3f) key=%08X\n",
                 idx, F<uint32_t>(c, kModelKey), g_host_of.count(idx) ? 1 : 0, who, nd,
                 (uint32_t)(uintptr_t)F<void*>(c, kRes), F<uint32_t>(c, kFlee), F<uint32_t>(c, kChase), q.x, q.y,
                 std::hypot(F<float>(c, kVel), F<float>(c, kVel + 4)), F<int>(c, kLevel), F<float>(c, kTarget),
                 F<float>(c, kTarget + 4), host_key);
        out += b;
    });
    return out;
}

} // namespace cellsync
