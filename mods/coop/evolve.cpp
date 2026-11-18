#include "evolve.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "creations.h"
#include "npc.h"
#include "fixes.h"
#include "world.h"
#include "sporekit_game.hpp"
#include "game.h"
#include "modes.h"

namespace evolve {
namespace {
const SkHost* H;
SkMod* M;
bool g_cocoon = true;
COOP_FIX("creature.cocoon", "BUG-9160", g_cocoon,
         "creature: a partner in their own editor stays in the game's cocoon where they stood and comes out with the new body");
bool g_personal = true;
COOP_FIX("creature.personal_editor", "BUG-9160", g_personal,
         "creature: the host's mating call opens the host's own editor (no evolution vote); everyone evolves on their own");
constexpr double kEnterSeconds = 2.4; // gen_evolve_cocoon, then the hold (runtime 2026-11-12)
constexpr double kHoldReplay = 6.0;   // the hold is played again this often (it does not loop by itself)
constexpr double kBodyWait = 15.0;    // back in the world with a new design: until the puppet wears it, this long at most
constexpr double kNoDesignWait = 5.0; // back in the world and no new design came (left without saving): this long
constexpr double kSacAgain = 0.5;     // the glow is made again this soon after it faded
constexpr double kShellEvery = 1.8;   // the woven shell is laid again this often
bool g_egg_watch = true;
COOP_FIX("creature.egg_watch", "BUG-9161", g_egg_watch,
         "creature: an egg scene after a mating that does not end by itself is finished as Escape does (the editor opens)");
constexpr uint32_t kLaidEgg = 0xB6A1DBE1u; // CRG_LaidEgg
constexpr double kEggWatch = 12.0;
double g_egg_since = -1;
bool g_egg_pressed;
uint32_t g_egg_finished;
bool g_family = true;
COOP_FIX("creature.family_species", "BUG-9161", g_family,
         "creature: the own nest family and herd wear the own creature's species (as in the game), so mating and the egg scene work");
uint32_t g_family_herds, g_family_members;
double g_family_at;
bool g_direct = true;
COOP_FIX("creature.mating_direct", "BUG-9175", g_direct,
         "creature: a mating whose pairing has not reached the egg scene after 30 s ends in the editor directly");
// the game's pairing has this long to reach its egg scene (12 s seen on the host); BUG-9175 (2026-11-14): 30 s felt broken
// to the owner (they clicked the mate again and again), 18 s now
constexpr double kStallAfter = 18.0;
constexpr uint32_t kMating = 0x02D852F0u, kMatingAtNest = 0xD23FE7CAu; // the avatar's actions (coop.cpp mate_response)
int g_mate_stage;      // 0 none, 1 in the mating action, 2 in the cocoon, 3 the request made (until the action ends)
double g_mate_at;      // when the stage began
uint32_t g_mate_direct; // tests: matings finished by the mod
uint32_t g_wraps, g_emerges, g_own_emerges;
uint32_t g_prev_mode;
bool g_own_due;
double g_own_due_at;
} // namespace

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
}

bool cocoon_on() { return g_cocoon; }
bool personal_editor() { return g_personal; }

