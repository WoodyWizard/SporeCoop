// WP-K (BUG-1303): a partner in an editor of its own is shown in the game's cocoon (cocoon.h).
#include "cocoon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "creations.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/interp.h"

namespace cocoon {
namespace {
const SkHost* H;
SkMod* M;

bool g_on = true;
COOP_FIX("creature.cocoon", "BUG-1303", g_on,
         "creature: a partner in an editor of its own stays in the game's cocoon where it stood and comes out with its new body (it disappeared before)");

constexpr double kEnterSeconds = 2.4;  // gen_evolve_cocoon, then the hold (the fork's runtime, 2026-11-12)
constexpr double kHoldReplay = 6.0;    // the hold animation does not loop by itself: played again this often
constexpr double kShellEvery = 1.8;    // the woven shell lasts ~2 s: the next one before it fades
constexpr double kGlowAgain = 0.5;     // the glow is made again this soon after it faded
constexpr double kBodyWait = 15.0;     // back with a new design: until the puppet wears it, this long at most
constexpr double kNoDesignWait = 6.0;  // back and no new design (left with the red cross): this long
constexpr double kEmergeSeconds = 2.5; // the emerge animation plays where the cocoon stood, then the driving goes on
constexpr float kShrunk = 0.12f;       // the creature's size inside (the fork's BUG-9181: nothing shows through the shell)
constexpr float kShellFits = 0.9f;     // the shell at scale 1 holds a creature of bounding radius ~0.9 (fork, 2026-11-13)
constexpr float kDrift = 0.2f;         // a held body moved farther than this from its spot is put back

enum Phase : int { kNone = 0, kEnter = 1, kHold = 2, kWait = 3, kEmerge = 4 };
const char* phase_name(int p)
{
    static const char* n[] = {"none", "enter", "hold", "wait", "emerge"};
    return p >= 0 && p <= 4 ? n[p] : "?";
}

// a world effect of ours and the mode / world it was made in: called (IsActive, Stop, Release) only while this game is
// still there; after a change the game tore it down and the pointer is dropped (effects.cpp, BUG-0451 review item 7)
struct Fx {
    void* fx = nullptr;
    uint32_t mode = 0, world = 0;
};
bool fx_here(const Fx& f) { return f.fx && f.mode == game::mode_id() && f.world == modes::current_world(); }
void fx_end(Fx& f, bool stop)
{
    if (fx_here(f)) {
        if (stop && game::effect_active(f.fx))
            game::stop_world_effect(f.fx);
        game::release_world_effect(f.fx);
    }
    f = {};
}

struct Cocoon {
    int phase = kNone;
    double at = 0;       // when the phase began
    double played = 0;   // the last play of the hold animation
    double shell_at = -1e9, glow_at = -1e9;
    Fx glow;             // crg_cocoon_hold, ours to stop and release
    void* puppet = nullptr; // the body in it (a new body of the partner is a new puppet: it goes in too)
    uint32_t body0 = 0;  // the species the puppet wore when it went in
    float size = 0;      // the body's scale before it was shrunk (given back as it comes out)
    float radius = 0;    // the bounding radius of the first body: the shell keeps that size
    net::Vec3 spot;      // where it went in: every shell there, every body held there (fork BUG-9194)
    net::Quat rot;
    // tests (coop.cocoon): the game's own state read back right after the mod's plays
    uint32_t seen_hold = 0, seen_emerge = 0, emerged_body = 0;
};
std::map<uint8_t, Cocoon> g_c;
std::map<uint8_t, Cocoon> g_last; // tests (coop.cocoon): each partner's last finished cocoon (no pointers used)
std::vector<Fx> g_once; // shells and bursts: released once they ended
uint32_t g_wraps, g_new_bodies, g_emerges, g_emerges_new, g_waited_out, g_shells, g_glows, g_dropped;

// one of the game's cocoon effects at `at`, its up (local z) along the planet's up there (with the world's z the effects
// lay on their side, fork 2026-11-12); the 3x3 holds the local axes as rows (the fork's runtime check)
void* effect_at(const net::Vec3& at, uint32_t id, float scale)
{
    float len = std::sqrt(at.x * at.x + at.y * at.y + at.z * at.z);
    if (!(len > 1.f))
        len = 1.f;
    const float u[3] = {at.x / len, at.y / len, at.z / len};
    float r[3] = {u[1], -u[0], 0.f};
    float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (rl < 1e-3f) {
        r[0] = 1.f, r[1] = 0.f, r[2] = 0.f;
        rl = 1.f;
    }
    for (float& v : r)
        v /= rl;
    const float fw[3] = {u[1] * r[2] - u[2] * r[1], u[2] * r[0] - u[0] * r[2], u[0] * r[1] - u[1] * r[0]};
    uint8_t xf[0x44] = {};
    float* f = (float*)(xf + 4);
    f[0] = at.x, f[1] = at.y, f[2] = at.z;
    f[3] = scale;
    float* m = f + 4;
    const float* axes[3] = {r, fw, u};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i * 3 + j] = axes[i][j];
    return game::create_world_effect(id, xf);
}

float shell_scale(const Cocoon& c)
{
    const float r = c.radius;
    return std::isfinite(r) && r > 0 ? std::clamp(r / kShellFits, 0.8f, 4.f) : 1.f;
}

Fx make(const Cocoon& c, uint32_t id)
{
    Fx f;
    f.fx = effect_at(c.spot, id, shell_scale(c));
    if (f.fx) {
        f.mode = game::mode_id();
        f.world = modes::current_world();
    }
    return f;
}

void once(const Cocoon& c, uint32_t id)
{
    Fx f = make(c, id);
    if (f.fx) {
        g_once.push_back(f);
        if (id == kFxShell)
            g_shells++;
    }
}

void shell(Cocoon& c, double now)
{
    once(c, kFxShell);
    c.shell_at = now;
}

void glow(Cocoon& c, double now)
{
    fx_end(c.glow, true);
    c.glow = make(c, kFxGlow);
    c.glow_at = now;
    if (c.glow.fx)
        g_glows++;
}

uint32_t anim_now(void* p)
{
    uint32_t a = 0;
    return p && game::drawn_state(p, nullptr, &a) ? a : 0;
}

void hold_at_spot(void* p, const Cocoon& c)
{
    if (net::dist(game::position(p), c.spot) > kDrift)
        game::teleport(p, c.spot, c.rot);
}

void shrink(void* p, Cocoon& c, float k)
{
    if (!(c.size > 0)) {
        c.size = game::scale(p);
        if (!(c.size > 0) || !std::isfinite(c.size))
            c.size = 1.f;
    }
    const float want = c.size * k;
    if (std::fabs(game::scale(p) - want) > 0.002f)
        game::set_scale(p, want);
}

// body p goes into the cocoon. fresh: the partner just went into its editor (the curl in, then the hold); else a body
// already inside (a new body of the partner: shrunk at once, the hold pose, the glow)
void wrap(uint8_t id, void* p, Cocoon& c, double now, bool fresh)
{
    if (fresh) {
        c.spot = game::position(p);
        c.rot = game::orientation(p);
        c.radius = game::bounding_radius(p);
        c.body0 = game::species_instance(p);
        c.phase = kEnter;
        c.at = now;
        g_wraps++;
    } else {
        g_new_bodies++;
        game::teleport(p, c.spot, c.rot); // (a new body is made where the partner is now: into the same cocoon)
    }
    c.puppet = p;
    c.size = 0;
    game::hold_pose(p, true);
    game::play_animation(p, fresh ? kAnimEnter : kAnimHold, -1, true);
    c.played = now;
    if (!fresh) {
        shrink(p, c, kShrunk);
        c.seen_hold = anim_now(p);
    }
    if (now - c.shell_at >= kShellEvery || fresh)
        shell(c, now);
    if (!fresh)
        glow(c, now);
    H->log(M, SK_INFO, "cocoon: partner %u %s (body %p species %08X, radius %.2f, at %.1f %.1f %.1f) (BUG-1303)", id,
           fresh ? "goes into the cocoon" : "has a new body: it goes into the same cocoon", p, game::species_instance(p),
           c.radius, c.spot.x, c.spot.y, c.spot.z);
}

// inside: the shell laid again in time, the glow made again when it faded, the hold pose played again
void keep(void* p, Cocoon& c, double now)
{
    hold_at_spot(p, c);
    shrink(p, c, kShrunk);
    if (now - c.shell_at >= kShellEvery)
        shell(c, now);
    if ((!c.glow.fx || !fx_here(c.glow) || !game::effect_active(c.glow.fx)) && now - c.glow_at >= kGlowAgain)
        glow(c, now);
    if (now - c.played >= kHoldReplay || anim_now(p) != kAnimHold) {
        if (now - c.played >= 0.5) { // (not every frame while the game has not taken it yet)
            game::play_animation(p, kAnimHold, -1, true);
            c.played = now;
        }
    }
    uint32_t a = anim_now(p);
    if (a)
        c.seen_hold = a;
}

void release(uint8_t id, bool grow)
{
    auto it = g_c.find(id);
    if (it == g_c.end())
        return;
    Cocoon& c = it->second;
    fx_end(c.glow, true);
    if (c.puppet && game::creature_listed(c.puppet)) {
        game::hold_pose(c.puppet, false);
        if (grow && c.size > 0)
            game::set_scale(c.puppet, c.size);
    } else if (c.puppet)
        game::hold_pose(c.puppet, false); // (only the list entry: never a call on a freed body)
    c.glow = {};
    g_last[id] = c;
    g_c.erase(it);
}

void reset_all()
{
    // lifecycle reset: mod state only (no game calls): the effects went with the world or the session's puppets
    for (auto& [id, c] : g_c)
        if (c.puppet)
            game::hold_pose(c.puppet, false);
    g_dropped += (uint32_t)g_c.size();
    g_c.clear();
    g_once.clear();
    g_last.clear();
}
size_t held_n() { return g_c.size() + g_once.size(); }
LIFECYCLE_STATE("cocoon.partners", lifecycle::kSession | lifecycle::kWorld, nullptr, reset_all, held_n);

} // namespace

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
}

