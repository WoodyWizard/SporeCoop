// The session glue of the coop core (B2 of 0.7.15: moved out of coop.cpp, docs/audit-0.7.15/core.md CORE-2): the session's
// callbacks (setup_callbacks: connect, join, leave, every message, the players' states), the one session end of every
// path (end_session), host / join / leave, the roles and ready flags, and the host's world for its guests: the share at
// a join or a phase change (on_mode_change, follow_phase, share_failed and its retries, the waits for a cinematic), the
// evolution in place (BUG-0508) and the lobby table.
// COOP_VERSION is defined in coop.cpp (tools/package.sh, release_check.sh and the Makefile read it there); this file
// takes the same text from build/version.h, which the Makefile makes from it. A -DCOOP_VERSION on the command line wins
// here as in coop.cpp (tools/tests/update_check.py builds the next version so, BUG-1189).
#include "version.h"
#ifndef COOP_VERSION
#define COOP_VERSION SC_VER_STR
#endif
#include "lifecycle.h"
#include "civ_wallet.h"
#include "coop_state.h"
#include "store.h"
#include "species_line.h"
#include "own_progress.h"
#include "revive.h"
#include "history.h"
#include "designs.h"
#include "net/compat.h"
#include "net/manifest.h"

namespace coop_core {

// BUG-0019: the world sent after an evolution was saved while the host's game played the
// cinematic of the evolution (CRG_SpeciesMigrate, a caption waiting for OK): the creature in it is
// still the hatchling (age 0) and the migration not started, and a guest's game that loads it never
// grows up nor migrates. Such a share waits until the cinematic had 3 s to start and none plays (at
// most 120 s). `coop.npc sharecine off`: at once, as before (tests).
bool g_share_cine_fix = true;
COOP_FIX("npc.sharecine", "BUG-0019", g_share_cine_fix, "world: a share waits for the evolution cinematic to end");
double g_share_cine_since = 0, g_share_due_at = 0;
// BUG-0232: the cinematic wait counts per guest (Remote::cine_wait_since), restarted whenever the
// guest is marked as needing the world, and the timers end with the session. Off: one global timer
// reset only when a world goes out (0.7.3.0).
bool g_share_cine_each_fix = true;
COOP_FIX("npc.sharecine_each", "BUG-0232", g_share_cine_each_fix, "world: the wait for the evolution cinematic counts per guest");

// BUG-0508: after every host editor (Cell and Creature) every guest got the whole world again (a 3 MB
// save) and went Cell / Creature -> galaxy menu -> load, although the host's new creation (its bundle,
// creations.cpp) had already rebuilt its puppet. A guest of this version in the same world and stage
// now keeps playing: the bundle rebuilds the host's puppet and a guest that plays the world's species
// takes the host's new one in place (creations.cpp `follow`); in Creature the host still saves its own
// game once (its avatar takes the evolved species only with a save) but sends nothing. The world still
// goes when the evolution needs it (docs/re/creations.md#creations-evolve-in-place): a migration started
// (the brain grew, BUG-0019), the guest in another world or stage, an older guest, or (Creature) a guest
// whose avatar still plays the pre-editor species 30 s later.
static uint32_t g_evo_key_before = 0; // host: its avatar's species when its editor opened (Creature)
static int g_evo_age_before = -1;     // host: its avatar's age (+0xB34) then
static bool g_evo_cine = false;       // host: a cinematic held a share since the last evolution
unsigned g_evo_inplace = 0, g_evo_fallback = 0; // tests: guests kept in place, worlds sent after all
// the migration state of this game's player (cPlayer +0x69C property 0x514A219, docs/re/creature.md:
// CreatureGame_AfterEditor): 1 = a migration started by this evolution (CRG_SpeciesMigrate), 2 = its goal
static int host_migration()
{
    static uintptr_t a_GetProperty = H->sym("SporeCoop::Player_GetProperty");
    uint8_t* nm = (uint8_t*)game::noun_manager();
    void* pl = nm && skg::readable(nm + 0x74, 4) ? *(void**)(nm + 0x74) : nullptr;
    return a_GetProperty && pl ? skg::thiscall<int>(a_GetProperty, pl, 0x514A219u) : -1;
}
static int g_evo_migration_before = -1;
static uint32_t g_evo_seen_key = 0; // host: the avatar's species, age and migration in the Creature stage
static int g_evo_seen_age = -1, g_evo_seen_mig = -1;
static double g_evo_seen_at = 0;
unsigned g_evo_saves = 0; // host: saves made for an evolution in place (tests)
static bool g_evo_save_due = false;
// BUG-0517: after an evolution editor the stage's return (0xD43DE0) reloads the avatar's species profile
// in place with the new species (0x4DFBC0: the same profile object, its key +0x504 the new one, the world's
// player species +0xA4 too) and calls vt 0xAC on the avatar and its own-species creatures, but their own
// species key (+0xB28) stays the pre-editor one until the game is saved: every reader of +0xB28 (the
// mod's trace and state, the relations of GetRelationship) saw the old species for minutes on a host that
// did not save (docs/re/creations.md#creations-evolve-key). The key of every creature whose profile is
// the avatar's and whose key is the pre-editor one is set to the profile's, once, within 60 s after the
// editor; then the host's save of BUG-0508 is not needed. Any role, also alone.
double g_evo_key_until = 0;   // watch after an editor (Creature)
uint32_t g_evo_keys_set = 0;  // tests: creatures given the new key
bool g_evo_key_done = false;  // this evolution's keys were set (the host's save is skipped)
void evo_key_tick(double now);
bool g_evo_save_test_off = false; // tests (BUG-0517): the host's in-place save is not made
static int host_avatar_age()
{
    void* av = game::mode_id() == modes::kCreature ? game::avatar() : nullptr;
    return av && skg::readable((uint8_t*)av + 0xB34, 4) ? *(int*)((uint8_t*)av + 0xB34) : -1;
}
static const std::string* peer_version(uint8_t id)
{
    for (auto& p : g_sess.players())
        if (p.id == id)
            return &p.version;
    return nullptr;
}
// host: may guest r take the host's evolution in place (same world, the host's stage or an editor)?
static bool evolve_inplace_ok(uint8_t id, const Remote& r, uint32_t stage)
{
    // (called from on_mode_change too, before g_adapter / g_world follow the new mode)
    const modes::Adapter* ad = modes::for_mode(stage);
    uint32_t world = ad ? modes::current_place(ad) : 0;
    if (r.echo || r.buf.empty() || !ad || ad->own_worlds)
        return false;
    const std::string* v = peer_version(id);
    if (!v || v->empty())
        return false; // an older SporeCoop: it does not follow the host's species in place
    const auto& ls = r.buf.latest();
    bool editor = ls.mode == modes::kEdit || ls.mode == modes::kEditor;
    return (ls.mode == stage || editor) && world && ls.world == world;
}

// BUG-1258 (lab evo_screens_switch_slow: a guest in its OWN first Creature editor read "Alice evolved the species:
// everybody gets the new body"): the host's return from an editor was told to everybody as the species' new body for
// all. In Creature every player makes its own creature (the landfall editor: own editors, BUG-0315 / BUG-1240; the
// mating evolution: offers, BUG-0516) unless the session plays one creature (creature.mode host). On: the notice says
// the host made / evolved its creature. Off: "everybody gets the new body" (0.7.15).
static bool g_evolve_notice_fix = true;
COOP_FIX("core.evolve_notice", "BUG-1258", g_evolve_notice_fix,
         "creature: the host's evolution is told as its own creature's, not as everybody's new body (each player evolves its own)");
bool evolve_own_text(uint32_t to) { return g_evolve_notice_fix && to == modes::kCreature && !host_creature_mode(); }

// BUG-0941: a host that loads another Space save from the galaxy menu sent no world to its connected guests: the
// stage is the same and in Space the state's world is the place (star, planet), so nothing told the guests the save
// was another one; they played on in their copy of the old save. The host's save file (world::current_game_key: the
// slot folder and the planet file) when it entered Space, and when it left Space for the menu, is compared with the
// one it loads into Space: another one is shared with everybody as a new stage is. `coop.fix space.load_follow off`:
// no share (0.7.12; BUG-0933 then holds the shared money on the guests).
static bool g_space_load_follow_fix = true;
COOP_FIX("space.load_follow", "BUG-0941", g_space_load_follow_fix,
         "space: guests follow the host into another Space save it loads");
std::string g_space_key; // the host's Space save as last seen at a mode change
uint32_t g_space_load_shares;

void on_mode_change(uint32_t from, uint32_t to)
{
    if (from == modes::kSpace && to == modes::kMenu) { // BUG-0941: the save it leaves (as written by the quit)
        std::string k = world::current_game_key();
        if (!k.empty())
            g_space_key = k;
    }
    if (to == modes::kLoad || to == modes::kMenu || (stage_mode(to) && to != modes::kTribe))
        g_last_editor_req = 0; // (BUG-0849: the end of Tribe is over; a Tribe game loaded later starts clean)
    if (to == modes::kMenu && stage_mode(from) && g_sess.role() == net::Role::Host && !g_remotes.empty())
        feed("the others play on alone until you load a game again (they get its world)");
    bool editor = to == modes::kEdit || to == modes::kEditor;
    if (editor && stage_mode(from)) {
        g_was_in_editor = true;
        // BUG-0508: what the evolution changes, as last seen in the stage (the avatar is gone by now)
        g_evo_key_before = from == modes::kCreature ? g_evo_seen_key : 0;
        g_evo_age_before = from == modes::kCreature ? g_evo_seen_age : -1;
        g_evo_migration_before = from == modes::kCreature ? g_evo_seen_mig : -1;
    }
    if (!editor && (from == modes::kEdit || from == modes::kEditor)) {
        g_own_editor = false;
        g_editor_left_at = H->time_seconds(); // BUG-0404: the evolution's cinematic starts now
    }
    if (!stage_mode(to))
        return;
    bool new_stage = g_last_stage && to != g_last_stage;
    bool evolved = g_was_in_editor && !new_stage;
    g_was_in_editor = false;
    if (evolved && to == modes::kCreature) { // BUG-0517: any role, also without a session
        g_evo_key_until = H->time_seconds() + 60.0;
        g_evo_key_done = false;
    }
    uint32_t last = g_last_stage;
    g_last_stage = to;
    bool other_save = false; // BUG-0941
    if (to == modes::kSpace && from != modes::kEdit && from != modes::kEditor && from != modes::kScenario) {
        std::string k = world::current_game_key();
        other_save = g_space_load_follow_fix && last == modes::kSpace && !new_stage && !evolved && !k.empty() &&
                     !g_space_key.empty() && k != g_space_key;
        if (other_save)
            LOG("space: the host loaded another Space save (%s, was %s): it goes to everybody (BUG-0941)", k.c_str(),
                g_space_key.c_str());
        if (!k.empty())
            g_space_key = k;
    }
    if (g_sess.role() != net::Role::Host || !g_share_world || g_remotes.empty() || !last)
        return;
    if (!new_stage && !evolved && !other_save)
        return;
    if (other_save)
        g_space_load_shares++;
    if (evolved && tribe_graduating()) { // BUG-0849: no evolution; the Civ stage comes next and is shared then
        LOG("back from the editor %08X in the end of Tribe: no share (the Civilization stage is shared)", g_last_editor_req);
        notice(g_name + " made a design for the Civilization stage: it comes next", -1);
        return;
    }
    g_evo_cine = false;
    g_evo_save_due = evolved;
    for (auto& [id, r] : g_remotes)
        if (!r.echo) {
            r.needs_world = true; // sent by follow_phase as soon as the host controls something
            r.cine_wait_since = 0; // a new wait (BUG-0232)
            r.evo_inplace = evolved && evolve_inplace_ok(id, r, to); // BUG-0508: decided after the wait
            r.evo_check_until = 0;
        }
    g_share_due_at = H->time_seconds() + (evolved ? 3.0 : 0.0); // BUG-0019: its cinematic starts first
    if (new_stage && last == modes::kCreature && to == modes::kTribe)
        world::host_reload_after_share(); // BUG-0332: one ground for everybody (world.cpp)
    notice(new_stage            ? g_name + " entered the " + modes::mode_name(to) + " stage: everybody follows"
           : other_save         ? g_name + " loaded another game: everybody follows"
           : design_stage(to) ? g_name + " changed a design: everybody gets the new designs"
           // BUG-1156: the Tribal Outfitter: the tribe's new outfit, not a new body (designs.cpp sends it)
           : designs::outfit_texts(to, g_last_editor_req) ? g_name + " made a new outfit for the tribe: everybody's tribe wears it"
           // BUG-0588: the City Hall and vehicle editors of the end of Tribe are no evolution
           : to == modes::kTribe && (tribe::stage_end() || tribe_civ_editor(g_last_editor_req)) ? g_name + " made a design for the Civilization stage: it comes next"
           // BUG-1258: in Creature each player evolves its own creature (own editors, offers): only creature.mode host
           // gives everybody the host's new body
           : evolve_own_text(to) ? g_name + (g_last_editor_req == kEditorLandfall ? " made its creature: every player makes its own"
                                                                                  : " evolved its creature")
                              : g_name + " evolved the species: everybody gets the new body",
           -1);
}

void evo_key_tick(double now)
{
    if (g_evo_key_until <= 0)
        return;
    if (now > g_evo_key_until) {
        g_evo_key_until = 0;
        return;
    }
    void* av = g_mode == modes::kCreature ? game::avatar() : nullptr;
    if (!av)
        return;
    uint8_t* prof = *(uint8_t**)((uint8_t*)av + 0xB20);
    if (!prof || IsBadReadPtr(prof + 0x504, 12))
        return;
    skg::ResourceKey nk = *(skg::ResourceKey*)(prof + 0x504);
    skg::ResourceKey ok = *(skg::ResourceKey*)((uint8_t*)av + game::kSpeciesKeyOff);
    if (!nk.instance || !memcmp(&nk, &ok, sizeof nk))
        return; // not applied yet (the stage's return), or nothing to do
    if (g_evo_key_before && ok.instance != g_evo_key_before)
        return; // the avatar is not the pre-editor species (a swap of the mod's): not ours to touch
    struct U {
        uint8_t* prof;
        skg::ResourceKey ok, nk;
        uint32_t n;
    } u{prof, ok, nk, 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            uint8_t* c = (uint8_t*)o;
            if (noun != 0x18EB45Eu || !game::object_alive(o) || IsBadReadPtr(c, 0xB34))
                return true;
            skg::ResourceKey* k = (skg::ResourceKey*)(c + game::kSpeciesKeyOff);
            if (*(uint8_t**)(c + 0xB20) == u->prof && !memcmp(k, &u->ok, sizeof u->ok)) {
                *k = u->nk;
                u->n++;
            }
            return true;
        },
        &u);
    g_evo_keys_set += u.n;
    g_evo_key_done = true;
    g_evo_key_until = 0;
    LOG("evolved: the avatar's species key %08X -> %08X (its profile's, %u creature(s), BUG-0517)", ok.instance, nk.instance,
        u.n);
}

