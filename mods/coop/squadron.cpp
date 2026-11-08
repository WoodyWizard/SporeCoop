#include "squadron.h"
#include "civ.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "game.h"
#include "modes.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/validate.h"
#include "space_rivalry.h"
#include "spacecoop.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "creations.h"
#include "savestrip.h"

using skg::thiscall;
using skg::vcall;

namespace squadron {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;
static uintptr_t a_GetAllUFOs, a_TakeDamage, a_FireTool, a_SpawnUFO, a_PlayerUFOGet, a_PlayerEmpireID;

// ---- cGameDataUFO (docs/re/space.md) ----
constexpr uintptr_t kLoco = 0x34, kPos = 0x38, kRot = 0x44, kCombat = 0x508, kMaxHp = 0x534, kState = 0x53C,
                    kHp = 0x540, kInvincible = 0x588, kType = 0x714, kNextPos = 0x718, kNextVel = 0x724,
                    kNextRot = 0x730, kAtDest = 0x74C, kDest = 0x750, kModel = 0x780, kPol = 0x30;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
static bool in_space() { return g_ok && game::mode_id() == modes::kSpace; }

static void* player_ufo()
{
    void* mgr = a_PlayerUFOGet ? skg::cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    return mgr ? F<void*>(mgr, 0x40) : nullptr;
}

static std::vector<void*> all_ufos()
{
    std::vector<void*> r;
    void* nm = game::noun_manager();
    void* v = nm ? thiscall<void*>(a_GetAllUFOs, nm) : nullptr;
    if (!v)
        return r;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    for (void** it = b; it && it < e && r.size() < 256; ++it)
        if (*it && game::object_alive(*it) && F<int>(*it, kState) != 2)
            r.push_back(*it);
    return r;
}

static net::Vec3 pos_of(void* u)
{
    auto& p = F<float[3]>(u, kPos);
    return {p[0], p[1], p[2]};
}

// ---------------- host ----------------
static std::unordered_map<void*, uint16_t> g_hid;
static std::unordered_map<uint16_t, void*> g_hid_rev;
static uint16_t g_next_id = 1;
static double g_last_send;
static uint16_t g_cycle;
static uint8_t g_drop_chunk = 0xFF; // tests (coop.fleet drop <chunk> <n>): skip sending that chunk n times
static int g_drop_left;
static uint32_t g_hits_in, g_damage_out, g_puppet_shots;

// ---------------- guest ----------------
struct Mirror {
    void* u = nullptr;
    net::Vec3 pos;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    net::VelocityEstimate est; // ship snapshots carry no velocity
};
static std::unordered_map<uint16_t, Mirror> g_mirrors;
static std::unordered_map<void*, uint16_t> g_mirror_of;
static std::unordered_set<uint16_t> g_cycle_ids;
static uint16_t g_rx_cycle = 0xFFFF;
static bool g_rx_any;         // a cycle was seen in this session (g_rx_cycle is valid)
static uint32_t g_rx_mask;    // BUG-0037: the chunks of g_rx_cycle that arrived (chunks <= 15)
static bool g_rx_done;        // the removal pass of g_rx_cycle ran
// BUG-0037: the removal pass (explosion of every mirror missing from the cycle) ran on the last
// chunk even when an earlier chunk of the cycle was lost or late, and a late chunk of an older
// cycle reset the cycle. `coop.fleet chunkfix 0` restores that (tests).
// BUG-0190: a mirror is pinned so the game's controller update draws it where the host's ship is
// (modes::space_pin: its own offset taken out, its zoom altitude set); off: the host position itself
static bool g_mirror_pin_fix = true;
COOP_FIX("fleet.mirror_pin", "BUG-0190", g_mirror_pin_fix,
         "space: a host ship's mirror stays where the mod puts it (its controller update skipped, pinned minus its offset)");
static bool g_chunk_fix = true;
COOP_FIX("fleet.chunk", "BUG-0037", g_chunk_fix, "space: the mirror removal pass runs only on a complete cycle of ship chunks");
// BUG-0084: the guest's own escort (posse, type 3 ships) is not the host's to simulate.
// `coop.fleet keepposse 0` restores the old loop that destroyed it (tests).
static bool g_keep_posse = true;
COOP_FIX("fleet.posse", "BUG-0084", g_keep_posse, "space: the guest's own escort is not destroyed as a host mirror (coop.fleet keepposse)");
static bool g_model_fix = true;
COOP_FIX("fleet.model_check", "BUG-0323", g_model_fix, "space: a host ship whose model the guest lacks is mirrored with the default model");
static uint32_t g_unknown_models;
static bool g_fake_model; // tests: the host sends made-up model instances
static uint32_t g_fake_inst; // tests (BUG-0469): the host sends this model instance instead
static bool g_fake_zero;      // tests (BUG-0723): the host sends a model key of zeros
// BUG-0723: a host ship whose model key is all zeros was mirrored with SpawnUFO(type, &pol, {0}): SpawnUFO then takes
// its own default key (0xCB89C3D6, docs/re/space.md#space-npc-model) and the gfx the default UFO 0x9D5DB21A, neither
// of which the GOG 2024 install has: a mirror without a model (the BUG-0546 / BUG-0686 guards only keep it from
// faulting), and that guard then drew it with the last ship model it had (lab 20261108-031320: two pirates as the
// guest's own ship). A zero key is now a model this game does not have: the model of a ship of that empire this game
// made itself, else the model the game gives a raid without an empire (cPlayer +0x1280), else no mirror.
// `coop.fix fleet.zero_key off`: SpawnUFO with the zeros (0.7.10).
static bool g_zero_key_fix = true;
COOP_FIX("fleet.zero_key", "BUG-0723", g_zero_key_fix, "space: a host ship with no model key is mirrored with a local ship model of its empire or the game's raid model, never with SpawnUFO's default (a model this game lacks)");
// BUG-0724: the host's empire's own ships (escorts, colony defenders) carry the keys of the host's own ship creations
// (My Creations, ids sequential per profile); the guest's game resolved such a key to ITS OWN creation of that id. Each
// key goes through creations::host_design (the BUG-0647 path of civ vehicles): the host's records under a key of ours.
// `coop.fix fleet.host_designs off`: the key as the host sent it (0.7.10).
static bool g_ship_designs_fix = true;
COOP_FIX("fleet.host_designs", "BUG-0724", g_ship_designs_fix, "space: a guest's mirrors of the host's empire's ships show the host's own ship designs, not the guest's creation of the same id");
static uint32_t g_zero_keys, g_design_waits, g_designs_used;
static std::unordered_set<uint32_t> g_unknown_logged;
static uint32_t g_stale_chunks, g_partial_cycles, g_posse_kept, g_content_checked;
static bool g_applying;
static uint32_t g_created, g_removed, g_hits_sent, g_damage_in, g_local_removed, g_mirror_hits;

// BUG-0420 (class: hits without a relation filter): a hit between the players' empire and a ship of that same empire
// (an escort of the host's, a Defender of its system) was relayed like an enemy's: the host's escort's stray shot on a
// guest's puppet hurt the guest's own ship, a guest's shot at the host's escort mirror hurt the escort. The game does
// not apply friendly fire to the player's own ship; now neither does the relay. Off: as 0.7.10.
static bool g_friendly_fix = true;
COOP_FIX("space.friendly_hits", "BUG-0420", g_friendly_fix, "space: shots between the players' empire and its own escorts are not relayed as hits");
static uint32_t g_friendly_dropped;
static bool friendly(void* ship)
{
    if (!g_friendly_fix || !ship)
        return false;
    uint32_t mine = a_PlayerEmpireID ? skg::cdecl_call<uint32_t>(a_PlayerEmpireID) : 0xFFFFFFFFu;
    return mine != 0xFFFFFFFFu && mine != 0 && F<uint32_t>(ship, kPol) == mine;
}

// ---------------- BUG-0677: the host's NPC ship shots on the guests ----------------
// A guest's mirrors never fire (h_FireTool returns 0 for them: the host's game decides the fights and relays the hits), so
// a guest saw the host's NPC ships fight without a beam (the detector: default_beam_projectile in the host's game only).
// Now the host counts the shots its NPC ships make (FireTool returned true; the weapon's slot in the ship +0x790..+0x7A4,
// its target: a ship of the host's list, a player's ship, or a noun near a place) and sends them in MSG_UFO_STATE with
// chunk kShotChunk / chunks 0 (an older guest drops it: chunks 0 fails guest_state's check). The guest fires its mirror's
// own weapon (the same slot) at the same target with the game's FireTool, so its game makes the projectile, the beam and
// the explosion. Their hits do nothing there: a mirror's hit on the own ship, a puppet, a mirror, a posse ship or (civ.cpp
// cosmetic_hit) a building is dropped; the host's MSG_UFO_DAMAGE brings the real damage. The pattern of civ.shots
// (BUG-0477, BUG-0530, BUG-0534, BUG-0688). `coop.fix space.shots off`: mirrors never fire (0.7.10).
static bool g_shots_fix = true;
COOP_FIX("space.shots", "BUG-0677", g_shots_fix, "space: a guest sees the host's NPC ships fire (the copies hurt nothing there)");
constexpr uint8_t kShotChunk = 0xFE;
constexpr uintptr_t kWeapons = 0x790; // mpNPCWeapon and the ground / near / medium / far air / abduct tools: 6 slots
constexpr double kShotSend = 0.2, kShotKeep = 1.0, kShotRetry = 0.1;
struct ShipShot {
    uint8_t count = 0;  // host: the shots that left (FireTool true), wrapping; guest: the host's count
    uint8_t done = 0;   // guest: the host's shots this game fired
    uint8_t slot = 0xFF; // the weapon's slot (0..5), 0xFF: the combatant's weapon (vt 0x14)
    int32_t dp = -1, a5 = 0; // FireTool's damage point and arg 5, as the host's game passed them
    net::Vec3 aim{};
    uint8_t tkind = 0;  // 0 none, 1 a host NPC ship (tid), 2 a player's ship (tid = player), 3 tnoun near tpos, 4 not here
    uint16_t tid = 0;
    uint32_t tnoun = 0;
    net::Vec3 tpos{};   // the target's place in the host's game
    double seen = -100; // host: the last shot; guest: the last list that had it
    double tried = -100; // guest: the last FireTool of the mirror
};
static std::unordered_map<uint16_t, ShipShot> g_ship_shots, g_ship_shots_in; // host / guest: ship id -> its shots
static double g_shots_sent_at = -100;
static bool g_shot_new;
static double g_shot_at_me_until = -100; // guest: a copied shot was aimed at this game's own ship until then
static uintptr_t a_ToolMgrGet, a_ToolMouseUp;
static uint32_t g_shots_seen, g_shots_tx, g_shots_untracked, g_shots_rx, g_shots_fired, g_shots_refused, g_shots_nomirror,
    g_shots_notool, g_shots_notarget, g_shots_released, g_shot_hits_dropped, g_shot_nullhits_dropped;

static bool in_image(uintptr_t a)
{
    static uintptr_t lo, hi;
    if (!hi)
        if (HMODULE exe = GetModuleHandleW(nullptr)) {
            auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
            lo = (uintptr_t)exe;
            hi = lo + nt->OptionalHeader.SizeOfImage;
        }
    return a >= lo && a < hi;
}
// a game tool (cSpaceToolData) object: readable, its vtable in the game's image
static bool tool_ok(void* t) { return t && !IsBadReadPtr(t, 0x1C0) && in_image(*(uintptr_t*)t); }

static void* weapon_in(void* u, uint8_t slot)
{
    void* t = nullptr;
    if (slot < 6)
        t = F<void*>(u, kWeapons + 4u * slot);
    else {
        void* comb = (uint8_t*)u + kCombat;
        uintptr_t vt = *(uintptr_t*)comb;
        if (in_image(vt) && !IsBadReadPtr((void*)vt, 0x18) && in_image(((uintptr_t*)vt)[0x14 / 4]))
            t = vcall<void*>(comb, 0x14 / 4); // the combatant's weapon, as cCombatSimulator::Update gets it
    }
    return tool_ok(t) ? t : nullptr;
}

bool shot_is_cosmetic(void* att)
{
    if (!g_shots_fix || !att || !is_guest() || !in_space() || !g_mirror_of.count((uint8_t*)att - kCombat))
        return false;
    g_shot_hits_dropped++;
    return true;
}

// ---------------- hooks ----------------
using TakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const float*, void*);
static TakeDamage_t o_TakeDamage;
static int SK_THISCALL h_TakeDamage(void* combat, float dmg, uint32_t pol, int type, const float* dir, void* attacker)
{
    if (in_space() && (uint8_t*)combat - kCombat == player_ufo()) // rescue: a lethal hit may only disable us
        dmg = spacecoop::filter_own_damage((uint8_t*)combat - kCombat, dmg, type);
    if (!in_space() || g_applying || !C.sess || !C.sess->connected())
        return o_TakeDamage(combat, dmg, pol, type, dir, attacker);
    void* u = (uint8_t*)combat - kCombat;
    void* me = player_ufo();
    void* att = attacker ? (uint8_t*)attacker - kCombat : nullptr;
    if (is_guest() && g_shots_fix && att && g_mirror_of.count(att) && !g_mirror_of.count(u) && u != me) {
        g_shot_hits_dropped++; // BUG-0677: a mirror's copied shot hurts nothing here (before the rivalry relay)
        return 0;
    }
    if (rivalry::on_hit(u, att, dmg, type, dir))
        return 0; // our hit on the ship of a player at war with us: its owner takes it (space_rivalry.cpp)
    if (is_guest()) {
        auto it = g_mirror_of.find(u);
        if (it != g_mirror_of.end()) { // a mirror is hit: only our shots count, the host applies them
            g_mirror_hits++;
            if (me && att == me && type != 10) {
                net::Writer w;
                w.u16(it->second);
                w.f32(dmg);
                w.u32((uint32_t)type);
                w.f32(dir ? dir[0] : 0); w.f32(dir ? dir[1] : 0); w.f32(dir ? dir[2] : 0);
                C.sess->send_reliable_to(0, net::MSG_UFO_HIT, w);
                g_hits_sent++;
            }
            return 0;
        }
        if (u == me && att && g_mirror_of.count(att))
            return 0; // the host decides when its ships hit us (MSG_UFO_DAMAGE)
        if (g_shots_fix && att && g_mirror_of.count(att)) { // BUG-0677: a copied shot hits a puppet or an escort
            g_shot_hits_dropped++;
            return 0;
        }
        if (g_shots_fix && u == me && !att && H->time_seconds() < g_shot_at_me_until) {
            g_shot_nullhits_dropped++; // BUG-0677: a copied shot's hit without an attacker (an area's tick)
            return 0;
        }
    }
    if (is_host() && C.puppet_owner) {
        int owner = C.puppet_owner(u);
        bool own = owner > 0 && att && C.puppet_owner(att) < 0 && att != me && friendly(att);
        if (own)
            g_friendly_dropped++;
        if (owner > 0 && att && C.puppet_owner(att) < 0 && att != me && !own) {
            // an NPC ship hit a guest's ship: the guest takes it on its own ship
            net::Writer w;
            w.f32(dmg);
            w.u32((uint32_t)type);
            w.f32(dir ? dir[0] : 0); w.f32(dir ? dir[1] : 0); w.f32(dir ? dir[2] : 0);
            auto it = g_hid.find(att);
            w.u16(it == g_hid.end() ? 0xFFFF : it->second);
            C.sess->send_reliable_to((uint8_t)owner, net::MSG_UFO_DAMAGE, w);
            g_damage_out++;
        }
    }
    return o_TakeDamage(combat, dmg, pol, type, dir, attacker);
}

