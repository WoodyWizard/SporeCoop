#include "lifecycle.h"
#include "effects.h"
#include "player_life.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <utility>

#include "fixes.h"
#include "game.h"
#include "mirror_fx.h"
#include "modes.h"
#include "net/protocol.h"
#include "net/validate.h"
#include "sporekit_game.hpp"

namespace effects {

static const SkHost* H;
static SkMod* M;
static Ctx C;
// BUG-0795 (class BUG-0426): a partner's effect message was played on the raw puppet pointer: one the game freed
// between two frames (a teardown, the UFO abduction BUG-0520, the game's own destroy) or a Cell cell / Tribe marker
// after a stage switch went into the game's effect code (cCreatureBase effect pool) on freed or foreign memory. Now
// only on a Creature puppet that is the live object made for that player (game::listed and its serial, this frame's
// world); the message is dropped otherwise (coop.effects: gone=). Old (`coop.fix effects.puppet_live off`): raw.
static bool g_puppet_live_fix = true;
COOP_FIX("effects.puppet_live", "BUG-0795", g_puppet_live_fix, "creature: a partner's effect is only played on its creature while that creature is still alive in this game (the game's effect code ran on a freed one)");
static uint32_t g_puppet_gone;
static uint32_t g_test_no_puppet; // tests (BUG-1517): players whose puppet this game has not (a bit per player id)
static void* partner_puppet(uint8_t player)
{
    if (player < 32 && (g_test_no_puppet >> player & 1))
        return nullptr;
    if (g_puppet_live_fix)
        return C.puppet_live ? C.puppet_live(player) : nullptr;
    return C.puppet_of ? C.puppet_of(player) : nullptr;
}

// BUG-0451: the whole class of creature effects, relayed from the game's one effect path.
static bool g_fix = true;
COOP_FIX("creature.effects_relay", "BUG-0451", g_fix,
         "creature: every effect a player's creature makes (hearts, social, attack, ...) shows on the partner's puppet");

// BUG-0471: an effect is sent only while a partner plays in the same place, and a received start
// is played only while its owner does (a guest in the main menu, an editor, a loading screen or at
// another star does not create the host's effects in whatever world its game has then).
static uint32_t g_elsewhere_tx, g_elsewhere_rx;
static bool nobody_here() { return C.anyone_here && !C.anyone_here(); }

// BUG-0451 (Tribe / Civilization): 0.7.4 relayed effect starts only in Cell and Creature, because a relayed world
// effect made the receivers fault hundreds of times in Skinner::cSkinnerTexturePainter::StartRender+0x28
// (effects_party_civ out/lab/20261103-215559, effects_party_tribe out/lab/20261103-223036). The cause: the
// skin painter (Skinner::cSkinPainterPipeline::ExecutePipeline, it paints a creature's skin when a model is
// built, e.g. a partner's puppet or a tribe member) creates its paint effects in an effect world of its own
// on the main thread; the world-effect hook saw them like any effect, and one made near the local player was
// sent. Created in the receiver's game effect world it has no painter and faults every frame while it lives
// (docs/re/creature.md#creature-effects-painter). Only effects of the game's own effect world are sent and an id
// this game saw only in another world is never played (BUG-0451); with that, Tribe and Civilization relay again
// (`effects.other_stages`, on). Off: the 0.7.4 behaviour (no effect starts there; stops pass).
static bool g_other_stages = true;
COOP_FIX("effects.other_stages", "BUG-0451", g_other_stages,
         "effects: also relayed in Tribe and Civilization (off: Cell and Creature only, as 0.7.4)");
// Space: off since 0.7.4 as "not verified" (no effect of the own UFO could be made in a test, docs/re/creature.md).
// BUG-0895: effects_party_space (three players, the game's own effect at each player's ship, relayed host -> guest and
// guest -> guest) passes with it and fails without it, no fault: on by default now. `coop.fix effects.space off`: off.
static bool g_space = true;
COOP_FIX("effects.space", "BUG-0895", g_space, "effects: also relayed in Space (the effects a player's ship makes show on the partners' screens)");
static uint32_t g_stage_skipped;
static bool stage_off()
{
    uint32_t m = game::mode_id();
    bool off = m == modes::kSpace ? !g_space : (m == modes::kTribe || m == modes::kCiv) ? !g_other_stages : false;
    if (off)
        g_stage_skipped++;
    return off;
}
// ids this game created only in another effect world (the skin painter's): a received start of one is not
// played (a partner of another build, or one that sent it before this fix). Fixed size, no allocation.
static constexpr size_t kForeignIds = 256;
static uint32_t g_foreign_ids[kForeignIds];
static size_t g_foreign_n, g_foreign_at;
static uint32_t g_w_foreign, g_w_foreign_near, g_rx_foreign;
static bool foreign_id(uint32_t id)
{
    for (size_t i = 0; i < g_foreign_n; i++)
        if (g_foreign_ids[i] == id)
            return true;
    return false;
}
static void note_foreign_id(uint32_t id)
{
    if (foreign_id(id))
        return;
    g_foreign_ids[g_foreign_at] = id; // a ring: the oldest id goes first
    g_foreign_at = (g_foreign_at + 1) % kForeignIds;
    if (g_foreign_n < kForeignIds)
        g_foreign_n++;
}

// Rate limit: a creature can make a burst of one-shot effects (footsteps, bites, particles). 40 a
// second is far above every effect the game makes for one avatar and below a packet flood.
static constexpr double kRate = 40.0;
static double g_tokens = 20.0;
static double g_tokens_at = 0;

static uint32_t g_sent, g_rx, g_played, g_stopped, g_skipped, g_failed, g_dropped;
// why a world effect the hooks queued was not sent (coop.effects: the frame-time and test diagnosis)
static uint32_t g_w_no_pos, g_w_relayed, g_w_far, g_w_seen, g_w_rate;
// received messages dropped before any game call (BUG-0451 review: a peer's message is data)
static uint32_t g_dropped_spoof;
static uint32_t g_bad_pool, g_bad_id, g_bad_bone, g_bad_xf, g_bad_short;
static constexpr float kPosMax = 1e5f; // a world position further out is not a planet's stage
// BUG-1517 (UIFX-6): the bone of a pool-3 one-shot was checked against the RECEIVER's puppet of the sender before the
// host's relay, so a host with no live puppet of guest 1 (its puppet not made yet, the host elsewhere) dropped guest 1's
// one-shots for guests 2-3 too, who have that puppet. On: the wire checks stay before the relay, the bone is checked
// against the puppet that plays it, on every game, right before the game's CreateEffect. `coop.fix effects.bone_local
// off`: checked before the relay (0.7.16.1)
static bool g_bone_local_fix = true;
COOP_FIX("effects.bone_local", "BUG-1517", g_bone_local_fix, "effects: a guest's one-shot effect reaches the other guests also when the host has no creature of that guest");
static uint32_t g_relayed_unchecked; // pool-3 one-shots relayed before the bone check (BUG-1517)
// BUG-1048: a received world effect's scale must be in the sender's range (0, 1000]; off: any finite scale
static bool g_scale_fix = true;
COOP_FIX("effects.scale_check", "BUG-1048", g_scale_fix, "effects: a partner's world effect with a scale of 0, below 0 or over 1000 is dropped");
// Every field of a received start / stop is checked here before the game is called (BUG-0451
// review item 3); false = dropped and counted.
static bool valid_message(uint8_t player, uint8_t stop, uint8_t pool, uint32_t id, uint32_t key, const uint8_t* xf)
{
    if (stop) { // the sender stops a pool key: pool 0, the key in `key`
        if (pool != 0) {
            g_bad_pool++;
            return false;
        }
        if (!key) {
            g_bad_id++;
            return false;
        }
        return true;
    }
    if (pool != 1 && pool != 2 && pool != 3 && pool != 5) {
        g_bad_pool++;
        return false;
    }
    if (!id) {
        g_bad_id++;
        return false;
    }
    if (pool == 5) {
        float f[16]; // position, uniform scale, 3x3 matrix
        memcpy(f, xf + 4, sizeof f);
        if (!net::effect_xf_ok(f, kPosMax, g_scale_fix)) { // BUG-1048: the scale too
            g_bad_xf++;
            return false;
        }
    } else if (pool == 3 && !g_bone_local_fix) { // (BUG-1517: on, checked at the play)
        // the game indexes the model's bone array (stride 0x2bc = 700) with this index and does not
        // check it (cCreatureBase::CreateEffect 0xC150D0); the array's end gives the count
        void* puppet = partner_puppet(player);
        if (!puppet || !game::creature_bone_ok(puppet, (int32_t)key)) {
            g_bad_bone++;
            return false;
        }
    }
    return true;
}
static bool g_log;

struct Last {
    uint32_t id = 0, key = 0;
    uint8_t pool = 0;
    bool stop = false;
    bool game = false;    // the game's effect object existed right after it was played
    bool active = false;  // ... and it was active then (its vt 0x10)
    bool has_pos = false; // ... and its position (effect + 0x9C, the transform the game set)
    float x = 0, y = 0, z = 0;
    double at = 0;
};
static Last g_last_tx;
static std::map<uint8_t, Last> g_last_rx;
static std::map<uint8_t, Last> g_last_rx_start; // the last start per player (a stop may follow)
PLAYER_STATE("effects", [](uint8_t id) { g_last_rx.erase(id); g_last_rx_start.erase(id); }, // BUG-0422
             [](uint8_t id) -> size_t { return g_last_rx.count(id) + g_last_rx_start.count(id); });
// the pool keys we sent a start for: a stop is sent once, not on every frame the game stops a key.
// A fixed array: this is filled from inside the game's effect creation (no allocation there).
static uint32_t g_sent_keys[64];
static size_t g_sent_n;
static uint8_t g_last_rx_from = 0xFF;
// tests (BUG-1243): the last world effects sent and played here, with where the local player was then
struct Recent {
    char dir = 0; // 's' sent, 'p' played, 'm' made by this game (any world effect with a place)
    uint8_t player = 0;
    uint32_t id = 0;
    float x = 0, y = 0, z = 0, d = -1; // d: distance to the local player's object (-1: none)
    float scale = -1;                  // BUG-1266: the effect's uniform scale (its transform +0x10), -1: not read
    bool held = false;                 // BUG-1266: made by the Cell graphics update for a cell it drives
    double at = 0;
};
static constexpr size_t kRecent = 160;
static Recent g_recent[kRecent];
static size_t g_recent_at, g_recent_n;
static void note_recent(char dir, uint8_t player, uint32_t id, const net::Vec3& p, float scale = -1, bool held = false)
{
    Recent& r = g_recent[g_recent_at];
    r.dir = dir;
    r.player = player;
    r.id = id;
    r.x = p.x, r.y = p.y, r.z = p.z;
    r.scale = scale;
    r.held = held;
    r.d = -1;
    if (C.local_pos) {
        net::Vec3 l = C.local_pos();
        if (l.x != 0 || l.y != 0 || l.z != 0)
            r.d = net::dist(l, p);
    }
    r.at = H ? H->time_seconds() : 0;
    g_recent_at = (g_recent_at + 1) % kRecent;
    g_recent_n = g_recent_n < kRecent ? g_recent_n + 1 : kRecent;
}
// world effects already sent this frame (effect pointer, id): the manager and the world create
// hooks both queue the same effect, so the second one is skipped. Open addressing keyed by the
// pointer: one probe for nearly every effect, fixed size, no allocation, cleared once a frame
// (BUG-0451 review item 6; the old shape scanned 2048 entries per queued world effect).
struct WorldSent { void* fx; uint32_t id; };
static constexpr size_t kWorldSentSlots = 512; // power of two; far more than a frame queues
static WorldSent g_world_sent[kWorldSentSlots];
static uint32_t g_world_sent_probes;
static inline size_t ws_slot(void* fx)
{
    uint32_t h = (uint32_t)((uintptr_t)fx >> 4) * 2654435761u;
    return (h >> 20) & (kWorldSentSlots - 1);
}
static bool world_sent_seen(void* fx, uint32_t id)
{
    size_t i = ws_slot(fx);
    for (unsigned n = 0; n < 6; n++, i = (i + 1) & (kWorldSentSlots - 1)) {
        g_world_sent_probes++;
        if (g_world_sent[i].fx == fx && g_world_sent[i].id == id)
            return true;
        if (!g_world_sent[i].fx)
            return false;
    }
    return false;
}
static void world_sent_add(void* fx, uint32_t id)
{
    size_t i = ws_slot(fx);
    for (unsigned n = 0; n < 6; n++, i = (i + 1) & (kWorldSentSlots - 1)) {
        if (!g_world_sent[i].fx || (g_world_sent[i].fx == fx && g_world_sent[i].id == id)) {
            g_world_sent[i] = {fx, id};
            return;
        }
    }
    g_world_sent[ws_slot(fx)] = {fx, id}; // 6 probes over: take the home slot (does not happen here)
}
static void world_sent_clear()
{
    for (auto& e : g_world_sent)
        if (e.fx)
            e = WorldSent{};
}
static uint32_t g_world;
// The world effects the relay created on this game (BUG-0451 review item 4). A looping effect (a
// fire, an aura) never ends by itself, so without a stop the relayed ones pile up. Fixed array:
// 64 live at most, the oldest stopped first; anything still active after 8 s is stopped. A stage or
// world change drops the list without touching the effects: the game freed them with its effect
// manager.
struct Live { void* fx; double at; };
static constexpr size_t kLiveMax = 64;
static constexpr double kLiveSec = 8.0;
static Live g_live[kLiveMax];
static size_t g_live_n;
static uint32_t g_live_stopped, g_live_expired, g_live_released, g_live_dropped, g_live_mode, g_live_world;
static bool g_live_init;
// BUG-0451 review item 7: the reference cEffectsManager::CreateEffect gives the caller is released
// when the relay forgets the effect. Off: the old behaviour (every relayed world effect stays
// allocated for the session), for the 60 s measurement test.
static bool g_release_fix = true;
COOP_FIX("effects.release", "BUG-0451", g_release_fix,
         "effects: the relay releases the reference it holds on a world effect it created (no leak)");

static double now() { return H ? H->time_seconds() : 0; }

// BUG-0824: who wants to know of every world effect that starts here (this game's own and the relayed ones)
static WorldStartFn g_world_start;
void set_world_start_observer(WorldStartFn fn) { g_world_start = fn; }

// The game's 0x44-byte Transform (VisualEffect_SetTransform 0xA827B0 as cCreatureBase's creators
// build it): u16 flags, u16 change counter, Vec3 position at +4, uniform scale at +0x10, 3x3
// rotation at +0x14. A world effect we relay gets the position the game put on it, scale 1 and the
// identity rotation (BUG-0451 review item 5; the first shape sent a zero scale and a zero matrix).
static void make_transform(uint8_t* xf, const net::Vec3& p)
{
    memset(xf, 0, 0x44);
    float* f = (float*)(xf + 4);
    f[0] = p.x;
    f[1] = p.y;
    f[2] = p.z;
    f[3] = 1.0f; // scale
    f[4] = 1.0f; // rotation: identity
    f[5] = 0.0f;
    f[6] = 0.0f;
    f[7] = 0.0f;
    f[8] = 1.0f;
    f[9] = 0.0f;
    f[10] = 0.0f;
    f[11] = 0.0f;
    f[12] = 1.0f;
}

// BUG-0608 (a relayed effect at the sender's ground height): a world effect (a hit impact, a splash) carries the
// sender's absolute position; where the two games' grounds differ (Tribe villages made live, BUG-0332; any terrain
// the games do not share) it was played metres under or over the receiver's ground. The sender now appends the
// effect's height over its own ground (an f32 after the transform: older SporeCoop stops reading before it, as the
// NPC states' agl trailer, BUG-0425) and the receiver puts the effect at that height over ITS ground, as every
// remote body. `coop.fix effects.ground off`: the sender's absolute position (0.7.10).
static bool g_ground_fix = true;
COOP_FIX("effects.ground", "BUG-0608", g_ground_fix, "effects: an effect in the world is played on the receiver's own ground (impacts, splashes)");
constexpr float kGroundAglMax = 30.f; // m: farther off the ground (a sky effect) the absolute position is kept
static uint32_t g_ground_moved;
static float sender_agl(const net::Vec3& p)
{
    float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    float g = r > 1.f ? game::ground_radius(p) : 0.f;
    if (g <= 0 || std::fabs(r - g) > kGroundAglMax)
        return net::kNoAgl;
    return r - g;
}
// the transform's position (xf + 4) at `agl` over this game's ground; false: left as sent
static bool onto_own_ground(uint8_t* xf, float agl)
{
    if (!g_ground_fix || !net::agl_known(agl))
        return false;
    float* f = (float*)(xf + 4);
    float r = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (!(r > 1.f))
        return false;
    net::Vec3 p{f[0], f[1], f[2]};
    float g = game::ground_radius(p);
    if (g <= 0 || std::fabs(g + agl - r) > kGroundAglMax)
        return false;
    float k = (g + agl) / r;
    f[0] *= k;
    f[1] *= k;
    f[2] *= k;
    return true;
}

// BUG-0465: a world effect was sent with its position only (identity rotation, scale 1, transform flags 0, which also
// tell the game to ignore the rotation): the sea monster of a player who drowns (the death cinematic's startEffect
// crg_seamonster_attack 0x8A71529C at the avatar, CRG_AvatarDied_SeaMonster of Spore_Game.package 0x024A0E52 /
// 0x40890200 / 0x9EA3031A) was played in the partner's game at the right place but not upright on the planet: tilted
// into the water / the ground, nobody saw it. Now the effect's own transform is sent, as the game's
// VisualEffect_SetTransform 0xA827B0 keeps it at effect +0x9C ({u16 flags, u16 changes, pos, scale, 3x3 rotation}, the
// same layout as the message's 0x44 bytes).
static uint32_t g_xform_sent, g_xform_bad;
static bool copy_transform(uint8_t* xf, void* fx, const net::Vec3& p)
{
    const uint8_t* t = (const uint8_t*)fx + 0x9C;
    if (!skg::readable(t, 0x38))
        return false;
    const float* f = (const float*)(t + 4);
    float sc = f[3];
    if (!std::isfinite(sc) || sc <= 0.f || sc > 1000.f)
        return false;
    for (int i = 0; i < 13; i++)
        if (!std::isfinite(f[i]))
            return false;
    if (std::fabs(f[0] - p.x) > 0.01f || std::fabs(f[1] - p.y) > 0.01f || std::fabs(f[2] - p.z) > 0.01f)
        return false;
    memset(xf, 0, 0x44);
    memcpy(xf, t, 0x38);
    return true;
}

// tests (BUG-0465): the last effect of a watched id this game made (its own) or played (a partner's), with its transform
static uint32_t g_watch_id;
static void* g_watch_own;
static void* g_watch_rx;
static double g_watch_own_at, g_watch_rx_at;

static bool take_token(double t)
{
    if (g_tokens_at <= 0)
        g_tokens_at = t;
    g_tokens += (t - g_tokens_at) * kRate;
    if (g_tokens > kRate)
        g_tokens = kRate;
    g_tokens_at = t;
    if (g_tokens < 1)
        return false;
    g_tokens -= 1;
    return true;
}

static const char* who(void* c)
{
    if (!c)
        return "none";
    if (c == game::avatar())
        return "avatar";
    return game::is_puppet(c) ? "puppet" : "other";
}

// The game calls the effect hooks also from its UI skinner render pipeline (Tribe/Civ): nothing
// there may allocate, call game code other than the hook itself, or send on the network. The hooks
// only copy the event into this fixed ring; effects::tick (main thread) decides and sends.
struct Pending {
    void* creature = nullptr;
    game::EffectEvent ev;
    bool world = false;
    uint32_t id = 0;
    void* fx = nullptr;
    bool hit = false; // BUG-0603: an attack's per-target hit impact at `pos`
    net::Vec3 pos;
};
static Pending g_q[256];
static uint32_t g_qh, g_qt;
static std::atomic_flag g_qlock = ATOMIC_FLAG_INIT;
static std::atomic<uint32_t> g_qdropped{0};

static bool q_push(const Pending& p)
{
    while (g_qlock.test_and_set(std::memory_order_acquire)) {
    }
    uint32_t h = g_qh;
    bool ok = ((h + 1) & 255) != g_qt;
    if (ok) {
        g_q[h] = p;
        g_qh = (h + 1) & 255;
    }
    g_qlock.clear(std::memory_order_release);
    if (!ok)
        g_qdropped++;
    return ok;
}

static bool q_pop(Pending& p)
{
    while (g_qlock.test_and_set(std::memory_order_acquire)) {
    }
    bool ok = g_qt != g_qh;
    if (ok) {
        p = g_q[g_qt];
        g_qt = (g_qt + 1) & 255;
    }
    g_qlock.clear(std::memory_order_release);
    return ok;
}

static void on_local_effect(void* creature, const game::EffectEvent& ev)
{
    if (!creature || !H)
        return;
    Pending p;
    p.creature = creature;
    p.ev = ev;
    q_push(p);
}

// an effect created for any object: keep it; tick reads the position the game gave it (+0x9C) and
// decides whether it is on the local player's own object (the hooks run on the main thread only, so
// no transform is copied here)
// BUG-1243 (players' video: "I fought the air"): the Cell stage's hatch effects, cell_egg_L0_autohatch (0xB4B06F12, the
// respawn's) and cell_hatch_ice (0x082BF046, the co-op revive's; FNV-1 of the names), are pictures for the player whose
// cell hatches: they black out the whole screen and fade it in over ~1 s, the egg and its whirl drawn over the view.
// Played in the host's game 70 m from its own cell, each blacked out the host's screen (brightness 129 -> 3-6, lab
// 20261118-132515; docs/re/cell.md#cell-partner-hatch). A guest knocked out while it fought far away and revived or
// respawned sent them to the host twice: its world effect relayed (process_world: made at the guest's own cell) and the
// host's own play on the guest's puppet (cellsync on_ko). The host's screen went black and its cell was drawn in the
// hatch's whirl while the host fought nothing. Now such an effect is neither sent nor played for a partner, and the
// puppet gets none. Old (`coop.fix effects.cell_own_only off`): relayed and played.
static bool g_own_only_fix = true;
COOP_FIX("effects.cell_own_only", "BUG-1243", g_own_only_fix, "cell: a partner's respawn no longer plays the hatch's black screen and egg on your own screen (it looked as if your own cell was spun in a fight)");
bool cell_own_only_fix() { return g_own_only_fix; }
static uint32_t fnv1(const char* s) // the game's effect id of a name (0x932E50(name, 0x811C9DC5, 1): FNV-1, lower case)
{
    uint32_t h = 0x811C9DC5u;
    for (; *s; s++)
        h = (h * 0x01000193u) ^ (uint8_t)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
    return h;
}
static bool own_only(uint32_t id)
{
    static const uint32_t ids[] = {fnv1("cell_egg_L0_autohatch"), fnv1("cell_hatch_ice")};
    for (uint32_t x : ids)
        if (x == id)
            return true;
    return false;
}
static uint32_t g_own_only_made, g_own_only_held_tx, g_own_only_held_rx; // tests: made in this game, not sent, not played

// BUG-1266 (key-frame review: the host's electric zap drawn on the guest's screen as a trail of giant white rings
// centred on the host's cell): the Cell graphics update (SporeCoop::CellGfx_Update) creates the effects a cell's
// graphics object keeps and drives them every frame: the electric part's charge field (0x17AAB61F / 0x5CA636DC,
// SporeCoop::CellGfx_ElectricField: at the part, the scale 2 x the zap range, stopped when the charge ends), the zap's
// lightning (0x7D84966F / 0xA1FB7133, SporeCoop::CellGfx_ZapBolt: a path from the attacker set every frame), the
// propulsion and glows. The relay sent each as a one-shot world effect: the copy stayed where it was made with the
// first frame's transform, never stopped by the game, for the relay's 8 s (kLiveSec), one more per zap. The partner's
// game runs the same update for its own copy of the cell. On: effects made in that update are neither sent nor, for
// an older partner's, the charge field and the lightning played. Off (`coop.fix effects.cell_held off`): sent and
// played (0.7.15).
static bool g_held_fix = true;
COOP_FIX("effects.cell_held", "BUG-1266", g_held_fix,
         "cell: a partner's electric zap no longer leaves a trail of giant white rings on your screen (a cell's own glows and charge are drawn by your game)");
static uint32_t g_held_tx, g_held_rx; // tests: not sent (made by the cell graphics update), not played (an older sender's)
static bool held_id(uint32_t id) // the electric part's effects (an older partner still sends them)
{
    return id == 0x17AAB61Fu || id == 0x5CA636DCu || id == 0x7D84966Fu || id == 0xA1FB7133u;
}
static float fx_scale(void* fx) // the effect's uniform scale: its transform (effect +0x9C) +0x10
{
    const float* f = fx && skg::readable((uint8_t*)fx + 0xAC, 4) ? (const float*)((uint8_t*)fx + 0xAC) : nullptr;
    return f && std::isfinite(*f) ? *f : -1.f;
}
static void on_world_effect(void* fx, uint32_t id)
{
    if (!fx || !id)
        return;
    if (own_only(id))
        g_own_only_made++;
    Pending p;
    p.world = true;
    p.fx = fx;
    p.id = id;
    q_push(p);
}

// BUG-0603: the hit impact of an attack animation on one of its targets (game.cpp's hook of the animation event
// handler's target branch). The game that runs the attacker makes it; a remote copy of the attacker plays the
// relayed animation without targets and never does, so it is sent as a world effect at the hit spot.
static uint32_t g_hit_sent, g_hit_rate, g_hit_off;
static void on_hit_effect(uint32_t id, const net::Vec3& pos)
{
    if (!id || !H)
        return;
    Pending p;
    p.hit = true;
    p.id = id;
    p.pos = pos;
    q_push(p);
}
static void process_hit(uint32_t id, const net::Vec3& p)
{
    if (!g_fix || !C.sess || !C.sess->connected()) {
        g_hit_off++;
        return;
    }
    if (stage_off() || nobody_here()) {
        g_hit_off++;
        return;
    }
    if (foreign_id(id)) // never a skin painter's id (BUG-0451)
        return;
    if (!take_token(now())) {
        g_dropped++;
        g_hit_rate++;
        return;
    }
    g_hit_sent++;
    g_sent++;
    if (g_log)
        H->log(M, SK_INFO, "effect: sent hit id %08X at %.2f %.2f %.2f", id, p.x, p.y, p.z);
    uint8_t xf[0x44];
    make_transform(xf, p);
    net::Writer w;
    w.u8(C.sess->my_id());
    w.u8(0);
    w.u8(5);
    w.u32(id);
    w.u32(0);
    w.raw(xf, 0x44);
    w.f32(sender_agl(p)); // BUG-0608: its height over this game's ground
    C.sess->send_event(net::MSG_EFFECT, w);
}

static bool g_stop_always_fix = true;
COOP_FIX("effects.stop_always", "BUG-1252", g_stop_always_fix, "effects: the end of an effect of a player's creature always reaches the partners (a burst of effects dropped it and the effect stayed on the partner's screen)");
static uint32_t g_stops_unlimited; // stops sent past the empty rate limit (coop.effects)

static void process_local(void* creature, const game::EffectEvent& ev)
{
    if (!g_fix || !creature)
        return;
    if (creature != game::avatar() && !game::is_puppet(creature)) { // BUG-0600: a host's NPC / member: its mirrors
        mirror_fx::on_creature_effect(creature, ev);
        return;
    }
    if (creature != game::avatar() || game::is_puppet(creature))
        return;
    if (!C.sess || !C.sess->connected())
        return; // the host's own id is 0, a valid player id
    if (ev.stop && !ev.key)
        return;
    if (!ev.stop && !ev.id)
        return;
    if (!ev.stop && stage_off())
        return;
    if (!ev.stop && nobody_here()) {
        g_elsewhere_tx++;
        return;
    }
    if (ev.stop) { // a stop only for a start the partner got: the game stops some keys every frame
        size_t i = 0;
        while (i < g_sent_n && g_sent_keys[i] != ev.key)
            i++;
        if (i == g_sent_n)
            return;
        g_sent_keys[i] = g_sent_keys[--g_sent_n];
    } else if (ev.pool == 1 || ev.pool == 2) {
        bool have = false;
        for (size_t i = 0; i < g_sent_n; i++)
            have |= g_sent_keys[i] == ev.key;
        if (!have && g_sent_n < 64)
            g_sent_keys[g_sent_n++] = ev.key;
    }
    double t = now();
    // BUG-1252: a stop is never dropped by the rate limit. Its key has just left g_sent_keys, so a dropped stop was
    // never sent again and the partner's puppet kept the effect for good (the hunger bubble after the creature ate,
    // a pool effect of an ability): in a burst of effects (a fight, `coop.effects load`) the bucket is empty for
    // seconds. A stop is at most one per started key: it costs a token when there is one and goes anyway.
    // `coop.fix effects.stop_always off`: a stop is dropped like a start (0.7.15).
    if (!take_token(t)) {
        if (ev.stop && g_stop_always_fix) {
            g_stops_unlimited++;
        } else {
            g_dropped++;
            if (g_log)
                H->log(M, SK_INFO, "effect: dropped by the rate limit: id %08X key %08X pool %u stop %d",
                       ev.id, ev.key, ev.pool, ev.stop ? 1 : 0);
            return;
        }
    }

    g_last_tx = Last{};
    g_last_tx.id = ev.id;
    g_last_tx.key = ev.key;
    g_last_tx.pool = ev.pool;
    g_last_tx.stop = ev.stop;
    g_last_tx.at = t;
    g_sent++;
    if (g_log)
        H->log(M, SK_INFO, "effect: sent id %08X key %08X pool %u stop %d", ev.id, ev.key, ev.pool, ev.stop ? 1 : 0);
    net::Writer w;
    w.u8(C.sess->my_id());
    w.u8(ev.stop ? 1 : 0);
    w.u8(ev.pool);
    w.u32(ev.id);
    w.u32(ev.key);
    C.sess->send_event(net::MSG_EFFECT, w);
}

static void process_world(void* fx, uint32_t id)
{
    if (!g_fix || !fx || !id || !C.sess || !C.sess->connected())
        return;
    if (game::effect_relayed(fx)) { // a creature hook already sent this effect
        g_w_relayed++;
        return;
    }
    if (game::effect_foreign(fx)) { // the skin painter's own effect world: never a world effect to relay
        note_foreign_id(id);
        g_w_foreign++;
        net::Vec3 fp;
        if (game::effect_position(fx, fp) && C.near_local && C.near_local(fp))
            g_w_foreign_near++; // one made near the local player (the kind that faulted the partner, BUG-0451)
        if (g_log)
            H->log(M, SK_INFO, "effect: world id %08X dropped, made in another effect world (skin painter)", id);
        return;
    }
    // the position the game's transform setter put into effect+0x9C. Animation-event effects are
    // skipped in the hook (0x9FBF00 sets a thread-local flag): the animation relay shows those.
    net::Vec3 p;
    if (!game::effect_position(fx, p)) {
        g_w_no_pos++;
        if (g_log)
            H->log(M, SK_INFO, "effect: world id %08X dropped, no position at %p", id, fx);
        return;
    }
    bool held = game::effect_cell_held(fx);
    note_recent('m', C.sess->my_id(), id, p, fx_scale(fx), held); // tests (BUG-1243): every world effect this game made
    if (g_held_fix && held) { // BUG-1266: the partner's game draws the cell's own effects itself
        g_held_tx++;
        return;
    }
    if (g_own_only_fix && own_only(id)) { // BUG-1243: the own cell's hatch is no picture for a partner
        g_own_only_held_tx++;
        return;
    }
    if (g_watch_id && id == g_watch_id) {
        g_watch_own = fx;
        g_watch_own_at = now();
    }
    if (g_world_start)
        g_world_start(C.sess->my_id(), id, fx, p);
    if (!C.near_local || !C.near_local(p)) {
        g_w_far++;
        if (g_log)
            H->log(M, SK_INFO, "effect: world id %08X at %.2f %.2f %.2f is not on the local player", id, p.x, p.y, p.z);
        return;
    }
    if (stage_off())
        return;
    if (nobody_here()) {
        g_elsewhere_tx++;
        return;
    }
    if (world_sent_seen(fx, id)) {
        g_w_seen++;
        return;
    }
    double t = now();
    if (!take_token(t)) {
        g_dropped++;
        g_w_rate++;
        return;
    }
    world_sent_add(fx, id);
    g_world++;
    g_last_tx = Last{};
    g_last_tx.id = id;
    g_last_tx.key = 0;
    g_last_tx.pool = 5;
    g_last_tx.at = t;
    g_sent++;
    if (g_log)
        H->log(M, SK_INFO, "effect: sent world id %08X at %.2f %.2f %.2f", id, p.x, p.y, p.z);
    note_recent('s', C.sess->my_id(), id, p, fx_scale(fx), held);
    uint8_t xf[0x44];
    if (copy_transform(xf, fx, p))
        g_xform_sent++;
    else {
        g_xform_bad++;
        make_transform(xf, p);
    }
    net::Writer w;
    w.u8(C.sess->my_id());
    w.u8(0);
    w.u8(5);
    w.u32(id);
    w.u32(0);
    w.raw(xf, 0x44);
    w.f32(sender_agl(p)); // BUG-0608: its height over this game's ground
    C.sess->send_event(net::MSG_EFFECT, w);
}

// forget a listed effect and drop our reference on it. We hold that reference until here, so the
// object is still alive even when the effect ended by itself (IsActive false): IsActive and Stop on
// a listed effect are safe. After the Release the object may be destroyed, so nothing may use the
// pointer afterwards.
static void live_forget(void* fx, bool release)
{
    if (release && g_release_fix && game::release_world_effect(fx))
        g_live_released++;
}

static void live_add(void* fx, double t)
{
    if (!fx)
        return;
    if (g_live_n >= kLiveMax) { // more than 64 live: stop and forget the oldest
        if (game::effect_active(g_live[0].fx) && game::stop_world_effect(g_live[0].fx))
            g_live_stopped++;
        live_forget(g_live[0].fx, true);
        memmove(g_live, g_live + 1, (g_live_n - 1) * sizeof g_live[0]);
        g_live_n--;
    }
    g_live[g_live_n++] = {fx, t};
}

static void live_tick(double t, uint32_t mode, uint32_t world)
{
    if (!g_live_init || mode != g_live_mode || world != g_live_world) {
        // Stage / world change: the game's effect manager tore its effects down. Do NOT call into
        // them (Stop, IsActive or Release: the world may have destroyed their internals already),
        // just drop the pointers; the references we held stay with those objects (the game's
        // teardown path; never Release here, BUG-0451 review item 7).
        g_live_dropped += (uint32_t)g_live_n;
        g_live_n = 0;
        g_live_mode = mode;
        g_live_world = world;
        g_live_init = true;
        return;
    }
    size_t w = 0;
    for (size_t i = 0; i < g_live_n; i++) {
        Live e = g_live[i];
        bool active = game::effect_active(e.fx);
        if (!active) { // it ended by itself: forget it and drop our reference
            live_forget(e.fx, true);
            continue;
        }
        if (t - e.at >= kLiveSec) {
            if (game::stop_world_effect(e.fx))
                g_live_stopped++;
            g_live_expired++;
            live_forget(e.fx, true);
            continue;
        }
        g_live[w++] = e;
    }
    g_live_n = w;
}

// the workload of the frame-time measurement (out/REPORT.md): every period the game creates `n`
// effects at the local player, which the relay hooks see like effects of the game's own code
static uint32_t g_load_n;
static uint32_t g_load_ms = 1000;
static uint32_t g_load_id; // 0: the last effect id the game created (a valid id in this stage)
static uint32_t g_load_ok, g_load_failed;
static double g_load_at;
static bool g_load_fix = true; // coop.fix effects.load_stop off: the load's effects are never stopped (BUG-1268)
COOP_FIX("effects.load_stop", "BUG-1268", g_load_fix, "effects: the test workload `coop.effects load` stops its effects after 8 s (they stayed at the player for the session)");

// BUG-1268: the load's own effects, kept apart from the relay's live list (their upkeep is the workload's cost, not
// the relay's: effects_party measures the relay under 1 %). Stopped and released after 8 s or at the cap, dropped
// without a call on a stage or world change (the game tore them down), as the live list does.
static Live g_lload[kLiveMax];
static size_t g_lload_n;
static uint32_t g_lload_mode, g_lload_world, g_lload_stopped;

static void lload_drop(size_t i, bool stop)
{
    void* fx = g_lload[i].fx;
    if (stop && game::effect_active(fx) && game::stop_world_effect(fx))
        g_lload_stopped++;
    game::release_world_effect(fx);
}

static void lload_tick(double t)
{
    uint32_t mode = game::mode_id(), world = C.world ? C.world() : 0;
    if (mode != g_lload_mode || world != g_lload_world) {
        g_lload_n = 0;
        g_lload_mode = mode;
        g_lload_world = world;
        return;
    }
    size_t w = 0;
    for (size_t i = 0; i < g_lload_n; i++) {
        if (!game::effect_active(g_lload[i].fx)) {
            lload_drop(i, false);
            continue;
        }
        if (t - g_lload[i].at >= kLiveSec) {
            lload_drop(i, true);
            continue;
        }
        g_lload[w++] = g_lload[i];
    }
    g_lload_n = w;
}

static void lload_add(void* fx, double t)
{
    if (!g_lload_n) { // the world the list's effects live in
        g_lload_mode = game::mode_id();
        g_lload_world = C.world ? C.world() : 0;
    }
    if (g_lload_n >= kLiveMax) {
        lload_drop(0, true);
        memmove(g_lload, g_lload + 1, (g_lload_n - 1) * sizeof g_lload[0]);
        g_lload_n--;
    }
    g_lload[g_lload_n++] = {fx, t};
}

static void load_tick(double t)
{
    if (g_lload_n)
        lload_tick(t);
    if (!g_load_n || t < g_load_at || !C.local_pos)
        return;
    g_load_at = t + g_load_ms / 1000.0;
    if (g_load_at < t)
        g_load_at = t + 1.0;
    uint32_t id = g_load_id ? g_load_id : game::last_effect_id(true);
    if (!id) {
        if (g_log)
            H->log(M, SK_INFO, "effect: perf load waits for the game to create an effect (no id yet)");
        return;
    }
    net::Vec3 p = C.local_pos();
    uint8_t xf[0x44];
    make_transform(xf, p);
    for (uint32_t i = 0; i < g_load_n; i++) {
        void* fx = game::create_local_effect(id, xf);
        if (fx) {
            g_load_ok++;
            // BUG-1268: kept nowhere before, a looping id stayed at the player for the rest of the session: on the
            // host of the lab a white disc over its creature in the next tests of a warm game
            if (g_load_fix)
                lload_add(fx, t);
        } else {
            g_load_failed++;
        }
    }
}

// the cost of this module on the main thread, printed in coop.status and once a minute as a
// `perf:` line in the game log (the frame-time numbers in out/REPORT.md)
static double g_tick_sum, g_tick_max;
static uint64_t g_tick_frames;
static double g_perf_at;
static double g_started_at;
static uint32_t g_perf_creates, g_perf_probes, g_perf_evicted, g_perf_sent, g_perf_mgr, g_perf_world, g_perf_anim;
static double g_perf_hook_sum;
static uint64_t g_perf_hook_calls;

// BUG-0527: the parts of the tick, so a test can tell the relay's own cost from the test workload's game calls
// (load_tick: `coop.effects load`, the game's CreateEffect at the player) and from the machine's load. Wall ms per
// part; the relay part (the tick without load_tick) split into frames under SLOW_S and slow frames. Measured
// 2026-11-04 (lane 4, load average 8-12): 29-35 slow frames in 20 s (5-822 ms each) held 2.6-5.3 % of the main
// thread's wall time but no more than 0-5 jiffies of the thread's CPU time (GetThreadTimes, 10 ms steps under
// Wine): the OS preempted those ticks, the relay did not spend them. Under a lighter load (5.8) 0-1 slow frames.
// The thread's CPU time itself is no measure here: its 10 ms step is a whole frame, and the call costs ~50 us.
struct Part {
    double sum = 0, max = 0;
    uint64_t n = 0;
    void add(double dt, uint64_t k = 1)
    {
        sum += dt;
        max = dt > max ? dt : max;
        n += k;
    }
};
static const double SLOW_S = 0.005;
static Part g_p_load, g_p_rx, g_p_live, g_p_rest, g_p_relay, g_p_fast, g_p_slow;

void tick(double)
{
    double t0 = H ? H->time_seconds() : 0;
    game::set_effect_relay_enabled(g_fix); // also notes the game's main thread id
    double load_wall = 0;
    if (g_fix && H && (g_load_n || g_lload_n)) {
        double a = H->time_seconds();
        load_tick(t0);
        load_wall = H->time_seconds() - a;
        g_p_load.add(load_wall);
    } else if (g_fix) {
        load_tick(t0);
    }
    double t1 = H ? H->time_seconds() : 0;
    Pending p;
    uint64_t rx = 0;
    while (q_pop(p)) {
        rx++;
        if (p.hit)
            process_hit(p.id, p.pos);
        else if (p.world)
            process_world(p.fx, p.id);
        else
            process_local(p.creature, p.ev);
    }
    double t2 = H ? H->time_seconds() : 0;
    live_tick(t0, game::mode_id(), C.world ? C.world() : 0);
    double t3 = H ? H->time_seconds() : 0;
    world_sent_clear();
    game::clear_effect_table();
    if (!H)
        return;
    double dt = H->time_seconds() - t0;
    if (rx)
        g_p_rx.add(t2 - t1, rx);
    g_p_live.add(t3 - t2);
    g_p_rest.add(dt - (t3 - t0));
    double relay = dt - load_wall;
    g_p_relay.add(relay);
    (relay < SLOW_S ? g_p_fast : g_p_slow).add(relay);
    g_tick_sum += dt;
    g_tick_max = dt > g_tick_max ? dt : g_tick_max;
    g_tick_frames++;
    if (g_perf_at <= 0)
        g_perf_at = t0;
    if (t0 - g_perf_at >= 60) {
        double wall = t0 - g_perf_at;
        g_perf_at = t0;
        uint32_t c = 0, pr = 0, ev = 0, mg = 0, wo = 0, an = 0;
        game::effect_table_stats(c, pr, ev, mg, wo, an);
        double hs = 0, hm = 0;
        uint64_t hc = 0;
        game::effect_hook_cost(hs, hm, hc);
        double dh = hs - g_perf_hook_sum;
        uint64_t dc = hc - g_perf_hook_calls;
        H->log(M, SK_INFO,
               "perf: effects hooks %.4f %% of the main thread (%.3f ms work over %.0f s, %llu calls, avg %.3f us, "
               "max %.3f us); tick avg %.4f max %.3f ms/frame over %llu frames; hooks %u creates (mgr %u, world %u, "
               "animation %u skipped), %u table probes, %u evicted; sent %u; load %u effects per %u ms",
               wall > 0 ? dh / wall * 100 : 0.0, dh * 1000, wall, (unsigned long long)dc,
               dc ? dh / dc * 1e6 : 0.0, hm * 1e6, g_tick_frames ? g_tick_sum / g_tick_frames * 1000 : 0.0,
               g_tick_max * 1000, (unsigned long long)g_tick_frames, c - g_perf_creates, mg - g_perf_mgr,
               wo - g_perf_world, an - g_perf_anim, pr - g_perf_probes, ev - g_perf_evicted,
               g_sent - g_perf_sent, g_load_n, g_load_ms);
        g_tick_sum = g_tick_max = 0;
        g_tick_frames = 0;
        g_perf_creates = c;
        g_perf_probes = pr;
        g_perf_evicted = ev;
        g_perf_sent = g_sent;
        g_perf_mgr = mg;
        g_perf_world = wo;
        g_perf_anim = an;
        g_perf_hook_sum = hs;
        g_perf_hook_calls = hc;
    }
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_EFFECT)
        return;
    uint8_t player = r.u8(), stop = r.u8(), pool = r.u8();
    uint32_t id = r.u32(), key = r.u32();
    uint8_t xf[0x44];
    float agl = net::kNoAgl;
    if (r.ok() && pool == 5) {
        if (r.left() < sizeof xf) {
            g_bad_short++;
            return;
        }
        r.raw(xf, sizeof xf);
        if (r.ok() && r.left() >= 4) { // BUG-0608 (an older sender has none)
            agl = r.f32();
            if (!std::isfinite(agl) || std::fabs(agl) > kGroundAglMax)
                agl = net::kNoAgl;
        }
    }
    if (!r.ok()) {
        g_bad_short++;
        return;
    }
    // Whose message is it? The host relays only a guest's own effect (a guest cannot name another
    // player, BUG-0201: the id would choose whose puppet plays it); a guest accepts only the host's
    // relay (messages are never sent guest to guest).
    net::Role role = C.sess ? C.sess->role() : net::Role::None;
    if (role == net::Role::Host ? (from != 0 && player != from) : (from != 0)) {
        g_dropped_spoof++;
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u dropped, it names player %u (spoofed)", from, player);
        return;
    }
    if (!valid_message(player, stop, pool, id, key, xf)) {
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u dropped by validation (player %u stop %u pool %u id %08X key %08X)",
                   from, player, stop, pool, id, key);
        return;
    }
    if (C.sess && role == net::Role::Host && from != 0) { // relay to the other guests
        net::Writer w;
        w.u8(player);
        w.u8(stop);
        w.u8(pool);
        w.u32(id);
        w.u32(key);
        if (pool == 5) {
            w.raw(xf, sizeof xf); // the world effect's transform, or guest 3+ never gets the position
            w.f32(agl);           // BUG-0608
        }
        C.sess->broadcast_reliable(net::MSG_EFFECT, w, from);
        if (pool == 3 && !stop && g_bone_local_fix)
            g_relayed_unchecked++;
    }
    g_rx++;
    if (!g_fix)
        return;
    if (!stop && stage_off())
        return;
    if (!stop && C.here && !C.here(player)) {
        g_elsewhere_rx++;
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u skipped, not in this place (id %08X pool %u)", player, id, pool);
        return;
    }
    if (!stop && pool == 5 && g_own_only_fix && own_only(id)) { // BUG-1243: an older partner's hatch
        g_own_only_held_rx++;
        return;
    }
    if (!stop && pool == 5 && g_held_fix && held_id(id) && game::mode_id() == modes::kCell) { // BUG-1266: an older partner's
        g_held_rx++;
        return;
    }
    if (!stop && pool == 5 && foreign_id(id)) {
        g_rx_foreign++;
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u world id %08X not played: a skin painter effect here", player, id);
        return;
    }
    void* puppet = pool == 5 ? reinterpret_cast<void*>(1) : partner_puppet(player);
    if (!puppet && pool != 5 && g_puppet_live_fix && C.puppet_of && C.puppet_of(player)) {
        g_puppet_gone++; // BUG-0795: a puppet that is no live creature of this game now (freed, a cell, a marker)
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u skipped, its puppet is no live creature here (id %08X key %08X)", player, id, key);
        return;
    }
    if (!puppet) {
        g_skipped++;
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u skipped, no puppet (id %08X key %08X)", player, id, key);
        return;
    }
    if (!stop && pool == 3 && g_bone_local_fix && !game::creature_bone_ok(puppet, (int32_t)key)) {
        g_bad_bone++; // the game indexes the model's bone array with it unchecked (CreateEffect 0xC150D0)
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u dropped, bone %u not on its puppet here (id %08X)", player, key, id);
        return;
    }
    game::EffectEvent ev{id, key, pool, stop != 0};
    Last last;
    last.id = id;
    last.key = key;
    last.pool = pool;
    last.stop = ev.stop;
    last.at = now();
    if (ev.stop) {
        if (game::stop_effect(puppet, key)) {
            g_stopped++;
        } else {
            g_failed++; // the partner's game had no such effect (yet): nothing to stop
        }
    } else {
        if (pool == 5 && onto_own_ground(xf, agl))
            g_ground_moved++;
        void* fx = pool == 5 ? game::create_world_effect(id, xf)
                  : pool == 3 ? game::create_creature_effect(puppet, (int)key, id)
                              : game::start_effect(puppet, ev);
        if (fx) {
            g_played++;
            last.game = true;
            last.active = game::effect_active(fx);
            if (pool == 5) {
                live_add(fx, now());
                if (g_watch_id && id == g_watch_id) {
                    g_watch_rx = fx;
                    g_watch_rx_at = now();
                }
                // where the game put it (its transform at effect +0x9C, BUG-0608), else what was sent
                const float* p = skg::readable((uint8_t*)fx + 0xA0, 12) ? (const float*)((uint8_t*)fx + 0xA0)
                                                                         : (const float*)(xf + 4);
                last.has_pos = true;
                last.x = p[0];
                last.y = p[1];
                last.z = p[2];
                if (g_world_start)
                    g_world_start(player, id, fx, net::Vec3{p[0], p[1], p[2]});
                note_recent('p', player, id, net::Vec3{p[0], p[1], p[2]}, fx_scale(fx));
            } else if (pool == 3) { // a one-shot effect has no pool entry: the game created it on the puppet
                net::Vec3 p = game::position(puppet);
                last.has_pos = true;
                last.x = p.x;
                last.y = p.y;
                last.z = p.z;
            } else if (skg::readable((uint8_t*)fx + 0xA0, 12)) {
                const float* p = (const float*)((uint8_t*)fx + 0xA0); // the Vector3 at effect +0x9C
                last.has_pos = true;
                last.x = p[0];
                last.y = p[1];
                last.z = p[2];
            }
        } else {
            g_failed++;
        }
    }
    g_last_rx[player] = last;
    if (!ev.stop)
        g_last_rx_start[player] = last;
    g_last_rx_from = player;
    if (g_log)
        H->log(M, SK_INFO, "effect: from %u played on %s id %08X key %08X pool %u stop %d game %d active %d",
               player, who(puppet), id, key, pool, ev.stop ? 1 : 0, last.game ? 1 : 0, last.active ? 1 : 0);
}

