// Cell: the Lineages cells' side of the pool (BUG-9275, ported from Lineages BUG-9111; lineage_cells.h decides which
// cells): a native spawn of a creature species may take a player cell's model, the guest's mirrors of such a cell are
// built from its own key of that model, and a player cell's model is loaded for the cell graphics before its first use.
// docs/re/cell.md#cell-lineage-cells-port.
#include "cellsync_internal.h"
#include "call_guard.h"
#include "creations.h"
#include "slotfs.h"
#include "lineage_cells.h"

#include <cmath>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace cellsync {

int g_marker_depth; // > 0 while PopulateMarker runs on the host (h_Populate): the native spawns
CALL_GUARD_DEPTH("cellsync.marker_depth", g_marker_depth); // BUG-1047: a caught fault in PopulateMarker leaves no depth

// BUG-9111 (Lineages): a player cell's model is loaded for the cell graphics before a cell of it is drawn, as the
// editor does for the avatar: PreloadModel (the model data set G+0x70) and cCellGFX::PreloadCreature (0xE64980, cdecl
// void(u32 instance), plain ret: loads {instance, cll, 0x40616200} and keeps it in cCellGFX+0x28).
static uintptr_t a_PreloadModel, a_PreloadCreature;
static std::unordered_set<uint32_t> g_preloaded;
static unsigned g_preloads;
static void preload_cell_model(const uint32_t key[3])
{
    if (!key[0] || !in_cell() || g_preloaded.count(key[0]) || g_preloaded.size() >= 512)
        return;
    g_preloaded.insert(key[0]);
    if (a_PreloadModel)
        skg::cdecl_call<void>(a_PreloadModel, key[0], key[1], key[2]);
    if (a_PreloadCreature && key[2] == 0x40616200u && key[1] == 0x3D97A8E4u)
        skg::cdecl_call<void>(a_PreloadCreature, key[0]);
    g_preloads++;
}

static unsigned g_lin_outside, g_lin_considered, g_lin_swapped, g_lin_own, g_lin_carrier, g_lin_nofit, g_lin_mapped;
static std::unordered_set<uint32_t> g_part_carriers;        // .cell instances whose death dropped a part token
static std::unordered_map<uint32_t, uint8_t> g_native_diet; // model instance -> bit 0 carnivore, bit 1 herbivore
static std::mt19937 g_lin_rng{std::random_device{}()};
static std::wstring carriers_path() { return slotfs::widen(H->mods_dir()) + L"\\coop-cell-part-carriers.txt"; }
static void load_carriers()
{
    FILE* f = _wfopen(carriers_path().c_str(), L"r");
    if (!f)
        return;
    unsigned v;
    while (fscanf(f, "%x", &v) == 1 && g_part_carriers.size() < 4096)
        g_part_carriers.insert(v);
    fclose(f);
}

// host: a loot drop of the dead cell of resource `res` made a part token: that species keeps its native look (its look
// teaches the part, docs/re/cell.md#cell-lineage-cells)
void lineage_loot(void* res, const uint32_t* cells, int n)
{
    if (!res || is_guest())
        return;
    for (int i = 0; i < n; i++) {
        int ct, ai;
        if (!res_types(modes::cell_at(cells[i]), &ct, &ai) || ct != 6)
            continue;
        const uint32_t inst = F<uint32_t>(res, 0);
        if (!inst || !g_part_carriers.insert(inst).second)
            return;
        if (FILE* f = _wfopen(carriers_path().c_str(), L"a")) {
            fprintf(f, "%08X\n", inst);
            fclose(f);
        }
        H->log(M, SK_INFO, "coop: cell: species %08X dropped a part token: it keeps its native look (BUG-9275)", inst);
        return;
    }
}

