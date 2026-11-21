// World sharing between host and guests (save folder transfer).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "founders.h"
#include "sporekit.h"

namespace world {

void init(const SkHost* host, SkMod* mod, std::function<void(const std::string&)> feed);
// host: queue the current save folder for a guest (optionally save the game first)
// tag: given back to the share-failed callback (coop.cpp: the guest's join serial, BUG-0231)
bool share_with(uint8_t peer, std::string& err, bool save_first = false, uint32_t tag = 0);
// host: a queued share failed for good ("cannot share the world"); planet_missing: the played
// planet had no file after 3 saves. Called from tick(); not for shares dropped with their player
void set_share_failed(void (*f)(uint8_t peer, uint32_t tag, bool planet_missing));
bool share_now(uint8_t peer, std::string& err);
uint64_t resume_world(); // guest: receipt only while the verified Cell snapshot stays loaded
bool can_resume_world(uint64_t receipt); // host: same Cell world, save and progress
// On the galaxy menu the requested size applies to a new planet. An existing
// named save keeps its own rules. Legacy saves keep the original defaults.
founders::CampaignRules rules_for_current();
// guest: the host sent its campaign rules for this world and stage (a co-op campaign, not a stage-chooser game)
bool host_campaign();
void receive_campaign_rules(const net::Session& session, uint8_t from, net::Reader& packet);
void native_campaign_loaded(const std::string& key);
void campaign_guest_joined(net::Session& session);
bool change_seats(net::Session& session, unsigned seats, std::string& message); // from the next stage (Space: load)
// more seats in the running stage at once (Cell to Civ; owner decision 2026-11-12, founders::raise_now)
bool raise_seats_now(net::Session& session, unsigned seats, std::string& message);
// the Co-op window's Set Seats: more seats now (Cell to Civ), fewer (or any in Space) from the next stage
bool set_seats(net::Session& session, unsigned seats, std::string& message);
void invalidate_campaign_rules(); // native load, including reloading the same slot
bool setup_campaign(net::Session& session, const founders::CampaignRules& requested, std::string& error);
// the game's own save (pause menu "Save"); also works on planets in space
bool save_game(std::string& err);
// owner 2026-11-17 ("seamless for uninterrupted gameplay", alpha 520ace7b): Spore's silent save (message 0x689C9B9 with
// data: no game-time pause, no busy cursor, no wait, no "game saved" box; docs/re/creature.md#creature-save-steps), the
// same checks as save_game; only the write itself remains (0.7-0.9 s). Live (BUG-9304): the campaign autosave only
bool silent_save(std::string& err);
// Host milestone, saved once the native save path is ready.
void request_autosave();
// Tribal reconstruction: confirm a new native save, transfer it to connected
// peers, then reload the host through the existing menu/load path.
bool checkpoint_reload(std::string& err);
bool post_quit_to_menu(std::string& err);
// host (BUG-0754): seconds since a world share to this guest was asked for (the save, the transfer and the guest's load
// follow), a large number when none
double since_share(uint8_t peer);
// the game's quit message 0x4715068 (save and leave the stage for the galaxy menu); refused in an adventure (BUG-0854)
bool post_quit_to_menu(std::string& err);
extern bool g_box_hold; // tests (BUG-0482): our save's "game saved" box is not confirmed while set
bool saved_box_up();     // the "game saved" box of our last save is up (seen, watched)
// coop.cpp: why the game must not save now (an editor opens or is open, a mode switch is pending),
// nullptr when it may
void set_save_blocker(const char* (*probe)());
// host: a save of ours is still being written (the game's editor waits for it: save_game)
bool save_busy();
// host: a share waits for its save (not posted yet)
bool save_pending();
// host: a world share waits because the Space stage saves only off a planet (BUG-0382)
bool share_waits_for_planet();
// the session ended: drop the shares and transfers queued for its players (BUG-0140)
void reset_shares();
// the session ended: drop the guest's queued loads of a received world and a half-received
// transfer (BUG-0074); the player's own coop.load stays
void on_session_end();
// guest: a world load waits (for the galaxy menu or for an editor to close): no own editor now
bool load_queued();
// BUG-9240: a load from the galaxy menu was just started and the game is not in it yet (15 s at most)
bool load_starting();
// guest (BUG-9208): hold a received world while the first-creature editor is requested or open
void hold_for_editor(bool on);
// host (BUG-9210): a player's share waits while this says its own nest is about to be made (2 min at most)
void set_share_hold(std::function<bool(uint8_t, uint64_t)> hold); // (player, ms since the share was asked for)
// host (BUG-9304, decision 7M B): the share of a live Creature -> Tribe entry (the one the host reloads after,
// tribe.host_reload) waits while this says the partners' villages are about to be made (20 s at most), so the save
// carries them and no founders' checkpoint reload follows. (ms since the share was asked for)
void set_entry_share_hold(bool (*hold)(uint64_t waited_ms));
// host: every queued share is held by that hook now (its save is not posted: the founding may run meanwhile)
bool entry_share_held();
uint32_t shared_loads(); // BUG-9223: how many received copies of the host's world this game loaded
// guest: it plays its own planet-side Space intro (the personal launch, BUG-9071): the host's world waits for its
// launch, so a queued load is not coming now (BUG-9202: the loading screen stayed over the playable Civ stage)
bool local_intro_running();
// guest: a world from the host is arriving now; `fraction` its share received (0..1) (BUG-9195: the loading screen)
bool incoming(float& fraction);
extern bool g_share_reset_fix; // tests: `coop.corefix share_reset 0` = the old behaviour
// host: stream queued transfers (call every frame)
void tick(net::Session& s);
bool offer_launch(net::Session& session, uint8_t peer, uint32_t join);
void on_launch_message(net::Session& session, uint8_t from, net::Reader& reader);
void guest_tick(net::Session& session); // guests: load a world that arrived while in an editor
void begin_local_space_intro(); // reserve the guest's first planet-side tutorial through orbital departure
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
void set_host_stage_probe(uint32_t (*probe)());
// share_host: a received world of that host (dropped when the session ends, BUG-0074); "" for the
// player's own load (coop.load)
bool load_slot(const std::wstring& slot, const std::string& planet_file, std::string& err,
               const std::string& share_host = "");
// load Games\<slot_name>\<planet>.spo of the local profile (works from the galaxy menu)
bool load_local(const std::string& slot_name, const std::string& planet, std::string& err);
// BUG-9239 (4S): the save folder the game writes this game to and the planet played ("" in a menu)
bool restart_point(std::wstring& slot, std::string& planet);
// BUG-0332: after a live Creature -> Tribe change the host loads its own share save once it has gone out
void host_reload_after_share();
uint32_t host_reloads();
bool host_reloading(); // BUG-0332: the host's reload of the new Tribe world runs (PF_RESYNC)

// the galaxy menu's intro cinematic GGE_Intro plays (docs/re/space.md 7.9; ~10 s starting ~16 s after
// the menu came up, its input lock drops all clicks on the menu); callable in the frame
bool menu_intro_playing();
// tests (BUG-0167): the galaxy menu intro starts in the first tick of the next stage (coop.introstage)
void arm_intro_in_stage();
// a cinematic or a caption of the game plays (cCinematicManager +0x2C): the HUD is hidden, an editor
// opened meanwhile came up without its interface (coop.cpp own editor)
bool cinematic_playing();
uint32_t cinematic_id(); // the playing cinematic's registry id (0: none), BUG-0656
// ends the cinematic that plays (EndCinematic: stop, release the input lock, post its end message);
// outside the frame. True if one played (BUG-0404: a cinematic left playing in an editor keeps it
// without its buttons)
bool end_cinematic(const char* where);
// the game time stands (cGameTimeManager +0x48 bit 0): the pause menu, the Sporepedia, My Collections,
// a planner (busy.h kPaused); callable in the frame
bool time_paused();
// BUG-1101: the game's pause reasons counted now (cGameTimeManager +0x4C map, +0x68 counts; non-zero ones), the count
// of one, and the game's own Pause / Resume of one (docs/re/tribe.md#tribe-game-time-pauses); callable in the frame
std::vector<std::pair<uint32_t, int>> pause_reasons();
void time_pause(uint32_t reason, bool on);
// BUG-1101: the cinematic manager's own pause (+0x160: 0 none, 1 reason 0x4BF38A5, 2 reason 0x600C4AE), set by the
// cinematic scripts' "pause" step (message 0x46AC0AC) and lifted by "resume" (0x46AC986) or the cinematic's end
uint32_t cinematic_pause_slot();
// the game's own "resume" of that pause (MessageSend 0x46AC986: Resume of the slot's reason, the slot 0); outside
// the frame. True if the slot was set
bool cinematic_pause_resume();

// BUG-0225 (#159): the save file of the game being played, "<slot dir>\<planet>.spo" in lower case
// (UTF-8): the planet file alone is not unique (co-op worlds alternate between two slots of the same
// planet, BUG-0035, and the guest's own copy can be beside them). "" = unknown (a menu: "SaveGame",
// snapshot.spo, no save database). Not for every frame (it asks the resource database).
std::string current_game_key();
std::string game_key_for_planet(const std::string& name); // selected save DB, including immediately before LoadGameByName
// BUG-0225: called after a received world was written into a slot (lower-case dir, UTF-8, no
// trailing '\'): the game that was in that slot no longer exists as it was
void set_slot_replaced_hook(void (*fn)(const std::string& dir));

} // namespace world
