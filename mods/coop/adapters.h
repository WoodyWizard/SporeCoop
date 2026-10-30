// The local IPv4 addresses (the "Friends connect to" hint, the LAN scan's broadcast targets),
// BUG-0286: GetAdaptersAddresses crashed the game inside Wine's nsi.dll (a memcpy with a garbage
// count from the \\.\Nsi device reply) on the main thread, from the co-op window's refresh.
// With the fix (core.adapters) the lookup runs on a worker thread of its own, at most once per
// kMaxAgeMs, with the documented ERROR_BUFFER_OVERFLOW retry and a fault guard: a fault inside it
// ends the worker (the list keeps its last value, no more lookups this run), never the game.
// The main thread only reads the last list. Off: the old synchronous lookup every 10 s.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sporekit.h"

namespace adapters {

struct Addr {
    std::string ip;
    uint32_t broadcast; // network byte order
};

void init(const SkHost* host, SkMod* mod); // starts the first lookup in the background
// the last known addresses (main thread). With the fix: never calls GetAdaptersAddresses here,
// starts a background lookup when the list is older than kMaxAgeMs.
std::vector<Addr> list();
int command(int argc, const char* const* argv, SkOut* out); // coop.adapters
int shutdown();                                              // unload: lookups still running

} // namespace adapters
