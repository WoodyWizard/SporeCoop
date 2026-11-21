// Space feature "empires" (MSG_SPACE kinds 100..109): every player's own empire in the Space stage, the last phase of
// "Separated Lives" (BUG-9279; the rules in space_empires_logic.h, docs/re/space.md#space-own-empires).
//
// Every partner who plays a species of their own (its party seat, party.h: its own creature, its own Civ nation) has
// an empire record of its own, kept by the host next to the planet's save (<planet>.spo.duoempires, never inside the
// game's save):
// * Host: a seat >= 1 with a species of its own and no empire yet gets one when the host's game is in Space (the
//   first Space world after the Civilization launch, or a save of an older version); its money: the host empire's
//   money at that moment. The host answers a guest's K_ASK with K_EMPIRE (have 1: its seat and money; have 0: it plays
//   the host's empire, the shared money as before), takes its K_REPORTs (its money every 2 s) and writes the file with
//   every save of the game. A player who is away keeps their empire and money in the file; they get them back when
//   they join again, also on another evening.
// * Guest: after every Space world it loads from the host it asks; with its own empire its game's money (the player
//   cEmpire +0xD0) is set to its own and shared.cpp stops sharing the Space money for it (own_money()). Its game earns
//   and spends by itself; nothing it does moves the host's money, and the host's purchases never move its money.
// * Diplomacy: a guest with its own empire keeps its own relations with the NPC empires (space_diplomacy.cpp shares
//   nothing for it). It sends its table to the host when it changes (K_DIPLO); the host keeps it by seat
//   (<planet>.spo.duoempdiplo, with every save) and gives it back after every Space world the guest loads
//   (K_DIPLO_SET, right after K_EMPIRE): a rejoin, also on another evening, keeps its own wars and alliances.
// * Its name: on its own game the guest's player empire is called "<player>'s empire" (the name the host's game gives
//   its allied mirror, space_allies.cpp), set back before every save of that game.
// * Rivals mode (space_rivalry.cpp) has budgets of its own: this feature waits while it is on.
// `coop.fix space.own_empires off`: nothing is founded or asked, the shared money of 0.7.15.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "fixes.h"
#include "founders.h"
#include "identity.h"
#include "modes.h"
#include "own_world.h"
#include "party.h"
#include "player_life.h"
#include "space_empires.h"
#include "space_empires_logic.h"
#include "space_ext.h"
#include "space_map.h"
#include "creations.h"
#include "sporekit_game.hpp"
#include "wire/space_empires.h"
#include "world.h"

using skg::cdecl_call;
using skg::thiscall;

