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
    K_ROAR = 2,        // guest -> host {u32 ability type}: the guest's roar / siren song, played by its puppet (BUG-0501)
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

// ---------------------------------------------------------------- BUG-0501
// The roar (ability type 0x31, Raging Roar) is not a hit: the behaviour tree's action node 0x06005DAF (node table row
// 0x158B1F8: enter 0xD7B5B0, tick SporeCoop::BT_SpecialAbilityTick 0xD8E840, exit 0xD7B600) plays the ability's
// animation on its first tick and walks every creature within the ability's range (+0xA8 + +0xF4) that is not the
// avatar (spatial vt 0x58), not dead (+0xB5E), not of the roarer's species and not epic: its stimuli (+0xB4C + 8) get
// (0, 0x4000000, FLT_MAX, roarer), its goal slots (+0xB48) the roar's slot (template 1 of group 1 for the ability's
// +0xA0 seconds: the creature runs away), stimulus 0x400 is cleared and the siren song's slot (2, 1) ends. On a guest
// that walk finds only proxies (mirrors without AI): the host's animals never heard the guest's roar. The roar is a
// consequence ability of the player (the guest's avatar has it, its puppet in the host's game, made from the species,
// does not: lab 20261105-105734), so the host cannot run the node on the puppet: the guest sends its roar (range and
// seconds of its ability) and the host's game gets the same four calls on its creatures around the guest's puppet,
// with the puppet as the roarer. Their flight reaches the guest as their states.
// `coop.fix creature.roar_relay off`: the roar stays in the guest's game (0.7.7).
static bool g_roar_fix = true;
COOP_FIX("creature.roar_relay", "BUG-0501", g_roar_fix, "creature: the guest's roar scares the host's animals");
typedef int(__cdecl* AbilityTick_t)(void*, int, int, int, int, uint32_t*);
static AbilityTick_t o_AbilityTick;
static uintptr_t a_PostStim, a_GoalStart, a_StimClear, a_GoalFind, a_GoalEnd, a_IsEpic;
static uint32_t g_roar_sent, g_roar_got, g_roar_played, g_roar_hit, g_roar_bad, g_roar_last_type;
static constexpr uint32_t kRoar = 0x31;
static constexpr uint32_t kCreatureNoun = 0x18EB45Eu;

static uint8_t* ability_ptr(void* c, int idx)
{
    if (!c || idx < 0 || idx >= skg::vcall<int>(c, 0xB0 / 4))
        return nullptr;
    uint8_t* ab = skg::vcall<uint8_t*>(c, 0xB4 / 4, idx);
    return ab && skg::readable(ab, 0x100) ? ab : nullptr;
}

static uint32_t ability_type(void* c, int idx)
{
    uint8_t* ab = ability_ptr(c, idx);
    return ab ? *(uint32_t*)(ab + 8) : 0;
}

static int ability_index(void* c, uint32_t type)
{
    int n = c ? skg::vcall<int>(c, 0xB0 / 4) : 0;
    for (int i = 0; i < n; i++)
        if (ability_type(c, i) == type)
            return i;
    return -1;
}

static int __cdecl h_AbilityTick(void* c, int a2, int a3, int a4, int a5, uint32_t* st)
{
    if (g_roar_fix && c && st && st[1] == 0 && is_guest() && c == game::avatar() && game::in_creature_stage()) {
        uint8_t* ab = ability_ptr(c, (int)st[0]);
        if (ab && *(uint32_t*)(ab + 8) == kRoar) { // the first tick of the own roar: the host's game plays it too
            float range = *(float*)(ab + 0xA8) + *(float*)(ab + 0xF4), secs = *(float*)(ab + 0xA0);
            net::Writer w;
            w.u8(K_ROAR);
            w.u32(kRoar);
            w.f32(range);
            w.f32(secs);
            C.sess->send_reliable_to(0, net::MSG_PLAYER_RULE, w);
            g_roar_sent++;
            g_roar_last_type = kRoar;
        }
    }
    return o_AbilityTick(c, a2, a3, a4, a5, st);
}

struct RoarQ {
    void* roarer;
    net::Vec3 at;
    float range, secs;
    uint32_t hit;
};

