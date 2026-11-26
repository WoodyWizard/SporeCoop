// BUG-1271 (WP-T1 of docs/plan-0.7.16.md): the members of the player tribe are of their owner's own species on
// every game (docs/re/tribe.md#tribe-two-species, #tribe-own-species).
//
// The game gives a new member the tribe's species slot (cTribe +0x18CC, Citizen_Init 0xC27330); a member's species is
// its own field (+0xB20 the profile, +0xB28 the key), its model, size and abilities follow it, and nothing of the game
// puts it back. Each game here (host and guests alike) gives every member whose owner (owner_of: the host's split, the
// guest's mirror table) has an own species line (species::key(owner, Stage::Tribe), docs/01-architecture.md
// #arch-species-line: its tribe outfit, else its creature) that line's species with cCreatureAnimal::SetSpecies(keepState 1),
// and a member the mod gave a line's species goes back to the slot's when its owner has none any more (a gift to the
// host, its owner left, creature.mode host, the switch off, the session ended). A key compare every 0.15 s finds
// what needs a change (a new member, a baby, an owner change, a world load, a rejoin, the outfitter; a change of a line
// starts the next pass at once, species::on_change); one member per
// pass (a body is built over several frames, BUG-0318), never while a cinematic plays (BUG-1173), never in the first
// 3 s of a world, never at the end of Tribe. Nothing is sent: every game knows the owners and the lines.
//
// Saves (owner decision 2026-11-18, docs/plan-0.7.16.md#plan-0716-decisions): while a save is written every member
// the mod changed (and any member that shows a key of a line, species::is_line_key) carries the slot's key in
// +0xB28..+0xB30 (the three dwords the save writes, no rebuild), its own
// key comes back on the next frame (the civ.host_buildings pattern): the host's save holds the host's species only,
// a guest's save the same; the guest's look comes back from the mod after the load.
// CORRECTION (2026-11-19, BUG-1315): this strip alone did not keep the key out (the record is written from +0xB28 after
// something set it from the profile again: docs/re/creations.md#creations-save-keys); savestrip's key filter
// (savestrip.keys) writes the slot's key at the stream for every object. The strip stays as a second guard.
//
// `coop.fix tribe.own_species off`: every member is the slot's species (0.7.15; members the mod changed go back).
//
// BUG-1299 (`tribe.birth_species`): a member whose owner is known when the game makes it (the host's baby of a known
// buyer, a guest's mirror of the host's member) is born in its owner's line species: the citizen's init 0xC27330 reads
// the tribe's slot through cTribe_SpeciesSlot 0xC8E9B0, which answers the line's profile while that spawn runs (only
// for the player tribe, slot 0, inside birth_begin / birth_end). The pass above then finds it already right; before,
// it showed the slot's species until the pass reached it (0.5 s of seeing it, one member per 0.15 s, 3 s after a world).
// Off: born in the slot's species, changed by the pass (0.7.16 before WP-S2).
#include "tribe.h"
#include "tribe_internal.h"
#include "tribe_logic.h"
#include "tribe_rivals.h"
#include "species_line.h"

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

static bool g_birth_fix = true;
COOP_FIX("tribe.birth_species", "BUG-1299", g_birth_fix,
         "tribe: a new member of a player with an own species is born in that species (no moment in the tribe's)");
// BUG-1313: cCreatureAnimal::SetSpecies 0xC03920 on a creature that is not grown up (+0xB34 != 1, no flag 0x200 in
// +0xB58) in the Creature or Tribe mode starts the growth timer at +0x1628 (0xBC3260: call [this+0x1C]); a tribe
// member (a citizen) has no such timer there: the call goes to a float (lab 20261119-105127, tribe_birth_species_off:
// 0x40A00000 on the guest, 0 on the host, both games right after the pass changed the baby). On: the pass never
// changes a kid (it waits until it is grown up; a kid of a known owner is born in its species, tribe.birth_species).
// Off: a kid is changed like an adult (0.7.16 before the fix: a fault on every game).
static bool g_kid_fix = true;
COOP_FIX("tribe.kid_species", "BUG-1313", g_kid_fix,
         "tribe: a tribe kid's species is never changed while it grows up (the game faulted on every game)");
