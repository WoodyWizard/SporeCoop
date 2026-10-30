// World sharing between host and guests (save folder transfer).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace world {

void init(const SkHost* host, SkMod* mod, std::function<void(const std::string&)> feed);
// host: queue the current save folder for a guest (optionally save the game first)
bool share_with(uint8_t peer, std::string& err, bool save_first = false);
bool share_now(uint8_t peer, std::string& err);
// the game's own save (pause menu "Save"); also works on planets in space
bool save_game(std::string& err);
// coop.cpp: why the game must not save now (an editor opens or is open, a mode switch is pending),
// nullptr when it may
void set_save_blocker(const char* (*probe)());
// host: a save of ours is still being written (the game's editor waits for it: save_game)
bool save_busy();
// host: a share waits for its save (not posted yet)
bool save_pending();
// the session ended: drop the shares and transfers queued for its players (BUG-0140)
void reset_shares();
// the session ended: drop the guest's queued loads of a received world and a half-received
// transfer (BUG-0074); the player's own coop.load stays
void on_session_end();
// guest: a world load waits (for the galaxy menu or for an editor to close): no own editor now
bool load_queued();
extern bool g_share_reset_fix; // tests: `coop.corefix share_reset 0` = the old behaviour
// host: stream queued transfers (call every frame)
void tick(net::Session& s);
void guest_tick(); // guests: load a world that arrived while in an editor
// guest: handle MSG_WORLD_* messages
void on_message(uint8_t type, net::Reader& r, const std::string& host_name);
std::string status();
// host: called with the files of a share before its offer goes out (creations: the species of the
// host's own creations first)
void set_share_hook(std::function<void(uint8_t peer, const std::vector<const std::vector<uint8_t>*>& files)> f);
// guest: a world transfer in progress (bytes so far / total)
bool progress(uint32_t& got, uint32_t& total);
void set_auto_load(bool on);
// true while the local player controls something in a stage (coop.cpp): a load waits until the
// stage has been playable for a few seconds before it leaves it
void set_playing_probe(bool (*probe)());
// where the local player is (the adapter's place, coop.cpp): a share save waits 2 s after a change
void set_place_probe(uint32_t (*probe)());
// share_host: a received world of that host (dropped when the session ends, BUG-0074); "" for the
// player's own load (coop.load)
bool load_slot(const std::wstring& slot, const std::string& planet_file, std::string& err,
               const std::string& share_host = "");
// load Games\<slot_name>\<planet>.spo of the local profile (works from the galaxy menu)
bool load_local(const std::string& slot_name, const std::string& planet, std::string& err);

// the galaxy menu's intro cinematic GGE_Intro plays (docs/re/space.md 7.9; ~10 s starting ~16 s after
// the menu came up, its input lock drops all clicks on the menu); callable in the frame
bool menu_intro_playing();
// a cinematic or a caption of the game plays (cCinematicManager +0x2C): the HUD is hidden, an editor
// opened meanwhile came up without its interface (coop.cpp own editor)
bool cinematic_playing();

} // namespace world