bool on() { return g_on; }
bool active(uint8_t id) { return g_c.count(id) != 0; }

bool editing(uint8_t id, void* p, double now)
{
    if (!g_on || !p) {
        if (g_c.count(id))
            release(id, true);
        return false;
    }
    auto it = g_c.find(id);
    if (it == g_c.end()) {
        Cocoon& c = g_c[id];
        wrap(id, p, c, now, true);
        return true;
    }
    Cocoon& c = it->second;
    if (c.puppet != p) { // a new body while it edits
        wrap(id, p, c, now, false);
        c.phase = kHold;
        c.at = now;
        return true;
    }
    if (c.phase == kWait || c.phase == kEmerge) { // into its editor again before it came out
        if (c.phase == kEmerge) {
            shrink(p, c, kShrunk);
            game::hold_pose(p, true);
        }
        c.phase = kHold;
        c.at = now;
        c.played = -1e9;
    }
    if (c.phase == kEnter) {
        hold_at_spot(p, c);
        // the curl in: the creature shrinks with it, one shell laid again in time (fork BUG-9194: no second cocoon)
        const float t = (float)std::clamp((now - c.at) / kEnterSeconds, 0.0, 1.0);
        shrink(p, c, 1.f - (1.f - kShrunk) * t);
        if (now - c.shell_at >= kShellEvery)
            shell(c, now);
        if (now - c.at >= kEnterSeconds) {
            c.phase = kHold;
            c.at = now;
            c.played = -1e9; // the hold pose at once
            glow(c, now);
            keep(p, c, now);
        }
        return true;
    }
    keep(p, c, now);
    return true;
}