// the AI type of a .cell's creature (0: not a creature with AI)
static int res_creature(void* ref)
{
    if (!ref || !a_GetData)
        return 0;
    void* holder = nullptr;
    uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, ref, &holder);
    if (holder)
        --F<int>(holder, 0xC);
    if (!res || F<int>(res, 0xB4) != 2)
        return 0;
    int d = S() ? F<int>(S(), 0x7C) : 1;
    uint8_t* ai = res + 0xE0;
    if (d == 0 && F<int>(res, 0x248) != -1)
        ai = res + 0x248;
    else if (d == 2 && F<int>(res, 0x194) != -1)
        ai = res + 0x194;
    return F<int>(ai, 0);
}

// modes.cpp h_GetModelKey: the model a new cell of resource `ref` gets (true: `out` instead of the native key)
static bool choose_model(void* ref, const uint32_t native[3], uint32_t out[3])
{
    if (!g_marker_depth) {
        g_lin_outside++;
        return false;
    }
    if (!native[0] || is_guest() || !in_cell() || !lineage_cells::enabled())
        return false;
    uint8_t* s = S();
    const bool own_species = s && F<uint32_t>(s, 0x10) == native[0]; // the native spread of the player's own species
    if (lineage_cells::current_only()) {
        // BUG-9285: only the player species' spawns (6-8 at the avatar's level, never another species' size) take a
        // partner's current cell: in a partner's tile pass that partner's, in the host's pass any player's
        if (!own_species)
            return false;
        g_lin_considered++;
        if (!lineage_cells::pick_player(tile_pass_player(), out)) {
            g_lin_own++;
            return false;
        }
        g_lin_swapped++;
        return true;
    }
    if (own_species) {
        g_lin_own++;
        return false;
    }
    const int ai = res_creature(ref);
    if (!ai)
        return false;
    if (g_part_carriers.count(F<uint32_t>(ref, 0))) {
        g_lin_carrier++;
        return false;
    }
    g_lin_considered++;
    if (std::uniform_int_distribution<unsigned>(0, 99)(g_lin_rng) >= lineage_cells::share_percent())
        return false;
    // The prey-and-threat AI (0x1006, the common one) picks its prey with the model's own diet (IsPrey -> CanEat reads
    // the model, docs/re/cell.md#cell-lineage-cells): any cell's mouth leads it, a carnivore hunts and a herbivore
    // grazes. Other behaviours keep the native model's diet.
    uint8_t diet = 3;
    if (ai != 0x1006) {
        auto it = g_native_diet.find(native[0]);
        if (it == g_native_diet.end()) {
            uint8_t* md = a_ModelData ? skg::cdecl_call<uint8_t*>(a_ModelData, native[0], native[1], native[2]) : nullptr;
            it = g_native_diet.emplace(native[0], md ? uint8_t((md[0x1250] ? 1 : 0) | (md[0x1251] ? 2 : 0)) : 0).first;
        }
        diet = it->second;
    }
    if (!diet || !lineage_cells::pick(diet & 1, diet & 2, out)) {
        g_lin_nofit++;
        return false;
    }
    g_lin_swapped++;
    return true;
}

void lineage_init()
{
    a_PreloadModel = H->sym("Simulator::Cell::PreloadModel");
    a_PreloadCreature = H->sym("Simulator::Cell::cCellGFX::PreloadCreature");
    load_carriers();
    modes::set_cell_model_chooser(choose_model);
}

void lineage_forget() // the Cell stage was left: its graphics went with it
{
    g_preloaded.clear();
}

// guest: the key a mirror of the host's cell model `key` is built from (a host's Lineages cell: our own key of it)
void lineage_mirror_key(const uint32_t key[3], uint32_t use[3])
{
    use[0] = key[0], use[1] = key[1], use[2] = key[2];
    if (!key[0])
        return;
    const creations::Key l = creations::local_species({key[0], key[1], key[2]});
    if (l.instance == key[0])
        return;
    use[0] = l.instance, use[1] = l.type, use[2] = l.group;
    g_lin_mapped++;
    preload_cell_model(use);
}

bool own_cell_model(uint32_t key[3])
{
    uint8_t* s = S();
    if (!s)
        return false;
    key[0] = F<uint32_t>(s, 0x10), key[1] = F<uint32_t>(s, 0x14), key[2] = F<uint32_t>(s, 0x18);
    return key[0] != 0;
}

