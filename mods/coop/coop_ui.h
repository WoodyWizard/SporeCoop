// Co-op user interface built from Spore's own UI: a "Co-op" item in the main menu, a "Report a problem" item
// in the game's pause menu and a game dialog in steps (0.7.12 F1, docs/08-ui.md#ui-window-steps): the
// connect steps (first start: the name; Host or Join; Host: the port; Join: the games on the network and an
// address or host name), the lobby (players, colour, stage, ping, ready, chat, world transfer, the host's
// address), the session settings page (settings_core.h rows by group) and, in the Space stage, "space" (the
// rows of the space features, space_ext.h). docs/08-ui.md.
#pragma once

#include <cstdint>
#include "sui.h"
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
    bool buddy = false;     // 6J: on this player's friends list (the name in light blue)
    bool party = false;     // 6J: in this player's party
    bool leads = false;     // 6J: leads its party (more than one in it)
};

std::string load_ui_str(const char* key); // [ui] <key> of the Co-op window's settings ("" none)

struct LanGame {
    std::string name, address, info;
    int players = 0, max_players = 0;
    int scaling = -1;
    unsigned settings = 0;
};

// what the UI needs from the mod (filled in coop.cpp)
struct Api {
    std::function<std::string()> name;
    std::function<void(const std::string&)> set_name;
    std::function<bool(const std::string& address, std::string& err, const std::string& password)> join;
    std::function<bool(uint16_t port, unsigned seats, unsigned scaling, std::string& err, const std::string& password)> host;
    // the Set Seats button: world::set_seats (more seats now, fewer from the next stage; Space: from the next load)
    std::function<bool(unsigned seats, std::string& message)> queue_seats;
    std::function<void()> leave;
    std::function<int()> role;        // 0 offline, 1 host, 2 guest
    std::function<bool()> connected;
    std::function<std::string()> session_line;
    std::function<std::string()> invite;
    std::function<unsigned()> seat_limit;
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
    std::function<bool()> paced_transfer;
    std::function<void(bool)> set_paced_transfer;
    // BUG-9112: does this player receive its partners' foreign creations (replaces BUG-9105's Share Onward)
    std::function<bool()> foreign_creations;
    std::function<void(bool)> set_foreign_creations;
    // BUG-9111: the host's Player Cells setting (Lineages cells in the Cell stage)
    std::function<bool()> lineage_cells;
    std::function<void(bool)> set_lineage_cells;
    // own creation of the current stage: kind 0 creature / 1 cell / 2 ship, its name ("" = the
    // world's own), how many own creations of that kind exist; false = not shown. cycle: -1 / +1
    std::function<bool(int& kind, std::string& name, int& own)> creation;
    std::function<bool()> one_creature; // F5: a guest plays the host's creature (no arrows, no Evolve now)
    std::function<void(int)> creation_cycle;
    // BUG-9135 roadmap phase 4 (Tribe, tribe::late_prompt): this player has no village of their own; 1 they play no
    // creature of their own (the creation row asks them to choose one), 2 their own creature is chosen (the village
    // comes); 3 / 4 the same while their village still wears its stand-in (BUG-9150); 0 no prompt.
    // create_creature_offered: the "Create New Creature" button (tribe.late_create_editor)
    std::function<int()> late_prompt;
    std::function<bool()> create_creature_offered;
    std::function<void()> create_creature;
    // tribe / civilization: how many own units are selected (what: "member", "vehicle", "city"),
    // the partner they would go to; give_do gives them
    std::function<int(std::string& what, std::string& to)> give;
    std::function<void()> give_do;
    std::function<void()> give_cycle;  // the next other player as the recipient (BUG-0075)
    std::function<int()> give_choices; // how many players a gift can go to
    std::function<bool()> give_pick_space; // BUG-1050: Space shows the ">" picker too (coop.fix space.give_pick)
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
    // decision 5Y A (alpha): the galaxy server pinned at the top of the server list. galaxy_info: its name and its line
    // (connected: the planets and players), false when the build has none; galaxy_connect: connect to that address
    std::function<bool(std::string& name, std::string& line, bool& connected)> galaxy_info;
    std::function<bool(const std::string& address, std::string& err)> galaxy_connect;
    std::function<void()> galaxy_disconnect;
    std::function<void(bool)> lan_scan; // start/stop looking for games on the network
    std::function<std::vector<std::string>()> local_addresses;
    std::function<uint32_t()> mode; // game mode id
    std::function<std::string()> version_line; // "SporeCoop 0.7.2.1  -  GOG 2024" (window footer)
    std::function<std::string()> report;       // writes a problem report, returns its path ("" = cannot)
    // a screenshot for the report first: 1 requested, 0 not possible, -1 switched off (coop.ini)
    std::function<int()> report_shot;
    std::function<int()> report_shot_state;    // 1 being taken, 0 written, -1 failed
    // host: "Ask the players for their reports" (lobby and F9, any time; F4 of 0.7.12, BUG-0767). Returns the line the
    // window shows ("" = nothing to say); unset = the button says it is not available
    std::function<std::string()> ask_reports;
    // F4 (BUG-0767, sreport.h): the host asks the guests now (its "Report a problem" did too); the host's line about
    // the round ("" none); the guest's question (1: asked now, line = the question; 0: line = what happened to its
    // report) and its click
    std::function<bool()> report_round;
    std::function<std::string()> report_line;
    std::function<int(std::string& line)> report_prompt;
    std::function<void(bool send)> report_answer;
    // the lobby's address line: the host's "ip:port, ..." to give to friends, a guest's the host it joined
    std::function<std::string()> host_address;
    uint16_t default_port = 0;
};