static uint32_t g_sp_wait_kid;
// BUG-1319: a guest's mirror spawned while its world opened was born in a line's species (tribe.birth_species) and the
// world's reset (WORLD_STATE below) then forgot that the mod gave it that key; a member the mod never touched keeps
// its species, so when the mirror was bound to another player's member (lab 20261119-112300: the host's chief #1 on
// the guest's game wore the guest's creature for good) nothing put it back. On: a member that shows a line's key (a
// shared 0x7B / creations' 0x7C copy, or a key of a current line) and is not in the table is taken as the mod's (the
// game never gives a member such a key), so the pass makes it its owner's or the slot's again. Off: kept (0.7.16
// before the fix).
static bool g_stray_fix = true;
COOP_FIX("tribe.stray_line_key", "BUG-1319", g_stray_fix,
         "tribe: a member that wears a player's own species by mistake (another player's member) gets its owner's again");
static uint32_t g_sp_adopted;
using SpeciesSlot_t = void*(SK_THISCALL*)(void*, int);
static SpeciesSlot_t o_SpeciesSlot;
static void* g_birth_tribe;
static void* g_birth_prof;
static uint32_t g_birth_key, g_births, g_birth_answers;

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

// the player's Tribe species on this game (species::key: its tribe outfit, else its own creature; the same key on every
// game, registered here). {} = no own line: the host (the slot is its species), creature.mode host, a guest that plays
// the world's own species, an owner code (0xFE shared, 0xFF none), or the slot's own species
static creations::Key line_key(uint8_t pid, const creations::Key& slot)
{
    if (pid >= 0xFE)
        return {};
    creations::Key k = species::key(pid, species::Stage::Tribe);
    if (!k.instance || k == slot || creations::host_species(k) == slot || creations::local_species(slot) == k)
        return {};
    return k;
}
// BUG-1315: the world's species of Tribe for the save's key filter: the player tribe's slot (the last pass's while the
// game leaves Tribe)
static bool g_world_reg = species::set_world_species(species::Stage::Tribe, []() -> creations::Key {
    uint8_t* slot = game::mode_id() == modes::kTribe ? slot_profile(player_tribe()) : nullptr;
    if (slot)
        return {F<uint32_t>(slot, kProfKey), F<uint32_t>(slot, kProfKey + 4), F<uint32_t>(slot, kProfKey + 8)};
    return {g_sp_slot_key[0], g_sp_slot_key[1], g_sp_slot_key[2]};
});
// a change of a player's line: the next pass now (not in 0.15 s)
static bool g_line_reg = species::on_change("tribe.own_species", [](uint8_t) { g_sp_scan_at = -100; });

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

