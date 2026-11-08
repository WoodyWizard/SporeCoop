#include "creature_hp.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "net/interp.h"
#include "sporekit_game.hpp"

namespace creature_hp {

static const SkHost* H;
static SkMod* M;
static Ctx C;

// ---------------------------------------------------------------- BUG-0373
// The partner's puppet used to be pinned to its own full health every frame ("puppets mirror the
// remote health, never die locally"): the other player's health bar never moved, whatever hit the
// partner on its own screen ("we don't see each other's HP"). Now the puppet carries the partner's
// health and its max health (GetMaxHitPoints answers the partner's for a puppet), never below 1 % of
// it (a puppet never dies locally; a dead partner's puppet is hidden).
// `coop.fix creature.partner_hp off`: the puppet stays at its own full health (0.7.3.3).
static bool g_partner_hp = true;
COOP_FIX("creature.partner_hp", "BUG-0373", g_partner_hp, "creature: a partner's creature shows the partner's health and max health");

// ---------------------------------------------------------------- BUG-0374
// The host's pack (posse, +0xB58 & 0x100) and family (the avatar's herd) fought a partner: a guest's
// hit on one of them is applied on the host with the guest's puppet as the attacker, and the game's
// TakeDamage makes the victim (and its herd) fight back against the puppet; the damage filter
// forwarded every NPC hit on a puppet to its player, so the pack hurt and killed the guest. Now a
// partner's hit on the host's party is dropped (no friendly fire, as hits between players are), and a
// hit of the host's party on a puppet is not forwarded.
// `coop.fix npc.party_peace off`: both as in 0.7.3.3.
static bool g_party_peace = true;
COOP_FIX("npc.party_peace", "BUG-0374", g_party_peace, "creature: the host's pack and family never fight a partner");

// ---------------------------------------------------------------- BUG-0375
// The base max hit points of a creature (creature vt 0x7C, 0xC04170) add the brain level's health
// (table 0x150C890 by level, 0x4D1310) only for the world's player species (cSpeciesManager +0xA4);
// another species gets the herd's / tuning's value or level 0's. A guest playing its own creature in
// the host's world is such a species in its own game: its max health stayed at level 0's (14 while
// the host had 29 and 44). Its own avatar now gets what the player species gets.
// `coop.fix creature.own_brain_hp off`: level 0's health (0.7.3.3).
static bool g_own_brain_hp = true;
COOP_FIX("creature.own_brain_hp", "BUG-0375", g_own_brain_hp, "creature: an own creature of another species gets the brain level's health");

constexpr uint32_t kAvatarBit = 0x200, kPackBit = 0x100;

typedef float(SK_THISCALL* MaxHp_t)(void*);
static MaxHp_t o_BaseMaxHp, o_GetMaxHp;
static uintptr_t a_SpeciesMgrGet, a_PlayerSpecies, a_LevelHealth;

struct PuppetHp {
    float hp = 0, max = 0;
};
static std::unordered_map<void*, PuppetHp> g_puppets;
static uint32_t g_partner_hits_dropped, g_party_hits_dropped, g_puppet_sets;

static void* player_species()
{
    void* mgr = a_SpeciesMgrGet && a_PlayerSpecies ? skg::cdecl_call<void*>(a_SpeciesMgrGet) : nullptr;
    return mgr ? skg::thiscall<void*>(a_PlayerSpecies, mgr) : nullptr;
}

static int brain_level(void* c) { return skg::vcall<int>(c, 0xD8 / 4); } // 0xC02A60: [0x169E370] for the avatar / pack

// the player species' rule for our own avatar: LevelHealth(level) + species health (+0x56C)
static bool own_base(void* c, float& out)
{
    if (!g_own_brain_hp || !a_LevelHealth || !c || !game::in_creature_stage())
        return false;
    uint32_t flags = skg::field<uint32_t>(c, game::kFlagsOff);
    if (!(flags & kAvatarBit))
        return false;
    void* sp = skg::field<void*>(c, game::kSpeciesOff);
    void* ps = player_species();
    if (!sp || !ps || sp == ps)
        return false; // the world's species: the game adds the level itself
    out = skg::cdecl_call<float>(a_LevelHealth, brain_level(c)) + skg::field<float>(sp, 0x56C);
    return true;
}

static float SK_THISCALL h_BaseMaxHp(void* c)
{
    float v;
    if (own_base(c, v))
        return v;
    return o_BaseMaxHp(c);
}

// ---------------------------------------------------------------- BUG-0732 (class BUG-0418)
// A creature's size is creature vt 0x80 (0xC04B50, thiscall(creature, bool) -> float, applied with SetScale by the
// callers: the brain upgrade 0xD3FCA0 gives the avatar SetScale(vt 0x80(0))): the avatar normalizing scale
// (0xD2E800 / 0xD2E720 from cSpeciesManager PlayerSpecies +0x55C) and the brain factor (0xC0B5A0: 1.0 .. 1.5 by level)
// only for a creature of the world's player species (PlayerSpecies 0x4DF490). A guest playing its own creature is
// another species in its own game: it never grew with its brain upgrades (docs/re/creature.md#creature-player-species-callers).
// Now vt 0x80 of the own avatar of another species runs with PlayerSpecies answering the avatar's own species (the
// game's rule with its own species), and the normalizing global the call recomputed is put back (the world's species
// keep theirs). `coop.fix creature.own_scale off`: the size of a stranger (0.7.10).
static bool g_own_scale = true;
COOP_FIX("creature.own_scale", "BUG-0732", g_own_scale, "creature: an own creature of another species grows with its brain upgrades as the world's species does");
typedef float(SK_THISCALL* Scale_t)(void*, uint32_t);
typedef void*(SK_THISCALL* PlayerSpecies_t)(void*);
static Scale_t o_Scale;
static PlayerSpecies_t o_PlayerSpecies;
static void* g_ps_override; // (main thread: vt 0x80 of the own avatar is running)
static float* g_norm_global;
static uint32_t g_own_scales;

static void* SK_THISCALL h_PlayerSpecies(void* mgr)
{
    if (g_ps_override)
        return g_ps_override;
    return o_PlayerSpecies(mgr);
}

static float SK_THISCALL h_Scale(void* c, uint32_t arg)
{
    if (!g_own_scale || !o_PlayerSpecies || g_ps_override || !c || !game::in_creature_stage() ||
        !(skg::field<uint32_t>(c, game::kFlagsOff) & kAvatarBit))
        return o_Scale(c, arg);
    void* sp = skg::field<void*>(c, game::kSpeciesOff);
    void* mgr = a_SpeciesMgrGet ? skg::cdecl_call<void*>(a_SpeciesMgrGet) : nullptr;
    void* ps = mgr ? o_PlayerSpecies(mgr) : nullptr;
    if (!sp || !ps || sp == ps)
        return o_Scale(c, arg);
    float keep = g_norm_global ? *g_norm_global : 0.f;
    g_ps_override = sp;
    float r = o_Scale(c, arg);
    g_ps_override = nullptr;
    if (g_norm_global)
        *g_norm_global = keep;
    g_own_scales++;
    return r;
}

static float SK_THISCALL h_GetMaxHp(void* combatant)
{
    if (g_partner_hp && !g_puppets.empty()) {
        void* c = (uint8_t*)combatant - game::kCombatOff;
        auto it = g_puppets.find(c);
        if (it != g_puppets.end() && it->second.max > 0 && game::is_puppet(c))
            return it->second.max;
    }
    return o_GetMaxHp(combatant);
}

// ---------------------------------------------------------------- BUG-0462
// The game asks "is this creature of the player's species" through creature vt 0xD0 (0xC0C430: +0xB20 ==
// cSpeciesManager PlayerSpecies, the world's species). GetRelationship 0xBA40E0 takes the species record or
// the NPC's archetype tuning (+0xE84 +0x390) only when one of the two creatures is of the player species;
// otherwise it falls back to a diet / size rule (0xBA2900). A guest playing its own creature in the host's
// world is another species in its own game: every NPC's relation to it came from that rule (4 instead of the
// tuning's 3 for the fixture's herbivores, 1 "hostile" for others), and CreatureSocialMode (the click, the
// face) differed from the host's. On the host the same happened to the guests' puppets the NPCs' AI looks at
// (npc.cpp ai_target). With creature.relations every player's species counts as the player species: the own
// avatar's and the partners' puppets' (refreshed every frame in tick()).
// `coop.fix creature.relations off`: the old rule (0.7.3.4).
static bool g_relations_fix = true;
COOP_FIX("creature.relations", "BUG-0462", g_relations_fix, "creature: animals have the same relation (neutral, hostile, allied) to every player as to the host");

typedef bool(SK_THISCALL* IsPlayerSpecies_t)(void*);
static IsPlayerSpecies_t o_IsPlayerSpecies;
static void* g_party_species[8];
static int g_party_n = 0;
static uint32_t g_party_answers = 0;

// BUG-0462 follow-up: the hook answered "player species" for the partners' species in every caller of vt 0xD0;
// the family / hatchling / pack / nest code reads the same predicate, and a guest's hatchling no longer stepped
// out of its egg after a death (puppet_jump). Now only the two relation paths get the party answer: calls from
// inside GetRelationship 0xBA40E0 (736 bytes) and CreatureSocialMode 0xC0E800 (522 bytes), which the click, the
// cursor (0xD2FFD0) and the face reach through GetRelationship / CreatureSocialMode. Every other caller gets the
// game's own answer (docs/re/creature.md#creature-relations-scope).
static uintptr_t g_rel_lo, g_rel_hi, g_mode_lo, g_mode_hi;
static uint32_t g_party_other_callers = 0;

static bool SK_THISCALL h_IsPlayerSpecies(void* c)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    bool r = o_IsPlayerSpecies(c);
    if (r || !g_relations_fix || !g_party_n || !c)
        return r;
    if (!((ret >= g_rel_lo && ret < g_rel_hi) || (ret >= g_mode_lo && ret < g_mode_hi))) {
        g_party_other_callers++;
        return r;
    }
    void* sp = *(void**)((uint8_t*)c + game::kSpeciesOff);
    for (int i = 0; i < g_party_n; i++)
        if (sp == g_party_species[i]) {
            g_party_answers++;
            return true;
        }
    return false;
}