namespace spaceempires {

enum Kind : uint8_t {
    K_ASK = 100,       // guest -> host {to 0, serial 0}: my Space world opened, which empire is mine?
    K_EMPIRE = 101,    // host -> guest {to, serial, have, seat, money}: your own empire and its money (have 0: the host's)
    K_REPORT = 102,    // guest -> host {to 0, serial answered, money}: my own empire's money now
    K_DIPLO = 103,     // guest -> host {to 0, serial answered, part}: my own empire's diplomacy table (one part)
    K_DIPLO_SET = 104, // host -> guest {to, serial of its K_EMPIRE, part}: your table as I kept it (count 0: none)
};

static const SkHost* H;
static SkMod* M;
static uintptr_t a_PlayerEmpire, a_StrAssign, a_Color, a_SetShip, a_SetCaptain;

static bool g_fix = true;
COOP_FIX("space.own_empires", "BUG-9279", g_fix,
         "space: a partner who plays a species of their own has their own empire and money (kept with the planet)");
// own ship and captain: on its game a partner's empire flies the partner's newest own ship design (or the one
// `coop.space empires ship <hex>` chose) and has the partner's own species as its captain, set again after every Space
// world it loads from the host (the host's save holds the host's), put back before a save
static bool g_ship_fix = true;
COOP_FIX("space.own_ship", "BUG-9279", g_ship_fix,
         "space: a partner's own empire flies the partner's own ship design and has the partner's species as captain");
static bool g_name_fix = true;
COOP_FIX("space.own_empire_name", "BUG-9279", g_name_fix,
         "space: a partner's own empire carries the partner's name and colour on the partner's game");

constexpr uintptr_t kColorId = 0x10, kColorRgb = 0x30; // cEmpire colour id, cached RGB floats (docs/re/space.md 15.1)
constexpr uintptr_t kName = 0x3C;
constexpr uintptr_t kShipKey = 0xB8, kCaptainKey = 0xC4; // cEmpire spacecraft / captain ResourceKey  // cEmpire name, eastl::string16 {begin, end, cap} (docs/re/space.md 13.1)
constexpr uintptr_t kMoney = 0xD0; // cEmpire money (int; docs/re/space.md 13.1)
constexpr double kAskEvery = 3.0, kReportEvery = 2.0, kFoundEvery = 1.0, kDiploEvery = 5.0, kDiploResend = 60.0;
const wchar_t* const kSuffix = L".duoempires";
const wchar_t* const kDiploSuffix = L".duoempdiplo";
constexpr uint32_t kDiploMagic = 0x31444D45u; // EMD1
constexpr size_t kDiploMax = wire::space_empires::kPartMax * wire::space_empires::kPartsMax;
constexpr size_t kDiploFileMax = 5 + kMaxSeats * (5 + kDiploMax);

namespace ew = wire::space_empires;

static void* player_empire()
{
    void* e = a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr;
    return e && !IsBadReadPtr(e, kMoney + 4) ? e : nullptr;
}
static int money_now()
{
    void* e = player_empire();
    return e ? *(int*)((uint8_t*)e + kMoney) : -1;
}
static bool set_money(int32_t v)
{
    void* e = player_empire();
    if (!e)
        return false;
    *(int*)((uint8_t*)e + kMoney) = clamp_money(v);
    return true;
}
static bool active() { return g_fix && spaceext::connected() && !spaceext::rivals(); }

static std::u16string name_of_empire(void* e)
{
    std::u16string s;
    if (!e)
        return s;
    const char16_t* b = *(const char16_t**)((uint8_t*)e + kName);
    const char16_t* en = *(const char16_t**)((uint8_t*)e + kName + 4);
    if (!b || en < b || en - b > 200 || IsBadReadPtr(b, (size_t)(en - b) * 2 + 2))
        return s;
    return std::u16string(b, en);
}
static void set_name(void* e, const std::u16string& v)
{
    if (e && a_StrAssign)
        thiscall<void>(a_StrAssign, (uint8_t*)e + kName, v.data(), v.data() + v.size());
}
static std::u16string utf16(const std::string& s)
{
    std::u16string o;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = (uint8_t)s[i];
        size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (n == 2 && i + 1 < s.size())
            c = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        else if (n == 3 && i + 2 < s.size())
            c = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        else if (n != 1)
            c = '?';
        o += (char16_t)(c > 0xFFFF ? '?' : c);
        i += n;
    }
    return o;
}
static std::string utf8(const std::u16string& s)
{
    std::string o;
    for (char16_t c : s) {
        if (c < 0x80)
            o += (char)c;
        else if (c < 0x800) {
            o += (char)(0xC0 | (c >> 6));
            o += (char)(0x80 | (c & 0x3F));
        } else {
            o += (char)(0xE0 | (c >> 12));
            o += (char)(0x80 | ((c >> 6) & 0x3F));
            o += (char)(0x80 | (c & 0x3F));
        }
    }
    return o;
}

static void send_parts(uint8_t kind, const std::vector<uint8_t>& bytes, uint8_t to, uint32_t serial, uint16_t version)
{
    for (auto& p : ew::parts_of(bytes, to, serial, version)) {
        net::Writer w;
        p.write(w);
        spaceext::send(kind, w);
    }
}

// ---------------- the host ----------------
static std::string g_planet;    // the planet whose roster g_roster is
static Roster g_roster;
static bool g_read;
static std::array<Wallet, kMaxSeats> g_wallet{}; // by seat: the guest's money as it reported it (this session)
static std::array<std::vector<uint8_t>, kMaxSeats> g_diplo{}; // by seat: its own diplomacy table (ew::table_bytes)
static std::array<ew::Joiner, kMaxSeats> g_diplo_in{};
static bool g_diplo_dirty;
static std::map<uint8_t, int> g_told;            // player -> the seat its last K_EMPIRE named (-1: the host's empire)
static uint32_t g_serial;
static uint16_t g_diplo_version;
static double g_next_found;
static std::string g_last_err;
// a player who left: its id goes to the next newcomer
PLAYER_STATE("space_empires", [](uint8_t id) { g_told.erase(id); }, [](uint8_t id) -> size_t { return g_told.count(id); });
static struct {
    uint32_t founded, asks, answers, reports, reports_bad, writes, write_fail, diplo_in, diplo_bad, diplo_out;
} g_n;

