// BUG-1328 (docs/re/creations.md#creations-props-key0): the property list of creation key 0.
//
// A creation's properties (its name 0x75ED3A1C, the editor flags 0x2E0FCC74 / 0x99524FAF / 0xAE773A29, the captain's
// item config 0xC339D5C7, ...) are a .prop {instance, 0x00B1B104, group & ~0x1A00 | 0xE500}, read by
// SporeCoop::CaptainProps_Get 0x5BF1E0 and written by SporeCoop::CaptainProps_Save 0x5BF220 (into the creation's own
// database, else the profile's Pollination.package 0x11AC19C, where it stays). "Name Your Captain" writes one for the
// new captain (SporeCoop::NameCaptainDialog_OnButton 0xE1DC60: CaptainProps_Copy 0x5BF4A0 of the template 0x15A51D8,
// then CaptainProps_SetName 0xEEBE90 with the dialog's name); when the world's species cannot be copied the captain
// key is all zero (BUG-1111) and the list is still written: {0, 0x00B1B104, 0x0000E500} with the captain's name lands
// in the player's Pollination.package for good. Every editor opened without a creation key (an EditorRequest +0x10 of
// 0/0/0: the end of Tribe's City Hall and vehicle editors, our own arrival editors of WP-X1) then reads that list in
// Editors::cEditor::OnEnter (0x58F9CD): a non-empty name in it makes the name panel read-only
// (UI::EditorNamePanel::Initialize's +0x11 = 0, +0x20 = 0): no name field, no dice (the random name), and "You must
// give your creation a name before you can save" for ever (lab lane 5, whose guest profile had such a list named
// "Breer" from the space_captain_none test of 2026-11-16/17: line_stages_outfit / line_stages_editors /
// civ_own_arrival never left the guest's City Hall editor there). The game's own end-of-Tribe City Hall editor has no
// red cross: such a profile cannot leave the Tribe stage.
//
// The fix (`game.creation_props_key0`): CaptainProps_Get answers "no list" for a key whose instance is 0 (no creation
// has instance 0; an unpolluted profile answers the same), so the editors start with an empty, editable name and the
// dice. The list stays in the file (the game's own data: nothing of the player's profile is rewritten). Off: the
// game's code (rel/0.7.16 before the fix).
// `coop.creationprops`: the guard's count and whether this game has a list for key 0; tests: `plant <name>` writes one
// the way the captain dialog does, `remove` takes it out again.
#include "creation_props.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "fixes.h"
#include "sporekit_game.hpp"