void tick()
{
    int n = 0;
    if (g_relations_fix && o_IsPlayerSpecies && game::in_creature_stage()) {
        void* ps = player_species();
        auto add = [&](void* c) {
            void* sp = c && skg::readable(c, 0xB24) ? skg::field<void*>(c, game::kSpeciesOff) : nullptr;
            if (!sp || sp == ps || n >= 8)
                return;
            for (int i = 0; i < n; i++)
                if (g_party_species[i] == sp)
                    return;
            g_party_species[n++] = sp;
        };
        add(game::avatar());
        if (C.puppet_of)
            for (uint8_t id = 0; id < 8; id++)
                if (void* p = C.puppet_of(id))
                    if (game::is_puppet(p))
                        add(p);
    }
    g_party_n = n;
}

bool drive_puppet(void* p, float health, float max_health)
{
    if (!g_partner_hp || !o_GetMaxHp) {
        g_puppets.erase(p);
        return false;
    }
    if (max_health <= 0 || !std::isfinite(max_health) || !std::isfinite(health)) {
        g_puppets.erase(p); // nothing reported (yet): its own full health
        return false;
    }
    auto& e = g_puppets[p];
    e.max = max_health;
    e.hp = std::clamp(health, 0.01f * max_health, max_health);
    game::set_health(p, e.hp);
    g_puppet_sets++;
    return true;
}