// EMD1: u32 magic, u8 n, n x {u8 seat, u32 len, len bytes (ew::table_bytes)}
static std::vector<uint8_t> diplo_encode()
{
    net::Writer w;
    w.u32(kDiploMagic);
    uint8_t n = 0;
    for (size_t s = 1; s < kMaxSeats; s++)
        n += !g_diplo[s].empty();
    w.u8(n);
    for (size_t s = 1; s < kMaxSeats; s++) {
        if (g_diplo[s].empty())
            continue;
        w.u8((uint8_t)s);
        w.u32((uint32_t)g_diplo[s].size());
        for (uint8_t b : g_diplo[s])
            w.u8(b);
    }
    return w.data;
}
static bool diplo_decode(const std::vector<uint8_t>& bytes)
{
    net::Reader r(bytes.data(), bytes.size());
    if (r.u32() != kDiploMagic)
        return false;
    std::array<std::vector<uint8_t>, kMaxSeats> v{};
    const unsigned n = r.u8();
    for (unsigned i = 0; i < n && r.ok(); i++) {
        const unsigned s = r.u8();
        const uint32_t len = r.u32();
        if (!r.ok() || s < 1 || s >= kMaxSeats || len > kDiploMax || len > r.left())
            return false;
        v[s].resize(len);
        for (uint32_t j = 0; j < len; j++)
            v[s][j] = r.u8();
        std::vector<wire::space_diplomacy::SnapRec> t;
        if (!ew::table_read(v[s], t))
            return false;
    }
    if (!r.ok() || r.left())
        return false;
    g_diplo = std::move(v);
    return true;
}

static void roster_load()
{
    const std::string planet = world::current_game_key();
    if (planet == g_planet && g_read)
        return;
    g_planet = planet;
    g_roster = {};
    g_wallet = {};
    g_diplo = {};
    g_diplo_in = {};
    g_diplo_dirty = false;
    g_told.clear();
    g_read = true;
    std::vector<uint8_t> bytes;
    if (!planet.empty() && founders::read_side(planet, kSuffix, bytes, kFileMax)) {
        if (decode(bytes, g_roster))
            H->log(M, SK_INFO, "empires: this planet's own empires: %s", describe(g_roster).c_str());
        else
            H->log(M, SK_WARN, "empires: the planet's empire file is not valid: ignored");
    }
    bytes.clear();
    if (!planet.empty() && founders::read_side(planet, kDiploSuffix, bytes, kDiploFileMax) && !diplo_decode(bytes))
        H->log(M, SK_WARN, "empires: the planet's empire diplomacy file is not valid: ignored");
}

static bool roster_write(const char* why)
{
    if (g_planet.empty() || !g_roster.seats())
        return false;
    if (!founders::write_side(g_planet, kSuffix, encode(g_roster))) {
        g_n.write_fail++;
        g_last_err = "could not write the empire file";
        H->log(M, SK_WARN, "empires: could not write the empire file (%s)", why);
        return false;
    }
    bool any = g_diplo_dirty;
    for (auto& d : g_diplo)
        any = any || !d.empty();
    if (any) {
        if (founders::write_side(g_planet, kDiploSuffix, diplo_encode()))
            g_diplo_dirty = false;
        else
            g_n.write_fail++;
    }
    g_n.writes++;
    H->log(M, SK_INFO, "empires: written (%s): %s", why, describe(g_roster).c_str());
    return true;
}

// the host's own seat: usually 0, but a planet played by other founders before has the host on a later seat (lane C's
// Kealeneon: the host on seat 2, seat 0 a founder of another PC)
static int host_seat()
{
    const int s = party::seat_of_player(spaceext::my_id());
    return s < 0 ? 0 : s;
}
static uint32_t host_species()
{
    const auto& seats = party::roster().seats;
    const int h = host_seat();
    return h < (int)seats.size() ? seats[h].species : 0;
}

// the seat of a seated player wants an empire of its own (a species of its own, not the host's)
static bool wants_empire(int seat)
{
    const auto& seats = party::roster().seats;
    if (seat < 1 || seat >= (int)kPlaySeats || seat >= (int)seats.size() || seat == host_seat())
        return false;
    return seats[seat].species && seats[seat].species != host_species();
}

static void send_empire(uint8_t player, int seat)
{
    ew::Empire m;
    m.to = player;
    m.serial = ++g_serial;
    m.have = g_roster.founded(seat);
    m.seat = m.have ? (uint8_t)seat : 0;
    m.money = m.have ? g_roster.e[seat].money : 0;
    m.species = m.have ? g_roster.e[seat].species : 0;
    if (m.have)
        g_wallet[seat].serial = m.serial;
    net::Writer w;
    m.write(w);
    spaceext::send(K_EMPIRE, w);
    g_told[player] = m.have ? seat : -1;
    g_n.answers++;
    if (m.have) { // its relations as it left them (count 0: none kept, the host save's are its start)
        send_parts(K_DIPLO_SET, g_diplo[seat], player, m.serial, ++g_diplo_version);
        g_n.diplo_out++;
    }
    H->log(M, SK_INFO, "empires: player %u (%s) told: %s", player, spaceext::name_of(player).c_str(),
           m.have ? ("its own empire, seat " + std::to_string(seat) + ", money " + std::to_string(m.money) +
                     ", diplomacy " + std::to_string(g_diplo[seat].size()) + " bytes")
                        .c_str()
                  : "the host's empire");
}

