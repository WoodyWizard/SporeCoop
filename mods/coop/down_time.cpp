#include "down_time.h"

#include "fixes.h"

namespace downed {
static bool g_minute = true;
COOP_FIX("core.down_60", "BUG-9149", g_minute,
         "cell / creature / adventure / tribe / space: a downed player waits a minute for a partner (off: the stages' 30 s)");
bool minute() { return g_minute; }
static bool g_revive_5s = true;
COOP_FIX("core.revive_5s", "BUG-9307", g_revive_5s,
         "cell / creature / adventure / tribe / space: a partner stays close 5 s to revive (off: 1.5 s, space 2 s)");
bool revive_5s() { return g_revive_5s; }
} // namespace downed