// one of the game's cocoon effects where the creature stands (world effects: the creature does not move meanwhile),
// its up (local z) along the planet's up there (the position from the planet's centre): with the world's z instead
// the sac lay on its side (seen 2026-11-12). `rows`: the 3x3 holds the local axes as rows (else as columns).
float g_fx_scale = 1.f, g_fx_up = 0.f;
bool g_fx_rows = true;
static void* effect_at_pos(const net::Vec3& at, uint32_t id, float scale, float up_by, bool rows)
{
    float len = sqrtf(at.x * at.x + at.y * at.y + at.z * at.z);
    if (!(len > 1.f)) len = 1.f;
    const float u[3] = {at.x / len, at.y / len, at.z / len};
    float r[3] = {u[1], -u[0], 0.f}; // up x z, then normalised (any axis not along up)
    float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (rl < 1e-3f) { r[0] = 1.f; r[1] = 0.f; r[2] = 0.f; rl = 1.f; }
    for (float& v : r) v /= rl;
    const float fw[3] = {u[1] * r[2] - u[2] * r[1], u[2] * r[0] - u[0] * r[2], u[0] * r[1] - u[1] * r[0]};
    uint8_t xf[0x44] = {};
    float* f = (float*)(xf + 4);
    f[0] = at.x + u[0] * up_by; f[1] = at.y + u[1] * up_by; f[2] = at.z + u[2] * up_by;
    f[3] = scale;
    float* m = f + 4;
    const float* axes[3] = {r, fw, u};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[rows ? i * 3 + j : j * 3 + i] = axes[i][j];
    return game::create_world_effect(id, xf);
}
// sized to the creature (2026-11-13: at scale 1 a creature of bounding radius 1.24 fitted in the shell, one of 1.40
// with a long neck stuck its head out): the creature's radius / kShellFits, never below 0.8. `radius`: the size it had
// before it was shrunk inside (0: its own now)
constexpr float kShellFits = 0.9f;
// BUG-9194 (the owner 2026-11-13, a screenshot: the cocoons doubled): the shells laid again every kShellEvery overlap the
// fading one for ~0.2 s; at the creature's position then, a held puppet's small drift showed two shells side by side
// (at the owner's frame rate, not at 10 FPS). Every shell of one cocoon now at the spot it went in.
// `coop.fix creature.cocoon_one_spot off`: at the creature's position each time.
static bool g_one_spot = true;
COOP_FIX("creature.cocoon_one_spot", "BUG-9194", g_one_spot, "creature: every shell of one cocoon is laid at the same spot");
static void* effect_at(void* c, uint32_t id, float radius = 0, const net::Vec3* spot = nullptr)
{
    const float r = radius > 0 ? radius : game::bounding_radius(c);
    const float size = std::isfinite(r) && r > 0 ? std::clamp(r / kShellFits, 0.8f, 4.f) : 1.f;
    return effect_at_pos(g_one_spot && spot ? *spot : game::position(c), id, g_fx_scale * size, g_fx_up, g_fx_rows);
}

// BUG-9181 (the owner, 2026-11-13: "I think growing the cocoon isn't the way. Maybe shrink the creature till they're
// really small? Then grow them back to their stored size when coming out"): inside its cocoon a creature is
// kShrunk of its size, so no neck, tail or wing shows through the shell; it gets its stored size back as it comes out
constexpr float kShrunk = 0.12f;
static void shrink(void* c, Cocoon& cc)
{
    if (!cc.size) {
        cc.size = game::scale(c);
        if (!(cc.size > 0) || !std::isfinite(cc.size)) cc.size = 1.f;
    }
    if (std::fabs(game::scale(c) - cc.size * kShrunk) > 0.001f) game::set_scale(c, cc.size * kShrunk); // (kept: a sync may reset it)
}
static void grow_back(void* c, Cocoon& cc)
{
    if (cc.size > 0 && c && game::object_alive(c)) game::set_scale(c, cc.size);
    cc.size = 0;
}
static void end_sac(Cocoon& c)
{
    if (!c.fx) return;
    if (game::effect_active(c.fx)) game::stop_world_effect(c.fx);
    game::release_world_effect(c.fx);
    c.fx = nullptr;
}

void drop(Cocoon& c, bool keep_state)
{
    end_sac(c);
    if (!keep_state) c = {};
}