void inject(uint8_t from, uint8_t player, uint8_t stop, uint8_t pool, uint32_t id, uint32_t key,
            const net::Vec3& pos, float agl)
{
    net::Writer w;
    w.u8(player);
    w.u8(stop);
    w.u8(pool);
    w.u32(id);
    w.u32(key);
    if (pool == 5) {
        uint8_t xf[0x44];
        make_transform(xf, pos);
        w.raw(xf, sizeof xf);
        if (net::agl_known(agl))
            w.f32(agl);
    }
    net::Reader r(w.data.data(), w.size());
    on_message(net::MSG_EFFECT, from, r);
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    g_tokens = 20.0;
    g_tokens_at = 0;
    g_sent = g_rx = g_played = g_stopped = g_skipped = g_failed = g_dropped = g_puppet_gone = 0;
    g_w_no_pos = g_w_relayed = g_w_far = g_w_seen = g_w_rate = 0;
    g_elsewhere_tx = g_elsewhere_rx = 0;
    g_w_foreign = g_w_foreign_near = g_rx_foreign = 0;
    g_dropped_spoof = 0;
    g_bad_pool = g_bad_id = g_bad_bone = g_bad_xf = g_bad_short = 0;
    g_last_tx = Last{};
    g_last_rx.clear();
    g_last_rx_start.clear();
    g_last_rx_from = 0xFF;
    g_sent_n = 0;
    world_sent_clear();
    g_world_sent_probes = 0;
    g_world = 0;
    g_tick_sum = g_tick_max = 0;
    g_tick_frames = 0;
    g_perf_at = g_started_at = now();
    g_perf_hook_sum = 0;
    g_perf_hook_calls = 0;
    g_load_n = g_load_ms = g_load_id = g_load_ok = g_load_failed = 0;
    g_lload_n = 0; // dropped without a call (as the live list on a world change)
    g_lload_stopped = 0;
    g_perf_creates = g_perf_probes = g_perf_evicted = g_perf_sent = 0;
    g_live_n = 0;
    g_live_stopped = g_live_expired = g_live_released = g_live_dropped = 0;
    g_live_mode = g_live_world = 0;
    g_live_init = false;
    game::set_effect_relay_enabled(g_fix);
    game::set_effect_listener(on_local_effect);
    game::set_world_effect_listener(on_world_effect);
    game::set_hit_effect_listener(on_hit_effect);
}

