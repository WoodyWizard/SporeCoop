#include "npc.h"
#include "modes.h"
#include "tribe.h"
#include "teamplay.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>

#include "sporekit_game.hpp"

#include "creations.h"
#include "driver.h"
#include "game.h"
#include "net/validate.h"
#include "npc_ids.h"
#include "world.h"
#include "sporekit_game.hpp"
#include "fixes.h"

namespace npc {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_enabled = true;

static constexpr float kRelevance = 110.f;  // m around a guest
static constexpr float kHysteresis = 20.f;  // keep until kRelevance + this
static constexpr double kStateInterval = 0.1;
static constexpr float kMaxHit = 10000.f; // a guest's reported hit (MSG_DAMAGE_REQ) is clamped to 0..this
static constexpr float kRtsRelevance = 250.f; // tribe stage: m around the player tribe

// NPC creatures are shared in the creature stage and, around the player tribe, in the tribe stage
static bool npc_stage() { return game::in_creature_stage() || game::mode_id() == 0x1654C02; }
static bool rts_stage() { return game::mode_id() == 0x1654C02; }

// ------------------------------------------------------------------ host side
struct HostNpc {
    uint16_t id;
    double last_seen;
    uint32_t species = 0; // what the object was when it got the id (BUG-0044)
    bool dead = false;
};
static std::unordered_map<void*, HostNpc> g_host_npcs;  // creature -> id
static std::unordered_map<uint16_t, void*> g_host_by_id;
static std::map<uint8_t, std::set<uint16_t>> g_known;  // guest -> net ids it mirrors
static uint16_t g_next_id = 1;
static double g_last_state = 0;
static double g_frozen_since = 0;                       // host: no creature updates since then
static std::unordered_set<void*> g_seen_now;            // creatures alive this frame
static bool g_reuse_fix = true; // coop.npc reusefix off: a reused creature object keeps its id (BUG-0044)
static bool g_id_fix = true;    // coop.npc idfix off: ids wrap over ids in use (BUG-0023)
// BUG-0169: a proxy that stays more than 2.5 m off for 3 s is put in place whatever the host's
// NPC reports as its speed (coop.npc stallfix off: the old driving)
static bool g_stall_fix = true;
COOP_FIX("npc.reuse", "BUG-0044", g_reuse_fix, "creature: a reused creature object gets a new NPC id");
COOP_FIX("npc.id", "BUG-0023", g_id_fix, "creature: NPC ids never wrap over ids in use");
COOP_FIX("npc.stall", "BUG-0169", g_stall_fix, "creature: a proxy 2.5 m off for 3 s is put in place");
// 0.7.2.x fixes kept for the tests (off: the code before them)
static bool g_stale_fix = true, g_world_despawn_fix = true, g_death_fix = true;
COOP_FIX("npc.stale_proxy", "BUG-0009", g_stale_fix, "creature: a guest drops a proxy the host stopped sending states for");
COOP_FIX("npc.world_despawn", "BUG-0009", g_world_despawn_fix, "creature: the host despawns its NPCs of a world it left at the guests");
COOP_FIX("npc.play_death", "BUG-0015", g_death_fix, "creature: a guest's proxy of a killed NPC plays the death");
void set_stall_fix(bool on) { g_stall_fix = on; }
// a killed proxy plays the game's death (BUG-0015); coop.npc deathfix off (= npc.play_death off): it
// does not (tests: seeds the 0-health mirror that stands alive, BUG-0148)
void set_death_fix(bool on) { g_death_fix = on; }
static uint32_t g_reused = 0;   // host: creature objects that became another NPC (new id)
void set_reuse_fix(bool on) { g_reuse_fix = on; }
void set_id_fix(bool on) { g_id_fix = on; }
void debug_next_id(uint16_t id) { g_next_id = id; }

// ------------------------------------------------------------------ guest side
struct Proxy {
    void* c = nullptr;
    game::Key key{};
    uint8_t flags = 0;
    float health = 0, max_health = 0;
    bool killed = false;
    int fail = 0; // 1 species, 2 herd, 3 create
    double last_rx = 0; // local time of its last spawn / state from the host
    net::SnapshotBuffer buf;
    drive::State drv;
};
static std::map<uint16_t, Proxy> g_proxies;
static std::unordered_map<void*, uint16_t> g_proxy_of;
static std::map<uint64_t, void*> g_herds; // species instance/group -> herd for proxies
// guest: the game's own herd of each species (seen on the local NPCs the update filter removes).
// Proxies go into it: Create then gives them the herd's archetype tuning (+0xE84) like any NPC of
// the nest (social games, epic flag, DNA worth need it; docs/re/creature.md 8), and the species
// still has its creatures where the game counts them
static std::map<uint64_t, void*> g_local_herds;
static uint32_t g_proxies_in_nest = 0;
static uint64_t herd_key(const game::Key& k) { return ((uint64_t)k.group << 32) | k.instance; }
static std::vector<void*> g_kill;          // local NPCs to remove (guest)
static uint32_t g_proxy_lost = 0;
static double g_last_states_rx = -100; // guest: the last NPC state batch from the host
static uint32_t g_stale_dropped = 0;   // guest: proxies the host stopped talking about (ghosts)
static uint32_t g_deaths_played = 0; // guest: proxy deaths replayed (Die + animation)
static uint32_t g_removed_local = 0, g_hits_sent = 0, g_hits_applied = 0, g_damage_in = 0, g_kills_credited = 0;

static bool g_host_away = false; // guest: the host is in the main menu, our own game runs the NPCs
static uint32_t g_away_proxies = 0;
static uint32_t g_local_alive = 0; // guest: own NPCs that ran Update last frame (host away: the game's)
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

bool is_proxy(void* c) { return g_proxy_of.count(c) != 0; }
static bool g_strays_on = true; // tests: remove_saved_strays on/off
COOP_FIX("npc.strays", "BUG-0008", g_strays_on, "creature: creatures without tuning in herds like ours (saved puppets) are removed");
void set_strays(bool on) { g_strays_on = on; }
void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }

// called from inside the game's creature update loop: keep it cheap
static bool update_filter(void* c)
{
    if (!g_enabled || !is_guest() || !npc_stage())
        return true;
    if (game::is_puppet(c) || c == game::avatar())
        return true;
    if (g_host_away) // the host left the world: the guest plays on alone with its own NPCs
        return true;
    if (void* h = game::herd_of(c))
        if (h != game::avatar_herd())
            g_local_herds[herd_key(game::species_key(c))] = h;
    g_kill.push_back(c); // a local NPC: the host owns NPCs
    return false;
}

