// Players' old cells as wild species (BUG-1477, lineage_cells.h; the rules: lineage_cells_logic.h). Idea and RE of
// PR #211 by IltonBJSilva, ported onto our creations stream (line bundles), our kin filter's chain and the old-peer
// guard of creations 1.2.
#include "creations_internal.h"
#include "lineage_cells.h"
#include "lineage_cells_logic.h"
#include "net/compat.h"
#include "species_line_core.h"

#include <random>

namespace lineage_cells {

using creations::C;
using creations::H;
using creations::M;
using creations::Key;

static bool g_fix = true;
COOP_FIX("cell.lineage_cells", "BUG-1477", g_fix,
         "cell: with the session setting on, some wild creatures take the look of the players' old cells (a guest's only with its consent)");

constexpr uint32_t kCllType = 0x3D97A8E4u, kCellGroup = 0x40616200u;

struct Offered {          // one of this game's own old cells, packed
    uint32_t own = 0;     // its My Creations instance
    uint32_t shared = 0;  // its shared key (0: refused here)
    std::vector<uint8_t> data;
};
static std::vector<Offered> g_mine;         // ours (the host's own pool cells, or what a guest offers)
static bool g_offered_last;                 // we offered last round (a guest's withdrawal goes once)
static std::set<uint32_t> g_sent_host;      // guest: shared keys sent to the host in this connection
static std::map<uint32_t, std::pair<uint8_t, std::vector<uint8_t>>> g_store; // host: shared -> {owner, bundle}
static std::vector<lineage::Cand> g_pool;   // host: the cells spawns may take
static std::set<uint32_t> g_registered;     // shared keys registered on this game
static std::set<uint32_t> g_preloaded;      // preloaded in this Cell stage
static bool g_consent;                      // guest: the co-op window's box
static double g_offer_at = -100, g_send_at = -100;
static unsigned g_share = lineage::kSharePercent;
static std::mt19937 g_rng{std::random_device{}()};
static uintptr_t a_PreloadModel, a_PreloadCreature;
static unsigned g_packed, g_refused, g_in, g_out, g_withdrawn, g_picks, g_no_fit, g_considered, g_old_peer_held,
    g_preloads, g_swaps_seen;

static bool session() { return C.sess && C.sess->connected(); }
static bool is_host() { return session() && C.sess->role() == net::Role::Host; }
static bool is_guest() { return session() && C.sess->role() == net::Role::Client; }
static bool setting() { return g_fix && settings::get(settings::kLineageCells) != 0; }

static bool reads(const std::vector<net::FamilyRev>& f, bool has)
{
    return net::compat::has_minor(f, has, (uint8_t)net::Family::creations, 1, lineage::kCreationsMinor);
}
static bool peer_reads(uint8_t id)
{
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess && C.sess->peer_families(id, has, f) && reads(f, has);
}
static bool host_reads()
{
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess && C.sess->host_families(has, f) && reads(f, has);
}
static std::vector<uint8_t> peers()
{
    std::vector<uint8_t> v;
    if (session())
        for (auto& p : C.sess->players())
            if (p.id != C.sess->my_id())
                v.push_back(p.id);
    return v;
}

static Key key_of(uint32_t shared) { return {shared, kCllType, kCellGroup}; }

static Key register_bundle(const std::vector<uint8_t>& data, uint32_t shared, const std::string& whose)
{
    Key k = creations::line_register(data, shared, whose);
    if (k.instance)
        g_registered.insert(k.instance);
    else
        g_refused++;
    return k;
}

static void pool_add(uint32_t shared, uint8_t owner)
{
    for (auto& c : g_pool)
        if (c.key == shared)
            return;
    if (g_pool.size() >= lineage::kMaxPool)
        return;
    lineage::Cand c;
    c.key = shared;
    c.owner = owner;
    g_pool.push_back(c);
}

static void send_withdraw(int to, uint8_t owner, uint32_t id)
{
    creations::send_bundle(to, owner, (creations::Kind)creations::kKindLineageCell, id, {});
    g_withdrawn++;
}

// this game's own old cells: packed and registered once each; those no longer offered go
static void offer_tick(double now)
{
    const bool host = is_host();
    const bool want = lineage::offers(setting(), host, g_consent) && (host || host_reads());
    if (!want) {
        if (g_offered_last && is_guest())
            send_withdraw(0, C.sess->my_id(), 0); // every cell of ours goes from the host's pool
        if (g_offered_last)
            H->log(M, SK_INFO, "lineage cells: our old cells are not offered any more (BUG-1477)");
        g_offered_last = false;
        g_mine.clear();
        g_sent_host.clear();
        if (host)
            std::erase_if(g_pool, [](const lineage::Cand& c) { return c.owner == 0; });
        return;
    }
    if (now - g_offer_at < 10.0)
        return;
    g_offer_at = now;
    g_offered_last = true;
    std::vector<uint32_t> own, pre;
    if (!creations::premades_listed(creations::kCell))
        for (auto& o : creations::options(creations::kCell))
            own.push_back(o.key.instance);
    const uint32_t playing = creations::mine(creations::kCell).key.instance;
    const std::vector<uint32_t> want_keys = lineage::own_offer(own, playing, pre);
    // gone from the offer (the cell played now, deleted): withdrawn
    for (auto it = g_mine.begin(); it != g_mine.end();) {
        if (std::find(want_keys.begin(), want_keys.end(), it->own) != want_keys.end()) {
            ++it;
            continue;
        }
        if (it->shared) {
            if (host)
                std::erase_if(g_pool, [&](const lineage::Cand& c) { return c.owner == 0 && c.key == it->shared; });
            else if (g_sent_host.erase(it->shared))
                send_withdraw(0, C.sess->my_id(), it->shared);
        }
        it = g_mine.erase(it);
    }
    unsigned packs = 0; // (each pack reads My Creations again: a few per round)
    for (uint32_t k : want_keys) {
        bool known = false;
        for (auto& m : g_mine)
            known |= m.own == k;
        if (known || packs >= 4)
            continue;
        packs++;
        Offered o;
        o.own = k;
        uint32_t content = 0;
        if (creations::line_pack({k, kCllType, kCellGroup}, o.data, content) && content) {
            o.shared = species::shared_instance(content);
            if (!register_bundle(o.data, o.shared, "our old cell " + creations::hex(k)).instance)
                o.shared = 0;
            else
                g_packed++;
        }
        g_mine.push_back(std::move(o));
    }
    if (host)
        for (auto& m : g_mine)
            if (m.shared) {
                g_store[m.shared] = {0, m.data};
                pool_add(m.shared, 0);
            }
}

// host: every peer that reads kind 12 gets each pool cell once; a guest: its offer to the host
static void send_tick(double now)
{
    if (now - g_send_at < 1.0)
        return;
    g_send_at = now;
    if (is_guest()) {
        unsigned budget = 2;
        for (auto& m : g_mine)
            if (m.shared && !g_sent_host.count(m.shared) && budget) {
                budget--;
                creations::send_bundle(0, C.sess->my_id(), (creations::Kind)creations::kKindLineageCell, m.shared, m.data);
                g_sent_host.insert(m.shared);
                g_out++;
            }
        return;
    }
    if (!is_host())
        return;
    for (uint8_t p : peers()) {
        if (!peer_reads(p))
            continue; // an older peer: never sent (it drops kind 12 anyway), and the host does not swap while it is here
        unsigned budget = 2;
        for (auto& c : g_pool) {
            if (!budget || c.got.count(p) || c.owner == p)
                continue;
            auto st = g_store.find(c.key);
            if (st == g_store.end())
                continue;
            budget--;
            creations::send_bundle(p, st->second.first, (creations::Kind)creations::kKindLineageCell, c.key, st->second.second);
            c.got[p] = now;
            g_out++;
        }
    }
}

// both, in Cell: every registered cell's model loaded for the cell graphics before a cell of it is drawn, as the
// editor does for the avatar (#211: PreloadModel and cCellGFX::PreloadCreature)
static void preload_tick()
{
    if (game::mode_id() != modes::kCell || !modes::cell_game()) {
        g_preloaded.clear();
        return;
    }
    for (uint32_t k : g_registered) {
        if (g_preloaded.count(k) || g_preloaded.size() >= 256)
            continue;
        g_preloaded.insert(k);
        if (a_PreloadModel)
            skg::cdecl_call<void>(a_PreloadModel, k, kCllType, kCellGroup);
        if (a_PreloadCreature)
            skg::cdecl_call<void>(a_PreloadCreature, k);
        g_preloads++;
    }
}

void tick(double now)
{
    if (!a_PreloadModel) {
        a_PreloadModel = H->sym("Simulator::Cell::PreloadModel");
        a_PreloadCreature = H->sym("Simulator::Cell::cCellGFX::PreloadCreature");
    }
    if (!session())
        return;
    offer_tick(now);
    send_tick(now);
    preload_tick();
}

void on_bundle(uint8_t owner, uint8_t from, uint32_t id, std::vector<uint8_t>&& data)
{
    if (!g_fix || !lineage::is_shared(id) || data.empty())
        return;
    g_in++;
    if (is_host()) {
        if (owner != from || !setting())
            return; // a guest offers only its own, and only while the host plays the setting
        if (g_store.size() >= lineage::kMaxPool && !g_store.count(id))
            return;
        if (!register_bundle(data, id, "player " + std::to_string(owner) + "'s old cell").instance)
            return;
        g_store[id] = {owner, std::move(data)};
        pool_add(id, owner);
        H->log(M, SK_INFO, "lineage cells: player %u offers its old cell %08X (BUG-1477)", owner, id);
        return;
    }
    if (from != 0)
        return;
    register_bundle(data, id, "player " + std::to_string(owner) + "'s old cell");
}

void on_withdraw(uint8_t owner, uint8_t from, uint32_t id)
{
    if (!is_host() || owner != from)
        return;
    const size_t before = g_pool.size();
    std::erase_if(g_pool, [&](const lineage::Cand& c) { return lineage::withdrawn(c, owner, id); });
    for (auto it = g_store.begin(); it != g_store.end();)
        it = it->second.first == owner && (id == 0 || it->first == id) ? g_store.erase(it) : std::next(it);
    if (before != g_pool.size())
        H->log(M, SK_INFO, "lineage cells: player %u withdrew %s (BUG-1477)", owner, id ? creations::hex(id).c_str() : "its old cells");
}

// lifecycle-exempt: called by creations::on_player_join (registered, creations.session and the player life of creations)
void on_player_join(uint8_t id)
{
    for (auto& c : g_pool)
        c.got.erase(id); // a newcomer under this id gets every cell again
}

// lifecycle-exempt: called by creations::on_player_leave (registered, creations.session and the player life of creations)
void on_player_leave(uint8_t id)
{
    on_withdraw(id, id, 0);
    for (auto& c : g_pool)
        c.got.erase(id);
}

// lifecycle-exempt: called by creations::on_session_end (registered, creations.session and the player life of creations)
void on_session_end()
{
    g_pool.clear();
    g_store.clear();
    g_mine.clear();
    g_sent_host.clear();
    g_offered_last = false;
    g_consent = false; // asked again in every session
    g_offer_at = -100;
    // (the registered packages stay, as the line creations': nothing of a player's own answers for a 0x7B key)
}

int box()
{
    if (!g_fix || !is_guest() || !setting() || game::mode_id() != modes::kCell)
        return -1;
    return g_consent ? 1 : 0;
}

void toggle()
{
    g_consent = !g_consent;
    g_offer_at = -100;
    H->log(M, SK_INFO, "lineage cells: our old cells as wild species: %s (BUG-1477)", g_consent ? "yes" : "no");
    if (C.feed)
        C.feed(g_consent ? "your old cells may appear as wild species in this session"
                         : "your old cells do not appear as wild species");
}

bool active()
{
    if (!setting() || !is_host() || g_pool.empty())
        return false;
    std::vector<bool> r;
    for (uint8_t p : peers())
        r.push_back(peer_reads(p));
    bool ok = lineage::host_swaps(true, r);
    if (!ok)
        g_old_peer_held++;
    return ok;
}

bool pick(bool any_diet, bool native_carn, bool native_herb, int (*diet_of)(uint32_t), uint32_t out[3])
{
    g_considered++;
    if (!lineage::roll(std::uniform_int_distribution<unsigned>(0, 99)(g_rng), g_share))
        return false;
    const double now = H->time_seconds();
    const std::vector<uint8_t> ps = peers();
    std::vector<const lineage::Cand*> fit;
    for (auto& c : g_pool) {
        if (c.diet < 0 && diet_of)
            c.diet = (int8_t)diet_of(c.key);
        if (c.diet < 0 || !lineage::ready(c, ps, now, lineage::kLead))
            continue;
        if (lineage::diet_fits(c.diet & 1, c.diet & 2, native_carn, native_herb, any_diet))
            fit.push_back(&c);
    }
    if (fit.empty()) {
        g_no_fit++;
        return false;
    }
    const lineage::Cand* c = fit[std::uniform_int_distribution<size_t>(0, fit.size() - 1)(g_rng)];
    out[0] = c->key;
    out[1] = kCllType;
    out[2] = kCellGroup;
    g_picks++;
    return true;
}

bool is_lineage(uint32_t instance) { return g_registered.count(instance) != 0; }
void cell_left() { g_preloaded.clear(); }
void count_swap(bool swapped) { g_swaps_seen += swapped; }

std::string status()
{
    char b[420];
    snprintf(b, sizeof b,
             "lineage cells: fix=%d setting=%d role=%s consent=%d share=%u mine=%u pool=%u store=%u registered=%u "
             "packed=%u refused=%u in=%u out=%u withdrawn=%u considered=%u picks=%u no_fit=%u old_peer_held=%u "
             "preloads=%u swaps=%u\n",
             (int)g_fix, (int)setting(), is_host() ? "host" : is_guest() ? "guest" : "-", (int)g_consent, g_share,
             (unsigned)g_mine.size(), (unsigned)g_pool.size(), (unsigned)g_store.size(), (unsigned)g_registered.size(),
             g_packed, g_refused, g_in, g_out, g_withdrawn, g_considered, g_picks, g_no_fit, g_old_peer_held, g_preloads,
             g_swaps_seen);
    std::string out = b;
    for (auto& c : g_pool) {
        snprintf(b, sizeof b, "pool %08X owner=%u diet=%d peers=%u\n", c.key, c.owner, (int)c.diet, (unsigned)c.got.size());
        out += b;
    }
    for (auto& m : g_mine) {
        snprintf(b, sizeof b, "mine %08X shared=%08X\n", m.own, m.shared);
        out += b;
    }
    return out;
}

std::string command(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[0] == "share") // tests: more swaps in a short run
        g_share = (unsigned)std::clamp(atoi(a[1].c_str()), 0, 100);
    if (a.size() >= 2 && a[0] == "consent") { // tests: the co-op window's box
        bool want = a[1] == "on" || a[1] == "1";
        if (want != g_consent)
            toggle();
    }
    return status();
}

} // namespace lineage_cells