void on_session_end()
{
    g_last_rx.clear();
    g_last_rx_start.clear();
    g_last_rx_from = 0xFF;
    g_sent_n = 0;
    world_sent_clear();
    // the session ends: stop and release what the relay holds, but only while the world those effects
    // live in is still the one being played (a session can end during a world teardown: then the list
    // is dropped like on a stage change, never called into)
    bool same_world = g_live_init && game::mode_id() == g_live_mode && (C.world ? C.world() : 0) == g_live_world;
    for (size_t i = 0; i < g_live_n && same_world; i++) {
        if (game::effect_active(g_live[i].fx) && game::stop_world_effect(g_live[i].fx))
            g_live_stopped++;
        live_forget(g_live[i].fx, true);
    }
    if (!same_world)
        g_live_dropped += (uint32_t)g_live_n;
    g_live_n = 0;
}
LIFECYCLE_HOOK("effects.session", lifecycle::kSessionEnd, -78, [](const lifecycle::Ctx&) { on_session_end(); }, true); // C2 (BUG-1141): end_session

bool set_log(bool on)
{
    g_log = on;
    game::set_effect_log(on);
    return g_log;
}

// tests (BUG-1252): the rate limit's bucket in debt by n tokens, as a burst of effects leaves it (n / 40 s to refill)
void test_no_puppet(uint8_t player, bool on)
{
    if (player < 32)
        g_test_no_puppet = on ? g_test_no_puppet | 1u << player : g_test_no_puppet & ~(1u << player);
}

