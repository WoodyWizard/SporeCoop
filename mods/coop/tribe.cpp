#include "tribe.h"
#include "creature_hp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "coop_ui.h"
#include "creations.h"
#include "founders.h"
#include "world.h"
#include "driver.h"
#include "game.h"
#include "modes.h"
#include "mouse.h"
#include "shared.h"
#include "npc.h"
#include "space_ext.h"
#include "stage_epoch.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;

// ---- layout (docs/re/tribe.md) ----
constexpr uint32_t kTribeNoun = 0x18C6D19;
constexpr uint32_t kHutNoun = 0x1E4DAAE, kToolNoun = 0x18C8F0C;
constexpr uintptr_t kMembersOff = 0x340;   // cTribe: vector<cCreatureCitizen*>
constexpr uintptr_t kNativeLeaderOff = 0x11C; // cCommunity::mpLeader
constexpr uintptr_t kTribePosOff = 0x124;  // cTribe spatial position
constexpr uintptr_t kOwnerTribeOff = 0x1014; // cCreatureCitizen::mpOwnerTribe
constexpr uintptr_t kAgeOff = 0xB34;       // 0 baby, 1 adult
constexpr uintptr_t kSelectedOff = 0x12C;  // spatial+0x6C IsSelected
static uintptr_t a_CitizenVt; // Simulator::cCreatureCitizen::vftable
constexpr uint32_t kTreeNoun = 0x6ABEE7E;    // cTreeData (planet flora: fruit trees), not a cGameData
static uintptr_t a_TreeVt;    // Simulator::cTreeData::vftable (main vtable)

static uintptr_t a_GetPlayerTribe, a_DoAction, a_SetValueVector3, a_BTTick, a_DoActionForObjects, a_WalkSelected,
    a_SetSelected, a_CreateCitizen, a_AddMember, a_SpawnNpcTribe, a_SetChieftain, a_SetLeader;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
bool active() { return g_ok && C.sess && C.sess->connected() && game::mode_id() == modes::kTribe; }
// BUG-0151: while the host is away (main menu) nothing simulates or orders the guest's members (the
// host sends no states and drops orders outside Tribe): the guest's own game runs their AI and its
// orders until the host is back. `coop.tribe fix away 0`: the old frozen tribe (tests).
static bool g_fix_away = true;
COOP_FIX("tribe.away", "BUG-0151", g_fix_away, "tribe: while the host is away the guest's own game runs its members");
static uint32_t g_away_orders;
static bool local_play() { return g_fix_away && is_guest() && C.host_away && C.host_away(); }

static void* player_tribe()
{
    void* nm = game::noun_manager();
    return nm ? thiscall<void*>(a_GetPlayerTribe, nm) : nullptr;
}

static bool is_citizen(void* c) { return c && !IsBadReadPtr(c, 8) && *(uintptr_t*)c == a_CitizenVt; }

static std::vector<void*> members(void* t)
{
    std::vector<void*> v;
    void** b = F<void**>(t, kMembersOff);
    void** e = F<void**>(t, kMembersOff + 4);
    for (void** it = b; it && it < e && v.size() < 256; ++it)
        if (*it && game::object_alive(*it))
            v.push_back(*it);
    return v;
}

static std::vector<void*> all_tribes()
{
    std::vector<void*> v;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun == kTribeNoun && game::object_alive(o))
                ((std::vector<void*>*)u)->push_back(o);
            return true;
        },
        &v);
    return v;
}

static net::Vec3 tribe_pos(void* t)
{
    auto& p = F<float[3]>(t, kTribePosOff);
    return {p[0], p[1], p[2]};
}

// ---- planet trees (docs/re/tribe.md, "Trees"): the flora object keeps a sorted vector of patches,
// each patch has three fixed_vectors of intrusive_ptr<cTreeData>; a tree's position is at +0x10.
// Gather orders on a fruit tree name a cTreeData, which is no cGameData: it is found by position.
static bool is_tree(void* t) { return t && !IsBadReadPtr(t, 0x20) && *(uintptr_t*)t == a_TreeVt; }

template <typename Fn>
static void for_each_tree(Fn fn)
{
    static uintptr_t get = H->sym("SporeCoop::Flora_Get");
    uint8_t* fl = get ? skg::cdecl_call<uint8_t*>(get) : nullptr;
    if (!fl || IsBadReadPtr(fl + 0x1E5F0, 8))
        return;
    uint8_t* b = F<uint8_t*>(fl, 0x1E5F0);
    uint8_t* e = F<uint8_t*>(fl, 0x1E5F4);
    constexpr size_t kPatch = 0x19A8;
    if (!b || e < b || (size_t)(e - b) % kPatch || (size_t)(e - b) / kPatch > 20000)
        return;
    for (uint8_t* el = b; el < e; el += kPatch) {
        uint8_t* rec = el + 8;
        for (uintptr_t vo : {0x44u, 0x9Cu, 0xF4u}) {
            void** tb = F<void**>(rec, vo);
            void** te = F<void**>(rec, vo + 4);
            if (!tb || te < tb || te - tb > 64 || IsBadReadPtr(tb, (te - tb) * 4))
                continue;
            for (void** it = tb; it < te; ++it)
                if (is_tree(*it))
                    fn(*it, net::Vec3{F<float>(*it, 0x10), F<float>(*it, 0x14), F<float>(*it, 0x18)});
        }
    }
}

static void* find_tree(const net::Vec3& pos, float max_dist)
{
    void* best = nullptr;
    float bd = max_dist;
    for_each_tree([&](void* t, const net::Vec3& p) {
        float d = net::dist(p, pos);
        if (d < bd) {
            bd = d;
            best = t;
        }
    });
    return best;
}

void for_each_tribe_center(void (*fn)(const std::string& key, const net::Vec3& pos, void* user), void* user)
{
    if (!g_ok)
        return;
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t i = 0; i < tribes.size(); i++)
        fn("tribe" + std::to_string(i) + (tribes[i] == pt ? "p" : ""), tribe_pos(tribes[i]), user);
}

void for_each_tree_pos(void (*fn)(const net::Vec3& pos, bool fruit, void* user), void* user)
{
    if (g_ok) // +0x48: the fruit left (bits, +0x4C = all slots ~): a tree without fruit is no gather target
        for_each_tree([&](void* t, const net::Vec3& p) { fn(p, F<uint32_t>(t, 0x48) != 0, user); });
}

// ---------------- host ----------------
struct HMember {
    uint16_t id;
    uint8_t owner;
};
static std::unordered_map<void*, HMember> g_hm;
static std::unordered_map<uint16_t, void*> g_hm_by_id;
static uint16_t g_next_id = 1;
static uint32_t g_player_members_seen = 0;
static std::vector<uint8_t> g_split_for; // player ids the player tribe was split for
static founders::Roster g_order_roster; // host opt-in for this world and session only
static founders::Outfits g_outfits;
struct OutfitVote {
    uint32_t world = 0, token = 0, previous = 0, model = 0;
    uint8_t owner = 0;
    bool ready = false, saved = false, host_entered = false;
};
static OutfitVote g_outfit;
static uint32_t g_outfit_serial;
static bool g_outfit_queued;
static double g_outfit_poll;
static double g_outfit_settled;
static bool g_outfit_recovery = true;
COOP_FIX("tribe.outfit_recovery", "BUG-9044", g_outfit_recovery,
         "tribe: accept a new voted outfit from a recognized earlier founder appearance");
static bool founder_model(size_t seat, uint32_t model)
{
    return model && (g_outfits.contains(seat, model) || g_order_roster.entries[seat].species == model);
}
static bool g_restore_village_orders = true;
static bool g_guest_order_mode;
static bool g_repair_requested, g_repair_served, g_guest_orders_held;
static double g_extra_since = -1;
static uint32_t g_form_skipped;
static int g_food_cycle = -1;
static float g_home_food = -1, g_guest_food = -1;
bool village_orders() { return is_host() ? g_order_roster.guest().faction != 0 : g_guest_order_mode; }
std::string village_food_line()
{
    if (!village_orders() || g_home_food < 0 || g_guest_food < 0) return {};
    char line[88];
    snprintf(line, sizeof line, "Village food   Host %.2f   Guest %.2f", g_home_food, g_guest_food);
    return line;
}
static double g_last_send;
static uint16_t g_cycle;
static uint32_t g_cmds_done, g_cmds_rejected;
static std::string g_last_cmd; // the last order (guest: sent, host: done or why it was rejected), tests
struct PerformanceDispatch {
    uint32_t sequence = 0;
    unsigned target = 0, requested = 0, dispatched = 0, records = 0;
    bool deferred = false;
};
static PerformanceDispatch g_perform_dispatch;
static std::string g_last_local; // the host's own last order (tests)
static uint32_t g_local_orders;
static uint32_t g_gifts;

struct FounderRescue {
    uint16_t member = 0;
    uint32_t faction = 0;
    double until = 0, near_since = -1, grace_until = 0;
    net::Vec3 anchor;
    uint32_t attacker_faction = 0;
    bool hut_down = false;
};
static std::array<FounderRescue, 2> g_founder_rescues;
static std::array<std::pair<uint16_t, double>, 2> g_remote_rescues;
constexpr uint32_t kRescueTag = 0x56525254; // TRRV
constexpr size_t kRescueBytes = 12;
// BUG-9016: replacing the guest's citizens discarded their native chief. The
// co-op chief markers do not restore cCommunity::mpLeader or the native staff.
static bool g_native_chiefs_fix = true;
COOP_FIX("tribe.native_chiefs", "BUG-9016", g_native_chiefs_fix, "tribe: mirror the host's native chieftain after a complete citizen snapshot");
static bool g_growth_fix = true;
COOP_FIX("tribe.growth", "BUG-9021", g_growth_fix,
         "tribe: mirror native baby growth without replacing the citizen");
constexpr uint32_t kNativeChiefsTag = 0x46484354; // TCHF
static bool founder_downed(void* c)
{
    auto it = g_hm.find(c);
    if (it == g_hm.end()) return false;
    for (const auto& rescue : g_founder_rescues)
        if (rescue.until > 0 && (rescue.member == it->second.id ||
            (rescue.hut_down && F<uint32_t>(c, 0x30) == rescue.faction))) return true;
    return false;
}

// ---------------- guest ----------------
static float g_test_lift = 0.f; // tests (BUG-0329): `coop.tribe lift <m>` on a guest
struct GMember {
    void* c = nullptr;
    uint16_t id = 0; // the host's member id
    uint8_t tribe = 0, owner = 0xFE, age = 1;
    net::Vec3 pos, vel;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    drive::State ds;
    net::SnapshotBuffer buf; // BUG-0365 (tribe.mirror_time): the host's states by its clock
    net::Vec3 aim;           // coop.track: where the driving aimed this frame
    float aim_speed = 0;     // and the host's speed of that state
};
static std::unordered_map<uint16_t, GMember> g_gm;
// BUG-0365 (see mirror_drive)
static bool g_mirror_walk_fix = true, g_mirror_time_fix = true;
COOP_FIX("tribe.mirror_walk", "BUG-0365", g_mirror_walk_fix, "tribe: a guest's mirror of a walking member walks on the host's path instead of stop-and-go and surges");
COOP_FIX("tribe.mirror_time", "BUG-0365", g_mirror_time_fix, "tribe: a guest's mirrors follow the host's states by its clock (an interpolation buffer), not by their arrival");
static uint32_t g_mirror_walks, g_mirror_timed;
// BUG-0444: the mirrors kept their own distance from the planet's centre (own_height, BUG-0329) and were never
// put onto this game's ground: that is the game's own locomotion, and a guest's game does not update its
// citizens for 20-40 s after a cold join (game time paused, no BehaviorTreeTick) or while it is paused. A member
// the host walked up a slope then was placed on the host's path at its old height, deeper and deeper under the
// guest's ground (2.6-3.3 m for ~20 s in tribe_coop's hunt; 9 m in 60 m up a hill, tribe_mirror_ground).
// tribe.mirror_ground: the ground policy of the animal proxies (npc.proxy_ground, BUG-0399, driver.h): a new
// mirror is made on this game's ground when the host's position is under it, a walking one is placed onto it
// (WalkPlace::ground, and out_of_ground for a mirror deeper than its 3 m), a standing one found more than 0.25 m
// under it is teleported onto it (drive::step's ground). `coop.fix tribe.mirror_ground off`: own height only.
static bool g_mirror_ground_fix = true;
COOP_FIX("tribe.mirror_ground", "BUG-0444", g_mirror_ground_fix, "tribe: a guest's mirrors of members are made and kept on this game's ground, never left under it (as the animal proxies)");
static uint32_t g_mirror_grounded; // diagnostics (coop.tribe): standing mirrors put out of the ground
constexpr double kMirrorDelay = 0.2;  // s: the interpolation delay (the host sends every 0.12 s or more)
constexpr float kMirrorWalkMax = 8.f; // farther off: drive::step (it teleports at 8 m)
constexpr double kPathDt = 0.1;       // s: the interpolated path's velocity is taken over this
static std::unordered_map<void*, uint16_t> g_gid_of;
static std::unordered_set<uint16_t> g_cycle_ids;
static std::unordered_set<uint8_t> g_cycle_chunks; // chunks of the current cycle received
static uint32_t g_incomplete;                      // cycles whose cleanup was skipped (a chunk missing)
static uint16_t g_rx_cycle = 0xFFFF;
static uint32_t g_spawned, g_removed, g_cmds_sent;
static std::vector<void*> g_local_tribe_of; // host tribe index -> local tribe
static double g_march_until;
static uint32_t g_march_click;
static bool g_attack_click;
static bool g_raid_click;
static bool g_equip_click;
static bool g_heal_click;
static bool g_gift_click;
static bool g_perform_click;
static bool g_performance_fix = true;
COOP_FIX("tribe.performance", "BUG-9035", g_performance_fix,
         "tribe: route guest performances and instrument choices through native sessions");
static bool g_gift_orders = true;
COOP_FIX("tribe.gift_orders", "BUG-9033", g_gift_orders,
         "tribe: relay validated guest food gifts through native citizen AI");
static bool g_heal_orders = true;
static bool g_eat_orders = true;
COOP_FIX("tribe.eat_orders", "BUG-9032", g_eat_orders,
         "tribe: guest villagers can eat from their own native food mat");
COOP_FIX("tribe.heal_orders", "BUG-9031", g_heal_orders,
         "tribe: equipped guest healers can heal living founder citizens");
static int g_build_kind;
static bool g_target_ray = true;
COOP_FIX("tribe.target_ray", "BUG-9019", g_target_ray,
         "tribe: visible targets above the home village sphere can be clicked");

static bool target_near_side(const net::Vec3& camera, const net::Vec3& point)
{
    net::Vec3 direction{point.x - camera.x, point.y - camera.y, point.z - camera.z};
    float length = drive::speed(direction);
    if (length < 1) return false;
    float radius = drive::speed(point) * 0.985f;
    float b = (camera.x * direction.x + camera.y * direction.y + camera.z * direction.z) / length;
    float c = camera.x * camera.x + camera.y * camera.y + camera.z * camera.z - radius * radius;
    float disc = b * b - c;
    return disc <= 0 || -b - std::sqrt(disc) >= length - 2;
}
static uint32_t g_building_sequence, g_building_applied;
static double g_building_sent;
static std::unordered_map<uint32_t, uint32_t> g_building_received;

static bool g_equipment_fix = true;
COOP_FIX("tribe.equipment", "BUG-9022", g_equipment_fix,
         "tribe: mirror native citizen specialization and held tool models");
static uint32_t g_equipment_sequence;
static double g_equipment_sent;
static std::unordered_map<uint16_t, uint32_t> g_equipment_received;

static void host_equipment(double now)
{
    if (now - g_equipment_sent < 0.3) return;
    g_equipment_sent = now;
    ++g_equipment_sequence;
    struct Entry { uint16_t id; uint8_t tool, item; };
    std::vector<Entry> entries;
    for (const auto& [citizen, member] : g_hm) {
        if (!game::object_alive(citizen) || !is_citizen(citizen)) continue;
        unsigned tool = F<uint32_t>(citizen, 0x102C), item = F<uint32_t>(citizen, 0xFFC);
        if (tool <= 11 && item <= 23) entries.push_back({member.id, (uint8_t)tool, (uint8_t)item});
    }
    for (size_t start = 0; start < entries.size(); start += 128) {
        net::Writer packet;
        packet.u32(modes::current_world()); packet.u32(g_equipment_sequence);
        size_t end = std::min(start + 128, entries.size());
        packet.u8((uint8_t)(end - start));
        for (size_t i = start; i < end; ++i) {
            packet.u16(entries[i].id); packet.u8(entries[i].tool); packet.u8(entries[i].item);
        }
        for (const auto& player : C.sess->players()) if (player.id)
            C.sess->send_unreliable_to(player.id, net::MSG_TRIBE_EQUIPMENT, packet);
    }
}

static void guest_equipment(net::Reader& packet)
{
    if (!g_equipment_fix) return;
    uint32_t world = packet.u32(), sequence = packet.u32();
    unsigned count = packet.u8();
    if (!packet.ok() || !world || world != modes::current_world() || count > 128 || packet.left() != count * 4) return;
    struct Entry { uint16_t id; uint8_t tool, item; };
    std::vector<Entry> entries;
    for (unsigned i = 0; i < count; ++i) {
        Entry e{packet.u16(), packet.u8(), packet.u8()};
        if (!e.id || e.tool > 11 || e.item > 23) return;
        entries.push_back(e);
    }
    static uintptr_t specialize = H->sym("SporeCoop::Citizen_SetSpecializedTool");
    static uintptr_t hold = H->sym("SporeCoop::Citizen_SetHandheldItem");
    if (!specialize || !hold) return;
    for (const auto& e : entries) {
        auto previous = g_equipment_received.find(e.id);
        if (previous != g_equipment_received.end() && (int32_t)(sequence - previous->second) <= 0) continue;
        auto member = g_gm.find(e.id);
        if (member == g_gm.end()) continue; // state may arrive after equipment; retry next snapshot
        void* citizen = member->second.c;
        if (!game::object_alive(citizen) || !is_citizen(citizen) || F<uint32_t>(citizen, kAgeOff) != 1) continue;
        if (F<uint32_t>(citizen, 0x102C) != e.tool) thiscall<void>(specialize, citizen, (int)e.tool);
        // Use the local species' preferred grasper. Host limb indices are not portable.
        if (F<uint32_t>(citizen, 0xFFC) != e.item) thiscall<void>(hold, citizen, (int)e.item, -1);
        g_equipment_received[e.id] = sequence;
    }
}

static void host_buildings(double now)
{
    if (!village_orders() || now - g_building_sent < 0.3) return;
    g_building_sent = now;
    std::vector<void*> buildings;
    game::for_each_object([](void* object, uint32_t noun, void* data) {
        if ((noun == kHutNoun || noun == kToolNoun) && game::object_alive(object)) {
            float hp = F<float>(object, 0x158), maximum = F<float>(object, 0x14C);
            net::Vec3 position{};
            if (std::isfinite(hp) && std::isfinite(maximum) && hp >= 0 && maximum > 0 && hp <= maximum &&
                maximum <= 1e7f && game::object_position(object, position))
                ((std::vector<void*>*)data)->push_back(object);
        }
        return true;
    }, &buildings);
    ++g_building_sequence;
    for (size_t start = 0; start < buildings.size(); start += 24) {
        net::Writer packet;
        packet.u32(modes::current_world()); packet.u32(g_building_sequence);
        size_t end = std::min(start + 24, buildings.size());
        packet.u8((uint8_t)(end - start));
        for (size_t i = start; i < end; ++i) {
            void* object = buildings[i];
            net::Vec3 position{};
            game::object_position(object, position);
            packet.u32(F<uint32_t>(object, 0x24)); packet.u32(game::noun_id(object)); packet.u32(F<uint32_t>(object, 0x30));
            packet.f32(position.x); packet.f32(position.y); packet.f32(position.z);
            packet.f32(F<float>(object, 0x158)); packet.f32(F<float>(object, 0x14C));
        }
        for (const auto& player : C.sess->players()) if (player.id)
            C.sess->send_unreliable_to(player.id, net::MSG_TRIBE_BUILDINGS, packet);
    }
}

static bool g_tools_fix = true;
COOP_FIX("tribe.tools", "BUG-9023", g_tools_fix,
         "tribe: reconcile native rack creation, placement and removal from host inventories");
static uint32_t g_tools_sequence;
static double g_tools_sent;
static std::unordered_map<uint32_t, uint32_t> g_tools_received;
static uint16_t g_tool_unlocks;
struct TribeInfo {
    game::Key key{};
    uint8_t arch = 0xFF;
};
static TribeInfo tribe_info(void* t);

static bool g_npc_food_fix = true;
COOP_FIX("tribe.npc_food", "BUG-9034", g_npc_food_fix,
         "tribe: mirror NPC food stores after gifts and native food interactions");
static double g_npc_food_sent;
static uint32_t g_npc_food_sequence, g_npc_food_received;
struct VillageStore { uint32_t faction, species; net::Vec3 position; float amount; };

// A locally spawned mirror has its own political ID; even an equal number
// may identify an entirely different NPC (BUG-9063). Reuse the authoritative
// village inventory. Keep only scalar IDs across frames and loads.
static bool g_faction_mapping_fix = true;
COOP_FIX("tribe.faction_mapping", "BUG-9063", g_faction_mapping_fix,
         "tribe: resolve host political IDs to the corresponding local villages");
struct LocalFaction {
    uint32_t object, faction;
    net::Vec3 host_site, ground_offset;
};
static std::unordered_map<uint32_t, LocalFaction> g_local_factions;
static uint32_t g_faction_world;

static uint32_t local_faction(uint32_t remote, const std::vector<void*>& tribes)
{
    if (!g_faction_mapping_fix) return remote;
    auto it = g_local_factions.find(remote);
    if (it == g_local_factions.end() || g_faction_world != modes::current_world()) return 0;
    for (void* t : tribes)
        if (F<uint32_t>(t, 0x24) == it->second.object && F<uint32_t>(t, 0x30) == it->second.faction)
            return it->second.faction;
    return 0;
}

static void bind_factions(uint32_t world, const std::vector<VillageStore>& stores, const std::vector<void*>& tribes)
{
    g_faction_world = world;
    g_local_factions.clear();
    std::unordered_map<uint32_t, unsigned> uses;
    for (const auto& s : stores) {
        void* match = nullptr;
        for (void* t : tribes) {
            const auto pos = tribe_pos(t);
            const float distance = net::dist(pos, s.position);
            if (!(distance < 12.f)) continue;
            auto key = tribe_info(t).key;
            if (!key.instance || (key.instance != s.species && key.instance !=
                creations::local_species({s.species, key.type, key.group}).instance)) continue;
            if (distance >= 3.f) {
                // A reloaded village can stand on a different local terrain
                // radius (BUG-0332). Require the same bearing, local ground,
                // species, world and unique native village before binding.
                const float remote_radius = drive::speed(s.position), local_radius = drive::speed(pos);
                if (!(remote_radius > 1.f && local_radius > 1.f)) continue;
                const float scale = local_radius / remote_radius;
                const net::Vec3 projected{s.position.x*scale, s.position.y*scale, s.position.z*scale};
                if (!(net::dist(projected, pos) < .75f &&
                      std::fabs(local_radius - game::ground_radius(pos)) < 1.f)) continue;
            }
            if (match) { match = nullptr; break; }
            match = t;
        }
        if (!match) continue;
        const auto pos = tribe_pos(match);
        LocalFaction local{F<uint32_t>(match, 0x24), F<uint32_t>(match, 0x30), s.position, {}};
        if (net::dist(pos, s.position) >= 3.f)
            local.ground_offset = {pos.x-s.position.x, pos.y-s.position.y, pos.z-s.position.z};
        if (!local.object || !local.faction) continue;
        g_local_factions.emplace(s.faction, local);
        ++uses[local.faction];
    }
    for (auto it = g_local_factions.begin(); it != g_local_factions.end();)
        if (uses[it->second.faction] != 1) it = g_local_factions.erase(it); else ++it;
}

static void host_npc_food(double now)
{
    if (!g_npc_food_fix || !g_order_roster.guest().faction || now - g_npc_food_sent < 1) return;
    auto tribes = all_tribes();
    if (tribes.size() > 32) return;
    std::vector<VillageStore> stores;
    for (void* t : tribes) {
        double amount = 0;
        uint32_t species = tribe_info(t).key.instance;
        if (!species || !shared::tribe_food(t, amount) || !std::isfinite(amount) || amount < 0 || amount > 1000000) continue;
        stores.push_back({F<uint32_t>(t, 0x30), species, tribe_pos(t), (float)amount});
    }
    net::Writer packet;
    packet.u32(modes::current_world()); packet.u32(++g_npc_food_sequence);
    packet.u8((uint8_t)stores.size());
    for (const auto& s : stores) {
        packet.u32(s.faction); packet.u32(s.species);
        packet.f32(s.position.x); packet.f32(s.position.y); packet.f32(s.position.z); packet.f32(s.amount);
    }
    g_npc_food_sent = now;
    for (const auto& player : C.sess->players()) if (player.id)
        C.sess->send_unreliable_to(player.id, net::MSG_TRIBE_FOOD, packet);
}

static void guest_npc_food(net::Reader& packet)
{
    uint32_t world = packet.u32(), sequence = packet.u32();
    unsigned count = packet.u8();
    if ((!g_npc_food_fix && !g_faction_mapping_fix) || !packet.ok() || !world || world != modes::current_world() || count > 32 ||
        packet.left() != count * 24 || (g_npc_food_received && (int32_t)(sequence - g_npc_food_received) <= 0)) return;
    std::vector<VillageStore> stores;
    std::unordered_set<uint32_t> seen;
    for (unsigned i = 0; i < count; ++i) {
        VillageStore s{packet.u32(), packet.u32(), {packet.f32(), packet.f32(), packet.f32()}, packet.f32()};
        if (!s.faction || !s.species || !seen.insert(s.faction).second || !std::isfinite(s.position.x) ||
            !std::isfinite(s.position.y) || !std::isfinite(s.position.z) || !std::isfinite(s.amount) || s.amount < 0 || s.amount > 1000000) return;
        stores.push_back(s);
    }
    auto tribes = all_tribes();
    if (g_faction_mapping_fix) bind_factions(world, stores, tribes);
    g_npc_food_received = sequence;
    if (!g_npc_food_fix) return;
    for (const auto& s : stores) for (void* t : tribes) {
        if (F<uint32_t>(t, 0x30) != local_faction(s.faction, tribes) ||
            (!g_faction_mapping_fix && net::dist(tribe_pos(t), s.position) > 3)) continue;
        // Founder stores already follow the more frequent complete citizen cycle.
        if (t == player_tribe()) break;
        bool mine = false;
        for (const auto& [id, member] : g_gm) if (member.c && member.owner == C.sess->my_id() &&
            F<void*>(member.c, kOwnerTribeOff) == t) { mine = true; break; }
        if (mine) break;
        auto key = tribe_info(t).key;
        if (key.instance == s.species || key.instance == creations::local_species({s.species, key.type, key.group}).instance)
            shared::mirror_village_food(t, s.amount);
        break;
    }
}

static bool g_relationships_fix = true;
COOP_FIX("tribe.relationships", "BUG-9033", g_relationships_fix,
         "tribe: reconcile ordered native relationship records from the host");
static double g_relationships_sent;
static uint32_t g_relationships_sequence;
static std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_relationships_received;
struct Relationship {
    uint32_t a, b, flags;
    std::vector<std::pair<uint32_t, float>> events;
};

template<class Fn> static bool relationship_walk(void* map, Fn fn)
{
    if (!map || !skg::readable(map, 0x18)) return false;
    std::vector<void*> stack;
    std::unordered_set<void*> seen;
    void* node = F<void*>(map, 0xC);
    while (node || !stack.empty()) {
        while (node) {
            if (!skg::readable(node, 0x40) || seen.size() >= 256 || !seen.insert(node).second) return false;
            stack.push_back(node);
            node = F<void*>(node, 4);
        }
        node = stack.back(); stack.pop_back();
        fn(node);
        node = F<void*>(node, 0);
    }
    return true;
}