// BUG-0677 (host): a shot one of the listed NPC ships made
static void ship_fired(void* tool, void* u, const float* aim, int dp, int a5, void* target)
{
    if (!g_shots_fix || !is_host() || !in_space() || !aim)
        return;
    auto it = g_hid.find(u);
    if (it == g_hid.end()) {
        g_shots_untracked++;
        return;
    }
    ShipShot s;
    s.aim = {aim[0], aim[1], aim[2]};
    if (!std::isfinite(s.aim.x) || !std::isfinite(s.aim.y) || !std::isfinite(s.aim.z))
        return;
    for (uint8_t i = 0; i < 6; i++)
        if (F<void*>(u, kWeapons + 4u * i) == tool)
            s.slot = i;
    s.dp = dp;
    s.a5 = a5;
    if (target) {
        void* t = (uint8_t*)target - kLoco;
        auto th = g_hid.find(t);
        int owner = t == player_ufo() ? 0 : C.puppet_owner ? C.puppet_owner(t) : -1;
        if (th != g_hid.end()) {
            s.tkind = 1;
            s.tid = th->second;
            s.tpos = pos_of(t);
        } else if (owner >= 0) {
            s.tkind = 2;
            s.tid = (uint16_t)owner;
            s.tpos = pos_of(t);
        } else if (!IsBadReadPtr(t, 0x40) && in_image(*(uintptr_t*)t) && game::object_alive(t) &&
                   game::object_position(t, s.tpos)) {
            s.tkind = 3;
            s.tnoun = game::noun_id(t);
        } else
            s.tkind = 4; // a target the guest cannot find: it does not fire this one (a beam needs its target, BUG-0534)
    }
    s.seen = H->time_seconds();
    auto& e = g_ship_shots[it->second];
    s.count = (uint8_t)(e.count + 1);
    e = s;
    g_shot_new = true;
    g_shots_seen++;
}

