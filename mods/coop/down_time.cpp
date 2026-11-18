#include "down_time.h"

#include "fixes.h"

namespace downed {
static bool g_minute = true;
COOP_FIX("core.down_60", "BUG-9149", g_minute,
         "cell / creature / adventure / tribe / space: a downed player waits a minute for a partner (off: the stages' 30 s)");
bool minute() { return g_minute; }
} // namespace downed