// the cocoon on puppet `puppet` (again: a new body is a new puppet): the pose held, inside, its sac around it
static void wrap(void* puppet, Cocoon& c, double now, bool fresh)
{
    if (!c.radius) c.radius = game::bounding_radius(puppet);
    if (!c.spot_set || c.puppet != puppet) {
        c.spot = game::position(puppet);
        c.spot_set = true;
    }
    game::hold_pose(puppet, true);
    game::play_animation(puppet, fresh ? kCocoonEnter : kCocoonHold, -1, true);
    end_sac(c);
    if (void* fx = effect_at(puppet, kFxCreate, c.radius, &c.spot)) game::release_world_effect(fx); // the woven shell, ~2 s, ends by itself
    c.shell_at = now;
    if (!fresh) {
        shrink(puppet, c); // inside now
        c.fx = effect_at(puppet, kFxHold, c.radius, &c.spot);
        c.fx_at = now;
    }
    c.puppet = puppet;
    c.played = now;
}
// the sac fades after a few seconds and the hold pose ends: both again (the owner, 2026-11-12: "the cocoons weren't
// visible to MSI, even after a minute")
static void keep(void* puppet, Cocoon& c, double now)
{
    if (c.puppet != puppet) { c.size = 0; c.radius = 0; c.spot_set = false; wrap(puppet, c, now, false); return; } // (a new body: its own size)
    // the woven shell lasts ~2 s (runtime 2026-11-13: full 0.2 s after it was made, gone at 2.0 s): the next one
    // before it fades, so the cocoon stays closed while the partner edits
    if (now - c.shell_at >= kShellEvery) {
        if (void* fx = effect_at(puppet, kFxCreate, c.radius, &c.spot)) game::release_world_effect(fx);
        c.shell_at = now;
    }
    shrink(puppet, c);
    if ((!c.fx || !game::effect_active(c.fx)) && now - c.fx_at >= kSacAgain) {
        end_sac(c);
        c.fx = effect_at(puppet, kFxHold, c.radius, &c.spot);
        c.fx_at = now;
    }
    uint32_t anim = 0;
    if (now - c.played >= kHoldReplay && (!game::drawn_state(puppet, nullptr, &anim) || anim != kCocoonHold)) {
        game::play_animation(puppet, kCocoonHold, -1, true);
        c.played = now;
    }
}

// BUG-9194 (owner play test 2026-11-14: "they stand still, the cocoon appears, then disappears, then a new cocoon appears
// and shrinks them"): the first shell (~2 s) faded before the step to the hold at kEnterSeconds, and that step treated the
// body as a new one (c.puppet cleared): a second shell and the shrink to kShrunk at once. Now one cocoon from start to
// end: while going in the shell is laid again on time and the creature shrinks with the spin; the step to the hold keeps
// the body and its shell timer (the hold pose and the sac only). `coop.fix creature.cocoon_smooth off`: as before.
static bool g_smooth = true;
COOP_FIX("creature.cocoon_smooth", "BUG-9194", g_smooth, "creature: one cocoon while going in (no second shell), the creature shrinks with the spin");
static void entering(void* c, Cocoon& cc, double now)
{
    if (now - cc.shell_at >= kShellEvery) {
        if (void* fx = effect_at(c, kFxCreate, cc.radius, &cc.spot)) game::release_world_effect(fx);
        cc.shell_at = now;
    }
    if (!cc.size) {
        cc.size = game::scale(c);
        if (!(cc.size > 0) || !std::isfinite(cc.size)) cc.size = 1.f;
    }
    const float t = (float)std::clamp((now - cc.at) / kEnterSeconds, 0.0, 1.0);
    const float want = cc.size * (1.f - (1.f - kShrunk) * t);
    if (std::fabs(game::scale(c) - want) > 0.002f) game::set_scale(c, want);
}
// the step from going in to holding: the same body, the hold pose and its sac now (keep() lays the next shell on time)
static void to_hold(void* c, Cocoon& cc, double now)
{
    if (g_smooth) cc.played = -1e9; // the hold pose at once
    else cc.puppet = nullptr;        // (before: a new body for keep(), a second shell)
    keep(c, cc, now);
}

// BUG-9181: the family's cocoons (below)
static void family_in(uint8_t who, void* body, double now);
static void family_keep(uint8_t who, double now);
static void family_out(uint8_t who);

void puppet_editing(uint8_t player, void* puppet, Cocoon& c, double now)
{
    if (!g_cocoon || !puppet) return;
    if (c.state == 0 || c.state == 3) { // in (again: back to its editor before it came out)
        const bool fresh = c.state == 0;
        const uint32_t key = fresh ? creations::player_key(player, creations::kCreature).instance : c.key;
        wrap(puppet, c, now, fresh);
        c.state = fresh ? 1 : 2;
        c.at = now;
        c.key = key;
        if (fresh) ++g_wraps;
        if (fresh) family_in(player, puppet, now); // BUG-9181: its family too
        return;
    }
    family_keep(player, now);
    if (c.state == 1 && now - c.at >= kEnterSeconds) {
        to_hold(puppet, c, now); // the hold and its sac now
        c.state = 2;
        c.at = now;
        return;
    }
    if (c.state == 1 && g_smooth) entering(puppet, c, now);
    if (c.state == 2) keep(puppet, c, now);
}

