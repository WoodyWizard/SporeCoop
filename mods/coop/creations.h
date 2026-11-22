// Every player keeps their own creations (creature, cell, spaceship): docs/re/creations.md.
//  - own creation: picked from the player's My Creations (EditorSaves.package); a guest defaults to
//    its newest creation, the host to the species of its world
//  - its records (bem + metadata) go to everybody (the host relays them and sends them to players
//    joining later); a receiver stores them under a collision-free instance in its own small
//    package and registers it with the game's resource manager at runtime
//  - puppets of a player are built from that player's creation; the local avatar takes the own one
//    (abilities follow the parts)
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/dbpf.h"
#include "net/session.h"
#include "sporekit.h"
#include "slotfs.h"

namespace founders { struct Roster; }

namespace creations {

enum Kind : uint8_t { kCreature = 0, kCell = 1, kUfo = 2, kKinds = 3 };
// BUG-0469: every kind of model this game can be asked to build for the mod, not only a player's
// creation: a civ mirror vehicle or a building of a city, a host ship. (Kind stays the creations'
// own: its numbers are on the wire.)
enum ModelKind : uint8_t { kMkCreature = 0, kMkCell, kMkUfo, kMkVehicle, kMkBuilding, kMkKinds };
using Key = dbpf::Key;

struct Option {
    Key key;
    std::string name;
};

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void(const std::string&)> notice; // shown to every player (BUG-0345)
    std::function<std::string(uint8_t)> name_of;
    std::function<void(uint8_t player)> appearance_changed; // rebuild that player's puppet
    std::function<void()> cell_rebuilt; // the own cell hatched again: move it next to a partner
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool key_known(const Key& k); // some registered database has this resource (RM FindRecord)
bool key_in_editor_saves(const Key& k); // BUG-9329: the database that answers for it is the player's own (EditorSaves)
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_player_join(uint8_t id); // host: everybody's creations to the newcomer
void on_session_end();
// BUG-0076: a player left: forget its creations (ids are reused: the next player of the id was shown
// with them, and the host handed them to later joiners). coop.corefix creations_leave 0 = keep them
void on_player_leave(uint8_t id);
extern bool g_leave_fix;

// the creation of a player, usable on this machine ({} = none / not received yet)
Key player_key(uint8_t player, Kind k);
// BUG-9191: a player's creature picture for the Party panel: registered here (png 0x2F7D0004) under portrait_of(player)
// in portrait_group(); 0 = not received (yet)
uint32_t portrait_group();
uint32_t portrait_of(uint8_t player);
// this profile's SporeCoop folder (next to EditorSaves.package), "" while the game has not opened it yet
std::wstring coop_dir();
// cSpeciesProfile* of a player's creature, nullptr if none
void* player_species(uint8_t player);
// BUG-9137: when the player's creation of kind k last changed on this machine (0 = never received)
double player_key_changed_at(uint8_t player, Kind k);
// BUG-9137 (host): the player plays one of the host's own creations (no creature of its own here)
bool plays_host_creation(uint8_t player, Kind k);
// BUG-9137 (host): the creature key is one of the host's own creations (My Creations)
bool host_owns(uint32_t creature_instance);
// BUG-9150 (host, Tribe): a stand-in creature for player `player`'s late village: a copy of the host's creature `host`
// under a key of the player's own, archived in the planet `planet` (founder_ archives). out: its key.
bool make_stand_in(uint8_t player, const Key& host, const std::string& planet, Key& out, std::string& err);
// BUG-9135 roadmap phase 4 (guest): this player plays a creature of their own My Creations (what share_own sends with
// its records), not the host's species, one of the host's creations or "the world's own"
bool plays_own_creature();
bool own_creature_new_here(); // BUG-9203: this guest's own creature first played in this world in this run
// SporeCoop\worlds.ini [section] <world crc> = text: small per-world notes of this player (BUG-9165: its own DNA).
// "" when none; an empty value removes the note.
std::string world_note(const char* section, uint32_t world);
void world_note_set(const char* section, uint32_t world, const std::string& value);
// BUG-9135 roadmap phase 4 (guest, tribe.late_create_editor): the creature editor opened from Tribe for a new creature;
// back in this Tribe world, the newest own creation it saved becomes this player's creature (as after a Creature
// editor). cancel: the request did not reach the game.
void begin_new_creature();
void begin_first_creature();  // BUG-9207: the editor opened in the galaxy menu while the host makes a new world
void cancel_first_creature();
bool first_creature_made();    // BUG-9209: a first creature was saved and not played yet
bool first_creature_pending(); // BUG-9209: back from the first creature's editor, its result not read yet
bool needs_first_creature(uint32_t world); // BUG-9208: no creature of its own in the host's world yet
// owner 2026-11-17 (alpha test): "It didn't give me a creature creator for the first time connecting. It loaded some
// other creature." A galaxy planet's new host with no creature of its own there (5U A) makes one first:
// host_needs_first_creature: no record for this world, the avatar is not one of its own creations, no choice of the
// world's own; host_first_creature_begin before its editor opens: the newest creation saved there becomes its
// creature in this world (recorded, then played)
bool host_needs_first_creature();
void host_first_creature_begin();
void cancel_new_creature();
bool archive_guest_model(uint8_t guest, const std::string& planet, std::string& err);
// Register only validated creature resources belonging to this planet before the native load.
bool restore_guest_model(const std::string& planet, std::string& err);
// Add custom Civ designs to the outgoing snapshot, without editing the loaded slot.
bool archive_world_designs(std::vector<slotfs::File>& files, std::string& err);

// Species of the host's world that a guest's game does not have (creatures of the host's own My
// Creations or of its Sporepedia downloads live on its planet): the guest asks, the host sends the
// creation's records, the guest registers them under the same key. Without them the guest could
// not build these NPCs at all (an ally in the host's pack was invisible, player report 2026-10-25).
void want_species(const Key& k); // guest: this NPC species is unknown here
// BUG-9287 / BUG-9290 (the galaxy's creation store): a creation's bundle (its records; a creature's creaturedata too)
// to put there, and the install of one got from there: registered under a key of ours from its content (as a host's
// own species or design on a guest), the key returned ({} not installed)
bool creation_bundle(const Key& k, std::vector<uint8_t>& out);
Key creation_from_bundle(const std::vector<uint8_t>& data);
bool species_pending(const Key& k); // guest: asked, no answer yet
bool avatar_swap_settling(double now); // BUG-0318: the own avatar got a new body moments ago (editors wait)
bool landfall_pending();
void begin_tribal_outfit(); // snapshot only a guest's voted, own-species outfitter
void begin_personal_launch(); // the guest's first spacecraft editor was accepted
bool personal_captain_outfit(uint32_t instance);
void begin_captain_outfit();
bool needs_launch_checkpoint(const std::string& planet, const founders::Roster& pair);
bool needs_local_space_intro(const std::string& received_planet, bool civilization);
void finish_local_space_intro();
bool schedule_training_restart();
bool prepare_training_ship(void* request, bool& restoring);
void confirm_tribal_outfit(); // host accepted the saved model and founder lineage

// Species of the host's world that are the host's own creations (its My Creations). Creation ids
// are per profile and sequential, so a guest often owns another creation under the same key, and its
// game (its own records, its caches of creaturedata and baked models, all by key) showed that one
// at the host's nests: the player report of 2026-10-25, "Woopsies" at the host's nest were the
// guest's own "Datzu" on the guest. The host sends these creations to each guest before its world
// and before an NPC of one; the guest registers each under a key of its own (0x7D......, from the
// content) and builds the host's NPCs of it from that key; the host's species table reaches it
// under both keys (docs/re/creature.md 15).
void host_species_out(uint8_t peer, const Key& k); // host: before an NPC of species k goes to peer
void world_species_out(uint8_t peer, const std::vector<const std::vector<uint8_t>*>& files); // host: before a world share
// BUG-9111: a player cell of the host's Cell pool to a guest (false: no records found here)
bool host_cell_out(uint8_t peer, const Key& k);
// the FNV-1 of a model's own record (0: not found) and its name: one cell under two keys
uint32_t model_signature(const Key& k, std::string* name = nullptr, std::string* author = nullptr);
Key local_species(const Key& k); // guest: the host's key of a species (creature or BUG-9111 cell) -> the key its NPCs use here
Key host_species(const Key& k);  // guest: ours -> the host's

// BUG-0469: the model a caller may build from `model` (docs/re/creations.md#creations-content-all-kinds):
//  - kSame: this game has the model and every part / paint it names, build it from that key
//  - kSubstituted: some ids this game does not have were shown as substitutes in a copy of the model
//    records, registered under the model's own key above the game's packages: build it from the
//    returned key (the key itself, unchanged: what the game reads for it is our copy)
//  - kRefused: do not build the model here (the caller uses the game's own default or waits)
// allow_copy stays false when the caller cannot pass another key to the game (a building: the game
// reads the civ's design key itself), then a model with unknown ids is refused.
struct Model {
    enum Status : uint8_t { kSame = 0, kSubstituted, kRefused } status = kRefused;
    Key key; // {} = do not build it here
};
Model ensure_model(const Key& model, ModelKind kind, bool allow_copy = true);
std::string content_status();
// BUG-0647: guest: the key a mirror of the host's design `k` is built from (a key of ours for a design of
// the host's own My Creations, else k); *pending: asked the host, no answer yet (do not build it yet)
Key host_design(const Key& k, ModelKind kind, bool* pending);
std::string design_status();
std::string model_hash(const Key& k); // tests: the hash of the bem record this game answers for k // the counters of ensure_model, for the tests and the support trace
bool content_all_kinds(); // the switch content.all_kinds (off: 0.7.3.5 for the new call sites)

// BUG-0341: creatures with parts of the "Creepy & Cute" pack on a game without it (the game faulted
// at 0x441F3B building them): not built here; the host's world is not loaded while one of its own
// creations needs such parts (the names, "" = none)
bool have_creepy_cute();
// Checks the game's live resource manager when a session is opened.
bool campaign_content(const char* version, std::string& profile, std::string& error);
std::string world_missing_parts();
std::vector<uint32_t> model_parts(const std::vector<uint8_t>& bem); // part ids a creature model names

// My Creations of this kind, newest first
std::vector<Option> options(Kind k);
Option mine(Kind k);
bool one_creature(); // F5: creature.mode host (every guest plays the host's cell and creature)
void choose(Kind k, const Key& key); // {} = keep the world's own
// UI: the next / previous own creation
void cycle(Kind k, int dir);

std::string status();
// support trace (trace.h): the own creation of the stage, where the choice came from, what the avatar has
std::string trace_line(Kind k);
std::string debug(const std::vector<std::string>& args);
// Native PNG import, called only outside Present. Development probe.
bool import_library_png(const std::wstring& path, Key& key, std::string& error);
bool inspect_library_png(const std::wstring& path, uint32_t& group, std::string& author, std::string& name);
bool regenerate_library_png(const std::wstring& image, const Key& source, const std::wstring& output);

} // namespace creations