// mirrors never fire on their own (the host's ships shoot on the host); BUG-0677: the guest fires the host's shots
using FireTool_t = int(SK_THISCALL*)(void*, void*, void*, const float*, int, int, void*, int);
static FireTool_t o_FireTool;
static int SK_THISCALL h_FireTool(void* mgr, void* tool, void* owner, const float* aim, int dp, int a5, void* target,
                                  int show)
{
    if (owner && is_guest() && in_space() && g_mirror_of.count((uint8_t*)owner - kLoco))
        return 0;
    if (owner && spacecoop::disabled() && (uint8_t*)owner - kLoco == player_ufo()) // a disabled ship can't shoot
        return 0;
    // the partners' ships here are Defender UFOs: their AI shot at every hostile ship in range on
    // its own (pirates near the guest's puppet died on the host while the guest did not fire);
    // only the partner's real shots count (MSG_UFO_HIT)
    if (owner && in_space() &&
        ((C.puppet_owner && C.puppet_owner((uint8_t*)owner - kLoco) >= 0) || modes::space_retired((uint8_t*)owner - kLoco))) {
        g_puppet_shots++;
        return 0;
    }
    int r = o_FireTool(mgr, tool, owner, aim, dp, a5, target, show);
    if ((r & 0xFF) && owner) // BUG-0677
        ship_fired(tool, (uint8_t*)owner - kLoco, aim, dp, a5, target);
    // BUG-0477: the host's Civ shots go to the guests; BUG-0530: FireTool's al tells whether a shot left
    civ::fire_observed(tool, owner, aim, target, (r & 0xFF) != 0);
    civ::fire_local(owner, (uintptr_t)__builtin_return_address(0), (r & 0xFF) != 0);
    return r;
}

// BUG-0378, BUG-0190: the UFO controller update (SporeCoop::UFOController_Update, a virtual
// thiscall(this, ctx) ret 4; ctx +0x14 = the ship's loco, +0x10 = dt, +0..8 = the velocity it asks
// for) runs every frame after our Present and moves each ship to mNextPosition + its offset; in the
// galactic core it also pulls the ship to its own orbit and offset. A puppet or mirror pinned this
// frame (modes::space_pin with its switch on) keeps the pose we gave it: no velocity, original not
// called. Off (space.puppet_pin / fleet.mirror_pin): the original runs for it.
using CtlUpdate_t = void(SK_THISCALL*)(void*, float*);
static CtlUpdate_t o_CtlUpdate;
static uint32_t g_ctl_skipped;
static void SK_THISCALL h_CtlUpdate(void* self, float* ctx)
{
    void* loco = ctx ? *(void**)((uint8_t*)ctx + 0x14) : nullptr;
    if (loco && modes::space_pinned((uint8_t*)loco - kLoco)) {
        ctx[0] = ctx[1] = ctx[2] = 0;
        g_ctl_skipped++;
        return;
    }
    o_CtlUpdate(self, ctx);
}

// BUG-0479: a partner's ship is a Defender NPC UFO here (modes.cpp space_spawn), and the game sizes every
// UFO itself each frame (cModelObject_SetScale 0xC88630 on the ship's loco +0x34, the scale at loco +0x64):
// an NPC ship smaller than the player's own (3.30 vs 4.95 in a star system, 0.84 vs 4.82 seen on a
// planet), so each player saw the other's ship at an NPC's size. A size set by the mod lasts one frame
// (measured: 4.95 set, 3.30 again 2 s later), so this is not a replicated field: the game's own setter
// is hooked and a partner's puppet gets the size the game gives the own ship in this view.
// `coop.fix space.puppet_scale off`: the game's NPC size (0.7.3.6).
static bool g_puppet_scale_fix = true;
COOP_FIX("space.puppet_scale", "BUG-0479", g_puppet_scale_fix,
         "space: a partner's ship is drawn at a player ship's size, not an NPC ship's");
using SetScale_t = void(SK_THISCALL*)(void*, float);
static SetScale_t o_SetScale;
static void* g_loco_vt;                      // the UFO loco's vtable (read from the own ship)
static uint32_t g_scale_puppet_calls, g_scale_replaced;
static uintptr_t g_scale_caller;             // diagnostics: where the game sizes a puppet from
static float g_scale_game = 0, g_scale_own = 0;
// BUG-0485 (diagnostics): who gives an object the scale 10 the detector saw on the host's commodity
// nodes (noun 0x0403DF5C, loco vtable 0x1469A28 -> this setter): caller and the object's vtable, logged
// once per caller
static uintptr_t g_ten_callers[8];
static void SK_THISCALL h_SetScale(void* obj, float s)
{
    if (s > 9.99f && s < 10.01f && obj) {
        uintptr_t ra = (uintptr_t)__builtin_return_address(0) - H->image_base() + 0x400000;
        for (auto& c : g_ten_callers)
            if (c == ra)
                break;
            else if (!c) {
                c = ra;
                H->log(M, SK_INFO, "scale 10 set on an object with vtable %08X from %08X (BUG-0485 diagnostics)",
                       (unsigned)(*(uintptr_t*)obj - H->image_base() + 0x400000), (unsigned)ra);
                break;
            }
    }
    if (g_ok && obj && g_loco_vt && *(void**)obj == g_loco_vt && in_space()) {
        void* u = (uint8_t*)obj - kLoco;
        void* me = player_ufo();
        if (me && u != me && C.puppet_owner && C.puppet_owner(u) >= 0) {
            g_scale_puppet_calls++;
            g_scale_caller = (uintptr_t)__builtin_return_address(0) - H->image_base() + 0x400000;
            g_scale_game = s;
            float own = F<float>(me, kLoco + 0x64); // cModelObject_GetScale: loco +0x64
            g_scale_own = own;
            if (g_puppet_scale_fix && std::isfinite(own) && own > 0.01f && own < 1000.f) {
                s = own;
                g_scale_replaced++;
            }
        }
    }
    o_SetScale(obj, s);
}

// BUG-0522: the ships this game shows for the session (the partners' puppets, Defenders of my empire made
// by modes.cpp space_spawn; a guest's mirrors of the host's NPC ships; a puppet retired to a game mission)
// are not this player's world, but a Space save wrote them like the game's own: a guest that loaded its
// save alone had them back as ordinary Defenders of its empire where they stood (lab 20261104-094503:
// both puppets and the mirror, no longer invincible). A savestrip filter writes a null reference for them
// (both roles: the host's share save holds the guests' puppets too). BUG-0470's null rule: no list was
// found that reads a null back where it wants a ship (cSimulatorSpaceGame +0x78, ModAPI's mNPC_UFOs, was
// an empty vector in a system with 3 NPC ships; the noun list skips nulls; a reload ran with no fault),
// so nothing is taken out of a list while the save is written. `coop.fix space.save_strip off`: saved
// like the game's own (0.7.5).
static bool g_save_strip_fix = true;
COOP_FIX("space.save_strip", "BUG-0522", g_save_strip_fix, "saves: the partners' ships and the host's ship mirrors are kept out of Space saves");
constexpr uint32_t kUfoNoun = 0x018EBADCu;
static std::unordered_set<void*> g_strip_ufos;
static uint32_t g_ufo_stripped, g_ufo_strip_saves;

static std::vector<void*> listed_ufos() // every listed UFO, the dying ones too (they are written as well)
{
    std::vector<void*> r;
    void* nm = game::noun_manager();
    void* v = nm ? thiscall<void*>(a_GetAllUFOs, nm) : nullptr;
    if (!v)
        return r;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    for (void** it = b; it && it < e && r.size() < 512; ++it)
        if (*it && game::object_alive(*it))
            r.push_back(*it);
    return r;
}

static void ufo_save_start()
{
    g_strip_ufos.clear();
    if (!g_save_strip_fix || !in_space())
        return;
    void* me = player_ufo();
    for (void* u : listed_ufos())
        if (u != me && (g_mirror_of.count(u) || (C.puppet_owner && C.puppet_owner(u) >= 0) || modes::space_retired(u)))
            g_strip_ufos.insert(u);
    if (g_strip_ufos.empty())
        return;
    g_ufo_strip_saves++;
    H->log(M, SK_INFO, "space: %u ships of ours leave this save (space.save_strip)", (unsigned)g_strip_ufos.size());
}

static void ufo_save_end() { g_strip_ufos.clear(); }

