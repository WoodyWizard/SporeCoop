// BUG-9283 (Lineages port, from PR #196's creature_rescue.cpp: its knockout of BUG-9118 / BUG-9146 / BUG-9149, the party
// wipe of BUG-9226): a player's downed creature in the Creature stage. docs/re/creature.md#creature-pack-rescue-downed.
//
// Every game guards its own creature (teamplay::on_own_damage, before the pack's one-off rescue of BUG-0099's teamplay):
// a lethal hit while a partner plays in the same world puts it down where it stands: 1 health, stopped and held at the
// spot, no hit taken. The host watches every knockout (its own, and a guest's from MSG_CREATURE_RESCUE K_DOWN): a partner
// who stands within 6 m of the downed creature for 1.5 s revives it (40 % health); after 60 s (creature.down_60; 30 s
// off), or when its player held G for a second, it dies the game's way and respawns at its own nest (home_nests.cpp, the
// guest's game hatches there), and the pack loses 10 % of its DNA (the host's total, dna_cost). With every player down at
// once nobody can revive anybody: after 2 s every window runs out together (creature.party_wipe). The host sends the
// windows to the guests every second (K_LINES): every game draws "DOWN <s>" over the downed creatures.
// Not ported from Lineages: the progress debt (its BUG-9119, the debt instead of the DNA loss), the revive ring
// (ring_timer, BUG-9141: here a text tag), the death pose (BUG-9156: the creature stands), the spectator camera, the
// adventure captain (BUG-9089).
#include "creature_rescue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "creature_rescue_logic.h"
#include "fixes.h"
#include "game.h"
#include "give_up.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/interp.h"

namespace creature_rescue {

static bool g_fix = true;
COOP_FIX("creature.rescue", "BUG-9283", g_fix,
         "creature: a lethal hit downs the own creature while a partner is here; a partner next to it revives it, else it respawns at its nest");
static bool g_wipe = true;
COOP_FIX("creature.party_wipe", "BUG-9283", g_wipe, "creature: when every player is down, every window runs out at once (everyone respawns at their nest)");
static bool g_minute = true;
COOP_FIX("creature.down_60", "BUG-9283", g_minute, "creature: a downed creature waits a minute for a partner (off: 30 s)");

static const SkHost* H;
static SkMod* M;
static Ctx C;

// ---- this game's own creature ----
static void* g_body;           // the own creature that is down (null: not down)
static uint32_t g_serial;      // the knockout numbers of this game (counting up)
static uint32_t g_local_id;    // the knockout of g_body
static net::Vec3 g_anchor;     // where it fell
static net::Quat g_facing;
static double g_local_start, g_last_request, g_protect_until, g_test_give_up, g_give_up_sent;
// the verdict "not revived" while the game takes no damage (a caption that pauses the game, as the brain upgrade's: the
// lethal hit did nothing on lane D, 2026-11-20): the creature stays held and the hit is tried again every second
static void* g_kill_body;
static double g_kill_next, g_kill_until;
// ---- host ----
static std::array<Watch, kMaxPlayers> g_watch;
static std::array<net::Vec3, kMaxPlayers> g_watch_pos{};
static double g_wipe_since = -1, g_lines_next;
static bool g_lines_open;
// ---- guest: the host's lines ----
struct Remote {
    Line l;
    double until = 0;
};
static std::vector<Remote> g_remote;
// ---- counters (coop.rescue) ----
static uint32_t g_downs, g_revives, g_expired, g_gave_up, g_wipes, g_blows_held, g_requests, g_lines_sent, g_lines_rx,
    g_lines_bad, g_results_rx, g_self_ended, g_dna_lost, g_kill_retries;
static std::string g_last = "-";

static double window() { return down_seconds(g_minute); }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client; }
static bool in_creature() { return game::mode_id() == modes::kCreature; }