void init(const SkHost* host, SkMod* mod, Api api);
void tick();     // every frame (inside Present): only schedules UI work outside the frame
void open(bool on);
void open_lobby(); // open on the lobby page (not the settings / space page): a guest asked for its report
bool is_open();
// SK_EV_OVERLAY: a short tooltip for the co-op window button under the mouse
void draw_tooltip();
// BUG-0599: the window covers (x, y) (screen pixels of the game window): hide it for `secs` so that a click of the
// mod there reaches the game's window under it (the "game saved" box's OK); false = not open or not there
bool step_aside(int x, int y, double secs);
int input_lock_now(); // the game's input lock count (cGameInputManager +0x110; > 0 = clicks are dropped)
// BUG-9318: our loading cover (join / reload / editor return) is over the game: set by coop.cpp's overlay each frame
void set_cover_up(bool up);
bool cover_up();
bool game_modal_up(); // a modal window of the game is up (UTFWin window manager +0x82C: the pause menu, a box; not ours)
void shutdown(); // mod unload: hide our windows
// 3X A: an editor was requested: Spore's UI goes to 100 % before the editor builds its own (it stays there until
// the editor is left)
void editor_opening();
// BUG-9156 (owner 2026-11-12: "The red button doesn't match our button on the co-op menu. You need to ensure
// consistency"): the downed screen's box and buttons are the Co-op window's own look (its gold button, its font, the
// tooltip background). Called every frame (also from the overlay: the change itself runs outside the frame). show:
// the parts are up; box: the SPECTATING box with `name` and its < > buttons; give / box_area: screen rects;
// on_click(-1 previous, +1 next, 0 give up). True when the native parts exist (else the caller draws its own).
bool set_downed_hud(bool show, bool box, const std::string& name, const sui::Rect& give, const sui::Rect& box_area,
                    void (*on_click)(int));
