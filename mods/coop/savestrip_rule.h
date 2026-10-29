// Portable rules of the save filter (savestrip.cpp, BUG-0255); tests/npc_tests.cpp runs them.
#pragma once

#include <cstdint>

namespace savestrip {

// One list of our objects per save (BUG-0255). Every save opens its own COMSerializer and all of
// one save's Writes run in one frame (the main thread does not draw a frame inside a save; the
// first/last frame of each save is logged by savestrip.cpp). So a Write starts a new save when the
// serializer differs from the last one or a frame has passed since the last Write. Before, "0.5 s
// since the last Write" stood in for it: a second save soon after through a serializer at the same
// address used the first save's pointers.
struct SaveState {
    void* ser = nullptr;
    uint32_t frame = 0;
    bool any = false;
};

inline bool new_save(SaveState& s, void* ser, uint32_t frame)
{
    bool fresh = !s.any || ser != s.ser || frame != s.frame;
    s.ser = ser;
    s.frame = frame;
    s.any = true;
    return fresh;
}

// An empty herd made for our objects (BUG-0255): stripped only when it is surely empty: its own
// member list is readable and empty, no creature of this save points at it, and every creature of
// the save could be read (one that could not might point at it).
inline bool strip_empty_herd(bool members_readable, bool members_empty, bool back_linked, bool any_unreadable)
{
    return members_readable && members_empty && !back_linked && !any_unreadable;
}

} // namespace savestrip
