// Every player's own nest (home_nests.h; BUG-9262, ported from PR #196's home_nests.cpp: the Lineages tickets named in
// the comments are that branch's, kept for the history; every switch here is BUG-9262's).
#include "home_nests.h"
#include "lifecycle.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "creations.h"
#include "fixes.h"
#include "founders.h"
#include "game.h"
#include "net/interp.h"
#include "party.h"
#include "social.h"
#include "sporekit_game.hpp"
#include "world.h"

namespace home_nests {

static const SkHost* H;
static SkMod* M;
static Ctx C;

static bool g_fix = true;
COOP_FIX("creature.own_nests", "BUG-9262", g_fix,
         "creature: every player with their own species has their own nest next to the host's (hatch, respawn)");

// (Lineages BUG-9210: "'Bringing Your Nest' only happened after the player spawned into the world"): the host's
// new-world intro (a cinematic, the game paused) held every seat's nest for its whole length while the guests already
// played at the host's nest. Nests are made during it too, and a guest's first world waits for its nest
// (world::set_share_hold). Off: the nests after the intro, the world at once.
static bool g_nest_first = true;
COOP_FIX("creature.nest_before_world", "BUG-9262", g_nest_first,
         "creature: a guest's own nest is made during the host's intro and its world waits for it (no reload after spawning)");

constexpr uint32_t kNestNoun = 0x52AA6122; // cNest
constexpr uint32_t kCrtType = 0x2B978C46, kCrtGroup = 0x40626200;
constexpr uint32_t kCreatureNoun = 0x18EB45Eu; // cCreatureAnimal
constexpr float kNear = 160.f;             // a seat's nest belongs to the host's current home within this
constexpr int kFamily = 4;                 // the members the game keeps in a seat's nest herd

struct SeatNest {
    void* herd = nullptr;
    net::Vec3 pos{};
    uint32_t species = 0;
    int32_t rate = 0; // the game's respawn time while family_back holds it short
};
static std::array<SeatNest, founders::kMaxSeats> g_nests;
constexpr double kFillEvery = 10;                          // fill_nest: at most every 10 s per seat
static std::array<double, founders::kMaxSeats> g_filled_at{};
static uint32_t g_members_made;
static void fill_nest(void* ctx);
static double g_next;
static bool g_queued;
static uint32_t g_made, g_failed, g_rebuilt, g_ally_set, g_shares, g_followed, g_follow_members;
// (Lineages BUG-9151): a player who evolves (or picks another creature of their own) keeps their nest: the herd (+0xA4)
// and its family take the new species in place, as the game's own evolution does with the player's family.
// (Lineages BUG-9167): the world for a player's new nest goes out once the game put a member in the new herd (it
// respawns members after its respawn time): an empty copy on the guest died out or could not be joined.
static bool g_share_member = true;
COOP_FIX("creature.nest_share_member", "BUG-9262", g_share_member,
         "creature: the world for a player's new nest goes out once the nest has a member (90 s at most)");
constexpr double kShareWait = 90;
static bool g_family_back = true;
COOP_FIX("creature.nest_family_back", "BUG-9262", g_family_back,
         "creature: a player's nest with no living member gets members of its species (a new nest, a family that died)");
static uint32_t g_family_backs;
struct ShareDue {
    uint8_t player = 0xFF;
    double since = -1;
};
static std::array<ShareDue, founders::kMaxSeats> g_share_due;
static uint32_t g_share_waits;
static bool g_follow = true;
COOP_FIX("creature.nest_follows_evolution", "BUG-9262", g_follow,
         "creature: a partner who evolves keeps their nest; its herd and family take the new species (no second nest, no world resend)");
static std::string g_last;

static bool is_host();
static bool host_home(net::Vec3& out);
static void* nest_herd_near(void* profile, const net::Vec3& home, float r, net::Vec3* at);
// (Lineages BUG-9215): a player who joined while the host was on its death screen got the world saved with the host's
// dead avatar and arrived dead. The joiner's world now waits while the host is dead (the hold's 2 minutes at most) and
// the host is told once to respawn. Off: shared at once.
static bool g_dead_hold = true;
COOP_FIX("creature.join_after_respawn", "BUG-9262", g_dead_hold,
         "creature: a player joining while the host is dead gets the world once the host respawned (2 min at most)");
static double g_dead_told;

// `player`'s own nest is about to be made (or waits for its first member): its world waits (world::set_share_hold)
static const char* g_hold_why = "-"; // the last answer of nest_due (status, tests)
static bool nest_due_why(uint8_t player, uint64_t waited_ms)
{
    if (g_dead_hold && is_host() && game::in_creature_stage()) {
        void* av = game::avatar();
        if (av && game::is_dead(av)) {
            const double now = GetTickCount64() / 1000.0;
            if (C.notice && now - g_dead_told > 30) {
                g_dead_told = now;
                C.notice(C.sess->name_of(player) + " is joining: respawn so they can enter the world");
            }
            g_hold_why = "the host is dead";
            return true;
        }
    }
    if (!g_fix || !g_nest_first || !is_host() || !game::in_creature_stage())
        return g_hold_why = "off / not the creature stage", false;
    if (void* av = game::avatar(); !av || game::is_dead(av))
        return g_hold_why = "no host avatar", false;
    const int seat = party::seat_of_player(player);
    const auto& roster = party::roster();
    // a player who just joined: its creature (and so its seat's species) comes a few seconds after (15 s at most)
    if (seat <= 0 || seat >= (int)roster.seats.size())
        return g_hold_why = "no seat yet", waited_ms < 15000;
    const uint32_t species = roster.seats[seat].species;
    if (!species)
        return g_hold_why = "no species yet", waited_ms < 15000;
    if (species == roster.seats[0].species)
        return g_hold_why = "the host's species", false; // the host's nest
    if (seat < (int)g_share_due.size() && g_share_due[seat].since >= 0)
        return g_hold_why = "made, waits for a member", true;
    net::Vec3 home;
    if (!host_home(home))
        return g_hold_why = "no host home", false;
    void* profile = game::species_by_key({species, kCrtType, kCrtGroup});
    if (!profile)
        return g_hold_why = "species not here yet", true;
    const bool none = !nest_herd_near(profile, home, kNear, nullptr);
    g_hold_why = none ? "its nest is about to be made" : "its nest is there";
    return none;
}
static bool nest_due(uint8_t player, uint64_t waited_ms)
{
    const char* before = g_hold_why;
    const bool hold = nest_due_why(player, waited_ms);
    if (g_hold_why != before)
        H->log(M, SK_INFO, "home nests: player %u's world share %s (%s, %llu ms)", player, hold ? "waits" : "may go", g_hold_why,
               (unsigned long long)waited_ms);
    return hold;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    world::set_share_hold(nest_due);
}

bool enabled() { return g_fix; }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }

