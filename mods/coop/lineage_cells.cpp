// BUG-9275: Lineages cells (lineage_cells.h), ported from Lineages BUG-9111 (PR #196, mods/coop/lineage_cells.cpp).
#include "lineage_cells.h"

#include "cellsync.h"
#include "creations.h"
#include "fixes.h"
#include "settings.h"

#include <windows.h>

#include <map>
#include <random>
#include <set>

namespace lineage_cells {

static const SkHost* H;
static SkMod* M;
static net::Session* S;
static bool g_fix = true;
COOP_FIX("cell.lineage_cells", "BUG-9275", g_fix,
         "cell: the players' own cells appear as about one in six spawns of a native creature species of the same diet (Lineages cells)");
static unsigned g_share = kSharePercent;

struct Cand {
    creations::Key key; // as this game builds it
    bool carn = false, herb = false;
    uint32_t sig = 0;               // its bem bytes: one cell under several keys counts once
    std::string name, from;         // for the trace: "host" or "guest:<id>"
    std::map<uint8_t, double> sent; // peer -> when its records went out
};
static std::vector<Cand> g_pool; // built outside the frame; read by pick() inside it
static SRWLOCK g_lock = SRWLOCK_INIT;
static bool g_building = false;
static double g_built_at = -1e9, g_now = 0;
static unsigned g_picks, g_no_fit, g_refused, g_current_skipped, g_sent, g_seen, g_no_sig, g_no_diet, g_dupes, g_builds;
static std::string g_current, g_current_keys; // the signatures and keys left out at the last build (tests)
static std::mt19937 g_rng{std::random_device{}()};
constexpr double kRefreshSeconds = 15, kGuestLead = 8; // a guest gets a cell's records this long before its first use
constexpr size_t kGuestMax = 16;                       // own cells a guest sends (newest first)
// guest: own cells sent to the host in this connection
static std::set<uint32_t> g_out;
static unsigned g_out_n;
static double g_out_last;

bool enabled() { return g_fix && settings::on(settings::kLineageCells); }
unsigned share_percent() { return g_share; }

void init(const SkHost* host, SkMod* mod, net::Session* session)
{
    H = host;
    M = mod;
    S = session;
}

static bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }

static std::string hex(uint32_t v)
{
    char b[12];
    snprintf(b, sizeof b, "%08X", v);
    return b;
}

// outside the frame: the candidates now
static void build(void*)
{
    std::vector<Cand> pool;
    // the players' current cells: never NPCs (by their bem bytes: the same cell under My Creations' key, the creations
    // stream's 0x7C key and a guest cell's 0x7B key)
    std::set<uint32_t> current;
    uint32_t own[3];
    if (cellsync::own_cell_model(own))
        if (uint32_t s = creations::model_signature({own[0], own[1], own[2]}))
            current.insert(s);
    if (S && S->connected())
        for (auto& p : S->players()) {
            if (p.id == S->my_id())
                continue;
            const creations::Key k = creations::player_key(p.id, creations::kCell);
            if (uint32_t s = k.instance ? creations::model_signature(k) : 0)
                current.insert(s);
        }
    std::string cur;
    for (uint32_t s : current)
        cur += (cur.empty() ? "" : ",") + hex(s);
    std::set<uint32_t> seen;
    std::string cur_keys;
    auto add = [&](const creations::Key& key, const std::string& name, const std::string& from) {
        if (!key.instance || pool.size() >= 256)
            return;
        g_seen++;
        std::string meta_name;
        const uint32_t sig = creations::model_signature(key, &meta_name);
        if (!sig) {
            g_no_sig++;
            return;
        }
        if (!seen.insert(sig).second) {
            g_dupes++;
            return;
        }
        if (current.count(sig)) {
            g_current_skipped++;
            cur_keys += (cur_keys.empty() ? "" : ",") + hex(key.instance);
            return;
        }
        auto m = creations::ensure_model(key, creations::kMkCell);
        if (m.status == creations::Model::kRefused || !m.key.instance) {
            g_refused++;
            return;
        }
        const uint32_t k[3] = {m.key.instance, m.key.type, m.key.group};
        Cand c;
        if (!cellsync::model_diet(k, c.carn, c.herb) || (!c.carn && !c.herb)) {
            g_no_diet++;
            return;
        }
        cellsync::preload_model(k); // drawn as itself
        c.key = m.key;
        c.sig = sig;
        c.name = meta_name.empty() ? name : meta_name;
        c.from = from;
        pool.push_back(std::move(c));
    };
    for (auto& o : creations::options(creations::kCell))
        add(o.key, o.name, "host");
    for (auto& g : creations::guest_cells())
        add(g.key, g.name, "guest:" + std::to_string(g.owner));
    static size_t logged = (size_t)-1;
    if (pool.size() != logged) {
        logged = pool.size();
        H->log(M, SK_INFO, "coop: Lineages cells: %u player cell(s) for the Cell pool, %u left out as a player's current cell (BUG-9275)",
               (unsigned)pool.size(), (unsigned)current.size());
    }
    AcquireSRWLockExclusive(&g_lock);
    for (auto& c : pool) // keep what went out already
        for (auto& old : g_pool)
            if (old.sig == c.sig)
                c.sent = old.sent;
    g_pool = std::move(pool);
    g_current = cur;
    g_current_keys = cur_keys;
    g_builds++;
    ReleaseSRWLockExclusive(&g_lock);
    g_building = false;
}

