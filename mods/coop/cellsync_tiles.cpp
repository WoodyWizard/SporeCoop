// Cell: the population of the pool (the guest's own population off, the level change's wipe, the host's tile
// passes around every player, BUG-0174 / BUG-0295, the world scale and the stack tests, the AI's targets and the
// loot drops); moved out of cellsync.cpp (C8 of 0.7.15, BUG-1183).
#include "cellsync_internal.h"

namespace cellsync {

// A level change removes every cell of the old scale (RemoveCellsOfOldScale 0xE7CD10). On the
// guest these are mirrors: the host removes its own cells at its own level change (they made the
// guest send ~600 "eaten" reports per level change before h_Destroy kept untouched mirrors).
Wipe_t o_Wipe;
void SK_CDECL h_Wipe()
{
    if (is_guest() && in_cell())
        g_n.wipes++; // h_Destroy keeps the mirrors (not touched by us): the host's state replaces them
    g_in_wipe = is_host() && in_cell();
    g_n.wipe_calls++;
    o_Wipe();
    g_in_wipe = false;
}

// BUG-1408: a guest's own pool that starts on tiles made while the population was the host's. The tile pass of
// entering Cell (RebuildLevel's PopulateTiles, all 1) runs ~15 ms before solo_tick starts the guest's own pool in a
// Cell world of its own, so h_Populate refused every gameplay marker of the ~40 tiles of the screen; the game populates
// a tile only when it makes it, and those tiles stayed (cell pool 1/4096 for the first 40 s in every own_cell run). The
// own pool then held only the cells of the few tiles the drift of the camera made later at the edge of the active
// range, and lost all of them at once when those tiles left it (the tile cull 0xE7C3E6: 175 -> 1, lab 20261122-193406;
// the host's own pool swings the same way but keeps the cells of its other tiles). Now the tiles whose population
// h_Populate refused are noted (h_PopTile), and when the own pool starts with none of the host's cells (no mirror
// released, or another Cell world) the guest's tile passes populate them, kRefillPerPass at a time below kPopCap.
// Off (`coop.fix cell.solo_refill off`): those tiles stay empty (0.7.16.1).
static bool g_fix_solo_refill = true;
COOP_FIX("cell.solo_refill", "BUG-1408", g_fix_solo_refill,
         "cell: a guest that plays its own Cell pool gets the cells of the tiles it made while it mirrored the host");
constexpr uint32_t kRefillPerPass = 4;
static cell_logic::RefusedTiles g_refused; // guest: tile objects whose gameplay population h_Populate refused
static int g_tile_depth;                   // inside PopulateTile (h_PopTile)
CALL_GUARD_DEPTH("cellsync.tile_depth", g_tile_depth);
static bool g_tile_refused;                // a gameplay marker of the tile being populated was refused
static bool g_refill_due;                  // guest: the own pool started without the host's cells: refill g_refused
static uint32_t g_refilled, g_refill_passes, g_refill_starts;

Populate_t o_Populate;
int SK_CDECL h_Populate(float prob, int layer, int level, void* query, void* bbox, void* marker, int x)
{
    // guests do not spawn their own creatures/food on the gameplay layer: the host's are mirrored
    if (is_guest() && in_cell() && query == fg_query()) {
        if (!g_solo) {
            if (g_tile_depth > 0)
                g_tile_refused = true; // BUG-1408
            return 0;
        }
        g_n.solo_spawns++; // BUG-0205: the host is away, our game fills its own pool
    }
    kin_pop_enter(); // BUG-1257: the population makes these cells (cellsync_kin.cpp)
    int r = o_Populate(prob, layer, level, query, bbox, marker, x);
    kin_pop_leave();
    return r;
}

// ---- BUG-0174: the host keeps the world populated around every player, not only around itself ----
// Spore populates and keeps only the tiles around the local view (PopulateTiles 0xE7D370 with the
// list cCellGame+0x4128 and the view boxes 0x16B3C88, docs/re/cell.md#cell-partner-tiles). In co-op
// the host's game is the one world and a guest's own population is off (h_Populate), so a guest more
// than one screen from the host swam in an empty pool. Now the host runs one more PopulateTiles pass
// per partner, with its own tile list and the view boxes moved onto the partner's cell; a tile two
// players share is populated once (h_PopTile), and a tile / cell inside any player's box is kept
// (h_TileActive). The players are the host's view centres (view_centres.h: the partners noted by coop.cpp,
// the same registry as BUG-0280's Creature / Tribe hook), at most views::kMaxCentres.
// BUG-0292: a cell whose pose is not finite (NaN, inf) is neither sent nor mirrored: a NaN mirror on
// a guest pushed its own cell to NaN through the soft-body contact, its camera and view boxes went
// NaN and PopulateTiles filled its stack with garbage tiles (fault, then a crash, at PopulateTile+0x8A).
// Old (`coop.fix pool.finite_state off`): sent and mirrored as they come.
bool g_fix_finite = true;
COOP_FIX("pool.finite_state", "BUG-0292", g_fix_finite, "cell: a cell with a NaN / infinite pose is neither sent by the host nor mirrored by a guest");
// BUG-0299: a guest's own cell still went NaN with no NaN mirror (4 players, lab 20261030-053709): its
// view boxes followed, PopulateTiles filled the tile pool with garbage (a fault) and the next level-up
// crashed in PopulateTile+0x8A. Now no PopulateTiles / PopulateTile runs while the own view boxes are
// not finite, and the own cell is put back at its last finite position (soft-body particles around
// it, at rest).
// BUG-0975: a fall of several levels at once (a guest at food 327 that joins a host at food 0, the host's
// total taken in one AddFoodPoints) leaves the world scale G+0x514C at the old level's for ~3 s while
// PopulateTiles builds the new level's active set from it: the lowest levels' tiles are tiny and the
// game writes thousands of entries into its stack buffer of 511 (0xC0000005 at 0x00E5FA50, the process
// gone without a crash report). Now no pass runs while the scale is still more than one level above its
// target (or the set would not fit): ~2 s after a fall of three levels; a fall of one level is untouched. Old (`coop.fix cell.tile_cap off`): as before.
static bool g_fix_tile_cap = true;
COOP_FIX("cell.tile_cap", "BUG-0975", g_fix_tile_cap, "cell: no tile pass while the world scale is more than one level above its target after a fall, or while the active set would overflow the game's 511-tile buffer");
double g_tilecap_max;
uintptr_t a_TileSizes;
bool pose_finite(const net::Vec3& p, float vx, float vy, float size)
{
    return views::finite_pos(p) && std::isfinite(vx) && std::isfinite(vy) && std::isfinite(size) && std::fabs(vx) < 1e6f &&
           std::fabs(vy) < 1e6f;
}
// BUG-0295: the game's cell broadphase (the sweep-and-prune step 0xBBB450 / merge 0xBBAF10, called
// from SimUpdate) merges the endpoints in a stack buffer of 4096 entries (2 per cell): a game with
// more than ~2048 live cells overruns it (fault at 0xBBB07B, the stack and heap corrupted, crashes
// later in DestroyCell and on worker threads; docs/re/cell.md#cell-broadphase-cap). With 3-4 players
// the host's partner passes and each guest's 120 m state let the pools grow past 2300 cells. Now:
// a partner's tile is populated only while the host holds fewer than kPopCap (1100) cells (the others wait,
// `g_starved`, and are populated when room comes back; a tile on the partner's own screen up to
// kPopCapScreen), each guest gets at most its kSendCap
// nearest cells, and a guest creates no new mirror at kMirrorCap live cells.
// tests (BUG-0718): `coop.pool budget <pop> <screen>` moves the host's two caps (a budget that misses, as the game's
// own loot and spawns do), `coop.pool budget` alone puts them back
uint32_t kPopCap = 1100, kPopCapScreen = 1300;
int g_tile_level = -1; // the level base of the last PopulateTiles call (tests)
std::unordered_map<uint32_t, uint8_t> g_starved; // tile objects of ours not populated for the budget -> the partner (0xFF: unknown)
uint32_t cells_live() { return G() ? F<uint32_t>(G(), 0x1C + 0xC) : 0; }
static bool over_budget() { return cells_live() >= kPopCap; }
uintptr_t a_PopTiles, a_PopTile, a_TileActive, a_ViewBoxes;
constexpr size_t kTileCap = 2048; // entries of our own lists' fixed buffers (the active set is < 512 tiles)
std::unordered_map<int, PartnerTiles> g_ptiles; // partner id -> its tile list
views::Set g_views;                          // host: itself + every partner (this frame)
static std::vector<std::array<float, 12>> g_view_boxes; // the view boxes of each centre (host first)
static TileList* g_pass_list;   // the list of the pass PopulateTiles is running for us (null: none)
static int g_pass = -1;         // -1 none, 0 the host's pass, k > 0 the pass of centre k
static bool g_union;            // h_TileActive: also true inside another centre's box
static std::unordered_set<uint32_t> g_shadow_tiles; // tile objects not populated (another list has the tile)
// BUG-0999: a host view box whose centre is not a usable position (not finite, or at the 1e7 edge that view_bad lets
// through and views::finite_pos refuses) leaves the view set empty: the partner pass skips then (np = size() - 1
// wrapped to SIZE_MAX and read past both lists). Off: the old unchecked pass (a fault on the host).
static bool g_fix_views_empty = true;
COOP_FIX_UNSAFE("cell.views_empty", "BUG-0999", g_fix_views_empty, "cell: the host's partner tile pass is skipped when its view set is empty (its own view centre not usable)");
int g_test_view_edge; // tests (BUG-0999): the host's view box at the 1e7 edge for this many tile passes
// BUG-1000: the tile bookkeeping of a flush. The between-frames check runs after the cleanup of lists a rebuild
// freed (it logged a false "dead entries" warning after every rebuild and used up the 20 warnings), and a flush of
// every partner list hands a deleted tile's area only to the host's list (before: also to a shadow in another
// partner list, often one the same loop flushes next: cells made for a tile deleted a moment later).
static bool g_fix_flush_handover = true;
COOP_FIX("cell.flush_handover", "BUG-1000", g_fix_flush_handover, "cell: a flush of the partner tile lists hands deleted tiles only to the host's list, and the between-frames tile check runs after the rebuild cleanup");
static bool g_flushing;                                   // tiles_delete runs inside the flush of every partner list
// BUG-1509 (CELL-7 of the external audit of 0.7.15.5, an incomplete fix of BUG-1000): only pop_tiles' flush set
// g_flushing. The world scale step's flush (h_WorldScale, a level change) and partner_tiles_reset(true) deleted every
// partner list without it, so hand_over populated a shadow in the NEXT partner list with a direct PopTile (past
// h_PopTile's checks), and the same loop deleted it at once: BUG-1000's cascade (cells made for nothing, 3-4 players).
// On: every flush of all partner lists marks them flushing. Off (`coop.fix cell.flush_all_lists off`): 0.7.16.1.
static bool g_fix_flush_all_lists = true;
COOP_FIX("cell.flush_all_lists", "BUG-1509", g_fix_flush_all_lists,
         "cell: the level change's flush of the partner tile lists hands no tile to a list it deletes next (cells made for nothing)");
static void flush_lists_begin();
static void flush_lists_end();
static std::unordered_set<TileList*> g_flush_lists;       // the partner lists that flush (this pass)

PopTiles_t o_PopTiles;
PopTile_t o_PopTile;
TileActive_t o_TileActive;

float* view_boxes() { return a_ViewBoxes ? (float*)a_ViewBoxes : nullptr; }
// BUG-0299: the own view boxes hold garbage (the own cell went NaN)
static bool view_bad()
{
    float* vb = view_boxes();
    if (!vb)
        return false;
    for (int i = 0; i < 12; i++)
        if (!std::isfinite(vb[i]) || std::fabs(vb[i]) > 1e7f)
            return true;
    return false;
}

// a tile object index of the pool cCellGame+0x38 is still alive (pool layout: docs/re/cell.md 0)
static bool tile_alive(uint32_t idx)
{
    uint8_t* g = G();
    if (!g)
        return false;
    uint8_t* data = F<uint8_t*>(g, 0x38);
    uint32_t used = F<uint32_t>(g, 0x38 + 0x10), size = F<uint32_t>(g, 0x38 + 0x14), slot = idx & 0xFFFF;
    if (!data || !size || size > 0x1000 || slot >= used)
        return false;
    return F<uint32_t>(data + (size_t)slot * size, 0) == idx;
}

static TileList* host_tiles() { return G() ? (TileList*)(G() + 0x4128) : nullptr; }

// every list but `except` that holds the tile already
// (a tile of ours left empty for the cell budget, BUG-0295, does not count: it has no cells)
// BUG-1510 (CELL-8 of the external audit of 0.7.15.5): a plain shadow (a tile another list held populated, tiles_shared /
// tiles_bg) has no cells either, but only starved ones were left out: hand_over over the budget marks one shadow of a
// key starved and leaves the others, then feed_starved found another list's plain shadow, took the tile for populated
// and dropped the mark: nobody held it populated (no food there until the lists changed; 3-4 players, >= 1100 cells).
// On: no shadow counts as populated, and the starved mark names the partner whose list holds it (feed_starved's
// screen check between the caps found no view for player 0xFF). Off (`coop.fix cell.shadow_empty off`): 0.7.16.1.
bool g_fix_shadow_empty = true;
COOP_FIX("cell.shadow_empty", "BUG-1510", g_fix_shadow_empty,
         "cell: a shared tile whose cells went is populated again by another player's list (it could stay empty, no food)");
unsigned g_shadow_counted; // tests (BUG-1510): a shadow taken for a populated tile (switch off), or left out (on)
static bool tile_elsewhere(int32_t key, TileList* except)
{
    auto has = [&](TileList* l) {
        if (!l || l == except || !cell_logic::tile_list_has(l->begin, l->end, key))
            return false;
        if (g_starved.empty() && (!g_fix_shadow_empty || g_shadow_tiles.empty()))
            return true;
        for (int32_t* e = l->begin; e < l->end; e += 2) {
            if (e[0] != key || g_starved.count((uint32_t)e[1]))
                continue;
            const bool shadow = g_shadow_tiles.count((uint32_t)e[1]) != 0;
            g_shadow_counted += shadow;
            if (cell_logic::tile_entry_populated(false, shadow, g_fix_shadow_empty))
                return true;
        }
        return false;
    };
    if (has(host_tiles()))
        return true;
    for (auto& [id, pt] : g_ptiles)
        if (has(pt.list))
            return true;
    return false;
}

// BUG-0295: in a partner's pass (the view boxes moved onto the partner), is the tile on the partner's
// screen? IsTileActive reads the gameplay range as the visible box doubled, so asked with the
// visible box halved it answers for the visible box itself.
static bool partner_core(const uint32_t* t)
{
    float* vb = view_boxes();
    if (!vb || !o_TileActive)
        return false;
    float save[6];
    memcpy(save, vb, sizeof save);
    for (int i : {0, 1}) {
        float c = (save[i] + save[i + 3]) * 0.5f, h = (save[i + 3] - save[i]) * 0.25f;
        vb[i] = c - h;
        vb[i + 3] = c + h;
    }
    bool r = o_TileActive((const int32_t*)(t + 1));
    memcpy(vb, save, sizeof save);
    return r;
}
// a partner's tile waits: above kPopCap cells unless it is on the partner's screen, above
// kPopCapScreen always (the partner's own screen keeps food while the host's pool is busy)
// The cells of a populated tile show up in the pool's live count only over the next frames, so
// several tiles in one frame overshot the caps (host peaks 1714-1722 with 1250 / 1500, labs
// 20261030-053258, -062717): from kPopCap - 250 on, at most 2 partner tiles per frame.
static int g_frame_tiles;
static bool starve_tile(const uint32_t* t)
{
    uint32_t live = cells_live();
    if (live >= kPopCap - 250 && g_frame_tiles >= 2)
        return true;
    if (live < kPopCap)
        return false;
    if (live < kPopCapScreen && partner_core(t)) {
        g_n.tiles_core++;
        return false;
    }
    return true;
}

void SK_CDECL h_PopTile(uint32_t* t, int all)
{
    if (view_bad()) {
        g_n.nan_pop_skips++;
        return;
    }
    if (t && g_pass >= 0) {
        if (g_pass > 0 && t[1] != 0) { // the host does not need a partner's background
            g_n.tiles_bg++;
            g_shadow_tiles.insert(t[0]);
            return;
        }
        if (tile_elsewhere(cell_logic::tile_key((int32_t)t[1], (int32_t)t[2], (int32_t)t[3], (int32_t)t[4]), g_pass_list)) {
            g_n.tiles_shared++; // its cells are there already: populating it again doubled the food
            g_shadow_tiles.insert(t[0]);
            return;
        }
        // BUG-0295: populated later, when the pool has room. The host's own gameplay tiles too while
        // partners play (its screen like theirs, the ring around it only below kPopCap): at level 4 the
        // host's own doubled box alone held ~1000 cells and the partners' screens got nothing
        // (cell_4p, lab 20261030-064009)
        bool budgeted = g_pass > 0 || (g_pass == 0 && t[1] == 0 && g_views.size() > 1);
        if (budgeted && (size_t)g_pass < g_views.size() && starve_tile(t)) {
            g_n.tiles_starved++;
            g_shadow_tiles.insert(t[0]);
            g_starved[t[0]] = (uint8_t)g_views.list()[(size_t)g_pass].player;
            return;
        }
        if (g_pass > 0) {
            g_n.tiles_partner++;
            g_frame_tiles++;
        }
    } else if (t && !g_shadow_tiles.empty() && g_shadow_tiles.count(t[0]))
        return; // RemoveCellsOfOldScale repopulates every tile object: not the shared ones twice
    if (!t || !is_guest() || !in_cell()) {
        o_PopTile(t, all);
        return;
    }
    // BUG-1408: a guest notes the tiles whose gameplay population was refused (the host's pool is mirrored)
    const bool outer = g_tile_depth == 0, refused_before = g_tile_refused;
    if (outer)
        g_tile_refused = false;
    {
        callguard::Depth d(g_tile_depth);
        o_PopTile(t, all);
    }
    if (outer) {
        if (t[1] == 0) {
            g_refused.on_populated(t[0], g_tile_refused);
            if (g_refused.idx.size() > 1024)
                g_refused.prune(tile_alive);
        }
        g_tile_refused = false;
    } else
        g_tile_refused = g_tile_refused || refused_before;
}

// BUG-1408: the tile object of a pool index (pool G+0x38: docs/re/cell.md 0), null when it is not alive
static uint32_t* tile_obj(uint32_t idx)
{
    if (!tile_alive(idx))
        return nullptr;
    uint8_t* g = G();
    return (uint32_t*)(F<uint8_t*>(g, 0x38) + (size_t)(idx & 0xFFFF) * F<uint32_t>(g, 0x38 + 0x14));
}

void solo_refill_start(uint32_t released, bool elsewhere)
{
    g_refill_due = cell_logic::solo_refill_due(g_fix_solo_refill, released, elsewhere) && !g_refused.idx.empty();
    if (g_refill_due)
        g_refill_starts++;
}
void solo_refill_stop() { g_refill_due = false; }
std::string solo_refill_status()
{
    char b[120];
    snprintf(b, sizeof b, " solo_refill=%u/%u due=%d refused=%u fix=%d", g_refilled, g_refill_starts, (int)g_refill_due,
             (unsigned)g_refused.idx.size(), (int)g_fix_solo_refill);
    return b;
}

// a guest's tile pass while it plays its own pool: kRefillPerPass of the refused tiles of its list populated now
static void solo_refill(TileList* list)
{
    if (!g_refill_due || !g_solo || !is_guest() || !list || scale_moving())
        return;
    g_refused.prune(tile_alive);
    std::vector<uint32_t> todo;
    for (int32_t* e = list->begin; e < list->end; e += 2)
        if (g_refused.has((uint32_t)e[1]))
            todo.push_back((uint32_t)e[1]);
    uint32_t n = cell_logic::solo_refill_take((uint32_t)todo.size(), kRefillPerPass, cells_live(), kPopCap);
    for (uint32_t i = 0; i < n; i++)
        if (uint32_t* t = tile_obj(todo[i]))
            if (t[1] == 0) {
                h_PopTile(t, 0);
                g_refilled++;
            }
    g_refill_passes++;
    if (todo.size() <= n) {
        g_refill_due = false;
        trace::line("cell", "own pool: %u tiles made while the population was the host's populated now in %u passes; cell "
                            "pool %u (BUG-1408)", g_refilled, g_refill_passes, cells_live());
    }
}

static int32_t g_last_tile[4]; // tests (BUG-1064): the last tile asked outside a pass (the cull's, right before its DestroyCell)
bool SK_CDECL h_TileActive(const int32_t* t)
{
    if (t && g_pass < 0)
        memcpy(g_last_tile, t, sizeof g_last_tile);
    // inside a partner pass the only caller is PopulateTiles' delete loop, whose erase would shorten
    // the host's list (tiles_delete did this list's deletes already): it deletes nothing of ours
    if (g_pass > 0)
        return true;
    bool r = o_TileActive(t);
    float* vb = view_boxes();
    if (r || !g_union || !t || t[0] != 0 || !vb || g_view_boxes.size() < 2)
        return r;
    float save[12];
    memcpy(save, vb, sizeof save);
    for (auto& b : g_view_boxes) {
        if (!memcmp(b.data(), save, sizeof save))
            continue; // the box it was just asked with
        memcpy(vb, b.data(), sizeof save);
        if ((r = o_TileActive(t)))
            break;
    }
    memcpy(vb, save, sizeof save);
    if (r)
        g_n.tiles_kept++;
    return r;
}

void area_cull_note(uint8_t area, uint8_t* c)
{
    float* vb = view_boxes();
    uint8_t* s = S();
    uint8_t* pc = area ? puppet_cell(area) : avatar();
    if (area >= 4 || !vb || !pc || !s)
        return;
    AreaCull& st = g_area_cull[area];
    st.n++;
    uint32_t age = g_sim_frame - g_boxes_frame;
    st.stale += age != 0;
    st.maxage = std::max(st.maxage, age);
    for (size_t k = 0; k < g_views.size() && k < g_view_boxes.size(); k++)
        if (g_views.list()[k].player == area) {
            auto& b = g_view_boxes[k];
            float off = std::hypot((b[0] + b[3]) * 0.5f - pos_of(pc).x, (b[1] + b[4]) * 0.5f - pos_of(pc).y);
            if (std::isfinite(off))
                st.maxoff = std::max(st.maxoff, off);
        }
    float hx = (vb[3] - vb[0]) * 0.5f, hy = (vb[4] - vb[1]) * 0.5f;
    if (hx > 0 && hy > 0)
        st.maxrel = std::max(st.maxrel, std::max(std::fabs(pos_of(c).x - pos_of(pc).x) / hx, std::fabs(pos_of(c).y - pos_of(pc).y) / hy));
    int d = g_last_tile[1] - cell_logic::level_of_food(F<int32_t>(s, 0x1C));
    st.lvl[std::clamp(d + 2, 0, 7)]++;
    if (o_TileActive) { // would the tile be active with the host's box this frame moved onto the area's cell?
        float save[12];
        memcpy(save, vb, sizeof save);
        float dx = pos_of(pc).x - (save[0] + save[3]) * 0.5f, dy = pos_of(pc).y - (save[1] + save[4]) * 0.5f;
        for (int i : {0, 3})
            vb[i] += dx;
        for (int i : {1, 4})
            vb[i] += dy;
        bool fresh = o_TileActive(g_last_tile), boxed = false;
        st.fresh_ok += fresh;
        // the area's own box of g_view_boxes, as h_TileActive's union reads it
        float bc[4] = {};
        for (size_t k = 0; k < g_views.size() && k < g_view_boxes.size(); k++)
            if (g_views.list()[k].player == area) {
                memcpy(vb, g_view_boxes[k].data(), sizeof save);
                boxed = o_TileActive(g_last_tile);
                st.box_ok += boxed;
                bc[0] = (vb[0] + vb[3]) * 0.5f, bc[1] = (vb[1] + vb[4]) * 0.5f, bc[2] = vb[3] - vb[0], bc[3] = vb[4] - vb[1];
            }
        memcpy(vb, save, sizeof save);
        if (st.n <= 4)
            H->log(M, SK_INFO, "coop: areas: cull area %d cell (%.2f %.2f) its area's cell (%.2f %.2f) box %s(%.2f %.2f %.2fx%.2f) host vb (%.2f %.2f "
                               "%.2fx%.2f) tile %d/%d/%d/%d food %d scale %.4f -> %.4f frame %u: fresh %d box %d",
                   area, pos_of(c).x, pos_of(c).y, pos_of(pc).x, pos_of(pc).y, "", bc[0], bc[1], bc[2], bc[3], (save[0] + save[3]) * 0.5f,
                   (save[1] + save[4]) * 0.5f, save[3] - save[0], save[4] - save[1], g_last_tile[0], g_last_tile[1], g_last_tile[2], g_last_tile[3],
                   F<int32_t>(s, 0x1C), F<float>(G(), 0x514C), F<float>(G(), 0x5150), g_sim_frame, fresh, boxed);
        st.union_ok += h_TileActive(g_last_tile);
    }
    st.layer1 += g_last_tile[0] != 0;
}

// Delete the tiles of one of our lists that `keep` refuses, as PopulateTiles' own delete loop does
// (the tile's +0x300 object released, the pool G+0x38 slot freed). Never through the game's loop:
// its erase is inlined with the host's list end (`cCellGame+0x412C`), so a tile it deleted from our
// list shortened the HOST's list and read past ours (crash at PopulateTiles+0xC3, lab
// 20261030-001703, BUG-0174).
static std::vector<uint32_t> g_freed_dbg;
// A tile of ours that goes while another list holds the same key unpopulated (a shadow: its cells
// were ours): that tile is populated now, or the area lost its food. Seen after a level change: our
// tiles went back to the pool and the host's shadowed tiles around its own cell stayed empty
// (RemoveCellsOfOldScale skips shadows; cell_new_game: the host found no meat on its screen, lab
// 20261030-022912).
static void hand_over(int32_t key, TileList* from)
{
    uint8_t* g = G();
    if (!g || g_shadow_tiles.empty())
        return;
    uint8_t* data = F<uint8_t*>(g, 0x38);
    uint32_t size = F<uint32_t>(g, 0x38 + 0x14);
    auto try_list = [&](TileList* l, int player) {
        if (!l || l == from)
            return false;
        for (int32_t* e = l->begin; e < l->end; e += 2)
            if (e[0] == key && g_shadow_tiles.count((uint32_t)e[1]) && tile_alive((uint32_t)e[1])) {
                uint32_t* t = (uint32_t*)(data + (size_t)((uint32_t)e[1] & 0xFFFF) * size);
                if (l != host_tiles() && over_budget()) { // BUG-0295: a partner's area waits for room
                    if (g_flush_lists.count(l)) // (BUG-1509: also a flush without g_flushing, counted for its twin)
                        g_n.handed_flushed++; // BUG-1000: waits in a list this pass deletes
                    g_starved.emplace((uint32_t)e[1], (uint8_t)(g_fix_shadow_empty ? player : 0xFF)); // BUG-1510
                    return true;
                }
                g_shadow_tiles.erase((uint32_t)e[1]);
                g_starved.erase((uint32_t)e[1]);
                if (t[1] == 0) { // gameplay layer only (a partner's background was never ours)
                    int pass = g_pass;
                    g_pass = -1;
                    o_PopTile(t, 0);
                    g_pass = pass;
                    g_n.tiles_handed++;
                }
                return true;
            }
        return false;
    };
    if (try_list(host_tiles(), 0))
        return;
    if (g_flushing && g_fix_flush_handover) // BUG-1000: every partner list goes in this pass
        return;
    for (auto& [id, pt] : g_ptiles)
        if (try_list(pt.list, id)) {
            if (g_flush_lists.count(pt.list)) // (BUG-1509: g_flush_lists is empty outside a flush)
                g_n.handed_flushed++;
            return;
        }
}

template <typename Keep>
static void tiles_delete(TileList* l, Keep keep)
{
    uint8_t* g = G();
    if (!l || !g)
        return;
    uint8_t* pool = g + 0x38;
    uint8_t* data = F<uint8_t*>(pool, 0);
    uint32_t size = F<uint32_t>(pool, 0x14);
    int32_t* out = l->begin;
    for (int32_t* e = l->begin; e < l->end; e += 2) {
        uint32_t idx = (uint32_t)e[1];
        uint8_t* t = tile_alive(idx) ? data + (size_t)(idx & 0xFFFF) * size : nullptr;
        if (t && (F<int32_t>(t, 8) == -1 || keep((const int32_t*)(t + 4)))) {
            out[0] = e[0];
            out[1] = e[1];
            out += 2;
            continue;
        }
        if (t) {
            if (void* o = F<void*>(t, 0x300)) {
                void** vt = *(void***)o;
                skg::thiscall<void>((uintptr_t)vt[3], o, 1);
                if ((o = F<void*>(t, 0x300))) {
                    F<void*>(t, 0x300) = nullptr;
                    skg::thiscall<void>((uintptr_t)(*(void***)o)[1], o);
                }
            }
            bool was_shadow = g_shadow_tiles.count(idx) != 0;
            // cObjectPool_::DeleteObject 0xB72370: the slot goes to the head of the free list
            F<uint32_t>(t, 0) = F<uint32_t>(pool, 4);
            F<uint32_t>(pool, 0xC)--;
            F<uint32_t>(pool, 4) = idx & 0xFFFF;
            g_shadow_tiles.erase(idx);
            g_starved.erase(idx);
            g_n.tiles_drained++;
            if (g_freed_dbg.size() < 6)
                g_freed_dbg.push_back(idx);
            if (!was_shadow)
                hand_over(e[0], l);
        }
    }
    l->end = out;
}

// our list forgets its entries without deleting them (the pool they were in is gone)
static void tiles_forget(PartnerTiles& pt)
{
    if (pt.list) {
        pt.list->begin = pt.list->end = pt.list->fixed = pt.buf;
        pt.list->cap = pt.buf + kTileCap * 2;
    }
}

static void tiles_free(PartnerTiles& pt)
{
    delete pt.list; // a buffer the game grew it into (more than kTileCap tiles) is left to the game's heap
    delete[] pt.buf;
    pt.list = nullptr;
    pt.buf = nullptr;
}

// every tile of the list goes (the partner left)
static void tiles_drain(int, PartnerTiles& pt)
{
    if (pt.list)
        tiles_delete(pt.list, [](const int32_t*) { return false; });
}

// The world scale step 0xE73E50 (cdecl void(float dt), early in SimUpdate): while the scale G+0x514C
// moves towards G+0x5150 (a level change, over several frames) it rescales the world through
// 0xE6DB70, which empties the host's tile list and fills it again from EVERY tile object of the pool,
// one per key. Our tiles then sat in both lists (both freed them: faults at CreateObject+0x37 after
// `coop.pool food 1000`, lab 20261030-004900) or replaced the host's own tile of the same key (that
// one leaked). So our tiles go back to the pool before such a step, and no partner pass runs until
// the scale settles (h_PopTiles).
// BUG-1066: the food of a partner or of `coop.pool food` comes in food_drain (BUG-0633), right before the game's
// step: its AddFoodPoints sets the target scale and this very step rescales, with our tiles still in the pool (the
// flush looked at the scale before the drain). The rescale put our tile objects into the host's list (one per key,
// the last wins: our shadow of a shared key replaced the host's own populated tile), h_PopTiles took them out again
// and the host's own tiles of those keys stayed live in no list (15-16 a change). Now the flush runs after the drain.
// Old (`coop.fix cell.scale_food_flush off`): before it.
static bool g_fix_scale_food_flush = true;
COOP_FIX("cell.scale_food_flush", "BUG-1066", g_fix_scale_food_flush, "cell: the host gives its partner tiles back before the world rescale that a partner's food starts (its own tiles were left in no list)");
WorldScale_t o_WorldScale;
uintptr_t a_WorldScale;
bool scale_moving()
{
    uint8_t* g = G();
    return g && F<float>(g, 0x514C) != F<float>(g, 0x5150);
}
// tests (BUG-0307): `coop.pool stack <frames>`: the nearest cell is put on our own cell's centre at the start
// of <frames> cell steps (two cells less than 0.004 apart: the game's collision leaves the contact normal unset)

// the world centre of a cell's first collision sphere (0xE6AD80: model data +4 + 0x48 i, offset turned by
// +0x5C when +0x48 & 2, times the size +0x58, plus +0x4C); its own position without model data
net::Vec3 first_sphere(uint8_t* c)
{
    net::Vec3 p = pos_of(c);
    auto* k = (uint32_t*)(c + kModelKey);
    uint8_t* md = a_ModelData && k[0] && F<uint32_t>(c, 0x248) ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
    if (!md)
        return p;
    int n = F<int>(md, 0);
    for (int i = 0; i < n && i < 64; i++) {
        auto* e = (float*)(md + 4 + 0x48 * (size_t)i);
        if (((uint32_t*)e)[0] == 1)
            continue;
        float x = e[1], y = e[2], z = e[3];
        if (F<uint8_t>(c, 0x48) & 2) {
            auto* m = (float*)(c + 0x5C);
            float rx = m[3] * y + m[6] * z + m[0] * x, ry = m[4] * y + m[7] * z + m[1] * x, rz = m[5] * y + m[8] * z + m[2] * x;
            x = rx, y = ry, z = rz;
        }
        float sz = F<float>(c, kSize);
        return {p.x + sz * x, p.y + sz * y, p.z + sz * z};
    }
    return p;
}
// right before the collision pair of our cell and the stacked one: the stacked one's first sphere onto ours
void stack_pair(uint8_t* av, uint8_t* other)
{
    if (g_stack_frames <= 0 || !other || F<uint32_t>(other, 0) != g_stack_idx)
        return;
    net::Vec3 w = first_sphere(av), o = first_sphere(other), p = pos_of(other);
    modes::cell_move(other, {p.x + w.x - o.x, p.y + w.y - o.y, p.z + w.z - o.z}, true);
    g_n.stack_pairs++;
}
// tests (BUG-0307): `coop.pool stack2 <frames>`: two cells without a soft body (outside the active area)
// on one centre at the start of <frames> cell steps: the game's sphere-sphere collision 0xE73BB0 then
// gives the contact the first cell's centre as its normal
static void stack2_tick()
{
    if (g_stack2_frames <= 0)
        return;
    uint8_t* a = modes::cell_at(g_stack2_a);
    if (!a) {
        g_stack2_frames = 0;
        return;
    }
    net::Vec3 p = pos_of(a);
    bool last = --g_stack2_frames <= 0;
    for (uint32_t idx : g_stack2)
        if (uint8_t* c = modes::cell_at(idx)) {
            g_stack2_nf |= cell_nonfinite(c);
            float v = std::hypot(F<float>(c, kVel), F<float>(c, kVel + 4));
            if (!std::isfinite(v) || v > g_stack2_vmax)
                g_stack2_vmax = std::isfinite(v) ? v : INFINITY;
            if (!last && views::finite_pos(p))
                stop_at(c, p);
        }
}
static void stack_tick()
{
    stack2_tick();
    uint8_t* av = g_stack_frames > 0 ? avatar() : nullptr;
    uint8_t* c = av ? modes::cell_at(g_stack_idx) : nullptr;
    if (!c || c == av) {
        g_stack_frames = 0;
        return;
    }
    g_stack_frames--;
    stop_at(av, pos_of(av));
    stop_at(c, pos_of(av));
}
static bool g_fix_rescale_origin = true;
COOP_FIX("cell.rescale_origin", "BUG-1394", g_fix_rescale_origin,
         "cell: keep WorldScaleStep recentering at (0,0,0) when a connected peer's camera is >1000 m from origin");

void SK_CDECL h_WorldScale(float dt)
{
    g_last_dt = dt;
    g_sim_frame++;
    own_probe("step start");
    stack_tick();
    auto flush = [] {
        if (!g_ptiles.empty() && in_cell() && scale_moving()) {
            flush_lists_begin(); // BUG-1509
            for (auto& [id, pt] : g_ptiles)
                if (pt.list && pt.list->begin != pt.list->end) {
                    g_n.tiles_flush++;
                    g_n.tiles_scale_flush++;
                    tiles_delete(pt.list, [](const int32_t*) { return false; });
                }
            flush_lists_end();
        }
    };
    if (!g_fix_scale_food_flush)
        flush();
    food_drain(false); // BUG-0633: the food from outside the step, as a meal of this step
    if (g_fix_scale_food_flush) // BUG-1066: after the drain, whose level change starts the scale moving
        flush();
    puppet_poison(dt); // BUG-0716: the partners' poison parts, as the avatar's in this step
    // BUG-1394: WorldScaleStep (0x00E73E50) passes cCellUI+0x48 to Cell_RescaleAbout (0x00E6DB70), which recentered
    // the entire world whenever |cam|^2 > 1e6. In co-op, hold cCellUI+0x48 at (0,0,0) across the call so all peers
    // rescale around the shared origin, then restore cCellUI+0x48 scaled by old_scale / new_scale.
    uint8_t* g = G();
    uint8_t* ui = (a_CellUI && g && in_cell()) ? *(uint8_t**)a_CellUI : nullptr;
    float* cam = (ui && skg::readable(ui + 0x48, 0x0C)) ? (float*)(ui + 0x48) : nullptr;
    float old_sc = g ? F<float>(g, 0x514C) : 0.f;
    float target_sc = g ? F<float>(g, 0x5150) : 0.f;
    bool moving = g && old_sc != target_sc;
    bool connected = C.sess && C.sess->connected();
    bool hold = cam && cell_logic::rescale_needs_origin_hold(g_fix_rescale_origin, connected, moving,
                                                             cam[0], cam[1], cam[2]);
    float saved_cam[3] = {};
    if (hold) {
        saved_cam[0] = cam[0];
        saved_cam[1] = cam[1];
        saved_cam[2] = cam[2];
        cam[0] = cam[1] = cam[2] = 0.f;
    }
    o_WorldScale(dt);
    if (hold) {
        float new_sc = g ? F<float>(g, 0x514C) : old_sc;
        cell_logic::rescale_restore_cam(saved_cam, old_sc, new_sc, cam);
        g_n.rescale_origin_holds++;
    }
}

// BUG-1509: every partner list is deleted by the loop that follows (hand_over gives a tile to the host's list only)
// (the lists are noted with the switch off too: hand_over counts a hand-over into one of them, the twin's measure)
static void flush_lists_begin()
{
    g_flush_lists.clear();
    for (auto& [id, pt] : g_ptiles)
        if (pt.list && pt.list->begin != pt.list->end)
            g_flush_lists.insert(pt.list);
    g_flushing = g_fix_flush_all_lists;
}
static void flush_lists_end()
{
    g_flushing = false;
    g_flush_lists.clear();
}

// tests (BUG-1510, `coop.pool tilestat`): the shadows and starved tiles of our lists now
std::string tiles_shadow_status()
{
    char b[120];
    snprintf(b, sizeof b, "tilestat shadows=%u starved=%u shadow_counted=%u shadow_empty=%d\n", (unsigned)g_shadow_tiles.size(),
             (unsigned)g_starved.size(), g_shadow_counted, (int)g_fix_shadow_empty);
    return b;
}

void partner_tiles_reset(bool drain)
{
    if (drain && in_cell())
        flush_lists_begin(); // BUG-1509
    for (auto& [id, pt] : g_ptiles) {
        if (drain && in_cell())
            tiles_drain(0, pt);
        tiles_free(pt);
    }
    if (drain && in_cell())
        flush_lists_end();
    g_ptiles.clear();
    g_shadow_tiles.clear();
    g_starved.clear();
    g_views.clear();
    g_view_boxes.clear();
    g_union = false;
}

// debug: every tile object is in exactly one list (the host's or one of ours); log the first step
// where the pool's live count and the lists disagree
static void tiles_check(const char* step, int level)
{
    uint8_t* g = G();
    TileList* h = host_tiles();
    if (!g || !h || g_n.tile_checks_bad > 20)
        return;
    size_t n = (size_t)((h->end - h->begin) / 2), dead = 0;
    uint32_t first_dead = 0, first_key = 0;
    for (int32_t* e = h->begin; e < h->end; e += 2)
        if (!tile_alive((uint32_t)e[1]) && !dead++) {
            first_dead = (uint32_t)e[1];
            first_key = (uint32_t)e[0];
        }
    for (auto& [id, pt] : g_ptiles)
        if (pt.list) {
            n += (size_t)((pt.list->end - pt.list->begin) / 2);
            for (int32_t* e = pt.list->begin; e < pt.list->end; e += 2)
                dead += !tile_alive((uint32_t)e[1]);
        }
    uint32_t live = F<uint32_t>(g, 0x38 + 0xC);
    if (n != live || dead) {
        g_n.tile_checks_bad++;
        if (!strncmp(step, "the frame between", 17) && dead) // dead entries: a list that points at freed tiles
            g_n.tile_check_warns_between++;
        std::string fr;
        for (uint32_t i : g_freed_dbg)
            fr += " " + std::to_string(i & 0xFFFF) + "/" + std::to_string(i >> 16);
        H->log(M, SK_WARN, "coop: tiles: check after %s (level %d): pool live %u, list entries %u, dead entries %u (host: first dead %u/%u key %08X); we freed%s",
               step, level, live, (unsigned)n, (unsigned)dead, first_dead & 0xFFFF, first_dead >> 16, first_key, fr.c_str());
        if (n && h->begin < h->end)
            H->log(M, SK_WARN, "coop: tiles: host list first entries %08X:%u/%u %08X:%u/%u", (uint32_t)h->begin[0],
                   (uint32_t)h->begin[1] & 0xFFFF, (uint32_t)h->begin[1] >> 16, h->end - h->begin > 2 ? (uint32_t)h->begin[2] : 0,
                   h->end - h->begin > 2 ? (uint32_t)h->begin[3] & 0xFFFF : 0, h->end - h->begin > 2 ? (uint32_t)h->begin[3] >> 16 : 0);
    }
}

// BUG-0295: tiles of ours left empty for the cell budget are populated, a few per frame, once the
// host's pool has room again (still alive: the generation of the index matches)
static void feed_starved()
{
    uint8_t* g = G();
    if (!g || g_starved.empty())
        return;
    uint8_t* data = F<uint8_t*>(g, 0x38);
    uint32_t size = F<uint32_t>(g, 0x38 + 0x14);
    int n = 0;
    float* vb = view_boxes();
    for (auto it = g_starved.begin(); it != g_starved.end() && n < 4;) {
        uint32_t idx = it->first;
        if (!tile_alive(idx) || !g_shadow_tiles.count(idx)) {
            it = g_starved.erase(it);
            continue;
        }
        uint32_t live = cells_live();
        if (live >= kPopCapScreen || (live >= kPopCap - 250 && g_frame_tiles >= 2))
            break;
        uint32_t* t = (uint32_t*)(data + (size_t)(idx & 0xFFFF) * size);
        if (live >= kPopCap) { // between the caps: only a tile on its partner's screen (as starve_tile)
            bool core = false;
            for (size_t k = 0; vb && k < g_views.size() && k < g_view_boxes.size(); k++)
                if (g_views.list()[k].player == it->second) {
                    float save[12];
                    memcpy(save, vb, sizeof save);
                    memcpy(vb, g_view_boxes[k].data(), sizeof save);
                    core = partner_core(t);
                    memcpy(vb, save, sizeof save);
                    break;
                }
            if (!core) {
                ++it;
                continue;
            }
            g_n.tiles_core++;
        }
        // another list populated the same tile meanwhile: ours stays a plain shadow
        bool other = tile_elsewhere(cell_logic::tile_key((int32_t)t[1], (int32_t)t[2], (int32_t)t[3], (int32_t)t[4]), nullptr);
        it = g_starved.erase(it);
        if (other)
            continue;
        g_shadow_tiles.erase(idx);
        if (t[1] == 0) {
            o_PopTile(t, 0);
            g_n.tiles_fed++;
            g_frame_tiles++;
            n++;
        }
    }
}

// BUG-0975: the active set of this pass would not fit the game's buffer
static bool tiles_overflow(int level)
{
    uint8_t* g = G();
    uint8_t* s = S();
    float* vb = view_boxes();
    if (!g_fix_tile_cap || !g || !s || !vb || !a_TileSizes)
        return false;
    double n = cell_logic::active_tiles(level, F<int32_t>(s, 0x1C), F<float>(g, 0x514C), vb, (const float*)a_TileSizes, 16);
    // also while the scale is still more than one level (x sqrt(10)) above its target: a pass that fit the
    // buffer at scale ratio ~7 filled the new level with that many cells at once and a game alone faulted in
    // CreateSoftBody / CellPhysicsStep (2138 faults, lab 20261112-195008). From one level above on it is the
    // game's own one-level fall (its debug keys), left as it is (cell_levelup_view's -150).
    bool falling = F<float>(g, 0x514C) > F<float>(g, 0x5150) * 3.5f;
    if (n <= cell_logic::kActiveTileCap && !falling)
        return false;
    if (g_n.tilecap_skips++ % 30 == 0)
        trace::line("tiles", "pass skipped: its active set would be %.0f tiles (the game's buffer holds 511; held until the scale settles), level %d, food %d, "
                             "world scale %.3f -> %.3f (BUG-0975, %u skipped)",
                    n, level, F<int32_t>(s, 0x1C), F<float>(g, 0x514C), F<float>(g, 0x5150), g_n.tilecap_skips);
    g_tilecap_max = std::max(g_tilecap_max, n);
    return true;
}

// the view centres: the host's view box, then every partner's cell; each centre's box (h_TileActive's union)
static void build_views(const float* vb, double now, std::array<float, 12>& host_box)
{
    memcpy(host_box.data(), vb, sizeof(float) * 12);
    views::Box hb{host_box[0], host_box[1], host_box[3], host_box[4]};
    g_views.clear();
    // the partners' centres: the one registry (views::note, coop.cpp: every partner shown in our
    // place), the same the Creature and Tribe stages use (BUG-0280); a host view that is not finite
    // gets no partner pass at all (BUG-0292)
    if (views::finite_pos({hb.cx(), hb.cy(), 0.f})) {
        g_views.set_host({hb.cx(), hb.cy(), 0.f});
        views::Centre pc[views::kMaxCentres];
        size_t n = views::partners(modes::kCell, now, pc, views::kMaxCentres - 1);
        for (size_t i = 0; i < n; i++)
            g_views.add(pc[i].player, {pc[i].pos.x, pc[i].pos.y, 0.f});
    }
    for (auto& v : g_views.list()) // tests (BUG-1064): how far each partner's noted centre is from where its cell is drawn
        if (v.player)
            if (uint8_t* pc = puppet_cell(v.player)) {
                float e = std::hypot(v.pos.x - pos_of(pc).x, v.pos.y - pos_of(pc).y);
                float& m = scale_moving() ? g_centre_err_moving : g_centre_err_max;
                if (std::isfinite(e) && e > m)
                    m = e;
            }
    g_view_boxes.clear();
    for (auto& v : g_views.list()) {
        float dx = v.pos.x - hb.cx(), dy = v.pos.y - hb.cy();
        std::array<float, 12> b = host_box;
        for (int i : {0, 3})
            b[i] += dx;
        for (int i : {1, 4})
            b[i] += dy;
        if (!g_view_boxes.empty()) // a partner's background box is empty: no layer 1 tiles for it
            for (int i = 6; i < 12; i++)
                b[i] = 0;
        g_view_boxes.push_back(b);
    }
    g_union = g_view_boxes.size() > 1;
    g_boxes_frame = g_sim_frame;
}

static void pop_tiles(int level, TileList* list, int all);
void SK_CDECL h_PopTiles(int level, TileList* list, int all)
{
    float* vb = view_boxes();
    if (g_test_view_edge > 0 && vb && list && list == host_tiles() && is_host() && in_cell()) {
        // tests (BUG-0999): the host's own view box with both x edges at 1e7 (a centre views::finite_pos refuses,
        // a box view_bad lets through) for this pass; the game's box comes back right after it
        g_test_view_edge--;
        float keep[12];
        memcpy(keep, vb, sizeof keep);
        for (int i : {0, 3, 6, 9})
            vb[i] = 1e7f;
        pop_tiles(level, list, all);
        memcpy(vb, keep, sizeof keep);
        return;
    }
    pop_tiles(level, list, all);
}
static void pop_tiles(int level, TileList* list, int all)
{
    if (view_bad()) { // BUG-0299: the active set from NaN boxes is garbage
        g_n.nan_pop_skips++;
        return;
    }
    if (tiles_overflow(level))
        return;
    float* vb = view_boxes();
    bool ours = vb && list && list == host_tiles() && is_host() && in_cell() && C.puppets;
    if (!ours) {
        if (!g_ptiles.empty() || g_union)
            partner_tiles_reset(in_cell() && list == host_tiles()); // not hosting a Cell session any more: the lists are the old pool's
        bool own = list && list == host_tiles() && in_cell();
        if (own)
            g_tile_level = level;
        if (own)
            tiles_check("the frame between (no partner tiles)", level);
        o_PopTiles(level, list, all);
        if (own)
            solo_refill(list); // BUG-1408
        if (own) {
            tiles_check("the own pass (no partner tiles)", level);
            static double last_log;
            double now = H->time_seconds();
            if (now - last_log > 10.0) {
                last_log = now;
                uint8_t* tp = G() + 0x38;
                trace::line("tiles", "pool %u/%u, own list %d, level %d (no partner tiles here); cell pool %u/%u",
                            F<uint32_t>(tp, 0xC), F<uint32_t>(tp, 0x18), (int)((list->end - list->begin) / 2), level,
                            F<uint32_t>(G(), 0x1C + 0xC), F<uint32_t>(G(), 0x1C + 0x18));
            }
        }
        return;
    }
    // A level change rescales the world (0xE6DB70, called by 0xE73E50 early in SimUpdate): it empties
    // the host's list and fills it again from EVERY tile object of the pool, ours included. Both
    // lists then held our tiles and both freed them (a double free: faults at CreateObject+0x37 after
    // `coop.pool food 1000`, lab 20261030-004900). Our tiles leave the host's list again here.
    if (!g_ptiles.empty()) {
        std::unordered_set<uint32_t> mine;
        for (auto& [id, pt] : g_ptiles)
            if (pt.list)
                for (int32_t* e = pt.list->begin; e < pt.list->end; e += 2)
                    mine.insert((uint32_t)e[1]);
        int32_t* out = list->begin;
        for (int32_t* e = list->begin; e < list->end; e += 2) {
            if (mine.count((uint32_t)e[1])) {
                g_n.tiles_unshared++;
                continue;
            }
            out[0] = e[0];
            out[1] = e[1];
            out += 2;
        }
        list->end = out;
    }
    if (!g_fix_flush_handover)
        tiles_check("the frame between", level);
    // a rebuild cleared the tile pool (RebuildLevel 0xE80010): our lists point at freed tiles
    for (auto& [id, pt] : g_ptiles)
        if (pt.list && pt.list->begin != pt.list->end && !tile_alive((uint32_t)pt.list->begin[1])) {
            tiles_forget(pt);
            g_n.tile_resets++;
        }
    if (g_fix_flush_handover) // BUG-1000: after the lists a rebuild freed are forgotten
        tiles_check("the frame between", level);
    if (g_shadow_tiles.size() > 4096)
        for (auto it = g_shadow_tiles.begin(); it != g_shadow_tiles.end();)
            it = tile_alive(*it) ? std::next(it) : g_shadow_tiles.erase(it);
    double now = H->time_seconds();
    std::array<float, 12> host_box;
    build_views(vb, now, host_box);
    kin_before_pass(); // BUG-1257
    // our deletes first (h_TileActive: any player's box), before the host's pass. On a level change
    // (the level base differs) or a rebuild (all), or when the tile pool is short of room, every tile
    // of ours goes back first: the host's own pass must never find the pool full (CreateObject then
    // returns 0 and the tile overwrites slot 0: faults at CreateObject+0x37 after `coop.pool food
    // 1000`, lab 20261030-004900). The partners' passes populate their areas again afterwards.
    static int last_level = INT_MIN;
    g_tile_level = level;
    uint8_t* tp = G() + 0x38;
    bool room = F<uint32_t>(tp, 0x18) >= F<uint32_t>(tp, 0xC) + 4 * 0x40;
    bool all_go = all || level != last_level || !room;
    last_level = level;
    g_flush_lists.clear();
    if (all_go)
        for (auto& [id, pt] : g_ptiles)
            if (pt.list && pt.list->begin != pt.list->end)
                g_flush_lists.insert(pt.list);
    g_flushing = all_go;
    for (auto& [id, pt] : g_ptiles) {
        if (all_go && pt.list && pt.list->begin != pt.list->end) {
            g_n.tiles_flush++;
            tiles_delete(pt.list, [](const int32_t*) { return false; });
        } else
            tiles_delete(pt.list, [](const int32_t* t) { return h_TileActive(t); });
    }
    g_flushing = false;
    g_flush_lists.clear();
    tiles_check("our deletes", level);
    g_freed_dbg.clear();
    g_pass = 0;
    g_pass_list = list;
    g_frame_tiles = 0;
    o_PopTiles(level, list, all);
    tiles_check("the host's pass", level);
    if (g_views.size() == 0 && g_fix_views_empty) // BUG-0999: the host's own centre was not usable this pass
        g_n.views_empty_skips++;
    else if (!all && !scale_moving()) { // a rebuild (all) populates the host's view in one go, a moving
                                        // world scale refills the host's list from every tile: the partners follow
        // BUG-0295: the partners take turns being first, so the one that comes first does not take
        // all the room under the cell budget every frame
        static size_t turn;
        size_t np = g_views.size() - 1;
        turn++;
        for (size_t j = 0; j < np; j++) {
            size_t k = 1 + (turn + j) % np;
            auto& pt = g_ptiles[g_views.list()[k].player];
            pt.seen = now;
            if (!pt.list) {
                pt.buf = new int32_t[kTileCap * 2];
                pt.list = new TileList();
                memset(pt.list, 0, sizeof(TileList));
                tiles_forget(pt);
            }
            // the tile pool (cCellGame+0x38) has a fixed capacity and CreateObject returns 0 when it
            // is full: PopulateTiles then used slot 0 of another tile (a double free later, the free
            // list broken: 4060 faults at CreateObject+0x37, lab 20261030-002902). One call creates
            // at most 0x33 tiles: a partner pass runs only with room for that and the host's next.
            if (F<uint32_t>(tp, 0x18) < F<uint32_t>(tp, 0xC) + 4 * 0x40) {
                g_n.tiles_full++;
                continue;
            }
            memcpy(vb, g_view_boxes[k].data(), sizeof(float) * 12);
            g_pass = (int)k;
            g_pass_list = pt.list;
            o_PopTiles(level, pt.list, 0);
            g_n.tile_passes++;
            tiles_check("a partner's pass", level);
        }
        memcpy(vb, host_box.data(), sizeof(float) * 12);
        feed_starved();
    }
    g_pass = -1;
    g_pass_list = nullptr;
    g_n.cells_peak = std::max(g_n.cells_peak, cells_live());
    static double last_log;
    if (now - last_log > 10.0) { // the support log: how full the tile pool is with the partners' areas
        last_log = now;
        size_t ours = 0;
        for (auto& [id, pt] : g_ptiles)
            if (pt.list)
                ours += (size_t)((pt.list->end - pt.list->begin) / 2);
        trace::line("tiles", "pool %u/%u, host list %d, partner lists %u (%u tiles), level %d, flushes %u, full %u; cell pool %u/%u",
                    F<uint32_t>(tp, 0xC), F<uint32_t>(tp, 0x18), (int)((list->end - list->begin) / 2),
                    (unsigned)g_ptiles.size(), (unsigned)ours, level, g_n.tiles_flush, g_n.tiles_full,
                    F<uint32_t>(G(), 0x1C + 0xC), F<uint32_t>(G(), 0x1C + 0x18));
    }
    // a partner that left (or has no cell here any more): the game deletes its tiles
    for (auto it = g_ptiles.begin(); it != g_ptiles.end();) {
        if (!g_views.find((uint8_t)it->first) && now - it->second.seen > 2.0) {
            tiles_drain(level, it->second);
            tiles_free(it->second);
            it = g_ptiles.erase(it);
        } else
            ++it;
    }
}

const views::Set& view_centres() { return g_views; }

// ShouldNotAttack 0xE57480 (cdecl bool(attacker, victim)): the prey / threat tests of the NPC AI
// (IsPrey 0xE68910, IsThreat 0xE68870) and DamageCell ask it. Its "wontAttackPlayer(WhenSmall)"
// flags of a species (resource +0x300/+0x301) apply only when the victim is the avatar
// (G+0x411C). A partner's cell is a player too: for it the game answers as for the avatar, so an
// NPC that spares the host spares the guest as well (and the other way round).
NoAttack_t o_NoAttack;
NoAttack_t& o_NoAttack_ptr() { return o_NoAttack; }
bool SK_CDECL h_NoAttack(uint8_t* a, uint8_t* b)
{
    uint8_t* g = G();
    if (!g || !b || !in_cell() || !modes::cell_is_puppet(b))
        return o_NoAttack(a, b);
    uint32_t& av = F<uint32_t>(g, 0x411C);
    uint32_t keep = av;
    size_t mark = game::swap_open(game::swap_undo_u32, &av, keep, "cell avatar index"); // BUG-0110
    av = F<uint32_t>(b, 0); // single-threaded cell step: restored right after
    bool r = o_NoAttack(a, b);
    av = keep;
    game::swap_close(mark);
    return r;
}

// BUG-0306: PickTargets 0xE70CF0 (cdecl bool(cell*, resData*), docs/re/cell.md#cell-pick-stale) runs
// after the behaviour's liveness check 0xE70E30. A cell in a group (+0x38C) copies its leader's
// flee / chase target and timers without checking them; when the leader's chase target died
// and the leader has not run its own check yet (it comes later in the pool, or it skips the
// behaviours), 0xE79C30 passes the dead target to 0xE57CE0 as NULL and IsDamageable reads
// [0+0x108]. Wine swallows the fault and aborts the frame, so the leader never runs again and
// the follower faults every frame (8616 times in lab 20261030-104149). Drop a dead copied target
// here, as 0xE70E30 does on the next frame.
PickTargets_t o_PickTargets;
uintptr_t a_PickTargets;
bool SK_CDECL h_PickTargets(uint8_t* c, void* res)
{
    bool r = o_PickTargets(c, res);
    if (!c || !G() || modes::cell_is_puppet(c)) // a puppet keeps its -1 (modes.cpp)
        return r;
    uint32_t& chase = F<uint32_t>(c, kChase);
    if (chase && !modes::cell_at(chase)) {
        chase = 0;
        F<float>(c, 0x230) = 0.f; // chase timer
        g_n.stale_targets++;
    }
    uint32_t& flee = F<uint32_t>(c, kFlee);
    if (flee && !modes::cell_at(flee)) {
        flee = 0;
        F<float>(c, 0x21C) = 0.f; // flee timer
        g_n.stale_targets++;
    }
    return r;
}

// SpawnLoot 0xE771D0: the cells a death leaves behind (meat chunks, part tokens) come from here
// (KillCell, a break 0xE7ADD0, DestroyCell(idx, 1)). Counted per machine: loot_calls / loot_cells.
SpawnLoot_t o_SpawnLoot;
// BUG-1004: SpawnLoot appends every cell it makes to the list it is given, recursion included, with no bound. The
// list is a static one of 4096 entries (the cell pool holds 4096 cells: the game can never write more). Off: the
// old 256-entry stack array (a loot drop of more than 256 cells writes past it on the stack).
static bool g_fix_loot_list = true;
COOP_FIX_UNSAFE("cell.loot_list", "BUG-1004", g_fix_loot_list, "cell: the host's list of a loot drop's new cells holds as many as the cell pool (4096), not 256 on the stack");
uint32_t g_loot_big_n;        // tests (BUG-1004): the next foreground loot drop makes this many cells of one entry
int g_loot_listed_last = -1;  // cells the last listed drop appended (the game's count)
int g_loot_listed_big = -1;   // tests: cells the armed drop appended
std::string g_loot_seen;      // tests: the entry types of the tables seen while armed
std::deque<std::pair<uint32_t, double>> g_loot_recent; // host: cells the last loot drops created
uint32_t pool_live()
{
    uint8_t* g = G();
    return g ? F<uint32_t>(g, 0x1C + 0xC) : 0;
}
void SK_CDECL h_SpawnLoot(void* res, void* key, void* query, void* pos, float size, int level, void* table, float dt,
                                 uint32_t* out, int* nout)
{
    bool fg = in_cell() && query && query == fg_query();
    if (fg && is_guest() && !g_solo) { // BUG-0205: our own pool while the host is away drops its own loot
        // The gameplay cells of the pool are the host's (no PopulateMarker here either): loot the guest's
        // game drops (a mirror broken or killed by the game here, DestroyCell(idx, 1)) would exist only on
        // this machine and be purged as stray at the next state. The host drops its own, mirrored here.
        g_n.loot_skipped++;
        return;
    }
    uint32_t before = fg ? pool_live() : 0;
    // the callers pass no list of the new cells (KillCell, E7ADD0, DestroyCell): ours records them
    // (E771D0 appends every cell it creates when the list is given, recursion included)
    static uint32_t big[4096]; // BUG-1004: the pool's size (the game's own recursion passes its list on, never ours again)
    uint32_t small[256];
    uint32_t* buf = g_fix_loot_list ? big : small;
    int cap = g_fix_loot_list ? 4096 : 256;
    int n = 0;
    bool mine = fg && !out && !nout;
    // tests (BUG-1004): one entry of the drop's table makes g_loot_big_n cells, or a repeat entry repeats g_loot_big_n
    // times (the other entries weigh 0 for this call)
    struct Saved { int32_t* e; int32_t count; float weight; };
    std::vector<Saved> saved;
    void* holder = nullptr;
    if (mine && g_loot_big_n && table && a_GetData) {
        int32_t* t = skg::cdecl_call<int32_t*>(a_GetData, table, &holder);
        if (g_loot_seen.size() < 200) {
            g_loot_seen += t ? " n" + std::to_string(t[1]) + ":" : " null";
            for (int i = 0; t && t[0] && i < t[1] && i < 8; i++)
                g_loot_seen += std::to_string(((int32_t*)((uint8_t*)(uintptr_t)t[0] + 0x1C * i))[0]);
        }
        if (t && t[0] && t[1] > 0 && t[1] < 64) {
            bool one = false;
            int kind = 1; // a cell entry, else a repeat entry (repeated n times, each a drop of its sub-table)
            for (int i = 0; i < t[1]; i++)
                if (((int32_t*)((uint8_t*)(uintptr_t)t[0] + 0x1C * i))[0] == 1)
                    kind = 1, i = t[1];
                else if (((int32_t*)((uint8_t*)(uintptr_t)t[0] + 0x1C * i))[0] == 2)
                    kind = 2;
            for (int i = 0; i < t[1]; i++) {
                int32_t* e = (int32_t*)((uint8_t*)(uintptr_t)t[0] + 0x1C * i);
                saved.push_back({e, e[4], ((float*)e)[3]});
                if (e[0] == kind && !one) {
                    one = true;
                    e[4] = (int32_t)g_loot_big_n;
                    ((float*)e)[3] = 1.f;
                } else
                    ((float*)e)[3] = 0.f;
            }
            if (!one) { // no cell entry in this table: left as it was
                for (auto& v : saved)
                    ((float*)v.e)[3] = v.weight;
                saved.clear();
            }
        }
        g_loot_big_n = saved.empty() ? g_loot_big_n : 0;
    }
    o_SpawnLoot(res, key, query, pos, size, level, table, dt, mine ? buf : out, mine ? &n : nout);
    for (auto& v : saved) {
        v.e[4] = v.count;
        ((float*)v.e)[3] = v.weight;
    }
    if (holder)
        --F<int>(holder, 0xC);
    if (mine)
        g_loot_listed_last = n;
    if (mine && !saved.empty())
        g_loot_listed_big = n;
    if (fg) {
        g_n.loot_calls++;
        uint32_t after = pool_live();
        if (after > before)
            g_n.loot_cells += after - before;
        double now = H->time_seconds();
        for (int i = 0; i < n && i < cap; i++)
            g_loot_recent.push_back({buf[i], now});
        while (g_loot_recent.size() > 64)
            g_loot_recent.pop_front();
    }
}

} // namespace cellsync