static bool live_herd(void* h)
{
    return h && game::object_alive(h) && skg::readable(h, 0x21C) && game::noun_id(h) == game::kHerdNoun;
}

static bool nest_pos_of(void* herd, net::Vec3& out)
{
    void* nest = live_herd(herd) ? game::herd_nest(herd) : nullptr;
    return nest && game::object_alive(nest) && game::object_position(nest, out);
}

// the nest herd of `profile` nearest to `at` within `within` m that is not extinct (+0x190), or null
static void* nest_herd_near(void* profile, const net::Vec3& at, float within, net::Vec3* pos)
{
    struct F {
        void* profile;
        net::Vec3 at;
        float best;
        void* herd = nullptr;
        net::Vec3 pos{};
    } f{profile, at, within};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto& f = *(F*)u;
            net::Vec3 q;
            if (noun == game::kHerdNoun && live_herd(o) && game::herd_species(o) == f.profile && !skg::field<uint8_t>(o, 0x190) &&
                nest_pos_of(o, q)) {
                float d = net::dist(q, f.at);
                if (d < f.best) {
                    f.best = d;
                    f.herd = o;
                    f.pos = q;
                }
            }
            return true;
        },
        &f);
    if (pos && f.herd)
        *pos = f.pos;
    return f.herd;
}