// guest: our own cells to the host, one every 2 s
static void guest_tick(double now, bool in_cell)
{
    if (!is_guest()) {
        g_out.clear(); // the next connection gets them again
        return;
    }
    if (!in_cell || !enabled() || now - g_out_last < 2.0 || g_out.size() >= kGuestMax)
        return;
    g_out_last = now;
    for (auto& o : creations::options(creations::kCell)) {
        if (g_out.count(o.key.instance))
            continue;
        g_out.insert(o.key.instance);
        if (creations::guest_cell_out(o.key))
            g_out_n++;
        return;
    }
}

void tick(double now, bool in_cell)
{
    g_now = now;
    guest_tick(now, in_cell);
    if (!enabled() || !in_cell || is_guest()) {
        if (!in_cell && !g_pool.empty() && !g_building) {
            AcquireSRWLockExclusive(&g_lock);
            g_pool.clear();
            ReleaseSRWLockExclusive(&g_lock);
            g_built_at = -1e9;
        }
        return;
    }
    if (!g_building && now - g_built_at >= kRefreshSeconds) {
        g_building = true;
        g_built_at = now;
        H->run_outside_frame(build, nullptr);
    }
    // every guest gets each cell's records before the cell is used (its mirrors are built from them)
    if (!S || !S->connected() || g_building)
        return;
    static double last;
    if (now - last < 1.0)
        return;
    last = now;
    AcquireSRWLockExclusive(&g_lock);
    std::set<uint8_t> here;
    for (auto& p : S->players())
        here.insert(p.id);
    for (auto& c : g_pool) // a player who left gets everything again when it comes back
        std::erase_if(c.sent, [&](const auto& s) { return !here.count(s.first); });
    for (auto& p : S->players()) {
        if (p.id == S->my_id())
            continue;
        unsigned budget = 4; // a few bundles a second
        for (auto& c : g_pool) {
            if (c.sent.count(p.id) || !budget)
                continue;
            budget--;
            if (creations::host_cell_out(p.id, c.key)) {
                c.sent[p.id] = now;
                g_sent++;
            } else
                c.sent[p.id] = 1e18; // no records here: never ready for this peer
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool pick(bool carnivore, bool herbivore, uint32_t key[3])
{
    if (!enabled() || is_guest() || !TryAcquireSRWLockShared(&g_lock))
        return false;
    std::vector<const Cand*> fit;
    std::vector<uint8_t> peers;
    if (S && S->connected())
        for (auto& p : S->players())
            if (p.id != S->my_id())
                peers.push_back(p.id);
    for (auto& c : g_pool) {
        // behaviour follows the mouth: the cell eats what the native species eats (an omnivore fits either)
        if (!((c.carn && carnivore) || (c.herb && herbivore)))
            continue;
        bool ready = true;
        for (uint8_t p : peers) {
            auto it = c.sent.find(p);
            ready &= it != c.sent.end() && g_now - it->second >= kGuestLead;
        }
        if (ready)
            fit.push_back(&c);
    }
    bool ok = !fit.empty();
    if (ok) {
        const Cand* c = fit[std::uniform_int_distribution<size_t>(0, fit.size() - 1)(g_rng)];
        key[0] = c->key.instance;
        key[1] = c->key.type;
        key[2] = c->key.group;
        g_picks++;
    } else
        g_no_fit++;
    ReleaseSRWLockShared(&g_lock);
    return ok;
}

bool is_candidate(uint32_t instance)
{
    if (!TryAcquireSRWLockShared(&g_lock))
        return false;
    bool r = false;
    for (auto& c : g_pool)
        r |= c.key.instance == instance;
    ReleaseSRWLockShared(&g_lock);
    return r;
}

std::string debug(const std::vector<std::string>& args)
{
    if (args.size() >= 2 && args[0] == "share") // tests: more swaps for a short run
        g_share = (unsigned)std::min(100l, std::max(0l, strtol(args[1].c_str(), nullptr, 10)));
    std::string out = std::string("lineage cells ") + (enabled() ? "on" : "off") + " fix=" + (g_fix ? "1" : "0") +
                      " role=" + (is_guest() ? "guest" : "host") + " share=" + std::to_string(g_share) +
                      " pool=" + std::to_string(g_pool.size()) + " builds=" + std::to_string(g_builds) +
                      " picks=" + std::to_string(g_picks) + " no_fit=" + std::to_string(g_no_fit) +
                      " refused=" + std::to_string(g_refused) + " current_skipped=" + std::to_string(g_current_skipped) +
                      " sent=" + std::to_string(g_sent) + " seen=" + std::to_string(g_seen) + " no_sig=" + std::to_string(g_no_sig) +
                      " no_diet=" + std::to_string(g_no_diet) + " dupes=" + std::to_string(g_dupes) +
                      " guest_out=" + std::to_string(g_out_n) + "\n";
    AcquireSRWLockShared(&g_lock);
    out += "current " + (g_current.empty() ? std::string("none") : g_current) + " keys " +
           (g_current_keys.empty() ? std::string("none") : g_current_keys) + "\n";
    for (auto& c : g_pool) {
        char b[200];
        snprintf(b, sizeof b, "cell %08X sig=%08X %s%s '%s' from=%s peers=%u\n", c.key.instance, c.sig, c.carn ? "C" : "-",
                 c.herb ? "H" : "-", c.name.c_str(), c.from.c_str(), (unsigned)c.sent.size());
        out += b;
    }
    ReleaseSRWLockShared(&g_lock);
    return out;
}

} // namespace lineage_cells
