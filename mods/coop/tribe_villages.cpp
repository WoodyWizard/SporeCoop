// Every player's own tribe (BUG-9268; Lineages port phase 5, from PR #196's tribe.cpp "founders" parts: the automatic
// founding, the seat of a village, the native counts, the guest's own controls; the Lineages tickets named below are that
// branch's, kept for the history). docs/re/tribe.md#tribe-own-villages, docs/01-architecture.md#arch-own-villages.
//
// Host: a partner who plays a creature of their own (their party seat's species, party.h) gets a village of that species
// next to the host's (the game's SpawnNpcTribe: an NPC tribe of the host's game with 3 members and 15 food), allied with
// the host's and every other partner's village. Its members are that partner's in MSG_TRIBE_STATE (nobody's while they
// are offline: the village goes on by itself), the host runs their orders (tribe_host.cpp host_command), the village
// keeps its own food (an NPC tribe's store: wsync's tribe_food row shows it on every game). The village is recorded by
// party seat in <planet>.spo.duofounders (founders::Roster) and the partner's creature is archived with the planet
// (creations::archive_guest_model), so a reload, a rejoin or an offline partner keep it. A partner who plays the host's
// species shares the host's village (the 0.7.15 split). The NPC tribe controller does not count the players' villages
// as rivals (h_NounData): the planet still gets its rivals and the stage ends as usual when they are beaten or allied
// (the players move to Civilization together with the existing vote).
// Guest: its own village is an NPC tribe of its game holding the mirrors of its members; it counts as the player's
// (Tribe_IsPlayerOwned) and the terrain cursor selects among its members, so the game's own box, click and right-click
// order them (the orders go to the host as before).
#include "tribe_internal.h"
#include "tribe_seats.h"

#include <array>

#include "founders.h"
#include "identity.h"
#include "party.h"

