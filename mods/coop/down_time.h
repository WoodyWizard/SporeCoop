// How long a downed player waits for a partner, on every stage with a revive (Cell knockout, Creature / Adventure
// downed, Tribe last villager or village, Space disabled ship). Owner 2026-11-12: "Let's increase the 30s downed timer
// to 1m. It takes time for players to reach their teammate to even attempt a revive." (BUG-9149)
#pragma once

namespace downed {
// `coop.fix core.down_60` (on): a minute. Off: each stage's switch decides as before (its `*.down_30`: 30 s, else the
// stage's old short time).
bool minute();
inline double seconds(bool down_30, double short_s) { return minute() ? 60.0 : down_30 ? 30.0 : short_s; }
} // namespace downed
