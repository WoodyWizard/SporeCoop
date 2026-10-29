// Support logging (docs/REPORTING.md#reporting-reading-logs): a periodic state line per player
// ("trace:") and event lines of what players do and what the mod decides ("eat:", "body:", "vote:",
// ...), cheap and rate limited, so a player's report zip answers "why could only one of us eat".
// `[coop] log_detail=0|1|2` in coop.ini, `coop.log detail N` in the console:
// 0 = only the old lines, 1 = trace + events (default), 2 = verbose (more events per window).
#pragma once

#include <string>

#include "sporekit.h"

namespace trace {

extern int g_detail;

void init(const SkHost* host, SkMod* mod);
inline bool on(int level = 1) { return g_detail >= level; }

// "<kind>: <text>" when the detail is at least `level`
void line(const char* kind, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void verbose(const char* kind, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// an event that can repeat many times a second: the first few of `key` in each 10 s window are
// written, the rest counted; tick() writes "<kind>: <key> x<N> more in the last 10 s"
void ev(const char* kind, const char* key, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
void tick(double now);

// "%08X" of a value, "-" for 0
std::string hex(uint32_t v);

} // namespace trace
