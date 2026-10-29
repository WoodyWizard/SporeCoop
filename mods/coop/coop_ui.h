// Co-op user interface built from Spore's own UI: a "Co-op" item in the main menu and a
// game dialog with three pages, "connect" (name, address, games on the network, hosting),
// "lobby" (players, stage, ping, ready, chat, world transfer) and, in the Space stage, "space"
// (the rows of the space features, space_ext.h). docs/08-ui.md.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sporekit.h"

namespace coop_ui {

struct PlayerRow {
    uint8_t id = 0;
    std::string name;
    std::string stage; // "Creature", "Space, other world", ...
    bool host = false, me = false, ready = false;
    int ping_ms = -1;
};

struct LanGame {
    std::string name, address, info;
    int players = 0, max_players = 0;
};

// what the UI needs from the mod (filled in coop.cpp)
struct Api {
    std::function<std::string()> name;
    std::function<void(const std::string&)> set_name;
    std::function<bool(const std::string& address, std::string& err)> join;
    std::function<bool(uint16_t port, std::string& err)> host;
    std::function<void()> leave;
    std::function<int()> role;        // 0 offline, 1 host, 2 guest
    std::function<bool()> connected;
    std::function<std::string()> session_line;
    std::function<std::vector<PlayerRow>()> players;
    std::function<std::string()> world_line; // transfer progress / hints, "" if nothing
    std::function<bool()> ready;
    std::function<void(bool)> set_ready;
    std::function<void()> share_world; // host (during a vote: start the evolution now)
    std::function<bool()> vote_active;
    // own creation of the current stage: kind 0 creature / 1 cell / 2 ship, its name ("" = the
    // world's own), how many own creations of that kind exist; false = not shown. cycle: -1 / +1
    std::function<bool(int& kind, std::string& name, int& own)> creation;
    std::function<void(int)> creation_cycle;
    // tribe / civilization: how many own units are selected (what: "member", "vehicle", "city"),
    // the partner they would go to; give_do gives them
    std::function<int(std::string& what, std::string& to)> give;
    std::function<void()> give_do;
    std::function<void()> give_cycle;  // the next other player as the recipient (BUG-0075)
    std::function<int()> give_choices; // how many players a gift can go to
    // space: travel to a partner's star system (false = not possible now, else whose)
    std::function<bool(std::string& to)> jump;
    std::function<void()> jump_do;
    // a SporeCoop update (autoupdate.h): the line and the button text ("" = no button)
    std::function<bool(std::string& line, std::string& button)> update;
    std::function<void()> update_do;
    // civilization: the own role (0 none, 1 military, 2 economic, 3 religious); role_cycle(+1/-1)
    std::function<int()> civ_role;
    std::function<void(int)> civ_role_cycle;
    std::function<void(const std::string&)> say;
    std::function<std::vector<std::string>()> chat;
    std::function<std::vector<LanGame>()> lan_games;
    std::function<void(bool)> lan_scan; // start/stop looking for games on the network
    std::function<std::vector<std::string>()> local_addresses;
    std::function<uint32_t()> mode; // game mode id
    std::function<std::string()> version_line; // "SporeCoop 0.7.2.1  -  GOG 2024" (window footer)
    std::function<std::string()> report;       // writes a problem report, returns its path ("" = cannot)
    uint16_t default_port = 0;
};

void init(const SkHost* host, SkMod* mod, Api api);
void tick();     // every frame (inside Present): only schedules UI work outside the frame
void open(bool on);
bool is_open();
int input_lock_now(); // the game's input lock count (cGameInputManager +0x110; > 0 = clicks are dropped)
void shutdown(); // mod unload: hide our windows
std::string debug_state();
extern bool g_log_msgs; // debug: log UI messages reaching our handlers
extern bool g_lan_pages_fix; // tests: `coop.corefix lan_pages 0` lists the first three games only (BUG-0183)
extern bool g_status_fix; // tests: `coop.corefix ui_status 0` keeps the status of the last button (BUG-0104)
// the session was joined (connected) or ended (why): replaces the status line of the join
void session_event(bool connected, const std::string& why);
extern bool g_recenter_fix;  // tests: `coop.corefix window_recenter 0` keeps the first position (BUG-0113)
extern bool g_menu_drop_fix; // tests: `coop.corefix menu_drop 0` keeps the menu item pointer after the menu (BUG-0163)

// the dialog text font (0 until the co-op window was built once)
uint32_t text_font();
// the window speaks Russian (ui lang = ru, or auto on a Russian system)
bool russian();

} // namespace coop_ui