// (Lineages BUG-9167, BUG-9173, BUG-9246): the spot of a new nest: flat land around it, at the home's height, clear of
// other nests, never under the sea; else the closest in height (up to 20 m)
static bool site_near(const net::Vec3& home, net::Vec3& out)
{
    const float r = std::sqrt(home.x * home.x + home.y * home.y + home.z * home.z);
    const float h0 = game::ground_radius(home);
    if (!(r > 1) || !std::isfinite(h0))
        return false;
    const float water = game::water_radius(); // 0: unknown (no check)
    auto wet = [&](float g) { return water > 1 && g < water + 1.f; };
    net::Vec3 n{home.x / r, home.y / r, home.z / r};
    net::Vec3 u{n.z, 0, -n.x};
    float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (ul < 1e-3f) {
        u = {0, -n.z, n.y};
        ul = std::sqrt(u.y * u.y + u.z * u.z);
    }
    u = {u.x / ul, u.y / ul, u.z / ul};
    net::Vec3 v{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};
    std::vector<net::Vec3> nests;
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            net::Vec3 q;
            if (noun == kNestNoun && game::object_alive(o) && game::object_position(o, q))
                ((std::vector<net::Vec3>*)p)->push_back(q);
            return true;
        },
        &nests);
    // the land around the spot at the home's height too (rings of 10 and 20 m): a nest on a small island got no member
    auto flat_around = [&](const net::Vec3& c) {
        for (float rr : {10.f, 20.f})
            for (int j = 0; j < 8; j++) {
                const float b = j * 45 * 0.0174533f;
                net::Vec3 p{c.x + rr * (u.x * std::cos(b) + v.x * std::sin(b)), c.y + rr * (u.y * std::cos(b) + v.y * std::sin(b)),
                            c.z + rr * (u.z * std::cos(b) + v.z * std::sin(b))};
                const float g = game::ground_radius(p);
                if (!std::isfinite(g) || std::fabs(g - h0) > 6 || wet(g))
                    return false;
            }
        return true;
    };
    struct {
        int no_ground = 0, height = 0, near_nest = 0, wet = 0;
        float best_dh = 1e9f;
        net::Vec3 best{};
    } why;
    // the spots tried: 12 directions at 60, 50, 70, 45, 80 m, then 24 directions at 55-100 m
    std::vector<std::pair<float, float>> spots; // (distance m, direction degrees)
    for (float dist : {60.f, 50.f, 70.f, 45.f, 80.f})
        for (int k = 0; k < 12; k++)
            spots.push_back({dist, k * 30 + dist});
    for (float dist : {55.f, 65.f, 75.f, 90.f, 100.f})
        for (int k = 0; k < 24; k++)
            spots.push_back({dist, k * 15 + 7.5f});
    for (int pass = 0; pass < 2; ++pass) // 0: flat land around it, 1: the spot alone
        for (const auto& [dist, deg] : spots) {
            const float a = deg * 0.0174533f;
            net::Vec3 q{home.x + dist * (u.x * std::cos(a) + v.x * std::sin(a)), home.y + dist * (u.y * std::cos(a) + v.y * std::sin(a)),
                        home.z + dist * (u.z * std::cos(a) + v.z * std::sin(a))};
            const float g = game::ground_radius(q), len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
            if (!std::isfinite(g) || g < 1 || len < 1) {
                why.no_ground += pass == 1;
                continue;
            }
            q = {q.x * g / len, q.y * g / len, q.z * g / len};
            if (wet(g)) {
                why.wet += pass == 1;
                continue;
            }
            bool clear = true;
            for (const auto& o : nests)
                clear &= net::dist(o, q) >= 25.f;
            if (!clear) {
                why.near_nest += pass == 1;
                continue;
            }
            if (std::fabs(g - h0) > 6) {
                why.height += pass == 1;
                if (std::fabs(g - h0) < why.best_dh) {
                    why.best_dh = std::fabs(g - h0);
                    why.best = q;
                }
                continue;
            }
            if (pass == 1 || flat_around(q)) {
                if (pass == 1)
                    H->log(M, SK_INFO, "home nests: no spot with flat land around it 45-100 m from the host's nest: one without");
                out = q;
                return true;
            }
        }
    static int told = 0;
    if (told++ < 5)
        H->log(M, SK_WARN, "home nests: no spot at the home's height (radius %.1f): %d no ground, %d off by more than 6 m "
               "(the closest %.1f m), %d within 25 m of a nest, %d under the sea", h0, why.no_ground, why.height,
               why.best_dh < 1e8f ? why.best_dh : -1.f, why.near_nest, why.wet);
    if (why.best_dh < 20.f) {
        out = why.best;
        return true;
    }
    return false;
}

// (Lineages BUG-9234): while the host's species migrates (player property 0x514A219 is 1 or 2), the host's home is the
// migration's target nest (CreatureGame_NestHerdFor(1)): a partner's nest is made next to it before the migration's
// world goes out, so the partner loads once. Off: the nest the avatar has.
static bool g_migration_target_fix = true;
COOP_FIX("creature.nest_at_migration_target", "BUG-9262", g_migration_target_fix,
         "creature: during a migration a partner's nest is made at the new nest at once (one world load, not two)");
