// The per-player store on the host's disk (E1 of docs/plan-0.7.15.md, owner decision 13; BUG-1149): what belongs to
// each player of a world (a guest's Space colonies, badges and rank, Rivals budgets and relations, the owners of Civ
// cities and vehicles, the owners of Tribe members) kept next to the host's save, so that it survives "both quit, the
// host loads again, the guest joins again". docs/01-architecture.md#arch-player-store.
//
// Before, the shared world was the host's and saved, but "whose is it" lived only in the host's memory (space_kept.h
// KeptStore, civ.cpp owners_kept, tribe.cpp owners): a host restart lost it.
//
// * One file per world: the host's save slot folder and planet file ("game3/madas.spo", world::current_game_key's
//   last two parts), the file `<Spore>/Games-sporecoop-store/<slot>/<planet>.txt` beside the Games folder (never in the
//   slot: a world share sends every file of the slot to the guests, and the game reads the slot's files).
// * Per player: the player's stable key (stable_key(): a salted 64-bit digest of net::Session::player_key, which is a
//   digest of its rejoin token; the token itself never reaches the disk), its last session name (shown in the status,
//   the fallback below) and the unix time it was last written. Per world: records that are no player's (the Rivals
//   mode, the Civ owner table of the cities, which names its owners by stable key).
// * Records are versioned per feature ("space.colonies" v1, ...): a reader takes only versions it knows; a record of a
//   feature or version it does not know is kept as it is and written back (a newer mod's record survives an older
//   mod's save).
// * Written atomically (a temporary file, then a replace) after every save of the game the host makes (the save hook,
//   outside the frame); read when the host's world opens. A damaged or foreign file (another world line, a bad check
//   sum) is an empty store; the damaged file is kept beside as `.bad` for a look.
//
// The player key rule (BUG-1107 / BUG-1109 stay fixed): a player is found by its key only. The rejoin token (rejoin.h)
// is the same game's across restarts (BUG-1135: kept on the guest's disk per host address and name, net.rejoin_month:
// 30 days), so the same player comes back as itself. A player whose token changed (another PC, a reinstall, another
// host address, more than 30 days) is a new player: its things are NOT given by name automatically (another game
// under the same name must not get them, BUG-1109). The host is told instead and may hand them over by its word:
// `coop.store adopt <player>` moves the records of the stored player of the same name to that player (adopt()).
//
// Portable (no Windows, no game): tests/store_tests.cpp runs it natively.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace pstore {

constexpr int kFormat = 1;                     // the file's format line "sporecoop-store 1"
constexpr size_t kMaxPlayers = 64;             // players per world: the ones seen longest ago go first
constexpr size_t kMaxRecordBytes = 256 * 1024; // one record
constexpr size_t kMaxFileBytes = 8u << 20;     // the whole file (read: a bigger file is no store)

// Record names reserved for 0.7.17 (WP-0 of docs/plan-0.7.17.md; declared only: nothing reads or writes them yet).
// The data is each player's (kept under its stable key); the WP decides between player records and one world record
// that names its players by stable key (as civ.owners does), and documents the layout in docs/01-architecture.md.
constexpr const char* kRecCivWallet = "civ.wallet"; // WP-M1 (feat/w68-wallet): a player's own Civ money (civ.own_money)
constexpr const char* kRecHistory = "history";      // WP-H1 (feat/w70-history): consequence cards, trait progress, archetype
constexpr const char* kRecProgress = "progress";    // WP-P1/P2 (feat/w69-progress): Cell food / spent, Creature DNA / bar /
                                                    // brain level; WP-R1 (feat/w71-revive): the progress debt column

struct Record {
    uint16_t ver = 0;
    std::vector<uint8_t> data;
    bool operator==(const Record& o) const { return ver == o.ver && data == o.data; }
};

struct Player {
    std::string name; // the session name when last written
    int64_t seen = 0; // unix time
    std::map<std::string, Record> recs;
};

// FNV-1a 32 (the file's check sum)
inline uint32_t fnv32(const std::string& s)
{
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s)
        h = (h ^ c) * 0x01000193u;
    return h;
}

