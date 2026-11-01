#include "parts.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/protocol.h"
#include "sporekit_game.hpp"

namespace parts {

using namespace skg;

static const SkHost* H;
static SkMod* M;
static Ctx C;

// ---------------------------------------------------------------- BUG-0401
// A part a player finds (a skeleton's reward, a creature's drop, an evolution reward) is unlocked in
// that player's own game only: the creature stage's event handler 0xD47FC0 picks it (0xD47E50) and
// cCollectableItems 0x5976D0 -> 0x597410 sets the item's state (no UnlockPart call); loads and the
// cell->creature start go through UnlockPart 0x596DA0 (docs/re/creations.md#creations-part-pickup).
// Nothing sent a part to the other games: a guest's own editor (BUG-0368: the guest's collected parts)
// never offered the parts the host found, and the parts a guest found were lost at its next world
// load (the host's save). Now the party shares one set: every game polls its collected parts (1 Hz),
// a guest sends the parts it has and the host's set lacks to the host, the host unlocks them in its
// game (UnlockPart, cost 0) and sends its whole set to every guest (at once on a change, else every
// 10 s), a guest that plays the host's world unlocks what it lacks. Nothing is ever locked.
// `coop.fix parts.shared off`: every game keeps its own parts (0.7.3.3).
static bool g_fix = true;
COOP_FIX("parts.shared", "BUG-0401", g_fix, "creature: the parts a player finds are unlocked for the whole party (host-held union)");

// MSG_PARTS: u8 kind, u8 origin player, u32 world (the sender's save CRC), u16 n, n x {u32 lo, u32 hi}
constexpr uint8_t kKindSet = 0;  // host -> guests: (a chunk of) the host's whole set, silent
constexpr uint8_t kKindNews = 1; // host -> guests: parts newly unlocked, found by `origin`
constexpr uint8_t kKindFound = 2; // guest -> host: parts this guest has that the host's set lacks
constexpr int kChunk = 100;       // keys per message (8 bytes each: well under one packet)
constexpr int kMaxKeys = 4096;    // a received message with more is refused
constexpr double kPoll = 1.0, kFullEvery = 10.0, kResend = 3.0;

static uintptr_t a_IsUnlocked, a_UnlockPart, a_StrategyGet, a_ExecuteAction;

static uint32_t g_tx_sets, g_tx_news, g_tx_found, g_rx_sets, g_rx_news, g_rx_found, g_applied, g_refused, g_stale;

// host
static std::set<Key> g_announced;              // the set the guests were last sent
static uint32_t g_announced_world = 0;
static double g_last_poll = -100, g_last_full = -100;
static std::map<Key, uint8_t> g_pending;       // guests' finds not applied yet -> who found it
static std::map<Key, uint8_t> g_origin;        // the finder of a key unlocked from g_pending (for the news)

// guest
static std::set<Key> g_host_set;               // the host's set (of g_host_world)
static uint32_t g_host_world = 0;
static bool g_have_set = false;
static double g_last_found_tx = -100;
static std::map<Key, int> g_found_tries;       // how often a key was sent (a key the host refuses stops)
constexpr int kMaxTries = 5;

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

bool census(int& unlocked, uint32_t& hash, int& unlockable, std::vector<Key>* locked, std::vector<Key>* unlocked_keys)
{
    unlocked = unlockable = 0;
    hash = 0;
    void* items = C.items ? C.items() : nullptr;
    if (!items || !a_IsUnlocked || !readable((char*)items + 0x1490, 8))
        return false;
    void** b = field<void**>(items, 0x1490);
    uint32_t n = field<uint32_t>(items, 0x1494);
    for (uint32_t i = 0; b && n < 100000 && i < n && readable(b + i, 4); i++)
        for (uint32_t* nd = (uint32_t*)b[i]; nd && unlockable < 100000; nd = (uint32_t*)nd[10]) {
            if (!readable(nd, 0x2C))
                break;
            unlockable++;
            if (thiscall<bool>(a_IsUnlocked, items, nd[0], nd[1])) {
                unlocked++;
                uint32_t h = (nd[0] ^ (nd[1] * 0x9E3779B1u)) * 0x85EBCA6Bu;
                hash += h ^ (h >> 13);
                if (unlocked_keys)
                    unlocked_keys->push_back({nd[0], nd[1]});
            } else if (locked)
                locked->push_back({nd[0], nd[1]});
        }
    return true;
}

// the keys of `want` that are unlockable here and still locked get unlocked (cost 0); returns them
static std::vector<Key> unlock_missing(const std::set<Key>& want)
{
    std::vector<Key> locked, done;
    int n, all;
    uint32_t h;
    void* items = C.items ? C.items() : nullptr;
    if (!items || !a_UnlockPart || !census(n, h, all, &locked))
        return done;
    for (auto& k : locked)
        if (want.count(k) && thiscall<bool>(a_UnlockPart, items, k.first, k.second, 0)) {
            done.push_back(k);
            g_applied++;
        }
    return done;
}

static void write_keys(net::Writer& w, uint8_t kind, uint8_t origin, uint32_t world, const Key* k, size_t n)
{
    w.u8(kind);
    w.u8(origin);
    w.u32(world);
    w.u16((uint16_t)n);
    for (size_t i = 0; i < n; i++) {
        w.u32(k[i].first);
        w.u32(k[i].second);
    }
}

static void host_send(uint8_t kind, uint8_t origin, const std::vector<Key>& keys)
{
    size_t at = 0;
    do { // an empty set still goes out (the guests learn the host's world)
        net::Writer w;
        size_t n = std::min(keys.size() - at, (size_t)kChunk);
        write_keys(w, kind, origin, g_announced_world, keys.data() + at, n);
        C.sess->broadcast_reliable(net::MSG_PARTS, w);
        (kind == kKindSet ? g_tx_sets : g_tx_news)++;
        at += n;
    } while (at < keys.size());
}

static std::string count_text(size_t n) { return n == 1 ? "a new creature part" : std::to_string(n) + " new creature parts"; }

static void host_tick(double now)
{
    uint32_t world = C.world ? C.world() : 0;
    if (!world)
        return;
    if (world != g_announced_world) { // another world: its own set, no news for the load's difference
        g_announced.clear();
        g_origin.clear();
        g_announced_world = world;
        g_last_full = -100;
    }
    if (!g_pending.empty()) { // the guests' finds
        std::set<Key> want;
        for (auto& [k, o] : g_pending)
            want.insert(k);
        for (auto& k : unlock_missing(want))
            g_origin[k] = g_pending[k];
        g_pending.clear();
    }
    std::vector<Key> mine;
    int n, all;
    uint32_t h;
    if (!census(n, h, all, nullptr, &mine))
        return;
    std::set<Key> now_set(mine.begin(), mine.end());
    bool first = g_announced.empty();
    std::map<uint8_t, std::vector<Key>> news; // by finder
    for (auto& k : now_set)
        if (!g_announced.count(k)) {
            auto o = g_origin.find(k);
            news[o == g_origin.end() ? 0 : o->second].push_back(k);
        }
    g_origin.clear();
    bool changed = now_set != g_announced;
    g_announced = std::move(now_set);
    if (!first)
        for (auto& [origin, keys] : news) {
            host_send(kKindNews, origin, keys);
            H->log(M, SK_INFO, "parts: %zu new part(s) found by player %u, %zu in the party's set", keys.size(), origin,
                   g_announced.size());
            if (origin != 0 && C.feed)
                C.feed(C.sess->name_of(origin) + " found " + count_text(keys.size()) + ": it is in your editor too");
        }
    if (changed || now - g_last_full >= kFullEvery) {
        g_last_full = now;
        host_send(kKindSet, 0, std::vector<Key>(g_announced.begin(), g_announced.end()));
    }
}

static void guest_tick(double now)
{
    uint32_t world = C.world ? C.world() : 0;
    if (!g_have_set || !world || world != g_host_world || !C.guest_in_host_world || !C.guest_in_host_world())
        return;
    auto done = unlock_missing(g_host_set); // what the host's world has (also after our own reload)
    if (!done.empty())
        H->log(M, SK_INFO, "parts: %zu part(s) of the party's set unlocked here", done.size());
    std::vector<Key> mine;
    int n, all;
    uint32_t h;
    if (!census(n, h, all, nullptr, &mine))
        return;
    std::vector<Key> extra; // found here, not in the host's set yet: sent until the host's set has them
    if (now - g_last_found_tx < kResend)
        return;
    for (auto& k : mine)
        if (!g_host_set.count(k) && g_found_tries[k] < kMaxTries)
            extra.push_back(k);
    if (extra.empty())
        return;
    g_last_found_tx = now;
    for (auto& k : extra)
        g_found_tries[k]++;
    for (size_t at = 0; at < extra.size(); at += kChunk) {
        net::Writer w;
        write_keys(w, kKindFound, C.sess->my_id(), world, extra.data() + at, std::min(extra.size() - at, (size_t)kChunk));
        C.sess->send_reliable_to(0, net::MSG_PARTS, w);
        g_tx_found++;
    }
    H->log(M, SK_INFO, "parts: sent %zu part(s) found here to the host", extra.size());
}

void tick(double now)
{
    if (!g_fix || !C.sess || !a_IsUnlocked || !a_UnlockPart)
        return;
    if (game::mode_id() != modes::kCreature || !game::avatar() || now - g_last_poll < kPoll)
        return;
    g_last_poll = now;
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_PARTS || !g_fix)
        return;
    uint8_t kind = r.u8(), origin = r.u8();
    uint32_t world = r.u32();
    uint16_t n = r.u16();
    if (!r.ok() || n > kMaxKeys || r.left() < (size_t)n * 8) {
        g_refused++;
        return;
    }
    std::vector<Key> keys(n);
    for (auto& k : keys) {
        k.first = r.u32();
        k.second = r.u32();
    }
    if (is_host() && kind == kKindFound && from != 0) {
        g_rx_found++;
        if (world == 0 || world != (C.world ? C.world() : 0)) { // found in another world than ours
            g_stale++;
            return;
        }
        for (auto& k : keys)
            g_pending.emplace(k, from); // applied by the next poll in Creature
        return;
    }
    if (is_guest() && from == 0 && (kind == kKindSet || kind == kKindNews)) {
        (kind == kKindSet ? g_rx_sets : g_rx_news)++;
        if (world != g_host_world) { // the host plays another world now: its set starts over
            g_host_set.clear();
            g_found_tries.clear();
            g_host_world = world;
        }
        g_have_set = true;
        g_host_set.insert(keys.begin(), keys.end());
        if (kind == kKindNews && origin != C.sess->my_id() && C.feed && !keys.empty())
            C.feed(C.sess->name_of(origin) + " found " + count_text(keys.size()) + ": it is in your editor too");
    }
}

