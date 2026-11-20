// The galaxy server (decision 5O A, docs/12-server-and-players.md#server-front-door): the one address every player
// connects to. Runs no Spore: it keeps the players (a slot each), the planets (who hosts each one now, who is on it)
// and relays the planet sessions' packets between a planet's host and its players (net/galaxy.h).
//
// usage: galaxy_server [--local] [--worlds <dir>] [--warm-handover] [--planet-players <n>] [--max-players <n>]
//                      [--max-per-address <n>] [--max-worlds <n>] [--max-world-mb <n>] [port] [galaxy name]
//        (port 47900 by default; --local: 127.0.0.1 only; worlds: the folder of stored worlds, "worlds" by default)
//
// Planets are made when a player enters one; the host is the first player on a planet. Decision 5Q A: the server keeps
// each planet's world (one file per planet in the worlds folder, kept across restarts): the host uploads it (its game
// saves and sends the save folder, every few minutes and when it leaves), and the next first player on the planet
// downloads it, loads it and hosts it. Players entering while the host is not hosting yet are asked to wait.
// Decision 5V A: when the host leaves (or times out) and players stay, the next one hosts the stored world (the host
// uploads it before it leaves) and the others join it once it is READY. Not yet: identities and bans across planets.
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../../mods/coop/net/galaxy.h"
#include "../../mods/coop/parties.h"

using namespace net;
using namespace net::galaxy;

struct Client {
    Endpoint ep;
    uint16_t slot = 0;
    std::string name, version, planet;
    uint64_t identity = 0;
    uint32_t last_ms = 0;
    uint32_t stage = 0; // 6K: the game mode it plays (its PARTY kPartyStage; 0 a menu or a load)
    uint32_t fetch_ms = 0;   // its last FETCH (one every kFetchEveryMs: each reads and checks a whole world)
    uint8_t ping_off = 0;    // pings in a row whose planet state disagrees with this server's (net/galaxy.h PING)
};
// limits (2026-11-18 review: nothing bounded the players, the worlds or a world's size); each has a switch
static size_t g_max_players = 1024, g_max_per_address = 64, g_max_worlds = 500;
static uint32_t g_max_world_bytes = 32u << 20;
constexpr uint32_t kFetchEveryMs = 3000, kDownloadGiveUpMs = 30000;
constexpr size_t kMaxPlanetName = 64, kMaxPlayerName = 32;

struct Planet {
    uint16_t host = 0;
    bool ready = false;         // its host's session hosts it (READY)
    uint64_t token = 0;         // 6B C: the last hand-over's token (players still in the world rejoin with it)
    std::set<uint16_t> members; // the host too
};

// a stored world (the worlds folder): file = GWS2, str8 planet, u32 bytes, u32 crc, u8 tech level, u32 x3 species key,
// the bytes (GWS1: without the tech level and species)
struct Stored {
    std::string file;
    uint32_t size = 0, crc = 0;
    uint8_t tech = 0;
    uint32_t species[3]{};
    int64_t at = 0; // 6I A: when it was stored (time(), 0: loaded from the folder at the start: its age unknown)
    std::string save; // step 4 part 6: the save's own planet name inside the world (blob_planet): a later upload must match
};
struct Meta {
    uint8_t tech = 0;
    uint32_t species[3]{};
    uint8_t era = 0; // (protocol 7: the stage the uploaded world is in)
};
constexpr int kKeepSaves = 3; // step 4 part 6: the older saves kept beside a planet's world (<file>.1 .. .3)
static uint32_t g_era_refused = 0, g_save_refused = 0, g_migrated = 0;
static std::map<uint16_t, Meta> g_upload_meta; // by player slot: the facts its upload carried
static std::map<std::string, Stored> g_stored;
static std::string g_worlds_dir = "worlds";
// 6S (BUG-9273): the key each identity first came with (kept in <worlds>/identities.txt) and the address cookie
static std::map<uint64_t, uint64_t> g_keys;
static uint64_t g_cookie_secret[2];
static uint32_t g_key_refused = 0, g_challenged = 0;
static uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
static uint64_t cookie_for(const net::Endpoint& ep, uint32_t bucket)
{
    return mix64(g_cookie_secret[0] ^ mix64(((uint64_t)ep.ip << 16) ^ ep.port) ^ mix64(bucket ^ g_cookie_secret[1]));
}
static std::string keys_path() { return g_worlds_dir + "/identities.txt"; }
static void load_keys()
{
    FILE* f = fopen(keys_path().c_str(), "r");
    if (!f)
        return;
    unsigned long long id = 0, key = 0;
    while (fscanf(f, "%llx %llx", &id, &key) == 2)
        if (id && key)
            g_keys[id] = key;
    fclose(f);
}
static void keep_key(uint64_t id, uint64_t key)
{
    g_keys[id] = key;
    if (FILE* f = fopen(keys_path().c_str(), "a")) {
        fprintf(f, "%016llx %016llx\n", (unsigned long long)id, (unsigned long long)key);
        fclose(f);
    }
}
static std::map<uint16_t, XferIn> g_uploads;    // by player slot
static std::map<uint16_t, XferOut> g_downloads; // by player slot
static uint32_t g_next_xfer = 1;
// decision 6B C research (--warm-handover): the new host of a hand-over keeps the world it plays (it was a guest of the
// old host) instead of loading the stored one: no reload for it
static bool g_warm_handover;
static uint8_t g_planet_players = kPlanetPlayers; // 6E B: the players a planet takes (--planet-players)

static UdpSocket g_sock;
static std::map<uint16_t, Client> g_clients; // by slot
static std::map<std::string, Planet> g_planets;
static std::string g_name = "SporeCoop galaxy";
static uint16_t g_next_slot = 1;
static uint64_t g_relay_bytes, g_relay_packets, g_relay_dropped;
static uint64_t g_fan_in_bytes, g_fan_out_bytes, g_fan_packets, g_fan_dropped; // 6F B