static bool ufo_save_filter(void* o, uint32_t noun)
{
    if (noun != kUfoNoun || g_strip_ufos.empty() || !g_strip_ufos.count(o))
        return false;
    g_ufo_stripped++;
    return true;
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetAllUFOs = H->sym("Simulator::GetAllUFOs");
    a_TakeDamage = H->sym("Simulator::cGameDataUFO::TakeDamage");
    a_FireTool = H->sym("Simulator::cToolManager::FireTool");
    a_SpawnUFO = H->sym("Simulator::SpawnUFO");
    a_PlayerUFOGet = H->sym("Simulator::cSimulatorPlayerUFO::Get");
    a_PlayerEmpireID = H->sym("Simulator::GetPlayerEmpireID");
    a_ToolMgrGet = H->sym("Simulator::cToolManager::Get");          // BUG-0677
    a_ToolMouseUp = H->sym("Simulator::cToolManager::OnMouseUp");
    g_ok = a_GetAllUFOs && a_TakeDamage && a_FireTool && a_SpawnUFO && a_PlayerUFOGet && a_PlayerEmpireID;
    if (!g_ok) {
        H->log(M, SK_WARN, "squadron: symbols missing, space co-op disabled");
        return false;
    }
    savestrip::add_filter(ufo_save_filter); // BUG-0522
    savestrip::add_save_hooks(ufo_save_start, ufo_save_end);
    H->hook_create(M, (void*)a_TakeDamage, (void*)h_TakeDamage, (void**)&o_TakeDamage);
    H->hook_create(M, (void*)a_FireTool, (void*)h_FireTool, (void**)&o_FireTool);
    if (uintptr_t a = H->sym("SporeCoop::UFOController_Update")) // GOG 2024 only (BUG-0378)
        H->hook_create(M, (void*)a, (void*)h_CtlUpdate, (void**)&o_CtlUpdate);
    if (uintptr_t a = H->sym("cModelObject_SetScale")) // BUG-0479
        H->hook_create(M, (void*)a, (void*)h_SetScale, (void**)&o_SetScale);
    return true;
}

// ---------------- host ----------------
static void host_tick(double now)
{
    if (now - g_last_send < 0.1)
        return;
    g_last_send = now;
    void* me = player_ufo();
    std::vector<void*> ships;
    for (void* u : all_ufos())
        if (u != me && F<int>(u, kType) != 0 && (!C.puppet_owner || C.puppet_owner(u) < 0) &&
            !modes::space_retired(u)) // BUG-0218: a retired puppet is not one of our NPC ships
            ships.push_back(u);
    std::unordered_set<void*> alive(ships.begin(), ships.end());
    for (auto it = g_hid.begin(); it != g_hid.end();) {
        if (!alive.count(it->first)) {
            g_hid_rev.erase(it->second);
            it = g_hid.erase(it);
        } else
            ++it;
    }
    for (void* u : ships)
        if (!g_hid.count(u)) {
            uint16_t id = net::alloc_id(g_next_id, g_hid_rev); // BUG-0154: never a live id, never 0
            if (!id)
                continue;
            g_hid[u] = id;
            g_hid_rev[id] = u;
        }
    g_cycle++;
    size_t per = 18, chunks = std::max<size_t>(1, (ships.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        size_t from = k * per, to = std::min(ships.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            void* u = ships[i];
            net::Vec3 p = pos_of(u);
            auto& q = F<float[4]>(u, kRot);
            auto* key = &F<uint32_t>(u, kModel);
            w.u16(g_hid[u]);
            w.u8((uint8_t)F<int>(u, kType));
            w.u32(F<uint32_t>(u, kPol));
            // tests (BUG-0323): `coop.fleet fakemodel 1` sends a model instance the guest has not got,
            // as a host whose Sporepedia cache holds other creations does
            if (g_fake_zero) { // tests (BUG-0723): a ship without a model key
                w.u32(0); w.u32(0); w.u32(0);
            } else {
                w.u32(g_fake_inst ? g_fake_inst : g_fake_model ? key[0] ^ 0x5A5A5A5Au : key[0]); w.u32(key[1]); w.u32(key[2]);
            }
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(q[0]); w.f32(q[1]); w.f32(q[2]); w.f32(q[3]);
            w.f32(F<float>(u, kHp));
            w.f32(F<float>(u, kMaxHp));
        }
        w.u32(net::now_ms()); // BUG-0122: the host's time of the snapshot (velocity on the guest)
        if (g_drop_left > 0 && k == g_drop_chunk) { // tests: a lost packet
            g_drop_left--;
            continue;
        }
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_UFO_STATE, w);
    }
}

// BUG-0677: the shots of the last second, every 0.2 s, a new one at once (at most every 0.05 s)
static void host_send_shots(double now)
{
    bool soon = g_shot_new && now - g_shots_sent_at >= 0.05;
    if (!g_shots_fix || (now - g_shots_sent_at < kShotSend && !soon))
        return;
    g_shots_sent_at = now;
    g_shot_new = false;
    std::vector<std::pair<uint16_t, const ShipShot*>> list;
    for (auto it = g_ship_shots.begin(); it != g_ship_shots.end();) {
        if (now - it->second.seen > kShotKeep || !g_hid_rev.count(it->first)) {
            it = g_ship_shots.erase(it);
            continue;
        }
        list.emplace_back(it->first, &it->second);
        ++it;
    }
    constexpr size_t kPerMsg = 24; // 43 bytes each: one message stays under kMaxPacket
    for (size_t at = 0; at < list.size() && at < 4 * kPerMsg; at += kPerMsg) {
        size_t n = std::min(kPerMsg, list.size() - at);
        net::Writer w;
        w.u16(0); w.u8(kShotChunk); w.u8(0);
        w.u8((uint8_t)n);
        for (size_t i = at; i < at + n; i++) {
            const ShipShot& s = *list[i].second;
            w.u16(list[i].first); w.u8(s.count); w.u8(s.slot);
            w.u32((uint32_t)s.dp); w.u32((uint32_t)s.a5);
            w.f32(s.aim.x); w.f32(s.aim.y); w.f32(s.aim.z);
            w.u8(s.tkind); w.u16(s.tid); w.u32(s.tnoun);
            w.f32(s.tpos.x); w.f32(s.tpos.y); w.f32(s.tpos.z);
        }
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_UFO_STATE, w);
        g_shots_tx++;
    }
}

// ---------------- guest ----------------
// the game frees NPC UFOs on its own (every context switch: DestroyNPCUFOs), the mirrors too:
// a pointer is only used while it is in the UFO noun list
static bool live(void* u)
{
    if (u && game::ufo_listed(u) && game::object_alive(u))
        return true;
    if (u)
        game::g_ufos_freed++;
    return false;
}

// BUG-0054: a ship handed to the game's TakeDamage as the attacker must still exist. The game frees
// mirrors and puppets on every context switch; a hit message handled in the frame after that (the
// messages run before squadron::tick drops the freed pointers) passed a freed object, which the
// game reads and AddRefs (AddHostileUnit). `coop.fleet hitfix 0` restores the old unchecked path.
static bool g_hit_fix = true;
COOP_FIX_UNSAFE("fleet.hit", "BUG-0054", g_hit_fix, "space: a hit message never passes a freed ship to TakeDamage");
static uint16_t g_hold_mirror = 0xFFFF; // tests (coop.fleet freemirror): this freed mirror stays listed
static void* g_stale_puppet;            // tests (coop.fleet freepuppet): the next MSG_UFO_HIT's attacker
static uint32_t g_att_dropped, g_att_freed;

static void* hit_attacker(void* a)
{
    if (a && g_hit_fix && (!live(a) || F<int>(a, kState) == 2)) {
        g_att_dropped++;
        a = nullptr;
    }
    if (a && !game::ufo_listed(a)) // what the game gets: counted for the tests, with the fix off
        g_att_freed++;
    return a;
}

static void* hit_puppet(uint8_t from)
{
    void* p = C.puppet_of ? C.puppet_of(from) : nullptr;
    if (g_stale_puppet) {
        p = g_stale_puppet;
        g_stale_puppet = nullptr;
    }
    if (p && g_hit_fix && live(p) && !modes::space_puppet_ok(p)) // only the mod's player puppets (BUG-0111)
        p = nullptr, g_att_dropped++;
    return hit_attacker(p);
}

// BUG-0323: the model of a live ship this game made for the empire `pol` (the same type first)
static bool local_model(uint32_t pol, int type, uint32_t* key)
{
    void* best = nullptr;
    for (void* u : all_ufos()) {
        if (u == player_ufo() || g_mirror_of.count(u) || F<uint32_t>(u, kPol) != pol)
            continue;
        const uint32_t* k = &F<uint32_t>(u, kModel);
        if (!(k[0] | k[1] | k[2]) || !creations::key_known({k[0], k[1], k[2]}))
            continue;
        if (!best || F<int>(u, kType) == type)
            best = u;
    }
    static std::unordered_map<uint32_t, std::array<uint32_t, 3>> seen; // pol -> a model it had here
    if (!best) {
        auto it = seen.find(pol); // the local ships are removed once the host's are mirrored
        if (it == seen.end() || !creations::key_known({it->second[0], it->second[1], it->second[2]}))
            return false;
        memcpy(key, it->second.data(), 12);
        return true;
    }
    memcpy(key, &F<uint32_t>(best, kModel), 12);
    memcpy(seen[pol].data(), key, 12);
    return true;
}