static void* relationship_manager()
{
    uintptr_t get = H->sym("Simulator::cRelationshipManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

static void* relationship_record(void* manager, uint32_t a, uint32_t b)
{
    void* result = nullptr;
    relationship_walk((uint8_t*)manager + 0x24, [&](void* node) {
        if (F<uint32_t>(node, 0x10) == a && F<uint32_t>(node, 0x14) == b) result = (uint8_t*)node + 0x18;
    });
    return result;
}

static bool relationship_events(void* record, std::vector<std::pair<uint32_t, float>>& events)
{
    bool ok = relationship_walk((uint8_t*)record + 8, [&](void* node) {
        events.emplace_back(F<uint32_t>(node, 0x10), F<float>(node, 0x14));
    });
    if (!ok || events.size() > 64) return false;
    for (const auto& [key, value] : events) if (!key || !std::isfinite(value)) return false;
    return true;
}

static void host_relationships(double now)
{
    if (!g_relationships_fix || !g_order_roster.guest().faction || now - g_relationships_sent < 1) return;
    g_relationships_sent = now;
    void* manager = relationship_manager();
    if (!manager) return;
    std::unordered_set<uint32_t> factions;
    for (void* t : all_tribes()) factions.insert(F<uint32_t>(t, 0x30));
    std::vector<Relationship> records;
    if (!relationship_walk((uint8_t*)manager + 0x24, [&](void* node) {
        Relationship r{F<uint32_t>(node, 0x10), F<uint32_t>(node, 0x14), F<uint32_t>(node, 0x1C) & 3u, {}};
        if (r.a != r.b && factions.count(r.a) && factions.count(r.b) && relationship_events((uint8_t*)node + 0x18, r.events))
            records.push_back(std::move(r));
    })) return;
    uint32_t sequence = ++g_relationships_sequence;
    for (const auto& r : records) {
        net::Writer packet;
        packet.u32(modes::current_world()); packet.u32(sequence);
        packet.u32(r.a); packet.u32(r.b); packet.u32(r.flags); packet.u8((uint8_t)r.events.size());
        for (const auto& [key, value] : r.events) { packet.u32(key); packet.f32(value); }
        for (const auto& player : C.sess->players()) if (player.id)
            C.sess->send_unreliable_to(player.id, net::MSG_TRIBE_RELATIONSHIPS, packet);
    }
}

static void guest_relationships(net::Reader& packet)
{
    uint32_t world = packet.u32(), sequence = packet.u32();
    Relationship r{packet.u32(), packet.u32(), packet.u32(), {}};
    unsigned count = packet.u8();
    if (!g_relationships_fix || !packet.ok() || world != modes::current_world() || !world ||
        !r.a || !r.b || r.a == r.b || r.flags > 3 || count > 64 || packet.left() != count * 8) return;
    uint32_t last = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t key = packet.u32(); float value = packet.f32();
        if (key <= last || !std::isfinite(value)) return;
        r.events.emplace_back(key, value); last = key;
    }
    const auto tribes = all_tribes();
    r.a = local_faction(r.a, tribes);
    r.b = local_faction(r.b, tribes);
    if (!r.a || !r.b || r.a == r.b) return;
    auto pair = std::make_pair(r.a, r.b);
    auto previous = g_relationships_received.find(pair);
    if (previous != g_relationships_received.end() && (int32_t)(sequence - previous->second) <= 0) return;
    bool have_a = false, have_b = false;
    for (void* t : tribes) { have_a |= F<uint32_t>(t, 0x30) == r.a; have_b |= F<uint32_t>(t, 0x30) == r.b; }
    if (!have_a || !have_b) return;
    void* manager = relationship_manager();
    uintptr_t reset = H->sym("Simulator::cRelationshipManager::ResetRelationship");
    uintptr_t index = H->sym("SporeCoop::RelEventMap_Index");
    uintptr_t calculate = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
    if (!manager || !reset || !index || !calculate) return;
    void* record = relationship_record(manager, r.a, r.b);
    std::vector<std::pair<uint32_t, float>> mine;
    bool same = record && relationship_events(record, mine) && mine == r.events;
    if (!same) {
        thiscall<void>(reset, manager, r.a, r.b);
        record = relationship_record(manager, r.a, r.b);
        if (!record) return;
        for (const auto& [key, value] : r.events) {
            float* slot = thiscall<float*>(index, (uint8_t*)record + 8, &key);
            if (!slot) return;
            *slot = value;
        }
    }
    F<uint32_t>(record, 4) = (F<uint32_t>(record, 4) & ~3u) | r.flags;
    if (!same) thiscall<float>(calculate, manager, r.a, r.b, 0);
    g_relationships_received[pair] = sequence;
}

static bool g_conquest_refresh = true;
COOP_FIX("tribe.conquest_refresh", "BUG-9025", g_conquest_refresh,
         "tribe: register guest raid goals and transfer settled native campaign records");
static std::unordered_map<uint32_t, bool> g_village_defeats;
static uint16_t g_campaign_unlocks;
static uint32_t g_campaign_segments;
static bool g_campaign_observed;
static double g_campaign_refresh_at;
static double g_campaign_poll_at;
static uint64_t g_campaign_epoch;
static std::unordered_set<uint32_t> g_campaign_targets;

// Isolated six-rival qualification. The native planet stores fractional
// goal progress. The sidecar identifies the one reward deferred to the next
// totem milestone. Only a deliberately enrolled campaign uses this path.
static founders::SixRivals g_six;
static bool g_six_loaded, g_six_hook_ready;
static uintptr_t g_earned_result_return, a_GrantEarnedTools;
using QueueResult = void(SK_THISCALL*)(void*, unsigned, void*);
static QueueResult o_QueueResult;

static void* tribal_player()
{
    void* manager = game::noun_manager();
    return manager ? F<void*>(manager, 0x74) : nullptr;
}

static void load_six_trial()
{
    if (g_six_loaded || !g_order_roster.guest().faction) return;
    auto planet = world::current_game_key();
    if (planet.empty()) return;
    founders::read_six_rivals(planet, g_order_roster, g_six);
    g_six_loaded = true;
}

static void SK_THISCALL h_QueueResult(void* self, unsigned outcome, void* target)
{
    if (!g_six.extra || (uintptr_t)__builtin_return_address(0) != g_earned_result_return ||
        !is_host() || !active() || !target || (outcome != 1 && outcome != 2)) {
        o_QueueResult(self, outcome, target); return;
    }
    auto* player = tribal_player();
    if (!player || F<float>(player, 0x10F4) != 5.f) return;
    float& points = F<float>(player, 0x10F0);
    if (!game::object_alive(target) || !std::isfinite(points) || points < 3.f || points >= 5.f) return;
    const uint32_t faction = F<uint32_t>(target, 0x30);
    if (!faction || g_order_roster.contains_faction(faction)) return;
    // The remaining three objectives occupy two native milestones (3 to 5).
    int step = (int)std::lround((points - 3.f) * 1.5f);
    if (step > 2 || std::fabs(points - (3.f + step * (2.f / 3.f))) > .001f) return;
    if (!step) {
        auto next = g_six;
        next.deferred = faction; next.outcome = outcome;
        if (!founders::save_six_rivals(world::current_game_key(), g_order_roster, next, false)) {
            if (C.notice) C.notice("Could not record the extended Tribal objective. Check the campaign save.");
            return;
        }
        g_six = next;
    } else {
        if (!g_six.deferred || faction == g_six.deferred) return;
        const float old = points, tier = std::floor(points);
        // Let the native queue grant exactly one milestone, with its normal
        // color, tool unlocks and cinematic, then publish the full fraction.
        size_t rollback = game::swap_open(game::swap_undo_u32, &points, F<uint32_t>(player, 0x10F0), "Tribal trial goal points");
        points = tier;
        o_QueueResult(self, outcome, target);
        bool awarded = points == tier + 1.f;
        points = old;
        game::swap_close(rollback);
        if (!awarded) return;
        if (step == 1) for (void* tribe : all_tribes()) {
            if (F<uint32_t>(tribe, 0x30) == g_six.deferred) {
                skg::cdecl_call<void>(a_GrantEarnedTools, tribe); break;
            }
        }
    }
    points = 3.f + (step + 1) * (2.f / 3.f);
    g_campaign_refresh_at = H->time_seconds() + 2;
}

static void register_campaign_target(uint32_t faction)
{
    if (!g_conquest_refresh || !g_campaign_targets.insert(faction).second) return;
    struct Pending { uint32_t faction; uint64_t epoch; };
    H->run_outside_frame([](void* value) {
        Pending pending = *static_cast<Pending*>(value);
        delete static_cast<Pending*>(value);
        if (pending.epoch != g_campaign_epoch || !active() || !is_host()) return;
        uintptr_t get = H->sym("SporeCoop::TribeGoalUI_Get");
        uintptr_t select = H->sym("SporeCoop::TribeGoalUI_SelectTarget");
        void* ui = get ? skg::cdecl_call<void*>(get) : nullptr;
        if (ui && select) for (void* tribe : all_tribes()) {
            if (F<uint32_t>(tribe, 0x30) != pending.faction) continue;
            // Native raid input registers this goal before issuing action 15.
            // Resolve by faction here: the world may have changed since receipt.
            thiscall<void>(select, ui, tribe);
            return;
        }
        g_campaign_targets.erase(pending.faction);
    }, new Pending{faction, g_campaign_epoch});
}

static uint16_t tool_unlocks()
{
    uintptr_t get = H->sym("SporeCoop::TribalGame_Get");
    uintptr_t check = H->sym("SporeCoop::TribalGame_IsToolUnlocked");
    void* game = get ? skg::cdecl_call<void*>(get) : nullptr;
    uint16_t mask = 0;
    if (game && check) for (int kind = 1; kind <= 9; ++kind)
        if (thiscall<bool>(check, game, kind)) mask |= (uint16_t)(1u << kind);
    return mask;
}

static void host_campaign_tick(double now)
{
    if (!g_conquest_refresh || !g_order_roster.guest().faction || now < g_campaign_poll_at) return;
    g_campaign_poll_at = now + .5;
    bool changed = false;
    for (void* t : all_tribes()) {
        uint32_t faction = F<uint32_t>(t, 0x30);
        if (g_order_roster.contains_faction(faction)) continue;
        bool defeated = F<uint8_t>(t, 0x556) != 0;
        auto previous = g_village_defeats.find(faction);
        if (previous != g_village_defeats.end() && !previous->second && defeated) changed = true;
        g_village_defeats[faction] = defeated;
    }
    uint16_t unlocked = tool_unlocks();
    if (g_campaign_observed && unlocked != g_campaign_unlocks) changed = true;
    g_campaign_unlocks = unlocked;
    // Alliances can award a segment without unlocking a new tool. Observe the
    // native result itself so those victories also reach the guest checkpoint.
    void* home = player_tribe();
    void* totem = home ? F<void*>(home, 0x19D0) : nullptr;
    if (totem && skg::readable(totem, 0x124)) {
        uintptr_t begin = F<uintptr_t>(totem, 0x11C), end = F<uintptr_t>(totem, 0x120);
        if (end >= begin && (end - begin) % 4 == 0 && (end - begin) / 4 <= 64) {
            uint32_t segments = (uint32_t)((end - begin) / 4);
            if (g_campaign_observed && segments != g_campaign_segments) changed = true;
            g_campaign_segments = segments;
        }
    }
    g_campaign_observed = true;
    if (changed) g_campaign_refresh_at = now + 2;
    if (!g_campaign_refresh_at || now < g_campaign_refresh_at || world::cinematic_playing() ||
        world::load_queued() || world::save_busy()) return;
    // The native goal system owns rewards and totem history. A health packet
    // cannot reproduce those records. Reuse the confirmed-save transfer path;
    // the host keeps playing and an absent partner receives them on reconnect.
    for (const auto& rescue : g_founder_rescues) if (rescue.until) return;
    bool queued = false;
    for (const auto& player : C.sess->players()) if (player.id) {
        std::string error;
        if (!world::share_with(player.id, error, true)) {
            g_campaign_refresh_at = now + 2;
            return;
        }
        queued = true;
    }
    g_campaign_refresh_at = 0;
    if (queued && C.notice) C.notice("Saving Tribal conquest for your partner...");
}

static bool tool_cost(int kind, int& cost)
{
    uintptr_t get = H->sym("Simulator::GetTribeToolData");
    void* data = kind >= 1 && kind <= 9 && get ? skg::cdecl_call<void*>(get, kind) : nullptr;
    if (!data || IsBadReadPtr(data, 0x7C) || F<int>(data, 0xC) != kind) return false;
    cost = F<int>(data, 0x38);
    return cost >= 0 && cost <= 1000000;
}

std::string village_tool_line(int kind)
{
    static const char* names[] = {"", "Flaming Torches", "Stone Axes", "Throwing Spears", "Wooden Horns",
        "Maracas", "Didgeridoos", "Gathering Canes", "Fishing Spears", "Healing Rods"};
    int cost;
    if (!tool_cost(kind, cost)) return "Tool unavailable on this game build.";
    uint16_t mask = is_host() ? tool_unlocks() : g_tool_unlocks;
    return std::string(names[kind]) + ((mask & (1u << kind)) ? " (" + std::to_string(cost) + " food)." : " (ally or conquer to unlock).");
}

static bool tool_inventory(void* tribe, std::array<void*, 11>& tools)
{
    void** begin = F<void**>(tribe, 0x36C), **end = F<void**>(tribe, 0x370);
    if (begin == end) return true;
    if (!begin || end < begin || end - begin > 10 || IsBadReadPtr(begin, (end - begin) * 4)) return false;
    for (void** it = begin; it < end; ++it) {
        void* tool = *it;
        if (!tool || IsBadReadPtr(tool, 0x250) || !game::object_alive(tool) || game::noun_id(tool) != kToolNoun) return false;
        unsigned kind = F<uint32_t>(tool, 0x1FC);
        // The native planner creates a temporary dragged rack before placement.
        // Defer the whole inventory, including deletions, until it is committed.
        if (!kind || kind > 10 || tools[kind] || (F<uint32_t>(tool, 0x84) & 8) ||
            F<void*>(tool, 0x1F8) != tribe) return false;
        tools[kind] = tool;
    }
    return true;
}

static void host_tools(double now)
{
    if (now - g_tools_sent < 0.5) return;
    g_tools_sent = now;
    ++g_tools_sequence;
    uint16_t unlocked = tool_unlocks();
    for (void* tribe : all_tribes()) {
        std::array<void*, 11> tools{};
        if (!tool_inventory(tribe, tools)) continue;
        net::Writer packet;
        packet.u32(modes::current_world()); packet.u32(g_tools_sequence);
        packet.u32(F<uint32_t>(tribe, 0x30));
        net::Vec3 anchor = tribe_pos(tribe);
        packet.f32(anchor.x); packet.f32(anchor.y); packet.f32(anchor.z);
        packet.u16(unlocked);
        packet.u8((uint8_t)std::count_if(tools.begin(), tools.end(), [](void* p) { return p; }));
        for (unsigned kind = 1; kind <= 10; ++kind) if (void* tool = tools[kind]) {
            packet.u8((uint8_t)kind);
            const auto& p = F<net::Vec3>(tool, 0x38);
            const auto& q = F<net::Quat>(tool, 0x44);
            packet.f32(p.x); packet.f32(p.y); packet.f32(p.z);
            packet.f32(q.x); packet.f32(q.y); packet.f32(q.z); packet.f32(q.w);
            packet.f32(F<float>(tool, 0x158)); packet.f32(F<float>(tool, 0x14C));
        }
        for (const auto& player : C.sess->players()) if (player.id)
            C.sess->send_unreliable_to(player.id, net::MSG_TRIBE_TOOLS, packet);
    }
}

static void guest_tools(net::Reader& packet)
{
    if (!g_tools_fix) return;
    uint32_t world = packet.u32(), sequence = packet.u32(), faction = packet.u32();
    net::Vec3 anchor{packet.f32(), packet.f32(), packet.f32()};
    uint16_t unlocked = packet.u16();
    unsigned count = packet.u8();
    auto finite = [](const net::Vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    if (!packet.ok() || !world || world != modes::current_world() || !faction || !finite(anchor) ||
        (unlocked & ~0x3FEu) || count > 10 || packet.left() != count * 37) return;
    struct State { bool present = false; net::Vec3 position; net::Quat rotation; float hp, maximum; };
    std::array<State, 11> states{};
    for (unsigned i = 0; i < count; ++i) {
        unsigned kind = packet.u8();
        if (!kind || kind > 10 || states[kind].present) return;
        auto& s = states[kind]; s.present = true;
        s.position = {packet.f32(), packet.f32(), packet.f32()};
        s.rotation = {packet.f32(), packet.f32(), packet.f32(), packet.f32()};
        s.hp = packet.f32(); s.maximum = packet.f32();
        float norm = s.rotation.x*s.rotation.x + s.rotation.y*s.rotation.y + s.rotation.z*s.rotation.z + s.rotation.w*s.rotation.w;
        if (!finite(s.position) || net::dist(s.position, anchor) > 200 || !std::isfinite(norm) || norm < 0.9f || norm > 1.1f ||
            !std::isfinite(s.hp) || !std::isfinite(s.maximum) || s.hp < 0 || s.maximum <= 0 || s.hp > s.maximum || s.maximum > 1e7f) return;
    }
    auto previous = g_tools_received.find(faction);
    if (previous != g_tools_received.end() && (int32_t)(sequence - previous->second) <= 0) return;
    void* tribe = nullptr;
    const auto tribes = all_tribes();
    // The original shared-village mode does not broadcast separate village stores.
    uint32_t local = (g_guest_order_mode || g_faction_world) ? local_faction(faction, tribes) : faction;
    if (!local) return;
    net::Vec3 offset{};
    auto binding = g_local_factions.find(faction);
    if (g_faction_mapping_fix && binding != g_local_factions.end()) {
        if (net::dist(anchor, binding->second.host_site) >= 3) return;
        offset = binding->second.ground_offset;
    }
    const net::Vec3 site{anchor.x+offset.x, anchor.y+offset.y, anchor.z+offset.z};
    for (void* t : tribes) if (F<uint32_t>(t, 0x30) == local && net::dist(tribe_pos(t), site) < 3) { tribe = t; break; }
    if (!tribe || !F<void*>(tribe, 0x18CC)) return;
    std::array<void*, 11> tools{};
    if (!tool_inventory(tribe, tools)) return;
    static uintptr_t layout = H->sym("SporeCoop::Tribe_UpdateToolLayout");
    if (!layout) return;
    for (unsigned kind = 1; kind <= 10; ++kind) {
        const auto& s = states[kind];
        void* tool = tools[kind];
        if (!s.present) {
            if (tool) vcall<void>(tribe, 0xB8 / 4, tool);
            continue;
        }
        bool created = !tool;
        if (!tool) {
            tool = vcall<void*>(tribe, 0xB4 / 4, (int)kind);
            if (!tool) return;
            vcall<void>(tool, 0x64 / 4, tribe);
        }
        const net::Vec3 placement{s.position.x+offset.x, s.position.y+offset.y, s.position.z+offset.z};
        if (created || net::dist(F<net::Vec3>(tool, 0x38), placement) > 0.01f ||
            std::memcmp(&F<net::Quat>(tool, 0x44), &s.rotation, sizeof(s.rotation))) {
            vcall<void>((uint8_t*)tool + 0x34, 0x44 / 4, &placement, &s.rotation);
            thiscall<void>(layout, tribe, (int)kind);
        }
        F<float>(tool, 0x158) = s.hp; F<float>(tool, 0x14C) = s.maximum;
    }
    g_tools_received[faction] = sequence;
    g_tool_unlocks = unlocked;
}

static void guest_buildings(net::Reader& packet)
{
    uint32_t world = packet.u32(), sequence = packet.u32();
    unsigned count = packet.u8();
    if (!packet.ok() || !world || world != modes::current_world() || count > 24 || packet.left() != count * 32) return;
    struct State { uint32_t id, noun, faction; net::Vec3 position; float hp, maximum; };
    std::vector<State> states;
    for (unsigned i = 0; i < count; ++i) {
        State state{packet.u32(), packet.u32(), packet.u32(), {packet.f32(), packet.f32(), packet.f32()}, packet.f32(), packet.f32()};
        if (!state.id || (state.noun != kHutNoun && state.noun != kToolNoun) ||
            !std::isfinite(state.position.x) || !std::isfinite(state.position.y) || !std::isfinite(state.position.z) ||
            !std::isfinite(state.hp) || !std::isfinite(state.maximum) || state.hp < 0 || state.maximum <= 0 ||
            state.hp > state.maximum || state.maximum > 1e7f) return;
        states.push_back(state);
    }
    const auto tribes = all_tribes();
    for (const auto& state : states) {
        auto previous = g_building_received.find(state.id);
        if (previous != g_building_received.end() && (int32_t)(sequence - previous->second) <= 0) continue;
        uint32_t faction = local_faction(state.faction, tribes);
        if (!faction) continue;
        auto position = state.position;
        auto binding = g_local_factions.find(state.faction);
        if (g_faction_mapping_fix && binding != g_local_factions.end()) {
            const auto offset = binding->second.ground_offset;
            position = {position.x+offset.x, position.y+offset.y, position.z+offset.z};
        }
        void* object = game::find_object(state.noun, position, 3.f);
        if (!object || !game::object_alive(object) || F<uint32_t>(object, 0x30) != faction) continue;
        // Ownership and destruction are native world changes, reconciled by checkpoints.
        // Health snapshots never run a guest-side TakeDamage or its game-over events.
        F<float>(object, 0x158) = state.hp;
        F<float>(object, 0x14C) = state.maximum;
        g_building_received[state.id] = sequence;
        ++g_building_applied;
    }
}

uint8_t owner_of(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm.find(c);
        return it == g_hm.end() ? 0xFF : it->second.owner;
    }
    auto it = g_gid_of.find(c);
    if (it == g_gid_of.end())
        return 0xFF;
    return g_gm[it->second].owner;
}

// ---------------- hooks ----------------
static bool registered_guest_village(void* t, void* home);
static bool g_guest_ai_fix = true;
COOP_FIX("tribe.guest_ai", "BUG-9020", g_guest_ai_fix,
         "tribe: recognize the registered guest village as player-owned for native citizen AI");
using TribePlayerOwned_t = bool(SK_THISCALL*)(void*);
static TribePlayerOwned_t o_TribePlayerOwned;
static bool SK_THISCALL h_TribePlayerOwned(void* spatial)
{
    if (g_guest_ai_fix && active() && is_host() && g_order_roster.guest().faction &&
        registered_guest_village((uint8_t*)spatial - 0x120, player_tribe())) return true;
    return o_TribePlayerOwned(spatial);
}
static bool g_shared_capacity = true;
COOP_FIX("tribe.shared_capacity", "BUG-9039", g_shared_capacity,
         "tribe: both founding villages use the shared campaign population limit");
using MaxPopulation_t = int(SK_THISCALL*)(void*);
static MaxPopulation_t o_MaxPopulation;
static int SK_THISCALL h_MaxPopulation(void* tribe)
{
    if (g_shared_capacity && active() && village_orders() && !local_play()) {
        void* home = player_tribe();
        if (home && tribe && tribe != home && game::object_alive(home) && game::object_alive(tribe)) {
            bool founder = is_host() && registered_guest_village(tribe, home);
            if (is_guest()) for (const auto& [id, member] : g_gm) {
                if (member.owner == C.sess->my_id() && member.c && game::object_alive(member.c) &&
                    F<void*>(member.c, kOwnerTribeOff) == tribe) { founder = true; break; }
            }
            // Query the original native getter on the campaign tribe. This
            // follows earned level changes and native/modded tuning without
            // changing the guest's species, faction or village object.
            if (founder) return o_MaxPopulation(home);
        }
    }
    return o_MaxPopulation(tribe);
}
using BTTick_t = int(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static BTTick_t o_BTTick;
static uint32_t g_bt_skipped, g_bt_calls; // diagnostics (coop.tribe): mirrors' AI ticks skipped / all ticks
// behaviour tree tick (shared by every creature kind): skipped for citizen mirrors on guests
static int SK_CDECL h_BTTick(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t self, uint32_t a5, uint32_t a6,
                             uint32_t a7, uint32_t a8)
{
    g_bt_calls++;
    if (active() && is_host() && founder_downed((void*)self)) return 0;
    // C26E70 caches IsPlayerOwned in citizen AI flags at creation. Re-evaluate
    // only our registered guest citizens, including those loaded from a save.
    if (active() && is_host() && g_order_roster.guest().faction && is_citizen((void*)self) &&
        registered_guest_village(F<void*>((void*)self, kOwnerTribeOff), player_tribe())) {
        if (void* ai = F<void*>((void*)self, 0xB4C)) {
            auto& flags = F<uint32_t>(ai, 0x5FC);
            flags = (flags & ~1u) | (g_guest_ai_fix ? 1u : 0u);
        }
    }
    if (is_guest() && is_citizen((void*)self) && game::mode_id() == modes::kTribe && !local_play()) {
        g_bt_skipped++;
        return 0;
    }
    return o_BTTick(a0, a1, a2, a3, self, a5, a6, a7, a8);
}

// a player may select only their own members of the player tribe
using SetSelected_t = int(SK_THISCALL*)(void*, int);
static SetSelected_t o_SetSelected;
static int SK_THISCALL h_SetSelected(void* spatial, int sel)
{
    if ((sel & 0xFF) && active()) {
        void* c = (uint8_t*)spatial - 0xC0;
        uint8_t owner = owner_of(c);
        if (owner != 0xFF && owner != C.sess->my_id())
            sel = 0;
    }
    return o_SetSelected(spatial, sel);
}

struct Target {
    uint8_t kind = 0; // 0 none, 1 citizen id, 2 object by noun + position, 3 tree by position
    uint16_t id = 0;
    uint32_t noun = 0;
    net::Vec3 pos;
};

static Target encode_target(void* t)
{
    Target r;
    if (!t)
        return r;
    auto it = g_gid_of.find(t);
    if (it != g_gid_of.end()) {
        r.kind = 1;
        r.id = it->second;
        return r;
    }
    if (is_tree(t)) { // a planet tree: no cGameData, the host finds its own copy by position
        r.kind = 3;
        r.noun = kTreeNoun;
        r.pos = {F<float>(t, 0x10), F<float>(t, 0x14), F<float>(t, 0x18)};
        return r;
    }
    r.kind = 2;
    r.noun = game::noun_id(t);
    game::object_position(t, r.pos);
    return r;
}

static void write_target(net::Writer& w, const Target& t)
{
    w.u8(t.kind);
    w.u16(t.id);
    w.u32(t.noun);
    w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z);
}

static Target read_target(net::Reader& r)
{
    Target t;
    t.kind = r.u8();
    t.id = r.u16();
    t.noun = r.u32();
    t.pos.x = r.f32(); t.pos.y = r.f32(); t.pos.z = r.f32();
    return t;
}

enum : uint8_t { CMD_ACTION = 0, CMD_WALK = 1, CMD_GIVE = 2, CMD_FORAGE = 3, CMD_REPAIR = 4, CMD_REBUILD = 5, CMD_BABY = 6, CMD_BUILD = 7, CMD_INSTRUMENT = 8 };
static bool queue_instrument(uint8_t from, uint16_t id, unsigned tool, uint32_t token);
static bool request_instrument(unsigned tool);
static bool release_completed_performers(uint8_t from);
struct PendingPerformance {
    std::vector<uint16_t> ids;
    Target target;
    uint32_t world = 0;
    uint8_t from = 0;
    double due = 0;
};
static PendingPerformance g_pending_performance;
using AbilityKey_t = bool(SK_CDECL*)(int);
static AbilityKey_t o_AbilityKey;
static bool SK_CDECL h_AbilityKey(int key)
{
    if (active() && is_guest() && !local_play() && village_orders() &&
        key >= '1' && key <= '3' && !performance_line().empty()) {
        request_instrument((unsigned)(key - '1' + 4));
        return true;
    }
    return o_AbilityKey(key);
}
static bool buy_guest_baby(uint8_t from, uint16_t witness);
static bool buy_guest_tool(uint8_t from, uint16_t witness, int kind, net::Vec3 pos);
static bool queue_partner_rebuild(uint8_t from, uint16_t member);

static bool send_cmd(uint8_t kind, const std::vector<uint16_t>& ids, int action, const Target& tgt, const net::Vec3& pos)
{
    if (kind != CMD_REPAIR && (ids.empty() || g_guest_orders_held))
        return false;
    net::Writer w;
    w.u8(kind);
    w.u8((uint8_t)std::min<size_t>(ids.size(), 64));
    for (size_t i = 0; i < ids.size() && i < 64; i++)
        w.u16(ids[i]);
    w.u32((uint32_t)action);
    write_target(w, tgt);
    w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
    if (kind == CMD_INSTRUMENT) w.u32(modes::current_world());
    if (!C.sess->send_reliable_to(0, net::MSG_TRIBE_CMD, w)) return false;
    g_cmds_sent++;
    char b[128];
    snprintf(b, sizeof b, "kind=%u n=%u action=%d target=%u/%u/%X (%.1f %.1f %.1f)", kind, (unsigned)ids.size(), action,
             tgt.kind, tgt.id, tgt.noun, tgt.pos.x, tgt.pos.y, tgt.pos.z);
    g_last_cmd = b;
    return true;
}

// cTribeInputStrategy::DoActionForObjects(vector<cSpatialObject*>*, action, target, prop) - every
// order on an object goes through it; on guests the order goes to the host instead
using DoActionForObjects_t = int(SK_THISCALL*)(void*, void*, int, void*, void*);
static DoActionForObjects_t o_DoActionForObjects;
static int SK_THISCALL h_DoActionForObjects(void* self, void* vec, int action, void* target, void* prop)
{
    if (local_play()) {
        g_away_orders++;
        return o_DoActionForObjects(self, vec, action, target, prop);
    }
    if (!is_guest() || game::mode_id() != modes::kTribe) {
        if (is_host() && game::mode_id() == modes::kTribe) {
            char lb[96];
            snprintf(lb, sizeof lb, "action=%d n=%d obj=%p noun=%X", action,
                     vec ? (int)(F<void**>(vec, 4) - F<void**>(vec, 0)) : -1, target, target ? game::noun_id(target) : 0);
            g_last_local = lb;
            g_local_orders++;
        }
        return o_DoActionForObjects(self, vec, action, target, prop);
    }
    std::vector<uint16_t> ids;
    void** b = vec ? F<void**>(vec, 0) : nullptr;
    void** e = vec ? F<void**>(vec, 4) : nullptr;
    for (void** it = b; it && it < e; ++it) {
        auto f = g_gid_of.find((uint8_t*)*it - 0xC0);
        if (f != g_gid_of.end())
            ids.push_back(f->second);
    }
    send_cmd(CMD_ACTION, ids, action, encode_target(target), {});
    char ob[48];
    snprintf(ob, sizeof ob, " obj=%p", target);
    g_last_cmd += ob;
    return ids.empty() ? 1 : 0;
}

// cTribeInputStrategy::WalkSelectedTo(const Vector3*)
using WalkSelected_t = int(SK_THISCALL*)(void*, const float*);
static WalkSelected_t o_WalkSelected;
static int SK_THISCALL h_WalkSelected(void* self, const float* pos)
{
    if (local_play()) {
        g_away_orders++;
        return o_WalkSelected(self, pos);
    }
    if (!is_guest() || game::mode_id() != modes::kTribe || !pos) {
        if (is_host() && game::mode_id() == modes::kTribe && pos) {
            char lb[96];
            snprintf(lb, sizeof lb, "walk to (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);
            g_last_local = lb;
            g_local_orders++;
        }
        return o_WalkSelected(self, pos);
    }
    std::vector<uint16_t> ids;
    for (auto& [id, m] : g_gm)
        if (m.c && F<uint8_t>(m.c, kSelectedOff) && m.owner == C.sess->my_id())
            ids.push_back(id);
    send_cmd(CMD_WALK, ids, 4, {}, {pos[0], pos[1], pos[2]});
    char b[64];
    snprintf(b, sizeof b, " to (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);
    g_last_cmd += b;
    return 0;
}

static void coop_init();

// BUG-9002: identify which native path formed a village while following the host.
static bool g_founder_npc_budget = true;
COOP_FIX("tribe.founder_npc_budget", "BUG-9047", g_founder_npc_budget,
         "tribe: keep cooperative founders out of the native NPC objective budget");
using NounData_t = void*(SK_THISCALL*)(void*, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);
static NounData_t o_NounData;
static uintptr_t g_form_count_return;
static uintptr_t g_spawn_count_return;
static bool g_founder_spawn_gate = true;
COOP_FIX("tribe.founder_spawn_gate", "BUG-9059", g_founder_spawn_gate,
         "tribe: exclude the partner village from native rival appearance conditions");
static unsigned g_form_calls, g_form_budget_filtered, g_form_budget_total;
static bool g_allied_consequence = true;
COOP_FIX("tribe.allied_consequence", "BUG-9066", g_allied_consequence,
         "tribe: count a guest-earned NPC alliance in the native graduation score");
using FactionFlag = bool(SK_THISCALL*)(void*, const uint32_t*);
static FactionFlag o_FactionFlag;
static uintptr_t g_consequence_flag_return;
static bool SK_THISCALL h_FactionFlag(void* profile, const uint32_t* faction)
{
    if (o_FactionFlag(profile, faction)) return true;
    if (!g_allied_consequence || (uintptr_t)__builtin_return_address(0) != g_consequence_flag_return ||
        !active() || !is_host() || !faction || !g_order_roster.guest().faction ||
        g_order_roster.contains_faction(*faction)) return false;
    // This caller is iterating live tribes and passed the current tribe's
    // political ID. Require the earned diplomatic result on both founders.
    // The game still weighs opinions, history, and its own role thresholds.
    void* manager = relationship_manager();
    static uintptr_t calculate = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
    if (!manager || !calculate) return false;
    const float allied = F<float>(manager, 0x1C);
    if (!std::isfinite(allied) || allied <= 0) return false;
    auto allied_with = [&](uint32_t founder) {
        return thiscall<float>(calculate, manager, *faction, founder, 0) >= allied;
    };
    return allied_with(g_order_roster.home().faction) && allied_with(g_order_roster.guest().faction);
}
static void* SK_THISCALL h_NounData(void* self, uintptr_t create, uintptr_t clear,
                                  uintptr_t add, uintptr_t filter, uint32_t noun)
{
    void* data = o_NounData(self, create, clear, add, filter, noun);
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    bool factory_count = caller == g_form_count_return && g_founder_npc_budget;
    bool spawn_count = caller == g_spawn_count_return && g_founder_spawn_gate;
    if ((!factory_count && !spawn_count) ||
        !active() || !is_host() || noun != kTribeNoun || !g_order_roster.guest().faction ||
        !skg::readable(data, 16)) return data;
    uintptr_t begin = F<uintptr_t>(data, 4), end = F<uintptr_t>(data, 8);
    if (!begin || end < begin || (end-begin)%4 || end-begin < 8 || end-begin > 256 ||
        !skg::readable((void*)begin, end-begin)) return data;
    for (uintptr_t at = begin; at < end; at += 4) {
        void* tribe = *(void**)at;
        if (!tribe || !game::object_alive(tribe) || F<uint32_t>(tribe, 0x30) != g_order_roster.guest().faction) continue;
        // Both verified callers consume only (end-begin)/4 immediately after
        // returning, for appearance conditions or NPC quota/species selection.
        // Supply a private count view; never remove a village from the native
        // noun manager or alter the shared vector used by other game systems.
        static thread_local uintptr_t count_view[4];
        memcpy(count_view, data, sizeof count_view);
        count_view[2] -= 4;
        ++g_form_budget_filtered;
        g_form_budget_total = (unsigned)((end-begin)/4);
        return count_view;
    }
    return data;
}

using SpawnVillage = void*(SK_CDECL*)(const skg::Vector3*, int, int, int, bool, void*);
static SpawnVillage o_SpawnVillage;
using FormVillage = int(SK_THISCALL*)(void*, uint32_t);
static FormVillage o_FormVillage;
static int SK_THISCALL h_FormVillage(void* controller, uint32_t arg)
{
    if (is_host()) ++g_form_calls;
    if (g_guest_order_mode && is_guest() && game::mode_id() == modes::kTribe && !local_play()) {
        ++g_form_skipped;
        return 0;
    }
    return o_FormVillage(controller, arg);
}
static void* SK_CDECL h_SpawnVillage(const skg::Vector3* pos, int archetype, int count, int food, bool flag, void* species)
{
    void* tribe = o_SpawnVillage(pos, archetype, count, food, flag, species);
    if (tribe && is_guest()) {
        uintptr_t caller = (uintptr_t)__builtin_return_address(0) - H->rebase(0);
        H->log(M, SK_INFO, "tribe: guest village created caller=%08X faction=%08X members=%d food=%d", (unsigned)caller,
               F<uint32_t>(tribe, 0x30), count, food);
    }
    return tribe;
}

// A fresh replacement needs the factory's planner/chief initialization, but
// its descendants must inherit the registered founder's political identity.
// Consume this synchronous request before SpawnTribe creates any descendants.
using SpawnTribe_t = void(SK_THISCALL*)(void*, const skg::Vector3*, int, int, bool);
static SpawnTribe_t o_SpawnTribe;
static uint32_t g_refound_faction;
static bool g_refound_ready;
static void SK_THISCALL h_SpawnTribe(void* tribe, const skg::Vector3* pos, int count, int food, bool flag)
{
    if (g_refound_faction) {
        uint32_t faction = g_refound_faction;
        g_refound_faction = 0;
        vcall<void>(tribe, 0x48 / 4, faction); // native cGameData::SetPoliticalID
    }
    o_SpawnTribe(tribe, pos, count, food, flag);
}

using TribeMessage_t = bool(SK_THISCALL*)(void*, uint32_t, void*);
static TribeMessage_t o_TribeMessage;
static bool SK_THISCALL h_TribeMessage(void* listener, uint32_t message, void* data);

using ToolLayout_t = void(SK_THISCALL*)(void*, int);
static ToolLayout_t o_ToolLayout;
static bool g_layout_profile_fix = true;
COOP_FIX("tribe.layout_profile", "BUG-9009", g_layout_profile_fix,
         "tribe: defer a structure layout after its tribe species has been cleared");
static uint32_t g_layout_deferred;
static void SK_THISCALL h_ToolLayout(void* tribe, int kind)
{
    // The native cleanup clears the profile while a hut can still retain its
    // tribe and request a layout. CE6AE0 reads the profile's dimensions directly.
    if (g_layout_profile_fix && !F<void*>(tribe, 0x18CC)) {
        if (++g_layout_deferred <= 8)
            H->log(M, SK_INFO, "tribe: deferred structure layout %d for cleared species (mode %08X)", kind, game::mode_id());
        return;
    }
    o_ToolLayout(tribe, kind);
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetPlayerTribe = H->sym("Simulator::cGameNounManager::GetPlayerTribe");
    a_DoAction = H->sym("Simulator::cCreatureCitizen::DoAction");
    a_SetValueVector3 = H->sym("App::Property::SetValueVector3");
    a_BTTick = H->sym("Simulator::BehaviorTreeTick");
    a_DoActionForObjects = H->sym("Simulator::cTribeInputStrategy::DoActionForObjects");
    a_WalkSelected = H->sym("Simulator::cTribeInputStrategy::WalkSelectedTo");
    a_SetSelected = H->sym("Simulator::cCreatureCitizen::SetSelected");
    a_CreateCitizen = H->sym("Simulator::cTribe::CreateCitizen");
    a_AddMember = H->sym("Simulator::cTribe::AddMember");
    a_CitizenVt = H->sym("Simulator::cCreatureCitizen::vftable");
    a_TreeVt = H->sym("Simulator::cTreeData::vftable");
    a_SpawnNpcTribe = H->sym("Simulator::SpawnNpcTribe"); // BUG-0381 (optional)
    a_SetChieftain = H->sym("SporeCoop::Tribe_SetChieftain");
    a_SetLeader = H->sym("SporeCoop::Community_SetLeader");
    g_ok = a_GetPlayerTribe && a_DoAction && a_SetValueVector3 && a_BTTick && a_DoActionForObjects && a_WalkSelected &&
           a_SetSelected && a_CreateCitizen && a_AddMember && a_CitizenVt && a_TreeVt;
    if (!g_ok) {
        H->log(M, SK_WARN, "tribe: symbols missing, tribe co-op disabled");
        return false;
    }
    H->hook_create(M, (void*)a_BTTick, (void*)h_BTTick, (void**)&o_BTTick);
    if (uintptr_t key = H->sym("SporeCoop::AbilityBar_OnKey"))
        H->hook_create(M, (void*)key, (void*)h_AbilityKey, (void**)&o_AbilityKey);
    if (uintptr_t owned = H->sym("SporeCoop::Tribe_IsPlayerOwned"))
        H->hook_create(M, (void*)owned, (void*)h_TribePlayerOwned, (void**)&o_TribePlayerOwned);
    if (uintptr_t capacity = H->sym("SporeCoop::Tribe_MaxPopulation"))
        H->hook_create(M, (void*)capacity, (void*)h_MaxPopulation, (void**)&o_MaxPopulation);
    H->hook_create(M, (void*)a_SetSelected, (void*)h_SetSelected, (void**)&o_SetSelected);
    H->hook_create(M, (void*)a_DoActionForObjects, (void*)h_DoActionForObjects, (void**)&o_DoActionForObjects);
    H->hook_create(M, (void*)a_WalkSelected, (void*)h_WalkSelected, (void**)&o_WalkSelected);
    if (a_SpawnNpcTribe)
        H->hook_create(M, (void*)a_SpawnNpcTribe, (void*)h_SpawnVillage, (void**)&o_SpawnVillage);
    if (uintptr_t spawn = H->sym("SporeCoop::Tribe_Spawn"))
        g_refound_ready = H->hook_create(M, (void*)spawn, (void*)h_SpawnTribe, (void**)&o_SpawnTribe) == 0;
    if (uintptr_t form = H->sym("SporeCoop::NpcTribeController_FormVillage"))
        H->hook_create(M, (void*)form, (void*)h_FormVillage, (void**)&o_FormVillage);
    g_form_count_return = H->sym("SporeCoop::FormVillage_TribeCountReturn");
    g_spawn_count_return = H->sym("SporeCoop::NpcTribeUpdate_TribeCountReturn");
    g_earned_result_return = H->sym("SporeCoop::Tribe_CompletedGoalResultReturn");
    a_GrantEarnedTools = H->sym("SporeCoop::Tribe_GrantEarnedTools");
    if (uintptr_t result = H->sym("SporeCoop::Tribe_QueueEarnedResult"); result && g_earned_result_return && a_GrantEarnedTools)
        g_six_hook_ready = H->hook_create(M, (void*)result, (void*)h_QueueResult, (void**)&o_QueueResult) == 0;
    g_consequence_flag_return = H->sym("SporeCoop::TribeConsequence_FactionFlagReturn");
    if (uintptr_t flag = H->sym("SporeCoop::Planet_FactionFlag"); flag && g_consequence_flag_return)
        H->hook_create(M, (void*)flag, (void*)h_FactionFlag, (void**)&o_FactionFlag);
    if (uintptr_t get = H->sym("Simulator::cGameNounManager::GetData"); get && (g_form_count_return || g_spawn_count_return))
        H->hook_create(M, (void*)get, (void*)h_NounData, (void**)&o_NounData);
    if (uintptr_t handle = H->sym("SporeCoop::TribalGame_HandleMessage"))
        H->hook_create(M, (void*)handle, (void*)h_TribeMessage, (void**)&o_TribeMessage);
    if (uintptr_t layout = H->sym("SporeCoop::Tribe_UpdateToolLayout"))
        H->hook_create(M, (void*)layout, (void*)h_ToolLayout, (void**)&o_ToolLayout);
    coop_init();
    return true;
}

// ---------------- host: snapshots + orders ----------------
// BUG-0340 (second part, the owner's request): at a player's arrival in Tribe and after a rejoin each
// player's chief gets its own spot: a chief closer to another than the sum of their sizes + 1 m goes to
// its ring spot round the village. Only then (not on later chief changes: no jump in a fight).
// `coop.fix tribe.place_chiefs off`: chiefs stay where the game put them.
static bool g_place_fix = true;
COOP_FIX("tribe.place_chiefs", "BUG-0340", g_place_fix, "tribe: at arrival and after a rejoin every player's chief too close to another gets its own spot round the village");
static double g_place_due = 0, g_place_until = 0;
static uint32_t g_placed;
static std::unordered_set<void*> g_placed_now; // chiefs placed in this arrival's window
static void arrival_place_due(double now)
{
    g_placed_now.clear();
    g_place_due = now + 3.0;   // the split and the chiefs settle first
    g_place_until = now + 60.0; // a player still loading: the guests' games come in later
}
constexpr size_t kEntry = 2 + 1 + 1 + 1 + 12 + 12 + 16 + 4; // one member in a tribe state chunk
constexpr uint32_t kStateTrailer = 0x54435354u; // BUG-0365: a chunk's trailer, magic + the host's ms
constexpr size_t kTrailerSize = 8;

// BUG-0381: an NPC tribe the host's game formed during play (cNpcTribeController, 0xCC6440: "a new
// tribe has appeared") is formed by the host's game only: the guests' games never formed it (2 of 5
// tribes on every guest for minutes in a 4-player campaign) and the host's members of it had no
// tribe to be mirrored into on a guest (sync-check "t: 27 missing on the guest"). The host appends
// each tribe's species and archetype to its state chunks (after the clock trailer); a guest makes a
// host NPC tribe it has no tribe for (Simulator::SpawnNpcTribe at the host's tribe position, no
// members: the mirrors are spawned into it as for any tribe). Off: the guest has only the tribes its
// own game formed.
static bool g_npc_tribes_fix = true;
COOP_FIX("tribe.npc_tribes", "BUG-0381", g_npc_tribes_fix,
         "tribe: a guest makes the host's NPC tribes its own game did not form (their members were missing)");
constexpr uint32_t kTribeInfoTag = 0x49425254u; // "TRBI": the tribe table's species / archetype after the trailer
constexpr size_t kTribeInfo = 12 + 1;           // species key (instance, type, group) + archetype (0xFF: none)
constexpr uintptr_t kTribeArchetypeOff = 0x550; // cTribe: the archetype SpawnNpcTribe stores (< 0x20)
static uint32_t g_npc_tribes_made, g_npc_tribes_failed;
static TribeInfo tribe_info(void* t)
{
    TribeInfo i;
    uint32_t a = F<uint32_t>(t, kTribeArchetypeOff);
    i.arch = a < 0x20 ? (uint8_t)a : 0xFF;
    for (void* c : members(t)) { // a citizen is a cCreatureBase: its species key at +0xB28 (game::species_key)
        game::Key k = game::species_key(c);
        if (k.instance) {
            i.key = k;
            break;
        }
    }
    return i;
}

static uint8_t village_commander()
{
    if (C.sess->players().size() == 2)
        for (const auto& p : C.sess->players())
            if (p.id && founder_model(1, creations::player_key(p.id, creations::kCreature).instance))
                return p.id;
    return 0xFE; // no matching guest in this session
}

static bool registered_guest_village(void* t, void* home)
{
    return t && home && t != home && F<uint32_t>(home, 0x30) == g_order_roster.home().faction &&
           F<uint32_t>(t, 0x30) == g_order_roster.guest().faction && founder_model(1, tribe_info(t).key.instance);
}

void begin_outfit_vote()
{
    if (!active() || !is_host() || !village_orders()) return;
    uint8_t guest = village_commander();
    if (!guest || guest >= 0xFE) return;
    g_outfit = {modes::current_world(), ++g_outfit_serial,
                creations::player_key(guest, creations::kCreature).instance, 0, guest};
    if (!g_outfit.token) g_outfit.token = ++g_outfit_serial;
    net::Writer w;
    w.u8(0); w.u32(g_outfit.world); w.u32(g_outfit.token); w.u32(g_outfit.previous); w.u32(0);
    if (!C.sess->send_reliable_to(guest, net::MSG_TRIBE_OUTFIT, w)) g_outfit = {};
}

void finish_own_outfit(bool saved)
{
    if (is_guest() && g_outfit.world == modes::current_world() && g_outfit.token) {
        g_outfit.ready = true; g_outfit.saved = saved;
    }
}

void cancel_outfit_vote() { g_outfit = {}; }

bool outfits_pending()
{
    if (!is_host() || !g_outfit.token || g_outfit.world != modes::current_world()) return false;
    for (const auto& p : C.sess->players()) if (p.id == g_outfit.owner) return true;
    return false;
}

static void receive_outfit(uint8_t from, net::Reader& r)
{
    unsigned kind = r.u8();
    uint32_t world = r.u32(), token = r.u32(), previous = r.u32(), model = r.u32();
    if (!r.ok() || r.left() || !world || world != modes::current_world() || !token || !previous) return;
    if (kind == 0 && from == 0 && is_guest() && active() && !model) {
        g_outfit = {world, token, previous, 0, C.sess->my_id()};
    } else if (kind == 1 && is_host() && from == g_outfit.owner && g_outfit.token == token &&
               g_outfit.world == world && g_outfit.previous == previous && model) {
        g_outfit.model = model; g_outfit.ready = true;
    } else if (kind == 2 && from == 0 && is_guest() && g_outfit.token == token &&
               g_outfit.world == world && g_outfit.previous == previous && g_outfit.model == model && model) {
        creations::confirm_tribal_outfit();
        g_outfit = {};
    }
}

static bool g_founder_survival = true;
COOP_FIX("tribe.founder_survival", "BUG-9007", g_founder_survival,
         "tribe: a surviving founding village prevents the host population rewind");
static uint32_t g_founder_rewinds_held;

static bool SK_THISCALL h_TribeMessage(void* listener, uint32_t message, void* data)
{
    // The event's native body also changes the camera and loss state before it
    // starts TRG_PRE_PopDestroyed. Keep the whole event out of a surviving duo.
    if (message == 0x05C80207 && g_founder_survival && active() && is_host() && C.sess->players().size() == 2) {
        founders::Roster roster;
        void* home = player_tribe();
        if (home && founders::read(world::current_game_key(), roster) && F<uint32_t>(home, 0x30) == roster.home().faction) {
            for (void* t : all_tribes()) {
                if (t == home || F<uint32_t>(t, 0x30) != roster.guest().faction || F<uint8_t>(t, 0x556) ||
                    (tribe_info(t).key.instance != roster.guest().species &&
                     !g_outfits.contains(1, tribe_info(t).key.instance))) continue;
                for (void* c : members(t)) {
                    if (game::health(c) <= 0 || F<uint8_t>(c, 0xB5E)) continue;
                    g_founder_rewinds_held++;
                    if (C.notice) C.notice("Your partner's village survives. The shared world continues.");
                    return false;
                }
            }
        }
    }
    return o_TribeMessage(listener, message, data);
}

static void host_tick(double now)
{
    if (now - g_last_send < 0.12)
        return;
    g_last_send = now;
    void* pt = player_tribe();
    if (pt && g_restore_village_orders) {
        founders::Roster saved;
        std::string error;
        if (founders::read(world::current_game_key(), saved) && F<uint32_t>(pt, 0x30) == saved.home().faction &&
            founders::read_outfits(world::current_game_key(), g_outfits, error))
            g_order_roster = saved;
        g_restore_village_orders = false;
    }
    auto tribes = all_tribes();
    std::vector<uint8_t> players;
    for (auto& p : C.sess->players())
        players.push_back(p.id);
    std::sort(players.begin(), players.end());
    if (players != g_split_for && pt) {
        arrival_place_due(now); // BUG-0340: a player arrived or rejoined
        // the players changed: split the player tribe again, in member order
        g_split_for = players;
        g_player_members_seen = 0;
        for (void* c : members(pt)) {
            auto it = g_hm.find(c);
            if (it != g_hm.end())
                it->second.owner = players[g_player_members_seen++ % players.size()];
        }
    }
    std::unordered_set<void*> alive;
    struct Entry {
        uint16_t id;
        uint8_t tribe, owner, age;
        void* c;
    };
    std::vector<Entry> entries;
    const bool village_orders = g_order_roster.guest().faction != 0;
    const uint8_t guest = village_orders ? village_commander() : 0xFE;
    for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
        bool player = tribes[ti] == pt;
        const uint8_t village_owner = player ? 0 : (village_orders && registered_guest_village(tribes[ti], pt) ? guest : 0xFE);
        for (void* c : members(tribes[ti])) {
            alive.insert(c);
            auto it = g_hm.find(c);
            if (it == g_hm.end()) {
                // the player tribe is split between the players in turns; others belong to nobody
                uint8_t owner = player && !players.empty() ? players[g_player_members_seen++ % players.size()] : 0xFE;
                // BUG-0154: never a live id, never 0
                it = g_hm.emplace(c, HMember{net::alloc_id(g_next_id, g_hm_by_id), owner}).first;
                g_hm_by_id[it->second.id] = c;
            }
            if (village_orders || !player)
                it->second.owner = village_owner;
            entries.push_back({it->second.id, (uint8_t)ti, it->second.owner, F<uint8_t>(c, kAgeOff), c});
        }
    }
    for (auto it = g_hm.begin(); it != g_hm.end();) {
        if (!alive.count(it->first)) {
            g_hm_by_id.erase(it->second.id);
            it = g_hm.erase(it);
        } else
            ++it;
    }
    // chunks that fit one packet; every chunk carries the tribe table. A chunk larger than a packet
    // is dropped by the connection: with 24 members per chunk (fixed until 2026-10-23) the first
    // chunk (the player tribe first) never arrived once the planet had 24 citizens, and the guest
    // removed those mirrors at the end of every cycle (play_tribe.py: the guest's tribe emptied)
    g_cycle++;
    size_t nt = std::min<size_t>(tribes.size(), 32);
    struct Food { uint32_t species = 0; double amount = 0; };
    std::vector<Food> food(nt);
    size_t food_bytes = 0;
    if (village_orders) {
        g_home_food = g_guest_food = -1;
        for (size_t i = 0; i < nt; ++i) {
            void* t = tribes[i];
            if (t != pt && !registered_guest_village(t, pt)) continue;
            auto species = tribe_info(t).key.instance;
            if (species && shared::tribe_food(t, food[i].amount)) {
                food[i].species = species;
                food_bytes += 8;
                (t == pt ? g_home_food : g_guest_food) = (float)food[i].amount;
            }
        }
    }
    std::vector<TribeInfo> infos; // BUG-0381
    if (g_npc_tribes_fix)
        for (size_t ti = 0; ti < nt; ti++)
            infos.push_back(tribes[ti] == pt ? TribeInfo{} : tribe_info(tribes[ti]));
    size_t head = 2 + 1 + 1 + 1 + nt * 13 + food_bytes + 1 + kTrailerSize + (infos.empty() ? 0 : 4 + 1 + nt * kTribeInfo);
    if (village_orders) head += kRescueBytes;
    if (g_native_chiefs_fix) head += 5 + nt * 2;
    size_t per = std::max<size_t>(1, (net::kMaxPacket - 64 - head) / kEntry);
    size_t chunks = std::max<size_t>(1, (entries.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        w.u8((uint8_t)std::min<size_t>(tribes.size(), 32));
        for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
            net::Vec3 p = tribe_pos(tribes[ti]);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            bool home = tribes[ti] == pt;
            w.u8((home ? 1 : 0) | (food[ti].species ? 2 : 0) | (home && village_orders ? 4 : 0));
            if (food[ti].species) {
                w.u32(food[ti].species);
                w.f32((float)food[ti].amount);
            }
        }
        size_t from = k * per, to = std::min(entries.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            const Entry& e = entries[i];
            net::Vec3 p = game::position(e.c), v = game::velocity(e.c);
            net::Quat q = game::orientation(e.c);
            w.u16(e.id); w.u8(e.tribe); w.u8(e.owner); w.u8(e.age);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(v.x); w.f32(v.y); w.f32(v.z);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.f32(game::health(e.c));
        }
        w.u32(kStateTrailer); // BUG-0365: the host's clock (guests before 0.7.3.4 stop reading before it)
        w.u32((uint32_t)(now * 1000.0));
        if (!infos.empty()) { // BUG-0381: after the trailer (guests before it stop reading at the trailer)
            w.u32(kTribeInfoTag);
            w.u8((uint8_t)infos.size());
            for (auto& i : infos) {
                w.u32(i.key.instance); w.u32(i.key.type); w.u32(i.key.group);
                w.u8(i.arch);
            }
        }
        if (g_native_chiefs_fix) {
            w.u32(kNativeChiefsTag);
            w.u8((uint8_t)nt);
            for (size_t ti = 0; ti < nt; ++ti) {
                void* chief = F<void*>(tribes[ti], kNativeLeaderOff);
                auto found = g_hm.find(chief);
                w.u16(found != g_hm.end() && F<void*>(chief, kOwnerTribeOff) == tribes[ti] ? found->second.id : 0);
            }
        }
        if (village_orders) {
            w.u32(kRescueTag);
            for (const auto& rescue : g_founder_rescues) {
                w.u16(rescue.until > 0 ? rescue.member : 0);
                w.u16((uint16_t)std::clamp(std::ceil((rescue.until - now) * 10), 0.0, 100.0));
            }
        }
        for (auto& p : C.sess->players())
            if (p.id != 0) {
                for (auto& i : infos) // the host's own creation among them: its records first (creations.h)
                    if (i.key.instance)
                        creations::host_species_out(p.id, {i.key.instance, i.key.type, i.key.group});
                C.sess->send_unreliable_to(p.id, net::MSG_TRIBE_STATE, w);
            }
    }
}

static void* resolve_target(const Target& t)
{
    if (t.kind == 1) {
        auto it = g_hm_by_id.find(t.id);
        return it == g_hm_by_id.end() ? nullptr : it->second;
    }
    if (t.kind == 2 && t.noun)
        return game::find_object(t.noun, t.pos, 8.f);
    if (t.kind == 3)
        return find_tree(t.pos, 1.f);
    return nullptr;
}

static bool gift_cost(int& cost)
{
    uintptr_t tuning = H->sym("SporeCoop::TribeTuning_ptr");
    uintptr_t fallback = H->sym("SporeCoop::TribeGiftCost_default");
    uintptr_t get = H->sym("App::Property::GetFloat");
    if (!tuning || !fallback || !get) return false;
    float value = *(const float*)fallback;
    if (void* list = *(void**)tuning) skg::cdecl_call<bool>(get, list, 0xFD444753u, &value);
    if (!std::isfinite(value) || value < 1 || value > 1000000) return false;
    cost = (int)value;
    return true;
}

// host: members of `from` change hands (a gift); owners travel with the snapshots
static void give_on_host(uint8_t from, uint8_t to, const std::vector<uint16_t>& ids)
{
    if (g_order_roster.guest().faction) return;
    void* pt = player_tribe();
    int n = 0;
    for (uint16_t id : ids) {
        auto it = g_hm_by_id.find(id);
        auto hm = it == g_hm_by_id.end() ? g_hm.end() : g_hm.find(it->second);
        if (hm == g_hm.end() || hm->second.owner != from || F<void*>(hm->first, kOwnerTribeOff) != pt)
            continue;
        hm->second.owner = to;
        n++;
    }
    g_gifts += n;
    if (n && C.notice)
        C.notice(C.sess->name_of(from) + " gave " + std::to_string(n) + (n == 1 ? " tribe member to " : " tribe members to ") +
                 C.sess->name_of(to));
}

static void host_command(uint8_t from, net::Reader& r, bool resumed = false)
{
    uint8_t kind = r.u8(), n = r.u8();
    std::vector<uint16_t> ids;
    for (int i = 0; i < n && r.ok(); i++)
        ids.push_back(r.u16());
    int action = (int)r.u32();
    Target tgt = read_target(r);
    net::Vec3 pos;
    pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32();
    if (!r.ok())
        return;
    const bool village_orders = g_order_roster.guest().faction != 0;
    if (kind != CMD_INSTRUMENT && from == g_pending_performance.from)
        g_pending_performance = {}; // a newer player order supersedes the queued performance
    if (kind == CMD_INSTRUMENT) {
        uint32_t world = r.u32();
        // Target kind4 carries the opaque native session token, not an object address.
        if (!r.ok() || r.left() || world != modes::current_world() || !world || !village_orders ||
            !g_performance_fix || ids.size() != 1 || tgt.kind != 4 ||
            !queue_instrument(from, ids.front(), (unsigned)action, tgt.noun)) ++g_cmds_rejected;
        else ++g_cmds_done;
        return;
    }
    if (kind == CMD_ACTION && action == 26 && tgt.kind == 1 && !resumed)
        g_perform_dispatch = {g_perform_dispatch.sequence + 1, tgt.id, n};
    void* forage_target = nullptr;
    if (kind == CMD_BUILD) {
        if (!village_orders || ids.size() != 1 || from >= 0xFE || from != village_commander() ||
            !buy_guest_tool(from, ids[0], action, pos)) g_cmds_rejected++;
        return;
    }
    if (kind == CMD_BABY) {
        if (!village_orders || ids.size() != 1 || from >= 0xFE || from != village_commander() ||
            !buy_guest_baby(from, ids[0])) g_cmds_rejected++;
        return;
    }
    if (kind == CMD_REBUILD) {
        if (!village_orders || ids.size() != 1 || from >= 0xFE || from != village_commander() ||
            !queue_partner_rebuild(from, ids[0])) g_cmds_rejected++;
        return;
    }
    if (kind == CMD_REPAIR) {
        if (!village_orders || g_repair_served || from >= 0xFE || from != village_commander()) return;
        g_repair_served = true;
        std::string err;
        bool queued = world::share_with(from, err, true);
        if (C.notice) C.notice(queued ? "An extra guest village was detected. Reloading the host's saved planet on the guest." :
                              "Could not repair the guest's village roster: " + err);
        return;
    }
    if (kind == CMD_FORAGE) {
        if (!village_orders || from >= 0xFE || from != village_commander()) { g_cmds_rejected++; return; }
        void* fruit = nullptr;
        float nearest = 300.f;
        void* home = player_tribe();
        for (void* t : all_tribes()) {
            if (!registered_guest_village(t, home)) continue;
            const net::Vec3 site = tribe_pos(t);
            for_each_tree([&](void* tree, const net::Vec3& p) {
                float d = net::dist(site, p);
                if (d < nearest && F<uint32_t>(tree, 0x48)) { nearest = d; fruit = tree; }
            });
            break;
        }
        if (!fruit) {
            g_cmds_rejected++;
            if (C.notice) C.notice("No fruit tree within 300 metres of the guest village.");
            return;
        }
        kind = CMD_ACTION;
        action = 2;
        tgt = encode_target(fruit);
        forage_target = fruit;
    }
    const bool gather = kind == CMD_ACTION && action == 2 && tgt.kind == 3 && tgt.noun == kTreeNoun;
    const bool attack = kind == CMD_ACTION && action == 7 && tgt.kind == 1;
    const bool raid = kind == CMD_ACTION && action == 15 && tgt.kind == 2 && (tgt.noun == kHutNoun || tgt.noun == kToolNoun);
    const bool equip = kind == CMD_ACTION && action == 3 && tgt.kind == 2 && tgt.noun == kToolNoun;
    const bool heal = g_heal_orders && kind == CMD_ACTION && action == 5 && tgt.kind == 1;
    const bool eat = g_eat_orders && kind == CMD_ACTION && action == 0 && tgt.kind == 2 && tgt.noun == 0x629BAFE;
    const bool gift = g_gift_orders && kind == CMD_ACTION && action == 9 && tgt.kind == 2 && tgt.noun == kTribeNoun;
    const bool perform = g_performance_fix && kind == CMD_ACTION && action == 26 && tgt.kind == 1;
    if (village_orders && ((kind != CMD_WALK && !gather && !attack && !raid && !equip && !heal && !eat && !gift && !perform) || from != village_commander() || from >= 0xFE || !std::isfinite(pos.x) ||
                           !std::isfinite(pos.y) || !std::isfinite(pos.z))) {
        g_cmds_rejected++;
        return;
    }
    if (kind == CMD_GIVE) {
        if (action >= 0 && action <= 0xFF && net::is_player(*C.sess, (uint8_t)action))
            give_on_host(from, (uint8_t)action, ids);
        else
            g_cmds_rejected++;
        return;
    }
    // CitizenAction values of the game's own orders are 0..28 (docs/re/tribe.md 2)
    if (kind == CMD_ACTION && (action < 0 || action > 28)) {
        g_cmds_rejected++;
        return;
    }
    if (kind != CMD_ACTION && kind != CMD_WALK)
        return;
    void* pt = player_tribe();
    if (village_orders && (!std::isfinite(tgt.pos.x) || !std::isfinite(tgt.pos.y) || !std::isfinite(tgt.pos.z))) {
        g_cmds_rejected++;
        return;
    }
    void* target = forage_target ? forage_target : (kind == CMD_ACTION ? resolve_target(tgt) : nullptr);
    if (village_orders && equip && (!target || !game::object_alive(target) || game::noun_id(target) != kToolNoun ||
        F<float>(target, 0x158) <= 0 || F<uint32_t>(target, 0x30) != g_order_roster.guest().faction ||
        F<uint32_t>(target, 0x1FC) < 1 || F<uint32_t>(target, 0x1FC) > 9)) {
        g_cmds_rejected++;
        return;
    }
    if (village_orders && raid && (!target || !game::object_alive(target) || game::noun_id(target) != tgt.noun ||
        F<float>(target, 0x158) <= 0 || g_order_roster.contains_faction(F<uint32_t>(target, 0x30)))) {
        g_cmds_rejected++;
        return;
    }
    if (village_orders && (attack || perform) && (!target || !is_citizen(target) || !game::object_alive(target) ||
        game::health(target) <= 0 || F<uint8_t>(target, 0xB5E) ||
        g_order_roster.contains_faction(F<uint32_t>(target, 0x30)))) {
        g_cmds_rejected++;
        return;
    }
    if (village_orders && gather && (!target || !F<uint32_t>(target, 0x48))) {
        g_cmds_rejected++;
        return;
    }
    if (village_orders && heal && (!target || !is_citizen(target) || !game::object_alive(target) ||
        game::health(target) <= 0 || F<uint8_t>(target, 0xB5E) || founder_downed(target) ||
        game::health(target) >= game::max_health(target) ||
        !g_order_roster.contains_faction(F<uint32_t>(target, 0x30)))) {
        g_cmds_rejected++;
        return;
    }
    if (village_orders && eat && (!target || !game::object_alive(target) || game::noun_id(target) != 0x629BAFE)) {
        g_cmds_rejected++;
        return;
    }
    int gift_price = 0;
    if (village_orders && gift) {
        auto tribes = all_tribes();
        if (!target || std::find(tribes.begin(), tribes.end(), target) == tribes.end() ||
            g_order_roster.contains_faction(F<uint32_t>(target, 0x30)) || F<uint8_t>(target, 0x556) ||
            !F<void*>(target, 0x260) || !F<void*>(target, 0x368) ||
            F<float>(F<void*>(target, 0x368), 0x158) <= 0 || !gift_cost(gift_price)) {
            g_cmds_rejected++;
            return;
        }
    }
    char b[160];
    snprintf(b, sizeof b, "from=%u kind=%u n=%u action=%d target=%u/%u/%X (%.1f %.1f %.1f) -> %p", from, kind,
             (unsigned)ids.size(), action, tgt.kind, tgt.id, tgt.noun, tgt.pos.x, tgt.pos.y, tgt.pos.z, target);
    g_last_cmd = b;
    if (kind == CMD_ACTION && tgt.kind != 0 && !target) {
        g_cmds_rejected++;
        g_last_cmd += " rejected: no such target here";
        return; // the object is gone on the host
    }
    if (village_orders && perform && !resumed && release_completed_performers(from)) {
        g_perform_dispatch.deferred = true;
        // Cancellation replans on the following simulation ticks. Reissuing
        // action26 in this frame would reinstate the action before it exits.
        g_pending_performance = {ids, tgt, modes::current_world(), from, H->time_seconds() + .15};
        return;
    }
    for (uint16_t id : ids) {
        auto it = g_hm_by_id.find(id);
        if (it == g_hm_by_id.end()) {
            g_cmds_rejected++;
            continue;
        }
        void* c = it->second;
        if (founder_downed(c)) { g_cmds_rejected++; continue; }
        auto hm = g_hm.find(c);
        if (hm == g_hm.end() || hm->second.owner != from || !game::object_alive(c)) {
            g_cmds_rejected++;
            continue;
        }
        void* actual_tribe = F<void*>(c, kOwnerTribeOff);
        if (village_orders ? !registered_guest_village(actual_tribe, pt) : actual_tribe != pt) {
            g_cmds_rejected++;
            continue;
        }
        if (village_orders && equip && (F<void*>(target, 0x1F8) != actual_tribe ||
            F<uint32_t>(c, kAgeOff) != 1 || F<void*>(actual_tribe, kNativeLeaderOff) == c)) {
            g_cmds_rejected++;
            continue;
        }
        if (village_orders && heal && (game::health(c) <= 0 || F<uint8_t>(c, 0xB5E) ||
            F<uint32_t>(c, kAgeOff) != 1 || F<int>(c, 0x102C) != 9 || c == target)) {
            g_cmds_rejected++;
            continue;
        }
        if (village_orders && eat && (game::health(c) <= 0 || F<uint8_t>(c, 0xB5E) ||
            F<uint32_t>(c, kAgeOff) != 1 || F<void*>(actual_tribe, 0x260) != target ||
            F<void*>(target, 0x10C) != actual_tribe || vcall<int>(actual_tribe, 0xA4 / 4) <= 0)) {
            g_cmds_rejected++;
            continue;
        }
        if (village_orders && gift && (game::health(c) <= 0 || F<uint8_t>(c, 0xB5E) ||
            F<uint32_t>(c, kAgeOff) != 1 || vcall<int>(actual_tribe, 0xA4 / 4) < gift_price)) {
            g_cmds_rejected++;
            continue;
        }
        if (village_orders && perform && (game::health(c) <= 0 || F<uint8_t>(c, 0xB5E) ||
            F<uint32_t>(c, kAgeOff) != 1 || (F<int>(c, 0x102C) != 11 &&
            (F<int>(c, 0x102C) < 4 || F<int>(c, 0x102C) > 6)))) {
            ++g_cmds_rejected;
            continue;
        }
        if (kind == CMD_WALK) {
            void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
            float p[3] = {pos.x, pos.y, pos.z};
            if (rec)
                thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)p);
        } else {
            if (village_orders && (raid || gift || perform)) register_campaign_target(F<uint32_t>(target, 0x30));
            void* record = thiscall<void*>(a_DoAction, c, action, target, (void*)nullptr);
            if (perform) {
                ++g_perform_dispatch.dispatched;
                if (record) ++g_perform_dispatch.records;
            }
        }
        g_cmds_done++;
    }
}

