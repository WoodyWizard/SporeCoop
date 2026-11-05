#include "player_rules.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "fixes.h"
#include "game.h"
#include "npc.h"
#include "sporekit_game.hpp"

namespace player_rules {

static const SkHost* H;
static SkMod* M;
static Ctx C;

enum Kind : uint8_t {
    K_STUN_PLAYER = 0, // host -> guest {u8 player, f32 seconds}
    K_STUN_NPC = 1,    // guest -> host {u16 npc id, f32 seconds}
};

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

// ---------------------------------------------------------------- BUG-0554
// The stun of a charge is not a hit: the ability hit 0xC20390 applies the damage through TakeDamage (which the
// mod forwards as MSG_DAMAGE) and then the ability's effects, kind 1 = the victim's vt 0x94 (CreatureStun
// 0xC02C70, thiscall(creature, float seconds) ret 4: an avatar's seconds scaled and clamped, then the
// stimulus 0x1000 with the seconds as its value, which the creature's behaviour tree plays as the stars and the
// stagger). In the host's game an NPC's charge stuns the guest's puppet, which has no AI: the guest's own
// creature never got it. Now the host sends it to that guest and the guest's game stuns its own avatar; the
// other way round a guest's charge on a proxy (no AI either) is sent to the host, whose game stuns its NPC.
// `coop.fix creature.stun_relay off`: the stun stays on the puppet / proxy (0.7.6).
static bool g_stun_fix = true;
COOP_FIX("creature.stun_relay", "BUG-0554", g_stun_fix, "creature: a charge's stun reaches the stunned player's own creature and the host's NPC");

static bool g_air_hit_fwd(); // BUG-0555 (below)
static void* g_hit_attacker_fwd(); // the attacker of the ability hit running now (CreatureAttackTarget), or null
typedef void(SK_THISCALL* Stun_t)(void*, float);
static Stun_t o_Stun;
static uint32_t g_stun_sent_player, g_stun_sent_npc, g_stun_got_player, g_stun_got_npc, g_stun_bad;
static float g_stun_last;

static uint8_t puppet_player(void* c)
{
    if (!C.puppet_of || !c || !game::is_puppet(c))
        return 0xFF;
    for (uint8_t id = 1; id < 8; id++)
        if (C.puppet_of(id) == c)
            return id;
    return 0xFF;
}

static void SK_THISCALL h_Stun(void* c, float secs)
{
    if (g_stun_fix && game::in_creature_stage() && std::isfinite(secs) && secs > 0) {
        if (is_host()) {
            uint8_t p = puppet_player(c);
            if (p != 0xFF && g_air_hit_fwd())
                return; // BUG-0555: no stun from a hit the partner's creature in the air does not take
            if (p != 0xFF) { // the partner's creature: its own game stuns it (its puppet mirrors it, no AI here)
                net::Writer w;
                w.u8(K_STUN_PLAYER);
                w.u8(p);
                w.f32(secs);
                C.sess->send_reliable_to(p, net::MSG_PLAYER_RULE, w);
                g_stun_sent_player++;
                g_stun_last = secs;
                return;
            }
        } else if (is_guest()) {
            uint16_t id = npc::is_proxy(c) ? npc::id_of(c) : 0xFFFF;
            // only the own creature's hit is the guest's to send: a partner's puppet plays its abilities in this game
            // too (the animation relay), its stun of a proxy is the partner's game's (lab 20261104-212957: got_npc=1 on
            // the host with no charge of the guest)
            if (id != 0xFFFF && g_hit_attacker_fwd() != game::avatar())
                return; // a proxy has no AI: nothing here to stun
            if (id != 0xFFFF) { // a host NPC's mirror: the host's game stuns the NPC
                net::Writer w;
                w.u8(K_STUN_NPC);
                w.u16(id);
                w.f32(secs);
                C.sess->send_reliable_to(0, net::MSG_PLAYER_RULE, w);
                g_stun_sent_npc++;
                g_stun_last = secs;
                return;
            }
        }
    }
    o_Stun(c, secs);
}

// ---------------------------------------------------------------- BUG-0555
// The game keeps NPCs from biting or charging a creature in the air in one place: the attack node's ability choice
// 0xD666B0 (BT_AttackTick 0xD67CD0) allows only the ranged types 0x20 / 0x3D / 0x42 / 0x48 against a target that is
// not grounded (+0x137 == 0, +0x110 & 0x1000 clear) 1 m or more away. Neither the hit frame 0xC1E8B0 (3D reach) nor
// the hit 0xC20390 looks again. On the host the guest's puppet is held in the air only while the guest's state says
// it is airborne (PF_JUMPING) and the game sets +0x137 back to 1 at every animation start (BUG-0532): an attack
// chosen in such a frame, or before the take-off, landed on a guest that glides in its own game. Now the host
// answers the rule at the hit for a puppet: while its player is airborne, a hit of a melee ability (bite, charge,
// strike) from an NPC more than 1 m away carries no damage and no stun to the partner, as the attack node would not
// have chosen it.
// `coop.fix creature.air_immune off`: every hit on the puppet lands (0.7.6).
static bool g_air_fix = true;
COOP_FIX("creature.air_immune", "BUG-0555", g_air_fix, "creature: NPCs cannot bite or charge a player whose creature is in the air");
typedef bool(SK_THISCALL* AttackTarget_t)(void*, void*, uint32_t, uint32_t);
static AttackTarget_t o_AttackTarget;
static uint32_t g_air_dropped, g_air_passed;
static bool g_air_hit = false; // inside a melee hit on an airborne partner's puppet (its damage and stun stay here)

static bool ranged_type(uint32_t t) { return t == 0x20 || t == 0x3D || t == 0x42 || t == 0x48; }

static void* g_hit_attacker = nullptr;
static uint32_t g_hit_idx = 0; // its ability index
static void* g_hit_attacker_fwd() { return g_hit_attacker; }

static bool hit_body(void* attacker, void* target, uint32_t idx, uint32_t a4);

static bool SK_THISCALL h_AttackTarget(void* attacker, void* target, uint32_t idx, uint32_t a4)
{
    void* was = g_hit_attacker;
    uint32_t was_idx = g_hit_idx;
    g_hit_attacker = attacker;
    g_hit_idx = idx;
    bool r = hit_body(attacker, target, idx, a4);
    g_hit_attacker = was;
    g_hit_idx = was_idx;
    return r;
}

static bool hit_body(void* attacker, void* target, uint32_t idx, uint32_t a4)
{
    if (g_air_fix && target && attacker && is_host() && C.airborne && game::in_creature_stage()) {
        void* victim = (uint8_t*)target - game::kCombatOff;
        uint8_t p = puppet_player(victim);
        if (p != 0xFF && C.airborne(p)) {
            uint8_t* ab = (int)idx < skg::vcall<int>(attacker, 0xB0 / 4) ? skg::vcall<uint8_t*>(attacker, 0xB4 / 4, (int)idx)
                                                                         : nullptr;
            uint32_t type = ab && skg::readable(ab, 0xC) ? *(uint32_t*)(ab + 8) : 0;
            float d = net::dist(game::position(attacker), game::position(victim));
            if (!ranged_type(type) && d >= 1.f) {
                // the game's hit runs (it ends a charge: dropping the call left a charging NPC flying on towards the
                // puppet in the air, 82 m up, lab 20261104-212957), only its damage and stun are not carried to the
                // partner (npc.cpp damage_filter, h_Stun): the attack node would not have picked it
                g_air_dropped++;
                g_air_hit = true;
                bool r = o_AttackTarget(attacker, target, idx, a4);
                g_air_hit = false;
                return r;
            }
            g_air_passed++;
        }
    }
    return o_AttackTarget(attacker, target, idx, a4);
}

bool air_hit_now() { return g_air_hit; }

uint32_t hit_ability_type()
{
    if (!g_hit_attacker)
        return 0;
    int idx = (int)g_hit_idx;
    if (idx < 0 || idx >= skg::vcall<int>(g_hit_attacker, 0xB0 / 4))
        return 0;
    uint8_t* ab = skg::vcall<uint8_t*>(g_hit_attacker, 0xB4 / 4, idx);
    return ab && skg::readable(ab, 0xC) ? *(uint32_t*)(ab + 8) : 0;
}

// guest: the same rule in the guest's own game, for a hit the host decided while its last state of the guest was on
// the ground (the network's delay: a jump or a glide starts in the guest's game first; lab 20261104-221242 #1: one
// bite landed 1.5 m up)
static uint32_t g_air_dropped_guest;
bool guest_drops_air_hit(void* av, void* attacker, uint32_t ability)
{
    if (!g_air_fix || !av || !attacker || !ability || ranged_type(ability) || !game::in_creature_stage() ||
        game::grounded(av) || net::dist(game::position(av), game::position(attacker)) < 1.f)
        return false;
    g_air_dropped_guest++;
    return true;
}
static bool g_air_hit_fwd() { return g_air_hit; }

// ---------------------------------------------------------------- BUG-0556
// The host's Summon Flock (ability 0x33 -> the drama event cSummonCreatureEvent, start 0xD50EA0) makes a herd and
// puts each creature into the host's pack (cPosseSimulator_AddMember 0xD542E0: +0xB58 |= 0x100, the pack list).
// The game shows a creature as friendly (the rollover's ally mark through CreatureSelectionUI 0xB6C970, the social
// cursor 0xD2FFD0) by creature vt 0xD4 (SporeCoop::CreatureFriendly 0xC0C3B0: relation 6 to the avatar, impressed
// +0xB58 & 0x80, or the player species): the pack bit is not read there. On the host a pack member is friendly by
// its relation to its leader's species; the guest's proxy is a stranger of another herd: the flock was not
// allied with the guest. Now the host sends the pack bit (NF_PACK) and the guest's vt 0xD4 answers true for the
// proxies of the host's pack, only for them (the party's allies are the guest's allies).
// `coop.fix creature.party_allies off`: the guest's game decides by the species (0.7.6).
static bool g_allies_fix = true;
COOP_FIX("creature.party_allies", "BUG-0556", g_allies_fix, "creature: the host's pack and summoned flock are allies of the guest too");
typedef bool(SK_THISCALL* Friendly_t)(void*);
static Friendly_t o_Friendly;
static uint32_t g_allies_answers;

static bool SK_THISCALL h_Friendly(void* c)
{
    bool r = o_Friendly(c);
    if (!r && g_allies_fix && c && is_guest() && npc::proxy_in_host_pack(c)) {
        g_allies_answers++;
        return true;
    }
    return r;
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_PLAYER_RULE || !C.sess)
        return;
    uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (kind == K_STUN_PLAYER) {
        uint8_t p = r.u8();
        float secs = r.f32();
        if (!r.ok() || from != 0 || !is_guest() || p != C.sess->my_id())
            return;
        if (!std::isfinite(secs) || secs <= 0 || secs > 30) {
            g_stun_bad++;
            return;
        }
        void* av = game::avatar();
        if (!o_Stun || !av || !game::in_creature_stage() || game::is_dead(av))
            return;
        o_Stun(av, secs); // the game's own stun of the avatar (its avatar scaling, the stimulus, the stars)
        g_stun_got_player++;
        g_stun_last = secs;
    } else if (kind == K_STUN_NPC) {
        uint16_t id = r.u16();
        float secs = r.f32();
        if (!r.ok() || from == 0 || !is_host())
            return;
        if (!std::isfinite(secs) || secs <= 0 || secs > 30) {
            g_stun_bad++;
            return;
        }
        void* c = npc::host_npc(id);
        if (!o_Stun || !c || !game::in_creature_stage() || game::is_dead(c))
            return;
        o_Stun(c, secs);
        g_stun_got_npc++;
        g_stun_last = secs;
    }
}