// The creature AI knows one player: NPCTickAI and its behaviour tree notice, chase, attack and
// flee only the avatar (cGameNounManager +0x54). Aggressive NPCs walked past a partner's puppet and
// went for the host (player report 2026-10-25: "aggressive enemies didn't target my friend",
// docs/re/creature.md 14). On the host an NPC nearer to a partner than to the host's avatar ticks
// with that partner's puppet as the player; its attacks on the puppet go to the partner through the
// damage filter (MSG_DAMAGE), like any NPC hit on a puppet.
static bool g_partner_targets = true;
COOP_FIX("npc.partners", "BUG-0016", g_partner_targets, "creature: the host's NPCs notice, chase and attack a partner's puppet");
static constexpr float kPartnerAiRange = 80.f; // m: NPCs farther from a partner than this ignore it
static std::unordered_map<void*, void*> g_ai_target; // host: NPC -> the puppet it treats as the player
static uint32_t g_ai_partner_picks = 0;
void set_partner_targets(bool on) { g_partner_targets = on; }

static void* ai_target(void* c)
{
    if (!g_enabled || !g_partner_targets || !is_host() || !game::in_creature_stage())
        return nullptr;
    void* av = game::avatar();
    auto prev = g_ai_target.find(c);
    void* was = prev != g_ai_target.end() ? prev->second : nullptr;
    void* pick = nullptr;
    // the pack and the avatar's family follow and defend the host's avatar
    if (av && !game::is_dead(c) && !game::pack_member(c) && game::herd_of(c) != game::avatar_herd()) {
        net::Vec3 np = game::position(c);
        // an NPC far from every partner keeps the avatar (its notice and chase ranges are tens of m)
        float best = std::min(net::dist(np, game::position(av)), kPartnerAiRange);
        for (auto& p : C.sess->players()) {
            if (p.id == 0)
                continue;
            void* pup = C.puppet_of ? C.puppet_of(p.id) : nullptr;
            net::Vec3 gp;
            if (!pup || !game::is_puppet(pup) || !game::object_alive(pup) || !C.player_pos(p.id, gp) ||
                (C.player_dead && C.player_dead(p.id)))
                continue;
            // 3 m either way: an NPC between the two players does not switch every frame
            float d = net::dist(np, game::position(pup)) + (pup == was ? -3.f : 3.f);
            if (d < best) {
                best = d;
                pick = pup;
            }
        }
    }
    if (pick) {
        if (pick != was)
            g_ai_partner_picks++;
        g_ai_target[c] = pick;
    } else if (prev != g_ai_target.end())
        g_ai_target.erase(prev);
    return pick;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    game::set_update_filter(update_filter);
    game::set_ai_target_picker(ai_target);
}

static net::NpcState host_state(uint16_t id, void* c)
{
    net::NpcState s;
    s.net_id = id;
    s.pos = game::position(c);
    s.rot = game::orientation(c);
    s.vel = game::velocity(c);
    s.health = game::health(c);
    s.flags = (game::is_dead(c) ? net::NF_DEAD : 0) | (game::impressed(c) ? net::NF_IMPRESSED : 0);
    return s;
}

static void send_spawn(uint8_t guest, uint16_t id, void* c)
{
    net::NpcSpawn sp;
    sp.net_id = id;
    game::Key k = game::species_key(c);
    // one of our own creations (another creation of the guest may have its key): its records first
    creations::host_species_out(guest, {k.instance, k.type, k.group});
    sp.key[0] = k.instance;
    sp.key[1] = k.type;
    sp.key[2] = k.group;
    sp.scale = game::scale(c);
    void* av = game::avatar();
    sp.flags = (game::is_dead(c) ? net::NF_DEAD : 0) |
               (av && game::herd_of(c) && game::herd_of(c) == game::herd_of(av) ? net::NF_FAMILY : 0);
    sp.pos = game::position(c);
    sp.rot = game::orientation(c);
    sp.health = game::health(c);
    sp.max_health = game::max_health(c);
    net::Writer w;
    sp.write(w);
    C.sess->send_reliable_to(guest, net::MSG_NPC_SPAWN, w);
}

static void send_despawn(uint8_t guest, uint16_t id)
{
    net::Writer w;
    w.u16(id);
    w.u8(0);
    C.sess->send_reliable_to(guest, net::MSG_NPC_DESPAWN, w);
}

// host: net id `id` of creature object `c` is over: the guests that mirror it drop it
static void retire(uint16_t id, void* c)
{
    for (auto& [g, ks] : g_known)
        if (ks.erase(id))
            send_despawn(g, id);
    auto b = g_host_by_id.find(id);
    if (b != g_host_by_id.end() && b->second == c)
        g_host_by_id.erase(b);
    g_ai_target.erase(c);
    teamplay::forget_target(c);
}

static void host_tick(double now, const std::vector<void*>& creatures)
{
    void* av = game::avatar();
    net::Vec3 center;
    bool rts = rts_stage() && C.stage_center && C.stage_center(center);
    if (!av && !rts)
        return;
    // no creature updated this frame: the simulation stands still (pause menu, the death caption
    // waiting for OK, a cinematic). Not a reason to forget them: the guests lost every NPC after 2 s
    // and got them again when the host went on (play_creature.py 2026-10-23)
    if (creatures.empty()) {
        g_frozen_since = g_frozen_since > 0 ? g_frozen_since : now;
        return;
    }
    if (g_frozen_since > 0) { // the frozen time does not count as unseen
        double gap = now - g_frozen_since;
        for (auto& [c, hn] : g_host_npcs)
            hn.last_seen += gap;
        g_frozen_since = 0;
    }
    g_seen_now.clear();
    for (void* c : creatures) {
        if (c == av || game::is_puppet(c) || game::is_destroyed(c))
            continue;
        uint32_t species = game::species_instance(c);
        bool dead = game::is_dead(c);
        auto it = g_host_npcs.find(c);
        if (it != g_host_npcs.end() && g_reuse_fix &&
            npc_object_reused(it->second.species, it->second.dead, species, dead)) {
            // the game freed this NPC and built another one in the same object (BUG-0044): the
            // guests drop the old one, the new one gets a new id and a spawn with its species
            retire(it->second.id, c);
            g_host_npcs.erase(it);
            it = g_host_npcs.end();
            g_reused++;
        }
        if (it == g_host_npcs.end()) {
            uint16_t id = alloc_npc_id(
                g_next_id, [](uint16_t i) { return g_host_by_id.count(i) != 0; }, g_id_fix);
            if (!id) // 65534 NPCs at once: not in this world
                continue;
            g_host_npcs[c] = {id, now, species, dead};
            g_host_by_id[id] = c;
        } else {
            it->second.last_seen = now;
            it->second.dead = dead;
        }
        g_seen_now.insert(c);
    }
    // forget creatures that stopped updating (destroyed / unloaded)
    for (auto it = g_host_npcs.begin(); it != g_host_npcs.end();) {
        if (now - it->second.last_seen > 2.0) {
            retire(it->second.id, it->first);
            it = g_host_npcs.erase(it);
        } else
            ++it;
    }
    bool send_states = now - g_last_state >= kStateInterval;
    if (send_states)
        g_last_state = now;
    for (auto& p : C.sess->players()) {
        if (p.id == 0)
            continue;
        auto& known = g_known[p.id];
        net::Vec3 gp;
        float relevance = kRelevance;
        if (C.in_place && !C.in_place(p.id)) { // another world (BUG-0130): its proxies go, sent again later
            for (uint16_t id : known)
                send_despawn(p.id, id);
            known.clear();
            continue;
        }
        if (rts) {
            gp = center;
            relevance = kRtsRelevance;
        } else if (!C.player_pos(p.id, gp)) {
            for (uint16_t id : known)
                send_despawn(p.id, id);
            known.clear();
            continue;
        }
        std::vector<net::NpcState> batch;
        for (void* c : g_seen_now) {
            uint16_t id = g_host_npcs[c].id;
            float d = net::dist(game::position(c), gp);
            bool k = known.count(id) != 0;
            if (!k && d < relevance) {
                send_spawn(p.id, id, c);
                known.insert(id);
                k = true;
            } else if (k && d > relevance + kHysteresis) {
                send_despawn(p.id, id);
                known.erase(id);
                k = false;
            }
            if (k && send_states)
                batch.push_back(host_state(id, c));
        }
        // unreliable batches that fit in one packet
        size_t per = (net::kMaxPacket - 64) / net::NpcState::kSize;
        for (size_t i = 0; i < batch.size(); i += per) {
            net::Writer w;
            w.u32(net::now_ms());
            size_t n = std::min(per, batch.size() - i);
            w.u8((uint8_t)n);
            for (size_t j = 0; j < n; j++)
                batch[i + j].write(w);
            C.sess->send_unreliable_to(p.id, net::MSG_NPC_STATE, w);
        }
    }
    for (auto it = g_known.begin(); it != g_known.end();) { // guests that left
        bool present = false;
        for (auto& p : C.sess->players())
            if (p.id == it->first)
                present = true;
        it = present ? std::next(it) : g_known.erase(it);
    }
}

