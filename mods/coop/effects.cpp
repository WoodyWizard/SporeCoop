#include "effects.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <utility>

#include "fixes.h"
#include "game.h"
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

// Rate limit: a creature can make a burst of one-shot effects (footsteps, bites, particles). 40 a
// second is far above every effect the game makes for one avatar and below a packet flood.
static constexpr double kRate = 40.0;
static double g_tokens = 20.0;
static double g_tokens_at = 0;

static uint32_t g_sent, g_rx, g_played, g_stopped, g_skipped, g_failed, g_dropped;
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
// world effects already sent (effect pointer, id): the transform setter runs every frame for
// persistent effects; a one-shot world effect is sent once. A fixed ring (no allocation in the
// render pipeline).
struct WorldSent { void* fx; uint32_t id; };
static WorldSent g_world_sent[2048];
static size_t g_world_n_entries;
static uint32_t g_world;

static double now() { return H ? H->time_seconds() : 0; }

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
    if (game::effect_relayed(fx)) // a creature hook already sent this effect
        return;
    // the position the game's transform setter put into effect+0x9C. Animation-event effects are
    // skipped in the hook (0x9FBF00 sets a thread-local flag): the animation relay shows those.
    net::Vec3 p;
    if (!game::effect_position(fx, p) || !C.near_local || !C.near_local(p))
        return;
    for (size_t i = 0; i < 2048; i++)
        if (g_world_sent[i].fx == fx && g_world_sent[i].id == id)
            return;
    double t = now();
    if (!take_token(t)) {
        g_dropped++;
        return;
    }
    g_world_sent[g_world_n_entries++ & 2047] = {fx, id};
    g_world++;
    g_last_tx = Last{};
    g_last_tx.id = id;
    g_last_tx.key = 0;
    g_last_tx.pool = 5;
    g_last_tx.at = t;
    g_sent++;
    if (g_log)
        H->log(M, SK_INFO, "effect: sent world id %08X at %.2f %.2f %.2f", id, p.x, p.y, p.z);
    // the receiver's game sets the position with the base transform setter, which reads only the
    // three floats at +4; the rest of the 0x44-byte transform stays zero
    uint8_t xf[0x44] = {};
    float* q = (float*)(xf + 4);
    q[0] = p.x;
    q[1] = p.y;
    q[2] = p.z;
    net::Writer w;
    w.u8(C.sess->my_id());
    w.u8(0);
    w.u8(5);
    w.u32(id);
    w.u32(0);
    w.raw(xf, 0x44);
    C.sess->send_event(net::MSG_EFFECT, w);
}

// the workload of the frame-time measurement (out/REPORT.md): every period the game creates `n`
// effects at the local player, which the relay hooks see like effects of the game's own code
static uint32_t g_load_n;
static uint32_t g_load_ms = 1000;
static uint32_t g_load_id = 0xE1B24DDBu; // the mating call's hearts
static double g_load_at;

static void load_tick(double t)
{
    if (!g_load_n || t < g_load_at || !C.local_pos)
        return;
    g_load_at = t + g_load_ms / 1000.0;
    if (g_load_at < t)
        g_load_at = t + 1.0;
    net::Vec3 p = C.local_pos();
    uint8_t xf[0x44] = {};
    float* q = (float*)(xf + 4);
    q[0] = p.x;
    q[1] = p.y;
    q[2] = p.z;
    for (uint32_t i = 0; i < g_load_n; i++)
        game::create_local_effect(g_load_id, xf);
}

// the cost of this module on the main thread, printed in coop.status and once a minute as a
// `perf:` line in the game log (the frame-time numbers in out/REPORT.md)
static double g_tick_sum, g_tick_max;
static uint64_t g_tick_frames;
static double g_perf_at;
static uint32_t g_perf_creates, g_perf_probes, g_perf_evicted, g_perf_sent;