// a 64-bit digest of `text` under `salt` (FNV-1a 64, then a finaliser): the stable key of a session player key
inline uint64_t digest(uint64_t salt, const std::string& text)
{
    uint64_t h = 0xCBF29CE484222325ull;
    auto add = [&h](uint8_t c) {
        h ^= c;
        h *= 0x100000001B3ull;
    };
    for (int i = 0; i < 8; i++)
        add((uint8_t)(salt >> (8 * i)));
    for (char c : text)
        add((uint8_t)c);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}

// the stored key of a session player key (net::Session::player_key: "t:<hash of the token>" or "n:<name>"):
// "s:<16 hex>"; the host itself (its own things: a city it owns) is "host"; "" stays ""
inline std::string stable_key(uint64_t salt, const std::string& player_key, bool is_host = false)
{
    if (is_host)
        return "host";
    if (player_key.empty())
        return "";
    char b[24];
    snprintf(b, sizeof b, "s:%016llx", (unsigned long long)digest(salt, player_key));
    return b;
}

// BUG-1424 (NET-12 of the 0.7.15.5 audit): what a read of a store file found. Unreadable: the file may be there but
// could not be opened or read now (a sharing violation of another program, access denied, an I/O error)
enum class Got { Text, Missing, Unreadable };

// the salt file (salt.txt): what to do with what its read found. Every stored player's key is digest(salt, token):
// a new salt over the old file loses every stored player of every world of that host, so a new one only when the
// file is not there or holds no salt (that one is kept as salt.txt.bad); unreadable: no keys now, read again later.
// fix false: as 0.7.16 (any failure made a new salt and overwrote the file).
enum class SaltStep { Use, MakeNew, NoKeys };
inline SaltStep salt_step(Got got, bool parsed, bool fix)
{
    if (got == Got::Text && parsed)
        return SaltStep::Use;
    if (fix && got == Got::Unreadable)
        return SaltStep::NoKeys;
    return SaltStep::MakeNew;
}

// a world's file: unreadable is no empty store (its first write would replace every stored player of the world);
// the world stays closed and is read again. fix false: as 0.7.16 (an empty store, then written over the file)
enum class WorldStep { Parse, Empty, Wait };
inline WorldStep world_step(Got got, bool fix)
{
    if (got == Got::Text)
        return WorldStep::Parse;
    return fix && got == Got::Unreadable ? WorldStep::Wait : WorldStep::Empty;
}

// the world name of a game key (world::current_game_key: "<save database dir>\<planet>.spo", lower case): its last
// two parts "<slot>/<planet>" ("game3/madas.spo"); "" when it has fewer
inline std::string world_of_game_key(const std::string& key)
{
    std::string k = key;
    for (auto& c : k)
        if (c == '\\')
            c = '/';
    while (!k.empty() && k.back() == '/')
        k.pop_back();
    size_t a = k.rfind('/');
    if (a == std::string::npos || a == 0)
        return "";
    size_t b = k.rfind('/', a - 1);
    std::string w = k.substr(b == std::string::npos ? 0 : b + 1);
    if (w.size() < 3 || w.front() == '/' || w.find('/') == std::string::npos || w.find("..") != std::string::npos)
        return "";
    for (auto& c : w)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    return w;
}

// text in a line: printable ASCII but ' ' and '%' as is, everything else %XX (UTF-8 names round-trip)
inline std::string esc(const std::string& s)
{
    std::string o;
    for (unsigned char c : s) {
        if (c > 0x20 && c < 0x7F && c != '%')
            o += (char)c;
        else {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            o += b;
        }
    }
    return o.empty() ? "%" : o; // "%" alone: the empty text
}
inline bool unesc(const std::string& s, std::string& out)
{
    out.clear();
    if (s == "%")
        return true;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '%') {
            out += s[i];
            continue;
        }
        if (i + 2 >= s.size())
            return false;
        unsigned v = 0;
        if (sscanf(s.substr(i + 1, 2).c_str(), "%2x", &v) != 1)
            return false;
        out += (char)v;
        i += 2;
    }
    return true;
}
inline std::string hex(const std::vector<uint8_t>& d)
{
    static const char* x = "0123456789abcdef";
    std::string o;
    o.reserve(d.size() * 2);
    for (uint8_t c : d) {
        o += x[c >> 4];
        o += x[c & 15];
    }
    return o.empty() ? "-" : o;
}
inline bool unhex(const std::string& s, std::vector<uint8_t>& out)
{
    out.clear();
    if (s == "-")
        return true;
    if (s.size() % 2 || s.size() / 2 > kMaxRecordBytes)
        return false;
    auto v = [](char c) -> int {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    };
    for (size_t i = 0; i < s.size(); i += 2) {
        int a = v(s[i]), b = v(s[i + 1]);
        if (a < 0 || b < 0)
            return false;
        out.push_back((uint8_t)(a << 4 | b));
    }
    return true;
}