void forget_puppet(void* p) { g_puppets.erase(p); }

bool host_party(void* c)
{
    if (!c)
        return false;
    if (skg::field<uint32_t>(c, game::kFlagsOff) & kPackBit)
        return true;
    void* herd = game::herd_of(c);
    return herd && herd == game::avatar_herd();
}

// ---------------------------------------------------------------- BUG-0735 (class BUG-0418)
// The pack defends the avatar in TakeDamage 0xC075E0: a hit on the avatar (+0xB58 & 0x200) posts 0x400 {attacker} on it
// and cPosseSimulator 0xD52E40(0x400, 0) gives every pack member that is not allied with the attacker the attack
// command stimulus 0x10 aimed at it (0xC0C070: clear 0x20030030 / 0x40040000, post 0x10, FLT_MAX, sender the
// attacker; docs/re/creature.md#creature-player-species-callers). A partner's puppet is no avatar: the host's pack (and
// the creatures a guest recruited, who are host pack members) stood by while an NPC fought the guest. Now an NPC's
// hit on a puppet gives the host's pack members within kDefendRange of it the same stimulus.
// `coop.fix npc.pack_defends_partner off`: nobody defends the guest (0.7.10).
static bool g_pack_defends = true;
COOP_FIX("npc.pack_defends_partner", "BUG-0735", g_pack_defends, "creature: the host's pack goes for an animal that attacks a partner, as it does for the host's own creature");
static uintptr_t a_CmdStim, a_CanDefend;
static constexpr float kDefendRange = 40.f;
static uint32_t g_defends, g_defend_posts;
static std::unordered_map<void*, double> g_defend_at; // attacker -> the last time the pack was sent at it

