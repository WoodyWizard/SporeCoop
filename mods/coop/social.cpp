#include "social.h"
#include "fixes.h"
#include "creations.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "game.h"
#include "net/validate.h"
#include "npc.h"
#include "sporekit_game.hpp"

using namespace skg;

namespace social {

static const SkHost* H;
static SkMod* M;
static Ctx C;

static uintptr_t a_AbilityMode;
static uintptr_t a_RelMgrGet, a_RelGet, a_RelEvent, a_RelRecord, a_SocialMode, a_DnaWorth, a_PlayAbility,
    a_PlayReaction;

static constexpr uint32_t kSocialStimulus = 0x10000; // the avatar engages an NPC socially
static constexpr float kActRange = 12.f;             // m between the guest and the NPC for an action
static constexpr double kEngageTime = 60.0;          // an engagement without actions ends after this

enum Kind : uint8_t { K_ACT = 0, K_PROGRESS = 1, K_RESULT = 2, K_GOAL = 3 };

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

// ------------------------------------------------------------------ species relationship table
struct SpeciesRec {
    uint32_t key[3];
    int32_t relation;
    uint32_t x, impressed, killed;
    bool operator==(const SpeciesRec& o) const
    {
        return !memcmp(key, o.key, sizeof key) && relation == o.relation && x == o.x && impressed == o.impressed &&
               killed == o.killed;
    }
};

static void* rel_mgr() { return a_RelMgrGet ? cdecl_call<void*>(a_RelMgrGet) : nullptr; }

// the table: hash map at manager +0x20 (buckets +4, count +8); node {ResourceKey, record {relation,
// ?, impressed, killed, -1}, ..., next at +0x20} (docs/re/creature.md 8)
static std::vector<SpeciesRec> read_table()
{
    std::vector<SpeciesRec> v;
    void* mgr = rel_mgr();
    if (!mgr)
        return v;
    uint8_t* map = (uint8_t*)mgr + 0x20;
    void** buckets = field<void**>(map, 4);
    uint32_t n = field<uint32_t>(map, 8);
    for (uint32_t i = 0; buckets && i < n && i < 4096; i++)
        for (uint32_t* nd = (uint32_t*)buckets[i]; nd && v.size() < 512; nd = (uint32_t*)nd[8])
            v.push_back({{nd[0], nd[1], nd[2]}, (int32_t)nd[3], nd[4], nd[5], nd[6]});
    std::sort(v.begin(), v.end(), [](const SpeciesRec& a, const SpeciesRec& b) {
        return memcmp(a.key, b.key, sizeof a.key) < 0;
    });
    return v;
}

static std::vector<SpeciesRec> g_sent;   // host: the table last sent
static double g_table_sent = 0;
static std::vector<SpeciesRec> g_host_table; // guest: the host's table
static bool g_have_table = false;
static uint32_t g_table_rx = 0, g_table_fixed = 0, g_table_tx = 0;

static void send_table(const std::vector<SpeciesRec>& t, int to)
{
    net::Writer w;
    w.u16((uint16_t)t.size());
    for (auto& r : t) {
        w.u32(r.key[0]); w.u32(r.key[1]); w.u32(r.key[2]);
        w.u32((uint32_t)r.relation); w.u32(r.x); w.u32(r.impressed); w.u32(r.killed);
    }
    if (to < 0)
        C.sess->broadcast_reliable(net::MSG_SPECIES, w);
    else
        C.sess->send_reliable_to((uint8_t)to, net::MSG_SPECIES, w);
    g_table_tx++;
}

// guest: the host's values win. The guest's own game changes the table too (its NPCs are removed
// and come back as mirrors; a removed nest made "The Kook species is extinct!" on the guest's goal
// card, play_creature.py 2026-10-23)
// the goal cards of a species ("Impress 3 Kook to ally") are game objects (noun 0x1406A572) that
// re-read the table only when a relationship message reaches them: a written table needs a nudge
static constexpr uint32_t kSpeciesGoalNoun = 0x1406A572;
static uintptr_t a_GoalUI, a_GoalRefresh;
static uint32_t g_cards_refreshed = 0;
static void refresh_cards(void*) // outside the frame: card texts are UI
{
    if (!game::in_creature_stage())
        return;
    if (a_GoalRefresh)
        game::for_each_object(
            [](void* o, uint32_t noun, void*) {
                if (noun == kSpeciesGoalNoun && game::object_alive(o)) {
                    thiscall<void>(a_GoalRefresh, o);
                    g_cards_refreshed++;
                }
                return true;
            },
            nullptr);
    if (a_GoalUI)
        if (void* ui = cdecl_call<void*>(a_GoalUI))
            vcall<void>(ui, 0x28 / 4, 0);
}

static void enforce_table()
{
    if (!g_have_table || !a_RelRecord)
        return;
    void* mgr = rel_mgr();
    if (!mgr)
        return;
    bool changed = false;
    for (auto& r : g_host_table) {
        ResourceKey k{r.key[0], r.key[1], r.key[2]};
        uint32_t* rec = thiscall<uint32_t*>(a_RelRecord, (uint8_t*)mgr + 0x20, &k);
        if (!rec)
            continue;
        if ((int32_t)rec[0] != r.relation || rec[1] != r.x || rec[2] != r.impressed || rec[3] != r.killed) {
            rec[0] = (uint32_t)r.relation;
            rec[1] = r.x;
            rec[2] = r.impressed;
            rec[3] = r.killed;
            g_table_fixed++;
            changed = true;
        }
    }
    // a record the host does not have (made by the guest's own game: seen with relation 3 after a
    // 10-minute session) goes back to "not set" (relation 0 = the archetype's default)
    for (auto& r : read_table()) {
        if (!r.relation && !r.x && !r.impressed && !r.killed)
            continue;
        bool host_has = false;
        for (auto& h : g_host_table)
            host_has |= !memcmp(h.key, r.key, sizeof r.key);
        if (host_has)
            continue;
        ResourceKey k{r.key[0], r.key[1], r.key[2]};
        if (uint32_t* rec = thiscall<uint32_t*>(a_RelRecord, (uint8_t*)mgr + 0x20, &k)) {
            rec[0] = rec[1] = rec[2] = rec[3] = 0;
            g_table_fixed++;
            changed = true;
        }
    }
    if (changed)
        H->run_outside_frame(refresh_cards, nullptr);
}

// ------------------------------------------------------------------ guest: actions on mirrors
static int g_engaged = -1;            // proxy id the avatar engaged socially
static double g_engaged_at = 0;
static bool g_keys[4];
static uint32_t g_engages = 0, g_acts_sent = 0, g_results_in = 0;

typedef void*(SK_THISCALL* Post_t)(void*, uint32_t, uint32_t, float, void*);
static Post_t o_Post;
static bool g_log_posts = false;
static uint32_t g_posts_logged = 0;

// BT_PostStimulus 0xBC9940: the avatar posts the social stimulus 0x10000 to the NPC it engages in
// the social stance (a click on it). A mirror has no AI to answer: remember it for the actions.
static void* SK_THISCALL h_Post(void* stimuli, uint32_t lo, uint32_t hi, float value, void* sender)
{
    void* av = game::avatar();
    if (av && sender == av) {
        void* ai = (uint8_t*)stimuli - 8; // the AI block (creature +0xB4C) + 8
        if (g_log_posts && g_posts_logged < 400) {
            g_posts_logged++;
            H->log(M, SK_INFO, "social: avatar posts %08X/%08X v=%.2f to ai %p", lo, hi, value, ai);
        }
        if ((lo & kSocialStimulus) && is_guest()) {
            int id = npc::proxy_id_of_ai(ai);
            if (id >= 0) {
                if (id != g_engaged)
                    g_engages++;
                g_engaged = id;
                g_engaged_at = H->time_seconds();
            }
        }
    }
    return o_Post(stimuli, lo, hi, value, sender);
}

// ApplyEvent 0xBA49E0 on the guest: its own game counted every proxy death replayed by npc.cpp
// (TakeDamage: event 2 "attacked", then 5/0 "killed") as a kill of the species, three of them made
// the species "extinct" (0xBA4730: the "species is extinct" goal card, the species' herds stopped,
// play_creature.py 2026-10-23). The host's table is the truth for mirrored NPCs (MSG_SPECIES).
typedef void(SK_THISCALL* RelEvent_t)(void*, int, void*, void*);
static RelEvent_t o_RelEvent;
static uint32_t g_events_dropped = 0;

static void SK_THISCALL h_RelEvent(void* mgr, int event, void* key, void* creature)
{
    if (creature && C.sess && C.sess->role() == net::Role::Client &&
        (npc::proxy_id(creature) >= 0 || game::is_puppet(creature))) {
        g_events_dropped++;
        return;
    }
    // BUG-0021 diagnostics: every species relationship event of the host's game (the Kooks' impressed count
    // went from 2 back to 1 between two impressions with no Kook killed: coop_creature_social_ally.py)
    uint32_t* rec = (a_RelRecord && key && mgr) ? thiscall<uint32_t*>(a_RelRecord, (uint8_t*)mgr + 0x20, key) : nullptr;
    int32_t r0 = rec ? (int32_t)rec[0] : -99;
    uint32_t i0 = rec ? rec[2] : 0, k0 = rec ? rec[3] : 0;
    o_RelEvent(mgr, event, key, creature);
    rec = (a_RelRecord && key && mgr) ? thiscall<uint32_t*>(a_RelRecord, (uint8_t*)mgr + 0x20, key) : nullptr;
    if (rec && ((int32_t)rec[0] != r0 || rec[2] != i0 || rec[3] != k0))
        H->log(M, SK_INFO, "social: relationship event %d species %08X by %p (npc %d%s): relation %d -> %d, impressed %u -> %u, "
               "killed %u -> %u", event, *(uint32_t*)key, creature, creature ? npc::host_id(creature) : -1,
               creature && creature == game::avatar() ? ", the avatar" : "", r0, (int32_t)rec[0], i0, rec[2], k0, rec[3]);
}

// SpeciesGoal_Update 0xC2EE60 pays the card's DNA (allied: 25 for the Kook, extinct: its hunt
// reward) when it sees the species become allied / extinct. A card exists on the machine whose
// player is near the species, so the host's, the guest's or both pay it: each machine reports it,
// the host pays it into the shared pool once per species and outcome (MSG_SOCIAL K_GOAL).
typedef void(SK_THISCALL* GoalUpdate_t)(void*, uint32_t*, uint32_t*);
static GoalUpdate_t o_GoalUpdate;
static void* g_goal = nullptr; // the card being updated
static uint32_t g_goal_dna_dropped = 0, g_goal_dna_paid = 0;
// BUG-0205: a guest's goal reward (K_GOAL) that reaches the host while it has no creature (an editor,
// a load) waits here and is checked and paid when the host's creature is back. false: dropped (tests)
bool g_goal_owe_fix = true;
COOP_FIX("core.away_goal", "BUG-0205", g_goal_owe_fix, "creature: a goal reward that reaches a host without a creature is paid later");
struct OwedGoal {
    ResourceKey k;
    int outcome;
    float dna;
};
static std::vector<OwedGoal> g_goal_owed;
static uint32_t g_goal_owed_paid = 0;
static std::map<std::pair<uint64_t, int>, bool> g_goal_paid; // host: (species, outcome) paid

static void SK_THISCALL h_GoalUpdate(void* goal, uint32_t* a, uint32_t* b)
{
    void* prev = g_goal;
    g_goal = goal;
    o_GoalUpdate(goal, a, b);
    g_goal = prev;
}

static int goal_outcome(const ResourceKey& k) // the species' relation now: 6 allied, 5 extinct
{
    void* mgr = rel_mgr();
    return mgr && a_RelGet ? thiscall<int>(a_RelGet, mgr, &k, (void*)nullptr, (void*)nullptr) : 0;
}

static uint64_t key64(const ResourceKey& k) { return ((uint64_t)k.group << 32) | k.instance; }

// host: a card's reward, from its own card or reported by a guest; false = paid already
static bool host_goal_pay(const ResourceKey& k, int outcome)
{
    auto id = std::make_pair(key64(k), outcome);
    if (g_goal_paid.count(id))
        return false;
    g_goal_paid[id] = true;
    g_goal_dna_paid++;
    return true;
}

bool goal_dna(float delta)
{
    if (!g_goal || !C.sess || !C.sess->connected())
        return false;
    ResourceKey k = field<ResourceKey>(g_goal, 0x80); // the card's species (init 0xC30300)
    int outcome = goal_outcome(k);
    if (C.sess->role() == net::Role::Host) {
        if (host_goal_pay(k, outcome))
            return false; // the host's game pays it (and the total goes to everybody)
        g_goal_dna_dropped++;
        return true;
    }
    net::Writer w; // guest: the host decides (on the host's key of the species)
    w.u8(K_GOAL);
    w.u16(0);
    creations::Key hk = creations::host_species({k.instance, k.type, k.group});
    w.u32(hk.instance); w.u32(hk.type); w.u32(hk.group);
    w.u8((uint8_t)outcome);
    w.f32(delta);
    C.sess->send_reliable_to(0, net::MSG_SOCIAL, w);
    g_goal_dna_dropped++;
    return true;
}

// abilities of a creature: vt 0xB0 count, vt 0xB4 get(i); ability +8 type, +0x10 1 on the social
// ones (sing, ...; docs/re/creature.md 8)
static int ability_count(void* c) { return c ? vcall<int>(c, 0xB0 / 4) : 0; }
static void* ability(void* c, int i) { return vcall<void*>(c, 0xB4 / 4, i); }

static int social_ability(void* c, int slot) // index of the slot-th social ability, -1
{
    int n = ability_count(c);
    for (int i = 0, k = 0; i < n && i < 64; i++) {
        void* a = ability(c, i);
        if (a && field<uint32_t>(a, 0x10) == 1 && k++ == slot)
            return i;
    }
    return -1;
}

static int ability_by_type(void* c, uint32_t type)
{
    int n = ability_count(c);
    for (int i = 0; i < n && i < 64; i++)
        if (void* a = ability(c, i); a && field<uint32_t>(a, 8) == type)
            return i;
    return -1;
}

static bool play_ability(void* c, int idx)
{
    return a_PlayAbility && c && idx >= 0 && thiscall<int>(a_PlayAbility, c, idx, (void*)nullptr) != -1;
}

static void send_act(uint16_t id, uint32_t type)
{
    net::Writer w;
    w.u8(K_ACT);
    w.u16(id);
    w.u32(type);
    C.sess->send_reliable_to(0, net::MSG_SOCIAL, w);
    g_acts_sent++;
}

// a social ability next to the engaged mirror: the avatar performs it (the game does nothing
// without an NPC session) and the host counts it as a round of the NPC's social game
static double g_last_act = -100; // when an action was last sent (the key poll and the bar's hook)
static bool act_index(int idx)
{
    void* av = game::avatar();
    void* px = g_engaged >= 0 ? npc::proxy((uint16_t)g_engaged) : nullptr;
    if (!av || !px || net::dist(game::position(av), game::position(px)) > kActRange)
        return false;
    if (idx < 0 || !play_ability(av, idx))
        return false; // no such ability, cooling down or no energy
    send_act((uint16_t)g_engaged, field<uint32_t>(ability(av, idx), 8));
    g_engaged_at = g_last_act = H->time_seconds();
    return true;
}
static bool act(int slot) // a social ability key (1-4)
{
    void* av = game::avatar();
    return av && act_index(social_ability(av, slot));
}

// BUG-0179 (players, two groups: "the creatures do not want to be friends" with the guest): the guest's
// game uses an ability of the HUD's bar, by a key or a CLICK on the bar's button, through
// AbilityBar_Use 0xD49560(ability index) (docs/re/creature.md#creature-guest-social-bar). Only the keys
// 1-4 were polled: a click on the social ability's button next to an engaged mirror did nothing
// (no animation, nothing sent), a player who uses the mouse could never befriend anything. The hook
// takes a social ability used next to the engaged mirror in the social stance like the key (the
// avatar plays it, the host counts the round); anything else goes on to the game. Off: keys only.
static bool g_bar_fix = true;
COOP_FIX("social.bar_click", "BUG-0179", g_bar_fix, "creature: a guest's click on a social ability of the ability bar plays the social game with the engaged creature (not only the keys 1-4)");
static uint32_t g_bar_acts = 0, g_bar_passed = 0;
typedef void(SK_CDECL* BarUse_t)(int);
static BarUse_t o_BarUse;
static void SK_CDECL h_BarUse(int idx)
{
    void* av = is_guest() && g_bar_fix && g_engaged >= 0 && game::in_creature_stage() && !(C.blocked && C.blocked())
                   ? game::avatar()
                   : nullptr;
    if (av && a_AbilityMode && cdecl_call<int>(a_AbilityMode) == 0 && idx >= 0 && idx < ability_count(av)) {
        void* a = ability(av, idx);
        if (a && field<uint32_t>(a, 0x10) == 1) { // a social ability (sing, dance, ...)
            if (H->time_seconds() - g_last_act < 0.5 || act_index(idx)) {
                g_bar_acts++; // (a key press also reaches here: the poll sent it a moment ago)
                return;
            }
        }
    }
    g_bar_passed++;
    o_BarUse(idx);
}

// BUG-0021: in the social stance (GetAbilityMode 0) a guest's key 1 next to the Kook it was impressing (and the click
// that engages one) sent a hit now and then (coop_creature_social_ally.py: `hits_sent` +1 with the stance 0 before and after the key; the host
// applied it, the Kook died and the species lost its impressions: (4, 2) -> (1, 0, killed 1), no ally). A player in
// the social stance cannot attack: such a hit is not sent. `coop.fix social.no_bite off`: sent (0.7.3.3).
static bool g_no_bite_fix = true;
COOP_FIX("social.no_bite", "BUG-0021", g_no_bite_fix, "creature: a guest's hit in the social stance is not sent to the host (it killed the creature being impressed)");
static uint32_t g_social_hits_dropped = 0;


bool drop_social_hit(uint16_t proxy_id)
{
    // any proxy: the click that engages a Kook sent a hit too (before any social act, lab 20261101-151116 #3).
    // Test hits (`coop.npc hit`, npc::debug_hit) pass: they are no input of the player
    if (!g_no_bite_fix || npc::test_hit_now() || !a_AbilityMode || cdecl_call<int>(a_AbilityMode) != 0)
        return false;
    if (g_social_hits_dropped++ < 5)
        H->log(M, SK_INFO, "social: a hit of the avatar on npc %u in the social stance is not sent (engaged %d)", proxy_id,
               g_engaged);
    return true;
}

static void guest_tick(double now)
{
    if (g_engaged >= 0 && (now - g_engaged_at > kEngageTime || !npc::proxy((uint16_t)g_engaged)))
        g_engaged = -1;
    // the stance: GetAbilityMode 0 social, 1 combat
    bool social_stance = a_AbilityMode && cdecl_call<int>(a_AbilityMode) == 0;
    bool on = g_engaged >= 0 && social_stance && !(C.blocked && C.blocked());
    // BUG-0149: bit 0 of GetAsyncKeyState ("pressed since the last call") also counts a digit typed
    // in another window: it is taken only while the game was in front since the last poll
    HWND gw = H->game_window ? (HWND)H->game_window() : nullptr;
    bool front = !gw || GetForegroundWindow() == gw;
    static bool s_was_front = true;
    SHORT mask = front && s_was_front ? 0x8001 : 0x8000;
    if (!front)
        on = false;
    s_was_front = front;
    for (int k = 0; k < 4; k++) {
        SHORT st = GetAsyncKeyState('1' + k); // bit 0: pressed since the last call (a short tap)
        bool down = (st & mask) != 0;
        if (down && !g_keys[k] && on && now - g_last_act >= 0.5) // (the bar's hook may have sent it)
            act(k);
        g_keys[k] = down;
    }
}

// ------------------------------------------------------------------ host: the guests' games
struct Game {
    int rounds = 0;
    double last = 0;
};
static std::map<std::pair<uint8_t, uint16_t>, Game> g_games;
static uint32_t g_acts_in = 0, g_impressed = 0, g_refused = 0;

struct Pending { // applied outside the frame (the result refreshes the creature UI)
    uint8_t player;
    uint16_t npc;
};
static std::vector<Pending> g_pending;

static void send_progress(uint8_t to, uint16_t id, int done, int needed)
{
    net::Writer w;
    w.u8(K_PROGRESS);
    w.u16(id);
    w.u8((uint8_t)done);
    w.u8((uint8_t)needed);
    C.sess->send_reliable_to(to, net::MSG_SOCIAL, w);
}

static void send_result(uint16_t id, uint8_t player, bool ok, uint32_t anim, float dna)
{
    net::Writer w;
    w.u8(K_RESULT);
    w.u16(id);
    w.u8(player);
    w.u8(ok ? 1 : 0);
    w.u32(anim);
    w.f32(dna);
    C.sess->broadcast_reliable(net::MSG_SOCIAL, w);
}

static int social_mode(void* npc_c) { return a_SocialMode ? thiscall<int>(a_SocialMode, npc_c) : 0; }

static int rounds_needed(void* npc_c)
{
    // the native game: tuning +0x424 rounds, one more for an alpha (+0xB58 bit 0x1; 0xDA43D0)
    void* tuning = field<void*>(npc_c, 0xE84);
    int n = tuning ? (int)field<uint32_t>(tuning, 0x424) : 1;
    n = std::clamp(n, 1, 6);
    if (field<uint32_t>(npc_c, game::kFlagsOff) & 1)
        n++;
    return n;
}

static uint32_t reaction_anim(int relation) // as the social success 0xD9BC40 picks it
{
    switch (relation) {
    case 1: return 0xF37DB906;
    case 2: case 5: return 0xD37DB8F6;
    case 3: return 0x538BF747;
    default: return 0x738BF75B;
    }
}

// the native result of a won social game (0xD9BC40) without its meter UI: the species' impress
// event (counts, ally at the tuning's threshold), the NPC's DNA worth, the "impressed" flag, the
// NPC's reaction animation
static void apply_success(uint8_t player, uint16_t id)
{
    void* c = npc::host_npc(id);
    void* mgr = rel_mgr();
    if (!c || !mgr || !a_RelEvent || game::is_dead(c) || game::impressed(c)) {
        g_refused++; // gone, died or impressed by somebody else meanwhile
        send_result(id, player, false, 0, 0);
        return;
    }
    void* av = game::avatar();
    void* key = (uint8_t*)c + game::kSpeciesKeyOff;
    int rel = a_RelGet ? thiscall<int>(a_RelGet, mgr, key, c, av) : 0;
    int mode = social_mode(c);
    uint32_t anim = reaction_anim(rel);
    thiscall<void>(a_RelEvent, mgr, 3, key, c);
    float dna = 0;
    if (mode != 4 && a_DnaWorth) {
        dna = thiscall<float>(a_DnaWorth, c, 1);
        if (dna > 0 && dna < 10000 && C.pace)
            dna = C.pace(dna); // BUG-0510: the party's pace (a guest's friend, as the host's own are in on_dna)
        if (dna > 0 && dna < 10000)
            game::dna_add_raw(dna); // the host's pool, shared with everybody (MSG_DNA)
    }
    game::set_impressed(c, true);
    if (a_PlayReaction)
        thiscall<int>(a_PlayReaction, c, anim, 1, -1);
    g_impressed++;
    send_result(id, player, true, anim, dna);
    if (C.feed)
        C.feed(C.sess->name_of(player) + " impressed a creature (+" + std::to_string((int)(dna + 0.5f)) + " DNA)");
}

static void pending_task(void*)
{
    auto v = std::move(g_pending);
    g_pending.clear();
    for (auto& p : v)
        apply_success(p.player, p.npc);
}

static void host_act(uint8_t from, uint16_t id, uint32_t type)
{
    g_acts_in++;
    void* c = npc::host_npc(id);
    void* pup = C.puppet_of ? C.puppet_of(from) : nullptr;
    if (!c || game::is_dead(c) || game::impressed(c) || social_mode(c) == 0 ||
        (pup && net::dist(game::position(pup), game::position(c)) > kActRange + 4)) {
        g_refused++;
        send_result(id, from, false, 0, 0);
        return;
    }
    if (pup) // the host sees the guest perform it
        play_ability(pup, ability_by_type(pup, type));
    double now = H->time_seconds();
    Game& g = g_games[{from, id}];
    if (now - g.last > kEngageTime)
        g.rounds = 0;
    g.last = now;
    g.rounds++;
    int needed = rounds_needed(c);
    send_progress(from, id, std::min(g.rounds, needed), needed);
    if (g.rounds < needed)
        return;
    g_games.erase({from, id});
    if (g_pending.empty())
        H->run_outside_frame(pending_task, nullptr);
    g_pending.push_back({from, id});
}

// ------------------------------------------------------------------ module
void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    a_RelMgrGet = H->sym("Simulator::cSpeciesRelationshipManager::Get");
    a_RelGet = H->sym("Simulator::cSpeciesRelationshipManager::GetRelationship");
    a_RelEvent = H->sym("Simulator::cSpeciesRelationshipManager::ApplyEvent");
    a_RelRecord = H->sym("SporeCoop::SpeciesRelationMap_Record");
    a_SocialMode = H->sym("SporeCoop::CreatureSocialMode");
    a_DnaWorth = H->sym("SporeCoop::CreatureDnaWorth");
    a_PlayAbility = H->sym("Simulator::cCreatureBase::PlayAbility");
    a_PlayReaction = H->sym("SporeCoop::CreaturePlayAnim");
    a_GoalUI = H->sym("SporeCoop::CreatureGoalUI_Get");
    a_GoalRefresh = H->sym("SporeCoop::SpeciesGoal_Refresh");
    a_AbilityMode = H->sym("Simulator::cCreatureGameData::GetAbilityMode");
    if (uintptr_t a = H->sym("SporeCoop::AbilityBar_Use")) // BUG-0179
        H->hook_create(M, (void*)a, (void*)h_BarUse, (void**)&o_BarUse);
    if (uintptr_t a = H->sym("SporeCoop::BT_PostStimulus"))
        H->hook_create(M, (void*)a, (void*)h_Post, (void**)&o_Post);
    if (a_RelEvent)
        H->hook_create(M, (void*)a_RelEvent, (void*)h_RelEvent, (void**)&o_RelEvent);
    if (uintptr_t a = H->sym("SporeCoop::SpeciesGoal_Update"))
        H->hook_create(M, (void*)a, (void*)h_GoalUpdate, (void**)&o_GoalUpdate);
}