static void host_found()
{
    const auto& seats = party::roster().seats;
    std::vector<Seat> want;
    for (size_t s = 1; s < seats.size(); ++s)
        if ((int)s != host_seat()) // the host's own empire is the game's
            want.push_back({(int)s, seats[s].species, seats[s].player});
    const int seat = next_empire(g_roster, want, host_species());
    if (seat < 1)
        return;
    const int host_money = money_now();
    if (host_money < 0)
        return; // no player empire yet
    Empire& e = g_roster.e[seat];
    e.founded = true;
    e.player = seats[seat].player;
    e.species = seats[seat].species;
    e.money = start_money(host_money);
    if (!roster_valid(g_roster)) {
        e = {};
        g_last_err = "the empire of seat " + std::to_string(seat) + " would make the roster invalid";
        return;
    }
    g_n.founded++;
    roster_write("founded");
    const uint8_t p = party::player_of_seat(seat);
    std::string who = !seats[seat].name.empty() ? seats[seat].name : "seat " + std::to_string(seat);
    H->log(M, SK_INFO, "empires: %s's own empire founded (seat %d, species %08X, money %d)", who.c_str(), seat, e.species,
           e.money);
    spaceext::notice(who + spaceext::tr("'s own empire is founded: its own money, allied with yours",
                                        ": основана своя империя, свои деньги, союзник"));
    if (p != party::kNobody && g_told.count(p))
        send_empire(p, seat); // it plays now: its money is its own from now on
}

static void host_tick(double now)
{
    roster_load();
    if (!spaceext::in_space() || now < g_next_found)
        return;
    g_next_found = now + kFoundEvery;
    host_found();
}

// the save hook (any stage, before the game writes): the players' money goes into the empire file with the save
static void host_save()
{
    if (!g_fix || !spaceext::is_host() || !g_read || g_planet != world::current_game_key())
        return;
    if (g_roster.seats())
        roster_write("the game saves");
}

static void host_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    if (kind == K_ASK) {
        ew::Ask a;
        if (!a.read(r) || a.to != 0)
            return;
        g_n.asks++;
        if (!spaceext::in_space())
            return; // it asks again
        roster_load();
        const int seat = party::seat_of_player(origin);
        if (seat < 0)
            return; // not seated yet: it asks again
        if (wants_empire(seat) && !g_roster.founded(seat))
            return; // founded within a second: it asks again
        send_empire(origin, seat);
        return;
    }
    if (kind == K_REPORT) {
        ew::Report m;
        if (!m.read(r) || m.to != 0)
            return;
        const int seat = party::seat_of_player(origin);
        if (!g_roster.founded(seat) || !g_wallet[seat].report(m.serial, m.money)) {
            g_n.reports_bad++;
            return;
        }
        g_n.reports++;
        g_roster.e[seat].money = g_wallet[seat].money;
        return;
    }
    if (kind == K_DIPLO) {
        ew::Part p;
        const int seat = party::seat_of_player(origin);
        if (!p.read(r) || p.to != 0 || !g_roster.founded(seat) || !g_wallet[seat].serial ||
            p.serial != g_wallet[seat].serial) {
            g_n.diplo_bad++;
            return; // a table of a world the host has not answered yet: not its own
        }
        std::vector<uint8_t> bytes;
        if (!g_diplo_in[seat].add(p, bytes))
            return;
        std::vector<wire::space_diplomacy::SnapRec> t;
        if (!bytes.empty() && !ew::table_read(bytes, t)) {
            g_n.diplo_bad++;
            return;
        }
        if (bytes != g_diplo[seat]) {
            g_diplo[seat] = std::move(bytes);
            g_diplo_dirty = true;
        }
        g_n.diplo_in++;
        H->log(M, SK_INFO, "empires: seat %d's own diplomacy: %u empires", seat, (unsigned)t.size());
    }
}

bool own_money_player(uint8_t player)
{
    if (!active() || !spaceext::is_host())
        return false;
    auto it = g_told.find(player);
    return it != g_told.end() && it->second >= 1;
}

