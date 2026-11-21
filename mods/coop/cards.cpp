// BUG-9306 / decision 7Q A: each player's own stage cards (cards.h).
#include "cards.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "game.h"
#include "identity.h"
#include "modes.h"
#include "net/protocol.h"
#include "slotfs.h"
#include "world.h"

namespace cards {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

constexpr uint32_t kFileTag = 0x31445243u; // CRD1
constexpr uint8_t kReport = 0, kYours = 1;

// guest
static Own g_own;                   // own cards (caught here or sent back by the host)
static uint32_t g_written[kStages]; // the value wsync last wrote into the field (the host's)
static bool g_seen[kStages];        // a first look in this world happened (its value is the save's, not a graduation)
static uint32_t g_world;
static uint32_t g_caught, g_reports_tx, g_yours_rx;
// host
static std::map<uint64_t, Own> g_rec; // player identity -> its own cards
static std::string g_key;             // the save the records belong to
static uint32_t g_gen = 1, g_reports_rx, g_saved, g_unknown_id;
static std::map<uint8_t, uint32_t> g_sent_gen; // player -> generation last sent to it

static void* cplayer()
{
    void* nm = game::noun_manager();
    return nm ? *(void**)((uint8_t*)nm + 0x74) : nullptr;
}

static std::wstring side_path(const std::string& key)
{
    if (key.size() < 4 || key.compare(key.size() - 4, 4, ".spo")) return {};
    std::wstring planet = slotfs::widen(key);
    DWORD attr = GetFileAttributesW(planet.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return planet + L".duo-cards";
}

static void load(const std::string& key)
{
    g_rec.clear();
    g_key = key;
    ++g_gen;
    const std::wstring path = side_path(key);
    if (path.empty()) return;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    uint8_t b[4 + 1 + 64 * 29] = {};
    DWORD got = 0;
    const bool ok = ReadFile(f, b, sizeof b, &got, nullptr);
    CloseHandle(f);
    if (!ok) return;
    net::Reader r(b, got);
    if (r.u32() != kFileTag) return;
    const uint8_t n = r.u8();
    for (uint8_t i = 0; i < n && i < 64 && r.ok(); ++i) {
        const uint64_t id = r.u64();
        Own o;
        o.mask = r.u8();
        for (int s = 0; s < kStages; ++s) o.v[s] = r.u32();
        if (r.ok() && id && o.mask) g_rec[id] = o;
    }
    H->log(M, SK_INFO, "cards: %u founder record(s) of own stage cards read for this save (7Q A)", (unsigned)g_rec.size());
}

static bool save()
{
    const std::wstring path = side_path(g_key);
    if (path.empty()) return false;
    net::Writer w;
    w.u32(kFileTag);
    w.u8((uint8_t)std::min<size_t>(g_rec.size(), 64));
    size_t n = 0;
    for (auto& [id, o] : g_rec) {
        if (n++ >= 64) break;
        w.u64(id);
        w.u8(o.mask);
        for (int s = 0; s < kStages; ++s) w.u32(o.v[s]);
    }
    const std::wstring temp = path + L".duo-tmp-" + std::to_wstring(GetCurrentProcessId());
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, w.data.data(), (DWORD)w.data.size(), &wrote, nullptr) && wrote == w.data.size() && FlushFileBuffers(f);
    CloseHandle(f);
    if (ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING)) {
        ++g_saved;
        return true;
    }
    DeleteFileW(temp.c_str());
    return false;
}

static const char* stage_name(int s)
{
    static const char* n[kStages] = {"cell", "creature", "tribe", "civ", "space"};
    return s >= 0 && s < kStages ? n[s] : "?";
}