static void* proxy_herd(const game::Key& k, uint8_t flags, const net::Vec3& pos, void* species)
{
    void* species_nest = species;
    if (flags & net::NF_FAMILY)
        if (void* h = game::avatar_herd())
            return h;
    // the nest of the species in this world (from the save): by the host's key. A creation of the
    // host's own has a key of its own here (creations::local_species), its nests the host's key
    creations::Key hkey = creations::host_species({k.instance, k.type, k.group});
    if (hkey.instance != k.instance)
        if (void* sp = game::species_by_key({hkey.instance, hkey.type, hkey.group}))
            species_nest = sp;
    uint64_t hk = herd_key({hkey.instance, hkey.type, hkey.group});
    auto lh = g_local_herds.find(hk);
    if (lh != g_local_herds.end()) {
        if (game::object_alive(lh->second)) {
            g_proxies_in_nest++;
            return lh->second;
        }
        g_local_herds.erase(lh);
    }
    // not seen on a local NPC yet (proxies made before the game's NPCs ran an update): the nest
    // herd of the species among the game objects
    struct Find {
        void* species;
        void* found;
    } f{species_nest, nullptr};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* f = (Find*)u;
            if (noun != game::kHerdNoun || !game::object_alive(o) || game::herd_species(o) != f->species ||
                !game::herd_nest(o))
                return true;
            f->found = o;
            return false;
        },
        &f);
    if (f.found) {
        g_local_herds[hk] = f.found;
        g_proxies_in_nest++;
        return f.found;
    }
    auto it = g_herds.find(hk);
    if (it != g_herds.end())
        return it->second;
    void* h = game::create_herd(pos, species);
    if (h)
        g_herds[hk] = h;
    return h;
}

static void destroy_proxy(uint16_t id)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end())
        return;
    if (it->second.c) {
        g_proxy_of.erase(it->second.c);
        if (game::is_destroyed(it->second.c)) // the game removed it already: only forget it
            game::puppet_unregister(it->second.c);
        else
            game::destroy_creature(it->second.c);
    }
    g_proxies.erase(it);
}

static void guest_tick(double now)
{
    void* av = game::avatar();
    // remove local NPCs collected by the update filter
    std::sort(g_kill.begin(), g_kill.end());
    g_kill.erase(std::unique(g_kill.begin(), g_kill.end()), g_kill.end());
    for (void* c : g_kill)
        if (c != av && !game::is_puppet(c) && !game::is_destroyed(c) && !game::destroy_pending(c)) {
            game::destroy_creature(c);
            g_removed_local++;
        }
    g_kill.clear();
    if (!av && !rts_stage()) // the tribe stage has no avatar
        return;
    // the first frames of a world: the update filter meets the game's own NPCs and learns their
    // herds (g_local_herds) before the proxies are made
    // (with the world counter: the avatar of a reloaded world may have the old address, BUG-0119)
    static void* s_av = nullptr;
    static uint32_t s_epoch = 0;
    static double s_av_t0 = 0;
    uint32_t epoch = game::world_epoch();
    if (av != s_av || epoch != s_epoch) {
        s_av = av;
        s_epoch = epoch;
        s_av_t0 = now;
    }
    if (now - s_av_t0 < 1.0)
        return;
    // A proxy the host stopped sending states for while it sends them for the others is not an
    // NPC of the host any more (its id belongs to a world the host has left: before 2026-10-25 the
    // host renumbered its NPCs after its editor or a load without a despawn, and the guest kept
    // the old ones for good: a second, living copy of every NPC, a kill of the host's left it
    // standing). A host that stands still (pause menu) sends no states at all: nothing is dropped.
    if (g_stale_fix && now - g_last_states_rx < 0.5) {
        std::vector<uint16_t> stale;
        for (auto& [id, px] : g_proxies)
            if (now - px.last_rx > 5.0)
                stale.push_back(id);
        for (uint16_t id : stale) {
            destroy_proxy(id);
            g_stale_dropped++;
        }
    }
    for (auto& [id, px] : g_proxies) {
        net::PlayerState s;
        if (!px.buf.sample(now, s))
            continue;
        // the guest's own game destroyed the proxy (its tribe killed it, a cleanup): forget the
        // object at once (it is freed later; it was driven every frame after that) and make a new
        // one unless the host says the NPC is dead. NPCs vanished from guests this way in a 4-player
        // tribe run (coverage 26-48 % for 4 minutes).
        if (px.c && game::is_destroyed(px.c)) {
            g_proxy_of.erase(px.c);
            game::puppet_unregister(px.c);
            px.c = nullptr;
            px.drv = drive::State{};
            g_proxy_lost++;
        }
        if (!px.c && (s.flags & net::NF_DEAD))
            continue;
        if (!px.c) {
            void* species = game::species_by_key(px.key);
            void* herd = species ? proxy_herd(px.key, px.flags, s.pos, species) : nullptr;
            int fail = !species ? 1 : !herd ? 2 : 0;
            if (!fail) {
                px.c = game::spawn_creature(s.pos, species, herd);
                if (!px.c)
                    fail = 3;
            }
            if (fail) {
                // a species of the host's world this game lacks (the host's own creations and its
                // Sporepedia downloads populate its planet): the host sends it, the next try works
                if (fail == 1)
                    creations::want_species({px.key.instance, px.key.type, px.key.group});
                if (px.fail != fail)
                    H->log(M, SK_WARN, "npc %u: proxy spawn failed (%s) key=%08X/%08X/%08X", id,
                           fail == 1 ? "unknown species" : fail == 2 ? "no herd" : "Create failed", px.key.instance,
                           px.key.type, px.key.group);
                px.fail = fail;
                continue;
            }
            game::puppet_register(px.c); // no local AI
            g_proxy_of[px.c] = id;
            game::teleport(px.c, s.pos, s.rot);
        }
        if (!px.killed)
            drive::step(px.c, s.pos, s.rot, s.vel, px.drv, now, 0, true, 8.f, g_stall_fix);
        else { // a corpse lies where the host has it: the guest's dead proxy shot off into the sky
               // (~2.5 km/s away from the planet; driven, it stayed ~80 m up: play_tribe.py)
            *(net::Vec3*)((uint8_t*)px.c + game::kVelOff) = {0, 0, 0};
            game::teleport(px.c, s.pos, s.rot);
        }
        if ((s.flags & net::NF_DEAD) && !px.killed) {
            px.killed = true;
            // play the death locally with a just-lethal hit: the knockback grows with the damage
            // (1e6 threw the corpse ~150 m up, it hung 15-80 m away from the host's corpse for
            // the rest of its life: play_creature.py 2026-10-23)
            game::apply_damage(px.c, std::max(game::health(px.c), 0.f) + 0.5f, 0, nullptr);
            // the death itself is the behaviour tree's (0xD85B80): a proxy has no AI and stood on
            // with 0 health, alive for the game (not edible, "still alive" for the guest; player
            // report 2026-10-25, docs/re/creature.md 14)
            if (g_death_fix) {
                game::play_death(px.c);
                g_deaths_played++;
            }
        } else if (!px.killed && s.health > 0)
            game::set_health(px.c, s.health);
        if (!px.killed) // the host's NPC was impressed (by anybody): the proxy shows it too
            game::set_impressed(px.c, (s.flags & net::NF_IMPRESSED) != 0);
    }
}

