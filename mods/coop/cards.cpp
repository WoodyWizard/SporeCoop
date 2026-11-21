// Every player's own stage history (cards.h, BUG-9263; BUG-9264 the trait progress credit).
#include "cards.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "fixes.h"
#include "game.h"
#include "identity.h"
#include "lifecycle.h"
#include "modes.h"
#include "own_world.h"
#include "party.h"
#include "sporekit_game.hpp"

namespace cards {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

// BUG-9263: on = each seat has its own consequence cards and Creature trait progress (party file PTY2), a guest's game
// shows and plays its own; off = every game shows the host's (the wsync row "stages"), as 0.7.15.
static bool g_own_fix = true;
COOP_FIX("party.own_cards", "BUG-9263", g_own_fix,
         "party: every player has their own stage history (consequence cards and the powers they give)");
// BUG-9264: on = a guest's kill or social win adds its trait progress to that guest's seat; off = to the host's
// cPlayer (the lethal hit lands as the host avatar's) or to nobody (a social win), as 0.7.15.
static bool g_credit_fix = true;
COOP_FIX("party.trait_credit", "BUG-9264", g_credit_fix,
         "party: a guest's kills and social wins count for that guest's Creature card, not the host's");

// the stage ids cPlayer::GetConsequenceTrait / SetConsequenceTrait take (fnv("CellGame") ...), in field order
static constexpr uint32_t kStage[founders::kCards] = {0xA426730B, 0xAD56080C, 0xF71FA311, 0xBEB528CB, 0x2DB6DAD3};
static constexpr uint32_t kCardsOff = 0x10FC, kSocialOff = 0x1278, kCombatOff = 0x127C;

// the trait ids of each stage (the release cheat setConsequenceTrait's table, docs/re/creations.md#creations-own-cards)
static bool valid_card(int stage, uint32_t v)
{
    static const uint32_t ids[founders::kCards][10] = {
        {0xCFB01B93, 0xA8EC6F99, 0x5ECE4770},             // cell carnivore, herbivore, omnivore
        {0x17E5EF84, 0xFD159D91, 0xB9DE15F2},             // creature aggressive, social, mixed
        {0x0CAC124D, 0x60A78928, 0xC2CA9495},             // tribe aggressive, social, mixed
        {0x0A35D0F5, 0x3360727F, 0x6AEB96A1},             // civ military, economic, religious
        {0x50FEE6BF, 0xB40D2187, 0x2B35F523, 0xB436C0BC, 0x9CE4639C, 0xB7CB9F7A, 0xCF6C2AB4, 0xD9C806E4, 0xA5656E84,
         0xF966D429}, // space bard, ecologist, zealot, diplomat, scientist, trader, shaman, warrior, wanderer, knight
    };
    if (stage < 0 || stage >= (int)founders::kCards || !v)
        return false;
    for (uint32_t id : ids[stage])
        if (id && id == v)
            return true;
    return false;
}

struct Record {
    bool own = false;
    uint32_t cards[founders::kCards] = {};
    float social = 0, combat = 0;
    bool operator==(const Record&) const = default;
};

// ---- host
static uint8_t g_actor = 0xFF;                  // the player whose action the game is paying now (Actor)
static std::map<uint64_t, uint32_t> g_cell_of;  // identity -> the Cell card its game reported last
static std::map<uint8_t, Record> g_sent;         // player -> the record sent last
static std::map<uint8_t, double> g_sent_at;
static double g_host_next;
static uint32_t g_credits[2], g_credit_fails, g_decided[founders::kCards], g_tx, g_cell_rx, g_seeded;
// ---- guest
static bool g_have;
static Record g_mine;
static double g_guest_next, g_cell_next;
static uint32_t g_cell_sent, g_rx, g_writes, g_progress_writes, g_cell_tx;

SESSION_STATE("cards.session", nullptr, [] {
    g_have = false;
    g_mine = {};
    g_cell_sent = 0;
    g_sent.clear();
    g_sent_at.clear();
    g_cell_of.clear();
    g_actor = 0xFF;
}, []() -> size_t { return (g_have ? 1 : 0) + g_sent.size() + g_cell_of.size(); });

using AddTrait_t = void(SK_THISCALL*)(void*, float, bool);
static AddTrait_t o_AddTrait;

static bool is_host() { return S && S->connected() && S->role() == net::Role::Host; }
static bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }

static void* cplayer()
{
    uintptr_t get = H->sym("Simulator::cGameNounManager::Get");
    void* nm = get ? skg::cdecl_call<void*>(get) : nullptr;
    void* p = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? skg::field<void*>(nm, 0x74) : nullptr;
    return p && skg::readable((uint8_t*)p + kCardsOff, 0x184) ? p : nullptr;
}