// BUG-0723: the model the game itself gives a ship without an empire's design: a raid without an attacker empire copies
// its UFO key from cPlayer +0x1280 (cGameNounManager +0x74; 0xC5C8F3, docs/re/space.md#space-npc-model), a model this
// game has. false: none known here.
static bool pirate_model(uint32_t* key)
{
    static uintptr_t a_nm = H->sym("Simulator::cGameNounManager::Get");
    void* nm = a_nm ? skg::cdecl_call<void*>(a_nm) : nullptr;
    void* pl = nm && skg::readable(nm, 0x78) ? F<void*>(nm, 0x74) : nullptr;
    if (!pl || !skg::readable(pl, 0x128C))
        return false;
    const uint32_t* k = &F<uint32_t>(pl, 0x1280);
    if (!(k[0] | k[2]) || !creations::key_known({k[0], k[1] ? k[1] : 0x476A98C7u, k[2]}))
        return false;
    key[0] = k[0];
    key[1] = k[1] ? k[1] : 0x476A98C7u;
    key[2] = k[2];
    return true;
}

static void destroy_mirror(void* u, bool explode)
{
    if (!live(u))
        return;
    g_applying = true;
    if (explode) { // the game's own death: explosion, removal
        F<uint8_t>(u, kInvincible) = 0;
        float dir[3] = {0, 0, 1};
        o_TakeDamage((uint8_t*)u + kCombat, 1e6f, 0xFFFFFFFFu, 0, dir, nullptr);
    } else
        game::destroy_creature(u); // DestroyInstance works for any cGameData
    g_applying = false;
}

static void guest_shots(net::Reader& r, uint8_t n)
{
    if (!g_shots_fix)
        return;
    double now = H->time_seconds();
    for (int i = 0; i < n && r.ok(); i++) {
        ShipShot s;
        uint16_t id = r.u16();
        s.count = r.u8(); s.slot = r.u8();
        s.dp = (int32_t)r.u32(); s.a5 = (int32_t)r.u32();
        s.aim.x = r.f32(); s.aim.y = r.f32(); s.aim.z = r.f32();
        s.tkind = r.u8(); s.tid = r.u16(); s.tnoun = r.u32();
        s.tpos.x = r.f32(); s.tpos.y = r.f32(); s.tpos.z = r.f32();
        if (!r.ok() || !net::finite3(&s.aim.x) || !net::finite3(&s.tpos.x))
            return;
        auto f = g_ship_shots_in.find(id);
        bool fresh = f == g_ship_shots_in.end();
        auto& e = g_ship_shots_in[id];
        uint8_t done = fresh ? (uint8_t)(s.count - 1) : e.done; // a new shooter: its latest shot, not the old ones
        if ((uint8_t)(s.count - done) > 4)
            done = (uint8_t)(s.count - 4); // a burst lost on the way: at most 4 behind
        double tried = fresh ? -100 : e.tried;
        e = s;
        e.done = done;
        e.tried = tried;
        e.seen = now;
        g_shots_rx++;
    }
}

// BUG-0677: this game's copy of a ship's weapon stops when the host's list no longer has the ship (a beam or an area
// left firing stays in this game otherwise, as BUG-0530's AoE did in Civ)
static void release_shot(uint16_t id, const ShipShot& s)
{
    auto m = g_mirrors.find(id);
    void* u = m == g_mirrors.end() ? nullptr : m->second.u;
    if (!a_ToolMouseUp || !a_ToolMgrGet || !u || !live(u))
        return;
    void* tool = weapon_in(u, s.slot);
    if (!tool || !F<uint8_t>(tool, 0x80)) // +0x80: firing (FireTool sets it, OnMouseUp clears it)
        return;
    if (void* mgr = skg::cdecl_call<void*>(a_ToolMgrGet)) {
        thiscall<int>(a_ToolMouseUp, mgr, tool);
        g_shots_released++;
    }
}

static void* shot_target(const ShipShot& s)
{
    if (s.tkind == 1) {
        auto m = g_mirrors.find(s.tid);
        void* t = m == g_mirrors.end() ? nullptr : m->second.u;
        return t && live(t) && F<int>(t, kState) != 2 ? t : nullptr;
    }
    if (s.tkind == 2) {
        void* t = C.sess && s.tid == C.sess->my_id() ? player_ufo() : C.puppet_of ? C.puppet_of((uint8_t)s.tid) : nullptr;
        return t && live(t) ? t : nullptr;
    }
    if (s.tkind == 3) {
        struct Q {
            uint32_t noun;
            net::Vec3 at;
            float best = 6.f;
            void* found = nullptr;
        } q{s.tnoun, s.tpos};
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto& q = *(Q*)u;
                net::Vec3 p;
                if (n == q.noun && game::object_alive(o) && game::object_position(o, p) && net::dist(p, q.at) < q.best) {
                    q.best = net::dist(p, q.at);
                    q.found = o;
                }
                return true;
            },
            &q);
        return q.found;
    }
    return nullptr;
}

