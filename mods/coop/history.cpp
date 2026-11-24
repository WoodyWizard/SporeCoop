// Every player's own stage history (history.h, history_logic.h; WP-H1 of docs/plan-0.7.17.md, BUG-1470, BUG-1471).
// Ideas and RE from PR #207 (own consequence cards, the AddTraitProgress credit) and PR #217 (the own Space archetype
// tools) by IltonBJSilva, ported on the per-player store and the Actor scope.
#include "history.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "actor_scope.h"
#include "fixes.h"
#include "game.h"
#include "history_logic.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/manifest.h"
#include "own_world.h"
#include "sporekit_game.hpp"
#include "store.h"
#include "wire/history.h"

namespace coop_core {
bool host_creature_mode(); // vote.cpp: creature.mode host (one creature, one history for everybody)
}
using coop_core::host_creature_mode;

namespace history {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

#define LOG(...) H->log(M, SK_INFO, __VA_ARGS__)

// BUG-1470: on = in creature.mode own every guest has its own consequence cards and Creature trait progress (the host
// keeps them in the per-player store, sends each guest its own, the guest's game shows and plays them); off = every
// game shows the host's (the wsync row "stages"), as 0.7.16.1
static bool g_own_fix = true;
COOP_FIX("core.own_history", "BUG-1470",
         g_own_fix, "core: every player has its own stage history (consequence cards and Creature trait progress) in creature.mode own");
// BUG-1471: on = the trait progress of a guest's kill or social win landed on the host's game goes to that guest
// (own history) or, in the shared history, a social win counts at all; off = a guest's kill raises the host's
// progress, its social win nobody's (0.7.16.1)
static bool g_credit_fix = true;
COOP_FIX("core.trait_credit", "BUG-1471", g_credit_fix,
         "creature: a guest's kills and social wins count for that guest's own trait progress, not the host's");
// BUG-1470 (#217 b): on = in Space a guest's empire gets the archetype of its own Space card (its index and tools,
// SpaceGame_SetArchetype once per world); off = the empire keeps the archetype of the host's save
static bool g_arch_fix = true;
COOP_FIX("space.own_archetype", "BUG-1470", g_arch_fix,
         "space: a guest's empire plays the archetype of its own stage history (its own archetype tools)");

// ---- symbols
static uintptr_t a_AddTrait, a_SetCard, a_GetCard, a_CreatureCard, a_CellCard, a_SpaceFromTraits, a_SetArchetype,
    a_SpaceGame, a_PlayerEmpire, a_NounGet, a_CellGame;
using AddTrait_t = void(SK_THISCALL*)(void*, float, bool);
static AddTrait_t o_AddTrait;

// ---- host
static Book g_book;
static uint32_t g_book_serial;
static std::map<uint8_t, wire::history::Record> g_sent; // player -> the record sent last
static std::map<uint8_t, double> g_sent_at;
static double g_host_next;
static uint32_t g_hook_calls, g_redirected[2], g_redirect_fails, g_seeded, g_decided[kStages], g_tx, g_cell_rx,
    g_released, g_social_wins, g_loaded;
// ---- guest
static bool g_have;
static wire::history::Record g_mine;
static double g_guest_next, g_cell_next, g_space_since;
static uint32_t g_cell_sent, g_rx, g_card_writes, g_progress_writes, g_cell_tx, g_local_calls, g_arch_calls;
static uint32_t g_arch_applied; // the Space card whose archetype this world got (0: none yet)

SESSION_STATE("history.session", nullptr, [] {
    g_have = false;
    g_mine = {};
    g_cell_sent = 0;
    g_sent.clear();
    g_sent_at.clear();
}, []() -> size_t { return (g_have ? 1 : 0) + g_sent.size(); });
WORLD_STATE("history.archetype", nullptr, [] {
    g_arch_applied = 0;
    g_space_since = 0;
}, []() -> size_t { return g_arch_applied ? 1 : 0; });

static bool is_host() { return S && S->role() == net::Role::Host; }
static bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }
static bool own_on() { return active(g_own_fix, host_creature_mode()); }

