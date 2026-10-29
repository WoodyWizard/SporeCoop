// Space feature registry (0.7 "living galaxy" features). Every Space feature lives in a file of
// its own (space_<name>.cpp) and registers itself here with a static initializer:
//
//   static bool reg = spaceext::add({.name = "map", .kind_lo = 10, .kind_hi = 19, ...});
//
// The registry wires each feature into the mod: MSG_SPACE kinds (spacecoop.cpp relays them), the
// frame tick, the session end, the overlay, the "Space" page of the co-op window (F9), status
// lines and `coop.space <name> ...` debug commands. Features never edit coop.cpp / coop_ui.cpp
// for these. docs/re/space.md has the game knowledge, docs/09-coop-design.md the design.
//
// MSG_SPACE kinds (u8, after the kind byte: u8 origin player id, then the feature's payload):
//   1..9    spacecoop.cpp (rescue, raids, missions, terraforming, spice gifts)
//   10..19  space_map      partner on the galaxy map, colonies roster
//   20..29  space_contracts co-op contracts
//   30..39  space_diplomacy shared relations with NPC empires
//   40..49  space_finale   the galaxy core together
//   50..59  space_rivalry  rivals mode (own budgets, PvP)
//   60..69  space_allies   partner colonies as an allied empire
//   70..79  space_trade    tool gifts and trade between players
//   80..99  free
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/buffer.h"
#include "spacecoop.h"

namespace spaceext {

// one line of the "Space" page in the co-op window: a text and an optional button
struct Row {
    std::string text;
    std::string button;               // "" = no button
    std::function<void()> on_click;   // runs outside the frame (UI and game calls are safe there)
    uint32_t color = 0;               // 0 = the default white
};

struct Feature {
    std::string name;                 // also the `coop.space <name> ...` sub-command
    uint8_t kind_lo = 0, kind_hi = 0; // MSG_SPACE kinds it owns (inclusive), 0 = none
    // after spacecoop::init (symbols, hooks); false = the feature stays off (logged)
    std::function<bool(const SkHost*, SkMod*)> init;
    std::function<void(double now)> tick;                                  // every frame, inside Present
    std::function<void(uint8_t kind, uint8_t origin, net::Reader&)> on_message; // origin != me
    std::function<void()> on_session_end;
    std::function<void()> draw_overlay;                                    // inside Present: draw only
    std::function<std::vector<Row>()> rows;                                // the F9 "Space" page
    std::function<std::string()> status;                                   // one line for coop.space
    std::function<std::string(const std::vector<std::string>& args)> debug; // coop.space <name> args...
};

// registration (static initializers of the feature files)
bool add(Feature f);
std::vector<Feature>& all();

// ---- for features ----
const SkHost* host();
SkMod* mod();
const spacecoop::Ctx& ctx();
bool connected();
bool is_host();
uint8_t my_id();
std::string name_of(uint8_t id);
bool in_space();          // the Space stage is running (false while the `away` test hook is on)
// tests (BUG-0226, BUG-0228, BUG-0229): `coop.space away 1|0` makes the Space modules act as
// outside Space while the Space world stays (as an editor or an adventure entered from Space does):
// in_space() false, no inventory. Cleared at session end and on unload; never saved.
bool away();
void set_away(bool on);
// The Space world in memory (BUG-0228, BUG-0229; a Space-only stand-in for #151's stage epoch).
// +1 on the first kSpace frame after the Space world was torn down (a frame in any mode other
// than kSpace, kEdit, kEditor or kScenario: a load, the galaxy menu, another stage) and when, in
// kSpace, the player's cEmpire changes from one non-null pointer to another. Editors and
// adventures entered from Space keep it. Two equal values: the same Space world. 0 = Space not
// seen yet since the mod loaded (a hot reload starts again at 0). Updated in tick_all, before
// every feature's tick of that frame. Not seen: a reload that shows no non-Space frame and reuses
// the old cEmpire's address.
uint32_t world_epoch();
// the game's saves made while the mod runs (space_allies' save hook counts them; 0 without it)
void note_save();
uint32_t saves();
// another module's work before every game save of any stage (called by the same hook before the
// game writes; BUG-0121: the Tribe co-op epic leaves first)
void on_save(void (*fn)());
bool russian();           // the co-op window speaks Russian
inline const char* tr(const char* en, const char* ru) { return russian() ? ru : en; }
void notice(const std::string& text); // a local toast + chat log line
// to everybody (the host relays a guest's message to the other guests); a message for one
// player carries its id in the payload and the others ignore it
void send(uint8_t kind, const net::Writer& body);

// ---- allies mirrors (owned by space_allies.cpp) ----
// a political id of a mirror empire of this copy (a partner's colonies): local to each copy, never
// shared by pol (BUG-0071)
bool allies_mirror_pol(uint32_t pol);
// the game's missions (cMission*, every state; space_allies.cpp reads the noun list)
std::vector<void*> game_missions();

// ---- rivals mode (owned by space_rivalry.cpp; other features only read it) ----
// rivals(): the host's session option "players are rival empires" (own budgets, diplomacy per
// player). relation(a, b) between two players: allies (the default, also when rivals() is off),
// neutral or at war. The setters are for space_rivalry.cpp only.
enum class Relation : uint8_t { Ally = 0, Neutral = 1, War = 2 };
bool rivals();
Relation relation(uint8_t a, uint8_t b);
void set_rivals(bool on);
void set_relation(uint8_t a, uint8_t b, Relation r);
void clear_relations();

// ---- wiring (spacecoop.cpp, coop.cpp, coop_ui.cpp) ----
void init_all(const SkHost* h, SkMod* m, const spacecoop::Ctx* c);
void tick_all(double now);
bool dispatch(uint8_t kind, uint8_t origin, net::Reader& r); // false: no feature owns the kind
void session_end_all();
void overlay_all();
std::vector<Row> rows_all(); // the page's rows, grouped by feature
std::string status_all();
bool debug(const std::vector<std::string>& args, std::string& out); // false: not a feature's command

} // namespace spaceext