// The Party panel (the owner's mockup 2026-11-13, images/partymockup.psd, after the game's Pack cards): a "Party"
// header and, per player, a name plate (the own one with a gold edge) with the name and where they are, and a framed
// card with their creature's Sporepedia picture and a health bar. A card click calls on_portrait(player). Called every
// frame; the change runs outside the frame. `scale`: the UI scale (1 = the mockup's size at 1280x960). True when the
// native parts exist (else the caller draws its text list).
struct PartyRow {
    uint8_t player = 0xFF;
    std::string name, place;
    bool me = false;
    float health = -1; // 0..1, below 0: no bar
    float energy = -1; // 3G: the second bar (Creature hunger, Space energy), 0..1; below 0: none
    uint32_t stage = 0; // 3G: the player's game mode (the card's art: Cell, Creature, Tribe, Civ, Space)
    uint32_t portrait = 0, portrait_group = 0; // the creature's png (0x2F7D0004) key, 0 = none
    bool operator==(const PartyRow&) const = default;
};
bool set_party_hud(bool show, const std::vector<PartyRow>& rows, float scale, void (*on_portrait)(uint8_t player));
// the panel's screen rect while it is shown (empty when not)
sui::Rect party_hud_area();
void register_party_commands();

// decision 4S (BUG-9239): the memory box in the middle of the screen (mem_refresh.cpp fills it); on_click(0) Ignore,
// on_click(1) Continue. Callable in the frame (the change is made outside it).
struct MemoryPopup {
    bool show = false, buttons = true;
    float used = 0;          // 0..1, the bar's fill
    // the bar's zones (owner 2026-11-16): green, yellow from yellow_at used, red from red_at used; each zone dim until
    // the fill reaches it, bright where it is filled
    float yellow_at = 0.8f, red_at = 0.9f;
    std::string title, msg[2], line;
    bool operator==(const MemoryPopup& o) const
    {
        return show == o.show && buttons == o.buttons && (used - o.used) * (used - o.used) < 4e-6f &&
               yellow_at == o.yellow_at && red_at == o.red_at && title == o.title && msg[0] == o.msg[0] && msg[1] == o.msg[1] && line == o.line;
    }
};
bool set_memory_popup(const MemoryPopup& want, void (*on_click)(int));

enum MenuStyle { kMenuBlue = 0, kMenuGold = 1, kMenuRed = 2, kMenuGrey = 3 };
// decision 6J (owner 2026-11-18): the players' right-click menu ("View Creature, Invite to Party, Whisper, Add Friend",
// "similar to all native menus"): a panel in the Co-op window's look at a screen point, a gold title and one button per
// item; on_pick(code) when one is chosen (then it closes). x, y: client pixels.
struct MenuItem {
    std::string text;
    int code = 0;
    int style = kMenuBlue; // our button art: data/ui/lineages_button_<colour>_*.png
};
void show_player_menu(float x, float y, const std::string& title, const std::vector<MenuItem>& items,
                      void (*on_pick)(int code));
