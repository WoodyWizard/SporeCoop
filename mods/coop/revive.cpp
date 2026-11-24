// Revive a downed player in the Creature stage (revive.h; WP-R1 of 0.7.17, BUG-1472). Ported from PR #214's
// creature_rescue.cpp by IltonBJSilva (its knockout, revive, party wipe, the pausing caption's kill_retries), on our
// pack rescue (teamplay.cpp stays the first line), with the price charged to the downed player (revive_logic.h
// price_for) and the old-peer guards of creature 1.3. docs/03-gameplay.md#gameplay-revive,
// docs/re/creature.md#creature-revive.
#include "revive.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "fixes.h"
#include "game.h"
#include "give_up.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/interp.h"
#include "own_progress.h"
#include "revive_logic.h"
#include "settings.h"
#include "world.h"

namespace revive {

// the 1 hp hold: a downed creature takes no hit, stays at 1 health and where it fell (off: only the downing hit is
// dropped; the next one kills it: the twin creature_revive_hold_off)
static bool g_hold_fix = true;
COOP_FIX("creature.revive_hold", "BUG-1472", g_hold_fix,
         "creature, creature.revive: a downed creature is held at 1 health where it fell and takes no hit until it is revived or its window ends");
// the price of a death after a knockout is the downed player's own (its wallet, the shared pool, its progress debt);
// off: PR #214's rule, 10 % of the host's DNA for every death
static bool g_own_price_fix = true;
COOP_FIX("creature.revive_debt", "BUG-1472", g_own_price_fix,
         "creature, creature.revive: a downed player who is not revived pays its own price (10 % of its own DNA, or a debt on its bar under own progress), not the host");

static const SkHost* H;
static SkMod* M;
static Ctx C;

// ---- this game's own creature
static void* g_body;            // the own creature that is down (null: not down)
static uint32_t g_serial;       // the knockout numbers of this game (counting up)
static uint32_t g_local_id;     // the knockout of g_body (kept after a death until the host's verdict)
static net::Vec3 g_anchor;      // where it fell
static net::Quat g_facing;
static double g_local_start, g_last_request, g_protect_until, g_test_give_up, g_give_up_sent;
// PR #214's kill_retries: a verdict "not revived" while the game takes no damage (a caption that pauses the game, the
// brain upgrade's: the lethal hit did nothing on its lane D, 2026-11-20): held still, the hit again every second
static void* g_kill_body;
static double g_kill_next, g_kill_until;
// ---- host
static std::array<Watch, kMaxPlayers> g_watch;
static std::array<net::Vec3, kMaxPlayers> g_watch_pos{};
static double g_wipe_since = -1, g_lines_next;
static bool g_lines_open;
// ---- guest: the host's lines
struct Remote {
    Line l;
    double until = 0;
};
static std::vector<Remote> g_remote;
// ---- tests (coop.revive)
static uint32_t g_downs, g_revives, g_expired, g_gave_up, g_died_other, g_wipes, g_blows_held, g_requests, g_lines_sent,
    g_lines_rx, g_lines_bad, g_results_rx, g_self_ended, g_kill_retries, g_old_refused;
static Verdict g_last_verdict; // the last verdict on this game's own knockout
static Price g_last_charge;    // host: the last price charged (anybody's)
static std::string g_last = "-";

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client; }
static bool in_creature() { return game::mode_id() == modes::kCreature; }
static uint8_t me() { return C.sess ? C.sess->my_id() : 0; }

bool on() { return settings::on(settings::kRevive); }

static void note(const std::string& text, bool everybody)
{
    g_last = text;
    H->log(M, SK_INFO, "revive: %s", text.c_str());
    if (everybody && C.notice)
        C.notice(text);
    else if (!everybody && C.feed)
        C.feed(text);
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
}

// the host's mod reads MSG_RESCUE (creature 1.3): a guest goes down only then (an older host would never answer)
static bool host_reads()
{
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess && C.sess->host_families(has, f) && peer_reads(f, has);
}
static bool peer_reads_id(uint8_t id)
{
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess && C.sess->peer_families(id, has, f) && peer_reads(f, has);
}