// ---------------- the guest ----------------
static bool g_own;          // the host said: my own empire (sticky for the session)
static int g_seat;
static uint32_t g_answer;   // the serial of the host's last K_EMPIRE
static uint32_t g_answered; // the Space world epoch the last K_EMPIRE was applied in (0: none)
static double g_next_ask, g_next_report, g_next_diplo, g_diplo_sent_at;
static bool g_told_player;
static uint32_t g_g_asks, g_g_sets, g_g_reports, g_g_diplo_sets, g_g_diplo_sent;
static bool g_diplo_ready;           // the host's K_DIPLO_SET of g_answer is applied: my table is my own from now
static ew::Joiner g_diplo_join;
static std::vector<uint8_t> g_diplo_last; // the table I sent last
static uint16_t g_g_version;
// the name and colour: the empire object I renamed, its game name and colour, the epoch I did it in
static void* g_renamed;
static std::u16string g_orig_name;
static uint32_t g_orig_color;
static float g_orig_rgb[3];
// the ship and captain: the game's keys before ours (put back before a save), ours, the species the host told
struct Key3 {
    uint32_t i = 0, t = 0, g = 0;
    bool operator==(const Key3&) const = default;
};
static Key3 g_orig_ship, g_orig_captain, g_my_ship, g_my_captain;
static bool g_ship_set, g_captain_set;
static uint32_t g_species;
static uint32_t g_ship_choice; // coop.space empires ship <hex>
static Key3& key_at(void* e, uintptr_t off) { return *(Key3*)((uint8_t*)e + off); }
static double g_next_ship_try;
// the ship: the chosen or the newest own ship design of this player's library (Empire_SetSpacecraftKey also gives the
// empire's other ships that model); false: none known yet
static bool apply_ship(void* e)
{
    Key3 ship{};
    for (auto& o : creations::options(creations::kUfo))
        if (!g_ship_choice || o.key.instance == g_ship_choice) {
            ship = {o.key.instance, o.key.type, o.key.group};
            break;
        }
    if (ship.i && !ship.t) // an option names the instance only: the ship type and group of every ship design
        ship.t = 0x476A98C7u, ship.g = 0x40656200u;
    static uint32_t s_told;
    // (not creations::key_known: a design of EditorSaves.package is no ResourceManager record under the ship key until
    // the game makes its model; a key the game cannot draw gets the default ship, gamefixes game.ufo_model)
    if (!ship.i || !a_SetShip) {
        if (ship.i && s_told != ship.i) {
            s_told = ship.i;
            H->log(M, SK_INFO, "empires: no ship setter for %08X/%08X/%08X", ship.i, ship.t, ship.g);
        }
        return false;
    }
    g_orig_ship = key_at(e, kShipKey);
    g_my_ship = ship;
    if (!(g_orig_ship == ship))
        thiscall<void>(a_SetShip, e, &ship);
    g_ship_set = true;
    H->log(M, SK_INFO, "empires: my ship %08X (the game's %08X)", ship.i, g_orig_ship.i);
    return true;
}
static void ship_task(void*)
{
    void* e = player_empire();
    if (e && g_renamed == e && !g_ship_set && g_own && g_ship_fix)
        apply_ship(e);
}
static uint32_t g_renamed_epoch;

bool own_money() { return active() && !spaceext::is_host() && g_own; }
int own_seat() { return own_money() ? g_seat : -1; }

static std::u16string own_name() { return utf16(spaceext::name_of(spaceext::my_id()) + "'s empire"); }