void hide_player_menu();
bool player_menu_open();
bool point_in_player_menu(float x, float y); // client pixels
// the press at a client pixel on one of the menu's items: chosen (true); the mouse hook does it itself, so a menu works
// under the game's input lock (a caption, the death screen) too
bool player_menu_press(float x, float y);
// decision 6L A (owner 2026-11-18): the Social Menu, a panel at the right edge (F8 anywhere, a button with the two
// person icon next to the Party tab, the galaxy menu's Social item): the party, friends (online or not, where they
// play) and the galaxy's players, with Invite / Whisper / Leave buttons in our own art and the right-click menu on a row
struct SocialRow {
    std::string name, where; // where: "Samuth - Creature", "Galaxy menu", "Offline"
    uint64_t identity = 0;
    int session_id = -1;     // a player of this session (-1: not here)
    int section = 0;         // 0 the party, 1 friends, 2 the galaxy's players
    bool online = true, leader = false, me = false, buddy = false;
};
struct SocialState {
    bool button = false; // the HUD button (in a stage, not paused, not in an editor)
    bool galaxy = false; // connected to a galaxy server (its players listed)
    std::vector<SocialRow> rows;
    int party_n = 0, friends_online = 0, online_n = 0;
    uint32_t stage = 0;  // this player's game mode (the button's art)
    int eras = -1;       // BUG-9334: the "Players in other stages" row: 1 shown, 0 hidden, -1 no row (not on a galaxy)
    int chat = -1;       // 8O: the "Chat shows" row: 1 everyone's lines, 0 the party's only, -1 no row
};
enum SocialAction { kSocLeave = 1, kSocWhisper, kSocInvite, kSocMenu, kSocEras, kSocChat };
void set_social(const SocialState& s, void (*on_action)(int action, const SocialRow& row, float x, float y));
void toggle_social();
bool social_open();
bool social_press(float x, float y, bool right); // client pixels; true: the press was the Social Menu's
std::string social_debug(); // tests (BUG-9334): open, the eras row's button (client pixels) and its caption
// 8M A (BUG-9340): the planet details panel beside Spore's Choose A Stage screen for a galaxy server planet: one row
// per stage (Spore's own stage icon, its players and host, a new stage, grey ones), the friends on the planet
struct PlanetLine {
    std::string name, line, names; // the stage; its players and host (or why grey); the hovered stage's players
    int tone = 0;                  // 0 played (white), 1 a new stage (gold), 2 grey
    bool dot = false;              // players there (green)
    bool hover = false;            // the pointer is on its button on Spore's screen
    void* icon = nullptr;          // Spore's stage button (its drawable is the row's icon)
};
struct PlanetPanel {
    bool open = false;
    std::string title, sub, foot;
    std::vector<PlanetLine> lines;
    std::vector<std::pair<std::string, std::string>> friends; // name, where
    float right = 0, top = 0, bottom = 0; // Spore's stage buttons on the screen (client pixels): the panel goes right of them
};
void set_planet_panel(const PlanetPanel& p);
void planet_panel_hide(); // at once (the screen goes: its icons' drawables are Spore's)
std::string planet_panel_debug(); // tests: open, its rectangle, the rows as shown
// the open Social Menu or player menu covers this screen rectangle (the overlay's name tags and arrows stay off it)
bool ui_covers(float x1, float y1, float x2, float y2);
// the player of the Co-op window's lobby row or the Party card drawn at a client pixel (-1 none)
int player_at(float x, float y);
// the Co-op window opens with its chat line holding `text` (a whisper: "/w <name> ")
void chat_prefill(const std::string& text);
std::string player_at_debug(); // tests: the rows' drawn rectangles and players
std::string debug_state();
extern bool g_log_msgs; // debug: log UI messages reaching our handlers
extern bool g_lan_pages_fix; // tests: `coop.corefix lan_pages 0` lists the first three games only (BUG-0183)
extern bool g_status_fix; // tests: `coop.corefix ui_status 0` keeps the status of the last button (BUG-0104)
// the session was joined (connected) or ended (why): replaces the status line of the join
void session_event(bool connected, const std::string& why);
void remember_server(const std::string& address, const std::string& name);
extern bool g_recenter_fix;  // tests: `coop.corefix window_recenter 0` keeps the first position (BUG-0113)
extern bool g_menu_drop_fix; // tests: `coop.corefix menu_drop 0` keeps the menu item pointer after the menu (BUG-0163)
// tests: the window goes back to its first start (the name step), as with no name chosen yet
void first_start();
// tests / layout shots (coop.uistate color <argb>): the lobby's colour of players without one (0 = "-")
void set_test_color(uint32_t argb);
// README pictures (coop.uistate demo <address> <real port> <shown port>, `demo off`): a documentation address in
// place of the PC's own, the shown port in place of the real one in the window's texts
void set_demo(const std::string& addr, const std::string& from, const std::string& to);
// the text with the demo port in place of the real one (unchanged without `coop.uistate demo`)
std::string demo_text(std::string t);
// tests (coop.uistate widgets): every visible widget, `widget x1 y1 x2 y2 L|B <caption width> '<caption>'`
std::string widgets_state();
// tests (ui_golden's twin): a named control of coop.uistate moved by (dx, dy) or hidden until "off"
std::string test_nudge(const std::string& name, float dx, float dy, bool hide, float dw = 0);

// the dialog text font (0 until the co-op window was built once)
uint32_t text_font();
// the window speaks Russian (ui lang = ru, or auto on a Russian system)
bool russian();
void set_russian(bool ru); // tests (coop.uistate lang): the texts set from now on

} // namespace coop_ui
