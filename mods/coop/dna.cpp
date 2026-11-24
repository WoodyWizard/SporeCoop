// DNA routing of the coop core (B2 of 0.7.15: moved out of coop.cpp, docs/audit-0.7.15/core.md CORE-2): the DNA totals'
// state, the cause ledger's notices, the single-player pace (dna_pace.h), where a gain goes (on_dna, dna_route.h), the
// DNA a guest earned while the host was away (BUG-0829) and a guest's own spends (BUG-0466). The portable rules are in
// dna_route.h, dna_ledger.h and dna_pace.h; the messages are handled in the session callbacks, the totals are sent from
// the frame, the away state is coop.cpp's host_away_tick.
#include "coop_state.h"
#include "actor_scope.h"
#include "dna_own.h"
#include "net/validate.h"
#include "own_progress.h"
#include "store.h"

namespace coop_core {

// WP-P1 / P2 of 0.7.17 (BUG-1469, own_progress.h): with all.shared_dna 2 every player's DNA and bar are its own: a gain
// stays on the game that made it (nothing forwarded, nothing paced), the host sends no total and passes what its game
// paid for a guest's deed (the hunt credit, the Actor scope) to that guest, a spend stays where it was made
uint32_t g_own_totals_ignored = 0, g_own_gains_kept = 0, g_own_deeds_given = 0; // tests (coop.progress)

float g_dna_sent = -1;      // host: last broadcast total
double g_dna_at = 0;        // host: when it was sent
uint32_t g_dna_rx = 0, g_dna_fwd = 0;

// DNA earned locally: the host's total is authoritative
extern bool g_host_away;
extern float g_solo_dna;
extern uint32_t g_solo_mode, g_mode;
// BUG-0151: a goal card's reward a guest earns while the host is away is kept like any other DNA
// (social::goal_dna sent it to a host that drops it); the host keeps a guest's MSG_DNA_ADD that
// arrives while it has no creature (editor, load) and pays it when it has one again.
// `coop.fix core.away_dna off`: the old behaviour (tests).
bool g_away_dna_fix = true;
COOP_FIX("core.away_dna", "BUG-0151", g_away_dna_fix, "core: DNA a guest earns while the host is away is kept");
float g_dna_owed = 0;     // host: guests' DNA that arrived without an avatar
uint32_t g_dna_owed_paid; // tests
// BUG-0231 (GitHub #165, dna_route.h row 2): DNA a guest earns while it plays another world of
// the host's stage stays in that world (0.7.3.0 sent it to the host's world).
uint32_t g_dna_own = 0; // tests: gains kept in the guest's own world (row 2)
// BUG-0466 (class G6, BUG-0423): a guest's own DNA spend (its own creature editor of a vote: the
// editor takes the price from the game's GetEvolutionPoints when it closes) was never sent: the guest
// forwards only gains (on_dna), so the host's next total (sent on a change and every 2 s) put the
// guest's DNA back to what it had before the editor (free DNA, and the host's total never paid for
// the guest's parts). Now a guest samples its own DNA while it plays the host's world: a drop it
// made itself (not a host total, not a load) is a spend; it is kept across the editor (the stage
// epoch: an editor and its Done keep it, a load does not), held while the host is away, sent once
// to the host as a negative MSG_DNA_ADD (an older host ignores it: d > 0 only) and taken off the
// party's total there (never below 0).
float g_dna_seen = 0;           // guest: the game's DNA after the last sample / host total / own gain
bool g_dna_seen_ok = false;
static uint32_t g_dna_seen_epoch = 0;  // the Creature stage epoch of g_dna_seen (a load starts a new one)
float g_dna_spend = 0;          // guest: spent here, not sent yet (<= 0)
static uint32_t g_dna_spend_world = 0; // the world it was spent in
float g_dna_spend_owed = 0;     // host: guests' spends that arrived while it had no creature (<= 0)
uint32_t g_dna_spends_sent = 0, g_dna_spends_got = 0; // tests
void feed_kind(const std::string& s, const char* kind);
// the DNA cause ledger and its notices (dna_ledger.h; player request 2026-11-01, BUG-0351)
std::vector<dna_ledger::Part> g_dna_out; // host: the causes since the last total sent
dna_ledger::Notices g_dna_notices;
uint32_t g_dna_noticed;
void dna_note(const dna_ledger::Part& p, uint8_t me)
{
    if (dna_ledger::worth_a_notice(p, me))
        g_dna_notices.add(p.cause, p.player, p.amount, H->time_seconds());
}
// host: a gain applied here now (before the game adds it)
void dna_host_gain(const dna_ledger::Part& p, float before)
{
    LOG("dna: %.0f -> %.0f cause=%s", before, before + p.amount, dna_ledger::causes_text({p}).c_str());
    if (!g_sess.connected())
        return;
    bool merged = false;
    for (auto& o : g_dna_out)
        if (o.cause == p.cause && o.player == p.player) {
            o.amount += p.amount;
            merged = true;
        }
    if (!merged && g_dna_out.size() < 16)
        g_dna_out.push_back(p);
    dna_note(p, 0);
}
void dna_notices_tick(double now)
{
    for (auto& p : g_dna_notices.due(now)) {
        bool mine = g_sess.connected() && p.player == g_sess.my_id();
        feed_kind(dna_ledger::notice_text(p, g_sess.name_of(p.player), mine), "good");
        g_dna_noticed++;
    }
}

bool guest_elsewhere();
bool guest_space_other();
// BUG-0510 (dna_pace.h): every gain entering the party's shared DNA / food is scaled by 1/N, N = the
// players playing the host's world now, so a party evolves at the single-player pace. The host
// decides (coop.ini [coop] dna_pace, 0 = the full sum) and announces the divisor with its totals
// (MSG_DNA's and MSG_CELL_FOOD's optional tail); each gain is scaled once, where its game made it.
int g_pace_heard = 1;          // guest: the host's divisor (1 until a total with one, or from an older host)
uint32_t g_paced = 0;          // tests: gains scaled here
float g_paced_in = 0, g_paced_out = 0; // tests: their sum before / after
extern uint32_t g_world;
// host: the players playing its world now (itself and the guests in its stage and world)
static int pace_playing()
{
    int n = 1;
    if (!g_world)
        return n;
    for (auto& [id, r] : g_remotes) {
        if (r.echo || r.buf.empty())
            continue;
        const auto& ls = r.buf.latest();
        if (ls.mode == g_mode && ls.world == g_world)
            n++;
    }
    return n;
}
// the divisor the host announces
int dna_pace_announce()
{
    return dna_pace::divisor(settings::on(settings::kDnaPace), pace_playing());
}
// the divisor for a gain of this machine's game now (1: whole)
int pace_div()
{
    if (dna_pace::g_bypass || !g_sess.connected() || own_progress::active()) // BUG-1469: own bars, a solo game's pace
        return 1;
    if (g_sess.role() == net::Role::Host)
        return dna_pace_announce();
    if (guest_elsewhere() || g_host_away)
        return 1; // its own world / playing alone meanwhile: not the party's total
    return std::max(1, g_pace_heard);
}
float dna_pace_gain(float amount)
{
    int div = pace_div();
    float out = dna_pace::share(amount, div);
    if (out != amount) {
        g_paced++;
        g_paced_in += amount;
        g_paced_out += out;
        trace::line("dna", "pace 1/%d: +%.1f -> +%.1f", div, amount, out);
    }
    return out;
}
dna_pace::Carry g_pace_carry;
int dna_pace_meal(int amount) // Cell: an own meal's food points
{
    int div = pace_div();
    int out = g_pace_carry.take(amount, div);
    if (out != amount) {
        g_paced++;
        g_paced_in += (float)amount;
        g_paced_out += (float)out;
        trace::line("dna", "pace 1/%d: meal +%d -> +%d", div, amount, out);
    }
    return out;
}
void dna_pace_heard(int div) { g_pace_heard = std::clamp(div, 1, 255); }

bool on_dna(float& delta)
{
    bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
    if (own_progress::active()) { // BUG-1469: this game's own DNA and bar (each game pays its own goal card too)
        if (g_sess.role() == net::Role::Host && delta > 0 && g_mode == modes::kCreature) {
            // the game paid our avatar for a guest's deed: inside an Actor scope (WP-H1: a guest's hit landing as ours, a
            // guest's social win) or within the hunt credit of a guest's kill (npc.cpp)
            uint8_t who = actor::current();
            uint8_t cause = own_progress::kGainActor;
            if (who == actor::kNone) {
                auto part = dna_ledger::g_credit.gain(delta, H->time_seconds());
                if (part.cause == dna_ledger::Cause::Hunt)
                    who = part.player, cause = own_progress::kGainHunt;
            }
            if (who != actor::kNone && who != g_sess.my_id() && net::is_player(g_sess, who)) {
                own_progress::give_gain(who, delta, cause);
                g_own_deeds_given++;
                LOG("dna: +%.0f of %s's deed goes to %s (own progress)", delta, g_sess.name_of(who).c_str(),
                    g_sess.name_of(who).c_str());
                return false;
            }
        }
        if (delta > 0) {
            g_own_gains_kept++;
            // WP-R1 (BUG-1472): a revive's progress debt is paid from this game's own next gains (DNA and bar)
            delta = own_progress::pay_debt(delta);
            if (!(delta > 0))
                return false;
        }
        if (g_sess.role() == net::Role::Host && delta != 0 && g_mode == modes::kCreature)
            LOG("dna: %.0f -> %.0f cause=own", game::dna(), game::dna() + delta);
        return true;
    }
    auto g = dna_route::guest_gain(guest, delta > 0, guest_elsewhere(), g_away_dna_fix && g_host_away);
    if (g == dna_route::Gain::Own) {
        g_dna_own++;
        trace::ev("dna", "own", "+%.0f kept in this world (the host plays another one)", delta);
        return true;
    }
    if (g == dna_route::Gain::Solo) {
        if (g_solo_dna <= 0)
            g_solo_mode = g_mode; // the stage it is earned in (BUG-0233)
        g_solo_dna += delta; // nobody to send it to: kept here, given to the pack on the host's return
        g_dna_seen += delta;  // BUG-0466: applied here now, not a spend
        return true;
    }
    if (social::goal_dna(delta))
        return false; // a goal card's reward: the host pays it into the shared pool once
    if (g == dna_route::Gain::Local) {
        if (g_sess.role() == net::Role::Host && delta > 0 && g_mode == modes::kCreature)
            delta = dna_pace_gain(delta); // BUG-0510: the host's own gain (after the goal card: those stay whole)
        if (g_sess.role() == net::Role::Host && delta != 0 && g_mode == modes::kCreature)
            dna_host_gain(dna_ledger::g_credit.gain(delta, H->time_seconds()), game::dna());
        return true;
    }
    if (g_host_away) { // core.away_dna off: the old order (a goal card's reward went to the host)
        g_solo_dna += delta;
        return true;
    }
    net::Writer w;
    w.f32(dna_pace_gain(delta)); // BUG-0510: its share, with the host's divisor (the host adds it whole)
    g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
    g_dna_fwd++;
    return false; // applied when the host's total comes back
}

// BUG-0829: the DNA a guest earned while the host was away is in its game already (the bar moved) and goes to the host
// at the away end. A host total sent before the host added it (the 2 s resend, a change of the host's own) arrived after
// it: the guest's DNA went down to it (SetEvolutionPoints, the bar stays) and up again with the next total
// (AddEvolutionPoints: the bar moved a second time, the guest's bar ahead of the host's by that DNA). Now, after the
// away end, a total without the host's "earned by this player" cause is not applied until one with it came (at most
// kSoloAwait s). `coop.fix core.solo_await off`: every total applied (0.7.11).
bool g_solo_await_fix = true;
COOP_FIX("core.solo_await", "BUG-0829", g_solo_await_fix, "creature: DNA a guest earned while the host was away moves its evolution bar once (older host totals are not applied over it)");
float g_solo_await = 0;          // the solo DNA sent at the away end, its total not seen yet
double g_solo_await_until = 0;
uint32_t g_solo_stale_totals = 0; // tests: host totals older than the solo DNA, not applied

// BUG-0466: the guest's own spends (see g_dna_spend above)
void dna_spend_sample()
{
    if (g_sess.role() != net::Role::Client || !g_sess.connected()) {
        g_dna_seen_ok = false;
        g_dna_spend = 0;
        return;
    }
    if (game::mode_id() != modes::kCreature)
        return; // an editor, a load: the last value is kept (an editor's price shows when it closes;
                // a world held for the editor's end may load before the avatar is back: no avatar asked)
    uint32_t epoch = game::stage_epoch(modes::kCreature);
    if (guest_elsewhere() || !g_dna_seen_ok || epoch != g_dna_seen_epoch) {
        g_dna_seen = game::dna(); // another world or a load: its value is not a change of ours
        g_dna_seen_ok = !guest_elsewhere();
        g_dna_seen_epoch = epoch;
        return;
    }
    float cur = game::dna(), d = cur - g_dna_seen;
    if (d < -0.5f) {
        if (g_dna_spend == 0)
            g_dna_spend_world = g_stage_world; // (an editor has no world of its own)
        g_dna_spend += d;
        LOG("dna: spent %.0f here (%.0f -> %.0f): goes to the host", -d, g_dna_seen, cur);
    }
    g_dna_seen = cur;
}

// a spend the mod made for the game (the own editor's price): counted at once, not left to the
// sample (a host total can arrive before the next tick: it would undo it)
void dna_spend_note(float d)
{
    if (g_sess.role() != net::Role::Client || !g_sess.connected() || d >= 0 || own_progress::active()) // BUG-1469: ours
        return;
    if (g_dna_spend == 0)
        g_dna_spend_world = g_stage_world; // (an editor has no world of its own)
    g_dna_spend += d;
    if (g_dna_seen_ok)
        g_dna_seen += d;
    LOG("dna: spent %.0f here (the own editor's price): goes to the host", -d);
}

// the game's DNA was set by the mod (a host total): the next sample starts from it
void dna_spend_resync()
{
    if (g_dna_seen_ok)
        g_dna_seen = game::dna();
}

void dna_spend_tick(double now)
{
    static double last_send = -100;
    if (own_progress::active()) { // BUG-1469: a spend is the spender's own: nothing goes to the host
        g_dna_spend = 0;
        g_dna_seen_ok = false;
        return;
    }
    dna_spend_sample();
    if (g_dna_spend > -0.5f)
        return;
    if (g_dna_spend_world != g_world && g_mode == modes::kCreature && game::avatar()) {
        LOG("dna: a spend of %.0f made in world %08X dropped (now %08X)", -g_dna_spend, g_dna_spend_world, g_world);
        g_dna_spend = 0; // that world is gone (another save): its DNA has no total to come off
        return;
    }
    if (g_host_away || !host_here() || guest_elsewhere() || now - last_send < 0.25)
        return; // held: the host has no creature now (its own editor) or plays elsewhere
    net::Writer w;
    w.f32(g_dna_spend);
    g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
    g_dna_spends_sent++;
    last_send = now;
    g_dna_spend = 0;
}


// ---- WP-D of 0.7.16 (BUG-1291, dna_own.h): own DNA per player. The host keeps every guest's wallet (its Creature
// budget, its Cell spent count) in g_book and in the per-player store; the party's income is the host's bar step
// (own_dna_tick); a guest's spend comes off its wallet only; MSG_DNA carries each guest's own budget and the party bar.
// A guest whose mod is older than this (creature / npc family minor below 2) plays the shared pool as in 0.7.15.
bool g_own_dna_fix = true;
COOP_FIX("creature.own_dna", "BUG-1291", g_own_dna_fix,
         "creature, cell: every player has its own DNA (every gain counts for everybody, a player's editor spends only its own); off: one shared pool (0.7.15)");
bool g_dna_bar_set_fix = true;
COOP_FIX("creature.dna_bar_set", "BUG-1291", g_dna_bar_set_fix,
         "creature: a guest's own DNA is set with the game's setter and its evolution bar follows the party's (off: through AddEvolutionPoints, the bar runs ahead after a reload)");
// BUG-1393: a creature's respawn drops the player's evolution bar to its whole number (the game's own rule, seen on
// every death of lab 20261121-003202 mem_census: 19.2 -> 19.0, 36.9 -> 36.0, 53.5 -> 53.0). A guest's own drop is
// undone by the next total (the bar up to the party's); the host's drop is the party's, but a guest's bar never went
// down: it stayed up to 0.9 ahead until the next gain (sync-check "bar: host 36.0, guest 36.9" for 30 s). On: a guest
// follows a setback of the party's bar under one point. Off: the bar never down (0.7.16 before the fix).
bool g_bar_setback_fix = true;
COOP_FIX("creature.bar_setback", "BUG-1393", g_bar_setback_fix,
         "creature: when the host's creature dies the guests' evolution bars lose the same part of a point as the host's (the party's bar)");
bool g_own_dna_store_fix = true;
COOP_FIX("creature.own_dna_store", "BUG-1291", g_own_dna_store_fix,
         "creature, cell: every guest's own DNA is kept in the host's per-player store (a reload, a host restart, a rejoin); off: a guest starts again at the host's DNA");
dna_own::Book g_book;
static uint32_t g_book_serial = 0;
std::map<uint8_t, float> g_dna_sent_to; // host: the last total sent to each guest
uint32_t g_own_firsts = 0, g_own_spends = 0, g_own_loaded = 0, g_own_totals = 0, g_own_applied = 0; // tests
bool g_own_heard = false; // guest: the host's last total was our own wallet
float g_own_bar_heard = NAN;

bool host_creature_mode();
bool own_dna_on()
{
    // all.shared_dna: 0 own wallet, 1 shared, 2 own progress (BUG-1469: get(), never on(); value 2 refused or switched
    // off plays as own wallet: own_progress::dna_mode)
    return g_sess.role() == net::Role::Host && own_progress::dna_mode() == own_progress::kOwnWallet &&
           dna_own::active(g_own_dna_fix, false, host_creature_mode());
}
// host: the mod of player `id` reads its own wallet (family `fam` minor 2 or newer)
bool dna_peer_reads(uint8_t id, net::Family fam)
{
    bool has = false;
    std::vector<net::FamilyRev> v;
    if (!g_sess.peer_families(id, has, v))
        return false;
    const net::FamilyRev* t = nullptr;
    for (auto& x : v)
        if (x.family == (uint8_t)fam)
            t = &x;
    const net::FamilyInfo* mine = net::family_info((uint8_t)fam);
    return dna_own::peer_reads(has, t != nullptr, t ? t->rev : 0, t ? t->minor : 0, mine ? mine->rev : 0);
}
// host: guest `id` has its own wallet now
bool dna_own_wallet(uint8_t id, net::Family fam)
{
    return id != 0 && own_dna_on() && dna_peer_reads(id, fam);
}
std::string dna_wallet_key(uint8_t id)
{
    std::string k = store::key_of(id);
    return k.empty() ? "id:" + std::to_string(id) : k; // (no stable key: kept in memory only)
}
// host: the world of the stage played (an editor keeps it)
static uint32_t wallet_world() { return g_stage_world ? g_stage_world : g_world; }

// host: guest `id`'s own Creature budget (first sight: the host's DNA now)
float dna_own_budget(uint8_t id)
{
    bool made = false;
    float b = g_book.budget(dna_wallet_key(id), wallet_world(), game::dna(), &made);
    if (made) {
        g_own_firsts++;
        LOG("dna: %s's own DNA starts at %.0f (the host's now, world %08X)", g_sess.name_of(id).c_str(), b, wallet_world());
    }
    return b;
}
// host: a guest's own spend (d < 0): its wallet only
void dna_own_spend(uint8_t id, float d)
{
    float before = dna_own_budget(id);
    float after = g_book.add(dna_wallet_key(id), wallet_world(), d, game::dna());
    g_own_spends++;
    LOG("dna: %s spent %.0f of its own DNA: %.0f -> %.0f (the host's stays %.0f)", g_sess.name_of(id).c_str(), -d, before,
        after, game::dna());
}
// host, Cell: guest `id`'s own spent count (first sight: the host's count)
int dna_own_cell_spent(uint8_t id, int host_spent)
{
    bool made = false;
    int v = g_book.spent(dna_wallet_key(id), wallet_world(), host_spent, &made);
    if (made) {
        g_own_firsts++;
        LOG("dna: %s's own Cell DNA starts with the host's spent count %d (world %08X)", g_sess.name_of(id).c_str(), v,
            wallet_world());
    }
    return v;
}
int dna_own_cell_spend(uint8_t id, int spent, int host_spent, int max_spent)
{
    int before = dna_own_cell_spent(id, host_spent);
    int after = g_book.spend_cell(dna_wallet_key(id), wallet_world(), spent, host_spent, max_spent);
    g_own_spends++;
    LOG("dna: %s's own cell editor spent %d: its spent count %d -> %d (the host's stays %d)", g_sess.name_of(id).c_str(),
        spent, before, after, host_spent);
    return after;
}

// the store: every guest's wallet beside the host's save ("dna.own" v1), read when the host's world opens
static void book_load()
{
    pstore::Store* s = store::world();
    uint32_t ser = store::serial();
    if (!s || ser == g_book_serial)
        return;
    g_book_serial = ser;
    std::map<std::string, dna_own::Wallet> keep;
    for (auto& [k, x] : g_book.w) // a wallet the store has no record of (a guest seen since the last save) stays
        if (!s->get(k, dna_own::kStoreFeature))
            keep[k] = x;
    g_book.clear();
    if (!g_own_dna_store_fix) {
        LOG("dna: the host's world opened: every guest's own DNA starts again (creature.own_dna_store off)");
        return;
    }
    g_book.w = keep;
    int n = 0;
    for (auto& [k, p] : s->players) {
        const pstore::Record* r = s->get(k, dna_own::kStoreFeature);
        dna_own::Wallet x;
        if (r && r->ver == dna_own::kStoreVersion && dna_own::decode(r->data, x)) {
            g_book.w[k] = x;
            n++;
            g_own_loaded++;
        }
    }
    if (n)
        LOG("dna: the guests' own DNA of %d players read from the store of %s", n, store::world_name().c_str());
}
static void book_save(pstore::Store& s, int64_t now)
{
    if (!g_own_dna_store_fix || g_sess.role() != net::Role::Host)
        return;
    for (auto& [k, x] : g_book.w) {
        if (k.rfind("s:", 0) != 0)
            continue; // no stable key: memory only
        std::string name;
        for (auto& p : g_sess.players())
            if (p.id != 0 && store::key_of(p.id) == k)
                name = p.name;
        s.put(k, name, dna_own::kStoreFeature, dna_own::kStoreVersion, dna_own::encode(x), now);
    }
}
static bool g_book_reg = store::add({dna_own::kStoreFeature, book_save,
                                     [](const std::string& from, const std::string& to) { g_book.adopt(from, to); }});

// every frame (before the away state, so a payment of owed DNA in this frame is seen in the next): the host's
// world store and the party's income
void own_dna_tick()
{
    if (g_sess.role() != net::Role::Host)
        return;
    book_load();
    if (!own_dna_on() || !g_sess.connected()) {
        g_book.restart();
        return;
    }
    if (g_mode != modes::kCreature || !game::avatar())
        return; // an editor, a load: the baseline is kept (no gain there); a load opens a new store
    float g = g_book.observe(game::evolution_bar(), wallet_world(), store::serial());
    if (g > 0)
        trace::line("dna", "party +%.1f -> every own DNA of world %08X (%d wallets)", g, wallet_world(), (int)g_book.w.size());
}

// BUG-1341 (lab 20261120-041304 dna_notice, dna_away): the host's DNA totals go out in the frame's totals phase, the
// income observer above runs later in the same frame (the stage phase). A party gain of frame N went to the guests in
// frame N+1 with its causes and every guest's OLD wallet (only the bar moved: guest log "dna: own 0 -> 0, bar 0.0 ->
// party 3.5 cause=hunt:1+4"), the wallet's step came one total later without its cause ("own 0 -> 4 ... cause=unknown").
// On: the totals phase credits the gain first (the same observer: a second call in one frame sees no step), only while
// the host plays its creature in this frame and the last (no load, no editor in between: the world and the store are
// the ones the observer knows). Off: the credit waits for the stage phase (0.7.16 before the fix).
bool g_own_dna_cause_fix = true;
COOP_FIX("creature.own_dna_cause", "BUG-1341", g_own_dna_cause_fix,
         "creature: a party gain reaches every guest's own DNA in the same total as its cause (off: one total later, cause unknown)");
uint32_t g_own_early = 0; // tests: gains credited from the totals phase
void own_dna_credit_now()
{
    if (!g_own_dna_cause_fix || g_sess.role() != net::Role::Host || !own_dna_on() || !g_sess.connected())
        return;
    if (g_mode != modes::kCreature || game::mode_id() != modes::kCreature || !game::avatar())
        return;
    if (store::serial() != g_book_serial)
        return; // a store opened since the stage phase read it: that phase starts the new baseline
    float g = g_book.observe(game::evolution_bar(), wallet_world(), store::serial());
    if (g > 0) {
        g_own_early++;
        trace::line("dna", "party +%.1f -> every own DNA of world %08X (%d wallets, with the total)", g, wallet_world(),
                    (int)g_book.w.size());
    }
}

// BUG-1311: MSG_DNA is sent from the frame's totals phase, before the frame reads the game's mode (g_mode): in the first
// frame after the host's editor (or a load) the host has its avatar again while g_mode still names the editor, and every
// guest with its own wallet got the host's own DNA as a shared total (the guest's game went to the host's DNA through
// AddEvolutionPoints, whose bar step stays: a guest that had spent more than the host ran ahead of the party's bar).
// Lab 20261119-102828 evo_screens_switch_lowfps (guest "115 -> 65 cause=unknown"), 20261119-110020 evo_bar_editors.
// On: the game's mode now decides. Off: g_mode (0.7.16 before the fix).
bool g_own_dna_edge_fix = true;
COOP_FIX("creature.own_dna_edge", "BUG-1311", g_own_dna_edge_fix,
         "creature: the host's first DNA total after its editor or a load is each guest's own (not the host's DNA for one frame)");
uint32_t g_own_edge_strays = 0; // tests: totals of the host's DNA sent to a guest with its own wallet at a mode edge

// host: the total and the tail for guest `id` (MSG_DNA is sent per guest)
float dna_total_for(uint8_t id, float host_total, dna_own::Tail& tail)
{
    tail = {};
    tail.bar = game::evolution_bar();
    tail.world = wallet_world();
    bool creature = g_own_dna_edge_fix ? game::mode_id() == modes::kCreature : g_mode == modes::kCreature;
    if (!creature && game::mode_id() == modes::kCreature && dna_own_wallet(id, net::Family::creature))
        g_own_edge_strays++; // (the fix off: the stray total of BUG-1311)
    if (!creature || !dna_own_wallet(id, net::Family::creature))
        return host_total;
    tail.flags = dna_own::kFlagOwn;
    return dna_own_budget(id);
}

// BUG-1310: the lab's gates (the sync-check, the trace line's dna_own=, coop.wstate's wallet rows) compared every guest's
// DNA with the host's own; with own wallets the two differ on purpose. The host names what each guest's game must show:
// its wallet (first sight not yet: the host's DNA now, what the wallet will start at). False: that guest plays the shared
// pool (the setting, creature.mode host, an older mod) and is compared with the host's DNA as before. Off: never (every
// guest compared with the host's DNA, 0.7.16 before the fix).
bool g_own_dna_gates_fix = true;
COOP_FIX("diag.own_dna", "BUG-1310", g_own_dna_gates_fix,
         "sync-check, monitor, detector: a guest with its own DNA is compared with the host's record of its wallet (the bar stays the party's)");
bool dna_own_expect(uint8_t id, float& v)
{
    if (!g_own_dna_gates_fix || g_sess.role() != net::Role::Host || !dna_own_wallet(id, net::Family::creature))
        return false;
    std::string k = dna_wallet_key(id);
    v = g_book.has_budget(k, wallet_world()) ? g_book.w[k].creature.value : std::max(0.f, game::dna());
    return true;
}
bool dna_own_expect_cell(uint8_t id, int host_spent, int& v)
{
    if (!g_own_dna_gates_fix || g_sess.role() != net::Role::Host || !dna_own_wallet(id, net::Family::npc))
        return false;
    auto it = g_book.w.find(dna_wallet_key(id));
    v = it != g_book.w.end() && it->second.cell.world == wallet_world() ? (int)it->second.cell.value : std::max(0, host_spent);
    return true;
}
// host, the trace line (BUG-1310): " dna_own=<id>:<v>,..." of every guest with its own wallet (Creature), "" for none
std::string dna_own_field()
{
    if (g_mode != modes::kCreature)
        return "";
    std::string o;
    char b[32];
    for (auto& p : g_sess.players()) {
        float v;
        if (p.id == 0 || !dna_own_expect(p.id, v))
            continue;
        snprintf(b, sizeof b, "%s%u:%.0f", o.empty() ? " dna_own=" : ",", p.id, v);
        o += b;
    }
    return o;
}

void dna_send_spend(float amount)
{
    if (g_sess.role() != net::Role::Client || !g_sess.connected() || amount <= 0)
        return;
    net::Writer w;
    w.f32(-amount);
    g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
    g_dna_spends_sent++;
}

std::string own_dna_status()
{
    std::string o;
    char b[260];
    snprintf(b, sizeof b, "own dna: on=%d fix=%d bar_set=%d store=%d heard=%d bar_heard=%.1f firsts=%u spends=%u loaded=%u totals=%u applied=%u steps=%u credited=%.1f edge=%d strays=%u gates=%d cause=%d early=%u\n",
             (int)own_dna_on(), (int)g_own_dna_fix, (int)g_dna_bar_set_fix, (int)g_own_dna_store_fix, (int)g_own_heard,
             g_own_bar_heard, g_own_firsts, g_own_spends, g_own_loaded, g_own_totals, g_own_applied, g_book.steps,
             g_book.credited, (int)g_own_dna_edge_fix, g_own_edge_strays, (int)g_own_dna_gates_fix, (int)g_own_dna_cause_fix,
             g_own_early);
    o += b;
    for (auto& p : g_sess.players()) {
        if (p.id == 0 || g_sess.role() != net::Role::Host)
            continue;
        std::string k = dna_wallet_key(p.id);
        auto it = g_book.w.find(k);
        snprintf(b, sizeof b, "wallet %u '%s': reads_own=%d creature=%s%.1f cell=%s%d\n", p.id, p.name.c_str(),
                 (int)dna_peer_reads(p.id, net::Family::creature),
                 it != g_book.w.end() && it->second.creature.world == wallet_world() ? "" : "(other world) ",
                 it != g_book.w.end() ? it->second.creature.value : -1.f,
                 it != g_book.w.end() && it->second.cell.world == wallet_world() ? "" : "(other world) ",
                 it != g_book.w.end() ? (int)it->second.cell.value : -1);
        o += b;
    }
    return o;
}

} // namespace coop_core