void drain_tokens(double n)
{
    g_tokens = -n;
    g_tokens_at = now();
}

void set_load(uint32_t n, uint32_t period_ms, uint32_t id)
{
    g_load_n = n;
    g_load_ms = period_ms ? period_ms : 1000;
    g_load_at = now();
    g_load_id = id;
    g_load_ok = g_load_failed = 0;
}

static std::string pool_map(void* c, uint32_t off)
{
    std::string s;
    char b[220];
    if (!c || !skg::readable((uint8_t*)c + off, 0x34))
        return "map: unreadable\n";
    uint8_t* m = (uint8_t*)c + off;
    uint32_t count = *(uint32_t*)(m + 0xC);
    void** buckets = *(void***)(m + 4);
    uint32_t nb = *(uint32_t*)(m + 8);
    snprintf(b, sizeof b, "map +%03X: count=%u buckets=%u at %p\n", off, count, nb, (void*)buckets);
    s += b;
    if (!buckets || nb > 4096 || !skg::readable(buckets, (size_t)nb * 4))
        return s + "map: bad buckets\n";
    unsigned n = 0;
    for (uint32_t i = 0; i < nb && n < 40; i++) {
        uint8_t* node = (uint8_t*)buckets[i];
        while (node && n < 40) {
            if (!skg::readable(node, 12)) {
                s += "map: bad node\n";
                return s;
            }
            // BUG-1241 (tests): whether the pool's effect is still active (IVisualEffect vt 0x10)
            snprintf(b, sizeof b, "  b%u key=%08X value=%p active=%d\n", i, *(uint32_t*)node, *(void**)(node + 4),
                     game::effect_active(*(void**)(node + 4)) ? 1 : 0);
            s += b;
            n++;
            node = *(uint8_t**)(node + 8);
        }
    }
    return s;
}

