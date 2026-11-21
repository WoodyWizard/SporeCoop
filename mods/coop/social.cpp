#include "social.h"
#include "party.h"
#include "lifecycle.h"
#include "mouse.h"
#include "fixes.h"
#include "creations.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "game.h"
#include "guest_pack.h"
#include "player_life.h"
#include "net/validate.h"
#include "npc.h"
#include "player_rules.h"
#include "progress_debt.h"
#include "sporekit_game.hpp"
#include "world.h"

using namespace skg;

namespace social {

unsigned g_stage_msg_win = 0; // coop.cpp h_StageMsg: 0x60B4123 seen by the creature-stage handler (tests)

static const SkHost* H;
static SkMod* M;
static Ctx C;

static uintptr_t a_AbilityMode;
static uintptr_t a_RelMgrGet, a_RelGet, a_RelEvent, a_RelRecord, a_SocialMode, a_DnaWorth, a_PlayAbility,
    a_PlayReaction;

static constexpr uint32_t kSocialStimulus = 0x10000; // the avatar engages an NPC socially
static constexpr float kActRange = 12.f;             // m between the guest and the NPC for an action
static constexpr double kEngageTime = 60.0;          // an engagement without actions ends after this

enum Kind : uint8_t { K_ACT = 0, K_PROGRESS = 1, K_RESULT = 2, K_GOAL = 3, K_ENGAGE = 4, K_PROMPT = 5 };

// BUG-0525: the NPC's prompts of a guest's social game (docs/re/creature.md#creature-social-prompts). The native
// session (SocialPropose 0xDA4360, its tick 0xDA4950) has the NPC perform the abilities of a prompt list (npc
// +0x1020, random from its species profile +0x7A4, one per round) and counts the player's answer to each prompt
// (stimulus 0x10000000 kind 1 from the answer node 0xD9C100, checked in 0xD9B070): its ability's +0x10 equal to
// the prompt's gives the ability's points (+0x3C), another 0.1 of them (0x1582F1C); the meter is full at the sum
// of the prompts' points, a prompt waits 10 s (0x1582F20) after the NPC's performance, at the end of the list an
// unfilled meter is a failure (animation 0x53685D8F). A guest's game with a mirror had no session anywhere: the
// host counted any social ability as a round. Now the host runs that list for the guest's game: it picks the
// prompts when the guest engages its NPC (K_ENGAGE), plays each prompt on its NPC (relayed by npc.anims) and sends
// it (K_PROMPT: the guest plays it on its mirror), and counts only an answer to an open prompt, by its points.
// Off: any social ability is a round, no prompts (0.7.5).
static bool g_prompt_fix = true;
COOP_FIX("social.prompts", "BUG-0525", g_prompt_fix, "creature: in a guest's social game the creature shows what to do (its prompts) and only an answer to a prompt counts");
static constexpr double kPromptGreet = 2.0;   // s from the engagement to the first prompt (the NPC's greeting)
static constexpr double kPromptWait = 13.0;   // s a prompt stays open: its performance (~3 s) + 10 s (0x1582F20)
static constexpr double kPromptNext = 1.5;    // s from an answer to the next prompt
static constexpr uint32_t kFailAnim = 0x53685D8Fu; // the NPC's animation of a failed social game (0xDA4C7A)
static uint32_t g_prompts_sent = 0, g_prompts_in = 0, g_answers = 0, g_answers_wrong = 0, g_answers_early = 0,
                g_prompt_fails = 0;
static uint32_t g_prompt_anim = 0;  // guest: the animation of the last prompt it got
static int g_prompt_npc = -1;       // guest: the proxy it was for
static double g_prompt_at = -100;   // guest: when

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
// BUG-0420: the host's species table was written into a guest's own world every 0.5 s once the guest played another
// world of the host's stage (only new tables were gated): its own relations were reset to the host's. Off: as 0.7.10.
static bool g_world_fix = true;
COOP_FIX("social.world_gate", "BUG-0420", g_world_fix, "creature: a guest in another world keeps its own species relations");
static uint32_t g_table_forgot = 0;
static double g_table_rx_at = 0; // BUG-0699 diagnostics: when the host's last table came

// BUG-0699: the host looked at its species table once a second: a kill, an impression or an alliance reached the
// guests 1-1.5 s later (the detector's species_relation / species_killed rows, found again 1.5 s after its first look).
// On: every 0.25 s (the table is sent only when it changed). Off: once a second (0.7.10).
static bool g_table_fast_fix = true;
COOP_FIX("social.table_fast", "BUG-0699", g_table_fast_fix,
         "creature: a change of the species relations (a kill, an alliance) reaches the guests within a third of a second");
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

// ---------------------------------------------------------------- BUG-0561
// The Creature minimap shows a nest as allied (the green face, icon flag 0x200000 in 0xD425A0, picture 0xB3B6763E in
// 0xE10560) only when the nest's herd has mbBestFriends (+0x191) set or is the world's player species; it never
// reads the relationship table. The game sets +0x191 on every herd of a species in ApplyEvent 0xBA4A00 when the
// relation changes (0xBA4840 -> cHerd_SetBestFriends 0xC6A210(new == 6)). A guest gets the host's table written
// into its own (enforce_table), no ApplyEvent runs in its game: its herds of an allied species kept +0x191 = 0 and
// the minimap the grey "discovered" mark. Now the guest's herds follow the table: +0x191 = (relation == 6).
// `coop.fix social.nest_ally off`: the flag stays as the guest's own game set it (0.7.6).
static bool g_nest_ally_fix = true;
COOP_FIX("social.nest_ally", "BUG-0561", g_nest_ally_fix, "creature: an allied species' nests show the ally face on the guest's map too");
static uint32_t g_nest_ally_set = 0;

static void herds_follow_table()
{
    uintptr_t set = H->sym("SporeCoop::cHerd_SetBestFriends");
    if (!g_nest_ally_fix || !set || !g_have_table || !is_guest() || !game::in_creature_stage())
        return;
    struct Q {
        std::vector<std::pair<void*, bool>> sp; // species profile, allied
        uintptr_t set;
    } q{{}, set};
    for (auto& r : g_host_table)
        if (void* sp = game::species_by_key({r.key[0], r.key[1], r.key[2]}))
            q.sp.push_back({sp, r.relation == 6});
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun != game::kHerdNoun || !skg::readable(o, 0x194))
                return true;
            auto* q = (Q*)u;
            void* sp = game::herd_species(o);
            for (auto& [s, allied] : q->sp)
                if (s == sp) {
                    if ((field<uint8_t>(o, 0x191) != 0) != allied) {
                        thiscall<void>(q->set, o, (int)allied);
                        g_nest_ally_set++;
                    }
                    break;
                }
            return true;
        },
        &q);
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
            if (g_table_fixed < 400) // BUG-0699 diagnostics: when a guest's record took the host's values
                H->log(M, SK_INFO, "social: species %08X: relation %d -> %d, impressed %u -> %u, killed %u -> %u from the "
                       "host's table (%.1f s old)", r.key[0], (int32_t)rec[0], r.relation, rec[2], r.impressed, rec[3],
                       r.killed, H->time_seconds() - g_table_rx_at);
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
static int social_mode(void* npc_c) { return a_SocialMode ? thiscall<int>(a_SocialMode, npc_c) : 0; }
static uint32_t g_posts_logged = 0;

// Decision 7L (owner 2026-11-20: "If a partner impresses a creature to join a pack, it should go to their pack like
// native spore"), BUG-9308. The native game: CreatureSocialMode (the creature's social goal to the avatar) is 4 for a
// friendly creature (allied species, impressed, the own species; docs/re/creature.md#creature-native-recruit); the
// success of a goal-4 game (0xD9BC40) gives no DNA and sets the session's recruit flag (+0x1FC & 0x10), and the session
// end (0xD9F7A0) adds the creature to the avatar's pack (cPosseSimulator_AddMember). A partner's social game runs on the
// host (host_act, apply_success): nobody ever recruited, its won creature only got the impressed flag; the partner's
// recruit path (player_rules h_AddMember -> K_RECRUIT) waits for an AddMember on the partner's game, which runs no
// session for a mirror. And an impressed creature was refused a second game (the native recruit game of a befriended
// one). Now the host asks the social goal as the partner's own game would (inside guest_pack's party context: the
// partner's puppet is the avatar, its pack the posse), lets a goal-4 game run also on an impressed creature, and at its
// win recruits it into that partner's pack (guest_pack::recruit, the game's own AddMember there), with no DNA as
// natively. The guest opens its meter for a goal-4 creature also when it is impressed, and posts no part-reward message
// for it (the native success posts 0x60B4123 only for goals 1-3).
// Off (`coop.fix social.partner_recruit off`): impressed only, no pack (67-dev).
static bool g_recruit_fix = true;
COOP_FIX("social.partner_recruit", "BUG-9308", g_recruit_fix, "creature: a creature a partner wins in a recruit social game joins that partner's pack (decision 7L)");
static uint32_t g_recruits = 0, g_recruits_refused = 0;

