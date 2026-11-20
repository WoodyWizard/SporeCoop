// DNA routing of the coop core (B2 of 0.7.15: moved out of coop.cpp, docs/audit-0.7.15/core.md CORE-2): the DNA totals'
// state, the cause ledger's notices, the single-player pace (dna_pace.h), where a gain goes (on_dna, dna_route.h), the
// DNA a guest earned while the host was away (BUG-0829) and a guest's own spends (BUG-0466). The portable rules are in
// dna_route.h, dna_ledger.h and dna_pace.h; the messages are handled in the session callbacks, the totals are sent from
// the frame, the away state is coop.cpp's host_away_tick.
#include "coop_state.h"
#include "own_progress.h"

namespace coop_core {

// BUG-9265 (own progress, own_progress.h): with the session setting all.own_progress every player's DNA and bar are
// their own: a guest keeps its gains (nothing forwarded), the host sends no total and pays a guest's hunt to that guest,
// nothing is paced (each bar fills at a solo game's pace by itself), a spend stays where it was made
uint32_t g_own_totals_ignored = 0, g_own_gains_kept = 0, g_own_hunts_given = 0; // tests (coop.progress)

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
    if (dna_pace::g_bypass || !g_sess.connected() || own_progress::active()) // BUG-9265: own bars, a solo game's pace
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
    if (own_progress::active()) { // BUG-9265: this game's own DNA and bar
        if (g_sess.role() == net::Role::Host && delta > 0 && g_mode == modes::kCreature) {
            const auto part = dna_ledger::g_credit.gain(delta, H->time_seconds());
            if (part.cause == dna_ledger::Cause::Hunt && part.player != g_sess.my_id()) {
                // a guest's kill of one of our animals: the game paid it to our avatar, it is that guest's
                own_progress::give_gain(part.player, delta);
                g_own_hunts_given++;
                LOG("dna: +%.0f of %s's hunt goes to %s (own progress)", delta, g_sess.name_of(part.player).c_str(),
                    g_sess.name_of(part.player).c_str());
                return false;
            }
        }
        if (delta > 0)
            g_own_gains_kept++;
        if (guest)
            g_dna_seen += delta; // (BUG-0466's sampling: not a spend)
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
    if (g_sess.role() != net::Role::Client || !g_sess.connected() || d >= 0)
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
    if (own_progress::active()) { // BUG-9265: a spend is the spender's own
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

} // namespace coop_core