// BUG-0231: a share that failed in world::tick ("cannot share the world: ...", after the save) was
// never retried in that phase: follow_phase / player_join had already set world_sent, so a guest
// in another world of the host's stage stayed there for good. Retried at most 3 times per phase
// and guest, 30 s apart; then the host is told to use coop.share. A "planet file missing" failure
// is not retried (world.cpp already saved 3 times).
uint32_t g_join_serial = 0;   // host: counts joins (Remote::join_serial)
uint32_t g_share_retries = 0; // tests
constexpr int kShareRetries = 3;
constexpr double kShareRetryGap = 30.0;

void share_failed(uint8_t peer, uint32_t tag, bool planet_missing)
{
    if (planet_missing || !tag || g_sess.role() != net::Role::Host)
        return;
    auto it = g_remotes.find(peer);
    if (it == g_remotes.end() || it->second.join_serial != tag)
        return; // the player left, or its id belongs to a newcomer by now (BUG-0166)
    Remote& r = it->second;
    uint64_t key = ((uint64_t)g_mode << 32) | modes::current_world();
    if (r.share_try_key != key) {
        r.share_try_key = key;
        r.share_tries = 0;
    }
    if (r.share_tries >= kShareRetries) {
        feed("cannot share the world with " + r.name + ": use coop.share");
        return;
    }
    r.share_tries++;
    g_share_retries++;
    r.retry_at = H->time_seconds() + kShareRetryGap;
    r.world_sent = 0; // follow_phase sends it again
    if (r.share_at_join) {
        r.needs_world = true;
        r.cine_wait_since = 0; // a new wait (BUG-0232)
    }
    LOG("share for %s failed: retry %d of %d in %.0f s", r.name.c_str(), r.share_tries, kShareRetries, kShareRetryGap);
}

// BUG-0382: after Civ -> Space every guest stayed in its Civ world for ~90 s (campaign_space, the
// monitor's stage_apart) and nobody said why: the host's share waits for the stage's cinematic
// (BUG-0019) and then for the host's ship to leave the home planet (the Space stage saves only off a
// planet, docs/07-modes.md 7.2). The host now tells everybody once per wait that the world goes out
// when its ship leaves the planet, and its trace line says why the share waits (`share_wait=planet`
// or `cinematic`, read by the monitor).
static double g_share_cine_held_at = -1e9; // host: a share was held for a cinematic (follow_phase)
static bool g_planet_wait_told;
// host: why a share waits now ("planet", "cinematic"), nullptr when none does
const char* share_wait_reason()
{
    if (g_sess.role() != net::Role::Host)
        return nullptr;
    if (world::share_waits_for_planet())
        return "planet";
    return H->time_seconds() - g_share_cine_held_at < 2.0 ? "cinematic" : nullptr;
}
void share_wait_tick()
{
    bool planet = g_sess.role() == net::Role::Host && world::share_waits_for_planet();
    if (planet && !g_planet_wait_told)
        notice(g_name + "'s world goes to the others when " + g_name +
                   "'s ship leaves the planet (the Space stage saves only in a solar system or the galaxy)",
               -1);
    g_planet_wait_told = planet;
}