static bool roar_one(void* o, uint32_t noun, void* u)
{
    auto* q = (RoarQ*)u;
    uint8_t* t = (uint8_t*)o;
    if (noun != kCreatureNoun || o == q->roarer || !game::object_alive(o) || !skg::readable(t, 0xB60))
        return true;
    // the node's filter (0xD8EBCA..0xD8EC0A): not the avatar, alive, another species, not epic
    if (skg::vcall<bool>(t + 0xC0, 0x58 / 4) || t[0xB5E] ||
        *(uint32_t*)(t + 0xB20) == *(uint32_t*)((uint8_t*)q->roarer + 0xB20) || skg::thiscall<bool>(a_IsEpic, t))
        return true;
    if (net::dist(game::position(o), q->at) > q->range)
        return true;
    uint8_t* ai = *(uint8_t**)(t + 0xB4C);
    void* goals = *(void**)(t + 0xB48);
    if (!ai || !goals)
        return true;
    // the node's four calls, in its order (0xD8EC0C..0xD8EC7E)
    skg::thiscall<void>(a_PostStim, ai + 8, 0u, 0x4000000u, 3.4028235e38f, q->roarer);
    skg::thiscall<uint32_t>(a_GoalStart, goals, 1, 1, (void*)nullptr, q->secs, (void*)nullptr, (void*)t);
    skg::thiscall<void>(a_StimClear, ai + 8, 0x400u, 0u);
    uint32_t h = skg::thiscall<uint32_t>(a_GoalFind, goals, 2, 1, 0, 0);
    if (h != 0xFFFFFFFFu)
        skg::thiscall<void>(a_GoalEnd, goals, h);
    q->hit++;
    return true;
}

static void roar_on_host(uint8_t from, float range, float secs)
{
    void* p = C.puppet_of ? C.puppet_of(from) : nullptr;
    if (!p || !a_PostStim || !a_GoalStart || !a_StimClear || !a_GoalFind || !a_GoalEnd || !a_IsEpic ||
        !game::in_creature_stage() || game::is_dead(p))
        return;
    RoarQ q{p, game::position(p), range, secs, 0};
    game::for_each_object(roar_one, &q);
    g_roar_played++;
    g_roar_hit += q.hit;
    g_roar_last_type = kRoar;
    H->log(M, SK_INFO, "rules: roar of player %u (%.0f m, %.1f s): %u creatures of the host's game heard it", from, range,
           secs, q.hit);
}

// ---------------------------------------------------------------- BUG-0558
// The drama UFO (cUFOAbductionEvent) takes a creature by its ship's behaviour tree: the pick node 0x0519E372 (tick
// 0xDC26B0) walks the creatures within 50 m of the avatar and keeps the last one that is adult (+0xB5E == 0), not the
// avatar (spatial vt 0x58, the avatar's species profile, 5 m or more away), not epic (cCreatureAnimal_IsEpic called at
// 0xDC2B5A) and not in BT node 0x3704748; the follow node then hovers over it and fires the abduct beam. In the host's
// game the guest's puppet is a creature of another species near the host's avatar: the ship picked it and tried to
// lift a creature the mod places at the guest's position every frame ("the ship tries to pick up the guest but
// cannot"), while the guest's game had no ship at all. The player is never the ship's target in the game's rules:
// now the pick sees a partner's puppet as it sees an epic (the IsEpic answer at that one call site, by its return
// address), so the host's ship takes the host's animals only, as in a game of one.
// `coop.fix creature.ufo_puppet off`: the host's ship may pick the guest's puppet (0.7.7).
static bool g_ufo_fix = true;
COOP_FIX("creature.ufo_puppet", "BUG-0558", g_ufo_fix, "creature: the host's UFO never tries to take the guest's creature");
typedef bool(SK_THISCALL* IsEpic_t)(void*);
static uint32_t g_ufo_skipped, g_ufo_seen;
} // namespace player_rules

// cCreatureAnimal_IsEpic touches EAX only, and its ~90 callers (one optimised build) keep ECX / EDX across the call
// (the first hook here, a C function, cleared ECX under CreatureStun's caller: 0xBC9A6D faulted on [0 + ...], lab
// 20261105-113638). So the detour is a stub that jumps straight to the game's code unless it is called from the UFO
// pick, and saves ECX / EDX around the decision there.
extern "C" {
uintptr_t p2_ufo_pick_ret = 0;
void* p2_o_IsEpic = nullptr;
bool __cdecl p2_ufo_pick_epic(void* c);
void p2_h_IsEpic();
}
__asm__(".text\n"
        ".globl _p2_h_IsEpic\n"
        "_p2_h_IsEpic:\n"
        "  movl (%esp), %eax\n"
        "  cmpl _p2_ufo_pick_ret, %eax\n"
        "  jne 1f\n"
        "  pushl %ecx\n"
        "  pushl %edx\n"
        "  pushl %ecx\n"
        "  call _p2_ufo_pick_epic\n"
        "  addl $4, %esp\n"
        "  popl %edx\n"
        "  popl %ecx\n"
        "  movzbl %al, %eax\n"
        "  ret\n"
        "1:\n"
        "  jmp *_p2_o_IsEpic\n");