// a feature name: [a-z0-9._-], 1..40 characters
inline bool feature_ok(const std::string& f)
{
    if (f.empty() || f.size() > 40)
        return false;
    for (char c : f)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return false;
    return true;
}
inline bool key_ok(const std::string& k)
{
    if (k == "host")
        return true;
    if (k.size() != 18 || k[0] != 's' || k[1] != ':')
        return false;
    for (size_t i = 2; i < k.size(); i++)
        if (!((k[i] >= '0' && k[i] <= '9') || (k[i] >= 'a' && k[i] <= 'f')))
            return false;
    return true;
}

enum class Parsed : uint8_t { Ok, Empty, Foreign, Damaged };
inline const char* parsed_name(Parsed p)
{
    switch (p) {
    case Parsed::Ok: return "ok";
    case Parsed::Empty: return "empty";
    case Parsed::Foreign: return "another world's";
    case Parsed::Damaged: return "damaged";
    }
    return "?";
}

class Store {
public:
    std::string world;                     // "<slot>/<planet>"
    int64_t saved = 0;                     // unix time of the write
    std::map<std::string, Record> wrecs;   // world records
    std::map<std::string, Player> players; // stable key -> player

    void reset(const std::string& w)
    {
        world = w;
        saved = 0;
        wrecs.clear();
        players.clear();
    }

    // the file's text for world `w`. Anything but this format (another version, another world, a cut or changed
    // file) is no store: the store is then empty for `w`
    Parsed parse(const std::string& text, const std::string& w)
    {
        reset(w);
        if (text.empty())
            return Parsed::Empty;
        if (text.size() > kMaxFileBytes)
            return Parsed::Damaged;
        // the check sum covers everything before the "end" line
        size_t endl = text.rfind("\nend ");
        if (endl == std::string::npos)
            return Parsed::Damaged;
        unsigned sum = 0;
        if (sscanf(text.c_str() + endl + 1, "end %8x", &sum) != 1 || sum != fnv32(text.substr(0, endl + 1)))
            return Parsed::Damaged;
        std::string body = text.substr(0, endl + 1);
        std::map<std::string, Record> wr;
        std::map<std::string, Player> pl;
        std::string fw;
        int64_t sv = 0;
        Player* cur = nullptr;
        size_t pos = 0;
        bool first = true;
        while (pos < body.size()) {
            size_t nl = body.find('\n', pos);
            std::string line = body.substr(pos, nl - pos);
            pos = nl + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            std::vector<std::string> t = split(line);
            if (first) {
                if (t.size() < 2 || t[0] != "sporecoop-store" || t[1] != std::to_string(kFormat))
                    return Parsed::Damaged;
                first = false;
                continue;
            }
            if (t[0] == "world" && t.size() == 2) {
                if (!unesc(t[1], fw))
                    return Parsed::Damaged;
            } else if (t[0] == "saved" && t.size() == 2) {
                sv = atoll(t[1].c_str());
            } else if (t[0] == "w" && t.size() == 4) {
                Record r;
                if (!feature_ok(t[1]) || !rec(t[2], t[3], r))
                    return Parsed::Damaged;
                wr[t[1]] = std::move(r);
            } else if (t[0] == "p" && t.size() == 4) {
                if (!key_ok(t[1]) || pl.size() >= kMaxPlayers)
                    return Parsed::Damaged;
                Player& p = pl[t[1]];
                p.seen = atoll(t[2].c_str());
                if (!unesc(t[3], p.name))
                    return Parsed::Damaged;
                cur = &p;
            } else if (t[0] == "r" && t.size() == 4) {
                Record r;
                if (!cur || !feature_ok(t[1]) || !rec(t[2], t[3], r))
                    return Parsed::Damaged;
                cur->recs[t[1]] = std::move(r);
            }
            // other lines: a newer format's, skipped
        }
        if (first)
            return Parsed::Damaged;
        if (fw != w)
            return Parsed::Foreign;
        saved = sv;
        wrecs = std::move(wr);
        players = std::move(pl);
        return Parsed::Ok;
    }

