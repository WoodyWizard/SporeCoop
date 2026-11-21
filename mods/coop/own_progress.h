// Own progress in the Cell and Creature stages (BUG-9265; the rules and formats: own_progress_logic.h,
// docs/01-architecture.md#architecture-own-progress).
//
// The session setting all.own_progress (default on): every player's DNA and evolution bar are their own (the gains of
// their own deeds; their own editor's price), nobody evolves alone: the stage changes only when every player's own bar
// is full (and then everybody agrees in the existing vote). The host keeps every guest's last values by the player's
// identity (identity.h key) and the planet (<planet>.spo.duoprogress next to the save, founders::write_side), and gives
// them back when a copy of the host's world opens on that guest (a join, a rejoin, a reload, a new world share), since
// the copy holds the host's own values. Off: the party's one shared total of 0.7.15 (dna_route.h, dna_pace.h).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/buffer.h"
#include "net/session.h"
#include "own_progress_logic.h"
#include "sporekit.h"

namespace own_progress {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;  // this player's notification
    std::function<bool()> elsewhere;                // guest: plays another world than the host's (host_world.h)
    std::function<bool()> one_creature;             // creature.mode host: everybody plays the host's creature
    std::function<bool()> vote_active;              // an evolution / switch vote runs
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);
void on_player_left(uint8_t player);
void on_session_end();

// the setting is on, a session with partners runs and every player plays their own creature: DNA and bars are each
// player's own (the shared-total code steps aside: dna.cpp, cellsync, social, wsync)
bool active();
// this game's own values now (false: not in Cell / Creature with a cell or an avatar)
bool local_values(Values& out);
// every player's own bar is full in the stage played now; `waiting` names those who still fill theirs
bool all_full(std::string* waiting = nullptr);

// the stage's end. Cell: the host's click on History's check is held until every bar is full (then it runs again by
// itself: switch_resume_due); a guest's click is never its own switch. Creature: the host's stage-end request is
// refused until every bar is full (the game keeps its evolve button), with a notice. True: held / refused (told).
bool hold_stage_end(bool host);
// host, Cell: the held click on History's check may run now (every bar full); cleared by the call
bool switch_resume_due();

// host: a gain its game paid for a guest's deed (the hunt credit, a social success): sent to that guest as its own
void give_gain(uint8_t player, float dna);
// guest: a gain the host's game paid for our deed (MSG_PROGRESS kGain, a social result)
void take_gain(float dna, const char* why);

// BUG-9287 (Cell: a player at a lower level than the host found nothing to eat: the one pool is populated at the HOST's
// level, also around the partners, docs/re/host-centric.md G7): nobody grows a Cell level alone. An own meal that would
// take this game's food over the next level line (every 100 food, cell_logic::level_of_food) while a partner is still
// on a lower level gives only what fits; the rest is kept (banked) and added as soon as every partner has reached that
// level. Only own meals (AvatarEat's AddFoodPoints) are clipped. `cur`: this game's food now. Returns the part of
// `amount` the game takes now. `coop.fix cell.level_together off`: each player's level on its own (0.7.15.94).
int clip_meal(int amount, int cur);

// tests: give back now (guest: ask the host again as after a load)
void resync();
std::string status();

} // namespace own_progress