std::string map_dump()
{
    std::string s;
    char b[160];
    if (void* av = game::avatar()) {
        snprintf(b, sizeof b, "avatar %p\n", av);
        s += b;
        s += pool_map(av, 0xCC8);
        s += pool_map(av, 0xD68);
    }
    if (C.all_puppets)
        for (const auto& [id, puppet] : C.all_puppets()) {
            if (!puppet || (g_puppet_live_fix && partner_puppet(id) != puppet)) // (BUG-0795, BUG-0919)
                continue;
            snprintf(b, sizeof b, "puppet %u %p\n", id, puppet);
            s += b;
            s += pool_map(puppet, 0xCC8);
            s += pool_map(puppet, 0xD68);
        }
    return s;
}

std::string probe(uint32_t key)
{
    std::string s;
    char b[220];
    void* av = game::avatar();
    if (av) {
        void* fx = game::effect_from_pools(av, key);
        snprintf(b, sizeof b, "probe avatar key=%08X game=%p active=%d\n", key, fx, game::effect_active(fx) ? 1 : 0);
        s += b;
    }
    if (C.all_puppets)
        for (const auto& [id, puppet] : C.all_puppets()) {
            if (!puppet || (g_puppet_live_fix && partner_puppet(id) != puppet)) // (BUG-0795, BUG-0919)
                continue;
            void* fx = game::effect_from_pools(puppet, key);
            snprintf(b, sizeof b, "probe %u key=%08X game=%p active=%d\n", id, key, fx, game::effect_active(fx) ? 1 : 0);
            s += b;
        }
    return s;
}

