// Shared Adventure Editor runtime module (WP-AE of 0.8.0, BUG-0863):
//   Host-authoritative collaborative Galactic Adventures editor behind COOP_FIX("adventure.shared_editor"):
//   - Marker create/clone/delete, transform (move/rotate/scale) with 10 Hz live drag previews, acts, captain asset
//   - Renewable per-marker leases (5 s inactivity expiry) preventing simultaneous conflicting drags
//   - Per-player Undo/Redo inverse delta stack (intercepting native full-resource Undo/Redo)
//   - Late-join chunked snapshot & delta reconciliation on MSG_ADV_EDITOR (82, creature 1.5)
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/buffer.h"
#include "net/session.h"
#include "sporekit.h"

namespace adv_editor {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> notice; // broadcast notice
    std::function<void(const std::string&)> feed;   // local feed line
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);

// Console command handler for `coop.adveditor` (registered with SK_CMD_OUTSIDE_FRAME)
std::string command(const std::vector<std::string>& args);

} // namespace adv_editor