bool model_diet(const uint32_t key[3], bool& carnivore, bool& herbivore)
{
    if (!in_cell() || !a_ModelData || !key[0] || !creations::key_known({key[0], key[1], key[2]}))
        return false; // an unknown key faults in GetCellModelData (0xE83EC8, BUG-0455)
    uint8_t* md = skg::cdecl_call<uint8_t*>(a_ModelData, key[0], key[1], key[2]);
    if (!md)
        return false;
    carnivore = md[0x1250] != 0;
    herbivore = md[0x1251] != 0;
    return true;
}

void preload_model(const uint32_t key[3]) { preload_cell_model(key); }

// coop.lineage (tests): the counters, and the live creature cells of the pool by model key (n=<count>)
std::string lineage_debug()
{
    char b[300];
    snprintf(b, sizeof b,
             "cell lineage outside=%u considered=%u swapped=%u own=%u carrier=%u nofit=%u mapped=%u carriers=%u preloads=%u\n",
             g_lin_outside, g_lin_considered, g_lin_swapped, g_lin_own, g_lin_carrier, g_lin_nofit, g_lin_mapped,
             (unsigned)g_part_carriers.size(), g_preloads);
    std::string out = b;
    if (!in_cell() || !avatar())
        return out;
    std::map<uint32_t, int> live;
    uint8_t* av = avatar();
    for_each_cell([&](uint8_t* c, uint32_t) {
        if (c == av || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) || strcmp(kind_of(c), "creature") ||
            F<void*>(c, kQuery) != fg_query())
            return;
        live[F<uint32_t>(c, kModelKey)]++;
    });
    out += "live";
    for (auto& [k, n] : live) {
        snprintf(b, sizeof b, " %08X=%d", k, n);
        out += b;
    }
    out += "\n";
    // the player-model cells (host: a Lineages cell's key; guest: a key of ours of a host's Lineages cell): where they
    // are, their model's diet and what their AI chases now (+0x22C, the chase target) or flees (+0x220)
    unsigned rows = 0;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (c == av || rows >= 40 || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) ||
            F<void*>(c, kQuery) != fg_query())
            return;
        const uint32_t m = F<uint32_t>(c, kModelKey);
        const bool player_model = is_guest() ? creations::host_species({m, 0x3D97A8E4u, 0x40616200u}).instance != m
                                             : lineage_cells::is_candidate(m);
        if (!player_model)
            return;
        rows++;
        uint8_t* md = a_ModelData ? skg::cdecl_call<uint8_t*>(a_ModelData, m, F<uint32_t>(c, kModelKey + 4), F<uint32_t>(c, kModelKey + 8)) : nullptr;
        net::Vec3 p = pos_of(c), me = av ? pos_of(av) : net::Vec3{};
        const uint32_t chase = F<uint32_t>(c, 0x22C), flee = F<uint32_t>(c, 0x220);
        uint8_t* t = chase ? modes::cell_at(chase) : nullptr;
        snprintf(b, sizeof b, "pcell %u model=%08X diet=%s%s d=%.1f dx=%.1f dy=%.1f chase=%u chase_kind=%s flee=%u level=%d\n", idx, m,
                 md && md[0x1250] ? "C" : "-", md && md[0x1251] ? "H" : "-", std::hypot(p.x - me.x, p.y - me.y), p.x - me.x, p.y - me.y, chase,
                 t ? (t == av ? "avatar" : modes::cell_is_puppet(t) ? "puppet" : kind_of(t)) : "none", flee,
                 F<int>(c, 0x358)); // BUG-9285: the cell's level (+0x358, docs/re/cell.md GetScaleDifference)
        out += b;
    });
    uint32_t own[3] = {};
    own_cell_model(own);
    uint8_t* s = S();
    snprintf(b, sizeof b, "own %08X food_level=%d\n", own[0], s ? cell_logic::level_of_food(F<int32_t>(s, 0x1C)) : -1);
    return out + b;
}

} // namespace cellsync