static void say(const char* fmt, ...)
{
    time_t t = time(nullptr);
    char ts[16];
    strftime(ts, sizeof ts, "%H:%M:%S", localtime(&t));
    printf("%s ", ts);
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
    fflush(stdout);
}

static void send(const Client& c, const Writer& w) { g_sock.send_to(c.ep, w.data.data(), w.data.size()); }

static Client* by_endpoint(const Endpoint& e)
{
    for (auto& [s, c] : g_clients)
        if (c.ep == e)
            return &c;
    return nullptr;
}

static uint16_t new_slot()
{
    for (int i = 0; i < 65535; i++) {
        uint16_t s = g_next_slot++;
        if (!g_next_slot)
            g_next_slot = 1;
        if (s && !g_clients.count(s))
            return s;
    }
    return 0;
}

static void leave_planet(Client& c, const char* why)
{
    g_uploads.erase(c.slot);
    g_downloads.erase(c.slot);
    if (c.planet.empty())
        return;
    auto it = g_planets.find(c.planet);
    if (it != g_planets.end()) {
        Planet& p = it->second;
        p.members.erase(c.slot);
        if (p.host == c.slot) {
            p.host = 0;
            p.ready = false;
            if (!p.members.empty()) { // 5V A: the hand-over: the next player hosts the stored world, the others wait
                const uint16_t next = *p.members.begin();
                p.host = next;
                p.token = ((uint64_t)rand() << 48) ^ ((uint64_t)rand() << 32) ^ ((uint64_t)rand() << 16) ^ (uint64_t)rand() ^
                          ((uint64_t)now_ms() << 20) ^ 1; // (never 0)
                auto st = g_stored.find(c.planet);
                Writer role = control(ROLE);
                role.str8(c.planet);
                role.u8(kHostRole);
                role.u16(next);
                role.u32(st == g_stored.end() || g_warm_handover ? 0 : st->second.size);
                role.u64(p.token);
                send(g_clients[next], role);
                for (uint16_t m : p.members)
                    if (m != next) {
                        Writer w = control(HOSTMOVE);
                        w.str8(c.planet);
                        w.u16(next);
                        w.u64(p.token);
                        send(g_clients[m], w);
                    }
                say("planet '%s': its host %u '%s' left (%s): %u '%s' hosts it now (%s), %zu player(s) wait",
                    c.planet.c_str(), c.slot, c.name.c_str(), why, next, g_clients[next].name.c_str(),
                    st == g_stored.end() || g_warm_handover ? "the world it plays" : "the world it plays, else the stored one", p.members.size() - 1);
            } else
                say("planet '%s': its host %u '%s' left (%s)", c.planet.c_str(), c.slot, c.name.c_str(), why);
        } else
            say("planet '%s': %u '%s' left (%s)", c.planet.c_str(), c.slot, c.name.c_str(), why);
        if (p.members.empty()) {
            say("planet '%s' is empty: forgotten", c.planet.c_str());
            g_planets.erase(it);
        }
    }
    c.planet.clear();
}

static std::string world_file(const std::string& planet)
{
    std::string safe;
    for (char ch : planet)
        safe += (isalnum((unsigned char)ch) || ch == '-' || ch == '_') ? ch : '_';
    char h[16];
    snprintf(h, sizeof h, "-%08X", crc32(0, (const uint8_t*)planet.data(), planet.size()));
    return g_worlds_dir + "/" + safe.substr(0, 40) + h + ".world";
}

static void load_stored()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(g_worlds_dir, ec);
    for (auto& e : fs::directory_iterator(g_worlds_dir, ec)) {
        if (e.path().extension() != ".world")
            continue;
        FILE* f = fopen(e.path().string().c_str(), "rb");
        if (!f)
            continue;
        uint8_t head[4 + 1 + 255 + 8 + 1 + 12 + 5 + 255]; // (GWS2: + the tech level and the species key; a 243+ byte name
                                                         // did not load; then the world's own first bytes: its save's name)
        const size_t n = fread(head, 1, sizeof head, f);
        fclose(f);
        Reader r(head, n);
        uint32_t magic = r.u32();
        std::string planet = r.str8();
        Stored st;
        st.size = r.u32();
        st.crc = r.u32();
        if (magic == 0x32535747u) {
            st.tech = r.u8();
            for (auto& v : st.species)
                v = r.u32();
        }
        if (!r.ok() || (magic != 0x31535747u && magic != 0x32535747u) || planet.empty())
            continue;
        st.save = blob_planet(std::vector<uint8_t>(head + (n - r.left()), head + n));
        st.file = e.path().string();
        if (!era_of(planet)) { // step 4 part 1: a world stored before eras goes to its stage's era (its tech level; 0: Cell)
            const uint8_t era = st.tech ? era_of_tech(st.tech) : kEraCell;
            const std::string key = era_key(planet, era), to = world_file(key);
            if (era && !g_stored.count(key)) {
                std::filesystem::rename(st.file, to, ec);
                if (!ec) {
                    say("stored world: '%s' is the era '%s' now", planet.c_str(), key.c_str());
                    planet = key;
                    st.file = to;
                    g_migrated++;
                }
                ec.clear();
            }
        }
        if (g_stored.count(planet))
            continue;
        g_stored[planet] = st;
        say("stored world: '%s' (%u KB)", planet.c_str(), st.size >> 10);
    }
}