static bool stage_mode(uint32_t m)
{
    return m == modes::kCell || m == modes::kCreature || m == modes::kTribe || m == modes::kCiv || m == modes::kSpace;
}

static Record read_player(void* p)
{
    Record r;
    r.own = true;
    for (size_t i = 0; i < founders::kCards; ++i)
        r.cards[i] = skg::field<uint32_t>(p, kCardsOff + 4 * i);
    r.social = skg::field<float>(p, kSocialOff);
    r.combat = skg::field<float>(p, kCombatOff);
    if (!std::isfinite(r.social) || !std::isfinite(r.combat))
        r.social = r.combat = 0;
    return r;
}

Actor::Actor(uint8_t player) : prev(g_actor) { g_actor = player; }
Actor::~Actor() { g_actor = prev; }

static Record seat_record(int seat)
{
    Record r;
    const auto& s = party::roster().seats[seat];
    r.own = s.own_cards;
    memcpy(r.cards, s.cards, sizeof r.cards);
    r.social = s.social;
    r.combat = s.combat;
    return r;
}

static bool store(int seat, const Record& r)
{
    return party::set_history(seat, r.own, r.cards, r.social, r.combat);
}

bool credit(uint8_t player, bool social, float amount)
{
    if (!g_own_fix || !g_credit_fix || !is_host() || !std::isfinite(amount) || amount == 0)
        return false;
    const int seat = party::seat_of_player(player);
    if (seat < 1) {
        g_credit_fails++;
        return false;
    }
    Record r = seat_record(seat);
    if (!r.own) { // seated a moment ago: the next tick seeds it; count from the host's cards
        if (void* p = cplayer()) {
            Record h = read_player(p);
            memcpy(r.cards, h.cards, sizeof r.cards);
        }
        r.own = true;
        r.social = r.combat = 0;
    }
    (social ? r.social : r.combat) += amount;
    if (!store(seat, r)) {
        g_credit_fails++;
        return false;
    }
    g_credits[social ? 0 : 1]++;
    return true;
}

// cPlayer::AddTraitProgress (thiscall(cPlayer*, float amount, bool social) ret 8): the game's kill (TakeDamage),
// social win (0xD9BC40) and species goal (SpeciesGoal_Update) add to the player's social / combat progress
static void SK_THISCALL h_AddTrait(void* self, float amount, bool social)
{
    if (g_actor != 0xFF && S && g_actor != S->my_id() && credit(g_actor, social, amount))
        return; // a guest's action: its seat's progress, not the host's
    o_AddTrait(self, amount, social);
}

// the Creature card the game would give for this progress now (Creature_GetConsequenceTrait reads cPlayer +0x1278 /
// +0x127C and the History timeline's Cell value): run with the seat's progress in the host's cPlayer, put back after
static uint32_t creature_card_for(float social, float combat)
{
    void* p = cplayer();
    uintptr_t decide = H->sym("SporeCoop::Creature_GetConsequenceTrait");
    if (!p || !decide)
        return 0;
    float& s = skg::field<float>(p, kSocialOff);
    float& c = skg::field<float>(p, kCombatOff);
    const float s0 = s, c0 = c;
    s = social;
    c = combat;
    uint32_t v = skg::cdecl_call<uint32_t>(decide);
    s = s0;
    c = c0;
    return valid_card(1, v) ? v : 0;
}

static uint32_t space_card_for(const uint32_t (&c)[founders::kCards])
{
    uintptr_t f = H->sym("SporeCoop::SpaceArchetype_FromTraits");
    for (int i = 0; i < 4; ++i)
        if (!valid_card(i, c[i]))
            return 0;
    uint32_t v = f ? skg::cdecl_call<uint32_t>(f, c[0], c[1], c[2], c[3]) : 0;
    return valid_card(4, v) ? v : 0;
}

// the seat's own card for a stage the host's world has done (host = the host's card)
static uint32_t decide(int stage, int seat, const Record& r, uint32_t host)
{
    uint32_t v = 0;
    switch (stage) {
    case 0: {
        auto it = g_cell_of.find(party::roster().seats[seat].player);
        v = it != g_cell_of.end() ? it->second : 0;
        break;
    }
    case 1: v = creature_card_for(r.social, r.combat); break;
    case 4: v = space_card_for(r.cards); break;
    default: break; // Tribe, Civ: one shared tribe / nation in this build, the same play: the host's card
    }
    if (!valid_card(stage, v))
        v = host;
    g_decided[stage]++;
    H->log(M, SK_INFO, "cards: seat %d gets stage %d's card %08X (the host's %08X)", seat, stage, v, host);
    return v;
}