static void send(uint8_t to, Kind kind, uint32_t id)
{
    net::Writer w;
    w.u8(kind);
    w.u32(modes::current_world());
    w.u32(id);
    C.sess->send_reliable_to(to, net::MSG_RESCUE, w);
}

static void forget()
{
    g_body = nullptr;
    g_local_id = 0;
    g_protect_until = 0;
    g_test_give_up = 0;
    g_kill_body = nullptr;
    g_watch = {};
    g_wipe_since = -1;
    g_lines_open = false;
    g_remote.clear();
}
LIFECYCLE_HOOK("revive", lifecycle::kSessionEnd | lifecycle::kWorldChange, -46, [](const lifecycle::Ctx&) { forget(); }, false);

// registered below (kPlayerLeave, BUG-1533); with core.player_events off session_glue.cpp's leave callback calls it
void on_player_leave(uint8_t id)
{
    if (id < kMaxPlayers && g_watch[id].active) {
        H->log(M, SK_INFO, "revive: player %u left: its window ends without a price", id);
        g_watch[id] = {};
        g_lines_next = 0;
    }
}
// BUG-1533: mod state only (no game call)
LIFECYCLE_HOOK("revive.player", lifecycle::kPlayerLeave, 10, [](const lifecycle::Ctx& c) { on_player_leave(c.player); }, false);

bool down() { return g_body != nullptr || g_kill_body != nullptr; }

bool pending()
{
    if (down())
        return true;
    for (const auto& w : g_watch)
        if (w.active)
            return true;
    const double now = H ? H->time_seconds() : 0;
    for (const auto& x : g_remote)
        if (x.until > now)
            return true;
    return false;
}

static bool standing(const net::PlayerState& p) { return p.health > 0 && !(p.flags & net::PF_DEAD); }
static bool alive(void* c) { return c && game::health(c) > 0 && !game::is_dead(c); }

// ---- the own creature
static void hold(void* body, bool keep_health = true)
{
    if (!g_hold_fix)
        return;
    if (keep_health)
        game::set_health(body, 1); // (the pack regen and the nest's heal would lift it)
    game::stop(body);
    // (not during a cinematic: the game places its actors itself, BUG-1173's class)
    if (!world::cinematic_playing() && net::dist(g_anchor, game::position(body)) > kAnchorM)
        game::teleport(body, g_anchor, g_facing);
}

static std::string price_text(const Price& p)
{
    char b[120];
    switch (p.kind) {
    case kPriceWallet:
        snprintf(b, sizeof b, "%.0f DNA of their own", p.amount);
        break;
    case kPricePool:
        snprintf(b, sizeof b, "%.0f DNA of the shared pool", p.amount);
        break;
    case kPriceDebt:
        snprintf(b, sizeof b, "a debt of %.0f DNA on their next gains", p.amount);
        break;
    default:
        snprintf(b, sizeof b, "nothing (no DNA)");
    }
    return b;
}

// the verdict on this game's own knockout (the host's, or the guest's own when no host answered)
static void resolve(const Verdict& v)
{
    void* body = g_body;
    g_body = nullptr;
    g_local_id = 0;
    g_last_verdict = v;
    if (!body || game::avatar() != body || !in_creature() || !alive(body))
        return; // (it died another way meanwhile: the game's death is done)
    if (v.result == R_REVIVED) {
        game::set_health(body, std::max(1.f, game::max_health(body) * kUpHealth));
        g_protect_until = H->time_seconds() + kProtectS;
        if (C.feed)
            C.feed("You are back on your feet.");
        return;
    }
    game::apply_damage(body, 1e6f, 0, nullptr); // the game's own death: the caption, the respawn at the nest
    if (alive(body)) { // the game takes no damage now (a pausing caption): again until it does
        g_kill_body = body;
        g_kill_next = H->time_seconds() + 1;
        g_kill_until = H->time_seconds() + 120;
    }
}