namespace tribe {

static bool g_villages = true;
COOP_FIX("tribe.own_villages", "BUG-9268", g_villages,
         "tribe: a player with a creature of their own has their own village of it (own members, own food), allied with the other players' villages");
static bool g_count_fix = true;
COOP_FIX("tribe.founder_npc_budget", "BUG-9268", g_count_fix,
         "tribe: the players' own villages are no rivals for the NPC tribe controller (how many rivals, when the first comes)");
static bool g_ai_owned = true;
COOP_FIX("tribe.guest_ai", "BUG-9268", g_ai_owned,
         "tribe: a partner's village is the player's for the host's game (its members take orders as player members: raids)");
static bool g_guest_native = true;
COOP_FIX("tribe.guest_native", "BUG-9268", g_guest_native,
         "tribe: a guest selects and orders its own village's members with the game's own controls (box, click, right-click)");
static bool g_team_ally = true;
COOP_FIX("tribe.team_allies", "BUG-9268", g_team_ally,
         "tribe: an NPC village allied with one player's village is every player's ally (a guest's gifts and performances count for the team)");
static bool g_own_player = true;
COOP_FIX("tribe.own_player_village", "BUG-9268", g_own_player,
         "tribe: a guest's game makes its own village its player tribe (HUD food, villager cards, hut menu)");
static bool g_cursor_listed = true;
COOP_FIX("tribe.cursor_listed", "BUG-9272", g_cursor_listed,
         "tribe: a guest's terrain cursor lists its own village only while that village is in the object list and no load is queued");
static uint32_t g_team_allies;

// ---- host ----
static founders::Roster g_roster;
static std::string g_roster_planet;
static bool g_roster_read;
static std::array<void*, founders::kMaxSeats> g_village{}; // the live village of each seat (0: the player tribe)
static std::array<tribe_seats::Wait, founders::kMaxSeats> g_wait{};
static double g_next, g_world_at, g_ally_next;
static bool g_queued;
struct KeySeen {
    uint32_t key = 0;
    double at = 0;
};
static std::array<KeySeen, 256> g_key_seen; // by network id: a player's creature and when it last changed
static uint32_t g_founded, g_found_failed, g_allied, g_shares, g_count_views, g_ai_bits, g_ally_refused, g_cmd_other;
static std::string g_last = "-";
static std::string g_notice_last;
static bool g_any_partner; // a partner village is live (cheap gate for the hooks)
// ---- guest ----
static void* g_own_village;
static uint32_t g_cursor_binds, g_owned_answers;

static void report(const std::string& text, bool notice)
{
    g_last = text;
    H->log(M, SK_INFO, "tribe villages: %s", text.c_str());
    if (notice && C.notice && text != g_notice_last) {
        g_notice_last = text;
        C.notice(text);
    }
}

static uint32_t faction_of(void* t) { return t ? F<uint32_t>(t, 0x30) : 0; }

static void roster_load()
{
    const std::string planet = world::current_game_key();
    if (planet == g_roster_planet && g_roster_read)
        return;
    g_roster_planet = planet;
    g_roster = {};
    g_roster_read = true;
    if (!planet.empty() && founders::read(planet, g_roster))
        H->log(M, SK_INFO, "tribe villages: this planet's founders: %s", founders::describe(g_roster).c_str());
}

// the live villages of the roster's seats (seat 0: the player tribe)
static void refresh_villages()
{
    g_village = {};
    g_any_partner = false;
    void* pt = player_tribe();
    g_village[0] = pt;
    if (!g_villages || !is_host())
        return;
    auto tribes = all_tribes();
    for (size_t s = 1; s < g_roster.entries.size(); ++s) {
        const auto& e = g_roster.entries[s];
        if (!e.founded())
            continue;
        for (void* t : tribes)
            if (t != pt && faction_of(t) == e.faction && !F<uint8_t>(t, 0x556)) { // (not conquered)
                g_village[s] = t;
                g_any_partner = true;
                break;
            }
    }
}

int village_seat(void* t)
{
    if (!t || !g_any_partner)
        return -1;
    for (size_t s = 1; s < g_village.size(); ++s)
        if (g_village[s] == t)
            return (int)s;
    return -1;
}

// host: the owner of a member of a partner's village (0xFF: t is no partner's village)
uint8_t village_member_owner(void* t)
{
    const int seat = village_seat(t);
    return seat < 1 ? 0xFF : tribe_seats::village_owner(party::player_of_seat(seat));
}

// host: the players here with a living village of their own
std::vector<uint8_t> villaged_players()
{
    std::vector<uint8_t> out;
    if (!g_any_partner)
        return out;
    for (size_t s = 1; s < g_village.size(); ++s)
        if (g_village[s]) {
            const uint8_t p = party::player_of_seat((int)s);
            if (p < 0xFE)
                out.push_back(p);
        }
    std::sort(out.begin(), out.end());
    return out;
}

// host: `from` may order a member of village `t` (the player tribe, or their own village)
bool village_orders_ok(void* t, void* pt, uint8_t from)
{
    if (t == pt)
        return true;
    const int seat = village_seat(t);
    if (seat < 1)
        return false;
    if (party::player_of_seat(seat) == from)
        return true;
    g_cmd_other++;
    return false;
}

// host: the village (cTribe) of an order's target: a tribe, a hut (+0x230), a tool (+0x1F8), a citizen (+0x1014)
static void* village_of_object(void* o)
{
    if (!o || !game::listed(o))
        return nullptr;
    const uint32_t noun = game::noun_id(o);
    return noun == kTribeNoun     ? o
           : noun == 0x1E4DAAEu   ? F<void*>(o, 0x230)
           : noun == 0x18C8F0Cu   ? F<void*>(o, 0x1F8)
           : noun == kMemberNoun  ? F<void*>(o, kOwnerTribeOff)
                                  : nullptr;
}

// host: a hostile order (7 attack, 15 raid) on a player's village (the host's or a partner's) is refused: the players'
// villages are allies
bool hostile_to_party(int action, void* target)
{
    if (!g_villages || (action != 7 && action != 15) || !target)
        return false;
    void* v = village_of_object(target);
    if (!v || (v != player_tribe() && village_seat(v) < 1))
        return false;
    g_ally_refused++;
    return true;
}

// ---- the alliance (outside the frame: the relation change can reach the UI) ----
static void* rel_mgr()
{
    static uintptr_t get = H->sym("Simulator::cRelationshipManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}
static uint32_t ally_pair(void* mgr, uint32_t a, uint32_t b)
{
    static uintptr_t calc = H->sym("Simulator::cRelationshipManager::CalculateRelationship"),
                     apply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
    if (!mgr || !calc || !apply || !a || !b || a == b)
        return 0;
    const float allied = F<float>(mgr, 0x1C);
    uint32_t n = 0;
    for (int dir = 0; dir < 2; ++dir) {
        const uint32_t x = dir ? b : a, y = dir ? a : b;
        const float now = thiscall<float>(calc, mgr, x, y, 0);
        const float d = tribe_seats::ally_delta(now, allied);
        if (d > 0) {
            (void)thiscall<float>(apply, mgr, x, y, 0x0530CF07u, d); // (Lineages: the event of a native relation change)
            n++;
        }
    }
    return n;
}
static void ally_task(void*)
{
    if (!g_villages || !active() || !is_host() || world::load_queued())
        return;
    refresh_villages();
    void* mgr = rel_mgr();
    std::vector<uint32_t> f;
    for (void* v : g_village)
        if (v && game::listed(v, kTribeNoun))
            f.push_back(faction_of(v));
    uint32_t n = 0;
    for (size_t i = 0; i < f.size(); ++i)
        for (size_t j = i + 1; j < f.size(); ++j)
            n += ally_pair(mgr, f[i], f[j]);
    if (n) {
        g_allied += n;
        H->log(M, SK_INFO, "tribe villages: %u relation(s) between the players' villages raised to allies", n);
    }
    // the team's alliances: an NPC village one player's village made an ally (a guest's gifts or performances run on
    // its own village's members, so the game raises the NPC's relation with that village only) is the ally of every
    // player's village, the host's included: the host's goal counts it as for the host's own gifts (its mission, made
    // for a guest's gift by BUG-1068, sees the alliance). Only an alliance is shared, never a lower relation (a raid of
    // one player is not the others').
    static uintptr_t calc = H->sym("Simulator::cRelationshipManager::CalculateRelationship"),
                     apply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
    if (!g_team_ally || f.size() < 2 || !mgr || !calc || !apply)
        return;
    const float allied = F<float>(mgr, 0x1C);
    if (!(allied > 0))
        return;
    uint32_t shared = 0;
    for (void* t : all_tribes()) {
        const uint32_t npc = faction_of(t);
        if (!npc || village_seat(t) >= 1 || t == g_village[0] || F<uint8_t>(t, 0x556))
            continue;
        float best = -1e9f;
        for (uint32_t v : f)
            best = std::max(best, thiscall<float>(calc, mgr, npc, v, 0));
        if (!(best >= allied))
            continue;
        for (uint32_t v : f)
            for (int dir = 0; dir < 2; ++dir) {
                const uint32_t x = dir ? v : npc, y = dir ? npc : v;
                const float now = thiscall<float>(calc, mgr, x, y, 0);
                if (std::isfinite(now) && now < best - 0.01f) {
                    (void)thiscall<float>(apply, mgr, x, y, 0x0530CF07u, best - now);
                    shared++;
                }
            }
    }
    if (shared) {
        g_team_allies += shared;
        H->log(M, SK_INFO, "tribe villages: an NPC village allied with one player's village is every player's ally (%u relation(s))", shared);
    }
}

// ---- the founding site (Lineages tribe.cpp founding_site: 100/140/180 m rings, 16 directions, level, dry) ----
static float len3(const net::Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
static bool surface_frame(const net::Vec3& c, net::Vec3& u, net::Vec3& v)
{
    u = {c.z, 0, -c.x};
    float radius = len3(c), ul = len3(u);
    if (ul < .001f) {
        u = {0, -c.z, c.y};
        ul = len3(u);
    }
    if (!std::isfinite(radius) || radius < 1 || ul < .001f)
        return false;
    u = {u.x / ul, u.y / ul, u.z / ul};
    v = {(c.y * u.z - c.z * u.y) / radius, (c.z * u.x - c.x * u.z) / radius, (c.x * u.y - c.y * u.x) / radius};
    return true;
}
// the ground within [low, high], level within 3 m 20 m away along u and v, 70 m from every occupied spot
static bool level_clear_site(net::Vec3& q, float low, float high, const std::vector<net::Vec3>& occupied, const net::Vec3& u,
                             const net::Vec3& v)
{
    float ground = game::ground_radius(q), length = len3(q);
    const float water = game::water_radius();
    if (!std::isfinite(ground) || ground < low || ground > high || length < 1 || (water > 1 && ground < water + 1))
        return false;
    q = {q.x * ground / length, q.y * ground / length, q.z * ground / length};
    for (const auto& other : occupied)
        if (net::dist(q, other) < 70.f)
            return false;
    for (const auto& d : {u, v})
        for (float sign : {-1.f, 1.f}) {
            net::Vec3 edge{q.x + d.x * 20 * sign, q.y + d.y * 20 * sign, q.z + d.z * 20 * sign};
            float h = game::ground_radius(edge);
            if (!std::isfinite(h) || std::fabs(h - ground) > 3)
                return false;
        }
    return true;
}
static bool founding_site(void* home, net::Vec3& result)
{
    std::vector<net::Vec3> occupied;
    for (void* t : all_tribes())
        occupied.push_back(tribe_pos(t));
    std::vector<net::Vec3> centres{tribe_pos(home)};
    for (size_t s = 1; s < g_village.size(); ++s)
        if (g_village[s])
            centres.push_back(tribe_pos(g_village[s]));
    // pass 0: at least as high as the home and at most 3 m above it (Lineages); pass 1: within 6 m either way
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& c : centres) {
            net::Vec3 u, v;
            const float height = game::ground_radius(c);
            if (!surface_frame(c, u, v) || !std::isfinite(height) || height < 1)
                continue;
            for (float distance : {100.f, 140.f, 180.f})
                for (int step = 0; step < 16; ++step) {
                    const float a = step * .3926990817f;
                    net::Vec3 q{c.x + distance * (u.x * std::cos(a) + v.x * std::sin(a)),
                                c.y + distance * (u.y * std::cos(a) + v.y * std::sin(a)),
                                c.z + distance * (u.z * std::cos(a) + v.z * std::sin(a))};
                    if (level_clear_site(q, pass ? height - 6 : height, pass ? height + 6 : height + 3, occupied, u, v)) {
                        result = q;
                        return true;
                    }
                }
        }
    return false;
}

// Lineages BUG-9135 (owner: "the nest becomes the village"): the partner's own Creature nest (home_nests.cpp: a nest herd
// of its species 45-100 m from the host's, which the Tribe world keeps) is its village's site: the nearest live nest of
// that species within 400 m of the host's village; a nest closer than 80 m to another village is moved out to 80 m
// along the line from the host's village (the village needs its own ground). Level within 3 m 20 m out, dry, within 6 m
// of the host village's height, 70 m from every village. `tribe.village_at_nest` off: the ring search only.
static bool g_at_nest = true;
COOP_FIX("tribe.village_at_nest", "BUG-9268", g_at_nest, "tribe: a partner's village is founded at their own Creature nest when its ground allows");
static uint32_t g_at_nest_n;
static bool nest_site(void* profile, void* home, net::Vec3& result)
{
    const net::Vec3 at = tribe_pos(home);
    const float height = game::ground_radius(at);
    if (!g_at_nest || !profile || !std::isfinite(height) || height < 1)
        return false;
    struct Scan {
        void* profile;
        net::Vec3 at;
        std::vector<std::pair<float, net::Vec3>> nests;
    } scan{profile, at, {}};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto& s = *(Scan*)u;
            net::Vec3 q;
            if (noun != game::kHerdNoun || !skg::readable(o, 0x21C) || game::herd_species(o) != s.profile || F<uint8_t>(o, 0x190))
                return true; // (another species, or extinct)
            void* nest = game::herd_nest(o);
            if (nest && game::listed(nest) && game::object_position(nest, q) && net::dist(q, s.at) <= 400.f && s.nests.size() < 64)
                s.nests.push_back({net::dist(q, s.at), q});
            return true;
        },
        &scan);
    std::sort(scan.nests.begin(), scan.nests.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<net::Vec3> villages;
    for (void* t : all_tribes())
        villages.push_back(tribe_pos(t));
    net::Vec3 hu, hv;
    if (!surface_frame(at, hu, hv))
        return false;
    for (const auto& [d, nest] : scan.nests) {
        // the nest's direction from the host's village (in its ground plane); candidates on that side: the nest itself
        // when far enough, then 80-125 m out, up to 30 degrees either way
        const net::Vec3 w{nest.x - at.x, nest.y - at.y, nest.z - at.z};
        const float a0 = std::atan2(w.x * hv.x + w.y * hv.y + w.z * hv.z, w.x * hu.x + w.y * hu.y + w.z * hu.z);
        std::vector<net::Vec3> cand;
        if (d >= 80.f)
            cand.push_back(nest);
        for (float dist : {std::max(80.f, d), 95.f, 110.f, 125.f})
            for (float da : {0.f, .26f, -.26f, .52f, -.52f}) {
                const float a = a0 + da;
                cand.push_back({at.x + dist * (hu.x * std::cos(a) + hv.x * std::sin(a)), at.y + dist * (hu.y * std::cos(a) + hv.y * std::sin(a)),
                                at.z + dist * (hu.z * std::cos(a) + hv.z * std::sin(a))});
            }
        for (net::Vec3 q : cand) {
            net::Vec3 u, v;
            if (net::dist(q, nest) <= 70.f && surface_frame(q, u, v) && level_clear_site(q, height - 6, height + 6, villages, u, v)) {
                result = q;
                return true;
            }
        }
    }
    return false;
}

// ---- founding (outside the frame: it creates game objects, archives a file and saves) ----
static void found_task(void* ctx)
{
    const uint8_t player = (uint8_t)(uintptr_t)ctx;
    g_queued = false;
    if (!g_villages || !active() || !is_host() || world::load_queued() || world::save_busy() || world::save_pending() ||
        world::cinematic_playing() || world::time_paused() || !a_SpawnNpcTribe || !net::is_player(*C.sess, player))
        return;
    roster_load();
    refresh_villages();
    const std::string planet = world::current_game_key();
    void* home = player_tribe();
    const int seat = party::seat_of_player(player);
    if (planet.empty() || !home || seat < 1 || seat >= (int)founders::kMaxSeats || g_village[seat])
        return;
    const uint32_t home_species = tribe_info(home).key.instance;
    const creations::Key key = creations::player_key(player, creations::kCreature);
    void* profile = creations::player_species(player);
    if (!home_species || !key.instance || key.instance == home_species || !profile)
        return;
    const std::string name = C.sess->name_of(player);
    std::string err;
    if (!creations::archive_guest_model(player, planet, err)) { // an offline partner's village must load without them
        g_found_failed++;
        report(name + "'s creature could not be kept with the planet: " + err, false);
        return;
    }
    net::Vec3 site;
    const bool at_nest = nest_site(profile, home, site);
    g_at_nest_n += at_nest;
    if (!at_nest && !founding_site(home, site)) {
        g_found_failed++;
        report("No clear, level site near the village for " + name + "'s own village yet.", true);
        return;
    }
    skg::Vector3 pos{site.x, site.y, site.z};
    // cdecl cTribe* SpawnNpcTribe(const Vector3*, int archetype, int members, int food, bool, cSpeciesProfile*)
    // (Lineages: archetype 0, 3 members, 15 food, the partner's profile)
    void* village = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, 0, 3, 15, 0, profile);
    if (!village || members(village).empty()) {
        g_found_failed++;
        report("The game made no village for " + name + ".", true);
        return;
    }
    founders::Roster next = g_roster;
    next.entries[0] = {home_species, faction_of(home), identity::own()};
    next.entries[seat] = {key.instance, faction_of(village), identity::of(player)};
    // a seat's species may be another seat's of an older roster (its village lost): that entry is let go
    for (size_t s = 1; s < next.entries.size(); ++s)
        if ((int)s != seat && next.entries[s].founded() && next.entries[s].species == key.instance)
            next.entries[s] = {};
    if (!founders::record(planet, next, err)) {
        g_found_failed++;
        report("The new village of " + name + " was not recorded: " + err, false);
        // (the village stays: an NPC village of the planet; founded again next time)
        return;
    }
    g_roster = next;
    g_founded++;
    refresh_villages();
    ally_task(nullptr);
    const net::Vec3 at = tribe_pos(village);
    char b[260];
    snprintf(b, sizeof b, "%s's own village (seat %d, species %08X, faction %08X) at (%.0f %.0f %.0f), %.0f m from the host's, %s",
             name.c_str(), seat, key.instance, faction_of(village), at.x, at.y, at.z, net::dist(at, tribe_pos(home)),
             at_nest ? "at their own nest" : "at the next free site");
    report(b, false);
    if (C.notice)
        C.notice(name + " has their own village now");
    // every guest gets the world again with the village in it (saved first: the planet keeps it)
    bool first = true;
    for (const auto& p : C.sess->players()) {
        if (p.id == C.sess->my_id())
            continue;
        std::string share_err;
        if (world::share_with(p.id, share_err, first))
            g_shares++;
        else
            H->log(M, SK_WARN, "tribe villages: the world with the new village not sent to %u: %s", p.id, share_err.c_str());
        first = false;
    }
}

void villages_host_tick(double now)
{
    if (!g_villages || !is_host() || now < g_next)
        return;
    g_next = now + 1;
    roster_load();
    refresh_villages();
    if (g_any_partner && now >= g_ally_next) {
        g_ally_next = now + 5;
        H->run_outside_frame(ally_task, nullptr);
    }
    void* home = player_tribe();
    if (!home || g_queued || stage_end() || world::load_queued() || world::cinematic_playing() || world::time_paused())
        return;
    const uint32_t home_species = tribe_info(home).key.instance;
    auto tribes = all_tribes();
    for (const auto& p : C.sess->players()) {
        if (p.id == C.sess->my_id())
            continue;
        const creations::Key key = creations::player_key(p.id, creations::kCreature);
        KeySeen& ks = g_key_seen[p.id];
        if (ks.key != key.instance || ks.at == 0) {
            ks.key = key.instance;
            ks.at = now;
        }
        tribe_seats::FoundingIn in;
        in.seat = party::seat_of_player(p.id);
        in.species = key.instance;
        in.host_species = home_species;
        in.profile = creations::player_species(p.id) != nullptr;
        in.village = in.seat >= 1 && in.seat < (int)g_village.size() && g_village[in.seat];
        for (void* t : tribes)
            if (village_seat(t) != in.seat && (t == home || village_seat(t) >= 1) && tribe_info(t).key.instance == key.instance)
                in.other_seat = true;
        in.world_age = now - g_world_at;
        in.key_age = now - ks.at;
        const tribe_seats::Wait w = tribe_seats::founding_wait(in);
        if (in.seat >= 0 && in.seat < (int)g_wait.size())
            g_wait[in.seat] = w;
        if (w == tribe_seats::Wait::Ready && !g_queued) {
            g_queued = true;
            H->run_outside_frame(found_task, (void*)(uintptr_t)p.id);
            return; // one a tick
        }
    }
}

// host, every behaviour tree tick of a creature: a partner village's members take orders as player members (Lineages
// BUG-9020: citizen Init C27330 caches IsPlayerOwned in the AI's flags, (+0xB4C)+0x5FC bit 0, at creation)
static bool g_ai_bit = true; // not a lab switch: tests only (BUG-9271): villages aibit 0 leaves the AI cached bit to the game
void villages_ai(void* c)
{
    if (!g_any_partner || !g_ai_owned || !g_ai_bit || !is_citizen(c) || village_seat(F<void*>(c, kOwnerTribeOff)) < 1)
        return;
    if (uint8_t* ai = F<uint8_t*>(c, 0xB4C); ai && skg::readable(ai + 0x5FC, 4) && !(F<uint32_t>(ai, 0x5FC) & 1)) {
        F<uint32_t>(ai, 0x5FC) |= 1;
        g_ai_bits++;
    }
}

// ---- guest ----
static void persp_update();
static void guest_relations_tick();
void villages_guest_tick()
{
    void* before = g_own_village;
    g_own_village = nullptr;
    if (!g_villages || !g_guest_native || !is_guest() || local_play()) {
        persp_update();
        return;
    }
    void* pt = player_tribe();
    const uint8_t me = C.sess->my_id();
    void* found = nullptr;
    g_guest.mirrors.for_each([&](GEntry& e) {
        if (found || e.data.owner != me || !mirror_ok(e))
            return;
        void* t = F<void*>(e.ref.p, kOwnerTribeOff);
        if (t && t != pt && game::listed(t, kTribeNoun))
            found = t;
    });
    // (a snapshot cycle that just rebuilt the mirrors keeps the village found before while it is in the list)
    g_own_village = found ? found : before && before != pt && game::listed(before, kTribeNoun) ? before : nullptr;
    persp_update();
    guest_relations_tick();
}

void* own_village() { return g_own_village; }

// ---- hooks ----
// cGameNounManager::GetData (thiscall, 5 stack args, ret 0x14): a cached view {dirty, begin +4, end +8, cap +0xC}.
// Two callers read only its count: NpcTribeController_FormTribe (0xCC647D: how many rivals, the species index) and the
// controller's update (0xCC6B62: count - 1 picks the next rival's appearance). They get a private copy with the
// partners' villages left out (Lineages BUG-9047 / BUG-9059); every other caller gets the game's own.
using NounData_t = void*(SK_THISCALL*)(void*, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);
static NounData_t o_NounData;
static uintptr_t a_FormCountRet, a_UpdateCountRet;

size_t founder_villages_in(const void* list)
{
    if (!g_any_partner || !list || !skg::readable(list, 12))
        return 0;
    uintptr_t b = F<uintptr_t>((void*)list, 4), e = F<uintptr_t>((void*)list, 8);
    if (!b || e < b || (e - b) % 4 || e - b > 1024 || !skg::readable((void*)b, e - b))
        return 0;
    std::vector<uint32_t> factions, partner;
    for (uintptr_t at = b; at < e; at += 4) {
        void* t = *(void**)at;
        factions.push_back(t && game::listed(t, kTribeNoun) ? faction_of(t) : 0);
    }
    for (size_t s = 1; s < g_village.size(); ++s)
        if (g_village[s])
            partner.push_back(faction_of(g_village[s]));
    return tribe_seats::hidden_villages(factions, partner);
}

static void* SK_THISCALL h_NounData(void* self, uintptr_t create, uintptr_t clear, uintptr_t add, uintptr_t filter, uint32_t noun)
{
    void* data = o_NounData(self, create, clear, add, filter, noun);
    const uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (!g_count_fix || !g_villages || !g_any_partner || noun != kTribeNoun || (ret != a_FormCountRet && ret != a_UpdateCountRet) ||
        !is_host() || !active())
        return data;
    const size_t hidden = founder_villages_in(data);
    if (!hidden)
        return data;
    static uintptr_t view[4];
    memcpy(view, data, sizeof view);
    view[2] -= 4 * hidden;
    g_count_views++;
    return view;
}

// BUG-9271: callers (game VAs, base 0x400000) that get the game's own answer for a partner's village on the host
static std::vector<uintptr_t> g_owned_deny;
static bool g_rec_on; // tests (BUG-9271): the callers asked during a SpawnMember of ours
static std::vector<uintptr_t> g_rec;
static bool owned_denied(uintptr_t ret)
{
    if (g_owned_deny.empty())
        return false;
    const uintptr_t va = ret - H->rebase(0);
    return std::find(g_owned_deny.begin(), g_owned_deny.end(), va) != g_owned_deny.end();
}

// Tribe_IsPlayerOwned (C8E660, bool thiscall(tribe spatial = tribe + 0x120)); a citizen's asks its tribe's
using Owned_t = bool(SK_THISCALL*)(void*);
static Owned_t o_Owned;
static bool SK_THISCALL h_Owned(void* spatial)
{
    if (o_Owned(spatial))
        return true;
    if (!g_villages || game::mode_id() != modes::kTribe || !C.sess || !C.sess->connected())
        return false;
    void* t = (uint8_t*)spatial - 0x120;
    if (g_rec_on && g_rec.size() < 64)
        g_rec.push_back(((uintptr_t)__builtin_return_address(0) - H->rebase(0)) | 1); // (bit 0: the tribe's)
    if (is_host() && g_ai_owned && village_seat(t) >= 1 && !owned_denied((uintptr_t)__builtin_return_address(0))) {
        g_owned_answers++;
        note_owned_caller((uintptr_t)__builtin_return_address(0)); // BUG-9271 (diagnostics)
        return true;
    }
    if (is_guest() && g_guest_native && g_own_village && t == g_own_village && !local_play()) {
        g_owned_answers++;
        return true;
    }
    return false;
}

// cGameTerrainCursor::GetTerrainCursor (B30EB0, cdecl): the cursor's candidate list (+0x164, a borrowed vector pointer,
// the player tribe's mSelectableMembers +0x354 in Tribe) is the guest's own village's while it has one (Lineages
// BUG-9152); back to the player tribe's when not
using Cursor_t = void*(SK_CDECL*)();
static Cursor_t o_Cursor;
static void* g_bound_list;
static void* g_native_list;
static void* SK_CDECL h_Cursor()
{
    void* cursor = o_Cursor();
    if (!cursor || !skg::readable((uint8_t*)cursor + 0x164, 4))
        return cursor;
    void*& list = F<void*>(cursor, 0x164);
    // (the village in the object list now, never a freed one: a world being torn down or loaded keeps the game's own)
    // (tribe.cursor_listed off, close to BUG-9272's first form: bound whenever a village is known, a load queued or not)
    const bool live = g_cursor_listed ? !world::load_queued() && game::listed(g_own_village, kTribeNoun) : g_own_village != nullptr;
    if (g_villages && g_guest_native && g_own_village && is_guest() && active() && !local_play() && live) {
        void* own = (uint8_t*)g_own_village + 0x354;
        if (list != own) {
            if (!g_bound_list)
                g_native_list = list; // the game's own (the player tribe's), put back below
            list = own;
            g_cursor_binds++;
        }
        g_bound_list = own;
    } else if (g_bound_list) {
        if (list == g_bound_list)
            list = g_native_list;
        g_bound_list = g_native_list = nullptr;
    }
    return cursor;
}

void villages_init()
{
    a_FormCountRet = H->sym("SporeCoop::FormTribe_TribeCountReturn");
    a_UpdateCountRet = H->sym("SporeCoop::NpcTribeUpdate_TribeCountReturn");
    if (uintptr_t get = H->sym("Simulator::cGameNounManager::GetData"); get && (a_FormCountRet || a_UpdateCountRet))
        H->hook_create(M, (void*)get, (void*)h_NounData, (void**)&o_NounData);
    if (uintptr_t owned = H->sym("SporeCoop::Tribe_IsPlayerOwned"))
        H->hook_create(M, (void*)owned, (void*)h_Owned, (void**)&o_Owned);
    if (uintptr_t cursor = H->sym("Simulator::cGameTerrainCursor::GetTerrainCursor"))
        H->hook_create(M, (void*)cursor, (void*)h_Cursor, (void**)&o_Cursor);
}

void villages_forget()
{
    g_village = {};
    g_wait = {};
    g_any_partner = false;
    g_roster_read = false;
    g_roster_planet.clear();
    g_queued = false;
    g_own_village = nullptr;
    g_world_at = H ? H->time_seconds() : 0;
    g_key_seen = {};
}
LIFECYCLE_HOOK("tribe.villages", lifecycle::kSessionEnd | lifecycle::kWorldChange, -49,
               [](const lifecycle::Ctx&) { villages_forget(); }, false);

static float village_food(void* t)
{
    // the tribe's food store: its resource bundles' amounts (as wsync's npc_food: the container +0x20C, its vector
    // +4 / +8, each bundle's amount +0x134)
    if (!t || !skg::readable((uint8_t*)t + 0x20C, 0xC))
        return -1;
    void** b = F<void**>(t, 0x210);
    void** e = F<void**>(t, 0x214);
    if (b == e)
        return 0;
    if (!b || e < b || e - b > 256 || !skg::readable(b, (e - b) * 4))
        return -1;
    float v = 0;
    for (void** it = b; it < e; ++it)
        if (*it && skg::readable((uint8_t*)*it + 0x134, 4))
            v += F<float>(*it, 0x134);
    return v;
}

std::string villages_status()
{
    char b[300];
    snprintf(b, sizeof b,
             "tribe villages: fix=%d at_nest=%u founded=%u failed=%u allied=%u shares=%u count_views=%u ai_bits=%u owned_answers=%u "
             "ally_refused=%u other_seat_cmds=%u cursor_binds=%u own=%p last: %s\n",
             (int)g_villages, g_at_nest_n, g_founded, g_found_failed, g_allied, g_shares, g_count_views, g_ai_bits, g_owned_answers,
             g_ally_refused, g_cmd_other, g_cursor_binds, g_own_village, g_last.c_str());
    std::string out = b;
    out += persp_status();
    if (is_host()) {
        out += "  roster: " + founders::describe(g_roster) + "\n";
        void* mgr = rel_mgr();
        static uintptr_t calc = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
        std::vector<float> food;
        std::vector<std::string> names;
        for (size_t s = 0; s < g_village.size(); ++s) {
            void* v = g_village[s];
            const auto& e = g_roster.entries[s];
            const uint8_t p = party::player_of_seat((int)s);
            if (!v && !e.founded() && s >= party::roster().seats.size())
                continue;
            const float rel = v && g_village[0] && v != g_village[0] && mgr && calc
                                  ? thiscall<float>(calc, mgr, faction_of(v), faction_of(g_village[0]), 0)
                                  : NAN;
            snprintf(b, sizeof b, "  seat %zu: player=%d species=%08X faction=%08X village=%p members=%zu food=%.1f relation=%.1f wait=%s\n", s,
                     p >= 0xFE ? -1 : (int)p, e.species, e.faction, v, v ? members(v).size() : 0, v ? village_food(v) : -1.f, rel,
                     s && active() ? tribe_seats::wait_name(g_wait[s]) : "-");
            out += b;
            food.push_back(v ? village_food(v) : -1.f);
            names.push_back(s == 0 ? "Host" : p < 0xFE ? C.sess->name_of(p) : "seat " + std::to_string(s));
        }
        // the NPC villages' relations with the host's village and with each partner's (the team's alliances)
        if (mgr && calc && g_village[0] && g_any_partner)
            for (void* t : all_tribes()) {
                if (t == g_village[0] || village_seat(t) >= 1)
                    continue;
                snprintf(b, sizeof b, "  npc %08X%s: relation host=%.1f", faction_of(t), F<uint8_t>(t, 0x556) ? " (conquered)" : "",
                         thiscall<float>(calc, mgr, faction_of(t), faction_of(g_village[0]), 0));
                out += b;
                for (size_t s2 = 1; s2 < g_village.size(); ++s2)
                    if (g_village[s2]) {
                        snprintf(b, sizeof b, " seat%zu=%.1f", s2, thiscall<float>(calc, mgr, faction_of(t), faction_of(g_village[s2]), 0));
                        out += b;
                    }
                out += "\n";
            }
        std::string line = tribe_seats::food_line(food, names);
        if (!line.empty())
            out += "  " + line + "\n";
    } else if (g_own_village) {
        void* pt = player_tribe();
        snprintf(b, sizeof b, "  own village %p faction=%08X members=%zu food=%.1f; the host's %p food=%.1f\n", g_own_village,
                 faction_of(g_own_village), members(g_own_village).size(), village_food(g_own_village), pt, village_food(pt));
        out += b;
    }
    return out;
}

static void persp_restore(const char* why);
std::string villages_debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[1] == "found" && a.size() >= 3 && is_host()) { // tests: found player N's village now
        if (g_queued)
            return "busy\n";
        g_queued = true;
        H->run_outside_frame(found_task, (void*)(uintptr_t)atoi(a[2].c_str()));
        return "queued\n";
    }
    if (a.size() >= 3 && a[1] == "baby" && is_host()) // tests: a baby for player N's own village (as its CMD_BABY)
        return village_baby((uint8_t)atoi(a[2].c_str())) ? "taken\n" : "not a village of its own\n";
    if (a.size() >= 2 && a[1] == "deny") { // tests (BUG-9271): deny <hex va>...: these callers get the game's answer
        g_owned_deny.clear();
        for (size_t i = 2; i < a.size(); ++i)
            g_owned_deny.push_back((uintptr_t)strtoul(a[i].c_str(), nullptr, 16));
        return "deny list: " + std::to_string(g_owned_deny.size()) + "\n";
    }
    if (a.size() >= 3 && a[1] == "nests" && is_host()) { // tests: the herds with a nest of player N's species
        void* profile = creations::player_species((uint8_t)atoi(a[2].c_str()));
        void* home = player_tribe();
        struct Scan {
            void* profile;
            net::Vec3 at;
            std::string out;
        } scan{profile, home ? tribe_pos(home) : net::Vec3{}, {}};
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto& sc = *(Scan*)u;
                net::Vec3 q;
                if (noun != game::kHerdNoun || !skg::readable(o, 0x21C) || game::herd_species(o) != sc.profile)
                    return true;
                void* nest = game::herd_nest(o);
                char b[120];
                snprintf(b, sizeof b, "herd %p nest %p extinct %d at %.0f m\n", o, nest, (int)F<uint8_t>(o, 0x190),
                         nest && game::object_position(nest, q) ? net::dist(q, sc.at) : -1.f);
                sc.out += b;
                return true;
            },
            &scan);
        net::Vec3 site;
        const bool ok = home && nest_site(profile, home, site);
        return scan.out + (ok ? "site at the nest\n" : "no site at a nest\n");
    }
    if (a.size() >= 4 && a[1] == "earn" && is_host()) { // tests: earn <seat> <npc faction hex>: that NPC village becomes
        // the ally of seat's village only (as a guest's gifts or performances leave it) and gets its goal mission (as
        // BUG-1068 makes it for a guest's gift); the team alliance follows within 5 s
        const int seat = atoi(a[2].c_str());
        const uint32_t npc = (uint32_t)strtoul(a[3].c_str(), nullptr, 16);
        void* v = seat >= 1 && seat < (int)g_village.size() ? g_village[seat] : nullptr;
        void* t = nullptr;
        for (void* x : all_tribes())
            if (faction_of(x) == npc)
                t = x;
        void* mgr = rel_mgr();
        if (!v || !t || !mgr)
            return "no such seat village or NPC village\n";
        struct E {
            uint32_t a, b;
            void* t;
        };
        H->run_outside_frame([](void* p) {
            E e = *(E*)p;
            delete (E*)p;
            static uintptr_t calc = H->sym("Simulator::cRelationshipManager::CalculateRelationship"),
                             apply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
            void* mgr = rel_mgr();
            if (!mgr || !calc || !apply)
                return;
            const float allied = F<float>(mgr, 0x1C);
            for (int dir = 0; dir < 2; ++dir) {
                const uint32_t x = dir ? e.b : e.a, y = dir ? e.a : e.b;
                const float now = thiscall<float>(calc, mgr, x, y, 0);
                (void)thiscall<float>(apply, mgr, x, y, 0x0530CF07u, allied + 1 - now);
            }
            wsync::goal_target(e.t);
        }, new E{npc, faction_of(v), t});
        return "queued\n";
    }
    if (a.size() >= 3 && a[1] == "aibit") {
        g_ai_bit = a[2] != "0";
        return "aibit " + std::to_string((int)g_ai_bit) + "\n";
    }
    if (a.size() >= 2 && a[1] == "reswap" && is_guest()) { // tests: the host's village back, the own one again next tick
        persp_restore("test");
        return "restored\n";
    }
    if (a.size() >= 2 && a[1] == "ally" && is_host()) {
        H->run_outside_frame(ally_task, nullptr);
        return "queued\n";
    }
    return villages_status();
}

} // namespace tribe