// host: player `p`'s own card for stage `s` (a report or a test command)
static bool host_record(uint8_t p, int s, uint32_t card, const char* how)
{
    const uint64_t id = identity::of(p);
    if (!id || s < 0 || s >= kStages) {
        ++g_unknown_id;
        H->log(M, SK_INFO, "cards: %s's own %s card %08X not kept: no identity for that player (7Q A)",
               S->name_of(p).c_str(), stage_name(s), card);
        return false;
    }
    if (g_key != world::current_game_key()) load(world::current_game_key());
    Own& o = g_rec[id];
    if ((o.mask & (1u << s)) && o.v[s] == card) return true;
    o.v[s] = card;
    o.mask |= (uint8_t)(1u << s);
    ++g_gen;
    const bool saved = save();
    H->log(M, SK_INFO, "cards: %s's own %s card %08X kept (%s; %s) (7Q A)", S->name_of(p).c_str(), stage_name(s), card, how,
           saved ? "saved with the world" : "not saved: the world has no save file yet");
    return true;
}

void stage_check(int i, uint32_t mine, uint32_t want)
{
    if (i < 0 || i >= kStages || !S || S->role() != net::Role::Client) return;
    const uint32_t world = modes::current_world();
    if (world != g_world) { // a load: the fields hold that save's values, not a graduation
        g_world = world;
        memset(g_seen, 0, sizeof g_seen);
    }
    if (g_seen[i] && mine != g_written[i] && mine) { // this game wrote a new card since wsync's last write: own play
        g_own.v[i] = mine;
        g_own.mask |= (uint8_t)(1u << i);
        ++g_caught;
        net::Writer w;
        w.u8(kReport);
        w.u8((uint8_t)i);
        w.u32(mine);
        if (S->send_reliable_to(0, net::MSG_CARDS, w)) ++g_reports_tx;
        H->log(M, SK_INFO, "cards: this game's own %s card %08X caught before the host's %08X replaced it, sent to the host (7Q A)",
               stage_name(i), mine, want);
    }
    g_seen[i] = true;
    g_written[i] = want;
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    if (kind == kReport && S->role() == net::Role::Host && from) {
        const uint8_t s = r.u8();
        const uint32_t card = r.u32();
        if (r.ok() && s < kStages && card) {
            ++g_reports_rx;
            host_record(from, s, card, "its game's graduation");
        }
        return;
    }
    if (kind == kYours && S->role() == net::Role::Client && from == 0) {
        Own o;
        o.mask = r.u8();
        for (int s = 0; s < kStages; ++s) o.v[s] = r.u32();
        if (!r.ok()) return;
        g_own = o;
        ++g_yours_rx;
        H->log(M, SK_INFO, "cards: the host's record of this player's own cards: %s (7Q A)", line(o).c_str());
    }
}

void tick(double)
{
    if (!S || S->role() != net::Role::Host) return;
    const std::string key = world::current_game_key();
    if (key != g_key) load(key);
    for (const auto& p : S->players()) { // each guest gets its record back (after a join, a load, a change)
        if (!p.id) continue;
        auto it = g_rec.find(identity::of(p.id));
        if (it == g_rec.end() || g_sent_gen[p.id] == g_gen) continue;
        net::Writer w;
        w.u8(kYours);
        w.u8(it->second.mask);
        for (int s = 0; s < kStages; ++s) w.u32(it->second.v[s]);
        if (S->send_reliable_to(p.id, net::MSG_CARDS, w)) g_sent_gen[p.id] = g_gen;
    }
}

void on_session_end()
{
    g_own = {};
    memset(g_seen, 0, sizeof g_seen);
    g_sent_gen.clear();
}

Own own() { return g_own; }
Own of_identity(uint64_t id)
{
    auto it = id ? g_rec.find(id) : g_rec.end();
    return it == g_rec.end() ? Own{} : it->second;
}

void effective(const Own& o, uint32_t out[kStages])
{
    void* p = cplayer();
    for (int s = 0; s < kStages; ++s)
        out[s] = (o.mask & (1u << s)) ? o.v[s] : p ? *(uint32_t*)((uint8_t*)p + 0x10FC + 4 * s) : 0;
}