namespace creationprops {

using skg::readable;

static const SkHost* H;
static SkMod* M;

constexpr uint32_t kPropsGroup = 0x0000E500; // the props group of a key whose group is 0 (0 & ~0x1A00 | 0xE500)
constexpr uint32_t kPollinationDb = 0x011AC19C; // the profile's Pollination.package (CaptainProps_Save's fallback)

using PropsGetFn = bool(__cdecl*)(const uint32_t* key, void** out);
static PropsGetFn g_get_orig;
static bool g_fix = true;
COOP_FIX("game.creation_props_key0", "BUG-1328", g_fix,
         "gamefixes: a creation property list under key 0 (a captain named while the world's species could not be "
         "copied) no longer makes every new creation's name panel read-only (no name field, no dice)");
static std::atomic<unsigned> g_refused{0};

static bool __cdecl props_get_guard(const uint32_t* key, void** out)
{
    if (g_fix && key && readable(key, 12) && !key[0]) {
        unsigned n = ++g_refused;
        if (n <= 4 || n % 500 == 0)
            H->log(M, SK_INFO, "creation props: the property list of key 0/%08X/%08X is not a creation's: none (BUG-1328, %u)",
                   key[1], key[2], n);
        return false;
    }
    return g_get_orig(key, out);
}

static void* prop_manager()
{
    static uintptr_t get = H->sym("App::IPropManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

static void release(void* obj)
{
    if (obj && readable(obj, 4))
        skg::vcall<int>(obj, 1); // Release
}

// the game's own list for {0, 0x00B1B104, 0xE500} (not through the guard), AddRef'd; null when there is none
static void* key0_list()
{
    static uintptr_t get = H->sym("App::cPropManager::GetPropertyList");
    void* pm = prop_manager();
    void* list = nullptr;
    if (!pm || !get || !skg::thiscall<bool>(get, pm, 0u, kPropsGroup, &list))
        return nullptr;
    return list;
}

// tests: a list for key 0 with this name, as NameCaptainDialog_OnButton writes the captain's when the species copy
// gave key 0 (the template 0x15A51D8 copied, the name set, saved: into Pollination.package)
static std::string plant(const char* name)
{
    static uintptr_t copy = H->sym("SporeCoop::CaptainProps_Copy"), set_name = H->sym("SporeCoop::CaptainProps_SetName"),
                     tmpl = H->sym("SporeCoop::NameCaptain_PropsTemplate");
    if (!copy || !set_name || !tmpl)
        return "no symbols\n";
    static const uint32_t zero[3] = {0, 0, 0};
    void* list = nullptr;
    if (!skg::cdecl_call<bool>(copy, zero, (const uint32_t*)tmpl, &list, (void*)nullptr) || !list)
        return "the template could not be copied\n";
    // an eastl string16 as the dialog passes it (begin, end, capacity, allocator); static: the game may keep the
    // pointer until the list is written
    static char16_t buf[64];
    static uintptr_t str[4];
    size_t n = 0;
    for (; name[n] && n < 63; n++)
        buf[n] = (char16_t)(unsigned char)name[n];
    buf[n] = 0;
    str[0] = (uintptr_t)buf;
    str[1] = (uintptr_t)(buf + n);
    str[2] = (uintptr_t)(buf + n + 1);
    str[3] = 0;
    skg::cdecl_call<void>(set_name, list, (const void*)str);
    release(list);
    return std::string("planted '") + name + "' under key 0\n";
}

// tests: the list under key 0 out of the profile's Pollination.package (the database CaptainProps_Save wrote it to:
// Resource::GetDatabaseByID(0x11AC19C), vtable 0x140A178; DeleteRecord is slot 0x40 there and in DatabasePackedFile's
// 0x1436938: thiscall bool(db, const ResourceKey*) ret 4; Flush is slot 0x24, thiscall bool(db)) and out of the manager's
// cache (RemovePropertyList)
static std::string remove_key0()
{
    static uintptr_t rm = H->sym("App::cPropManager::RemovePropertyList"), db_get = H->sym("Resource::GetDatabaseByID");
    static const uint32_t key[3] = {0, 0x00B1B104, kPropsGroup};
    void* db = db_get ? skg::cdecl_call<void*>(db_get, kPollinationDb) : nullptr;
    bool deleted = db && readable(db, 4) && skg::vcall<bool>(db, 0x40 / 4, (const uint32_t*)key);
    if (deleted)
        skg::vcall<bool>(db, 0x24 / 4); // Flush (writes the index now: a killed game keeps the deletion)
    void* pm = prop_manager();
    void* list = key0_list();
    if (list && pm && rm)
        skg::thiscall<void>(rm, pm, list);
    release(list);
    return std::string(deleted ? "deleted from Pollination.package" : "not in Pollination.package") +
           (list ? ", dropped from the cache\n" : "\n");
}

static int c_props(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string r;
    if (argc > 2 && !strcmp(argv[1], "plant"))
        r = plant(argv[2]);
    else if (argc > 1 && !strcmp(argv[1], "remove"))
        r = remove_key0();
    void* list = key0_list();
    release(list);
    H->out_printf(out, "%screationprops: fix=%d hooked=%d refused=%u key0_list=%d\n", r.c_str(), (int)g_fix,
                  (int)(g_get_orig != nullptr), g_refused.load(), (int)(list != nullptr));
    return 0;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    uintptr_t a = H->sym("SporeCoop::CaptainProps_Get");
    bool on = a && H->hook_create(M, (void*)a, (void*)&props_get_guard, (void**)&g_get_orig) == 0;
    if (!on)
        g_get_orig = nullptr;
    H->log(M, SK_INFO, "creation props: key 0 guard %s (BUG-1328)", on ? "on" : "off");
    H->command_add(M, "coop.creationprops",
                   "coop.creationprops [plant <name> | remove] - the property list of creation key 0 (BUG-1328); tests: "
                   "plant one as \"Name Your Captain\" writes it when the species copy failed, remove it",
                   c_props, nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace creationprops
