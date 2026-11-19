// BUG-1271 (WP-T1 of docs/plan-0.7.16.md): the members of the player tribe are of their owner's own species on
// every game (docs/re/tribe.md#tribe-two-species, #tribe-own-species).
//
// The game gives a new member the tribe's species slot (cTribe +0x18CC, Citizen_Init 0xC27330); a member's species is
// its own field (+0xB20 the profile, +0xB28 the key), its model, size and abilities follow it, and nothing of the game
// puts it back. Each game here (host and guests alike) gives every member whose owner (owner_of: the host's split, the
// guest's mirror table) has an own species line that line's species with cCreatureAnimal::SetSpecies(keepState 1),
// and a member the mod gave a line's species goes back to the slot's when its owner has none any more (a gift to the
// host, its owner left, creature.mode host, the switch off, the session ended). A key compare every 0.15 s finds
// what needs a change (a new member, a baby, an owner change, a world load, a rejoin, the outfitter); one member per
// pass (a body is built over several frames, BUG-0318), never while a cinematic plays (BUG-1173), never in the first
// 3 s of a world, never at the end of Tribe. Nothing is sent: every game knows the owners and the lines.
//
// Saves (owner decision 2026-11-18, docs/plan-0.7.16.md#plan-0716-decisions): while a save is written every member
// the mod changed carries the slot's key in +0xB28..+0xB30 (the three dwords the save writes, no rebuild), its own
// key comes back on the next frame (the civ.host_buildings pattern): the host's save holds the host's species only,
// a guest's save the same; the guest's look comes back from the mod after the load.
//
// `coop.fix tribe.own_species off`: every member is the slot's species (0.7.15; members the mod changed go back).
#include "tribe.h"
#include "tribe_internal.h"
#include "tribe_logic.h"