static bool store_world(const std::string& planet, const XferIn& in, const Meta& meta, std::string& why)
{
    if (!g_stored.count(planet) && g_stored.size() >= g_max_worlds) {
        why = "the galaxy keeps no more planets (its limit is reached)";
        return false;
    }
    const std::string file = world_file(planet), tmp = file + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) {
        why = "the server cannot write its worlds folder";
        return false;
    }
    Writer w;
    w.u32(0x32535747u); // "GWS2"
    w.str8(planet);
    w.u32(in.size);
    w.u32(in.crc);
    w.u8(meta.tech);
    for (uint32_t v : meta.species)
        w.u32(v);
    bool ok = fwrite(w.data.data(), 1, w.data.size(), f) == w.data.size() &&
              (in.data.empty() || fwrite(in.data.data(), 1, in.data.size(), f) == in.data.size());
    ok = fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok && std::filesystem::exists(file, ec)) { // step 4 part 6: the older saves stay (a broken host's save can be undone)
        for (int k = kKeepSaves; k > 1; k--)
            std::filesystem::rename(file + "." + std::to_string(k - 1), file + "." + std::to_string(k), ec);
        std::filesystem::copy_file(file, file + ".1", std::filesystem::copy_options::overwrite_existing, ec);
    }
    ec.clear();
    if (ok)
        std::filesystem::rename(tmp, file, ec);
    if (!ok || ec) {
        std::filesystem::remove(tmp, ec);
        why = "the server could not save the world";
        return false;
    }
    Stored& st = g_stored[planet];
    st.file = file;
    st.size = in.size;
    st.crc = in.crc;
    st.at = (int64_t)time(nullptr);
    st.tech = meta.tech;
    memcpy(st.species, meta.species, sizeof st.species);
    st.save = blob_planet(in.data);
    return true;
}

static bool read_world(const Stored& st, std::vector<uint8_t>& out)
{
    FILE* f = fopen(st.file.c_str(), "rb");
    if (!f)
        return false;
    std::vector<uint8_t> all;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        all.insert(all.end(), buf, buf + n);
    fclose(f);
    Reader r(all.data(), all.size());
    const uint32_t magic = r.u32();
    r.str8();
    const uint32_t size = r.u32(), crc = r.u32();
    if (magic == 0x32535747u)
        r.skip(1 + 12);
    if (!r.ok() || r.left() != size)
        return false;
    out.assign(r.cur(), r.cur() + size);
    return crc32(0, out.data(), out.size()) == crc;
}

static void party_player_in(Client& c); // 6K (below)
static void party_player_out(const Client& c);
static void on_party(Client& c, Reader& r);
static void send_players(const Client& c);
static Client* by_identity(uint64_t id);
static void party_note(const Client& to, uint8_t kind, parties::Result r, const std::string& other);

// the planet's host hosts it now (its READY, or its pings say so: a READY is sent once and can be lost)
static void planet_ready(Client& c)
{
    auto it = g_planets.find(c.planet);
    if (it == g_planets.end() || it->second.host != c.slot || it->second.ready)
        return;
    it->second.ready = true;
    say("planet '%s': hosted by %u '%s'", c.planet.c_str(), c.slot, c.name.c_str());
    for (uint16_t m : it->second.members) // 5V A: the players who waited for the hand-over join it
        if (m != c.slot) {
            Writer w = control(ROLE);
            w.str8(c.planet);
            w.u8(kGuestRole);
            w.u16(c.slot);
            w.u32(0);
            w.u64(it->second.token);
            send(g_clients[m], w);
            say("planet '%s': %u '%s' joins its new host", c.planet.c_str(), m, g_clients[m].name.c_str());
        }
}

static size_t players_from(const Endpoint& e)
{
    size_t n = 0;
    for (auto& [s, c] : g_clients)
        n += c.ep.ip == e.ip;
    return n;
}

// a name as the players' games show it: one line, no control characters (2026-11-18 review: a name with a line break
// forged feed lines and friends.txt entries on other players' games)
static std::string clean_name(std::string s, size_t max)
{
    std::string out;
    for (unsigned char ch : s)
        if (ch >= 0x20 && ch != 0x7F)
            out += (char)ch;
    if (out.size() > max) {
        out.resize(max);
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) // (not half a UTF-8 letter)
            out.pop_back();
        if (!out.empty() && (unsigned char)out.back() >= 0xC0)
            out.pop_back();
    }
    return out;
}

