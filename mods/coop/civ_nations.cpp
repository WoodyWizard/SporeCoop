// Every player's own nation in Civilization (BUG-9274; Lineages port phase 5 for Civ, from PR #196's civ.cpp
// founders_multi, refresh_founder_seats, founder_owner, h_CityOwned / h_VehicleOwned, h_CivAI, h_GetMoney /
// h_SpendMoney, h_HudProgress and campaign_tick; the Lineages tickets named below are that branch's). civ_nations.h,
// civ_seats.h, docs/re/civ.md#civ-own-nations.
#include "civ_internal.h"
#include "civ_nations.h"
#include "civ_seats.h"
#include "civ_wallet.h"

#include "founders.h"
#include "identity.h"
#include "party.h"
#include "tribe_seats.h"

namespace civ::nations {

static bool g_on = true;
COOP_FIX("civ.own_nations", "BUG-9274", g_on,
         "civ: a player with a species of their own has their own nation (capital, cities, vehicles, money), allied with the other players' nations");
static bool g_native = true;
COOP_FIX("civ.nations_native", "BUG-9274", g_native,
         "civ: a guest's own nation is the player's for its game (its clicks, box and city menus command it); its AI is the player's on every game");
static bool g_team = true;
COOP_FIX("civ.team_campaign", "BUG-9274", g_team,
         "civ: every player nation's cities count for the team (level, HUD progress); only the players' nations left = the planet is won");

constexpr uintptr_t kNationSpecies = 0x268, kExSites = 0xAC, kExSitesLeft = 0xB0, kSelectable = 0xD4, kWealth = 0x98,
                    kLevel = 0x298, kCitySpiral = 0x120;
constexpr uint32_t kSpeciesType = 0x2B978C46, kSpeciesGroup = 0x40626200;

// ---- the game's functions (init) ----
static uintptr_t a_CityTotal, a_RaiseLevel, a_GameCiv, a_ShowWon, a_WonPause, a_RelGet, a_RelCalc, a_RelApply;
static uintptr_t a_AIPlayerRet, a_HudRet;

// ---- host ----
static founders::Roster g_roster;          // <planet>.spo.duonations, by party seat
static std::string g_planet;
static bool g_read;
static std::array<uint8_t, founders::kMaxSeats> g_seat_player{}; // the connected player of each seat (kNobody: away)
static std::array<void*, founders::kMaxSeats> g_nation{};        // the live nation of each seat (seat 0: the player civ)
static bool g_any;                         // a partner nation lives
static bool g_queued, g_won_shown;
static double g_next, g_ally_next, g_camp_next, g_wallet_next;
static uint64_t g_epoch;                   // tasks queued in another world are dropped
enum class Phase : uint8_t { Idle, Requested };
static struct Founding {
    Phase phase = Phase::Idle;
    int seat = -1;
    creations::Key key{};
    std::vector<civ_seats::City> before;
    unsigned waited = 0; // founder ticks of running game since the request
} g_found;
static bool g_spawning;                    // our own SpawnNextCity call runs
static uint32_t g_founded, g_failed, g_allied, g_shares, g_ai_player, g_ai_held, g_money_reads, g_money_spent,
    g_hud, g_ff, g_levels, g_won;
static std::string g_last = "-", g_told;
static float g_hud_own, g_hud_team;
// ---- guest ----
static uint32_t g_own;                     // its own nation
static std::vector<uint32_t> g_team_pols;  // the players' nations it knows (from the host's city owners)
static uint32_t g_owned_answers, g_cursor_lists;

static thread_local void* g_ai_nation;     // the nation whose AI runs its player branch now

static bool host() { return is_host(); }
static bool guest() { return is_guest(); }
// the nations live in the host's game also while it plays alone (no session): a partner's nation waits for its player,
// it does not turn into an NPC nation (its AI, its money)
static bool in_civ() { return g_ok && game::mode_id() == modes::kCiv; }
static bool authority() { return in_civ() && !guest(); }
static void* nation_by(uint32_t pol) { return pol ? thiscall<void*>(a_CivByPol, nm(), pol) : nullptr; }
static uint32_t home_pol()
{
    void* pc = player_civ();
    return pc ? pol_of(pc) : 0;
}

static void report(const std::string& text, bool notice = false)
{
    g_last = text;
    H->log(M, SK_INFO, "civ nations: %s", text.c_str());
    if (notice && C.notice && text != g_told) {
        g_told = text;
        C.notice(text);
    }
}

static void roster_load()
{
    const std::string planet = world::current_game_key();
    if (planet == g_planet && g_read)
        return;
    g_planet = planet;
    g_roster = {};
    g_read = true;
    if (!planet.empty() && founders::read(planet, g_roster, founders::Stage::Civilization))
        H->log(M, SK_INFO, "civ nations: this planet's nations: %s", founders::describe(g_roster).c_str());
}

static void refresh()
{
    g_nation = {};
    g_any = false;
    g_nation[0] = player_civ();
    for (size_t s = 1; s < founders::kMaxSeats; ++s) {
        g_seat_player[s] = party::player_of_seat((int)s);
        const auto& e = g_roster.entries[s];
        if (!e.founded() || e.faction == home_pol())
            continue;
        if (void* n = nation_by(e.faction)) {
            g_nation[s] = n;
            g_any = true;
        }
    }
}

int seat_of_faction(uint32_t pol)
{
    if (!g_on || !pol || guest())
        return -1;
    const int s = g_roster.seat_of_faction(pol);
    return s >= 1 && g_nation[s] ? s : -1;
}

uint8_t owner_of_faction(uint32_t pol)
{
    if (!g_on || !pol)
        return 0xFF;
    if (!guest()) {
        const int s = seat_of_faction(pol);
        return s >= 1 ? civ_seats::seat_owner(s, g_seat_player[s]) : 0xFF;
    }
    if (guest() && g_own && pol == g_own)
        return C.sess->my_id();
    return 0xFF;
}

bool team_faction(uint32_t pol)
{
    if (!g_on || !pol)
        return false;
    if (pol == home_pol())
        return true;
    if (!guest())
        return seat_of_faction(pol) >= 1;
    return std::find(g_team_pols.begin(), g_team_pols.end(), pol) != g_team_pols.end();
}

// a partner's nation (not the player civ): its money is its own, its AI the player's
static bool partner_nation(void* country)
{
    if (!g_on || !country || !in_civ() || country == player_civ())
        return false;
    return team_faction(pol_of(country));
}

std::vector<uint8_t> nation_players()
{
    std::vector<uint8_t> out;
    if (!g_on || !host() || !g_any)
        return out;
    for (size_t s = 1; s < founders::kMaxSeats; ++s)
        if (g_nation[s] && g_seat_player[s] < civ_seats::kAwaySeat)
            out.push_back(g_seat_player[s]);
    return out;
}

void* nation_of_player(uint8_t player)
{
    if (!g_on || !host() || !g_any)
        return nullptr;
    for (size_t s = 1; s < founders::kMaxSeats; ++s)
        if (g_nation[s] && g_seat_player[s] == player)
            return g_nation[s];
    return nullptr;
}

void* paying_nation(void* city)
{
    void* pc = player_civ();
    if (!g_on || !city || !host())
        return pc;
    void* n = F<void*>(city, kCityCiv);
    return n && seat_of_faction(pol_of(city)) >= 1 ? n : pc;
}

uint8_t wire_owner(void* city, uint8_t owner)
{
    if (!g_on || !city || !host() || !g_any)
        return owner;
    return civ_seats::wire_owner(owner, seat_of_faction(pol_of(city)) >= 1);
}

uint32_t own_faction() { return g_on && guest() ? g_own : 0; }

bool hold_foundings() { return g_on && !guest() && g_found.phase == Phase::Requested && !g_spawning; }

void charge_build(void* city, float paid)
{
    void* n = paying_nation(city);
    if (!n || n == player_civ() || !(paid > 0) || !std::isfinite(paid))
        return;
    F<float>(n, kWealth) -= paid;
    g_money_spent++;
    H->log(M, SK_INFO, "civ nations: a building of nation %08X's own planner: %.0f from its money", pol_of(n), paid);
}

bool refund(uint8_t player, double amount)
{
    void* n = nation_of_player(player);
    if (!n || !(amount > 0))
        return false;
    F<float>(n, kWealth) += (float)amount;
    return true;
}

bool player_money(uint8_t player, double& money)
{
    void* n = nation_of_player(player);
    if (!n)
        return false;
    money = F<float>(n, kWealth);
    return true;
}

bool wallets() { return g_on && C.sess && (host() ? g_any : (guest() && g_own)); }

void* cursor_list()
{
    if (!g_on || !g_native || !guest() || !g_own || local_play() || !active() || world::load_queued())
        return nullptr;
    void* n = nation_by(g_own);
    if (n)
        g_cursor_lists++;
    return n ? (uint8_t*)n + kSelectable : nullptr;
}

bool friendly(uint32_t target, uint32_t attacker)
{
    if (!g_on || !in_civ())
        return false;
    if (!civ_seats::friendly_fire(team_faction(target), team_faction(attacker), target, attacker))
        return false;
    g_ff++;
    return true;
}

void captured(void* city, uint32_t pol)
{
    const uint8_t o = owner_of_faction(pol);
    if (o >= civ_seats::kAwaySeat || !C.sess)
        return;
    autosave::milestone("a city captured");
    if (C.feed)
        C.feed(C.sess->name_of(o) + " captured a city for their nation");
    (void)city;
}

// ---- the founding (host, outside the frame: it calls the game's founding, writes a file and shares the world) ----
static std::vector<civ_seats::City> city_list()
{
    std::vector<civ_seats::City> out;
    void* home = player_civ();
    for (void* city : all_cities()) {
        void* country = F<void*>(city, kCityCiv);
        civ_seats::City c;
        c.faction = pol_of(city);
        c.nation = country != nullptr;
        c.home = country && country == home;
        c.blank = country && pol_of(city) == pol_of(country) && !F<creations::Key>(city, 0x658).instance &&
                  !F<creations::Key>(country, kNationSpecies).instance;
        c.id = (uint64_t)(uintptr_t)city;
        out.push_back(c);
    }
    return out;
}

static bool can_work()
{
    return g_on && authority() && !world::load_queued() && !world::save_busy() && !world::save_pending() &&
           !world::time_paused() && !world::cinematic_playing();
}

static void share()
{
    std::string err;
    bool first = true, any = false;
    for (const auto& p : C.sess->players()) {
        if (p.id == C.sess->my_id())
            continue;
        any = true;
        if (world::share_with(p.id, err, first))
            g_shares++;
        else
            H->log(M, SK_WARN, "civ nations: the world with the new nation not sent to %u: %s", p.id, err.c_str());
        first = false;
    }
    if (!any && !world::save_game(err))
        H->log(M, SK_WARN, "civ nations: the world with the new nation not saved: %s", err.c_str());
}

static std::string seat_name(int seat)
{
    const uint8_t p = party::player_of_seat(seat);
    if (p < civ_seats::kAwaySeat && C.sess)
        return C.sess->name_of(p);
    const auto& r = party::roster();
    return seat < (int)r.seats.size() && !r.seats[seat].name.empty() ? r.seats[seat].name : "seat " + std::to_string(seat);
}

static void bind(uint32_t faction)
{
    void* nation = nation_by(faction);
    void* home = player_civ();
    const int seat = g_found.seat;
    const creations::Key key = g_found.key;
    g_found = {};
    if (!nation || !home || seat < 1) {
        g_failed++;
        report("the new capital's nation is gone before it was bound");
        return;
    }
    founders::Roster next = g_roster;
    next.entries[0] = {F<creations::Key>(home, kNationSpecies).instance, pol_of(home), identity::own()};
    const auto& party_seats = party::roster().seats;
    next.entries[seat] = {key.instance, faction, seat < (int)party_seats.size() ? party_seats[seat].player : 0};
    std::string err;
    if (!next.entries[0].species) { // the host nation's species key blank: its Tribe village's species, else the host's creature
        founders::Roster tribal;
        if (founders::read(g_planet, tribal, founders::Stage::Tribe) && tribal.entries[0].founded())
            next.entries[0].species = tribal.entries[0].species;
        else if (!party_seats.empty())
            next.entries[0].species = party_seats[0].species;
    }
    if (!founders::record(g_planet, next, err, founders::Stage::Civilization)) {
        g_failed++;
        report("the new nation of " + seat_name(seat) + " was not recorded: " + err);
        return; // (the city stays an NPC nation's)
    }
    F<creations::Key>(nation, kNationSpecies) = key;
    g_roster = next;
    g_founded++;
    refresh();
    void* capital = nullptr;
    for (void* c : all_cities())
        if (pol_of(c) == faction)
            capital = c;
    net::Vec3 at = capital ? city_pos(capital) : net::Vec3{};
    void* hc = nullptr;
    for (void* c : vec_of((uint8_t*)home + kCivCities)) {
        hc = c;
        break;
    }
    char b[260];
    snprintf(b, sizeof b, "%s has their own nation %08X (seat %d, species %08X), capital at (%.0f %.0f %.0f), %.0f m from the host's",
             seat_name(seat).c_str(), faction, seat, key.instance, at.x, at.y, at.z, hc ? net::dist(at, city_pos(hc)) : -1.f);
    report(b);
    if (C.notice)
        C.notice(seat_name(seat) + " has their own nation now");
    share();
}

static void found_task(void* ctx)
{
    g_queued = false;
    if ((uint64_t)(uintptr_t)ctx != g_epoch || !can_work())
        return;
    roster_load();
    refresh();
    void* home = player_civ();
    if (!home || g_planet.empty())
        return;
    if (g_found.phase == Phase::Requested) {
        const uint32_t pol = civ_seats::new_city_since(g_found.before, city_list(), g_roster);
        if (pol == civ_seats::kAmbiguous) {
            g_failed++;
            g_found = {};
            report("the new capital could not be told apart from another new city: no nation bound");
            return;
        }
        if (pol) {
            bind(pol);
            return;
        }
        if (++g_found.waited > 30) {
            g_failed++;
            g_found = {};
            report("the new capital did not appear within 60 s of running game: given up");
        }
        return;
    }
    // the next seat
    const uint32_t host_species = F<creations::Key>(home, kNationSpecies).instance;
    founders::Roster tribal;
    const bool have_tribal = founders::read(g_planet, tribal, founders::Stage::Tribe);
    std::vector<civ_seats::Want> wants;
    const auto& party_seats = party::roster().seats;
    for (int s = 1; s < (int)founders::kPlaySeats; ++s) {
        civ_seats::Want w;
        w.seat = s;
        if (have_tribal && tribal.entries[s].founded())
            w.species = tribal.entries[s].species;
        else if (s < (int)party_seats.size())
            w.species = party_seats[s].species;
        w.known = w.species && creations::key_known({w.species, kSpeciesType, kSpeciesGroup});
        if (w.species)
            wants.push_back(w);
    }
    const int seat = civ_seats::next_capital(g_roster, wants, host_species);
    if (seat < 1)
        return;
    void* ex = skg::cdecl_call<void*>(a_ExpansionGet);
    const auto site = ex ? civ_seats::site_state(F<int>(ex, kExSites), F<int>(ex, kExSitesLeft), all_cities().size())
                         : civ_seats::SiteState::Unknown;
    if (site != civ_seats::SiteState::Ready) {
        report(seat_name(seat) + "'s capital waits: " + civ_seats::site_name(site), site == civ_seats::SiteState::NoSite);
        return;
    }
    const uint8_t player = party::player_of_seat(seat);
    if (player < civ_seats::kAwaySeat) { // its creature kept with the planet (an away player's nation loads without it)
        std::string err;
        if (!creations::archive_guest_model(player, g_planet, err))
            H->log(M, SK_WARN, "civ nations: %s's creature not archived with the planet: %s", seat_name(seat).c_str(), err.c_str());
    }
    uint32_t species = 0;
    for (auto& w : wants)
        if (w.seat == seat)
            species = w.species;
    g_found = {};
    g_found.phase = Phase::Requested;
    g_found.seat = seat;
    g_found.key = {species, kSpeciesType, kSpeciesGroup};
    g_found.before = city_list();
    g_spawning = true;
    const bool ok = found_next_site(); // (civ.cpp h_SpawnNextCity: the guests found the same site)
    g_spawning = false;
    if (!ok) {
        g_found = {};
        g_failed++;
        report("the game founded no city for " + seat_name(seat) + "'s capital");
        return;
    }
    report("founding " + seat_name(seat) + "'s capital (seat " + std::to_string(seat) + ")");
}

// ---- the alliance (outside the frame: the relation change can reach the UI) ----
static void ally_task(void* ctx)
{
    if ((uint64_t)(uintptr_t)ctx != g_epoch || !g_on || !authority() || world::load_queued())
        return;
    refresh();
    void* mgr = a_RelGet ? skg::cdecl_call<void*>(a_RelGet) : nullptr;
    if (!mgr || !a_RelCalc || !a_RelApply)
        return;
    std::vector<uint32_t> f;
    for (void* n : g_nation)
        if (n)
            f.push_back(pol_of(n));
    const float allied = F<float>(mgr, 0x1C);
    uint32_t raised = 0;
    for (size_t i = 0; i < f.size(); ++i)
        for (size_t j = i + 1; j < f.size(); ++j)
            for (int dir = 0; dir < 2; ++dir) {
                const uint32_t x = dir ? f[j] : f[i], y = dir ? f[i] : f[j];
                const float now = thiscall<float>(a_RelCalc, mgr, x, y, 0);
                const float d = tribe_seats::ally_delta(now, allied);
                if (d > 0) {
                    (void)thiscall<float>(a_RelApply, mgr, x, y, 0x0530CF07u, d);
                    raised++;
                }
            }
    if (raised) {
        g_allied += raised;
        H->log(M, SK_INFO, "civ nations: %u relation(s) between the players' nations raised to allies", raised);
    }
}

// ---- the team campaign (outside the frame: RaiseLevel can start the game's unlock caption) ----
static size_t team_city_count(size_t* live)
{
    size_t n = 0, all = 0;
    for (void* c : all_cities()) {
        all++;
        n += team_faction(pol_of(c));
    }
    if (live)
        *live = all;
    return n;
}

static void campaign_task(void* ctx)
{
    if ((uint64_t)(uintptr_t)ctx != g_epoch || !g_on || !g_team || !can_work() || !g_any || !a_CityTotal || !a_RaiseLevel)
        return;
    refresh();
    size_t live = 0;
    const size_t team = team_city_count(&live);
    const int total = skg::cdecl_call<int>(a_CityTotal);
    for (void* n : g_nation) {
        if (!n)
            continue;
        const int have = F<int>(n, kLevel), want = civ_seats::team_tech(team, total, have);
        if (want > have) {
            thiscall<void>(a_RaiseLevel, n, want);
            g_levels++;
            H->log(M, SK_INFO, "civ nations: nation %08X's level %d -> %d (the team's %zu cities of %d)", pol_of(n), have, want, team,
                   total);
        }
    }
    void* home = player_civ();
    if (g_won_shown || !home || !civ_seats::team_won(team, live, total) || F<int>(home, kLevel) < 4)
        return;
    if (world::cinematic_playing() || world::time_paused())
        return; // (RaiseLevel's caption: the next time)
    g_won_shown = true;
    g_won++;
    report("only the players' nations are left: the planet is won (" + std::to_string(team) + " cities)", true);
    if (a_GameCiv && a_ShowWon && a_WonPause)
        if (void* gc = skg::cdecl_call<void*>(a_GameCiv)) {
            thiscall<void>(a_ShowWon, gc);
            thiscall<void>(a_WonPause, gc);
        }
}

void host_tick(double now)
{
    if (!g_on || !authority())
        return;
    roster_load();
    refresh();
    if (host()) {
    // the owners of the partner nations' cities and vehicles (every host tick: a player joins, leaves, comes back)
    for (void* c : g_host_cities) {
        const int s = seat_of_faction(pol_of(c));
        if (s >= 1)
            g_city_owner[c] = civ_seats::seat_owner(s, g_seat_player[s]);
    }
    hv_each([&](const HvRef& x) {
        const int s = seat_of_faction(x.h->pol);
        if (s >= 1)
            x.h->owner = civ_seats::seat_owner(s, g_seat_player[s]);
    });
    if (now >= g_wallet_next) {
        g_wallet_next = now + 0.5;
        for (size_t s = 1; s < founders::kMaxSeats; ++s)
            if (g_nation[s] && g_seat_player[s] < civ_seats::kAwaySeat)
                civ_wallet::nation_sync(g_seat_player[s], F<float>(g_nation[s], kWealth));
    }
    } // host()
    if (world::load_queued() || world::cinematic_playing() || world::time_paused())
        return;
    if (g_any && now >= g_ally_next) {
        g_ally_next = now + 5;
        H->run_outside_frame(ally_task, (void*)(uintptr_t)g_epoch);
    }
    if (g_any && g_team && now >= g_camp_next) {
        g_camp_next = now + 0.5;
        H->run_outside_frame(campaign_task, (void*)(uintptr_t)g_epoch);
    }
    if (!g_queued && now >= g_next) {
        g_next = now + 2;
        g_queued = true;
        H->run_outside_frame(found_task, (void*)(uintptr_t)g_epoch);
    }
}

void guest_tick(double)
{
    g_own = 0;
    g_team_pols.clear();
    if (!g_on || !guest() || !active())
        return;
    const uint8_t me = C.sess->my_id();
    const uint32_t hp = home_pol();
    for (auto& [c, o] : g_city_owner) {
        if (!c || !civ_seats::team_owner(o) || !game::listed(c))
            continue;
        const uint32_t pol = pol_of(c);
        if (std::find(g_team_pols.begin(), g_team_pols.end(), pol) == g_team_pols.end())
            g_team_pols.push_back(pol);
        if (o == me && pol != hp)
            g_own = pol;
    }
}

void forget()
{
    g_read = false;
    g_planet.clear();
    g_roster = {};
    g_nation = {};
    g_any = false;
    g_found = {};
    g_queued = false;
    g_won_shown = false;
    g_own = 0;
    g_team_pols.clear();
    g_epoch++;
}
LIFECYCLE_HOOK("civ.nations", lifecycle::kSessionEnd | lifecycle::kWorldChange, -45, [](const lifecycle::Ctx&) { forget(); }, false);

// ---- hooks ----
// cCity / cVehicle IsPlayerOwned (Lineages BUG-9054: the city's and the vehicle's spatial vt 0x58; plain ret): a
// guest's own nation is the player's for its game, the host's nation is not (it has its own)
using Owned_t = bool(SK_THISCALL*)(void*);
static Owned_t o_CityOwned, o_VehicleOwned;
static int owned_answer(void* obj)
{
    if (!g_on || !g_native || !g_own || !guest() || local_play() || game::mode_id() != modes::kCiv)
        return -1;
    const uint32_t pol = pol_of(obj);
    if (pol == g_own)
        return 1;
    if (pol == home_pol())
        return 0;
    return -1;
}
static bool SK_THISCALL h_CityOwned(void* spatial)
{
    const int a = owned_answer((uint8_t*)spatial - kCitySpiral);
    if (a >= 0) {
        g_owned_answers++;
        return a == 1;
    }
    return o_CityOwned(spatial);
}
static bool SK_THISCALL h_VehicleOwned(void* spatial)
{
    const int a = owned_answer((uint8_t*)spatial - kVSpatial);
    if (a >= 0) {
        g_owned_answers++;
        return a == 1;
    }
    return o_VehicleOwned(spatial);
}

// the nation AI (Civilization_AI, thiscall(nation), no stack arguments): a player's nation runs its player branch (its
// GetPlayerCivilization at Civilization_AI_PlayerCiv_Ret answers that nation: no NPC orders, purchases or wars); on
// the host every other nation waits while a capital is being founded (the new nation must not act as an NPC first)
using PlayerCiv_t = void*(SK_THISCALL*)(void*);
using CivAI_t = void(SK_THISCALL*)(void*);
static PlayerCiv_t o_PlayerCiv;
static CivAI_t o_CivAI;
static void* SK_THISCALL h_PlayerCiv(void* mgr)
{
    if (g_ai_nation && (uintptr_t)__builtin_return_address(0) == a_AIPlayerRet)
        return g_ai_nation;
    return o_PlayerCiv(mgr);
}
static void SK_THISCALL h_CivAI(void* country)
{
    if (g_on && authority() && g_found.phase == Phase::Requested && country != player_civ() &&
        !g_roster.contains_faction(pol_of(country))) {
        g_ai_held++;
        return;
    }
    if (!g_native || !partner_nation(country)) {
        o_CivAI(country);
        return;
    }
    void* prev = g_ai_nation;
    g_ai_nation = country;
    g_ai_player++;
    o_CivAI(country);
    g_ai_nation = prev;
}

// a player's nation's money is its own (+0x98): GetMoney adds an NPC nation's budgets (+0x4AC / +0x4B0), SpendMoney
// refills them (docs/re/civ.md#civ-wsync-owners)
using GetMoney_t = float(SK_THISCALL*)(void*);
using SpendMoney_t = void(SK_THISCALL*)(void*, float);
static GetMoney_t o_GetMoney;
static SpendMoney_t o_SpendMoney;
static float SK_THISCALL h_GetMoney(void* country)
{
    if (partner_nation(country)) {
        g_money_reads++;
        return F<float>(country, kWealth);
    }
    return o_GetMoney(country);
}
static void SK_THISCALL h_SpendMoney(void* country, float amount)
{
    if (partner_nation(country)) {
        g_money_spent++;
        F<float>(country, kWealth) -= amount;
        return;
    }
    o_SpendMoney(country, amount);
}

// the HUD's progress (CivMode_Progress, its caller CivHUD_Progress_Ret): every other player nation's cities added
using Progress_t = float(SK_THISCALL*)(void*);
static Progress_t o_Progress;
static float SK_THISCALL h_Progress(void* strategy)
{
    const float own = o_Progress(strategy);
    if ((uintptr_t)__builtin_return_address(0) != a_HudRet || !g_on || !g_team || !in_civ() || !a_CityTotal)
        return own;
    if (!guest() ? !g_any : g_team_pols.size() < 2)
        return own;
    const uint32_t hp = home_pol();
    size_t others = 0;
    for (void* c : all_cities()) {
        const uint32_t pol = pol_of(c);
        others += pol != hp && team_faction(pol);
    }
    g_hud++;
    g_hud_own = own;
    g_hud_team = civ_seats::team_progress(own, others, skg::cdecl_call<int>(a_CityTotal));
    return g_hud_team;
}

void init()
{
    a_CityTotal = H->sym("SporeCoop::cCivExpansion_GetCityTotal");
    a_RaiseLevel = H->sym("SporeCoop::cCivilization_RaiseLevel");
    a_GameCiv = H->sym("SporeCoop::GameCiv_Get");
    a_ShowWon = H->sym("SporeCoop::GameCiv_ShowPlanetWon");
    a_WonPause = H->sym("SporeCoop::GameCiv_PlanetWonPause");
    a_RelGet = H->sym("Simulator::cRelationshipManager::Get");
    a_RelCalc = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
    a_RelApply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
    a_AIPlayerRet = H->sym("SporeCoop::Civilization_AI_PlayerCiv_Ret");
    a_HudRet = H->sym("SporeCoop::CivHUD_Progress_Ret");
    if (uintptr_t a = H->sym("SporeCoop::City_IsPlayerOwned"))
        H->hook_create(M, (void*)a, (void*)h_CityOwned, (void**)&o_CityOwned);
    if (uintptr_t a = H->sym("SporeCoop::Vehicle_IsPlayerOwned"))
        H->hook_create(M, (void*)a, (void*)h_VehicleOwned, (void**)&o_VehicleOwned);
    if (uintptr_t ai = H->sym("SporeCoop::Civilization_AI"); ai && a_AIPlayerRet) {
        H->hook_create(M, (void*)a_PlayerCiv, (void*)h_PlayerCiv, (void**)&o_PlayerCiv);
        H->hook_create(M, (void*)ai, (void*)h_CivAI, (void**)&o_CivAI);
    }
    H->hook_create(M, (void*)a_GetMoney, (void*)h_GetMoney, (void**)&o_GetMoney);
    H->hook_create(M, (void*)a_SpendMoney, (void*)h_SpendMoney, (void**)&o_SpendMoney);
    if (uintptr_t p = H->sym("SporeCoop::CivMode_Progress"); p && a_HudRet)
        H->hook_create(M, (void*)p, (void*)h_Progress, (void**)&o_Progress);
}

std::string status()
{
    char b[400];
    snprintf(b, sizeof b,
             "civ nations: fix=%d native=%d team=%d role=%s any=%d founded=%u failed=%u allied=%u shares=%u ai_player=%u ai_held=%u "
             "money_reads=%u money_spent=%u hud=%u (own %.3f team %.3f) friendly_fire=%u levels=%u won=%u owned_answers=%u "
             "cursor_lists=%u founding=%s last: %s\n",
             (int)g_on, (int)g_native, (int)g_team, host() ? "host" : guest() ? "guest" : "-", (int)g_any, g_founded, g_failed,
             g_allied, g_shares, g_ai_player, g_ai_held, g_money_reads, g_money_spent, g_hud, g_hud_own, g_hud_team, g_ff, g_levels,
             g_won, g_owned_answers, g_cursor_lists, g_found.phase == Phase::Requested ? ("seat " + std::to_string(g_found.seat)).c_str() : "-",
             g_last.c_str());
    std::string out = b;
    if (!in_civ())
        return out;
    void* ex = skg::cdecl_call<void*>(a_ExpansionGet);
    size_t live = 0;
    const size_t team = team_city_count(&live);
    snprintf(b, sizeof b, "  cities %zu, the team's %zu, planet total %d, sites %d left %d\n", live, team,
             a_CityTotal ? skg::cdecl_call<int>(a_CityTotal) : -1, ex ? F<int>(ex, kExSites) : -1, ex ? F<int>(ex, kExSitesLeft) : -1);
    out += b;
    if (!guest()) {
        out += "  roster: " + founders::describe(g_roster) + "\n";
        for (size_t s = 0; s < founders::kMaxSeats; ++s) {
            void* n = g_nation[s];
            if (!n && !g_roster.entries[s].founded())
                continue;
            size_t cities = 0;
            if (n)
                for (void* c : all_cities())
                    cities += F<void*>(c, kCityCiv) == n;
            const uint8_t o = s ? civ_seats::seat_owner((int)s, g_seat_player[s]) : 0;
            snprintf(b, sizeof b, "  seat %zu: owner=%d nation=%08X species=%08X cities=%zu money=%.0f level=%d\n", s,
                     o >= civ_seats::kAwaySeat ? -1 : (int)o, n ? pol_of(n) : g_roster.entries[s].faction,
                     n ? F<creations::Key>(n, kNationSpecies).instance : 0, cities, n ? F<float>(n, kWealth) : -1.f,
                     n ? F<int>(n, kLevel) : -1);
            out += b;
        }
    } else {
        void* n = nation_by(g_own);
        size_t cities = 0;
        for (void* c : all_cities())
            cities += g_own && pol_of(c) == g_own;
        snprintf(b, sizeof b, "  own nation %08X species=%08X cities=%zu; team nations %zu\n", g_own,
                 n ? F<creations::Key>(n, kNationSpecies).instance : 0, cities, g_team_pols.size());
        out += b;
    }
    return out;
}

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[1] == "found" && authority()) { // tests: the founding pass now
        if (g_queued)
            return "busy\n";
        g_queued = true;
        H->run_outside_frame(found_task, (void*)(uintptr_t)g_epoch);
        return "queued\n";
    }
    if (a.size() >= 2 && a[1] == "ally" && authority()) {
        H->run_outside_frame(ally_task, (void*)(uintptr_t)g_epoch);
        return "queued\n";
    }
    if (a.size() >= 2 && a[1] == "spawn" && authority()) { // tests: the next free site gets an NPC city now (instantly)
        if (!in_civ())
            return "not in Civ\n";
        return found_next_site(true) ? "founded\n" : "no city founded\n";
    }
    if (a.size() >= 4 && a[1] == "capture" && authority()) { // tests: capture <host city index> <seat> [type]: that seat's nation takes it
        const size_t ci = (size_t)atoi(a[2].c_str());
        const int seat = atoi(a[3].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        void* n = seat >= 0 && seat < (int)founders::kMaxSeats ? g_nation[seat] : nullptr;
        if (!city || !n)
            return "no such city / nation\n";
        // (pol, type, instant 1, no nuked caption, a5 1: the instant path of a surrender, docs/re/civ.md#civ-stage-end)
        const int r = h_Capture(city, pol_of(n), a.size() > 4 ? atoi(a[4].c_str()) : 0, 1, 0, 1);
        return "capture -> " + std::to_string(r) + ", the city's nation now " + std::to_string(seat_of_faction(pol_of(city))) + "\n";
    }
    if (a.size() >= 2 && a[1] == "citizens") { // the live citizens by their city's nation and species (Lineages)
        struct Census {
            std::vector<void*> cities;
            std::map<std::pair<uint32_t, uint32_t>, unsigned> groups;
        } census{all_cities(), {}};
        game::for_each_object([](void* obj, uint32_t noun, void* data) {
            auto& cs = *static_cast<Census*>(data);
            if (noun != 0x018EB4B7 || !game::object_alive(obj)) // cCreatureCitizen
                return true;
            void* city = F<void*>(obj, 0x1010); // its owning city
            const bool known = std::find(cs.cities.begin(), cs.cities.end(), city) != cs.cities.end();
            ++cs.groups[{known ? pol_of(city) : 0, game::species_key(obj).instance}];
            return true;
        }, &census);
        std::string out;
        char b[120];
        for (auto& [k, n] : census.groups) {
            snprintf(b, sizeof b, "citizens city_pol=%08X species=%08X count=%u\n", k.first, k.second, n);
            out += b;
        }
        return out.empty() ? "no live citizen\n" : out;
    }
    if (a.size() >= 2 && a[1] == "relation" && authority()) { // the relation between the host's nation and each partner's
        void* mgr = a_RelGet ? skg::cdecl_call<void*>(a_RelGet) : nullptr;
        std::string out;
        char b[160];
        for (size_t s = 1; s < founders::kMaxSeats && mgr; ++s)
            if (void* n = g_nation[s]) {
                const float r1 = thiscall<float>(a_RelCalc, mgr, pol_of(n), home_pol(), 0);
                const float r2 = thiscall<float>(a_RelCalc, mgr, home_pol(), pol_of(n), 0);
                snprintf(b, sizeof b, "seat %zu %08X: relation to the host %.1f, the host's to it %.1f (allied %.1f)\n", s,
                         pol_of(n), r1, r2, F<float>(mgr, 0x1C));
                out += b;
            }
        return out.empty() ? "no partner nation\n" : out;
    }
    return status();
}

} // namespace civ::nations
