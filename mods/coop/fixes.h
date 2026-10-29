// One registry of the bug fixes' test switches (BUG-0116, docs/06-testing.md#testing-fix-registry).
//
// A test you never saw fail proves nothing (CLAUDE.md 3): a fix keeps its old behaviour behind a
// bool, on (the fixed code) by default. Each module registers its bool next to its definition:
//
//   static bool g_fix_x = true;
//   COOP_FIX (<"area.switch">, <"BUG-NNNN">, g_fix_x, <"one line: what the fix does">);
//
// and the console command `coop.fix` switches it (`coop.fix list`, `coop.fix <name> on|off`,
// `coop.fix all on|off`). The registry only points at the bools: the old per-module commands
// (`coop.pool fix purge 0`, `coop.npc strays off`, ...) keep working on the same bools.
// Names are `<area>.<switch>`, the area being the old command's (pool, npc, civ, tribe, ...).
// Portable (no Windows, no SporeKit): tests/fixes_tests.cpp runs it natively. SporeKit's own fixes
// stay under `kit.fix` (another DLL).
//
// Registry rules (BUG-0214, GitHub #148; docs/06-testing.md#testing-fix-registry):
//  R1 one source: in a coop.dll-only file the bool has COOP_FIX on the next line; in a portable file
//     (mods/coop/net/*, updater/*: also built into net_tests, fuzz_decoders, win_tests, the setup,
//     which link without fixes.cpp) the bool is `FIX_SWITCH(var);` (fix_switch.h) and is registered
//     once in mods/coop/fix_table.cpp. Never COOP_FIX in a portable file. A member of net::Fixes is
//     a portable switch too (fix_table.cpp).
//  R2 names `<area>.<switch>`, unique, one bool per name and one name per bool.
//  R3 one bug per switch: the off side is the code before that one fix and nothing else.
//  R4 COOP_FIX_UNSAFE: the off side can corrupt memory or lose data on disk. `coop.fix all off`
//     skips them (`all off --unsafe` does not); `coop.fix <name> off` still works.
//  R6 a fix whose off side is an out-of-bounds read of received data has no runtime switch: its
//     FIX_SWITCH is listed in fix_table.cpp's kUnitOnly[] (not registered).
//  A bool `= true` next to a BUG-NNNN / "tests:" / "old behaviour" comment that is not a lab switch
//  carries `// not a lab switch: <reason>` (fixes_tests checks this).
// A hot reload of coop.dll sets every switch back to on (the registry is per image).
#pragma once

#include <string>
#include <vector>

namespace fix {

struct Info {
    const char* name;   // <area>.<switch>, unique
    const char* ticket; // BUG-NNNN (several: "BUG-0001 BUG-0002")
    bool* on;           // true = the fixed code (default), false = the behaviour before the fix
    const char* what;   // one line: what the fix does
    bool unsafe_off;    // R4: the off side can corrupt memory or lose data (skipped by `all off`)
};

// register a switch (static initialisation: COOP_FIX). A second registration of a name, or a
// null pointer, is refused (counted in duplicates(), shown by list()). Returns whether it was taken.
bool reg(const char* name, const char* ticket, bool* on, const char* what, bool unsafe_off = false);
const std::vector<Info>& all(); // in the order of registration
const Info* find(const std::string& name);
bool set(const std::string& name, bool on); // false: unknown name
// every switch; returns how many changed. Off skips the unsafe ones unless `unsafe` (*skipped: how many)
size_t set_all(bool on, bool unsafe = false, size_t* skipped = nullptr);
// registered names that end with `tail` after a '.' or equal its last part ("session_end" ->
// "session.end" through the '_' / '.' fold): the hint for an unknown name
std::vector<std::string> suggest(const std::string& tail);
int duplicates();
std::string list(); // "N fixes (M off)" then one line per fix: name on|off ticket what
// the coop.fix command over its arguments (argv without the command name); *ok = false on a usage
// error or an unknown name
std::string command(const std::vector<std::string>& args, bool* ok = nullptr);

} // namespace fix

#define COOP_FIX_CAT2(a, b) a##b
#define COOP_FIX_CAT(a, b) COOP_FIX_CAT2(a, b)
// one line next to the switch's bool (at namespace scope, in one .cpp file only)
#define COOP_FIX(name, ticket, var, what)                                                                             \
    [[maybe_unused]] static const bool COOP_FIX_CAT(coop_fix_reg_, __LINE__) = ::fix::reg(name, ticket, &(var), what)
// R4: the same for a switch whose off side can corrupt memory or lose data on disk
#define COOP_FIX_UNSAFE(name, ticket, var, what)                                                                      \
    [[maybe_unused]] static const bool COOP_FIX_CAT(coop_fix_reg_, __LINE__) =                                         \
        ::fix::reg(name, ticket, &(var), what, true)
