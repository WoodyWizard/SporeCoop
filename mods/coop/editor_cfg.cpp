// Editor setup properties (group 0x40600100) edited in memory: the depth (nudge) handle for creature
// parts (roadmap 42). docs/re/creations.md#creations-editor-depth-handle
#include "editor_cfg.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sporekit_game.hpp"

namespace editor_cfg {
using namespace skg;

static const SkHost* H;
static uintptr_t a_GetProps, a_GetKeys, a_Set;
constexpr uint32_t kSetupGroup = 0x40600100;
constexpr uint32_t kManipulators = 0x10119203; // editorEnabledManipulators
struct Key { uint32_t instance, type, group; };

// PropManager vt 0x2C: GetPropertyList(instance, group, PropertyList**) (adds a reference)
static void* list_of(uint32_t instance)
{
    void* pm = a_GetProps ? cdecl_call<void*>(a_GetProps) : nullptr;
    void* list = nullptr;
    if (!pm || !vcall<bool>(pm, 0x2C / 4, instance, kSetupGroup, &list)) return nullptr;
    return list;
}

static void release(void* list) { if (list) vcall<int>(list, 0x04 / 4); }

static std::string manipulators(void* list)
{
    size_t count = 0;
    Key* keys = nullptr;
    if (!a_GetKeys || !cdecl_call<bool>(a_GetKeys, list, kManipulators, &count, &keys) || count > 256 ||
        (count && !readable(keys, count * sizeof(Key))))
        return "-";
    std::string s;
    char b[16];
    for (size_t i = 0; i < count; ++i) {
        snprintf(b, sizeof b, "%s%08X", i ? " " : "", keys[i].instance);
        s += b;
    }
    return s.empty() ? "(empty)" : s;
}

static int command(void*, int argc, const char* const* argv, SkOut* out)
{
    auto say = [&](const std::string& s) { H->out_printf(out, "%s", s.c_str()); };
    if (argc < 3) {
        say("usage: coop.editorcfg show <inst> | manips <inst> <id>... | copy <key> <from inst> <to inst>");
        return 1;
    }
    const std::string op = argv[1];
    if (op == "show") {
        void* list = list_of(strtoul(argv[2], nullptr, 16));
        if (!list) { say("no such editor setup"); return 1; }
        const bool nudge = vcall<bool>(list, 0x1C / 4, uint32_t(0xB8E88BEC));
        say("manipulators: " + manipulators(list) + (nudge ? "  nudge-tuning: yes" : "  nudge-tuning: no"));
        release(list);
        return 0;
    }
    if (op == "manips" && argc >= 4) {
        void* list = list_of(strtoul(argv[2], nullptr, 16));
        void* property = nullptr;
        if (!list || !a_Set || !vcall<bool>(list, 0x24 / 4, kManipulators, &property) || !property) {
            release(list);
            say("the setup has no own manipulator list");
            return 1;
        }
        std::vector<Key> keys;
        for (int i = 3; i < argc; ++i) keys.push_back({uint32_t(strtoul(argv[i], nullptr, 16)), 0, 0});
        const bool ok = thiscall<bool>(a_Set, property, int(0x20), int(0x14), keys.data(), sizeof(Key), keys.size());
        say(std::string(ok ? "set: " : "failed: ") + manipulators(list));
        release(list);
        return ok ? 0 : 1;
    }
    if (op == "copy" && argc >= 5) {
        const uint32_t key = strtoul(argv[2], nullptr, 16);
        void* from = list_of(strtoul(argv[3], nullptr, 16));
        void* to = list_of(strtoul(argv[4], nullptr, 16));
        void* property = nullptr;
        bool ok = from && to && vcall<bool>(from, 0x24 / 4, key, &property) && property;
        if (ok) vcall<void>(to, 0x14 / 4, key, property); // SetProperty copies the value
        ok = ok && vcall<bool>(to, 0x1C / 4, key);
        release(from);
        release(to);
        say(ok ? "copied" : "failed");
        return ok ? 0 : 1;
    }
    say("unknown operation");
    return 1;
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    a_GetProps = H->sym("App::IPropManager::Get");
    a_GetKeys = H->sym("App::Property::GetArrayKey");
    a_Set = H->sym("App::Property::Set");
    H->command_add(mod, "coop.editorcfg",
                   "coop.editorcfg show <inst> | manips <inst> <id>... | copy <key> <from> <to> - editor setup props (group 40600100, tests)",
                   command, nullptr, SK_CMD_MAIN_THREAD);
    return a_GetProps && a_GetKeys && a_Set;
}

} // namespace editor_cfg
