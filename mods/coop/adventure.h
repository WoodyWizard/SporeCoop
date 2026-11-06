#pragma once

#include "net/protocol.h"

namespace adventure {
// Only a pair already playing the same objective can share credit for it.
void tick(uint32_t world, const net::PlayerState* peer);
void reset();
net::AdventureGate outgoing();
}
