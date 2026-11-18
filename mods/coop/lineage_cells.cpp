// BUG-9111: Lineages cells (lineage_cells.h).
#include "lineage_cells.h"

#include "cellsync.h"
#include "creations.h"
#include "fixes.h"
#include "library.h"

#include <map>
#include <windows.h>
#include <random>
#include <set>

namespace lineage_cells {

static const SkHost* H;
static SkMod* M;
static net::Session* S;
static bool g_on = true;
static bool g_fix = true;
COOP_FIX("cell.lineage_cells", "BUG-9111", g_fix,
         "cell: player cells (own, lobby players', with Foreign Creations on any Cache cell) appear as about one in four native creature spawns");

struct Cand {
    creations::Key key;      // as this game builds it (ensure_model's key)
    bool carn = false, herb = false;
    uint32_t sig = 0;        // its model record's bytes: one cell under several keys counts once
    std::string name, from;  // for the trace
    std::map<uint8_t, double> sent; // peer -> when its records went out
};
static std::vector<Cand> g_pool;          // built outside the frame; read by pick() inside it
static SRWLOCK g_lock = SRWLOCK_INIT;
static bool g_building = false;
static double g_built_at = -1e9, g_now = 0;
static unsigned g_picks = 0, g_no_fit = 0, g_refused = 0, g_current_skipped = 0, g_sent = 0, g_seen = 0, g_no_sig = 0, g_no_diet = 0, g_dupes = 0;
static std::mt19937 g_rng{std::random_device{}()};
constexpr double kRefreshSeconds = 15, kGuestLead = 8; // a guest gets a cell's records this long before its first use

bool enabled() { return g_on && g_fix; }
void set_enabled(bool on)
{
    g_on = on;
    H->config_set_str(M, "cell", "lineage_cells", on ? "1" : "0");
}

void init(const SkHost* host, SkMod* mod, net::Session* session)
{
    H = host;
    M = mod;
    S = session;
    g_on = H->config_int(mod, "cell", "lineage_cells", 1) != 0;
}

static bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }

// outside the frame: the candidates now
static void build(void*)
{
    std::vector<Cand> pool;
    // the players' current cells: never NPCs (the native spread shows the own species by itself)
    std::set<uint32_t> current;
    // A partner's current cell reaches the host twice: from the creations stream (its editor's bem record)
    // and from the Lineages Cache (the PNG import keeps a cll record of another size): the bytes never
    // match, so a Cache cell is also left out when its author and name are a partner's current cell's.
    // (A partner's older cell of the same name is left out with it: the safe side.)
    std::set<std::pair<std::string, std::string>> partner_current;
    uint32_t own[3];
    if (cellsync::own_cell_model(own))
        if (uint32_t s = creations::model_signature({own[0], own[1], own[2]}))
            current.insert(s);
    if (S && S->connected())
        for (auto& p : S->players()) {
            if (p.id == S->my_id())
                continue;
            const creations::Key k = creations::player_key(p.id, creations::kCell);
            std::string name, author;
            if (uint32_t s = k.instance ? creations::model_signature(k, &name, &author) : 0)
                current.insert(s);
            if (!name.empty())
                partner_current.insert({author, name});
        }
    std::set<uint32_t> seen;
    auto add = [&](const creations::Key& key, const std::string& name, const std::string& from, const std::string& author) {
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
        const std::string& nm = meta_name.empty() ? name : meta_name;
        if (current.count(sig) || (!author.empty() && partner_current.count({author, nm}))) { // older versions stay allowed (decision 5)
            g_current_skipped++;
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
        cellsync::preload_model(k); // drawn as itself, not as a ghost
        c.key = m.key;
        c.sig = sig;
        c.name = nm;
        c.from = from;
        pool.push_back(std::move(c));
    };
    for (auto& o : creations::options(creations::kCell))
        add(o.key, o.name, "mine", "");
    for (auto& c : library::pool_cells())
        add(c.key, c.name, c.local ? "mine" : "cache:" + c.author, c.local ? "" : c.author);
    AcquireSRWLockExclusive(&g_lock);
    for (auto& c : pool) // keep what went out already
        for (auto& old : g_pool)
            if (old.sig == c.sig)
                c.sent = old.sent;
    g_pool = std::move(pool);
    ReleaseSRWLockExclusive(&g_lock);
    g_building = false;
}

void tick(double now, bool in_cell)
{
    g_now = now;
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

std::string debug(const std::vector<std::string>& args)
{
    if (!args.empty() && (args[0] == "on" || args[0] == "off"))
        set_enabled(args[0] == "on");
    std::string out = std::string("lineage cells ") + (enabled() ? "on" : "off") + " pool=" + std::to_string(g_pool.size()) +
                      " picks=" + std::to_string(g_picks) + " no_fit=" + std::to_string(g_no_fit) +
                      " refused=" + std::to_string(g_refused) + " current_skipped=" + std::to_string(g_current_skipped) +
                      " sent=" + std::to_string(g_sent) + " seen=" + std::to_string(g_seen) + " no_sig=" + std::to_string(g_no_sig) +
                      " no_diet=" + std::to_string(g_no_diet) + " dupes=" + std::to_string(g_dupes) + "\n";
    AcquireSRWLockShared(&g_lock);
    for (auto& c : g_pool) {
        char b[160];
        snprintf(b, sizeof b, "cell %08X %s%s '%s' %s peers=%u\n", c.key.instance, c.carn ? "C" : "-", c.herb ? "H" : "-",
                 c.name.c_str(), c.from.c_str(), (unsigned)c.sent.size());
        out += b;
    }
    ReleaseSRWLockShared(&g_lock);
    return out;
}

} // namespace lineage_cells
