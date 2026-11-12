#include "player_rules.h"
#include "savestrip.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "drama.h"
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
    K_UFO = 3,         // host -> guest {u8 n, n x {vec3 pos, quat rot}}: the host's drama UFOs now (BUG-0558), 10 Hz, unreliable
    K_HOLD = 4,        // any -> host -> others {u8 player, u32 noun, vec3 where it lay}: the player's creature picked it up (BUG-0458)
    K_DROP = 5,        // any -> host -> others {u8 player, u32 noun}: it let go of it (thrown or put down)
    K_PUT = 6,         // any -> host -> others {u8 player, u32 noun, vec3 pos}: where it came to rest in the thrower's game
    K_SPIT_PLAYER = 7, // host -> guest {u8 player, f32 seconds}: an NPC's spit hit the guest's puppet (BUG-0641)
    K_SPIT_NPC = 8,    // guest -> host {u16 npc id, f32 seconds}: the guest's spit hit a host NPC's mirror (BUG-0641)
    K_STUN_PROXY = 9,  // host -> guests {u16 npc id, f32 seconds}: the host's game stunned its NPC (BUG-0700)
    K_FLOCK = 10,      // guest -> host {}: the guest's Summon Flock, made by the host's game for its puppet (BUG-0736)
};
static unsigned g_flock_sent, g_flock_got;

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
// BUG-0700 (class BUG-0419): a host NPC's stun (a charge's, anybody's) stayed in the host's game: the guests' proxies of
// it showed no stun (the stimulus 0x1000 and its stars). On: the host sends it, each guest's game stuns its proxy with
// the game's own CreatureStun. Off: not sent (0.7.10).
static bool g_stun_proxy_fix = true;
COOP_FIX("creature.stun_proxy", "BUG-0700", g_stun_proxy_fix, "creature: an NPC stunned in the host's game is stunned on the guests' screens too");
static uint32_t g_stun_sent_proxy, g_stun_got_proxy;

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
            int nid = g_stun_proxy_fix ? npc::host_id(c) : -1;
            if (nid >= 0) { // BUG-0700: the guests' proxies of this NPC show it too
                net::Writer w;
                w.u8(K_STUN_PROXY);
                w.u16((uint16_t)nid);
                w.f32(secs);
                C.sess->broadcast_reliable(net::MSG_PLAYER_RULE, w);
                g_stun_sent_proxy++;
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
static void spit_after_hit(void* attacker, void* victim, uint32_t idx); // BUG-0641 (below)

static bool SK_THISCALL h_AttackTarget(void* attacker, void* target, uint32_t idx, uint32_t a4)
{
    void* was = g_hit_attacker;
    uint32_t was_idx = g_hit_idx;
    g_hit_attacker = attacker;
    g_hit_idx = idx;
    bool r = hit_body(attacker, target, idx, a4);
    if (target) // (the game's spit rule in 0xC20390 runs before the damage, whatever the hit returns)
        spit_after_hit(attacker, (uint8_t*)target - game::kCombatOff, idx); // BUG-0641
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

// ---------------------------------------------------------------- BUG-0641
// A spit stops a charge: the ability hit 0xC20390, for an ability of type 0x20 (spit), stuns the target (vt 0x94,
// CreatureStun, the spit's seconds at ability +0x80) when the target's current ability (creature +0xE8C, an index
// into vt 0xB4) is a charge (type 0x1F). The charge runs in its owner's game only: a guest's spit hit the mirror of a
// charging host NPC (no ability running there) and an NPC's spit hit the puppet of a charging guest on the host: the
// rule found no charge either way. Now the hit's game sends the spit to the charge's game, which applies the same
// rule to its own creature (the NPC on the host, the avatar on the guest).
// `coop.fix creature.spit_counter off`: a spit stops only a charge of the game's own creatures (0.7.9).
static bool g_spit_fix = true;
COOP_FIX("creature.spit_counter", "BUG-0641", g_spit_fix, "creature: a spit stops a charge of the partner's creature or of a host NPC");
static uint32_t g_spit_seen, g_spit_sent, g_spit_got, g_spit_stopped;
constexpr uint32_t kSpitType = 0x20, kChargeType = 0x1F;
constexpr uintptr_t kCurAbility = 0xE8C; // creature: the ability running now (index, -1 none)
static uint8_t* ability_of(void* c, int idx)
{
    if (!c || idx < 0 || idx >= skg::vcall<int>(c, 0xB0 / 4))
        return nullptr;
    uint8_t* ab = skg::vcall<uint8_t*>(c, 0xB4 / 4, idx);
    return ab && skg::readable(ab, 0x84) ? ab : nullptr;
}
static bool charging(void* c)
{
    if (!c || !skg::readable((uint8_t*)c + kCurAbility, 4))
        return false;
    uint8_t* ab = ability_of(c, *(int*)((uint8_t*)c + kCurAbility));
    return ab && *(uint32_t*)(ab + 8) == kChargeType;
}
// The charge's own hit ends it (the charger reaches its target): a spit the partner's game landed on the mirror while
// the charge ran there arrives here up to a round trip and the mirror's lag later. A charge that hit within
// kSpitGraceMs counts as running for a spit from the partner's game (the charges' last hits, by creature).
constexpr uint32_t kSpitGraceMs = 1000;
struct ChargeHit {
    void* c;
    uint32_t ms;
};
static ChargeHit g_charge_hits[16];
static uint32_t g_spit_late; // spits that stopped a charge that had just hit
static void note_charge_hit(void* c)
{
    ChargeHit* slot = &g_charge_hits[0];
    for (auto& h : g_charge_hits) {
        if (h.c == c) {
            slot = &h;
            break;
        }
        if (h.ms < slot->ms)
            slot = &h;
    }
    *slot = {c, GetTickCount()};
}
static bool charged_just_now(void* c)
{
    for (auto& h : g_charge_hits)
        if (h.c == c && GetTickCount() - h.ms <= kSpitGraceMs)
            return true;
    return false;
}
// after the game's hit: a spit on the partner's creature or an NPC's mirror goes to the game that runs its charge
static void spit_after_hit(void* attacker, void* victim, uint32_t idx)
{
    if (!g_spit_fix || !attacker || !victim || !game::in_creature_stage())
        return;
    uint8_t* ab = ability_of(attacker, (int)idx);
    if (ab && *(uint32_t*)(ab + 8) == kChargeType)
        note_charge_hit(attacker);
    if (!ab || *(uint32_t*)(ab + 8) != kSpitType)
        return;
    g_spit_seen++;
    float secs = *(float*)(ab + 0x80);
    if (!std::isfinite(secs) || secs <= 0 || secs > 30)
        return;
    net::Writer w;
    if (is_host()) {
        uint8_t p = puppet_player(victim);
        if (p == 0xFF || puppet_player(attacker) != 0xFF)
            return; // (a partner's spit on a puppet is that partner's game's)
        w.u8(K_SPIT_PLAYER);
        w.u8(p);
        w.f32(secs);
        C.sess->send_reliable_to(p, net::MSG_PLAYER_RULE, w);
    } else if (is_guest()) {
        if (attacker != game::avatar() || !npc::is_proxy(victim))
            return;
        w.u8(K_SPIT_NPC);
        w.u16(npc::id_of(victim));
        w.f32(secs);
        C.sess->send_reliable_to(0, net::MSG_PLAYER_RULE, w);
    } else
        return;
    g_spit_sent++;
}
static void spit_rx(void* c, float secs)
{
    g_spit_got++;
    if (!o_Stun || !c || game::is_dead(c))
        return;
    bool now = charging(c), late = !now && charged_just_now(c);
    if (g_spit_got <= 20)
        H->log(M, SK_INFO, "rules: a spit from the partner's game on %p (%.1f s): charging now %d, its charge hit just before %d",
               c, secs, (int)now, (int)late);
    if (!now && !late)
        return;
    o_Stun(c, secs); // the game's own rule: the charge's creature is stunned
    g_spit_stopped++;
    g_spit_late += late;
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
        if (ab && *(uint32_t*)(ab + 8) == 0x33 && drama::g_flock_relay) { // BUG-0736: the own Summon Flock
            net::Writer w;
            w.u8(K_FLOCK);
            C.sess->send_reliable_to(0, net::MSG_PLAYER_RULE, w);
            g_flock_sent++;
        }
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

// ---------------------------------------------------------------- BUG-0458, carried and thrown objects
// A creature carries a rock or a stick in a hold slot (creature +0xE40, Creature_HoldObject 0xC15B20 /
// Creature_ReleaseObject 0xC15EB0, docs/re/creature.md#creature-hold-slots). The player's own pickup and throw run in
// its own game only: the partner saw its creature walk with empty hands and the stick lying where it was, and after a
// throw the object lay in another place in each game. Now the own creature's hold / release is sent; the partner's game
// puts its copy of the object (the object of that noun nearest to where it lay, within 6 m: rocks and ornaments of the
// save are at the same places in both games) into the hold slot of the player's puppet, lets go of it on the release,
// and puts it where it came to rest in the thrower's game (sent when it stopped moving, or after 6 s).
// `coop.fix creature.carry_relay off`: carried objects stay in the carrier's game (0.7.8).
static bool g_carry_fix = true;
COOP_FIX("creature.carry_relay", "BUG-0458", g_carry_fix, "creature: the partner sees the stick or rock a player carries and where it lands");
static constexpr uint32_t kRockNoun = 0x02A8FB3Fu, kOrnamentNoun = 0x03A2511Eu;
// obj is the object's cSpatialObject (0x1186577; vt 0xB8 casts it to its cGameData, 0x17F243B); both return a bool
typedef bool(SK_THISCALL* Hold_t)(void*, void*, uint32_t, bool);
typedef bool(SK_THISCALL* Release_t)(void*, void*, int, int);
static Hold_t o_Hold;
static Release_t o_Release;
static uint32_t g_hold_tx, g_hold_rx, g_hold_done, g_hold_nocopy, g_drop_tx, g_drop_rx, g_put_tx, g_put_rx, g_hold_other;
static uint32_t g_hold_last_noun, g_put_fast, g_put_near;
struct Carried {        // the partner's copy of an object a player carries (or threw), in this game
    void* obj = nullptr;
    uint32_t noun = 0;
    bool held = false;
    bool dropped = false; // the player let go of it: released here at its first rest point
};
static std::map<uint8_t, Carried> g_carried; // player id -> its object here
// BUG-0814 (not reproduced, docs/re/creature.md#creature-freed-block-reuse): the game's destroy of a holding puppet
// releases its hold slot and the copy falls; `coop.rules carry` shows the last copy held for each player (tests)
static std::map<uint8_t, std::pair<void*, uint32_t>> g_last_copy; // tests: player -> (object, noun) last held here
struct Thrown {          // this game's own object after the own creature let go of it: watched until it rests
    void* obj = nullptr;
    uint32_t noun = 0;
    double t0 = 0, still_since = 0;
    net::Vec3 last{};       // where it was at still_since
    net::Vec3 sent{1e9f, 0, 0}; // the position sent last
    double t_moved = 0;         // when it last moved 5 cm or more
};
static std::vector<Thrown> g_thrown;
struct Put {             // the partner's copy put where the thrower's object rests: held there for 3 s (its own fall
    void* obj;           // and roll after the release moved it 14-17 m in the other game: lab 20261105-234535)
    uint32_t noun;
    net::Vec3 pos;
    double until;
};
static std::vector<Put> g_puts;
// the last position of an object both games agreed on (a rest point sent, or a put of the partner's): the identity of a
// carried object is its noun and this position (a creature walking into the copy pushed it 6-7 m away in one game:
// matched by its position then the other game had no object there, lab 20261106-001636)
static std::map<void*, net::Vec3> g_synced;
static bool g_in_relay = false;

static void* spatial_data(void* sp) // the cGameData of a cSpatialObject (0xAE6650 does the same)
{
    return sp && skg::readable(sp, 0x80) ? skg::vcall<void*>(sp, 0xB8 / 4, 0x17F243Bu) : nullptr;
}

static bool carry_noun(void* sp, void** gd, uint32_t* noun)
{
    void* o = spatial_data(sp);
    *gd = o;
    if (!o)
        return false;
    uint32_t n = game::noun_id(o);
    g_hold_last_noun = n;
    *noun = n;
    return n == kRockNoun || n == kOrnamentNoun;
}

static bool obj_listed(void* o, uint32_t noun) // a pointer kept across frames: still an object of that noun in the list
{
    struct Q {
        void* o;
        uint32_t n;
        bool found;
    } q{o, noun, false};
    game::for_each_object(
        [](void* x, uint32_t n, void* u) {
            auto* q = (Q*)u;
            if (x == q->o && n == q->n)
                q->found = true;
            return !q->found;
        },
        &q);
    return q.found && game::object_alive(o);
}

static void carry_send(uint8_t kind, uint8_t player, uint32_t noun, const net::Vec3* p, int except = -1)
{
    net::Writer w;
    w.u8(kind);
    w.u8(player);
    w.u32(noun);
    if (p)
        w.f32(p->x), w.f32(p->y), w.f32(p->z);
    if (is_host()) {
        for (auto& pl : C.sess->players())
            if (pl.id != 0 && (int)pl.id != except)
                C.sess->send_reliable_to(pl.id, net::MSG_PLAYER_RULE, w);
    } else if (is_guest())
        C.sess->send_reliable_to(0, net::MSG_PLAYER_RULE, w);
}

static uint8_t own_id() { return is_host() ? 0 : C.sess ? C.sess->my_id() : 0xFF; }

static bool SK_THISCALL h_Hold(void* c, void* obj, uint32_t slot, bool replace)
{
    uint32_t noun = 0;
    net::Vec3 at{};
    void* gd = nullptr;
    bool mine = !g_in_relay && g_carry_fix && game::in_creature_stage() && c && c == game::avatar() &&
                (is_host() || is_guest()) && carry_noun(obj, &gd, &noun) && game::object_position(gd, at);
    if (!mine && c == game::avatar())
        g_hold_other++;
    if (mine) {
        auto sy = g_synced.find(gd);
        if (sy != g_synced.end() && net::dist(sy->second, at) < 15.f)
            at = sy->second;
    }
    bool r = o_Hold(c, obj, slot, replace);
    if (mine && r) {
        for (auto it = g_thrown.begin(); it != g_thrown.end();)
            it = it->obj == gd ? g_thrown.erase(it) : std::next(it);
        carry_send(K_HOLD, own_id(), noun, &at);
        g_hold_tx++;
        H->log(M, SK_INFO, "rules: the own creature picked up a %s at (%.1f %.1f %.1f): sent (creature.carry_relay)",
               noun == kRockNoun ? "rock" : "stick / ornament", at.x, at.y, at.z);
    }
    return r;
}

static bool SK_THISCALL h_Release(void* c, void* obj, int a, int b)
{
    uint32_t noun = 0;
    void* gd = nullptr;
    bool mine = !g_in_relay && g_carry_fix && game::in_creature_stage() && c && c == game::avatar() &&
                (is_host() || is_guest()) && carry_noun(obj, &gd, &noun);
    bool r = o_Release(c, obj, a, b);
    if (mine && r) {
        carry_send(K_DROP, own_id(), noun, nullptr);
        g_drop_tx++;
        Thrown t;
        t.obj = gd;
        t.noun = noun;
        t.t0 = t.still_since = t.t_moved = H->time_seconds();
        game::object_position(gd, t.last);
        g_thrown.push_back(t);
    }
    return r;
}

static void carry_tick(double now)
{
    for (auto it = g_thrown.begin(); it != g_thrown.end();) { // own thrown objects: where they come to rest
        net::Vec3 p;
        if (!obj_listed(it->obj, it->noun) || !game::object_position(it->obj, p)) {
            it = g_thrown.erase(it);
            continue;
        }
        // where it is, 10 times a second when it is 0.3 m from the position sent last, also while it rolls fast (the
        // integration run's rock rolled on at over 1 m/s for 40 s after its release on the nest mound: with only its
        // slow points sent the copy lay 8.5 m from it, rel/0.7.9 out/lab/20261106-013035). The partner's copy follows
        // with a held object's physics and never within 4 m of the partner's own creature (a copy with its own physics
        // teleported along a flight hit that creature: 91 damage, lab 20261105-235733)
        if (now - it->still_since >= 0.1) {
            float v = net::dist(p, it->last) / (float)(now - it->still_since);
            it->still_since = now;
            it->last = p;
            if (v > 0.05f)
                it->t_moved = now;
            if (v >= 1.0f)
                g_put_fast++;
            if (net::dist(p, it->sent) > 0.3f || (v <= 0.05f && net::dist(p, it->sent) > 0.05f)) {
                carry_send(K_PUT, own_id(), it->noun, &p);
                g_put_tx++;
                it->sent = p;
                g_synced[it->obj] = p;
            }
        }
        if (now - it->t0 >= 60.0) // (60 s: a rock that lay still for a while may be pushed on again, by its thrower too)
            it = g_thrown.erase(it);
        else
            ++it;
    }
    for (auto it = g_puts.begin(); it != g_puts.end();) {
        void* sp = obj_listed(it->obj, it->noun) ? game::cast(it->obj, 0x1186577) : nullptr;
        if (!sp || now > it->until) {
            it = g_puts.erase(it);
            continue;
        }
        // never into or next to the own creature: an object teleported again and again by it crushed it (hit by none,
        // 142-348 damage: lab 20261106-001427, -002414); a copy left rolling there lies elsewhere (accepted)
        void* av = game::avatar();
        if (av && net::dist(game::position(av), it->pos) < 4.0f) {
            g_put_near++;
            ++it;
            continue;
        }
        const float* q = (const float*)((const uint8_t*)sp + 0x10); // its orientation stays
        float pp[3] = {it->pos.x, it->pos.y, it->pos.z}, qq[4] = {q[0], q[1], q[2], q[3]};
        skg::vcall<void>(sp, 0x44 / 4, pp, qq); // Teleport (as worldobj.cpp's vehicles)
        // and its physics as a held object's (0xD7D1A0: component 0x116DD1B +0x268 = 0, +0x1F0 = 3): the copy lies where
        // the thrower's rests instead of rolling off with the speed of its release here (5-115 m: lab 20261106-002616,
        // -002757); a creature picking it up and letting go of it gives it its physics back (0xC15D30)
        if (uint8_t* ph = skg::vcall<uint8_t*>(sp, 0xB8 / 4, 0x116DD1Bu))
            if (skg::readable(ph, 0x270)) {
                ph[0x268] = 0;
                *(uint32_t*)(ph + 0x1F0) = 3;
            }
        ++it;
    }
    static double s_prune_at = 0; // (object list walks: once a second)
    if (now - s_prune_at < 1.0)
        return;
    s_prune_at = now;
    for (auto it = g_synced.begin(); it != g_synced.end();) // (a few entries: the objects carried in this session)
        it = obj_listed(it->first, kRockNoun) || obj_listed(it->first, kOrnamentNoun) ? std::next(it) : g_synced.erase(it);
    for (auto& [id, cr] : g_carried) { // a copy the game freed (a load, the stage's end)
        if (cr.obj && !obj_listed(cr.obj, cr.noun))
            cr = Carried{};
    }
}

static void carry_rx(uint8_t kind, uint8_t from, net::Reader& r)
{
    uint8_t player = r.u8();
    uint32_t noun = r.u32();
    net::Vec3 p{};
    if (kind != K_DROP)
        p = {r.f32(), r.f32(), r.f32()};
    if (!r.ok() || !g_carry_fix || !game::in_creature_stage() || (noun != kRockNoun && noun != kOrnamentNoun))
        return;
    if (is_host()) {
        if (player != from) // a guest speaks for its own creature only
            return;
        carry_send(kind, player, noun, kind == K_DROP ? nullptr : &p, from); // the other guests
    } else if (from != 0)
        return;
    void* pup = C.puppet_of ? C.puppet_of(player) : nullptr;
    Carried& cr = g_carried[player];
    g_in_relay = true;
    if (kind == K_HOLD) {
        g_hold_rx++;
        if (cr.held && cr.obj && pup && obj_listed(cr.obj, cr.noun))
            o_Release(pup, game::cast(cr.obj, 0x1186577), 0, 0);
        cr = Carried{};
        void* o = game::find_object(noun, p, 6.0f); // (the copy may have rolled a few m after its last rest point)
        void* sp = o ? game::cast(o, 0x1186577) : nullptr;
        if (sp && pup && game::is_puppet(pup) && o_Hold(pup, sp, 0, true)) {
            cr.obj = o;
            cr.noun = noun;
            cr.held = true;
            g_last_copy[player] = {o, noun};
            g_hold_done++;
        } else {
            g_hold_nocopy++;
            H->log(M, SK_WARN, "rules: player %u picked up a %08X at (%.1f %.1f %.1f): no copy held here (object %p, puppet %p)",
                   player, noun, p.x, p.y, p.z, o, pup);
        }
    } else if (kind == K_DROP) {
        g_drop_rx++; // the copy stays in the puppet's hand until the thrower's object rests (K_PUT): no flight here
        cr.dropped = true;
    } else if (kind == K_PUT) {
        g_put_rx++;
        if (cr.held && cr.dropped && cr.obj && obj_listed(cr.obj, cr.noun)) {
            if (pup)
                o_Release(pup, game::cast(cr.obj, 0x1186577), 0, 0);
            cr.held = false;
        }
        if (cr.obj && !cr.held && obj_listed(cr.obj, cr.noun)) { // (the copy stays the player's for the next rest point)
            for (auto it = g_puts.begin(); it != g_puts.end();)
                it = it->obj == cr.obj ? g_puts.erase(it) : std::next(it);
            g_puts.push_back({cr.obj, cr.noun, p, H->time_seconds() + 3.0});
            g_synced[cr.obj] = p;
        }
    }
    g_in_relay = false;
}

static std::string carry_dump()
{
    std::string out;
    char b[200];
    auto slots = [&](const char* who, void* c) {
        if (!c || !skg::readable(c, 0xE48))
            return;
        uint8_t* v0 = *(uint8_t**)((uint8_t*)c + 0xE40), *v1 = *(uint8_t**)((uint8_t*)c + 0xE44);
        for (uint8_t* s = v0; s && s < v1 && skg::readable(s, 0x24); s += 0x24) {
            void* o = spatial_data(*(void**)(s + 0x20));
            net::Vec3 p{};
            if (o)
                game::object_position(o, p);
            snprintf(b, sizeof b, "hold %s socket=%d obj=%p noun=%08X pos=(%.1f %.1f %.1f)\n", who, *(int*)s, o,
                     o ? game::noun_id(o) : 0, p.x, p.y, p.z);
            out += b;
        }
    };
    slots("own", game::avatar());
    for (uint8_t id = 0; id < 8 && C.puppet_of; id++)
        if (void* pp = C.puppet_of(id)) {
            char w[16];
            snprintf(w, sizeof w, "puppet%u", id);
            slots(w, pp);
        }
    for (auto& [id, cr] : g_carried) {
        net::Vec3 p{};
        if (cr.obj && obj_listed(cr.obj, cr.noun))
            game::object_position(cr.obj, p);
        snprintf(b, sizeof b, "copy of player %u obj=%p held=%d pos=(%.1f %.1f %.1f)\n", id, cr.obj, (int)cr.held, p.x, p.y, p.z);
        out += b;
    }
    // BUG-0814 (tests): the last copy held for each player, also after its carry state ended: the game's own fields
    // (the held object +0x71: 1 = held by a creature; its physics component 0x116DD1B +0x1F0: 3 = attached)
    for (auto& [id, lc] : g_last_copy) {
        bool l = obj_listed(lc.first, lc.second);
        void* sp = l ? game::cast(lc.first, 0x1186577) : nullptr;
        void* ph = l ? game::cast(lc.first, 0x116DD1B) : nullptr;
        net::Vec3 q{};
        if (l)
            game::object_position(lc.first, q);
        snprintf(b, sizeof b, "last copy of player %u obj=%p listed=%d game_held=%d phys=%d pos=(%.1f %.1f %.1f)\n",
                 id, lc.first, (int)l, sp && skg::readable((uint8_t*)sp + 0x71, 1) ? (int)*((uint8_t*)sp + 0x71) : -1,
                 ph && skg::readable((uint8_t*)ph + 0x1F0, 4) ? *(int*)((uint8_t*)ph + 0x1F0) : -1, q.x, q.y, q.z);
        out += b;
    }
    snprintf(b, sizeof b, "carry fix=%d hold_tx=%u hold_rx=%u done=%u nocopy=%u drop_tx=%u drop_rx=%u put_tx=%u put_rx=%u "
             "other=%u last_noun=%08X fast=%u near=%u\n", (int)g_carry_fix, g_hold_tx, g_hold_rx, g_hold_done, g_hold_nocopy,
             g_drop_tx, g_drop_rx, g_put_tx, g_put_rx, g_hold_other, g_hold_last_noun, g_put_fast, g_put_near);
    return out + b;
}

// ---------------------------------------------------------------- BUG-0558, the host's UFO on the guest
// The drama UFO is an event of the host's world (a guest's own game starts none: creature.guest_events, BUG-0486), so the
// guest never saw the ship that came over the players (the report: the host saw the ship and its beam, the guest's game
// at the same second nothing). Now the host sends its ships' poses 10 times a second while it has any, and the guest's
// game shows a ship of its own at each: a type 7 ship made the way the event makes it (UFOAbductionEvent_SpawnUFO), not
// ticked (no behaviour tree runs on it: it takes nothing in the guest's world) and placed at the host's ship every frame;
// it goes (deferred destroy) when the host's ship is gone, when no pose came for 1.5 s and when the guest leaves Creature.
// `coop.fix creature.ufo_mirror off`: the guest sees no ship (0.7.8).
static bool g_ufo_mirror_fix = true;
COOP_FIX("creature.ufo_mirror", "BUG-0558", g_ufo_mirror_fix, "creature: the guest sees the UFO that visits the host's world");
struct UfoMirror {
    void* u = nullptr;
    net::Vec3 pos{};
    net::Quat rot{0, 0, 0, 1};
};
static std::vector<UfoMirror> g_ufo_mirrors;
static double g_ufo_rx_at = -1e9, g_ufo_tx_at = 0, g_ufo_had_at = -1e9;
static uint32_t g_ufo_tx, g_ufo_rx, g_ufo_made, g_ufo_gone, g_ufo_freed;
static unsigned g_ufo_stripped; // BUG-0817: saves' references to a mirror written as null
static uintptr_t a_SpawnEventUFO;

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
    char b2[200];
    snprintf(b2, sizeof b2, "mirror fix=%d mirrors=%u tx=%u rx=%u made=%u gone=%u freed=%u stripped=%u\n", (int)g_ufo_mirror_fix,
             (unsigned)g_ufo_mirrors.size(), g_ufo_tx, g_ufo_rx, g_ufo_made, g_ufo_gone, g_ufo_freed, g_ufo_stripped);
    return u.out + b + b2;
}


static void ufo_mirror_drop(size_t i)
{
    if (g_ufo_mirrors[i].u && game::ufo_listed(g_ufo_mirrors[i].u) && game::object_alive(g_ufo_mirrors[i].u)) {
        game::destroy_creature(g_ufo_mirrors[i].u); // the deferred path (any cGameData)
        g_ufo_gone++;
    }
    g_ufo_mirrors.erase(g_ufo_mirrors.begin() + (long)i);
}

static void ufo_mirror_rx(const std::vector<std::pair<net::Vec3, net::Quat>>& v)
{
    g_ufo_rx++;
    g_ufo_rx_at = H->time_seconds();
    if (!g_ufo_mirror_fix || !game::in_creature_stage())
        return;
    while (g_ufo_mirrors.size() > v.size())
        ufo_mirror_drop(g_ufo_mirrors.size() - 1);
    for (size_t i = 0; i < v.size(); i++) {
        if (i >= g_ufo_mirrors.size())
            g_ufo_mirrors.push_back({});
        g_ufo_mirrors[i].pos = v[i].first;
        g_ufo_mirrors[i].rot = v[i].second;
    }
}

static void ufo_place(void* u, const net::Vec3& p, const net::Quat& q)
{
    void* sp = game::cast(u, 0x1186577); // cSpatialObject
    if (!sp)
        return;
    struct { float x, y, z; } vp{p.x, p.y, p.z};
    struct { float x, y, z, w; } vq{q.x, q.y, q.z, q.w};
    skg::vcall<void>(sp, 0x44 / 4, &vp, &vq); // SetPositionAndOrientation (as game::teleport)
    // the ship's own flight (cGameDataUFO next position +0x718, destination +0x750, docs/re/space.md) put it back to
    // where it was made in every other frame (lab 20261105-232301): both are the host's pose too
    memcpy((uint8_t*)u + 0x718, &vp, 12);
    memcpy((uint8_t*)u + 0x750, &vp, 12);
}

static constexpr uint32_t kUfoNounM = 0x018EBADCu;

// BUG-0817: the mirror ship is a real type 7 UFO of the guest's game: a save the guest made while it was over the
// players wrote it like the game's own (only Space had a ship filter, squadron.cpp BUG-0522), and the guest's world
// had the host's ship back after a solo load (coop_ufo_mirror_save.py). A savestrip filter writes a null reference
// for it (the noun list skips nulls on load; the mirror is in no other list of the game: it is made by the event's
// spawn function without an event). `coop.fix creature.ufo_save_strip off`: saved (0.7.12).
static bool g_ufo_strip_fix = true;
COOP_FIX("creature.ufo_save_strip", "BUG-0817", g_ufo_strip_fix, "saves: the guest's mirror of the host's UFO is kept out of the guest's saves");
static bool ufo_save_filter(void* o, uint32_t noun)
{
    if (!g_ufo_strip_fix || noun != kUfoNounM || g_ufo_mirrors.empty())
        return false;
    for (auto& m : g_ufo_mirrors)
        if (m.u == o) {
            if (!g_ufo_stripped++)
                H->log(M, SK_INFO, "rules: the mirror of the host's UFO is kept out of this save (creature.ufo_save_strip)");
            return true;
        }
    return false;
}

void tick(double now)
{
    if (g_carry_fix && (!g_thrown.empty() || !g_puts.empty() || !g_carried.empty() || !g_synced.empty()))
        carry_tick(now);
    if (is_host() && game::in_creature_stage() && now - g_ufo_tx_at >= 0.1) {
        g_ufo_tx_at = now;
        struct Q {
            std::vector<std::pair<net::Vec3, net::Quat>> v;
        } q;
        game::for_each_object(
            [](void* o, uint32_t noun, void* p) {
                net::Vec3 v;
                auto* q = (Q*)p;
                void* sp = noun == kUfoNounM && game::object_alive(o) ? game::cast(o, 0x1186577) : nullptr;
                if (sp && game::object_position(o, v) && q->v.size() < 8) {
                    const float* r = (const float*)((const uint8_t*)sp + 0x10); // cSpatialObject::mOrientation
                    q->v.push_back({v, net::Quat{r[0], r[1], r[2], r[3]}});
                }
                return true;
            },
            &q);
        if (!q.v.empty())
            g_ufo_had_at = now;
        if (g_ufo_mirror_fix && (!q.v.empty() || now - g_ufo_had_at < 1.0)) { // (a few empty ones after the last ship)
            net::Writer w;
            w.u8(K_UFO);
            w.u8((uint8_t)q.v.size());
            for (auto& [p, r] : q.v) {
                w.f32(p.x), w.f32(p.y), w.f32(p.z);
                w.f32(r.x), w.f32(r.y), w.f32(r.z), w.f32(r.w);
            }
            for (auto& pl : C.sess->players())
                if (pl.id != 0)
                    C.sess->send_unreliable_to(pl.id, net::MSG_PLAYER_RULE, w);
            g_ufo_tx++;
        }
    }
    if (g_ufo_mirrors.empty())
        return;
    if (!is_guest() || !g_ufo_mirror_fix || !game::in_creature_stage() || now - g_ufo_rx_at > 1.5) {
        while (!g_ufo_mirrors.empty())
            ufo_mirror_drop(g_ufo_mirrors.size() - 1);
        return;
    }
    for (auto& m : g_ufo_mirrors) {
        if (m.u && (!game::ufo_listed(m.u) || !game::object_alive(m.u))) { // freed by the game (a load, a cleanup)
            m.u = nullptr;
            g_ufo_freed++;
        }
        if (!m.u && a_SpawnEventUFO) {
            struct { float x, y, z; } vp{m.pos.x, m.pos.y, m.pos.z};
            using Spawn_t = void*(SK_STDCALL*)(const void*, float, bool);
            m.u = ((Spawn_t)a_SpawnEventUFO)(&vp, 0.f, false);
            if (m.u) {
                g_ufo_made++;
                H->log(M, SK_INFO, "rules: the host's UFO is shown here at (%.1f %.1f %.1f) (creature.ufo_mirror)", m.pos.x,
                       m.pos.y, m.pos.z);
            }
        }
        if (m.u)
            ufo_place(m.u, m.pos, m.rot);
    }
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
        if (g_stun_proxy_fix) { // BUG-0700: every guest's proxy of it, the stunning guest's too
            net::Writer w;
            w.u8(K_STUN_PROXY);
            w.u16(id);
            w.f32(secs);
            C.sess->broadcast_reliable(net::MSG_PLAYER_RULE, w);
            g_stun_sent_proxy++;
        }
    } else if (kind == K_STUN_PROXY) { // BUG-0700
        uint16_t id = r.u16();
        float secs = r.f32();
        if (!r.ok() || from != 0 || !is_guest())
            return;
        if (!std::isfinite(secs) || secs <= 0 || secs > 30) {
            g_stun_bad++;
            return;
        }
        void* c = npc::proxy(id);
        if (!o_Stun || !c || !game::in_creature_stage() || game::is_dead(c))
            return;
        o_Stun(c, secs); // the game's own stun (not the hook: nothing goes back to the host)
        g_stun_got_proxy++;
    } else if (kind == K_SPIT_PLAYER || kind == K_SPIT_NPC) { // BUG-0641
        bool to_player = kind == K_SPIT_PLAYER;
        uint8_t p = to_player ? r.u8() : 0;
        uint16_t id = to_player ? 0 : r.u16();
        float secs = r.f32();
        if (!r.ok() || !std::isfinite(secs) || secs <= 0 || secs > 30 || !game::in_creature_stage())
            return;
        if (to_player && from == 0 && is_guest() && p == C.sess->my_id())
            spit_rx(game::avatar(), secs);
        else if (!to_player && from != 0 && is_host())
            spit_rx(npc::host_npc(id), secs);
    } else if (kind == K_HOLD || kind == K_DROP || kind == K_PUT) {
        carry_rx(kind, from, r);
    } else if (kind == K_UFO) {
        uint8_t n = r.u8();
        std::vector<std::pair<net::Vec3, net::Quat>> v;
        for (uint8_t i = 0; i < n && i < 8 && r.ok(); i++) {
            net::Vec3 p{r.f32(), r.f32(), r.f32()};
            net::Quat q{r.f32(), r.f32(), r.f32(), r.f32()};
            v.push_back({p, q});
        }
        if (r.ok() && from == 0 && is_guest())
            ufo_mirror_rx(v);
    } else if (kind == K_FLOCK) { // BUG-0736
        if (from == 0 || !is_host() || !drama::g_flock_relay || !game::in_creature_stage())
            return;
        g_flock_got++;
        void* p = C.puppet_of ? C.puppet_of(from) : nullptr;
        if (p && !game::is_dead(p))
            drama::summon_for(p);
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
             "player_rules: stun fix=%d sent_player=%u sent_npc=%u got_player=%u got_npc=%u bad=%u last=%.2f hook=%d "
             "sent_proxy=%u got_proxy=%u\n",
             (int)g_stun_fix, g_stun_sent_player, g_stun_sent_npc, g_stun_got_player, g_stun_got_npc, g_stun_bad,
             g_stun_last, (int)(o_Stun != nullptr), g_stun_sent_proxy, g_stun_got_proxy);
    std::string out = b;
    snprintf(b, sizeof b, "player_rules: air fix=%d dropped=%u passed=%u guest_dropped=%u hook=%d\n", (int)g_air_fix,
             g_air_dropped, g_air_passed, g_air_dropped_guest, (int)(o_AttackTarget != nullptr));
    out += b;
    snprintf(b, sizeof b, "player_rules: spit fix=%d seen=%u sent=%u got=%u stopped=%u late=%u\n", (int)g_spit_fix, g_spit_seen, g_spit_sent,
             g_spit_got, g_spit_stopped, g_spit_late);
    out += b;
    snprintf(b, sizeof b, "player_rules: allies fix=%d answers=%u hook=%d\n", (int)g_allies_fix, g_allies_answers,
             (int)(o_Friendly != nullptr));
    out += b;
    snprintf(b, sizeof b, "player_rules: roar fix=%d sent=%u got=%u played=%u heard=%u bad=%u last=0x%X hook=%d\n", (int)g_roar_fix,
             g_roar_sent, g_roar_got, g_roar_played, g_roar_hit, g_roar_bad, g_roar_last_type, (int)(o_AbilityTick != nullptr));
    out += b;
    snprintf(b, sizeof b, "player_rules: ufo fix=%d seen=%u skipped=%u\n", (int)g_ufo_fix, g_ufo_seen, g_ufo_skipped);
    out += b;
    snprintf(b, sizeof b, "player_rules: flock fix=%d sent=%u got=%u\n", (int)drama::g_flock_relay, g_flock_sent, g_flock_got);
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
    if (argc > 4 && !strcmp(argv[1], "use")) { // tests (BUG-0641): use <type hex> <from> <to>: me | p<player> | <npc id>
        auto who = [](const char* a) -> void* {
            if (!strcmp(a, "me"))
                return game::avatar();
            if (a[0] == 'p')
                return C.puppet_of ? C.puppet_of((uint8_t)atoi(a + 1)) : nullptr;
            uint16_t id = (uint16_t)atoi(a);
            return is_host() ? npc::host_npc(id) : npc::proxy(id);
        };
        uint32_t type = (uint32_t)strtoul(argv[2], nullptr, 16);
        void* a = who(argv[3]);
        void* t = who(argv[4]);
        uintptr_t pa = H->sym("Simulator::cCreatureBase::PlayAbility");
        if (!a || !t || !pa) {
            H->out_printf(out, "no attacker, target or PlayAbility\n");
            return 1;
        }
        int idx = -1, cnt = skg::vcall<int>(a, 0xB0 / 4);
        for (int i = 0; i < cnt && idx < 0; i++) {
            uint8_t* ab = skg::vcall<uint8_t*>(a, 0xB4 / 4, i);
            if (ab && skg::readable(ab, 0x10) && *(uint32_t*)(ab + 8) == type)
                idx = i;
        }
        if (idx < 0) {
            H->out_printf(out, "no ability of type %X\n", type);
            return 1;
        }
        skg::vcall<void>(a, 0x84 / 4, (uint8_t*)t + game::kCombatOff, 1, 0);
        int r = skg::thiscall<int>(pa, a, idx, (void*)nullptr);
        H->out_printf(out, "ability %d (type %X) played: %d\n", idx, type, r);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "charging")) { // tests (BUG-0641): charging me|p<player>|<npc id>: runs a charge now
        void* c = !strcmp(argv[2], "me") ? game::avatar()
                  : argv[2][0] == 'p'     ? (C.puppet_of ? C.puppet_of((uint8_t)atoi(argv[2] + 1)) : nullptr)
                  : is_host()             ? npc::host_npc((uint16_t)atoi(argv[2]))
                                          : npc::proxy((uint16_t)atoi(argv[2]));
        int cur = c && skg::readable((uint8_t*)c + kCurAbility, 4) ? *(int*)((uint8_t*)c + kCurAbility) : -2;
        uint8_t* ab = ability_of(c, cur);
        H->out_printf(out, "charging=%d current=%d type=%X\n", (int)charging(c), cur, ab ? *(uint32_t*)(ab + 8) : 0u);
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
    // tests (BUG-0557): cursor <id>: the Creature stage's cursor kind over NPC / proxy <id> (0xD30830: 8 eat, 7 attack,
    // 0xD socialize, 0x1C walk ...); click <id>: the player's click on it (0xD32FD0, the game's own action for the kind)
    if (argc > 2 && (!strcmp(argv[1], "cursor") || !strcmp(argv[1], "click"))) {
        void* c = is_host() ? npc::host_npc((uint16_t)atoi(argv[2])) : npc::proxy((uint16_t)atoi(argv[2]));
        uintptr_t a_cur = H->sym("SporeCoop::CreatureCursorKind"), a_click = H->sym("SporeCoop::CreatureClickObject"),
                  a_strat = H->sym("App::cCreatureModeStrategy::Get");
        if (!c || !a_cur || !a_click || !a_strat) {
            H->out_printf(out, "no creature / symbol\n");
            return 1;
        }
        int kind = skg::cdecl_call<int>(a_cur, c);
        if (!strcmp(argv[1], "click") && kind > 4 && kind != 0x1C) { // (0x1C reads ECX: see clickobj)
            void* st = skg::cdecl_call<void*>(a_strat);
            if (st)
                skg::thiscall<void>(a_click, st, c);
        }
        uint8_t* b = (uint8_t*)c;
        uint8_t* av = (uint8_t*)game::avatar();
        uint8_t* ai = av ? *(uint8_t**)(av + 0xB4C) : nullptr;
        net::Vec3 ap = av ? game::position(av) : net::Vec3{};
        // the eat node (start 0xD70E40, tick 0xD70FD0) needs the avatar's target (+0xE7C) to be the body, the body
        // dead (+0xB5E), +0x135 set and meat (+0xB84) above 0; +0xB50 is the creature eating it
        H->out_printf(out, "cursor kind=%d meat=%.2f dead=%d b135=%d eater=%p av=%p av_food=%.2f av_node=%08X av_target=%08X "
                           "av_pos=(%.1f %.1f %.1f) body_pos=(%.1f %.1f %.1f)\n",
                      kind, *(float*)(b + 0xB84), (int)b[0xB5E], (int)b[0x135], *(void**)(b + 0xB50), av,
                      av ? *(float*)(av + 0xBBC) : -1.f, ai && skg::readable(ai, 0x1DC) ? *(uint32_t*)(ai + 0x1D8) : 0,
                      av ? *(uint32_t*)(av + 0xE7C) : 0, ap.x, ap.y, ap.z, game::position(c).x, game::position(c).y,
                      game::position(c).z);
        uint8_t* sp = av ? *(uint8_t**)(av + 0xB20) : nullptr; // the avatar's species: +0x608 "eats meat" (0xC0B8E0)
        if (sp && skg::readable(sp, 0x618))
            H->out_printf(out, "avatar species %p +600: %d %d %d %d %d %d\n", sp, *(int*)(sp + 0x600), *(int*)(sp + 0x604),
                          *(int*)(sp + 0x608), *(int*)(sp + 0x60C), *(int*)(sp + 0x610), *(int*)(sp + 0x614));
        return 0;
    }
    // tests (BUG-0557): diet <n>: the own avatar's species +0x608, read by 0xC0B8E0 (named IsHervibore in the March 2017
    // symbols; the eat node 0xD70FD0 bites meat only when it is not 0): the creature fixture's species have 0
    if (argc > 2 && !strcmp(argv[1], "diet")) {
        uint8_t* av = (uint8_t*)game::avatar();
        uint8_t* sp = av ? *(uint8_t**)(av + 0xB20) : nullptr;
        if (!sp || !skg::readable(sp, 0x60C)) {
            H->out_printf(out, "no avatar species\n");
            return 1;
        }
        int old = *(int*)(sp + 0x608);
        *(int*)(sp + 0x608) = atoi(argv[2]);
        H->out_printf(out, "species %p +0x608 %d -> %d\n", sp, old, *(int*)(sp + 0x608));
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "carry")) { // tests (BUG-0458): hold slots of the own creature and the puppets
        H->out_printf(out, "%s", carry_dump().c_str());
        return 0;
    }
    // tests (BUG-0458): clickobj <hex noun> [m]: the player's click (0xD32FD0) on the object of that noun nearest to the
    // own creature (within m, default 30); the cursor kind first (0x19 pick up a rock, 0x16 / 0xD throw ...)
    // tests (BUG-0458): hold <hex noun> [m] / drop: the own creature holds the nearest such object in hold slot 0 (the
    // game's Creature_HoldObject, as the carry node 0xD84120 calls it, through the relay's hook) / lets go of it
    if (argc > 2 && !strcmp(argv[1], "hold")) {
        void* av = game::avatar();
        void* o = av ? game::find_object((uint32_t)strtoul(argv[2], nullptr, 16), game::position(av),
                                         argc > 3 ? (float)atof(argv[3]) : 10.f)
                     : nullptr;
        void* sp = o ? game::cast(o, 0x1186577) : nullptr;
        uint8_t* a = (uint8_t*)av;
        int slots = a ? (int)((*(uint8_t**)(a + 0xE44) - *(uint8_t**)(a + 0xE40)) / 0x24) : -1;
        if (!sp || !o_Hold) {
            H->out_printf(out, "no object / hook (slots=%d)\n", slots);
            return 1;
        }
        bool r = h_Hold(av, sp, 0, true);
        H->out_printf(out, "hold=%d slots=%d socket0=%d\n", (int)r, slots,
                      slots > 0 ? **(int**)(a + 0xE40) : -2);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "drop")) {
        uint8_t* a = (uint8_t*)game::avatar();
        void* sp = a && *(uint8_t**)(a + 0xE44) > *(uint8_t**)(a + 0xE40) ? *(void**)(*(uint8_t**)(a + 0xE40) + 0x20) : nullptr;
        if (!sp || !o_Release) {
            H->out_printf(out, "nothing held\n");
            return 1;
        }
        H->out_printf(out, "release=%d\n", (int)h_Release(a, sp, 0, 0));
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "findobj")) { // tests (BUG-0458): findobj <hex noun> [m]: the nearest such object
        void* av = game::avatar();
        void* o = av ? game::find_object((uint32_t)strtoul(argv[2], nullptr, 16), game::position(av),
                                         argc > 3 ? (float)atof(argv[3]) : 30.f)
                     : nullptr;
        net::Vec3 p{};
        if (!o || !game::object_position(o, p)) {
            H->out_printf(out, "no object\n");
            return 1;
        }
        H->out_printf(out, "obj=%p pos=(%.2f %.2f %.2f)\n", o, p.x, p.y, p.z);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "clickobj")) {
        void* av = game::avatar();
        uintptr_t a_cur = H->sym("SporeCoop::CreatureCursorKind"), a_click = H->sym("SporeCoop::CreatureClickObject"),
                  a_strat = H->sym("App::cCreatureModeStrategy::Get");
        void* o = av ? game::find_object((uint32_t)strtoul(argv[2], nullptr, 16), game::position(av),
                                         argc > 3 ? (float)atof(argv[3]) : 30.f)
                     : nullptr;
        if (!o || !a_cur || !a_click || !a_strat) {
            H->out_printf(out, "no object / symbol\n");
            return 1;
        }
        net::Vec3 p{};
        game::object_position(o, p);
        int kind = skg::cdecl_call<int>(a_cur, o);
        // only the kinds whose action does not read ECX (0x1C, the walk, reads the input handler's object: called with
        // the strategy it faulted, lab 20261105-234209)
        if (kind > 4 && kind != 0x1C)
            if (void* st = skg::cdecl_call<void*>(a_strat))
                skg::thiscall<void>(a_click, st, o);
        H->out_printf(out, "clicked obj=%p kind=%d pos=(%.1f %.1f %.1f)\n", o, kind, p.x, p.y, p.z);
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
    a_SpawnEventUFO = H->sym("SporeCoop::UFOAbductionEvent_SpawnUFO");
    savestrip::add_filter(ufo_save_filter); // BUG-0817
    if (uintptr_t a = H->sym("SporeCoop::Creature_HoldObject"))
        H->hook_create(M, (void*)a, (void*)h_Hold, (void**)&o_Hold);
    if (uintptr_t a = H->sym("SporeCoop::Creature_ReleaseObject"))
        H->hook_create(M, (void*)a, (void*)h_Release, (void**)&o_Release);
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
