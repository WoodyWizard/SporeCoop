// WP-C1 of docs/plan-0.7.16.md (BUG-1273): the citizens of a city are of its owner's species.
//
// A city's citizens are a pool of the city near the camera: CityCitizens_Sync 0xD5BF50 gives each the species
// City_Species 0xBD9F90 answers (the civilization's key +0x268) and puts it back whenever it runs, so a citizen's
// species can not be set one by one (docs/re/civ.md#civ-own-species). City_Species has five callers, all citizen
// code: the hook answers per city. A city owned by a player with an own species line (not the host, whose cities keep
// the civilization's species: the world's line) answers that player's Civ species (species::key(owner, Stage::Civ),
// docs/01-architecture.md#arch-species-line: its Civ outfit, else its tribe line); every game knows the city owners
// (g_city_owner), so each game makes its own pool of the right species, nothing is sent per citizen, nothing is
// written into the civilization. The shared capital (owner kShared) is the founder's: the host's (the Civ stage
// starts from the host's tribe); a captured or given city is its new owner's. An owner change (a capture, a gift, the
// cities dealt anew) or a species that arrives later is seen by a check of the citizen manager's city every 0.5 s:
// when one of its citizens is not of the species the city answers now, the city's sync runs once (outside the frame);
// a change of a line (species::on_change) looks the players' profiles up again at the next check.
// BUG-1321: a line creation is a new key on every game, and nothing of the game bakes it before the citizens are made
// of it: citizens of an unbaked species are drawn white (no paint) and stay so. A line's species is asked to be baked
// as soon as it is known here and the city answers it only once the game has baked it (until then the civilization's;
// the check then resyncs the city with the baked model). `coop.fix civ.own_species_bake off`: answered at once.
// `creature.mode` host (F5) keeps today's behaviour (every city the civilization's species).
// `coop.fix civ.own_species off`: every city the civilization's species (0.7.15).
#include "civ_internal.h"
#include "species_line.h"