static bool migration_target(net::Vec3& out)
{
    static uintptr_t prop = H->sym("SporeCoop::Player_GetProperty"), herd_for = H->sym("SporeCoop::CreatureGame_NestHerdFor"),
                     nest_pos = H->sym("SporeCoop::Herd_NestPosition");
    void* nm = game::noun_manager();
    void* player = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? *(void**)((uint8_t*)nm + 0x74) : nullptr;
    if (!prop || !herd_for || !nest_pos || !player)
        return false;
    const int st = skg::thiscall<int>(prop, player, 0x514A219u);
    if (st != 1 && st != 2)
        return false;
    void* h = skg::cdecl_call<void*>(herd_for, 1);
    if (!h || !skg::readable(h, 0x168) || !game::herd_nest(h) || h == game::avatar_herd())
        return false;
    const float* q = skg::thiscall<const float*>(nest_pos, h);
    if (!q || !skg::readable((void*)q, 12))
        return false;
    out = {q[0], q[1], q[2]};
    return true;
}
static bool host_home(net::Vec3& out)
{
    if (g_migration_target_fix && migration_target(out))
        return true;
    void* av = game::avatar();
    return av && game::nest_position(av, out);
}

// the seat whose nest is missing near the host's home gets one (outside the frame: it creates game objects)
static void make_nest(void* ctx)
{
    const int seat = (int)(intptr_t)ctx;
    g_queued = false;
    if (!g_fix || !is_host() || !game::in_creature_stage() || (!g_nest_first && game::cinematic_playing()) ||
        world::load_queued())
        return;
    const auto& roster = party::roster();
    if (seat <= 0 || seat >= (int)roster.seats.size())
        return;
    const uint32_t species = roster.seats[seat].species;
    void* profile = species ? game::species_by_key({species, kCrtType, kCrtGroup}) : nullptr;
    net::Vec3 home, site;
    if (!profile || !host_home(home) || nest_herd_near(profile, home, kNear, nullptr))
        return;
    if (!site_near(home, site)) {
        if (g_failed++ < 5)
            H->log(M, SK_WARN, "home nests: no spot 45-100 m from the host's nest for seat %d", seat);
        return;
    }
    void* herd = game::create_nest_herd(site, profile, kFamily, false);
    net::Vec3 at;
    if (!herd || !nest_pos_of(herd, at)) {
        if (g_failed++ < 5)
            H->log(M, SK_WARN, "home nests: the game made no nest herd for seat %d (species %08X)", seat, species);
        return;
    }
    // the host's nest's brain range: the relevance pass disables a nest herd whose range misses the brain level
    if (void* own = game::avatar_herd(); live_herd(own)) {
        skg::field<int32_t>(herd, 0x120) = skg::field<int32_t>(own, 0x120);
        skg::field<int32_t>(herd, 0x124) = skg::field<int32_t>(own, 0x124);
    }
    static uintptr_t best = H->sym("SporeCoop::cHerd_SetBestFriends");
    if (best)
        skg::thiscall<void>(best, herd, true);
    if (social::set_party_ally(species)) // decision 7: the party's species are best friends
        g_ally_set++;
    // an offline player's nest must load without them: their creature goes with the save (creations.cpp archive)
    std::string err;
    const uint8_t player = party::player_of_seat(seat);
    if (player != party::kNobody && !creations::archive_guest_model(player, world::current_game_key(), err))
        H->log(M, SK_WARN, "home nests: seat %d's creature not archived: %s", seat, err.c_str());
    const bool rebuilt = g_nests[seat].species == species;
    (rebuilt ? g_rebuilt : g_made)++;
    g_nests[seat] = {herd, at, species};
    if (g_family_back) { // its family now (CreateHerd made none): the world below goes out with it
        g_filled_at[seat] = H->time_seconds();
        fill_nest((void*)(intptr_t)seat);
    }
    char b[200];
    snprintf(b, sizeof b, "home nests: seat %d (%s) has its own nest of %08X at (%.0f %.0f %.0f), %.0f m from the host's", seat,
             roster.seats[seat].name.c_str(), species, at.x, at.y, at.z, net::dist(at, home));
    g_last = b;
    H->log(M, SK_INFO, "%s", b);
    if (C.notice && !roster.seats[seat].name.empty())
        C.notice(roster.seats[seat].name + "'s species has its own nest now");
    // the player's world is a copy of ours: without the new nest in it their game has no home to hatch at (and an
    // empty local copy faulted the game's claim). An online player gets the world again, once per new nest.
    if (player != party::kNobody && C.sess && player != C.sess->my_id()) {
        if (g_share_member) {
            g_share_due[seat] = {player, H->time_seconds()};
            return; // host_tick sends it once the nest has a member (family_back brings one in 2 s)
        }
        std::string share_err;
        if (world::share_with(player, share_err, true))
            g_shares++;
        else
            H->log(M, SK_WARN, "home nests: seat %d's world not sent again: %s", seat, share_err.c_str());
    }
}