// ---------------- guest: mirrors ----------------
// The game-over caption ("Alas! Your population has fallen below the minimum...") comes when no
// member of the player tribe has health left. A member added while it is up has no species fields
// (+0xB20, +0xB54) and the OK (bounce back) faulted at 0xC0DAE3 on it, then the game froze
// (BUG-0172). No member is added to the player tribe then. `coop.tribe fix gameover 0`: the old way.
static bool g_fix_gameover = true;
COOP_FIX("tribe.gameover", "BUG-0172", g_fix_gameover, "tribe: no member is added during the game-over caption");
static uint32_t g_spawns_blocked;
// BUG-0288: a guest's player tribe holds mirrors only. When every mirror had played its death (the
// host's tribe knocked out) the guard above refused every new mirror for good, also after the host
// bounced back and the dead mirrors were removed (an empty tribe has no member with health): the
// guests missed the host's whole new tribe (4-player campaign, "t: 16 missing on the guest"). The
// game-over caption comes on the host only (the guest's game shows none, its input lock stays 0):
// on a guest the guard holds only while its own game has a caption or cinematic up (the input lock).
// `coop.fix tribe.mirror_out off`: the guard as before.
static bool g_fix_mirror_out = true;
COOP_FIX("tribe.mirror_out", "BUG-0288", g_fix_mirror_out,
         "tribe: a guest mirrors the host's members after a game over (its own tribe is not out)");
static uint32_t g_mirror_out_passed;
static bool player_tribe_out(void* t)
{
    if (!g_fix_gameover || !t || t != player_tribe())
        return false;
    if (g_fix_mirror_out && is_guest() && coop_ui::input_lock_now() <= 0) {
        bool none = true;
        for (void* c : members(t))
            if (game::health(c) > 0)
                none = false;
        if (none)
            g_mirror_out_passed++;
        return false;
    }
    for (void* c : members(t))
        if (game::health(c) > 0)
            return false;
    return true;
}

static void* spawn_member(void* t, uint8_t age)
{
    if (player_tribe_out(t)) {
        g_spawns_blocked++;
        return nullptr;
    }
    // SpawnMember (vt 0x98) returns null at the population cap: then create + add directly
    void* c = vcall<void*>(t, 0x98 / 4, (int)age);
    if (!c) {
        c = thiscall<void*>(a_CreateCitizen, t, (int)age);
        if (c)
            thiscall<void>(a_AddMember, t, c, 1);
    }
    return c;
}

static bool g_mirror_member_lifetime = true;
COOP_FIX("tribe.mirror_member_lifetime", "BUG-9051", g_mirror_member_lifetime,
         "tribe: validate cached citizens against native village membership before reuse");
using MemberOwners = std::unordered_map<void*, void*>;
static MemberOwners native_member_owners()
{
    MemberOwners live;
    for (void* t : all_tribes())
        for (void* c : members(t))
            if (is_citizen(c) && F<void*>(c, kOwnerTribeOff) == t) live.emplace(c, t);
    return live;
}
static void remove_member(void* c)
{
    if (g_mirror_member_lifetime) {
        auto live = native_member_owners();
        auto member = live.find(c);
        if (member != live.end()) vcall<void>(member->second, 0x9C / 4, c, 1);
        return; // a stale cached address is not an object we may destroy
    }
    void* t = F<void*>(c, kOwnerTribeOff);
    if (t)
        vcall<void>(t, 0x9C / 4, c, 1); // RemoveMember(c, destroy)
}

// BUG-0381 (tribe.npc_tribes): a guest makes each host NPC tribe that has no tribe of this game
// within the mapping's 40 m (guest_state). `p`: the tribe table after the trailer (tag, count,
// count x kTribeInfo). One try per host tribe every 5 s (an unknown species is asked for first).
static std::map<std::pair<int, int>, double> g_npc_tribe_tried; // host tribe position (2 m grid) -> last try
static std::map<std::pair<int, int>, void*> g_npc_tribe_made;   // ... -> the tribe made for it here
static void make_npc_tribes(const uint8_t* p, size_t len, const std::vector<std::pair<net::Vec3, bool>>& htribes,
                            const std::vector<void*>& locals)
{
    uint32_t tag;
    memcpy(&tag, p, 4);
    if (!g_npc_tribes_fix || !a_SpawnNpcTribe || tag != kTribeInfoTag || p[4] != htribes.size() || len < 5)
        return;
    double now = H->time_seconds();
    for (size_t i = 0; i < htribes.size() && i < g_local_tribe_of.size(); i++) {
        if (htribes[i].second || g_local_tribe_of[i])
            continue;
        const uint8_t* e = p + 5 + i * kTribeInfo;
        game::Key k;
        memcpy(&k.instance, e, 4);
        memcpy(&k.type, e + 4, 4);
        memcpy(&k.group, e + 8, 4);
        uint8_t arch = e[12];
        if (!k.instance)
            continue; // no member yet: nothing to mirror
        const net::Vec3& hp = htribes[i].first;
        auto cell = std::make_pair((int)std::floor(hp.x / 2), (int)std::floor(hp.z / 2));
        auto made = g_npc_tribe_made.find(cell);
        if (made != g_npc_tribe_made.end()) { // made before (the game may have put it more than 40 m off)
            if (std::find(locals.begin(), locals.end(), made->second) != locals.end()) {
                g_local_tribe_of[i] = made->second;
                continue;
            }
            g_npc_tribe_made.erase(made); // gone (a raid, a load): may be made again
        }
        if (g_npc_tribes_made >= 64)
            continue; // a bound against a loop of tribes made and lost
        auto tried = g_npc_tribe_tried.find(cell);
        if (tried != g_npc_tribe_tried.end() && now - tried->second < 5.0)
            continue;
        g_npc_tribe_tried[cell] = now;
        creations::Key lk = creations::local_species({k.instance, k.type, k.group});
        void* species = game::species_by_key({lk.instance, lk.type, lk.group});
        if (!species) { // a species of the host's world this game lacks: the host sends it
            creations::want_species({k.instance, k.type, k.group});
            g_npc_tribes_failed++;
            continue;
        }
        skg::Vector3 pos{hp.x, hp.y, hp.z};
        // cdecl cTribe* SpawnNpcTribe(const Vector3& pos, int archetype, int members, int food, bool, cSpeciesProfile*)
        void* t = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, arch == 0xFF ? 0 : (int)arch, 0, 0, 0, species);
        if (!t) {
            g_npc_tribes_failed++;
            continue;
        }
        g_npc_tribes_made++;
        g_local_tribe_of[i] = t;
        g_npc_tribe_made[cell] = t;
        net::Vec3 lp = tribe_pos(t);
        H->log(M, SK_INFO,
               "tribe: the host's NPC tribe %u at (%.0f %.0f %.0f) is not in this game: made it (species %08X, archetype %d) at "
               "%.1f m",
               (unsigned)i, hp.x, hp.y, hp.z, k.instance, arch == 0xFF ? -1 : (int)arch, net::dist(lp, hp));
    }
    if (g_npc_tribe_tried.size() > 256)
        g_npc_tribe_tried.clear();
    if (g_npc_tribe_made.size() > 64)
        g_npc_tribe_made.clear();
}

