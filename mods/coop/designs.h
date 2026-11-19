// The shared world's designs on every game (wave E4, docs/re/creations.md#creations-stage-designs):
//  - Tribe: the player tribe's outfit. The Tribal Outfitter (editor 0x156276D1) saves a new creature (the tribe's
//    species with its outfit) in the player's My Creations and reloads the tribe's species profile with it in
//    place (cTribeUI 0xCDCC90 -> cSpeciesManager 0x4DFBC0 -> profile load 0x4D4F70) and rebuilds every member's
//    model. A guest kept in place after the host's editor (BUG-0508) kept the old look (BUG-1156): the host's
//    profile key goes in the wsync row `tribe_outfit`, the creation itself with creations::host_species_out, and a
//    guest reloads its tribe's profile with it the same way.
//  - Civ: the player civilization's culture set (design id -> creation key: City Hall, house, factory,
//    entertainment, the vehicles of each purpose and locomotion). The planner's Sporepedia pick and the editors set
//    an entry through cCivilization 0xBF4C50, which also re-models the civ's buildings and vehicles of that design.
//    Nothing sent it: a guest kept in place kept its old designs (BUG-1157). The host's entries go in the wsync row
//    `civ_design`; a guest whose entry differs calls the same 0xBF4C50 (the host's own My Creations are fetched by
//    the BUG-0647 / BUG-0670 paths, creations::host_design).
//  - A guest's own Tribe outfitter and Civ design editors in the host's world (its result changed only its own game
//    and the next world from the host dropped it, BUG-1158) are refused with a line that says why; a guest's own
//    pick in its planner goes back to the host's design with such a line.
//  - BUG-1272 (WP-T2, 0.7.16): in creature.mode own a guest's Tribal Outfitter is its own: the request is for its own
//    species line, its return does not reload the tribe's species slot on the guest, and the result is its tribe outfit
//    (species::set_tribe_outfit; every game dresses the guest's members in it). The refusal stays for host mode.
// The host's world is the truth: one tribe and one civilization are shared by all players, so a design is the
// shared one (there is no per-player outfit or design in the game: one species profile per tribe, one culture set
// per civilization).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace designs {

// a wsync row entry (wsync.cpp Entry: key + up to 6 values)
struct Val {
    uint32_t key = 0;
    uint32_t v[6] = {};
};

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void on_session_end(); // the guest's tribe profile gets the host's raw keys back (no key of ours in a later save)

// wsync rows (wsync.h: read on the host, sync on a guest; sync returns the differences seen)
extern bool g_outfit_fix; // tribe.outfit_sync (BUG-1156)
extern bool g_civ_fix;    // civ.design_sync (BUG-1157)
extern bool g_own_offline_fix; // designs.own_editor_offline (BUG-1298): own editors over a session's end
bool outfit_read(std::vector<Val>& out);
int outfit_sync(const std::vector<Val>& want, bool apply);
std::string outfit_fmt(const Val& e);
bool civ_read(std::vector<Val>& out);
int civ_sync(const std::vector<Val>& want, bool apply);
std::string civ_fmt(const Val& e);
// the player civilization's culture set entry for a design id (its key, 3 dwords); false: none (not generated)
bool culture_key(uint32_t design_id, uint32_t out[3]);

constexpr uint32_t kOutfitter = 0x156276D1u;   // the Tribal Outfitter (EditorRequest +0xC, Submit 0x5A947C)
constexpr uint32_t kShipEditor = 0x96B24187u;  // the spaceship editor at the end of Civ (0xCF56A0)
// BUG-1158: a guest's own editor request in its stage of the host's world that is refused, and the line it gets
// ("" = not refused). `own_world`: the guest plays a world of its own (then its editors are its own business).
std::string refuse_own_editor(uint32_t mode, uint32_t editor, bool own_world, const std::string& host);
// the texts of the host's outfitter (vote, return): true when the outfit reaches everybody (the switch)
bool outfit_texts(uint32_t mode, uint32_t editor);
// BUG-1272: a guest's own Tribal Outfitter request (the card's edit badge) in the host's world, creature.mode own: true
// when it is taken as ours (its key is now our own species line's; submit it), false otherwise (the refusal decides)
bool own_outfitter(void* req, uint32_t mode, uint32_t editor, bool own_world);
// BUG-1308: our own Tribal Outfitter (own_outfitter took the request) is on its way or open: its creation is our own
// (Creature Creator parts), the editor's part-list error is waived as in the own Creature editor (vote.cpp)
bool own_outfitter_open();
void tick(double now); // every frame: our outfitter's result becomes our tribe outfit

// the player civilization (null: none here), its culture set (Val.key the design id, v[0..2] the key) and one entry
// without generating a missing one (null: none); civ_editors.cpp (BUG-1275)
void* civ_player();
std::vector<Val> civ_culture();
const uint32_t* civ_design(uint32_t id);

std::string status();
std::string debug(const std::vector<std::string>& args);

} // namespace designs
