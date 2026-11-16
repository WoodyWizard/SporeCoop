// Decision 4S (owner 2026-11-16, BUG-9239): the memory refresh. Spore is a 32-bit game: 2 GB of address space, and it
// keeps what it loads until it is closed (docs/re/graphics.md#graphics-address-space); a long session ends in "out of
// memory". When the space runs low the player gets a box in the middle of the screen with a live bar of how full it is:
// "Spore is natively unable to clear memory usage. Press continue below to restart Spore and reconnect."
//   * first at 15 % left (or no free block of 48 MB), again at 10 % (or none of 24 MB) after an Ignore; an Ignore at
//     10 % is the last (the player is fine with the crash);
//   * the bar: green, yellow from 20 % left, red from 10 %;
//   * Continue: a guest leaves cleanly, Spore restarts and joins the same host again under the same name and rejoin
//     token; a host (or a game alone) saves, tells its guests (MSG_HOST_RESTART: they rejoin by themselves for 6
//     minutes), restarts, loads the same planet and hosts again on the same port.
// The restart: a hidden PowerShell waits for this process to end and starts SporeApp.exe with the same command line and
// environment; this process ends right after the save (TerminateProcess). What the new game must do is in
// <AppData>\SporeCoop\restart-<instance>.txt (profile-aware; read once at start, removed at once, ignored after 15 min).
// Off (`coop.fix core.memory_box off`): the old notice every 10 minutes (BUG-0631).
#pragma once

#include <cstdint>
#include <string>
#include "sporekit.h"

namespace mem_refresh {

struct Api {
    int (*role)();                // 0 none, 1 host, 2 guest (joining or connected)
    bool (*connected)();          // in a session that is up
    std::string (*host_address)(); // guest: "ip:port" of the host
    std::string (*password)();
    uint16_t (*port)();           // host
    unsigned (*seats)();          // host: the session's seat limit
    std::string (*name)();        // the player name as typed (its -NoShare- tag kept)
    uint64_t (*token)();          // guest: the rejoin token of this session
    bool (*join)(const std::string& addr, const std::string& name, const std::string& password, uint64_t token,
                 std::string& err);
    bool (*host)(uint16_t port, unsigned seats, const std::string& name, const std::string& password, std::string& err);
    void (*leave)();              // a clean leave (guest) / end of hosting (host)
    void (*announce)(uint16_t seconds); // host: MSG_HOST_RESTART to every guest
    void (*feed)(const std::string& line);
};

void init(const SkHost* host, SkMod* mod, const Api& api);
bool enabled();              // `coop.fix core.memory_box`
bool box_up();               // the memory box is on the screen (the partner markers are not drawn over it)
void tick(double now);       // every frame (cheap; the address space is walked on a thread)
void host_restarting(uint16_t seconds); // guest: the host said it restarts (MSG_HOST_RESTART)

} // namespace mem_refresh
