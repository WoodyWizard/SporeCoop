#include "effects.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <utility>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/protocol.h"
#include "sporekit_game.hpp"

namespace effects {

static const SkHost* H;
static SkMod* M;
static Ctx C;

// BUG-0451: the whole class of creature effects, relayed from the game's one effect path.
static bool g_fix = true;
COOP_FIX("creature.effects_relay", "BUG-0451", g_fix,
         "creature: every effect a player's creature makes (hearts, social, attack, ...) shows on the partner's puppet");

// BUG-0471: an effect is sent only while a partner plays in the same place, and a received start
// is played only while its owner does (a guest in the main menu, an editor, a loading screen or at
// another star does not create the host's effects in whatever world its game has then). Off: the
// old behaviour (sent and played in any place).
static bool g_here_fix = true;
COOP_FIX("effects.here", "BUG-0471", g_here_fix,
         "effects: relayed only between players in the same place (not into a menu, editor or loading screen)");
static uint32_t g_elsewhere_tx, g_elsewhere_rx;
static bool nobody_here() { return g_here_fix && C.anyone_here && !C.anyone_here(); }

// BUG-0451 (Tribe / Civilization): 0.7.4 relayed effect starts only in Cell and Creature, because a relayed world
// effect made the receivers fault hundreds of times in Skinner::cSkinnerTexturePainter::StartRender+0x28
// (effects_party_civ out/lab/20261103-215559, effects_party_tribe out/lab/20261103-223036). The cause: the
// skin painter (Skinner::cSkinPainterPipeline::ExecutePipeline, it paints a creature's skin when a model is
// built, e.g. a partner's puppet or a tribe member) creates its paint effects in an effect world of its own
// on the main thread; the world-effect hook saw them like any effect, and one made near the local player was
// sent. Created in the receiver's game effect world it has no painter and faults every frame while it lives
// (docs/re/creature.md#creature-effects-painter). `effects.main_world` (on) sends only effects of the game's
// own effect world and never plays an id this game saw only in another world; with it, Tribe and Civilization
// relay again (`effects.other_stages`, on). Off: the 0.7.4 behaviour (no effect starts there; stops pass).
static bool g_other_stages = true;
COOP_FIX("effects.other_stages", "BUG-0451", g_other_stages,
         "effects: also relayed in Tribe and Civilization (off: Cell and Creature only, as 0.7.4)");
// Space: not verified (no effect of the own UFO could be made in a test, docs/re/creature.md); off as in 0.7.4
static bool g_space = false;
COOP_FIX("effects.space", "BUG-0451", g_space, "effects: also relayed in Space (unverified, off by default)");
static bool g_main_world_fix = true;
COOP_FIX("effects.main_world", "BUG-0451", g_main_world_fix,
         "effects: the skin painter's own effects are never relayed (they faulted the partner's game in Tribe and Civ)");
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
        const float* f = (const float*)(xf + 4); // position, uniform scale, 3x3 matrix
        for (int i = 0; i < 16; i++) {
            float v;
            memcpy(&v, f + i, 4);
            if (!std::isfinite(v)) {
                g_bad_xf++;
                return false;
            }
        }
        if (!(fabsf(f[0]) < kPosMax && fabsf(f[1]) < kPosMax && fabsf(f[2]) < kPosMax)) {
            g_bad_xf++;
            return false;
        }
    } else if (pool == 3) {
        // the game indexes the model's bone array (stride 0x2bc = 700) with this index and does not
        // check it (cCreatureBase::CreateEffect 0xC150D0); the array's end gives the count
        void* puppet = C.puppet_of ? C.puppet_of(player) : nullptr;
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
// the pool keys we sent a start for: a stop is sent once, not on every frame the game stops a key.
// A fixed array: this is filled from inside the game's effect creation (no allocation there).
static uint32_t g_sent_keys[64];
static size_t g_sent_n;
static uint8_t g_last_rx_from = 0xFF;
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
static void on_world_effect(void* fx, uint32_t id)
{
    if (!fx || !id)
        return;
    Pending p;
    p.world = true;
    p.fx = fx;
    p.id = id;
    q_push(p);
}

static void process_local(void* creature, const game::EffectEvent& ev)
{
    if (!g_fix || !creature)
        return;
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
    if (!take_token(t)) {
        g_dropped++;
        if (g_log)
            H->log(M, SK_INFO, "effect: dropped by the rate limit: id %08X key %08X pool %u stop %d",
                   ev.id, ev.key, ev.pool, ev.stop ? 1 : 0);
        return;
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
            g_w_foreign_near++; // one a player's play would have sent without the fix
        if (g_main_world_fix) {
            if (g_log)
                H->log(M, SK_INFO, "effect: world id %08X dropped, made in another effect world (skin painter)", id);
            return;
        }
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
    uint8_t xf[0x44];
    make_transform(xf, p);
    net::Writer w;
    w.u8(C.sess->my_id());
    w.u8(0);
    w.u8(5);
    w.u32(id);
    w.u32(0);
    w.raw(xf, 0x44);
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

static void load_tick(double t)
{
    if (!g_load_n || t < g_load_at || !C.local_pos)
        return;
    g_load_at = t + g_load_ms / 1000.0;
    if (g_load_at < t)
        g_load_at = t + 1.0;
    uint32_t id = g_load_id ? g_load_id : game::last_effect_id(g_main_world_fix);
    if (!id) {
        if (g_log)
            H->log(M, SK_INFO, "effect: perf load waits for the game to create an effect (no id yet)");
        return;
    }
    net::Vec3 p = C.local_pos();
    uint8_t xf[0x44];
    make_transform(xf, p);
    for (uint32_t i = 0; i < g_load_n; i++) {
        if (game::create_local_effect(id, xf))
            g_load_ok++;
        else
            g_load_failed++;
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
    if (g_fix && H && g_load_n) {
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
        if (p.world)
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
    if (r.ok() && pool == 5) {
        if (r.left() < sizeof xf) {
            g_bad_short++;
            return;
        }
        r.raw(xf, sizeof xf);
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
        if (pool == 5)
            w.raw(xf, sizeof xf); // the world effect's transform, or guest 3+ never gets the position
        C.sess->broadcast_reliable(net::MSG_EFFECT, w, from);
    }
    g_rx++;
    if (!g_fix)
        return;
    if (!stop && stage_off())
        return;
    if (!stop && g_here_fix && C.here && !C.here(player)) {
        g_elsewhere_rx++;
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u skipped, not in this place (id %08X pool %u)", player, id, pool);
        return;
    }
    if (!stop && pool == 5 && g_main_world_fix && foreign_id(id)) {
        g_rx_foreign++;
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u world id %08X not played: a skin painter effect here", player, id);
        return;
    }
    void* puppet = pool == 5 ? reinterpret_cast<void*>(1) : (C.puppet_of ? C.puppet_of(player) : nullptr);
    if (!puppet) {
        g_skipped++;
        if (g_log)
            H->log(M, SK_INFO, "effect: from %u skipped, no puppet (id %08X key %08X)", player, id, key);
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
        void* fx = pool == 5 ? game::create_world_effect(id, xf)
                  : pool == 3 ? game::create_creature_effect(puppet, (int)key, id)
                              : game::start_effect(puppet, ev);
        if (fx) {
            g_played++;
            last.game = true;
            last.active = game::effect_active(fx);
            if (pool == 5) {
                live_add(fx, now());
                const float* p = (const float*)(xf + 4);
                last.has_pos = true;
                last.x = p[0];
                last.y = p[1];
                last.z = p[2];
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
            const net::Vec3& pos)
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
    g_sent = g_rx = g_played = g_stopped = g_skipped = g_failed = g_dropped = 0;
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
    g_perf_creates = g_perf_probes = g_perf_evicted = g_perf_sent = 0;
    g_live_n = 0;
    g_live_stopped = g_live_expired = g_live_released = g_live_dropped = 0;
    g_live_mode = g_live_world = 0;
    g_live_init = false;
    game::set_effect_relay_enabled(g_fix);
    game::set_effect_listener(on_local_effect);
    game::set_world_effect_listener(on_world_effect);
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

bool set_log(bool on)
{
    g_log = on;
    game::set_effect_log(on);
    return g_log;
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
            snprintf(b, sizeof b, "  b%u key=%08X value=%p\n", i, *(uint32_t*)node, *(void**)(node + 4));
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
            if (!puppet)
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
            if (!puppet)
                continue;
            void* fx = game::effect_from_pools(puppet, key);
            snprintf(b, sizeof b, "probe %u key=%08X game=%p active=%d\n", id, key, fx, game::effect_active(fx) ? 1 : 0);
            s += b;
        }
    return s;
}

std::string status()
{
    std::string s;
    char b[480];
    snprintf(b, sizeof b,
             "effects: fix=%d sent=%u received=%u played=%u stopped=%u skipped=%u failed=%u dropped_rate=%u world=%u\n",
             (int)g_fix, g_sent, g_rx, g_played, g_stopped, g_skipped, g_failed, g_dropped, g_world);
    s += b;
    uint32_t creates = 0, probes = 0, evicted = 0, mgr = 0, world = 0, anim = 0;
    game::effect_table_stats(creates, probes, evicted, mgr, world, anim);
    snprintf(b, sizeof b,
             "effects cost: tick avg %.4f max %.3f ms/frame over %llu frames; hooks %u creates (mgr %u, world %u, "
             "animation %u skipped), %u table probes, %u evicted; last game effect id %08X; load %u per %u ms: "
             "%u created, %u failed\n",
             g_tick_frames ? g_tick_sum / g_tick_frames * 1000 : 0.0, g_tick_max * 1000,
             (unsigned long long)g_tick_frames, creates, mgr, world, anim, probes, evicted, game::last_effect_id(g_main_world_fix),
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
    snprintf(b, sizeof b, "effects world drops: no_pos=%u relayed=%u far=%u seen=%u rate=%u sent_probes=%u\n",
             g_w_no_pos, g_w_relayed, g_w_far, g_w_seen, g_w_rate, g_world_sent_probes);
    s += b;
    snprintf(b, sizeof b, "effects received drops: spoof=%u pool=%u id=%u bone=%u transform=%u short=%u\n",
             g_dropped_spoof, g_bad_pool, g_bad_id, g_bad_bone, g_bad_xf, g_bad_short);
    s += b;
    snprintf(b, sizeof b, "effects elsewhere: not sent=%u not played=%u here_fix=%d other_stages=%d space=%d stage_skipped=%u\n",
             g_elsewhere_tx, g_elsewhere_rx, (int)g_here_fix, (int)g_other_stages, (int)g_space, g_stage_skipped);
    s += b;
    snprintf(b, sizeof b,
             "effects foreign world: created=%u last=%08X queued=%u near=%u not played=%u known ids=%u main_world_fix=%d\n",
             game::foreign_effects(), game::last_foreign_effect_id(), g_w_foreign, g_w_foreign_near, g_rx_foreign,
             (unsigned)g_foreign_n,
             (int)g_main_world_fix);
    s += b;
    snprintf(b, sizeof b,
             "effects world live: live=%u stopped=%u expired=%u released=%u dropped=%u release_fix=%d\n",
             (unsigned)g_live_n, g_live_stopped, g_live_expired, g_live_released, g_live_dropped,
             (int)g_release_fix);
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