static void on_control(const Endpoint& from, Reader& r)
{
    const uint8_t type = r.u8();
    Client* c = by_endpoint(from);
    if (type == HELLO) {
        const uint32_t proto = r.u32();
        const uint64_t identity = r.u64();
        std::string name = r.str8(), version = r.str8();
        if (!r.ok())
            return;
        const uint64_t key = r.left() >= 8 ? r.u64() : 0, cookie = r.left() >= 8 ? r.u64() : 0;
        if (proto != kProto) {
            Client tmp;
            tmp.ep = from;
            Writer w = control(REFUSED);
            w.str8("This galaxy server speaks another protocol: update SporeCoop Lineages.");
            send(tmp, w);
            return;
        }
        name = clean_name(name, kMaxPlayerName);
        version = clean_name(version, 48);
        if (!c) { // 6S: a new address proves it is real first (its cookie back): no slot, no list for a spoofed one
            const uint32_t bucket = now_ms() / 60000;
            if (!cookie || (cookie != cookie_for(from, bucket) && cookie != cookie_for(from, bucket - 1))) {
                Client tmp;
                tmp.ep = from;
                Writer w = control(CHALLENGE);
                w.u64(cookie_for(from, bucket));
                send(tmp, w);
                g_challenged++;
                return;
            }
        }
        if (identity) { // 6S: an identity is its first key's: another key is refused (nobody else can be you)
            auto k = g_keys.find(identity);
            if (k == g_keys.end() && key)
                keep_key(identity, key);
            else if (k != g_keys.end() && k->second != key) {
                Client tmp;
                tmp.ep = from;
                Writer w = control(REFUSED);
                w.str8(kIdentityTaken);
                send(tmp, w);
                if (g_key_refused++ < 20)
                    say("refused %s: identity %016llx with another key", from.masked().c_str(), (unsigned long long)identity);
                return;
            }
        }
        if (!c) {
            if (g_clients.size() >= g_max_players || players_from(from) >= g_max_per_address) {
                Client tmp;
                tmp.ep = from;
                Writer w = control(REFUSED);
                w.str8("This galaxy is full: try again later.");
                send(tmp, w);
                return;
            }
            const uint16_t s = new_slot();
            if (!s)
                return;
            Client& n = g_clients[s];
            n.slot = s;
            n.ep = from;
            c = &n;
        }
        c->name = name.empty() ? "Player" : name;
        c->version = version;
        c->identity = identity;
        c->last_ms = now_ms();
        Writer w = control(WELCOME);
        w.u16(c->slot);
        w.str8(g_name);
        w.u8(g_planet_players);
        send(*c, w);
        say("player %u '%s' connected from %s (%s)", c->slot, c->name.c_str(), from.masked().c_str(), version.c_str());
        party_player_in(*c); // 6K
        return;
    }
    if (!c) { // anything but HELLO from an unknown address
        Client tmp;
        tmp.ep = from;
        Writer w = control(REFUSED);
        w.str8(kNotConnected);
        send(tmp, w);
        return;
    }
    c->last_ms = now_ms();
    if (type == PARTY) { // 6K
        on_party(*c, r);
        return;
    }
    if (type == WHO) {
        send_players(*c);
        return;
    }
    if (type == WHISPER) { // 6L A: passed on to the player of that identity
        const uint64_t to = r.u64();
        const std::string text = r.str16();
        Client* o = r.ok() && !text.empty() && text.size() <= 600 ? by_identity(to) : nullptr;
        if (!o) {
            party_note(*c, 0, parties::Result::Unknown, "");
            return;
        }
        Writer w = control(WHISPER);
        w.u64(c->identity);
        w.str8(c->name);
        w.str16(text);
        send(*o, w);
        return;
    }
    if (type == PING) {
        send(*c, control(PONG));
        if (r.left() >= 2) { // its planet state (a LEAVE or a READY that was lost: followed after kPingMismatches pings)
            const std::string on = r.str8();
            const uint8_t state = r.u8();
            if (!r.ok())
                return;
            auto pl = g_planets.find(c->planet);
            const bool gone = on.empty() && state == kPingNone && !c->planet.empty();
            const bool hosts = state == kPingHosting && !c->planet.empty() && on == c->planet && pl != g_planets.end() &&
                               pl->second.host == c->slot && !pl->second.ready;
            if (!gone && !hosts)
                c->ping_off = 0;
            else if (++c->ping_off >= kPingMismatches) {
                c->ping_off = 0;
                if (gone)
                    leave_planet(*c, "its game says it left");
                else
                    planet_ready(*c);
            }
        }
    } else if (type == LIST) { // the planets with players, then the stored ones nobody is on
        std::vector<std::string> names;
        for (auto& [name, p] : g_planets)
            names.push_back(name);
        for (auto& [name, st] : g_stored)
            if (!g_planets.count(name))
                names.push_back(name);
        Writer w = control(PLANETS);
        const size_t n = names.size() < 40 ? names.size() : 40;
        w.u8((uint8_t)n);
        for (size_t i = 0; i < n; i++) {
            auto it = g_planets.find(names[i]);
            auto st = g_stored.find(names[i]);
            w.str8(names[i]);
            w.u16(it == g_planets.end() ? 0 : (uint16_t)it->second.members.size());
            w.u16(it == g_planets.end() ? 0 : it->second.host);
            w.u32(st == g_stored.end() ? 0 : st->second.size);
            w.u8(st == g_stored.end() ? 0 : st->second.tech);
            for (int k = 0; k < 3; k++)
                w.u32(st == g_stored.end() ? 0 : st->second.species[k]);
        }
        send(*c, w);
    } else if (type == ENTER) {
        std::string name = r.str8();
        if (!r.ok() || name.empty())
            return;
        if (name.size() > kMaxPlanetName || clean_name(name, kMaxPlanetName) != name) {
            Writer w = control(REFUSED);
            w.str8("That planet name cannot be used (too long, or odd characters).");
            send(*c, w);
            return;
        }
        c->ping_off = 0;
        auto ex = g_planets.find(name);
        if (ex != g_planets.end() && ex->second.host && !ex->second.ready && ex->second.host != c->slot) {
            Writer w = control(REFUSED);
            w.str8(name + " is being loaded by its host: enter again in a moment.");
            send(*c, w);
            return;
        }
        if (ex != g_planets.end() && !ex->second.members.count(c->slot) &&
            ex->second.members.size() >= g_planet_players) { // 6E B: a planet takes g_planet_players
            Writer w = control(REFUSED);
            w.str8(name + " is full (" + std::to_string((int)g_planet_players) + " players): pick another planet or try again later.");
            send(*c, w);
            return;
        }
        leave_planet(*c, "entered another planet");
        Planet& p = g_planets[name];
        c->planet = name;
        p.members.insert(c->slot);
        Writer w = control(ROLE);
        w.str8(name);
        if (!p.host) {
            p.host = c->slot;
            w.u8(kHostRole);
            w.u16(c->slot);
            auto st = g_stored.find(name);
            w.u32(st == g_stored.end() ? 0 : st->second.size);
            w.u64(0);
            w.u32(st == g_stored.end() ? 0 : st->second.crc);
            p.token = 0;
            say("planet '%s': %u '%s' hosts it%s", name.c_str(), c->slot, c->name.c_str(),
                st == g_stored.end() ? " (a new planet: its loaded world)" : " (from its stored world)");
        } else {
            w.u8(kGuestRole);
            w.u16(p.host);
            auto st = g_stored.find(name); // 6I A: the stored world on offer (the guest may load it first)
            w.u32(st == g_stored.end() ? 0 : st->second.size);
            w.u64(0);
            w.u32(st == g_stored.end() ? 0 : st->second.crc);
            const int64_t age = st == g_stored.end() || !st->second.at ? -1 : (int64_t)time(nullptr) - st->second.at;
            w.u32(age < 0 ? 0xFFFFFFFFu : (uint32_t)age); // its age in seconds (unknown: all ones)
            say("planet '%s': %u '%s' joins host %u '%s'", name.c_str(), c->slot, c->name.c_str(), p.host,
                g_clients[p.host].name.c_str());
        }
        send(*c, w);
    } else if (type == LEAVE) {
        leave_planet(*c, "left");
    } else if (type == READY) {
        planet_ready(*c);
    } else if (type == FETCH) { // a player of the planet wants its stored world (its host; 6I A: a guest too)
        auto it = g_planets.find(c->planet);
        auto st = g_stored.find(c->planet);
        std::vector<uint8_t> data;
        const uint32_t now = now_ms();
        if (auto cur = g_downloads.find(c->slot); cur != g_downloads.end() && cur->second.planet == c->planet)
            return; // (its world is on its way already: a FETCH again)
        if (c->fetch_ms && now - c->fetch_ms < kFetchEveryMs)
            return; // (each FETCH reads and checks a whole world)
        c->fetch_ms = now ? now : 1;
        if (it == g_planets.end() || !it->second.members.count(c->slot) || st == g_stored.end() ||
            !read_world(st->second, data)) {
            Writer w = control(REFUSED);
            w.str8("The galaxy server has no stored world of " + c->planet + " for you.");
            send(*c, w);
            return;
        }
        XferOut& x = g_downloads[c->slot];
        x.start(g_next_xfer++, c->planet, std::move(data));
        x.begin_ms = x.ack_ms = now_ms();
        send(*c, x.begin());
        say("planet '%s': sending its world to %u '%s' (%u KB)", c->planet.c_str(), c->slot, c->name.c_str(),
            st->second.size >> 10);
    } else if (type == XACK) {
        const uint32_t id = r.u32(), idx = r.u32();
        auto it = g_downloads.find(c->slot);
        if (r.ok() && it != g_downloads.end() && it->second.id == id) {
            it->second.on_ack(idx);
            it->second.ack_ms = now_ms();
        }
    } else if (type == XDONE) {
        const uint32_t id = r.u32();
        const uint8_t ok = r.u8();
        std::string why = r.str8();
        auto it = g_downloads.find(c->slot);
        if (it != g_downloads.end() && it->second.id == id) {
            say("planet '%s': %u '%s' %s its world%s%s (%u chunks resent)", it->second.planet.c_str(), c->slot,
                c->name.c_str(), ok ? "got" : "could not use", why.empty() ? "" : ": ", why.c_str(), it->second.resent);
            g_downloads.erase(it);
        }
    } else if (type == XBEGIN) { // an upload: only the planet's host, for its planet
        XferIn in;
        const bool fits = in.header(r, g_max_world_bytes); // (nothing allocated before the sender is checked)
        if (!r.ok())
            return;
        auto cur = g_uploads.find(c->slot);
        if (cur != g_uploads.end() && cur->second.id == in.id)
            return; // (its begin again)
        auto it = g_planets.find(c->planet);
        const bool host = !c->planet.empty() && in.planet == c->planet && it != g_planets.end() && it->second.host == c->slot;
        if (!host || !fits) {
            Writer w = control(XDONE);
            w.u32(in.id);
            w.u8(0);
            w.str8(!host ? "only the planet's host stores its world" : "this world is larger than the galaxy takes");
            send(*c, w);
            return;
        }
        Meta m;
        if (r.left() >= 13) { // the menu's facts of the planet (5X B)
            m.tech = r.u8();
            for (auto& v : m.species)
                v = r.u32();
            m.era = r.left() >= 1 ? r.u8() : 0;
        }
        // step 4 part 1: an era keeps worlds of its stage only (a host whose game went on to the next stage does not
        // turn the planet's Creature era into a Tribe world: its party moves to the Tribe era instead)
        const uint8_t key_era = era_of(in.planet);
        if (key_era && m.era != key_era) {
            Writer w = control(XDONE);
            w.u32(in.id);
            w.u8(0);
            w.str8(std::string("this world is in the ") + (m.era ? era_name(m.era) : "unknown") + " stage, not " +
                   era_name(key_era));
            send(*c, w);
            if (g_era_refused++ < 20)
                say("planet '%s': refused a world of another stage (%s) from %u '%s'", in.planet.c_str(),
                    m.era ? era_name(m.era) : "unknown", c->slot, c->name.c_str());
            return;
        }
        g_upload_meta[c->slot] = m;
        in.alloc();
        g_uploads[c->slot] = std::move(in);
    } else if (type == XCHUNK) {
        const uint32_t id = r.u32(), idx = r.u32();
        auto it = g_uploads.find(c->slot);
        if (!r.ok() || it == g_uploads.end() || it->second.id != id)
            return;
        XferIn& in = it->second;
        const bool was = in.complete();
        if (!in.chunk(idx, r))
            return;
        Writer a = control(XACK);
        a.u32(id);
        a.u32(idx);
        send(*c, a);
        if (was || !in.complete())
            return;
        std::string why;
        const bool valid = in.valid();
        // step 4 part 6: a planet's stored world is only replaced by a save of the same planet (another save uploaded
        // over it, by mistake or on purpose, is refused)
        auto prev = g_stored.find(in.planet);
        const std::string got = valid ? blob_planet(in.data) : "";
        const bool same = prev == g_stored.end() || prev->second.save.empty() || got == prev->second.save;
        if (valid && !same) {
            why = "this is the save of another planet (" + got + "), not " + prev->second.save;
            g_save_refused++;
        }
        const bool ok = valid && same && store_world(in.planet, in, g_upload_meta[c->slot], why);
        if (!valid)
            why = "the world arrived damaged";
        Writer w = control(XDONE);
        w.u32(id);
        w.u8(ok ? 1 : 0);
        w.str8(why);
        send(*c, w);
        say("planet '%s': %s its world from %u '%s' (%u KB)%s%s", in.planet.c_str(), ok ? "stored" : "did not store",
            c->slot, c->name.c_str(), in.size >> 10, why.empty() ? "" : ": ", why.c_str());
        in.data.clear(); // (kept as complete: late chunks are acked again)
        in.data.shrink_to_fit();
    }
}