std::string status()
{
    char b[256];
    snprintf(b, sizeof b,
             "player_rules: stun fix=%d sent_player=%u sent_npc=%u got_player=%u got_npc=%u bad=%u last=%.2f hook=%d\n",
             (int)g_stun_fix, g_stun_sent_player, g_stun_sent_npc, g_stun_got_player, g_stun_got_npc, g_stun_bad,
             g_stun_last, (int)(o_Stun != nullptr));
    std::string out = b;
    snprintf(b, sizeof b, "player_rules: air fix=%d dropped=%u passed=%u guest_dropped=%u hook=%d\n", (int)g_air_fix,
             g_air_dropped, g_air_passed, g_air_dropped_guest, (int)(o_AttackTarget != nullptr));
    out += b;
    snprintf(b, sizeof b, "player_rules: allies fix=%d answers=%u hook=%d\n", (int)g_allies_fix, g_allies_answers,
             (int)(o_Friendly != nullptr));
    return out + b;
}

static int c_rules(void*, int argc, const char* const* argv, SkOut* out)
{
    // tests: stun <seconds> [npc id]: the game's stun on our own avatar (or on host NPC / proxy <id>) through the hook
    if (argc > 2 && !strcmp(argv[1], "stun")) {
        void* c = argc > 3 ? (is_host() ? npc::host_npc((uint16_t)atoi(argv[3])) : npc::proxy((uint16_t)atoi(argv[3])))
                           : game::avatar();
        uintptr_t a = H->sym("SporeCoop::CreatureStun");
        if (!c || !a) {
            H->out_printf(out, "no creature\n");
            return 1;
        }
        // as the own creature's ability hit calls it (the hit's attacker is the avatar)
        void* was = g_hit_attacker;
        g_hit_attacker = game::avatar();
        skg::thiscall<void>(a, c, (float)atof(argv[2]));
        g_hit_attacker = was;
        H->out_printf(out, "stun called on %p\n", c);
        return 0;
    }
    // tests (host): charge <npc id> <player>: NPC <id> aims at player <player>'s puppet (SetTarget vt 0x84 with its
    // combatant, as the attack node's enter does) and plays its charge (ability type 0x1F, PlayAbility); the
    // charge's hit and its stun are the game's own (CreatureAbilityHit 0xC20390)
    if (argc > 3 && !strcmp(argv[1], "charge")) {
        void* n = is_host() ? npc::host_npc((uint16_t)atoi(argv[2])) : nullptr;
        void* p = C.puppet_of ? C.puppet_of((uint8_t)atoi(argv[3])) : nullptr;
        uintptr_t pa = H->sym("Simulator::cCreatureBase::PlayAbility");
        if (!n || !p || !pa) {
            H->out_printf(out, "no npc, puppet or PlayAbility\n");
            return 1;
        }
        int idx = -1, cnt = skg::vcall<int>(n, 0xB0 / 4);
        for (int i = 0; i < cnt && idx < 0; i++) {
            uint8_t* ab = skg::vcall<uint8_t*>(n, 0xB4 / 4, i);
            if (ab && skg::readable(ab, 0x10) && *(uint32_t*)(ab + 8) == 0x1F)
                idx = i;
        }
        if (idx < 0) {
            H->out_printf(out, "npc has no charge\n");
            return 1;
        }
        skg::vcall<void>(n, 0x84 / 4, (uint8_t*)p + game::kCombatOff, 1, 0);
        int r = skg::thiscall<int>(pa, n, idx, (void*)nullptr);
        H->out_printf(out, "charge ability %d played: %d\n", idx, r);
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "nests")) { // tests (BUG-0561): herds of species <inst> <type> <group>: mbBestFriends
        void* sp = game::species_by_key({(uint32_t)strtoul(argv[2], nullptr, 16), (uint32_t)strtoul(argv[3], nullptr, 16),
                                         (uint32_t)strtoul(argv[4], nullptr, 16)});
        struct Q {
            void* sp;
            std::string out;
        } q{sp, ""};
        if (sp)
            game::for_each_object(
                [](void* o, uint32_t noun, void* u) {
                    auto* q = (Q*)u;
                    if (noun == game::kHerdNoun && skg::readable(o, 0x194) && game::herd_species(o) == q->sp) {
                        char b[96];
                        snprintf(b, sizeof b, "herd %p nest=%p best_friends=%d\n", o, game::herd_nest(o),
                                 (int)skg::field<uint8_t>(o, 0x191));
                        q->out += b;
                    }
                    return true;
                },
                &q);
        H->out_printf(out, "%s", q.out.empty() ? "no herds\n" : q.out.c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "friendly")) { // tests: the game's vt 0xD4 of NPC / proxy <id> here
        void* c = is_host() ? npc::host_npc((uint16_t)atoi(argv[2])) : npc::proxy((uint16_t)atoi(argv[2]));
        if (!c) {
            H->out_printf(out, "no creature\n");
            return 1;
        }
        uintptr_t gfx = H->sym("Simulator::cCreatureBase::GetEffectFromPools");
        void* hearts = gfx ? skg::thiscall<void*>(gfx, c, 0x6A17B05Au) : nullptr; // the summoned flock's hearts
        H->out_printf(out, "friendly=%d pack=%d hearts=%d\n", (int)skg::vcall<bool>(c, 0xD4 / 4), (int)game::pack_member(c),
                      (int)(hearts != nullptr));
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    if (uintptr_t a = H->sym("SporeCoop::CreatureFriendly"))
        H->hook_create(M, (void*)a, (void*)h_Friendly, (void**)&o_Friendly);
    if (uintptr_t a = H->sym("SporeCoop::CreatureAttackTarget"))
        H->hook_create(M, (void*)a, (void*)h_AttackTarget, (void**)&o_AttackTarget);
    if (uintptr_t a = H->sym("SporeCoop::CreatureStun"))
        H->hook_create(M, (void*)a, (void*)h_Stun, (void**)&o_Stun);
    H->command_add(M, "coop.rules", "coop.rules [stun <s> [npc id]] - the game's player rules carried between the games (P1, BUG-0554)",
                   c_rules, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace player_rules