void pack_defend(void* puppet, void* attacker)
{
    if (!g_pack_defends || !a_CmdStim || !a_CanDefend || !puppet || !attacker || attacker == game::avatar() ||
        game::is_puppet(attacker) || host_party(attacker) || !game::in_creature_stage())
        return;
    double now = H->time_seconds();
    auto it = g_defend_at.find(attacker);
    if (it != g_defend_at.end() && now - it->second < 1.5)
        return;
    if (g_defend_at.size() > 64)
        g_defend_at.clear();
    g_defend_at[attacker] = now;
    void* nm = game::noun_manager();
    net::Vec3 at = game::position(puppet);
    unsigned n = 0;
    for (void** m = nm ? skg::field<void**>(nm, 0x5C) : nullptr, **e = nm ? skg::field<void**>(nm, 0x60) : nullptr;
         m && m < e && m - skg::field<void**>(nm, 0x5C) < 32; ++m) {
        void* c = *m;
        if (!c || c == attacker || IsBadReadPtr(c, 0x1680) || game::is_dead(c) || net::dist(game::position(c), at) > kDefendRange)
            continue;
        uint8_t* ai = skg::field<uint8_t*>(c, 0xB4C);
        if (!ai || IsBadReadPtr(ai, 0x200) || !skg::cdecl_call<bool>(a_CanDefend, c))
            continue;
        // as cPosseSimulator 0xD52E40 does for the avatar's attacker (case 0x400)
        skg::thiscall<void*>(a_CmdStim, c, 0x10u, 0u, 0x20030030u, 0x40040000u, 0x7F7FFFFFu, 0x100000u, 0u, 0u, 0u,
                             attacker, (uint32_t)0);
        n++;
    }
    if (n) {
        g_defends++;
        g_defend_posts += n;
    }
}

// ---------------------------------------------------------------- BUG-0734 (class BUG-0418)
// The behaviour tree's condition of node 0x04A1A1E6 (0xD7D9F0, cdecl, 8 arguments, the result in st0) takes the
// creature its senses picked and makes it the target when it is of the world's player species (`+0xB20 ==
// PlayerSpecies()`, the call 0xD7DA43), adult and of relation 2 or 5 to the NPC: the animals hostile to the players'
// species attack them. A guest playing its own creature is another species in the host's game: its puppet was never
// picked. Now, when the condition says no, it is asked again with PlayerSpecies answering each partner's species
// (the party of creature.relations). `coop.fix creature.hostile_party off`: the world's species only (0.7.10).
static bool g_hostile_party = true;
COOP_FIX("creature.hostile_party", "BUG-0734", g_hostile_party, "creature: animals hostile to the players' species attack a partner playing its own creature as they attack the host");
typedef float(__cdecl* HostileCond_t)(void*, void*, void*, void*, void*, void*, void*, void*);
static HostileCond_t o_HostileCond;
static uint32_t g_hostile_party_picks;
// a puppet of the partner's own creature has no archetype tuning (+0xE84): GetRelationship's diet rule then read a
// null (IsHerbivore 0xC0B8E8, data 0x608, lab 20261108-034334). While the game is asked about the party, each such
// puppet borrows the avatar's tuning, as in the NPC AI swap (game.cpp h_NPCTickAI).
struct Lent { void* p[8]; int n = 0; };
static Lent lend_tuning()
{
    Lent l;
    void* av = game::avatar();
    void* tun = av ? skg::field<void*>(av, 0xE84) : nullptr;
    if (!tun || !C.puppet_of)
        return l;
    for (uint8_t id = 1; id < 8 && l.n < 8; id++)
        if (void* p = C.puppet_of(id))
            if (game::is_puppet(p) && !skg::field<void*>(p, 0xE84)) {
                skg::field<void*>(p, 0xE84) = tun;
                l.p[l.n++] = p;
            }
    return l;
}
static void give_back(const Lent& l)
{
    void* av = game::avatar();
    void* tun = av ? skg::field<void*>(av, 0xE84) : nullptr;
    for (int i = 0; i < l.n; i++)
        if (skg::field<void*>(l.p[i], 0xE84) == tun)
            skg::field<void*>(l.p[i], 0xE84) = nullptr;
}
static float __cdecl h_HostileCond(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8)
{
    float r = o_HostileCond(a1, a2, a3, a4, a5, a6, a7, a8);
    if (r != 0.f || !g_hostile_party || !g_party_n || g_ps_override || !game::in_creature_stage() ||
        !(C.is_host && C.is_host()))
        return r;
    Lent l = lend_tuning();
    for (int i = 0; i < g_party_n && r == 0.f; i++) {
        g_ps_override = g_party_species[i];
        r = o_HostileCond(a1, a2, a3, a4, a5, a6, a7, a8);
        g_ps_override = nullptr;
        if (r != 0.f)
            g_hostile_party_picks++;
    }
    give_back(l);
    return r;
}