static void* cplayer()
{
    void* nm = a_NounGet ? skg::cdecl_call<void*>(a_NounGet) : nullptr;
    void* p = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? skg::field<void*>(nm, 0x74) : nullptr;
    return p && skg::readable(p, 0x1280) ? p : nullptr;
}
static bool stage_mode(uint32_t m)
{
    return m == modes::kCell || m == modes::kCreature || m == modes::kTribe || m == modes::kCiv || m == modes::kSpace;
}
static void read_cards(void* p, uint32_t (&c)[kStages])
{
    for (int i = 0; i < kStages; i++)
        c[i] = skg::field<uint32_t>(p, kCardsOff + 4 * i);
}

// the peer's mod reads MSG_PLAYER_HISTORY (session 1.3): the host asks peer_families, a guest host_families
static bool reads(bool has, const std::vector<net::FamilyRev>& v)
{
    const net::FamilyRev* t = nullptr;
    for (auto& x : v)
        if (x.family == (uint8_t)net::Family::session)
            t = &x;
    const net::FamilyInfo* mine = net::family_info((uint8_t)net::Family::session);
    return peer_reads(has, t != nullptr, t ? t->rev : 0, t ? t->minor : 0, mine ? mine->rev : 0);
}
static bool peer_reads_history(uint8_t id)
{
    bool has = false;
    std::vector<net::FamilyRev> v;
    return S && S->peer_families(id, has, v) && reads(has, v);
}
static bool host_reads_history()
{
    bool has = false;
    std::vector<net::FamilyRev> v;
    return S && S->host_families(has, v) && reads(has, v);
}
static std::string key_of(uint8_t id)
{
    std::string k = store::key_of(id);
    return k.empty() ? "id:" + std::to_string(id) : k; // no stable key: memory only
}

// ---- host: the store (record `history` v1 per player), read when the host's world opens
static void book_load()
{
    pstore::Store* s = store::world();
    uint32_t ser = store::serial();
    if (!s || ser == g_book_serial)
        return;
    g_book_serial = ser;
    int n = g_book.load(*s, store::world_name());
    g_loaded += (uint32_t)n;
    if (n)
        LOG("history: the own stage history of %d players read from the store of %s", n, store::world_name().c_str());
}
static void book_save(pstore::Store& s, int64_t now)
{
    if (!g_own_fix || !is_host())
        return;
    for (auto& [k, r] : g_book.recs) {
        if (k.rfind("s:", 0) != 0)
            continue;
        std::string name;
        for (auto& p : S->players())
            if (p.id != 0 && store::key_of(p.id) == k)
                name = p.name;
        s.put(k, name, kStoreFeature, kStoreVersion, encode(r), now);
    }
}
static bool g_store_reg = store::add({kStoreFeature, book_save,
                                      [](const std::string& from, const std::string& to) { g_book.adopt(from, to); }});

// host: the record of a present guest, made on first sight from the host's cPlayer (`p`)
static Record* record_of(uint8_t id, void* p)
{
    std::string k = key_of(id);
    auto it = g_book.recs.find(k);
    if (it != g_book.recs.end())
        return &it->second;
    if (!p)
        return nullptr;
    uint32_t c[kStages];
    read_cards(p, c);
    Record r = seed(c, skg::field<float>(p, kSocialOff), skg::field<float>(p, kCombatOff));
    g_seeded++;
    LOG("history: %s starts its own stage history from the host's: cards %08X %08X %08X %08X %08X social=%.1f "
        "combat=%.1f", S->name_of(id).c_str(), r.cards[0], r.cards[1], r.cards[2], r.cards[3], r.cards[4], r.social,
        r.combat);
    return &(g_book.recs[k] = r);
}

