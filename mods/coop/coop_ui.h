// Co-op user interface built from Spore's own UI: a "Co-op" item in the main menu and a
// game dialog with three pages, "connect" (name, address, games on the network, hosting),
// "lobby" (players, stage, ping, ready, chat, world transfer) and, in the Space stage, "space"
// (the rows of the space features, space_ext.h). docs/08-ui.md.
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
};

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
    std::function<std::string()> world_line; // transfer progress / hints, "" if nothing
    std::function<bool()> ready;
    std::function<void(bool)> set_ready;
    std::function<void()> share_world; // host (during a vote: start the evolution now)
    std::function<bool()> vote_active;
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
    // a screenshot for the report first: 1 requested, 0 not possible, -1 switched off (coop.ini)
    std::function<int()> report_shot;
    std::function<int()> report_shot_state;    // 1 being taken, 0 written, -1 failed
    uint16_t default_port = 0;
};

void init(const SkHost* host, SkMod* mod, Api api);
void tick();     // every frame (inside Present): only schedules UI work outside the frame
void open(bool on);
bool is_open();
// SK_EV_OVERLAY: a short tooltip for the co-op window button under the mouse
void draw_tooltip();
// BUG-0599: the window covers (x, y) (screen pixels of the game window): hide it for `secs` so that a click of the
// mod there reaches the game's window under it (the "game saved" box's OK); false = not open or not there
bool step_aside(int x, int y, double secs);
int input_lock_now(); // the game's input lock count (cGameInputManager +0x110; > 0 = clicks are dropped)
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
std::string debug_state();
extern bool g_log_msgs; // debug: log UI messages reaching our handlers
extern bool g_lan_pages_fix; // tests: `coop.corefix lan_pages 0` lists the first three games only (BUG-0183)
extern bool g_status_fix; // tests: `coop.corefix ui_status 0` keeps the status of the last button (BUG-0104)
// the session was joined (connected) or ended (why): replaces the status line of the join
void session_event(bool connected, const std::string& why);
void remember_server(const std::string& address, const std::string& name);
extern bool g_recenter_fix;  // tests: `coop.corefix window_recenter 0` keeps the first position (BUG-0113)
extern bool g_menu_drop_fix; // tests: `coop.corefix menu_drop 0` keeps the menu item pointer after the menu (BUG-0163)

// the dialog text font (0 until the co-op window was built once)
uint32_t text_font();
// the window speaks Russian (ui lang = ru, or auto on a Russian system)
bool russian();
void set_russian(bool ru); // tests (coop.uistate lang): the texts set from now on

} // namespace coop_ui