static void send_record(uint8_t to, const Record& r)
{
    net::Writer w;
    w.u8(0);
    w.u8(r.own ? 1 : 0);
    for (uint32_t c : r.cards)
        w.u32(c);
    w.f32(r.social);
    w.f32(r.combat);
    if (S->send_reliable_to(to, net::MSG_OWN_CARDS, w)) {
        g_sent[to] = r;
        g_sent_at[to] = H->time_seconds();
        g_tx++;
    }
}

static void host_tick(double now)
{
    if (now < g_host_next)
        return;
    g_host_next = now + 1;
    const auto& seats = party::roster().seats;
    void* p = cplayer();
    if (seats.empty() || !p || !stage_mode(game::mode_id()))
        return;
    const Record host = read_player(p);
    store(0, host);
    for (int seat = 1; seat < (int)seats.size(); ++seat) {
        Record r = seat_record(seat);
        if (!r.own) { // the history so far was the shared one: its own starts from the host's cards
            memcpy(r.cards, host.cards, sizeof r.cards);
            r.social = r.combat = 0;
            r.own = true;
            g_seeded++;
            H->log(M, SK_INFO, "cards: seat %d starts its own history from the host's cards", seat);
        }
        for (int i = 0; i < (int)founders::kCards; ++i) {
            if (!host.cards[i])
                r.cards[i] = 0; // not done in this world (an earlier save): not done for anybody
            else if (!r.cards[i])
                r.cards[i] = decide(i, seat, r, host.cards[i]);
        }
        store(seat, r);
        const uint8_t player = party::player_of_seat(seat);
        if (player == party::kNobody)
            continue;
        auto it = g_sent.find(player);
        if (it == g_sent.end() || !(it->second == r) || now - g_sent_at[player] > 5)
            send_record(player, r);
    }
}

// ---- guest
static bool guest_here(uint32_t m)
{
    if (!stage_mode(m) || own_world::elsewhere())
        return false;
    return !(m == modes::kSpace && own_world::space_other());
}

static uint32_t own_cell_card()
{
    uintptr_t gp = H->sym("Simulator::Cell::cCellGame::_ptr"), f = H->sym("SporeCoop::Cell_GetConsequenceTrait");
    void* g = gp && skg::readable((void*)gp, 4) ? *(void**)gp : nullptr;
    if (!f || !g || !skg::readable((uint8_t*)g + 0x5190, 4))
        return 0;
    void* data = skg::field<void*>(g, 0x5190); // cCellSerializableData: +0x1C.. the food eaten
    if (!data || !skg::readable(data, 0x30))
        return 0;
    uint32_t v = skg::cdecl_call<uint32_t>(f);
    return valid_card(0, v) ? v : 0;
}

static void guest_tick(double now)
{
    if (now < g_guest_next)
        return;
    g_guest_next = now + 0.5;
    const uint32_t m = game::mode_id();
    if (m == modes::kCell && now >= g_cell_next && guest_here(m)) { // its own diet decides its Cell card
        g_cell_next = now + 2;
        if (uint32_t v = own_cell_card(); v && v != g_cell_sent) {
            net::Writer w;
            w.u8(1);
            w.u32(v);
            if (S->send_reliable_to(0, net::MSG_OWN_CARDS, w)) {
                g_cell_sent = v;
                g_cell_tx++;
            }
        }
    }
    if (!g_have || !g_mine.own || !guest_here(m))
        return;
    void* p = cplayer();
    uintptr_t set = H->sym("SporeCoop::cPlayer_SetConsequenceTrait");
    if (!p || !set)
        return;
    for (size_t i = 0; i < founders::kCards; ++i)
        if (skg::field<uint32_t>(p, kCardsOff + 4 * i) != g_mine.cards[i]) {
            // the game's setter (in Space it also gives the empire the new cards, 0xC75B40)
            skg::thiscall<void>(set, p, kStage[i], g_mine.cards[i]);
            g_writes++;
        }
    float& s = skg::field<float>(p, kSocialOff);
    float& c = skg::field<float>(p, kCombatOff);
    if (std::fabs(s - g_mine.social) > 0.01f || std::fabs(c - g_mine.combat) > 0.01f) {
        s = g_mine.social;
        c = g_mine.combat;
        g_progress_writes++;
    }
}

bool guest_owns() { return g_own_fix && is_guest() && g_have && g_mine.own; }