void on_session_end()
{
    g_announced.clear();
    g_announced_world = 0;
    g_pending.clear();
    g_origin.clear();
    g_host_set.clear();
    g_host_world = 0;
    g_have_set = false;
    g_found_tries.clear();
    g_last_full = g_last_poll = g_last_found_tx = -100;
}

int find_part(int level)
{
    void* av = game::avatar();
    if (!av || !a_StrategyGet || !a_ExecuteAction || game::mode_id() != modes::kCreature)
        return -1;
    void* st = cdecl_call<void*>(a_StrategyGet);
    if (!st)
        return -1;
    // as 0xD46BE0 case 6 (a skeleton / bone pile reward) builds it: {creature, 0, level}
    uint32_t msg[3] = {(uint32_t)(uintptr_t)av, 0, (uint32_t)level};
    thiscall<void>(a_ExecuteAction, st, 0x5371F11u, (void*)msg);
    int n, all;
    uint32_t h;
    return census(n, h, all) ? n : -1;
}

std::string status()
{
    char b[400];
    snprintf(b, sizeof b,
             "shared=%d set=%zu host_set=%zu host_world=%08X have_set=%d pending=%zu tx_sets=%u tx_news=%u tx_found=%u "
             "rx_sets=%u rx_news=%u rx_found=%u applied=%u stale=%u refused=%u",
             g_fix ? 1 : 0, g_announced.size(), g_host_set.size(), g_host_world, g_have_set ? 1 : 0, g_pending.size(),
             g_tx_sets, g_tx_news, g_tx_found, g_rx_sets, g_rx_news, g_rx_found, g_applied, g_stale, g_refused);
    return b;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    a_IsUnlocked = H->sym("Simulator::cCollectableItems::IsUnlocked");
    a_UnlockPart = H->sym("Simulator::cCollectableItems::UnlockPart");
    a_StrategyGet = H->sym("App::cCreatureModeStrategy::Get");
    a_ExecuteAction = H->sym("App::cCreatureModeStrategy::ExecuteAction");
}

} // namespace parts