// BUG-9308 (live QA 2026-11-21, MSI): inside the partner's party context GetRelationship 0xBA40E0 returned 6 (allied)
// for every NPC (`coop.social pmode`: host relation 3 mode 2, the partner's relation 6 mode 4; ~ the puppet's own
// player-species tuning +0x390 = 6 is read as "the one that is not the avatar"), so a partner's first win on a neutral
// creature was a recruit game: no DNA, no impression step, straight into its pack. CreatureSocialMode 0xC0E800 (read
// live): the relation's jump table (0xC0EA0C) gives 1 / 2 -> 3, 3 -> 2, 4 -> 1, 5 -> 0 (3 while busy), each 0 when the
// creature is friendly (vt 0xD4, e.g. impressed); 6 -> 4 only in Creature when the creature is in no pack (+0xB58 &
// 0x100) and the pack is not full, else 0. The species relation is the party's (the host's table, the guests follow it):
// it is read outside the party context; the party context only decides goal 4 (the partner's pack: membership, room).
// Off (`coop.fix social.partner_relation off`): the goal from inside the party context (every NPC a recruit, first QA).
static bool g_partner_rel_fix = true;
COOP_FIX("social.partner_relation", "BUG-9308", g_partner_rel_fix, "creature: a partner's social goal uses the party's species relation (only allied species are recruit games)");
static uint32_t g_partner_rel_kept = 0;
// host: the NPC's social goal for player `player` (the partner's puppet as the avatar, its own pack)
static int partner_mode(uint8_t player, void* c)
{
    void* pup = g_recruit_fix && C.puppet_of ? C.puppet_of(player) : nullptr;
    if (g_recruit_fix && g_partner_rel_fix && pup) {
        void* mgr = rel_mgr();
        const int rel = mgr && a_RelGet ? thiscall<int>(a_RelGet, mgr, (uint8_t*)c + game::kSpeciesKeyOff, game::avatar(), c) : 0;
        if (rel != 6) { // not an allied species: the party's goal (1-3, or 0 for a friendly / busy one), as the host's
            g_partner_rel_kept++;
            return social_mode(c);
        }
    }
    const size_t tok = pup && guest_pack::party_mode() ? guest_pack::party_enter_puppet(pup) : 0;
    const int m = social_mode(c);
    if (tok)
        guest_pack::party_leave_token(tok);
    return m;
}
// host: the goal for whoever plays: the host's own avatar, or a partner (7L)
static int mode_for(uint8_t player, void* c)
{
    return g_recruit_fix && g_partner_rel_fix && C.sess && player != C.sess->my_id() ? partner_mode(player, c) : social_mode(c);
}
// host: the creature takes no social game from this player (impressed, unless a recruit game as in its own game)
static bool closed_to(uint8_t player, void* c)
{
    return game::impressed(c) && !(g_recruit_fix && partner_mode(player, c) == 4);
}
// guest: its meter opens for this proxy (its own game's social goal)
static bool open_to_me(void* px)
{
    const int m = social_mode(px);
    return m != 0 && (!game::impressed(px) || (g_recruit_fix && m == 4));
}

static void send_engage(uint16_t id) // BUG-0525: the host starts the NPC's prompts
{
    if (!g_prompt_fix)
        return;
    net::Writer w;
    w.u8(K_ENGAGE);
    w.u16(id);
    C.sess->send_reliable_to(0, net::MSG_SOCIAL, w);
}

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
                if (id != g_engaged) {
                    g_engages++;
                    send_engage((uint16_t)id);
                }
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
// (its switch core.away_goal was retired 2026-11-12, roadmap 0e: stable since 0.7.3.2)
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