static std::vector<uint16_t> guest_village_ids(bool selected_only = false);

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), nt = r.u8();
    if (!r.ok() || nt > 32 || !net::cycle_chunk_ok(chunk, chunks))
        return;
    std::vector<std::pair<net::Vec3, bool>> htribes;
    struct Food { uint8_t index; uint32_t species; float amount; };
    std::vector<Food> food;
    bool separate_food = false;
    for (int i = 0; i < nt && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        uint8_t flags = r.u8();
        if (flags & ~7 || ((flags & 4) && !(flags & 1))) return;
        htribes.push_back({p, (flags & 1) != 0});
        separate_food |= (flags & 4) != 0;
        if (flags & 2) {
            Food f{(uint8_t)i, r.u32(), r.f32()};
            if (!f.species || !std::isfinite(f.amount) || f.amount < 0 || f.amount > 1000000 || food.size() >= 2) return;
            food.push_back(f);
        }
    }
    if (!r.ok() || (!separate_food && !food.empty()))
        return;
    // host tribe index -> local tribe (the player tribe directly, others by position)
    auto locals = all_tribes();
    void* pt = player_tribe();
    g_local_tribe_of.assign(htribes.size(), nullptr);
    for (size_t i = 0; i < htribes.size(); i++) {
        if (htribes[i].second) {
            g_local_tribe_of[i] = pt;
            continue;
        }
        float best = 40.f;
        for (void* t : locals) {
            float d = net::dist(tribe_pos(t), htribes[i].first);
            if (t != pt && d < best) {
                best = d;
                g_local_tribe_of[i] = t;
            }
        }
    }
    if (cycle != g_rx_cycle) {
        g_rx_cycle = cycle;
        g_cycle_ids.clear();
        g_cycle_chunks.clear();
    }
    double now = H->time_seconds();
    uint8_t n = r.u8();
    // BUG-0365: the host's clock after the entries (none from a host before 0.7.3.4)
    bool timed = false;
    uint32_t host_ms = 0;
    // BUG-0381: the tribe table's species / archetype after the trailer (a host with tribe.npc_tribes)
    size_t extra = r.ok() && r.left() >= n * kEntry + kTrailerSize ? r.left() - n * kEntry - kTrailerSize : 0;
    g_remote_rescues = {};
    if (extra >= kRescueBytes) {
        net::Reader tail(r.cur() + r.left() - kRescueBytes, kRescueBytes);
        if (tail.u32() == kRescueTag) {
            extra -= kRescueBytes;
            for (auto& rescue : g_remote_rescues) {
                auto id = tail.u16(), tenths = tail.u16();
                if (id && tenths <= 100) rescue = {id, now + tenths / 10.0};
            }
        }
    }
    std::vector<uint16_t> native_chiefs;
    const size_t chiefs_bytes = 5 + htribes.size() * 2;
    if (extra >= chiefs_bytes) {
        net::Reader tail(r.cur() + n * kEntry + kTrailerSize + extra - chiefs_bytes, chiefs_bytes);
        if (tail.u32() == kNativeChiefsTag) {
            if (tail.u8() != htribes.size()) return;
            for (size_t i = 0; i < htribes.size(); ++i) native_chiefs.push_back(tail.u16());
            extra -= chiefs_bytes;
        }
    }
    bool info = extra >= 5 && extra == 5 + (size_t)r.cur()[n * kEntry + kTrailerSize + 4] * kTribeInfo;
    if (r.ok() && r.left() >= n * kEntry + kTrailerSize && (!extra || info)) {
        const uint8_t* t = r.cur() + n * kEntry;
        uint32_t magic;
        memcpy(&magic, t, 4);
        memcpy(&host_ms, t + 4, 4);
        timed = magic == kStateTrailer;
    }
    if (info && timed)
        make_npc_tribes(r.cur() + n * kEntry + kTrailerSize, extra, htribes, locals);
    g_mirror_timed += timed;
    auto live_members = g_mirror_member_lifetime ? native_member_owners() : MemberOwners{};
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        GMember m;
        m.tribe = r.u8(); m.owner = r.u8(); m.age = r.u8();
        m.pos.x = r.f32(); m.pos.y = r.f32(); m.pos.z = r.f32();
        m.vel.x = r.f32(); m.vel.y = r.f32(); m.vel.z = r.f32();
        m.rot.x = r.f32(); m.rot.y = r.f32(); m.rot.z = r.f32(); m.rot.w = r.f32();
        m.hp = r.f32();
        if (!r.ok() || m.age > 1)
            return;
        g_cycle_ids.insert(id);
        auto& g = g_gm[id];
        g.id = id;
        void* c = g.c;
        g.tribe = m.tribe; g.owner = m.owner; g.age = m.age;
        g.pos = m.pos; g.vel = m.vel; g.rot = m.rot; g.hp = m.hp; g.seen = now;
        if (g_test_lift != 0.f) { // tests (BUG-0329): the host's member as if its ground were higher
            float r = std::sqrt(g.pos.x * g.pos.x + g.pos.y * g.pos.y + g.pos.z * g.pos.z);
            if (r > 1.f) {
                float k = (r + g_test_lift) / r;
                g.pos = {g.pos.x * k, g.pos.y * k, g.pos.z * k};
            }
        }
        if (timed) {
            net::PlayerState ps{};
            ps.time_ms = host_ms;
            ps.flags = net::PF_IN_WORLD;
            ps.pos = g.pos;
            ps.rot = g.rot;
            ps.vel = g.vel;
            g.buf.delay = kMirrorDelay;
            g.buf.push(ps, now);
        }
        if (c && ((g_mirror_member_lifetime && !live_members.count(c)) || !game::object_alive(c))) {
            g_gid_of.erase(c);
            g.c = c = nullptr;
        }
        if (c && g_growth_fix && m.age == 1 && F<uint32_t>(c, kAgeOff) == 0) {
            // Modern citizen vt+78 (C248C0) grows the model, adds its spatial
            // object to the tribe's selectable vector and sets the adult AI bit.
            // It is a plain-thiscall method, not Init or a constructor.
            vcall<void>(c, 0x78 / 4);
        }
        if (c)
            continue;
        void* t = m.tribe < g_local_tribe_of.size() ? g_local_tribe_of[m.tribe] : nullptr;
        if (!t)
            continue;
        // bind to the nearest unbound local member of that tribe, else spawn one
        float best = 6.f;
        for (void* lc : members(t)) {
            if (g_gid_of.count(lc))
                continue;
            if (g_growth_fix && F<uint32_t>(lc, kAgeOff) != m.age)
                continue;
            float d = net::dist(game::position(lc), m.pos);
            if (d < best) {
                best = d;
                c = lc;
            }
        }
        if (!c) {
            c = spawn_member(t, m.age);
            if (!c)
                continue;
            // BUG-0444: the host's position (g.pos: with the tests' lift), onto this game's ground where that lies
            // higher (BUG-0329)
            game::teleport(c, g_mirror_ground_fix ? drive::out_of_ground(g.pos, true) : g.pos, m.rot);
            g_spawned++;
        }
        g.c = c;
        g_gid_of[c] = id;
    }
    if (!r.ok()) // a cut chunk does not count: the cycle stays incomplete, nothing is removed
        return;
    g_cycle_chunks.insert(chunk);
    if (chunk + 1 < chunks)
        return;
    if (g_cycle_chunks.size() < chunks) { // a chunk of this cycle was lost or comes late: keep everything
        g_incomplete++;
        return;
    }
    // Apply both stores and audit local villages from one complete cycle. Ignore late cycles.
    const bool new_complete_cycle = g_food_cycle < 0 || (int16_t)(cycle - g_food_cycle) > 0;
    if (new_complete_cycle) {
        g_food_cycle = cycle;
        g_guest_order_mode = separate_food;
        bool extra = separate_food && !local_play() && htribes.size() < 32 &&
                     std::none_of(g_local_tribe_of.begin(), g_local_tribe_of.end(), [](void* t) { return !t; }) &&
                     std::any_of(locals.begin(), locals.end(), [](void* t) {
                         return std::find(g_local_tribe_of.begin(), g_local_tribe_of.end(), t) == g_local_tribe_of.end();
                     });
        if (!extra) {
            g_extra_since = -1;
            g_guest_orders_held = false;
        } else if (g_extra_since < 0) {
            g_extra_since = now;
        } else if (now - g_extra_since >= 6 && !g_guest_orders_held && !guest_village_ids().empty()) {
            g_guest_orders_held = true;
            if (!g_repair_requested) {
                g_repair_requested = true;
                send_cmd(CMD_REPAIR, {}, 0, {}, {});
            }
            if (C.feed) C.feed("Village orders paused until the guest and host have the same villages.");
        }
        g_home_food = g_guest_food = -1;
        for (const auto& f : food) {
            void* t = g_local_tribe_of[f.index];
            if (!t) continue;
            auto local = tribe_info(t).key;
            if (local.instance == f.species || local.instance == creations::local_species({f.species, local.type, local.group}).instance) {
                shared::mirror_village_food(t, f.amount);
                (htribes[f.index].second ? g_home_food : g_guest_food) = f.amount;
            }
        }
    }
    // The guest's tribes are mirrors. Remove members no longer present on the host.
    for (auto it = g_gm.begin(); it != g_gm.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.c && (g_mirror_member_lifetime || game::object_alive(it->second.c))) {
                remove_member(it->second.c);
                g_removed++;
            }
            g_gid_of.erase(it->second.c);
            it = g_gm.erase(it);
        } else
            ++it;
    }
    for (void* t : g_local_tribe_of)
        if (t)
            for (void* lc : members(t))
                if (!g_gid_of.count(lc)) {
                    remove_member(lc);
                    g_removed++;
                }
    if (new_complete_cycle && timed && g_native_chiefs_fix && a_SetChieftain && a_SetLeader &&
        native_chiefs.size() == g_local_tribe_of.size()) {
        for (size_t i = 0; i < native_chiefs.size(); ++i) {
            void* t = g_local_tribe_of[i];
            if (!t || std::find(locals.begin(), locals.end(), t) == locals.end())
                continue; // a newly created tribe is checked against the next live list
            void* chief = nullptr;
            if (native_chiefs[i]) {
                auto found = g_gm.find(native_chiefs[i]);
                if (found == g_gm.end() || !g_cycle_ids.count(found->first) || found->second.tribe != i)
                    continue;
                chief = found->second.c;
                auto live = members(t);
                if (!chief || std::find(live.begin(), live.end(), chief) == live.end() ||
                    F<void*>(chief, kOwnerTribeOff) != t)
                    continue;
            }
            if (F<void*>(t, kNativeLeaderOff) != chief) {
                if (chief) thiscall<void>(a_SetChieftain, t, chief);
                else thiscall<void>(a_SetLeader, t, nullptr); // SetChieftain(0) elects a new chief
            }
        }
    }
}

// BUG-0173: a member knocked out on the host (health 0, the game's dead flag +0xB5E, behaviour
// node 0x05CA7FA7, it stays in the tribe until it respawns: the chieftain, NPC tribes) stood on the
// guest with its last health. The mirror's behaviour tree does not run on a guest, so the mod plays
// the death itself (Die + the death animation, game::play_death). When the host's member has health
// again, the dead mirror is replaced by a fresh one (the next snapshot binds or spawns it).
// `coop.tribe fix ko 0`: the code before (tests).
static bool g_fix_ko = true;
COOP_FIX("tribe.ko", "BUG-0173", g_fix_ko, "tribe: a guest's mirror of a member without health plays the death");
static uint32_t g_ko_shown, g_ko_replaced;

// BUG-0365: a mirror of a walking member was driven by drive::step towards the latest snapshot moved on
// by its arrival age: a MoveTo 0.25 s ahead every 0.1 s and SetPosition pulls. It walked at 9 m/s (~ the
// MoveTo given after SetDesiredSpeed resets the speed): while the host's member speeds up or slows down
// (1-5 m/s, every start and end of a walk) it reached the point ahead within a frame or two, stopped and
// walked again (the drawn step off the target's in 14-29 % of those frames), set off 0.5-1.6 m late and
// caught up at 15-24 m/s. The snapshots carried no sender time, so network jitter went straight into the
// target (the BUG-0122 class; over a mobile network 0.1 s windows matched the path in 71-80 %).
// * tribe.mirror_time: the host sends its clock with every chunk (an 8-byte trailer older guests ignore);
//   each mirror keeps a net::SnapshotBuffer (as the Creature puppets) and is driven towards the state
//   kMirrorDelay s back, walking at the interpolated path's velocity. Off (or from an older host): the
//   latest state + vel * age.
// * tribe.mirror_walk: while the member moves and the mirror is within kMirrorWalkMax of its path, the
//   shared drive::walk_place (driver.h, BUG-0360) walks it and places it on the path every frame (at its
//   own ground height, BUG-0329; the speed set after the MoveTo). Off: drive::step as in 0.7.3.3.
static void mirror_target(GMember& g, double now, net::Vec3& pos, net::Quat& rot, net::Vec3& vel, double& age)
{
    net::PlayerState s, back;
    if (g_mirror_time_fix && !g.buf.empty() && g.buf.sample(now, s)) {
        pos = s.pos;
        rot = s.rot;
        // the velocity of the interpolated path over the last kPathDt, not the newer state's: from a
        // standing state to a walking one the path moves slower than the walking state says, and the
        // game walks the mirror at this speed between two placings (it showed as stop-and-go)
        vel = s.vel;
        if (g.buf.sample(now - kPathDt, back)) {
            float k = (float)(1.0 / kPathDt);
            vel = {(s.pos.x - back.pos.x) * k, (s.pos.y - back.pos.y) * k, (s.pos.z - back.pos.z) * k};
            // never faster than the member walks: a late state over a bad network corrects the path by
            // 0.3-0.9 m at once, and walked at that path speed (13 m/s seen) the mirror surged ~1 m in a
            // frame on top of the placing's catch-up
            float ps = drive::speed(vel), ss = drive::speed(s.vel);
            if (ss > 0.3f && ps > ss) {
                float c = ss / ps;
                vel = {vel.x * c, vel.y * c, vel.z * c};
            }
        }
        age = 0;
        return;
    }
    pos = g.pos;
    rot = g.rot;
    vel = g.vel;
    age = now - g.seen;
}

// BUG-0444: this game's time is paused (cGameTimeManager +0x48 bit 0: after a cold join for 10-40 s, the pause
// menu): no citizen update runs, so nothing but the mod moves or grounds the mirrors
static bool game_time_paused()
{
    static uintptr_t get = H->sym("Simulator::cGameTimeManager::Get");
    uint8_t* tm = get ? skg::cdecl_call<uint8_t*>(get) : nullptr;
    return tm && !IsBadReadPtr(tm + 0x48, 1) && (tm[0x48] & 1);
}

// drive::step at the mirror's own height (BUG-0329) and, with tribe.mirror_ground, out of this game's ground
// (onto it from above too while the game is paused: nothing else brings it down)
static void mirror_step(GMember& g, const net::Vec3& pos, const net::Quat& rot, const net::Vec3& vel, double now, double age)
{
    const uint32_t gr = g.ds.grounded;
    if (g_mirror_ground_fix && game_time_paused()) {
        float a = (float)std::min(std::max(age, 0.0), 0.5);
        net::Vec3 t = drive::onto_ground(
            drive::at_own_height(net::Vec3{pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a}, game::position(g.c)));
        drive::step(g.c, t, rot, vel, g.ds, now, 0, true, 8.f, false, false, false, true);
    } else
        drive::step(g.c, pos, rot, vel, g.ds, now, age, true, 8.f, false, false, true, g_mirror_ground_fix);
    if (g.ds.grounded == gr)
        return;
    g_mirror_grounded++;
    if (g.ds.grounded <= 3) // (diagnostics: the fix at work, the first times per mirror)
        H->log(M, SK_INFO, "tribe: mirror %u (owner %u) was %.2f m under this game's ground: put onto it (tribe.mirror_ground)",
               (unsigned)g.id, (unsigned)g.owner, -g.ds.grounded_from);
}

static void mirror_drive(GMember& g, double now)
{
    net::Vec3 pos, vel;
    net::Quat rot;
    double age;
    mirror_target(g, now, pos, rot, vel, age);
    float a = (float)std::min(std::max(age, 0.0), 0.5);
    g.aim = drive::at_own_height(net::Vec3{pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a}, game::position(g.c));
    float sp = drive::speed(vel);
    g.aim_speed = sp;
    // BUG-0444: never aimed under this game's ground (walk_place's ground takes 3 m only), nor over it while the
    // game is paused (walked down a slope it stayed at its height, 3 m over the ground)
    if (g_mirror_ground_fix)
        g.aim = game_time_paused() ? drive::onto_ground(g.aim) : drive::out_of_ground(g.aim, true);
    if (g_mirror_walk_fix && sp > 0.3f && net::dist(game::position(g.c), g.aim) < kMirrorWalkMax) {
        drive::WalkPlace o;
        o.own_height = !g_mirror_ground_fix; // (the aim is at its own height already)
        o.speed_after_goal = true;
        o.max_dt = 0.4; // a slow guest keeps up with its members (driver.h)
        o.ground = g_mirror_ground_fix; // BUG-0444
        drive::walk_place(g.c, g.aim, rot, vel, g.ds, now, o);
        g_mirror_walks++;
        return;
    }
    drive::walk_end(g.c, g.aim, g.ds, now);
    mirror_step(g, pos, rot, vel, now, age);
}

static void guest_tick(double now)
{
    if (local_play())
        return; // the last states would hold the members for 3 s
    auto live_members = g_mirror_member_lifetime ? native_member_owners() : MemberOwners{};
    for (auto& [id, g] : g_gm) {
        if (g.c && g_mirror_member_lifetime && !live_members.count(g.c)) {
            g_gid_of.erase(g.c);
            g.c = nullptr;
        }
        if (!g.c || !game::object_alive(g.c) || now - g.seen > 3.0)
            continue;
        g.ds.drawn = game::position(g.c); // coop.track (BUG-0365): where the game drew it this frame
        if (g_fix_ko && g.hp <= 0) {
            if (!game::is_dead(g.c)) {
                game::set_health(g.c, 0);
                game::play_death(g.c);
                g_ko_shown++;
            }
            mirror_step(g, g.pos, g.rot, g.vel, now, now - g.seen);
            continue;
        }
        if (g_fix_ko && game::is_dead(g.c)) { // back on its feet on the host
            g_gid_of.erase(g.c);
            remove_member(g.c);
            g.c = nullptr;
            g_ko_replaced++;
            continue;
        }
        mirror_drive(g, now);
        float hp = game::health(g.c);
        if (std::fabs(hp - g.hp) > 0.01f && g.hp > 0)
            game::set_health(g.c, g.hp);
    }
}

// ---------------- co-op mechanics (docs/re/tribe.md, co-op features) ----------------
// pincer: members of two players hitting the same tribe within 10 s deal 50% more damage
// chief: every player has a chief (its lowest-id adult member, bigger, "★" tag); members near their
//        chief hit 20% harder; a chief about to die next to a partner's member keeps 30% health
// duet: performances of both players for the same tribe fill the social mini-game faster
// healers: a full heal of a partner's member brings 5 food
// epic: now and then an epic creature comes to the village; killed by both players: 80 food
constexpr double kPincerWindow = 10.0;
static uintptr_t a_CitizenTD, a_HealTick, a_PatientOf, a_AddFood, a_FoodFx, a_SessionGet, a_CreateHerd,
    a_HerdEnable, a_CreatureCreate;
static std::unordered_map<void*, std::unordered_map<uint8_t, double>> g_pincer; // enemy tribe -> owner -> time
static std::unordered_map<void*, double> g_pincer_told;
static std::unordered_map<uint8_t, void*> g_chief;                              // owner -> chief citizen
static std::unordered_map<void*, float> g_chief_scale;                          // chief -> its own scale
static std::unordered_map<void*, double> g_chief_saved;                         // chief -> last rescue
static uint32_t g_pincer_hits, g_aura_hits, g_chief_rescues, g_duet_ticks, g_heals, g_epics, g_epic_kills;
static void* g_epic;
static void* g_epic_herd; // the herd g_epic lives in (ours: quieted when the epic is gone)
static std::unordered_set<uint8_t> g_epic_hitters;
// BUG-0146: only the players' members count as hitters (an NPC tribe's member is owner 0xFE).
// BUG-0121: the epic's herd is reused and quieted, a live epic never goes into a save.
// `coop.tribe fix epic_players|epic_herd 0`: the old behaviour (tests).
static bool g_fix_epic_players = true, g_fix_epic_herd = true;
COOP_FIX("tribe.epic_players", "BUG-0146", g_fix_epic_players, "tribe: only players' members count as epic hitters");
COOP_FIX("tribe.epic_herd", "BUG-0121", g_fix_epic_herd, "tribe: the epic's herd is reused and quieted, a live epic is not saved");
static uint32_t g_epic_herd_reused, g_epic_saved_out;
constexpr uint32_t kEpicArchetype = 0xFC811218u;
static double g_epic_next, g_last_duet, g_duet_told;

struct Performance {
    uint16_t member = 0, round = 0, rounds = 0;
    uint32_t token = 0;
    uint8_t phase = 0, request = 0, ready = 0;
    float progress = 0;
};
static std::vector<Performance> g_performances;
static uint32_t g_performance_sequence, g_performance_received;
static double g_performance_sent, g_performance_seen;
static std::unordered_set<uint32_t> g_performance_rewarded;
static bool g_performance_campaign_fix = true;
COOP_FIX("tribe.performance_campaign", "BUG-9038", g_performance_campaign_fix,
         "tribe: share native-earned performance relationships with the linked campaign");

static void* native_performance(void* c, uint32_t& token, bool include_finished = false)
{
    if (!is_citizen(c) || !game::object_alive(c) || game::health(c) <= 0 || F<uint8_t>(c, 0xB5E)) return nullptr;
    void* ai = F<void*>(c, 0xB4C);
    if (!ai || !skg::readable(ai, 0x604) || F<uint32_t>(ai, 0x1D8) != 0xB46E8803u) return nullptr;
    void* block = F<void*>(ai, 0x600);
    if (!block || !skg::readable(block, 0x1AC) || !a_SessionGet) return nullptr;
    token = F<uint32_t>(block, 0x1A8);
    void* session = skg::cdecl_call<void*>(a_SessionGet, token);
    return session && skg::readable(session, 0x1E4) && F<uint32_t>(session, 4) < (include_finished ? 7u : 6u) &&
        F<void*>(session, 8) == F<void*>(c, kOwnerTribeOff) ? session : nullptr;
}

static bool g_performance_repeat_fix = true;
COOP_FIX("tribe.performance_repeat", "BUG-9037", g_performance_repeat_fix,
         "tribe: release completed native performances before starting another guest performance");
static bool release_completed_performers(uint8_t from)
{
    if (!g_performance_repeat_fix || from >= 0xFE || from != village_commander()) return false;
    uintptr_t cancel = H->sym("SporeCoop::Citizen_CancelAction");
    if (!cancel) return false;
    bool released = false;
    // Native terminal node9 retains the old action26 after the audience leaves.
    // Cancel every completed member before issuing any new order, so another
    // member cannot rejoin that finished session. Active sessions stay intact.
    for (const auto& [c, member] : g_hm) {
        if (member.owner != from || !game::object_alive(c) ||
            !registered_guest_village(F<void*>(c, kOwnerTribeOff), player_tribe())) continue;
        uint32_t token;
        void* session = native_performance(c, token, true);
        if (session && F<uint32_t>(session, 4) == 6) {
            skg::cdecl_call<bool>(cancel, c, 26);
            released = true;
        }
    }
    return released;
}

static void resume_performance(double now)
{
    if (g_pending_performance.ids.empty() || now < g_pending_performance.due) return;
    PendingPerformance pending = std::move(g_pending_performance);
    g_pending_performance = {};
    if (pending.world != modes::current_world() || !g_performance_repeat_fix || !village_orders()) return;
    net::Writer packet;
    packet.u8(CMD_ACTION); packet.u8((uint8_t)pending.ids.size());
    for (uint16_t id : pending.ids) packet.u16(id);
    packet.u32(26); write_target(packet, pending.target);
    packet.f32(0); packet.f32(0); packet.f32(0);
    net::Reader reader(packet.data.data(), packet.size());
    host_command(pending.from, reader, true); // resolves and validates all objects again
}

static bool queue_instrument(uint8_t from, uint16_t id, unsigned tool, uint32_t expected)
{
    if (!g_performance_fix || from >= 0xFE || from != village_commander() || tool < 4 || tool > 6) return false;
    auto it = g_hm_by_id.find(id);
    if (it == g_hm_by_id.end()) return false;
    void* c = it->second;
    auto member = g_hm.find(c);
    if (member == g_hm.end() || member->second.owner != from || !game::object_alive(c) ||
        !registered_guest_village(F<void*>(c, kOwnerTribeOff), player_tribe()) || founder_downed(c) ||
        F<uint32_t>(c, kAgeOff) != 1 || F<uint32_t>(c, 0x102C) != tool) return false;
    uint32_t token;
    void* session = native_performance(c, token);
    if (!session || token != expected || F<uint32_t>(session, 4) < 4 || !F<uint8_t>(session, 0x51)) return false;
    uintptr_t abilities = H->sym("SporeCoop::TribeAbilities"), find = H->sym("SporeCoop::Creature_AbilityIndex");
    uintptr_t post = H->sym("SporeCoop::BT_PostStimulus"), lifetime = H->sym("SporeCoop::TribeGiftCost_default");
    if (!abilities || !find || !post || !lifetime) return false;
    void* ability = ((void**)abilities)[tool - 1];
    int index = ability ? skg::cdecl_call<int>(find, c, ability) : -1;
    void* cooldowns = F<void*>(c, 0xC28);
    if (index < 0 || index >= 128 || !cooldowns || !skg::readable(cooldowns, (index + 1) * 16)) return false;
    float ready = F<float>(cooldowns, index * 16 + 4);
    if (!std::isfinite(ready) || ready < 1.f) return false;
    // D1FE00 queues both fields in the same frame. Never substitute PlayAbility
    // or write progress: the native session owns timing, scoring and rewards.
    void* stimulus = thiscall<void*>(post, (uint8_t*)F<void*>(c, 0xB4C) + 8,
                                   0x10000000u, 0u, *(float*)lifetime, (void*)nullptr);
    if (!stimulus) return false;
    F<int>(stimulus, 0xC) = index;
    return true;
}

static void share_performance_result(void* session, uint32_t token)
{
    if (!g_performance_campaign_fix || g_performance_rewarded.count(token)) return;
    void* ui = F<void*>(session, 0xE8);
    void* target = F<void*>(session, 0xC);
    uintptr_t success = H->sym("SporeCoop::TribePerformance_Succeeded");
    uintptr_t calculate = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
    void* manager = relationship_manager();
    unsigned result = F<uint32_t>(session, 0xEC);
    if (!ui || !skg::readable(ui, 0x6C) || !target || !game::object_alive(target) ||
        !success || !calculate || !manager || (result != 1 && result != 2) ||
        !thiscall<bool>(success, ui)) return;
    uint32_t faction = F<uint32_t>(target, 0x30);
    if (g_order_roster.contains_faction(faction)) return;
    float earned = thiscall<float>(calculate, manager, faction, g_order_roster.guest().faction, 0);
    if (!std::isfinite(earned)) return;
    struct Earned {
        uint64_t epoch;
        uint32_t world, target, guest, home;
        float value;
    };
    g_performance_rewarded.insert(token);
    // Native goal C2F750/C2F7F0 evaluates the campaign player's faction. Share
    // the guest's proven native result with that faction, then let the ordinary
    // goal system award the alliance, history, tools and totem. UI work runs
    // outside Present; the callback retains scalar identities only.
    H->run_outside_frame([](void* data) {
        Earned earned = *static_cast<Earned*>(data);
        delete static_cast<Earned*>(data);
        if (!g_performance_campaign_fix || !active() || !is_host() ||
            earned.epoch != g_campaign_epoch || earned.world != modes::current_world() ||
            earned.home != g_order_roster.home().faction || earned.guest != g_order_roster.guest().faction ||
            world::load_queued()) return;
        bool present = false;
        for (void* tribe : all_tribes())
            present |= F<uint32_t>(tribe, 0x30) == earned.target && !F<uint8_t>(tribe, 0x556);
        void* manager = relationship_manager();
        uintptr_t calculate = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
        uintptr_t apply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
        if (!present || !manager || !calculate || !apply) return;
        float guest = thiscall<float>(calculate, manager, earned.target, earned.guest, 0);
        float home = thiscall<float>(calculate, manager, earned.target, earned.home, 0);
        if (!std::isfinite(guest) || !std::isfinite(home) || guest < earned.value || home >= earned.value) return;
        (void)thiscall<float>(apply, manager, earned.target, earned.home, 0x0530CF07u, earned.value - home);
    }, new Earned{g_campaign_epoch, modes::current_world(), faction,
                  g_order_roster.guest().faction, g_order_roster.home().faction, earned});
}

static void host_performances(double now)
{
    if (now - g_performance_sent < 0.2 || !g_order_roster.guest().faction) return;
    g_performance_sent = now;
    std::vector<Performance> rows;
    if (g_performance_fix) for (const auto& [c, member] : g_hm) {
        if (member.owner != village_commander() || rows.size() >= 64 || !game::object_alive(c) ||
            !registered_guest_village(F<void*>(c, kOwnerTribeOff), player_tribe())) continue;
        Performance p; p.member = member.id;
        void* session = native_performance(c, p.token, true);
        if (!session) continue;
        p.phase = (uint8_t)F<uint32_t>(session, 4);
        if (p.phase == 6) { share_performance_result(session, p.token); continue; }
        g_performance_rewarded.erase(p.token);
        int request = F<int>(session, 0x1DC);
        p.request = request == 0 ? 5 : request == 1 ? 6 : request == 2 ? 4 : 0;
        p.ready = F<uint8_t>(session, 0x51) != 0;
        p.round = (uint16_t)std::min<uint32_t>(F<uint32_t>(session, 0x44), 65535);
        p.rounds = (uint16_t)std::min<uint32_t>(F<uint32_t>(session, 0x48), 65535);
        void* ui = F<void*>(session, 0xE8);
        if (ui && skg::readable(ui, 0x6C)) p.progress = F<float>(ui, 0x68);
        if (!std::isfinite(p.progress)) continue;
        p.progress = std::clamp(p.progress, 0.f, 1.f);
        rows.push_back(p);
    }
    net::Writer packet;
    packet.u32(modes::current_world()); packet.u32(++g_performance_sequence); packet.u8((uint8_t)rows.size());
    for (const auto& p : rows) {
        packet.u16(p.member); packet.u32(p.token); packet.u8(p.phase); packet.u8(p.request); packet.u8(p.ready);
        packet.f32(p.progress); packet.u16(p.round); packet.u16(p.rounds);
    }
    C.sess->send_unreliable_to(village_commander(), net::MSG_TRIBE_PERFORMANCE, packet);
}

static void guest_performances(net::Reader& packet)
{
    uint32_t world = packet.u32(), sequence = packet.u32();
    unsigned count = packet.u8();
    if (!g_performance_fix || !packet.ok() || !world || world != modes::current_world() || count > 64 ||
        packet.left() != count * 17 || (g_performance_received && (int32_t)(sequence - g_performance_received) <= 0)) return;
    std::vector<Performance> rows;
    std::unordered_set<uint16_t> seen;
    for (unsigned i = 0; i < count; ++i) {
        Performance p;
        p.member = packet.u16(); p.token = packet.u32(); p.phase = packet.u8(); p.request = packet.u8(); p.ready = packet.u8();
        p.progress = packet.f32(); p.round = packet.u16(); p.rounds = packet.u16();
        if (!p.member || !seen.insert(p.member).second || p.phase >= 6 || p.ready > 1 ||
            (p.request && (p.request < 4 || p.request > 6)) || !std::isfinite(p.progress) || p.progress < 0 || p.progress > 1)
            return;
        rows.push_back(p);
    }
    g_performances = std::move(rows); g_performance_received = sequence; g_performance_seen = H->time_seconds();
}

std::string performance_line()
{
    if (!g_performance_fix || !active() || !is_guest() || local_play() ||
        g_performances.empty() || H->time_seconds() - g_performance_seen > 2) return {};
    const auto& p = g_performances.front();
    const char* request = p.request == 4 ? "Horns [1]" : p.request == 5 ? "Maracas [2]" : "Didgeridoos [3]";
    return "Performance " + std::to_string((int)(p.progress * 100)) + "%: " +
        (p.ready && p.request ? std::string(request) : "wait for the audience");
}

static bool request_instrument(unsigned tool)
{
    if (tool < 4 || tool > 6 || performance_line().empty()) return false;
    auto selected = guest_village_ids(true);
    bool sent = false;
    for (const auto& p : g_performances) if (p.ready && std::find(selected.begin(), selected.end(), p.member) != selected.end()) {
        auto member = g_gm.find(p.member);
        if (member == g_gm.end() || !member->second.c || member->second.owner != C.sess->my_id() ||
            !game::object_alive(member->second.c) || F<uint32_t>(member->second.c, 0x102C) != tool) continue;
        Target target; target.kind = 4; target.noun = p.token;
        send_cmd(CMD_INSTRUMENT, {p.member}, (int)tool, target, {}); sent = true;
    }
    return sent;
}

static uint16_t member_id(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm.find(c);
        return it == g_hm.end() ? 0 : it->second.id;
    }
    auto it = g_gid_of.find(c);
    return it == g_gid_of.end() ? 0 : it->second;
}

uint16_t id_of(void* c) { return member_id(c); }

// tests (game bots): a readable key for tribe buildings on screen: hut<tribe index>, "p" for the player's
std::string object_key(void* obj)
{
    if (!g_ok || !obj)
        return "";
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t i = 0; i < tribes.size(); i++)
        if (F<void*>(tribes[i], 0x368) == obj) // cTribe::mpHut
            return "hut" + std::to_string(i) + (tribes[i] == pt ? "p" : "");
    return "";
}

// the tribe UI's interaction mode (TribeUI+0x390: 0 social, 1 attack; docs/re/tribe.md B), -1 unknown
static int ui_mode()
{
    uintptr_t get = H->sym("SporeCoop::cTribeUI_Get");
    void* ui = get ? skg::cdecl_call<void*>(get) : nullptr;
    return ui && !IsBadReadPtr((uint8_t*)ui + 0x390, 4) ? F<uint8_t>(ui, 0x390) : -1;
}