void tick(double now)
{
    if (!g_own_fix || !S || !S->connected())
        return;
    if (is_host())
        host_tick(now);
    else
        guest_tick(now);
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    if (kind == 0 && from == 0 && S->role() == net::Role::Client) {
        Record v;
        v.own = (r.u8() & 1) != 0;
        for (auto& c : v.cards)
            c = r.u32();
        v.social = r.f32();
        v.combat = r.f32();
        if (!r.ok() || !std::isfinite(v.social) || !std::isfinite(v.combat))
            return;
        for (int i = 0; i < (int)founders::kCards; ++i)
            if (v.cards[i] && !valid_card(i, v.cards[i]))
                return;
        g_mine = v;
        g_have = true;
        g_rx++;
        g_guest_next = 0;
    } else if (kind == 1 && S->role() == net::Role::Host) {
        const uint32_t v = r.u32();
        const uint64_t id = identity::of(from);
        if (r.ok() && id && valid_card(0, v)) {
            g_cell_of[id] = v;
            g_cell_rx++;
        }
    }
}

std::string status()
{
    char b[400];
    std::string out;
    snprintf(b, sizeof b,
             "cards: fix=%d credit_fix=%d role=%s\n"
             "  host: credits social=%u combat=%u fails=%u seeded=%u decided=%u,%u,%u,%u,%u sent=%u cell_reports=%u\n"
             "  guest: have=%d own=%d rx=%u writes=%u progress_writes=%u cell_sent=%08X cell_tx=%u\n"
             "  mine %08X %08X %08X %08X %08X social=%.1f combat=%.1f\n",
             (int)g_own_fix, (int)g_credit_fix, is_host() ? "host" : is_guest() ? "guest" : "none", g_credits[0],
             g_credits[1], g_credit_fails, g_seeded, g_decided[0], g_decided[1], g_decided[2], g_decided[3],
             g_decided[4], g_tx, g_cell_rx, (int)g_have, (int)g_mine.own, g_rx, g_writes, g_progress_writes,
             g_cell_sent, g_cell_tx, g_mine.cards[0], g_mine.cards[1], g_mine.cards[2], g_mine.cards[3],
             g_mine.cards[4], g_mine.social, g_mine.combat);
    out = b;
    if (void* p = cplayer()) {
        Record h = read_player(p);
        snprintf(b, sizeof b, "  cplayer %08X %08X %08X %08X %08X social=%.1f combat=%.1f\n", h.cards[0], h.cards[1],
                 h.cards[2], h.cards[3], h.cards[4], h.social, h.combat);
        out += b;
    }
    for (auto& [id, v] : g_cell_of) {
        snprintf(b, sizeof b, "  cell report %08X: %08X\n", (unsigned)(id >> 32), v);
        out += b;
    }
    return out;
}

int command(int argc, const char* const* argv, std::string& out)
{
    char b[200];
    if (argc >= 5 && !strcmp(argv[1], "credit")) { // tests: as a guest's kill (combat) or social win (social)
        bool ok = credit((uint8_t)atoi(argv[2]), !strcmp(argv[3], "social"), (float)atof(argv[4]));
        out = ok ? "credited\n" : "not credited (no seat / not the host / fix off)\n";
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "decide")) { // host: the Creature card this progress would give now
        snprintf(b, sizeof b, "creature card %08X\n", creature_card_for((float)atof(argv[2]), argc >= 4 ? (float)atof(argv[3]) : 0.f));
        out = b;
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "cell")) { // this game's own Cell card now (Cell only)
        snprintf(b, sizeof b, "own cell card %08X\n", own_cell_card());
        out = b;
        return 0;
    }
    if (argc >= 6 && !strcmp(argv[1], "space")) { // the archetype of four cards
        uint32_t c[founders::kCards] = {};
        for (int i = 0; i < 4; ++i)
            c[i] = (uint32_t)strtoul(argv[2 + i], nullptr, 16);
        snprintf(b, sizeof b, "space card %08X\n", space_card_for(c));
        out = b;
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "seat")) { // host tests: `seat <seat> <stage 0..4> <hex>` one card of a seat
        int seat = atoi(argv[2]), i = atoi(argv[3]);
        if (!is_host() || seat < 0 || seat >= (int)party::roster().seats.size() || i < 0 || i >= (int)founders::kCards) {
            out = "no such seat (host only)\n";
            return 0;
        }
        Record r = seat_record(seat);
        r.own = true;
        r.cards[i] = (uint32_t)strtoul(argv[4], nullptr, 16);
        out = store(seat, r) ? "set\n" : "not set\n";
        return 0;
    }
    out = status();
    return 0;
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    if (uintptr_t a = H->sym("SporeCoop::cPlayer_AddTraitProgress"))
        H->hook_create(M, (void*)a, (void*)h_AddTrait, (void**)&o_AddTrait);
    H->command_add(M, "coop.cards",
                   "coop.cards [credit <player> social|combat <amount> | decide <social> <combat> | cell | space <c1> <c2> "
                   "<c3> <c4> | seat <seat> <stage> <hex>] - every player's own stage history (BUG-9263)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::string s;
                       int rc = command(argc, argv, s);
                       H->out_printf(out, "%s", s.c_str());
                       return rc;
                   },
                   nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace cards
