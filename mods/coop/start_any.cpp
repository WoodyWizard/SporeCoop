// Every player's own start in a new game at any stage after Cell (start_any.h, BUG-9293).
#include "start_any.h"

#include <windows.h>

#include <map>
#include <set>

#include "creations.h"
#include "fixes.h"
#include "founders.h"
#include "game.h"
#include "identity.h"
#include "modes.h"
#include "net/protocol.h"
#include "own_world.h"
#include "party.h"
#include "start_any_logic.h"
#include "world.h"

namespace start_any {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

static bool g_fix = true;
COOP_FIX("party.start_any", "BUG-9293", g_fix,
         "party: in a new game started at Creature, Tribe, Civ or Space every guest chooses its own creature and the cards of the earlier stages");

// ---- host
static std::string g_planet;
static std::vector<Record> g_recs;
static bool g_dirty, g_save_queued;
static double g_next_planet, g_next_save, g_next_tick;
static std::set<uint8_t> g_asked;
static std::map<uint8_t, double> g_here_since;
static uint32_t g_asks, g_answers, g_refused, g_applied, g_saves, g_loaded;
// ---- guest
static int g_prompt;           // the start stage asked for, 0 = none
static uint32_t g_want;        // the creature we chose (0 = keep)
static uint32_t g_rx_asks, g_answers_tx, g_taken, g_creature_applies;

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
}

bool enabled() { return g_fix; }

uint32_t seat_card(uint64_t id, int stage)
{
    if (!g_fix || !id)
        return 0;
    char b[24];
    snprintf(b, sizeof b, "id:%016llX", (unsigned long long)id);
    const Record* r = find_record(g_recs, b);
    return r ? card_id(r->c, stage) : 0;
}
static bool is_host() { return S && S->connected() && S->role() == net::Role::Host; }
static bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }

// the stage index of a game mode (0 Cell .. 4 Space, -1 none)
static int stage_of(uint32_t m)
{
    switch (m) {
    case modes::kCell: return 0;
    case modes::kCreature: return 1;
    case modes::kTribe: return 2;
    case modes::kCiv: return 3;
    case modes::kSpace: return 4;
    default: return -1;
    }
}

static void save_outside(void*)
{
    g_save_queued = false;
    if (!g_dirty || g_planet.empty() || g_planet != world::current_game_key())
        return;
    if (!founders::planet_saved(g_planet))
        return; // a new game: written after its first save
    if (founders::write_side(g_planet, L".duostartany", encode_file(g_recs))) {
        g_dirty = false;
        g_saves++;
    }
}

static void planet_tick(double now)
{
    if (now < g_next_planet)
        return;
    g_next_planet = now + 1;
    const std::string planet = world::current_game_key();
    if (planet.empty() || !planet.ends_with(".spo") || game::mode_id() == modes::kMenu || planet == g_planet)
        return;
    g_planet = planet;
    g_recs.clear();
    g_dirty = false;
    g_asked.clear();
    std::vector<uint8_t> bytes;
    std::vector<Record> recs;
    if (founders::read_side(planet, L".duostartany", bytes, 8 + kMaxRecords * (256 + 16)) && decode_file(bytes, recs)) {
        g_recs = std::move(recs);
        g_loaded++;
        H->log(M, SK_INFO, "start any: %zu player choice(s) of this planet", g_recs.size());
    }
}

// the seat's own history from the choice: the chosen cards of the stages before the start, the rest 0 (cards.cpp
// decides the Space archetype and the later stages' cards as the stages are done)
static bool apply_cards(uint8_t player, const Choice& c)
{
    const int seat = party::seat_of_player(player);
    if (seat < 1)
        return false;
    uint32_t cards[founders::kCards] = {};
    for (int i = 0; i < kCardStages; i++)
        cards[i] = card_id(c, i);
    return party::set_history(seat, true, cards, 0, 0);
}

