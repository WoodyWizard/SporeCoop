#include "home_nests.h"

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
COOP_FIX("creature.own_nests", "BUG-9135", g_fix,
         "creature: every player with their own species has their own nest next to the host's (hatch, respawn, migration)");

constexpr uint32_t kNestNoun = 0x52AA6122; // cNest
constexpr uint32_t kCrtType = 0x2B978C46, kCrtGroup = 0x40626200;
constexpr float kNear = 160.f;             // a seat's nest belongs to the host's current home within this
constexpr int kFamily = 4;                 // the members the game keeps in a seat's nest herd

struct SeatNest {
    void* herd = nullptr;
    net::Vec3 pos{};
    uint32_t species = 0;
    int32_t rate = 0; // the game's respawn time while family_back holds it short
};
static std::array<SeatNest, founders::kMaxSeats> g_nests;
static double g_next;
static bool g_queued;
static uint32_t g_made, g_failed, g_rebuilt, g_ally_set, g_shares, g_followed, g_follow_members;
// BUG-9151: a player who evolves (or picks another creature of their own) keeps their nest: the herd (+0xA4) and its
// family take the new species in place, as the game's own evolution does with the player's family (the guest's game
// mirrors these members). Before, the host found no nest of the new species, built a second one elsewhere, left the old
// one standing as an AI nest of the old body and sent the whole world again. Off: a new nest (0.7.8).
// BUG-9167: the world for a player's new nest went out at once, before the game had put a member in the new herd (it
// respawns members after its respawn time, about a minute here). On the guest the empty copy died out (2026-11-13: the
// MSI's new nest gone from its game a few minutes later) or could not be joined ("no members here yet"), so the player
// stayed in the host's nest. The world now goes out once the new nest has a member, kShareWait s at most.
static bool g_share_member = true;
COOP_FIX("creature.nest_share_member", "BUG-9167", g_share_member,
         "creature: the world for a player's new nest goes out once the nest has a member (90 s at most)");
constexpr double kShareWait = 90;
// The game makes a nest herd's next member when its respawn time (+0x11C, mRespawnRate ms: 120000 here) has run
// (cHerd_Update, docs/re/creature.md#creature-join-editor-first). A player's nest with no living member (a new nest,
// BUG-9167; a family that died, BUG-9175 and decision 2E "else one is spawned": the player needs a mate) gets kEmptyNestMs
// until the next one, then the game's own time again (family_back, host_tick).
constexpr int32_t kEmptyNestMs = 2000, kGameRespawnMs = 120000;
static bool g_family_back = true;
COOP_FIX("creature.nest_family_back", "BUG-9175", g_family_back,
         "creature: a player's nest with no living member gets its next member in 2 s (a new nest, a family that died)");
static uint32_t g_family_backs;
struct ShareDue {
    uint8_t player = 0xFF;
    double since = -1;
};
static std::array<ShareDue, founders::kMaxSeats> g_share_due;
static uint32_t g_share_waits;
static bool g_follow = true;
COOP_FIX("creature.nest_follows_evolution", "BUG-9151", g_follow,
         "creature: a partner who evolves keeps their nest; its herd and family take the new species (no second nest, no world resend)");
static std::string g_last;

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
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
static void* nest_herd_near(void* profile, const net::Vec3& at, float within, net::Vec3* pos = nullptr)
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

static bool g_flat_site = true;
COOP_FIX("creature.nest_site_flat", "BUG-9167", g_flat_site,
         "creature: a player's new nest is put where the land around it is flat too (not a small island)");
static bool g_closest_height = true;
COOP_FIX("creature.nest_site_closest_height", "BUG-9173", g_closest_height,
         "creature: more spots for a player's nest (55-100 m), then the closest in height (up to 20 m) instead of none");