static int members_of(void* herd)
{
    return live_herd(herd) ? (int)((skg::field<uintptr_t>(herd, 0x44) - skg::field<uintptr_t>(herd, 0x40)) / 4) : -1;
}

// the living members of `herd` (its list +0x40 keeps the dead until the game removes them)
static int living_of(void* herd)
{
    struct F {
        void* herd;
        int n = 0;
    } f{herd};
    game::for_each_object([](void* c, uint32_t noun, void* u) {
        auto& f = *(F*)u;
        if (noun == kCreatureNoun && game::herd_of(c) == f.herd && game::object_alive(c) && !game::is_dead(c))
            ++f.n;
        return true;
    }, &f);
    return f.n;
}

// BUG-9262 (found in the port, runtime 2026-11-20 lane A): CreateHerd(size 4, createNest) makes the nest and the herd
// but no member (+0x40 empty), and cHerd::Update lays eggs only from a living member (docs/re/creature.md, "How NPC
// nests get eggs"): the Lineages nest, which waited for the game's respawn, stayed empty for good (0 members after
// 3 min with mRespawnRate 2000) and its world went out after kShareWait without a family. A player's nest with no
// living member now gets members of its own species made by the game's cCreatureAnimal::Create into its herd, next to
// the nest: kFamily into a nest that never had one, one into a family that died (the player needs a mate). Outside the
// frame, one seat at a time, at most every kFillEvery s per seat.
static void fill_nest(void* ctx)
{
    const size_t seat = (size_t)(intptr_t)ctx;
    g_queued = false;
    if (!g_fix || !g_family_back || !is_host() || !game::in_creature_stage() || seat >= g_nests.size() || world::load_queued())
        return;
    void* herd = g_nests[seat].herd;
    net::Vec3 at;
    void* species = live_herd(herd) ? game::herd_species(herd) : nullptr;
    if (!species || !nest_pos_of(herd, at) || living_of(herd) > 0)
        return;
    const int n = members_of(herd) <= 0 ? kFamily : 1;
    const float r = std::sqrt(at.x * at.x + at.y * at.y + at.z * at.z);
    if (!(r > 1))
        return;
    // around the nest at 3 m, on the ground (the nest's own up and two directions across it)
    const net::Vec3 up{at.x / r, at.y / r, at.z / r};
    net::Vec3 a{up.z, 0, -up.x};
    float al = std::sqrt(a.x * a.x + a.z * a.z);
    if (al < 1e-3f) {
        a = {0, -up.z, up.y};
        al = std::sqrt(a.y * a.y + a.z * a.z);
    }
    a = {a.x / al, a.y / al, a.z / al};
    const net::Vec3 b{up.y * a.z - up.z * a.y, up.z * a.x - up.x * a.z, up.x * a.y - up.y * a.x};
    int made = 0;
    for (int i = 0; i < n; ++i) {
        const float ang = (float)i * 6.2831853f / (float)n;
        net::Vec3 p{at.x + 3.f * (a.x * std::cos(ang) + b.x * std::sin(ang)), at.y + 3.f * (a.y * std::cos(ang) + b.y * std::sin(ang)),
                    at.z + 3.f * (a.z * std::cos(ang) + b.z * std::sin(ang))};
        const float g = game::ground_radius(p), len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (std::isfinite(g) && g > 1 && len > 1)
            p = {p.x * (g + 0.5f) / len, p.y * (g + 0.5f) / len, p.z * (g + 0.5f) / len};
        made += game::spawn_creature(p, species, herd) != nullptr;
    }
    g_members_made += made;
    if (g_family_backs++ < 20)
        H->log(M, SK_INFO, "home nests: seat %zu's nest had no living member: %d of %d member(s) made at (%.0f %.0f %.0f), now %d living",
               seat, made, n, at.x, at.y, at.z, living_of(herd));
}

// a player's nest never stays without a living member (fill_nest)
static bool family_back(size_t seat, void* herd, double now)
{
    if (!g_family_back || seat >= g_filled_at.size() || now - g_filled_at[seat] < kFillEvery || living_of(herd) > 0)
        return false;
    g_filled_at[seat] = now;
    g_queued = true;
    H->run_outside_frame(fill_nest, (void*)(intptr_t)seat);
    return true;
}