static void host_tick(double now)
{
    planet_tick(now);
    if (now < g_next_tick)
        return;
    g_next_tick = now + 0.5;
    const int stage = stage_of(game::mode_id());
    const bool world_here = !g_planet.empty() && g_planet == world::current_game_key() && !world::cinematic_playing();
    for (const auto& p : S->players()) {
        if (p.id == S->my_id())
            continue;
        if (!world_here || !own_world::peer_here(p.id)) {
            g_here_since.erase(p.id);
            continue;
        }
        auto it = g_here_since.emplace(p.id, now).first;
        const Record* rec = find_record(g_recs, identity::key(p.id));
        if (rec && rec->c.stage == stage) { // (a seat the cards tick seeded from the host again: ours back)
            const int seat = party::seat_of_player(p.id);
            if (seat >= 1) {
                const auto& s = party::roster().seats[seat];
                bool same = s.own_cards;
                for (int i = 0; i < kCardStages && same; i++)
                    same = s.cards[i] == card_id(rec->c, i);
                if (!same && apply_cards(p.id, rec->c))
                    g_applied++;
            }
        }
        AskState st;
        st.setting = enabled();
        st.stage = stage;
        st.new_party = party::planet_new();
        st.seated = party::seat_of_player(p.id) >= 1;
        st.in_world = now - it->second >= 3;
        st.recorded = rec != nullptr;
        st.asked = g_asked.count(p.id) != 0;
        if (!should_ask(st))
            continue;
        g_asked.insert(p.id);
        net::Writer w;
        w.u8(kAsk);
        w.u8((uint8_t)stage);
        S->send_reliable_to(p.id, net::MSG_START_ANY, w);
        g_asks++;
        H->log(M, SK_INFO, "start any: asked player %u (%s) for its own start at %s", p.id, p.name.c_str(),
               modes::mode_name(game::mode_id()));
    }
    if (g_dirty && !g_save_queued && now >= g_next_save) {
        g_next_save = now + 2;
        g_save_queued = true;
        H->run_outside_frame(save_outside, nullptr); // a file write: outside the frame (CLAUDE.md 2)
    }
}

static void host_answer(uint8_t from, net::Reader& r)
{
    Choice c;
    net::Writer w;
    w.u8(kTaken);
    const std::string key = identity::key(from);
    if (!read_choice(r, c) || !enabled() || g_planet.empty() || key.empty() || find_record(g_recs, key) ||
        c.stage != stage_of(game::mode_id()) || g_recs.size() >= kMaxRecords || !apply_cards(from, c)) {
        g_refused++;
        w.u8(0);
        S->send_reliable_to(from, net::MSG_START_ANY, w);
        H->log(M, SK_WARN, "start any: player %u's answer refused", from);
        return;
    }
    g_recs.push_back({key, c});
    g_dirty = true;
    g_answers++;
    w.u8(1);
    S->send_reliable_to(from, net::MSG_START_ANY, w);
    H->log(M, SK_INFO, "start any: player %u's own start: creature %08X, cards %08X %08X %08X %08X", from, c.creature,
           card_id(c, 0), card_id(c, 1), card_id(c, 2), card_id(c, 3));
}

// ---- guest
int prompt() { return is_guest() ? g_prompt : 0; }

bool answer(uint32_t creature, const int8_t (&cards)[4], std::string* why)
{
    auto no = [&](const char* w) {
        if (why)
            *why = w;
        return false;
    };
    if (!is_guest() || !g_prompt)
        return no("nobody asked for a start choice now");
    Choice c;
    c.stage = (uint8_t)g_prompt;
    c.creature = creature;
    for (int i = 0; i < kCardStages; i++)
        c.card[i] = i < cards_asked(c.stage) ? cards[i] : -1;
    if (!valid(c))
        return no("a card of every earlier stage is needed (0, 1 or 2)");
    if (creature) {
        bool mine = false;
        for (const auto& o : creations::options(creations::kCreature))
            mine |= o.key.instance == creature;
        if (!mine)
            return no("that creature is not one of your My Creations creatures");
    }
    net::Writer w;
    w.u8(kAnswer);
    write_choice(w, c);
    S->send_reliable_to(0, net::MSG_START_ANY, w);
    g_answers_tx++;
    g_want = creature;
    g_prompt = 0;
    return true;
}