void tick(double now)
{
    if (!C.sess || !game::in_creature_stage() || !game::avatar())
        return;
    if (is_host() && !g_goal_owed.empty()) { // BUG-0205: rewards that came while we had no creature
        auto owed = std::move(g_goal_owed);
        g_goal_owed.clear();
        for (auto& o : owed)
            if (goal_outcome(o.k) == o.outcome && host_goal_pay(o.k, o.outcome)) {
                game::dna_add_raw(o.dna);
                g_goal_owed_paid++;
                if (C.feed)
                    C.feed(std::string(o.outcome == 6 ? "A species became our ally" : "A goal was reached") +
                           ": +" + std::to_string((int)(o.dna + 0.5f)) + " DNA");
            }
    }
    if (is_host()) {
        static double last = 0;
        if (now - last < 1.0)
            return;
        last = now;
        auto t = read_table();
        for (auto it = g_goal_paid.begin(); it != g_goal_paid.end();) { // an older save loaded again
            bool still = false;
            for (auto& r : t)
                still |= (((uint64_t)r.key[2] << 32) | r.key[0]) == it->first.first && r.relation == it->first.second;
            it = still ? std::next(it) : g_goal_paid.erase(it);
        }
        if (!(t == g_sent) || now - g_table_sent > 10.0) { // changes at once, the whole table every 10 s
            g_sent = t;
            g_table_sent = now;
            send_table(t, -1);
        }
    } else if (is_guest()) {
        guest_tick(now);
        static double last = 0;
        if (now - last >= 0.5) {
            last = now;
            enforce_table();
        }
    }
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type == net::MSG_SPECIES) {
        if (!is_guest() || from != 0)
            return;
        uint16_t n = r.u16();
        std::vector<SpeciesRec> t;
        for (int i = 0; i < n && r.ok() && i < 512; i++) {
            SpeciesRec s;
            s.key[0] = r.u32(); s.key[1] = r.u32(); s.key[2] = r.u32();
            s.relation = (int32_t)r.u32(); s.x = r.u32(); s.impressed = r.u32(); s.killed = r.u32();
            t.push_back(s);
            // a creation of the host's own: its NPCs have a key of their own here, its nests (from the
            // save) the host's (creations::local_species): the same record under both
            creations::Key lk = creations::local_species({s.key[0], s.key[1], s.key[2]});
            if (lk.instance != s.key[0]) {
                s.key[0] = lk.instance;
                t.push_back(s);
            }
        }
        if (!r.ok())
            return;
        bool news = !g_have_table || !(t == g_host_table);
        g_host_table = std::move(t);
        g_have_table = true;
        g_table_rx++;
        if (game::in_creature_stage() && game::avatar()) {
            enforce_table();
            if (news) // the cards read the table only when told (also when ours had it already)
                H->run_outside_frame(refresh_cards, nullptr);
        }
        return;
    }
    if (type != net::MSG_SOCIAL)
        return;
    uint8_t kind = r.u8();
    uint16_t id = r.u16();
    if (kind == K_ACT && is_host()) {
        uint32_t ability_type = r.u32();
        if (r.ok())
            host_act(from, id, ability_type);
        return;
    }
    if (kind == K_GOAL && is_host()) {
        ResourceKey k;
        k.instance = r.u32(); k.type = r.u32(); k.group = r.u32();
        int outcome = r.u8();
        float dna = r.f32();
        // a card pays when its species became allied (6) or extinct (5): the host's own table (the
        // one every guest follows) must say so too
        bool sane = r.ok() && std::isfinite(dna) && dna > 0 && dna < 10000 && (outcome == 5 || outcome == 6);
        if (sane && !game::avatar() && g_goal_owe_fix) {
            if (g_goal_owed.size() < 64)
                g_goal_owed.push_back({k, outcome, dna});
            return;
        }
        if (sane && goal_outcome(k) == outcome && game::avatar() && host_goal_pay(k, outcome)) {
            game::dna_add_raw(dna);
            if (C.feed)
                C.feed(std::string(outcome == 6 ? "A species became our ally" : "A goal was reached") + ": +" +
                       std::to_string((int)(dna + 0.5f)) + " DNA");
        }
        return;
    }
    if (kind == K_PROGRESS && is_guest()) {
        int done = r.u8(), needed = r.u8();
        if (r.ok() && done < needed && C.feed)
            C.feed("Impressing: " + std::to_string(done) + "/" + std::to_string(needed));
        return;
    }
    if (kind == K_RESULT && is_guest()) {
        uint8_t player = r.u8();
        bool ok = r.u8() != 0;
        uint32_t anim = r.u32();
        float dna = r.f32();
        if (!r.ok())
            return;
        g_results_in++;
        void* px = npc::proxy(id);
        bool mine = player == C.sess->my_id();
        if (ok && px) {
            game::set_impressed(px, true);
            if (a_PlayReaction)
                thiscall<int>(a_PlayReaction, px, anim, 1, -1);
        }
        if (mine && C.feed)
            C.feed(ok ? "Impressed! +" + std::to_string((int)(dna + 0.5f)) + " DNA for the pack"
                      : "It does not want to socialize now");
        if (mine && g_engaged == id)
            g_engaged = -1;
    }
}