// decision 2: a flat spot 45-80 m from the host's home nest, at its height (within 6 m), 25 m from every other nest
static bool site_near(const net::Vec3& home, net::Vec3& out)
{
    const float r = std::sqrt(home.x * home.x + home.y * home.y + home.z * home.z);
    const float h0 = game::ground_radius(home);
    if (!(r > 1) || !std::isfinite(h0))
        return false;
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
    // BUG-9167: the land around the spot at the home's height too (rings of 10 and 20 m): a nest on a small island in
    // the water (2026-11-13) got no member for minutes (the game found no land for its family)
    auto flat_around = [&](const net::Vec3& c) {
        for (float r : {10.f, 20.f})
            for (int j = 0; j < 8; j++) {
                const float b = j * 45 * 0.0174533f;
                net::Vec3 p{c.x + r * (u.x * std::cos(b) + v.x * std::sin(b)), c.y + r * (u.y * std::cos(b) + v.y * std::sin(b)),
                            c.z + r * (u.z * std::cos(b) + v.z * std::sin(b))};
                const float g = game::ground_radius(p);
                if (!std::isfinite(g) || std::fabs(g - h0) > 6)
                    return false;
            }
        return true;
    };
    // BUG-9173: the closest in height of the spots clear of other nests, when none is within 6 m of the home's height
    // (a host nest on a slope, Samuth 2026-11-13: 42 tries, no nest, the player's family the host's species: no mate)
    struct {
        int no_ground = 0, height = 0, near_nest = 0;
        float best_dh = 1e9f;
        net::Vec3 best{};
    } why;
    // the spots tried: 12 directions at 60, 50, 70, 45, 80 m (as before); BUG-9173: then 24 directions at 55-100 m (a
    // ring of old nests at 50-65 m filled every one of the first 60 on Samuth)
    std::vector<std::pair<float, float>> spots; // (distance m, direction degrees)
    for (float dist : {60.f, 50.f, 70.f, 45.f, 80.f})
        for (int k = 0; k < 12; k++)
            spots.push_back({dist, k * 30 + dist});
    if (g_closest_height)
        for (float dist : {55.f, 65.f, 75.f, 90.f, 100.f})
            for (int k = 0; k < 24; k++)
                spots.push_back({dist, k * 15 + 7.5f});
    for (int pass = g_flat_site ? 0 : 1; pass < 2; ++pass) // 0: flat land around it, 1: as before
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
                    if (pass == 1 && g_flat_site)
                        H->log(M, SK_INFO, "home nests: no spot with flat land around it 45-80 m from the host's nest: one without");
                    out = q;
                    return true;
                }
            }
    static int told = 0;
    if (told++ < 5)
        H->log(M, SK_WARN, "home nests: no spot at the home's height (radius %.1f): %d no ground, %d off by more than 6 m "
               "(the closest %.1f m), %d within 25 m of a nest", h0, why.no_ground, why.height,
               why.best_dh < 1e8f ? why.best_dh : -1.f, why.near_nest);
    if (g_closest_height && why.best_dh < 20.f) {
        out = why.best;
        return true;
    }
    return false;
}

static bool host_home(net::Vec3& out)
{
    void* av = game::avatar();
    return av && game::nest_position(av, out);
}

// the seat whose nest is missing near the host's home gets one (outside the frame: it creates game objects)
static void make_nest(void* ctx)
{
    const int seat = (int)(intptr_t)ctx;
    g_queued = false;
    if (!g_fix || !is_host() || !game::in_creature_stage() || game::cinematic_playing() || world::load_queued())
        return;
    const auto& roster = party::roster();
    if (seat <= 0 || seat >= (int)roster.seats.size())
        return;
    const uint32_t species = roster.seats[seat].species;
    void* profile = species ? game::species_by_key({species, kCrtType, kCrtGroup}) : nullptr;
    net::Vec3 home, site;
    if (!profile || !host_home(home) || nest_herd_near(profile, home, kNear))
        return;
    if (!site_near(home, site)) {
        if (g_failed++ < 5)
            H->log(M, SK_WARN, "home nests: no flat spot 45-80 m from the host's nest for seat %d", seat);
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
    // an offline player's nest must load without them: their creature goes with the save (as a founder's)
    std::string err;
    const uint8_t player = party::player_of_seat(seat);
    if (player != party::kNobody && !creations::archive_guest_model(player, world::current_game_key(), err))
        H->log(M, SK_WARN, "home nests: seat %d's creature not archived: %s", seat, err.c_str());
    const bool rebuilt = g_nests[seat].species == species;
    (rebuilt ? g_rebuilt : g_made)++;
    g_nests[seat] = {herd, at, species};
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
        if (noun == 0x18EB45Eu && game::herd_of(c) == f.herd && game::object_alive(c) && !game::is_dead(c))
            ++f.n;
        return true;
    }, &f);
    return f.n;
}

