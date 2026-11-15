// The session settings (F3 of 0.7.12, docs/07-modes.md#modes-session-settings): the one API every module reads a
// co-op setting through. The table, the rules and the formats are portable in settings_core.h.
//
//   * the host's (or a player alone) values: the defaults of a new world come from coop.ini (each setting's `ini`
//     key, else the built-in default); a world keeps its values in <profile>/Spore/SporeCoop/settings/<crc>.txt and
//     the next session of that world plays with them;
//   * a guest plays with the host's table (MSG_SETTINGS, on its join and on every change), its own values only while
//     no host sent one (an older host);
//   * `coop.settings` lists and sets them; a guest cannot set one, a Session setting only before the start.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "settings_core.h"
#include "sporekit.h"

namespace settings {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<bool()> playing;              // this game plays a world now (a stage with its own creature)
    std::function<std::string()> world_name;    // the save being played ("Name.spo")
    std::function<bool()> rivals;               // the Rivals mode is on (off_in_rivals settings read off)
    std::function<void(const std::string&)> feed;
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick();                                     // every frame (main thread): the world opened / closed
void on_player_join(uint8_t id);                 // host: the newcomer gets the table
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_disconnect();                            // guest: the host's table is gone, its own values again

// the value in force on this machine (a guest: the host's), with the Rivals rule applied
int get(Id id);
inline bool on(Id id) { return get(id) != 0; }
// set by the host or a player alone; false (and *why) when refused. force: tests, the scope is not checked
bool set(Id id, int value, bool force, std::string* why = nullptr);
bool started();        // the session settings are fixed now (the host plays its world; a guest: the host's flag)
bool from_host();      // a guest that plays with the host's table
const Values& defaults(); // the defaults of a new world (coop.ini)
std::string diff();    // "key=value ..." of what differs from the built-in defaults (support report)

int command(void*, int argc, const char* const* argv, SkOut* out); // coop.settings

} // namespace settings