    std::string text() const
    {
        std::string o = "sporecoop-store " + std::to_string(kFormat) + "\n";
        o += "world " + esc(world) + "\n";
        o += "saved " + std::to_string((long long)saved) + "\n";
        for (auto& [f, r] : wrecs)
            o += "w " + f + " " + std::to_string(r.ver) + " " + hex(r.data) + "\n";
        for (auto& [k, p] : players) {
            if (p.recs.empty())
                continue; // a player with nothing kept is not written
            o += "p " + k + " " + std::to_string((long long)p.seen) + " " + esc(p.name) + "\n";
            for (auto& [f, r] : p.recs)
                o += "r " + f + " " + std::to_string(r.ver) + " " + hex(r.data) + "\n";
        }
        char b[24];
        snprintf(b, sizeof b, "end %08x\n", fnv32(o));
        return o + b;
    }

    // ---- per player ----
    // a player's record (any version: the caller checks r->ver); nullptr = none
    const Record* get(const std::string& key, const std::string& feature) const
    {
        auto p = players.find(key);
        if (p == players.end())
            return nullptr;
        auto r = p->second.recs.find(feature);
        return r == p->second.recs.end() ? nullptr : &r->second;
    }
    // true: changed. An empty `data` with `drop_empty` removes the record (a player with nothing left is not written)
    bool put(const std::string& key, const std::string& name, const std::string& feature, uint16_t ver,
             std::vector<uint8_t> data, int64_t now, bool drop_empty = false)
    {
        if (!key_ok(key) || !feature_ok(feature) || data.size() > kMaxRecordBytes)
            return false;
        if (drop_empty && data.empty())
            return erase(key, feature);
        Player& p = players[key];
        if (!name.empty())
            p.name = name;
        p.seen = std::max(p.seen, now);
        Record r{ver, std::move(data)};
        auto it = p.recs.find(feature);
        if (it != p.recs.end() && it->second == r)
            return false;
        p.recs[feature] = std::move(r);
        prune();
        return true;
    }
    bool erase(const std::string& key, const std::string& feature)
    {
        auto p = players.find(key);
        if (p == players.end())
            return false;
        bool e = p->second.recs.erase(feature) != 0;
        if (p->second.recs.empty())
            players.erase(p);
        return e;
    }
    // every player with a record of `feature` (stable keys)
    std::vector<std::string> keys_with(const std::string& feature) const
    {
        std::vector<std::string> v;
        for (auto& [k, p] : players)
            if (p.recs.count(feature))
                v.push_back(k);
        return v;
    }
    std::string name_of(const std::string& key) const
    {
        auto p = players.find(key);
        return p == players.end() ? std::string() : p->second.name;
    }

    // ---- world records ----
    const Record* get_world(const std::string& feature) const
    {
        auto r = wrecs.find(feature);
        return r == wrecs.end() ? nullptr : &r->second;
    }
    bool put_world(const std::string& feature, uint16_t ver, std::vector<uint8_t> data)
    {
        if (!feature_ok(feature) || data.size() > kMaxRecordBytes)
            return false;
        Record r{ver, std::move(data)};
        auto it = wrecs.find(feature);
        if (it != wrecs.end() && it->second == r)
            return false;
        wrecs[feature] = std::move(r);
        return true;
    }
    bool erase_world(const std::string& feature) { return wrecs.erase(feature) != 0; }