// BUG-1435: the host sent its world again about every 50 s (a player report, 8 shares in 6 minutes, the guest reloading
// each time). The guest plays on in the stage while the share is saved and sent (~40 s for 20 MB), so joined_phase was
// set on those frames; then the guest leaves the stage for the galaxy menu to load the world (its save on quit, the
// menu's 3 s settle: 5.2-5.4 s on the players' PCs), and the 5 s "left to the main menu" window below sent the world
// again. Now a guest waits for the load of the world sent last (any share: the join's, a phase's, coop.share, the
// BUG-0508 fallback): its frames in the stage before it are not "joined the phase", its menu pass is not "left to the
// menu". The wait ends when the guest is back in the host's mode after the share went out and the guest left the stage
// (it loaded the world), 30 s after the guest left the host's mode (a guest that really went to the main menu then gets
// the world again, 5 s later as before), or 120 s after the share went out without the guest leaving (auto_load_world=0,
// a load refused: it plays where it is). `coop.fix core.share_load_wait off`: the old window (0.7.16).
bool g_load_wait_fix = true;
COOP_FIX("core.share_load_wait", "BUG-1435", g_load_wait_fix, "core: a guest passing through the galaxy menu to load the world it was just sent is not taken for one that left to the main menu (no world sent again)");
constexpr double kLoadLeftWait = 30.0, kLoadStayWait = 120.0;
unsigned g_load_waits, g_load_wait_timeouts; // tests (coop.status): waits that ended with the load / by a timeout
// true while the guest has yet to load the world sent last
static bool load_wait(uint8_t id, Remote& r, uint32_t mode, double now)
{
    uint32_t ser = world::share_serial(id);
    if (ser != r.load_serial) {
        r.load_serial = ser;
        r.load_wait = true;
        r.joined_phase = r.load_played = false; // (a new world is on its way: it has not played that one yet)
        r.load_acked = r.load_left = 0;
    }
    if (!r.load_wait)
        return false;
    if (mode == g_mode)
        r.load_played = true;
    if (world::share_in_flight(id)) {
        r.load_acked = r.load_left = 0; // (the guest's frames until the world is there are in the old copy)
        return true;
    }
    if (!r.load_acked)
        r.load_acked = now;
    // the load's way: the galaxy menu, then the loading screen (an editor is not: the load waits until it is left)
    if ((mode == modes::kMenu || mode == modes::kLoad) && !r.load_left)
        r.load_left = now;
    bool loaded = r.load_left && mode == g_mode;
    const char* end = loaded                                              ? "loaded"
                      : r.load_left && now - r.load_left > kLoadLeftWait   ? "is not back 30 s after it left for"
                      : !r.load_left && now - r.load_acked > kLoadStayWait ? "did not leave its game in 120 s for"
                                                                            : nullptr;
    if (!end)
        return true;
    r.load_wait = false;
    r.joined_phase = r.load_played; // as before the share: it played the host's mode since
    (loaded ? g_load_waits : g_load_wait_timeouts)++;
    LOG("share: %s %s the world sent last (%.1f s after it went out)", r.name.c_str(), end, now - r.load_acked);
    return false;
}

// BUG-1434 (b): after a role swap the new host's galaxy menu showed its co-op copy of the first host's world (the
// galaxy menu shows the slot the save database points to: the one loaded last, BUG-0676), the whole galaxy with the
// first host's old planets, and nothing said so: the new host started one of those old planets instead of the world
// played together. Now a host whose save database points to a co-op copy of another player's world is told, once per
// copy and planet in a session, in the co-op window and a notification: whose copy it is, the planet it was made for,
// and, in a stage, whether the planet played is that one. `coop.fix world.copy_notice off`: nothing said (0.7.16).
bool g_copy_notice_fix = true;
COOP_FIX("world.copy_notice", "BUG-1434", g_copy_notice_fix, "world: a host playing from a co-op copy of another player's world is told whose copy it is and which planet it was made for");
void copy_notice_tick()
{
    static std::string told; // the copy (and planet) told in this session
    if (!g_copy_notice_fix || g_sess.role() != net::Role::Host) {
        told.clear();
        return;
    }
    bool stage = stage_mode(g_mode);
    if (!stage && g_mode != modes::kMenu)
        return;
    std::string slot, host, planet;
    bool own = false;
    if (!world::copy_origin(slot, host, planet, own) || host.empty() || host == g_name)
        return;
    std::string cur = stage ? modes::current_world_name() : std::string();
    if (cur.size() > 4 && cur.compare(cur.size() - 4, 4, ".spo") == 0)
        cur.resize(cur.size() - 4);
    if (stage && cur.empty())
        return;
    std::string key = slot + "|" + cur;
    if (key == told)
        return;
    told = key;
    std::string text;
    std::string cs, ch, cp;
    if (!stage && world::copy_old_planets() == 0) // BUG-1434 (0.7.17): a share of one planet, no old games in it
        text = "This galaxy (" + slot + ") is your copy of " + host + "'s world: " + planet + " is the world you played together" +
               (world::last_coop_world(cs, ch, cp) ? " (Continue in the co-op window loads it); " : "; ") + host +
               "'s own save is on " + host + "'s PC";
    else if (!stage)
        text = "This galaxy (" + slot + ") is your copy of " + host + "'s world, made for the planet " + planet +
               ". Its other planets are " + host + "'s older games; " + host + "'s own save is on " + host + "'s PC";
    else if (cur == planet)
        text = "You host " + planet + " from your copy of " + host + "'s world (" + slot + "). " + host +
               "'s own save of " + planet + " is on " + host + "'s PC" +
               (own ? ": this copy is your own save now" : ": your progress here is saved in this copy");
    else
        text = "You host " + cur + " from your copy of " + host + "'s world (" + slot + "): the copy was made for " +
               planet + ", " + cur + " is one of " + host + "'s older games, not the world you played together";
    feed_kind(text, "info");
}