namespace tribe {

static bool g_own_species_fix = true;
COOP_FIX("tribe.own_species", "BUG-1271", g_own_species_fix,
         "tribe: a player's tribe members are of that player's own creature on every game (the host's save keeps the tribe's species)");

constexpr uintptr_t kTribeSlot = 0x18CC;                               // cTribe: the species profile of new members
constexpr uintptr_t kProfKey = 0x504, kProfModel = 0x510, kProfGen = 0x53C; // cSpeciesProfile
constexpr uintptr_t kSpProf = 0xB20, kSpKey = 0xB28;                   // cCreatureBase: its profile, its key (3 dwords)
constexpr double kScanGap = 0.15, kSeenMin = 0.5, kWorldSettle = 3.0;

struct Ours {
    uint32_t serial = 0;
    uint32_t key = 0; // the key the mod gave it (+0xB28 after SetSpecies)
};
static std::unordered_map<void*, Ours> g_sp;                              // members the mod changed
static std::unordered_map<void*, std::pair<uint32_t, double>> g_sp_seen;  // member -> serial, first seen
struct Kept {
    void* c;
    uint32_t serial;
    uint32_t key[3];
};
static std::vector<Kept> g_sp_kept; // a save is being written: the members' own keys
static uint32_t g_sp_epoch;
static uint32_t g_sp_slot_key[3]; // the slot's key of the last pass (a save while the game leaves Tribe: no tribe then)
static double g_sp_epoch_at = -100, g_sp_scan_at = -100;
static uint32_t g_sp_applied, g_sp_reverted, g_sp_wait_cine, g_sp_wait_key, g_sp_saves, g_sp_stripped, g_sp_back;
static uint32_t g_sp_pending; // members that need a change now (last pass)
static uintptr_t a_SpSetSpecies, a_SpToolAttach, a_SpUiGet, a_SpAnimFind, a_SpAnimSet;

WORLD_STATE("tribe.own_species", &g_own_species_fix, [] {
    g_sp.clear();
    g_sp_seen.clear();
    g_sp_kept.clear();
}, []() -> size_t { return g_sp.size(); });

static bool sp_alive(void* c, uint32_t serial) { return kept_alive(c, serial, kMemberNoun); }

static uint8_t* slot_profile(void* t)
{
    uint8_t* p = t && skg::readable((uint8_t*)t + kTribeSlot, 4) ? F<uint8_t*>(t, kTribeSlot) : nullptr;
    return p && skg::readable(p, kProfGen + 4) ? p : nullptr;
}

// WP-0 shim: the player's own creature (creations' shared key 0x7C......, the same on every game, as the Creature
// stage's puppets use) until species_line (BUG-1270, species::key(pid, Stage::Tribe)) is merged. {} = no own line:
// the host (the slot is its species), creature.mode host, a guest that plays the world's own species.
static creations::Key line_key(uint8_t pid, const creations::Key& slot)
{
    if (pid == 0 || pid >= 0xFE || creations::one_creature())
        return {};
    creations::Key k = creations::player_key(pid, creations::kCreature);
    if (!k.instance || k == slot || creations::host_species(k) == slot || creations::local_species(slot) == k)
        return {};
    return k;
}

// the profile of a line's key here (null: not buildable yet: its bundle is on its way)
static void* line_profile(const creations::Key& k)
{
    if (!k.instance || !creations::key_known(k))
        return nullptr;
    void* p = game::species_by_key({k.instance, k.type, k.group});
    return p && skg::readable(p, kProfGen + 4) ? p : nullptr;
}

// the game's own steps after a member's model was built again (the Tribal Outfitter's return 0xCDCC90 for every
// member, designs.cpp rebuild_members): its tool in the hand and its hand's animation slot again
static void after_rebuild(void* c)
{
    void* ui = a_SpUiGet ? skg::cdecl_call<void*>(a_SpUiGet) : nullptr;
    void* tool_ui = ui && skg::readable(ui, 0x244) ? F<void*>(ui, 0x240) : nullptr;
    if (tool_ui && a_SpToolAttach)
        thiscall<void>(a_SpToolAttach, tool_ui, c, F<int>(c, kHeldToolOff));
    if (a_SpAnimFind && a_SpAnimSet)
        if (void* a = thiscall<void*>(a_SpAnimFind, c, F<int>(c, 0xFC4), (uint32_t*)nullptr))
            thiscall<void>(a_SpAnimSet, c, a);
}

static void set_species(void* c, void* prof, bool revert)
{
    float hp0 = game::health(c), max0 = game::max_health(c);
    thiscall<bool>(a_SpSetSpecies, c, prof, F<int>(c, kAgeOff), true);
    after_rebuild(c);
    float max1 = game::max_health(c); // the share of health over the change (BUG-0928's rule for the avatar)
    if (!game::is_dead(c) && hp0 > 0 && max0 > 0 && max1 > 0 && max1 != max0)
        game::set_health(c, std::max(1.f, std::min(1.f, hp0 / max0) * max1));
    uint32_t key = F<uint32_t>(c, kSpKey);
    if (revert) {
        g_sp.erase(c);
        g_sp_reverted++;
    } else {
        g_sp[c] = {game::object_serial(c), key};
        g_sp_applied++;
    }
    H->log(M, SK_INFO, "tribe: member #%u (owner %d) is now of species %08X%s (BUG-1271)", member_id(c),
           (int)owner_of(c), key, revert ? ": the tribe's again" : "");
}

void species_tick(double now)
{
    if (!g_ok || game::mode_id() != modes::kTribe || !a_SpSetSpecies)
        return;
    uint32_t ep = game::world_epoch();
    if (ep != g_sp_epoch) {
        g_sp_epoch = ep;
        g_sp_epoch_at = now;
    }
    if (now - g_sp_scan_at < kScanGap)
        return;
    g_sp_scan_at = now;
    void* t = player_tribe();
    uint8_t* slot = slot_profile(t);
    if (!slot)
        return;
    for (auto it = g_sp.begin(); it != g_sp.end();)
        it = sp_alive(it->first, it->second.serial) ? std::next(it) : g_sp.erase(it);
    for (auto it = g_sp_seen.begin(); it != g_sp_seen.end();)
        it = sp_alive(it->first, it->second.first) ? std::next(it) : g_sp_seen.erase(it);
    bool connected = C.sess && C.sess->connected();
    creations::Key sk{F<uint32_t>(slot, kProfKey), F<uint32_t>(slot, kProfKey + 4), F<uint32_t>(slot, kProfKey + 8)};
    g_sp_slot_key[0] = sk.instance, g_sp_slot_key[1] = sk.type, g_sp_slot_key[2] = sk.group;
    bool settled = now - g_sp_epoch_at >= kWorldSettle && !stage_end();
    void* todo = nullptr;
    void* todo_prof = nullptr;
    bool todo_revert = false;
    uint32_t pending = 0;
    std::map<uint32_t, void*> profs; // (this pass: one lookup per key)
    for (void* c : members(t)) {
        uint32_t serial = game::object_serial(c);
        auto seen = g_sp_seen.find(c);
        if (seen == g_sp_seen.end() || seen->second.first != serial) {
            g_sp_seen[c] = {serial, now};
            continue; // (its body may still be built)
        }
        uint8_t o = connected ? owner_of(c) : 0xFF;
        creations::Key lk = g_own_species_fix && connected ? line_key(o, sk) : creations::Key{};
        void* lp = nullptr;
        if (lk.instance) {
            auto pi = profs.find(lk.instance);
            lp = pi != profs.end() ? pi->second : (profs[lk.instance] = line_profile(lk));
        }
        auto oi = g_sp.find(c);
        uint32_t ours = oi != g_sp.end() ? oi->second.key : 0;
        uint32_t want = lp ? F<uint32_t>(lp, kProfKey) : lk.instance; // (what SetSpecies writes: the profile's key)
        auto act = tribe_logic::species_act(want, F<uint32_t>(c, kSpKey), sk.instance, ours, lp != nullptr);
        if (act == tribe_logic::SpeciesAct::Keep)
            continue;
        if (act == tribe_logic::SpeciesAct::Wait) {
            if (lk.instance && !creations::key_known(lk) && is_guest())
                creations::want_species(lk); // (a guest's partner's creation comes with its bundle; asked anyway)
            g_sp_wait_key++;
            continue;
        }
        pending++;
        if (todo || !settled || now - seen->second.second < kSeenMin || game::is_dead(c) || !has_body(c))
            continue;
        todo = c;
        todo_prof = act == tribe_logic::SpeciesAct::Revert ? (void*)slot : lp;
        todo_revert = act == tribe_logic::SpeciesAct::Revert;
    }
    g_sp_pending = pending;
    if (!todo)
        return;
    if (world::cinematic_playing()) { // BUG-1173: SetSpecies under a cinematic left it playing for good
        g_sp_wait_cine++;
        return;
    }
    set_species(todo, todo_prof, todo_revert);
}

// ---- saves: the slot's key in every member the mod changed while the save is written
static void sp_save_start()
{
    g_sp_kept.clear();
    if (g_sp.empty())
        return;
    // (the quit's save may run while the game already left Tribe: the members are still listed, the tribe's key is
    // the last pass's then)
    uint8_t* slot = game::mode_id() == modes::kTribe ? slot_profile(player_tribe()) : nullptr;
    const uint32_t* sk = slot ? &F<uint32_t>(slot, kProfKey) : g_sp_slot_key;
    if (!sk[0])
        return;
    for (auto& [c, o] : g_sp) {
        if (!sp_alive(c, o.serial))
            continue;
        uint32_t* k = &F<uint32_t>(c, kSpKey);
        if (k[0] == sk[0] && k[1] == sk[1] && k[2] == sk[2])
            continue;
        g_sp_kept.push_back({c, o.serial, {k[0], k[1], k[2]}});
        k[0] = sk[0], k[1] = sk[1], k[2] = sk[2];
        g_sp_stripped++;
    }
    if (!g_sp_kept.empty()) {
        g_sp_saves++;
        H->log(M, SK_INFO, "tribe: %u members carry the tribe's species %08X in this save (BUG-1271)",
               (unsigned)g_sp_kept.size(), sk[0]);
    }
}
static void sp_save_end()
{
    for (auto& k : g_sp_kept)
        if (sp_alive(k.c, k.serial)) {
            uint32_t* p = &F<uint32_t>(k.c, kSpKey);
            p[0] = k.key[0], p[1] = k.key[1], p[2] = k.key[2];
            g_sp_back++;
        }
    g_sp_kept.clear();
}

void species_init()
{
    a_SpSetSpecies = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    a_SpToolAttach = H->sym("SporeCoop::TribeToolUI_AttachTool");
    a_SpUiGet = H->sym("SporeCoop::cTribeUI_Get");
    a_SpAnimFind = H->sym("SporeCoop::Creature_FindAnimSlot");
    a_SpAnimSet = H->sym("SporeCoop::Creature_RestartAnimSlot");
    if (!a_SpSetSpecies)
        H->log(M, SK_WARN, "tribe: no SetSpecies: every member stays the tribe's species (BUG-1271)");
    savestrip::add_save_hooks(sp_save_start, sp_save_end);
}

std::string species_status()
{
    char b[300];
    snprintf(b, sizeof b,
             "own species: fix=%d changed=%zu applied=%u reverted=%u pending=%u waits_cinematic=%u waits_key=%u "
             "saves=%u stripped=%u back=%u\n",
             (int)g_own_species_fix, g_sp.size(), g_sp_applied, g_sp_reverted, g_sp_pending, g_sp_wait_cine, g_sp_wait_key,
             g_sp_saves, g_sp_stripped, g_sp_back);
    return b;
}

// `coop.tribe species`: every member of the player tribe, its owner, its species key, its model key, what it should be
std::string species_list()
{
    std::string out;
    char b[260];
    void* t = player_tribe();
    uint8_t* slot = slot_profile(t);
    if (!slot)
        return "no player tribe\n" + species_status();
    creations::Key sk{F<uint32_t>(slot, kProfKey), F<uint32_t>(slot, kProfKey + 4), F<uint32_t>(slot, kProfKey + 8)};
    snprintf(b, sizeof b, "slot %p key=%08X model=%08X\n", (void*)slot, sk.instance, F<uint32_t>(slot, kProfModel));
    out += b;
    bool connected = C.sess && C.sess->connected();
    for (void* c : members(t)) {
        uint8_t o = connected ? owner_of(c) : 0xFF;
        creations::Key lk = g_own_species_fix && connected ? line_key(o, sk) : creations::Key{};
        uint8_t* p = F<uint8_t*>(c, kSpProf);
        bool pr = p && skg::readable(p, kProfGen + 4);
        auto oi = g_sp.find(c);
        snprintf(b, sizeof b, "S #%u %p owner=%d key=%08X model=%08X line=%08X ours=%08X age=%d body=%d dead=%d tool=%d\n",
                 member_id(c), c, o == 0xFF ? -1 : (int)o, F<uint32_t>(c, kSpKey), pr ? F<uint32_t>(p, kProfModel) : 0u,
                 lk.instance, oi == g_sp.end() ? 0u : oi->second.key, F<int>(c, kAgeOff), (int)has_body(c),
                 (int)game::is_dead(c), F<int>(c, kHeldToolOff));
        out += b;
    }
    return out + species_status();
}

} // namespace tribe