static void name_undo()
{
    if (!g_renamed)
        return;
    if (g_renamed == player_empire() && g_renamed_epoch == spaceext::world_epoch()) {
        if (g_ship_set && a_SetShip && key_at(g_renamed, kShipKey) == g_my_ship)
            thiscall<void>(a_SetShip, g_renamed, &g_orig_ship);
        if (g_captain_set && a_SetCaptain && key_at(g_renamed, kCaptainKey) == g_my_captain)
            thiscall<void>(a_SetCaptain, g_renamed, &g_orig_captain);
        g_ship_set = g_captain_set = false;
        set_name(g_renamed, g_orig_name);
        *(uint32_t*)((uint8_t*)g_renamed + kColorId) = g_orig_color;
        memcpy((uint8_t*)g_renamed + kColorRgb, g_orig_rgb, sizeof g_orig_rgb);
    }
    g_renamed = nullptr;
}
static void name_apply()
{
    if (!g_name_fix || g_renamed || !g_own || g_answered != spaceext::world_epoch())
        return;
    void* e = player_empire();
    if (!e)
        return;
    g_orig_name = name_of_empire(e);
    g_ship_set = g_captain_set = false;
    if (g_ship_fix) {
        apply_ship(e); // (the library's list may come later: guest_tick tries again)
        // the captain: this player's own species (a creature of its own library)
        const Key3 cap{g_species, 0x2B978C46u, 0x40626200u};
        if (g_species && a_SetCaptain && creations::key_known({cap.i, cap.t, cap.g})) {
            g_orig_captain = key_at(e, kCaptainKey);
            g_my_captain = cap;
            if (!(g_orig_captain == cap))
                thiscall<void>(a_SetCaptain, e, &cap);
            g_captain_set = true;
        }
        H->log(M, SK_INFO, "empires: my captain %08X (the game's %08X)", g_captain_set ? cap.i : 0u, g_orig_captain.i);
    }
    g_orig_color = *(uint32_t*)((uint8_t*)e + kColorId);
    memcpy(g_orig_rgb, (uint8_t*)e + kColorRgb, sizeof g_orig_rgb);
    set_name(e, own_name());
    // its colour: the game's palette colour nearest to this player's co-op colour (the host's game draws its allied
    // mirror empire in that colour too, space_allies.cpp)
    if (a_Color) {
        const uint32_t argb = spacemap::player_color(spaceext::my_id());
        uint32_t best = 0;
        float bd = 1e9f;
        for (uint32_t i = 0; i < 12; i++) {
            const float* c = cdecl_call<float*>(a_Color, 0x53DBCF3u + i);
            if (!c || IsBadReadPtr(c, 12))
                continue;
            float d = 0;
            for (int k = 0; k < 3; k++) {
                const float want = ((argb >> (16 - 8 * k)) & 0xFF) / 255.f;
                d += (c[k] - want) * (c[k] - want);
            }
            if (d < bd) {
                bd = d;
                best = 0x53DBCF3u + i;
            }
        }
        const float* rgb = best ? cdecl_call<float*>(a_Color, best) : nullptr;
        if (rgb && !IsBadReadPtr(rgb, 12)) {
            *(uint32_t*)((uint8_t*)e + kColorId) = best;
            memcpy((uint8_t*)e + kColorRgb, rgb, 12);
        }
    }
    g_renamed = e;
    g_renamed_epoch = spaceext::world_epoch();
    H->log(M, SK_INFO, "empires: my empire is called '%s' here (the game's name '%s')", utf8(own_name()).c_str(),
           utf8(g_orig_name).c_str());
}
static bool g_name_queued;
static void name_task(void*)
{
    g_name_queued = false;
    name_apply();
}

// the save hook on the guest: the game's own name goes into its save
static void guest_save()
{
    if (!spaceext::is_host())
        name_undo();
}

static void guest_tick(double now)
{
    if (own_world::space_other() || !spaceext::in_space() || !modes::current_world())
        return; // a Space save of the guest's own, or not known yet
    const uint32_t e = spaceext::world_epoch();
    if (!e)
        return;
    if (g_renamed && g_renamed_epoch != e)
        g_renamed = nullptr; // another world: that empire object is gone
    if (g_answered != e) {
        if (now < g_next_ask)
            return;
        g_next_ask = now + kAskEvery;
        ew::Ask a;
        net::Writer w;
        a.write(w);
        spaceext::send(K_ASK, w);
        g_g_asks++;
        return;
    }
    if (!g_own)
        return;
    if (!g_renamed && g_name_fix && !g_name_queued && player_empire()) {
        g_name_queued = true;
        H->run_outside_frame(name_task, nullptr); // after a save, or the first time in this world
    }
    if (g_ship_fix && g_renamed && !g_ship_set && now >= g_next_ship_try) {
        g_next_ship_try = now + 3.0;
        H->run_outside_frame(ship_task, nullptr);
    }
    if (g_diplo_ready && now >= g_next_diplo) {
        g_next_diplo = now + kDiploEvery;
        auto bytes = ew::table_bytes(diplo::own_table());
        if (bytes.size() <= kDiploMax && (bytes != g_diplo_last || now - g_diplo_sent_at > kDiploResend)) {
            send_parts(K_DIPLO, bytes, 0, g_answer, ++g_g_version);
            g_diplo_last = std::move(bytes);
            g_diplo_sent_at = now;
            g_g_diplo_sent++;
        }
    }
    if (now < g_next_report)
        return;
    g_next_report = now + kReportEvery;
    const int m = money_now();
    if (m < 0)
        return;
    ew::Report rep;
    rep.serial = g_answer;
    rep.money = m;
    net::Writer w;
    rep.write(w);
    spaceext::send(K_REPORT, w);
    g_g_reports++;
}

