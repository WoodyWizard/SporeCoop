// The state the files of the coop core share (B2 of 0.7.15, docs/audit-0.7.15/core.md CORE-2): coop.cpp (the module
// entry, the frame, the world-share glue), coop_cmds.cpp (the console commands), dna.cpp (DNA routing), vote.cpp (the
// evolution vote and the own editor), puppets.cpp (the partners' puppets, the recorders) and session_glue.cpp (the
// session callbacks and the session end) were one file. Its anonymous namespace is the named namespace coop_core
// now, with a using-directive like the one an anonymous namespace has, so the code reads as before; what more than
// one of these files uses is declared here, everything else stays file-static. Only these files include this header.
// The moves are proved by tools/refactor_dump.py (docs/10-dev-pipeline.md#pipeline-refactor-proof).
#pragma once

#include <winsock2.h>
#include "lifecycle.h"
#include "call_guard.h"
#include "player_life.h"
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <mutex>
#include <cstddef>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <csignal>
#include <cstdio>
#include <deque>
#include <map>
#include <string>

#include "adapters.h"
#include "coop_ui.h"
#include "driver.h"
#include "game.h"
#include "modes.h"
#include "stage_epoch.h"
#include "mouse.h"
#include "shared.h"
#include "sui.h"
#include "teamplay.h"
#include "tribe.h"
#include "drama.h"
#include "cellsync.h"
#include "audio.h"
#include "census.h"
#include "civ.h"
#include "autoupdate.h"
#include "gamefixes.h"
#include "spacecoop.h"
#include "squadron.h"
#include "npc.h"
#include "creature_hp.h"
#include "player_rules.h"
#include "mirror_fx.h"
#include "effects.h"
#include "nest.h"
#include "fields.h"
#include "wsync.h"
#include "worldobj.h"
#include "realign.h"
#include "parts.h"
#include "rejoin.h"
#include "savestrip.h"
#include "cell_logic.h"
#include "busy.h"
#include "social.h"
#include "world.h"
#include "find.h"
#include "net/discovery.h"
#include "net/fixes.h"
#include "net/give_target.h"
#include "net/interp.h"
#include "net/lobby.h"
#include "net/session.h"
#include "net/utf8.h"
#include "creations.h"
#include "notify.h"
#include "hud_portrait.h"
#include "notify_text.h"
#include "syncdbg.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include <shlobj.h>
#include "share_wait.h"
#include "dna_route.h"
#include "dna_ledger.h"
#include "dna_pace.h"
#include "settings.h"
#include "sreport.h"
#include "space_ext.h"
#include "host_world.h"
#include "own_world.h"
#include "diag_coop.h"
#include "trace.h"
#include "view_centres.h"

extern const SkHost* H; // coop.cpp
extern SkMod* M;

#define LOG(...) H->log(M, SK_INFO, __VA_ARGS__)
#define SAY(...) H->out_printf(out, __VA_ARGS__)

namespace coop_core {
} // namespace coop_core
using namespace coop_core;