// a player's nest never stays without a living member: the game's respawn in kEmptyNestMs while it has none
static void family_back(size_t seat, void* herd)
{
    int32_t& rate = skg::field<int32_t>(herd, 0x11C);
    if (!g_family_back || rate <= 0 || rate >= 3600000)
        return;
    const bool empty = living_of(herd) == 0;
    // the game refills a nest that has bodies or eggs with eggs (2 min each, seen 2026-11-13): the nest's eggs hatch now
    // (the herd's eggs +0x54..+0x58, an egg's time to hatch +0x1F0 ms: nest.cpp kEggHatch)
    if (empty)
        for (void** e = skg::field<void**>(herd, 0x54), **end = skg::field<void**>(herd, 0x58); e && e < end && end - e < 64; ++e)
            if (*e && skg::readable(*e, 0x1F4) && skg::field<int32_t>(*e, 0x1F0) > kEmptyNestMs)
                skg::field<int32_t>(*e, 0x1F0) = kEmptyNestMs;
    if (empty && rate > kEmptyNestMs) {
        g_nests[seat].rate = rate;
        rate = kEmptyNestMs;
        if (g_family_backs++ < 20)
            H->log(M, SK_INFO, "home nests: seat %zu's nest has no living member: the next one in %d s (BUG-9175)", seat,
                   kEmptyNestMs / 1000);
    } else if (!empty && rate == kEmptyNestMs) {
        rate = g_nests[seat].rate > kEmptyNestMs ? g_nests[seat].rate : kGameRespawnMs;
    }
}

// BUG-9167: a new nest's world goes out once the nest has a member (or after kShareWait s)
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
        // (BUG-9195: that player's game covers the reload with the loading screen, coop.cpp ui.reload_cover)
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
            if (noun != 0x18EB45Eu || game::is_puppet(creature) || !game::object_alive(creature) || game::is_dead(creature) ||
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
    snprintf(b, sizeof b, "home nests: seat %d (%s)'s nest at (%.0f %.0f %.0f) follows its evolution %08X -> %08X (herd %p was %p, %u member(s), BUG-9151)",
             seat, roster.seats[seat].name.c_str(), at.x, at.y, at.z, n.species, species, n.herd, old_profile, x.changed);
    g_last = b;
    H->log(M, SK_INFO, "%s", b);
    n.species = species;
    g_followed++;
    g_follow_members += x.changed;
}