// The host decides when a phase ends: when the host plays a stage and a guest stays in another
// stage (or its editor) for a few seconds, the host world is sent again and the guest loads it.
void follow_phase(double now)
{
    if (g_sess.role() != net::Role::Host || !g_share_world || !g_adapter || g_adapter->own_worlds ||
        !stage_mode(g_mode))
        return;
    uint64_t key = ((uint64_t)g_mode << 32) | modes::current_world();
    if (g_mode == modes::kCreature && g_local && now - g_evo_seen_at > 0.5) { // BUG-0508
        g_evo_seen_at = now;
        g_evo_seen_key = game::species_instance(g_local);
        g_evo_seen_age = host_avatar_age();
        g_evo_seen_mig = host_migration();
    }
    for (auto& [id, r] : g_remotes) {
        if (r.echo)
            continue;
        if (r.retry_at && now < r.retry_at)
            continue; // BUG-0231: a failed share is retried 30 s later
        if (r.needs_world && g_local) { // joined while the host was in a menu
            if (g_share_cine_fix) { // BUG-0019: after the evolution cinematic, at most 120 s
                double& since = g_share_cine_each_fix ? r.cine_wait_since : g_share_cine_since;
                // the busy gate's share mask (busy.h kShareGate: the cinematic, not a pause, BUG-0404)
                unsigned gate = game_busy() & busy::kShareGate;
                bool cine = gate != 0;
                bool waited = since != 0 && cine;
                if (share_wait::hold(since, cine, now, g_share_due_at)) {
                    if (cine) {
                        g_share_cine_held_at = now; // BUG-0382: trace share_wait=cinematic
                        g_evo_cine = true; // BUG-0508: this evolution plays a cinematic: the world goes
                    }
                    continue;
                }
                if (waited)
                    LOG("share: the game is busy for 120 s (%s), the world is sent anyway", busy::names(gate).c_str());
            } else
                g_share_cine_since = 0;
            r.cine_wait_since = 0;
            if (r.evo_inplace) { // BUG-0508: the evolution in place, unless it needs the world after all
                r.evo_inplace = false;
                // (every Creature evolution plays a cinematic after the editor, ~10 s, and the host's avatar
                // takes the new species only after it: lab 20261103-173315; the migration is what the
                // guest's game cannot get without the world, BUG-0019)
                int age = host_avatar_age(), mig = g_mode == modes::kCreature ? host_migration() : -1;
                const char* why = !evolve_inplace_ok(id, r, g_mode)                      ? "left the world or stage"
                                  : g_mode == modes::kCreature && mig < 0                  ? "the migration state is unknown"
                                  : mig > 0 && mig != g_evo_migration_before              ? "a migration started"
                                                                                         : nullptr;
                // (the age is no sign: the evolved avatar is a hatchling, age 0, for a few seconds after the
                // cinematic of every evolution, lab 20261103-173652)
                if (!why) {
                    // The host's own avatar takes the evolved species only with a save of the game: without
                    // the share's save it stayed the pre-editor species for minutes while the world's player
                    // species (cSpeciesManager +0xA4) was the new one (lab 20261103-173652). The save is made,
                    // only not sent (once per evolution, docs/re/creations.md#creations-evolve-in-place).
                    if (g_mode == modes::kCreature && g_evo_save_due && g_evo_key_done) {
                        g_evo_save_due = false; // BUG-0517: the avatar has its new species already
                        LOG("evolved in place: no save needed (the avatar's species key was set, BUG-0517)");
                    }
                    if (g_mode == modes::kCreature && g_evo_save_due && g_evo_save_test_off) {
                        g_evo_save_due = false;
                        LOG("evolved in place: the save is off for a test (coop.evokey nosave)");
                    }
                    if (g_mode == modes::kCreature && g_evo_save_due) {
                        g_evo_save_due = false;
                        std::string err;
                        if (world::save_game(err))
                            g_evo_saves++;
                        else
                            LOG("evolved in place: the save for the host's new body failed: %s", err.c_str());
                    }
                    r.needs_world = false;
                    r.world_sent = key;
                    r.joined_phase = true;
                    r.evo_check_until = g_mode == modes::kCreature && g_evo_key_before ? now + 30.0 : 0;
                    g_evo_inplace++;
                    LOG("evolved in place: %s keeps its world (host species %08X -> %08X, age %d -> %d, migration %d -> %d, cinematic %d)",
                        r.name.c_str(), g_evo_key_before,
                        g_mode == modes::kCreature && game::avatar() ? game::species_instance(game::avatar()) : 0,
                        g_evo_age_before, age, g_evo_migration_before, mig, (int)g_evo_cine);
                    feed(r.name + " gets your new body in place (no reload)");
                    continue;
                }
                LOG("evolved: %s gets the world (%s)", r.name.c_str(), why);
            }
            r.evo_check_until = 0;
            r.needs_world = false;
            r.world_sent = key;
            r.joined_phase = false;
            r.share_at_join = true; // (a retry of it waits for needs_world again)
            std::string err;
            if (world::share_with(id, err, g_save_on_share, r.join_serial))
                feed("sending the world to " + r.name);
            else {
                feed("cannot share the world: " + err);
                share_failed(id, r.join_serial, false);
            }
            continue;
        }
        if (r.buf.empty())
            continue;
        const auto& ls = r.buf.latest();
        // BUG-0508: a guest kept in place whose avatar still plays the host's pre-editor species: the
        // apply failed there (or its switch is off): it gets the world after all
        if (r.evo_check_until) {
            if (ls.mode == modes::kCreature && ls.species && ls.species != g_evo_key_before)
                r.evo_check_until = 0; // it follows (or plays a creature of its own)
            else if (now > r.evo_check_until && ls.mode == modes::kCreature) {
                r.evo_check_until = 0;
                g_evo_fallback++;
                LOG("evolved in place: %s still plays %08X after 30 s: sending the world", r.name.c_str(), ls.species);
                r.needs_world = true;
                r.cine_wait_since = 0;
                continue;
            }
        }
        bool loading = g_load_wait_fix && load_wait(id, r, ls.mode, now); // BUG-1435
        // a guest in an editor is left alone (it may be evolving inside the same stage); a guest
        // that plays another stage than the host is pulled back into the host's stage
        bool behind = ls.mode != g_mode && stage_mode(ls.mode);
        // a guest that played this phase and went to the main menu (pause menu "Exit to Main
        // Menu") while staying in the session waited for the host's world forever (the lobby said
        // "Waiting for the host's world...", play_creature.py 2026-10-23): it gets the world again
        bool left_to_menu = ls.mode == modes::kMenu && r.joined_phase && !loading;
        if (left_to_menu)
            behind = true;
        // the host went to the main menu and loaded another planet of the same stage: the guest
        // stays on the old planet (same mode, another world) and nothing sent the new one
        bool other_world = !g_adapter->place && ls.mode == g_mode && ls.world && g_world && ls.world != g_world;
        if (other_world)
            behind = true;
        if (!behind) {
            r.other_mode_since = -1;
            if (ls.mode == g_mode && !loading)
                r.joined_phase = true;
            continue;
        }
        if (r.other_mode_since < 0)
            r.other_mode_since = now;
        // once per phase, or again when the guest had followed and then left the phase
        if (now - r.other_mode_since < 5.0 || (r.world_sent == key && !r.joined_phase))
            continue;
        r.world_sent = key;
        r.joined_phase = false;
        r.share_at_join = false;
        std::string err;
        if (world::share_with(id, err, g_save_on_share, r.join_serial))
            feed((left_to_menu || other_world ? "back to the world: sending it to " : "next phase: sending the world to ") + r.name);
        else {
            feed("cannot share the world: " + err);
            share_failed(id, r.join_serial, false);
        }
    }
}

void send_lobby()
{
    net::Writer w;
    auto pl = g_sess.players();
    std::vector<net::LobbyEntry> t;
    for (auto& p : pl) {
        net::LobbyEntry e;
        e.id = p.id;
        e.ready = g_ready[p.id];
        e.rtt = p.id == 0 ? 0 : g_sess.rtt_of(p.id);
        e.role = g_roles[p.id];
        t.push_back(e);
    }
    net::write_lobby(w, t, g_role_fix);
    for (auto& p : pl)
        if (p.id != 0)
            g_sess.send_reliable_to(p.id, net::MSG_LOBBY, w);
}

void lobby_tick(double now)
{
    if (g_sess.role() == net::Role::Host) {
        if (g_lobby_dirty || now - g_last_lobby > 1.0) {
            g_lobby_dirty = false;
            g_last_lobby = now;
            send_lobby();
            const char* mode = modes::mode_name(g_mode);
            std::string world = modes::current_world_name();
            if (world.size() > 4 && world.substr(world.size() - 4) == ".spo")
                world.resize(world.size() - 4);
            g_sess.lobby_info = std::string(mode) + (world.empty() || !g_adapter ? "" : " - " + world);
            // BUG-0776: discovery queries from a subnet of this machine's adapters are answered too (a virtual LAN
            // on public numbers); the list comes from the background lookup (adapters.cpp, BUG-0286)
            static uint32_t bc_gen = ~0u;
            if (adapters::generation() != bc_gen || g_sess.lan_broadcasts.empty()) {
                bc_gen = adapters::generation();
                g_sess.lan_broadcasts.clear();
                for (auto& a : local_addresses())
                    if (a.broadcast != 0xFFFFFFFFu && a.broadcast != 0)
                        g_sess.lan_broadcasts.push_back(a.broadcast);
            }
        }
    }
    if (g_disc_on) {
        uint32_t t = net::now_ms();
        if (g_lan_fake_n && (int32_t)(t - g_lan_fake_until) < 0) // coop.lanfake (tests)
            for (int i = 0; i < g_lan_fake_n; i++) {
                net::DiscoverReply rep;
                rep.port = net::kDefaultPort;
                rep.name = "fake" + std::to_string(i + 1);
                rep.players = 1;
                rep.max_players = 4;
                rep.info = "test";
                g_disc.on_reply({0x000200C0u | (uint32_t)(i + 1) << 24, 0}, rep, t); // 192.0.2.<i+1> (TEST-NET-1)
            }
        g_disc.update(t);
    }
}

// Every way a session ends runs the same cleanup (BUG-0033): the host or the network ending it
// (cb.disconnected), the player's Leave (do_leave), a new host/join while a session is open
// (Session::host/join only close the socket) and the mod unload. Before, each path cleaned its own
// subset of the stage modules: after a Leave the Space mirror ships flew on with their own AI, the
// tribe and civ tables and the creations of the others stayed, and a new session inherited the old
// remotes.
bool g_id_reuse_fix = true; // coop.fix core.id_reuse off: a joining player inherits an old entry of its id
COOP_FIX("core.id_reuse", "BUG-0076", g_id_reuse_fix, "core: a joining player does not inherit an old entry of its id");
COOP_FIX("core.unique_names", "BUG-0141", g_sess.unique_names, "net: players with the same name get unique names");
COOP_FIX("core.rejoin_name", "BUG-0209", g_sess.rejoin_by_name, "net: a rejoin after a crash replaces the silent entry of its name");
COOP_FIX("core.text_utf8", "BUG-0164", utf8::g_cut_fix, "net: texts are cut between characters, not bytes");
COOP_FIX("core.notify_text", "BUG-0129", notify_text::g_fix, "ui: notification lines wrap by characters, the panel kind is not guessed");
// the Give recipient picked with ">" (civ); BUG-0234: cleared when its player leaves and at the
// session end (it pointed at whoever got that id next)
uint8_t g_give_pick = net::kNoGiveTarget;
bool g_give_pick_reset_fix = true;
COOP_FIX("give.pick_reset", "BUG-0234", g_give_pick_reset_fix, "civ: the Give pick ends with its player and the session");
// BUG-0770: the session settings of the host and the session report's round / question / upload end on every session
// end (a Leave, a new host or join, the unload), not only when the network drops it (cb.disconnected)
bool g_session_state_end_fix = true;
bool g_settings_in = true; // not a lab switch: tests (coop.settingsin 0) drop the host's settings tables
COOP_FIX("core.session_state_end", "BUG-0770", g_session_state_end_fix, "core: the host's settings table and the session report end with every session");