namespace tribe {
bool own_village_info(int& m, float& food)
{
    if (!g_villages || !active())
        return false;
    void* v = is_host() ? (g_any_partner ? player_tribe() : nullptr)
                        : (g_own_village && game::listed(g_own_village, kTribeNoun) ? g_own_village : nullptr);
    if (!v)
        return false;
    m = (int)members(v).size();
    food = village_food(v);
    return food >= 0;
}
} // namespace tribe

// ---- the guest's own village as its game's player tribe (BUG-9268; Lineages BUG-9153, tribe.own_player_village) ----
// Spore has one player tribe per game (cGameNounManager +0x70, GetPlayerTribe): on a guest's game it is the host's
// village, so the HUD's food counter, the villager cards, the hut menu and the hints were the host's. A guest with its
// own village makes it the player tribe with the game's setter (SetPlayerTribe B1FD50: the new one retained, the old one
// released; the host's village stays in the object list) and puts the host's village back for its saves, the session's
// end, the host away (local play), a load and when the own village goes. The mod keeps meaning the host's village by
// player_tribe() (tribe.cpp) and swapped_host_tribe() (wsync, shared, designs, realign, wstate).
namespace tribe {

static void* g_persp_host; // the host's village while the guest's game shows its own one as the player's
static bool g_persp_saving;
static uint32_t g_persp_swaps, g_persp_restores, g_hutless, g_vbabies_sent, g_vbabies_made, g_rel_copied;
static double g_rel_next;

void* swapped_host_tribe() { return g_persp_host && game::listed(g_persp_host, kTribeNoun) ? g_persp_host : nullptr; }

static void* native_player_tribe()
{
    void* nm = game::noun_manager();
    return nm && skg::readable((uint8_t*)nm + 0x70, 4) ? F<void*>(nm, 0x70) : nullptr;
}

static bool persp_set(void* t)
{
    static uintptr_t set = H->sym("Simulator::cGameNounManager::SetPlayerTribe");
    void* nm = game::noun_manager();
    if (!set || !nm || !t || !game::listed(t, kTribeNoun))
        return false;
    thiscall<void>(set, nm, t);
    return native_player_tribe() == t;
}

static void persp_restore(const char* why)
{
    if (!g_persp_host)
        return;
    void* host = g_persp_host;
    g_persp_host = nullptr;
    if (game::listed(host, kTribeNoun) && native_player_tribe() != host)
        persp_set(host);
    g_persp_restores++;
    H->log(M, SK_INFO, "tribe villages: the host's village is this game's player tribe again (%s)", why);
}

static void persp_update()
{
    const bool want = g_villages && g_own_player && !g_persp_saving && active() && is_guest() && !local_play() &&
                      !world::load_queued() && g_own_village && game::listed(g_own_village, kTribeNoun);
    if (g_persp_host && (!want || native_player_tribe() != g_own_village || !game::listed(g_persp_host, kTribeNoun)))
        persp_restore(!want ? "not wanted now" : "changed");
    if (!want || g_persp_host)
        return;
    void* host = native_player_tribe();
    if (!host || host == g_own_village || !game::listed(host, kTribeNoun) || !persp_set(g_own_village))
        return;
    g_persp_host = host;
    g_persp_swaps++;
    H->log(M, SK_INFO, "tribe villages: this game's player tribe is its own village now (HUD, cards, hut menu)");
}

void villages_save(bool start)
{
    if (start) {
        g_persp_saving = true; // a save keeps the host's village as the player's
        persp_restore("save");
    } else
        g_persp_saving = false; // the next tick shows the own village again
}

// guest: the relations of its own village are the team's (the host's village's, which wsync's tribe_rel row keeps as on
// the host): the host's village is its ally, an NPC village has the relation it has with the host's (outside the frame)
static void relations_task(void*)
{
    void* own = g_own_village;
    void* host = player_tribe();
    void* mgr = rel_mgr();
    static uintptr_t calc = H->sym("Simulator::cRelationshipManager::CalculateRelationship"),
                     apply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
    if (!own || !host || own == host || !mgr || !calc || !apply || !active() || !is_guest() || world::load_queued() ||
        !game::listed(own, kTribeNoun) || !game::listed(host, kTribeNoun))
        return;
    const uint32_t o = faction_of(own), h = faction_of(host);
    g_rel_copied += ally_pair(mgr, o, h);
    for (void* t : all_tribes()) {
        const uint32_t npc = faction_of(t);
        if (t == own || t == host || !npc)
            continue;
        const float want = thiscall<float>(calc, mgr, npc, h, 0);
        for (int dir = 0; dir < 2; ++dir) {
            const uint32_t x = dir ? o : npc, y = dir ? npc : o;
            const float now = thiscall<float>(calc, mgr, x, y, 0);
            if (std::isfinite(want) && std::isfinite(now) && std::fabs(now - want) > 0.05f) {
                (void)thiscall<float>(apply, mgr, x, y, 0x0530CF07u, want - now);
                g_rel_copied++;
            }
        }
    }
}
static void guest_relations_tick()
{
    const double now = H->time_seconds();
    if (!g_own_village || now < g_rel_next)
        return;
    g_rel_next = now + 2;
    H->run_outside_frame(relations_task, nullptr);
}

// Tribe_HutStateIs2 (C90730) reads tribe +0x368 (the hut) without a null check; the Tribe UI's ring code asked it for a
// citizen's tribe without a hut right after a guest's game swapped its player tribe (Lineages, a read of 0x154). A tribe
// without a hut has no hut in that state.
using HutState_t = bool(SK_THISCALL*)(void*);
static HutState_t o_HutState;
static bool SK_THISCALL h_HutState(void* tribe)
{
    if (!tribe || !skg::readable((uint8_t*)tribe + 0x368, 4) || !F<void*>(tribe, 0x368)) {
        g_hutless++;
        return false;
    }
    return o_HutState(tribe);
}

// TribeHutUI_HandleMessage (CDBEA0, bool thiscall(this, IWindow*, Message*) ret 8): Add Baby (button 0x0625CD52) at the
// guest's own hut goes to the host (CMD_BABY with a member of its village: the host's game makes it in that village and
// takes its food), never the guest's own game
using HutUI_t = bool(SK_THISCALL*)(void*, void*, void*);
static HutUI_t o_HutUI;
static bool SK_THISCALL h_HutUI(void* self, void* window, void* msg)
{
    if (g_persp_host && msg && skg::readable(msg, 0x10) && F<uint32_t>(msg, 8) == 0x287259F6 && F<uint32_t>(msg, 0xC) == 0x0625CD52 &&
        active() && is_guest() && !local_play()) {
        uint16_t id = 0;
        const uint8_t me = C.sess->my_id();
        g_guest.mirrors.for_each([&](GEntry& e) {
            if (!id && e.data.owner == me && mirror_ok(e) && F<void*>(e.ref.p, kOwnerTribeOff) == g_own_village)
                id = e.id;
        });
        if (id) {
            net::Writer w;
            w.u8(CMD_BABY);
            w.u8(1);
            w.u16(id);
            w.u32(0);
            write_target(w, Target{});
            w.f32(0);
            w.f32(0);
            w.f32(0);
            C.sess->send_reliable_to(0, net::MSG_TRIBE_CMD, w);
            g_vbabies_sent++;
        }
        return true; // the host buys it (its food); the baby comes with the host's snapshots
    }
    return o_HutUI(self, window, msg);
}

// host: CMD_BABY from a player with a village of their own: a baby in that village, for its food (the game's price,
// Tribe_BabyFoodCost), within its population limit (the village's SpawnMember, vt 0x98, null at the limit). True: taken.
bool village_baby(uint8_t from)
{
    const int seat = party::seat_of_player(from);
    void* v = seat >= 1 && seat < (int)g_village.size() ? g_village[seat] : nullptr;
    if (!g_villages || !v || !game::listed(v, kTribeNoun))
        return false;
    static uintptr_t cost_at = H->sym("SporeCoop::Tribe_BabyFoodCost"), add = H->sym("SporeCoop::cTribe_AddFood");
    const float cost = cost_at ? *(const float*)cost_at : -1.f;
    const float food = village_food(v);
    auto tell = [&](const char* text) {
        net::Writer w;
        w.str8(text);
        C.sess->send_reliable_to(from, net::MSG_NOTICE, w);
    };
    if (!add || !std::isfinite(cost) || cost < 0 || cost > 100000) {
        tell("Births are not available on this game build.");
        return true;
    }
    if (food < cost) {
        tell("Your village needs more food to add a baby.");
        return true;
    }
    g_rec.clear();
    g_rec_on = true;
    void* baby = vcall<void*>(v, 0x98 / 4, 0);
    g_rec_on = false;
    {
        std::string l;
        char b[16];
        for (uintptr_t a : g_rec) {
            snprintf(b, sizeof b, " %08X", (unsigned)a);
            l += b;
        }
        H->log(M, SK_INFO, "tribe villages: IsPlayerOwned asked during the birth:%s", l.c_str());
    }
    if (!baby) {
        tell("Your village is at its population limit.");
        return true;
    }
    thiscall<void>(add, v, -(int)std::ceil(cost));
    g_vbabies_made++;
    return true;
}

// BUG-9271: who asked Tribe_IsPlayerOwned about a partner's village on the host (return addresses, unrebased)
static std::array<std::pair<uintptr_t, uint32_t>, 16> g_owned_callers{};
void note_owned_caller(uintptr_t ret)
{
    for (auto& [a, n] : g_owned_callers)
        if (a == ret || !a) {
            a = ret;
            n++;
            return;
        }
}
static std::string owned_callers()
{
    std::string out;
    char b[48];
    for (auto& [a, n] : g_owned_callers)
        if (a) {
            snprintf(b, sizeof b, " %08X:%u", (unsigned)(a - H->rebase(0)), n);
            out += b;
        }
    return out;
}

// BUG-9271 (diagnostics): the citizen's own IsPlayerOwned (C24590, citizen spatial = citizen + 0xC0: its tribe at
// +0xF54, i.e. citizen +0x1014) asks the tribe's; its callers for a partner's villager, by return address
static std::array<std::pair<uintptr_t, uint32_t>, 24> g_cit_callers{};
// BUG-9271: the Tribe UI adds a villager card for a new citizen of the player's (0xCCC6E8 asks the citizen's
// IsPlayerOwned, return 0xCCC6EA): a partner's villager got one on the host's game (seen: the partner's chief and two
// villagers after the founding, a card for every birth). That caller gets the game's own answer (false); the others,
// the AI's raids among them (Lineages BUG-9020), keep ours. Off: a card for each.
static bool g_host_cards_fix = true;
COOP_FIX("tribe.host_cards", "BUG-9271", g_host_cards_fix,
         "tribe: the host's villager cards show only the host's villagers, no partner village's");
static uintptr_t a_CardOwnedRet;
static uint32_t g_cards_refused;
using CitOwned_t = bool(SK_THISCALL*)(void*);
static CitOwned_t o_CitOwned;
static bool SK_THISCALL h_CitOwned(void* spatial)
{
    if (g_rec_on && g_rec.size() < 64)
        g_rec.push_back((uintptr_t)__builtin_return_address(0) - H->rebase(0));
    const bool r = o_CitOwned(spatial);
    if (r && g_any_partner && is_host() && skg::readable((uint8_t*)spatial + 0xF54, 4) &&
        village_seat(F<void*>(spatial, 0xF54)) >= 1) {
        const uintptr_t ret = (uintptr_t)__builtin_return_address(0);
        if (owned_denied(ret))
            return false;
        if (g_host_cards_fix && ret == a_CardOwnedRet && a_CardOwnedRet) {
            g_cards_refused++;
            return false;
        }
        for (auto& [a, n] : g_cit_callers)
            if (a == ret || !a) {
                a = ret;
                n++;
                break;
            }
    }
    return r;
}
std::string cit_callers()
{
    std::string out;
    char b[48];
    for (auto& [a, n] : g_cit_callers)
        if (a) {
            snprintf(b, sizeof b, " %08X:%u", (unsigned)(a - H->rebase(0)), n);
            out += b;
        }
    return out;
}

void villages_init2()
{
    a_CardOwnedRet = H->sym("SporeCoop::TribeUI_CardOwnedReturn");
    if (uintptr_t a = H->sym("SporeCoop::Citizen_IsPlayerOwned"))
        H->hook_create(M, (void*)a, (void*)h_CitOwned, (void**)&o_CitOwned);
    if (uintptr_t a = H->sym("SporeCoop::Tribe_HutStateIs2"))
        H->hook_create(M, (void*)a, (void*)h_HutState, (void**)&o_HutState);
    if (uintptr_t a = H->sym("SporeCoop::TribeHutUI_HandleMessage"))
        H->hook_create(M, (void*)a, (void*)h_HutUI, (void**)&o_HutUI);
}

std::string persp_status()
{
    char b[300];
    snprintf(b, sizeof b,
             "  perspective: native=%p host=%p own=%p swaps=%u restores=%u hutless=%u babies_sent=%u babies_made=%u rel_copied=%u "
             "team_allies=%u owned_callers=%s\n",
             native_player_tribe(), g_persp_host, g_own_village, g_persp_swaps, g_persp_restores, g_hutless, g_vbabies_sent,
             g_vbabies_made, g_rel_copied, g_team_allies, owned_callers().c_str());
    return b + std::string("  host cards refused ") + std::to_string(g_cards_refused) + "; citizen owned callers:" + cit_callers() + "\n";
}

// the session's end (also the mod's unload): the host's village back; a world change: the pointers are gone
LIFECYCLE_HOOK("tribe.villages_persp", lifecycle::kSessionEnd, -48, [](const lifecycle::Ctx&) { persp_restore("session end"); }, false);
LIFECYCLE_HOOK("tribe.villages_persp_world", lifecycle::kWorldChange, -48, [](const lifecycle::Ctx&) { g_persp_host = nullptr; }, false);

} // namespace tribe

namespace tribe {
// host: the partners' live villages; their food (the NPC stores) added to *food (BUG-0571's pace of the party)
size_t partner_villages(int* food)
{
    size_t n = 0;
    if (!g_villages || !is_host() || !g_any_partner)
        return 0;
    for (size_t s = 1; s < g_village.size(); ++s)
        if (g_village[s] && game::listed(g_village[s], kTribeNoun)) {
            n++;
            const float f = village_food(g_village[s]);
            if (food && f > 0)
                *food += (int)f;
        }
    return n;
}
} // namespace tribe
