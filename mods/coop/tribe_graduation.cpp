// The end of Tribe's graduation (TribeStage_Graduate, BUG-1325; docs/re/tribe.md#tribe-graduation-cinematic).
//
// After the City Hall editor the Tribe stage's enter states (TribeStage_EnterUpdate 0xCDE7E0, state 7, stage +0x3A0
// set) call TribeStage_Graduate 0xCDD0F0 once: it destroys the player tribe's food mat (+0x260, then cleared), takes the
// village down for Civ, sets the actors of TRG2CVG_ShowCityHall (ids 0x66C82C23, 0x3D7F90A, 0xCD5F0CA4, registered for
// the cinematic that plays now, cCinematicManager +0x148: none) and plays it with PlayCinematic(name, 1, 0, ...): no
// queue flag, so while another cinematic plays the manager drops it (0xADFCE0 returns 0). Its end message 0x44F1189 is
// what submits the vehicle editor (listener 0xCD1D60); without it the stage stays in Tribe with the advance button up,
// and the next advance plays TRG2CVG_PreEditor, the City Hall editor and Graduate again, which faults in
// cGameNounManager::DestroyInstance on the null mat (lab 20261119-120449: TRG_PRE_FirstTribeAppears of the first NPC
// tribe, FormTribe 0xCC6440, was playing when the host came back from the City Hall editor).
//
// tribe.city_hall_cinematic: a cinematic that plays when Graduate starts is ended first (the game's EndCinematic, as
// its OK does: its end message goes out), so ShowCityHall plays and the vehicle editor follows. Off: as 0.7.15.
// The second layer (a graduation that runs again) is gamefixes' game.destroy_null.
#include "tribe_internal.h"
#include "gamefixes.h"

namespace tribe {

static bool g_city_hall_cine_fix = true;
COOP_FIX("tribe.city_hall_cinematic", "BUG-1325", g_city_hall_cine_fix,
         "tribe: the end of Tribe's City Hall cinematic plays even when another cinematic plays at that moment (the vehicle editor follows)");

constexpr uint32_t kShowCityHallId = 0x10792A72u; // registry id (FNV of the lower-case name) of TRG2CVG_ShowCityHall
constexpr uintptr_t kFoodMatOff = 0x260;          // cTribe::mpFoodMat

using Graduate_t = void(__fastcall*)(void*);
static Graduate_t o_Graduate;
static uint32_t g_graduations, g_graduations_no_mat, g_cines_ended, g_city_hall_played, g_city_hall_missed;
static uint32_t g_ended_id, g_after_id;
static std::string g_plant; // tests: a cinematic the next graduation starts first (as FirstTribeAppears did)

static void __fastcall h_Graduate(void* stage)
{
    g_graduations++;
    void* pt = player_tribe();
    bool mat = pt && skg::readable((uint8_t*)pt + kFoodMatOff, 4) && F<void*>(pt, kFoodMatOff);
    if (!mat && g_graduations_no_mat++ < 5) {
        H->log(M, SK_WARN, "tribe: the end of Tribe runs again (graduation %u) and the player tribe has no food mat any "
                           "more: the City Hall editor came twice (BUG-1325)", g_graduations);
    }
    if (!g_plant.empty()) { // tests (coop.tribe gradcine <name>): the race of lab 20261119-120449 on demand
        uintptr_t get = H->sym("Simulator::cCinematicManager::Get"), play = H->sym("Simulator::cCinematicManager::PlayCinematic");
        void* cm = get ? skg::cdecl_call<void*>(get) : nullptr;
        if (cm && play)
            thiscall<int>(play, cm, g_plant.c_str(), 1, 0, 0, 0, 0);
        H->log(M, SK_INFO, "tribe: tests: %s started right before the graduation (now %08X)", g_plant.c_str(), world::cinematic_id());
        g_plant.clear();
    }
    if (g_city_hall_cine_fix && world::cinematic_playing()) {
        g_ended_id = world::cinematic_id();
        if (world::end_cinematic("when the end of Tribe starts (its City Hall cinematic would not play, BUG-1325)"))
            g_cines_ended++;
    }
    o_Graduate(stage);
    g_after_id = world::cinematic_id();
    if (g_after_id == kShowCityHallId)
        g_city_hall_played++;
    else
        g_city_hall_missed++;
    if (g_graduations <= 10) // (a graduation that faults is called again every frame: a storm of lines otherwise)
        H->log(M, g_after_id == kShowCityHallId ? SK_INFO : SK_WARN,
               "tribe: the end of Tribe (graduation %u): the City Hall's cinematic %s (playing %08X)%s", g_graduations,
               g_after_id == kShowCityHallId ? "plays" : "did not start: no vehicle editor will follow", g_after_id,
               g_after_id == kShowCityHallId ? "" : " (BUG-1325)");
}

void graduation_init()
{
    if (uintptr_t a = H->sym("SporeCoop::TribeStage_Graduate")) // BUG-1325 (optional)
        H->hook_create(M, (void*)a, (void*)h_Graduate, (void**)&o_Graduate);
}

// coop.tribe graduation / gradcine <name> (tests)
std::string graduation_cmd(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[0] == "gradcine") {
        g_plant = a[1];
        return "armed: the next graduation starts " + g_plant + " first";
    }
    uintptr_t null_from = 0;
    unsigned nulls = gamefixes::destroy_nulls(&null_from);
    char b[400];
    snprintf(b, sizeof b,
             "graduation: hooked=%d runs=%u without_mat=%u other_cinematics_ended=%u (last %08X) city_hall_played=%u "
             "city_hall_missed=%u last_after=%08X planted=%s fix=%d null_destroys_skipped=%u (last from %08X)\n",
             (int)(o_Graduate != nullptr), g_graduations, g_graduations_no_mat, g_cines_ended, g_ended_id, g_city_hall_played,
             g_city_hall_missed, g_after_id, g_plant.empty() ? "-" : g_plant.c_str(), (int)g_city_hall_cine_fix, nulls,
             (unsigned)null_from);
    return b;
}

} // namespace tribe