std::string watch(uint32_t id)
{
    if (id)
        g_watch_id = id, g_watch_own = g_watch_rx = nullptr;
    std::string out;
    char b[300];
    auto one = [&](const char* who, void* fx, double at) {
        if (!fx || now() - at > 30.0 || !skg::readable((uint8_t*)fx + 0x9C, 0x38)) {
            snprintf(b, sizeof b, "%s none\n", who);
            out += b;
            return;
        }
        const uint8_t* t = (const uint8_t*)fx + 0x9C;
        const float* f = (const float*)(t + 4);
        snprintf(b, sizeof b, "%s fx=%p age=%.1f active=%d flags=%04X pos=(%.2f %.2f %.2f) scale=%.3f rot=(%.3f %.3f %.3f | %.3f %.3f "
                 "%.3f | %.3f %.3f %.3f)\n", who, fx, now() - at, (int)game::effect_active(fx), *(const uint16_t*)t, f[0], f[1],
                 f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12]);
        out += b;
    };
    snprintf(b, sizeof b, "watch %08X xform sent=%u bad=%u\n", g_watch_id, g_xform_sent, g_xform_bad);
    out += b;
    one("own", g_watch_own, g_watch_own_at);
    one("rx", g_watch_rx, g_watch_rx_at);
    return out;
}