namespace player_rules {
static bool pick_epic(void* c)
{
    bool r = skg::thiscall<bool>((uintptr_t)p2_o_IsEpic, c);
    if (r)
        return r;
    g_ufo_seen++;
    if (g_ufo_fix && is_host() && puppet_player(c) != 0xFF) {
        g_ufo_skipped++;
        return true;
    }
    return false;
}
} // namespace player_rules

extern "C" bool __cdecl p2_ufo_pick_epic(void* c) { return player_rules::pick_epic(c); }

namespace player_rules {
static constexpr uint32_t kUfoNoun = 0x018EBADCu;
typedef bool(SK_THISCALL* UfoTick_t)(void*, float, double);
static UfoTick_t o_UfoTick;
static uint32_t g_ufo_ticks;
static bool SK_THISCALL h_UfoTick(void* u, float dt, double now) // diagnostics: the ship's behaviour tree runs
{
    g_ufo_ticks++;
    return o_UfoTick(u, dt, now);
}

static std::string ufo_dump()
{
    struct U {
        std::string out;
    } u;
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            net::Vec3 v;
            if (noun == kUfoNoun && game::object_alive(o) && game::object_position(o, v)) {
                char b[96];
                snprintf(b, sizeof b, "ufo %p pos=(%.1f %.1f %.1f)\n", o, v.x, v.y, v.z);
                ((U*)p)->out += b;
            }
            return true;
        },
        &u);
    for (uint8_t id = 1; id < 8 && C.puppet_of; id++)
        if (void* pp = C.puppet_of(id)) {
            net::Vec3 v = game::position(pp);
            char b[128];
            snprintf(b, sizeof b, "puppet %u %p pos=(%.1f %.1f %.1f) b2b0=%d\n", id, pp, v.x, v.y, v.z,
                     skg::readable((uint8_t*)pp + 0x2B0, 4) ? *(int*)((uint8_t*)pp + 0x2B0) : -1);
            u.out += b;
        }
    char b[96];
    snprintf(b, sizeof b, "pick fix=%d seen=%u skipped=%u ticks=%u hook=%d\n", (int)g_ufo_fix, g_ufo_seen, g_ufo_skipped,
             g_ufo_ticks, (int)(p2_o_IsEpic != nullptr && p2_ufo_pick_ret != 0));
    return u.out + b;
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
    } else if (kind == K_ROAR) {
        uint32_t t = r.u32();
        float range = r.f32(), secs = r.f32();
        if (!r.ok() || from == 0 || !is_host())
            return;
        g_roar_got++;
        if (t != kRoar || !std::isfinite(range) || range <= 0 || range > 100 || !std::isfinite(secs) || secs <= 0 ||
            secs > 120) {
            g_roar_bad++;
            return;
        }
        if (g_roar_fix)
            roar_on_host(from, range, secs);
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
    out += b;
    snprintf(b, sizeof b, "player_rules: roar fix=%d sent=%u got=%u played=%u heard=%u bad=%u last=0x%X hook=%d\n", (int)g_roar_fix,
             g_roar_sent, g_roar_got, g_roar_played, g_roar_hit, g_roar_bad, g_roar_last_type, (int)(o_AbilityTick != nullptr));
    out += b;
    snprintf(b, sizeof b, "player_rules: ufo fix=%d seen=%u skipped=%u\n", (int)g_ufo_fix, g_ufo_seen, g_ufo_skipped);
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
    // tests (BUG-0496): what the avatar's nest heal (0xC084B0 avatar branch -> +0xFB8, applied by vt 0x88 0xC02E10) reads
    if (argc > 1 && !strcmp(argv[1], "heal")) {
        uint8_t* av = (uint8_t*)game::avatar();
        if (!av || !skg::readable(av, 0x1000)) {
            H->out_printf(out, "no avatar\n");
            return 1;
        }
        uint8_t* ai = *(uint8_t**)(av + 0xB4C);
        uint32_t node = ai && skg::readable(ai, 0x1DC) ? *(uint32_t*)(ai + 0x1D8) : 0;
        // the behaviour tree's events (a list: node +0 next, +0x10 type, +0x24 ended, +0x8.. data): "Fight" 0x60995FC
        int fights = 0, mine = 0;
        uint32_t target = *(uint32_t*)(av + 0xE7C), comb = *(uint32_t*)(av + 0x5A8);
        uintptr_t lp = H->sym("SporeCoop::BT_EventList_ptr");
        uint8_t* list = lp && skg::readable((void*)lp, 4) ? *(uint8_t**)lp : nullptr;
        std::string fx;
        if (list && skg::readable(list, 4))
            for (uint8_t* n = *(uint8_t**)list; n && n != list && skg::readable(n, 0x28) && fights < 64; n = *(uint8_t**)n)
                if (*(uint32_t*)(n + 0x10) == 0x60995FC && !n[0x24]) {
                    fights++;
                    uint32_t d0 = *(uint32_t*)(n + 8);
                    mine += d0 == target || d0 == comb;
                    char b[48];
                    snprintf(b, sizeof b, " %08X", d0);
                    fx += b;
                }
        H->out_printf(out, "heal flag=%d hp=%.1f food=%.2f cine=%d node=%08X target=%08X comb=%08X fights=%d mine=%d data=%s\n",
                      (int)av[0xFB8], *(float*)(av + 0x5E0), *(float*)(av + 0xBBC), (int)game::cinematic_playing(), node,
                      target, comb, fights, mine, fx.c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "meat")) { // tests (BUG-0557): NPC / proxy <id>: the body's meat (+0xB84) and dead flag
        uint8_t* c = (uint8_t*)(is_host() ? npc::host_npc((uint16_t)atoi(argv[2])) : npc::proxy((uint16_t)atoi(argv[2])));
        if (!c || !skg::readable(c, 0xBC0)) {
            H->out_printf(out, "no creature\n");
            return 1;
        }
        H->out_printf(out, "creature %p meat=%.2f dead=%d hp=%.2f\n", c, *(float*)(c + 0xB84), (int)c[0xB5E],
                      *(float*)(c + 0x5E0));
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "ufo")) { // tests (BUG-0558): the UFOs of this game, the puppets, the pick's answers
        H->out_printf(out, "%s", ufo_dump().c_str());
        return 0;
    }
    // tests (BUG-0501): roar <hex ability type>: our avatar uses that ability as the behaviour tree's action node does
    // (its first tick 0xD8E840 through the hook: the game's animation and the walk over the creatures in range)
    if (argc > 2 && !strcmp(argv[1], "roar")) {
        void* av = game::avatar();
        uintptr_t a = H->sym("SporeCoop::BT_SpecialAbilityTick");
        int idx = ability_index(av, strtoul(argv[2], nullptr, 16));
        if (!av || !a || idx < 0) {
            H->out_printf(out, "no avatar, tick or ability\n");
            return 1;
        }
        uint32_t st[16] = {(uint32_t)idx, 0, 0};
        int r = ((AbilityTick_t)a)(av, 0, 0, 0, 0, st);
        H->out_printf(out, "ability %d ticked: %d phase=%u\n", idx, r, st[1]);
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
    a_PostStim = H->sym("SporeCoop::BT_PostStimulus");
    a_GoalStart = H->sym("SporeCoop::BT_GoalSlotStart");
    a_StimClear = H->sym("SporeCoop::BT_ClearStimulus");
    a_GoalFind = H->sym("SporeCoop::BT_GoalSlotFind");
    a_GoalEnd = H->sym("SporeCoop::BT_GoalSlotEnd");
    a_IsEpic = H->sym("SporeCoop::cCreatureAnimal_IsEpic");
    p2_ufo_pick_ret = H->sym("SporeCoop::UFOPick_IsEpicReturn");
    if (p2_ufo_pick_ret && a_IsEpic)
        H->hook_create(M, (void*)a_IsEpic, (void*)p2_h_IsEpic, (void**)&p2_o_IsEpic);
    if (uintptr_t a = H->sym("SporeCoop::UFO_TickBehavior"))
        H->hook_create(M, (void*)a, (void*)h_UfoTick, (void**)&o_UfoTick);
    if (uintptr_t a = H->sym("SporeCoop::BT_SpecialAbilityTick"))
        H->hook_create(M, (void*)a, (void*)h_AbilityTick, (void**)&o_AbilityTick);
    H->command_add(M, "coop.rules", "coop.rules [stun <s> [npc id]] - the game's player rules carried between the games (P1, BUG-0554)",
                   c_rules, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace player_rules