bool is_chief(void* c)
{
    for (auto& [o, ch] : g_chief)
        if (ch == c)
            return true;
    return false;
}

// health as the host has it: guests keep the last snapshot value (a mirror is never set to 0, so a
// knocked-out member would still look alive there and the machines would pick different chiefs)
static float host_health(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host)
        return game::health(c);
    auto it = g_gid_of.find(c);
    if (it == g_gid_of.end())
        return game::health(c);
    auto g = g_gm.find(it->second);
    return g == g_gm.end() ? game::health(c) : g->second.hp;
}

// A former chief gets its own size back (spatial vt 0x40 SetScale). BUG-0081: it is called only
// for a citizen that is still a member of the player tribe in the game's own list; before, the only
// check was object_alive, which reads the citizen's own memory and cannot tell a freed one (a guest
// mirror removed by guest_state, a load): the call went through a vtable read from freed memory.
// keep: the chiefs that stay (nullptr: none, the session ends). `coop.tribe fix chiefs 0`: the old check.
static bool g_fix_chiefs = true;
COOP_FIX_UNSAFE("tribe.chiefs", "BUG-0081", g_fix_chiefs, "tribe: chiefs checked against the player tribe's own list, not freed memory");
static uint32_t g_chief_restores, g_chief_gone, g_chief_bad_calls;
static void restore_chief_scales(const std::unordered_map<uint8_t, void*>* keep)
{
    std::unordered_set<void*> mem;
    if (void* pt = g_ok && game::mode_id() == modes::kTribe ? player_tribe() : nullptr) {
        void** b = F<void**>(pt, kMembersOff);
        void** e = F<void**>(pt, kMembersOff + 4);
        for (void** it = b; it && it < e && mem.size() < 256; ++it)
            if (*it)
                mem.insert(*it);
    }
    for (auto it = g_chief_scale.begin(); it != g_chief_scale.end();) {
        bool still = false;
        if (keep)
            for (auto& [o, c] : *keep)
                if (c == it->first)
                    still = true;
        if (still) {
            ++it;
            continue;
        }
        bool member = mem.count(it->first) != 0;
        if (g_fix_chiefs ? member : game::object_alive(it->first)) {
            if (!member)
                g_chief_bad_calls++; // the old check let a call into a non-member through (test)
            vcall<void>((uint8_t*)it->first + 0xC0, 0x40 / 4, it->second);
            g_chief_restores++;
        } else
            g_chief_gone++; // gone from the tribe: nothing to restore
        it = g_chief_scale.erase(it);
    }
}

// every machine picks the same chiefs (ids, owners and health come from the host)
static void chiefs_tick()
{
    void* pt = player_tribe();
    std::unordered_map<uint8_t, void*> want;
    std::unordered_map<uint8_t, uint16_t> best;
    if (pt)
        for (void* c : members(pt)) {
            uint8_t o = owner_of(c);
            uint16_t id = member_id(c);
            // a knocked-out member (the game's chieftain waits for its respawn with 0 health) leads nobody
            if (o >= 0xFE || !id || F<uint8_t>(c, kAgeOff) != 1 || !game::object_alive(c) || host_health(c) <= 0)
                continue;
            if (!best.count(o) || id < best[o]) {
                best[o] = id;
                want[o] = c;
            }
        }
    // scale: chiefs stand out (spatial vt 0x40 SetScale; citizen vt 0x80 its desired scale)
    restore_chief_scales(&want);
    for (auto& [o, c] : want)
        if (!g_chief_scale.count(c)) {
            float sc = vcall<float>(c, 0x80 / 4, 0); // GetDesiredScale(int), ret 4
            if (sc <= 0.01f || sc > 10.f)
                sc = 1.f;
            g_chief_scale[c] = sc;
            vcall<void>((uint8_t*)c + 0xC0, 0x40 / 4, sc * 1.3f);
        }
    g_chief = want;
}

static bool near_own_chief(void* c, uint8_t owner)
{
    auto it = g_chief.find(owner);
    return it != g_chief.end() && it->second != c && net::dist(game::position(it->second), game::position(c)) < 15.f;
}

// cCreatureCitizen TakeDamage 0xC26920 (this = citizen+0x5A8): every hit on a citizen
using CTD_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static CTD_t o_CitizenTD;
static CTD_t o_HutTD;
static bool protect_founder(void* victim, float damage, uint32_t attacker);
static bool protect_founder_hut(void* hut, float damage, uint32_t attacker);
static bool queue_founder_capture(bool guest, uint32_t attacker);
static std::set<uint32_t> g_combat_factions;
static std::map<uint16_t,float> g_remote_enemy_max, g_sent_enemy_max;
static double g_combat_poll, g_combat_resend;
static uint32_t g_combat_world;

static bool founder_village(void* village)
{
    return village && (village == player_tribe() || g_order_roster.contains_faction(F<uint32_t>(village,0x30)));
}

static bool village_allied_with_founder(void* village)
{
    auto home = player_tribe();
    auto manager = relationship_manager();
    static uintptr_t calculate = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
    if (!home || !manager || !calculate) return true;
    const auto faction = F<uint32_t>(village,0x30);
    const float allied = F<float>(manager,0x1C);
    if (!(allied > 0) || !std::isfinite(allied)) return true;
    for (auto partner : {F<uint32_t>(home,0x30),g_order_roster.guest().faction}) {
        if (partner && thiscall<float>(calculate,manager,faction,partner,0) >= allied) return true;
    }
    return false;
}

static float hostile_hit(void* target, void* attacker, float damage)
{
    if (damage <= 0 || !is_citizen(attacker) || !is_citizen(target)) return damage;
    void* attack_village = F<void*>(attacker,kOwnerTribeOff);
    void* target_village = F<void*>(target,kOwnerTribeOff);
    if (!attack_village || !target_village || attack_village == target_village) return damage;
    const bool defending = founder_village(target_village);
    if (defending == founder_village(attack_village)) return damage;
    auto village = defending ? attack_village : target_village;
    auto enemy = defending ? attacker : target;
    if (F<unsigned>(enemy,kAgeOff) != 1 || village_allied_with_founder(village)) return damage;
    auto rules = world::rules_for_current();
    if (rules.enemy_health_percent() != 100) {
        g_combat_factions.insert(F<uint32_t>(village,0x30));
        creature_hp::set_enemy_health(enemy,rules.enemy_health_percent());
    }
    return defending ? float(double(damage) * rules.enemy_damage_percent() / 100.0) : damage;
}

float reported_enemy_max(void* citizen)
{
    if (g_remote_enemy_max.empty()) return 0;
    auto it = g_gid_of.find(citizen);
    if (it == g_gid_of.end()) return 0;
    auto hp = g_remote_enemy_max.find(it->second);
    return hp == g_remote_enemy_max.end() ? 0.f : hp->second;
}

static void combat_tick(double now)
{
    if (!is_host() || now < g_combat_poll) return;
    g_combat_poll = now + .5;
    auto world = modes::current_world();
    if (world != g_combat_world) {
        g_combat_factions.clear();
        g_sent_enemy_max.clear();
        g_combat_world = world;
    }
    if (g_combat_factions.empty() && g_sent_enemy_max.empty()) return;
    std::map<uint16_t,float> scaled;
    const auto rules = world::rules_for_current();
    for (auto village : all_tribes()) {
        const auto faction = F<uint32_t>(village,0x30);
        if (!g_combat_factions.count(faction)) continue;
        bool hostile = !founder_village(village) && !village_allied_with_founder(village);
        if (!hostile) g_combat_factions.erase(faction);
        for (auto c : members(village)) {
            if (!is_citizen(c) || !game::object_alive(c) || F<unsigned>(c,kAgeOff) != 1) continue;
            creature_hp::set_enemy_health(c,hostile ? rules.enemy_health_percent() : 100);
            if (hostile && member_id(c)) scaled[member_id(c)] = game::max_health(c);
        }
    }
    if (scaled.size() > 2048 || (scaled == g_sent_enemy_max && now < g_combat_resend)) return;
    g_combat_resend = now + 5;
    g_sent_enemy_max = scaled;
    net::Writer w;
    w.u32(world);
    w.u16(uint16_t(scaled.size()));
    for (auto [id,hp] : scaled) { w.u16(id); w.f32(hp); }
    C.sess->broadcast_reliable(net::MSG_TRIBE_COMBAT,w);
}

static int SK_THISCALL h_CitizenTD(void* comb, float dmg, uint32_t pol, int type, const void* pos, void* att)
{
    if (is_host() && active()) {
        void* victim = (uint8_t*)comb - 0x5A8;
        void* pt = player_tribe();
        void* vt = F<void*>(victim, kOwnerTribeOff);
        void* ac = att && !IsBadReadPtr(att, 4) ? vcall<void*>(att, 0xC / 4) : nullptr;
        if (founder_downed(ac)) return 0;
        dmg = hostile_hit(victim,ac,dmg);
        uint8_t ao = ac && g_hm.count(ac) ? g_hm[ac].owner : 0xFF;
        double now = H->time_seconds();
        if (ao < 0xFE && vt && vt != pt) { // our member hits another tribe
            auto& hits = g_pincer[vt];
            hits[ao] = now;
            int players = 0;
            for (auto& [o, t] : hits)
                if (now - t < kPincerWindow)
                    players++;
            if (players >= 2) {
                dmg *= 1.5f;
                g_pincer_hits++;
                if (now - g_pincer_told[vt] > 60 && C.notice) {
                    g_pincer_told[vt] = now;
                    C.notice("pincer attack: +50% damage");
                }
            }
            if (near_own_chief(ac, ao)) {
                dmg *= 1.2f;
                g_aura_hits++;
            }
        }
        // a chief about to fall next to a partner's member: the tribe protects it
        if (vt == pt && is_chief(victim)) {
            float hp = F<float>(victim, 0x5E0), mx = F<float>(victim, 0x5D4);
            uint8_t o = owner_of(victim);
            if (dmg >= hp && mx > 0 && now - g_chief_saved[victim] > 60) {
                for (void* m : members(pt)) {
                    uint8_t mo = owner_of(m);
                    if (mo < 0xFE && mo != o && net::dist(game::position(m), game::position(victim)) < 15.f) {
                        dmg = std::max(0.f, hp - mx * 0.3f);
                        g_chief_saved[victim] = now;
                        g_chief_rescues++;
                        if (C.notice)
                            C.notice(C.sess->name_of(mo) + " saved " + C.sess->name_of(o) + "'s chief");
                        break;
                    }
                }
            }
        }
    }
    if (active() && is_host() && protect_founder((uint8_t*)comb - 0x5A8, dmg, pol)) return 0;
    return o_CitizenTD(comb, dmg, pol, type, pos, att);
}

static int SK_THISCALL h_HutTD(void* combatant, float damage, uint32_t faction, int type, const void* position, void* attacker)
{
    if (active() && is_host() && protect_founder_hut((uint8_t*)combatant - 0x120, damage, faction)) return 0;
    return o_HutTD(combatant, damage, faction, type, position, attacker);
}

static bool living(void* citizen)
{
    return game::health(citizen) > 0 && !F<uint8_t>(citizen, 0xB5E) && !founder_downed(citizen);
}

static bool intact_village(void* t)
{
    void* hut = t ? F<void*>(t, 0x368) : nullptr;
    return t && !F<uint8_t>(t, 0x556) && hut && game::object_alive(hut) && F<float>(hut, 0x158) > 0 &&
           F<void*>(hut, 0x230) == t && F<uint32_t>(hut, 0x30) == F<uint32_t>(t, 0x30);
}

static void* founder_village(bool guest)
{
    void* home = player_tribe();
    if (!home || !g_order_roster.guest().faction || F<uint32_t>(home, 0x30) != g_order_roster.home().faction) return nullptr;
    if (!guest) return home;
    for (void* t : all_tribes()) if (registered_guest_village(t, home)) return t;
    return nullptr;
}

static bool g_shared_hut_upgrade = true;
COOP_FIX("tribe.shared_hut_upgrade", "BUG-9045", g_shared_hut_upgrade,
         "tribe: apply earned campaign hut upgrades to the guest founder");
static bool g_hut_upgrade_queued;
static double g_hut_upgrade_poll;

static void shared_hut_upgrade_tick(double now)
{
    if (!g_shared_hut_upgrade || !is_host() || g_hut_upgrade_queued || now < g_hut_upgrade_poll ||
        world::load_queued() || world::save_busy() || world::time_paused() || world::cinematic_playing() ||
        rescue_pending() || outfits_pending()) return;
    g_hut_upgrade_poll = now + 1;
    void* home = founder_village(false);
    void* guest = founder_village(true);
    if (!intact_village(home) || !intact_village(guest) || F<int>(home, 0x38) == F<int>(guest, 0x38)) return;
    g_hut_upgrade_queued = true;
    H->run_outside_frame([](void* context) {
        uint64_t epoch = *(uint64_t*)context; delete (uint64_t*)context;
        g_hut_upgrade_queued = false;
        if (epoch != g_campaign_epoch || !active() || !is_host() || !g_shared_hut_upgrade ||
            world::load_queued() || world::save_busy() || world::time_paused() || world::cinematic_playing() ||
            rescue_pending() || outfits_pending()) return;
        void* home = founder_village(false);
        void* guest = founder_village(true);
        if (!intact_village(home) || !intact_village(guest)) return;
        int level = F<int>(home, 0x38);
        uintptr_t set = H->sym("SporeCoop::Community_SetLevel");
        uintptr_t refresh = H->sym("SporeCoop::Tribe_UpdateHutModel");
        if (!set || !refresh || level < 0 || level > 4 || level == F<int>(guest, 0x38)) return;
        // The native upgrade selects the species-appropriate hut, updates its
        // maximum health and layout, and refills health. Apply it once per
        // changed level, keeping the hut, purchases, village and citizens.
        thiscall<void>(set, guest, level);
        thiscall<void>(refresh, guest);
        g_campaign_refresh_at = H->time_seconds() + 2;
        H->log(M, SK_INFO, "tribe: guest founder hut received shared campaign level %d", level);
    }, new uint64_t(g_campaign_epoch));
}

static bool g_objective_backfill_queued;
static double g_objective_backfill_poll;
static bool missing_last_objective()
{
    void* home = founder_village(false);
    if (!intact_village(home) || !intact_village(founder_village(true))) return false;
    void* totem = F<void*>(home, 0x19D0);
    if (!totem || !game::object_alive(totem)) return false;
    uintptr_t begin = F<uintptr_t>(totem, 0x11C), end = F<uintptr_t>(totem, 0x120);
    if (!begin || end < begin || end-begin != 4*4) return false;
    auto tribes = all_tribes();
    if (tribes.size() != 6) return false; // two founders, only four normal NPC objectives
    void* manager = relationship_manager();
    uintptr_t relation = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
    if (!manager || !relation) return false;
    for (void* tribe : tribes) {
        uint32_t faction = F<uint32_t>(tribe, 0x30);
        if (g_order_roster.contains_faction(faction) || !intact_village(tribe)) continue;
        float feeling = thiscall<float>(relation, manager, faction, g_order_roster.home().faction, 0);
        if (!std::isfinite(feeling) || feeling < 7) return false;
    }
    return true;
}

static void objective_backfill_tick(double now)
{
    if (!g_founder_npc_budget || !g_form_count_return || g_objective_backfill_queued ||
        now < g_objective_backfill_poll || world::time_paused() || world::cinematic_playing() ||
        world::load_queued() || world::save_busy() || rescue_pending() || outfits_pending()) return;
    g_objective_backfill_poll = now + 5;
    if (!missing_last_objective()) return;
    g_objective_backfill_queued = true;
    H->run_outside_frame([](void* context) {
        uint64_t epoch = *(uint64_t*)context; delete (uint64_t*)context;
        g_objective_backfill_queued = false;
        if (epoch != g_campaign_epoch || !active() || !is_host() || !g_founder_npc_budget ||
            world::time_paused() || world::cinematic_playing() || world::load_queued() || world::save_busy() ||
            rescue_pending() || outfits_pending() || !missing_last_objective()) return;
        uintptr_t get = H->sym("Simulator::cNpcTribeController::Get");
        uintptr_t form = H->sym("SporeCoop::NpcTribeController_FormVillage");
        void* controller = get ? skg::cdecl_call<void*>(get) : nullptr;
        if (!controller || !form) return;
        // An older save may have consumed its native formation trigger already.
        // Retry that same native operation, with its corrected NPC budget. It
        // chooses an available marker/species and grants no campaign progress.
        // Its EAX is not a success flag; inspect the actual village count.
        thiscall<void>(form, controller, uint32_t(0));
        if (all_tribes().size() == 7) {
            g_campaign_refresh_at = H->time_seconds() + 2;
            H->log(M, SK_INFO, "tribe: native controller restored the fifth NPC objective");
        }
    }, new uint64_t(g_campaign_epoch));
}

static bool g_auto_founders = true;
COOP_FIX("tribe.auto_founders", "BUG-9040", g_auto_founders,
         "tribe: found and save a distinct guest species village automatically");
static double g_auto_found_at;
static bool g_auto_found_queued, g_auto_found_refresh;
static bool g_auto_found_manual_off;
static founders::Roster g_auto_found_created;
static std::string g_auto_found_notice;

static void add_clear_sites(net::Vec3 c, std::vector<net::Vec3>& occupied, size_t limit)
{
    net::Vec3 u{c.z, 0, -c.x};
    float radius = drive::speed(c), ul = drive::speed(u);
    if (ul < .001f) { u = {0, -c.z, c.y}; ul = drive::speed(u); }
    float height = game::ground_radius(c);
    if (!std::isfinite(radius) || radius < 1 || ul < .001f || !std::isfinite(height) || height < 1) return;
    u = {u.x/ul, u.y/ul, u.z/ul};
    net::Vec3 v{(c.y*u.z-c.z*u.y)/radius, (c.z*u.x-c.x*u.z)/radius, (c.x*u.y-c.y*u.x)/radius};
    for (float distance : {100.f, 140.f, 180.f}) for (int step = 0; step < 16; ++step) {
        float angle = step * .3926990817f;
        net::Vec3 q{c.x+distance*(u.x*std::cos(angle)+v.x*std::sin(angle)),
                    c.y+distance*(u.y*std::cos(angle)+v.y*std::sin(angle)),
                    c.z+distance*(u.z*std::cos(angle)+v.z*std::sin(angle))};
        float ground = game::ground_radius(q), length = drive::speed(q);
        // Stay at least as high as the inhabited home site, on similarly flat
        // ground. This deliberately declines valleys or steep terrain.
        if (!std::isfinite(ground) || ground < height || ground > height+3 || length < 1) continue;
        q = {q.x*ground/length, q.y*ground/length, q.z*ground/length};
        bool clear = true;
        for (const auto& other : occupied) if (net::dist(q, other) < 70.f) { clear = false; break; }
        if (!clear) continue;
        for (const auto& direction : {u, v}) for (float sign : {-1.f, 1.f}) {
            net::Vec3 edge{q.x+direction.x*20*sign, q.y+direction.y*20*sign, q.z+direction.z*20*sign};
            float edge_height = game::ground_radius(edge);
            if (!std::isfinite(edge_height) || std::fabs(edge_height-ground) > 3) clear = false;
        }
        if (clear) occupied.push_back(q);
        if (occupied.size() >= limit) return;
    }
}

static bool founding_site(void* home, net::Vec3& result)
{
    std::vector<net::Vec3> occupied;
    for (auto village : all_tribes()) occupied.push_back(tribe_pos(village));
    size_t target = occupied.size() + 1;
    add_clear_sites(tribe_pos(home),occupied,target);
    if (occupied.size() != target) return false;
    result = occupied.back();
    return true;
}

static void auto_founders_tick(double now)
{
    if (!g_auto_founders || g_auto_found_manual_off || !is_host() || g_auto_found_queued || now < g_auto_found_at ||
        C.sess->players().size() != 2 || world::cinematic_playing() || world::time_paused() ||
        world::load_queued() || world::save_busy() || world::save_pending()) return;
    g_auto_found_at = now + 5;
    if (g_order_roster.guest().faction && !g_auto_found_refresh) return;
    g_auto_found_queued = true;
    H->run_outside_frame([](void* context) {
        uint64_t epoch = *(uint64_t*)context; delete (uint64_t*)context;
        if (epoch != g_campaign_epoch) return;
        g_auto_found_queued = false;
        if (!g_auto_founders || g_auto_found_manual_off || !active() || !is_host() ||
            C.sess->players().size() != 2 || world::load_queued() || world::save_busy() ||
            world::save_pending() || world::cinematic_playing() || world::time_paused()) return;
        auto report = [](const std::string& text) {
            if (text == g_auto_found_notice) return;
            g_auto_found_notice = text;
            H->log(M, SK_INFO, "tribe automatic founders: %s", text.c_str());
            if (C.notice) C.notice(text);
        };
        auto planet = world::current_game_key();
        void* home = player_tribe();
        if (planet.empty() || !home || !intact_village(home) || !a_SpawnNpcTribe) return;
        uint8_t guest = 0;
        for (const auto& p : C.sess->players()) if (p.id) guest = p.id;
        auto key = creations::player_key(guest, creations::kCreature);
        void* species = guest ? creations::player_species(guest) : nullptr;
        uint32_t home_species = tribe_info(home).key.instance, home_faction = F<uint32_t>(home, 0x30);
        if (!guest || !species || !key.instance || !home_species || key.instance == home_species) return;
        if (g_auto_found_created.guest().faction &&
            (g_auto_found_created.home().species != home_species || g_auto_found_created.home().faction != home_faction ||
             g_auto_found_created.guest().species != key.instance)) {
            report("A founding operation is unfinished for the previous species. Keep that species selected to finish saving.");
            return;
        }
        founders::Roster saved;
        std::string error;
        if (founders::read(planet, saved)) {
            if (saved.home().species != home_species || saved.home().faction != home_faction ||
                saved.guest().species != key.instance) {
                report("The selected species do not match this planet's founders."); return;
            }
            g_order_roster = saved;
        } else {
            // Validate the destination before creating native objects. This
            // placeholder is never persisted; the factory supplies its faction.
            uint32_t placeholder = home_faction == 0xFFFFFFFFu ? 0xFFFFFFFEu : 0xFFFFFFFFu;
            if (!founders::can_record(planet, {home_species, key.instance, home_faction, placeholder}, error)) {
                report(error); return;
            }
            void* village = nullptr;
            for (void* t : all_tribes()) if (t != home && tribe_info(t).key.instance == key.instance) {
                if (village || !g_auto_found_created.guest().faction ||
                    F<uint32_t>(t, 0x30) != g_auto_found_created.guest().faction) {
                    report("This species already belongs to another village; automatic founding stopped."); return;
                }
                village = t;
            }
            if (!creations::archive_guest_model(guest, planet, error)) { report(error); return; }
            if (!village) {
                net::Vec3 site;
                if (!founding_site(home, site)) { report("No clear, level founding site near your village is available yet."); return; }
                skg::Vector3 pos{site.x, site.y, site.z};
                village = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, 0, 3, 15, 0, species);
                if (!village) { report("The native game could not create your partner's village."); return; }
                g_auto_found_created = {home_species, key.instance, home_faction, F<uint32_t>(village, 0x30)};
            }
            if (!intact_village(village) || members(village).empty()) {
                report("Your partner's native village is incomplete; automatic founding stopped."); return;
            }
            if (!founders::record(planet, g_auto_found_created, error)) { report(error); return; }
            g_order_roster = g_auto_found_created;
            g_auto_found_refresh = true;
        }
        if (g_auto_found_refresh) {
            if (!world::checkpoint_reload(error)) { report("Founders registered; checkpoint retry pending: " + error); return; }
            g_auto_found_refresh = false;
            report("Both species have their own villages. Saving and refreshing the shared campaign.");
        }
    }, new uint64_t(g_campaign_epoch));
}

static bool birth_cost(float& cost)
{
    uintptr_t address = H ? H->sym("SporeCoop::Tribe_BabyFoodCost") : 0;
    if (!address) return false;
    cost = *(const float*)address;
    return std::isfinite(cost) && cost >= 0 && cost <= 1000000;
}

std::string village_birth_line()
{
    float cost;
    if (!birth_cost(cost)) return "Births are unavailable on this game build.";
    char text[96];
    snprintf(text, sizeof text, "Add a baby (%.2f food from your village).", cost);
    return text;
}

static bool g_tool_slots = true;
COOP_FIX("tribe.tool_slots", "BUG-9024", g_tool_slots,
         "tribe: purchase into a native building slot so placement survives reload");

static bool place_committed_tool(void* village, int kind, void* slot, uintptr_t occupy,
                                 const net::Vec3& pos, uintptr_t layout)
{
    void* tool = vcall<void*>(village, 0xB4 / 4, kind);
    if (!tool) return false;
    vcall<void>(tool, 0x64 / 4, village);
    if (slot) thiscall<void>(occupy, slot, tool);
    vcall<void>((uint8_t*)tool + 0x34, 0x38 / 4, &pos);
    vcall<void>(tool, 0x64 / 4, village);
    thiscall<void>(layout, village, kind);
    return true;
}

static bool stock_trial_instrument(void* village)
{
    // The raw NPC factory creates a hut and members. A social audience also
    // needs an instrument rack. Use a free native slot and the same complete
    // ownership/placement operation used by the village planner.
    std::array<void*,11> tools{};
    if (!tool_inventory(village, tools)) return false;
    if (tools[4] || tools[5] || tools[6]) return true;
    uintptr_t nearest = H->sym("SporeCoop::CommunityLayout_NearestSlot");
    uintptr_t occupy = H->sym("SporeCoop::CommunitySlot_SetObject");
    uintptr_t layout = H->sym("SporeCoop::Tribe_UpdateToolLayout");
    if (!nearest || !occupy || !layout) return false;
    net::Vec3 center = tribe_pos(village);
    float radius = drive::speed(center);
    if (!(radius > 1.f)) return false;
    net::Vec3 up{center.x/radius, center.y/radius, center.z/radius};
    net::Vec3 u{up.z, 0, -up.x};
    if (drive::speed(u) < .01f) u = {0, -up.z, up.y};
    float length = drive::speed(u);
    u = {u.x/length, u.y/length, u.z/length};
    net::Vec3 v{up.y*u.z-up.z*u.y, up.z*u.x-up.x*u.z, up.x*u.y-up.y*u.x};
    for (int i=0; i<16; ++i) {
        float x=22.f*std::cos(i*.3926990817f), y=22.f*std::sin(i*.3926990817f);
        net::Vec3 point{center.x+u.x*x+v.x*y,center.y+u.y*x+v.y*y,center.z+u.z*x+v.z*y};
        void* slot=thiscall<void*>(nearest, (uint8_t*)village + 0x3B4, &point);
        if (!slot || !skg::readable(slot,24) || F<uint8_t>(slot,0) || F<void*>(slot,4)) continue;
        const auto pos=F<net::Vec3>(slot,0xC);
        float distance=net::dist(pos,center);
        if (distance >= 12 && distance <= 35) return place_committed_tool(village,5,slot,occupy,pos,layout);
    }
    return false;
}

static bool buy_guest_tool(uint8_t from, uint16_t witness, int kind, net::Vec3 pos)
{
    auto reject = [](const char* reason) { if (C.notice) C.notice(reason); return false; };
    auto found = g_hm_by_id.find(witness);
    void* village = founder_village(true);
    if (!village || !intact_village(village) || found == g_hm_by_id.end() || !living(found->second) ||
        owner_of(found->second) != from || F<void*>(found->second, kOwnerTribeOff) != village ||
        world::load_queued() || world::save_busy() || world::save_pending()) return false;
    int cost;
    uintptr_t layout = H->sym("SporeCoop::Tribe_UpdateToolLayout");
    uintptr_t purchased = H->sym("SporeCoop::Tribe_SetPurchasedTool");
    if (!layout || !purchased || !tool_cost(kind, cost)) return false;
    if (!(tool_unlocks() & (1u << kind))) return reject("Ally with or conquer other tribes to unlock that tool.");
    std::array<void*, 11> tools{};
    if (!tool_inventory(village, tools) || tools[kind]) return reject("That rack already exists, or the village planner is busy.");
    double food;
    if (!shared::tribe_food(village, food) || food < cost) return reject("Your village needs more food for that rack.");
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) return false;
    net::Vec3 anchor = tribe_pos(village);
    float distance = net::dist(pos, anchor);
    if (distance < 12.f || distance > 35.f) return reject("Place the rack on clear ground 12 to 35 metres from your hut.");
    float radius = std::sqrt(pos.x*pos.x + pos.y*pos.y + pos.z*pos.z);
    float ground = game::ground_radius(pos), home_ground = game::ground_radius(anchor);
    if (!std::isfinite(ground) || ground < 1 || radius < 1 || std::fabs(radius-ground) > 4 ||
        !std::isfinite(home_ground) || std::fabs(ground-home_ground) > 3)
        return reject("Choose level ground near your village for the rack.");
    pos = {pos.x*ground/radius, pos.y*ground/radius, pos.z*ground/radius};
    void* slot = nullptr;
    uintptr_t occupy = H->sym("SporeCoop::CommunitySlot_SetObject");
    if (g_tool_slots) {
        uintptr_t nearest = H->sym("SporeCoop::CommunityLayout_NearestSlot");
        if (!nearest || !occupy) return false;
        slot = thiscall<void*>(nearest, (uint8_t*)village + 0x3B4, &pos);
        if (!slot || IsBadReadPtr(slot, 24) || F<uint8_t>(slot, 0) || F<void*>(slot, 4))
            return reject("Choose an empty building spot around your village.");
        net::Vec3 snapped = F<net::Vec3>(slot, 0xC);
        if (!std::isfinite(snapped.x) || !std::isfinite(snapped.y) || !std::isfinite(snapped.z) ||
            net::dist(snapped, pos) > 12.f || net::dist(snapped, anchor) > 35.f)
            return reject("Choose ground closer to a building spot around your village.");
        pos = snapped;
    }
    struct Clearance { net::Vec3 pos; bool blocked = false; } clearance{pos};
    game::for_each_object([](void* object, uint32_t noun, void* context) {
        auto& c = *(Clearance*)context;
        // Ignore moving creatures; preserve eight metres around buildings.
        // The village's food mat is checked by its native reference below.
        if (noun != kHutNoun && noun != kToolNoun) return true;
        net::Vec3 at;
        if (game::object_alive(object) && game::object_position(object, at) && net::dist(at, c.pos) < 8.f) c.blocked = true;
        return true;
    }, &clearance);
    void* mat = F<void*>(village, 0x260);
    net::Vec3 mat_pos;
    if (mat && game::object_position(mat, mat_pos) && net::dist(mat_pos, pos) < 8.f) clearance.blocked = true;
    if (clearance.blocked) return reject("Leave eight metres between the new rack and existing village buildings.");
    if (!place_committed_tool(village,kind,slot,occupy,pos,layout)) return reject("The game could not create that tool rack.");
    thiscall<void>(purchased, village, kind);
    shared::mirror_village_food(village, food-cost);
    ++g_cmds_done;
    if (C.notice) C.notice("Your partner built a tool rack in their village.");
    return true;
}

static bool buy_guest_baby(uint8_t from, uint16_t witness)
{
    auto found = g_hm_by_id.find(witness);
    void* village = founder_village(true);
    if (!village || !intact_village(village) || found == g_hm_by_id.end() ||
        !game::object_alive(found->second) || !is_citizen(found->second) ||
        owner_of(found->second) != from || !living(found->second) ||
        F<void*>(found->second, kOwnerTribeOff) != village || world::load_queued() ||
        world::save_busy() || world::save_pending()) return false;
    float cost;
    double food;
    uintptr_t get = H->sym("SporeCoop::TribalGame_Get");
    uintptr_t blocked = H->sym("SporeCoop::TribalGame_BirthBlocked");
    void* ui = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!birth_cost(cost) || !blocked || !ui || !F<uint8_t>(ui, 0x3A3) ||
        skg::cdecl_call<bool>(blocked)) {
        if (C.notice) C.notice("Births are not available at this point in the Tribal campaign.");
        return false;
    }
    if (!shared::tribe_food(village, food) || food < cost) {
        if (C.notice) C.notice("Your partner's village needs more food to add a baby.");
        return false;
    }
    // Use the capped native path, never the mirror helper's uncapped fallback.
    // SpawnMember has no price side effect. Charge only after successful creation.
    void* baby = vcall<void*>(village, 0x98 / 4, 0);
    if (!baby) {
        if (C.notice) C.notice("Your partner's village is at its population limit.");
        return false;
    }
    shared::mirror_village_food(village, food - cost);
    ++g_cmds_done;
    if (C.notice) C.notice("A baby joined your partner's original species village.");
    return true;
}

static bool protect_founder(void* victim, float damage, uint32_t attacker)
{
    if (!std::isfinite(damage) || damage <= 0 || !g_order_roster.guest().faction) return false;
    if (founder_downed(victim)) return true;
    auto tracked = g_hm.find(victim);
    if (tracked == g_hm.end()) return false;
    double now = H->time_seconds();
    for (const auto& rescue : g_founder_rescues)
        if (rescue.member == tracked->second.id && now < rescue.grace_until) return true;
    if (C.sess->players().size() != 2 || village_commander() >= 0xFE || damage < game::health(victim) ||
        game::health(victim) <= 0 || F<uint8_t>(victim, 0xB5E)) return false;
    void* tribe = F<void*>(victim, kOwnerTribeOff);
    bool guest = tribe != player_tribe();
    if (tribe != founder_village(guest) || !intact_village(tribe)) return false;
    for (void* c : members(tribe)) if (c != victim && living(c)) return false;
    void* partner = founder_village(!guest);
    if (!partner) return false;
    bool survivor = false;
    for (void* c : members(partner)) survivor |= living(c);
    if (!survivor) return false;
    auto& rescue = g_founder_rescues[guest ? 1 : 0];
    rescue = {tracked->second.id, F<uint32_t>(tribe, 0x30), now + 10, -1, 0, game::position(victim)};
    rescue.attacker_faction = attacker;
    game::set_health(victim, 1);
    game::stop(victim);
    if (C.notice) C.notice("Last villager down. Bring a partner villager within 15 metres for 1.5 seconds to revive. 10 seconds before respawn or capture.");
    return true;
}

static bool protect_founder_hut(void* hut, float damage, uint32_t attacker)
{
    if (!std::isfinite(damage) || damage <= 0 || !village_orders() ||
        C.sess->players().size() != 2 || village_commander() >= 0xFE) return false;
    void* tribe = F<void*>(hut, 0x230);
    bool guest = tribe != player_tribe();
    if (!tribe || tribe != founder_village(guest) || F<void*>(tribe, 0x368) != hut || !intact_village(tribe)) return false;
    auto& rescue = g_founder_rescues[guest ? 1 : 0];
    double now = H->time_seconds();
    if (rescue.faction == F<uint32_t>(tribe, 0x30) && now < rescue.grace_until) return true;
    if (rescue.until && rescue.hut_down) return true;
    float health = F<float>(hut, 0x158);
    if (!std::isfinite(health) || health <= 0 || damage < health) return false;
    void* partner = founder_village(!guest);
    if (!partner) return false;
    auto helpers = members(partner);
    if (std::none_of(helpers.begin(), helpers.end(), living)) return false;
    if (!rescue.until) {
        void* representative = nullptr;
        for (void* person : members(tribe)) if (living(person) && g_hm.count(person)) { representative = person; break; }
        if (!representative) return false;
        rescue = {g_hm[representative].id, F<uint32_t>(tribe, 0x30), now + 10, -1, 0, game::position(representative)};
        rescue.attacker_faction = attacker;
        game::set_health(representative, 1);
    }
    rescue.hut_down = true;
    F<float>(hut, 0x158) = 1;
    for (void* person : members(tribe)) game::stop(person);
    if (C.notice) C.notice("Village down. Bring a partner villager to the marked survivor within 10 seconds to prevent capture.");
    return true;
}

