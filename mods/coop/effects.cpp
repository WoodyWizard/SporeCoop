#include "effects.h"

#include <cstdio>
#include <map>
#include <set>

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
// the pool keys we sent a start for: a stop is sent once, not on every frame the game stops a key
static std::set<uint32_t> g_sent_keys;
static uint8_t g_last_rx_from = 0xFF;

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

// every effect made for the local player's own creature (not an NPC, not a partner's puppet)
static void on_local_effect(void* creature, const game::EffectEvent& ev)
{
    if (!g_fix || !creature || !H)
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
        auto it = g_sent_keys.find(ev.key);
        if (it == g_sent_keys.end())
            return;
        g_sent_keys.erase(it);
    } else if (ev.pool == 1 || ev.pool == 2) {
        g_sent_keys.insert(ev.key);
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

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_EFFECT)
        return;
    uint8_t player = r.u8(), stop = r.u8(), pool = r.u8();
    uint32_t id = r.u32(), key = r.u32();
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
    void* puppet = C.puppet_of ? C.puppet_of(player) : nullptr;
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
        void* fx = pool == 3 ? game::create_creature_effect(puppet, (int)key, id) : game::start_effect(puppet, ev);
        if (fx) {
            g_played++;
            last.game = true;
            last.active = game::effect_active(fx);
            if (pool == 3) { // a one-shot effect has no pool entry: the game created it on the puppet
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
    g_last_rx_from = 0xFF;
    g_sent_keys.clear();
    game::set_effect_listener(on_local_effect);
}

void on_session_end()
{
    g_last_rx.clear();
    g_last_rx_from = 0xFF;
    g_sent_keys.clear();
}

bool set_log(bool on)
{
    g_log = on;
    game::set_effect_log(on);
    return g_log;
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
             "effects: fix=%d sent=%u received=%u played=%u stopped=%u skipped=%u failed=%u dropped_rate=%u\n",
             (int)g_fix, g_sent, g_rx, g_played, g_stopped, g_skipped, g_failed, g_dropped);
    s += b;
    if (g_last_tx.at > 0) {
        snprintf(b, sizeof b, "effects sent last: id=%08X key=%08X pool=%u stop=%d age=%.1fs\n", g_last_tx.id,
                 g_last_tx.key, g_last_tx.pool, g_last_tx.stop ? 1 : 0, now() - g_last_tx.at);
        s += b;
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
