// BUG-9277: each party's own evolution bar on a galaxy planet (party_bar.h).
#include "party_bar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "parties_net.h"
#include "slotfs.h"
#include "world.h"
#include "modes.h"
#include "sporekit.h"

#include <windows.h>

namespace party_bar {

static const SkHost* H;
static SkMod* M;
static bool g_fix = true;
COOP_FIX("creature.party_bar", "BUG-9277", g_fix,
         "creature: on a galaxy planet each party fills its own evolution bar (a stranger's gains no longer move yours)");

static std::map<uint32_t, float> g_bars;       // party table key (parties::table_key, never 0) -> its bar
static std::map<uint64_t, float> g_saved;      // player identity -> its party's bar (the planet's file)
static std::vector<uint64_t> g_game_owners;    // the identities whose bar the game's own is (the host's party)
static std::string g_key;                       // the planet (game key) the bars belong to
static uint32_t g_serial = 1, g_credits = 0, g_saves = 0, g_loads = 0, g_swaps = 0;
static float g_credited = 0;
static const net::Session* g_sess;
static uint32_t g_guest_world = 0; // BUG-9279
static bool g_warm_fix = true;
COOP_FIX("creature.party_bar_warm", "BUG-9279", g_warm_fix,
         "creature: a guest that takes a galaxy planet over in place keeps its own party's bar (not the old host's party's)");
static uint32_t g_warm_kept = 0;
void note_guest(uint32_t world) { g_guest_world = world; }
static constexpr uint32_t kMagic = 0x52414244; // "DBAR"

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
}

bool on() { return g_fix && world::party_difficulty() && game::in_creature_stage(); }

static std::vector<uint64_t> idents_of_key(const net::Session& s, uint32_t pk)
{
    std::vector<uint64_t> v;
    for (auto& p : s.players())
        if (parties::table_key(p.id) == pk)
            if (uint64_t id = parties::identity(p.id))
                v.push_back(id);
    return v;
}

static float& bar_of_key(const net::Session& s, uint32_t pk)
{
    auto it = g_bars.find(pk);
    if (it != g_bars.end())
        return it->second;
    float& b = g_bars[pk];
    b = 0; // (a party new to the planet starts its stage's bar from nothing)
    for (uint64_t id : idents_of_key(s, pk)) // (a party back on the planet: a member's saved bar)
        if (auto sv = g_saved.find(id); sv != g_saved.end()) {
            b = sv->second;
            break;
        }
    return b;
}

bool credit(const net::Session& s, uint8_t player, float amount)
{
    if (!on() || !(amount > 0) || amount > 100000)
        return false;
    const uint32_t pk = parties::table_key(player);
    if (!pk)
        return false; // the host's party: the game's own bar
    float& b = bar_of_key(s, pk);
    b = std::min(b + amount, 1e7f);
    g_serial++;
    g_credits++;
    g_credited += amount;
    return true;
}

float bar_for(const net::Session& s, uint8_t player, float host_bar)
{
    if (!on())
        return host_bar;
    const uint32_t pk = parties::table_key(player);
    return pk ? bar_of_key(s, pk) : host_bar;
}

bool changed_since(uint32_t serial_seen) { return serial_seen != g_serial; }
uint32_t serial() { return g_serial; }

static std::wstring path()
{
    const std::string key = world::current_game_key();
    return key.empty() ? std::wstring() : slotfs::widen(key + ".duo-progress");
}

void save()
{
    if (!g_sess || !on())
        return;
    const net::Session& s = *g_sess;
    const std::wstring p = path();
    if (p.empty())
        return;
    for (auto& [pk, b] : g_bars)
        for (uint64_t id : idents_of_key(s, pk))
            g_saved[id] = b;
    g_game_owners = idents_of_key(s, 0);
    const float own = game::evolution_bar();
    if (std::isfinite(own))
        for (uint64_t id : g_game_owners)
            g_saved[id] = own;
    net::Writer w;
    w.u32(kMagic);
    w.u32(1);
    w.u16((uint16_t)std::min<size_t>(g_game_owners.size(), 64));
    for (size_t i = 0; i < g_game_owners.size() && i < 64; i++)
        w.u64(g_game_owners[i]);
    const uint16_t n = (uint16_t)std::min<size_t>(g_saved.size(), 4096);
    w.u16(n);
    uint16_t k = 0;
    for (auto& [id, b] : g_saved) {
        if (k++ >= n)
            break;
        w.u64(id);
        w.f32(b);
    }
    const std::wstring tmp = p + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD put = 0;
    const bool ok = WriteFile(f, w.data.data(), (DWORD)w.data.size(), &put, nullptr) && put == w.data.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return;
    }
    g_saves++;
}