static void guest_fire(double now)
{
    if (!g_shots_fix || !a_ToolMgrGet) {
        g_ship_shots_in.clear();
        return;
    }
    for (auto it = g_ship_shots_in.begin(); it != g_ship_shots_in.end();) {
        ShipShot& s = it->second;
        uint16_t id = it->first;
        if (now - s.seen > kShotKeep + 0.3) {
            release_shot(id, s);
            it = g_ship_shots_in.erase(it);
            continue;
        }
        ++it;
        if (s.done == s.count || now - s.tried < kShotRetry)
            continue;
        s.tried = now;
        auto m = g_mirrors.find(id);
        void* u = m == g_mirrors.end() ? nullptr : m->second.u;
        if (!u || !live(u) || F<int>(u, kState) == 2) {
            g_shots_nomirror++;
            continue;
        }
        void* tool = weapon_in(u, s.slot);
        void* mgr = tool ? skg::cdecl_call<void*>(a_ToolMgrGet) : nullptr;
        if (!mgr) {
            g_shots_notool++;
            continue;
        }
        void* t = shot_target(s);
        if (s.tkind && !t) { // the game's weapon update reads a shot's target without a check (BUG-0534)
            g_shots_notarget++;
            continue;
        }
        net::Vec3 ap = s.aim;
        if (t) { // the host aimed at its copy of the target: the same offset from this game's copy
            net::Vec3 tp;
            bool ship = s.tkind == 1 || s.tkind == 2;
            if (ship)
                tp = pos_of(t);
            if (ship || game::object_position(t, tp))
                ap = {s.aim.x + tp.x - s.tpos.x, s.aim.y + tp.y - s.tpos.y, s.aim.z + tp.z - s.tpos.z};
            if (t == player_ufo())
                g_shot_at_me_until = now + 1.5;
        }
        float aim[3] = {ap.x, ap.y, ap.z};
        int fired = o_FireTool(mgr, tool, (uint8_t*)u + kLoco, aim, s.dp, s.a5, t ? (uint8_t*)t + kLoco : nullptr, 0);
        if (fired & 0xFF) {
            s.done++;
            g_shots_fired++;
        } else
            g_shots_refused++;
    }
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), n = r.u8();
    if (!r.ok() || !C.with_host || !C.with_host())
        return; // the host's ships are mirrored only where the host is
    if (chunk == kShotChunk && chunks == 0) { // BUG-0677: a shot list
        guest_shots(r, n);
        return;
    }
    if (g_chunk_fix) {
        if (!chunks || chunks > 31 || chunk >= chunks)
            return;
        int16_t age = (int16_t)(uint16_t)(cycle - g_rx_cycle);
        if (g_rx_any && age < 0 && age > -100) { // a late chunk of an older cycle (> -100: not a restart)
            g_stale_chunks++;
            return;
        }
        if (!g_rx_any || cycle != g_rx_cycle) {
            if (g_rx_any && !g_rx_done)
                g_partial_cycles++; // the previous cycle never completed: nothing was removed for it
            g_rx_any = true;
            g_rx_cycle = cycle;
            g_rx_mask = 0;
            g_rx_done = false;
            g_cycle_ids.clear();
        }
    } else if (cycle != g_rx_cycle) {
        g_rx_cycle = cycle;
        g_cycle_ids.clear();
    }
    double now = H->time_seconds();
    double sent = net::snapshot_time(r, (size_t)n * 55); // 55 bytes per ship
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        int type = r.u8();
        uint32_t pol = r.u32(), key[3];
        key[0] = r.u32(); key[1] = r.u32(); key[2] = r.u32();
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        net::Quat q;
        q.x = r.f32(); q.y = r.f32(); q.z = r.f32(); q.w = r.f32();
        float hp = r.f32(), mx = r.f32();
        (void)mx;
        if (!r.ok())
            return;
        g_cycle_ids.insert(id);
        auto& m = g_mirrors[id];
        m.pos = p; m.rot = q; m.hp = hp; m.seen = now;
        m.est.update(p, now, sent);
        if (id == g_hold_mirror)
            continue; // tests: keep the freed pointer until the next MSG_UFO_DAMAGE
        if (m.u && (!live(m.u) || F<int>(m.u, kState) == 2)) {
            g_mirror_of.erase(m.u);
            m.u = nullptr;
        }
        if (!m.u) {
            // BUG-0323: the host's NPC ships carry its model keys (cGameDataUFO +0x780). An NPC
            // empire's ships are creations of the host's Sporepedia cache (Pollination.package),
            // which differs between profiles: a key the guest's game does not have gave a ship
            // without a model, and the game faulted at 0xE98DD0 (null model in the ship gfx's
            // SetVisible) when the mirror came up. Such a mirror takes the game's default model.
            // `coop.fix fleet.model_check off`: the old spawn with any key.
            // (SpawnUFO with no key faulted the same way: the default model was missing too.) The
            // model of a ship of the same empire that this game made itself is taken instead; with
            // none the mirror waits (no ship rather than a broken one).
            bool zero = !(key[0] | key[1] | key[2]);
            if (zero)
                g_zero_keys++;
            if (!zero && g_ship_designs_fix) { // BUG-0724: the host's own ship design, under a key of ours
                bool pending = false;
                creations::Key hk = creations::host_design({key[0], key[1], key[2]}, creations::kMkUfo, &pending);
                if (pending) {
                    g_design_waits++;
                    continue; // asked the host: the mirror comes with its answer (a later cycle)
                }
                if (hk.instance != key[0])
                    g_designs_used++;
                key[0] = hk.instance; key[1] = hk.type; key[2] = hk.group;
            }
            bool have_key = zero ? !g_zero_key_fix : creations::key_known({key[0], key[1], key[2]});
            bool known = have_key;
            // BUG-0469: the model's key alone does not say whether this game can build the model: its
            // parts and paints (a ship design of another player's Sporepedia or of a pack) are checked
            // too. A model with ids this game does not have is built from our copy of it (registered
            // under the model's own key, so the key stays one the game has) with the substitutes; one
            // that cannot be built here is a model this game does not have (below).
            if (known && (key[0] | key[1] | key[2])) {
                creations::Model cm = creations::ensure_model({key[0], key[1], key[2]}, creations::kMkUfo);
                if (cm.key.instance) {
                    key[0] = cm.key.instance; key[1] = cm.key.type; key[2] = cm.key.group;
                    if (creations::content_all_kinds())
                        g_content_checked++;
                } else
                    known = false;
            }
            if (!known) {
                g_unknown_models++;
                bool sub = false;
                if (g_model_fix)
                    sub = local_model(pol, type, key);
                if (!sub && zero && g_zero_key_fix)
                    sub = pirate_model(key);
                if (g_unknown_logged.insert(key[0]).second)
                    H->log(M, SK_INFO, "space: host ship model %08X %s%s", key[0],
                           have_key ? "has parts or paints this game does not have"
                           : zero   ? "is none (a key of zeros, BUG-0723)" : "is not in this game",
                           !g_model_fix ? "" : sub ? ": the mirror takes a local ship's model" : ": no mirror yet");
                if (g_model_fix && !sub)
                    continue;
            }
            void* u = skg::cdecl_call<void*>(a_SpawnUFO, type, &pol, (void*)key);
            if (!u)
                continue;
            m.u = u;
            g_mirror_of[u] = id;
            float pp[3] = {p.x, p.y, p.z}, qq[4] = {q.x, q.y, q.z, q.w};
            vcall<void>((uint8_t*)u + kLoco, 0x44 / 4, (const float*)pp, (const float*)qq); // Teleport
            g_created++;
        }
    }
    if (g_chunk_fix) { // the removal pass needs every chunk of the cycle, in any order, once
        g_rx_mask |= 1u << chunk;
        if (g_rx_done || g_rx_mask != (chunks >= 32 ? 0xFFFFFFFFu : (1u << chunks) - 1))
            return;
        g_rx_done = true;
    } else if (chunk + 1 < chunks)
        return;
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.u) {
                g_mirror_of.erase(it->second.u);
                destroy_mirror(it->second.u, true);
                g_removed++;
            }
            it = g_mirrors.erase(it);
        } else
            ++it;
    }
    // our own NPC ships in the host's place: the host's are the real ones
    void* me = player_ufo();
    for (void* u : all_ufos()) {
        if (u == me || F<int>(u, kType) == 0 || g_mirror_of.count(u))
            continue;
        // the mod's player puppets (Defenders) stay; the game's own Defenders (an empire's system, a
        // colony's) go like the other NPC ships: the host's come as mirrors (BUG-0111: every type 6
        // was kept, and the guest saw each defender twice). coop.fleet typefix 0: the old check
        if (modes::ufo_ident_fix ? (C.puppet_owner && C.puppet_owner(u) >= 0) : F<int>(u, kType) == 6)
            continue;
        if (modes::space_retired(u)) // BUG-0218: a game mission holds it, never freed here
            continue;
        if (g_keep_posse && F<int>(u, kType) == 3) {
            // type 3 is the posse (DestroyNPCUFOs keeps exactly type 3 when keepPosse is set): our
            // own escort, whatever empire its ships came from, not one of the host's NPCs
            g_posse_kept++;
            continue;
        }
        destroy_mirror(u, false);
        g_local_removed++;
    }
}

static void guest_tick(double now)
{
    bool here = C.with_host && C.with_host();
    for (auto& [id, m] : g_mirrors) {
        if (id == g_hold_mirror)
            continue; // tests: the one frame between the free and this tick, held open
        if (m.u && !live(m.u)) { // freed by the game: spawned again with the next state
            g_mirror_of.erase(m.u);
            m.u = nullptr;
        }
        if (!m.u)
            continue;
        if (!here || now - m.seen > 3.0)
            continue;
        net::Vec3 at = m.est.at(now); // moved on with the estimated velocity (fast ships)
        float p[3] = {at.x, at.y, at.z}, q[4] = {m.rot.x, m.rot.y, m.rot.z, m.rot.w};
        vcall<void>((uint8_t*)m.u + kLoco, 0x38 / 4, (const float*)p); // SetPosition
        vcall<void>((uint8_t*)m.u + kLoco, 0x3C / 4, (const float*)q); // SetOrientation
        // the UFO's own controller steers to mNextPosition/mDestination: pin them (BUG-0190: minus the
        // mirror's own offset, with its zoom altitude, as for the puppets, modes::space_pin)
        modes::space_pin(m.u, p, g_mirror_pin_fix);
        auto& nv = F<float[3]>(m.u, kNextVel);
        nv[0] = nv[1] = nv[2] = 0;
        auto& nr = F<float[4]>(m.u, kNextRot);
        for (int i = 0; i < 4; i++)
            nr[i] = q[i];
        F<uint8_t>(m.u, kAtDest) = 1;
        F<float>(m.u, kHp) = m.hp > 0 ? m.hp : 1;
    }
    if (!here && !g_mirrors.empty()) { // we left the host's place: drop the mirrors
        for (auto& [id, m] : g_mirrors)
            if (m.u) {
                g_mirror_of.erase(m.u);
                destroy_mirror(m.u, false);
            }
        g_mirrors.clear();
    }
}

void tick(double now)
{
    modes::space_retired_tick(); // BUG-0218 (reads the UFO list only, before it touches a ship)
    if (!in_space() || !C.sess || !C.sess->connected() || !player_ufo())
        return;
    g_loco_vt = *(void**)((uint8_t*)player_ufo() + kLoco); // BUG-0479 (h_SetScale)
    if (is_host()) {
        host_tick(now);
        host_send_shots(now); // BUG-0677
    } else if (is_guest()) {
        guest_tick(now);
        if (C.with_host && C.with_host())
            guest_fire(now); // BUG-0677
        else
            g_ship_shots_in.clear();
    }
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!in_space() || !player_ufo())
        return;
    if (type == net::MSG_UFO_STATE && is_guest()) {
        guest_state(r);
    } else if (type == net::MSG_UFO_HIT && is_host()) {
        uint16_t id = r.u16();
        float dmg = r.f32();
        int t = (int)r.u32();
        float dir[3] = {r.f32(), r.f32(), r.f32()};
        auto it = g_hid_rev.find(id);
        if (!r.ok() || it == g_hid_rev.end() || !live(it->second) || F<int>(it->second, kState) == 2)
            return;
        if (friendly(it->second)) { // BUG-0420: a guest's shot at a ship of the players' own empire (an escort)
            g_friendly_dropped++;
            return;
        }
        dmg = net::sane_damage(dmg, 100000.f); // a guest's report: finite and not negative
        if (!net::finite3(dir))
            dir[0] = dir[1] = dir[2] = 0;
        void* puppet = hit_puppet(from);
        g_applying = true;
        o_TakeDamage((uint8_t*)it->second + kCombat, dmg, skg::cdecl_call<uint32_t>(a_PlayerEmpireID), t, dir,
                     puppet ? (uint8_t*)puppet + kCombat : nullptr);
        g_applying = false;
        g_hits_in++;
    } else if (type == net::MSG_UFO_DAMAGE && is_guest()) {
        float dmg = r.f32();
        int t = (int)r.u32();
        float dir[3] = {r.f32(), r.f32(), r.f32()};
        uint16_t att = r.u16();
        void* me = player_ufo();
        if (!r.ok() || !me)
            return;
        auto it = g_mirrors.find(att);
        void* a = hit_attacker(it != g_mirrors.end() ? it->second.u : nullptr);
        g_hold_mirror = 0xFFFF;
        // through the hook (g_applying: straight to the game after the own-ship filter): a lethal hit
        // next to a partner only disables the ship (spacecoop rescue). Calling the original here
        // skipped that, and the guest's ship died in real fights with the partner next to it.
        g_applying = true;
        h_TakeDamage((uint8_t*)me + kCombat, dmg, a ? F<uint32_t>(a, kPol) : 0xFFFFFFFFu, t, dir,
                     a ? (uint8_t*)a + kCombat : nullptr);
        g_applying = false;
        g_damage_in++;
    }
}