std::string recent()
{
    std::string s;
    char b[200];
    double t = now();
    for (size_t i = 0; i < g_recent_n; i++) {
        const Recent& r = g_recent[(g_recent_at + kRecent - g_recent_n + i) % kRecent];
        snprintf(b, sizeof b, "recent %c from=%u id=%08X at=(%.2f %.2f %.2f) local_d=%.2f age=%.1f scale=%.3f held=%d\n", r.dir,
                 r.player, r.id, r.x, r.y, r.z, r.d, t - r.at, r.scale, r.held ? 1 : 0);
        s += b;
    }
    return s.empty() ? std::string("recent: none\n") : s;
}

std::string status()
{
    std::string s;
    char b[480];
    snprintf(b, sizeof b,
             "effects: fix=%d sent=%u received=%u played=%u stopped=%u skipped=%u failed=%u dropped_rate=%u world=%u gone=%u "
             "stops_past_rate=%u\n",
             (int)g_fix, g_sent, g_rx, g_played, g_stopped, g_skipped, g_failed, g_dropped, g_world, g_puppet_gone,
             g_stops_unlimited);
    s += b;
    uint32_t creates = 0, probes = 0, evicted = 0, mgr = 0, world = 0, anim = 0;
    game::effect_table_stats(creates, probes, evicted, mgr, world, anim);
    snprintf(b, sizeof b,
             "effects cost: tick avg %.4f max %.3f ms/frame over %llu frames; hooks %u creates (mgr %u, world %u, "
             "animation %u skipped), %u table probes, %u evicted; last game effect id %08X; load %u per %u ms: "
             "%u created, %u failed\n",
             g_tick_frames ? g_tick_sum / g_tick_frames * 1000 : 0.0, g_tick_max * 1000,
             (unsigned long long)g_tick_frames, creates, mgr, world, anim, probes, evicted, game::last_effect_id(true),
             g_load_n, g_load_ms, g_load_ok, g_load_failed);
    s += b;
    snprintf(b, sizeof b,
             "effects cost parts: load %.3f ms (%llu, max %.3f) rx %.3f ms (%llu msgs, max %.3f) live %.3f ms (max %.3f) "
             "rest %.3f ms; relay %.3f ms (max %.3f) in %llu frames: under 5 ms %.3f ms, slow %llu frames %.3f ms\n",
             g_p_load.sum * 1000, (unsigned long long)g_p_load.n, g_p_load.max * 1000, g_p_rx.sum * 1000,
             (unsigned long long)g_p_rx.n, g_p_rx.max * 1000, g_p_live.sum * 1000, g_p_live.max * 1000,
             g_p_rest.sum * 1000, g_p_relay.sum * 1000, g_p_relay.max * 1000, (unsigned long long)g_p_relay.n,
             g_p_fast.sum * 1000, (unsigned long long)g_p_slow.n, g_p_slow.sum * 1000);
    s += b;
    uint32_t hc = 0, hret = 0, hfx = 0;
    game::hit_effect_calls(hc, hret, hfx);
    snprintf(b, sizeof b, "effects hits: seen=%u sent=%u off=%u rate=%u (event effects %u, on the animating object %u, last sent id %08X)\n",
             game::hit_effects_seen(), g_hit_sent, g_hit_off, g_hit_rate, hc, hfx, hret);
    s += b;
    snprintf(b, sizeof b, "effects world drops: no_pos=%u relayed=%u far=%u seen=%u rate=%u sent_probes=%u\n",
             g_w_no_pos, g_w_relayed, g_w_far, g_w_seen, g_w_rate, g_world_sent_probes);
    s += b;
    snprintf(b, sizeof b, "effects received drops: spoof=%u pool=%u id=%u bone=%u transform=%u short=%u relayed_unchecked=%u no_puppet_test=%X\n",
             g_dropped_spoof, g_bad_pool, g_bad_id, g_bad_bone, g_bad_xf, g_bad_short, g_relayed_unchecked, g_test_no_puppet);
    s += b;
    snprintf(b, sizeof b, "effects onto own ground: %u\n", g_ground_moved); // BUG-0608
    s += b;
    snprintf(b, sizeof b, "effects cell own only: fix=%d made=%u held_tx=%u held_rx=%u\n", (int)g_own_only_fix, g_own_only_made,
             g_own_only_held_tx, g_own_only_held_rx); // BUG-1243
    s += b;
    snprintf(b, sizeof b, "effects cell held: fix=%d made=%u not_sent=%u not_played=%u\n", (int)g_held_fix, game::cell_held_effects(),
             g_held_tx, g_held_rx); // BUG-1266
    s += b;
    snprintf(b, sizeof b, "effects elsewhere: not sent=%u not played=%u other_stages=%d space=%d stage_skipped=%u\n",
             g_elsewhere_tx, g_elsewhere_rx, (int)g_other_stages, (int)g_space, g_stage_skipped);
    s += b;
    snprintf(b, sizeof b,
             "effects foreign world: created=%u last=%08X queued=%u near=%u not played=%u known ids=%u\n",
             game::foreign_effects(), game::last_foreign_effect_id(), g_w_foreign, g_w_foreign_near, g_rx_foreign,
             (unsigned)g_foreign_n);
    s += b;
    snprintf(b, sizeof b,
             "effects world live: live=%u stopped=%u expired=%u released=%u dropped=%u release_fix=%d\n",
             (unsigned)g_live_n, g_live_stopped, g_live_expired, g_live_released, g_live_dropped,
             (int)g_release_fix);
    s += b;
    snprintf(b, sizeof b, "effects load live: live=%u stopped=%u fix=%d\n", (unsigned)g_lload_n, g_lload_stopped,
             (int)g_load_fix); // BUG-1268
    s += b;
    {
        double hs = 0, hm = 0;
        uint64_t hc = 0;
        game::effect_hook_cost(hs, hm, hc);
        double wall = g_started_at > 0 ? now() - g_started_at : 0;
        snprintf(b, sizeof b,
                 "effects hook work: %.3f ms over %.1f s (%.4f %% of the main thread), %llu calls, avg %.3f us, "
                 "max %.3f us\n",
                 hs * 1000, wall, wall > 0 ? hs / wall * 100 : 0.0, (unsigned long long)hc,
                 hc ? hs / hc * 1e6 : 0.0, hm * 1e6);
        s += b;
    }
    if (g_last_tx.at > 0) {
        snprintf(b, sizeof b, "effects sent last: id=%08X key=%08X pool=%u stop=%d age=%.1fs\n", g_last_tx.id,
                 g_last_tx.key, g_last_tx.pool, g_last_tx.stop ? 1 : 0, now() - g_last_tx.at);
        s += b;
    }
    for (auto& [id, l] : g_last_rx_start) {
        snprintf(b, sizeof b, "effects received start from %u: id=%08X key=%08X pool=%u game=%d active=%d age=%.1fs%s\n",
                 id, l.id, l.key, l.pool, l.game ? 1 : 0, l.active ? 1 : 0, now() - l.at, l.has_pos ? "" : " at none");
        s += b;
        if (l.has_pos) {
            snprintf(b, sizeof b, "effects received start at: %.2f %.2f %.2f\n", l.x, l.y, l.z);
            s += b;
        }
    }
    if (g_last_rx_from != 0xFF) {
        const Last& l = g_last_rx[g_last_rx_from];
        snprintf(b, sizeof b, "effects received last from %u: id=%08X key=%08X pool=%u stop=%d game=%d active=%d age=%.1fs%s\n",
                 g_last_rx_from, l.id, l.key, l.pool, l.stop ? 1 : 0, l.game ? 1 : 0, l.active ? 1 : 0, now() - l.at,
                 l.has_pos ? "" : " at none");
        s += b;
        if (l.has_pos) {
            snprintf(b, sizeof b, "effects received last at: %.2f %.2f %.2f\n", l.x, l.y, l.z);
            s += b;
        }
    }
    return s;
}

} // namespace effects
