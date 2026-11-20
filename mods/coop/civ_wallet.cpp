// Civilization: every player's own money (civ.own_money, BUG-9259). See civ_wallet.h and civ_wallet_logic.h.
#include "civ_internal.h"
#include "civ_wallet.h"
#include "civ_wallet_logic.h"
#include "civ_nations.h"

#include <map>
#include <set>

namespace civ_wallet {

using civ::C;
using civ::H;
using civ::M;

enum Kind : uint8_t { K_ASK = 1, K_SET = 2, K_ADD = 3, K_REPORT = 4, K_NATION = 5 };
constexpr uintptr_t kWealth = 0x98; // cCivilization mWealth (float; docs/re/civ.md: GetMoney returns it for the player)
constexpr double kReportEvery = 2.0;

// ---- the host ----
struct Guest {
    bool have = false; // the wallet was given (K_SET) in this Civ world
    civwallet::Ledger led;
};
static std::map<uint8_t, Guest> g_guests;      // by session id
static std::map<std::string, double> g_kept;   // by store key (store.h): a wallet of a player who left, or from disk
static std::map<std::string, std::string> g_kept_name;
static uint32_t g_seq;                          // the host's operations
static uint32_t g_store_serial;
static bool g_paying_as;                        // PayAs runs: an AddMoney in it is not income
static uint32_t g_host_epoch;                   // game::stage_epoch(kCiv) of the host's wallets
// ---- the guest ----
static bool g_have;                // the host's K_SET arrived for this Civ world
static uint32_t g_applied;         // the last host operation applied
static uint32_t g_asked_epoch;     // game::stage_epoch(kCiv) of the wallet asked for / given
static double g_last_report, g_last_ask;
static double g_pending;           // host changes that came while the game had no player civilization
// ---- counters (coop.wallet) ----
static struct {
    uint32_t asks, asks_early, sets, adds, reports, reports_bad, charges, refunds, newcomers, kept_back;
    double income, shared_out, charged;
} g_n;

static bool setting() { return settings::on(settings::kCivOwnMoney); }

bool active()
{
    // BUG-9274: a player's own nation keeps its own money also with the setting off
    return C.sess && C.sess->role() != net::Role::None && (setting() || civ::nations::wallets());
}

static bool host() { return C.sess && C.sess->role() == net::Role::Host; }

static bool in_civ_world() { return game::mode_id() == modes::kCiv && civ::player_civ(); }

static double money_of(void* civ) { return civ ? (double)skg::thiscall<float>(civ::a_GetMoney, civ) : 0; }

static void set_money(void* civ, double v)
{
    if (!civ)
        return;
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

static std::string key_of(uint8_t id) { return store::key_of(id); }

// host: a host operation on guest `p`'s wallet (+ credit, - charge)
static void host_op(uint8_t p, double amount)
{
    auto it = g_guests.find(p);
    if (it == g_guests.end() || !it->second.have || !civwallet::amount_ok(amount) || amount == 0)
        return;
    uint32_t seq = ++g_seq;
    it->second.led.op(seq, amount);
    send(p, K_ADD, seq, amount);
    g_n.adds++;
    std::string k = key_of(p);
    if (!k.empty())
        g_kept[k] = it->second.led.value();
}

static std::set<uint8_t> wallets()
{
    std::set<uint8_t> s;
    for (auto& [p, g] : g_guests)
        if (g.have)
            s.insert(p);
    return s;
}

bool host_add_money(void* civ, float amount, int difficulty)
{
    if (!active() || !setting() || !host() || g_paying_as || !civ || civ != civ::player_civ() || !(amount > 0))
        return false;
    double before = money_of(civ);
    civ::o_AddMoney(civ, amount, difficulty);
    double gain = money_of(civ) - before;
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
    return true;
}

bool host_refund(uint8_t player, double amount)
{
    if (!active() || !setting() || !host() || player == C.sess->my_id() || !g_guests.count(player) || !g_guests[player].have)
        return false;
    host_op(player, amount);
    g_n.refunds++;
    return true;
}

PayAs::PayAs(int player)
{
    // civ_wallet::active(), not the member active() (which is on_, false here: the purchase went to the host's money)
    if (!civ_wallet::active() || !setting() || !host() || player < 0 || player == C.sess->my_id())
        return;
    if (civ::nations::nation_of_player((uint8_t)player))
        return; // BUG-9274: its own nation pays (civ_host.cpp host_buy, the nation's own money)
    auto it = g_guests.find((uint8_t)player);
    civ_ = civ::player_civ();
    if (!civ_ || it == g_guests.end() || !it->second.have) {
        H->log(M, SK_WARN, "civ wallet: player %d's purchase is paid by the host: %s", player,
               !civ_ ? "no player civilization" : it == g_guests.end() ? "no wallet known" : "its wallet is not given yet");
        return;
    }
    on_ = true;
    player_ = (uint8_t)player;
    host_money_ = money_of(civ_);
    wallet_ = it->second.led.value();
    set_money(civ_, wallet_);
    g_paying_as = true;
}

PayAs::~PayAs()
{
    if (!on_)
        return;
    g_paying_as = false;
    double spent = wallet_ - money_of(civ_);
    H->log(M, SK_INFO, "civ wallet: player %u's purchase in the host's game: wallet %.0f, spent %.0f (the host's money %.0f)",
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

// host: the wallets of the world that opened (store.h), read once per world
static void host_store_tick()
{
    pstore::Store* w = store::world();
    if (!w || store::serial() == g_store_serial)
        return;
    g_store_serial = store::serial();
    g_kept.clear();
    g_kept_name.clear();
    for (auto& k : w->keys_with("civ.wallet"))
        if (const pstore::Record* r = w->get(k, "civ.wallet"); r && r->ver == 1 && r->data.size() == 4) {
            pstore::In in(r->data);
            double v = in.i32();
            if (civwallet::amount_ok(v)) {
                g_kept[k] = civwallet::clamp_money(v);
                g_kept_name[k] = w->name_of(k);
            }
        }
}

static void disk_save(pstore::Store& s, int64_t now)
{
    for (auto& [p, g] : g_guests) // the present guests' newest value
        if (g.have) {
            std::string k = key_of(p);
            if (!k.empty()) {
                g_kept[k] = g.led.value();
                g_kept_name[k] = C.sess ? C.sess->name_of(p) : "";
            }
        }
    for (auto& [k, v] : g_kept)
        if (pstore::key_ok(k)) {
            pstore::Out o;
            o.i32((int32_t)std::lround(civwallet::clamp_money(v)));
            s.put(k, g_kept_name[k], "civ.wallet", 1, o.d, now);
        }
}
static bool g_store_reg = store::add({"civ.wallet", disk_save, [](const std::string& from, const std::string& to) {
                                          auto it = g_kept.find(from);
                                          if (it != g_kept.end()) {
                                              g_kept[to] = it->second;
                                              g_kept.erase(it);
                                          }
                                      }});

static void host_ask(uint8_t from)
{
    g_n.asks++;
    void* civ = civ::player_civ();
    if (!civ || game::mode_id() != modes::kCiv) {
        g_n.asks_early++; // the guest asks again every few seconds until it has its wallet
        return;
    }
    Guest& g = g_guests[from];
    std::string k = key_of(from);
    double v;
    auto it = k.empty() ? g_kept.end() : g_kept.find(k);
    double nation = 0;
    if (civ::nations::player_money(from, nation)) { // BUG-9274: its own nation's money
        v = civwallet::clamp_money(nation);
        it = g_kept.end();
    } else if (!setting()) {
        g_n.asks_early++; // (a player without a nation of its own and the setting off: the shared money)
        return;
    } else if (g.have) { // asked again (its answer crossed a new ask): the wallet it has
        v = g.led.value();
    } else if (it != g_kept.end()) {
        v = it->second;
        g_n.kept_back++;
    } else { // a newcomer: an equal part of the host's money
        int holders = 2 + (int)wallets().size() - (g.have ? 1 : 0);
        v = civwallet::newcomer_share(money_of(civ), holders);
        set_money(civ, money_of(civ) - v);
        g_n.newcomers++;
    }
    uint32_t seq = ++g_seq;
    g.have = true;
    g.led.set(seq, v);
    if (!k.empty()) {
        g_kept[k] = v;
        g_kept_name[k] = C.sess->name_of(from);
    }
    send(from, K_SET, seq, v);
    g_n.sets++;
    H->log(M, SK_INFO, "civ wallet: player %u gets its own money %.0f (%s)", from, v, it != g_kept.end() ? "kept" : "a new part");
}

// ---- both ----
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
            double nation;
            if (civ::nations::player_money(from, nation))
                return; // BUG-9274: its nation's money is the host's (nation_sync)
            auto it = g_guests.find(from);
            if (!r.ok() || it == g_guests.end() || !it->second.have || !it->second.led.report(seq, v)) {
                g_n.reports_bad++;
                return;
            }
            g_n.reports++;
            std::string k = key_of(from);
            if (!k.empty())
                g_kept[k] = it->second.led.value();
        }
        return;
    }
    if (from != 0 || (kind != K_SET && kind != K_ADD && kind != K_NATION))
        return;
    uint32_t seq = r.u32();
    double v = r.f64();
    void* civ = civ::player_civ();
    if (!r.ok() || !civwallet::amount_ok(v))
        return;
    if (kind == K_ADD && g_have && seq > g_applied && !civ) { // (no player civilization now: applied when it is back)
        g_pending += v;
        g_applied = seq;
        return;
    }
    if (!civ || (kind == K_SET && game::mode_id() != modes::kCiv))
        return;
    if (kind == K_NATION) { // BUG-9274: its own nation's money (silent)
        if (g_have && seq > g_applied && game::mode_id() == modes::kCiv) {
            set_money(civ, v);
            g_applied = seq;
            g_n.adds++;
        }
        return;
    }
    if (kind == K_SET) {
        set_money(civ, v);
        g_have = true;
        g_applied = seq;
        g_pending = 0;
        g_n.sets++;
        if (C.feed) {
            char b[96];
            snprintf(b, sizeof b, "Your own money: %.0f (the host's setting: every player has their own money)", v);
            C.feed(b);
        }
    } else if (g_have && seq > g_applied) {
        set_money(civ, money_of(civ) + v);
        g_applied = seq;
        g_n.adds++;
    }
}

void nation_sync(uint8_t player, double money)
{
    if (!active() || !host() || !civwallet::amount_ok(money))
        return;
    auto it = g_guests.find(player);
    if (it == g_guests.end() || !it->second.have || std::fabs(it->second.led.value() - civwallet::clamp_money(money)) < 0.5)
        return;
    uint32_t seq = ++g_seq;
    it->second.led.set(seq, money);
    send(player, K_NATION, seq, civwallet::clamp_money(money));
    g_n.adds++;
}

// lifecycle-exempt: called by session_glue.cpp player_leave next to the other modules (it needs the leaver id)
void on_player_leave(uint8_t id)
{
    auto it = g_guests.find(id);
    if (it == g_guests.end())
        return;
    std::string k = key_of(id);
    if (it->second.have && !k.empty())
        g_kept[k] = it->second.led.value(); // it gets it back when it joins again
    g_guests.erase(it);
}

static void reset_session()
{
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
    if (!C.sess)
        return;
    if (host()) {
        host_store_tick();
        uint32_t e = game::stage_epoch(modes::kCiv);
        if (e != g_host_epoch) { // another Civ world (a load): the wallets are given again there (each guest asks)
            g_host_epoch = e;
            for (auto& [p, g] : g_guests)
                if (g.have) {
                    std::string k = key_of(p);
                    if (!k.empty())
                        g_kept[k] = g.led.value();
                    g.have = false;
                }
        }
        return;
    }
    if (!active() || !C.sess->connected())
        return;
    uint32_t epoch = game::stage_epoch(modes::kCiv);
    if (epoch != g_asked_epoch) { // another Civ world (a load, the host's world arrived): its wallet is asked for
        g_asked_epoch = epoch;
        g_have = false;
        g_pending = 0;
        g_last_ask = -1e9;
    }
    if (!in_civ_world()) // an editor keeps the wallet (the stage's world is the same)
        return;
    if (g_pending != 0 && g_have) {
        set_money(civ::player_civ(), money_of(civ::player_civ()) + g_pending);
        g_pending = 0;
    }
    if (!g_have) {
        if (now - g_last_ask >= 5.0) { // until the host has its own Civ world open and answers
            g_last_ask = now;
            send(0, K_ASK, 0, 0);
            g_n.asks++;
        }
        return;
    }
    if (g_have && now - g_last_report >= kReportEvery) {
        g_last_report = now;
        send(0, K_REPORT, g_applied, money_of(civ::player_civ()));
        g_n.reports++;
    }
}

std::string status()
{
    char b[400];
    snprintf(b, sizeof b,
             "civ wallet: setting=%s active=%d role=%s have=%d applied=%u asks=%u early=%u sets=%u adds=%u reports=%u bad=%u "
             "charges=%u charged=%.0f refunds=%u newcomers=%u kept_back=%u income=%.0f shared_out=%.0f\n",
             settings::on(settings::kCivOwnMoney) ? "on" : "off", (int)active(), host() ? "host" : "guest", (int)g_have,
             g_applied, g_n.asks, g_n.asks_early, g_n.sets, g_n.adds, g_n.reports, g_n.reports_bad, g_n.charges, g_n.charged, g_n.refunds,
             g_n.newcomers, g_n.kept_back, g_n.income, g_n.shared_out);
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
    if (sub == "income" && argc > 2) { // tests: as if the game paid the player civilization this income now
        void* civ = civ::player_civ();
        float v = (float)atof(argv[2]);
        if (!civ || !host()) {
            H->out_printf(out, "host in Civ only\n");
            return 1;
        }
        if (!host_add_money(civ, v, 0))
            civ::o_AddMoney(civ, v, 0);
        H->out_printf(out, "income %.0f paid\n", v);
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

} // namespace civ_wallet