namespace civ {

bool g_fix_own_species = true;
COOP_FIX("civ.own_species", "BUG-1273", g_fix_own_species,
         "civ: the citizens of a city a guest owns (captured, given, dealt) are of that guest's species on every game");

constexpr uintptr_t kSpawnerCitizens = 0xA4;   // PlanetEco_HerdSpawner() +0xA4: the city citizen manager
constexpr uintptr_t kMgrCity = 0xAF0;          // the manager's city (the one near the camera)
constexpr uintptr_t kMgrCitizens = 0xA64;      // its citizens (vector begin, end)
constexpr uintptr_t kCivSpeciesKey = 0x268;    // cCivilization: the species key (Civ_Species)
constexpr uintptr_t kCitizenCity = 0x1010;     // cCreatureCitizen: its city (Citizen_Init)
constexpr uint32_t kCitizenNoun = 0x18EB4B7u;  // cCreatureCitizen
constexpr uintptr_t kMgrExtra = 0xA78, kMgrExtraStride = 0x14; // the manager's 4 special citizens (vectors)
constexpr int kMgrExtras = 4;
constexpr double kCheckEvery = 0.5, kResyncGap = 3.0, kLineEvery = 1.0;

using CitySpecies_t = void*(SK_THISCALL*)(void*);
static CitySpecies_t o_CitySpecies;
static uintptr_t a_CitySpecies, a_CitizensSync, a_HerdSpawner, a_SetSpeciesProfile;

// BUG-1321: the line's species is answered only once the game has baked its model (white citizens otherwise)
static bool g_fix_own_species_bake = true;
COOP_FIX("civ.own_species_bake", "BUG-1321", g_fix_own_species_bake,
         "civ: a guest's city takes the guest's species once the game has baked its model (else the citizens are white)");

struct Line {
    creations::Key key; // the player's Civ species ({} none: the city answers the civilization's)
    void* prof = nullptr;
    bool baked = false;   // the game's bake of its model is done (BUG-1321)
    double asked = -1;    // when the bake was first asked for
};
static Line g_line[net::kMaxPlayers];
static double g_line_at = -100, g_check_at = -100;
static bool g_check_queued;
static void* g_resync_city;
static double g_resync_at = -100;
static uint32_t g_hook_calls, g_answered, g_resyncs, g_checks, g_line_misses, g_extra_set, g_bake_asked, g_bake_waits;

// the player's Civ species on this game (species::key: the same key on every game, registered here). The host (and a
// guest playing the host's creature, creature.mode host) has none: its cities keep the civilization's species.
static creations::Key line_key(uint8_t pid) { return species::key(pid, species::Stage::Civ); }

static bool own_mode() { return g_fix_own_species && species::own_mode(); }
// BUG-1315: the world's species of Civ for the save's key filter: the player civilization's
static bool g_world_reg = species::set_world_species(species::Stage::Civ, []() -> creations::Key {
    void* pc = game::mode_id() == modes::kCiv ? player_civ() : nullptr;
    if (!pc || IsBadReadPtr((uint8_t*)pc + kCivSpeciesKey, 12))
        return {};
    auto& k = F<skg::ResourceKey>(pc, kCivSpeciesKey);
    return {k.instance, k.type, k.group};
});
// a change of a player's line: its profile is looked up again at the next check
static bool g_line_reg = species::on_change("civ.own_species", [](uint8_t) { g_line_at = -100; });

// the species the owner's line gives a city (null: the game's own answer)
static void* owner_species(void* city)
{
    if (!own_mode() || game::mode_id() != modes::kCiv)
        return nullptr;
    int o = civlogic::citizen_line_owner(owner_of_city(city), true, net::kMaxPlayers);
    if (o < 0)
        return nullptr; // (the shared capital is its founder's: the civilization's)
    if (g_line[o].prof && !g_line[o].baked && g_fix_own_species_bake) {
        g_bake_waits++;
        return nullptr; // (not baked yet: the civilization's species until it is, BUG-1321)
    }
    return g_line[o].prof;
}

static void* SK_THISCALL h_CitySpecies(void* city)
{
    g_hook_calls++;
    if (void* p = city ? owner_species(city) : nullptr) {
        g_answered++;
        return p;
    }
    return o_CitySpecies(city);
}

static void clear_lines()
{
    for (auto& l : g_line)
        l = Line{};
    g_line_at = -100;
}
// (the profiles are looked up again in the next world; owner_species answers nothing meanwhile)
LIFECYCLE_HOOK("civ.own_species", lifecycle::kSessionEnd | lifecycle::kWorldChange, -43,
               [](const lifecycle::Ctx&) { clear_lines(); }, false);

static void refresh_lines()
{
    if (!C.sess)
        return;
    bool here[net::kMaxPlayers] = {};
    for (auto& p : C.sess->players())
        if (p.id < net::kMaxPlayers)
            here[p.id] = true;
    for (int i = 0; i < net::kMaxPlayers; i++) {
        creations::Key k = here[i] && own_mode() ? line_key((uint8_t)i) : creations::Key{};
        Line& l = g_line[i];
        if (!k.instance) {
            l = Line{};
            continue;
        }
        if (l.prof && l.key == k && l.baked)
            continue;
        if (!(l.prof && l.key == k)) {
            l = Line{};
            l.key = k;
            l.prof = creations::key_known(k) ? game::species_by_key({k.instance, k.type, k.group}) : nullptr;
            if (!l.prof) {
                g_line_misses++; // (not registered here yet: asked again in a second)
                continue;
            }
        }
        // BUG-1321: the game's bake of the species' model, asked for at once (the bake takes seconds; a capture later
        // finds it done)
        int st = game::model_baked({k.instance, k.type, k.group}, true);
        if (l.asked < 0) {
            l.asked = H->time_seconds();
            if (st == 0)
                g_bake_asked++;
        }
        l.baked = st != 0; // (-1: no bake manager to ask: as before)
        if (l.baked && st == 1)
            H->log(M, SK_INFO, "civ: player %d's Civ species %08X is baked here (%.1f s after it was known, BUG-1321)", i,
                   k.instance, H->time_seconds() - l.asked);
    }
}

static void* citizen_mgr()
{
    void* sp = a_HerdSpawner ? skg::cdecl_call<void*>(a_HerdSpawner) : nullptr;
    void* mgr = sp && !IsBadReadPtr(sp, kSpawnerCitizens + 4) ? F<void*>(sp, kSpawnerCitizens) : nullptr;
    return mgr && !IsBadReadPtr(mgr, kMgrCity + 4) ? mgr : nullptr;
}

// outside the frame: the citizen manager's city, its citizens against the species the city answers now
static void check_task(void*)
{
    g_check_queued = false;
    if (!active() || !a_CitizensSync)
        return;
    double now = H->time_seconds();
    if (now - g_line_at >= kLineEvery) {
        g_line_at = now;
        refresh_lines();
    }
    void* mgr = citizen_mgr();
    void* city = mgr ? F<void*>(mgr, kMgrCity) : nullptr;
    if (!city || !game::listed(city))
        return;
    g_checks++;
    void* want = thiscall<void*>(a_CitySpecies, city); // (through the hook)
    if (!want)
        return;
    auto other = [&](void* c) { return c && !IsBadReadPtr(c, game::kSpeciesOff + 4) && game::species_profile(c) != want; };
    bool differs = false;
    for (void* c : vec_of((uint8_t*)mgr + kMgrCitizens))
        differs = differs || other(c);
    // the manager's four special citizens (vectors at +0xA78 + i * 0x14, made once per city visit by 0xD5C6D0 through
    // Citizen_Init with the city's species then): CityCitizens_Sync never re-sets them (lab 20261118-180539: 4 of the
    // 19 citizens of a captured city kept the old species after its sync)
    std::vector<void*> extra;
    for (int i = 0; i < kMgrExtras; i++)
        for (void* c : vec_of((uint8_t*)mgr + kMgrExtra + i * kMgrExtraStride))
            if (other(c))
                extra.push_back(c);
    if ((!differs && extra.empty()) || (city == g_resync_city && now - g_resync_at < kResyncGap))
        return;
    g_resync_city = city;
    g_resync_at = now;
    g_resyncs++;
    H->log(M, SK_INFO, "civ: city %d (owner %u): its citizens take the species %p (civ.own_species; pool %d, special %u)",
           host_index_of_city(city), (unsigned)owner_of_city(city), want, (int)differs, (unsigned)extra.size());
    if (differs)
        thiscall<void>(a_CitizensSync, mgr, false); // the game's own sync: count and species (no owner / order reset)
    if (a_SetSpeciesProfile)
        for (void* c : extra)
            if (game::listed(c, kCitizenNoun)) {
                thiscall<void>(a_SetSpeciesProfile, c, want); // as Citizen_Init gives it
                g_extra_set++;
            }
}

void species_tick(double now)
{
    if (!a_CitySpecies || g_check_queued || now - g_check_at < kCheckEvery)
        return;
    g_check_at = now;
    g_check_queued = true;
    H->run_outside_frame(check_task, nullptr); // SetSpeciesProfile rebuilds models: outside the frame (CLAUDE.md 2)
}

// Saves (owner decision 2026-11-18: the host's save holds the host's species only). The city citizens near the camera
// are written into a Civ save with their species key (lab 20261118-180539: the host's save had the guest's key in its
// citizen records, noun 0x18EB4B7): while a save is written every citizen of a guest's line gets the player
// civilization's key in +0xB28..+0xB30 (the three dwords only, no rebuild) and its own back at the end (the
// civ.host_buildings pattern).
// CORRECTION (2026-11-19, BUG-1315): lab 20261119-113320 stripped 19 citizens and 30 ms later the save's key writer read
// the guest's key from those citizens' +0xB28 again (docs/re/creations.md#creations-save-keys): the save is kept clean
// by savestrip's key filter (savestrip.keys, the civilization's species for a line key); this strip stays a guard.
struct Stripped {
    void* c;
    skg::ResourceKey k;
};
static std::vector<Stripped> g_stripped;
static uint32_t g_save_stripped;
// `coop.fix civ.own_species_save off`: the citizens are saved with the guest's key (the save check of
// coop_civ_own_species.py fails: lab 20261118-180539, before the strip existed)
static bool g_fix_own_species_save = true;
COOP_FIX("civ.own_species_save", "BUG-1273", g_fix_own_species_save,
         "civ: a guest's species never reaches a save through the citizens of its city (the civilization's key while saving)");
static uint32_t g_save_starts, g_save_seen, g_save_nociv;
static void species_save_start()
{
    g_stripped.clear();
    g_save_starts++;
    void* pc = g_fix_own_species_save && a_CitySpecies && game::mode_id() == modes::kCiv ? player_civ() : nullptr;
    if (!pc) {
        g_save_nociv++;
        return;
    }
    struct U {
        skg::ResourceKey world;
    } u{F<skg::ResourceKey>(pc, kCivSpeciesKey)};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            if (noun != kCitizenNoun || game::is_destroyed(o) || IsBadReadPtr(o, game::kSpeciesKeyOff + 12))
                return true;
            auto& own = F<skg::ResourceKey>(o, game::kSpeciesKeyOff);
            g_save_seen++;
            bool ours = species::is_line_key({own.instance, own.type, own.group}); // (any line here, a left player's too)
            for (int i = 1; i < net::kMaxPlayers; i++)
                ours = ours || (g_line[i].key.instance && own.instance == g_line[i].key.instance &&
                                own.group == g_line[i].key.group);
            if (!ours)
                return true; // (the game's own species: the civilization's, an NPC's, a tribe member's)
            g_stripped.push_back({o, own});
            own = ((U*)p)->world;
            return true;
        },
        &u);
    g_save_stripped += (uint32_t)g_stripped.size();
    H->log(M, SK_INFO, "civ: %u citizens carry the civilization's species %08X in this save (BUG-1273)",
           (unsigned)g_stripped.size(), u.world.instance);
}
static void species_save_end()
{
    for (auto& x : g_stripped)
        if (game::listed(x.c, kCitizenNoun))
            F<skg::ResourceKey>(x.c, game::kSpeciesKeyOff) = x.k;
    g_stripped.clear();
}