static std::string status_ships();
void on_session_end()
{
    for (auto& [id, m] : g_mirrors)
        if (m.u)
            destroy_mirror(m.u, false);
    g_mirrors.clear();
    g_mirror_of.clear();
    g_hid.clear();
    g_hid_rev.clear();
    g_ship_shots.clear();    // BUG-0677
    g_ship_shots_in.clear();
    g_hold_mirror = 0xFFFF;
    g_stale_puppet = nullptr;
    modes::space_retired_clear("session end");
    g_cycle_ids.clear();
    g_rx_any = false;
    g_rx_mask = 0;
    g_rx_done = false;
    g_drop_left = 0;
}

std::string status()
{
    char sc[200];
    snprintf(sc, sizeof sc, "space puppet scale: fix=%d sized=%u replaced=%u game=%.3f own=%.3f from=%08X\n",
             g_puppet_scale_fix ? 1 : 0, g_scale_puppet_calls, g_scale_replaced, g_scale_game, g_scale_own,
             (unsigned)g_scale_caller);
    char sh[260];
    snprintf(sh, sizeof sh,
             "space shots: fix=%d seen=%u tx=%u untracked=%u rx=%u fired=%u refused=%u nomirror=%u notool=%u notarget=%u "
             "released=%u hits_dropped=%u nullhits_dropped=%u\n",
             g_shots_fix ? 1 : 0, g_shots_seen, g_shots_tx, g_shots_untracked, g_shots_rx, g_shots_fired, g_shots_refused,
             g_shots_nomirror, g_shots_notool, g_shots_notarget, g_shots_released, g_shot_hits_dropped,
             g_shot_nullhits_dropped);
    return status_ships() + sc + sh;
}

static std::string status_ships()
{
    char b[680];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b,
                 "space(host): ships=%u hits_in=%u damage_out=%u freed_by_game=%u puppet_shots_blocked=%u "
                 "att_dropped=%u att_freed=%u controller_skips=%u save_stripped=%u/%u friendly_dropped=%u\n",
                 (unsigned)g_hid.size(), g_hits_in, g_damage_out, game::g_ufos_freed, g_puppet_shots, g_att_dropped,
                 g_att_freed, g_ctl_skipped, g_ufo_stripped, g_ufo_strip_saves, g_friendly_dropped);
    else
        snprintf(b, sizeof b,
                 "space(guest): mirrors=%u created=%u removed=%u local_removed=%u hits_sent=%u damage_in=%u "
                 "freed_by_game=%u puppet_shots_blocked=%u mirror_hits=%u att_dropped=%u att_freed=%u stale_chunks=%u "
                 "partial_cycles=%u posse_kept=%u unknown_models=%u content_checked=%u controller_skips=%u save_stripped=%u/%u\n",
                 (unsigned)g_mirrors.size(), g_created, g_removed, g_local_removed, g_hits_sent, g_damage_in,
                 game::g_ufos_freed, g_puppet_shots, g_mirror_hits, g_att_dropped, g_att_freed, g_stale_chunks,
                 g_partial_cycles, g_posse_kept, g_unknown_models, g_content_checked, g_ctl_skipped, g_ufo_stripped,
                 g_ufo_strip_saves);
    if (!(C.sess && C.sess->role() == net::Role::Host)) { // BUG-0723 / BUG-0724
        size_t n = strlen(b);
        if (n && b[n - 1] == '\n')
            b[--n] = 0;
        snprintf(b + n, sizeof b - n, " zero_keys=%u design_waits=%u designs_used=%u\n", g_zero_keys, g_design_waits,
                 g_designs_used);
    }
    return b;
}

// ---------------- coop.fleet track (BUG-0378) ----------------
// What this game drew of every ship: sampled at the start of the mod's frame, i.e. after the game's
// own update moved the ships and the frame was rendered, before the mod writes any pose. Rows on the
// session clock, so a test compares one game's drawn ship with the other game's own ship at the same
// session time (lag) and at an earlier one (lag taken out: a real offset stays). kind: m = our own
// ship (the truth of this game), p = a partner's puppet, s = a host NPC ship (host: the ship itself,
// guest: its mirror). off = mOffsetFromPosition (+0x740), zoom = mZoomAltitude (+0x768).
// BUG-0388: a puppet's row also has the pose the mod gave it last (sx sy sz: the partner's state as
// sampled, before the game's update), how far that state was extrapolated past the newest snapshot (ex,
// s) and whether it was pinned for the game's update (pin); other rows: their own position, 0, 0.
struct TrackRow {
    float t;
    char kind;
    uint16_t id;
    float x, y, z, ox, oy, oz, zoom;
    float sx, sy, sz, ex;
    uint8_t pin;
};
static std::deque<TrackRow> g_track;
static bool g_track_on;
constexpr size_t kTrackMax = 120000;

static void track_rows(double t, bool clock_ok);
void frame_start(double t, bool clock_ok)
{
    track_rows(t, clock_ok);  // (before the clear: a row tells whether its puppet was pinned)
    modes::space_pin_frame(); // BUG-0378: what was pinned for the game's last update is done
}

static void track_rows(double t, bool clock_ok)
{
    if (!g_track_on || !clock_ok || !in_space() || !C.sess || !C.sess->connected())
        return;
    void* me = player_ufo();
    if (!me)
        return;
    auto row = [&](char kind, uint16_t id, void* u) {
        auto& p = F<float[3]>(u, kPos);
        auto& o = F<float[3]>(u, 0x740);
        float sp[3] = {p[0], p[1], p[2]}, ex = 0;
        bool noted = kind == 'p' && modes::space_drive_note(u, sp, ex);
        g_track.push_back({(float)t, kind, id, p[0], p[1], p[2], o[0], o[1], o[2], F<float>(u, 0x768), sp[0], sp[1], sp[2],
                           ex, (uint8_t)(noted && modes::space_pinned(u))});
    };
    for (void* u : all_ufos()) {
        if (u == me) {
            row('m', C.sess->my_id(), u);
            continue;
        }
        int owner = C.puppet_owner ? C.puppet_owner(u) : -1;
        if (owner >= 0)
            row('p', (uint16_t)owner, u);
        else if (is_host() && g_hid.count(u))
            row('s', g_hid[u], u);
        else if (is_guest() && g_mirror_of.count(u))
            row('s', g_mirror_of[u], u);
    }
    while (g_track.size() > kTrackMax)
        g_track.pop_front();
}

static std::string track_cmd(const std::vector<std::string>& a)
{
    char b[160];
    if (a.size() >= 2 && (a[1] == "on" || a[1] == "off")) {
        g_track_on = a[1] == "on";
        if (g_track_on)
            g_track.clear();
    } else if (a.size() >= 2 && a[1] == "dump") { // dump [from] [n]: t kind id x y z ox oy oz zoom
        size_t from = a.size() > 2 ? (size_t)atol(a[2].c_str()) : 0, n = a.size() > 3 ? (size_t)atol(a[3].c_str()) : 2000;
        std::string out;
        for (size_t i = from; i < g_track.size() && i < from + n; i++) {
            const auto& r = g_track[i];
            snprintf(b, sizeof b, "%.4f %c %u %.2f %.2f %.2f %.2f %.2f %.2f %.1f %.2f %.2f %.2f %.3f %u\n", r.t, r.kind, r.id,
                     r.x, r.y, r.z, r.ox, r.oy, r.oz, r.zoom, r.sx, r.sy, r.sz, r.ex, (unsigned)r.pin);
            out += b;
        }
        return out;
    }
    snprintf(b, sizeof b, "track %s samples=%u\n", g_track_on ? "on" : "off", (unsigned)g_track.size());
    return b;
}