// the trait progress of guest `who`'s act goes to its own record (own history, its mod reads it)
static bool redirect(uint8_t who, void* self, bool social, float amount)
{
    if (!is_host() || who == S->my_id() || !own_on() || !peer_reads_history(who))
        return false;
    Record* r = record_of(who, self);
    if (!r || !credit(*r, social, amount)) {
        g_redirect_fails++;
        return false;
    }
    g_redirected[social ? 0 : 1]++;
    LOG("history: %s's %s +%.2f goes to its own progress (social=%.1f combat=%.1f; the host's stays)",
        S->name_of(who).c_str(), social ? "social win" : "kill", amount, r->social, r->combat);
    return true;
}

// cPlayer::AddTraitProgress (thiscall(cPlayer*, float amount, bool social) ret 8): the kill in TakeDamage 0xC07C0C,
// the species goals 0xC2EFE9 / 0xC2F0F1, the social win 0xD9BD36
static void SK_THISCALL h_AddTrait(void* self, float amount, bool social)
{
    g_hook_calls++;
    if (is_guest())
        g_local_calls++; // (diagnostics: the guest's game adds its own; the host's record overwrites it)
    const uint8_t who = actor::current();
    if (who != actor::kNone && g_credit_fix && redirect(who, self, social, amount))
        return; // a guest's act: its own progress, not the host's
    o_AddTrait(self, amount, social);
}

void social_win(uint8_t player, float worth)
{
    if (!g_credit_fix || !a_AddTrait || !is_host() || !(worth > 0) || worth >= 10000)
        return;
    void* p = cplayer();
    if (!p)
        return;
    actor::Scope scope(player);
    skg::thiscall<void>(a_AddTrait, p, worth, true); // through the hook: the guest's own (own history) or the party's
    g_social_wins++;
}

// the Creature card the game's rule gives this progress now (Creature_GetConsequenceTrait 0xD38780 reads cPlayer
// +0x1278 / +0x127C and the History timeline's Cell value): run with it lent into the host's cPlayer (the swap
// ledger undoes it should the call not return, BUG-0110), put back right after
static uint32_t creature_card_for(float social, float combat)
{
    void* p = cplayer();
    if (!p || !a_CreatureCard || !sane(social) || !sane(combat))
        return 0;
    uint32_t& s = skg::field<uint32_t>(p, kSocialOff);
    uint32_t& c = skg::field<uint32_t>(p, kCombatOff);
    const uint32_t s0 = s, c0 = c;
    size_t mark = game::swap_open(game::swap_undo_u32, &s, s0, "history: trait progress (social)");
    game::swap_open(game::swap_undo_u32, &c, c0, "history: trait progress (combat)");
    memcpy(&s, &social, 4);
    memcpy(&c, &combat, 4);
    uint32_t v = skg::cdecl_call<uint32_t>(a_CreatureCard);
    s = s0;
    c = c0;
    game::swap_close(mark, 2);
    return valid_card(kCreature, v) ? v : 0;
}
// the Space archetype of four cards (SpaceArchetype_FromTraits 0x556440, cdecl, 4 args)
static uint32_t space_card_for(const uint32_t* c)
{
    for (int i = 0; i < 4; i++)
        if (!valid_card(i, c[i]))
            return 0;
    uint32_t v = a_SpaceFromTraits ? skg::cdecl_call<uint32_t>(a_SpaceFromTraits, c[0], c[1], c[2], c[3]) : 0;
    return valid_card(kSpace, v) ? v : 0;
}

static void send_record(uint8_t to, const wire::history::Record& rec, double now)
{
    net::Writer w;
    w.u8(wire::history::K_RECORD);
    rec.write(w);
    if (S->send_reliable_to(to, net::MSG_PLAYER_HISTORY, w)) {
        g_sent[to] = rec;
        g_sent_at[to] = now;
        g_tx++;
    }
}
static bool same(const wire::history::Record& a, const wire::history::Record& b)
{
    return a.flags == b.flags && !memcmp(a.cards, b.cards, sizeof a.cards) && a.social == b.social && a.combat == b.combat;
}

