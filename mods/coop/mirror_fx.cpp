// The sounds and effects a mirror's own game never makes (BUG-0600): see mirror_fx.h.
#include "mirror_fx.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "fixes.h"
#include "modes.h"
#include "npc.h"
#include "sporekit_game.hpp"
#include "tribe.h"

namespace mirror_fx {

static const SkHost* H;
static SkMod* M;
static Ctx C;

// BUG-0600: a guest heard none of the fight's hit, weapon and sprint sounds and none of the creature voices of
// the host's tribe members (lab: the host's game 51 such plays in a 40 s fight, the guest's 4, out/lab/20261105-115203).
// On: the host relays the members' and the NPCs' pool effects, sprints and combat voices; a guest plays them on
// its mirrors. Off: nothing sent, nothing played (0.7.7).
static bool g_fix = true;
COOP_FIX("mirror.fx", "BUG-0600", g_fix,
         "tribe / creature: a guest hears and sees the fight's effects, sprints and voices of the host's members and animals");

enum Kind : uint8_t {
    K_EFFECT = 0, // {u8 space, u16 id, u8 stop, u8 pool (1, 2: keyed start; 3: one-shot at a bone; 0: stop), u32 effect, u32 key}
    K_VOICE = 1,  // {u8 space, u16 id, u8 enter}: SporeCoop::Creature_CombatVoice (the growl 0xC881120E / the sigh 0x529FEC69)
    K_SPRINT = 2, // {u8 space, u16 id, u8 on}: SporeCoop::Creature_SetSprint (its Update makes the sprint effect E55DC391)
};
enum Space : uint8_t { S_NPC = 0, S_MEMBER = 1 };

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
static bool stage_ok()
{
    uint32_t m = game::mode_id();
    return m == modes::kCreature || m == modes::kTribe;
}

// host: the creature's net id among the mirrored ones (a tribe member first, then an npc.cpp NPC)
static bool ident(void* c, uint8_t& space, uint16_t& id)
{
    if (!c)
        return false;
    if (uint16_t m = tribe::id_of(c)) {
        space = S_MEMBER;
        id = m;
        return true;
    }
    uint16_t n = npc::id_of(c);
    if (n != 0xFFFF) {
        space = S_NPC;
        id = n;
        return true;
    }
    return false;
}
// guest: the mirror of the host's creature, alive and not dead
static void* mirror(uint8_t space, uint16_t id)
{
    void* c = space == S_MEMBER ? tribe::mirror_of(id) : space == S_NPC ? npc::proxy(id) : nullptr;
    if (!c || !game::object_alive(c) || game::is_destroyed(c) || game::is_dead(c))
        return nullptr;
    return c;
}

// a creature can make bursts of one-shot effects; 60 a second is far above a fight of a whole village
static constexpr double kRate = 60.0;
static double g_tokens = 30.0, g_tokens_at;
static bool take_token()
{
    double t = H->time_seconds();
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

static uint32_t g_sent[3], g_rx[3], g_played[3], g_nomirror, g_dropped, g_bad;

static void send(Kind k, const net::Writer& w, bool reliable)
{
    if (!take_token()) {
        g_dropped++;
        return;
    }
    for (auto& p : C.sess->players())
        if (p.id != 0) {
            if (reliable)
                C.sess->send_reliable_to(p.id, net::MSG_MIRROR_FX, w);
            else
                C.sess->send_unreliable_to(p.id, net::MSG_MIRROR_FX, w);
        }
    g_sent[k]++;
}

// keyed effects a guest got a start for, per creature: a stop goes out once (the game stops some keys every frame)
struct KeyOn { uint8_t space; uint16_t id; uint32_t key; };
static constexpr size_t kKeys = 256;
static KeyOn g_keys[kKeys];
static size_t g_keys_n;
static bool key_take(uint8_t s, uint16_t id, uint32_t key)
{
    for (size_t i = 0; i < g_keys_n; i++)
        if (g_keys[i].space == s && g_keys[i].id == id && g_keys[i].key == key) {
            g_keys[i] = g_keys[--g_keys_n];
            return true;
        }
    return false;
}
static void key_put(uint8_t s, uint16_t id, uint32_t key)
{
    for (size_t i = 0; i < g_keys_n; i++)
        if (g_keys[i].space == s && g_keys[i].id == id && g_keys[i].key == key)
            return;
    if (g_keys_n == kKeys)
        g_keys[0] = g_keys[--g_keys_n]; // full: the oldest slot goes (its stop is not sent)
    g_keys[g_keys_n++] = {s, id, key};
}

void on_creature_effect(void* c, const game::EffectEvent& ev)
{
    if (!g_fix || !is_host() || !stage_ok() || game::is_puppet(c))
        return;
    uint8_t s;
    uint16_t id;
    if (!ident(c, s, id))
        return;
    if (ev.stop) {
        if (!ev.key || !key_take(s, id, ev.key))
            return;
    } else {
        if (!ev.id || (ev.pool != 1 && ev.pool != 2 && ev.pool != 3))
            return;
        if (ev.pool != 3)
            key_put(s, id, ev.key);
    }
    net::Writer w;
    w.u8(K_EFFECT); w.u8(s); w.u16(id); w.u8(ev.stop ? 1 : 0); w.u8(ev.pool); w.u32(ev.id); w.u32(ev.key);
    send(K_EFFECT, w, ev.pool != 3); // a one-shot is worth nothing late; starts and stops are a pair
}

// SporeCoop::Creature_CombatVoice 0xC1B030: thiscall(creature, bool enter, bool force) ret 8; the in-combat byte +0xBB1
// says whether the voice played (it plays once per change)
using Voice_t = void(SK_THISCALL*)(void*, bool, bool);
static Voice_t o_Voice;
// SporeCoop::Creature_SetSprint 0xC1AE70: thiscall(creature, bool on) ret 4; the sprint bit is +0xFA4 & 0x20
using Sprint_t = void(SK_THISCALL*)(void*, bool);
static Sprint_t o_Sprint;
static bool g_playing; // a guest's own call of the game's function

static uint8_t byte_at(void* c, uint32_t off) { return skg::readable((uint8_t*)c + off, 1) ? *((uint8_t*)c + off) : 0; }

static void SK_THISCALL h_Voice(void* c, bool enter, bool force)
{
    if (g_playing || !g_fix || !is_host() || !c) {
        o_Voice(c, enter, force);
        return;
    }
    uint8_t before = byte_at(c, 0xBB1);
    o_Voice(c, enter, force);
    uint8_t after = byte_at(c, 0xBB1);
    uint8_t s;
    uint16_t id;
    if (before != after && stage_ok() && !game::is_puppet(c) && ident(c, s, id)) {
        net::Writer w;
        w.u8(K_VOICE); w.u8(s); w.u16(id); w.u8(after ? 1 : 0);
        send(K_VOICE, w, true);
    }
}
static void SK_THISCALL h_Sprint(void* c, bool on)
{
    if (g_playing || !g_fix || !is_host() || !c) {
        o_Sprint(c, on);
        return;
    }
    uint8_t before = byte_at(c, 0xFA4) & 0x20;
    o_Sprint(c, on);
    uint8_t after = byte_at(c, 0xFA4) & 0x20;
    uint8_t s;
    uint16_t id;
    if (before != after && stage_ok() && !game::is_puppet(c) && ident(c, s, id)) {
        net::Writer w;
        w.u8(K_SPRINT); w.u8(s); w.u16(id); w.u8(after ? 1 : 0);
        send(K_SPRINT, w, true);
    }
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_MIRROR_FX || from != 0 || !is_guest())
        return;
    uint8_t k = r.u8(), s = r.u8();
    uint16_t id = r.u16();
    if (!r.ok() || k > K_SPRINT) {
        g_bad++;
        return;
    }
    g_rx[k]++;
    if (!g_fix || !stage_ok())
        return;
    if (k == K_EFFECT) {
        uint8_t stop = r.u8(), pool = r.u8();
        uint32_t eid = r.u32(), key = r.u32();
        if (!r.ok() || (!stop && (!eid || pool < 1 || pool > 3)) || (stop && !key)) {
            g_bad++;
            return;
        }
        void* c = mirror(s, id);
        if (!c) {
            g_nomirror++;
            return;
        }
        bool ok;
        if (stop)
            ok = game::stop_effect(c, key);
        else if (pool == 3) // the bone index: the game indexes the model's bones with it unchecked
            ok = game::creature_bone_ok(c, (int32_t)key) && game::create_creature_effect(c, (int)key, eid);
        else
            ok = game::start_effect(c, {eid, key, pool, false}) != nullptr;
        if (ok)
            g_played[k]++;
        return;
    }
    uint8_t on = r.u8();
    if (!r.ok()) {
        g_bad++;
        return;
    }
    void* c = mirror(s, id);
    if (!c) {
        g_nomirror++;
        return;
    }
    g_playing = true;
    if (k == K_VOICE && o_Voice) {
        o_Voice(c, on != 0, true);
        g_played[k]++;
    } else if (k == K_SPRINT && o_Sprint) {
        o_Sprint(c, on != 0);
        g_played[k]++;
    }
    g_playing = false;
}

std::string status()
{
    char b[320];
    snprintf(b, sizeof b,
             "mirror_fx: fix=%d hooks voice=%d sprint=%d sent effect=%u voice=%u sprint=%u rx %u/%u/%u played %u/%u/%u "
             "no_mirror=%u dropped=%u bad=%u keys=%zu\n",
             (int)g_fix, o_Voice ? 1 : 0, o_Sprint ? 1 : 0, g_sent[0], g_sent[1], g_sent[2], g_rx[0], g_rx[1], g_rx[2],
             g_played[0], g_played[1], g_played[2], g_nomirror, g_dropped, g_bad, g_keys_n);
    return b;
}

static int c_status(void*, int, const char* const*, SkOut* out)
{
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = ctx;
    if (uintptr_t a = H->sym("SporeCoop::Creature_CombatVoice"))
        H->hook_create(M, (void*)a, (void*)h_Voice, (void**)&o_Voice);
    if (uintptr_t a = H->sym("SporeCoop::Creature_SetSprint"))
        H->hook_create(M, (void*)a, (void*)h_Sprint, (void**)&o_Sprint);
    H->command_add(M, "coop.mirrorfx", "coop.mirrorfx - the host's creature effects, sprints and voices played on a guest's mirrors (BUG-0600)",
                   c_status, nullptr, 0);
}

} // namespace mirror_fx