// Our creatures (the partner's puppet, NPC proxies) live in herds of our own (CreateHerd(.., 0, 0, 0,
// 0): no archetype, no nest, not the avatar's) and have no archetype tuning (+0xE84). A save made
// while they existed keeps them: loaded again (after a restart of the game) such a puppet is a
// plain game creature of the partner's species, without tuning and with its AI on, and the first
// attack on it read (+0xE84)->+0x32C in CreatureStageEventHandler (0xD48634: the host crashed 3
// times after killing "these guys", 2026-10-25; docs/re/creature.md 4). Every such creature that is
// not one of ours now is removed (also from saves that already hold them). Herds of the game always
// have an archetype (137 herds on Scalinus, none without), so a creature of the game is never one.
static uint32_t g_strays_removed = 0;
static double g_strays_at = 0;

static bool own_kind_herd(void* h)
{
    return h && !IsBadReadPtr(h, 0x21C) && game::object_alive(h) && game::noun_id(h) == game::kHerdNoun &&
           *(uint32_t*)((uint8_t*)h + 0x88) == 0 && !game::herd_nest(h) && !*(uint8_t*)((uint8_t*)h + 0x84);
}

static void remove_saved_strays(double now)
{
    if (!g_strays_on || now - g_strays_at < 1.0)
        return;
    g_strays_at = now;
    struct Acc {
        std::vector<void*> v;
        void* av;
    } a{{}, game::avatar()};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* a = (Acc*)u;
            if (noun != 0x18EB45Eu || o == a->av || !game::object_alive(o) || IsBadReadPtr(o, 0x1680) ||
                *(void**)((uint8_t*)o + 0xE84) || !game::species_profile(o) || game::is_puppet(o) || is_proxy(o) ||
                game::destroy_pending(o))
                return true;
            if (own_kind_herd(game::herd_of(o)))
                a->v.push_back(o);
            return true;
        },
        &a);
    for (void* c : a.v) {
        H->log(M, SK_INFO, "removed a creature of species %08X without tuning in a herd like ours (from a save made "
               "while a partner's puppet existed)", game::species_key(c).instance);
        game::destroy_creature(c);
        g_strays_removed++;
    }
}

// A respawn (0xD40FA0) or a hatch puts the avatar back into the nest as a hatchling whose behaviour
// tree waits (tree 0x9413EDEB) for a family member of the nest: that NPC's tree walks over, plays the
// greeting with it and makes it grow up (BT node 0xD64A30, state 9: 0xC04F30(1), 0xD52DF0(1)). A
// guest has no family of its own (the update filter removes its local NPCs, the host's mirrored NPCs
// have no AI), so after a death the guest stayed a hatchling for good: half the health, no control,
// the hatch pose with the egg shell on both screens (player report 2026-10-25, docs/re/creature.md
// 13). The guest does what the greeting does once its hatchling has waited a moment; the host only
// when its own family did not come for much longer than it takes (~13 s), e.g. when the greeter
// picked was a partner's puppet (puppets run no AI either).
static bool g_hatch_on = true; // tests: hatch help on/off
COOP_FIX("npc.hatch", "BUG-0014", g_hatch_on, "creature: a waiting hatchling grows up without its family's greeting");
static double g_hatch_wait = 0, g_hatch_last = 0;
static uint32_t g_hatch_helped = 0;
void set_hatch_help(bool on) { g_hatch_on = on; }
constexpr double kGuestHatchWait = 4.0, kHostHatchWait = 40.0;

static void hatch_tick(double now, bool simulating)
{
    double dt = now - g_hatch_last;
    g_hatch_last = now;
    void* av = game::avatar();
    if (!g_hatch_on || !C.sess || !C.sess->connected() || !game::in_creature_stage() || !av || game::is_dead(av) ||
        !game::hatchling_waiting(av)) {
        g_hatch_wait = 0;
        return;
    }
    if (!simulating || world::cinematic_playing() || dt > 1.0) // paused, a cinematic, a caption, a long frame
        return;
    g_hatch_wait += dt;
    bool no_family = is_guest() && g_enabled && !g_host_away; // the guest's own NPCs are removed
    if (g_hatch_wait < (no_family ? kGuestHatchWait : kHostHatchWait))
        return;
    g_hatch_wait = 0;
    if (game::grow_up(av)) {
        g_hatch_helped++;
        H->log(M, SK_INFO, "creature: the hatchling waited for its family %s: it grows up now",
               no_family ? "(a guest has none of its own)" : "too long");
    }
}