bool drop_partner_hit(void* npc)
{
    if (!g_party_peace || !host_party(npc))
        return false;
    g_partner_hits_dropped++;
    return true;
}

bool drop_party_hit(void* attacker)
{
    if (!g_party_peace || !attacker || game::is_puppet(attacker) || !host_party(attacker))
        return false;
    g_party_hits_dropped++;
    return true;
}

// the stimuli of a creature's behaviour tree naming `sender` (AI block +0xB4C, +8: 8 slots of 0x38
// bytes, masks +0 / +4, sender +0x2C; docs/re/creature.md 14.2)
static int stimuli_from(void* c, void* sender)
{
    uint8_t* ai = skg::field<uint8_t*>(c, 0xB4C);
    if (!ai || IsBadReadPtr(ai + 8, 8 * 0x38))
        return 0;
    int n = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t* s = ai + 8 + i * 0x38;
        if ((*(uint32_t*)s || *(uint32_t*)(s + 4)) && *(void**)(s + 0x2C) == sender)
            n++;
    }
    return n;
}

std::string status()
{
    char b[512];
    std::string out;
    void* av = game::avatar();
    if (av) {
        void* sp = skg::field<void*>(av, game::kSpeciesOff);
        void* ps = player_species();
        float base = o_BaseMaxHp ? o_BaseMaxHp(av) : -1.f;
        snprintf(b, sizeof b, "own: hp=%.2f/%.2f level=%d species=%p player_species=%p %s base_game=%.2f scale=%.3f\n",
                 game::health(av), game::max_health(av), brain_level(av), sp, ps, sp == ps ? "same" : "other", base,
                 game::scale(av));
        out += b;
    }
    if (C.puppet_of)
        for (uint8_t id = 0; id < 8; id++) {
            void* p = C.puppet_of(id);
            if (!p || !game::is_puppet(p))
                continue;
            auto it = g_puppets.find(p);
            snprintf(b, sizeof b, "puppet %u: hp=%.2f/%.2f reported=%.2f/%.2f scale=%.3f\n", id, game::health(p),
                     game::max_health(p), it != g_puppets.end() ? it->second.hp : -1.f,
                     it != g_puppets.end() ? it->second.max : -1.f, game::scale(p));
            out += b;
        }
    snprintf(b, sizeof b, "relations: fix=%d party_species=%d answers=%u other_callers=%u\n", (int)g_relations_fix, g_party_n,
             g_party_answers, g_party_other_callers);
    out += b;
    snprintf(b, sizeof b, "fixes: partner_hp=%d party_peace=%d own_brain_hp=%d sets=%u partner_hits_dropped=%u party_hits_dropped=%u own_scale=%d own_scales=%u\n",
             (int)g_partner_hp, (int)g_party_peace, (int)g_own_brain_hp, g_puppet_sets, g_partner_hits_dropped, g_party_hits_dropped,
             (int)g_own_scale, g_own_scales);
    out += b;
    snprintf(b, sizeof b, "party rules: hostile_party=%d picks=%u pack_defends=%d defends=%u posts=%u\n", (int)g_hostile_party,
             g_hostile_party_picks, (int)g_pack_defends, g_defends, g_defend_posts);
    out += b;
    return out;
}