static void guest_tick(double now)
{
    static double next;
    if (!g_want || now < next)
        return;
    next = now + 2;
    if (stage_of(game::mode_id()) < 1 || own_world::elsewhere())
        return;
    if (creations::mine(creations::kCreature).key.instance == g_want)
        return;
    for (const auto& o : creations::options(creations::kCreature))
        if (o.key.instance == g_want) {
            creations::choose(creations::kCreature, o.key); // the party seat follows (party.cpp species_of)
            g_creature_applies++;
            H->log(M, SK_INFO, "start any: our own creature %08X", g_want);
        }
}

void tick(double now)
{
    if (!S || !S->connected()) {
        g_prompt = 0;
        g_here_since.clear();
        g_asked.clear();
        return;
    }
    if (is_host())
        host_tick(now);
    else
        guest_tick(now);
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (is_host() && kind == kAnswer) {
        host_answer(from, r);
        return;
    }
    if (!is_guest() || from != 0)
        return;
    if (kind == kAsk) {
        const uint8_t stage = r.u8();
        if (!r.ok() || stage < 1 || stage > 4 || !enabled())
            return;
        g_rx_asks++;
        g_prompt = stage;
        H->log(M, SK_INFO, "start any: the host asks for our own start at stage %u", stage);
    } else if (kind == kTaken) {
        const uint8_t ok = r.u8();
        if (r.ok() && ok)
            g_taken++;
        else
            g_want = 0;
    }
}

std::string status()
{
    char b[600];
    snprintf(b, sizeof b,
             "start any: fix=%d role=%s stage=%d new_party=%d planet=%s records=%zu\n"
             "  host: asks=%u answers=%u refused=%u applied=%u saves=%u loaded=%u asked=%zu\n"
             "  guest: prompt=%d want=%08X asks_rx=%u answers=%u taken=%u creature_applies=%u mine=%08X\n",
             (int)g_fix, is_host() ? "host" : is_guest() ? "guest" : "none", stage_of(game::mode_id()), (int)party::planet_new(),
             g_planet.empty() ? "-" : g_planet.substr(g_planet.find_last_of("\\/") + 1).c_str(), g_recs.size(), g_asks, g_answers,
             g_refused, g_applied, g_saves, g_loaded, g_asked.size(), g_prompt, g_want, g_rx_asks, g_answers_tx, g_taken,
             g_creature_applies, creations::mine(creations::kCreature).key.instance);
    std::string out = b;
    for (const auto& r : g_recs) {
        snprintf(b, sizeof b, "  record %s: stage=%u creature=%08X cards=%08X %08X %08X %08X\n", r.key.c_str(), r.c.stage,
                 r.c.creature, card_id(r.c, 0), card_id(r.c, 1), card_id(r.c, 2), card_id(r.c, 3));
        out += b;
    }
    return out;
}

// coop.startany [pick <creature hex|keep> <c0> [c1] [c2] [c3] | ask <player> | creatures]
int command(int argc, const char* const* argv, std::string& out)
{
    const std::string a1 = argc > 1 ? argv[1] : "";
    if (a1 == "pick" && argc >= 4) {
        uint32_t creature = strcmp(argv[2], "keep") ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0;
        int8_t cards[4] = {-1, -1, -1, -1};
        for (int i = 0; i < 4 && 3 + i < argc; i++)
            cards[i] = (int8_t)atoi(argv[3 + i]);
        std::string why;
        out = answer(creature, cards, &why) ? "answered\n" : "refused: " + why + "\n";
        return 0;
    }
    if (a1 == "ask" && argc >= 3) { // host (tests): ask that player again (its record decides)
        g_asked.erase((uint8_t)atoi(argv[2]));
        out = "asked again when it has no choice recorded\n";
        return 0;
    }
    if (a1 == "creatures") {
        for (const auto& o : creations::options(creations::kCreature))
            out += "creature " + std::string(o.name) + " " + [&] {
                char b[12];
                snprintf(b, sizeof b, "%08X", o.key.instance);
                return std::string(b);
            }() + "\n";
        return 0;
    }
    out = status();
    return 0;
}

} // namespace start_any