static void kill_tick(void* avatar, double now)
{
    if (!g_kill_body)
        return;
    // Also stop if the creature was healed/respawned into a fresh body reusing the pointer (e.g. baby_game).
    if (g_kill_body != avatar || !alive(avatar) || game::health(avatar) > 2.f || now > g_kill_until) {
        g_kill_body = nullptr;
        return;
    }
    hold(avatar, false); // (not its health: 1 every frame undid the game's death of the lethal hit, PR #214 lane D)
    if (world::cinematic_playing() || now < g_kill_next)
        return;
    g_kill_next = now + 1;
    g_kill_retries++;
    game::apply_damage(avatar, 1e6f, 0, nullptr);
}

bool guard(void* avatar, float& damage)
{
    if (!g_hold_fix || !avatar || damage <= 0 || !C.sess || !C.sess->connected())
        return false;
    if ((g_body && avatar == g_body) || (g_kill_body && avatar == g_kill_body)) { // down: no hit lands
        damage = 0;
        g_blows_held++;
        return true;
    }
    if (H->time_seconds() < g_protect_until && avatar == game::avatar()) { // just revived
        damage = 0;
        g_blows_held++;
        return true;
    }
    return false;
}

bool down_now(void* avatar, float& damage)
{
    if (!on() || !C.sess || !C.sess->connected() || !avatar || damage <= 0 || g_body || g_kill_body)
        return false;
    HitIn in;
    in.lethal = lethal(damage, game::health(avatar));
    if (!in.lethal || !in_creature() || avatar != game::avatar()) // (the common case: no partner list below)
        return false;
    in.revive = true;
    in.partner_here = C.peers && !C.peers().empty();
    in.host_reads = is_host() || host_reads();
    if (catch_hit(in) != Catch::Down) {
        if (in.partner_here && !in.host_reads)
            g_old_refused++; // an older host: the game's death, as before
        return false;
    }
    const double now = H->time_seconds();
    if (++g_serial == 0)
        ++g_serial;
    g_body = avatar;
    g_local_id = g_serial;
    g_anchor = game::position(avatar);
    g_facing = game::orientation(avatar);
    g_local_start = now;
    g_last_request = now;
    const float hp = game::health(avatar), hit = damage;
    game::set_health(avatar, 1);
    game::stop(avatar);
    damage = 0;
    g_downs++;
    char b[200];
    snprintf(b, sizeof b, "%s is down! Stand next to them within %d s to revive them.",
             C.sess->name_of(me()).c_str(), (int)kWindowS);
    note(b, true);
    if (C.feed)
        C.feed("You are down: a partner standing next to you revives you. Hold G to give up.");
    H->log(M, SK_INFO, "revive: own creature down (knockout %u, hit %.1f on %.1f health)", g_local_id, hit, hp);
    if (is_host()) {
        g_watch[me()] = {g_local_id, true, now, -1, false};
        g_lines_next = 0;
    } else {
        send(0, K_DOWN, g_local_id);
        g_requests++;
    }
    return true;
}

// ---- host
static Price charge(uint8_t v)
{
    PriceIn in;
    in.own_price = g_own_price_fix;
    in.dna_mode = C.dna_mode ? C.dna_mode() : 0;
    in.pool_dna = game::dna();
    in.own_wallet = C.has_wallet && C.has_wallet(v);
    if (v == me())
        in.player_dna = game::dna();
    else if (in.dna_mode == 2) {
        float d = 0;
        in.player_dna = own_progress::live_dna(v, d) ? d : 0.f;
    } else if (in.own_wallet && C.wallet)
        in.player_dna = C.wallet(v);
    Price p = price_for(in);
    switch (p.kind) {
    case kPricePool:
        game::dna_set_raw(game::dna() - p.amount); // the host's total (shared: every game follows it)
        break;
    case kPriceWallet:
        if (v == me())
            game::dna_set_raw(game::dna() - p.amount); // the host's own DNA (own wallet: nobody else's)
        else if (C.charge_wallet)
            C.charge_wallet(v, -p.amount);
        break;
    case kPriceDebt:
        own_progress::charge_debt(v, p.amount);
        break;
    default:
        break;
    }
    g_last_charge = p;
    H->log(M, SK_INFO, "revive: the price of player %u's death: kind %u, %.0f DNA (mode %d, own price %d)", v,
           (unsigned)p.kind, p.amount, in.dna_mode, (int)g_own_price_fix);
    return p;
}

