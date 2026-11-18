#include "party.h"

#include <cstdio>

#include "creations.h"
#include "game.h"
#include "identity.h"
#include "world.h"

namespace party {

static const SkHost* H;
static SkMod* M;
static net::Session* S;
static founders::Party g_party;
static std::string g_planet;        // the planet g_party belongs to ("" = none loaded)
static bool g_dirty, g_save_queued;
static double g_next;
static uint32_t g_saves, g_save_errors, g_full_logged;

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
}

static bool is_host() { return S && S->role() == net::Role::Host; }

static std::string name_of(uint8_t player)
{
    if (S)
        for (const auto& p : S->players())
            if (p.id == player)
                return p.name.substr(0, 64);
    return {};
}

static uint32_t species_of(uint8_t player)
{
    // the host plays its world's species (the avatar's own key); a partner's creature is its shared key here
    if (S && player == S->my_id())
        if (void* av = game::avatar())
            if (uint32_t own = game::species_key(av).instance)
                return own;
    return creations::player_key(player, creations::kCreature).instance;
}

// load this planet's party, or start one with the host in seat 0
static void load(const std::string& planet)
{
    g_planet = planet;
    g_dirty = false;
    founders::Party saved;
    if (founders::read_party(planet, saved)) {
        g_party = std::move(saved);
        H->log(M, SK_INFO, "party: %zu seat(s) on this planet", g_party.seats.size());
    } else {
        g_party = {};
        g_party.seats.push_back({identity::own(), species_of(0), name_of(0)});
        g_dirty = true;
    }
    if (!g_party.seats[0].player && identity::own()) { // a party written before the host's identity was known
        g_party.seats[0].player = identity::own();
        g_dirty = true;
    }
}

static void save_outside(void*)
{
    g_save_queued = false;
    if (!g_dirty || g_planet.empty() || g_planet != world::current_game_key())
        return;
    std::string err;
    if (founders::record_party(g_planet, g_party, err)) {
        g_dirty = false;
        g_saves++;
    } else if (g_save_errors++ < 5) {
        H->log(M, SK_WARN, "party: not saved: %s", err.c_str());
    }
}

void tick(double now)
{
    if (!is_host() || now < g_next)
        return;
    g_next = now + 1;
    const std::string planet = world::current_game_key();
    if (planet.empty() || !planet.ends_with(".spo")) {
        g_planet.clear();
        g_party = {};
        return;
    }
    if (planet != g_planet)
        load(planet);
    for (const auto& p : S->players()) {
        const uint64_t id = p.id == S->my_id() ? identity::own() : identity::of(p.id);
        if (!id)
            continue; // an older partner, or its identity has not arrived yet
        int seat = g_party.seat_of(id);
        if (seat < 0) {
            if (g_party.seats.size() >= founders::kPlaySeats) {
                if (g_full_logged++ < 3)
                    H->log(M, SK_INFO, "party: %s gets no seat, the campaign's %zu seats are taken", name_of(p.id).c_str(),
                           founders::kPlaySeats);
                continue;
            }
            g_party.seats.push_back({id, 0, {}});
            seat = int(g_party.seats.size() - 1);
            H->log(M, SK_INFO, "party: %s takes seat %d", name_of(p.id).c_str(), seat);
            g_dirty = true;
        }
        auto& s = g_party.seats[seat];
        const uint32_t species = species_of(p.id);
        const std::string name = name_of(p.id);
        if ((species && species != s.species) || (!name.empty() && name != s.name)) {
            if (species)
                s.species = species;
            if (!name.empty())
                s.name = name;
            g_dirty = true;
        }
    }
    if (g_dirty && !g_save_queued) {
        g_save_queued = true;
        H->run_outside_frame(save_outside, nullptr);
    }
}

void on_world_change()
{
    g_planet.clear();
    g_party = {};
    g_dirty = false;
}

void on_session_end() { on_world_change(); }

const founders::Party& roster() { return g_party; }

int seat_of_player(uint8_t player)
{
    if (!is_host() || g_party.seats.empty())
        return -1;
    if (S && player == S->my_id())
        return 0;
    return g_party.seat_of(identity::of(player));
}

uint8_t player_of_seat(int seat)
{
    if (!is_host() || seat < 0 || seat >= (int)g_party.seats.size())
        return kNobody;
    if (seat == 0)
        return S->my_id();
    return identity::player_of(g_party.seats[seat].player);
}

bool bind(int seat, uint8_t player)
{
    if (!is_host() || seat <= 0 || seat >= (int)g_party.seats.size() || g_party.seats[seat].player)
        return false;
    const uint64_t id = identity::of(player);
    if (!id || g_party.seat_of(id) >= 0)
        return false;
    g_party.seats[seat].player = id;
    g_dirty = true;
    H->log(M, SK_INFO, "party: seat %d is %s's", seat, name_of(player).c_str());
    return true;
}

bool party_species(uint32_t creature_instance)
{
    if (!creature_instance)
        return false;
    for (const auto& s : g_party.seats)
        if (s.species == creature_instance)
            return true;
    return false;
}

int founder_seat_of(const founders::Roster& r, uint8_t player, LegacyMatch legacy)
{
    if (!S || player == kNobody)
        return -1;
    const size_t n = r.seats();
    if (player == S->my_id() && S->role() == net::Role::Host)
        return n ? 0 : -1;
    const uint64_t id = identity::of(player);
    if (id) {
        const int seat = r.seat_of_player(id);
        if (seat >= 1)
            return seat;
    }
    for (size_t seat = 1; seat < n; ++seat) // a seat without identity: the stage's own match
        if (!r.entries[seat].player && legacy && legacy((int)seat, player))
            return (int)seat;
    return -1;
}

uint8_t founder_player_of(const founders::Roster& r, int seat, LegacyMatch legacy)
{
    if (!S || seat < 0 || seat >= (int)r.seats())
        return kNobody;
    if (seat == 0)
        return S->role() == net::Role::Host ? S->my_id() : 0;
    if (r.entries[seat].player)
        return identity::player_of(r.entries[seat].player);
    for (const auto& p : S->players())
        if (p.id != S->my_id() && legacy && legacy(seat, p.id) && founder_seat_of(r, p.id, legacy) == seat)
            return p.id;
    return kNobody;
}

std::vector<uint8_t> unseated_partners(const founders::Roster& r, LegacyMatch legacy)
{
    std::vector<uint8_t> out;
    if (!S)
        return out;
    for (const auto& p : S->players())
        if (p.id != S->my_id() && founder_seat_of(r, p.id, legacy) < 0)
            out.push_back(p.id);
    return out;
}

std::string status()
{
    char b[200];
    snprintf(b, sizeof b, "party: planet=%s seats=%zu saves=%u errors=%u dirty=%d\n", g_planet.empty() ? "-" : "set",
             g_party.seats.size(), g_saves, g_save_errors, (int)g_dirty);
    std::string out = b;
    for (size_t i = 0; i < g_party.seats.size(); ++i) {
        const auto& s = g_party.seats[i];
        const uint8_t p = player_of_seat((int)i);
        snprintf(b, sizeof b, "  seat %zu: %s id=%08X species=%08X player=%s\n", i, s.name.empty() ? "?" : s.name.c_str(),
                 (unsigned)(s.player >> 32), s.species, p == kNobody ? "offline" : std::to_string(p).c_str());
        out += b;
    }
    return out;
}

} // namespace party