int creature_relationship(void* c)
{
    auto key = game::species_key(c);
    return goal_outcome({key.instance,key.type,key.group});
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
    world::request_autosave();
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

// BUG-9297 (a guest, 67-dev: "the impress bar was still not functioning"): the keys 1-4 and the ability bar took an
// ability as social only when its +0x10 was 1 (the sing). +0x10 is the social kind a prompt asks for
// (docs/re/creature.md#creature-social-prompts; combat abilities have 0), so a guest's dance, charm and pose went to
// the game's own path, were never sent as answers, and a prompt of those kinds could not be answered. Now any kind
// but 0 is a social answer (as player_rules' social_use already took it, BUG-9136). Off (`coop.fix social.all_kinds
// off`): only the sing (67-dev).
static bool g_all_kinds_fix = true;
COOP_FIX("social.all_kinds", "BUG-9297", g_all_kinds_fix, "creature: a guest's dance, charm and pose answer the creature's prompts too (not only the sing)");
static bool is_social(void* a)
{
    const uint32_t kind = a ? field<uint32_t>(a, 0x10) : 0;
    return g_all_kinds_fix ? kind != 0 : kind == 1;
}

static int social_ability(void* c, int slot) // index of the slot-th social ability, -1
{
    int n = ability_count(c);
    for (int i = 0, k = 0; i < n && i < 64; i++) {
        void* a = ability(c, i);
        if (a && is_social(a) && k++ == slot)
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
    void* a = ability(av, idx);
    send_act((uint16_t)g_engaged, field<uint32_t>(a, 8));
    static unsigned logged = 0; // BUG-9297: which answer left (60 lines a game run)
    if (logged < 60) {
        logged++;
        H->log(M, SK_INFO, "social: act sent to npc %d: ability %d type %08X kind %u", g_engaged, idx, field<uint32_t>(a, 8),
               field<uint32_t>(a, 0x10));
    }
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
    if (player_rules::summon_blocked(idx)) // decision 5FA: the party's Summon Flock is out
        return;
    void* av = is_guest() && g_bar_fix && g_engaged >= 0 && game::in_creature_stage() && !(C.blocked && C.blocked())
                   ? game::avatar()
                   : nullptr;
    if (av && a_AbilityMode && cdecl_call<int>(a_AbilityMode) == 0 && idx >= 0 && idx < ability_count(av)) {
        void* a = ability(av, idx);
        if (a && is_social(a)) { // a social ability (sing, dance, charm, pose; BUG-9297)
            if (H->time_seconds() - g_last_act < 0.5 || act_index(idx)) {
                g_bar_acts++; // (a key press also reaches here: the poll sent it a moment ago)
                return;
            }
        }
    }
    g_bar_passed++;
    o_BarUse(idx);
}

// BUG-0179: the guest's social game looked simplified: no social meter on its screen, and the game's
// success message 0x60B4123 never posted for its win (the creature-stage handler 0xD47FC0 rolls the
// species' part reward on it, docs/re/creature.md#creature-guest-meter). The native meter is one UI
// object of the creature mode, [[cCreatureModeStrategy +0x68] +0x60]; SocialPropose 0xDA4360 shows it
// (Reset, SetVisible 0, rounds, relation face, SetVisible 1), the session tick moves its bar (SetBar),
// the win closes it (Finish, relation face), the session end hides it (SetVisible 0). The guest does
// the same around its game with a mirror: open at the engagement, the bar at each round the host
// counts (K_PROGRESS), closed at the result, and the win's message posted on its own game.
// Off: no meter, no message (0.7.4).
static bool g_meter_fix = true;
COOP_FIX("social.guest_meter", "BUG-0179", g_meter_fix, "creature: a guest's social game shows the game's social meter and posts the game's success message (part rewards) like the host's");
static uintptr_t a_StrategyGet, a_ExecuteAction, a_MeterReset, a_MeterVisible, a_MeterRounds, a_MeterFace, a_MeterBar,
    a_MeterFinish;
static int g_meter_npc = -1;   // proxy id the meter is open for
static int g_meter_rounds = 1; // rounds shown on it
static uint32_t g_meter_opened = 0, g_meter_bars = 0, g_meter_closed = 0, g_win_msgs = 0;
static float g_meter_frac = 0;
// BUG-9258 (BigTexas: "the animation for the pink bar isn't smooth"): the bar jumped to each new value when the host's
// count arrived (one answer, one round trip later). It glides there now: the meter tick moves it a step a frame (about
// 0.4 s for a round). Off (`coop.fix social.meter_glide off`): it jumps.
static bool g_meter_glide = true;
COOP_FIX("social.meter_glide", "BUG-9258", g_meter_glide, "creature: a guest's social meter glides to each new fill instead of jumping");
static float g_meter_goal = 0;   // where the bar goes (the host's count)
static double g_meter_step_at = 0;
// BUG-9305 (owner, 67-dev: "Even when it was the correct one, it snapped the fill, rather than gradually filling like
// native Spore"). The native fill (spore-decomp, docs/re/creature.md#creature-meter-native-fill): an answer starts a
// step on the meter (cSPUISocialRainbow StartFill: the step's entry goes into the bar's list at once, the bar's end
// target set), then every frame SocialValues::Update moves the bar there by the answer animation's progress (SetBar(i,
// elapsed / length)): a linear fill over the answer's animation. The guest's glide (BUG-9258) had three snaps:
// meter_fill added a round's entry only when the fill passed half the round (lround), and the bar draws nothing
// without an entry, so the first half of a round was invisible and then appeared at once; the 20 % a step glide was
// most of the way there in 0.1 s; and the win (K_RESULT, right behind the last K_PROGRESS) set the meter full at once
// and the meter closed the next frame. Now the step's entry is there from its start, the bar moves linearly over
// kFillTime s, the win waits for the fill (kWinWait s at most) and the full meter stays kWinHold s.
// Off (`coop.fix social.meter_smooth off`): the 67-dev glide.
static bool g_meter_smooth = true;
COOP_FIX("social.meter_smooth", "BUG-9305", g_meter_smooth, "creature: a guest's social meter fills gradually like the native one (no snap at a round or at the win)");
constexpr double kFillTime = 1.0; // s for one answer's fill (~ the native answer animation)
constexpr double kWinWait = 3.0;  // s at most the win waits for the fill
constexpr double kWinHold = 1.5;  // s the full meter stays after the win
static float g_glide_from = 0;    // where the bar stood when the current goal arrived
static double g_glide_t0 = 0;     // when
static int g_win_npc = -1;        // the win waits for the fill
static double g_win_at = 0, g_hold_until = 0;
static uint32_t g_wins_deferred = 0;
static int g_meter_mode = -1;     // decision 7L: the proxy's social goal when its meter opened (4: a recruit game)
static bool smooth() { return g_meter_glide && g_meter_smooth; }
static float glide_pos(double now) // where the bar should stand now
{
    const double t = kFillTime > 0 ? std::clamp((now - g_glide_t0) / kFillTime, 0.0, 1.0) : 1.0;
    return g_glide_from + (g_meter_goal - g_glide_from) * (float)t;
}
static void glide_to(float goal, double now) // a new goal: the linear fill starts from where the bar is
{
    if (goal <= g_meter_goal + 1e-4f)
        return;
    g_glide_from = std::max(g_meter_frac, glide_pos(now));
    g_glide_t0 = now;
    g_meter_goal = goal;
}
static int g_meter_vis_at_win = -1; // the meter layout's own visible byte when the win arrived (tests)

// the layout's visible byte: SetVisible 0xE319D0 is UILayout::SetVisible(meter + 0xC), which writes
// [layout +0x14] +0x8C (0x8103B0); -1 when there is no window
static int meter_visible(void* m)
{
    uint8_t* lay = (uint8_t*)m + 0xC;
    uint8_t* w = *(uint8_t**)(lay + 0x14);
    return w && !IsBadReadPtr(w + 0x8C, 1) ? w[0x8C] : -1;
}

static void* meter_ui()
{
    if (!a_StrategyGet || !game::in_creature_stage())
        return nullptr;
    uint8_t* st = cdecl_call<uint8_t*>(a_StrategyGet);
    if (!st || IsBadReadPtr(st + 0x68, 4))
        return nullptr;
    uint8_t* p = *(uint8_t**)(st + 0x68);
    if (!p || IsBadReadPtr(p + 0x60, 4))
        return nullptr;
    void* m = *(void**)(p + 0x60);
    return m && !IsBadReadPtr(m, 0x200) ? m : nullptr;
}

static int relation_to(void* c)
{
    void* mgr = rel_mgr();
    void* av = game::avatar();
    return mgr && a_RelGet && c && av ? thiscall<int>(a_RelGet, mgr, (uint8_t*)c + game::kSpeciesKeyOff, c, av) : 0;
}

static int rounds_needed(void* npc_c);

struct MeterTask {
    int op; // 0 open, 1 bar, 2 win, 3 close
    int npc;
    float frac;
    int rounds;
};
static std::vector<MeterTask> g_meter_tasks;
// BUG-1055: meter steps of an ended session are not shown after it; the close (op 3, BUG-0808) stays queued
SESSION_STATE("social.meter_tasks", nullptr, [] {
    g_meter_tasks.erase(std::remove_if(g_meter_tasks.begin(), g_meter_tasks.end(), [](const MeterTask& t) { return t.op != 3; }),
                        g_meter_tasks.end());
}, []() -> size_t {
    size_t n = 0;
    for (auto& t : g_meter_tasks)
        n += t.op != 3;
    return n;
});

// BUG-1058: meter_tick marked the meter open for the engaged animal when it queued the open; a task that found no meter
// window (meter_ui null) dropped the open and the meter never came for that social game. Now only the task's real open
// marks it (the queue's own check keeps a second open away meanwhile); a missed open is queued again after
// kMeterRetry s. `coop.fix social.meter_retry off`: marked at the queue (0.7.13)
static bool g_meter_retry_fix = true;
COOP_FIX("social.meter_retry", "BUG-1058", g_meter_retry_fix, "creature: a guest's social meter opens also when the first try found no meter window");
constexpr double kMeterRetry = 0.25;
static double g_meter_missed_at;
static unsigned g_meter_missed, g_meter_test_null; // (tests: meter tasks that find no meter window)
// BUG-9230 (owner 2026-11-16 on MSI: "the on screen UI didn't fill up with the pink bar"): the meter has two bars that grow
// towards each other from the arc's ends (+0x3C and +0x40, Steam/GOG 2024 0.626 and 2.516: each bar's full length is half
// the gap, 0.945); the meter is full when they meet. SocialMeter_SetBar(i, f) only puts bar i between the ends of its
// current step (pos = lo + (hi - lo) * f, lo / pos / hi at +0x134 / +0x138 / +0x13C + i * 0xA4); the native session sets
// those ends for every round (TITAN 2026-11-16, a native meter: after round 1 of 3 both bars lo 0, hi 0.315). The guest's
// meter never got them (lo = hi = 0: every SetBar drew nothing, the progress only in the feed lines). Now both bars get
// their step's ends (lo = where it stands, hi = progress x the bar's length) and SetBar moves them there.
// Off (`coop.fix social.meter_fill off`): bar 0 only, without ends (62-dev).
static bool g_meter_fill_fix = true;
COOP_FIX("social.meter_fill", "BUG-9230", g_meter_fill_fix, "creature: a guest's social meter fills its bars as the impressing goes on (both bars, the native look)");
static uint32_t g_meter_fills = 0;
// the meter is on screen for this NPC's social game, with its fill (then the feed lines are not needed)
static bool meter_shows(int npc_id) { return g_meter_fill_fix && g_meter_npc == npc_id && g_meter_npc >= 0; }
static void meter_fill(void* m, float frac)
{
    uint8_t* u = (uint8_t*)m;
    if (IsBadWritePtr(u + 0x134, 0xB0)) return;
    const float a = *(float*)(u + 0x3C), b = *(float*)(u + 0x40);
    if (!(b > a) || !std::isfinite(a) || !std::isfinite(b)) return;
    const float len = (b - a) / 2, to = std::max(0.f, std::min(1.f, frac)) * len;
    // what is drawn: each bar's list of finished steps (vector begin / end / capacity at +0xA4 / +0xA8 / +0xAC + i * 0xA4,
    // its own storage for 6 at +0xBC + i * 0xA4; entries of 0x14 bytes {the step's end on the arc, 1.0, -FLT_MAX, 0, 0.01},
    // read from a native meter: bar 0 ends at +0x3C + its length so far, bar 1 at +0x40 - it, the last at pi / 2 where they
    // meet). The guest's list was empty: nothing drawn. Rebuilt for the rounds done (rounds at +0x44).
    const int rounds = *(int*)(u + 0x44);
    const float f01 = std::max(0.f, std::min(1.f, frac));
    // BUG-9305: the round being filled has its entry from its start, as StartFill pushes it (else nothing is drawn
    // until half the round); off: rounded (the entry at half the round)
    const int done = rounds <= 0 ? 0
                     : smooth()  ? std::min(rounds, (int)std::ceil(f01 * rounds - 1e-3f))
                                 : std::min(rounds, (int)std::lround(f01 * rounds));
    for (int i = 0; i < 2; i++) {
        uint8_t* v = u + 0xA4 + i * 0xA4;
        uint8_t* own = u + 0xBC + i * 0xA4;
        if (*(uint8_t**)v != own || done > 6) continue; // (not its own storage: left alone)
        for (int k = 0; k < done; k++) {
            float* e = (float*)(own + k * 0x14);
            const float at = len * (k + 1) / rounds;
            e[0] = i == 0 ? a + at : b - at;
            e[1] = 1.0f;
            e[2] = -3.40282347e38f; // -FLT_MAX
            e[3] = 0.0f;
            e[4] = 0.01f;
        }
        *(uint8_t**)(v + 4) = own + done * 0x14;
    }
    for (int i = 0; i < 2; i++) {
        float* lo = (float*)(u + 0x134 + i * 0xA4);
        lo[0] = lo[1]; // the step starts where the bar stands
        lo[2] = to;
        thiscall<void>(a_MeterBar, m, i, 1.0f); // pos = hi (+0x24 set: "redraw")
    }
    // the redraw: the native meter tick (Steam 0xD237CB) does this when +0x24 is set; a guest has no session to tick it.
    // FindWindowByID(meter's layout +0xC, the bars' window 0x562BD40, recursive) and that window's vt 0x90
    static uintptr_t a_find = H->sym("UTFWin::UILayout::FindWindowByID");
    if (a_find)
        if (void* w = thiscall<void*>(a_find, u + 0xC, 0x562BD40u, 1))
            skg::vcall<void>(w, 0x90 / 4);
    g_meter_fills++;
}

static void meter_run(void*) // outside the frame: the meter is UI
{
    auto v = std::move(g_meter_tasks);
    g_meter_tasks.clear();
    void* m = meter_ui();
    if (g_meter_test_null && m) { // tests (BUG-1058): as if the meter window were not there yet
        g_meter_test_null--;
        m = nullptr;
    }
    if (!m && !v.empty()) {
        g_meter_missed++;
        g_meter_missed_at = H->time_seconds();
    }
    for (auto& t : v) {
        void* px = t.npc >= 0 ? npc::proxy((uint16_t)t.npc) : nullptr;
        if (!m || !a_MeterVisible)
            break;
        if (t.op == 0 && px && a_MeterReset && a_MeterRounds && a_MeterFace) {
            thiscall<void>(a_MeterReset, m);
            thiscall<void>(a_MeterVisible, m, 0);
            thiscall<void>(a_MeterRounds, m, t.rounds);
            thiscall<void>(a_MeterFace, m, relation_to(px));
            thiscall<void>(a_MeterVisible, m, 1);
            g_meter_npc = t.npc;
            g_meter_rounds = t.rounds;
            g_meter_frac = 0;
            g_meter_goal = 0;
            g_glide_from = 0;
            g_win_npc = -1;
            g_meter_mode = social_mode(px);
            g_meter_opened++;
        } else if (t.op == 1 && t.npc == g_meter_npc && a_MeterBar) {
            if (g_meter_fill_fix) meter_fill(m, t.frac); // BUG-9230
            else thiscall<void>(a_MeterBar, m, 0, t.frac);
            g_meter_frac = t.frac;
            g_meter_bars++;
        } else if (t.op == 2 && t.npc == g_meter_npc) {
            g_meter_vis_at_win = meter_visible(m);
            if (a_MeterBar && g_meter_fill_fix) meter_fill(m, 1.0f); // BUG-9230: both bars meet
            else if (a_MeterBar)
                thiscall<void>(a_MeterBar, m, 0, 1.0f);
            g_meter_frac = 1;
            if (a_MeterFinish)
                thiscall<void>(a_MeterFinish, m);
            if (px && a_MeterFace)
                thiscall<void>(a_MeterFace, m, relation_to(px));
            // the win's message, as CreatureSocialSuccess posts it ({avatar, the creature, 0}): the
            // handler rolls the species' part reward (coop.cpp h_StageMsg gives a proxy a blank +0xE84)
            void* av = game::avatar();
            const bool recruit = g_recruit_fix && g_meter_mode == 4; // 7L: the native goal-4 success posts no message
            if (px && av && a_StrategyGet && a_ExecuteAction && !recruit)
                if (void* st = cdecl_call<void*>(a_StrategyGet)) {
                    void* msg[3] = {av, px, nullptr};
                    thiscall<void>(a_ExecuteAction, st, 0x60B4123u, (void*)msg);
                    g_win_msgs++;
                }
        } else if (t.op == 3) {
            thiscall<void>(a_MeterVisible, m, 0);
            if (g_meter_npc == t.npc)
                g_meter_npc = -1;
            g_meter_closed++;
        }
    }
}

static void meter_task(int op, int npc_id, float frac = 0, int rounds = 1)
{
    if (!g_meter_fix)
        return;
    if (g_meter_tasks.empty())
        H->run_outside_frame(meter_run, nullptr);
    g_meter_tasks.push_back({op, npc_id, frac, rounds});
}

// BUG-0808: a guest that left (or lost the host) in the middle of a social game kept the game's meter (face and bar) on
// its screen in its single-player game: meter_tick closes it, and it runs only in a session. Now on_session_end queues
// the close (outside the frame). `coop.fix social.meter_end off`: left open (0.7.11).
static bool g_meter_end_fix = true;
COOP_FIX("social.meter_end", "BUG-0808", g_meter_end_fix, "creature: the social meter of a guest's open social game is hidden when the session ends");

// guest, every frame: the meter follows the engagement
static void meter_tick()
{
    if (!g_meter_fix)
        return;
    const double now = H->time_seconds();
    // BUG-9305: the win waits for the fill to reach the end, then the full meter stays a moment before the close
    if (g_win_npc >= 0 && g_win_npc != g_meter_npc)
        g_win_npc = -1;
    if (g_win_npc >= 0 && g_meter_tasks.empty() && (g_meter_frac >= 0.999f || now - g_win_at > kWinWait)) {
        meter_task(2, g_win_npc);
        g_win_npc = -1;
        g_hold_until = now + kWinHold;
    }
    if (smooth() && g_meter_npc >= 0 && g_meter_tasks.empty()) { // BUG-9305: linear, every frame
        const float to = glide_pos(now);
        if (to > g_meter_frac + 0.001f)
            meter_task(1, g_meter_npc, to);
    }
    // BUG-9258: the bar glides towards the host's count, a step every ~30 ms
    if (!smooth() && g_meter_glide && g_meter_npc >= 0 && g_meter_goal > g_meter_frac + 0.002f && g_meter_tasks.empty()) {
        if (now - g_meter_step_at >= 0.03) {
            g_meter_step_at = now;
            const float step = std::max(0.02f, (g_meter_goal - g_meter_frac) * 0.2f);
            meter_task(1, g_meter_npc, std::min(g_meter_goal, g_meter_frac + step));
        }
    }
    if (g_engaged >= 0 && g_engaged != g_meter_npc && g_meter_tasks.empty() && g_win_npc < 0) {
        void* px = npc::proxy((uint16_t)g_engaged);
        if (g_meter_retry_fix && H->time_seconds() - g_meter_missed_at < kMeterRetry)
            return; // (BUG-1058: a missed open, tried again shortly)
        if (px && open_to_me(px))
            meter_task(0, g_engaged, 0, rounds_needed(px));
        else
            return;
        if (!g_meter_retry_fix)
            g_meter_npc = g_engaged; // (0.7.13: marked before the task opened it)
    } else if (g_engaged < 0 && g_meter_npc >= 0 && g_meter_tasks.empty() && g_win_npc < 0 &&
               !(smooth() && now < g_hold_until)) {
        meter_task(3, g_meter_npc);
    }
}

void debug_meter_null(unsigned n) { g_meter_test_null = n; } // tests (BUG-1058)

// tests (BUG-9230): `coop.social meterfill <done> <rounds> [close]`: the meter opened (as op 0 does) and filled to done / rounds
static int g_test_done, g_test_rounds, g_test_close;
static void meter_test_run(void*)
{
    void* m = meter_ui();
    if (!m || !a_MeterVisible) return;
    if (g_test_close) { thiscall<void>(a_MeterVisible, m, 0); return; }
    if (a_MeterReset && a_MeterRounds) {
        thiscall<void>(a_MeterReset, m);
        thiscall<void>(a_MeterRounds, m, g_test_rounds);
        thiscall<void>(a_MeterVisible, m, 1);
    }
    if (a_MeterBar && g_test_done > 0) { // as op 1 does (the switch off: 62-dev's one SetBar)
        if (g_meter_fill_fix) meter_fill(m, (float)g_test_done / g_test_rounds);
        else thiscall<void>(a_MeterBar, m, 0, (float)g_test_done / g_test_rounds);
    }
}
void debug_meter_fill(int done, int rounds, bool close)
{
    g_test_done = done; g_test_rounds = std::max(1, rounds); g_test_close = close;
    H->run_outside_frame(meter_test_run, nullptr);
}

// tests (BUG-9244): every species goal card (noun 0x1406A572): its species (+0x80), state (+0x7C), cached impressed / killed
// (+0x8C / +0x90), cached relation (+0xA0) and the table's record now
std::string cards_dump()
{
    std::string out;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun != kSpeciesGoalNoun || !game::object_alive(o) || IsBadReadPtr(o, 0xC0)) return true;
            auto& s = *(std::string*)u;
            uint8_t* g = (uint8_t*)o;
            char b[260];
            uint32_t inst = *(uint32_t*)(g + 0x80);
            int rel = -1, imp = -1, kil = -1;
            if (void* mgr = rel_mgr(); mgr && a_RelRecord) {
                ResourceKey k{inst, 0x2B978C46u, 0x40626200u};
                if (uint32_t* rec = thiscall<uint32_t*>(a_RelRecord, (uint8_t*)mgr + 0x20, &k)) { rel = (int)rec[0]; imp = (int)rec[2]; kil = (int)rec[3]; }
            }
            snprintf(b, sizeof b, "card %p species=%08X state=%d cached imp=%d killed=%d rel=%d | table rel=%d imp=%d killed=%d | +78=%08X +84=%08X +88=%08X\n",
                     o, inst, *(int*)(g + 0x7C), *(int*)(g + 0x8C), *(int*)(g + 0x90), *(int*)(g + 0xA0), rel, imp, kil,
                     *(uint32_t*)(g + 0x78), *(uint32_t*)(g + 0x84), *(uint32_t*)(g + 0x88));
            s += b;
            return true;
        },
        &out);
    return out.empty() ? "no cards\n" : out;
}

