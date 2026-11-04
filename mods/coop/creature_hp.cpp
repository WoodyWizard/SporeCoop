#include "creature_hp.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "fixes.h"
#include "game.h"
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
        snprintf(b, sizeof b, "own: hp=%.2f/%.2f level=%d species=%p player_species=%p %s base_game=%.2f\n", game::health(av),
                 game::max_health(av), brain_level(av), sp, ps, sp == ps ? "same" : "other", base);
        out += b;
    }
    if (C.puppet_of)
        for (uint8_t id = 0; id < 8; id++) {
            void* p = C.puppet_of(id);
            if (!p || !game::is_puppet(p))
                continue;
            auto it = g_puppets.find(p);
            snprintf(b, sizeof b, "puppet %u: hp=%.2f/%.2f reported=%.2f/%.2f\n", id, game::health(p), game::max_health(p),
                     it != g_puppets.end() ? it->second.hp : -1.f, it != g_puppets.end() ? it->second.max : -1.f);
            out += b;
        }
    snprintf(b, sizeof b, "relations: fix=%d party_species=%d answers=%u other_callers=%u\n", (int)g_relations_fix, g_party_n,
             g_party_answers, g_party_other_callers);
    out += b;
    snprintf(b, sizeof b, "fixes: partner_hp=%d party_peace=%d own_brain_hp=%d sets=%u partner_hits_dropped=%u party_hits_dropped=%u\n",
             (int)g_partner_hp, (int)g_party_peace, (int)g_own_brain_hp, g_puppet_sets, g_partner_hits_dropped, g_party_hits_dropped);
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
        snprintf(line, sizeof line, "pack %p hp=%.2f from_partner=%d dead=%d\n", c, game::health(c), from, (int)game::is_dead(c));
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
    H->command_add(M, "coop.hp", "coop.hp [pack|attack <player>|packhit <player> <hp>|packtarget <player> [combatant]] - creature health of the own avatar, partners' puppets, the pack (tests)",
                   c_hp, nullptr, SK_CMD_MAIN_THREAD);
    H->command_add(M, "coop.sneak", "coop.sneak [on|off|force] - the sneak state of the own avatar and the partners' puppets (tests, BUG-0463)",
                   c_sneak, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace creature_hp