static void host_tick(double now)
{
    book_load();
    if (now < g_host_next)
        return;
    g_host_next = now + 0.5;
    const bool on = own_on();
    void* p = cplayer();
    const bool stage = stage_mode(game::mode_id()) && p;
    uint32_t host[kStages] = {};
    if (stage)
        read_cards(p, host);
    for (auto& pl : S->players()) {
        if (pl.id == S->my_id())
            continue;
        auto sent = g_sent.find(pl.id);
        if (!on || !peer_reads_history(pl.id)) { // the shared history (again): tell a guest that had its own
            if (sent != g_sent.end() && (sent->second.flags & wire::history::kFlagOwn)) {
                send_record(pl.id, {}, now);
                g_released++;
                LOG("history: %s plays the shared stage history again", pl.name.c_str());
            }
            continue;
        }
        if (!stage)
            continue; // an editor, a load: the host's cards are not readable now
        Record* r = record_of(pl.id, p);
        if (!r)
            continue;
        unsigned ch = update(*r, host, [&](int stage, const Record& x) -> uint32_t {
            uint32_t v = stage == kCell ? x.cell_own : stage == kCreature ? creature_card_for(x.social, x.combat)
                       : stage == kSpace ? space_card_for(x.cards) : 0;
            g_decided[stage]++;
            LOG("history: %s's own card of stage %d: %08X (the host's %08X)", pl.name.c_str(), stage, v, host[stage]);
            return v;
        });
        if (ch)
            LOG("history: %s's cards now %08X %08X %08X %08X %08X (changed %X)", pl.name.c_str(), r->cards[0], r->cards[1],
                r->cards[2], r->cards[3], r->cards[4], ch);
        wire::history::Record rec;
        rec.flags = wire::history::kFlagOwn;
        memcpy(rec.cards, r->cards, sizeof rec.cards);
        rec.social = r->social;
        rec.combat = r->combat;
        if (sent == g_sent.end() || !same(sent->second, rec) || now - g_sent_at[pl.id] > 5)
            send_record(pl.id, rec, now);
    }
}

// ---- guest
static bool guest_here(uint32_t m)
{
    if (!stage_mode(m) || own_world::elsewhere())
        return false;
    return !(m == modes::kSpace && own_world::space_other());
}

bool guest_owns()
{
    return g_own_fix && is_guest() && g_have && (g_mine.flags & wire::history::kFlagOwn) && !host_creature_mode();
}

// this game's own Cell card now (Cell_GetConsequenceTrait 0xE5C9D0 reads cCellGame +0x5190, its own diet)
static uint32_t own_cell_card()
{
    if (!a_CellCard || !a_CellGame || game::mode_id() != modes::kCell || !skg::readable((void*)a_CellGame, 4))
        return 0;
    void* g = *(void**)a_CellGame;
    void* data = g && skg::readable((uint8_t*)g + 0x5190, 4) ? skg::field<void*>(g, 0x5190) : nullptr;
    if (!data || !skg::readable(data, 0x30))
        return 0;
    uint32_t v = skg::cdecl_call<uint32_t>(a_CellCard);
    return valid_card(kCell, v) ? v : 0;
}

static void* player_empire() { return a_PlayerEmpire ? skg::cdecl_call<void*>(a_PlayerEmpire) : nullptr; }
static int empire_archetype()
{
    void* e = player_empire();
    return e && skg::readable((uint8_t*)e + kArchetypeOff, 4) ? skg::field<int>(e, kArchetypeOff) : -1;
}

static void guest_space(void* p, double now)
{
    if (!g_space_since)
        g_space_since = now;
    if (!g_arch_fix || !a_SetArchetype || !a_SpaceGame || now - g_space_since < 5)
        return; // the world's own Space start first
    const uint32_t own = g_mine.cards[kSpace];
    const int idx = empire_archetype();
    if (idx < 0 || !archetype_due(own, skg::field<uint32_t>(p, kCardsOff + 4 * kSpace), idx, g_arch_applied))
        return;
    void* game = skg::cdecl_call<void*>(a_SpaceGame);
    if (!game)
        return;
    g_arch_applied = own; // once per world and card, whatever the call does
    skg::thiscall<void>(a_SetArchetype, game, false, false); // the cPlayer's Space card: index +0x58 and its tools
    g_arch_calls++;
    LOG("history: our empire takes its own archetype %08X (index %d -> %d)", own, idx, empire_archetype());
}

