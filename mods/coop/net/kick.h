// `coop.kick <id | name>` (BUG-1108, plan 0.7.15 owner decision 8: a console command, no UI): the host drops a guest
// from the session; the guest sees why ("rejected: the host removed you from the session") and its session ends. Not a
// ban: the player may join again under the host's join rules. Portable (net_tests runs it).
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

#include "session.h"

namespace net {

constexpr const char* kKickText = "the host removed you from the session";

// args: the words after the command name; the answer for the console
inline std::string kick_command(Session& s, const std::vector<std::string>& args)
{
    if (s.role() != Role::Host)
        return "coop.kick: only the host removes players\n";
    std::string list;
    for (auto& p : s.players())
        if (p.id != s.my_id())
            list += "  [" + std::to_string(p.id) + "] " + p.name + "\n";
    if (args.empty())
        return "coop.kick <id | name>: removes a player from the session\n" + (list.empty() ? "  (no guests)\n" : list);
    std::string who;
    for (auto& a : args) // a name may have spaces ("Bob 2")
        who += (who.empty() ? "" : " ") + a;
    int id = -1;
    for (auto& p : s.players()) // the name first: a player may be called "2"
        if (p.id != s.my_id() && p.name == who)
            id = p.id;
    if (id < 0 && !who.empty() && who.find_first_not_of("0123456789") == std::string::npos)
        id = atoi(who.c_str());
    if (id == s.my_id())
        return "coop.kick: the host does not remove itself (coop.leave ends the session)\n";
    if (id < 0 || id > 255 || !s.kick((uint8_t)id, kKickText))
        return "coop.kick: no guest '" + who + "'\n" + (list.empty() ? "  (no guests)\n" : list);
    return "removing player " + std::to_string(id) + " '" + s.name_of((uint8_t)id) + "'\n";
}

} // namespace net