void tick(double)
{
    double t0 = H ? H->time_seconds() : 0;
    game::set_effect_relay_enabled(g_fix); // also notes the game's main thread id
    if (g_fix)
        load_tick(t0);
    Pending p;
    while (q_pop(p)) {
        if (p.world)
            process_world(p.fx, p.id);
        else
            process_local(p.creature, p.ev);
    }
    game::clear_effect_table();
    if (!H)
        return;
    double dt = H->time_seconds() - t0;
    g_tick_sum += dt;
    g_tick_max = dt > g_tick_max ? dt : g_tick_max;
    g_tick_frames++;
    if (g_perf_at <= 0)
        g_perf_at = t0;
    if (t0 - g_perf_at >= 60) {
        g_perf_at = t0;
        uint32_t c = 0, pr = 0, ev = 0;
        game::effect_table_stats(c, pr, ev);
        H->log(M, SK_INFO,
               "perf: effects tick avg %.4f max %.3f ms/frame over %llu frames; hooks %u creates, %u table probes, "
               "%u evicted; sent %u; load %u effects per %u ms",
               g_tick_frames ? g_tick_sum / g_tick_frames * 1000 : 0.0, g_tick_max * 1000,
               (unsigned long long)g_tick_frames, c - g_perf_creates, pr - g_perf_probes, ev - g_perf_evicted,
               g_sent - g_perf_sent, g_load_n, g_load_ms);
        g_tick_sum = g_tick_max = 0;
        g_tick_frames = 0;
        g_perf_creates = c;
        g_perf_probes = pr;
        g_perf_evicted = ev;
        g_perf_sent = g_sent;
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
        if (r.left() < sizeof xf)
            return;
        r.raw(xf, sizeof xf);
    }
    if (!r.ok())
        return;
    if (C.sess && C.sess->role() == net::Role::Host && from != 0) { // relay to the other guests
        net::Writer w;
        w.u8(player);
        w.u8(stop);
        w.u8(pool);
        w.u32(id);
        w.u32(key);
        C.sess->broadcast_reliable(net::MSG_EFFECT, w, from);
    }
    g_rx++;
    if (!g_fix)
        return;
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
            } else if (skg::readable((uint8_t*)fx + 0x9C, 12)) {
                const float* p = (const float*)((uint8_t*)fx + 0x9C);
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

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    g_tokens = 20.0;
    g_tokens_at = 0;
    g_sent = g_rx = g_played = g_stopped = g_skipped = g_failed = g_dropped = 0;
    g_last_tx = Last{};
    g_last_rx.clear();
    g_last_rx_start.clear();
    g_last_rx_from = 0xFF;
    g_sent_n = 0;
    g_world_n_entries = 0;
    g_world = 0;
    g_tick_sum = g_tick_max = 0;
    g_tick_frames = 0;
    g_perf_at = 0;
    g_perf_creates = g_perf_probes = g_perf_evicted = g_perf_sent = 0;
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
    g_world_n_entries = 0;
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
    if (id)
        g_load_id = id;
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
    char b[320];
    snprintf(b, sizeof b,
             "effects: fix=%d sent=%u received=%u played=%u stopped=%u skipped=%u failed=%u dropped_rate=%u world=%u\n",
             (int)g_fix, g_sent, g_rx, g_played, g_stopped, g_skipped, g_failed, g_dropped, g_world);
    s += b;
    uint32_t creates = 0, probes = 0, evicted = 0;
    game::effect_table_stats(creates, probes, evicted);
    snprintf(b, sizeof b,
             "effects cost: tick avg %.4f max %.3f ms/frame over %llu frames; hooks %u creates, %u table probes, "
             "%u evicted; load %u per %u ms\n",
             g_tick_frames ? g_tick_sum / g_tick_frames * 1000 : 0.0, g_tick_max * 1000,
             (unsigned long long)g_tick_frames, creates, probes, evicted, g_load_n, g_load_ms);
    s += b;
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