static void guest_tick(double now)
{
    if (now < g_guest_next)
        return;
    g_guest_next = now + 0.5;
    const uint32_t m = game::mode_id();
    if (m != modes::kSpace)
        g_space_since = 0;
    if (m == modes::kCell && now >= g_cell_next && guest_here(m) && own_on() && host_reads_history()) {
        g_cell_next = now + 2;
        if (uint32_t v = own_cell_card(); v && v != g_cell_sent) { // its own diet decides its Cell card
            net::Writer w;
            w.u8(wire::history::K_CELL);
            wire::history::Cell{v}.write(w);
            if (S->send_reliable_to(0, net::MSG_PLAYER_HISTORY, w)) {
                g_cell_sent = v;
                g_cell_tx++;
                LOG("history: our own Cell card now %08X (told the host)", v);
            }
        }
    }
    if (!guest_owns() || !guest_here(m))
        return;
    void* p = cplayer();
    if (!p || !a_SetCard)
        return;
    uint32_t mine[kStages];
    read_cards(p, mine);
    Record r;
    memcpy(r.cards, g_mine.cards, sizeof r.cards);
    r.social = g_mine.social;
    r.combat = g_mine.combat;
    if (unsigned d = card_diff(mine, r)) {
        for (int i = 0; i < kStages; i++)
            if (d & (1u << i)) // the game's setter (in Space it also gives the empire the new cards, 0xC75B40)
                skg::thiscall<void>(a_SetCard, p, kStageId[i], r.cards[i]);
        g_card_writes++;
        LOG("history: our own cards %08X %08X %08X %08X %08X written (were %08X %08X %08X %08X %08X)", r.cards[0],
            r.cards[1], r.cards[2], r.cards[3], r.cards[4], mine[0], mine[1], mine[2], mine[3], mine[4]);
    }
    float& s = skg::field<float>(p, kSocialOff);
    float& c = skg::field<float>(p, kCombatOff);
    if (progress_differs(s, c, r)) {
        s = r.social;
        c = r.combat;
        g_progress_writes++;
    }
    if (m == modes::kSpace)
        guest_space(p, now);
}

void tick(double now)
{
    if (!S || !a_NounGet)
        return;
    if (is_host())
        host_tick(now);
    else if (is_guest() && g_own_fix)
        guest_tick(now);
}

void on_message(uint8_t from, net::Reader& r)
{
    if (!S || !g_own_fix)
        return;
    const uint8_t kind = r.u8();
    if (kind == wire::history::K_RECORD && from == 0 && S->role() == net::Role::Client) {
        wire::history::Record v;
        if (!v.read(r) || !sane(v.social) || !sane(v.combat))
            return;
        for (int i = 0; i < kStages; i++)
            if (!card_ok(i, v.cards[i]))
                return;
        bool was = g_have && (g_mine.flags & wire::history::kFlagOwn);
        g_mine = v;
        g_have = true;
        g_rx++;
        g_guest_next = 0;
        if (was != ((v.flags & wire::history::kFlagOwn) != 0))
            LOG("history: the host says we play %s stage history", v.flags & wire::history::kFlagOwn ? "our own" : "the shared");
    } else if (kind == wire::history::K_CELL && is_host() && from != 0) {
        wire::history::Cell c;
        if (!c.read(r) || !valid_card(kCell, c.card) || !own_on() || own_world::peer_elsewhere(from))
            return;
        if (Record* x = record_of(from, cplayer())) {
            x->cell_own = c.card;
            g_cell_rx++;
        }
    }
}