void on_session_end()
{
    g_engaged = -1;
    g_goal_paid.clear();
    g_goal_owed.clear();
    g_games.clear();
    g_have_table = false;
    g_host_table.clear();
    g_sent.clear();
}

std::string status()
{
    char b[300];
    if (is_host())
        snprintf(b, sizeof b,
                 "social(host): acts_in=%u impressed=%u refused=%u games=%u species_tx=%u goal_dna_paid=%u "
                 "goal_dna_dropped=%u goal_owed=%u goal_owed_paid=%u\n",
                 g_acts_in, g_impressed, g_refused, (unsigned)g_games.size(), g_table_tx, g_goal_dna_paid,
                 g_goal_dna_dropped, (unsigned)g_goal_owed.size(), g_goal_owed_paid);
    else
        snprintf(b, sizeof b,
                 "social(guest): engaged=%d engages=%u acts_sent=%u results_in=%u species_rx=%u species_fixed=%u "
                 "events_dropped=%u cards_refreshed=%u goal_dna_sent=%u social_hits_dropped=%u bar_acts=%u bar_passed=%u\n",
                 g_engaged, g_engages, g_acts_sent, g_results_in, g_table_rx, g_table_fixed, g_events_dropped,
                 g_cards_refreshed, g_goal_dna_dropped, g_social_hits_dropped, g_bar_acts, g_bar_passed);
    return b;
}