uint32_t tribe_power_bits(const uint32_t c[kStages])
{
    // 0xD1F400 (Tribe mode): the cell and creature cards, each id one bit (docs/re/tribe.md#tribe-chief-powers)
    static const uint32_t id[6] = {0xE5F355B3u, 0xEE0C1239u, 0x5961D110u, 0xB6295724u, 0xC7981631u, 0x27CB4512u};
    uint32_t bits = 0;
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 6; ++k)
            if (c[s] == id[k]) bits |= 0x100000u << k;
    return bits;
}

std::string line(const Own& o)
{
    char b[160];
    snprintf(b, sizeof b, "mask=%02X cell=%08X creature=%08X tribe=%08X civ=%08X space=%08X", o.mask, o.v[0], o.v[1], o.v[2], o.v[3],
             o.v[4]);
    return b;
}

uint32_t generation() { return g_gen; }

// coop.cards [own <stage> <card> | set <player> <stage> <card> | clear <player>]
static int c_cards(void*, int argc, const char* const* argv, SkOut* out)
{
    const bool host = S && S->role() == net::Role::Host;
    if (argc >= 4 && !strcmp(argv[1], "own")) { // tests: as if this game had just graduated with that card
        const int s = atoi(argv[2]);
        const uint32_t card = (uint32_t)strtoul(argv[3], nullptr, 16);
        if (s < 0 || s >= kStages || !card) { H->out_printf(out, "own <stage 0..4> <card hex>\n"); return 0; }
        if (host) {
            const bool ok = host_record(0, s, card, "the test command");
            H->out_printf(out, "%s\n", ok ? "kept for the host's identity (the host's own seat uses its cPlayer anyway)" : "not kept");
            return 0;
        }
        g_own.v[s] = card;
        g_own.mask |= (uint8_t)(1u << s);
        net::Writer w;
        w.u8(kReport);
        w.u8((uint8_t)s);
        w.u32(card);
        H->out_printf(out, "own %s card %08X: %s\n", stage_name(s), card,
                      S && S->send_reliable_to(0, net::MSG_CARDS, w) ? "sent to the host" : "not sent");
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "set") && host) { // tests: player <id>'s own card
        const int s = atoi(argv[3]);
        const uint32_t card = (uint32_t)strtoul(argv[4], nullptr, 16);
        const bool ok = s >= 0 && s < kStages && card && host_record((uint8_t)atoi(argv[2]), s, card, "the test command");
        H->out_printf(out, "%s\n", ok ? "kept" : "not kept (set <player id> <stage 0..4> <card hex>; the player needs an identity)");
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "clear") && host) {
        const bool ok = g_rec.erase(identity::of((uint8_t)atoi(argv[2]))) > 0;
        if (ok) { ++g_gen; save(); }
        H->out_printf(out, "%s\n", ok ? "cleared" : "no record");
        return 0;
    }
    uint32_t eff[kStages];
    effective(g_own, eff);
    H->out_printf(out, "cards own %s caught=%u reports_tx=%u yours_rx=%u\n", line(g_own).c_str(), g_caught, g_reports_tx, g_yours_rx);
    H->out_printf(out, "effective cell=%08X creature=%08X tribe=%08X chief_power_bits=%08X\n", eff[0], eff[1], eff[2],
                  tribe_power_bits(eff));
    if (host) {
        H->out_printf(out, "host records=%u gen=%u reports_rx=%u saved=%u unknown_id=%u key=%s\n", (unsigned)g_rec.size(), g_gen,
                      g_reports_rx, g_saved, g_unknown_id, g_key.c_str());
        for (auto& [id, o] : g_rec) {
            const uint8_t p = identity::player_of(id);
            H->out_printf(out, "  %08X... %s %s\n", (unsigned)(id >> 32), p == 0xFF ? "(not here)" : S->name_of(p).c_str(),
                          line(o).c_str());
        }
    }
    return 0;
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    H->command_add(mod, "coop.cards",
                   "coop.cards [own <stage 0..4> <card hex> | set <player> <stage> <card hex> | clear <player>] - each player's own "
                   "stage cards (7Q A, BUG-9306): this game's, the host's records; own / set: tests (a graduation's card)",
                   c_cards, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace cards