static void founder_rescue_tick(double now)
{
    for (size_t seat = 0; seat < g_founder_rescues.size(); ++seat) {
        auto& rescue = g_founder_rescues[seat];
        if (!rescue.until) continue;
        auto found = g_hm_by_id.find(rescue.member);
        void* t = founder_village(seat != 0);
        if (!t || F<uint32_t>(t, 0x30) != rescue.faction || found == g_hm_by_id.end()) { rescue = {}; continue; }
        void* body = found->second;
        auto people = members(t);
        if (std::find(people.begin(), people.end(), body) == people.end() || F<uint8_t>(body, 0xB5E)) { rescue = {}; continue; }
        if (!intact_village(t)) {
            rescue = {};
            skg::Vector3 direction{};
            o_CitizenTD((uint8_t*)body + 0x5A8, 1e6f, 0xFFFFFFFFu, 0, &direction, nullptr);
            continue;
        }
        game::set_health(body, 1);
        game::stop(body);
        if (rescue.hut_down) for (void* person : people) game::stop(person);
        if (net::dist(game::position(body), rescue.anchor) > 0.4f)
            game::teleport(body, rescue.anchor, game::orientation(body));
        bool partner_near = false;
        if (void* partner = founder_village(seat == 0))
            for (void* c : members(partner))
                if (living(c) && net::dist(game::position(c), rescue.anchor) <= 15) partner_near = true;
        if (!partner_near) rescue.near_since = -1;
        else if (rescue.near_since < 0) rescue.near_since = now;
        bool revived = partner_near && rescue.near_since + 1.5 <= std::min(now, rescue.until);
        if (!revived && now < rescue.until) continue;
        if (!revived && queue_founder_capture(seat != 0, rescue.attacker_faction)) continue;
        double cost = 0;
        if (!revived) {
            double food = 0;
            if (shared::tribe_food(t, food) && std::isfinite(food) && food > 0) {
                cost = std::min(food, std::ceil(food * 0.10));
                shared::mirror_village_food(t, food - cost);
            }
            net::Vec3 at = tribe_pos(t);
            if (void* mat = F<void*>(t, 0x260); mat && game::object_alive(mat)) game::object_position(mat, at);
            game::teleport(body, at, game::orientation(body));
        }
        game::set_health(body, std::max(1.f, game::max_health(body) * 0.4f));
        bool refresh_hut = rescue.hut_down;
        if (refresh_hut) {
            void* hut = F<void*>(t, 0x368);
            F<float>(hut, 0x158) = std::max(1.f, F<float>(hut, 0x14C) * 0.4f);
        }
        rescue.until = 0;
        rescue.grace_until = now + 2;
        if (C.notice) C.notice(revived ? "Your partner revived the last villager." :
            "The last villager respawned at its village. Food setback " + std::to_string((int)cost) + ".");
        if (refresh_hut) {
            std::string error;
            if (!world::checkpoint_reload(error) && C.notice) C.notice("Village recovered; world refresh failed: " + error);
        }
    }
}

float revive_seconds(void* c)
{
    double now = H->time_seconds();
    uint16_t id = id_of(c);
    if (!id) return 0;
    if (is_host()) {
        for (const auto& rescue : g_founder_rescues)
            if (rescue.member == id && rescue.until > 0) return (float)std::max(0.1, rescue.until - now);
    } else {
        for (const auto& rescue : g_remote_rescues)
            if (rescue.first == id && rescue.second > now) return (float)(rescue.second - now);
    }
    return 0;
}

bool rescue_pending()
{
    if (!active()) return false;
    if (is_host()) {
        for (const auto& rescue : g_founder_rescues) if (rescue.until > 0) return true;
    } else {
        for (const auto& rescue : g_remote_rescues) if (rescue.second > H->time_seconds()) return true;
    }
    return false;
}

// Shared native reconstruction operation. The explicit host probe below is
// retained for regression fixtures; normal requests come from a living helper.
static std::string g_home_probe_result;
static std::string g_home_probe_world;
static bool g_home_probe_pending;
static bool g_refound_campaign = true;
COOP_FIX("tribe.refound_campaign", "BUG-9030", g_refound_campaign,
         "tribe: preserve earned totem history and level when rebuilding a founder");

static bool restore_founder_campaign(void* old, void* replacement)
{
    uintptr_t level = H->sym("SporeCoop::Community_SetLevel");
    uintptr_t create = H->sym("SporeCoop::Tribe_CreateTotem");
    uintptr_t append = H->sym("SporeCoop::Totem_AddSegment");
    uintptr_t hut_model = H->sym("SporeCoop::Tribe_UpdateHutModel");
    int earned_level = F<int>(old, 0x38);
    if (!level || !create || !append || !hut_model || earned_level < 0 || earned_level > 4) return false;
    void* prior = F<void*>(old, 0x19D0);
    if (prior) {
        if (!game::object_alive(prior)) return false;
        uintptr_t types = F<uintptr_t>(prior, 0x11C), end = F<uintptr_t>(prior, 0x120);
        uintptr_t colors = F<uintptr_t>(prior, 0x130), colors_end = F<uintptr_t>(prior, 0x134);
        if (end < types || end - types > 256 || (end - types) % 4 ||
            colors_end < colors || colors_end - colors != (end - types) * 3) return false;
        size_t count = (end - types) / 4;
        if (count && (IsBadReadPtr((void*)types, count * 4) || IsBadReadPtr((void*)colors, count * 12))) return false;
        for (size_t i = 0; i < count; ++i) {
            if (((int*)types)[i] < 0 || ((int*)types)[i] > 1) return false;
            for (int k = 0; k < 3; ++k) if (!std::isfinite(((float*)colors)[i * 3 + k])) return false;
        }
        if (F<void*>(replacement, 0x19D0) || !thiscall<bool>(create, replacement)) return false;
        void* totem = F<void*>(replacement, 0x19D0);
        if (!totem || !game::object_alive(totem)) return false;
        F<int>(totem, 0x144) = F<int>(prior, 0x144);
        for (size_t i = 0; i < count; ++i)
            thiscall<void>(append, totem, ((int*)types)[i], (const float*)(colors + i * 12));
    }
    thiscall<void>(level, replacement, earned_level);
    return true;
}

static std::string rebuild_founder(uint32_t conqueror_id, bool defeated_guest = false, const net::Vec3* chosen = nullptr)
{
    if (!active() || !is_host() || C.sess->players().size() != 2 || rescue_pending() || !g_refound_ready)
        return "Requires a two-player Tribal campaign with no pending rescue.";
    if (world::save_busy() || world::load_queued()) return "Wait for the current save or load to finish.";
    founders::Roster roster;
    void* home = player_tribe();
    if (!home || !founders::read(world::current_game_key(), roster) ||
        F<uint32_t>(home, 0x30) != roster.home().faction)
        return "The original host founder is not loaded.";
    void* conqueror = nullptr;
    void* survivor = nullptr;
    auto tribes = all_tribes();
    for (void* t : tribes) {
        if (F<uint32_t>(t, 0x30) == conqueror_id) conqueror = t;
        if (F<uint32_t>(t, 0x30) == (defeated_guest ? roster.home().faction : roster.guest().faction)) survivor = t;
    }
    if (defeated_guest) {
        home = nullptr;
        for (void* t : tribes) if (F<uint32_t>(t, 0x30) == roster.guest().faction) home = t;
        if (!home) return "The defeated guest founder is not loaded.";
    }
    uint32_t founder_faction = defeated_guest ? roster.guest().faction : roster.home().faction;
    if (!conqueror || !survivor || conqueror == home || conqueror == survivor || !intact_village(survivor))
        return "Requires an enemy conqueror and an intact surviving partner village.";
    auto old_people = members(home);
    for (void* c : old_people) if (game::health(c) > 0 && !F<uint8_t>(c, 0xB5E))
        return "Your partner still has a living villager and does not need a new village.";
    auto helpers = members(survivor);
    if (std::none_of(helpers.begin(), helpers.end(), living)) return "No surviving partner member.";
    void* hut = F<void*>(home, 0x368);
    void* mat = F<void*>(home, 0x260);
    void* species = F<void*>(home, 0x18CC);
    if (!hut || !mat || !species || !game::object_alive(hut) || !game::object_alive(mat))
        return "The native host buildings or species are missing.";
    uintptr_t selective = H->sym("SporeCoop::Tribe_RemoveSelectable");
    uintptr_t defeat = H->sym("SporeCoop::Tribe_SetDefeated");
    uintptr_t assign = H->sym("SporeCoop::IntrusivePtr_Assign");
    uintptr_t owner = H->sym("SporeCoop::GameData_SetOwner2");
    uintptr_t destroy = H->sym("Simulator::cGameNounManager::DestroyInstance");
    uintptr_t set_player = H->sym("Simulator::cGameNounManager::SetPlayerTribe");
    void* manager = game::noun_manager();
    if (!selective || !defeat || !assign || !owner || !destroy || !set_player || !manager || !a_SpawnNpcTribe)
        return "The native replacement methods are unavailable.";
    net::Vec3 c = tribe_pos(survivor), tangent{c.z, 0, -c.x};
    float radius = drive::speed(c), length = drive::speed(tangent);
    if (radius < 1 || length < 1) return "No valid test-site direction.";
    net::Vec3 q{c.x + tangent.x * 120 / length, c.y, c.z + tangent.z * 120 / length};
    float qr = drive::speed(q);
    skg::Vector3 pos{q.x * radius / qr, q.y * radius / qr, q.z * radius / qr};
    if (chosen) {
        if (!std::isfinite(chosen->x) || !std::isfinite(chosen->y) || !std::isfinite(chosen->z) ||
            std::abs(drive::speed(*chosen) - radius) > 30) return "The rebuilding site is not on this planet.";
        pos = {chosen->x, chosen->y, chosen->z};
    }
    for (void* t : tribes)
        if (net::dist(tribe_pos(t), {pos.x, pos.y, pos.z}) < 60)
            return "The host replacement test site is occupied.";
    // Prepare the new tribe before modifying the defeated settlement. A null
    // factory result must leave the native player and captured-site candidates
    // untouched. The outside-frame operation does not expose duplicate factions
    // to a simulation tick.
    g_refound_faction = founder_faction;
    void* replacement = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, 0, 3, 15, 0, species);
    g_refound_faction = 0;
    if (!replacement) return "Native replacement failed; the original settlement was retained.";
    auto new_people = members(replacement);
    if (F<uint32_t>(replacement, 0x30) != founder_faction || new_people.size() != 3 ||
        !intact_village(replacement) || std::any_of(new_people.begin(), new_people.end(), [](void* person) {
            return !living(person);
        })) {
        thiscall<void>(destroy, manager, replacement);
        return "Native replacement was incomplete; the original settlement was retained.";
    }
    // Reconstruct scalar campaign history through the native factory and segment
    // method. The old totem remains alive until replacement preparation succeeds.
    if (g_refound_campaign && !restore_founder_campaign(home, replacement)) {
        thiscall<void>(destroy, manager, replacement);
        return "Campaign history could not be restored; the original settlement was retained.";
    }
    vcall<int>(home, 0); vcall<int>(hut, 0); vcall<int>(mat, 0);
    for (void* body : old_people) {
        vcall<int>(body, 0);
        vcall<void>(home, 0x9C / 4, body, 1);
        thiscall<void>(selective, home, body);
        vcall<int>(body, 1);
    }
    thiscall<void>(defeat, home, true);
    vcall<void>(hut, 0x54 / 4, conqueror);
    vcall<void>(mat, 0x54 / 4, conqueror);
    if (F<void*>(hut, 0x2C) == home) thiscall<void>(owner, hut, conqueror);
    // Keep the native player getter valid during the synchronous replacement.
    // The old primary references must be detached before DestroyInstance,
    // which otherwise retires the captured buildings too (BUG-9017).
    if (!defeated_guest) thiscall<void>(set_player, manager, survivor);
    for (unsigned count = 0; count < 32; ++count) {
        void** begin = F<void**>(home, 0x36C);
        if (begin == F<void**>(home, 0x370)) break;
        vcall<void>(home, 0xB8 / 4, *begin);
    }
    thiscall<void>(assign, (uint8_t*)home + 0x368, nullptr);
    thiscall<void>(assign, (uint8_t*)home + 0x260, nullptr);
    thiscall<void>(destroy, manager, home);
    if (!defeated_guest) thiscall<void>(set_player, manager, replacement);
    if (g_refound_campaign)
        thiscall<void>(H->sym("SporeCoop::Tribe_UpdateHutModel"), replacement);
    vcall<int>(mat, 1); vcall<int>(hut, 1); vcall<int>(home, 1);
    char result[140];
    snprintf(result, sizeof result, "%s replacement %08X faction %08X: %u members, native player=%d",
             defeated_guest ? "Guest" : "Host", F<uint32_t>(replacement, 0x24), F<uint32_t>(replacement, 0x30),
             (unsigned)members(replacement).size(), player_tribe() == replacement);
    return result;
}

static bool g_rebuild_pending;
static std::string g_rebuild_world;
static bool queue_partner_rebuild(uint8_t from, uint16_t member)
{
    if (!active() || !is_host() || !village_orders() || g_rebuild_pending || !member ||
        !net::is_player(*C.sess, from) || C.sess->players().size() != 2) return false;
    auto found = g_hm_by_id.find(member);
    if (found == g_hm_by_id.end() || owner_of(found->second) != from || !living(found->second)) return false;
    g_rebuild_world = world::current_game_key();
    if (g_rebuild_world.empty()) return false;
    g_rebuild_pending = true;
    H->run_outside_frame([](void* data) {
        g_rebuild_pending = false;
        uint8_t from = (uint8_t)((uintptr_t)data >> 16);
        uint16_t member = (uint16_t)(uintptr_t)data;
        if (!active() || !is_host() || g_rebuild_world != world::current_game_key()) return;
        auto found = g_hm_by_id.find(member);
        if (found == g_hm_by_id.end() || owner_of(found->second) != from || !living(found->second)) return;
        const bool guest = from == 0;
        uint32_t faction = guest ? g_order_roster.guest().faction : g_order_roster.home().faction;
        void* defeated = nullptr;
        for (void* t : all_tribes()) if (F<uint32_t>(t, 0x30) == faction) defeated = t;
        void* hut = defeated ? F<void*>(defeated, 0x368) : nullptr;
        if (!hut || !game::object_alive(hut) || F<void*>(hut, 0x230) == defeated) {
            if (C.notice) C.notice("Your partner has no captured village to rebuild.");
            return;
        }
        net::Vec3 site = game::position(found->second);
        struct SiteCheck { net::Vec3 site; bool occupied = false; } check{site};
        game::for_each_object([](void* object, uint32_t noun, void* data) {
            auto& check = *static_cast<SiteCheck*>(data);
            net::Vec3 position;
            if (noun == 0x1E4DAAE && game::object_alive(object) && game::object_position(object, position) &&
                net::dist(position, check.site) < 60) check.occupied = true;
            return true;
        }, &check);
        if (check.occupied) {
            if (C.notice) C.notice("Move the selected helper at least 60 metres from existing village huts, then rebuild.");
            return;
        }
        auto result = rebuild_founder(F<uint32_t>(hut, 0x30), guest, &site);
        if (!result.starts_with("Host replacement ") && !result.starts_with("Guest replacement ")) {
            if (C.notice) C.notice(result);
            return;
        }
        std::string error;
        if (C.notice) C.notice(world::checkpoint_reload(error) ?
            "Your partner's original species has a new village. Saving and refreshing both worlds." :
            "Village rebuilt, but the world refresh failed: " + error);
    }, (void*)(((uintptr_t)from << 16) | member));
    return true;
}

static bool g_capture_pending;
static std::string g_capture_world;
static bool queue_founder_capture(bool guest, uint32_t attacker)
{
    if (g_capture_pending) return true;
    if (!attacker || g_order_roster.contains_faction(attacker))
        return false;
    bool enemy = false;
    for (void* t : all_tribes())
        if (F<uint32_t>(t, 0x30) == attacker && intact_village(t)) enemy = true;
    if (!enemy) return false; // Wildlife or an unavailable conqueror uses food-loss respawn.
    if (!H->sym("SporeCoop::Tribe_SetDefeated") || !H->sym("SporeCoop::GameData_SetOwner2")) return false;
    void* victim_tribe = founder_village(guest);
    void* mat = victim_tribe ? F<void*>(victim_tribe, 0x260) : nullptr;
    if (!mat || !game::object_alive(mat)) return false;
    if (world::save_busy() || world::load_queued()) return true;
    g_capture_world = world::current_game_key();
    if (g_capture_world.empty()) return false;
    g_capture_pending = true;
    H->run_outside_frame([](void* value) {
        bool guest = (uintptr_t)value != 0;
        g_capture_pending = false;
        if (!active() || !is_host() || g_capture_world != world::current_game_key()) return;
        auto& rescue = g_founder_rescues[guest ? 1 : 0];
        void* tribe = founder_village(guest);
        void* conqueror = nullptr;
        for (void* t : all_tribes())
            if (F<uint32_t>(t, 0x30) == rescue.attacker_faction && intact_village(t)) conqueror = t;
        auto found = g_hm_by_id.find(rescue.member);
        uintptr_t defeat = H->sym("SporeCoop::Tribe_SetDefeated");
        uintptr_t owner = H->sym("SporeCoop::GameData_SetOwner2");
        if (!rescue.until || !tribe || !conqueror || !intact_village(tribe) ||
            found == g_hm_by_id.end() || !defeat || !owner) return;
        void* body = found->second;
        auto population = members(tribe);
        if (std::find(population.begin(), population.end(), body) == population.end()) return;
        for (void* person : population) if (person != body && living(person)) return;
        void* hut = F<void*>(tribe, 0x368);
        void* mat = F<void*>(tribe, 0x260);
        if (!mat || !game::object_alive(mat)) return;
        // Keep the original native tribe and its primary references while it
        // waits for a helper. This captured state survives native save/load.
        thiscall<void>(defeat, tribe, true);
        vcall<void>(hut, 0x54 / 4, conqueror);
        vcall<void>(mat, 0x54 / 4, conqueror);
        if (F<void*>(hut, 0x2C) == tribe) thiscall<void>(owner, hut, conqueror);
        uint32_t attacker = rescue.attacker_faction;
        bool village_down = rescue.hut_down;
        if (village_down) F<float>(hut, 0x158) = std::max(1.f, F<float>(hut, 0x14C) * 0.4f);
        rescue = {};
        skg::Vector3 direction{};
        auto casualties = village_down ? population : std::vector<void*>{body};
        for (void* person : casualties) vcall<int>(person, 0);
        for (void* person : casualties) {
            if (game::health(person) > 0 && !F<uint8_t>(person, 0xB5E))
                o_CitizenTD((uint8_t*)person + 0x5A8, 1e6f, attacker, 0, &direction, nullptr);
            vcall<int>(person, 1);
        }
        std::string error;
        if (!world::checkpoint_reload(error)) {
            if (C.notice) C.notice("Village captured; world refresh failed: " + error);
        } else if (C.notice) C.notice("Village captured. Your partner can help rebuild your original species at a new site.");
    }, (void*)(uintptr_t)(guest ? 1 : 0));
    return true;
}

static void bonus_food(int n)
{
    void* pt = player_tribe();
    if (!pt || !a_AddFood)
        return;
    thiscall<void>(a_AddFood, pt, n);
    if (a_FoodFx)
        thiscall<void>(a_FoodFx, pt, (float)n);
}

// heal tick 0xDB9B20 (cdecl: arg1 healer, *arg6 patient): a full heal of a partner's member
using HealTick_t = bool(SK_CDECL*)(void*, void*, void*, void*, void*, void**, void*, float, void*, void*, void*, void*);
static HealTick_t o_HealTick;
static bool SK_CDECL h_HealTick(void* healer, void* a2, void* a3, void* a4, void* a5, void** target, void* a7, float a8,
                                void* a9, void* a10, void* a11, void* a12)
{
    void* pat = nullptr;
    float hp0 = 0, mx = 0;
    if (is_host() && active() && target && a_PatientOf) {
        pat = skg::cdecl_call<void*>(a_PatientOf, *target);
        if (pat) {
            hp0 = F<float>(pat, 0x5E0);
            mx = F<float>(pat, 0x5D4);
        }
    }
    bool r = o_HealTick(healer, a2, a3, a4, a5, target, a7, a8, a9, a10, a11, a12);
    if (pat && mx > 0 && hp0 < mx && F<float>(pat, 0x5E0) >= mx) {
        uint8_t ho = owner_of(healer), po = owner_of(pat);
        if (ho < 0xFE && po < 0xFE && ho != po) {
            bonus_food(5);
            if (g_heals++ % 4 == 0 && C.notice)
                C.notice(C.sess->name_of(ho) + " healed " + C.sess->name_of(po) + "'s tribe member: +5 food");
        }
    }
    return r;
}

// duet: performances (BT node 0xB46E8803) of both players for the same tribe
static void duet_tick(double now)
{
    if (now - g_last_duet < 0.5 || !a_SessionGet)
        return;
    g_last_duet = now;
    void* pt = player_tribe();
    if (!pt)
        return;
    std::unordered_map<void*, std::unordered_set<uint8_t>> perf; // target tribe -> owners
    std::unordered_map<void*, void*> ui_of;
    for (void* c : members(pt)) {
        uint8_t o = owner_of(c);
        uint8_t* ai = o < 0xFE ? F<uint8_t*>(c, 0xB4C) : nullptr;
        if (!ai || IsBadReadPtr(ai, 0x604) || F<uint32_t>(ai, 0x1D8) != 0xB46E8803u)
            continue;
        uint8_t* blk = F<uint8_t*>(ai, 0x600);
        if (!blk || IsBadReadPtr(blk, 0x1AC))
            continue;
        uint8_t* ss = skg::cdecl_call<uint8_t*>(a_SessionGet, F<int>(blk, 0x1A8));
        if (!ss || IsBadReadPtr(ss, 0xF0))
            continue;
        void* target = F<void*>(ss, 0xC);
        if (!target)
            continue;
        perf[target].insert(o);
        ui_of[target] = F<void*>(ss, 0xE8);
    }
    for (auto& [target, owners] : perf) {
        if (owners.size() < 2)
            continue;
        void* ui = ui_of[target];
        if (ui && !IsBadReadPtr(ui, 0x6C)) {
            float& p = F<float>(ui, 0x68);
            if (p < 0.97f)
                p = std::min(0.97f, p + 0.04f); // +8% a second on top of the performance
            g_duet_ticks++;
        }
        if (now - g_duet_told > 60 && C.notice) {
            g_duet_told = now;
            C.notice("duet: friendship grows faster");
        }
    }
}

// a herd of ours for epics: the epic archetype, no nest, not the avatar's, target size 0 (the
// game's own herds have a target size). prof: of this species (null: any). Checked in the object list.
static bool is_epic_herd_of_ours(void* o)
{
    return game::object_alive(o) && skg::readable(o, 0x21C) && F<uint32_t>(o, 0x88) == kEpicArchetype &&
           !game::herd_nest(o) && !F<uint8_t>(o, 0x84) && F<int32_t>(o, 0xF4) == 0;
}

static void* epic_herd_find(void* prof)
{
    struct Q {
        void* prof;
        void* found = nullptr;
    } q{prof};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (noun != game::kHerdNoun || !is_epic_herd_of_ours(o) || (q->prof && game::herd_species(o) != q->prof))
                return true;
            q->found = o;
            return false;
        },
        &q);
    return q.found;
}

static bool herd_listed(void* h)
{
    struct Q {
        void* want;
        bool found = false;
    } q{h};
    if (!h)
        return false;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (o == q->want && noun == game::kHerdNoun)
                q->found = true;
            return !q->found;
        },
        &q);
    return q.found;
}

// the epic is gone (killed, removed before a save, the session ended): its herd spawns nothing.
// Target size 0 holds; the game enables the herd again after its member died (lane 7: disabled at
// the kill, enabled again within seconds; a disable later stayed), so `enabled` is best effort.
static void epic_herd_release()
{
    if (g_fix_epic_herd && g_epic_herd && herd_listed(g_epic_herd))
        game::herd_quiet(g_epic_herd);
    g_epic_herd = nullptr;
}

// epic: a giant of a species on the planet comes to the village (host)
static bool spawn_epic()
{
    void* pt = player_tribe();
    if (!pt || !a_CreateHerd || !a_HerdEnable || !a_CreatureCreate || g_epic)
        return false;
    net::Vec3 tp = tribe_pos(pt);
    // a species already on the planet (the guests have its model): the nearest animal
    void* prof = nullptr;
    struct Pick {
        net::Vec3 tp;
        void* prof = nullptr;
        float bd = 1e9f;
    } pick{tp};
    game::for_each_object(
        [](void* a, uint32_t noun, void* u) {
            auto* pk = (Pick*)u;
            if (noun != 0x18EB45E || !game::object_alive(a))
                return true;
            float d = net::dist(game::position(a), pk->tp);
            if (d < pk->bd && game::species_profile(a)) {
                pk->bd = d;
                pk->prof = game::species_profile(a);
            }
            return true;
        },
        &pick);
    prof = pick.prof;
    if (!prof)
        return false;
    float len = std::sqrt(tp.x * tp.x + tp.y * tp.y + tp.z * tp.z);
    // 60 m from the village, along a tangent of the planet
    net::Vec3 up = len > 1 ? net::Vec3{tp.x / len, tp.y / len, tp.z / len} : net::Vec3{0, 0, 1};
    net::Vec3 side = std::fabs(up.z) < 0.9f ? net::Vec3{up.y, -up.x, 0} : net::Vec3{0, up.z, -up.y};
    float sl = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
    net::Vec3 p{tp.x + side.x / sl * 60.f, tp.y + side.y / sl * 60.f, tp.z + side.z / sl * 60.f};
    float pp[3] = {p.x, p.y, p.z};
    void* nmgr = game::noun_manager();
    // BUG-0121: one quiet epic herd of the species is reused (also one a save brought back), not a
    // new herd per epic that stayed in the world and in every save
    void* herd = g_fix_epic_herd ? epic_herd_find(prof) : nullptr;
    if (herd)
        g_epic_herd_reused++;
    else
        herd = nmgr ? thiscall<void*>(a_CreateHerd, nmgr, pp, prof, 0, false, 0, false) : nullptr;
    if (!herd)
        return false;
    F<uint32_t>(herd, 0x88) = kEpicArchetype; // an archetype with isEpic = 1
    F<uint32_t>(herd, 0xF0) = 0;
    if (g_fix_epic_herd)
        F<float>(herd, 0x34) = pp[0], F<float>(herd, 0x38) = pp[1], F<float>(herd, 0x3C) = pp[2]; // its home
    thiscall<void>(a_HerdEnable, herd, true);
    void* c = skg::cdecl_call<void*>(a_CreatureCreate, pp, prof, 1, herd, false, false);
    if (g_fix_epic_herd)
        F<int32_t>(herd, 0xF4) = 0; // mTargetHerdSize: the game adds no more epics to it
    if (!c) {
        if (g_fix_epic_herd)
            game::herd_quiet(herd);
        return false;
    }
    g_epic = c;
    g_epic_herd = herd;
    g_epic_hitters.clear();
    g_epics++;
    if (C.notice)
        C.notice("an epic creature comes! together: 80 food");
    return true;
}

// the epic is still an animal of the game's object list (BUG-0171: object_alive reads the
// creature's own memory and cannot tell a freed one; g_epic is a raw pointer kept for minutes)
static bool epic_listed()
{
    struct Q {
        void* want;
        bool found = false;
    } q{g_epic};
    if (!g_epic)
        return false;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (o == q->want)
                q->found = true;
            return !q->found;
        },
        &q);
    return q.found;
}

static void epic_tick(double now)
{
    if (g_epic && (!epic_listed() || !game::object_alive(g_epic) || game::is_dead(g_epic))) {
        int food = g_epic_hitters.size() >= 2 ? 80 : 30;
        bonus_food(food);
        g_epic_kills++;
        if (C.notice)
            C.notice(std::string("epic creature down: +") + std::to_string(food) + " food");
        g_epic = nullptr;
        epic_herd_release();
        g_epic_next = now + 600;
    }
    if (!g_epic_next)
        g_epic_next = now + 480; // the first one after 8 minutes together
    if (!g_epic && now > g_epic_next && C.sess->players().size() >= 2) {
        g_epic_next = now + 600;
        spawn_epic();
    }
}

// the epic's hitters (animals go through the game's creature damage filter)
void note_animal_hit(void* animal, void* attacker_creature)
{
    if (animal && animal == g_epic && attacker_creature && g_hm.count(attacker_creature)) {
        uint8_t o = g_hm[attacker_creature].owner;
        if (!g_fix_epic_players || (o < 0xFE && net::is_player(*C.sess, o)))
            g_epic_hitters.insert(o);
    }
}


// BUG-0340: every player's chief (the player's character in Tribe) could stand on another's: the
// members the game spawns for a tribe (its hut, the co-op split of new members, a rejoin) appear at
// one spot, and two chiefs standing in each other blocked each other. On the host, two chiefs that
// both stand still less than kChiefApart apart for a second are parted: the later owner's chief goes
// to its own spot on a ring round the village (kChiefRing m from the tribe's centre, a quarter turn
// per owner, on this game's ground). The guests follow through the snapshots.
// `coop.fix tribe.spread_chiefs off`: chiefs stay where they are.
static bool g_spread_fix = true;
COOP_FIX("tribe.spread_chiefs", "BUG-0340", g_spread_fix, "tribe: two players' chiefs standing in each other are parted onto a ring round the village");
constexpr float kChiefApart = 1.5f, kChiefRing = 12.f;
static std::map<std::pair<uint8_t, uint8_t>, double> g_stack_since;
static uint32_t g_spread_moves;

static net::Vec3 ring_spot(const net::Vec3& centre, uint8_t owner)
{
    float r = std::sqrt(centre.x * centre.x + centre.y * centre.y + centre.z * centre.z);
    if (r < 1.f)
        return centre;
    net::Vec3 up{centre.x / r, centre.y / r, centre.z / r};
    net::Vec3 a = std::fabs(up.y) < 0.9f ? net::Vec3{0, 1, 0} : net::Vec3{1, 0, 0};
    net::Vec3 e1{up.y * a.z - up.z * a.y, up.z * a.x - up.x * a.z, up.x * a.y - up.y * a.x};
    float l = std::sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
    e1 = {e1.x / l, e1.y / l, e1.z / l};
    net::Vec3 e2{up.y * e1.z - up.z * e1.y, up.z * e1.x - up.x * e1.z, up.x * e1.y - up.y * e1.x};
    float t = 6.2831853f * (float)(owner % 8) / 4.f + (owner >= 4 ? 0.785f : 0.f);
    float cs = std::cos(t) * kChiefRing, sn = std::sin(t) * kChiefRing;
    net::Vec3 p{centre.x + e1.x * cs + e2.x * sn, centre.y + e1.y * cs + e2.y * sn, centre.z + e1.z * cs + e2.z * sn};
    float g = game::ground_radius(p), pr = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (g > 1.f && pr > 1.f)
        p = {p.x * g / pr, p.y * g / pr, p.z * g / pr};
    return p;
}

// a chief put on its spot also gets a walk order there (the game's own order, as a player's walk on
// the host): a member only put there walked back to where it stood before (seen: 9 m in 6 s)
static void put_chief(void* c, const net::Vec3& spot)
{
    game::teleport(c, spot, game::orientation(c));
    void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
    float p[3] = {spot.x, spot.y, spot.z};
    if (rec)
        thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)p);
}

static void spread_chiefs(double now)
{
    void* pt = player_tribe();
    if (!g_spread_fix || !pt)
        return;
    net::Vec3 centre = tribe_pos(pt);
    std::vector<std::pair<uint8_t, void*>> ch(g_chief.begin(), g_chief.end());
    std::sort(ch.begin(), ch.end());
    for (size_t i = 0; i < ch.size(); i++)
        for (size_t j = i + 1; j < ch.size(); j++) {
            auto key = std::make_pair(ch[i].first, ch[j].first);
            void *a = ch[i].second, *b = ch[j].second;
            bool stacked = game::object_alive(a) && game::object_alive(b) &&
                           net::dist(game::position(a), game::position(b)) < kChiefApart &&
                           drive::speed(game::velocity(a)) < 0.3f && drive::speed(game::velocity(b)) < 0.3f;
            if (!stacked) {
                g_stack_since.erase(key);
                continue;
            }
            auto it = g_stack_since.emplace(key, now).first;
            if (now - it->second < 1.0)
                continue;
            put_chief(b, ring_spot(centre, ch[j].first));
            g_spread_moves++;
            g_stack_since.erase(key);
            H->log(M, SK_INFO, "tribe: %s's chief stood in %s's: put on its own spot round the village",
                   C.sess->name_of(ch[j].first).c_str(), C.sess->name_of(ch[i].first).c_str());
        }
}

static void place_chiefs(double now)
{
    void* pt = player_tribe();
    if (!g_place_fix || !pt || g_place_due == 0 || now < g_place_due)
        return;
    if (now > g_place_until) {
        g_place_due = 0;
        return;
    }
    g_place_due = now + 2.0; // looked at again every 2 s inside the window (a guest's members come late)
    net::Vec3 centre = tribe_pos(pt);
    std::vector<std::pair<uint8_t, void*>> ch(g_chief.begin(), g_chief.end());
    std::sort(ch.begin(), ch.end());
    for (size_t j = 1; j < ch.size(); j++) {
        void* b = ch[j].second;
        if (!game::object_alive(b))
            continue;
        bool close = false;
        for (size_t i = 0; i < ch.size() && !close; i++) {
            void* a = ch[i].second;
            if (i == j || !game::object_alive(a))
                continue;
            close = net::dist(game::position(a), game::position(b)) <
                    game::bounding_radius(a) + game::bounding_radius(b) + 1.f;
        }
        if (!close)
            continue;
        if (g_placed_now.count(b))
            continue; // once per arrival
        g_placed_now.insert(b);
        put_chief(b, ring_spot(centre, ch[j].first));
        g_placed++;
        H->log(M, SK_INFO, "tribe: %s's chief stood next to another one at the arrival: put on its own spot round the village",
               C.sess->name_of(ch[j].first).c_str());
    }
}

static void coop_tick(double now)
{
    chiefs_tick();
    if (is_host()) {
        spread_chiefs(now);
        place_chiefs(now);
        duet_tick(now);
        epic_tick(now);
    }
}

