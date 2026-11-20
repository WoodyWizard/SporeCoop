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
    if (!o || !game::object_alive(o))
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
        if (v && game::object_alive(v))
            f.push_back(faction_of(v));
    uint32_t n = 0;
    for (size_t i = 0; i < f.size(); ++i)
        for (size_t j = i + 1; j < f.size(); ++j)
            n += ally_pair(mgr, f[i], f[j]);
    if (n) {
        g_allied += n;
        H->log(M, SK_INFO, "tribe villages: %u relation(s) between the players' villages raised to allies", n);
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
    if (!founding_site(home, site)) {
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
    char b[200];
    snprintf(b, sizeof b, "%s's own village (seat %d, species %08X, faction %08X) at (%.0f %.0f %.0f), %.0f m from the host's", name.c_str(),
             seat, key.instance, faction_of(village), at.x, at.y, at.z, net::dist(at, tribe_pos(home)));
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
void villages_ai(void* c)
{
    if (!g_any_partner || !g_ai_owned || !is_citizen(c) || village_seat(F<void*>(c, kOwnerTribeOff)) < 1)
        return;
    if (uint8_t* ai = F<uint8_t*>(c, 0xB4C); ai && skg::readable(ai + 0x5FC, 4) && !(F<uint32_t>(ai, 0x5FC) & 1)) {
        F<uint32_t>(ai, 0x5FC) |= 1;
        g_ai_bits++;
    }
}

// ---- guest ----
void villages_guest_tick()
{
    g_own_village = nullptr;
    if (!g_villages || !g_guest_native || !is_guest() || local_play())
        return;
    void* pt = player_tribe();
    const uint8_t me = C.sess->my_id();
    g_guest.mirrors.for_each([&](GEntry& e) {
        if (g_own_village || e.data.owner != me || !mirror_ok(e))
            return;
        void* t = F<void*>(e.ref.p, kOwnerTribeOff);
        if (t && t != pt && game::object_alive(t))
            g_own_village = t;
    });
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
        factions.push_back(t && game::object_alive(t) ? faction_of(t) : 0);
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
    if (is_host() && g_ai_owned && village_seat(t) >= 1) {
        g_owned_answers++;
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
    if (g_villages && g_guest_native && g_own_village && is_guest() && active() && !local_play() && !world::load_queued() &&
        game::listed(g_own_village, kTribeNoun)) {
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
             "tribe villages: fix=%d founded=%u failed=%u allied=%u shares=%u count_views=%u ai_bits=%u owned_answers=%u "
             "ally_refused=%u other_seat_cmds=%u cursor_binds=%u own=%p last: %s\n",
             (int)g_villages, g_founded, g_found_failed, g_allied, g_shares, g_count_views, g_ai_bits, g_owned_answers,
             g_ally_refused, g_cmd_other, g_cursor_binds, g_own_village, g_last.c_str());
    std::string out = b;
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

std::string villages_debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[1] == "found" && a.size() >= 3 && is_host()) { // tests: found player N's village now
        if (g_queued)
            return "busy\n";
        g_queued = true;
        H->run_outside_frame(found_task, (void*)(uintptr_t)atoi(a[2].c_str()));
        return "queued\n";
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
