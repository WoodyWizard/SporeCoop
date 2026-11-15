// Updates from the project's GitHub releases (updater/): a check in the background after the
// start, an "Update" button in the co-op window, the files installed before the next start.
// [update] check = 1 | 0 and url = <releases/latest API address> in coop.ini.
#pragma once

#include <string>

#include "sporekit.h"

namespace autoupdate {

void init(const SkHost* host, SkMod* mod, const char* version);
void set_feed(void (*feed)(const std::string&)); // notifications (main thread)
void tick(); // main thread: notifications
// the co-op window: the text of the update line and of its button ("" = nothing to show)
bool ui(std::string& line, std::string& button);
void start_update();
std::string command(const std::string& arg, const std::string& val = {}); // coop.update [check | apply | status | url | testdelay | applydelay]
// on_unload: no thread logs or notifies any more; returns how many still run (each holds the
// module, so the old image stays mapped until they end: BUG-0060)
int shutdown();
extern bool g_pin_fix; // tests: `coop.corefix update_pin 0`, the old threads without a module reference

} // namespace autoupdate