void puppet_back(uint8_t player, void* puppet, Cocoon& c, double now)
{
    if (!c.state) return;
    if (!puppet || !g_cocoon) {
        if (puppet) { game::hold_pose(puppet, false); grow_back(puppet, c); }
        drop(c);
        return;
    }
    if (c.state != 3) { // back: still in the cocoon until the new body is there
        c.state = 3;
        c.at = now;
        family_out(player); // BUG-9181: the family comes out first
    }
    const uint32_t key = creations::player_key(player, creations::kCreature).instance;
    const bool designed = key && key != c.key;
    const bool worn = designed && game::species_instance(puppet) == key;
    if (!worn && !(designed ? now - c.at >= kBodyWait : now - c.at >= kNoDesignWait)) {
        keep(puppet, c, now);
        return;
    }
    grow_back(c.puppet && game::object_alive(c.puppet) ? c.puppet : puppet, c);
    drop(c);
    game::hold_pose(puppet, false); // (BUG-9181: no emerge: the partner hatches from its egg on its own game)
    ++g_emerges;
}

// BUG-9181 (the owner's sequence, 2026-11-13): when a player goes into its editor after a mating, its living family
// (its species, within kFamilyRange of it; never the egg) spins into the game's cocoon on every game; when it is back,
// the family comes out first and the player hatches from its egg the game's own way (no cocoon of the player's own body
// on its own game, no own emerge). Kept per player: kOwnFamily for this game's own player.
constexpr uint8_t kOwnFamily = 0xFE;
constexpr float kFamilyRange = 50.f;
struct Wrapped {
    void* c;
    uint32_t serial;
    Cocoon cc;
    net::Vec3 at{};       // where it went in: held there (the host's members run their own AI: one walked half out)
    net::Quat rot{};
};
std::map<uint8_t, std::vector<Wrapped>> g_family_cocoons;
uint32_t g_family_wrapped, g_family_out;

static bool wrapped_alive(const Wrapped& w)
{
    return w.c && game::object_alive(w.c) && game::object_serial(w.c) == w.serial && !game::is_dead(w.c);
}

static bool g_family_own_species = true;
COOP_FIX("creature.family_own_species", "BUG-9193", g_family_own_species,
         "creature: a partner playing our species (no creature yet) does not put our family into cocoons");
// the living family of `body` (its species near it) goes into the cocoon
static void family_in(uint8_t who, void* body, double now)
{
    auto& list = g_family_cocoons[who];
    if (!list.empty() || !body || !g_cocoon) return;
    struct Find {
        void *body, *avatar, *profile;
        net::Vec3 at;
        std::vector<Wrapped>* list;
    } f{body, game::avatar(), game::species_profile(body), game::position(body), &list};
    if (!f.profile) return;
    // BUG-9193: a partner without a creature of its own plays this game's species (a late joiner in its first editor):
    // its "family" here is ours; nobody of ours goes in for it. `coop.fix creature.family_own_species off`: as before.
    if (g_family_own_species && who != kOwnFamily && f.avatar && game::species_profile(f.avatar) == f.profile) {
        H->log(M, SK_INFO, "evolve: a partner who plays our species evolves: our family stays out of the cocoon (BUG-9193)");
        return;
    }
    game::for_each_object([](void* c, uint32_t noun, void* u) {
        auto& f = *static_cast<Find*>(u);
        if (noun != 0x18EB45Eu || c == f.body || c == f.avatar || !game::object_alive(c) || game::is_dead(c) ||
            game::species_profile(c) != f.profile || net::dist(game::position(c), f.at) > kFamilyRange)
            return true;
        f.list->push_back({c, game::object_serial(c), {}, game::position(c), game::orientation(c)});
        return true;
    }, &f);
    for (auto& w : list) {
        wrap(w.c, w.cc, now, true); // the spin into the cocoon (gen_evolve_cocoon), the woven shell
        w.cc.state = 1;
        w.cc.at = now;
    }
    g_family_wrapped += (uint32_t)list.size();
    if (!list.empty())
        H->log(M, SK_INFO, "evolve: %zu of %s family into the cocoon (BUG-9181)", list.size(),
               who == kOwnFamily ? "our" : "a partner's");
}