// ---------------- decision 6K: the parties, kept here (parties.h) ----------------
static parties::BasicBook<uint16_t> g_parties;
// a player that dropped (timed out, reconnected under another slot): its party for a while, by identity
struct PartyGhost {
    uint16_t party = 0;
    uint32_t at = 0;
};
static std::map<uint64_t, PartyGhost> g_party_ghosts;
static uint64_t g_party_requests, g_party_views;
constexpr uint32_t kPartyGhostMs = 5 * 60 * 1000;

static Client* by_identity(uint64_t id)
{
    if (!id)
        return nullptr;
    for (auto& [s, c] : g_clients)
        if (c.identity == id)
            return &c;
    return nullptr;
}

static void send_party_view(uint16_t slot)
{
    auto it = g_clients.find(slot);
    if (it == g_clients.end())
        return;
    const auto* q = g_parties.find(slot);
    Writer w = control(PARTYVIEW);
    w.u16(q ? q->id : 0);
    w.u16(q ? q->leader : slot);
    const size_t n = q ? q->members.size() : 0;
    w.u8((uint8_t)n);
    for (size_t i = 0; i < n; i++) {
        const Client& m = g_clients[q->members[i]];
        w.u16(m.slot);
        w.u64(m.identity);
        w.str8(m.name);
        w.str8(m.planet);
        w.u32(m.stage);
    }
    const int from = g_parties.pending_from(slot);
    const Client* f = from > 0 && g_clients.count((uint16_t)from) ? &g_clients[(uint16_t)from] : nullptr;
    w.u16(f ? f->slot : 0);
    w.u64(f ? f->identity : 0);
    w.str8(f ? f->name : "");
    send(it->second, w);
    g_party_views++;
}