std::string debug(const std::vector<std::string>& a)
{
    if (!a.empty() && a[0] == "track")
        return track_cmd(a);
    void* me = player_ufo();
    if (!me || !in_space())
        return "not in space";
    if (!a.empty() && a[0] == "list") {
        std::string out;
        char b[200];
        for (void* u : all_ufos()) {
            net::Vec3 p = pos_of(u);
            int id = -1;
            if (g_hid.count(u))
                id = g_hid[u];
            else if (g_mirror_of.count(u))
                id = g_mirror_of[u];
            // (inv, model, puppet: BUG-0522's save test tells our ships from the game's by them)
            snprintf(b, sizeof b, "  ship #%d %p type=%d pol=%08X hp=%.0f/%.0f%s pos=(%.0f %.0f %.0f) inv=%d model=%08X%s\n",
                     id, u, F<int>(u, kType), F<uint32_t>(u, kPol), F<float>(u, kHp), F<float>(u, kMaxHp),
                     u == me ? " (me)" : "", p.x, p.y, p.z, (int)F<uint8_t>(u, kInvincible), F<uint32_t>(u, kModel),
                     modes::space_puppet_ok(u) ? " puppet" : "");
            out += b;
        }
        return status() + out;
    }
    if (a.size() >= 3 && a[0] == "shoot") { // shoot <id> <dmg>: our ship hits ship #id (through the hook)
        void* t = nullptr;
        uint16_t id = (uint16_t)atoi(a[1].c_str());
        if (is_guest()) {
            auto it = g_mirrors.find(id);
            t = it == g_mirrors.end() ? nullptr : it->second.u;
        } else {
            auto it = g_hid_rev.find(id);
            t = it == g_hid_rev.end() ? nullptr : it->second;
        }
        if (!t || !live(t))
            return "no such ship";
        float dir[3] = {0, 0, 1};
        h_TakeDamage((uint8_t*)t + kCombat, (float)atof(a[2].c_str()), skg::cdecl_call<uint32_t>(a_PlayerEmpireID), 0,
                     dir, (uint8_t*)me + kCombat);
        return "shot";
    }
    if (a.size() >= 2 && a[0] == "spawn") { // spawn <type> [own]: a ship without an empire (pirate) next to
        // us, or with our own empire's political id (`own`: type 3 = an escort ship of ours, BUG-0084)
        uint32_t pol = a.size() >= 3 && a[2] == "own" ? skg::cdecl_call<uint32_t>(a_PlayerEmpireID) : 0xFFFFFFFFu;
        void* u = skg::cdecl_call<void*>(a_SpawnUFO, atoi(a[1].c_str()), &pol, (void*)&F<uint32_t>(me, kModel));
        if (!u)
            return "spawn failed";
        net::Vec3 p = pos_of(me);
        float pp[3] = {p.x + 30.f, p.y, p.z + 10.f};
        auto& q = F<float[4]>(me, kRot);
        vcall<void>((uint8_t*)u + kLoco, 0x44 / 4, (const float*)pp, (const float*)q);
        char b[64];
        snprintf(b, sizeof b, "spawned %p type=%d pol=%08X", u, F<int>(u, kType), F<uint32_t>(u, kPol));
        return b;
    }
    if (a.size() >= 3 && a[0] == "hurt") { // host: ship #id hits player <p>'s puppet [with dmg]
        auto it = g_hid_rev.find((uint16_t)atoi(a[1].c_str()));
        void* pup = C.puppet_of ? C.puppet_of((uint8_t)atoi(a[2].c_str())) : nullptr;
        if (it == g_hid_rev.end() || !pup || !live(it->second) || !live(pup))
            return "no such ship or puppet";
        float dir[3] = {0, 0, 1};
        h_TakeDamage((uint8_t*)pup + kCombat, a.size() > 3 ? (float)atof(a[3].c_str()) : 5.f,
                     F<uint32_t>(it->second, kPol), 0, dir, (uint8_t*)it->second + kCombat);
        return "hit sent";
    }
    if (a.size() >= 2 && a[0] == "fakemodel") { // tests (BUG-0323): 1 = send made-up model instances
        g_fake_zero = a[1] == "zero";              // zero (BUG-0723): send keys of zeros
        if (g_fake_zero)
            return "fakemodel zero";
        g_fake_model = a[1] != "0";                // 2 <hex instance> (BUG-0469): send that model instance
        g_fake_inst = a.size() > 2 ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : 0;
        char b[96] = {};
        snprintf(b, sizeof b, "fakemodel %s%s%08X", g_fake_model ? "1" : "0", g_fake_inst ? " instance " : "",
                 g_fake_inst);
        return b;
    }
    if (a.size() >= 2 && a[0] == "chunkfix") { // BUG-0037: 0 = the removal pass on the last chunk (tests)
        g_chunk_fix = a[1] != "0";
        return std::string("chunkfix ") + (g_chunk_fix ? "1" : "0");
    }
    if (a.size() >= 2 && a[0] == "keepposse") { // BUG-0084: 0 = the guest destroys its own escort (tests)
        g_keep_posse = a[1] != "0";
        return std::string("keepposse ") + (g_keep_posse ? "1" : "0");
    }
    if (a.size() >= 3 && a[0] == "drop") { // tests (BUG-0037, host): skip the next n sends of chunk k
        g_drop_chunk = (uint8_t)atoi(a[1].c_str());
        g_drop_left = atoi(a[2].c_str());
        return "dropping";
    }
    if (a.size() >= 2 && (a[0] == "impostor_id" || a[0] == "impostor_mission")) { // tests (BUG-0218)
        void* pup = C.puppet_of ? C.puppet_of((uint8_t)atoi(a[1].c_str())) : nullptr;
        if (a[0] == "impostor_mission" && a.size() >= 4) // impostor_mission <p> 0 <ptr>: a retired ship lets go
            pup = (void*)(uintptr_t)strtoull(a[3].c_str(), nullptr, 16);
        if (!pup)
            return "no puppet";
        std::string r = modes::space_ident_test(pup, a[0], a.size() < 3 || a[2] != "0");
        char b[64];
        snprintf(b, sizeof b, " (%p)", pup);
        return r + b;
    }
    if (a.size() >= 1 && a[0] == "ident") // tests (BUG-0218): the identity counters
        return modes::space_ident_test(nullptr, "", false);
    if (a.size() >= 2 && (a[0] == "puppet" || a[0] == "impostor")) { // tests (BUG-0111): player <p>'s puppet;
        // impostor: make it vulnerable, as a game Defender made at the freed puppet's address is (another
        // model key would do too, but an invalid one costs every mirror of the ship a caught fault)
        void* pup = C.puppet_of ? C.puppet_of((uint8_t)atoi(a[1].c_str())) : nullptr;
        if (!pup || !live(pup))
            return "no puppet";
        if (a[0] == "impostor")
            F<uint8_t>(pup, kInvincible) = 0;
        char b[120];
        snprintf(b, sizeof b, "puppet %p type=%d pol=%08X model=%08X invincible=%d", pup, F<int>(pup, kType),
                 F<uint32_t>(pup, kPol), F<uint32_t>(pup, kModel), (int)F<uint8_t>(pup, kInvincible));
        return b;
    }
    if (a.size() >= 2 && a[0] == "typefix") { // BUG-0111: 0 = any listed Defender is a partner's puppet (tests)
        modes::ufo_ident_fix = a[1] != "0";
        return std::string("typefix ") + (modes::ufo_ident_fix ? "1" : "0");
    }
    if (a.size() >= 2 && a[0] == "hitfix") { // BUG-0054: 0 = the old unchecked attacker (tests)
        g_hit_fix = a[1] != "0";
        return std::string("hitfix ") + (g_hit_fix ? "1" : "0");
    }
    if (a.size() >= 2 && a[0] == "freemirror") { // tests (BUG-0054): free mirror #id as a context switch
        // does, and keep its pointer listed until the next MSG_UFO_DAMAGE (the one-frame window)
        auto it = g_mirrors.find((uint16_t)atoi(a[1].c_str()));
        if (!is_guest() || it == g_mirrors.end() || !it->second.u || !live(it->second.u))
            return "no such mirror";
        g_hold_mirror = it->first;
        game::destroy_creature(it->second.u);
        return live(it->second.u) ? "still listed" : "freed";
    }
    if (a.size() >= 2 && a[0] == "freepuppet") { // tests (BUG-0054, host): free player <p>'s puppet; the
        // next MSG_UFO_HIT gets that pointer as its attacker (the frame before drive_puppet notices)
        void* pup = C.puppet_of ? C.puppet_of((uint8_t)atoi(a[1].c_str())) : nullptr;
        if (!is_host() || !pup || !live(pup))
            return "no such puppet";
        g_stale_puppet = pup;
        game::destroy_creature(pup);
        return live(pup) ? "still listed" : "freed";
    }
    return "usage: coop.fleet list | spawn <type> [own] | shoot <id> <dmg> | hurt <ship id> <player> | hitfix 0|1 | "
           "chunkfix 0|1 | keepposse 0|1 | drop <chunk> <n> | freemirror <id> | freepuppet <player>";
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    auto rot = [](void* u) {
        auto& r = F<float[4]>(u, kRot);
        return net::Quat{r[0], r[1], r[2], r[3]};
    };
    auto ufos = all_ufos();
    auto live = [&](void* x) { return std::find(ufos.begin(), ufos.end(), x) != ufos.end(); };
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [u, id] : g_hid)
            if (live(u))
                v.push_back({"s" + std::to_string(id), pos_of(u), rot(u)});
    } else {
        for (auto& [id, m] : g_mirrors)
            if (m.u && live(m.u))
                v.push_back({"s" + std::to_string(id), pos_of(m.u), rot(m.u)});
    }
    return v;
}

} // namespace squadron