// a new nest's world goes out once the nest has a member (or after kShareWait s)
static void share_due_tick(double now)
{
    for (size_t seat = 1; seat < g_share_due.size(); ++seat) {
        ShareDue& d = g_share_due[seat];
        if (d.since < 0)
            continue;
        if (party::player_of_seat((int)seat) != d.player) { // the player left (or another one sits there)
            d = {};
            continue;
        }
        const int members = members_of(g_nests[seat].herd);
        if (members == 0 && now - d.since < kShareWait)
            continue;
        std::string share_err;
        const bool ok = world::share_with(d.player, share_err, true);
        if (ok)
            g_shares++;
        if (members > 0 && now - d.since > 1)
            g_share_waits++;
        H->log(M, ok ? SK_INFO : SK_WARN, "home nests: seat %zu's world goes out %.0f s after its nest was made (%d member(s)%s)%s%s",
               seat, now - d.since, members, members == 0 ? ", waited long enough" : "", ok ? "" : ": not sent: ",
               share_err.c_str());
        d = {};
    }
}

// seat `ctx`'s nest takes its player's new species (outside the frame: SetSpecies rebuilds models)
static void follow_species(void* ctx)
{
    const int seat = (int)(intptr_t)ctx;
    g_queued = false;
    if (!g_fix || !g_follow || !is_host() || !game::in_creature_stage() || game::cinematic_playing() || world::load_queued())
        return;
    const auto& roster = party::roster();
    if (seat <= 0 || seat >= (int)roster.seats.size() || seat >= (int)g_nests.size())
        return;
    SeatNest& n = g_nests[seat];
    const uint32_t species = roster.seats[seat].species;
    void* profile = species ? game::species_by_key({species, kCrtType, kCrtGroup}) : nullptr;
    net::Vec3 at;
    if (!profile || !n.species || n.species == species || !nest_pos_of(n.herd, at))
        return;
    void* old_profile = game::herd_species(n.herd);
    skg::field<void*>(n.herd, 0xA4) = profile; // the herd's species (a cSpeciesProfile of the species manager)
    static const uintptr_t set_species = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    struct X {
        void *herd, *prof;
        uintptr_t set;
        uint32_t changed;
    } x{n.herd, profile, set_species, 0};
    if (set_species)
        game::for_each_object([](void* creature, uint32_t noun, void* p) {
            auto& x = *static_cast<X*>(p);
            if (noun != kCreatureNoun || game::is_puppet(creature) || !game::object_alive(creature) || game::is_dead(creature) ||
                game::herd_of(creature) != x.herd || game::species_profile(creature) == x.prof)
                return true;
            const int age = *(int*)((uint8_t*)creature + 0xB34);
            skg::thiscall<bool>(x.set, creature, x.prof, age, true);
            ++x.changed;
            return true;
        }, &x);
    if (social::set_party_ally(species))
        g_ally_set++;
    std::string err;
    const uint8_t player = party::player_of_seat(seat);
    if (player != party::kNobody && !creations::archive_guest_model(player, world::current_game_key(), err))
        H->log(M, SK_WARN, "home nests: seat %d's new creature not archived: %s", seat, err.c_str());
    char b[220];
    snprintf(b, sizeof b, "home nests: seat %d (%s)'s nest at (%.0f %.0f %.0f) follows its creature %08X -> %08X (herd %p was %p, %u member(s))",
             seat, roster.seats[seat].name.c_str(), at.x, at.y, at.z, n.species, species, n.herd, old_profile, x.changed);
    g_last = b;
    H->log(M, SK_INFO, "%s", b);
    n.species = species;
    g_followed++;
    g_follow_members += x.changed;
}

// (Lineages BUG-9173): each seat's nest is noted per planet in worlds.ini (section "nest<seat>", "x y z"). A player
// whose creature comes back under another species key gets that nest back, following the new key as after an
// evolution; before, every such session built one more nest next to the host's. Off: a new nest each time.
static bool g_note_fix = true;
COOP_FIX("creature.nest_note", "BUG-9262", g_note_fix,
         "creature: a player's nest is noted per planet and taken back when their species key changed (not a new one)");
static std::array<net::Vec3, founders::kMaxSeats> g_noted{};
static std::array<uint32_t, founders::kMaxSeats> g_noted_world{};
constexpr uint32_t kNotedSpecies = 1; // g_nests[].species of a nest found by its note (its old key is not known)

static void note_nest(size_t seat, const net::Vec3& at)
{
    const uint32_t w = g_note_fix && C.world ? C.world() : 0;
    if (!w || (g_noted_world[seat] == w && net::dist(g_noted[seat], at) < 1))
        return;
    char key[16], v[64];
    snprintf(key, sizeof key, "nest%zu", seat);
    snprintf(v, sizeof v, "%.1f %.1f %.1f", at.x, at.y, at.z);
    creations::world_note_set(key, w, v);
    g_noted[seat] = at;
    g_noted_world[seat] = w;
}