std::string meter_dump() // tests: the game's meter object as the guest's screen has it
{
    void* m = meter_ui();
    char b[760];
    if (!m)
        return "meter: none\n";
    uint8_t* u = (uint8_t*)m;
    auto entries = [u](int i) { // BUG-9305: the bar's list of steps (what is drawn), entries of 0x14 bytes
        uint8_t* s = *(uint8_t**)(u + 0xA4 + i * 0xA4);
        uint8_t* e = *(uint8_t**)(u + 0xA8 + i * 0xA4);
        return s && e >= s ? (int)((e - s) / 0x14) : -1;
    };
    // bar i: lo +0x90+(i+1)*0xA4, pos +0x94+.., hi +0x98+.. (SetBar 0xE31990)
    float lo = *(float*)(u + 0x134), pos = *(float*)(u + 0x138), hi = *(float*)(u + 0x13C);
    float frac = hi != lo ? (pos - lo) / (hi - lo) : 0;
    snprintf(b, sizeof b,
             "meter: ui=%p visible=%d visible_at_win=%d raw=%.3f/%.3f/%.3f bar1=%.3f/%.3f/%.3f fills=%u full=%d rounds=%d bar0=%.2f open_for=%d opened=%u bars=%u closed=%u win_msgs=%u stage_win_msgs=%u "
             "frac_set=%.2f missed=%u entries=%d/%d goal=%.2f glide_from=%.2f win_wait=%d wins_deferred=%u mode=%d smooth=%d\n",
             m, meter_visible(m), g_meter_vis_at_win, lo, pos, hi, *(float*)(u + 0x1D8), *(float*)(u + 0x1DC), *(float*)(u + 0x1E0),
             g_meter_fills, (int)u[0x26], *(int*)(u + 0x44), frac, g_meter_npc, g_meter_opened, g_meter_bars, g_meter_closed, g_win_msgs,
             g_stage_msg_win, g_meter_frac, g_meter_missed, entries(0), entries(1), g_meter_goal, g_glide_from, g_win_npc,
             g_wins_deferred, g_meter_mode, (int)smooth());
    return b;
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
    meter_tick();
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
    // BUG-0713 (class BUG-0429): a digit the game window took from its queue (mouse.h); a tap between two polls is
    // not missed and a digit typed in another program never counts (input.window off: the polling above)
    static uint32_t s_seen[4] = {mouse::key_presses('1'), mouse::key_presses('2'), mouse::key_presses('3'),
                                 mouse::key_presses('4')};
    for (int k = 0; k < 4; k++) {
        bool press = mouse::key_pressed('1' + k, s_seen[k]);
        if (mouse::g_window_input) {
            if (press && on && now - g_last_act >= 0.5)
                act(k);
            continue;
        }
        SHORT st = GetAsyncKeyState('1' + k); // bit 0: pressed since the last call (a short tap)
        bool down = (st & mask) != 0;
        if (down && !g_keys[k] && on && now - g_last_act >= 0.5) // (the bar's hook may have sent it)
            act(k);
        g_keys[k] = down;
    }
}