static void send_lines(double now, bool any)
{
    std::vector<Line> lines;
    for (uint8_t p = 0; p < kMaxPlayers; ++p) {
        const Watch& w = g_watch[p];
        if (!w.active)
            continue;
        Line l;
        l.player = p;
        l.tenths = tenths_left(seconds_left(w, now, kWindowS));
        l.revive = (uint8_t)std::lround(revive_progress(w, now) * 100);
        lines.push_back(l);
    }
    net::Writer wr;
    wr.u8(K_LINES);
    wr.u32(modes::current_world());
    write_lines(wr, lines);
    for (const auto& p : C.sess->players()) // (only to peers that read MSG_RESCUE)
        if (p.id != me() && peer_reads_id(p.id))
            C.sess->send_reliable_to(p.id, net::MSG_RESCUE, wr);
    g_lines_sent++;
    g_lines_open = any;
}

static void host_tick(double now, void* avatar, const std::vector<net::PlayerState>& peers)
{
    auto peer = [&](uint8_t id) -> const net::PlayerState* {
        for (const auto& p : peers)
            if (p.id == id)
                return &p;
        return nullptr;
    };
    const uint8_t my = me();
    // a watched guest who left this world (another place, its creature gone) ends its window without a price
    for (uint8_t v = 0; v < kMaxPlayers; ++v)
        if (v != my && g_watch[v].active && !peer(v)) {
            g_watch[v] = {};
            H->log(M, SK_INFO, "revive: player %u's window ends: it is not in this world any more", v);
        }
    const bool host_standing = avatar && !g_watch[my].active && alive(avatar);
    bool any = false, standing_any = host_standing;
    for (const auto& w : g_watch)
        any |= w.active;
    for (const auto& p : peers)
        if (standing(p) && !(p.id < kMaxPlayers && g_watch[p.id].active))
            standing_any = true;
    if (wipe_due(g_wipe_since, any, standing_any, now)) {
        for (auto& w : g_watch)
            if (w.active)
                w.begin = now - kWindowS;
        g_wipes++;
        note("The whole party is down: everyone dies and respawns at the nest.", true);
    }
    for (uint8_t v = 0; v < kMaxPlayers; ++v) {
        Watch& w = g_watch[v];
        if (!w.active)
            continue;
        if (v == my && !w.gave_up && (give_up::held(H, now) || now < g_test_give_up)) {
            w.gave_up = true;
            g_test_give_up = 0;
        }
        net::Vec3 at = v == my ? (avatar ? game::position(avatar) : g_anchor) : peer(v)->pos;
        g_watch_pos[v] = at;
        // a standing player who is not down within reach: the host's own creature (a guest's window) or any partner
        bool near = v != my && host_standing && net::dist(game::position(avatar), at) <= kReachM;
        for (const auto& p : peers)
            if (!near && p.id != v && standing(p) && !(p.id < kMaxPlayers && g_watch[p.id].active) &&
                net::dist(p.pos, at) <= kReachM)
                near = true;
        const bool gave_up = w.gave_up;
        const Step st = step(w, near, now, kWindowS);
        if (st == Step::Waiting)
            continue;
        w.active = false; // one verdict per knockout
        const std::string who = C.sess->name_of(v);
        Verdict vd;
        vd.id = w.id;
        if (st == Step::Revived) {
            g_revives++;
            vd.result = R_REVIVED;
            note(who + " was revived.", true);
        } else {
            (gave_up ? g_gave_up : g_expired)++;
            vd.result = R_EXPIRED;
            vd.price = charge(v);
            note(who + (gave_up ? " gave up" : " was not revived") + ": back at the nest. The price: " +
                     price_text(vd.price) + ".",
                 true);
        }
        if (v == my)
            resolve(vd);
        else {
            net::Writer o;
            o.u8(K_RESULT);
            o.u32(modes::current_world());
            write_verdict(o, vd);
            C.sess->send_reliable_to(v, net::MSG_RESCUE, o);
        }
        g_lines_next = 0;
    }
    any = false;
    for (const auto& w : g_watch)
        any |= w.active;
    if (any ? now >= g_lines_next : g_lines_open) {
        g_lines_next = now + 1;
        send_lines(now, any);
    }
}