// the pack (cGameNounManager +0x5C begin / +0x60 end, docs/re/creature.md 11): each member's health
// and how many of its AI's stimuli name a partner's puppet as the sender
static std::string pack()
{
    std::string out;
    void* nm = game::noun_manager();
    if (!nm)
        return "no noun manager\n";
    void** b = skg::field<void**>(nm, 0x5C);
    void** e = skg::field<void**>(nm, 0x60);
    char line[256];
    for (void** it = b; it && it < e && it - b < 32; ++it) {
        void* c = *it;
        if (!c || IsBadReadPtr(c, 0x1680))
            continue;
        int from = 0;
        if (C.puppet_of)
            for (uint8_t id = 1; id < 8; id++)
                if (void* p = C.puppet_of(id))
                    from += stimuli_from(c, p);
        void* av = game::avatar();
        float dp = -1;
        if (C.puppet_of)
            for (uint8_t id = 1; id < 8; id++)
                if (void* p = C.puppet_of(id))
                    if (game::is_puppet(p) && (dp < 0 || net::dist(game::position(c), game::position(p)) < dp))
                        dp = net::dist(game::position(c), game::position(p));
        snprintf(line, sizeof line, "pack %p hp=%.2f from_partner=%d dead=%d target=%p d_avatar=%.1f d_puppet=%.1f\n", c,
                 game::health(c), from, (int)game::is_dead(c), skg::field<void*>(c, 0xE7C),
                 av ? net::dist(game::position(c), game::position(av)) : -1.f, dp);
        out += line;
    }
    return out.empty() ? "pack empty\n" : out;
}