static void tick_frame(double now)
{
    const auto& creatures = game::creatures_updated(); // must run every frame
    game::swap_guard_tick();                            // outside every NPC tick here
    game::destroy_deferred_tick();                      // creatures removed while a cinematic played
    if (game::in_creature_stage())
        remove_saved_strays(now);
    hatch_tick(now, !creatures.empty());
    // creature stage only: adventure creatures are scripted by the adventure on every machine
    if (!g_enabled || !C.sess || !npc_stage())
        return;
    if (is_host())
        host_tick(now, creatures);
    else if (is_guest()) {
        uint32_t n = 0;
        for (void* c : creatures)
            n += !game::is_puppet(c) && c != game::avatar();
        g_local_alive = n;
        bool away = C.host_away && C.host_away();
        if (away && !g_host_away) { // the host's NPCs stand frozen and nothing removes them: go
            std::vector<uint16_t> ids;
            for (auto& [id, px] : g_proxies)
                ids.push_back(id);
            g_away_proxies += (uint32_t)ids.size();
            for (uint16_t id : ids)
                destroy_proxy(id);
            g_herds.clear();
        }
        g_host_away = away;
        if (!away)
            guest_tick(now);
    }
}

// BUG-0082: g_kill holds pointers of this frame's update only. Every frame that does not reach
// guest_tick (the frame the host goes away, NPC sync off, another stage) drops them: they used to
// wait, possibly minutes, until the host was back, and guest_tick then destroyed objects the game
// had freed or reused meanwhile. `coop.npc killfix off` keeps the old behaviour (tests).
static bool g_kill_clear = true;
COOP_FIX_UNSAFE("npc.kill", "BUG-0082", g_kill_clear, "creature: the guest's kill list is cleared every frame (coop.npc killfix)");
void set_kill_clear(bool on) { g_kill_clear = on; }

void tick(double now)
{
    tick_frame(now);
    if (g_kill_clear)
        g_kill.clear(); // guest_tick empties it itself; anything left here is from a skipped frame
}

void debug_kill_hold()
{
    // tests: what the update filter collects on the frame the host goes away (the avatar is a safe
    // stand-in: guest_tick never destroys it)
    if (void* av = game::avatar())
        g_kill.push_back(av);
}

void on_world_change()
{
    // the host's NPCs of the old world are gone and the new ones get new ids: the guests drop the
    // old ones (they mirrored them for good before 2026-10-25: a living copy of every NPC next to
    // the new one after each editor or load of the host)
    if (is_host() && g_world_despawn_fix)
        for (auto& [g, ks] : g_known)
            for (uint16_t id : ks)
                send_despawn(g, id);
    for (auto& [id, px] : g_proxies) {
        if (px.c)
            game::puppet_unregister(px.c);
        px.c = nullptr;
        px.killed = false;
        px.drv = drive::State{};
    }
    g_proxy_of.clear();
    g_herds.clear();
    g_local_herds.clear();
    g_kill.clear();
    g_host_npcs.clear();
    g_host_by_id.clear();
    g_ai_target.clear();
    teamplay::reset();
    for (auto& [g, ks] : g_known)
        ks.clear(); // host re-sends spawns
}

// BUG-0125: host_tick forgets what a guest mirrors only while the simulation runs; a guest that
// left and joined again (the same id) while the host was paused kept the old set and got no spawns
static bool g_known_fix = true;
COOP_FIX("npc.known", "BUG-0125", g_known_fix, "creature: a guest that left forgets its known NPCs while the host is paused too");
void set_known_fix(bool on) { g_known_fix = on; }
void on_player_leave(uint8_t id)
{
    if (g_known_fix)
        g_known.erase(id);
}

void on_session_end()
{
    std::vector<uint16_t> ids;
    for (auto& [id, px] : g_proxies)
        ids.push_back(id);
    for (uint16_t id : ids)
        destroy_proxy(id);
    g_herds.clear();
    g_local_herds.clear();
    g_host_npcs.clear();
    g_host_by_id.clear();
    g_known.clear();
    g_kill.clear();
    g_ai_target.clear();
    teamplay::reset();
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    double now = H->time_seconds();
    switch (type) {
    case net::MSG_NPC_SPAWN: {
        net::NpcSpawn sp;
        if (!is_guest() || !sp.read(r))
            return;
        destroy_proxy(sp.net_id);
        Proxy& px = g_proxies[sp.net_id];
        // the host's own creations have keys of their own here (creations::local_species)
        creations::Key lk = creations::local_species({sp.key[0], sp.key[1], sp.key[2]});
        px.key = {lk.instance, lk.type, lk.group};
        px.flags = sp.flags;
        px.health = sp.health;
        px.max_health = sp.max_health;
        net::PlayerState s;
        // the spawn carries no host time: older than any state. The guest's own clock here made
        // every state older than this snapshot whenever the guest's clock was ahead of the host's
        // (another PC, another Wine bottle): they were all dropped and the NPC stood still
        s.time_ms = 0;
        s.pos = sp.pos;
        s.rot = sp.rot;
        s.flags = sp.flags;
        s.health = sp.health;
        px.buf.delay = 0.15;
        px.buf.push(s, now);
        px.last_rx = now;
        break;
    }
    case net::MSG_NPC_DESPAWN: {
        if (!is_guest())
            return;
        uint16_t id = r.u16();
        destroy_proxy(id);
        break;
    }
    case net::MSG_NPC_STATE: {
        if (!is_guest())
            return;
        uint32_t t = r.u32();
        uint8_t n = r.u8();
        bool whole = r.ok() && n > 0;
        for (int i = 0; i < n && r.ok(); i++) {
            net::NpcState st;
            if (!st.read(r)) {
                whole = false;
                break;
            }
            auto it = g_proxies.find(st.net_id);
            if (it == g_proxies.end())
                continue;
            net::PlayerState s;
            s.time_ms = t;
            s.pos = st.pos;
            s.rot = st.rot;
            s.vel = st.vel;
            s.health = st.health;
            s.flags = st.flags;
            it->second.buf.push(s, now);
            it->second.last_rx = now;
        }
        if (whole) // the host sends states (an empty or cut batch says nothing about stale proxies)
            g_last_states_rx = now;
        break;
    }
    case net::MSG_DAMAGE_REQ: { // host: a guest hit one of our NPCs
        net::DamageReq q;
        if (!is_host() || !q.read(r))
            return;
        auto it = g_host_by_id.find(q.net_id);
        if (it == g_host_by_id.end() || !g_seen_now.count(it->second))
            return;
        void* npc = it->second;
        void* attacker = C.puppet_of(from);
        float dmg = teamplay::on_player_hit(npc, from, net::sane_damage(q.damage, kMaxHit));
        // kill credit: the game pays DNA, goal progress and the kill popup only for a kill by the
        // player's own avatar (TakeDamage 0xC07A78: attacker spatial vt 0x58, docs/re/creature.md 2).
        // A guest's puppet is not it, so a guest's kill paid nothing. The pack shares its progress:
        // the lethal hit lands as the host avatar's (one hit, the target dies, no aggro to move).
        void* av = game::avatar();
        if (av && !game::is_dead(npc) && game::health(npc) > 0 && game::health(npc) <= dmg) {
            attacker = av;
            g_kills_credited++;
        }
        game::apply_damage(npc, dmg, q.type, attacker);
        g_hits_applied++;
        break;
    }
    case net::MSG_DAMAGE: { // guest: the host says we were hit
        net::Damage d;
        if (!is_guest() || !d.read(r) || d.player != C.sess->my_id())
            return;
        void* av = game::avatar();
        if (!av)
            return;
        void* attacker = nullptr;
        auto it = g_proxies.find(d.attacker);
        if (it != g_proxies.end())
            attacker = it->second.c;
        game::apply_damage(av, teamplay::on_own_damage(av, d.damage), d.type, attacker);
        g_damage_in++;
        break;
    }
    }
}

