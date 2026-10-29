// A fix switch in a portable file (mods/coop/net/*, updater/*), BUG-0214 rule R1 (fixes.h).
// These files are also built into net_tests, fuzz_decoders, win_tests, release_tsan and the setup,
// which link without fixes.cpp, so the bool is only defined here and registered with its name in
// mods/coop/fix_table.cpp (coop.dll). tests/fixes_tests.cpp fails on a FIX_SWITCH that is neither
// registered there nor listed in its kUnitOnly[].
#pragma once

#define FIX_SWITCH(var) bool var = true