// ---- coop.history
static std::string status()
{
    char b[512];
    std::string out;
    snprintf(b, sizeof b,
             "history: own_fix=%d credit_fix=%d arch_fix=%d role=%s active=%d\n"
             "  host: hook calls=%u redirected social=%u combat=%u fails=%u social_wins=%u seeded=%u loaded=%u "
             "decided=%u,%u,%u,%u,%u sent=%u released=%u cell_reports=%u records=%u world=%s\n"
             "  guest: have=%d own=%d owns=%d rx=%u card_writes=%u progress_writes=%u local_calls=%u cell_sent=%08X "
             "cell_tx=%u arch_calls=%u arch_applied=%08X\n"
             "  mine %08X %08X %08X %08X %08X social=%.1f combat=%.1f\n",
             (int)g_own_fix, (int)g_credit_fix, (int)g_arch_fix, is_host() ? "host" : is_guest() ? "guest" : "none",
             (int)own_on(), g_hook_calls, g_redirected[0], g_redirected[1], g_redirect_fails, g_social_wins, g_seeded,
             g_loaded, g_decided[0], g_decided[1], g_decided[2], g_decided[3], g_decided[4], g_tx, g_released, g_cell_rx,
             (unsigned)g_book.recs.size(), g_book.world.empty() ? "-" : g_book.world.c_str(), (int)g_have,
             (int)(g_mine.flags & 1), (int)guest_owns(), g_rx, g_card_writes, g_progress_writes, g_local_calls,
             g_cell_sent, g_cell_tx, g_arch_calls, g_arch_applied, g_mine.cards[0], g_mine.cards[1], g_mine.cards[2],
             g_mine.cards[3], g_mine.cards[4], g_mine.social, g_mine.combat);
    out = b;
    if (void* p = cplayer()) {
        // the game's own getter (cPlayer::GetConsequenceTrait 0xC75880), what the game's History and powers read
        uint32_t g[kStages] = {};
        for (int i = 0; i < kStages && a_GetCard; i++)
            g[i] = skg::thiscall<uint32_t>(a_GetCard, p, kStageId[i]);
        snprintf(b, sizeof b, "  game cards %08X %08X %08X %08X %08X social=%.2f combat=%.2f\n", g[0], g[1], g[2], g[3],
                 g[4], skg::field<float>(p, kSocialOff), skg::field<float>(p, kCombatOff));
        out += b;
    }
    if (game::mode_id() == modes::kSpace) {
        snprintf(b, sizeof b, "  empire archetype=%d\n", empire_archetype());
        out += b;
    }
    for (auto& [k, r] : g_book.recs) {
        snprintf(b, sizeof b, "  record %s %08X %08X %08X %08X %08X social=%.1f combat=%.1f cell_own=%08X\n", k.c_str(),
                 r.cards[0], r.cards[1], r.cards[2], r.cards[3], r.cards[4], r.social, r.combat, r.cell_own);
        out += b;
    }
    return out;
}