static void note(const std::string& text, bool everybody)
{
    g_last = text;
    H->log(M, SK_INFO, "creature rescue: %s", text.c_str());
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

static void send(uint8_t to, Kind kind, uint32_t id, uint8_t result = 0)
{
    net::Writer w;
    w.u8(kind);
    w.u32(modes::current_world());
    w.u32(id);
    if (kind == K_RESULT)
        w.u8(result);
    C.sess->send_reliable_to(to, net::MSG_CREATURE_RESCUE, w);
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
LIFECYCLE_HOOK("creature.rescue", lifecycle::kSessionEnd | lifecycle::kWorldChange, -46, [](const lifecycle::Ctx&) { forget(); },
               false);
// a player who left: its window ends without a charge (its game is gone; a rejoin starts its knockouts anew)
LIFECYCLE_HOOK("creature.rescue_leave", lifecycle::kPlayerLeave, -46,
               [](const lifecycle::Ctx& c) {
                   if (c.player < kMaxPlayers)
                       g_watch[c.player] = {};
               },
               false);

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

// ---- the own creature ----
static void hold(void* body, bool keep_health = true)
{
    if (keep_health)
        game::set_health(body, 1); // (the nest's heal and the pack regen would lift it)
    game::stop(body);
    if (net::dist(g_anchor, game::position(body)) > 0.4f)
        game::teleport(body, g_anchor, g_facing);
}

// the host's verdict on this game's own knockout (or the guest's own when no host answered)
static void resolve(Result result)
{
    void* body = g_body;
    g_body = nullptr;
    g_local_id = 0;
    if (!body || game::avatar() != body || !in_creature())
        return;
    if (result == R_REVIVED) {
        game::set_health(body, std::max(1.f, game::max_health(body) * kUpHealth));
        g_protect_until = H->time_seconds() + kProtectS;
        if (C.feed)
            C.feed("You are back on your feet.");
        return;
    }
    game::apply_damage(body, 1e6f, 0, nullptr); // the game's own death: the respawn at the own nest
    if (game::health(body) > 0 && !game::is_dead(body)) { // the game takes no damage now: again until it does
        g_kill_body = body;
        g_kill_next = H->time_seconds() + 1;
        g_kill_until = H->time_seconds() + 120;
    }
}

// a verdict "not revived" the game did not take yet: held where it fell, the lethal hit again every second
static void kill_tick(void* avatar, double now)
{
    if (!g_kill_body)
        return;
    if (g_kill_body != avatar || game::health(avatar) <= 0 || game::is_dead(avatar) || now > g_kill_until) {
        g_kill_body = nullptr;
        return;
    }
    hold(avatar, false); // (not its health: 1 every frame undid the game's death of the lethal hit, lane D 2026-11-20)
    if (now < g_kill_next)
        return;
    g_kill_next = now + 1;
    g_kill_retries++;
    game::apply_damage(avatar, 1e6f, 0, nullptr);
}

bool protect(void* avatar, float& damage)
{
    if (!g_fix || !C.sess || !C.sess->connected() || !avatar || damage <= 0)
        return false;
    const double now = H->time_seconds();
    if ((g_body && avatar == g_body) || (g_kill_body && avatar == g_kill_body)) { // down: no hit lands
        damage = 0;
        g_blows_held++;
        return true;
    }
    if (now < g_protect_until && avatar == game::avatar()) {
        damage = 0;
        g_blows_held++;
        return true;
    }
    DownIn in;
    in.fix = g_fix;
    in.creature = in_creature() && avatar == game::avatar();
    in.connected = true;
    in.damage = damage;
    in.health = game::health(avatar);
    if (!lethal(in.damage, in.health) || !in.creature) // (the common case: no partner list below)
        return false;
    in.partner_here = C.peers && !C.peers().empty();
    if (!should_down(in))
        return false;
    if (++g_serial == 0)
        ++g_serial;
    g_body = avatar;
    g_local_id = g_serial;
    g_anchor = game::position(avatar);
    g_facing = game::orientation(avatar);
    g_local_start = now;
    g_last_request = now;
    game::set_health(avatar, 1);
    game::stop(avatar);
    damage = 0;
    g_downs++;
    char b[200];
    snprintf(b, sizeof b, "%s is down! Stand next to them within %d s to revive them.", C.sess->name_of(C.sess->my_id()).c_str(),
             (int)window());
    note(b, true);
    if (C.feed)
        C.feed("You are down: a partner standing next to you revives you. Hold G to give up.");
    H->log(M, SK_INFO, "creature rescue: own creature down (knockout %u, hit %.1f on %.1f health)", g_local_id, in.damage, in.health);
    if (is_host()) {
        Watch& w = g_watch[0];
        w = {g_local_id, true, now, -1, false};
        g_lines_next = 0;
    } else {
        send(0, K_DOWN, g_local_id);
        g_requests++;
    }
    return true;
}

// ---- host ----
static float pack_charge(const std::string& who, bool gave_up)
{
    const float cost = dna_cost(game::dna());
    if (cost > 0)
        game::dna_add_raw(-cost);
    g_dna_lost += (uint32_t)cost;
    char b[200];
    snprintf(b, sizeof b, "%s %s: back at their nest. The pack lost %.0f DNA.", who.c_str(),
             gave_up ? "gave up" : "was not revived", cost);
    note(b, true);
    return cost;
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
        l.tenths = tenths_left(seconds_left(w, now, window()));
        l.revive = (uint8_t)std::lround(revive_progress(w, now) * 100);
        lines.push_back(l);
    }
    net::Writer wr;
    wr.u8(K_LINES);
    wr.u32(modes::current_world());
    write_lines(wr, lines);
    C.sess->broadcast_reliable(net::MSG_CREATURE_RESCUE, wr);
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
    // a watched guest who left this world (another place, its creature gone) ends its window without a charge
    for (uint8_t v = 1; v < kMaxPlayers; ++v)
        if (g_watch[v].active && !peer(v)) {
            g_watch[v] = {};
            H->log(M, SK_INFO, "creature rescue: player %u's window ends: it is not in this world any more", v);
        }
    bool any = false, standing_any = false;
    for (const auto& w : g_watch)
        any |= w.active;
    if (avatar && !g_watch[0].active && game::health(avatar) > 0 && !game::is_dead(avatar))
        standing_any = true;
    for (const auto& p : peers)
        if (standing(p) && !(p.id < kMaxPlayers && g_watch[p.id].active))
            standing_any = true;
    if (g_wipe && wipe_due(g_wipe_since, any, standing_any, now)) {
        for (auto& w : g_watch)
            if (w.active)
                w.begin = now - window();
        g_wipes++;
        note("The whole party is down: everyone respawns at their nest.", true);
    }
    for (uint8_t v = 0; v < kMaxPlayers; ++v) {
        Watch& w = g_watch[v];
        if (!w.active)
            continue;
        if (v == 0 && !w.gave_up && (give_up::held(H, now) || now < g_test_give_up)) {
            w.gave_up = true;
            g_test_give_up = 0;
        }
        net::Vec3 at;
        if (v == 0)
            at = avatar ? game::position(avatar) : g_anchor;
        else
            at = peer(v)->pos;
        g_watch_pos[v] = at;
        // a standing player who is not down within reach: the host's own creature (a guest's window) or any partner
        bool near = false;
        if (v != 0 && avatar && !g_watch[0].active && game::health(avatar) > 0 && !game::is_dead(avatar) &&
            net::dist(game::position(avatar), at) <= kReachM)
            near = true;
        for (const auto& p : peers)
            if (!near && p.id != v && standing(p) && !(p.id < kMaxPlayers && g_watch[p.id].active) &&
                net::dist(p.pos, at) <= kReachM)
                near = true;
        const bool gave_up = w.gave_up;
        const Step st = step(w, near, now, window());
        if (st == Step::Waiting)
            continue;
        w.active = false; // one verdict per knockout
        const std::string who = v == 0 ? C.sess->name_of(C.sess->my_id()) : C.sess->name_of(v);
        if (st == Step::Revived) {
            g_revives++;
            note(who + " was revived.", true);
        } else {
            (gave_up ? g_gave_up : g_expired)++;
            pack_charge(who, gave_up);
        }
        const Result r = st == Step::Revived ? R_REVIVED : R_EXPIRED;
        if (v == 0)
            resolve(r);
        else
            send(v, K_RESULT, w.id, r);
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

// ---- guest ----
static void guest_tick(double now, const std::vector<net::PlayerState>& peers)
{
    (void)peers;
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
        H->log(M, SK_INFO, "creature rescue: this player gave up (knockout %u)", g_local_id);
    }
    if (now - g_local_start >= window() + kNoHostS) { // a host of an older version (or in another world) never answers
        g_self_ended++;
        note("Nobody revived you: back at your nest.", false);
        resolve(R_EXPIRED);
    }
}

void tick(double now)
{
    if (!H || !C.sess)
        return;
    void* avatar = game::avatar();
    if (!C.sess->connected() || !in_creature() || !avatar) {
        if (g_body || g_lines_open)
            forget();
        return;
    }
    if (g_body && g_body != avatar) { // another body (an editor, a load): the knockout is over
        g_body = nullptr;
        g_local_id = 0;
    }
    kill_tick(avatar, now);
    if (g_body) {
        if (!g_fix) { // switched off meanwhile: the creature is the game's again
            g_body = nullptr;
            g_watch[0] = {};
        } else
            hold(g_body);
    }
    const std::vector<net::PlayerState> peers = C.peers ? C.peers() : std::vector<net::PlayerState>{};
    if (is_host())
        host_tick(now, avatar, peers);
    else if (is_guest())
        guest_tick(now, peers);
}

void message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    const uint32_t world = r.u32();
    if (!r.ok() || !C.sess || !C.sess->connected() || !in_creature() || world != modes::current_world())
        return;
    const double now = H->time_seconds();
    if (kind == K_LINES) {
        std::vector<Line> lines;
        if (from != 0 || !read_lines(r, lines)) {
            g_lines_bad++;
            return;
        }
        g_lines_rx++;
        g_remote.clear();
        for (const auto& l : lines)
            g_remote.push_back({l, now + l.tenths / 10.0});
        return;
    }
    const uint32_t id = r.u32();
    if (!r.ok() || !id)
        return;
    if (is_host() && from != 0 && from < kMaxPlayers) {
        if (kind == K_DOWN) {
            Watch& w = g_watch[from];
            if (begin_ok(w, id)) {
                w = {id, true, now, -1, false};
                g_lines_next = 0;
                H->log(M, SK_INFO, "creature rescue: player %u down (knockout %u)", from, id);
            }
        } else if (kind == K_GIVE_UP && g_watch[from].active && g_watch[from].id == id)
            g_watch[from].gave_up = true;
        return;
    }
    if (is_guest() && from == 0 && kind == K_RESULT) {
        const uint8_t res = r.u8();
        if (!r.ok() || id != g_local_id || (res != R_REVIVED && res != R_EXPIRED))
            return;
        g_results_rx++;
        resolve((Result)res);
    }
}

float seconds_left_of(uint8_t player)
{
    if (!H || !C.sess || !C.sess->connected() || !in_creature() || player >= kMaxPlayers)
        return 0;
    const double now = H->time_seconds();
    if (is_host())
        return g_watch[player].active ? (float)std::max(0.1, seconds_left(g_watch[player], now, window())) : 0.f;
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
    const uint8_t me = C.sess->my_id();
    if (is_host()) {
        for (uint8_t p = 0; p < kMaxPlayers; ++p) {
            const Watch& w = g_watch[p];
            if (!w.active)
                continue;
            void* body = p == me ? g_body : C.puppet_of ? C.puppet_of(p) : nullptr;
            if (body)
                fn(body, p, (float)seconds_left(w, now, window()), revive_progress(w, now), user);
        }
        return;
    }
    for (const auto& x : g_remote) {
        if (x.until <= now)
            continue;
        void* body = x.l.player == me ? g_body : C.puppet_of ? C.puppet_of(x.l.player) : nullptr;
        if (body)
            fn(body, x.l.player, (float)(x.until - now), x.l.revive / 100.f, user);
    }
}

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[1] == "giveup") {
        g_test_give_up = H->time_seconds() + 5;
        return "give up asked\n";
    }
    if (a.size() >= 2 && a[1] == "down") { // a lethal hit on the own creature through the game's damage path (tests)
        void* av = game::avatar();
        if (!av)
            return "no creature\n";
        game::hit(av, game::health(av) + 50.f, 0, nullptr);
        char b[80];
        snprintf(b, sizeof b, "hit: hp now %.1f, down=%d\n", game::health(av), (int)down());
        return b;
    }
    const double now = H->time_seconds();
    char b[600];
    snprintf(b, sizeof b,
             "creature rescue: fix=%d wipe=%d window=%.0fs down=%d knockout=%u left=%.1fs downs=%u revives=%u expired=%u "
             "gave_up=%u wipes=%u blows_held=%u requests=%u lines_sent=%u lines_rx=%u lines_bad=%u results_rx=%u self_ended=%u "
             "dna_lost=%u kill_retries=%u\n  last: %s\n",
             (int)g_fix, (int)g_wipe, window(), (int)down(), g_local_id, g_body ? std::max(0.0, g_local_start + window() - now) : 0.0,
             g_downs, g_revives, g_expired, g_gave_up, g_wipes, g_blows_held, g_requests, g_lines_sent, g_lines_rx, g_lines_bad,
             g_results_rx, g_self_ended, g_dna_lost, g_kill_retries, g_last.c_str());
    std::string out = b;
    if (is_host())
        for (uint8_t p = 0; p < kMaxPlayers; ++p) {
            const Watch& w = g_watch[p];
            if (!w.active)
                continue;
            snprintf(b, sizeof b, "  player %u: knockout %u left=%.1fs revive=%.0f%% gave_up=%d at (%.1f %.1f %.1f)\n", p, w.id,
                     seconds_left(w, now, window()), revive_progress(w, now) * 100, (int)w.gave_up, g_watch_pos[p].x,
                     g_watch_pos[p].y, g_watch_pos[p].z);
            out += b;
        }
    else
        for (const auto& x : g_remote) {
            snprintf(b, sizeof b, "  player %u left=%.1fs revive=%u%%\n", x.l.player, x.until - now, x.l.revive);
            out += b;
        }
    return out;
}

} // namespace creature_rescue