std::string species_dump()
{
    std::string out;
    char b[200];
    for (auto& r : read_table()) {
        snprintf(b, sizeof b, "species %08X/%08X/%08X relation=%d x=%u impressed=%u killed=%u\n", r.key[0], r.key[1],
                 r.key[2], r.relation, r.x, r.impressed, r.killed);
        out += b;
    }
    return out.empty() ? "no species records\n" : out;
}

// tests (BUG-0462): how the game sees each NPC's relation to the own avatar, per NPC net id (host: its NPCs,
// guest: the proxies): the record or else the creature's archetype tuning (+0xE84) +0x390 decides
// (GetRelationship 0xBA40E0, docs/re/creature.md#creature-relations); CreatureSocialMode is what the click uses
std::string relations_dump()
{
    struct U {
        std::string out;
        void* av;
        void* mgr;
    } u{{}, game::avatar(), rel_mgr()};
    if (!u.av || !u.mgr || !a_RelGet)
        return "no avatar\n";
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            if (noun != 0x18EB45Eu || !game::object_alive(o) || IsBadReadPtr(o, 0x1680) || !game::species_profile(o))
                return true;
            int id = is_host() ? npc::host_id(o) : npc::proxy_id(o);
            if (id < 0 || u->out.size() > 12000)
                return true;
            uint8_t* t = *(uint8_t**)((uint8_t*)o + 0xE84);
            int def = t && !IsBadReadPtr(t + 0x390, 4) ? *(int*)(t + 0x390) : -1;
            int rel = thiscall<int>(a_RelGet, u->mgr, (uint8_t*)o + 0xB28, u->av, o);
            int mode = a_SocialMode ? thiscall<int>(a_SocialMode, o) : -1;
            char b[200];
            snprintf(b, sizeof b, "rel %d species=%08X tuning=%p default=%d relation=%d mode=%d herd=%p arch=%08X tkey=%08X%s\n", id,
                     game::species_key(o).instance, t, def, rel, mode, game::herd_of(o),
                     game::herd_of(o) ? *(uint32_t*)((uint8_t*)game::herd_of(o) + 0x88) : 0u,
                     t && !IsBadReadPtr(t - 4, 4) ? *(uint32_t*)(t - 4) : 0u, game::is_dead(o) ? " DEAD" : "");
            u->out += b;
            return true;
        },
        &u);
    return (u.out.empty() ? std::string("no NPCs\n") : u.out) + npc::tuning_status();
}

