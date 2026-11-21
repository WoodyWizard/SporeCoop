#include "creature_rescue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "fixes.h"
#include "game.h"
#include "give_up.h"
#include "modes.h"
#include "progress_debt.h"
#include "ring_timer.h"
#include "down_time.h"
#include "net/interp.h"

namespace creature_rescue {
// BUG-9127: the knockout was invisible (its text only in a console report). Off: as before.
static bool g_rescue_ui_fix = true;
COOP_FIX("creature.rescue_ui", "BUG-9127", g_rescue_ui_fix,
         "creature / adventure: a knockout shows its countdown on screen, tells everybody, and a downed partner's arrow is red");
bool ui_shown() { return g_rescue_ui_fix; }
namespace {
// BUG-9118: 30 s for a partner to come (10 before); the downed player can give up (hold G 1 s): the
// same as the timer running out, at once. `coop.fix creature.down_30 off`: 10 s.
bool g_long_fix = true;
COOP_FIX("creature.down_30", "BUG-9118", g_long_fix, "creature / adventure: a downed player waits 30 s for a partner (10 before)");
double window_s() { return downed::seconds(g_long_fix, 10.0); } // BUG-9149: a minute (core.down_60)
double hold() { return downed::revive_hold(1.5); } // BUG-9307
constexpr float reach = 6;
enum Event : uint8_t { Begin, Revived, Expired, GiveUp };
const SkHost* H;
net::Session* S;
void (*notice)(const std::string&, int);
uint32_t planet, serial, local_id;
void* body;
net::Vec3 anchor;
double local_start, last_request, protection_until;
// BUG-9146: every partner here (their last state in this world), not only the one partner of a duo
std::vector<net::PlayerState> peers;
bool standing(const net::PlayerState& p) { return p.health > 0 && !(p.flags & (net::PF_DEAD | net::PF_DOWNED)); }
const net::PlayerState* peer_of(uint8_t id)
{
    for (const auto& p : peers) if (p.id == id) return &p;
    return nullptr;
}
// a standing partner (not `except`) within reach of `at`, or null
const net::PlayerState* helper_near(const net::Vec3& at, uint8_t except = 0xFF)
{
    for (const auto& p : peers)
        if (p.id != except && standing(p) && net::dist(at, p.pos) <= 6.f) return &p;
    return nullptr;
}
bool g_leave_fix = true;
// BUG-9089 (44): a captain in an adventure is revived like a Creature-stage avatar (a partner has 10 s);
// if nobody comes, it gets back up beside a partner who is still standing, and only a captain whose
// partners are all down dies (the host's mission then fails for everyone, adventure.cpp).
bool g_adventure_fix = true;
COOP_FIX("adventure.revive", "BUG-9089", g_adventure_fix,
         "adventure: a downed captain is revived by a partner within 10 s, or gets back up beside one");
bool here(uint32_t mode) { return mode == modes::kCreature || (g_adventure_fix && mode == modes::kScenario); }
COOP_FIX("creature.recovery_leave", "BUG-9010", g_leave_fix,
         "creature: an existing death timer survives a partner leaving");

// The host tracks each victim independently, including simultaneous knockouts.
struct Rescue {
    uint32_t id = 0;
    bool active = false;
    double begin = 0, near_since = -1;
    std::string name;
    bool gave_up = false; // BUG-9118: the downed player gave up (as if the timer ran out)
};
bool local_give_up; // this game's downed player asked to give up (sent / applied by the next tick)
bool g_wipe_fix = true; // BUG-9226 (see the host's tick)
COOP_FIX("creature.party_wipe", "BUG-9226", g_wipe_fix, "creature: when the whole party is down, every timer runs out at once (respawn at the nests)");
double wipe_seen = -1;  // the host: since then nobody stands (-1: someone stands)
unsigned wipes = 0;
double own_near_since = -1; // BUG-9141: this downed player: a standing partner within reach since then (seen here)
Rescue watches[net::kMaxPlayers];
// A departed seat may be reused before its timer expires. Keep only the old
// episode's deadline and name, never an avatar pointer or the reusable seat.
std::vector<Rescue> departed;

void send(uint8_t to, Event event, uint32_t id)
{
    net::Writer w;
    w.u32(planet); w.u32(id); w.u8(event);
    S->send_reliable_to(to, net::MSG_CREATURE_RESCUE, w);
}

void begin(uint8_t player, uint32_t id, double now)
{
    auto& w = watches[player];
    if (!w.active && (!w.id || int32_t(id - w.id) > 0)) {
        w = {id, true, now, -1, S->name_of(player)};
        if (g_rescue_ui_fix) // BUG-9127: everybody hears it (the host's notice is relayed)
            notice(w.name + " is down! Stand next to them within " + std::to_string((int)window_s()) + " s.", -1);
    }
}

void charge(const std::string& name, bool gave_up = false)
{
    if (game::mode_id() == modes::kScenario) { // no DNA in an adventure
        notice(name + (gave_up ? " gave up." : " was down too long."), -1);
        return;
    }
    if (progress_debt::enabled()) {
        // BUG-9119: nothing is taken away: the pack owes the progress instead (new DNA repays it first)
        float debt = progress_debt::add_death(); // 10% of the progress bar so far; DNA untouched
        char text[120];
        if (debt > 0)
            std::snprintf(text, sizeof text, " %s. Progress debt: %.0f (new progress refills it first).", gave_up ? "gave up" : "fell", debt);
        else
            std::snprintf(text, sizeof text, " %s.", gave_up ? "gave up" : "fell");
        notice(name + text, -1);
        return;
    }
    float dna = game::dna(), cost = 0;
    if (std::isfinite(dna) && dna >= 1) {
        cost = std::min(std::floor(dna), std::ceil(dna * 0.10f));
        game::dna_add_raw(-cost);
    }
    char loss[80];
    std::snprintf(loss, sizeof loss, " %s. The pack lost %.0f DNA.", gave_up ? "gave up" : "fell", cost);
    notice(name + loss, -1);
}

void resolve(Event result)
{
    if (body) game::hold_pose(body, false); // BUG-9156: up again (revived), or the game's own death
    local_id = 0;
    if (!body || !here(game::mode_id()) || game::avatar() != body)
        return;
    const net::PlayerState* up = nullptr; // a partner who is still standing (BUG-9146: any of them)
    for (const auto& p : peers) if (standing(p)) { up = &p; break; }
    if (result == Expired && game::mode_id() == modes::kScenario && up) {
        const net::PlayerState& peer = *up;
        // the adventure goes on: back up beside the partner who is still standing
        net::Vec3 at = peer.pos;
        at.x += 2.f;
        game::teleport(body, at, game::orientation(body));
        game::set_health(body, std::max(1.f, game::max_health(body) * 0.4f));
        protection_until = H->time_seconds() + 3;
        notice("Back on your feet beside " + S->name_of(peer.id) + ".", -1);
        return;
    }
    if (result == Revived) {
        game::set_health(body, std::max(1.f, game::max_health(body) * 0.4f));
        protection_until = H->time_seconds() + 2;
    } else {
        game::apply_damage(body, 1e6f, 0, nullptr); // native death and nest respawn
    }
}
}

void init(const SkHost* host, net::Session* session, void (*notify)(const std::string&, int))
{
    H = host; S = session; notice = notify;
}

void reset()
{
    planet = local_id = 0;
    body = nullptr;
    peers.clear();
    protection_until = 0;
    for (auto& w : watches) w = {};
    departed.clear();
}

bool down() { return local_id != 0; }
bool pending()
{
    if (down()) return true;
    for (const auto& w : watches) if (w.active) return true;
    return !departed.empty();
}

bool continuing() { return g_leave_fix && pending(); }

void on_player_leave(uint8_t player)
{
    if (!g_leave_fix) { reset(); return; }
    if (player >= net::kMaxPlayers) return;
    auto& w = watches[player];
    if (S->role() == net::Role::Host && w.active) departed.push_back(w);
    w = {};
    std::erase_if(peers, [player](const net::PlayerState& p) { return p.id == player; });
}

// BUG-9156 (owner decision 1V): a downed creature plays the game's own death animation and keeps lying (the idle refresh
// is held, game::hold_pose), without the game's death (no Die: mbDead would hide the partner's puppet); the anchor
// hold no longer restarts the animation (place, not teleport). The partners' games do the same with its puppet on
// PF_DOWNED (modes.cpp). `coop.fix creature.downed_pose off`: standing, as before.
static bool g_downed_pose = true;
COOP_FIX("creature.downed_pose", "BUG-9156", g_downed_pose,
         "creature / adventure: a downed creature lies in the game's death pose until it is revived");
bool downed_pose_on() { return g_downed_pose; }
static double g_pose_at;
static void lie_down(void* c, double now)
{
    if (!g_downed_pose || !c) return;
    game::hold_pose(c, true);
    uint32_t anim = 0;
    if (game::drawn_state(c, nullptr, &anim) && anim == kDownedAnim && now - g_pose_at < 4) return;
    if (anim == kDownedAnim) return;
    game::play_animation(c, kDownedAnim, -1, true);
    g_pose_at = now;
}

bool protect(void* avatar, float& damage)
{
    if (!S || !S->connected() || !here(game::mode_id()) || !planet ||
        avatar != body || !avatar || damage <= 0) return false;
    double now = H->time_seconds();
    if (down() || now < protection_until) { damage = 0; return true; }
    if (peers.empty() || damage < game::health(avatar) || game::health(avatar) <= 0) return false;
    if (++serial == 0) ++serial;
    local_id = serial;
    anchor = game::position(body);
    local_start = now;
    own_near_since = -1;
    last_request = now;
    game::set_health(body, 1);
    game::stop(body);
    g_pose_at = 0;
    lie_down(body, now); // BUG-9156
    damage = 0;
    if (S->role() == net::Role::Host) begin(0, local_id, now);
    else send(0, Begin, local_id);
    return true;
}

void message(uint8_t from, net::Reader& r)
{
    uint32_t world = r.u32(), id = r.u32();
    uint8_t event = r.u8();
    if (!r.ok() || r.left() || !id || !planet || world != planet || !S->connected() ||
        !here(game::mode_id()) || !body || game::avatar() != body) return;
    if (S->role() == net::Role::Host) {
        const net::PlayerState* victim = from && from < net::kMaxPlayers ? peer_of(from) : nullptr;
        if (event == Begin && victim && (victim->flags & net::PF_DOWNED))
            begin(from, id, H->time_seconds());
        else if (event == GiveUp && from && from < net::kMaxPlayers && watches[from].active && watches[from].id == id)
            watches[from].gave_up = true; // BUG-9118: settled by the next tick
    } else if (from == 0 && id == local_id && (event == Expired || event == Revived)) {
        resolve((Event)event);
    }
}

void tick(uint32_t world, void* avatar, const std::vector<net::PlayerState>& partners, double now)
{
    if (world != planet || avatar != body) reset();
    planet = world;
    body = avatar;
    peers = partners;
    if (!planet || !body) return;
    if (down()) {
        const bool helper = helper_near(anchor) != nullptr;
        if (!helper) own_near_since = -1;
        else if (own_near_since < 0) own_near_since = now;
        game::set_health(body, 1); // BUG-9005: the game's nest also heals a downed avatar
        game::stop(body);
        if (net::dist(anchor, game::position(body)) > 0.4f) {
            if (g_downed_pose) game::place(body, anchor, game::orientation(body)); // BUG-9156: keeps the pose
            else game::teleport(body, anchor, game::orientation(body));
        }
        lie_down(body, now);
        if (S->role() == net::Role::Client && now - last_request > 1) {
            send(0, Begin, local_id); // the first request may predate the host's first usable peer state
            last_request = now;
        }
        if (S->role() == net::Role::Client && peers.empty() && now - local_start >= window_s() + 5) {
            resolve(Expired); // a host in another stage cannot resolve this body's knockout
            return;
        }
        // BUG-9118: giving up (hold G, or coop.giveup): the host settles it like an expired timer
        if (local_give_up || give_up::held(H, now)) {
            local_give_up = false;
            if (S->role() == net::Role::Client) send(0, GiveUp, local_id);
            else if (watches[0].active && watches[0].id == local_id) watches[0].gave_up = true;
        }
    } else
        local_give_up = false;
    if (S->role() != net::Role::Host) return;
    for (auto it = departed.begin(); it != departed.end();) {
        if (now - it->begin < window_s()) { ++it; continue; }
        charge(it->name);
        it = departed.erase(it);
    }
    // BUG-9226 (owner play test 2026-11-15: "When all party members die, it doesn't force the respawn"): with every
    // player down nobody can revive anyone; each waited out its 60 s. On: then every timer runs out at once (the game's
    // own death and respawn at the nest, the usual loss). Off: each waits (58-dev).
    if (g_wipe_fix) {
        bool any = false, all_down = down() || (body && game::health(body) <= 0);
        for (const auto& p : peers)
            if (standing(p) && !(p.id < net::kMaxPlayers && watches[p.id].active)) all_down = false;
        for (const auto& w : watches) any |= w.active;
        if (!any || !all_down)
            wipe_seen = -1;
        else if (wipe_seen < 0)
            wipe_seen = now;
        else if (now - wipe_seen > 2.0) { // 2 s with nobody standing (not one frame between two states)
            for (auto& w : watches)
                if (w.active) w.begin = now - window_s();
            notice("The whole party is down: everyone respawns.", -1);
            wipe_seen = -1;
            ++wipes;
        }
    }
    for (uint8_t victim = 0; victim < net::kMaxPlayers; ++victim) {
        auto& w = watches[victim];
        if (!w.active) continue;
        // BUG-9146: any standing player within reach revives: the host (a guest victim), or any partner who is not
        // down themselves (before: the one partner of a duo, and only the host could revive a guest)
        bool near = false;
        if (!victim) {
            for (const auto& p : peers)
                if (!(p.id < net::kMaxPlayers && watches[p.id].active) && standing(p) &&
                    net::dist(game::position(body), p.pos) <= reach) near = true;
        } else if (const net::PlayerState* v = peer_of(victim)) {
            near = !down() && game::health(body) > 0 && net::dist(game::position(body), v->pos) <= reach;
            for (const auto& p : peers)
                if (!near && p.id != victim && !(p.id < net::kMaxPlayers && watches[p.id].active) && standing(p) &&
                    net::dist(v->pos, p.pos) <= reach) near = true;
        }
        if (!near) w.near_since = -1;
        else if (w.near_since < 0) w.near_since = now;
        bool saved = !w.gave_up && near && w.near_since + hold() <= std::min(now, w.begin + window_s());
        if (!saved && !w.gave_up && now - w.begin < window_s()) continue;
        w.active = false; // one host debit per knockout, even if both players expire on this frame
        Event result = saved ? Revived : Expired;
        if (!saved) charge(w.name, w.gave_up);
        if (victim) send(victim, result, w.id);
        else resolve(result);
        if (saved) notice(w.name + " was revived.", -1);
    }
}

void give_up_now() { if (down()) local_give_up = true; } // tests and the console: coop.giveup

double window_seconds() { return window_s(); }
float reach_m() { return reach; }
double hold_seconds() { return hold(); }
float own_left()
{
    return down() ? ring_timer::fraction(window_s() - (H->time_seconds() - local_start), window_s()) : 0.f;
}
float own_revive()
{
    if (!down() || own_near_since < 0) return 0.f;
    return (float)std::clamp((H->time_seconds() - own_near_since) / hold(), 0.0, 1.0);
}
static bool g_hud_has_give_up; // BUG-9156: the downed screen's GIVE UP button says it
void set_give_up_button(bool shown) { g_hud_has_give_up = shown; }
std::string ui_line()
{
    if (!ring_timer::enabled()) return status();
    return down() && !g_hud_has_give_up ? "Hold G to give up" : std::string();
}

std::string status()
{
    if (down()) {
        int sec = std::max(0, (int)std::ceil(window_s() - (H->time_seconds() - local_start)));
        return "Downed: " + std::to_string(sec) + " s for your partner to revive you (hold G to give up)";
    }
    for (const auto& p : peers)
        if (p.flags & net::PF_DOWNED) return "Revive " + S->name_of(p.id) + ": stay close for " + std::to_string((int)hold()) + " s";
    return {};
}
}