bool back(uint8_t id, void* p, uint32_t reported, double now)
{
    auto it = g_c.find(id);
    if (it == g_c.end())
        return false;
    if (!g_on || !p) {
        release(id, true);
        return false;
    }
    Cocoon& c = it->second;
    if (c.puppet != p) { // its new look made a new body: into the same cocoon first
        wrap(id, p, c, now, false);
        if (c.phase == kEnter || c.phase == kHold || c.phase == kEmerge) {
            c.phase = kWait;
            c.at = now;
        }
    }
    if (c.phase == kEnter || c.phase == kHold) {
        c.phase = kWait;
        c.at = now;
        H->log(M, SK_INFO, "cocoon: partner %u is back: in the cocoon until it wears its new design (BUG-1303)", id);
    }
    if (c.phase == kWait) {
        const uint32_t key = creations::player_key(id, creations::kCreature).instance;
        const uint32_t target = key ? key : reported;
        const uint32_t wears = game::species_instance(p);
        const bool designed = target && target != c.body0;
        const bool worn = designed && wears == target;
        const bool out = worn || (designed ? now - c.at >= kBodyWait : now - c.at >= kNoDesignWait);
        if (!out) {
            keep(p, c, now);
            return true;
        }
        if (!worn)
            g_waited_out++;
        // out: the game's emerge, the burst, the body grows back to its size
        c.phase = kEmerge;
        c.at = now;
        fx_end(c.glow, true);
        once(c, kFxHatch);
        game::hold_pose(p, true);
        game::play_animation(p, kAnimEmerge, -1, true);
        c.seen_emerge = anim_now(p);
        c.emerged_body = wears;
        g_emerges++;
        if (wears != c.body0)
            g_emerges_new++;
        H->log(M, SK_INFO, "cocoon: partner %u comes out of the cocoon with body %08X (it went in as %08X; %s) (BUG-1303)", id,
               wears, c.body0, worn ? "its new design" : designed ? "the new design did not come in time" : "no new design");
        return true;
    }
    if (c.phase == kEmerge) {
        hold_at_spot(p, c);
        const float t = (float)std::clamp((now - c.at) / (kEmergeSeconds * 0.6), 0.0, 1.0);
        shrink(p, c, kShrunk + (1.f - kShrunk) * t);
        if (now - c.at < kEmergeSeconds)
            return true;
        release(id, true);
        return false;
    }
    release(id, true);
    return false;
}