static void family_keep(uint8_t who, double now)
{
    auto it = g_family_cocoons.find(who);
    if (it == g_family_cocoons.end()) return;
    for (auto& w : it->second) {
        if (!wrapped_alive(w)) continue;
        if (w.cc.state == 1 && now - w.cc.at >= kEnterSeconds) {
            if (!g_smooth) w.cc.puppet = nullptr; // the hold and its sac now
            else w.cc.played = -1e9;
            w.cc.state = 2;
        }
        if (w.cc.state == 1 && g_smooth) entering(w.c, w.cc, now);
        if (w.cc.state == 2) keep(w.c, w.cc, now);
        // (while going in, the spin's own small hop is left alone: back only when it wandered off)
        const float off = g_smooth && w.cc.state == 1 ? 1.5f : 0.2f;
        if (net::dist(game::position(w.c), w.at) > off) game::teleport(w.c, w.at, w.rot);
    }
}

// the family comes out (the game's emerge and its burst), first, before the player hatches
static void family_out(uint8_t who)
{
    auto it = g_family_cocoons.find(who);
    if (it == g_family_cocoons.end()) return;
    for (auto& w : it->second) {
        drop(w.cc, true);
        if (!wrapped_alive(w)) continue;
        grow_back(w.c, w.cc);
        game::hold_pose(w.c, false);
        game::play_animation(w.c, kEmerge, -1, true);
        if (void* fx = effect_at(w.c, kFxHatch, w.cc.radius)) game::release_world_effect(fx);
        ++g_family_out;
    }
    g_family_cocoons.erase(it);
}

void own_tick(uint32_t mode, void* avatar, bool busy, double now)
{
    const bool editor = mode == modes::kEdit || mode == modes::kEditor;
    if (g_prev_mode && (g_prev_mode == modes::kEdit || g_prev_mode == modes::kEditor) && mode == modes::kCreature) {
        g_own_due = true;
        g_own_due_at = now;
    }
    if (mode) g_prev_mode = mode;
    if (editor || mode != modes::kCreature) return;
    if (!g_own_due) return;
    if (!g_cocoon || now - g_own_due_at > 60) { g_own_due = false; family_out(kOwnFamily); return; }
    if (busy || !avatar || now - g_own_due_at < 1.0) return; // after the game's own fade from the editor
    g_own_due = false;
    // the family first; the own creature hatches from its egg the game's way (before: an own emerge, "started in
    // their egg like normal, then turned into a cocoon", the owner 2026-11-13)
    family_out(kOwnFamily);
    ++g_own_emerges;
}

void egg_tick(uint32_t mode, double now)
{
    if (mode != modes::kCreature || world::cinematic_id() != kLaidEgg) {
        g_egg_since = -1;
        g_egg_pressed = false;
        return;
    }
    if (g_egg_since < 0) g_egg_since = now; // the game's own egg scene (BUG-9181: no cocoon instead of it)
    if (!g_egg_watch || g_egg_pressed || now - g_egg_since < kEggWatch) return;
    g_egg_pressed = true;
    // the editor opens now: our family goes into the cocoon (seen when we are back)
    if (void* av = game::avatar()) family_in(kOwnFamily, av, now);
    HWND w = (HWND)H->game_window();
    if (!w) return;
    const LPARAM lp = 1 | (MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC) << 16);
    PostMessageW(w, WM_KEYDOWN, VK_ESCAPE, lp);
    PostMessageW(w, WM_KEYUP, VK_ESCAPE, lp | 0xC0000000);
    ++g_egg_finished;
    H->log(M, SK_INFO, "evolve: the egg scene ends after %.1f s (BUG-9161): Escape for the player, the editor opens",
           now - g_egg_since);
}