// The session-end structure (BUG-0214, GitHub #148; docs/06-testing.md#testing-fix-registry):
//  the module hooks: every path calls every module (BUG-0033);
//  the remotes (core.remotes): despawned and cleared on every path (BUG-0033);
//  the core's state (core.session) and the SESSION_STATE entries: every reset of the core's state, on every path,
//    each gated only by its own switch. It may run inside g_sess.update() (Disconnect, BUG-0029): mod state only,
//    no game calls, no sends, no waiting; idempotent.
// A new core reset goes into core_state_end or a SESSION_STATE entry; a module's into its own on_session_end, registered
// next to it with LIFECYCLE_HOOK(kSessionEnd) at an order of its own.
// C2 (BUG-1141): the end of a session is one fan-out of the lifecycle observers (lifecycle.h kSessionEnd, in their
// order: pfind -100, support, npc, nest, fields, wobj, realign, parts, effects, social, tribe, cellsync (kCellToo),
// creations, civ, squadron, spacecoop -40, the remotes -20, the vote and the puppets' test holds -16, the shares -15,
// shared, wsync, world -12, the core's session state -10, then every SESSION_STATE at 0: the order of 0.7.14's hand
// list), run here at once as before (game calls allowed: every caller is the main thread).
static const char* end_why(uint8_t path)
{
    EndPath p = (EndPath)path;
    return p == EndPath::Leave ? "left" : p == EndPath::Unload ? "mod unload" : p == EndPath::Restart ? "a new session" : "disconnected";
}
// the partners' puppets and views
static void remotes_end()
{
    for (auto& [id, r] : g_remotes)
        despawn(r);
    g_remotes.clear();
    views::clear();
}
// the vote and the puppet test holds
static void vote_end()
{
    if (g_vote_scope_fix)
        drop_vote("the session ended");
    g_vote_action_due = nullptr; // BUG-0404: a result still waiting at the busy gate ends with its session
    g_puppet_hold = false; // tests (coop.puppet hold, BUG-0251)
    g_puppet_hide_until = 0; // tests (coop.puppet respawn <s>, BUG-0453)
}
// the core's own session state (DNA, editors, shares' waits, settings, the session report)
static void core_state_end()
{
    g_dna_pending = -1;
    if (g_host_away_fix) { // BUG-0085
        g_host_away = false;
        g_host_away_since = -1;
        g_solo_dna = 0;
        g_solo_mode = 0;
    }
    g_dna_owed = 0;
    g_dna_owed_world = 0;
    g_solo_await = 0; // BUG-0829
    g_pace_heard = 1; // BUG-0510
    g_pace_carry.reset();
    g_dna_spend_owed = 0; // BUG-0466
    if (g_own_editor_flags_fix && !(game::mode_id() == modes::kEdit || game::mode_id() == modes::kEditor)) {
        g_own_editor = false; // BUG-0811: (in an editor its result still comes, and charges, as in a game of one)
        g_own_editor_charge_due = false;
    }
    g_dna_out.clear();
    g_dna_notices.clear();
    g_dna_sent_to.clear(); // WP-D (the wallets stay: the host's world keeps them)
    g_own_heard = false;
    g_state_mode_override = 0; // tests
    if (g_own_editor_end_fix) {
        g_own_editor_due = 0; // BUG-0234 (a task already queued reads it first and returns)
        g_own_editor_arriving = false; // BUG-1240
    }
    if (g_give_pick_reset_fix)
        g_give_pick = net::kNoGiveTarget; // BUG-0234: a new session starts with no pick
    if (g_share_cine_each_fix) { // BUG-0232: no wait of an old session shortens a later one
        g_share_cine_since = 0;
        g_share_due_at = 0;
    }
    if (g_session_state_end_fix) { // BUG-0770 (state only: safe inside g_sess.update())
        settings::on_disconnect();
        sreport::on_session_end();
    }
}
LIFECYCLE_HOOK("support.session", lifecycle::kSessionEnd, -99,
               [](const lifecycle::Ctx& c) { support::on_session_end(end_why(c.path)); }, false);
LIFECYCLE_HOOK("core.remotes", lifecycle::kSessionEnd, -20, [](const lifecycle::Ctx&) { remotes_end(); }, true);
LIFECYCLE_HOOK("core.vote", lifecycle::kSessionEnd, -16, [](const lifecycle::Ctx&) { vote_end(); }, true);
LIFECYCLE_HOOK("world.shares", lifecycle::kSessionEnd, -15, [](const lifecycle::Ctx&) { world::reset_shares(); }, false);
LIFECYCLE_HOOK("core.session", lifecycle::kSessionEnd, -10, [](const lifecycle::Ctx&) { core_state_end(); }, false);

void end_session(EndPath path, bool cell)
{
    lifecycle::Ctx c;
    c.event = lifecycle::kSessionEnd;
    c.path = (uint8_t)path;
    c.flags = cell ? lifecycle::kCellToo : 0;
    lifecycle::fanout().state_resets = g_lifecycle_fix; // BUG-1055: every SESSION_STATE (mod state only)
    lifecycle::fire(c, true);
}

// BUG-0204: the host name lookup of a join runs on a thread that holds a reference to coop.dll,
// so a hot reload during a slow lookup does not unmap the code under it (as BUG-0060)
struct PinnedStart {
    void (*fn)(void*);
    void* arg;
    HMODULE self;
};

DWORD WINAPI pinned_main(void* p)
{
    PinnedStart s = *static_cast<PinnedStart*>(p);
    delete static_cast<PinnedStart*>(p);
    s.fn(s.arg);
    FreeLibraryAndExitThread(s.self, 0);
}

bool spawn_pinned(void (*fn)(void*), void* arg)
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)(void*)&spawn_pinned, &self))
        return false;
    auto* s = new PinnedStart{fn, arg, self};
    HANDLE h = CreateThread(nullptr, 0, pinned_main, s, 0, nullptr);
    if (!h) {
        delete s;
        FreeLibrary(self);
        return false;
    }
    CloseHandle(h);
    return true;
}

// coop.slowdns (tests, BUG-0204): a name server that answers late
std::atomic<int> g_slow_dns_ms{0};
net::LookupFn g_real_lookup = nullptr;

bool slow_lookup(const std::string& host, uint32_t& ip)
{
    if (int ms = g_slow_dns_ms.load())
        Sleep((DWORD)ms);
    return g_real_lookup(host, ip);
}

