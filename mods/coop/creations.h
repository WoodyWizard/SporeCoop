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

namespace creations {

enum Kind : uint8_t { kCreature = 0, kCell = 1, kUfo = 2, kKinds = 3 };
// BUG-0469: every kind of model this game can be asked to build for the mod, not only a player's
// creation: a civ mirror vehicle or a building of a city, a host ship. (Kind stays the creations'
// own: its numbers are on the wire.)
enum ModelKind : uint8_t { kMkCreature = 0, kMkCell, kMkUfo, kMkVehicle, kMkBuilding, kMkKinds };
// MSG_CREATION kinds reserved for 0.7.17 (WP-0 of docs/plan-0.7.17.md, creations 1.2; declared only, nothing sends or
// handles them yet). Kinds 0-2 are Kind above, 3-8 are in creations.cpp, 9 is retired (WP-C2 of an unreleased 0.7.16
// build: never reuse it). An older peer drops a kind it does not know.
constexpr uint8_t kKindHostProps = 10;   // WP-K1 (feat/w74-captain): the host captain's property lists (BUG-1279)
constexpr uint8_t kKindCaptain = 11;     // WP-K1: a player's own captain (#217 a, BUG-1262)
constexpr uint8_t kKindLineageCell = 12; // WP-L2 (feat/w81-cell): a player's old cell as an NPC species (#211)
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
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_player_join(uint8_t id); // host: everybody's creations to the newcomer
void on_session_end();
// BUG-0076: a player left: forget its creations (ids are reused: the next player of the id was shown
// with them, and the host handed them to later joiners). coop.fix core.creations_leave off = keep them
void on_player_leave(uint8_t id);
extern bool g_leave_fix;

// the creation of a player, usable on this machine ({} = none / not received yet)
Key player_key(uint8_t player, Kind k);
// the key that player's game sent for it (its own creation's id there, a premade's, the world's own species): the
// species line tells the host's from a guest's by it ({} = none)
Key player_orig(uint8_t player, Kind k);
// cSpeciesProfile* of a player's creature, nullptr if none
void* player_species(uint8_t player);

// Species of the host's world that a guest's game does not have (creatures of the host's own My
// Creations or of its Sporepedia downloads live on its planet): the guest asks, the host sends the
// creation's records, the guest registers them under the same key. Without them the guest could
// not build these NPCs at all (an ally in the host's pack was invisible, player report 2026-10-25).
void want_species(const Key& k); // guest: this NPC species is unknown here
bool species_pending(const Key& k); // guest: asked, no answer yet
bool avatar_swap_settling(double now); // BUG-0318: the own avatar got a new body moments ago (editors wait)

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
Key local_species(const Key& k); // guest: the host's key of a species -> the key its NPCs use here
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
std::string world_missing_parts();
std::vector<uint32_t> model_parts(const std::vector<uint8_t>& bem); // part ids a creature model names

// My Creations of this kind, newest first (BUG-1476: a guest without an own cell gets the three premade diet cells)
std::vector<Option> options(Kind k);
bool premades_listed(Kind k); // BUG-1476: options(k) are the premades (the co-op window says "game cells")
Option mine(Kind k);
bool one_creature(); // F5: creature.mode host (every guest plays the host's cell and creature)
void choose(Kind k, const Key& key); // {} = keep the world's own
// UI: the next / previous own creation
void cycle(Kind k, int dir);
// BUG-1289: the co-op window shows the creature row in Tribe, Civ and the galaxy menu (a guest without an own
// creature in this world, or a click on the row in the last 30 s); plays_own: this game plays an own creation of k
bool late_row(double now);
bool plays_own(Kind k);
// BUG-1290: a note of this player per world (SporeCoop\worlds.ini [section] <world crc>; 0 = none, world 0 ignored)
uint32_t world_note(const char* section, uint32_t world);
void world_note_set(const char* section, uint32_t world, uint32_t v);

std::string status();
// support trace (trace.h): the own creation of the stage, where the choice came from, what the avatar has
std::string trace_line(Kind k);
std::string debug(const std::vector<std::string>& args);

// WP-0 (BUG-1270, species_line.cpp): a creation of the species line (an outfit, a Civ design) between the games.
// line_pack: the owner's bundle of `own` (model, metadata, pictures; from its own My Creations file when the creation is
// there: the host's world packages may answer the same id with the host's creation at priority 2000, else as this game
// reads it) and its content hash (the same on every game; species::shared_instance of it is its key everywhere); false:
// no model here. Civ designs (WP-C2) use it too: vehicles and buildings of My Creations.
// line_content: the content hash of a received bundle (0: not a bundle). line_register: registers a bundle under the
// shared instance `shared` (checked against its content) on this game and returns that key; {} = refused (a bad
// bundle, another content, parts this game lacks, BUG-0452). The owner registers its own bundle too.
// BUG-1297 (WP-S2a): own_here: the key this game builds its own creation of a kind from (an own creation under a key
// of the host's world is used under its shared remapped key, BUG-0278: the host's world package answers the own key
// here); host_shadows: a guest, and k is a key of the host's world on this game (its package answers for k here)
Key own_here(Kind k);
bool host_shadows(Kind k, const Key& key);
bool line_pack(const Key& own, std::vector<uint8_t>& out, uint32_t& content);
uint32_t line_content(const std::vector<uint8_t>& bundle);
Key line_register(const std::vector<uint8_t>& bundle, uint32_t shared, const std::string& whose);
std::string line_status();

} // namespace creations