// the views of every member of these players' parties (after a change)
static void send_party_views(std::initializer_list<uint16_t> slots)
{
    std::set<uint16_t> to;
    for (uint16_t s : slots)
        if (const auto* q = g_parties.find(s))
            to.insert(q->members.begin(), q->members.end());
        else
            to.insert(s);
    for (uint16_t s : to)
        send_party_view(s);
}

static void party_note(const Client& to, uint8_t kind, parties::Result r, const std::string& other)
{
    Writer w = control(PARTYNOTE);
    w.u8(kind);
    w.u8((uint8_t)r);
    w.str8(other);
    send(to, w);
}

// a player came (WELCOME): its own party, or the one it had when it dropped (a reconnect)
static void party_player_in(Client& c)
{
    if (Client* old = by_identity(c.identity); old && old->slot != c.slot) { // the same game under a new slot
        if (const auto* q = g_parties.find(old->slot); q && q->members.size() > 1)
            g_party_ghosts[c.identity] = {q->id, now_ms()};
        g_parties.remove(old->slot);
    }
    if (g_parties.find(c.slot))
        return;
    auto g = g_party_ghosts.find(c.identity);
    if (g != g_party_ghosts.end() && now_ms() - g->second.at < kPartyGhostMs && g_parties.rejoin(c.slot, g->second.party)) {
        say("party: %u '%s' is back in its party %u", c.slot, c.name.c_str(), g->second.party);
        g_party_ghosts.erase(g);
        send_party_views({c.slot});
        return;
    }
    g_parties.add(c.slot);
    send_party_view(c.slot);
}

// a player went (timed out): out of its party (the longest standing member leads), its place kept for a while
static void party_player_out(const Client& c)
{
    const auto* q = g_parties.find(c.slot);
    std::vector<uint16_t> others;
    if (q) {
        for (uint16_t m : q->members)
            if (m != c.slot)
                others.push_back(m);
        if (!others.empty() && c.identity)
            g_party_ghosts[c.identity] = {q->id, now_ms()};
    }
    g_parties.remove(c.slot);
    for (uint16_t m : others)
        send_party_view(m);
}

static void on_party(Client& c, Reader& r)
{
    const uint8_t kind = r.u8();
    const uint64_t other_id = r.u64();
    const uint8_t flag = r.u8();
    const uint32_t stage = r.u32();
    if (!r.ok())
        return;
    c.stage = stage;
    g_party_requests++;
    if (kind == kPartyStage) {
        send_party_views({c.slot}); // (the members see the stage)
        return;
    }
    Client* o = other_id ? by_identity(other_id) : nullptr;
    const std::string oname = o ? o->name : "";
    const uint32_t now = now_ms();
    parties::Result res = parties::Result::Unknown;
    switch (kind) {
    case kPartyInvite:
        if (!o)
            break;
        res = g_parties.invite(c.slot, o->slot, c.stage, o->stage, now / 1000.0);
        if (res == parties::Result::Ok) {
            say("party: %u '%s' invites %u '%s'", c.slot, c.name.c_str(), o->slot, o->name.c_str());
            send_party_view(o->slot); // (its view carries the invite)
        }
        break;
    case kPartyAnswer: {
        if (!o)
            break;
        const uint16_t old_party = g_parties.find(c.slot) ? g_parties.find(c.slot)->id : 0;
        std::vector<uint16_t> old_members;
        if (const auto* q = g_parties.find(c.slot))
            old_members = q->members;
        res = g_parties.answer(c.slot, o->slot, flag != 0, now / 1000.0);
        if (res != parties::Result::NoInvite && res != parties::Result::Unknown) // (not for an answer to no invite:
            party_note(*o, kPartyAnswer, res, c.name); //  anyone could send "X declined" lines to anyone)
        if (res == parties::Result::Ok) {
            say("party: %u '%s' joined %u '%s''s party", c.slot, c.name.c_str(), o->slot, o->name.c_str());
            for (uint16_t m : old_members)
                if (m != c.slot)
                    send_party_view(m);
        }
        (void)old_party;
        send_party_views({c.slot, o->slot});
        if (res == parties::Result::Declined)
            return;
        break;
    }
    case kPartyLeave: {
        std::vector<uint16_t> was;
        if (const auto* q = g_parties.find(c.slot))
            was = q->members;
        res = g_parties.leave(c.slot);
        for (uint16_t m : was)
            send_party_view(m);
        break;
    }
    case kPartyLead:
        if (!o)
            break;
        res = g_parties.set_leader(c.slot, o->slot);
        if (res == parties::Result::Ok)
            send_party_views({c.slot});
        break;
    case kPartyKick: {
        if (!o)
            break;
        std::vector<uint16_t> was;
        if (const auto* q = g_parties.find(c.slot))
            was = q->members;
        res = g_parties.kick(c.slot, o->slot);
        if (res == parties::Result::Ok) {
            say("party: %u '%s' removed %u '%s'", c.slot, c.name.c_str(), o->slot, o->name.c_str());
            party_note(*o, kPartyKick, parties::Result::Kicked, c.name);
            for (uint16_t m : was)
                send_party_view(m);
        }
        break;
    }
    default:
        return;
    }
    party_note(c, kind, res, oname);
}