bool damage_filter(void* c, float& damage, int type, void* attacker)
{
    if (!g_enabled || !C.sess || !C.sess->connected())
        return true;
    void* av = game::avatar();
    if (is_guest()) {
        auto it = g_proxy_of.find(c);
        if (it != g_proxy_of.end()) { // hit on a host NPC: the host applies it
            if (attacker == av) {
                net::DamageReq q;
                q.net_id = it->second;
                q.damage = damage;
                q.type = type;
                net::Writer w;
                q.write(w);
                C.sess->send_reliable_to(0, net::MSG_DAMAGE_REQ, w);
                g_hits_sent++;
            }
            return false;
        }
        if (game::is_puppet(c))
            return false; // no friendly fire on other players
        if (c == av)
            damage = teamplay::on_own_damage(av, damage);
        return true;
    }
    if (is_host()) {
        if (game::mode_id() == modes::kTribe)
            tribe::note_animal_hit(c, attacker);
        if (c == av) {
            damage = teamplay::on_own_damage(av, damage);
            return true;
        }
        if (!game::is_puppet(c)) {
            if (attacker == av)
                damage = teamplay::on_player_hit(c, 0, damage);
            return true;
        }
        // a guest's puppet was hit: forward real NPC hits to that guest
        for (auto& p : C.sess->players()) {
            if (p.id == 0 || C.puppet_of(p.id) != c)
                continue;
            if (attacker && attacker != av && !game::is_puppet(attacker)) {
                net::Damage d;
                d.player = p.id;
                d.damage = damage;
                d.type = type;
                auto it = g_host_npcs.find(attacker);
                d.attacker = it != g_host_npcs.end() ? it->second.id : 0xFFFF;
                net::Writer w;
                d.write(w);
                C.sess->send_reliable_to(p.id, net::MSG_DAMAGE, w);
            }
        }
        return false; // puppets mirror the guest's own health
    }
    return true;
}

std::string status()
{
    char b[512];
    if (is_host()) {
        size_t known = 0;
        for (auto& [g, ks] : g_known)
            known += ks.size();
        size_t on_partner = 0;
        for (auto& [c, pup] : g_ai_target)
            on_partner += g_seen_now.count(c) != 0;
        snprintf(b, sizeof b, "npc(host): tracked=%u alive=%u mirrored=%u hits_applied=%u guest_kills=%u strays_removed=%u "
                 "hatch_helped=%u partner_targets=%d on_partner=%u partner_picks=%u ai_swaps=%u swaps_restored=%u ledger_restored=%u reused=%u hunt_targets=%u\n",
                 (unsigned)g_host_npcs.size(), (unsigned)g_seen_now.size(), (unsigned)known, g_hits_applied,
                 g_kills_credited, g_strays_removed, g_hatch_helped, (int)g_partner_targets, (unsigned)on_partner,
                 g_ai_partner_picks, game::ai_swaps(), game::swaps_restored(), game::swap_ledger_restored(), g_reused, (unsigned)teamplay::hit_targets());
    } else {
        unsigned spawned = 0;
        for (auto& [id, px] : g_proxies)
            spawned += px.c != nullptr;
        snprintf(b, sizeof b, "npc(guest): proxies=%u spawned=%u local_removed=%u hits_sent=%u damage_in=%u lost=%u host_away=%d "
                 "away_dropped=%u local_alive=%u in_nest=%u nests=%u stale_dropped=%u strays_removed=%u hatch_helped=%u "
                 "deaths_played=%u kill_held=%u\n",
                 (unsigned)g_proxies.size(), spawned, g_removed_local, g_hits_sent, g_damage_in, g_proxy_lost, (int)g_host_away,
                 g_away_proxies, g_local_alive, g_proxies_in_nest, (unsigned)g_local_herds.size(), g_stale_dropped,
                 g_strays_removed, g_hatch_helped, g_deaths_played, (unsigned)g_kill.size());
    }
    return b + game::destroy_deferred_status() + "\n";
}

} // namespace npc