// the live nest herd at the spot noted for `seat` on this planet (not the host's, not another seat's), or null
static void* noted_nest(size_t seat, const net::Vec3& home, net::Vec3& at)
{
    const uint32_t w = g_note_fix && C.world ? C.world() : 0;
    if (!w)
        return nullptr;
    char key[16];
    snprintf(key, sizeof key, "nest%zu", seat);
    const std::string s = creations::world_note(key, w);
    net::Vec3 p{};
    if (s.empty() || sscanf(s.c_str(), "%f %f %f", &p.x, &p.y, &p.z) != 3 || net::dist(p, home) > kNear)
        return nullptr;
    struct F {
        net::Vec3 p;
        void* own;
        void* herd = nullptr;
        net::Vec3 at{};
    } f{p, game::avatar_herd()};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto& f = *(F*)u;
            net::Vec3 q;
            if (!f.herd && noun == game::kHerdNoun && o != f.own && live_herd(o) && !skg::field<uint8_t>(o, 0x190) &&
                nest_pos_of(o, q) && net::dist(q, f.p) < 6) {
                f.herd = o;
                f.at = q;
            }
            return true;
        },
        &f);
    for (size_t i = 1; f.herd && i < g_nests.size(); ++i)
        if (i != seat && g_nests[i].herd == f.herd)
            return nullptr;
    if (f.herd)
        at = f.at;
    return f.herd;
}

// (Lineages BUG-9204): a nest is "?" on the game's minimap until its player has been near it (minimap 0xD425A0: nest
// +0x124 bit 0 clear gives the icon flag 0x80000). The host marks every party seat's nest as discovered; the guests
// get it with the world. Off: as before.
static bool g_known_fix = true;
COOP_FIX("creature.party_nests_known", "BUG-9262", g_known_fix, "creature: every party seat's nest is discovered on the minimap");
static void mark_known(void* herd)
{
    uint8_t* nest = g_known_fix && live_herd(herd) ? (uint8_t*)game::herd_nest(herd) : nullptr;
    if (nest && game::object_alive(nest) && skg::readable(nest + 0x124, 1) && !(nest[0x124] & 1)) {
        nest[0x124] |= 1;
        H->log(M, SK_INFO, "home nests: a party nest marked discovered on the minimap");
    }
}

void host_tick(double now)
{
    if (!g_fix || !is_host() || now < g_next || g_queued)
        return;
    g_next = now + 2;
    if (!game::in_creature_stage() || world::load_queued() ||
        (!g_nest_first && (game::cinematic_playing() || world::time_paused())))
        return;
    share_due_tick(now);
    net::Vec3 home;
    if (!host_home(home))
        return;
    const auto& roster = party::roster();
    const uint32_t host_species = roster.seats.empty() ? 0 : roster.seats[0].species;
    for (size_t seat = 1; seat < roster.seats.size() && seat < g_nests.size(); ++seat) {
        const uint32_t species = roster.seats[seat].species;
        if (!species || species == host_species) {
            g_nests[seat] = {}; // plays the host's species: the host's nest
            continue;
        }
        void* profile = game::species_by_key({species, kCrtType, kCrtGroup});
        if (!profile)
            continue; // its creature is not here (yet)
        net::Vec3 at;
        if (void* herd = nest_herd_near(profile, home, kNear, &at)) {
            g_nests[seat] = {herd, at, species, g_nests[seat].herd == herd ? g_nests[seat].rate : 0};
            note_nest(seat, at);
            mark_known(herd);
            // (an offline player's nest is the AI's: it may die out, decision 8)
            if (party::player_of_seat((int)seat) != party::kNobody && family_back(seat, herd, now))
                return; // one a tick
            continue;
        }
        // its nest of the species it played before is still here: that nest follows (one a tick)
        if (g_follow && g_nests[seat].species && g_nests[seat].species != species && nest_pos_of(g_nests[seat].herd, at) &&
            net::dist(at, home) < kNear && party::player_of_seat((int)seat) != party::kNobody) {
            g_queued = true;
            H->run_outside_frame(follow_species, (void*)(intptr_t)seat);
            return;
        }
        // the nest noted for this seat on this planet: its species key changed since (one a tick)
        if (g_follow && g_nests[seat].species != kNotedSpecies && party::player_of_seat((int)seat) != party::kNobody)
            if (void* herd = noted_nest(seat, home, at)) {
                g_nests[seat] = {herd, at, kNotedSpecies};
                g_queued = true;
                H->run_outside_frame(follow_species, (void*)(intptr_t)seat);
                return;
            }
        // none near the host's home: a new player, a migration, an extinct nest. Made only while the seat's player is
        // here: an offline player's nest is the AI's (it may die out) and is made again on return.
        if (party::player_of_seat((int)seat) == party::kNobody)
            continue;
        g_queued = true;
        H->run_outside_frame(make_nest, (void*)(intptr_t)seat);
        return; // one a tick
    }
}

