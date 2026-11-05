// The game's audio commands, as the game sends them (tests: BUG-0568, docs/re/tribe.md#tribe-guest-music).
//
// Every sound the game starts or stops goes through one command builder of Audio::AudioSystem (the
// singleton at Audio::AudioSystem::Get): vt 0x38 begins a command (0x3475365 play, 0x347536B stop, 0x3475376 set a
// property, ...), vt 0x40 / 0x3C add an int / float parameter (0x3475381 the sound id, 0x3475385 the track), vt 0x58
// submits it. The lab games run muted (the test profiles' settings), so a test cannot listen: `coop.audio` lists the
// commands the game submitted since a mark and the tracks it plays now (played and not stopped), with the sound id.
// Read-only: the hooks record and call the original.
#include "audio.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "game.h"
#include "sporekit_game.hpp"

namespace audio {

static const SkHost* H;
static SkMod* M;

static constexpr uint32_t kPlay = 0x3475365, kStop = 0x347536B, kSound = 0x3475381, kTrack = 0x3475385,
                          kListener = 0x3991984;

struct Cmd {
    uint32_t cmd = 0, sound = 0, track = 0, n = 0;
    uint32_t key[6] = {}, val[6] = {};
    uint32_t from[4] = {};
};
struct Event {
    double t;
    uint32_t mode, cmd, sound, track, n;
    uint32_t key[6], val[6];
    uint32_t from[4];
};

static std::mutex g_mu;
static thread_local Cmd t_cur;
static std::vector<Event> g_events;             // since the last mark (at most kMax)
static std::map<uint32_t, Event> g_playing;     // track -> its last play (removed by a stop)
static uint32_t g_submits, g_plays, g_stops, g_dropped;
static constexpr size_t kMax = 20000;
static bool g_on;
// players' games: the hooks only pass through until a test asks (coop.audio), no stack scan or lock per command
static volatile bool g_rec;

static double now_s()
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

using Begin_t = void(SK_THISCALL*)(void*, uint32_t);
using SetInt_t = void(SK_THISCALL*)(void*, uint32_t, uint32_t);
using Submit_t = void(SK_THISCALL*)(void*);
static Begin_t o_Begin;
static SetInt_t o_SetInt;
static Submit_t o_Submit;

static void SK_THISCALL h_Begin(void* self, uint32_t cmd)
{
    if (!g_rec)
        return o_Begin(self, cmd);
    t_cur = Cmd{};
    t_cur.cmd = cmd;
    // who asked: the first return addresses into the game's code on the stack (RE of the callers; no frame pointers)
    const uint32_t* sp = (const uint32_t*)__builtin_frame_address(0);
    for (int k = 0, f = 0; k < 96 && f < 4; k++) {
        uint32_t v = sp[k];
        if (v > 0x401000 && v < 0x1300000)
            t_cur.from[f++] = v;
    }
    o_Begin(self, cmd);
}
static void SK_THISCALL h_SetInt(void* self, uint32_t key, uint32_t v)
{
    if (!g_rec)
        return o_SetInt(self, key, v);
    if (key == kSound)
        t_cur.sound = v;
    else if (key == kTrack)
        t_cur.track = v;
    if (t_cur.n < 6)
        t_cur.key[t_cur.n] = key, t_cur.val[t_cur.n] = v;
    t_cur.n++;
    o_SetInt(self, key, v);
}
static void SK_THISCALL h_Submit(void* self)
{
    if (!g_rec)
        return o_Submit(self);
    Cmd c = t_cur;
    t_cur = Cmd{};
    if (c.cmd && c.cmd != kListener) { // (the listener's moves, every frame: counted only)
        Event e{now_s(), game::mode_id(), c.cmd, c.sound, c.track, c.n, {}, {}};
        memcpy(e.key, c.key, sizeof e.key);
        memcpy(e.val, c.val, sizeof e.val);
        memcpy(e.from, c.from, sizeof e.from);
        std::lock_guard<std::mutex> lk(g_mu);
        g_submits++;
        if (g_events.size() >= kMax) { // the oldest go (a ring of the last kMax)
            g_events.erase(g_events.begin(), g_events.begin() + kMax / 4);
            g_dropped += kMax / 4;
        }
        g_events.push_back(e);
        if (c.cmd == kPlay) {
            g_plays++;
            if (c.track)
                g_playing[c.track] = e;
        } else if (c.cmd == kStop) {
            g_stops++;
            if (c.track)
                g_playing.erase(c.track);
        }
    } else if (c.cmd) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_submits++;
    }
    o_Submit(self);
}

static bool install()
{
    if (g_on)
        return true;
    uintptr_t b = H->sym("SporeCoop::AudioSystem_BeginCommand"), i = H->sym("SporeCoop::AudioSystem_SetInt"),
              s = H->sym("SporeCoop::AudioSystem_Submit");
    if (!b || !i || !s)
        return false; // (GOG 2016: not known)
    H->hook_create(M, (void*)b, (void*)h_Begin, (void**)&o_Begin);
    H->hook_create(M, (void*)i, (void*)h_SetInt, (void**)&o_SetInt);
    H->hook_create(M, (void*)s, (void*)h_Submit, (void**)&o_Submit);
    g_on = true;
    return true;
}

// coop.audio [mark | list [n] | playing]
static int c_audio(void*, int argc, const char* const* argv, SkOut* out)
{
    g_rec = true; // from the first command on (the test's mark)
    const char* a = argc > 1 ? argv[1] : "";
    if (!g_on) {
        H->out_printf(out, "audio: no hooks on this build\n");
        return 1;
    }
    std::lock_guard<std::mutex> lk(g_mu);
    double t = now_s();
    if (!strcmp(a, "mark")) {
        g_events.clear();
        g_dropped = 0;
        H->out_printf(out, "audio: marked (playing %zu)\n", g_playing.size());
        return 0;
    }
    if (!strcmp(a, "list")) {
        size_t n = argc > 2 ? (size_t)atoi(argv[2]) : 200, from = g_events.size() > n ? g_events.size() - n : 0;
        for (size_t k = from; k < g_events.size(); k++) {
            const Event& e = g_events[k];
            std::string kv;
            char b[32];
            for (uint32_t j = 0; j < e.n && j < 6; j++) {
                snprintf(b, sizeof b, " %08X=%X", e.key[j], e.val[j]);
                kv += b;
            }
            H->out_printf(out, "ev %.2f mode=%08X cmd=%08X sound=%08X track=%u n=%u%s from=%X,%X,%X,%X\n", e.t - t, e.mode, e.cmd,
                          e.sound, e.track, e.n, kv.c_str(), e.from[0], e.from[1], e.from[2], e.from[3]);
        }
    }
    for (auto& kv : g_playing)
        H->out_printf(out, "playing track=%u sound=%08X since=%.1f mode=%08X\n", kv.first, kv.second.sound, t - kv.second.t,
                      kv.second.mode);
    H->out_printf(out, "audio: submits=%u plays=%u stops=%u events=%zu dropped=%u playing=%zu\n", g_submits, g_plays, g_stops,
                  g_events.size(), g_dropped, g_playing.size());
    return 0;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    install();
    H->command_add(M, "coop.audio",
                   "coop.audio [mark | list [n]] - the game's audio commands since the mark and the tracks it plays now (tests, BUG-0568)",
                   c_audio, nullptr, 0);
}

} // namespace audio