namespace npc {

// a fingerprint of a creature's body as the game built it: its abilities (count, type and level of
// each; they come from the creation's parts). Two creations under one key give different prints
// (tests: the guest must build the host's creation, not its own of the same key)
static uint32_t body_print(void* c)
{
    if (!c || IsBadReadPtr(c, 0x1680))
        return 0;
    int n = skg::vcall<int>(c, 0xB0 / 4);
    uint32_t h = 0x811C9DC5u ^ (uint32_t)n;
    for (int i = 0; i < n && i < 64; i++) {
        uint8_t* a = skg::vcall<uint8_t*>(c, 0xB4 / 4, i);
        uint32_t v[2] = {a && !IsBadReadPtr(a, 0x14) ? *(uint32_t*)(a + 8) : 0u, a && !IsBadReadPtr(a, 0x14) ? *(uint32_t*)(a + 0xC) : 0u};
        for (int j = 0; j < 8; j++)
            h = (h ^ ((uint8_t*)v)[j]) * 0x01000193u;
    }
    return h;
}

std::string list()
{
    std::string out;
    char b[200];
    void* av = game::avatar();
    net::Vec3 ap = av ? game::position(av) : net::Vec3{};
    if (is_host()) {
        double now = H->time_seconds();
        for (auto& [c, hn] : g_host_npcs) {
            if (!g_seen_now.count(c)) { // maybe freed already: no field reads
                snprintf(b, sizeof b, "  #%u %p not updated for %d ms\n", hn.id, c, (int)((now - hn.last_seen) * 1000));
                out += b;
                continue;
            }
            net::Vec3 p = game::position(c);
            // " DEAD" right after dist: tests match "dist=N( DEAD)?"
            snprintf(b, sizeof b, "  #%u %p species=%08X hp=%.2f dist=%.1f%s pos=(%.1f %.1f %.1f)%s%s body=%08X\n", hn.id,
                     c, game::species_key(c).instance, game::health(c), net::dist(p, ap), game::is_dead(c) ? " DEAD" : "",
                     p.x, p.y, p.z, game::pack_member(c) ? " pack" : "", g_ai_target.count(c) ? " on_partner" : "",
                     body_print(c));
            out += b;
        }
    } else {
        double now = H->time_seconds();
        for (auto& [k, h] : g_local_herds) {
            snprintf(b, sizeof b, "  nest %08X/%08X herd=%p alive=%d noun=%08X\n", (uint32_t)k, (uint32_t)(k >> 32), h,
                     (int)game::object_alive(h), game::noun_id(h));
            out += b;
        }
        for (auto& [id, px] : g_proxies) {
            net::PlayerState st;
            bool have = px.buf.sample(now, st);
            float err = px.c && have ? net::dist(game::position(px.c), st.pos) : -1.f;
            snprintf(b, sizeof b, "  #%u species=%08X %s hp=%.2f dist=%.1f err=%.2f age=%.2f%s want=(%.1f %.1f %.1f) puppet=%d ptr=%p herd=%p body=%08X\n",
                     id, px.key.instance, px.c ? "spawned" : "pending", px.c ? game::health(px.c) : 0.f,
                     px.c ? net::dist(game::position(px.c), ap) : -1.f, err, px.buf.age(now),
                     px.killed ? " DEAD" : "", st.pos.x, st.pos.y, st.pos.z, px.c ? (int)game::is_puppet(px.c) : -1, px.c, px.c ? game::herd_of(px.c) : nullptr, body_print(px.c));
            out += b;
        }
    }
    return out;
}

// debug: the living creature objects (all, or those of one species, or only those without archetype
// tuning +0xE84): ours (puppets, proxies) and any other (a creature brought back by a save made while
// ours existed, docs/re/creature.md 4)
std::string creatures(bool untuned_only, uint32_t species)
{
    struct Acc {
        std::string out;
        unsigned n = 0, untuned = 0, ours = 0, idle = 0;
        void* av;
        bool untuned_only;
        uint32_t species;
    } a{"", 0, 0, 0, 0, game::avatar(), untuned_only, species};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* a = (Acc*)u;
            if (noun != 0x18EB45Eu || !game::object_alive(o) || IsBadReadPtr(o, 0x1680))
                return true;
            void* e84 = *(void**)((uint8_t*)o + 0xE84);
            if (!game::species_profile(o)) { // a creature object without a species: not in the world (pool)
                a->idle++;
                return true;
            }
            if ((a->untuned_only && e84) || (a->species && game::species_key(o).instance != a->species))
                return true;
            void* h = game::herd_of(o);
            bool ours = game::is_puppet(o) || g_proxy_of.count(o);
            bool hr = h && !IsBadReadPtr(h, 0x21C);
            char b[352];
            net::Vec3 p = game::position(o);
            snprintf(b, sizeof b, "  creature %p species=%08X prof=%p e84=%p herd=%p arch=%08X nest=%p flags=%08X body=%08X%s%s%s%s pos=(%.1f %.1f %.1f)\n", o,
                     game::species_key(o).instance, game::species_profile(o), e84, h, hr ? *(uint32_t*)((uint8_t*)h + 0x88) : 0u,
                     hr ? game::herd_nest(h) : nullptr, *(uint32_t*)((uint8_t*)o + 0xB58), body_print(o), ours ? " ours" : "",
                     o == a->av ? " avatar" : "", game::is_dead(o) ? " DEAD" : "", e84 ? "" : " untuned", p.x, p.y, p.z);
            a->out += b;
            a->n++;
            a->untuned += !e84;
            a->ours += ours && !e84;
            return true;
        },
        &a);
    char b[160];
    snprintf(b, sizeof b, "creatures %u: untuned %u (ours %u, other %u); %u without a species\n", a.n, a.untuned,
             a.ours, a.untuned - a.ours, a.idle);
    return b + a.out;
}

// debug: the herds (noun 0x01BE418E) without an archetype (ours: CreateHerd(.., 0, 0, 0, 0)) with
// species profile, target size, enabled flag, nest; the game's own herds are only counted
std::string herds(bool all)
{
    struct Acc {
        std::string out;
        unsigned all = 0, plain = 0;
        bool every;
        net::Vec3 at;
    } a;
    a.every = all;
    void* av = game::avatar();
    a.at = av ? game::position(av) : net::Vec3{};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun != game::kHerdNoun || !game::object_alive(o) || IsBadReadPtr(o, 0x21C))
                return true;
            auto* a = (Acc*)u;
            a->all++;
            if (a->every) { // every herd: archetype, species key, home and its distance from the avatar
                auto* home = (float*)((uint8_t*)o + 0x34);
                void* sp = game::herd_species(o);
                uint32_t inst = sp && !IsBadReadPtr(sp, 0x10) ? *(uint32_t*)((uint8_t*)sp + 0xC) : 0;
                char b[200];
                snprintf(b, sizeof b, "  herd %p arch=%08X species=%p/%08X nest=%p dist=%.0f\n", o,
                         *(uint32_t*)((uint8_t*)o + 0x88), sp, inst, game::herd_nest(o),
                         net::dist(net::Vec3{home[0], home[1], home[2]}, a->at));
                a->out += b;
                return true;
            }
            if (*(uint32_t*)((uint8_t*)o + 0x88))
                return true;
            a->plain++;
            char b[220];
            void* sp = game::herd_species(o); // a cSpeciesProfile: its key at +0x504
            snprintf(b, sizeof b, "  herd %p species=%p key=%08X arch=%08X target=%d enabled=%d nest=%p avatar_owned=%d\n", o,
                     sp, sp && !IsBadReadPtr(sp, 0x510) ? *(uint32_t*)((uint8_t*)sp + 0x504) : 0u,
                     *(uint32_t*)((uint8_t*)o + 0x88), *(int32_t*)((uint8_t*)o + 0xF4),
                     *(uint8_t*)((uint8_t*)o + 0x218), game::herd_nest(o), *(uint8_t*)((uint8_t*)o + 0x84));
            a->out += b;
            return true;
        },
        &a);
    char b[96];
    snprintf(b, sizeof b, "herds %u, without an archetype %u\n", a.all, a.plain);
    return b + a.out;
}

bool debug_hit(uint16_t id, float damage)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end() || !it->second.c || !game::avatar())
        return false;
    game::hit(it->second.c, damage, 0, game::avatar());
    return true;
}

} // namespace npc