static int command(int argc, const char* const* argv, std::string& out)
{
    char b[200];
    const char* a = argc > 1 ? argv[1] : "";
    // tests: `credit <player> social|combat <amount>` as the game credits a kill / social win of that player on this
    // (host) game: the game's AddTraitProgress inside an actor::Scope naming it (player 0: the host's own)
    if (!strcmp(a, "credit") && argc >= 5) {
        void* p = cplayer();
        if (!p || !a_AddTrait || !is_host()) {
            out = "not credited (host only, in a stage)\n";
            return 0;
        }
        uint8_t who = (uint8_t)atoi(argv[2]);
        bool social = !strcmp(argv[3], "social");
        float amount = (float)atof(argv[4]);
        uint32_t before = g_redirected[0] + g_redirected[1];
        if (who == S->my_id()) {
            skg::thiscall<void>(a_AddTrait, p, amount, social);
        } else {
            actor::Scope scope(who);
            skg::thiscall<void>(a_AddTrait, p, amount, social);
        }
        out = g_redirected[0] + g_redirected[1] != before ? "credited to the player's own progress\n"
                                                           : "credited through the game (the host's cPlayer)\n";
        return 0;
    }
    // tests (host): `end creature`: the Creature stage's end as the game writes it (0xD2C7D3): the host's Creature card
    // by the game's own rule on its own progress
    if (!strcmp(a, "end") && argc >= 3 && !strcmp(argv[2], "creature")) {
        void* p = cplayer();
        if (!p || !a_CreatureCard || !a_SetCard || !is_host() || game::mode_id() != modes::kCreature) {
            out = "not ended (host only, in Creature)\n";
            return 0;
        }
        uint32_t v = skg::cdecl_call<uint32_t>(a_CreatureCard);
        skg::thiscall<void>(a_SetCard, p, kStageId[kCreature], v);
        snprintf(b, sizeof b, "the host's Creature card %08X\n", v);
        out = b;
        return 0;
    }
    // tests (host): `set <player> <stage 0..4> <hex>` one card of a player's record (0: host's cPlayer card; guest: 0 = decided again)
    if (!strcmp(a, "set") && argc >= 5) {
        uint8_t who = (uint8_t)atoi(argv[2]);
        int i = atoi(argv[3]);
        uint32_t v = (uint32_t)strtoul(argv[4], nullptr, 16);
        if (who == 0) {
            void* p = cplayer();
            if (!is_host() || !p || !a_SetCard || i < 0 || i >= kStages || !card_ok(i, v)) {
                out = "not set (host only, in a stage, a card of the stage)\n";
                return 0;
            }
            skg::thiscall<void>(a_SetCard, p, kStageId[i], v);
            out = "set\n";
            return 0;
        }
        Record* r = is_host() && i >= 0 && i < kStages && card_ok(i, v) ? record_of(who, cplayer()) : nullptr;
        if (!r) {
            out = "not set (host only, a present guest, a card of the stage)\n";
            return 0;
        }
        r->cards[i] = v;
        if (i < kSpace)
            r->cards[kSpace] = 0; // the archetype follows the four cards: decided again
        out = "set\n";
        return 0;
    }
    if (!strcmp(a, "space") && argc >= 6) { // the archetype of four cards (the game's rule)
        uint32_t c[4];
        for (int i = 0; i < 4; i++)
            c[i] = (uint32_t)strtoul(argv[2 + i], nullptr, 16);
        snprintf(b, sizeof b, "space card %08X index %d\n", space_card_for(c), archetype_index(space_card_for(c)));
        out = b;
        return 0;
    }
    if (!strcmp(a, "decide") && argc >= 4) { // host: the Creature card this progress would give now
        snprintf(b, sizeof b, "creature card %08X\n", creature_card_for((float)atof(argv[2]), (float)atof(argv[3])));
        out = b;
        return 0;
    }
    if (!strcmp(a, "cell")) { // this game's own Cell card now (Cell only)
        snprintf(b, sizeof b, "own cell card %08X\n", own_cell_card());
        out = b;
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
    a_NounGet = H->sym("Simulator::cGameNounManager::Get");
    a_AddTrait = H->sym("SporeCoop::cPlayer_AddTraitProgress");
    a_SetCard = H->sym("SporeCoop::cPlayer_SetConsequenceTrait");
    a_GetCard = H->sym("SporeCoop::cPlayer_GetConsequenceTrait");
    a_CreatureCard = H->sym("SporeCoop::Creature_GetConsequenceTrait");
    a_CellCard = H->sym("SporeCoop::Cell_GetConsequenceTrait");
    a_SpaceFromTraits = H->sym("SporeCoop::SpaceArchetype_FromTraits");
    a_SetArchetype = H->sym("SporeCoop::SpaceGame_SetArchetype");
    a_SpaceGame = H->sym("Simulator::cSimulatorSpaceGame::Get");
    a_PlayerEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_CellGame = H->sym("Simulator::Cell::cCellGame::_ptr");
    if (a_AddTrait)
        H->hook_create(M, (void*)a_AddTrait, (void*)h_AddTrait, (void**)&o_AddTrait);
    H->command_add(M, "coop.history",
                   "coop.history [credit <player> social|combat <amount> | end creature | set <player> <stage> <hex> | "
                   "space <c1> <c2> <c3> <c4> | decide <social> <combat> | cell] - every player's own stage history "
                   "(BUG-1470)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::string s;
                       int rc = command(argc, argv, s);
                       H->out_printf(out, "%s", s.c_str());
                       return rc;
                   },
                   nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace history
