// The campaign's party (party.h; Lineages port phase 2, BUG-9260).
#include "party.h"

#include <cmath>
#include <cstdio>

#include "creations.h"
#include "game.h"
#include "identity.h"
#include "lifecycle.h"
#include "modes.h"
#include "world.h"

namespace party {

static const SkHost* H;
static SkMod* M;
static net::Session* S;
static founders::Party g_party;
static std::string g_planet; // the planet g_party belongs to ("" = none loaded)
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

// this planet's party, or a new one with the host in seat 0
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
        g_party.seats.push_back({identity::own(), species_of(S->my_id()), name_of(S->my_id())});
        g_dirty = true;
        H->log(M, SK_INFO, "party: a new party on this planet, the host in seat 0");
    }
    if (!g_party.seats[0].player && identity::own()) { // written before the host's identity was known
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
    // the galaxy menu still names the planet played last: a player joining there is not seated on it
    if (planet.empty() || !planet.ends_with(".spo") || game::mode_id() == modes::kMenu) {
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
                    H->log(M, SK_INFO, "party: %s gets no seat, the campaign's %zu seats are taken",
                           name_of(p.id).c_str(), founders::kPlaySeats);
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
        H->run_outside_frame(save_outside, nullptr); // a file write: outside the frame (CLAUDE.md 2)
    }
}

static void forget()
{
    g_planet.clear();
    g_party = {};
    g_dirty = false;
}
// a new world or the session's end: the next tick loads the party of the planet then played
LIFECYCLE_HOOK("party.forget", lifecycle::kSessionEnd | lifecycle::kWorldChange, -11,
               [](const lifecycle::Ctx&) { forget(); }, false);

const founders::Party& roster() { return g_party; }
const std::string& planet() { return g_planet; }

bool set_history(int seat, bool own, const uint32_t (&cards)[founders::kCards], float social, float combat)
{
    if (!is_host() || seat < 0 || seat >= (int)g_party.seats.size() || !std::isfinite(social) || !std::isfinite(combat))
        return false;
    auto& s = g_party.seats[seat];
    bool same = s.own_cards == own && s.social == social && s.combat == combat;
    for (size_t i = 0; i < founders::kCards; ++i)
        same = same && s.cards[i] == cards[i];
    if (same)
        return true;
    s.own_cards = own;
    for (size_t i = 0; i < founders::kCards; ++i)
        s.cards[i] = cards[i];
    s.social = social;
    s.combat = combat;
    g_dirty = true; // the next tick saves it (outside the frame)
    return true;
}

int seat_of_player(uint8_t player)
{
    if (!is_host() || g_party.seats.empty())
        return -1;
    if (player == S->my_id())
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

bool party_species(uint32_t creature_instance)
{
    if (!creature_instance)
        return false;
    for (const auto& s : g_party.seats)
        if (s.species == creature_instance)
            return true;
    return false;
}

std::string status()
{
    char b[200];
    // the planet's file name only (no folder: status lines go into reports)
    const size_t slash = g_planet.find_last_of("\\/");
    const std::string planet = g_planet.empty() ? "-" : g_planet.substr(slash == std::string::npos ? 0 : slash + 1);
    snprintf(b, sizeof b, "party: planet=%s seats=%zu saves=%u errors=%u dirty=%d\n", planet.c_str(), g_party.seats.size(),
             g_saves, g_save_errors, (int)g_dirty);
    std::string out = b;
    for (size_t i = 0; i < g_party.seats.size(); ++i) {
        const auto& s = g_party.seats[i];
        const uint8_t p = player_of_seat((int)i);
        snprintf(b, sizeof b, "  seat %zu: %s id=%08X species=%08X player=%s\n", i, s.name.empty() ? "?" : s.name.c_str(),
                 (unsigned)(s.player >> 32), s.species, p == kNobody ? "offline" : std::to_string(p).c_str());
        out += b;
        // BUG-9263: the seat's own stage history
        snprintf(b, sizeof b, "    cards own=%d %08X %08X %08X %08X %08X social=%.1f combat=%.1f\n", (int)s.own_cards,
                 s.cards[0], s.cards[1], s.cards[2], s.cards[3], s.cards[4], s.social, s.combat);
        out += b;
    }
    return out;
}

} // namespace party