void* family_herd(uint8_t player)
{
    if (!g_fix || !is_host())
        return nullptr;
    const int seat = party::seat_of_player(player);
    net::Vec3 at;
    if (seat <= 0 || seat >= (int)g_nests.size() || !g_nests[seat].herd || !nest_pos_of(g_nests[seat].herd, at))
        return nullptr;
    return g_nests[seat].herd;
}

bool home_for(uint8_t player, net::Vec3& out)
{
    if (g_fix && is_host()) {
        const int seat = party::seat_of_player(player);
        if (seat > 0 && seat < (int)g_nests.size() && nest_pos_of(g_nests[seat].herd, out))
            return true;
    }
    return host_home(out);
}

void on_world_change()
{
    g_nests = {};
    g_share_due = {};
    g_filled_at = {};
    g_queued = false;
    g_next = 0;
}
// a new world or the session's end: the seats' nests are found again in the next world
LIFECYCLE_HOOK("home_nests.forget", lifecycle::kSessionEnd | lifecycle::kWorldChange, -12,
               [](const lifecycle::Ctx&) { on_world_change(); }, false);

std::string status()
{
    char b[400];
    snprintf(b, sizeof b, "home nests: fix=%d made=%u rebuilt=%u followed=%u (%u members) failed=%u ally_set=%u world_resent=%u share_waits=%u fills=%u members_made=%u last: %s\n",
             (int)g_fix, g_made, g_rebuilt, g_followed, g_follow_members, g_failed, g_ally_set, g_shares, g_share_waits,
             g_family_backs, g_members_made, g_last.empty() ? "-" : g_last.c_str());
    std::string out = b;
    net::Vec3 home;
    const bool have_home = host_home(home);
    const auto& roster = party::roster();
    for (size_t i = 1; i < g_nests.size(); ++i) {
        net::Vec3 p;
        if (!nest_pos_of(g_nests[i].herd, p)) {
            if (i < roster.seats.size() && roster.seats[i].species) {
                const uint32_t sp = roster.seats[i].species;
                void* prof = game::species_by_key({sp, kCrtType, kCrtGroup});
                snprintf(b, sizeof b, "  seat %zu: species %08X (profile %p, player %d), no nest of its own\n", i, sp, prof,
                         (int)party::player_of_seat((int)i));
                out += b;
            }
            continue;
        }
        void* hd = g_nests[i].herd;
        snprintf(b, sizeof b, "  seat %zu: species %08X nest (%.0f %.0f %.0f) %.0f m from the host's, members %d living %d (herd %p enabled %d respawn %d ms extinct %d best_friends %d) relation %d%s\n",
                 i, g_nests[i].species, p.x, p.y, p.z, have_home ? net::dist(p, home) : -1.f, members_of(hd), living_of(hd), hd,
                 (int)skg::field<uint8_t>(hd, 0x218), skg::field<int32_t>(hd, 0x11C), (int)skg::field<uint8_t>(hd, 0x190),
                 (int)skg::field<uint8_t>(hd, 0x191), social::relation_of(g_nests[i].species),
                 g_share_due[i].since >= 0 ? ", its world goes out with its first member" : "");
        out += b;
    }
    return out;
}

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[0] == "make" && is_host()) { // tests: make seat N's nest now (when it has none near)
        g_queued = true;
        H->run_outside_frame(make_nest, (void*)(intptr_t)atoi(a[1].c_str()));
        return "queued\n";
    }
    if (a.size() >= 2 && a[0] == "extinct" && is_host()) { // tests (decision 8): seat N's nest dies out (the game's own call)
        const int seat = atoi(a[1].c_str());
        static uintptr_t extinct = H->sym("SporeCoop::cHerd_SetExtinct");
        if (seat <= 0 || seat >= (int)g_nests.size() || !live_herd(g_nests[seat].herd) || !extinct)
            return "no nest for that seat\n";
        skg::thiscall<void>(extinct, g_nests[seat].herd);
        return "extinct\n";
    }
    return status();
}

} // namespace home_nests
