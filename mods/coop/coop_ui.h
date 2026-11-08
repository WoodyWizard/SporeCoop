// Co-op user interface built from Spore's own UI: a "Co-op" item in the main menu, a "Report a problem" item
// in the game's pause menu and a game dialog in steps (0.7.12 F1, docs/08-ui.md#ui-window-steps): the
// connect steps (first start: the name; Host or Join; Host: the port; Join: the games on the network and an
// address or host name), the lobby (players, colour, stage, ping, ready, chat, world transfer, the host's
// address), the session settings page (settings_core.h rows by group) and, in the Space stage, "space" (the
// rows of the space features, space_ext.h). docs/08-ui.md.
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
    uint32_t color = 0; // the player's colour (ARGB, F2 of 0.7.12 fills it); 0 = none yet (the lobby shows "-")
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
    // F2: the own colour's ">" in the lobby: the next palette colour (pfind::request_color); false and the reason in line
    // when it is refused (another player has it), else what happened ("Your colour: Blue", "Asked the host for Blue")
    std::function<bool(std::string& line)> color_next;
    std::function<std::string()> world_line; // transfer progress / hints, "" if nothing
    std::function<bool()> ready;
    std::function<void(bool)> set_ready;
    std::function<void()> share_world; // host (during a vote: start the evolution now)
    std::function<bool()> vote_active;
    // BUG-0516 (guest): the host evolves without a vote and offers this guest its own editor: the seconds the
    // offer still holds (0 = none). The ready button then reads "Evolve now" and set_ready(true) takes the offer.
    std::function<int()> evolve_offer;
    // BUG-0597: what "Send my world" would do now: 0 send it, 1 no world loaded (a menu: the world goes
    // to everyone once the host plays), 2 nobody has joined
    std::function<int()> share_state;
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
    std::function<std::vector<uint32_t>()> chat_colors; // F2: per chat() line its writer's colour (0: the default)
    std::function<std::vector<LanGame>()> lan_games;
    std::function<void(bool)> lan_scan; // start/stop looking for games on the network
    std::function<std::vector<std::string>()> local_addresses;
    std::function<uint32_t()> mode; // game mode id
    std::function<std::string()> version_line; // "SporeCoop 0.7.2.1  -  GOG 2024" (window footer)
    std::function<std::string()> report;       // writes a problem report, returns its path ("" = cannot)
    // a screenshot for the report first: 1 requested, 0 not possible, -1 switched off (coop.ini)
    std::function<int()> report_shot;
    std::function<int()> report_shot_state;    // 1 being taken, 0 written, -1 failed
    // host: "Ask the players for their reports" (lobby and F9, any time; F4 of 0.7.12 does the asking). Returns
    // the line the window shows ("" = nothing to say); unset = the button says it is not available yet
    std::function<std::string()> ask_reports;
    // the lobby's address line: the host's "ip:port, ..." to give to friends, a guest's the host it joined
    std::function<std::string()> host_address;
    uint16_t default_port = 0;
};

void init(const SkHost* host, SkMod* mod, Api api);
void tick();     // every frame (inside Present): only schedules UI work outside the frame
void open(bool on);
bool is_open();
// BUG-0599: the window covers (x, y) (screen pixels of the game window): hide it for `secs` so that a click of the
// mod there reaches the game's window under it (the "game saved" box's OK); false = not open or not there
bool step_aside(int x, int y, double secs);
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
// tests: the window goes back to its first start (the name step), as with no name chosen yet
void first_start();
// tests / layout shots (coop.uistate color <argb>): the lobby's colour of players without one (0 = "-")
void set_test_color(uint32_t argb);

// the dialog text font (0 until the co-op window was built once)
uint32_t text_font();
// the window speaks Russian (ui lang = ru, or auto on a Russian system)
bool russian();
void set_russian(bool ru); // tests (coop.uistate lang): the texts set from now on

} // namespace coop_ui