static void guest_message(uint8_t kind, net::Reader& r)
{
    if (kind == K_DIPLO_SET) {
        ew::Part p;
        if (!p.read(r) || p.to != spaceext::my_id() || p.serial != g_answer || !g_own)
            return;
        std::vector<uint8_t> bytes;
        if (!g_diplo_join.add(p, bytes))
            return;
        std::vector<wire::space_diplomacy::SnapRec> t;
        if (!bytes.empty()) {
            if (!ew::table_read(bytes, t) || !diplo::own_apply(t))
                return; // not in Space now: the next K_EMPIRE brings it again
        }
        g_diplo_ready = true;
        g_diplo_last = bytes;
        g_diplo_sent_at = H ? H->time_seconds() : 0;
        g_next_diplo = g_diplo_sent_at + kDiploEvery;
        g_g_diplo_sets++;
        H->log(M, SK_INFO, "empires: my own diplomacy from the host: %u empires", (unsigned)t.size());
        return;
    }
    if (kind != K_EMPIRE)
        return;
    ew::Empire m;
    if (!m.read(r) || m.to != spaceext::my_id())
        return;
    if (own_world::space_other() || !spaceext::in_space())
        return; // it asks again in the host's Space world
    if (m.have && !set_money(m.money))
        return; // no player empire now: it asks again
    g_own = m.have;
    g_seat = m.have ? m.seat : -1;
    g_species = m.have ? m.species : 0;
    g_answer = m.serial;
    g_answered = spaceext::world_epoch();
    g_next_report = 0;
    g_diplo_ready = false; // until its K_DIPLO_SET
    g_g_sets++;
    H->log(M, SK_INFO, "empires: %s", m.have ? ("my own empire, seat " + std::to_string(m.seat) + ", money " +
                                                std::to_string(m.money)).c_str()
                                             : "I play the host's empire (shared money)");
    if (m.have && !g_told_player) {
        g_told_player = true;
        spaceext::notice(spaceext::tr("Your own empire: your own money ($", "Ваша империя: свои деньги ($") +
                         std::to_string(m.money) + spaceext::tr("), your own diplomacy", "), своя дипломатия"));
    }
}

// ---------------- wiring ----------------
static void tick(double now)
{
    if (!active())
        return;
    if (spaceext::is_host())
        host_tick(now);
    else
        guest_tick(now);
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    if (!active())
        return;
    if (spaceext::is_host())
        host_message(kind, origin, r);
    else
        guest_message(kind, r);
}

// lifecycle-exempt: a Space feature (space_ext.h), run by spaceext::session_end_all from the spacecoop.session hook
static void on_session_end()
{
    name_undo();
    g_wallet = {};
    g_diplo_in = {};
    g_told.clear();
    g_own = false;
    g_seat = -1;
    g_answer = 0;
    g_answered = 0;
    g_next_ask = g_next_report = g_next_diplo = g_diplo_sent_at = 0;
    g_told_player = false;
    g_diplo_ready = false;
    g_diplo_join = {};
    g_diplo_last.clear();
}

static std::string status()
{
    char b[480];
    if (spaceext::is_host() || !spaceext::connected())
        snprintf(b, sizeof b,
                 "empires: fix=%d host roster=[%s] founded=%u asks=%u answers=%u reports=%u bad=%u writes=%u fail=%u"
                 " diplo in=%u bad=%u out=%u%s%s",
                 (int)g_fix, describe(g_roster).c_str(), g_n.founded, g_n.asks, g_n.answers, g_n.reports, g_n.reports_bad,
                 g_n.writes, g_n.write_fail, g_n.diplo_in, g_n.diplo_bad, g_n.diplo_out, g_last_err.empty() ? "" : " err=",
                 g_last_err.c_str());
    else
        snprintf(b, sizeof b,
                 "empires: fix=%d guest own=%d seat=%d money=%d name='%s' color=%08X serial=%u answered_epoch=%u epoch=%u asks=%u"
                 " sets=%u reports=%u diplo ready=%d sets=%u sent=%u ship=%08X captain=%08X",
                 (int)g_fix, (int)g_own, g_seat, money_now(), utf8(name_of_empire(player_empire())).c_str(),
                 player_empire() ? *(uint32_t*)((uint8_t*)player_empire() + kColorId) : 0u, g_answer,
                 g_answered, spaceext::world_epoch(), g_g_asks, g_g_sets, g_g_reports, (int)g_diplo_ready, g_g_diplo_sets,
                 g_g_diplo_sent, player_empire() ? key_at(player_empire(), kShipKey).i : 0u,
                 player_empire() ? key_at(player_empire(), kCaptainKey).i : 0u);
    return b;
}