void setup_callbacks()
{
    net::g_spawn_thread = spawn_pinned;
    g_sess.build = build_id();
    // the versions every peer logs on a join (CAP_VERSION): mod, SporeKit, game build and exe hash
    g_sess.version = std::string("coop " COOP_VERSION ", SporeKit ") + H->kit_version() + ", " + H->build_name() + " " +
                     std::string(H->exe_sha256()).substr(0, 8);
    g_sess.cb.log = [](const std::string& s) { LOG("net: %s", s.c_str()); };
    g_sess.cb.connected = [] {
        support::on_session_start();
        support::on_connected();
        pfind::on_connected(); // F2: the colour this player picked goes to the host
        feed("connected to host");
        coop_ui::session_event(true, ""); // BUG-0104: "Connecting to ..." ends here
    };
    g_sess.cb.disconnected = [](const std::string& why) {
        support::on_disconnected(why);
        rejoin::clear(net::g_fix.join_seen); // the session ended (BUG-0235); kept for this host and name (BUG-1001)
        settings::on_disconnect();
        sreport::on_disconnect();
        feed("disconnected: " + why);
        coop_ui::session_event(false, why);
        chat_log("* disconnected: " + why);
        g_ready.clear();
        g_lobby_rtt.clear();
        end_session(EndPath::Disconnect, true);
    };
    g_sess.cb.player_join = [](const net::PlayerInfo& p) {
        support::on_player_join(p);
        settings::on_player_join(p.id); // F3: the host's session settings first
        store::on_player_join(p.id);    // E1: a stored name under a new key: the host is told
        own_progress::on_player_join(p.id); // BUG-1469: an older mod is not let in while own progress is played
        pfind::on_player_join(p.id);    // F2: the host gives the newcomer a colour, everybody gets the table
        shared::on_player_join(g_sess.role());
        g_world_announced = 0; // BUG-0933: the newcomer's WELCOME may have named no world (the host in an editor)
        host_busy_join(p.id); // C3 (BUG-1142): the host's busy state as last told
        wsync::on_player_join();
        creations::on_player_join(p.id);
        species::on_player_join(p.id); // WP-0 (BUG-1270): after the creations (the rows name their creatures)
        npc::on_player_leave(p.id); // a newcomer under a reused id knows no NPCs yet (BUG-0125)
        if (g_player_forget_fix && p.id != g_sess.my_id())
            player_life::forget_all(p.id); // BUG-0422: nothing of an earlier player of this id
        g_dna_sent = -1; // resend the DNA total to the newcomer
        g_dna_sent_to.erase(p.id); // WP-D: its own total too
        auto old = g_remotes.find(p.id);
        if (g_id_reuse_fix && old != g_remotes.end() && !old->second.echo) {
            // BUG-0109 / BUG-0076: an entry of an earlier player under this id (a ghost made by a
            // late state): its puppet and its buffer are not the newcomer's (states of the new
            // player's clock older than the ghost's newest were all dropped as old)
            despawn(old->second);
            g_remotes.erase(old);
        }
        auto& r = g_remotes[p.id];
        r.id = p.id;
        r.name = p.name;
        r.join_serial = ++g_join_serial; // BUG-0231: the tag of this guest's shares
        r.share_tries = 0;
        r.share_try_key = 0;
        r.retry_at = 0;
        trace::line("session", "player %u '%s' joined (we are %s, %s, world %s): %s", p.id, p.name.c_str(), role_str(),
                    modes::mode_name(g_mode), trace::hex(g_world).c_str(),
                    p.version.empty() ? "version unknown (pre-0.7.4)" : p.version.c_str());
        if (!p.version.empty() && p.version != g_sess.version) {
            // the same exe build is checked by the protocol; another mod or SporeKit version is allowed
            // but behaves differently: a support report needs to see it at once
            H->log(M, SK_WARN, "versions differ: player %u '%s' runs %s, this game %s", p.id, p.name.c_str(),
                   p.version.c_str(), g_sess.version.c_str());
            // B6 (BUG-1137, owner decision 7): another 0.7.x is let in with this warning on both sides (a family of
            // another revision that the stage uses was refused by net::Session already)
            if (p.version.substr(0, p.version.find(',')) != g_sess.version.substr(0, g_sess.version.find(',')))
                feed_kind(net::compat::warn_text(p.name, p.version, g_sess.version), "info");
        }
        feed_kind(p.name + " joined the pack", "join"); // a name like "Lost" is not danger (BUG-0129)
        chat_log("* " + p.name + " joined");
        g_lobby_dirty = true;
        bool own_worlds = g_adapter && g_adapter->own_worlds;
        bool playing = g_adapter && g_local && stage_mode(g_mode);
        if (g_sess.role() == net::Role::Host && g_share_world && !own_worlds && !playing) {
            // in menus or loading screens there is no world to send yet (the current save name
            // is a stale snapshot): send it when the host starts playing
            r.needs_world = true;
            r.cine_wait_since = 0;
            feed(p.name + " waits for your world: load or start a game");
        } else if (g_sess.role() == net::Role::Host && g_share_world && !own_worlds) {
            std::string err;
            bool save = g_save_on_share && g_adapter; // only while playing, not from menus
            r.world_sent = ((uint64_t)g_mode << 32) | modes::current_world();
            r.joined_phase = false;
            r.share_at_join = true;
            if (!world::share_with(p.id, err, save, r.join_serial)) {
                feed("cannot share the world: " + err);
                share_failed(p.id, r.join_serial, false);
            } else
                feed("sending the world to " + p.name);
        }
    };
    g_sess.cb.custom = [](uint8_t type, uint8_t from, net::Reader& r) {
        // B3 of 0.7.15 (BUG-1136): the route of every type comes from net/messages.def (net::dispatch_route: the type's
        // handler, and for the handlers of one role only that role, as the hand-written chain of 0.7.14 had them; the
        // golden test in tests/validate_tests.cpp compares the two for every type and role). The host's direction gate
        // (a host-only type from a guest) is in net::Session, before this callback.
        // The world gate: a guest elsewhere drops the host's world state (also before the host's first state when the
        // host named no world in WELCOME: one Civ cycle applied in that window bound the guest's own vehicles for good,
        // BUG-0231; with Welcome::world a guest in the host's world is Same from WELCOME on and loses nothing). B3: it
        // comes first, before every route: it only drops HostWorld and unknown types, and none of the routes the 0.7.14
        // chain tested before it takes one of those (tests/validate_tests.cpp checks that)
        if (from == 0 && gate_drops(type)) {
            if (type == net::MSG_DNA) {
                float total = r.f32();
                if (r.ok())
                    g_dna_pending = total;
            }
            g_place_dropped++;
            return;
        }
        switch (net::dispatch_route(type, g_sess.role() == net::Role::Host)) {
        case net::Route::None:
            return;
        case net::Route::Diag: { // support diagnostics: clock, marks, sync-check (diag_coop.h)
            support::on_message(from, r);
            return;
        }
        case net::Route::Anim: {
            net::AnimEvent ev;
            if (ev.read(r))
                on_remote_anim(ev);
            return;
        }
        case net::Route::Settings: { // F3: the host's session settings (Session class: wherever we play)
            if (!g_settings_in)
                return; // tests (coop.settingsin 0): a host older than 0.7.12 sends no table
            settings::on_message(type, from, r);
            return;
        }
        case net::Route::Colors: { // F2: the player colours (Session class)
            pfind::on_message(type, from, r);
            return;
        }
        case net::Route::Report: { // F4: the session report (BUG-0767)
            sreport::on_message(from, r);
            return;
        }
        case net::Route::HostWorld: { // BUG-0933: the host's save now (Session class)
            uint32_t w = r.u32();
            if (from == 0 && r.ok() && w != 0 && g_sess.role() == net::Role::Client) {
                g_host_save = w;
                g_host_save_heard++;
            }
            return;
        }
        case net::Route::HostBusy: { // C3 (BUG-1142): the host's game is paused, in an editor or away (Session class)
            host_busy_message(from, r);
            return;
        }
        case net::Route::Effect: { // BUG-0451: a partner's creature made an effect
            effects::on_message(type, from, r);
            return;
        }
        case net::Route::Dna: {
            float total = r.f32(); // (row 6, elsewhere, is the gate above: kept as g_dna_pending)
            std::vector<dna_ledger::Part> parts; // the host's causes (an older host sends none)
            dna_own::Tail tail;                  // WP-D: after the pace byte (an older host sends none)
            if (r.ok() && r.left() >= 1) {
                unsigned n = r.u8();
                for (unsigned i = 0; i < n && r.left() >= 6; i++) {
                    uint8_t c = r.u8(), pl = r.u8();
                    float a = r.f32();
                    if (c >= 1 && c <= 4 && std::isfinite(a) && std::fabs(a) < 100000)
                        parts.push_back({(dna_ledger::Cause)c, pl, a});
                }
                // BUG-0510: the host's pace divisor (an older host sends none: the full sum)
                dna_pace_heard(r.ok() && r.left() >= 1 ? r.u8() : 1);
                tail = dna_own::Tail::read(r);
            } else
                dna_pace_heard(1);
            if (own_progress::active()) { // BUG-1469: our DNA and bar are our own (a host total is the host's)
                g_own_totals_ignored++;
                return;
            }
            if (r.ok() && g_solo_await > 0) { // BUG-0829
                bool paid = false;
                for (auto& p : parts)
                    paid = paid || (p.cause == dna_ledger::Cause::Earned && p.player == g_sess.my_id() &&
                                    p.amount >= g_solo_await * 0.5f);
                if (paid || H->time_seconds() > g_solo_await_until)
                    g_solo_await = 0;
                else if (!g_host_away && dna_route::host_total(false, false, game::avatar() != nullptr) == dna_route::Total::Apply) {
                    g_solo_stale_totals++;
                    LOG("dna: host total %.0f before it added the %.0f DNA earned here while it was away: not applied (BUG-0829)",
                        total, g_solo_await);
                    for (auto& p : parts)
                        dna_note(p, g_sess.my_id());
                    return;
                }
            }
            if (r.ok() && dna_route::host_total(false, g_host_away, game::avatar() != nullptr) == dna_route::Total::Apply) {
                dna_spend_sample(); // BUG-0466: a spend since the last tick is ours, not undone by this total
                total = std::max(0.f, total + g_dna_spend); // a spend not sent yet stays off
                float before = game::dna(), d = total - before;
                bool own = tail.own() && (!tail.world || !g_world || tail.world == g_world);
                g_own_heard = own;
                g_own_bar_heard = tail.has ? tail.bar : NAN;
                if (own && g_dna_bar_set_fix) {
                    // WP-D (BUG-1291): our own wallet: the budget with the game's setter (the DNA only, as its editor
                    // spends), the bar up to the party's (AddEvolutionPoints first; never down). After a reload our
                    // game has the host's save value and bar: a plain AddEvolutionPoints of the difference would move
                    // the bar by it (the fork's "set the budget, move the bar" order, credited to Lineages)
                    float bar = game::evolution_bar();
                    auto a = dna_own::apply_own(total, tail.bar, before, bar, g_bar_setback_fix);
                    if (a.set) {
                        LOG("dna: own %.0f -> %.0f, bar %.1f -> party %.1f%s cause=%s", before, total, bar, tail.bar,
                            a.raise > 0 ? "" : a.raise < 0 ? " (the party's setback, BUG-1393)" : " (kept)",
                            dna_ledger::causes_text(parts).c_str());
                        game::dna_set_and_bar(total, a.raise);
                        g_own_applied++;
                        for (auto& p : parts)
                            dna_note(p, g_sess.my_id());
                    }
                } else if (d != 0) {
                    LOG("dna: %.0f -> %.0f cause=%s", before, total, dna_ledger::causes_text(parts).c_str());
                    game::dna_add_raw(d); // through the game so the UI/progress updates
                    for (auto& p : parts)
                        dna_note(p, g_sess.my_id());
                }
                dna_spend_resync();
                g_dna_rx++;
            }
            return;
        }
        case net::Route::DnaAdd: {
            float d = r.f32();
            if (own_progress::active()) { // BUG-1469: a guest's DNA is its own (a stray forward is not the party's)
                g_own_totals_ignored++;
                return;
            }
            auto add = dna_route::host_add(game::avatar() != nullptr, g_away_dna_fix);
            if (r.ok() && d > 0 && d < 10000 && add == dna_route::Add::Pay) {
                dna_host_gain({dna_ledger::Cause::Earned, from, d}, game::dna());
                game::dna_add_raw(d);
                feed(g_sess.name_of(from) + " earned " + std::to_string((int)d) + " DNA");
            } else if (r.ok() && d < 0 && d > -100000) { // BUG-0466: a guest's own spend
                g_dna_spends_got++;
                if (dna_own_wallet(from, net::Family::creature)) // WP-D (BUG-1291): its own DNA only, the host's game untouched
                    dna_own_spend(from, d);
                else if (add == dna_route::Add::Pay) {
                    float dd = std::max(d, -game::dna()); // the total never goes below 0
                    dna_host_gain({dna_ledger::Cause::Earned, from, dd}, game::dna());
                    game::dna_add_raw(dd);
                } else
                    g_dna_spend_owed += d; // in an editor or a load: taken off when our creature is back
            } else if (r.ok() && d > 0 && d < 10000 && add == dna_route::Add::Owe && g_dna_owed < 100000) {
                trace::line("dna", "from %u +%.0f kept as owed (no own creature now)", from, d);
                if (g_dna_owed <= 0)
                    g_dna_owed_world = g_stage_world; // BUG-0233: paid only into this world
                g_dna_owed += d; // in an editor or a load: paid when our creature is back
            }
            return;
        }
        case net::Route::WorldTime: {
            net::Quat q;
            q.x = r.f32(); q.y = r.f32(); q.z = r.f32(); q.w = r.f32();
            if (r.ok() && game::avatar() && !wsync::owns_time_of_day() && // BUG-0579: else the wsync row
                !adventure_sky_own(game::mode_id()))                       // BUG-0789
                game::set_time_of_day(q);
            return;
        }
        case net::Route::Vote: {
            bool on = r.u8() != 0;
            int left = r.u8();
            std::string text = r.str8();
            if (!r.ok())
                return;
            bool has_editor = r.left() >= 4;
            uint32_t editor = has_editor ? r.u32() : 0;
            uint8_t flags = r.left() >= 1 ? r.u8() : 0; // BUG-0516 (older hosts send none)
            if (flags & kVoteOffer) { // the host evolves now: this guest may follow with its own editor
                if (!editor || (g_vote_world_fix && guest_elsewhere()) || g_mode != modes::kCreature || !g_local ||
                    g_own_editor_due || host_creature_mode()) { // F5: the host evolves for everybody
                    trace::line("vote", "offer '%s' not taken: %s", text.c_str(),
                                g_mode != modes::kCreature || !g_local ? "not playing the Creature stage" : "another world or an editor due");
                    return;
                }
                g_offers_got++;
                g_offer_editor = editor;
                g_offer_until = H->time_seconds() + (left > 0 ? left : kOfferSeconds);
                trace::line("vote", "offer by the host: '%s' editor=%s", text.c_str(), trace::hex(editor).c_str());
                feed(text);
                if (!coop_ui::is_open()) {
                    g_offer_opened_ui = true;
                    coop_ui::open(true);
                }
                g_lobby_dirty = true;
                return;
            }
            // BUG-0420: a guest that plays another world of the host's stage takes no part in the host's vote
            // (no vote screen, no evolution editor of the host's world); a vote it saw start here still ends
            if (g_vote_world_fix && !g_vote.active && guest_elsewhere()) {
                g_vote_elsewhere++;
                if (on)
                    trace::line("vote", "by the host: '%s' not taken, this guest plays another world", text.c_str());
                return;
            }
            bool started = on && !g_vote.active, ended = !on && g_vote.active;
            if (on || (g_vote_drop_editor_fix && ended && has_editor))
                g_vote.editor = editor; // a cancelled vote ends with 0 (BUG-0234)
            // the evolution starts: everybody evolves its own creature in its own editor (own_editor_tick)
            // F5 (BUG-0769): creature.mode host: no own editor, the host's evolution comes as the new species
            // BUG-1421 (tests: coop.vote noavatar <s> makes this end see no avatar)
            bool have_local = g_local && !(ended && H->time_seconds() < g_vote_noavatar_until);
            // BUG-1421 (CORE-2): in Creature without the avatar yet (its landing): on its way too
            bool landing = g_own_editor_no_avatar_fix && g_mode == modes::kCreature && !have_local;
            if (ended && g_vote.editor && g_mode == modes::kCreature && have_local && !(g_vote_world_fix && guest_elsewhere()) &&
                !host_creature_mode()) {
                g_own_editor_due = g_vote.editor;
                g_own_editor_due_at = g_own_editor_due_first = H->time_seconds();
                g_own_editor_arriving = false;
            } else if (ended && g_own_editor_arrival_fix && g_vote.editor == kEditorLandfall &&
                       (landing || (g_mode != modes::kCreature && g_mode != modes::kTribe && g_mode != modes::kCiv &&
                                    g_mode != modes::kSpace)) &&
                       !(g_vote_world_fix && guest_elsewhere()) && !host_creature_mode()) {
                // BUG-1240: the first Creature editor's vote ended while this guest is still on its way to the host's
                // Creature world (Cell, the galaxy menu, the load, BUG-1421: the landing without its avatar yet): its
                // own editor opens when it has arrived
                g_own_editor_due = g_vote.editor;
                g_own_editor_due_at = g_own_editor_due_first = H->time_seconds();
                g_own_editor_arriving = true;
                g_own_editor_arrivals++;
                LOG("own editor: the first Creature editor's vote ended before this guest arrived (%s%s): it opens after the arrival (BUG-1240)",
                    modes::mode_name(g_mode), landing ? ", no avatar yet, BUG-1421" : "");
                feed("evolution: your own editor opens when you have arrived in the Creature stage");
            }
            if (ended && g_vote.editor) // WP-X1 (BUG-1276): the host's end-of-Tribe editors: our own ones after the arrival
                civ_arrival_vote(g_vote.editor, g_mode);
            g_vote.active = on;
            g_vote.left = left;
            g_vote.text = text;
            if (started || ended)
                trace::line("vote", "%s by the host: '%s' editor=%s%s", started ? "started" : "ended", text.c_str(),
                            trace::hex(editor).c_str(),
                            ended && g_own_editor_due ? ": the own editor opens when the stage has settled" : "");
            if (started) {
                g_ready[g_sess.my_id()] = false;
                g_vote_opened_ui = !coop_ui::is_open();
                coop_ui::open(true);
            }
            if (ended && g_vote_opened_ui) {
                coop_ui::open(false);
                g_vote_opened_ui = false;
            }
            return;
        }
        case net::Route::Notice: {
            std::string t = r.str8();
            if (!r.ok())
                return;
            if (g_sess.role() == net::Role::Host)
                notice(t, from); // relay to the other guests
            else {
                feed(t);
                chat_log("* " + t);
            }
            return;
        }
        case net::Route::Role: {
            uint8_t role = r.u8();
            if (r.ok() && role < 4) {
                g_roles[from] = role;
                g_lobby_dirty = true;
            }
            return;
        }
        case net::Route::Ready: {
            uint8_t on = r.u8();
            if (r.ok()) {
                g_ready[from] = on != 0;
                g_lobby_dirty = true;
                trace::line("vote", "player %u ready=%d received (vote %s)", from, (int)on, g_vote.active ? "on" : "off");
            }
            return;
        }
        case net::Route::Lobby: {
            std::vector<net::LobbyEntry> t;
            bool has_roles = false;
            if (net::read_lobby(r, t, has_roles, g_role_fix)) {
                std::map<uint8_t, bool> ready;
                std::map<uint8_t, int> rtt;
                for (auto& e : t) {
                    ready[e.id] = e.ready;
                    rtt[e.id] = e.rtt;
                }
                bool mine = g_ready[g_sess.my_id()];
                g_ready = ready;
                g_ready[g_sess.my_id()] = mine; // our own flag is ours until the host confirms it
                g_lobby_rtt = rtt;
                if (has_roles) {
                    uint8_t my_role = g_roles[g_sess.my_id()];
                    if (g_role_fix)
                        g_roles.clear(); // players that left are gone from the table
                    for (auto& e : t)
                        g_roles[e.id] = e.role;
                    g_roles[g_sess.my_id()] = my_role;
                }
            }
            return;
        }
        case net::Route::Shared: {
            shared::on_message(g_sess, type, from, r);
            return;
        }
        case net::Route::PingMark: { // u8 origin, vec3, u32 mode
            uint8_t origin = r.u8();
            net::Vec3 p;
            p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
            uint32_t mode = r.u32();
            if (!r.ok())
                return;
            if (g_sess.role() == net::Role::Host) { // relay to the other guests
                origin = from;
                net::Writer w;
                w.u8(origin);
                w.f32(p.x); w.f32(p.y); w.f32(p.z); w.u32(mode);
                g_sess.broadcast_reliable(net::MSG_PING_MARK, w, from);
            }
            add_mark(origin, p, mode);
            return;
        }
        case net::Route::Space: {
            spacecoop::on_message(type, from, r);
            return;
        }
        case net::Route::Squadron: {
            squadron::on_message(type, from, r);
            return;
        }
        case net::Route::Civ: {
            civ::on_message(type, from, r);
            return;
        }
        case net::Route::CivWallet: { // BUG-1468: every player's own Civ money (civ.own_money)
            civ_wallet::on_message(from, r);
            return;
        }
        case net::Route::Creation: {
            creations::on_message(type, from, r);
            return;
        }
        case net::Route::SpeciesLine: { // WP-0 (BUG-1270): the species line
            species::on_message(type, from, r);
            return;
        }
        case net::Route::Progress: { // WP-P1 / P2 of 0.7.17 (BUG-1469): own progress
            own_progress::on_message(from, r);
            return;
        }
        case net::Route::Rescue: // WP-R1 of 0.7.17 (BUG-1472): a downed creature and its revive
            revive::on_message(from, r);
            return;
        case net::Route::History: // WP-H1 (BUG-1470): every player's own stage history
            history::on_message(from, r);
            return;
        case net::Route::Tribe: {
            tribe::on_message(type, from, r);
            return;
        }
        case net::Route::Cell:
            cellsync::on_message(type, from, r);
            return;
        case net::Route::Npc:
            npc::on_message(type, from, r);
            return;
        case net::Route::Social:
            social::on_message(type, from, r);
            return;
        case net::Route::Nest: // BUG-0384
            nest::on_message(type, from, r);
            return;
        case net::Route::Fields: // BUG-0419 G2: replicated fields of mirrored objects
            fields::on_message(type, from, r);
            return;
        case net::Route::Wsync: // W1: host-owned world state
            wsync::on_message(type, from, r);
            return;
        case net::Route::Parts: // BUG-0401
            parts::on_message(type, from, r);
            return;
        case net::Route::Wobj: // BUG-0472, BUG-0459
            wobj::on_message(type, from, r);
            return;
        case net::Route::PlayerRule: // P1 of 0.7.7: stun (BUG-0554)
            player_rules::on_message(type, from, r);
            return;
        case net::Route::MirrorFx: // BUG-0600: the host's creature effects, sprints and voices on the mirrors
            mirror_fx::on_message(type, from, r);
            return;
        case net::Route::World: // the host's save (a guest's game only)
            if (from == 0)
                world::on_message(type, r, g_sess.name_of(0));
            return;
        }
    };
    g_sess.cb.old_peer = [](uint8_t, const std::string& name) { // BUG-0219: once per player and join
        // the log line (net: ... older SporeCoop) says what is lost: long chat cut, no species table
        feed_kind(net::compat::warn_text(name, "", g_sess.version), "info"); // B6: the warning of every other version
    };
    g_sess.cb.compat = [](uint8_t, const std::string& text, bool refused) { // B6 (BUG-1137)
        H->log(M, SK_WARN, "compat: %s", text.c_str());
        feed_kind(text, refused ? "leave" : "info");
        chat_log("* " + text);
    };
    g_sess.cb.player_leave = [](uint8_t id, uint8_t reason) {
        trace::line("session", "player %u '%s' left: %s", id, g_remotes.count(id) ? g_remotes[id].name.c_str() : "?",
                    reason == 1 ? "timed out (no packets)" : "left or replaced by its rejoin");
        support::on_player_leave(id, reason); // its link statistics (diag_stats.cpp)
        sreport::on_player_leave(id);         // BUG-0767: its report will not come
        if (g_role_fix)
            g_roles.erase(id); // ids are reused: the next newcomer starts with no role (BUG-0064)
        shared::on_player_leave(id);
        creations::on_player_leave(id); // BUG-0076: the next player of this id is not shown as this one
        npc::on_player_leave(id);       // BUG-0125
        own_progress::on_player_leave(id); // BUG-1469: its live values go (its record stays, by key)
        revive::on_player_leave(id);       // BUG-1472: its window ends without a price
        views::forget(id);              // BUG-0280
        pfind::on_player_leave(id);     // F2: its colour is free again
        if (g_player_forget_fix)
            player_life::forget_all(id); // BUG-0422: every module's state of this id (player_life.h)
        g_give_pick = net::give_pick_after_leave(g_give_pick, id, g_give_pick_reset_fix); // BUG-0234
        if (g_ready.erase(id) | g_lobby_rtt.erase(id)) // BUG-0772 guard: also for a leaver without a Remote
            g_lobby_dirty = true;
        auto it = g_remotes.find(id);
        if (it == g_remotes.end())
            return;
        feed_kind(it->second.name + " left", "leave");
        chat_log("* " + it->second.name + " left");
        g_ready.erase(id);
        g_lobby_dirty = true;
        cellsync::on_player_leave(id);
        despawn(it->second);
        g_remotes.erase(it);
    };
    g_sess.cb.player_state = [](const net::PlayerState& s) {
        if (game::pose_bad(s.pos, s.rot, s.vel)) // BUG-0614: never into the buffer (its velocity is written raw)
            return;
        auto& r = g_remotes[s.id];
        r.id = s.id;
        if (r.name.empty())
            r.name = g_sess.name_of(s.id);
        r.buf.push(s, H->time_seconds());
    };
    g_sess.cb.chat = [](uint8_t id, const std::string& t) {
        feed_kind("<" + g_sess.name_of(id) + "> " + t, "chat"); // never guessed from the words (BUG-0129)
        chat_log(g_sess.name_of(id) + ": " + t, pfind::color_of(id)); // F2: in the writer's colour
    };
}