// BUG-9173: each seat's nest is noted per planet in worlds.ini (section "nest<seat>", "x y z"). A player whose creature
// comes back under another species key (a rejoin under another network id: roadmap 1b) gets that nest back, following
// the new key as after an evolution (BUG-9151); before, every such session built one more nest next to the host's
// (Samuth 2026-11-13: a ring of 12 at 50-65 m that left no spot for a new one). Off: a new nest each time.
static bool g_note_fix = true;
COOP_FIX("creature.nest_note", "BUG-9173", g_note_fix,
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

// Decision 2Y A (owner 2026-11-13: the "big circle of random nests around the center nest"): before BUG-9180 a
// partner's creature had a new species key at every rejoin under another network id, and every such session built one
// more nest next to the host's (BUG-9173). Once per planet the host removes the nests the mod made for player creatures
// no seat plays any more: a nest herd within kNear of the host's home, a family of kFamily (ours), its members of a
// player creation's key (0x7C......), and that species neither the host's nor any seat's (an offline seat's nest is
// the AI's and stays, decision 8). Its members, its eggs, the nest and the herd go through the game's own destroy.
// `coop.fix creature.nest_ring_cleanup off`: they stay.
static bool g_ring_fix = true;
COOP_FIX("creature.nest_ring_cleanup", "BUG-9182", g_ring_fix,
         "creature: once per planet the host removes old player nests no seat uses any more (left by the old species keys)");
static uint32_t g_ring_world, g_ring_removed;
static void ring_cleanup(void*)
{
    g_queued = false;
    const uint32_t world = C.world ? C.world() : 0;
    net::Vec3 home;
    if (!g_ring_fix || !world || world == g_ring_world || !is_host() || !game::in_creature_stage() ||
        game::cinematic_playing() || world::load_queued() || !host_home(home))
        return;
    g_ring_world = world;
    std::vector<void*> keep; // the species profiles in use: the host's and every seat's
    if (void* av = game::avatar()) keep.push_back(game::species_profile(av));
    const auto& roster = party::roster();
    for (size_t i = 0; i < roster.seats.size(); ++i)
        if (roster.seats[i].species)
            if (void* pr = game::species_by_key({roster.seats[i].species, kCrtType, kCrtGroup})) keep.push_back(pr);
    std::vector<net::Vec3> noted; // a seat's nest noted on this planet (BUG-9173) is taken over, never removed
    for (size_t seat = 1; seat < founders::kMaxSeats; ++seat) {
        char key[16];
        snprintf(key, sizeof key, "nest%zu", seat);
        const std::string n = creations::world_note(key, world);
        net::Vec3 p{};
        if (!n.empty() && sscanf(n.c_str(), "%f %f %f", &p.x, &p.y, &p.z) == 3) noted.push_back(p);
    }
    struct Cand { void* herd; void* nest; std::vector<void*> members; };
    struct Find { net::Vec3 home; std::vector<void*>* keep; std::vector<net::Vec3>* noted; void* own; std::vector<Cand> out; }
        f{home, &keep, &noted, game::avatar_herd(), {}};
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto& f = *static_cast<Find*>(u);
        net::Vec3 q;
        if (noun != game::kHerdNoun || o == f.own || !live_herd(o) || !nest_pos_of(o, q) || net::dist(q, f.home) > kNear ||
            skg::field<int32_t>(o, 0xF4) != kFamily)
            return true;
        void* prof = game::herd_species(o);
        for (void* k : *f.keep)
            if (k == prof) return true;
        for (size_t i = 1; i < g_nests.size(); ++i)
            if (g_nests[i].herd == o) return true;
        for (const auto& n : *f.noted)
            if (net::dist(n, q) < 6) return true;
        f.out.push_back({o, game::herd_nest(o), {}});
        return true;
    }, &f);
    std::vector<void*> living; // every species a living creature has anywhere (an empty nest of one of them is kept)
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto& v = *static_cast<std::vector<void*>*>(u);
        if (noun == 0x18EB45Eu && game::object_alive(o) && !game::is_dead(o)) {
            void* p = game::species_profile(o);
            if (std::find(v.begin(), v.end(), p) == v.end()) v.push_back(p);
        }
        return true;
    }, &living);
    for (auto& c : f.out) { // its members (the herd's own list) and their key: a player creation's
        struct Mem { void* herd; std::vector<void*>* m; bool player_key = true; } m{c.herd, &c.members};
        game::for_each_object([](void* o, uint32_t noun, void* u) {
            auto& m = *static_cast<Mem*>(u);
            if (noun == 0x18EB45Eu && game::object_alive(o) && game::herd_of(o) == m.herd) {
                m.m->push_back(o);
                m.player_key &= (game::species_key(o).instance & 0xFE000000u) == 0x7C000000u;
            }
            return true;
        }, &m);
        // an empty one: ours by its family size and the respawn times we leave (120000, or 2000 from a new nest), and its
        // species on no living creature (the ring on Samuth, 2026-11-13: families died out, nests marked extinct)
        const int32_t rate = skg::field<int32_t>(c.herd, 0x11C);
        const bool alive_species = std::find(living.begin(), living.end(), game::herd_species(c.herd)) != living.end();
        const bool orphan = c.members.empty() && (rate == 120000 || rate == kEmptyNestMs) && !alive_species;
        if (!orphan && (c.members.empty() || !m.player_key)) {
            net::Vec3 q{};
            nest_pos_of(c.herd, q);
            H->log(M, SK_INFO, "home nests: kept nest herd %p at (%.0f %.0f %.0f): %zu member(s)%s, respawn %d ms, extinct %d, species living %d",
                   c.herd, q.x, q.y, q.z, c.members.size(), c.members.empty() ? "" : (m.player_key ? "" : ", not a player creation's key"),
                   rate, (int)skg::field<uint8_t>(c.herd, 0x190), (int)alive_species);
            c.herd = nullptr; // not provably a player creation's nest: kept
            continue;
        }
        game::herd_quiet(c.herd); // no new members
        for (void* mem : c.members) game::destroy_creature(mem);
        for (void** e = skg::field<void**>(c.herd, 0x54), **end = skg::field<void**>(c.herd, 0x58); e && e < end && end - e < 64; ++e)
            if (*e && game::object_alive(*e)) game::destroy_instance(*e);
        if (c.nest && game::object_alive(c.nest)) game::destroy_instance(c.nest);
        game::destroy_instance(c.herd);
        ++g_ring_removed;
    }
    unsigned n = 0;
    for (auto& c : f.out) n += c.herd != nullptr;
    H->log(M, SK_INFO, "home nests: %u old player nest(s) removed round the host's home, %zu checked (decision 2Y, BUG-9182)", n,
           f.out.size());
}