static std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> out;
    if (!active())
        return out;
    if (!spaceext::is_host()) {
        if (g_own)
            out.push_back({spaceext::tr("Your own empire: your own money and diplomacy",
                                        "Ваша империя: свои деньги и дипломатия"),
                           "", nullptr, 0});
        return out;
    }
    const auto& seats = party::roster().seats;
    for (size_t s = 1; s < kMaxSeats; ++s) {
        if (!g_roster.e[s].founded)
            continue;
        std::string who = s < seats.size() && !seats[s].name.empty() ? seats[s].name : "seat " + std::to_string(s);
        const bool on = party::player_of_seat((int)s) != party::kNobody;
        out.push_back({who + spaceext::tr("'s own empire: $", ": своя империя: $") + std::to_string(g_roster.e[s].money) +
                           (on ? "" : spaceext::tr(" (away)", " (нет в игре)")),
                       "", nullptr, 0});
    }
    return out;
}

static std::string debug(const std::vector<std::string>& a)
{
    // coop.space empires [money <n> | write | found <player> <species>]: tests
    if (a.size() >= 2 && a[0] == "money") {
        const int v = atoi(a[1].c_str());
        return set_money(v) ? "empires: my money set to " + std::to_string(v) : "empires: no player empire";
    }
    if (a.size() >= 2 && a[0] == "ship" && !spaceext::is_host()) { // the ship design my empire flies (0: the newest)
        g_ship_choice = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        name_undo();
        return "empires: my ship choice " + a[1] + " (set again in a moment)";
    }
    if (!a.empty() && a[0] == "ships") {
        std::string s;
        for (auto& o : creations::options(creations::kUfo)) {
            char b[96];
            snprintf(b, sizeof b, "ship %08X '%s'\n", o.key.instance, o.name.c_str());
            s += b;
        }
        return s.empty() ? "no own ship designs\n" : s;
    }
    if (!a.empty() && a[0] == "write")
        return roster_write("test") ? "empires: written" : "empires: not written";
    // tests (a lab fixture without a guest on a species of its own): found <player id> <species hex>: that player's
    // seat gets an empire as if it played that species, then the player is told again
    if (a.size() >= 3 && a[0] == "found" && spaceext::is_host()) {
        const uint8_t p = (uint8_t)atoi(a[1].c_str());
        const int seat = party::seat_of_player(p);
        const int host_money = money_now();
        if (seat < 1 || seat >= (int)kPlaySeats || seat == host_seat() || g_roster.founded(seat) || host_money < 0)
            return "empires: no seat to found for player " + a[1];
        Empire& e = g_roster.e[seat];
        e = {true, party::roster().seats[seat].player, (uint32_t)strtoul(a[2].c_str(), nullptr, 16), start_money(host_money)};
        if (!roster_valid(g_roster)) {
            e = {};
            return "empires: not valid";
        }
        g_n.founded++;
        roster_write("test found");
        send_empire(p, seat);
        return "empires: seat " + std::to_string(seat) + " founded";
    }
    std::string s = status() + "\n";
    if (spaceext::is_host() || !spaceext::connected()) {
        const auto& seats = party::roster().seats;
        for (size_t i = 1; i < kMaxSeats; ++i) {
            if (!g_roster.e[i].founded)
                continue;
            std::vector<wire::space_diplomacy::SnapRec> t;
            if (!g_diplo[i].empty())
                ew::table_read(g_diplo[i], t);
            char b[240];
            snprintf(b, sizeof b, "empire seat=%zu name=%s species=%08X money=%d online=%d serial=%u diplo=%u\n", i,
                     i < seats.size() ? seats[i].name.c_str() : "?", g_roster.e[i].species, g_roster.e[i].money,
                     (int)(party::player_of_seat((int)i) != party::kNobody), g_wallet[i].serial, (unsigned)t.size());
            s += b;
        }
    }
    return s;
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    a_PlayerEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_StrAssign = H->sym("wstring_copy");
    a_Color = H->sym("Simulator::GetCachedColorFromId");
    a_SetShip = H->sym("SporeCoop::Empire_SetSpacecraftKey");
    a_SetCaptain = H->sym("SporeCoop::cEmpire_SetCaptainKey");
    spaceext::on_save(host_save);
    spaceext::on_save(guest_save);
    return a_PlayerEmpire != 0;
}

static bool reg = spaceext::add({.name = "empires",
                                 .kind_lo = 100,
                                 .kind_hi = 109,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = on_session_end,
                                 .rows = rows,
                                 .status = status,
                                 .debug = debug});

} // namespace spaceempires