static int c_hp(void*, int argc, const char* const* argv, SkOut* out)
{
    auto say = [&](const std::string& s) { H->out_printf(out, "%s", s.c_str()); };
    if (argc > 1 && !strcmp(argv[1], "pack")) {
        say(pack());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "attack")) { // host (tests): the avatar "attacks" a partner's puppet
        static uintptr_t get = H->sym("App::cCreatureModeStrategy::Get"), exec = H->sym("App::cCreatureModeStrategy::ExecuteAction");
        void* av = game::avatar();
        void* p = C.puppet_of ? C.puppet_of((uint8_t)atoi(argv[2])) : nullptr;
        void* strat = get && exec ? skg::cdecl_call<void*>(get) : nullptr;
        if (!av || !p || !strat) {
            say("no avatar, puppet or strategy\n");
            return 1;
        }
        void* msg[3] = {av, p, nullptr};
        skg::thiscall<void>(exec, strat, 0xD335362Cu, (void*)msg);
        say("attack message sent\n");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "packtarget")) { // host (tests, BUG-0512): every pack member takes a partner's
        void* p = C.puppet_of ? C.puppet_of((uint8_t)atoi(argv[2])) : nullptr; // puppet as its target (SetTarget
        void* nm = game::noun_manager();                                        // vt 0x84, as the attack node's enter)
        bool sub = argc > 3 && !strcmp(argv[3], "combatant"); // the combatant (+0x5A8) instead of the creature
        int n = 0;
        for (void** it = nm && p ? skg::field<void**>(nm, 0x5C) : nullptr, **e = nm ? skg::field<void**>(nm, 0x60) : nullptr;
             it && it < e; ++it)
            if (*it && !IsBadReadPtr(*it, 0x1680) && !game::is_dead(*it)) {
                skg::vcall<void>(*it, 0x84 / 4, sub ? (void*)((uint8_t*)p + 0x5A8) : p, 1, 0);
                char b[96];
                snprintf(b, sizeof b, "pack %p target %p (puppet %p)\n", *it, skg::field<void*>(*it, 0xE7C), p);
                say(b);
                n++;
            }
        if (!n)
            say("no puppet or no pack member\n");
        return n ? 0 : 1;
    }
    if (argc > 1 && !strcmp(argv[1], "dmglog")) { // RE / tests: the last hits through the game's TakeDamage
        say(game::damage_log(argc > 2 && !strcmp(argv[2], "clear")));
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "hostiles")) { // host (tests, BUG-0734): the adult animals hostile to a puppet
        void* p = C.puppet_of ? C.puppet_of((uint8_t)atoi(argv[2])) : nullptr;  // (relation 2 / 5), nearest first, and
        static uintptr_t rget = H->sym("Simulator::cSpeciesRelationshipManager::Get"); // how many creatures target it
        void* rm = rget ? skg::cdecl_call<void*>(rget) : nullptr;
        if (!p || !rm || !g_rel_lo) {
            say("no puppet or relationship manager\n");
            return 1;
        }
        struct Q { void* p; void* rm; std::vector<std::pair<float, void*>> v; int targeting; } q{p, rm, {}, 0};
        Lent l = lend_tuning();
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* q = (Q*)u;
                if (noun != 0x018EB45E || o == q->p || !game::object_alive(o) || IsBadReadPtr(o, 0x1680) ||
                    game::is_puppet(o) || game::is_dead(o) || o == game::avatar())
                    return true;
                if (skg::field<void*>(o, 0xE7C) == q->p)
                    q->targeting++;
                if (!skg::field<void*>(o, 0xE84))
                    return true; // (no tuning: not an animal of the world)
                int rel = skg::thiscall<int>(g_rel_lo, q->rm, (uint8_t*)o + 0xB28, o, q->p);
                if ((rel == 2 || rel == 5) && skg::field<int>(o, 0xB34) == 1)
                    q->v.push_back({net::dist(game::position(o), game::position(q->p)), o});
                return true;
            },
            &q);
        give_back(l);
        std::sort(q.v.begin(), q.v.end());
        char b[160];
        snprintf(b, sizeof b, "hostiles %u targeting=%d picks=%u\n", (unsigned)q.v.size(), q.targeting, g_hostile_party_picks);
        std::string s = b;
        for (size_t i = 0; i < q.v.size() && i < 8; i++) {
            net::Vec3 at = game::position(q.v[i].second);
            snprintf(b, sizeof b, "  hostile %p d=%.1f at=(%.1f %.1f %.1f)\n", q.v[i].second, q.v[i].first, at.x, at.y, at.z);
            s += b;
        }
        say(s);
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "npchit")) { // host (tests, BUG-0735): the nearest wild animal hits a puppet
        void* p = C.puppet_of ? C.puppet_of((uint8_t)atoi(argv[2])) : nullptr;
        struct Q { void* p; void* best; float d; } q{p, nullptr, 60.f};
        if (p)
            game::for_each_object(
                [](void* o, uint32_t noun, void* u) {
                    auto* q = (Q*)u;
                    if (noun != 0x018EB45E || o == q->p || o == game::avatar() || !game::object_alive(o) ||
                        IsBadReadPtr(o, 0x1680) || game::is_puppet(o) || game::is_dead(o) || host_party(o))
                        return true;
                    float d = net::dist(game::position(o), game::position(q->p));
                    if (d < q->d) {
                        q->d = d;
                        q->best = o;
                    }
                    return true;
                },
                &q);
        if (!q.best) {
            say("no puppet or no animal near it\n");
            return 1;
        }
        if (q.d > 6.f) { // an attacker is next to its victim: brought within 3 m
            net::Vec3 pp = game::position(p), ap = game::position(q.best);
            float k = 3.f / q.d;
            net::Vec3 at{pp.x + (ap.x - pp.x) * k, pp.y + (ap.y - pp.y) * k, pp.z + (ap.z - pp.z) * k};
            game::teleport(q.best, at, game::orientation(q.best));
        }
        game::hit(p, (float)atof(argv[3]), 0, q.best); // through the damage filter (npc::damage_filter)
        char b[96];
        snprintf(b, sizeof b, "animal %p (%.1f m) hit the puppet\n", q.best, q.d);
        say(b);
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "packhit")) { // host (tests, the monitor's posse_damage): a pack member hits a puppet
        void* p = C.puppet_of ? C.puppet_of((uint8_t)atoi(argv[2])) : nullptr;
        void* nm = game::noun_manager();
        void* member = nullptr;
        for (void** it = nm ? skg::field<void**>(nm, 0x5C) : nullptr, **e = nm ? skg::field<void**>(nm, 0x60) : nullptr;
             it && it < e && !member; ++it)
            if (*it && !IsBadReadPtr(*it, 0x1680) && !game::is_dead(*it))
                member = *it;
        if (!p || !member) {
            say("no puppet or no pack member\n");
            return 1;
        }
        game::hit(p, (float)atof(argv[3]), 0, member); // through the damage filter (npc::damage_filter)
        say("pack member hit the puppet\n");
        return 0;
    }
    say(status());
    return 0;
}