// BUG-9175 / BUG-9181: the game's pairing runs on every game now (a guest's mate is lent to its own AI, coop.cpp
// mate_keeper); only a pairing that has not reached its egg scene after kStallAfter s ends in the editor directly
void mating_tick(uint32_t mode, void* avatar, bool guest, bool busy, double now, Request request)
{
    (void)guest;
    void* ctl = mode == modes::kCreature && avatar ? skg::field<void*>(avatar, 0xB4C) : nullptr;
    const uint32_t action = ctl ? skg::field<uint32_t>(ctl, 0x1D8) : 0;
    const bool mating = action == kMating || action == kMatingAtNest;
    if (!ctl && g_mate_stage == 3) return; // in the editor it asked for: armed again only once the action is seen ended
    if (!g_direct || !mating || world::cinematic_id() == kLaidEgg) {
        g_mate_stage = 0;
        return;
    }
    if (g_mate_stage == 0) {
        g_mate_stage = 1;
        g_mate_at = now;
    }
    if (busy || !request || g_mate_stage != 1 || now - g_mate_at < kStallAfter) return;
    g_mate_stage = 3; // once per mating action: armed again when the action ended
    ++g_mate_direct;
    family_in(kOwnFamily, avatar, now);
    H->log(M, SK_WARN, "evolve: the game's pairing stalled %.0f s: the editor directly (BUG-9175)", now - g_mate_at);
    H->run_outside_frame([](void* f) {
        const int r = reinterpret_cast<Request>(f)();
        H->log(M, r < 0 ? SK_WARN : SK_INFO, "evolve: own editor after the stalled mating -> %d (BUG-9175)", r);
    }, reinterpret_cast<void*>(request));
}

void family_tick(uint32_t mode, void* avatar, bool busy, double now)
{
    if (!g_family || mode != modes::kCreature || !avatar || busy || now - g_family_at < 2.0) return;
    g_family_at = now;
    void* herd = game::herd_of(avatar);
    void* prof = game::species_profile(avatar);
    if (!herd || !prof || !game::herd_nest(herd)) return;
    static const uintptr_t set_species = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    if (!set_species) return;
    struct Ctx {
        void *herd, *prof, *avatar;
        uintptr_t set;
        uint32_t changed;
    } x{herd, prof, avatar, set_species, 0};
    // the herd's own species (+0xA4) stays: with it on the own creature's the game offered no mate any more (2026-11-13:
    // every member class 7 after the mating call; the herd's older species back: a member class 9 again)
    const bool herd_old = false;
    game::for_each_object([](void* creature, uint32_t noun, void* p) {
        auto& x = *static_cast<Ctx*>(p);
        // (not a guest's copies of the host's members: they come with the right species since BUG-9180; converted here
        // they took the host's species while the avatar still wore it after a respawn, the MSI 2026-11-13)
        if (noun != 0x18EB45Eu || creature == x.avatar || game::is_puppet(creature) || npc::is_proxy(creature) ||
            !game::object_alive(creature) ||
            game::is_dead(creature) || game::herd_of(creature) != x.herd || game::species_profile(creature) == x.prof)
            return true;
        const int age = *(int*)((uint8_t*)creature + 0xB34);
        skg::thiscall<bool>(x.set, creature, x.prof, age, true);
        ++x.changed;
        return true;
    }, &x);
    if (herd_old || x.changed) {
        g_family_herds += herd_old;
        g_family_members += x.changed;
        H->log(M, SK_INFO, "evolve: the own family follows the own creature's species %08X: herd %s, %u member(s) (BUG-9161)",
               game::species_key(avatar).instance, herd_old ? "changed" : "kept", x.changed);
    }
}

std::string debug()
{
    char b[260];
    snprintf(b, sizeof b,
             "evolve: cocoon=%d personal_editor=%d wraps=%u emerges=%u own_emerges=%u own_due=%d egg_watch=%d finished=%u fx scale %.2f up %.2f rows %d\n",
             (int)g_cocoon, (int)g_personal, g_wraps, g_emerges, g_own_emerges, (int)g_own_due, (int)g_egg_watch,
             g_egg_finished, g_fx_scale, g_fx_up, (int)g_fx_rows);
    std::string out = b;
    snprintf(b, sizeof b, "family: fix=%d herds=%u members=%u\n", (int)g_family, g_family_herds, g_family_members);
    out += b;
    snprintf(b, sizeof b, "mating: direct=%d stage=%d finished_by_mod=%u\n", (int)g_direct, g_mate_stage, g_mate_direct);
    out += b;
    return out;
}

static uint32_t fnv1(const char* s)
{
    uint32_t h = 0x811C9DC5u;
    for (; *s; ++s) h = (h * 0x01000193u) ^ (uint8_t)tolower((unsigned char)*s);
    return h;
}