static void send_players(const Client& c)
{
    Writer w = control(PLAYERS);
    std::vector<const Client*> v;
    for (auto& [s, o] : g_clients)
        v.push_back(&o);
    size_t n = 0, bytes = 3;
    for (; n < v.size(); n++) { // (one datagram: as many as fit)
        const size_t b = 2 + 8 + 1 + v[n]->name.size() + 1 + v[n]->planet.size() + 2;
        if (bytes + b > 1150)
            break;
        bytes += b;
    }
    w.u16((uint16_t)n);
    for (size_t i = 0; i < n; i++) {
        const auto* q = g_parties.find(v[i]->slot);
        w.u16(v[i]->slot);
        w.u64(v[i]->identity);
        w.str8(v[i]->name);
        w.str8(v[i]->planet);
        w.u16(q ? q->id : 0);
    }
    send(c, w);
}

static void on_relay(const Endpoint& from, const uint8_t* data, size_t n)
{
    Client* c = by_endpoint(from);
    if (!c || n < 3) {
        g_relay_dropped++;
        return;
    }
    c->last_ms = now_ms();
    uint16_t to;
    std::memcpy(&to, data + 1, 2);
    auto it = g_clients.find(to);
    // only within one planet, and only between that planet's host and one of its players
    if (it == g_clients.end() || c->planet.empty() || it->second.planet != c->planet) {
        g_relay_dropped++;
        return;
    }
    const Planet& p = g_planets[c->planet];
    if (p.host != c->slot && p.host != to) {
        g_relay_dropped++;
        return;
    }
    std::vector<uint8_t> out(data, data + n);
    std::memcpy(out.data() + 1, &c->slot, 2); // (to the receiver: where it came from)
    g_sock.send_to(it->second.ep, out.data(), out.size());
    g_relay_bytes += n;
    g_relay_packets++;
}