// ---- guest
static void guest_tick(double now)
{
    if (!g_body)
        return;
    if (now - g_last_request >= kRequestS) { // the first request may come before the host has this guest's state
        send(0, K_DOWN, g_local_id);
        g_last_request = now;
        g_requests++;
    }
    if ((give_up::held(H, now) || now < g_test_give_up) && now - g_give_up_sent > 2) {
        g_test_give_up = 0;
        g_give_up_sent = now;
        send(0, K_GIVE_UP, g_local_id);
        H->log(M, SK_INFO, "revive: this player gave up (knockout %u)", g_local_id);
    }
    if (now - g_local_start >= kWindowS + kNoHostS) { // the host never answered (it left this world)
        g_self_ended++;
        note("Nobody revived you: back at the nest.", false);
        Verdict v;
        v.id = g_local_id;
        v.result = R_EXPIRED;
        resolve(v);
    }
}

// the game killed the downed creature another way (a path past the damage filter, the hold off): its window ends now
static void died_while_down()
{
    g_died_other++;
    H->log(M, SK_INFO, "revive: the downed creature died another way (knockout %u)", g_local_id);
    g_body = nullptr;
    if (is_host())
        g_watch[me()].gave_up = true; // (the verdict charges the price; resolve leaves the dead creature to the game)
    else
        send(0, K_GIVE_UP, g_local_id);
}

void tick(double now)
{
    if (!H || !C.sess)
        return;
    void* avatar = game::avatar();
    if (!C.sess->connected() || !in_creature() || !avatar) {
        if (g_body || g_lines_open || !g_remote.empty())
            forget();
        return;
    }
    if (g_body && g_body != avatar) { // another body (an editor, a load): the knockout is over
        g_body = nullptr;
        g_local_id = 0;
    }
    kill_tick(avatar, now);
    if (g_body) {
        if (!on()) { // switched off meanwhile: the creature is the game's again
            g_body = nullptr;
            g_watch[me()] = {};
        } else if (!alive(g_body))
            died_while_down();
        else
            hold(g_body);
    }
    const std::vector<net::PlayerState> peers = C.peers ? C.peers() : std::vector<net::PlayerState>{};
    if (is_host())
        host_tick(now, avatar, peers);
    else if (is_guest())
        guest_tick(now);
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    const uint32_t world = r.u32();
    if (!r.ok() || !C.sess || !C.sess->connected() || !in_creature() || world != modes::current_world())
        return;
    const double now = H->time_seconds();
    if (kind == K_LINES) {
        std::vector<Line> lines;
        if (from != 0 || !is_guest() || !read_lines(r, lines)) {
            g_lines_bad++;
            return;
        }
        g_lines_rx++;
        g_remote.clear();
        for (const auto& l : lines)
            g_remote.push_back({l, now + l.tenths / 10.0});
        return;
    }
    if (is_guest() && from == 0 && kind == K_RESULT) {
        Verdict v;
        if (!read_verdict(r, v) || v.id != g_local_id)
            return;
        g_results_rx++;
        if (v.result == R_EXPIRED && C.feed)
            C.feed("Nobody revived you: back at the nest. Your price: " + price_text(v.price) + ".");
        resolve(v);
        return;
    }
    const uint32_t id = r.u32();
    if (!r.ok() || !id || !is_host() || from == 0 || from >= kMaxPlayers)
        return;
    if (kind == K_DOWN) {
        Watch& w = g_watch[from];
        if (!begin_ok(w, id))
            return;
        if (!on() || !peer_reads_id(from)) { // (never from an older peer: it does not know the type)
            g_old_refused++;
            return;
        }
        w = {id, true, now, -1, false};
        g_lines_next = 0;
        H->log(M, SK_INFO, "revive: player %u down (knockout %u)", from, id);
    } else if (kind == K_GIVE_UP && g_watch[from].active && g_watch[from].id == id)
        g_watch[from].gave_up = true;
}

float seconds_left_of(uint8_t player)
{
    if (!H || !C.sess || !C.sess->connected() || !in_creature() || player >= kMaxPlayers)
        return 0;
    const double now = H->time_seconds();
    if (is_host())
        return g_watch[player].active ? (float)std::max(0.1, seconds_left(g_watch[player], now, kWindowS)) : 0.f;
    for (const auto& x : g_remote)
        if (x.l.player == player && x.until > now)
            return (float)(x.until - now);
    return 0;
}

