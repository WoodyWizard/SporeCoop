// Arrival placement must never reclaim a chief after it starts moving.
#pragma once
#include <cmath>
#include "net/protocol.h"
namespace chief_arrival {
struct Movement {
    net::Vec3 first{};
    bool seen = false, moved = false;
    bool stationary(net::Vec3 at, float speed)
    {
        if (!seen) { first = at; seen = true; }
        float x = at.x-first.x, y = at.y-first.y, z = at.z-first.z;
        if (!std::isfinite(speed) || speed >= 0.3f ||
            !std::isfinite(x+y+z) || x*x+y*y+z*z > 1.5f*1.5f)
            moved = true;
        return !moved;
    }
};
}