// ------------------------------------------------------------------ host: the guests' games
struct Prompt {
    uint32_t type, key; // the ability's +8 and +0x10 (what an answer must match)
    float points;       // +0x3C, rounded as SocialPropose sums them
    uint32_t anim;      // the ability's first animation (cCreatureAbility::GetAnimationID(0))
};
struct Game {
    int rounds = 0;
    double last = 0;
    // BUG-0525: the prompt list of this game
    std::vector<Prompt> prompts;
    int idx = -1;          // the prompt open now (-1: before the first)
    bool answered = true;  // the open prompt got its answer (or none is open)
    double prompt_at = 0, next_at = 0;
    float score = 0, needed = 0;
    bool recruit = false;  // decision 7L: a recruit game on an impressed creature (it stays open)
};
static std::map<std::pair<uint8_t, uint16_t>, Game> g_games;
static uint32_t g_acts_in = 0, g_impressed = 0, g_refused = 0;
// BUG-0831 (class BUG-0422): a guest that left in the middle of a social game left the host's NPC performing its prompts
// and, after the prompt list, the fail reaction (up to 60 s); a newcomer under the same id got that game's lines and
// result. The games of a player who leaves or joins are dropped (player_life). `coop.fix social.forget_leaver off`: kept.
static bool g_forget_leaver = true;
COOP_FIX("social.forget_leaver", "BUG-0831", g_forget_leaver, "creature: the host's NPC stops a social game whose guest left (a newcomer of that id inherits nothing)");
static uint32_t g_games_forgotten = 0;
static size_t games_of(uint8_t id)
{
    size_t n = 0;
    for (auto& [k, g] : g_games)
        n += k.first == id;
    return n;
}
PLAYER_STATE("social.games", [](uint8_t id) {
    if (!g_forget_leaver)
        return;
    for (auto it = g_games.begin(); it != g_games.end();)
        if (it->first.first == id) {
            it = g_games.erase(it);
            g_games_forgotten++;
        } else
            ++it;
}, [](uint8_t id) -> size_t { return games_of(id); });

std::string games_dump()
{
    std::string out;
    char b[96];
    for (auto& [k, g] : g_games) {
        snprintf(b, sizeof b, "game player=%u npc=%u idx=%d prompts=", k.first, k.second, g.idx);
        out += b;
        for (auto& p : g.prompts) {
            snprintf(b, sizeof b, "%08X,", p.anim);
            out += b;
        }
        out += "\n";
    }
    snprintf(b, sizeof b, "fail_anim=%08X games=%u forgotten=%u\n", kFailAnim, (unsigned)g_games.size(), g_games_forgotten);
    return out + b;
}


struct Pending { // applied outside the frame (the result refreshes the creature UI)
    uint8_t player;
    uint16_t npc;
};
static std::vector<Pending> g_pending;
// BUG-1055: a success of the ended session is not paid out after it (apply_success outside the frame)
SESSION_STATE("social.pending", nullptr, [] { g_pending.clear(); }, []() -> size_t { return g_pending.size(); });

static void send_progress(uint8_t to, uint16_t id, int done, int needed)
{
    net::Writer w;
    w.u8(K_PROGRESS);
    w.u16(id);
    w.u8((uint8_t)done);
    w.u8((uint8_t)needed);
    C.sess->send_reliable_to(to, net::MSG_SOCIAL, w);
}

