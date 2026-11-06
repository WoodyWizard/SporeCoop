#pragma once

#include <string>
#include "net/session.h"
#include "sporekit.h"

namespace creature_rescue {
void init(const SkHost* host, net::Session* session, void (*notify)(const std::string&, int));
void tick(uint32_t world, void* avatar, const net::PlayerState* partner, double now);
void reset();
void on_player_leave(uint8_t player);
bool continuing();
bool down();
bool pending();
bool protect(void* avatar, float& damage);
void message(uint8_t from, net::Reader& r);
std::string status();
}