// decision 6F B: the planet's host sends one message for several of its players (net/galaxy.h kFan)
static void on_fan(const Endpoint& from, const uint8_t* data, size_t n)
{
    Client* c = by_endpoint(from);
    if (!c || n < 3 || c->planet.empty()) {
        g_fan_dropped++;
        return;
    }
    c->last_ms = now_ms();
    auto pl = g_planets.find(c->planet);
    if (pl == g_planets.end() || pl->second.host != c->slot) { // only the planet's host
        g_fan_dropped++;
        return;
    }
    const size_t count = data[1];
    const size_t body_at = 2 + 2 * count;
    if (!count || count > kMaxFanSlots || n <= body_at) {
        g_fan_dropped++;
        return;
    }
    const size_t body = n - body_at;
    std::vector<uint8_t> out(3 + body);
    out[0] = kFan;
    std::memcpy(out.data() + 1, &c->slot, 2);
    std::memcpy(out.data() + 3, data + body_at, body);
    g_fan_in_bytes += n;
    uint16_t sent[kMaxFanSlots];
    size_t nsent = 0;
    for (size_t i = 0; i < count; i++) {
        uint16_t to;
        std::memcpy(&to, data + 2 + 2 * i, 2);
        auto it = g_clients.find(to);
        // (each player once: a slot listed 64 times got 64 copies, 2026-11-18 review)
        const bool twice = std::find(sent, sent + nsent, to) != sent + nsent;
        if (!twice)
            sent[nsent++] = to;
        if (twice || to == c->slot || it == g_clients.end() || !pl->second.members.count(to)) {
            g_fan_dropped++;
            continue;
        }
        g_sock.send_to(it->second.ep, out.data(), out.size());
        g_fan_out_bytes += out.size();
        g_fan_packets++;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    bool local = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--local"))
            local = true; // only this PC can reach it (127.0.0.1): tests, no firewall prompt
        else if (!strcmp(argv[i], "--worlds") && i + 1 < argc)
            g_worlds_dir = argv[++i];
        else if (!strcmp(argv[i], "--warm-handover"))
            g_warm_handover = true;
        else if (!strcmp(argv[i], "--max-players") && i + 1 < argc)
            g_max_players = (size_t)std::max(1, atoi(argv[++i]));
        else if (!strcmp(argv[i], "--max-per-address") && i + 1 < argc)
            g_max_per_address = (size_t)std::max(1, atoi(argv[++i]));
        else if (!strcmp(argv[i], "--max-worlds") && i + 1 < argc)
            g_max_worlds = (size_t)std::max(1, atoi(argv[++i]));
        else if (!strcmp(argv[i], "--max-world-mb") && i + 1 < argc)
            g_max_world_bytes = (uint32_t)std::min(128, std::max(1, atoi(argv[++i]))) << 20;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("usage: galaxy_server [--local] [--worlds <dir>] [--warm-handover] [--planet-players <n>]\n"
                   "                     [--max-players <n>] [--max-per-address <n>] [--max-worlds <n>] [--max-world-mb <n>]\n"
                   "                     [port] [galaxy name]\n");
            return 0;
        }
        else if (!strcmp(argv[i], "--planet-players") && i + 1 < argc) { // 6E B: the players a planet takes
            const int n = atoi(argv[++i]);
            if (n < 2 || n > kMaxPlanetPlayers) {
                fprintf(stderr, "--planet-players: 2 to %d\n", (int)kMaxPlanetPlayers);
                return 1;
            }
            g_planet_players = (uint8_t)n;
        }
        else
            args.push_back(argv[i]);
    }
    uint16_t port = args.size() > 0 ? (uint16_t)atoi(args[0].c_str()) : kDefaultPort;
    if (args.size() > 1)
        g_name = args[1];
    if (local)
        net::g_bind_ip = 0x0100007Fu; // 127.0.0.1 in network byte order
    if (!net_init()) {
        fprintf(stderr, "network init failed\n");
        return 1;
    }
    std::string err;
    if (!g_sock.open(port, &err)) {
        fprintf(stderr, "cannot open UDP port %u: %s\n", port, err.c_str());
        return 1;
    }
    g_sock.set_buffers(4 << 20); // (bursts: a world sent to a joining player)
    srand((unsigned)time(nullptr) ^ now_ms()); // (the hand-over tokens: not the same every run)
    {
        std::random_device rd; // 6S: the address cookies' secret (new each run: a cookie lives 1 to 2 minutes)
        g_cookie_secret[0] = ((uint64_t)rd() << 32) ^ rd();
        g_cookie_secret[1] = ((uint64_t)rd() << 32) ^ rd() ^ ((uint64_t)now_ms() << 20);
    }
    load_keys();
    say("galaxy server '%s' on UDP port %u (protocol %u), worlds in '%s', %u players a planet", g_name.c_str(),
        g_sock.local_port(), kProto, g_worlds_dir.c_str(), (unsigned)g_planet_players);
    say("limits: %zu players (%zu an address), %zu stored worlds of %u MB at most", g_max_players, g_max_per_address,
        g_max_worlds, g_max_world_bytes >> 20);
    load_stored();
    std::vector<uint8_t> buf(65536);
    uint32_t last_status = now_ms(), last_sweep = now_ms();
    uint64_t status_bytes = 0, status_fan_in = 0, status_fan_out = 0;
    for (;;) {
        Endpoint from;
        int n;
        bool any = false;
        int burst = 0; // (a flood must not starve the downloads and the sweep below)
        while (burst++ < 2048 && (n = g_sock.recv_from(from, buf.data(), buf.size())) != 0) {
            if (n < 0)
                continue; // (an error or a dropped datagram: go on)
            any = true;
            try { // (a datagram never ends the server: out of memory on a world, anything thrown below)
                if (buf[0] == kRelay)
                    on_relay(from, buf.data(), (size_t)n);
                else if (buf[0] == kFan)
                    on_fan(from, buf.data(), (size_t)n);
                else if (buf[0] == kControl && n >= 2) {
                    Reader r(buf.data() + 1, (size_t)n - 1);
                    on_control(from, r);
                }
            } catch (const std::exception& e) {
                say("a datagram from %s was dropped: %s", from.masked().c_str(), e.what());
            }
        }
        const uint32_t now = now_ms();
        for (auto it = g_downloads.begin(); it != g_downloads.end();) // (given up: its player gone, or no ack for 30 s)
            it = !g_clients.count(it->first) || now - it->second.ack_ms > kDownloadGiveUpMs ? g_downloads.erase(it) : std::next(it);
        for (auto& [slot, x] : g_downloads) { // the worlds on their way to players
            auto c = g_clients.find(slot);
            if (c == g_clients.end())
                continue;
            if (!x.acked && now - x.begin_ms > 500) {
                x.begin_ms = now;
                send(c->second, x.begin());
            }
            x.pump(now, [&](const Writer& w) { send(c->second, w); });
        }
        if (now - last_sweep > 1000) { // players silent for 20 s are gone
            last_sweep = now;
            std::vector<uint16_t> gone;
            for (auto& [s, c] : g_clients)
                if (now - c.last_ms > 20000)
                    gone.push_back(s);
            for (uint16_t s : gone) {
                say("player %u '%s' timed out", s, g_clients[s].name.c_str());
                leave_planet(g_clients[s], "timed out");
                party_player_out(g_clients[s]); // 6K
                g_upload_meta.erase(s); // (not for the next player of this slot)
                g_clients.erase(s);
            }
            g_parties.lapse(now / 1000.0); // (invites nobody answered: no longer shown as pending)
            for (auto it = g_party_ghosts.begin(); it != g_party_ghosts.end();)
                it = now - it->second.at > kPartyGhostMs ? g_party_ghosts.erase(it) : std::next(it);
        }
        if (now - last_status > 30000) {
            const double kbs = (g_relay_bytes - status_bytes) / 1024.0 / ((now - last_status) / 1000.0);
            const double secs = (now - last_status) / 1000.0;
            say("status: %zu player(s), %zu planet(s), relay %.1f KB/s (%llu packets, %llu dropped in all), "
                "copies: in %.1f KB/s out %.1f KB/s (%llu sent, %llu dropped in all), parties %zu (%llu requests, %llu views), "
                "eras: %u migrated, %u uploads of another stage and %u of another planet refused",
                g_clients.size(), g_planets.size(), kbs, (unsigned long long)g_relay_packets,
                (unsigned long long)g_relay_dropped, (g_fan_in_bytes - status_fan_in) / 1024.0 / secs,
                (g_fan_out_bytes - status_fan_out) / 1024.0 / secs, (unsigned long long)g_fan_packets,
                (unsigned long long)g_fan_dropped, g_parties.all().size(), (unsigned long long)g_party_requests,
                (unsigned long long)g_party_views, g_migrated, g_era_refused, g_save_refused);
            last_status = now;
            status_bytes = g_relay_bytes;
            status_fan_in = g_fan_in_bytes;
            status_fan_out = g_fan_out_bytes;
        }
        if (!any)
            g_sock.wait(5); // (wakes as soon as a datagram arrives: a sleep waited ~16 ms on Windows)
    }
}