// BUG-9258 (BigTexas, 2026-11-18: "It does not want to socialize" while impressing): every "no" the host sends a guest,
// with its reason, in the host's log (40 lines a session at most)
static uint32_t g_no_logged = 0;
static void log_no(const char* why, uint8_t player, uint16_t id, void* c, void* pup)
{
    if (g_no_logged >= 40)
        return;
    g_no_logged++;
    const float d = c && pup ? net::dist(game::position(c), game::position(pup)) : -1.f;
    H->log(M, SK_INFO, "social: player %u's social game with npc %u ends: %s (social mode %d, dead %d, impressed %d, "
           "its creature %.1f m from the npc here)", player, id, why, c ? social_mode(c) : -1, c ? (int)game::is_dead(c) : -1,
           c ? (int)game::impressed(c) : -1, d);
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
    if (!c || !mgr || !a_RelEvent || game::is_dead(c) || closed_to(player, c)) {
        g_refused++; // gone, died or impressed by somebody else meanwhile
        log_no("won, but the npc is gone, dead or impressed already", player, id, c, C.puppet_of ? C.puppet_of(player) : nullptr);
        send_result(id, player, false, 0, 0);
        return;
    }
    void* av = game::avatar();
    void* key = (uint8_t*)c + game::kSpeciesKeyOff;
    int rel = a_RelGet ? thiscall<int>(a_RelGet, mgr, key, c, av) : 0;
    int mode = social_mode(c);
    // decision 7L: the goal as the partner's own game has it (the host's own avatar for the host's games)
    const bool partner = g_recruit_fix && C.sess && player != C.sess->my_id();
    if (partner)
        mode = partner_mode(player, c);
    uint32_t anim = reaction_anim(rel);
    thiscall<void>(a_RelEvent, mgr, 3, key, c);
    float dna = 0;
    if (mode != 4 && a_DnaWorth) {
        dna = thiscall<float>(a_DnaWorth, c, 1);
        if (dna > 0 && dna < 10000 && C.pace)
            dna = C.pace(dna); // BUG-0510: the party's pace (a guest's friend, as the host's own are in on_dna)
        if (dna > 0 && dna < 10000)
            dna = progress_debt::absorb(dna); // BUG-9119: a progress debt is repaid first
        if (dna > 0 && dna < 10000)
            game::dna_add_raw(dna); // the host's pool, shared with everybody (MSG_DNA)
    }
    game::set_impressed(c, true);
    if (a_PlayReaction)
        thiscall<int>(a_PlayReaction, c, anim, 1, -1);
    g_impressed++;
    // decision 7L: a won recruit game (goal 4) puts the creature into the partner's pack, as the native session end does
    if (partner && mode == 4) {
        const bool ok = guest_pack::recruit(player, c);
        ok ? g_recruits++ : g_recruits_refused++;
        H->log(M, SK_INFO, "social: player %u won a recruit game (social goal 4) with npc %u: %s (decision 7L)", player, id,
               ok ? "it joins that player's pack" : "the pack refused it (full, in a pack already, or no party pack)");
    }
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

static uint32_t ability_anim(void* a) // cCreatureAbility::GetAnimationID(0) 0x4D3D30: +0x48 vector, default 0x4330667
{
    uint32_t* b = field<uint32_t*>(a, 0x48);
    uint32_t* e = field<uint32_t*>(a, 0x4C);
    return b && e > b && !IsBadReadPtr(b, 4) ? *b : 0x4330667u;
}

// the prompts as SocialPropose picks them: rounds (rounds_needed) abilities of the NPC drawn from its species
// profile's candidates (+0x7A4, ability indices); empty when the species has none
static std::vector<Prompt> pick_prompts(void* c)
{
    std::vector<Prompt> v;
    uint8_t* sp = (uint8_t*)game::species_profile(c);
    if (!sp || IsBadReadPtr(sp + 0x7A4, 8))
        return v;
    int* p0 = *(int**)(sp + 0x7A4);
    int* p1 = *(int**)(sp + 0x7A8);
    int nc = p0 && p1 > p0 ? (int)(p1 - p0) : 0;
    if (nc <= 0 || nc > 64 || IsBadReadPtr(p0, nc * 4))
        return v;
    int n = rounds_needed(c);
    for (int i = 0; i < n; i++) {
        int ai = p0[rand() % nc];
        uint8_t* a = ai >= 0 && ai < ability_count(c) ? (uint8_t*)ability(c, ai) : nullptr;
        if (!a || IsBadReadPtr(a, 0x50))
            continue;
        v.push_back({field<uint32_t>(a, 8), field<uint32_t>(a, 0x10), std::max(1.f, std::round(field<float>(a, 0x3C))),
                     ability_anim(a)});
    }
    return v;
}

static void send_prompt(uint8_t to, uint16_t id, int i, int n, const Prompt& p)
{
    net::Writer w;
    w.u8(K_PROMPT);
    w.u16(id);
    w.u8((uint8_t)i);
    w.u8((uint8_t)n);
    w.u32(p.type);
    w.u32(p.key);
    w.u32(p.anim);
    C.sess->send_reliable_to(to, net::MSG_SOCIAL, w);
    g_prompts_sent++;
}

static void fail_game(uint8_t player, uint16_t id)
{
    log_no("the prompt list is through and the meter not full", player, id, npc::host_npc(id),
           C.puppet_of ? C.puppet_of(player) : nullptr);
    if (void* c = npc::host_npc(id); c && a_PlayReaction && !game::is_dead(c))
        thiscall<int>(a_PlayReaction, c, kFailAnim, 1, -1);
    g_prompt_fails++;
    send_result(id, player, false, kFailAnim, 0);
}

// host: the guest engaged NPC #id (K_ENGAGE): a new game with its prompt list
static void host_engage(uint8_t from, uint16_t id)
{
    void* c = npc::host_npc(id);
    if (!g_prompt_fix || !c || game::is_dead(c) || closed_to(from, c) || mode_for(from, c) == 0) {
        if (g_prompt_fix)
            log_no("an engagement the host does not start", from, id, c, C.puppet_of ? C.puppet_of(from) : nullptr);
        return;
    }
    double now = H->time_seconds();
    Game& g = g_games[{from, id}];
    if (!g.prompts.empty() && now - g.last < kEngageTime && g.idx < (int)g.prompts.size())
        return; // a game with it is on
    g = Game{};
    g.recruit = game::impressed(c); // (closed_to let it through: a recruit game, 7L)
    g.prompts = pick_prompts(c);
    g.last = now;
    g.next_at = now + kPromptGreet;
    for (auto& p : g.prompts)
        g.needed += p.points;
    if (g.prompts.empty())
        g_games.erase({from, id}); // no candidates: the old way (any social ability is a round)
}

// host, every frame: the guests' games with prompts go on (next prompt, a prompt that waited too long, the end)
static void host_prompt_tick(double now)
{
    for (auto it = g_games.begin(); it != g_games.end();) {
        Game& g = it->second;
        uint8_t from = it->first.first;
        uint16_t id = it->first.second;
        void* c = npc::host_npc(id);
        if (g.prompts.empty()) {
            ++it;
            continue;
        }
        if (!c || game::is_dead(c) || (game::impressed(c) && !g.recruit) || now - g.last > kEngageTime) {
            it = g_games.erase(it);
            continue;
        }
        bool open = g.idx >= 0 && !g.answered;
        if (open && now - g.prompt_at > kPromptWait) { // not answered in time: the prompt is lost
            g.answered = true;
            g.next_at = now;
            open = false;
        }
        if (!open && now >= g.next_at) {
            if (g.idx + 1 >= (int)g.prompts.size()) { // the list is through and the meter not full
                fail_game(from, id);
                it = g_games.erase(it);
                continue;
            }
            g.idx++;
            g.answered = false;
            g.prompt_at = now;
            const Prompt& p = g.prompts[g.idx];
            if (a_PlayReaction) // the NPC performs it (npc.anims shows it on the guest's mirror)
                thiscall<int>(a_PlayReaction, c, p.anim, 1, -1);
            send_prompt(from, id, g.idx, (int)g.prompts.size(), p);
        }
        ++it;
    }
}

static void host_act(uint8_t from, uint16_t id, uint32_t type)
{
    g_acts_in++;
    void* c = npc::host_npc(id);
    void* pup = C.puppet_of ? C.puppet_of(from) : nullptr;
    if (!c || game::is_dead(c) || closed_to(from, c) || mode_for(from, c) == 0 ||
        (pup && net::dist(game::position(pup), game::position(c)) > kActRange + 4)) {
        g_refused++;
        log_no(!c ? "an act on an npc this game does not have" : game::is_dead(c) ? "an act on a dead npc"
               : closed_to(from, c) ? "an act on an npc impressed already" : mode_for(from, c) == 0 ? "an act while the npc does not socialize"
               : "an act from too far", from, id, c, pup);
        send_result(id, from, false, 0, 0);
        return;
    }
    int pi = pup ? ability_by_type(pup, type) : -1;
    if (pup) // the host sees the guest perform it
        play_ability(pup, pi);
    double now = H->time_seconds();
    auto gi = g_games.find({from, id});
    if (g_prompt_fix && gi != g_games.end() && !gi->second.prompts.empty()) { // BUG-0525: an answer to a prompt
        Game& g = gi->second;
        g.last = now;
        if (g.idx < 0 || g.answered) {
            g_answers_early++; // no prompt open: the native session takes no answer then
            return;
        }
        const Prompt& p = g.prompts[g.idx];
        uint8_t* a = pi >= 0 ? (uint8_t*)ability(pup, pi) : nullptr;
        uint32_t key = a ? field<uint32_t>(a, 0x10) : (type == p.type ? p.key : 0xFFFFFFFFu);
        float pts = a ? std::max(1.f, field<float>(a, 0x3C)) : 1.f;
        bool match = key == p.key;
        g.score += match ? pts : 0.1f * pts; // 0xD9B070: another ability gives 0.1 (0x1582F1C) of its points
        g.answered = true;
        g.next_at = now + kPromptNext;
        g_answers++;
        if (!match)
            g_answers_wrong++;
        static unsigned logged = 0; // BUG-9297: each answer against its prompt (60 lines a game run)
        if (logged < 60) {
            logged++;
            H->log(M, SK_INFO, "social: player %u answers npc %u: type %08X kind %u to prompt %d (type %08X kind %u): %s, "
                   "score %.1f of %.1f", from, id, type, key, g.idx, p.type, p.key, match ? "match" : "another kind",
                   g.score, g.needed);
        }
        int needed = std::max(1, (int)std::lround(g.needed * 10));
        int done = std::min(needed, (int)std::lround(g.score * 10));
        send_progress(from, id, std::min(done, 255), std::min(needed, 255));
        if (g.score + 1e-3f < g.needed)
            return;
        g_games.erase(gi);
        if (g_pending.empty())
            H->run_outside_frame(pending_task, nullptr);
        g_pending.push_back({from, id});
        return;
    }
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
    a_StrategyGet = H->sym("App::cCreatureModeStrategy::Get");
    a_ExecuteAction = H->sym("App::cCreatureModeStrategy::ExecuteAction");
    a_MeterReset = H->sym("SporeCoop::SocialMeter_Reset"); // BUG-0179
    a_MeterVisible = H->sym("SporeCoop::SocialMeter_SetVisible");
    a_MeterRounds = H->sym("SporeCoop::SocialMeter_SetRounds");
    a_MeterFace = H->sym("SporeCoop::SocialMeter_SetRelation");
    a_MeterBar = H->sym("SporeCoop::SocialMeter_SetBar");
    a_MeterFinish = H->sym("SporeCoop::SocialMeter_Finish");
    if (uintptr_t a = H->sym("SporeCoop::AbilityBar_Use")) // BUG-0179
        H->hook_create(M, (void*)a, (void*)h_BarUse, (void**)&o_BarUse);
    if (uintptr_t a = H->sym("SporeCoop::BT_PostStimulus"))
        H->hook_create(M, (void*)a, (void*)h_Post, (void**)&o_Post);
    if (a_RelEvent)
        H->hook_create(M, (void*)a_RelEvent, (void*)h_RelEvent, (void**)&o_RelEvent);
    if (uintptr_t a = H->sym("SporeCoop::SpeciesGoal_Update"))
        H->hook_create(M, (void*)a, (void*)h_GoalUpdate, (void**)&o_GoalUpdate);
}

// BUG-9244 (owner 2026-11-16: "The Crikiitis species is extinct!" on the Hunt card, twice, with Crikiitis standing next to
// the player): set_party_ally gives every party species relation 6 (decision 7, best friends), the game's own "goal done"
// (an Impress goal reads it as allied, a Hunt goal as extinct: 0xBA4750 writes 6 for an extinction too). A seat's species
// changes (the player's own creature, an evolution, a guest that first reports the host's species: seat 1 of the TITAN QA
// world went 0654D6B6 -> 066890BD -> 06538C27 -> 7C068C08 in one day) and the old species kept its 6 for good: a Hunt
// goal on it (Bye Mouth 065D0889, an old seat species whose nest lives on) said "extinct". The game itself never leaves
// 6 with no impressions and no kills on a species other than the player's: such a record that is no party species now is
// set back to 0 (not set: the species' archetype decides again; the guests follow the host's table). Off
// (`coop.fix social.stale_party_marks off`): the marks stay (62-dev).
static bool g_stale_marks_fix = true;
COOP_FIX("social.stale_party_marks", "BUG-9244", g_stale_marks_fix,
         "creature: a species that was a player's (best friends, relation 6) is no longer marked once no player plays it (its Hunt card said 'extinct')");
static uint32_t g_stale_marks_cleared = 0;
static void clear_stale_marks()
{
    void* mgr = rel_mgr();
    if (!g_stale_marks_fix || !mgr || !is_host() || !game::in_creature_stage()) return;
    void* av = game::avatar();
    const uint32_t own = av ? game::species_key(av).instance : 0;
    uint8_t* map = (uint8_t*)mgr + 0x20;
    void** buckets = field<void**>(map, 4);
    uint32_t n = field<uint32_t>(map, 8), seen = 0;
    for (uint32_t i = 0; buckets && i < n && i < 4096; i++)
        for (uint32_t* nd = (uint32_t*)buckets[i]; nd && seen < 512; nd = (uint32_t*)nd[8], seen++) {
            if ((int32_t)nd[3] != 6 || nd[5] || nd[6] || !nd[0] || nd[0] == own || party::party_species(nd[0])) continue;
            nd[3] = 0;
            if (++g_stale_marks_cleared <= 40)
                H->log(M, SK_INFO, "social: species %08X kept relation 6 with no impressions or kills and plays for nobody now: set back "
                       "to 0 (BUG-9244, %u)", nd[0], g_stale_marks_cleared);
        }
}

void tick(double now)
{
    if (game::lend_ledger_on()) // BUG-0882: outside every goal update (a fault inside one left it set)
        g_goal = nullptr;
    if (!C.sess || !game::in_creature_stage() || !game::avatar())
        return;
    if (is_host() && !g_goal_owed.empty()) { // BUG-0205: rewards that came while we had no creature
        auto owed = std::move(g_goal_owed);
        g_goal_owed.clear();
        for (auto& o : owed)
            if (goal_outcome(o.k) == o.outcome && host_goal_pay(o.k, o.outcome)) {
                if (float left = progress_debt::absorb(o.dna); left > 0) // BUG-9119: the debt first
                    game::dna_add_raw(left);
                g_goal_owed_paid++;
                if (C.feed)
                    C.feed(std::string(o.outcome == 6 ? "A species became our ally" : "A goal was reached") +
                           ": +" + std::to_string((int)(o.dna + 0.5f)) + " DNA");
            }
    }
    if (is_host())
        host_prompt_tick(now);
    if (is_host()) {
        static double last = 0;
        if (now - last < (g_table_fast_fix ? 0.25 : 1.0))
            return;
        last = now;
        static double stale_at = 0;
        if (now - stale_at > 3.0 && party::roster().seats.size()) { // BUG-9244 (once the roster is known)
            stale_at = now;
            clear_stale_marks();
        }
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
            // BUG-0420: a guest that plays another world keeps its own species table: the host's last table
            // is forgotten (a new one comes with the host's world, MSG_SPECIES is gated while elsewhere)
            if (g_world_fix && C.elsewhere && C.elsewhere()) {
                if (g_have_table)
                    g_table_forgot++;
                g_have_table = false;
                g_host_table.clear();
            } else {
                enforce_table();
                herds_follow_table();
            }
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
        g_table_rx_at = H->time_seconds();
        if (game::in_creature_stage() && game::avatar()) {
            enforce_table();
            herds_follow_table();
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
    if (kind == K_ENGAGE && is_host()) {
        if (r.ok())
            host_engage(from, id);
        return;
    }
    if (kind == K_PROMPT && is_guest()) { // BUG-0525: the NPC's prompt: its mirror performs it
        int i = r.u8(), n = r.u8();
        uint32_t type = r.u32(), key = r.u32(), anim = r.u32();
        (void)type; (void)key;
        if (!r.ok())
            return;
        g_prompts_in++;
        void* px = npc::proxy(id);
        if (px && a_PlayReaction && !game::is_dead(px))
            thiscall<int>(a_PlayReaction, px, anim, 1, -1);
        g_prompt_anim = anim;
        g_prompt_npc = id;
        g_prompt_at = H->time_seconds();
        if (id == g_engaged)
            g_engaged_at = g_prompt_at;
        // (owner 2026-11-16, BUG-9230: "If the impress bar is working, we don't need the notifications anymore"): the feed
        // lines only when no meter shows this social game
        if (C.feed && id == g_engaged && n > 1 && !meter_shows(id))
            C.feed("Answer it: " + std::to_string(i + 1) + "/" + std::to_string(n));
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
        if (sane && !game::avatar()) {
            if (g_goal_owed.size() < 64)
                g_goal_owed.push_back({k, outcome, dna});
            return;
        }
        if (sane && goal_outcome(k) == outcome && game::avatar() && host_goal_pay(k, outcome)) {
            if (float left = progress_debt::absorb(dna); left > 0) // BUG-9119: the debt first
                game::dna_add_raw(left);
            if (C.feed)
                C.feed(std::string(outcome == 6 ? "A species became our ally" : "A goal was reached") + ": +" +
                       std::to_string((int)(dna + 0.5f)) + " DNA");
        }
        return;
    }
    if (kind == K_PROGRESS && is_guest()) {
        int done = r.u8(), needed = r.u8();
        if (r.ok() && needed > 0 && id == g_engaged) {
            const float f = std::min(1.f, (float)done / needed);
            if (smooth() && id == g_meter_npc)
                glide_to(f, H->time_seconds()); // BUG-9305
            else if (g_meter_glide && id == g_meter_npc)
                g_meter_goal = std::max(g_meter_goal, f);
            else
                meter_task(1, id, f);
        }
        if (r.ok() && done < needed && C.feed && !meter_shows(id))
            C.feed("Impressing: " + std::to_string(100 * done / needed) + "%");
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
        } else if (px && anim && a_PlayReaction && !game::is_dead(px)) // BUG-0525: a failed game's animation
            thiscall<int>(a_PlayReaction, px, anim, 1, -1);
        if (mine && !ok)
            H->log(M, SK_INFO, "social: the host ended our social game with npc %u (%s)", id,
                   anim == kFailAnim ? "the meter was not full at the end of the prompts" : "refused: see the host's log");
        // BUG-9258: a lost social game said "It does not want to socialize now" too, the same line as a refusal: the
        // player could not tell "this species won't" from "you missed its moves"
        if (mine && C.feed)
            C.feed(ok ? "Impressed! +" + std::to_string((int)(dna + 0.5f)) + " DNA for the pack"
                 : anim == kFailAnim ? "It lost interest: copy the move it shows with the same ability to fill the bar"
                                     : "It does not want to socialize now");
        if (mine && ok && id == g_meter_npc) {
            if (smooth()) { // BUG-9305: the win when the fill is there
                const double now = H->time_seconds();
                glide_to(1.f, now);
                g_win_npc = id;
                g_win_at = now;
                g_wins_deferred++;
            } else
                meter_task(2, id);
        }
        if (mine && g_engaged == id)
            g_engaged = -1;
    }
}

void on_session_end()
{
    // BUG-0808: the meter of a social game still open is hidden (meter_tick, which closes it, stops with the session)
    if (g_meter_end_fix && g_meter_npc >= 0)
        meter_task(3, g_meter_npc);
    g_engaged = -1;
    g_meter_npc = -1;
    g_win_npc = -1;
    g_goal_paid.clear();
    g_goal_owed.clear();
    g_games.clear();
    g_have_table = false;
    g_host_table.clear();
    g_sent.clear();
}

std::string status()
{
    char b[700];
    if (is_host())
        snprintf(b, sizeof b,
                 "social(host): acts_in=%u impressed=%u refused=%u games=%u species_tx=%u goal_dna_paid=%u "
                 "goal_dna_dropped=%u goal_owed=%u goal_owed_paid=%u prompts_sent=%u answers=%u answers_wrong=%u "
                 "answers_early=%u prompt_fails=%u games_forgotten=%u recruits=%u recruits_refused=%u partner_rel_kept=%u\n",
                 g_acts_in, g_impressed, g_refused, (unsigned)g_games.size(), g_table_tx, g_goal_dna_paid,
                 g_goal_dna_dropped, (unsigned)g_goal_owed.size(), g_goal_owed_paid, g_prompts_sent, g_answers,
                 g_answers_wrong, g_answers_early, g_prompt_fails, g_games_forgotten, g_recruits, g_recruits_refused,
                 g_partner_rel_kept);
    else
        snprintf(b, sizeof b,
                 "social(guest): engaged=%d engages=%u acts_sent=%u results_in=%u species_rx=%u species_fixed=%u "
                 "events_dropped=%u cards_refreshed=%u goal_dna_sent=%u social_hits_dropped=%u bar_acts=%u bar_passed=%u "
                 "prompts_in=%u prompt_npc=%d prompt_anim=%08X prompt_age=%.1f table_forgot=%u\n",
                 g_engaged, g_engages, g_acts_sent, g_results_in, g_table_rx, g_table_fixed, g_events_dropped,
                 g_cards_refreshed, g_goal_dna_dropped, g_social_hits_dropped, g_bar_acts, g_bar_passed, g_prompts_in,
                 g_prompt_npc, g_prompt_anim, H->time_seconds() - g_prompt_at, g_table_forgot);
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
// roadmap phase 2 (owner decision 7): a party species (a player's own creature with its own nest) is best friends with
// the host's: relation 6 in the host's table (the game's own "allied"; the guests follow the host's table)
bool set_party_ally(uint32_t inst)
{
    void* mgr = rel_mgr();
    if (!mgr || !a_RelRecord || !is_host() || !inst)
        return false;
    ResourceKey k{inst, 0x2B978C46u, 0x40626200u};
    uint32_t* rec = thiscall<uint32_t*>(a_RelRecord, (uint8_t*)mgr + 0x20, &k);
    if (!rec)
        return false;
    rec[0] = 6;
    return true;
}

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
    H->run_outside_frame(refresh_cards, nullptr); // (BUG-9244 tests: the cards read the table again)
    return true;
}

bool debug_event(uint32_t inst, int event)
{
    // BUG-0168: the game's own relationship event, as the avatar's egg node calls it (0xD716F0: event 6 when it walks
    // to an egg, 7 when it eats it; creature null, docs/re/creature.md#creature-npc-aggression). Event 7 makes the
    // species hostile (relation 5) through the game's own transition, as eating its egg does.
    void* mgr = rel_mgr();
    if (!mgr || !a_RelEvent || event < 0 || event > 7)
        return false;
    ResourceKey k{inst, 0x2B978C46u, 0x40626200u};
    thiscall<void>(a_RelEvent, mgr, event, &k, (void*)nullptr);
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

// tests (BUG-0525): a creature's abilities as the social game reads them (+8 type, +0x10 the key the native
// session compares, +0x3C points) and its species' prompt candidates (profile +0x7A4: ability indices)
std::string abilities_dump(int id)
{
    void* c = id < 0 ? game::avatar() : is_host() ? npc::host_npc((uint16_t)id) : npc::proxy((uint16_t)id);
    if (!c)
        return "no such creature\n";
    std::string out;
    char b[200];
    int n = ability_count(c);
    for (int i = 0; i < n && i < 64; i++) {
        uint8_t* a = (uint8_t*)ability(c, i);
        if (!a || IsBadReadPtr(a, 0x108))
            continue;
        snprintf(b, sizeof b, "ab %d type=0x%X key=%u points=%.2f energy=%.2f kind=%d range=%.1f anim=%08X\n", i,
                 *(uint32_t*)(a + 8), *(uint32_t*)(a + 0x10), *(float*)(a + 0x3C), *(float*)(a + 0x104), *(int*)(a + 0x7C),
                 *(float*)(a + 0xA8), ability_anim(a)); // (anim: BUG-0891, what the ability plays)
        out += b;
        // the hit's extra effects (0xC20390: kinds at +0xB8..+0xBC, chances at +0xCC; kind 1 = stun for +0x80 s through
        // the victim's vt 0x94, docs/re/creature.md#creature-stun)
        int* k0 = *(int**)(a + 0xB8);
        int* k1 = *(int**)(a + 0xBC);
        float* ch = *(float**)(a + 0xCC);
        if (k0 && k1 > k0 && k1 - k0 < 16 && !IsBadReadPtr(k0, (k1 - k0) * 4)) {
            out += "   fx";
            for (int* q = k0; q < k1; q++) {
                float cv = ch && !IsBadReadPtr(ch + (q - k0), 4) ? ch[q - k0] : -1.f;
                snprintf(b, sizeof b, " %d:%.2f", *q, cv);
                out += b;
            }
            snprintf(b, sizeof b, " stun=%.2f\n", *(float*)(a + 0x80));
            out += b;
        }
    }
    uint8_t* sp = (uint8_t*)game::species_profile(c);
    if (sp && !IsBadReadPtr(sp + 0x7A4, 8)) {
        int* p0 = *(int**)(sp + 0x7A4);
        int* p1 = *(int**)(sp + 0x7A8);
        out += "prompts";
        for (int* q = p0; q && q < p1 && q - p0 < 32 && !IsBadReadPtr(q, 4); q++) {
            snprintf(b, sizeof b, " %d", *q);
            out += b;
        }
        out += "\n";
    }
    return out;
}

// tests (BUG-9308): host: NPC #id's social goal for the host's avatar and for player p (inside p's party), with the
// species relation GetRelationship gives in each
std::string pmode_dump(uint16_t id, uint8_t player)
{
    void* c = is_host() ? npc::host_npc(id) : nullptr;
    void* mgr = rel_mgr();
    if (!c || !mgr || !a_RelGet)
        return "no such npc (host only)\n";
    void* key = (uint8_t*)c + game::kSpeciesKeyOff;
    auto rel = [&] { return thiscall<int>(a_RelGet, mgr, key, game::avatar(), c); };
    const int r0 = rel(), m0 = social_mode(c);
    void* pup = C.puppet_of ? C.puppet_of(player) : nullptr;
    const size_t tok = pup && guest_pack::party_mode() ? guest_pack::party_enter_puppet(pup) : 0;
    void* av_in = game::avatar();
    const int r1 = rel(), m1 = social_mode(c);
    if (tok)
        guest_pack::party_leave_token(tok);
    char b[260];
    snprintf(b, sizeof b, "npc %u: host relation=%d mode=%d | player %u (puppet %p, party %s, avatar there %p) relation=%d mode=%d "
             "| impressed=%d pack=%d partner_mode=%d\n", id, r0, m0, player, pup, tok ? "entered" : "not entered", av_in, r1, m1,
             (int)game::impressed(c), (int)game::pack_member(c), partner_mode(player, c));
    return b;
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

// tests (BUG-0525): the guest answers the engaged mirror #id with its ability of type <type> (any kind: a
// combat ability is an answer of another kind than the prompt's), as the key / bar path sends it
bool debug_answer(uint16_t id, uint32_t type)
{
    void* av = game::avatar();
    if (!is_guest() || !av || !npc::proxy(id))
        return false;
    int idx = ability_by_type(av, type);
    if (idx < 0)
        return false;
    play_ability(av, idx);
    send_act(id, type);
    g_engaged_at = g_last_act = H->time_seconds();
    return true;
}

// tests (BUG-0994): the avatar posts the social stimulus to creature c's AI block through the game's BT_PostStimulus
// (its hooked entry: h_Post runs as for a click on it); then which proxy the guest engaged
std::string debug_post(void* c)
{
    static uintptr_t a = H->sym("SporeCoop::BT_PostStimulus");
    void* av = game::avatar();
    if (!a || !av || !c || !game::creature_listed(c))
        return "no avatar, no creature or no BT_PostStimulus\n";
    void* ai = *(void**)((uint8_t*)c + 0xB4C);
    if (!ai)
        return "the creature has no AI block\n";
    int e0 = g_engaged;
    uint32_t n0 = g_engages, g0 = npc::ai_lookup_gone();
    thiscall<void*>(a, (uint8_t*)ai + 8, kSocialStimulus, 0u, 1.0f, av);
    char b[200];
    snprintf(b, sizeof b, "posted to %p (ai %p): engaged %d -> %d, engages +%u, lookups skipped as gone +%u\n", c, ai, e0,
             g_engaged, g_engages - n0, npc::ai_lookup_gone() - g0);
    return b;
}

bool debug_social(uint16_t id, int slot)
{
    if (slot < 0) { // toggle the stimulus log
        g_log_posts = !g_log_posts;
        return true;
    }
    if (!is_guest() || !npc::proxy(id))
        return false;
    if (id != g_engaged) // as if the avatar had engaged it (a click on it)
        send_engage(id);
    g_engaged = id;
    g_engaged_at = H->time_seconds();
    return act(slot);
}

} // namespace social