// BUG-9204 (owner play test 2026-11-14: "The nests are visible on the map, they're just '?'. They're not 'Discovered'"):
// a nest is "?" on the game's minimap until its player has been near it (minimap 0xD425A0: nest +0x124 bit 0 clear gives
// the icon flag 0x80000; runtime 2026-11-14: setting the bit turned a seat nest's "?" into the friendly face at once).
// The host marks every party seat's nest as discovered; the guests get it with the world (the shares carry the save).
// `coop.fix creature.party_nests_known off`: as before.
static bool g_known_fix = true;
COOP_FIX("creature.party_nests_known", "BUG-9204", g_known_fix, "creature: every party seat's nest is discovered on the minimap");
static void mark_known(void* herd)
{
    uint8_t* nest = g_known_fix && live_herd(herd) ? (uint8_t*)game::herd_nest(herd) : nullptr;
    if (nest && game::object_alive(nest) && skg::readable(nest + 0x124, 1) && !(nest[0x124] & 1)) {
        nest[0x124] |= 1;
        H->log(M, SK_INFO, "home nests: a party nest marked discovered on the minimap (BUG-9204)");
    }
}

void host_tick(double now)
{
    if (!g_fix || !is_host() || now < g_next || g_queued)
        return;
    g_next = now + 2;
    if (!game::in_creature_stage() || game::cinematic_playing() || world::time_paused() || world::load_queued())
        return;
    share_due_tick(now);
    net::Vec3 home;
    if (!host_home(home))
        return;
    if (g_ring_fix && C.world && C.world() && C.world() != g_ring_world && !g_queued) { // decision 2Y A, once a planet
        g_queued = true;
        H->run_outside_frame(ring_cleanup, nullptr);
        return;
    }
    const auto& roster = party::roster();
    const uint32_t host_species = roster.seats.empty() ? 0 : roster.seats[0].species;
    for (size_t seat = 1; seat < roster.seats.size(); ++seat) {
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
            mark_known(herd); // BUG-9204
            if (party::player_of_seat((int)seat) != party::kNobody)
                family_back(seat, herd); // (an offline player's nest is the AI's: it may die out, decision 8)
            continue;
        }
        // BUG-9151: its nest of the species it played before is still here: that nest follows (one a tick)
        if (g_follow && g_nests[seat].species && g_nests[seat].species != species && nest_pos_of(g_nests[seat].herd, at) &&
            net::dist(at, home) < kNear && party::player_of_seat((int)seat) != party::kNobody) {
            g_queued = true;
            H->run_outside_frame(follow_species, (void*)(intptr_t)seat);
            return;
        }
        // BUG-9173: the nest noted for this seat on this planet: its species key changed since (one a tick; an old
        // save's seat once: since BUG-9180 the key no longer changes with the network id)
        if (g_follow && g_nests[seat].species != kNotedSpecies && party::player_of_seat((int)seat) != party::kNobody)
            if (void* herd = noted_nest(seat, home, at)) {
                g_nests[seat] = {herd, at, kNotedSpecies};
                g_queued = true;
                H->run_outside_frame(follow_species, (void*)(intptr_t)seat);
                return;
            }
        // none near the host's home: a new player, a migration (decision 3), an extinct nest (decision 8). Made only while
        // the seat's player is here: an offline player's nest is the AI's (it may die out) and is made again on return.
        if (party::player_of_seat((int)seat) == party::kNobody)
            continue;
        g_queued = true;
        H->run_outside_frame(make_nest, (void*)(intptr_t)seat);
        return; // one a tick
    }
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
    g_queued = false;
    g_next = 0;
}

std::string status()
{
    char b[360];
    snprintf(b, sizeof b, "home nests: fix=%d made=%u rebuilt=%u followed=%u (%u members) failed=%u ally_set=%u world_resent=%u last: %s\n",
             (int)g_fix, g_made, g_rebuilt, g_followed, g_follow_members, g_failed, g_ally_set, g_shares,
             g_last.empty() ? "-" : g_last.c_str());
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
                snprintf(b, sizeof b, "  seat %zu: species %08X (profile %p, player %d), no nest of its own (kept herd %p alive %d extinct %d)\n",
                         i, sp, prof, (int)party::player_of_seat((int)i), g_nests[i].herd, (int)live_herd(g_nests[i].herd),
                         live_herd(g_nests[i].herd) ? (int)skg::field<uint8_t>(g_nests[i].herd, 0x190) : -1);
                out += b;
            }
            continue;
        }
        void* hd = g_nests[i].herd;
        snprintf(b, sizeof b, "  seat %zu: species %08X nest (%.0f %.0f %.0f) %.0f m from the host's, members %d (herd %p enabled %d respawn %d ms extinct %d)%s\n",
                 i, g_nests[i].species, p.x, p.y, p.z, have_home ? net::dist(p, home) : -1.f, members_of(hd), hd,
                 (int)skg::field<uint8_t>(hd, 0x218), skg::field<int32_t>(hd, 0x11C), (int)skg::field<uint8_t>(hd, 0x190),
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