void puppet_gone(uint8_t id, void* p, bool keep_state)
{
    auto it = g_c.find(id);
    if (p)
        game::hold_pose(p, false);
    if (it == g_c.end())
        return;
    if (!keep_state) {
        fx_end(it->second.glow, true);
        g_c.erase(it);
        return;
    }
    fx_end(it->second.glow, true); // (the new body gets its own at once)
    it->second.puppet = nullptr;
}

void tick(double)
{
    size_t w = 0;
    for (size_t i = 0; i < g_once.size(); i++) {
        Fx f = g_once[i];
        if (!fx_here(f)) // the game tore it down with its world: dropped
            continue;
        if (!game::effect_active(f.fx)) {
            game::release_world_effect(f.fx);
            continue;
        }
        g_once[w++] = f;
    }
    g_once.resize(w);
    for (auto& [id, c] : g_c)
        if (c.glow.fx && !fx_here(c.glow))
            c.glow = {};
}

std::string status(uint8_t only)
{
    char b[400];
    snprintf(b, sizeof b,
             "cocoon: fix=%d wraps=%u new_bodies=%u emerges=%u emerges_new=%u waited_out=%u shells=%u glows=%u dropped=%u "
             "effects=%zu held_poses=%zu\n",
             (int)g_on, g_wraps, g_new_bodies, g_emerges, g_emerges_new, g_waited_out, g_shells, g_glows, g_dropped,
             g_once.size(), game::poses_held());
    std::string out = b;
    for (auto& [id, c] : g_c) {
        if (only != 0xFF && id != only)
            continue;
        void* p = c.puppet && game::creature_listed(c.puppet) ? c.puppet : nullptr;
        float w[3] = {c.spot.x, c.spot.y, c.spot.z}, sc[2] = {0, 0};
        bool vis = H->world_to_screen(w, sc) != 0;
        snprintf(b, sizeof b,
                 "partner %u phase=%s for=%.1f puppet=%p anim=%08X held=%d glow=%d scale=%.3f size=%.3f species=%08X body0=%08X "
                 "seen_hold=%08X seen_emerge=%08X emerged_body=%08X spot=(%.2f %.2f %.2f) screen=%s%.0f %.0f\n",
                 id, phase_name(c.phase), H->time_seconds() - c.at, p, anim_now(p), (int)(p && game::pose_held(p)),
                 (int)(fx_here(c.glow) && game::effect_active(c.glow.fx)), p ? game::scale(p) : 0.f, c.size,
                 p ? game::species_instance(p) : 0u, c.body0, c.seen_hold, c.seen_emerge, c.emerged_body, c.spot.x, c.spot.y,
                 c.spot.z, vis ? "" : "off ", sc[0], sc[1]);
        out += b;
    }
    for (auto& [id, c] : g_last) {
        if (only != 0xFF && id != only)
            continue;
        snprintf(b, sizeof b, "last %u body0=%08X seen_hold=%08X seen_emerge=%08X emerged_body=%08X\n", id, c.body0,
                 c.seen_hold, c.seen_emerge, c.emerged_body);
        out += b;
    }
    return out;
}

} // namespace cocoon