// tests (BUG-0463): coop.sneak [on|off|ability] - the sneak state the game holds for the own avatar and the
// partners' puppets (+0xBB1, noise radius +0xB80, alpha +0xB7C, energy +0xE58); on / off: the game's setter on
// the own avatar without force (it needs the sneak ability, as the ability key does)
static int c_sneak(void*, int argc, const char* const* argv, SkOut* out)
{
    char b[256];
    std::string s;
    void* av = game::avatar();
    if (argc > 1 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off") || !strcmp(argv[1], "force"))) {
        uintptr_t a = H->sym("SporeCoop::CreatureSetSneaking");
        if (!av || !a) {
            H->out_printf(out, "no avatar\n");
            return 1;
        }
        // force: the setter's own force flag (a creature without the sneak ability, tests only)
        skg::thiscall<void>(a, av, (int)(argv[1][1] != 'f'), (int)(argv[1][0] == 'f'));
    }
    auto line = [&](const char* who, int id, void* c) {
        uint8_t* model = skg::field<uint8_t*>(c, 0xB54);
        snprintf(b, sizeof b, "%s %d: sneak=%d noise=%.2f alpha=%.2f energy=%.2f model_alpha=%.2f b5e=%d b58=%X ptr=%p\n", who, id,
                 (int)game::sneaking(c), game::noise_radius(c), game::sneak_alpha(c),
                 skg::readable(c, 0xE5C) ? skg::field<float>(c, 0xE58) : -1.f,
                 model && skg::readable(model + 0x80, 4) ? *(float*)(model + 0x80) : -1.f, (int)skg::field<uint8_t>(c, 0xB5E),
                 skg::field<uint32_t>(c, 0xB58), c);
        s += b;
    };
    if (av)
        line("own", -1, av);
    if (C.puppet_of)
        for (uint8_t id = 0; id < 8; id++)
            if (void* p = C.puppet_of(id))
                if (game::is_puppet(p))
                    line("puppet", id, p);
    H->out_printf(out, "%s", s.c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_SpeciesMgrGet = H->sym("Editors::cSpeciesManager::Get");
    a_PlayerSpecies = H->sym("SporeCoop::cSpeciesManager_PlayerSpecies");
    a_LevelHealth = H->sym("SporeCoop::CreatureLevelHealth");
    if (uintptr_t a = H->sym("SporeCoop::cCreatureAnimal_BaseMaxHitPoints"))
        H->hook_create(M, (void*)a, (void*)h_BaseMaxHp, (void**)&o_BaseMaxHp);
    g_rel_lo = H->sym("Simulator::cSpeciesRelationshipManager::GetRelationship");
    g_rel_hi = g_rel_lo ? g_rel_lo + 736 : 0;
    g_mode_lo = H->sym("SporeCoop::CreatureSocialMode");
    g_mode_hi = g_mode_lo ? g_mode_lo + 522 : 0;
    if (uintptr_t a = H->sym("SporeCoop::cCreatureBase_IsPlayerSpecies"))
        H->hook_create(M, (void*)a, (void*)h_IsPlayerSpecies, (void**)&o_IsPlayerSpecies);
    if (uintptr_t a = H->sym("SporeCoop::cCreatureAnimal_GetMaxHitPoints"))
        H->hook_create(M, (void*)a, (void*)h_GetMaxHp, (void**)&o_GetMaxHp);
    // BUG-0732: the size rule of the own avatar (PlayerSpecies is hooked with it: it answers the override only inside)
    g_norm_global = (float*)H->sym("SporeCoop::g_AvatarNormalizingScale");
    uintptr_t a_scale = H->sym("SporeCoop::cCreatureAnimal_ComputeScale");
    if (a_scale && a_PlayerSpecies && g_norm_global &&
        H->hook_create(M, (void*)a_PlayerSpecies, (void*)h_PlayerSpecies, (void**)&o_PlayerSpecies) == 0) {
        H->hook_create(M, (void*)a_scale, (void*)h_Scale, (void**)&o_Scale);
        if (uintptr_t a = H->sym("SporeCoop::BT_HostileSpeciesTarget")) // BUG-0734
            H->hook_create(M, (void*)a, (void*)h_HostileCond, (void**)&o_HostileCond);
    }
    a_CmdStim = H->sym("SporeCoop::CreatureCommandStimulus"); // BUG-0735
    a_CanDefend = H->sym("SporeCoop::PosseMemberCanDefend");
    H->command_add(M, "coop.hp", "coop.hp [pack|attack <player>|packhit <player> <hp>|packtarget <player> [combatant]] - creature health of the own avatar, partners' puppets, the pack (tests)",
                   c_hp, nullptr, SK_CMD_MAIN_THREAD);
    H->command_add(M, "coop.sneak", "coop.sneak [on|off|force] - the sneak state of the own avatar and the partners' puppets (tests, BUG-0463)",
                   c_sneak, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace creature_hp