// BUG-1573: exported so tribe_rivals::swap_tribe_species re-attaches chieftain staff / tools to the new species' hand bone
void rebuild_member_hand(void* c)
{
    if (c && game::listed(c, 0x18EB4B7u))
        after_rebuild(c);
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

// cTribe_SpeciesSlot (thiscall cSpeciesProfile*(cTribe*, int i) ret 4): the line's profile while a birth of ours runs
static void* SK_THISCALL h_SpeciesSlot(void* t, int i)
{
    if (g_birth_prof && t == g_birth_tribe && i == 0) {
        g_birth_answers++;
        return g_birth_prof;
    }
    return o_SpeciesSlot(t, i);
}

void birth_begin(void* t, uint8_t owner)
{
    g_birth_tribe = nullptr, g_birth_prof = nullptr, g_birth_key = 0;
    if (!g_birth_fix || !g_own_species_fix || !o_SpeciesSlot || !t || t != player_tribe() || !C.sess ||
        !C.sess->connected() || game::mode_id() != modes::kTribe || owner >= 0xFE || world::cinematic_playing() ||
        tribe_rivals::active())
        return;
    uint8_t* slot = slot_profile(t);
    if (!slot)
        return;
    creations::Key sk{F<uint32_t>(slot, kProfKey), F<uint32_t>(slot, kProfKey + 4), F<uint32_t>(slot, kProfKey + 8)};
    void* lp = line_profile(line_key(owner, sk));
    if (!lp)
        return;
    g_birth_tribe = t, g_birth_prof = lp, g_birth_key = F<uint32_t>(lp, kProfKey);
}
void birth_end(const std::vector<void*>& made)
{
    if (g_birth_prof)
        for (void* c : made)
            if (c && skg::readable((uint8_t*)c + kSpKey, 12) && F<uint32_t>(c, kSpKey) == g_birth_key) {
                g_sp[c] = {game::object_serial(c), g_birth_key};
                g_births++;
                H->log(M, SK_INFO, "tribe: member #%u (owner %d) is born of species %08X (BUG-1299)", member_id(c),
                       (int)owner_of(c), g_birth_key);
            }
    g_birth_tribe = nullptr, g_birth_prof = nullptr, g_birth_key = 0;
}

void species_tick(double now)
{
    if (!g_ok || game::mode_id() != modes::kTribe || !a_SpSetSpecies)
        return;
    // BUG-1580: In Tribe Rivals each player owns a distinct cTribe whose species slot + members are managed by
    // tribe_rivals on both Host and Guest; skip Together-mode per-member own_species pass so it never fights or
    // reverts a player's rival tribe species.
    if (tribe_rivals::active())
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
        if (!ours && g_stray_fix) { // BUG-1319: a line's key the mod gave it before a world reset forgot that
            uint32_t* ck = &F<uint32_t>(c, kSpKey);
            if (ck[0] && ck[0] != sk.instance &&
                (species::is_shared(ck[0]) || species::is_copy(ck[0]) || species::is_line_key({ck[0], ck[1], ck[2]}))) {
                ours = ck[0];
                g_sp[c] = {serial, ours};
                g_sp_adopted++;
            }
        }
        uint32_t want = lp ? F<uint32_t>(lp, kProfKey) : lk.instance; // (what SetSpecies writes: the profile's key)
        auto act = tribe_logic::species_act(want, F<uint32_t>(c, kSpKey), sk.instance, ours, lp != nullptr);
        if (act == tribe_logic::SpeciesAct::Keep)
            continue;
        if (act == tribe_logic::SpeciesAct::Wait) { // (a line's key is registered here: its profile is being made)
            g_sp_wait_key++;
            continue;
        }
        if (g_kid_fix && F<int>(c, kAgeOff) != 1) { // BUG-1313: a kid waits until it is grown up
            g_sp_wait_kid++;
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
    if (g_sp.empty() && (game::mode_id() != modes::kTribe || species::players().empty()))
        return;
    // (the quit's save may run while the game already left Tribe: the members are still listed, the tribe's key is
    // the last pass's then)
    uint8_t* slot = game::mode_id() == modes::kTribe ? slot_profile(player_tribe()) : nullptr;
    const uint32_t* sk = slot ? &F<uint32_t>(slot, kProfKey) : g_sp_slot_key;
    if (!sk[0])
        return;
    auto strip = [&](void* c, uint32_t serial) {
        uint32_t* k = &F<uint32_t>(c, kSpKey);
        if (k[0] == sk[0] && k[1] == sk[1] && k[2] == sk[2])
            return;
        for (auto& x : g_sp_kept)
            if (x.c == c)
                return;
        g_sp_kept.push_back({c, serial, {k[0], k[1], k[2]}});
        k[0] = sk[0], k[1] = sk[1], k[2] = sk[2];
        g_sp_stripped++;
    };
    for (auto& [c, o] : g_sp)
        if (sp_alive(c, o.serial))
            strip(c, o.serial);
    // and any member of the player tribe that shows a key of a line without a record of ours (a guard: a guest's key
    // never reaches a save)
    if (slot)
        for (void* c : members(player_tribe())) {
            uint32_t* k = &F<uint32_t>(c, kSpKey);
            if (!g_sp.count(c) && species::is_line_key({k[0], k[1], k[2]}))
                strip(c, game::object_serial(c));
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
    if (uintptr_t a = H->sym("SporeCoop::cTribe_SpeciesSlot")) // BUG-1299
        H->hook_create(M, (void*)a, (void*)h_SpeciesSlot, (void**)&o_SpeciesSlot);
    savestrip::add_save_hooks(sp_save_start, sp_save_end);
}

std::string species_status()
{
    char b[340];
    snprintf(b, sizeof b,
             "own species: fix=%d changed=%zu applied=%u reverted=%u pending=%u waits_cinematic=%u waits_key=%u "
             "saves=%u stripped=%u back=%u births=%u birth_answers=%u waits_kid=%u adopted=%u\n",
             (int)g_own_species_fix, g_sp.size(), g_sp_applied, g_sp_reverted, g_sp_pending, g_sp_wait_cine, g_sp_wait_key,
             g_sp_saves, g_sp_stripped, g_sp_back, g_births, g_birth_answers, g_sp_wait_kid, g_sp_adopted);
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
