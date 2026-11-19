// BUG-0937: the Creature HUD's portrait (the square at the bottom right, window 0x03D99151) follows the avatar's
// species. The game sets it in its Creature HUD refresh (0xD2C000: UTFWin::Image::SetBackgroundByKey of the png
// {avatar species profile (+0xB20) +0x504, 0x2F7D0004, +0x50C}) when the stage starts; the mod's body swaps
// (cCreatureBase::SetSpecies on the avatar: the guest's own creature, the host's creature in creature.mode host and
// its evolved one) never ran it, so the guest's portrait kept the first species. On: twice a second, outside the
// frame, the portrait is set to the avatar's species png when the one it shows is another. Off: as before (0.7.12).
#include "hud_portrait.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "fixes.h"
#include "game.h"
#include "sporekit_game.hpp"
#include "sui.h"

namespace portrait {

using namespace skg;

static const SkHost* H;
static SkMod* M;
static std::function<bool()> g_creature_now;
static double g_next = 0;
static bool g_queued = false;
static uint32_t g_sets, g_checks, g_blind_sets;
static uint32_t g_blind_key[3]; // BUG-9264: the png last set while the window's picture could not be read
static uintptr_t a_set_by_key, a_get_image;

constexpr uint32_t kPortraitWin = 0x03D99151, kPngType = 0x2F7D0004;

bool g_portrait_fix = true;
COOP_FIX("creature.hud_portrait", "BUG-0937", g_portrait_fix, "creature: the HUD portrait shows the creature the player plays now (after its own creature or the host's new one replaced the first)");
// BUG-9264 (MSI 2026-11-19: a fresh guest's own first creature, the portrait kept the host's creature; `coop.portrait`
// said image=00000000 same=-1 sets=0): the window's picture could not be read: on the Steam / GOG 2024 build the
// portrait's drawable has the vtable 0x01C33248, not the GOG 2016 ImageDrawable's 0x01443248 that shows() checks (so
// BUG-0937 never set the portrait on the supported build), and "not known" left the game's picture. Now, when the picture cannot be read
// but the avatar's species png exists (GetImage), the portrait is set to it once per species. Off: as BUG-0937.
static bool g_blind_fix = true;
COOP_FIX("creature.hud_portrait_unread", "BUG-9264", g_blind_fix,
         "creature: the HUD portrait is set to the played creature's picture even when the picture it shows cannot be read");

// the png key of the avatar's species, as the game's HUD refresh builds it
static bool avatar_key(uint32_t k[3])
{
    void* av = game::avatar();
    if (!av || !readable(av, 0xB24))
        return false;
    uint8_t* sp = field<uint8_t*>(av, 0xB20);
    if (!sp || !readable(sp + 0x504, 0xC))
        return false;
    k[0] = *(uint32_t*)(sp + 0x504);
    k[1] = kPngType;
    k[2] = *(uint32_t*)(sp + 0x50C);
    return k[0] != 0;
}

static sui::Win window()
{
    sui::Win root = sui::main_window();
    sui::Win w = root ? sui::find(root, kPortraitWin) : nullptr;
    return w && sui::visible(w) ? w : nullptr;
}

// whether the window draws the avatar's species png: the window's drawable (UTFWin::ImageDrawable, vtable 0x01443248)
// holds its UTFWin::Image at +0x24 (runtime 2026-11-12: an object with the size 67 x 68 at +0x20 / +0x24, the
// portrait's size); UTFWin::Image::GetImage of a key answers the game's one Image of that png, so the two pointers
// are the same when the portrait shows it. -1: not known (no drawable, no image of that key)
static int shows(sui::Win w, const uint32_t key[3], void** shown_img = nullptr)
{
    if (!a_get_image)
        return -1;
    void* want = nullptr;
    uint32_t k[3] = {key[0], key[1], key[2]};
    if (!cdecl_call<bool>(a_get_image, k, &want, false, -1, -1) || !want)
        return -1; // the png is not there: nothing to show
    uint8_t* d = (uint8_t*)sui::drawable(w);
    if (!d || !readable(d, 0x28) || *(uint32_t*)d != 0x01443248) {
        vcall<void>(want, 1);
        return -2; // BUG-9264: the window's picture cannot be read
    }
    void* img = *(void**)(d + 0x24);
    if (shown_img)
        *shown_img = img;
    bool same = want == img;
    vcall<void>(want, 1); // the reference GetImage gave
    return same ? 1 : 0;
}

static void task(void*)
{
    g_queued = false;
    if (!g_creature_now || !g_creature_now()) {
        memset(g_blind_key, 0, sizeof g_blind_key); // (a new stage or load: set again)
        return;
    }
    sui::Win w = window();
    uint32_t want[3];
    if (!w || !avatar_key(want))
        return;
    g_checks++;
    int same = shows(w, want);
    if (same == -2 && g_blind_fix) { // BUG-9264: set once per species (the picture it shows is not known)
        if (!g_portrait_fix || !a_set_by_key || memcmp(g_blind_key, want, sizeof want) == 0)
            return;
        memcpy(g_blind_key, want, sizeof want);
        cdecl_call<bool>(a_set_by_key, w, (const uint32_t*)want, -1);
        g_sets++;
        g_blind_sets++;
        H->log(M, SK_INFO, "portrait: set to %08X!%08X (its picture could not be read, BUG-9264)", want[2], want[0]);
        return;
    }
    if (!g_portrait_fix || !a_set_by_key || same != 0) // (-1: the png is not there: the game's own picture stays)
        return;
    // UTFWin::Image::SetBackgroundByKey(IWindow*, const ResourceKey&, int index): cdecl, the HUD refresh passes -1
    cdecl_call<bool>(a_set_by_key, w, (const uint32_t*)want, -1);
    g_sets++;
}

void tick(double now)
{
    if (now < g_next || g_queued || !a_set_by_key)
        return;
    g_next = now + 0.5;
    g_queued = true;
    H->run_outside_frame(task, nullptr);
}

#define SAY(...) H->out_printf(out, __VA_ARGS__)
static int command(void*, int, const char* const*, SkOut* out)
{
    sui::Win w = window();
    uint32_t want[3] = {0, 0, 0};
    bool have = avatar_key(want);
    void* img = nullptr;
    int same = w && have ? shows(w, want, &img) : -1;
    // same: 1 the portrait draws the png of the avatar's species, 0 another picture, -1 not known
    uint8_t* d = w ? (uint8_t*)sui::drawable(w) : nullptr;
    SAY("portrait window=%d drawable=%p vt=%08X image=%p avatar=%d key=%08X!%08X.%08X same=%d fix=%d sets=%u (unread %u) "
        "checks=%u\n", w ? 1 : 0, (void*)d, d && readable(d, 4) ? *(uint32_t*)d : 0, img, (int)have, want[2], want[0],
        want[1], same, (int)g_portrait_fix, g_sets, g_blind_sets, g_checks);
    return 0;
}

void init(const SkHost* host, SkMod* mod, std::function<bool()> creature_now)
{
    H = host;
    M = mod;
    g_creature_now = std::move(creature_now);
    a_set_by_key = H->sym("UTFWin::Image::SetBackgroundByKey");
    a_get_image = H->sym("UTFWin::Image::GetImage");
    H->command_add(M, "coop.portrait",
                   "coop.portrait - BUG-0937: the Creature HUD portrait's image key against the avatar's species",
                   command, nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace portrait