bool species_init()
{
    a_CitySpecies = H->sym("SporeCoop::City_Species");
    a_CitizensSync = H->sym("SporeCoop::CityCitizens_Sync");
    a_HerdSpawner = H->sym("SporeCoop::PlanetEco_HerdSpawner");
    a_SetSpeciesProfile = H->sym("SporeCoop::CreatureBase_SetSpeciesProfile"); // (the 4 special citizens)
    if (!a_CitySpecies || !a_CitizensSync || !a_HerdSpawner) {
        H->log(M, SK_WARN, "civ: no City_Species / CityCitizens_Sync / PlanetEco_HerdSpawner: citizens keep the civilization's species");
        a_CitySpecies = 0;
        return false;
    }
    H->hook_create(M, (void*)a_CitySpecies, (void*)h_CitySpecies, (void**)&o_CitySpecies);
    savestrip::add_save_hooks(species_save_start, species_save_end);
    return true;
}

// coop.civ citizens: the lines, the manager's city, and every citizen object by its city and species key
std::string species_debug()
{
    std::string s;
    char b[400];
    snprintf(b, sizeof b, "own_species fix=%d mode_own=%d hooked=%d calls=%u answered=%u checks=%u resyncs=%u special_set=%u misses=%u save_stripped=%u save_starts=%u save_seen=%u save_nociv=%u bake_fix=%d bake_asked=%u bake_waits=%u\n",
             (int)g_fix_own_species, (int)species::own_mode(), (int)(a_CitySpecies != 0), g_hook_calls, g_answered,
             g_checks, g_resyncs, g_extra_set, g_line_misses, g_save_stripped, g_save_starts, g_save_seen, g_save_nociv,
             (int)g_fix_own_species_bake, g_bake_asked, g_bake_waits);
    s += b;
    for (int i = 0; i < net::kMaxPlayers; i++)
        if (g_line[i].key.instance) {
            snprintf(b, sizeof b, "line %d key=%08X prof=%p baked=%d\n", i, g_line[i].key.instance, g_line[i].prof,
                     (int)g_line[i].baked);
            s += b;
        }
    if (!active())
        return s + "not in a Civ session\n";
    void* pc = player_civ();
    snprintf(b, sizeof b, "civ species key=%08X\n", pc ? F<uint32_t>(pc, kCivSpeciesKey) : 0u);
    s += b;
    void* mgr = citizen_mgr();
    void* city = mgr ? F<void*>(mgr, kMgrCity) : nullptr;
    if (city) {
        void* civ = F<void*>(city, kCityCiv);
        snprintf(b, sizeof b, "mgr city=%d owner=%d civ_key=%08X answer=%p citizens=%u\n", host_index_of_city(city),
                 (int)owner_of_city(city), civ && !IsBadReadPtr(civ, kCivSpeciesKey + 4) ? F<uint32_t>(civ, kCivSpeciesKey) : 0u,
                 thiscall<void*>(a_CitySpecies, city), (unsigned)vec_of((uint8_t*)mgr + kMgrCitizens).size());
        s += b;
    } else
        s += "mgr city=none\n";
    // every citizen object: (its city's host index, species key) -> count
    struct U {
        std::map<std::pair<int, uint32_t>, int> n;
        std::map<std::pair<int, uint32_t>, game::Key> key;
    } u;
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            if (noun != kCitizenNoun || game::is_destroyed(o) || IsBadReadPtr(o, kCitizenCity + 4))
                return true;
            void* c = F<void*>(o, kCitizenCity);
            int ci = c ? host_index_of_city(c) : -2;
            game::Key k = game::species_key(o);
            ((U*)p)->n[{ci, k.instance}]++;
            ((U*)p)->key[{ci, k.instance}] = k;
            return true;
        },
        &u);
    for (auto& [k, n] : u.n) {
        // baked: the game's own bake state of that species' model now (BUG-1321: 0 = drawn white, no paint)
        snprintf(b, sizeof b, "citizens city=%d key=%08X n=%d baked=%d\n", k.first, k.second, n,
                 game::model_baked(u.key[k], false));
        s += b;
    }
    return s;
}

} // namespace civ