namespace npc {
void* proxy(uint16_t id)
{
    auto it = g_proxies.find(id);
    return it == g_proxies.end() ? nullptr : it->second.c;
}

void* host_npc(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    return it != g_host_by_id.end() && g_seen_now.count(it->second) && game::object_alive(it->second) ? it->second
                                                                                                     : nullptr;
}

int host_id(void* c)
{
    auto it = g_host_npcs.find(c);
    return it == g_host_npcs.end() ? -1 : it->second.id;
}

int proxy_id(void* c)
{
    auto it = g_proxy_of.find(c);
    return it == g_proxy_of.end() ? -1 : it->second;
}

int proxy_id_of_ai(void* ai)
{
    for (auto& [c, id] : g_proxy_of)
        if (c && *(void**)((uint8_t*)c + 0xB4C) == ai) // cCreatureAnimal +0xB4C: the AI block
            return id;
    return -1;
}

// host: the pack recruits NPC #id, as the end of a social game with an ally does (tests)
bool debug_recruit(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    if (!is_host() || it == g_host_by_id.end() || !g_seen_now.count(it->second))
        return false;
    static uintptr_t get = H->sym("SporeCoop::cPosseSimulator_Get"), add = H->sym("SporeCoop::cPosseSimulator_AddMember");
    void* posse = get && add ? skg::cdecl_call<void*>(get) : nullptr;
    return posse && skg::thiscall<bool>(add, posse, it->second);
}

// host: an NPC of the given species (crt instance) a few metres from the avatar, in a herd of its
// own (tests: a species only the host's profile has, like a creation of its My Creations)
void* debug_spawn(uint32_t instance)
{
    void* av = game::avatar();
    void* sp = av ? game::species_by_key({instance, 0x2B978C46u, 0x40626200u}) : nullptr;
    if (!sp)
        return nullptr;
    net::Vec3 p = game::position(av);
    p.x += 4.f;
    void* herd = game::create_herd(p, sp);
    return herd ? game::spawn_creature(p, sp, herd) : nullptr;
}

// host (tests, BUG-0044): dead NPC #id's object becomes a living creature of its species again, the
// state host_tick meets when the game builds a new creature in a freed one (cCreatureAnimal::Create
// with its reuse flag takes a free slot of the creature pool whose object had the same species
// profile, 0xAD2020). A natural reuse could not be forced: DestroyInstance on a corpse, then up to 8
// Create(reuse) of its species over 8 frames, never returned the freed object (2026-10-28, lane 5).
// So the object itself is made alive again: the death flag cleared, full health.
std::string debug_reuse(uint16_t id)
{
    void* c = host_npc(id);
    if (!is_host() || !c)
        return "not the host or no such npc";
    char b[160];
    snprintf(b, sizeof b, "npc #%u %p species %08X dead=%d: alive again (as a new creature in its object)", id, c,
             game::species_instance(c), (int)game::is_dead(c));
    *((uint8_t*)c + game::kDeadOff) = 0;
    game::set_health(c, game::max_health(c));
    return b;
}

// host (tests): an epic creature (archetype 0xFC811218, isEpic: always hostile to the player,
// docs/re/tribe.md E) `dist` m from player `player`'s creature (0: the host's avatar), of the species
// of the nearest NPC (everybody has its model)
void* debug_spawn_epic(uint8_t player, float dist, uint32_t arch, uint32_t species)
{
    void* av = game::avatar();
    void* who = player ? (C.puppet_of ? C.puppet_of(player) : nullptr) : av;
    if (!is_host() || !av || !who)
        return nullptr;
    net::Vec3 at = game::position(who);
    void* prof = nullptr;
    float bd = 1e9f;
    for (auto& [c, hn] : g_host_npcs) {
        if (!g_seen_now.count(c) || game::is_dead(c) || game::herd_of(c) == game::avatar_herd() || !game::species_profile(c))
            continue;
        float d = net::dist(game::position(c), at);
        if (d < bd) {
            bd = d;
            prof = game::species_profile(c);
        }
    }
    if (species) // a given species (crt instance), e.g. the one of an epic of the planet
        prof = game::species_by_key({species, 0x2B978C46u, 0x40626200u});
    if (!prof)
        return nullptr;
    float r = std::sqrt(at.x * at.x + at.y * at.y + at.z * at.z);
    net::Vec3 up = r > 1 ? net::Vec3{at.x / r, at.y / r, at.z / r} : net::Vec3{0, 0, 1};
    net::Vec3 side = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
    float sl = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
    net::Vec3 p{at.x + side.x / sl * dist + up.x, at.y + side.y / sl * dist + up.y, at.z + side.z / sl * dist + up.z};
    static uintptr_t herd_fn = H->sym("Simulator::cGameNounManager::CreateHerd"),
                     create = H->sym("Simulator::cCreatureAnimal::Create");
    void* nm = game::noun_manager();
    float pp[3] = {p.x, p.y, p.z};
    void* herd = nm && herd_fn && create ? skg::thiscall<void*>(herd_fn, nm, pp, prof, 0, false, 0, false) : nullptr;
    if (!herd)
        return nullptr;
    *(uint32_t*)((uint8_t*)herd + 0x88) = arch;  // e.g. 0x95511253 / 0xFC811218: isEpic = 1
    *(uint32_t*)((uint8_t*)herd + 0xF0) = 0;     // generation
    // as an epic herd of the planet has them (runtime 2026-10-25): archetype group 2, territory 50 m,
    // personality 9 (without these the spawned epic wandered and never attacked anybody)
    *(uint32_t*)((uint8_t*)herd + 0x8C) = 2;
    *(float*)((uint8_t*)herd + 0x108) = 50.f;
    *(uint32_t*)((uint8_t*)herd + 0x15C) = 9;
    void* c = skg::cdecl_call<void*>(create, pp, prof, 1, herd, false, false);
    *(int32_t*)((uint8_t*)herd + 0xF4) = 0; // no more members
    static uintptr_t enable = H->sym("Simulator::cHerd::SetEnabled");
    if (enable && c)
        skg::thiscall<void>(enable, herd, true); // CreateHerd leaves it disabled in the creature stage
    return c;
}

bool debug_host_hit(uint16_t id, float damage)
{
    auto it = g_host_by_id.find(id);
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !game::avatar())
        return false;
    game::hit(it->second, damage, 0, game::avatar()); // through the damage filter
    return true;
}

// tests: the message an attack of the avatar on host NPC `id` posts (FUN_00C02B40, the creature's
// attack vt 0xB8: ExecuteAction(0xD335362C, {attacker, target, 0}) to every creature-stage
// listener, CreatureStageEventHandler among them; docs/re/creature.md 4)
bool debug_attack_msg(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    void* av = game::avatar();
    static uintptr_t get = H->sym("App::cCreatureModeStrategy::Get"),
                     exec = H->sym("App::cCreatureModeStrategy::ExecuteAction");
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !av || !get || !exec)
        return false;
    void* strat = skg::cdecl_call<void*>(get);
    if (!strat)
        return false;
    void* msg[3] = {av, it->second, nullptr};
    skg::thiscall<void>(exec, strat, 0xD335362Cu, (void*)msg);
    return true;
}

bool debug_hurt(uint8_t player, uint16_t id, float damage)
{
    auto it = g_host_by_id.find(id);
    void* pup = C.puppet_of(player);
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !pup)
        return false;
    game::hit(pup, damage, 0, it->second);
    return true;
}
std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [c, h] : g_host_npcs) // updated this frame: an unloaded one may be freed already
            if (g_seen_now.count(c) && game::object_alive(c) && !game::is_dead(c)) // living ones only
                v.push_back({"n" + std::to_string(h.id), game::position(c), game::orientation(c)});
    } else {
        for (auto& [id, px] : g_proxies)
            if (px.c && game::object_alive(px.c) && !px.killed)
                v.push_back({"n" + std::to_string(id), game::position(px.c), game::orientation(px.c)});
    }
    return v;
}

} // namespace npc