int command(int argc, const char* const* argv, std::string& out)
{
    // tests: an effect by name (FNV-1 of the lower-case name) or 0x id at the own creature: world (`fx`) or on it (`cfx`)
    if (argc > 2 && (!strcmp(argv[1], "fx") || !strcmp(argv[1], "cfx")) && game::avatar()) {
        std::string res;
        for (int i = 2; i < argc; ++i) {
            const uint32_t id = !strncmp(argv[i], "0x", 2) ? (uint32_t)strtoul(argv[i], nullptr, 16) : fnv1(argv[i]);
            void* fx = nullptr;
            if (!strcmp(argv[1], "cfx")) fx = game::create_creature_effect(game::avatar(), -1, id);
            else fx = effect_at(game::avatar(), id);
            char b[96];
            snprintf(b, sizeof b, "%s %08X: %s\n", argv[i], id, fx ? "made" : "none");
            res += b;
        }
        out = res;
        return 0;
    }
    // tests: an animation on the own creature (`hold`: kept in its pose), `release`: the pose free again
    void* avatar = game::avatar();
    if (argc > 2 && !strcmp(argv[1], "calls")) { // RE: every `call rel32` / `jmp rel32` to a .sym address in the game's code
        const uint32_t target = (uint32_t)strtoul(argv[2], nullptr, 16);
        auto* base = (uint8_t*)GetModuleHandleA(nullptr);
        auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        const uintptr_t live = (uintptr_t)base + target - 0x400000;
        std::string res;
        int hits = 0;
        for (unsigned s = 0; s < nt->FileHeader.NumberOfSections && hits < 40; ++s, ++sec) {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const uint8_t* a = base + sec->VirtualAddress;
            const size_t n = sec->Misc.VirtualSize;
            for (size_t i = 0; i + 5 <= n && hits < 40; ++i) {
                if (a[i] != 0xE8 && a[i] != 0xE9) continue;
                int32_t rel;
                memcpy(&rel, a + i + 1, 4);
                if ((uintptr_t)(a + i + 5) + rel != live) continue;
                char b[40];
                snprintf(b, sizeof b, "%s %08X\n", a[i] == 0xE8 ? "call" : "jmp", (unsigned)(a + i - base + 0x400000));
                res += b;
                ++hits;
            }
        }
        out = res.empty() ? "none\n" : res;
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "find")) { // RE: a byte pattern (hex, ?? any byte) in the game's code, as .sym addresses
        std::vector<int> pat;
        for (const char* p = argv[2]; p[0] && p[1]; p += 2)
            pat.push_back(p[0] == '?' ? -1 : (int)strtoul(std::string(p, 2).c_str(), nullptr, 16));
        auto* base = (uint8_t*)GetModuleHandleA(nullptr);
        auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        std::string res;
        int hits = 0;
        for (unsigned s = 0; s < nt->FileHeader.NumberOfSections && hits < 40; ++s, ++sec) {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const uint8_t* a = base + sec->VirtualAddress;
            const size_t n = sec->Misc.VirtualSize;
            for (size_t i = 0; i + pat.size() <= n && hits < 40; ++i) {
                size_t k = 0;
                while (k < pat.size() && (pat[k] < 0 || a[i + k] == pat[k])) ++k;
                if (k != pat.size()) continue;
                char b[32];
                snprintf(b, sizeof b, "%08X\n", (unsigned)(a + i - base + 0x400000));
                res += b;
                ++hits;
            }
        }
        out = res.empty() ? "none\n" : res;
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "fxset")) { // tests: the cocoon effects' scale, lift (m) and axes (rows 1 / cols 0)
        g_fx_scale = (float)atof(argv[2]);
        g_fx_up = (float)atof(argv[3]);
        g_fx_rows = atoi(argv[4]) != 0;
        out = debug();
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "anim") && avatar) {
        const uint32_t id = (uint32_t)strtoul(argv[2], nullptr, 16);
        const bool hold = argc > 3 && !strcmp(argv[3], "hold");
        game::hold_pose(avatar, hold);
        game::play_animation(avatar, id, -1, true);
        char b[96];
        snprintf(b, sizeof b, "played %08X on the own creature%s\n", id, hold ? " (held)" : "");
        out = b + debug();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "release") && avatar) {
        game::hold_pose(avatar, false);
        out = "released\n" + debug();
        return 0;
    }
    out = debug();
    return 0;
}

} // namespace evolve