void for_each_downed(void (*fn)(void* body, uint8_t player, float seconds, float revive, void* user), void* user)
{
    if (!H || !C.sess || !C.sess->connected() || !in_creature())
        return;
    const double now = H->time_seconds();
    const uint8_t my = me();
    if (is_host()) {
        for (uint8_t p = 0; p < kMaxPlayers; ++p) {
            const Watch& w = g_watch[p];
            if (!w.active)
                continue;
            void* body = p == my ? g_body : C.puppet_of ? C.puppet_of(p) : nullptr;
            if (body)
                fn(body, p, (float)seconds_left(w, now, kWindowS), revive_progress(w, now), user);
        }
        return;
    }
    for (const auto& x : g_remote) {
        if (x.until <= now)
            continue;
        void* body = x.l.player == my ? g_body : C.puppet_of ? C.puppet_of(x.l.player) : nullptr;
        if (body)
            fn(body, x.l.player, (float)(x.until - now), x.l.revive / 100.f, user);
    }
}

std::string command(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[1] == "giveup") {
        g_test_give_up = H->time_seconds() + 5;
        return "give up asked\n";
    }
    if (a.size() >= 2 && a[1] == "down") { // tests: a lethal hit on the own creature through the game's damage path
        void* av = game::avatar();
        if (!av)
            return "no creature\n";
        game::hit(av, game::health(av) + 50.f, 0, nullptr);
        char b[80];
        snprintf(b, sizeof b, "hit: hp now %.1f, is_down=%d\n", game::health(av), (int)down());
        return b;
    }
    const double now = H->time_seconds();
    void* av = game::avatar();
    char b[700];
    snprintf(b, sizeof b,
             "revive: setting=%d hold=%d own_price=%d window=%.0fs is_down=%d knockout=%u left=%.1fs game_hp=%.2f "
             "game_dead=%d debt=%.1f\n  downs=%u revives=%u expired=%u gave_up=%u died_other=%u wipes=%u blows_held=%u "
             "requests=%u lines_sent=%u lines_rx=%u lines_bad=%u results_rx=%u self_ended=%u kill_retries=%u "
             "old_refused=%u\n  last_result=%u last_price_kind=%u last_price=%.1f charged_kind=%u charged=%.1f\n  last: %s\n",
             (int)on(), (int)g_hold_fix, (int)g_own_price_fix, kWindowS, (int)down(), g_local_id,
             g_body ? std::max(0.0, g_local_start + kWindowS - now) : 0.0, av ? game::health(av) : -1.f,
             av ? (int)game::is_dead(av) : -1, own_progress::local_debt(), g_downs, g_revives, g_expired, g_gave_up,
             g_died_other, g_wipes, g_blows_held, g_requests, g_lines_sent, g_lines_rx, g_lines_bad, g_results_rx,
             g_self_ended, g_kill_retries, g_old_refused, (unsigned)g_last_verdict.result,
             (unsigned)g_last_verdict.price.kind, g_last_verdict.price.amount, (unsigned)g_last_charge.kind,
             g_last_charge.amount, g_last.c_str());
    std::string out = b;
    if (is_host())
        for (uint8_t p = 0; p < kMaxPlayers; ++p) {
            const Watch& w = g_watch[p];
            if (!w.active)
                continue;
            snprintf(b, sizeof b, "  player %u: knockout %u left=%.1fs revive=%.0f%% gave_up=%d at (%.1f %.1f %.1f)\n", p,
                     w.id, seconds_left(w, now, kWindowS), revive_progress(w, now) * 100, (int)w.gave_up,
                     g_watch_pos[p].x, g_watch_pos[p].y, g_watch_pos[p].z);
            out += b;
        }
    else
        for (const auto& x : g_remote) {
            snprintf(b, sizeof b, "  player %u left=%.1fs revive=%u%%\n", x.l.player, x.until - now, x.l.revive);
            out += b;
        }
    return out;
}

} // namespace revive