// tests (BUG-0205): a guest reports a goal card's reward for species <inst> the way goal_dna does
bool debug_send_goal(uint32_t inst, int outcome, float dna)
{
    if (!C.sess || C.sess->role() != net::Role::Client || !C.sess->connected())
        return false;
    net::Writer w;
    w.u8(K_GOAL);
    w.u16(0);
    w.u32(inst); w.u32(0x2B978C46u); w.u32(0x40626200u);
    w.u8((uint8_t)outcome);
    w.f32(dna);
    return C.sess->send_reliable_to(0, net::MSG_SOCIAL, w);
}

// tests: the host's record of a creature species (as the game's ApplyEvent keeps it), sent on change
bool debug_set_record(uint32_t inst, int relation, uint32_t impressed)
{
    void* mgr = rel_mgr();
    if (!mgr || !a_RelRecord)
        return false;
    ResourceKey k{inst, 0x2B978C46u, 0x40626200u};
    uint32_t* rec = thiscall<uint32_t*>(a_RelRecord, (uint8_t*)mgr + 0x20, &k);
    if (!rec)
        return false;
    rec[0] = (uint32_t)relation;
    rec[2] = impressed;
    return true;
}

std::string debug_objects(uint32_t lo, uint32_t hi)
{
    struct U {
        uint32_t lo, hi;
        std::string out;
    } u{lo, hi, {}};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            uint32_t vt = *(uint32_t*)o;
            if (vt >= u->lo && vt < u->hi && u->out.size() < 4000) {
                char b[96];
                snprintf(b, sizeof b, "obj %p vt=%08X noun=%08X\n", o, vt, noun);
                u->out += b;
            }
            return true;
        },
        &u);
    return u.out;
}

bool debug_goto(uint16_t id) // tests: the guest's avatar steps next to proxy #id (2.5 m)
{
    void* av = game::avatar();
    void* px = npc::proxy(id);
    if (!av || !px)
        return false;
    net::Vec3 a = game::position(av), p = game::position(px);
    float d = net::dist(a, p);
    if (d < 0.1f)
        return false;
    float k = 2.5f / d;
    net::Vec3 t{p.x + (a.x - p.x) * k, p.y + (a.y - p.y) * k, p.z + (a.z - p.z) * k};
    game::teleport(av, t, game::orientation(av));
    return true;
}

bool debug_social(uint16_t id, int slot)
{
    if (slot < 0) { // toggle the stimulus log
        g_log_posts = !g_log_posts;
        return true;
    }
    if (!is_guest() || !npc::proxy(id))
        return false;
    g_engaged = id; // as if the avatar had engaged it
    g_engaged_at = H->time_seconds();
    return act(slot);
}

} // namespace social