static void coop_init()
{
    a_CitizenTD = H->sym("Simulator::cCreatureCitizen::TakeDamage");
    a_HealTick = H->sym("SporeCoop::BT_HealTick");
    a_PatientOf = H->sym("SporeCoop::BT_ValidTarget");
    a_AddFood = H->sym("SporeCoop::cTribe_AddFood");
    a_FoodFx = H->sym("Simulator::cTribe::UpdateFoodVisuals");
    a_SessionGet = H->sym("SporeCoop::SocialSession_Get");
    a_CreateHerd = H->sym("Simulator::cGameNounManager::CreateHerd");
    a_HerdEnable = H->sym("Simulator::cHerd::SetEnabled");
    a_CreatureCreate = H->sym("Simulator::cCreatureAnimal::Create");
    spaceext::on_save(before_save); // space_allies' save hook (the one hook on SaveGame)
    if (a_CitizenTD)
        H->hook_create(M, (void*)a_CitizenTD, (void*)h_CitizenTD, (void**)&o_CitizenTD);
    if (uintptr_t hut_damage = H->sym("SporeCoop::cTribeHut_TakeDamage"))
        H->hook_create(M, (void*)hut_damage, (void*)h_HutTD, (void**)&o_HutTD);
    if (a_HealTick && a_PatientOf)
        H->hook_create(M, (void*)a_HealTick, (void*)h_HealTick, (void**)&o_HealTick);
    H->log(M, SK_INFO, "tribe co-op: citizen damage %s, heal %s, duet %s, epic %s", a_CitizenTD ? "on" : "off",
           a_HealTick && a_PatientOf ? "on" : "off", a_SessionGet ? "on" : "off",
           a_CreateHerd && a_HerdEnable && a_CreatureCreate ? "on" : "off");
}

// a member that changed hands (a gift) stayed selected on the giver's machine, so the giver could
// still order the partner's member (play_tribe.py): drop members of others from the own selection
static uint32_t g_unselected;
static void drop_foreign_selection()
{
    void* pt = player_tribe();
    if (!pt)
        return;
    uint8_t me = C.sess->my_id();
    for (void* c : members(pt)) {
        uint8_t o = owner_of(c);
        if (o < 0xFE && o != me && F<uint8_t>(c, kSelectedOff)) {
            vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, 0); // SetSelected(false)
            g_unselected++;
        }
    }
}

static std::vector<uint16_t> guest_village_ids(bool selected_only)
{
    std::vector<uint16_t> ids;
    void* home = player_tribe();
    for (const auto& [id, m] : g_gm)
        if (home && m.c && m.owner == C.sess->my_id() && game::object_alive(m.c) && F<void*>(m.c, kOwnerTribeOff) != home &&
            game::health(m.c) > 0 && !F<uint8_t>(m.c, 0xB5E) && (!selected_only || F<uint8_t>(m.c, kSelectedOff)))
            ids.push_back(id);
    return ids;
}

static const char* request_performance(const Target& target)
{
    const auto ids = guest_village_ids(true);
    if (ids.empty()) return "Select your villagers before choosing Perform.";
    if (g_guest_orders_held) return "Village orders are paused while the campaign synchronizes.";
    if (!send_cmd(CMD_ACTION, ids, 26, target, {})) return "Could not send the performance order to the host.";
    return "Performance requested.";
}

static void march_click(double now)
{
    if (!g_march_until) return;
    if (now >= g_march_until || coop_ui::is_open() || local_play()) {
        g_march_until = 0;
        return;
    }
    if (mouse::clicks() == g_march_click) return;
    g_march_until = 0;
    int x, y;
    mouse::last_click(x, y);
    net::Vec3 pos;
    if (g_build_kind) {
        int kind = g_build_kind; g_build_kind = 0;
        auto ids = guest_village_ids();
        if (!ids.empty() && modes::planet_at_pixel(x, y, pos)) send_cmd(CMD_BUILD, {ids.front()}, kind, {}, pos);
        else if (C.feed) C.feed("No village ground under that click. Choose Build again to retry.");
        return;
    }
    if (g_attack_click) {
        g_attack_click = false;
        net::Vec3 camera, direction;
        if (!modes::camera_ray_at_pixel(x, y, camera, direction)) return;
        if (!g_target_ray && !modes::planet_at_pixel(x, y, pos)) return;
        if (g_raid_click || g_equip_click || g_gift_click) {
            struct Pick { int x, y; net::Vec3 surface, camera; void* object = nullptr; float distance = 24.f * 24.f; } pick{x, y, pos, camera};
            game::for_each_object([](void* object, uint32_t noun, void* data) {
                auto& pick = *(Pick*)data;
                if ((noun != kHutNoun && noun != kToolNoun) || !game::object_alive(object) || F<float>(object, 0x158) <= 0)
                    return true;
                if (g_equip_click && (noun != kToolNoun || F<uint32_t>(object, 0x1FC) < 1 || F<uint32_t>(object, 0x1FC) > 9)) return true;
                if (g_gift_click && (noun != kHutNoun || F<uint32_t>(object, 0x30) == g_order_roster.home().faction ||
                    F<uint32_t>(object, 0x30) == g_order_roster.guest().faction)) return true;
                net::Vec3 point;
                if (!game::object_position(object, point) || (g_target_ray ? !target_near_side(pick.camera, point) :
                    net::dist(point, pick.surface) > 25.f)) return true;
                float world[3] = {point.x, point.y, point.z}, screen[2];
                if (!H->world_to_screen(world, screen)) return true;
                float d = (screen[0] - pick.x) * (screen[0] - pick.x) + (screen[1] - pick.y) * (screen[1] - pick.y);
                if (std::isfinite(d) && d < pick.distance) { pick.distance = d; pick.object = object; }
                return true;
            }, &pick);
            if (pick.object) {
                void* target = g_gift_click ? vcall<void*>(pick.object, 0x58 / 4) : pick.object;
                if (target) send_cmd(CMD_ACTION, guest_village_ids(true), g_gift_click ? 9 : g_equip_click ? 3 : 15, encode_target(target), {});
            }
            else if (C.feed) C.feed(g_gift_click ? "No other tribe's hut under that click. Choose Gift food again to retry." :
                                  g_equip_click ? "No tool rack under that click. Choose Equip again to retry." :
                                  "No hut or tool building under that click. Choose Raid again to retry.");
            return;
        }
        uint16_t target = 0;
        float distance = 24.f * 24.f;
        for (const auto& [id, member] : g_gm) {
            if (!member.c || (g_heal_click ? member.owner >= 0xFE : member.owner < 0xFE) ||
                !game::object_alive(member.c) || game::health(member.c) <= 0 ||
                F<uint8_t>(member.c, 0xB5E)) continue;
            if (g_heal_click && (game::health(member.c) >= game::max_health(member.c) || founder_downed(member.c))) continue;
            net::Vec3 point = game::position(member.c);
            if (g_target_ray ? !target_near_side(camera, point) : net::dist(point, pos) > 25.f) continue;
            float world[3] = {point.x, point.y, point.z}, screen[2];
            if (!H->world_to_screen(world, screen)) continue;
            float d = (screen[0] - x) * (screen[0] - x) + (screen[1] - y) * (screen[1] - y);
            if (std::isfinite(d) && d < distance) { distance = d; target = id; }
        }
        if (target) {
            Target encoded; encoded.kind = 1; encoded.id = target;
            if (g_perform_click) {
                const char* result = request_performance(encoded);
                if (C.feed) C.feed(result);
            } else send_cmd(CMD_ACTION, guest_village_ids(true), g_heal_click ? 5 : 7, encoded, {});
        } else if (C.feed) C.feed(g_perform_click ? "No NPC villager under that click. Choose Perform again to retry." :
                                 g_heal_click ? "No friendly villager under that click. Choose Heal again to retry." :
                                 "No enemy villager under that click. Choose Attack again to retry.");
        return;
    }
    if (modes::planet_at_pixel(x, y, pos)) send_cmd(CMD_WALK, guest_village_ids(), 4, {}, pos);
}

static void outfit_tick(double now)
{
    if (is_host() && (world::time_paused() || world::cinematic_playing() || world::load_queued())) {
        g_outfit_settled = 0;
        return;
    }
    if (g_outfit_queued || now < g_outfit_poll || world::load_queued() || world::save_busy()) return;
    g_outfit_poll = now + 1;
    if (is_guest()) {
        if (!g_outfit.token || !g_outfit.ready || g_outfit.model || g_outfit.world != modes::current_world()) return;
        uint32_t model = g_outfit.saved ? creations::player_key(C.sess->my_id(), creations::kCreature).instance : g_outfit.previous;
        if (!model || (g_outfit.saved && model == g_outfit.previous)) return; // own bundle has not been published yet
        net::Writer w;
        w.u8(1); w.u32(g_outfit.world); w.u32(g_outfit.token); w.u32(g_outfit.previous); w.u32(model);
        if (C.sess->send_reliable_to(0, net::MSG_TRIBE_OUTFIT, w)) g_outfit.model = model;
        return;
    }
    if (!is_host() || !g_order_roster.guest().faction || world::cinematic_playing() || world::time_paused()) return;
    // Closing the native planner resumes time before all outfit callbacks have
    // updated the host citizens. Let those deferred native changes settle before
    // taking a checkpoint (the same five-second model gate
    // used by Creature edits), rather than racing a second save against a reload.
    if (!g_outfit_settled) g_outfit_settled = now;
    if (now - g_outfit_settled < 5) return;
    if (g_outfit.token && !outfits_pending()) g_outfit = {};
    if (g_outfit.token && (!g_outfit.ready || !g_outfit.host_entered)) return;
    void* home = player_tribe();
    void* guest = founder_village(true);
    if (!home || !guest) return;
    uint32_t home_model = tribe_info(home).key.instance;
    uint32_t guest_model = tribe_info(guest).key.instance;
    uint32_t target = g_outfit.token && g_outfit.model != g_outfit.previous ? g_outfit.model : g_outfits.current(1);
    if (!target) target = guest_model;
    if (!g_outfit.token && founder_model(0, home_model) && target == guest_model) return;
    g_outfit_queued = true;
    H->run_outside_frame([](void* context) {
        uint64_t epoch = *(uint64_t*)context; delete (uint64_t*)context;
        g_outfit_queued = false;
        if (epoch != g_campaign_epoch || !active() || !is_host() || world::load_queued()) return;
        static std::string last_error;
        auto fail = [&](const std::string& message) {
            if (message != last_error && C.notice) C.notice("Outfit not applied: " + message);
            last_error = message;
        };
        auto planet = world::current_game_key();
        std::string error;
        if (!founders::read_outfits(planet, g_outfits, error)) { fail(error); return; }
        void* home = player_tribe();
        void* guest = founder_village(true);
        if (!home || !guest) return;
        uint32_t home_model = tribe_info(home).key.instance;
        uint32_t old_guest = tribe_info(guest).key.instance;
        uint32_t target = g_outfit.token && g_outfit.model != g_outfit.previous ? g_outfit.model : g_outfits.current(1);
        if (!home_model || !target || !founder_model(1, old_guest)) return;
        if (g_outfit.token && (!g_outfit.ready || !g_outfit.host_entered || !outfits_pending())) return;
        if (g_outfit.token && target != g_outfit.previous &&
            (creations::player_key(g_outfit.owner, creations::kCreature).instance != target ||
             (g_outfit_recovery ? !g_outfits.contains(1, g_outfit.previous) :
                                 g_outfit.previous != g_outfits.current(1)))) return;
        bool changed = false;
        // The native host editor already applied this model. Keep its original
        // founding identity, recording only the newly saved appearance.
        if (!g_outfits.contains(0, home_model)) {
            if (!founders::record_outfit(planet, 0, g_outfits.current(0), home_model, error) ||
                !founders::read_outfits(planet, g_outfits, error)) { fail(error); return; }
            changed = true;
        }
        if (!g_outfits.contains(1, target)) {
            if (!g_outfit.token || !creations::archive_guest_model(g_outfit.owner, planet, error) ||
                !founders::record_outfit(planet, 1, g_outfits.current(1), target, error) ||
                !founders::read_outfits(planet, g_outfits, error)) { fail(error); return; }
            changed = true;
        }
        if (old_guest != target) {
            uintptr_t set_tribe = H->sym("SporeCoop::Tribe_SetSpeciesProfile");
            uintptr_t set_member = H->sym("Simulator::cCreatureBase::SetSpeciesProfile");
            void* profile = game::species_by_key({target, 0x2B978C46, 0x40626200});
            auto people = members(guest);
            if (!profile || !set_tribe || !set_member || people.empty()) { fail("The saved guest model is not ready."); return; }
            for (void* person : people)
                if (!game::object_alive(person) || !founder_model(1, game::species_key(person).instance)) {
                    fail("Village membership changed while applying the outfit."); return;
                }
            // Species profiles are owned by the species manager. The native
            // cTribe Save derives1904 keys from this raw14-profile array.
            for (unsigned i = 0; i < 14; ++i) {
                void* old = F<void*>(guest, 0x18CC + 4*i);
                if (old && founder_model(1, F<uint32_t>(old, 0x504)))
                    thiscall<void>(set_tribe, guest, profile, (int)i);
            }
            for (void* person : people) thiscall<void>(set_member, person, profile);
            changed = true;
        }
        if (g_outfit.token) {
            net::Writer w;
            w.u8(2); w.u32(g_outfit.world); w.u32(g_outfit.token); w.u32(g_outfit.previous); w.u32(g_outfit.model);
            if (!C.sess->send_reliable_to(g_outfit.owner, net::MSG_TRIBE_OUTFIT, w)) return;
        }
        g_outfit = {};
        last_error.clear();
        if (changed) {
            H->log(M, SK_INFO, "tribe: founder outfits saved home=%08X guest=%08X", home_model, target);
            if (!world::checkpoint_reload(error)) fail("Native outfit applied; checkpoint could not start: " + error);
        }
    }, new uint64_t(g_campaign_epoch));
}

void tick(double now)
{
    if (!active())
        return;
    auto_founders_tick(now);
    drop_foreign_selection();
    if (is_host()) {
        founder_rescue_tick(now);
        host_tick(now);
        combat_tick(now);
        load_six_trial();
        outfit_tick(now);
        shared_hut_upgrade_tick(now);
        objective_backfill_tick(now);
        host_buildings(now);
        host_equipment(now);
        host_tools(now);
        host_relationships(now);
        host_npc_food(now);
        host_performances(now);
        resume_performance(now);
        host_campaign_tick(now);
    }
    else if (is_guest()) {
        outfit_tick(now);
        guest_tick(now);
        march_click(now);
    }
    coop_tick(now);
}

static uint32_t g_rx, g_rx_skipped, g_resets;

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type == net::MSG_TRIBE_COMBAT) {
        if (!active() || !is_guest() || from != 0 || r.u32() != modes::current_world()) return;
        uint16_t count = r.u16();
        if (!r.ok() || count > 2048 || r.left() != size_t(count)*6) return;
        std::map<uint16_t,float> next;
        for (unsigned i = 0; i < count; ++i) {
            auto id = r.u16();
            auto max_hp = r.f32();
            if (!id || !(max_hp > 0) || !std::isfinite(max_hp) || !next.emplace(id,max_hp).second) return;
        }
        g_remote_enemy_max = std::move(next);
        return;
    }

    if (!g_ok)
        return;
    if (type == net::MSG_TRIBE_OUTFIT) { receive_outfit(from, r); return; }
    if (type == net::MSG_TRIBE_STATE) {
        g_rx++;
        if (!is_guest() || game::mode_id() != modes::kTribe)
            g_rx_skipped++;
    }
    if (type == net::MSG_TRIBE_STATE && from == 0 && is_guest() && game::mode_id() == modes::kTribe)
        guest_state(r);
    else if (type == net::MSG_TRIBE_BUILDINGS && from == 0 && is_guest() && game::mode_id() == modes::kTribe && !local_play())
        guest_buildings(r);
    else if (type == net::MSG_TRIBE_EQUIPMENT && from == 0 && is_guest() && game::mode_id() == modes::kTribe && !local_play())
        guest_equipment(r);
    else if (type == net::MSG_TRIBE_TOOLS && from == 0 && is_guest() && game::mode_id() == modes::kTribe && !local_play())
        guest_tools(r);
    else if (type == net::MSG_TRIBE_RELATIONSHIPS && from == 0 && is_guest() && game::mode_id() == modes::kTribe && !local_play())
        guest_relationships(r);
    else if (type == net::MSG_TRIBE_FOOD && from == 0 && is_guest() && game::mode_id() == modes::kTribe && !local_play())
        guest_npc_food(r);
    else if (type == net::MSG_TRIBE_PERFORMANCE && from == 0 && is_guest() && game::mode_id() == modes::kTribe && !local_play())
        guest_performances(r);
    else if (type == net::MSG_TRIBE_CMD && is_host() && game::mode_id() == modes::kTribe)
        host_command(from, r);
}

// BUG-0217: forget the co-op mechanics' pointers of a world that is gone (no game call: the
// objects are freed). The epic and its herd are no longer ours to destroy or quiet.
// tribe.chiefs gates only the chief and pincer part; the epic part has its own switch
// (tribe.epic_herd) and runs whatever tribe.chiefs says (BUG-0214: one bug per switch)
static void forget_world_tables()
{
    g_combat_factions.clear();
    g_remote_enemy_max.clear();
    g_sent_enemy_max.clear();
    g_combat_poll = g_combat_resend = 0;
    g_combat_world = 0;

    g_outfit = {};
    g_outfits = {};
    g_outfit_queued = false;
    g_outfit_poll = 0;
    g_outfit_settled = 0;
    g_hut_upgrade_queued = false;
    g_hut_upgrade_poll = 0;
    g_objective_backfill_queued = false;
    g_objective_backfill_poll = 0;
    g_auto_found_at = 0;
    g_auto_found_queued = g_auto_found_refresh = false;
    g_auto_found_manual_off = false;
    g_auto_found_created = {};
    g_auto_found_notice.clear();
    g_building_received.clear();
    g_equipment_received.clear();
    g_tools_received.clear();
    g_tool_unlocks = 0;
    g_relationships_received.clear();
    g_local_factions.clear();
    g_faction_world = 0;
    g_relationships_sent = 0;
    g_npc_food_sent = 0;
    g_npc_food_received = 0;
    g_performances.clear(); g_performance_received = 0; g_performance_seen = 0; g_performance_sent = 0;
    g_perform_click = false;
    g_pending_performance = {};
    g_performance_rewarded.clear();
    g_village_defeats.clear();
    g_campaign_unlocks = 0;
    g_campaign_segments = 0;
    g_six = {}; g_six_loaded = false;
    g_perform_dispatch = {};
    g_campaign_observed = false;
    g_campaign_refresh_at = 0;
    g_campaign_poll_at = 0;
    ++g_campaign_epoch;
    g_campaign_targets.clear();
    g_build_kind = 0;
    g_restore_village_orders = true;
    g_founder_rescues = {};
    g_remote_rescues = {};
    if (g_fix_chiefs) {
        g_chief_scale.clear();
        g_chief.clear();
        g_chief_saved.clear();
        g_pincer.clear();
        g_pincer_told.clear();
    }
    g_epic = nullptr;
    g_epic_herd = nullptr;
    g_epic_hitters.clear();
    if (g_fix_epic_herd)
        g_epic_next = 0; // BUG-0067: the first epic of the next session comes after 8 minutes again
}

// the co-op mechanics' tables of game pointers (chiefs, pincer, epic): a new session or world
// must not inherit them (BUG-0081: a former chief's size was restored through a freed pointer)
static void clear_coop_tables(bool restore)
{
    if (restore) {
        if (g_fix_chiefs)
            restore_chief_scales(nullptr); // the chiefs are plain members again after the session
        // the co-op epic ends with the session (BUG-0121: not left in the solo game and its saves)
        if (g_fix_epic_herd && g_epic && game::mode_id() == modes::kTribe && epic_listed())
            game::destroy_creature(g_epic);
        epic_herd_release();
    }
    forget_world_tables();
}

// BUG-0121: the host saves (pause menu, quit, the world sent to a joining guest): a live co-op epic
// and its herd's spawning must not go into the save, where a load brought it back as an untracked
// epic. The epic leaves without a reward; the next one comes a minute later.
void before_save()
{
    if (!g_ok || !g_fix_epic_herd || !g_epic || game::mode_id() != modes::kTribe)
        return;
    if (epic_listed())
        game::destroy_creature(g_epic);
    g_epic = nullptr;
    g_epic_hitters.clear();
    epic_herd_release();
    g_epic_saved_out++;
    double now = H->time_seconds();
    if (g_epic_next < now + 60)
        g_epic_next = now + 60;
    if (C.notice && is_host())
        C.notice("the epic creature went away");
}

void on_session_end()
{
    clear_coop_tables(true);
    g_food_cycle = -1;
    g_guest_order_mode = false;
    g_extra_since = -1;
    g_guest_orders_held = g_repair_requested = g_repair_served = false;
    g_march_until = 0;
    g_order_roster = {};
    g_split_for.clear();
    g_hm.clear();
    g_hm_by_id.clear();
    g_gm.clear();
    g_gid_of.clear();
    g_npc_tribe_made.clear(); // BUG-0381
    g_npc_tribe_tried.clear();
    g_player_members_seen = 0;
}

// a world of the Tribe stage is gone (a load, a received world, the stage left, the game-over
// bounce back): every pointer of it is forgotten, with no game call (BUG-0217: no longer through
// on_session_end, whose restoring clear would touch the old world if the order ever changed)
void on_world_change()
{
    g_resets++;
    g_food_cycle = -1;
    g_guest_order_mode = false;
    g_extra_since = -1;
    g_guest_orders_held = g_repair_requested = g_repair_served = false;
    g_march_until = 0;
    g_order_roster = {};
    forget_world_tables(); // every citizen of the old world is freed: nothing to restore
    g_split_for.clear();
    g_hm.clear();
    g_hm_by_id.clear();
    g_gm.clear();
    g_gid_of.clear();
    g_npc_tribe_made.clear(); // BUG-0381
    g_npc_tribe_tried.clear();
    g_player_members_seen = 0;
}

// BUG-0217: which Tribe world is open. Before, any frame without the stage's local entity (every
// editor: no adapter) counted as a world change, so an editor visit dropped the live co-op epic
// untracked into the share save that follows. Now: the stage counter (game::stage_epoch: a load,
// a received world, another stage), leaving Tribe for a mode that is not an editor, and the
// in-stage signals (another player tribe object, another save name). Limit: a reload of the same
// save that reuses the player tribe's address and never shows a non-stage frame is not seen.
// `coop.fix tribe.editor_world off`: the old trigger (coop.cpp avatar_change_check).
static bool g_fix_editor_world = true;
COOP_FIX("tribe.editor_world", "BUG-0217", g_fix_editor_world, "tribe: an editor visit keeps the world (the co-op epic stays tracked)");
static stage_epoch::Watch g_world_watch;
static bool g_world_left = true; // the Tribe world is not open (forgotten when it was left)
static uint32_t g_world_checks_fired;

bool editor_world_fix() { return g_fix_editor_world; }

void world_check()
{
    if (!g_fix_editor_world) {
        g_world_watch = {};
        g_world_left = true;
        return;
    }
    uint32_t m = game::mode_id();
    bool in = m == modes::kTribe;
    bool editor = m == modes::kEdit || m == modes::kEditor;
    if (editor && is_host() && g_outfit.token) { g_outfit.host_entered = true; g_outfit_settled = 0; }
    void* pt = in && g_ok ? player_tribe() : nullptr;
    bool ch = stage_epoch::changed(g_world_watch, game::stage_epoch(modes::kTribe), in, pt,
                                   in ? modes::current_world() : 0);
    bool fire = false;
    if (!in && !editor) { // the Tribe world is gone (menu, load, another stage)
        fire = !g_world_left;
        g_world_left = true;
    } else if (in) {
        fire = ch && !g_world_left; // a new world while one was open (in-stage signals)
        g_world_left = false;
    }
    if (fire) {
        g_world_checks_fired++;
        if (g_ok)
            H->log(M, SK_INFO, "tribe: the world changed (mode %08X, epoch %u)", m, game::stage_epoch(modes::kTribe));
        on_world_change();
    }
}

bool center(net::Vec3& pos)
{
    void* t = g_ok ? player_tribe() : nullptr;
    if (!t)
        return false;
    pos = tribe_pos(t);
    return true;
}

void for_each_owned(void (*fn)(void* citizen, uint8_t owner, void* user), void* user)
{
    void* t = active() ? player_tribe() : nullptr;
    if (!t)
        return;
    for (void* c : members(t)) {
        uint8_t o = owner_of(c);
        if (o != 0xFF && o != 0xFE)
            fn(c, o, user);
    }
}

void for_each_tracked(void (*fn)(const TrackMember& m, void* user), void* user)
{
    if (!active())
        return;
    if (is_host()) {
        for (auto& [c, hm] : g_hm)
            if (hm.owner < 0xFE && game::object_alive(c))
                fn({hm.id, hm.owner, c, nullptr, drive::speed(game::velocity(c)), 0, nullptr}, user);
        return;
    }
    double now = H->time_seconds();
    for (auto& [id, g] : g_gm) {
        if (g.owner >= 0xFE || !g.c || !game::object_alive(g.c) || now - g.seen > 3.0)
            continue;
        fn({id, g.owner, g.c, &g.aim, g.aim_speed, (uint16_t)g.ds.teleports, &g.ds.drawn}, user);
    }
}

std::string status()
{
    char b[448];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "tribe(host): members=%u cmds_done=%u rejected=%u gifts=%u unselected=%u\n",
                 (unsigned)g_hm.size(), g_cmds_done, g_cmds_rejected, g_gifts, g_unselected);
    else
        snprintf(b, sizeof b,
                 "tribe(guest): mirrors=%u spawned=%u removed=%u cmds_sent=%u rx=%u skipped=%u resets=%u "
                 "local_tribes=%u player_tribe=%p unselected=%u incomplete=%u ai_skipped=%u/%u away_orders=%u local=%d mirror_walks=%u timed=%u "
                 "npc_tribes_made=%u failed=%u mirror_grounded=%u\n",
                 (unsigned)g_gm.size(), g_spawned, g_removed, g_cmds_sent, g_rx, g_rx_skipped, g_resets,
                 (unsigned)all_tribes().size(), player_tribe(), g_unselected, g_incomplete, g_bt_skipped, g_bt_calls, g_away_orders, (int)local_play(),
                 g_mirror_walks, g_mirror_timed, g_npc_tribes_made, g_npc_tribes_failed, g_mirror_grounded);
    std::string out = b;
    out += "tribe building health applied=" + std::to_string(g_building_applied) + "\n";
    out += "founder population rewinds held=" + std::to_string(g_founder_rewinds_held) + "\n";
    out += "tribe cleared-species layouts deferred=" + std::to_string(g_layout_deferred) + "\n";
    if (village_orders()) out += "founder worlds: native village formation skipped=" + std::to_string(g_form_skipped) +
                                " guest orders held=" + std::to_string(g_guest_orders_held) + "\n";
    if (!g_last_cmd.empty())
        out += "tribe last order: " + g_last_cmd + "\n";
    out += "tribe ui mode=" + std::to_string(ui_mode()) + " (0 social, 1 attack)\n";
    if (!g_last_local.empty())
        out += "tribe last local order: #" + std::to_string(g_local_orders) + " " + g_last_local + "\n";
    std::string chiefs;
    for (auto& [o, c] : g_chief)
        chiefs += " " + std::to_string(o) + ":#" + std::to_string(member_id(c));
    snprintf(b, sizeof b,
             "tribe co-op: pincer=%u aura=%u chief_rescues=%u duet=%u heals=%u epics=%u epic_kills=%u epic=%p "
             "chiefs=%s spread=%u placed=%u\n",
             g_pincer_hits, g_aura_hits, g_chief_rescues, g_duet_ticks, g_heals, g_epics, g_epic_kills, g_epic,
             chiefs.c_str(), g_spread_moves, g_placed);
    out += b;
    // the chiefs' size in the game (spatial GetScale / their own scale) and the lifetime checks
    std::string scales;
    for (auto& [o, c] : g_chief) {
        auto it = g_chief_scale.find(c);
        char sb[32];
        snprintf(sb, sizeof sb, " %u:%.2f", o, it != g_chief_scale.end() && it->second > 0 ? game::scale(c) / it->second : 0.f);
        scales += sb;
    }
    snprintf(b, sizeof b,
             "tribe lifetime: chief_restores=%u chief_gone=%u chief_bad_calls=%u spawns_blocked=%u mirror_out_passed=%u fix_chiefs=%d "
             "fix_gameover=%d knocked_out=%u ko_replaced=%u epic_herd_reused=%u epic_saved_out=%u chief_scale=%s\n",
             g_chief_restores, g_chief_gone, g_chief_bad_calls, g_spawns_blocked, g_mirror_out_passed, (int)g_fix_chiefs, (int)g_fix_gameover,
             g_ko_shown, g_ko_replaced, g_epic_herd_reused, g_epic_saved_out, scales.c_str());
    return out + b;
}

// ---- debug (tests): orders through the same hooked functions the UI uses ----
static bool g_fix_act_check = true;
COOP_FIX("tribe.act_check", "BUG-0260", g_fix_act_check, "tribe: coop.tribe act refuses a tool / hut action on a member (tests)");
static bool act_needs_object(int action) { return action == 3 || action == 12 || action == 15 || action == 24; }

