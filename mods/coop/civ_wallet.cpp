// Civilization: every player's own money (civ.own_money, BUG-1468; idea and rules from PR #205 by IltonBJSilva). See
// civ_wallet.h and civ_wallet_logic.h; docs/re/civ.md#civ-own-money.
#include "civ_internal.h"
#include "civ_wallet.h"
#include "civ_wallet_logic.h"
#include "net/compat.h"
#include "own_world.h"

#include <map>
#include <set>

namespace civ_wallet {

using civ::C;
using civ::H;
using civ::M;

enum Kind : uint8_t { K_ASK = 1, K_SET = 2, K_ADD = 3, K_REPORT = 4 };
constexpr uintptr_t kWealth = 0x98; // cCivilization mWealth (float; GetMoney returns it for the player civilization)
constexpr double kReportEvery = 2.0, kAskEvery = 5.0;

// civ.wallet_row (BUG-1468): a guest with its own wallet does not take the host's shared Civ money total (shared.cpp
// MSG_RES) nor send its spending there. Off: the shared total overwrites the wallet (one money on the guest's screen).
static bool g_row_fix = true;
COOP_FIX("civ.wallet_row", "BUG-1468", g_row_fix,
         "civ: with own money a guest's Civ money is its own wallet, not the host's shared total");

// ---- the host ----
struct Guest {
    bool have = false; // the wallet was given (K_SET) in this Civ world
    civwallet::Ledger led;
    std::string key, name; // its stable store key (store.h) and name when the wallet was given
};
static std::map<uint8_t, Guest> g_guests;            // by session id
static std::map<std::string, double> g_kept;         // by store key: a wallet of a player who left, or from disk
static std::map<std::string, std::string> g_kept_name;
static uint32_t g_seq;                               // the host's operations
static uint32_t g_store_serial;
static bool g_paying_as;                             // PayAs runs
static uint32_t g_host_epoch;                        // game::stage_epoch(kCiv) of the host's wallets
static bool g_merge_due;                             // the store has wallets and the setting is off: back to the host
static bool g_end_due;                               // Space: the store's wallets end
// ---- the guest ----
static bool g_have;           // the host's K_SET arrived for this Civ world
static uint32_t g_applied;    // the last host operation applied
static uint32_t g_asked_epoch; // game::stage_epoch(kCiv) of the wallet asked for / given
static double g_last_report = -1e9, g_last_ask = -1e9;
static double g_pending;      // host changes that came while the game had no player civilization
// ---- counters (coop.wallet) ----
static struct {
    uint32_t asks, asks_early, asks_refused, sets, adds, reports, reports_bad, charges, refunds, newcomers, kept_back,
        merged, ended, dropped_elsewhere, trades, refused;
    double income, shared_out, charged, merged_money, traded;
} g_n;

static bool setting() { return settings::on(settings::kCivOwnMoney); }
static bool host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

bool active() { return C.sess && C.sess->role() != net::Role::None && setting(); }

// host: guest `id`'s mod reads MSG_CIV_WALLET (civ family minor civwallet::kMinor or newer, the same rev)
static bool peer_reads(uint8_t id)
{
    bool has = false;
    std::vector<net::FamilyRev> fam;
    if (!C.sess || !C.sess->peer_families(id, has, fam))
        return false;
    const net::FamilyInfo* mine = net::family_info((uint8_t)net::Family::civ);
    return mine && net::compat::has_minor(fam, has, (uint8_t)net::Family::civ, mine->rev, civwallet::kMinor);
}
// guest: the host's mod sends wallets (its WELCOME's families)
static bool host_reads()
{
    bool has = false;
    std::vector<net::FamilyRev> fam;
    if (!C.sess || !C.sess->host_families(has, fam))
        return false;
    const net::FamilyInfo* mine = net::family_info((uint8_t)net::Family::civ);
    return mine && net::compat::has_minor(fam, has, (uint8_t)net::Family::civ, mine->rev, civwallet::kMinor);
}

// this game's own announced civ minor (HELLO / WELCOME): `coop.manifest plant civ 1 1` makes it play as a 0.7.16.1 peer
// (the lab's old-peer case without an old build); a real older mod has none of this code
static bool self_reads()
{
    if (!C.sess)
        return false;
    const net::FamilyInfo* mine = net::family_info((uint8_t)net::Family::civ);
    return mine && net::compat::has_minor(C.sess->families, true, (uint8_t)net::Family::civ, mine->rev, civwallet::kMinor);
}

bool guest_own_money()
{
    return guest() && !civwallet::guest_takes_shared(g_row_fix, civwallet::guest_runs(setting(), host_reads() && self_reads()));
}

static bool in_civ_world() { return game::mode_id() == modes::kCiv && civ::player_civ(); }
static double money_of(void* civ) { return civ ? (double)skg::thiscall<float>(civ::a_GetMoney, civ) : 0; }
static void set_money(void* civ, double v)
{
    if (civ && skg::readable(civ, kWealth + sizeof(float)))
        *(float*)((uint8_t*)civ + kWealth) = (float)civwallet::clamp_money(v);
}

static void send(uint8_t to, uint8_t kind, uint32_t seq, double v)
{
    net::Writer w;
    w.u8(kind);
    if (kind != K_ASK) {
        w.u32(seq);
        w.f64(v);
    }
    C.sess->send_reliable_to(to, net::MSG_CIV_WALLET, w);
}

static void keep(const Guest& g)
{
    if (g.have && !g.key.empty()) {
        g_kept[g.key] = g.led.value();
        g_kept_name[g.key] = g.name;
    }
}

// host: an operation on guest `p`'s wallet (+ credit, - charge)
static void host_op(uint8_t p, double amount)
{
    auto it = g_guests.find(p);
    if (it == g_guests.end() || !it->second.have || !civwallet::amount_ok(amount) || amount == 0)
        return;
    uint32_t seq = ++g_seq;
    it->second.led.op(seq, amount);
    send(p, K_ADD, seq, amount);
    g_n.adds++;
    keep(it->second);
}

static std::set<uint8_t> wallets()
{
    std::set<uint8_t> s;
    for (auto& [p, g] : g_guests)
        if (g.have)
            s.insert(p);
    return s;
}

bool host_has_wallet(uint8_t player)
{
    auto it = g_guests.find(player);
    return active() && host() && it != g_guests.end() && it->second.have;
}

double host_income(void* civ, double gain)
{
    if (!active() || !host() || g_paying_as || !civ || civ != civ::player_civ() || !(gain > 0) || g_guests.empty())
        return 0;
    g_n.income += gain;
    std::multiset<uint8_t> owners;
    for (auto& [city, owner] : civ::g_city_owner)
        if (city && civ::pol_of(city) == civ::pol_of(civ))
            owners.insert(owner);
    auto parts = civwallet::split_income(gain, owners, C.sess->my_id(), wallets());
    double out = 0;
    for (auto& [p, v] : parts) {
        host_op(p, v);
        out += v;
    }
    if (out > 0) {
        set_money(civ, money_of(civ) - out);
        g_n.shared_out += out;
    }
    return out;
}

double host_trade_pair(uint8_t a, uint8_t b, double gain)
{
    if (!active() || !host())
        return 0;
    double out = 0;
    for (auto& [p, v] : civwallet::split_pair(gain, a, b, C.sess->my_id(), wallets())) {
        host_op(p, v);
        out += v;
    }
    if (out > 0) {
        g_n.trades++;
        g_n.traded += out;
    }
    return out;
}

bool host_refund(uint8_t player, double amount)
{
    if (!host_has_wallet(player) || player == C.sess->my_id())
        return false;
    host_op(player, amount);
    g_n.refunds++;
    return true;
}

PayAs::PayAs(int player)
{
    if (!active() || !host() || player < 0 || player == C.sess->my_id())
        return;
    auto it = g_guests.find((uint8_t)player);
    if (it == g_guests.end() || !it->second.have)
        return; // a guest without a wallet (an older peer, not in Civ yet): the shared money pays, as before
    civ_ = civ::player_civ();
    if (!civ_ || !skg::readable(civ_, kWealth + sizeof(float)))
        return;
    on_ = true;
    player_ = (uint8_t)player;
    host_money_ = (double)civ::F<float>(civ_, kWealth);
    wallet_ = it->second.led.value();
    set_money(civ_, wallet_);
    g_paying_as = true;
}

PayAs::~PayAs()
{
    if (!on_)
        return;
    g_paying_as = false;
    double spent = wallet_ - (double)civ::F<float>(civ_, kWealth);
    H->log(M, SK_INFO, "civ wallet: player %u's purchase in the host's game: wallet %.0f, spent %.0f (the host's money %.0f kept)",
           player_, wallet_, spent, host_money_);
    set_money(civ_, host_money_);
    if (spent != 0 && civwallet::amount_ok(spent)) {
        host_op(player_, -spent);
        if (spent > 0) {
            g_n.charges++;
            g_n.charged += spent;
        }
    }
}

// ---- the store (record civ.wallet v1: i32 money; docs/01-architecture.md#arch-player-store) ----
// host: the wallets of the world that opened, read once per store; with the setting off they go back to the host
static void host_store_tick()
{
    pstore::Store* w = store::world();
    if (!w || store::serial() == g_store_serial)
        return;
    g_store_serial = store::serial();
    g_kept.clear();
    g_kept_name.clear();
    for (auto& k : w->keys_with(pstore::kRecCivWallet))
        if (const pstore::Record* r = w->get(k, pstore::kRecCivWallet); r && civwallet::stored_ok(r->ver, r->data.size())) {
            pstore::In in(r->data);
            double v = in.i32();
            if (civwallet::amount_ok(v)) {
                g_kept[k] = civwallet::clamp_money(v);
                g_kept_name[k] = w->name_of(k);
            }
        }
    g_merge_due = g_end_due = false;
}

// host / alone: the kept wallets of a world played with the setting off go back to the host's money once (sum before =
// sum after), and in Space they end (the Civ money is over: docs/re/civ.md#civ-own-money); the records are erased
static void host_store_settle()
{
    pstore::Store* w = store::world();
    if (!w || g_kept.empty() || (C.sess && C.sess->role() == net::Role::Client))
        return;
    uint32_t mode = game::mode_id();
    bool off = !setting();
    if (mode == modes::kSpace) {
        g_end_due = true;
    } else if (off && mode == modes::kCiv && civ::player_civ())
        g_merge_due = true;
    if (!g_merge_due && !g_end_due)
        return;
    double sum = 0;
    std::vector<double> kept;
    for (auto& [k, v] : g_kept) {
        kept.push_back(v);
        sum += v;
        w->erase(k, pstore::kRecCivWallet);
    }
    if (g_merge_due) {
        void* civ = civ::player_civ();
        double before = money_of(civ);
        set_money(civ, civwallet::merge_back(before, kept));
        g_n.merged++;
        g_n.merged_money += sum;
        H->log(M, SK_INFO, "civ wallet: the setting is off: %u kept wallets (%.0f) back into the host's money %.0f -> %.0f",
               (unsigned)kept.size(), sum, before, money_of(civ));
        if (C.feed && sum > 0) {
            char b[160];
            snprintf(b, sizeof b, "Own money is off in this session: the players' kept money (%.0f) is back in the shared money", sum);
            C.feed(b);
        }
    } else {
        g_n.ended++;
        H->log(M, SK_INFO, "civ wallet: Space: %u kept Civ wallets (%.0f) end", (unsigned)kept.size(), sum);
    }
    g_kept.clear();
    g_kept_name.clear();
    g_guests.clear();
    g_merge_due = g_end_due = false;
}

static void disk_save(pstore::Store& s, int64_t now)
{
    if (!setting())
        return; // nothing is kept with the setting off (the records went back to the host)
    for (auto& [p, g] : g_guests) // the present guests' newest value
        keep(g);
    for (auto& [k, v] : g_kept)
        if (pstore::key_ok(k)) {
            pstore::Out o;
            o.i32((int32_t)std::lround(civwallet::clamp_money(v)));
            s.put(k, g_kept_name[k], pstore::kRecCivWallet, civwallet::kRecVer, o.d, now);
        }
}
static bool g_store_reg = store::add({pstore::kRecCivWallet, disk_save, [](const std::string& from, const std::string& to) {
                                          auto it = g_kept.find(from);
                                          if (it != g_kept.end()) {
                                              g_kept[to] = it->second;
                                              g_kept_name[to] = g_kept_name[from];
                                              g_kept.erase(it);
                                              g_kept_name.erase(from);
                                          }
                                      }});

static void host_ask(uint8_t from)
{
    g_n.asks++;
    void* civ = civ::player_civ();
    if (!civ || game::mode_id() != modes::kCiv) {
        g_n.asks_early++; // the guest asks again every few seconds until the host plays its Civ world
        return;
    }
    if (!civwallet::host_gives(setting(), peer_reads(from) && self_reads(), own_world::peer_elsewhere(from))) {
        g_n.asks_refused++;
        return;
    }
    Guest& g = g_guests[from];
    std::string k = store::key_of(from);
    auto it = k.empty() ? g_kept.end() : g_kept.find(k);
    double v;
    const char* how;
    if (g.have) { // asked again (its answer crossed a new ask): the wallet it has
        v = g.led.value();
        how = "again";
    } else if (it != g_kept.end()) {
        v = it->second;
        g_n.kept_back++;
        how = "kept";
    } else { // a newcomer: an equal part of the host's money (money moves, none is made)
        int holders = 2 + (int)wallets().size();
        v = civwallet::newcomer_share(money_of(civ), holders);
        set_money(civ, money_of(civ) - v);
        g_n.newcomers++;
        how = "a new part";
    }
    uint32_t seq = ++g_seq;
    g.have = true;
    g.key = k;
    g.name = C.sess->name_of(from);
    g.led.set(seq, v);
    keep(g);
    send(from, K_SET, seq, v);
    g_n.sets++;
    H->log(M, SK_INFO, "civ wallet: player %u gets its own money %.0f (%s)", from, v, how);
}

void on_message(uint8_t from, net::Reader& r)
{
    if (!C.sess)
        return;
    uint8_t kind = r.u8();
    if (host()) {
        if (from == C.sess->my_id() || !active())
            return;
        if (kind == K_ASK && r.ok()) {
            host_ask(from);
        } else if (kind == K_REPORT) {
            uint32_t seq = r.u32();
            double v = r.f64();
            auto it = g_guests.find(from);
            if (!r.ok() || it == g_guests.end() || !it->second.have || !it->second.led.report(seq, v)) {
                g_n.reports_bad++;
                return;
            }
            g_n.reports++;
            keep(it->second);
        }
        return;
    }
    if (from != 0 || (kind != K_SET && kind != K_ADD) || !guest_own_money())
        return;
    uint32_t seq = r.u32();
    double v = r.f64();
    void* civ = civ::player_civ();
    if (!r.ok() || !civwallet::amount_ok(v) || own_world::elsewhere())
        return;
    if (kind == K_ADD && g_have && seq > g_applied && !civ) { // (no player civilization now: applied when it is back)
        g_pending += v;
        g_applied = seq;
        return;
    }
    if (!civ || (kind == K_SET && game::mode_id() != modes::kCiv))
        return;
    if (kind == K_SET) {
        set_money(civ, v);
        g_have = true;
        g_applied = seq;
        g_pending = 0;
        g_last_report = -1e9;
        g_n.sets++;
        if (C.feed) {
            char b[128];
            snprintf(b, sizeof b, "Your own money: %.0f (the host's setting: every player has its own money)", v);
            C.feed(b);
        }
    } else if (g_have && seq > g_applied) {
        set_money(civ, money_of(civ) + v);
        g_applied = seq;
        g_n.adds++;
    }
}

// a leaver keeps its wallet by its key (it gets it back when it joins again)
PLAYER_STATE("civ.wallet",
             [](uint8_t id) {
                 auto it = g_guests.find(id);
                 if (it == g_guests.end())
                     return;
                 keep(it->second);
                 g_guests.erase(it);
             },
             [](uint8_t id) -> size_t { return g_guests.count(id); });

static void reset_session()
{
    for (auto& [p, g] : g_guests)
        keep(g);
    g_guests.clear();
    g_have = false;
    g_applied = 0;
    g_asked_epoch = 0;
    g_paying_as = false;
    g_pending = 0;
    g_host_epoch = 0;
}
LIFECYCLE_HOOK("civ.wallet", lifecycle::kSessionEnd, -43, [](const lifecycle::Ctx&) { reset_session(); }, false);

void tick(double now)
{
    if (!C.sess || C.sess->role() != net::Role::Client) { // the host, or alone
        host_store_tick();
        host_store_settle();
        if (!host() || !active())
            return;
        uint32_t e = game::stage_epoch(modes::kCiv);
        if (e != g_host_epoch) { // another Civ world (a load): the wallets are given again there (each guest asks)
            g_host_epoch = e;
            for (auto& [p, g] : g_guests) {
                keep(g);
                g.have = false;
            }
        }
        for (auto& [p, g] : g_guests)
            if (g.have && own_world::peer_elsewhere(p)) { // it plays a world of its own: its money there is not this one
                keep(g);
                g.have = false;
                g_n.dropped_elsewhere++;
            }
        return;
    }
    if (!guest_own_money())
        return;
    uint32_t epoch = game::stage_epoch(modes::kCiv);
    if (epoch != g_asked_epoch || own_world::elsewhere()) { // another Civ world: its wallet is asked for again
        g_asked_epoch = epoch;
        g_have = false;
        g_pending = 0;
        if (own_world::elsewhere())
            return;
    }
    if (!in_civ_world()) // an editor keeps the wallet (the stage's world is the same)
        return;
    if (g_pending != 0 && g_have) {
        set_money(civ::player_civ(), money_of(civ::player_civ()) + g_pending);
        g_pending = 0;
    }
    if (!g_have) {
        if (now - g_last_ask >= kAskEvery) { // until the host plays its Civ world and answers
            g_last_ask = now;
            send(0, K_ASK, 0, 0);
            g_n.asks++;
        }
        return;
    }
    if (now - g_last_report >= kReportEvery) {
        g_last_report = now;
        send(0, K_REPORT, g_applied, money_of(civ::player_civ()));
        g_n.reports++;
    }
}

std::string status()
{
    char b[520];
    snprintf(b, sizeof b,
             "civ wallet: setting=%s active=%d role=%s own=%d have=%d applied=%u asks=%u early=%u refused_asks=%u sets=%u "
             "adds=%u reports=%u bad=%u charges=%u charged=%.0f refunds=%u newcomers=%u kept_back=%u income=%.0f "
             "shared_out=%.0f trades=%u traded=%.0f merged=%u/%.0f ended=%u dropped_elsewhere=%u row_fix=%d\n",
             setting() ? "on" : "off", (int)active(), host() ? "host" : guest() ? "guest" : "none", (int)guest_own_money(),
             (int)g_have, g_applied, g_n.asks, g_n.asks_early, g_n.asks_refused, g_n.sets, g_n.adds, g_n.reports,
             g_n.reports_bad, g_n.charges, g_n.charged, g_n.refunds, g_n.newcomers, g_n.kept_back, g_n.income,
             g_n.shared_out, g_n.trades, g_n.traded, g_n.merged, g_n.merged_money, g_n.ended, g_n.dropped_elsewhere,
             (int)g_row_fix);
    std::string s = b;
    if (void* civ = civ::player_civ(); civ && game::mode_id() == modes::kCiv) {
        snprintf(b, sizeof b, "money=%.0f\n", money_of(civ));
        s += b;
    }
    for (auto& [p, g] : g_guests) {
        snprintf(b, sizeof b, "player %u: wallet=%.0f have=%d unconfirmed=%u\n", p, g.led.value(), (int)g.have,
                 (unsigned)g.led.sent.size());
        s += b;
    }
    for (auto& [k, v] : g_kept) {
        snprintf(b, sizeof b, "kept %s (%s): %.0f\n", k.c_str(), g_kept_name[k].c_str(), v);
        s += b;
    }
    return s;
}

int command(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string sub = argc > 1 ? argv[1] : "";
    if (sub == "income" && argc > 2) { // tests: as if the game's city tick paid the player civilization this income now
        void* civ = civ::player_civ();
        double v = atof(argv[2]);
        if (!civ || !host() || !(v > 0) || !civwallet::amount_ok(v)) {
            H->out_printf(out, "host in Civ only, a positive amount\n");
            return 1;
        }
        double before = money_of(civ);
        set_money(civ, before + v);
        double gain = money_of(civ) - before;
        double parts = host_income(civ, gain);
        shared::note_passive(2, gain - parts);
        H->out_printf(out, "income %.0f paid, %.0f to the guests\n", gain, parts);
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

} // namespace civ_wallet