    // ---- the name fallback, on the host's word ----
    // the stored players of this session name (a session suffix " 2" .. " 9" of a second player of the name is not part
    // of it), except `but` (the player's own key)
    std::vector<std::string> by_name(const std::string& name, const std::string& but = "") const
    {
        std::string n = base_name(name);
        std::vector<std::string> v;
        for (auto& [k, p] : players)
            if (k != but && k != "host" && base_name(p.name) == n && !n.empty())
                v.push_back(k);
        return v;
    }
    // the records of `from` become `to`'s (where both have one, from's wins: it is the one the host hands over); the
    // world records that name `from` are the caller's (adopt_in() below for a key list). true: something moved
    bool adopt(const std::string& from, const std::string& to, const std::string& to_name, int64_t now)
    {
        if (from == to || !key_ok(to) || !players.count(from))
            return false;
        Player src = std::move(players[from]);
        players.erase(from);
        Player& d = players[to];
        for (auto& [f, r] : src.recs)
            d.recs[f] = std::move(r);
        d.name = to_name.empty() ? src.name : to_name;
        d.seen = std::max({d.seen, src.seen, now});
        return true;
    }

    size_t records() const
    {
        size_t n = wrecs.size();
        for (auto& [k, p] : players)
            n += p.recs.size();
        return n;
    }

    // "Bob 2" -> "Bob" (net::Session makes a second "Bob" unique with " 2" .. " 9")
    static std::string base_name(const std::string& n)
    {
        if (n.size() > 2 && n[n.size() - 2] == ' ' && n.back() >= '2' && n.back() <= '9')
            return n.substr(0, n.size() - 2);
        return n;
    }

private:
    static std::vector<std::string> split(const std::string& line)
    {
        std::vector<std::string> t;
        size_t a = 0;
        while (a < line.size()) {
            size_t b = line.find(' ', a);
            if (b == std::string::npos)
                b = line.size();
            if (b > a)
                t.push_back(line.substr(a, b - a));
            a = b + 1;
        }
        return t;
    }
    static bool rec(const std::string& ver, const std::string& data, Record& r)
    {
        int v = atoi(ver.c_str());
        if (v < 0 || v > 65535 || ver.empty() || ver.find_first_not_of("0123456789") != std::string::npos)
            return false;
        r.ver = (uint16_t)v;
        return unhex(data, r.data);
    }
    void prune()
    {
        while (players.size() > kMaxPlayers) {
            auto old = players.end();
            for (auto it = players.begin(); it != players.end(); ++it)
                if (it->first != "host" && (old == players.end() || it->second.seen < old->second.seen))
                    old = it;
            if (old == players.end())
                break;
            players.erase(old);
        }
    }
};

// ---- small codecs for the records (little endian) ----
struct Out {
    std::vector<uint8_t> d;
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v) { u8((uint8_t)v), u8((uint8_t)(v >> 8)); }
    void u32(uint32_t v) { u16((uint16_t)v), u16((uint16_t)(v >> 16)); }
    void i32(int32_t v) { u32((uint32_t)v); }
    void f32(float v)
    {
        uint32_t u = 0;
        static_assert(sizeof u == sizeof v);
        memcpy(&u, &v, 4);
        u32(u);
    }
    void str(const std::string& s)
    {
        size_t n = std::min<size_t>(s.size(), 255);
        u8((uint8_t)n);
        d.insert(d.end(), s.begin(), s.begin() + n);
    }
};
struct In {
    const std::vector<uint8_t>& d;
    size_t p = 0;
    bool ok = true;
    explicit In(const std::vector<uint8_t>& v) : d(v) {}
    uint8_t u8()
    {
        if (p >= d.size()) {
            ok = false;
            return 0;
        }
        return d[p++];
    }
    uint16_t u16()
    {
        uint16_t a = u8();
        return (uint16_t)(a | u8() << 8);
    }
    uint32_t u32()
    {
        uint32_t a = u16();
        return a | (uint32_t)u16() << 16;
    }
    int32_t i32() { return (int32_t)u32(); }
    float f32()
    {
        uint32_t u = u32();
        float f = 0;
        memcpy(&f, &u, 4);
        return f;
    }
    std::string str()
    {
        size_t n = u8();
        if (!ok || p + n > d.size()) {
            ok = false;
            return "";
        }
        std::string s(d.begin() + p, d.begin() + p + n);
        p += n;
        return s;
    }
    bool end() const { return ok && p == d.size(); }
};

} // namespace pstore