static void* local_of(uint16_t id)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm_by_id.find(id);
        return it == g_hm_by_id.end() ? nullptr : it->second;
    }
    auto it = g_gm.find(id);
    return it == g_gm.end() ? nullptr : it->second.c;
}

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() == 1 && a[0] == "planned_sites") {
        if (!active() || !is_host()) return "Open a hosted Tribal campaign.";
        const auto rules = world::rules_for_current();
        const size_t target = rules.seats + rules.rival_target(5);
        if (target > 128) return "The requested site count exceeds the placement probe limit.";
        std::vector<net::Vec3> sites;
        for (auto village : all_tribes()) sites.push_back(tribe_pos(village));
        const size_t existing = sites.size();
        for (size_t i = 0; i < sites.size() && sites.size() < target; ++i)
            add_clear_sites(sites[i],sites,target);
        std::string out = "terrain sites " + std::to_string(sites.size()) + "/" + std::to_string(target) +
                          ", live villages " + std::to_string(existing) + "\n";
        for (size_t i = existing; i < sites.size(); ++i) {
            char line[96];
            snprintf(line,sizeof line,"clear %.2f %.2f %.2f\n",sites[i].x,sites[i].y,sites[i].z);
            out += line;
        }
        return out;
    }
    if (a.size() == 2 && a[0] == "combat_hp") {
        const auto id = uint16_t(strtoul(a[1].c_str(),nullptr,10));
        // Use the current native village member list, including the guest's
        // live mirror bindings, instead of dereferencing a saved pointer.
        for (auto t : all_tribes()) for (auto c : members(t)) {
            if (member_id(c) != id || !is_citizen(c) || !game::object_alive(c)) continue;
            char text[150];
            snprintf(text,sizeof text,"member %u hp=%.3f max=%.3f faction=%08X founder=%u native=%.3f",
                     id,game::health(c),game::max_health(c),F<uint32_t>(t,0x30),founder_village(t) ? 1u : 0u,creature_hp::native_citizen_max(c));
            return text;
        }
        return "No live Tribal member for this ID.";
    }
    if (a.size() == 1 && a[0] == "performance_orders" && active() && is_host()) {
        char line[160];
        snprintf(line, sizeof line, "sequence=%u target=%u requested=%u dispatched=%u records=%u deferred=%u",
                 g_perform_dispatch.sequence, g_perform_dispatch.target, g_perform_dispatch.requested,
                 g_perform_dispatch.dispatched, g_perform_dispatch.records, g_perform_dispatch.deferred ? 1u : 0u);
        return line;
    }
    if (a.size() == 2 && a[0] == "six_rival_trial") {
        if (!active() || !is_host() || !g_six_hook_ready || !g_order_roster.guest().faction)
            return "Requires the connected Tribal founder campaign.";
        load_six_trial();
        if (g_six.extra) return "Six-rival trial already active.";
        char* end = nullptr;
        const auto faction = (uint32_t)strtoul(a[1].c_str(), &end, 16);
        auto tribes = all_tribes();
        auto* player = tribal_player();
        if (!player || tribes.size() != 8 || !faction || !end || *end || g_order_roster.contains_faction(faction) ||
            F<float>(player, 0x10F0) != 3.f || F<float>(player, 0x10F4) != 5.f ||
            std::none_of(tribes.begin(), tribes.end(), [&](void* t){ return F<uint32_t>(t, 0x30) == faction; }))
            return "Requires three earned victories, both founders and six distinct NPC factions.";
        founders::SixRivals trial{faction, 0, 0};
        if (!founders::save_six_rivals(world::current_game_key(), g_order_roster, trial, true))
            return "Could not create the six-rival trial record.";
        g_six = trial; g_six_loaded = true;
        return "Six-rival trial active.";
    }
    if (a.size() == 1 && a[0] == "npc_budget")
        return "native formation calls=" + std::to_string(g_form_calls) + " filtered=" +
               std::to_string(g_form_budget_filtered) + " last_total=" + std::to_string(g_form_budget_total);
    if (a.size() == 1 && a[0] == "outfits") {
        founders::Outfits saved;
        std::string error;
        bool ok = founders::read_outfits(world::current_game_key(), saved, error);
        char line[240];
        snprintf(line, sizeof line, "outfits readable=%d home=%08X guest=%08X token=%u previous=%08X result=%08X ready=%d host_entered=%d pending=%d\n",
                 (int)ok, saved.current(0), saved.current(1), g_outfit.token, g_outfit.previous, g_outfit.model,
                 (int)g_outfit.ready, (int)g_outfit.host_entered, (int)outfits_pending());
        return std::string(line) + (ok ? "" : error);
    }
    // tests of the co-op mechanics through the game's damage path (host): ids as in coop.tribe
    auto hit = [](void* victim, void* attacker, float dmg) {
        float before = F<float>(victim, 0x5E0);
        // a dead victim takes no more blows: the test loop went on hitting the dying epic (BUG-0171)
        if (before <= 0 || game::is_dead(victim))
            return std::string("hp 0.00 -> 0.00 (dead)");
        // the combatant's TakeDamage (vt 0x18): the same path as a real blow, with our hooks
        // (the push vector: (0,0,0) as the game's own bites and strikes, docs/re/creature.md 3)
        static const float zero[3] = {0, 0, 0}; // the hit direction must not be null
        vcall<int>((uint8_t*)victim + 0x5A8, 0x18 / 4, dmg, 0xFFFFFFFFu, 0, zero,
                   attacker ? (uint8_t*)attacker + 0x5A8 : nullptr);
        char b[64];
        snprintf(b, sizeof b, "hp %.2f -> %.2f", before, F<float>(victim, 0x5E0));
        return std::string(b);
    };
    if (a.size() >= 3 && a[0] == "fix") { // fix chiefs|gameover 0|1: the code before BUG-0081 / BUG-0172 (tests)
        bool on = atoi(a[2].c_str()) != 0;
        if (a[1] == "chiefs")
            g_fix_chiefs = on;
        else if (a[1] == "gameover")
            g_fix_gameover = on;
        else if (a[1] == "ko")
            g_fix_ko = on;
        else if (a[1] == "epic_players")
            g_fix_epic_players = on;
        else if (a[1] == "epic_herd")
            g_fix_epic_herd = on;
        else if (a[1] == "away")
            g_fix_away = on;
        else
            return "usage: coop.tribe fix chiefs|gameover|ko|epic_players|epic_herd|away 0|1";
        return "fix " + a[1] + " " + (on ? "on" : "off");
    }
    if (a.size() >= 2 && a[0] == "drop_chief") { // drop_chief <owner>: the chief leaves the tribe at once
        // (RemoveMember(c, destroy) as guest_state does for a mirror the host no longer sends):
        // BUG-0081, the next chiefs_tick must not call into it
        auto it = g_chief.find((uint8_t)atoi(a[1].c_str()));
        if (it == g_chief.end() || !it->second)
            return "no chief";
        void* c = it->second;
        uint16_t id = member_id(c);
        remove_member(c);
        return "dropped chief #" + std::to_string(id);
    }
    if (a.size() >= 2 && a[0] == "stale_chief") { // stale_chief <id>: as if a former chief's freed block
        // now held this member (another tribe's citizen): the next chiefs_tick must leave it alone
        // (BUG-0081; before the fix it got the old chief's size, 2.0 here)
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        g_chief_scale[c] = 2.f;
        return "stale chief entry for #" + a[1];
    }
    if (a.size() >= 2 && a[0] == "scale") { // scale <id>: the member's size in the game (spatial GetScale)
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        char b[64];
        snprintf(b, sizeof b, "scale %.3f", c ? game::scale(c) : 0.f);
        return c ? std::string(b) : std::string("no such member");
    }
    if (a.size() >= 4 && a[0] == "pincer" && is_host()) { // pincer <victim id> <attacker id> <dmg>
        void* v = local_of((uint16_t)atoi(a[1].c_str())), *at = local_of((uint16_t)atoi(a[2].c_str()));
        if (!v || !at)
            return "no such member";
        return hit(v, at, (float)atof(a[3].c_str()));
    }
    if (!a.empty() && a[0] == "scales") { // the player tribe's members: size in the game / own size (BUG-0067)
        void* pt = game::mode_id() == modes::kTribe ? player_tribe() : nullptr;
        int n = 0, big = 0;
        float mx = 0;
        std::string list;
        for (void* c : pt ? members(pt) : std::vector<void*>{}) {
            float own = vcall<float>(c, 0x80 / 4, 0); // GetDesiredScale(int)
            if (own <= 0.01f || own > 10.f)
                continue;
            float r = game::scale(c) / own;
            n++;
            big += r > 1.15f;
            mx = std::max(mx, r);
            char e[48];
            snprintf(e, sizeof e, " %u:%.2f/%.2f", (unsigned)member_id(c), game::scale(c), own);
            list += e; // id:scale in the game/own size (tests, BUG-0152)
        }
        char b[96];
        snprintf(b, sizeof b, "scales members=%d big=%d max_ratio=%.2f", n, big, mx);
        return b + list;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "herds") { // the game's epic herds and epic creatures (BUG-0121)
        struct Cnt {
            uintptr_t is_epic;
            int herds = 0, ours = 0, enabled = 0, epics = 0;
        } n{H->sym("SporeCoop::cCreatureAnimal_IsEpic")};
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* n = (Cnt*)u;
                if (!game::object_alive(o))
                    return true;
                if (noun == game::kHerdNoun && skg::readable(o, 0x21C) && F<uint32_t>(o, 0x88) == kEpicArchetype) {
                    n->herds++;
                    n->ours += is_epic_herd_of_ours(o);
                    n->enabled += F<uint8_t>(o, 0x218) != 0;
                } else if (noun == 0x18EB45E && n->is_epic && !game::is_dead(o) && thiscall<bool>(n->is_epic, o))
                    n->epics++;
                return true;
            },
            &n);
        char b[192];
        snprintf(b, sizeof b, "epic herds=%d quiet_ours=%d enabled=%d live_epics=%d tracked=%p resets=%u world_changes=%u epoch=%u",
                 n.herds, n.ours, n.enabled, n.epics, g_epic, g_resets, g_world_checks_fired, game::stage_epoch(modes::kTribe));
        return b;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "quiet") { // RE: quiet every epic herd of ours now
        std::string out;
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                if (noun == game::kHerdNoun && is_epic_herd_of_ours(o)) {
                    char b[96];
                    uint8_t e0 = F<uint8_t>(o, 0x218);
                    game::herd_quiet(o);
                    snprintf(b, sizeof b, "herd %p enabled %d -> %d (ours %p)\n", o, e0, F<uint8_t>(o, 0x218), g_epic_herd);
                    *(std::string*)u += b;
                }
                return true;
            },
            &out);
        return out.empty() ? "no herd of ours" : out;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "save") { // as if the game saved now (BUG-0121)
        before_save();
        return "before_save done";
    }
    if (!a.empty() && a[0] == "epic" && is_host()) {
        if (a.size() >= 3 && a[1] == "hit") { // epic hit <attacker id> <dmg>
            void* at = local_of((uint16_t)atoi(a[2].c_str()));
            if (!g_epic || !at || !epic_listed())
                return "no epic / member";
            return hit(g_epic, at, a.size() > 3 ? (float)atof(a[3].c_str()) : 50.f);
        }
        bool ok = spawn_epic();
        uintptr_t is_epic = H->sym("SporeCoop::cCreatureAnimal_IsEpic");
        char b[96];
        snprintf(b, sizeof b, "epic %s %p is_epic=%d hp=%.0f", ok ? "spawned" : "not spawned", g_epic,
                 g_epic && is_epic ? (int)thiscall<bool>(is_epic, g_epic) : -1, g_epic ? F<float>(g_epic, 0x5E0) : 0.f);
        return b;
    }
    if (a.size() >= 3 && a[0] == "select") { // select <id> <0|1>
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, atoi(a[2].c_str()));
        return std::string("selected=") + std::to_string(F<uint8_t>(c, kSelectedOff));
    }
    if (a.size() >= 3 && a[0] == "select_own") { // select_own <n> <0|1>: the player tribe's n-th member (tests:
        // a guest in a world of its own has no shared ids, BUG-0231)
        void* pt = player_tribe();
        auto ms = pt ? members(pt) : std::vector<void*>{};
        size_t n = (size_t)atoi(a[1].c_str());
        if (n >= ms.size())
            return "no such member";
        vcall<int>((uint8_t*)ms[n] + 0xC0, 0x54 / 4, atoi(a[2].c_str()));
        return std::string("selected=") + std::to_string(F<uint8_t>(ms[n], kSelectedOff));
    }
    if (a.size() >= 4 && a[0] == "walk") { // walk <x> <y> <z> : the selected members walk there
        float p[3] = {(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        thiscall<int>(a_WalkSelected, (void*)nullptr, (const float*)p);
        return "walk ordered";
    }
    if (a.size() >= 3 && (a[0] == "act" || a[0] == "act_npc")) { // act <action> <member id | npc id>
        void* t = a[0] == "act" ? local_of((uint16_t)atoi(a[2].c_str())) : npc::proxy((uint16_t)atoi(a[2].c_str()));
        int action = atoi(a[1].c_str());
        // BUG-0260: the object actions (3 GrabTool, 12 Party, 15 RaidHuts, 24 Repair) take a tool or a
        // hut; with a citizen as the target DoAction read a field of the citizen as a tool's and
        // faulted (DoAction+0x78C, data 0x39C). The game's UI never pairs them (docs/re/tribe.md 2).
        if (g_fix_act_check && a[0] == "act" && t && act_needs_object(action))
            return "action " + std::to_string(action) + " needs a tool or a hut, not a member (attack a member: 7)";
        std::vector<void*> sel;
        for (auto& [id, g] : g_gm)
            if (g.c && F<uint8_t>(g.c, kSelectedOff))
                sel.push_back((uint8_t*)g.c + 0xC0);
        for (auto& [c, hm] : g_hm)
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, action, t, (void*)nullptr);
        return "action ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ")";
    }
    if (!a.empty() && a[0] == "stack" && is_host()) { // tests (BUG-0340): every chief onto the first one
        if (g_chief.empty())
            return "no chiefs";
        auto first = *std::min_element(g_chief.begin(), g_chief.end());
        net::Vec3 at = game::position(first.second);
        int n = 0;
        for (auto& [o, c] : g_chief)
            if (c != first.second) {
                game::teleport(c, at, game::orientation(c));
                n++;
            }
        return "stacked " + std::to_string(n) + " chiefs on the chief of player " + std::to_string(first.first);
    }
    if (a.size() >= 2 && a[0] == "lift" && is_guest()) { // lift <m>: tests (BUG-0329), the host's members m higher
        g_test_lift = (float)atof(a[1].c_str());
        return "members of the host taken " + a[1] + " m further from the planet's centre";
    }
    if (a.size() >= 2 && a[0] == "spawn" && is_host()) { // spawn <n>: test setup, n more adults in the player tribe
        void* pt = player_tribe();
        int n = 0;
        for (int i = 0; pt && i < atoi(a[1].c_str()) && i < 40; i++) {
            void* c = spawn_member(pt, 1);
            if (!c)
                break;
            n++;
        }
        return "spawned " + std::to_string(n);
    }
    if (a.size() == 3 && a[0] == "tool_fixture") {
        // Tests only: CreateTool leaves its owner unset. A raw console call followed
        // by SetTribe in a later frame crashes native tool Update at owner+556.
        if (!active() || !is_host()) return "Create a rack fixture on the active Tribal host.";
        unsigned kind = (unsigned)strtoul(a[1].c_str(), nullptr, 10);
        unsigned id = (unsigned)strtoul(a[2].c_str(), nullptr, 10);
        auto member = g_hm_by_id.find((uint16_t)id);
        if (kind < 1 || kind > 9 || id > 65535 || member == g_hm_by_id.end() || !living(member->second)) return "Invalid rack fixture.";
        void* citizen = member->second;
        void* tribe = F<void*>(citizen, kOwnerTribeOff);
        std::array<void*, 11> tools{};
        if (!tribe || !tool_inventory(tribe, tools) || tools[kind]) return "Rack exists or village is busy.";
        void* tool = vcall<void*>(tribe, 0xB4 / 4, (int)kind);
        if (!tool) return "Native rack creation failed.";
        vcall<void>(tool, 0x64 / 4, tribe);
        auto position = game::position(citizen);
        auto rotation = game::orientation(citizen);
        vcall<void>((uint8_t*)tool + 0x34, 0x44 / 4, &position, &rotation);
        if (uintptr_t layout = H->sym("SporeCoop::Tribe_UpdateToolLayout")) thiscall<void>(layout, tribe, (int)kind);
        return "Native rack fixture created without a purchase.";
    }
    if (a.size() == 1 && a[0] == "food") {
        if (!active()) return "Open the tribal planet first.";
        std::string result;
        for (void* t : all_tribes()) {
            double food;
            if (!shared::tribe_food(t, food)) continue;
            char row[80];
            snprintf(row, sizeof row, "species=%08X food=%.2f\n", tribe_info(t).key.instance, food);
            result += row;
        }
        return result;
    }
    if (a.size() == 1 && a[0] == "gather") {
        if (!active() || !is_guest() || local_play()) return "Gather on the guest in the host's Tribe world.";
        auto ids = guest_village_ids();
        if (ids.empty()) return "The host must enable orders for your registered village.";
        send_cmd(CMD_FORAGE, ids, 0, {}, {});
        return "Gather requested. The host will find the nearest tree with fruit.";
    }
    if (a.size() == 1 && a[0] == "performance") {
        std::string result = performance_line();
        for (const auto& p : g_performances) {
            result += "\nmember=" + std::to_string(p.member) + " token=" + std::to_string(p.token) +
                " phase=" + std::to_string(p.phase) + " request=" + std::to_string(p.request) +
                " ready=" + std::to_string(p.ready) + " progress=" + std::to_string(p.progress);
        }
        return result.empty() ? "No active guest performance." : result;
    }
    if (a.size() == 2 && a[0] == "instrument")
        return request_instrument((unsigned)strtoul(a[1].c_str(), nullptr, 10)) ?
            "Instrument choice sent." : "Select your performers and wait for the audience's request.";
    if (a.size() == 2 && (a[0] == "gift_to" || a[0] == "perform_to") && active() && is_guest() && !local_play()) {
        uint32_t faction = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        for (void* target : all_tribes()) if (F<uint32_t>(target, 0x30) == faction) {
            if (a[0] == "perform_to") {
                void* chief = F<void*>(target, kNativeLeaderOff);
                if (!chief || !game::object_alive(chief)) return "That tribe has no living chief.";
                return request_performance(encode_target(chief));
            }
            send_cmd(CMD_ACTION, guest_village_ids(true), 9, encode_target(target), {});
            return "Food gift requested.";
        }
        return "No such tribe.";
    }
    if (a.size() == 3 && a[0] == "relation" && active()) {
        uint32_t from = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        uint32_t to = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        bool have_from = false, have_to = false;
        for (void* t : all_tribes()) { have_from |= F<uint32_t>(t, 0x30) == from; have_to |= F<uint32_t>(t, 0x30) == to; }
        void* manager = relationship_manager();
        uintptr_t calculate = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
        if (!have_from || !have_to || !manager || !calculate) return "No such live tribe pair.";
        char line[96];
        snprintf(line, sizeof line, "relation %08X/%08X=%.6f", from, to, thiscall<float>(calculate, manager, from, to, 0));
        return line;
    }
    if (a.size() == 1 && a[0] == "eat") {
        if (!active() || !is_guest() || local_play()) return "Eat from the guest's Village page.";
        auto ids = guest_village_ids(true);
        if (ids.empty()) return "Select your villagers before choosing Eat.";
        void* citizen = g_gm.at(ids.front()).c;
        void* owner = citizen ? F<void*>(citizen, kOwnerTribeOff) : nullptr;
        void* mat = owner ? F<void*>(owner, 0x260) : nullptr;
        if (!mat || !game::object_alive(mat)) return "Your village has no food mat.";
        send_cmd(CMD_ACTION, ids, 0, encode_target(mat), {});
        return "Your selected villagers will eat at their village's food mat.";
    }
    if (a.size() == 2 && a[0] == "build") {
        int kind = atoi(a[1].c_str());
        if (!active() || !is_guest() || local_play() || guest_village_ids().empty() || kind < 1 || kind > 9)
            return "Build from the guest's Village page while your village has a living member.";
        if (!(g_tool_unlocks & (1u << kind))) return "Ally with or conquer other tribes to unlock that tool.";
        mouse::install(H->game_window());
        g_build_kind = kind;
        g_attack_click = g_raid_click = g_equip_click = g_heal_click = g_gift_click = g_perform_click = false;
        g_march_click = mouse::clicks();
        g_march_until = H->time_seconds() + 30;
        return "Click clear ground 12 to 35 metres from your hut within 30 seconds. Leave eight metres around other buildings.";
    }
    if (a.size() == 1 && a[0] == "baby") {
        if (!active() || !is_guest() || local_play()) return "Add a baby from the guest's Village page.";
        auto ids = guest_village_ids();
        if (ids.empty()) return "Your village needs a living member before adding a baby.";
        send_cmd(CMD_BABY, {ids.front()}, 0, {}, {});
        return "Birth requested. The host checks your village's food and population limit.";
    }
    if (a.size() == 1 && a[0] == "rebuild") {
        if (!active() || !village_orders()) return "Rebuilding requires a two-founder Tribal campaign.";
        uint16_t helper = 0;
        if (is_host()) {
            for (const auto& [person, entry] : g_hm)
                if (entry.owner == 0 && living(person) && F<uint8_t>(person, kSelectedOff)) { helper = entry.id; break; }
            return helper && queue_partner_rebuild(0, helper) ? "Rebuilding requested." : "Select a living villager to help your partner rebuild.";
        }
        for (const auto& [id, entry] : g_gm)
            if (entry.c && entry.owner == C.sess->my_id() && F<uint8_t>(entry.c, kSelectedOff)) { helper = id; break; }
        if (!helper) return "Select a living villager to help your partner rebuild.";
        send_cmd(CMD_REBUILD, {helper}, 0, {}, {});
        return "Rebuilding requested.";
    }
    if (a.size() == 1 && (a[0] == "march" || a[0] == "attack" || a[0] == "raid" || a[0] == "equip" || a[0] == "heal" || a[0] == "gift_food" || a[0] == "perform")) {
        if (!active() || !is_guest() || local_play() || guest_village_ids().empty())
            return "The host must enable village orders for your registered species.";
        mouse::install(H->game_window());
        g_build_kind = 0;
        g_raid_click = a[0] == "raid";
        g_equip_click = a[0] == "equip";
        g_heal_click = a[0] == "heal";
        g_gift_click = a[0] == "gift_food";
        g_perform_click = a[0] == "perform";
        g_attack_click = a[0] == "attack" || g_raid_click || g_equip_click || g_heal_click || g_gift_click || g_perform_click;
        if (g_attack_click && guest_village_ids(true).empty()) return "Select your villagers before choosing this action.";
        g_march_click = mouse::clicks();
        g_march_until = H->time_seconds() + 30;
        return g_perform_click ? "Left-click an NPC villager within 30 seconds. Bring your chief and musicians, then answer requests with 1, 2 or 3." :
            g_gift_click ? "Left-click another tribe's hut within 30 seconds. Your selected villagers will deliver food from your village." :
            g_heal_click ? "Left-click an injured friendly villager within 30 seconds. Selected villagers with healing tools will help them." :
            g_equip_click ? "Left-click a rack in your village within 30 seconds. Selected adult villagers will collect tools; your chief keeps their staff." :
            g_raid_click ? "Left-click an enemy hut or tool building within 30 seconds. Your selected villagers will raid it." :
            g_attack_click ? "Left-click an enemy villager within 30 seconds. Your selected villagers will attack." :
            "Left-click the destination within 30 seconds.";
    }
    if (a.size() == 1 && a[0] == "select_village") {
        if (!active() || !is_guest()) return "Select the village on the guest in Tribe Stage.";
        unsigned selected = 0;
        for (uint16_t id : guest_village_ids()) {
            void* c = g_gm.at(id).c;
            vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, 1);
            selected += F<uint8_t>(c, kSelectedOff) != 0;
        }
        return "Guest villagers selected: " + std::to_string(selected);
    }
    if (a.size() == 2 && a[0] == "village_orders") {
        if (!active() || !is_host()) return "Change village orders on the Tribe host.";
        if (rescue_pending()) return "Wait for the last villager to revive or respawn.";
        g_restore_village_orders = false;
        if (a[1] == "off") {
            g_auto_found_manual_off = true;
            g_order_roster = {};
            g_split_for.clear(); // restore the normal split on the next snapshot
            return "Shared tribe orders restored.";
        }
        founders::Roster roster;
        if (a[1] != "on") return "Use village_orders on or off.";
        if (!founders::read(world::current_game_key(), roster)) return "Register both founding villages first.";
        g_auto_found_manual_off = false;
        g_order_roster = roster;
        return "Village walking orders enabled for this host session.";
    }
    if (a.size() == 1 && a[0] == "register_guest") {
        if (!active() || !is_host() || C.sess->players().size() != 2)
            return "Register founders on the Tribe host with exactly one guest.";
        uint8_t guest = 0;
        for (const auto& player : C.sess->players())
            if (player.id) guest = player.id;
        auto key = creations::player_key(guest, creations::kCreature);
        void* home = player_tribe();
        if (!home || !guest || !key.instance || !creations::player_species(guest))
            return "Waiting for the guest's selected creature.";
        void* village = nullptr;
        for (void* t : all_tribes()) {
            if (tribe_info(t).key.instance != key.instance) continue;
            if (t == home || village) return "Founders need two distinct, identifiable villages.";
            village = t;
        }
        if (!village) return "No village matches the guest's selected species.";
        founders::Roster roster{tribe_info(home).key.instance, key.instance,
                               F<uint32_t>(home, 0x30), F<uint32_t>(village, 0x30)};
        std::string err;
        auto planet = world::current_game_key();
        if (!founders::can_record(planet, roster, err) || !creations::archive_guest_model(guest, planet, err)) return err;
        return founders::record(planet, roster, err) ? founders::describe(roster) : err;
    }
    if (a.size() == 2 && a[0] == "test_perspective") {
        if (!active() || !is_guest()) return "Requires a guest in Tribe Stage.";
        char* end = nullptr;
        uint32_t species = (uint32_t)strtoul(a[1].c_str(), &end, 16);
        if (!species || !end || *end) return "Supply the guest's creature instance in hex.";
        uintptr_t set = H->sym("Simulator::cGameNounManager::SetPlayerTribe");
        void* mgr = game::noun_manager();
        void* original = player_tribe();
        if (!set || !mgr || !original) return "Native tribe selection unavailable on this build.";
        void* other = nullptr;
        for (void* t : all_tribes()) {
            if (tribe_info(t).key.instance != species) continue;
            if (t == original || other) return "Expected one distinct guest village.";
            other = t;
        }
        if (!other) return "Guest village has not arrived.";
        // One main-thread call. Keep the original alive until the native manager holds it again.
        vcall<int>(original, 0);
        thiscall<void>(set, mgr, other);
        bool selected = player_tribe() == other;
        thiscall<void>(set, mgr, original);
        bool restored = player_tribe() == original;
        vcall<int>(original, 1);
        return selected && restored ? "Native guest village selected and original restored." :
                                      "Native tribe selection check failed.";
    }
    if (!a.empty() && a[0] == "villages") {
        if (!active()) return "Open a Tribe-stage session first.";
        std::string out;
        for (void* t : all_tribes()) {
            auto people = members(t);
            char line[180];
            snprintf(line, sizeof line, "village %08X species=%08X members=%u native_player=%d\n",
                     F<uint32_t>(t, 0x30), tribe_info(t).key.instance, (unsigned)people.size(), t == player_tribe());
            out += line;
        }
        return out;
    }
    // Explicit founding probes remain for fixtures. Normal campaign entry uses
    // auto_founders_tick with placement, archive, registration and checkpoint guards.
    if (a.size() == 1 && a[0] == "refound_home_status") return g_home_probe_result;
    if (a.size() == 2 && a[0] == "refound_home_probe") {
        char* end = nullptr;
        uint32_t conqueror = (uint32_t)strtoul(a[1].c_str(), &end, 16);
        if (!conqueror || !end || *end) return "Supply the enemy conqueror faction in hex.";
        if (g_home_probe_pending) return "Host refounding probe already queued.";
        g_home_probe_pending = true;
        g_home_probe_result = "pending";
        g_home_probe_world = world::current_game_key();
        H->run_outside_frame([](void* value) {
            g_home_probe_result = !g_home_probe_world.empty() && g_home_probe_world == world::current_game_key()
                ? rebuild_founder((uint32_t)(uintptr_t)value) : "The test world changed; host replacement cancelled.";
            if (g_home_probe_result.starts_with("Host replacement ")) {
                std::string error;
                if (world::checkpoint_reload(error)) {
                    if (C.notice) C.notice("Rebuilding village: saving and refreshing both players' worlds.");
                } else {
                    g_home_probe_result += "; recovery refresh failed: " + error;
                    if (C.notice) C.notice(g_home_probe_result);
                }
            }
            g_home_probe_pending = false;
            H->log(M, SK_INFO, "tribe: %s", g_home_probe_result.c_str());
        }, (void*)(uintptr_t)conqueror);
        return "Host refounding probe queued. Read refound_home_status.";
    }
    bool refounding = a.size() == 1 && a[0] == "refound_guest";
    bool founding = refounding || (!a.empty() && a[0] == "found_guest");
    if (founding && (!active() || !is_host() || !a_SpawnNpcTribe || C.sess->players().size() != 2))
        return "Founding requires a host and one guest in Tribe Stage.";
    // npc_tribe <members> <metres>: tests (BUG-0381), the host's game forms an NPC tribe that far from the
    // village (as the game's tribe controller does during play: the guests' games do not)
    if (founding || (a.size() >= 3 && a[0] == "npc_tribe" && is_host() && a_SpawnNpcTribe)) {
        void* pt = player_tribe();
        if (!pt)
            return "no player tribe";
        net::Vec3 c = tribe_pos(pt);
        float r = drive::speed(c), d = founding ? (refounding ? -120.f : 120.f) : (float)atof(a[2].c_str());
        if (r < 1)
            return "no village position";
        // a direction across the surface: c x (0,1,0) (or x (1,0,0) at a pole), then back onto the sphere
        net::Vec3 u{c.z, 0, -c.x};
        if (drive::speed(u) < 1e-3f)
            u = {0, -c.z, c.y};
        float ul = drive::speed(u);
        net::Vec3 q{c.x + u.x / ul * d, c.y + u.y / ul * d, c.z + u.z / ul * d};
        float ql = drive::speed(q);
        skg::Vector3 pos{q.x / ql * r, q.y / ql * r, q.z / ql * r};
        if (!founding && a[2] == "clear") {
            net::Vec3 site;
            bool found = false;
            for (void* anchor : all_tribes()) {
                if (founding_site(anchor, site)) { found = true; break; }
            }
            if (!found) return "No clear, level rival site was found.";
            pos = {site.x, site.y, site.z};
        }
        void* species = nullptr;
        uint32_t replacement_faction = 0;
        int archetype = 0;
        if (!founding && a.size() == 4 && a[2] == "clear") {
            char* end = nullptr;
            const auto instance = (uint32_t)strtoul(a[3].c_str(), &end, 16);
            if (!instance || !end || *end) return "Supply the rival's creature model key.";
            for (void* village : all_tribes())
                if (tribe_info(village).key.instance == instance) return "That creature already has a village.";
            species = game::species_by_key({instance, 0x2B978C46, 0x40626200});
            if (!species) return "The requested rival species is unavailable.";
            // Use the first native rival's configuration for a three-member
            // opponent. The raw factory leaves both archetypes without an
            // instrument, so stock the new opponent before returning.
            archetype = 2;
        }
        if (founding) {
            uint8_t guest = 0;
            for (const auto& player : C.sess->players())
                if (player.id) guest = player.id;
            auto key = creations::player_key(guest, creations::kCreature);
            if (!guest || !key.instance || !(species = creations::player_species(guest)))
                return "Waiting for the guest's selected creature. No village created.";
            for (void* other : all_tribes()) {
                if (tribe_info(other).key.instance == key.instance)
                    return "This species already has a village. No village created.";
                if (net::dist(tribe_pos(other), {pos.x, pos.y, pos.z}) < 60.f)
                    return "The test founding site is occupied. No village created.";
            }
            std::string err;
            auto planet = world::current_game_key();
            founders::Roster prior;
            bool registered = founders::read(planet, prior);
            if (refounding) {
                if (!registered || !g_refound_ready || prior.guest().species != key.instance ||
                    prior.home().faction != F<uint32_t>(pt, 0x30) || prior.home().species != tribe_info(pt).key.instance)
                    return "Refounding requires the original two founders and the supported native factory.";
                for (void* other : all_tribes())
                    if (F<uint32_t>(other, 0x30) == prior.guest().faction)
                        return "Retire the defeated village before refounding its faction.";
                replacement_faction = prior.guest().faction;
            } else if (registered) return "This planet already has two registered founders. No village created.";
            if (!creations::archive_guest_model(guest, planet, err)) return err;
        }
        g_refound_faction = replacement_faction;
        void* t = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, archetype, founding ? 3 : atoi(a[1].c_str()),
                                          founding ? 15 : 0, 0, species);
        g_refound_faction = 0;
        if (!t)
            return "SpawnNpcTribe returned null";
        if (archetype == 2 && !stock_trial_instrument(t))
            return "Rival village founded, but no native instrument slot could be filled.";
        net::Vec3 tp = tribe_pos(t);
        char b[160];
        snprintf(b, sizeof b, "npc tribe %p at (%.1f %.1f %.1f): %u members, %.0f m from the village", t, tp.x, tp.y, tp.z,
                 (unsigned)members(t).size(), net::dist(tp, c));
        return b;
    }
    if (!a.empty() && a[0] == "trees") { // trees near the village with their raw fields (RE)
        net::Vec3 at;
        center(at);
        std::vector<std::pair<float, std::string>> v;
        for_each_tree([&](void* t, const net::Vec3& p) {
            char b[256];
            float d = net::dist(p, at);
            snprintf(b, sizeof b, "%p d=%.0f pos=(%.1f %.1f %.1f) c=%04X e=%04X 1c=%.2f 48=%08X 4c=%08X 50=%08X 54=%08X 58=%08X 6c=%.2f 78=%08X 7c=%02X\n",
                     t, d, p.x, p.y, p.z, F<uint16_t>(t, 0xC), F<uint16_t>(t, 0xE), F<float>(t, 0x1C), F<uint32_t>(t, 0x48),
                     F<uint32_t>(t, 0x4C), F<uint32_t>(t, 0x50), F<uint32_t>(t, 0x54), F<uint32_t>(t, 0x58), F<float>(t, 0x6C),
                     F<uint32_t>(t, 0x78), F<uint8_t>(t, 0x7C));
            v.push_back({d, b});
        });
        std::sort(v.begin(), v.end());
        std::string out;
        for (size_t i = 0; i < v.size() && i < (a.size() > 1 ? (size_t)atoi(a[1].c_str()) : 25); i++)
            out += v[i].second;
        return out;
    }
    if (a.size() >= 2 && a[0] == "objs") { // objs <noun hex>: objects of a kind near the village, world positions
        struct Q {
            uint32_t noun;
            net::Vec3 at;
            std::vector<std::pair<float, std::string>> v;
        } q{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), {}, {}};
        center(q.at);
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto* q = (Q*)u;
                net::Vec3 p;
                if (n == q->noun && game::object_position(o, p)) {
                    char b[128];
                    float d = net::dist(p, q->at);
                    snprintf(b, sizeof b, "%p alive=%d pos=(%.1f %.1f %.1f) dist=%.1f\n", o, (int)game::object_alive(o), p.x,
                             p.y, p.z, d);
                    q->v.push_back({d, b});
                }
                return true;
            },
            &q);
        std::sort(q.v.begin(), q.v.end());
        std::string out;
        for (size_t i = 0; i < q.v.size() && i < 40; i++)
            out += q.v[i].second;
        return out + std::to_string(q.v.size()) + " objects";
    }
    return "usage: coop.tribe select <id> <0|1> | select_own <n> <0|1> | walk <x> <y> <z> | act <action> <target id> | objs <noun hex>";
}

// the citizen's current behaviour-tree node (ai = c+0xB4C, node id at ai+0x1D8; docs/re/tribe.md B)
static uint32_t bt_node(void* c)
{
    uint8_t* ai = F<uint8_t*>(c, 0xB4C);
    return ai && skg::readable(ai + 0x1D8, 4) ? *(uint32_t*)(ai + 0x1D8) : 0;
}

std::string list()
{
    std::string out;
    char b[240];
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t ti = 0; ti < tribes.size(); ti++) {
        net::Vec3 p = tribe_pos(tribes[ti]);
        auto ms = members(tribes[ti]);
        TribeInfo ti_info = tribe_info(tribes[ti]); // the members' species (BUG-0381 tests)
        snprintf(b, sizeof b, "tribe %u%s at (%.0f %.0f %.0f): %u members species=%08X archetype=%d\n", (unsigned)ti,
                 tribes[ti] == pt ? " (player)" : "", p.x, p.y, p.z, (unsigned)ms.size(), ti_info.key.instance,
                 ti_info.arch == 0xFF ? -1 : (int)ti_info.arch);
        out += b;
        for (void* c : ms) {
            net::Vec3 cp = game::position(c);
            uint8_t ow = owner_of(c);
            uint16_t id = 0;
            if (C.sess && C.sess->role() == net::Role::Host) {
                auto it = g_hm.find(c);
                id = it == g_hm.end() ? 0 : it->second.id;
            } else {
                auto it = g_gid_of.find(c);
                id = it == g_gid_of.end() ? 0 : it->second;
            }
            net::Vec3 v = game::velocity(c), sv{};
            const drive::State* ds = nullptr;
            if (C.sess && C.sess->role() != net::Role::Host) { // the host's velocity in the snapshot
                auto it = g_gm.find(id);
                if (it != g_gm.end()) {
                    sv = it->second.vel;
                    ds = &it->second.ds;
                }
            }
            snprintf(b, sizeof b,
                     "  #%u %p owner=%d age=%u sel=%u hp=%.1f pos=(%.1f %.1f %.1f) speed=%.1f host_speed=%.1f dead=%d bt=%08X radius=%.2f revive=%.1f\n",
                     id, c, ow == 0xFF ? -1 : (int)ow, F<uint8_t>(c, kAgeOff), F<uint8_t>(c, kSelectedOff),
                     game::health(c), cp.x, cp.y, cp.z, drive::speed(v), drive::speed(sv), (int)game::is_dead(c),
                     bt_node(c), game::bounding_radius(c), revive_seconds(c));
            out += b;
            if (ds) { // guests: how the mirror is driven (drive::step)
                snprintf(b, sizeof b, "     drive err=%.2f nudges=%u kept=%u teleports=%u\n", ds->last_err, ds->nudges,
                         ds->nudges_kept, ds->teleports);
                out += b;
            }
        }
    }
    return out;
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [c, h] : g_hm)
            if (game::object_alive(c))
                v.push_back({"t" + std::to_string(h.id), game::position(c), game::orientation(c)});
    } else {
        for (auto& [id, g] : g_gm)
            if (g.c && game::object_alive(g.c))
                v.push_back({"t" + std::to_string(id), game::position(g.c), game::orientation(g.c)});
    }
    return v;
}

int own_selected()
{
    if (!C.sess || !C.sess->connected() || game::mode_id() != modes::kTribe)
        return 0;
    int n = 0;
    uint8_t me = C.sess->my_id();
    if (C.sess->role() == net::Role::Host) {
        for (auto& [c, hm] : g_hm)
            if (hm.owner == me && F<uint8_t>(c, kSelectedOff))
                n++;
    } else
        for (auto& [id, m] : g_gm)
            if (m.c && m.owner == me && F<uint8_t>(m.c, kSelectedOff))
                n++;
    return n;
}

int give_selected(uint8_t to)
{
    if (!C.sess || !C.sess->connected() || game::mode_id() != modes::kTribe || to == C.sess->my_id())
        return 0;
    uint8_t me = C.sess->my_id();
    std::vector<uint16_t> ids;
    if (C.sess->role() == net::Role::Host) {
        for (auto& [c, hm] : g_hm)
            if (hm.owner == me && F<uint8_t>(c, kSelectedOff))
                ids.push_back(hm.id);
        give_on_host(me, to, ids);
    } else {
        for (auto& [id, m] : g_gm)
            if (m.c && m.owner == me && F<uint8_t>(m.c, kSelectedOff))
                ids.push_back(id);
        send_cmd(CMD_GIVE, ids, to, {}, {});
    }
    return (int)ids.size();
}

} // namespace tribe