// ---------------- actions (commands and UI) ----------------
uint16_t default_port() { return (uint16_t)(net::kDefaultPort + H->instance_id() - 1); }

void reset_lobby()
{
    if (g_role_fix)
        g_roles.clear(); // a new session starts with no roles (BUG-0064)
    g_ready.clear();
    g_lobby_rtt.clear();
    g_chat.clear();
    g_chat_col.clear();
}

// a host/join while a session is open: Session::host/join close the old one without cleanup
void end_open_session()
{
    if (g_sess.role() != net::Role::None) {
        g_sess.leave("restart");
        end_session(EndPath::Restart, true);
    }
}

bool do_host(uint16_t port, std::string& err)
{
    end_open_session();
    reset_lobby();
    if (!g_sess.host(port, g_name, err))
        return false;
    support::on_session_start();
    pfind::on_session_start(); // F2: the host takes its colour
    feed("hosting on port " + std::to_string(g_sess.port()));
    chat_log("* hosting on UDP port " + std::to_string(g_sess.port()));
    g_lobby_dirty = true;
    return true;
}

bool do_join(const std::string& addr, std::string& err)
{
    end_open_session();
    reset_lobby();
    g_sess.rejoin_token = rejoin::token_for_join(H->instance_id(), addr, g_name); // BUG-0235, BUG-1001
    if (!g_sess.join(addr, g_name, err))
        return false;
    { // the chat log goes into problem reports: no public address there
        net::Endpoint ep;
        std::string e, name;
        bool numeric = net::Endpoint::parse_numeric(addr, net::kDefaultPort, ep, e, &name);
        chat_log("* joining " + (numeric ? ep.masked() : std::string("a host name")));
    }
    return true;
}