// host: the planet's file read once per planet (the first Creature tick of a world with another key). The game's own
// bar is the old host's party's: a host of another party plays by its own party's saved bar (0 if none).
void tick(const net::Session& s)
{
    g_sess = &s;
    if (!on())
        return;
    const std::string key = world::current_game_key();
    if (key == g_key)
        return;
    g_key = key;
    g_bars.clear();
    g_saved.clear();
    g_game_owners.clear();
    g_serial++;
    if (key.empty())
        return;
    HANDLE f = CreateFileW(path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    std::vector<uint64_t> owners;
    if (f != INVALID_HANDLE_VALUE) {
        const DWORD size = GetFileSize(f, nullptr);
        std::vector<uint8_t> data(size <= (1u << 20) ? size : 0);
        DWORD got = 0;
        const bool ok = !data.empty() && ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr) && got == data.size();
        CloseHandle(f);
        if (ok) {
            net::Reader r(data.data(), data.size());
            if (r.u32() == kMagic && r.u32() == 1) {
                const uint16_t no = r.u16();
                for (uint16_t i = 0; i < no && r.ok(); i++)
                    owners.push_back(r.u64());
                std::map<uint64_t, float> store;
                const uint16_t n = r.u16();
                for (uint16_t i = 0; i < n && r.ok(); i++) {
                    const uint64_t id = r.u64();
                    const float b = r.f32();
                    if (std::isfinite(b) && b >= 0)
                        store[id] = b;
                }
                if (r.ok()) {
                    g_saved = std::move(store);
                    g_game_owners = owners;
                    g_loads++;
                }
            }
        }
    }
    // the game's bar belongs to the party that hosted it last (owners; a save from before this file: whoever hosted
    // it, so a host of another party is the case to handle only when owners are known)
    const auto mine = idents_of_key(s, 0);
    const bool same = g_game_owners.empty() || std::any_of(mine.begin(), mine.end(), [](uint64_t id) {
        return std::find(g_game_owners.begin(), g_game_owners.end(), id) != g_game_owners.end();
    });
    if (same)
        return;
    // BUG-9279: promoted in place (it was a guest of this world): its bar is its own party's already, and the old host's
    // party's is the one in the file (the game's bar was never theirs here)
    if (g_warm_fix && g_guest_world && g_guest_world == modes::current_world()) {
        g_game_owners = mine;
        g_warm_kept++;
        H->log(M, SK_INFO, "party bar: hosting in place a planet it was a guest of: its own party's bar stays (%.0f) "
                           "(BUG-9279)", game::evolution_bar());
        return;
    }
    const float own = game::evolution_bar();
    if (std::isfinite(own))
        for (uint64_t id : g_game_owners)
            g_saved[id] = own;
    float want = 0;
    for (uint64_t id : mine)
        if (auto sv = g_saved.find(id); sv != g_saved.end()) {
            want = sv->second;
            break;
        }
    game::set_bar(want);
    g_swaps++;
    H->log(M, SK_INFO, "party bar: this planet's bar was another party's (%.0f): this host's party plays its own (%.0f) "
                       "(BUG-9277)", own, want);
}

std::string status()
{
    char b[200];
    snprintf(b, sizeof b, "party bars: %s, %zu other part%s, %u credits (%.0f), saves %u, loads %u, swaps %u, kept in place %u",
             g_fix ? (on() ? "on" : "idle") : "off", g_bars.size(), g_bars.size() == 1 ? "y" : "ies", g_credits,
             g_credited, g_saves, g_loads, g_swaps, g_warm_kept);
    std::string s = b;
    for (auto& [pk, v] : g_bars) {
        snprintf(b, sizeof b, "; %X=%.0f", pk, v);
        s += b;
    }
    return s;
}

} // namespace party_bar
