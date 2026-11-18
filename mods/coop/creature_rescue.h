#pragma once

#include <string>
#include <vector>
#include "net/session.h"
#include "sporekit.h"

namespace creature_rescue {
void init(const SkHost* host, net::Session* session, void (*notify)(const std::string&, int));
// partners: every partner's last state in this world (BUG-9146: all of them, not the one of a duo)
void tick(uint32_t world, void* avatar, const std::vector<net::PlayerState>& partners, double now);
void reset();
void on_player_leave(uint8_t player);
bool continuing();
bool down();
bool pending();
bool protect(void* avatar, float& damage);
void message(uint8_t from, net::Reader& r);
std::string status();
bool ui_shown(); // BUG-9127: the on-screen knockout (coop.fix creature.rescue_ui)
void give_up_now(); // BUG-9118: the downed player gives up (the timer's result at once)
void set_give_up_button(bool shown); // BUG-9156: the downed screen shows GIVE UP (the "Hold G" line is left out)
// BUG-9141: the revive ring's numbers (ring_timer.h)
double window_seconds(); // a knockout's time (30 s; 10 with creature.down_30 off)
float reach_m();         // a partner this close revives
double hold_seconds();   // for this long
float own_left();        // this player is down: the fraction of its time left (0: not down)
float own_revive();      // this player is down: how far a partner's revive got, as seen here (0..1)
// the short line under the top bar while the ring shows the time: "Hold G to give up" for the downed player, nothing
// for the others (the ring and the red arrow say it); without the ring: status()
std::string ui_line();
// BUG-9156: the death pose a downed creature holds (the game's default death animation), on or off
constexpr uint32_t kDownedAnim = 0xF14FAA46u;
bool downed_pose_on();
}