void do_leave()
{
    g_sess.leave("user");
    // a clean leave: the next join is a new player (BUG-0235), except a join back to this host under this name, which
    // keeps the token: the host takes it back while it takes no new players (BUG-1001, off with net.join_seen)
    rejoin::clear(net::g_fix.join_seen);
    end_session(EndPath::Leave, cellsync::voluntary_leave_cleanup_enabled());
    reset_lobby();
}

const char* role_name(uint8_t r)
{
    return r == 1 ? "military" : r == 2 ? "economic" : r == 3 ? "religious" : "none";
}

void set_role(uint8_t role)
{
    g_roles[g_sess.my_id()] = role;
    if (g_sess.role() == net::Role::Client) {
        net::Writer w;
        w.u8(role);
        g_sess.send_reliable_to(0, net::MSG_ROLE, w);
    } else
        g_lobby_dirty = true;
    notice(g_name + " governs as " + std::string(role_name(role)) + " now", -1);
}

void set_ready(bool on)
{
    if (g_offer_editor && !g_vote.active && g_sess.role() == net::Role::Client) { // BUG-0516: "Evolve now"
        offer_take();
        return;
    }
    trace::line("vote", "own ready=%d set (vote %s)", (int)on, g_vote.active ? "on" : "off");
    g_ready[g_sess.my_id()] = on;
    if (g_sess.role() == net::Role::Client) {
        net::Writer w;
        w.u8(on ? 1 : 0);
        g_sess.send_reliable_to(0, net::MSG_READY, w);
    } else
        g_lobby_dirty = true;
}

int share_all()
{
    int n = 0;
    if (!(g_adapter && g_local)) { // not playing: no world yet, send it once the host plays
        for (auto& [id, r] : g_remotes)
            if (!r.echo) {
                r.needs_world = true;
                r.cine_wait_since = 0; // BUG-0232
            }
        feed("the world goes to everyone when you load or start a game");
        return 0;
    }
    for (auto& p : g_sess.players()) {
        std::string err;
        if (p.id != 0 && world::share_with(p.id, err, g_save_on_share && g_adapter))
            n++;
        else if (p.id != 0)
            feed("cannot share the world: " + err);
    }
    return n;
}

} // namespace coop_core
