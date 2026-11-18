// The galaxy server (decision 5O A, docs/12-server-and-players.md#server-front-door): the one address every player
// connects to. Runs no Spore: it keeps the players (a slot each), the planets (who hosts each one now, who is on it)
// and relays the planet sessions' packets between a planet's host and its players (net/galaxy.h).
//
// usage: galaxy_server [--local] [--worlds <dir>] [--warm-handover] [port] [galaxy name]
//        (port 47900 by default; --local: 127.0.0.1 only; worlds: the folder of stored worlds, "worlds" by default)
//
// Planets are made when a player enters one; the host is the first player on a planet. Decision 5Q A: the server keeps
// each planet's world (one file per planet in the worlds folder, kept across restarts): the host uploads it (its game
// saves and sends the save folder, every few minutes and when it leaves), and the next first player on the planet
// downloads it, loads it and hosts it. Players entering while the host is not hosting yet are asked to wait.
// Decision 5V A: when the host leaves (or times out) and players stay, the next one hosts the stored world (the host
// uploads it before it leaves) and the others join it once it is READY. Not yet: identities and bans across planets.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../../mods/coop/net/galaxy.h"

using namespace net;
using namespace net::galaxy;

struct Client {
    Endpoint ep;
    uint16_t slot = 0;
    std::string name, version, planet;
    uint64_t identity = 0;
    uint32_t last_ms = 0;
};

struct Planet {
    uint16_t host = 0;
    bool ready = false;         // its host's session hosts it (READY)
    std::set<uint16_t> members; // the host too
};

// a stored world (the worlds folder): file = GWS2, str8 planet, u32 bytes, u32 crc, u8 tech level, u32 x3 species key,
// the bytes (GWS1: without the tech level and species)
struct Stored {
    std::string file;
    uint32_t size = 0, crc = 0;
    uint8_t tech = 0;
    uint32_t species[3]{};
};
struct Meta {
    uint8_t tech = 0;
    uint32_t species[3]{};
};
static std::map<uint16_t, Meta> g_upload_meta; // by player slot: the facts its upload carried
static std::map<std::string, Stored> g_stored;
static std::string g_worlds_dir = "worlds";
static std::map<uint16_t, XferIn> g_uploads;    // by player slot
static std::map<uint16_t, XferOut> g_downloads; // by player slot
static uint32_t g_next_xfer = 1;
// decision 6B C research (--warm-handover): the new host of a hand-over keeps the world it plays (it was a guest of the
// old host) instead of loading the stored one: no reload for it
static bool g_warm_handover;

static UdpSocket g_sock;
static std::map<uint16_t, Client> g_clients; // by slot
static std::map<std::string, Planet> g_planets;
static std::string g_name = "SporeCoop galaxy";
static uint16_t g_next_slot = 1;
static uint64_t g_relay_bytes, g_relay_packets, g_relay_dropped;

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
                auto st = g_stored.find(c.planet);
                Writer role = control(ROLE);
                role.str8(c.planet);
                role.u8(kHostRole);
                role.u16(next);
                role.u32(st == g_stored.end() || g_warm_handover ? 0 : st->second.size);
                send(g_clients[next], role);
                for (uint16_t m : p.members)
                    if (m != next) {
                        Writer w = control(HOSTMOVE);
                        w.str8(c.planet);
                        w.u16(next);
                        send(g_clients[m], w);
                    }
                say("planet '%s': its host %u '%s' left (%s): %u '%s' hosts it now (%s), %zu player(s) wait",
                    c.planet.c_str(), c.slot, c.name.c_str(), why, next, g_clients[next].name.c_str(),
                    st == g_stored.end() || g_warm_handover ? "the world it plays" : "the stored world", p.members.size() - 1);
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
        uint8_t head[4 + 1 + 255 + 8];
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
        st.file = e.path().string();
        g_stored[planet] = st;
        say("stored world: '%s' (%u KB)", planet.c_str(), st.size >> 10);
    }
}

static bool store_world(const std::string& planet, const XferIn& in, const Meta& meta, std::string& why)
{
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
    st.tech = meta.tech;
    memcpy(st.species, meta.species, sizeof st.species);
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
        if (proto != kProto) {
            Client tmp;
            tmp.ep = from;
            Writer w = control(REFUSED);
            w.str8("This galaxy server speaks another protocol: update SporeCoop Lineages.");
            send(tmp, w);
            return;
        }
        if (!c) {
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
        send(*c, w);
        say("player %u '%s' connected from %s (%s)", c->slot, c->name.c_str(), from.masked().c_str(), version.c_str());
        return;
    }
    if (!c) { // anything but HELLO from an unknown address
        Client tmp;
        tmp.ep = from;
        Writer w = control(REFUSED);
        w.str8("Not connected to the galaxy server: connect first.");
        send(tmp, w);
        return;
    }
    c->last_ms = now_ms();
    if (type == PING) {
        send(*c, control(PONG));
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
        auto ex = g_planets.find(name);
        if (ex != g_planets.end() && ex->second.host && !ex->second.ready && ex->second.host != c->slot) {
            Writer w = control(REFUSED);
            w.str8(name + " is being loaded by its host: enter again in a moment.");
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
            say("planet '%s': %u '%s' hosts it%s", name.c_str(), c->slot, c->name.c_str(),
                st == g_stored.end() ? " (a new planet: its loaded world)" : " (from its stored world)");
        } else {
            w.u8(kGuestRole);
            w.u16(p.host);
            w.u32(0);
            say("planet '%s': %u '%s' joins host %u '%s'", name.c_str(), c->slot, c->name.c_str(), p.host,
                g_clients[p.host].name.c_str());
        }
        send(*c, w);
    } else if (type == LEAVE) {
        leave_planet(*c, "left");
    } else if (type == READY) {
        auto it = g_planets.find(c->planet);
        if (it != g_planets.end() && it->second.host == c->slot && !it->second.ready) {
            it->second.ready = true;
            say("planet '%s': hosted by %u '%s'", c->planet.c_str(), c->slot, c->name.c_str());
            for (uint16_t m : it->second.members) // 5V A: the players who waited for the hand-over join it
                if (m != c->slot) {
                    Writer w = control(ROLE);
                    w.str8(c->planet);
                    w.u8(kGuestRole);
                    w.u16(c->slot);
                    w.u32(0);
                    send(g_clients[m], w);
                    say("planet '%s': %u '%s' joins its new host", c->planet.c_str(), m, g_clients[m].name.c_str());
                }
        }
    } else if (type == FETCH) { // the planet's host wants its stored world
        auto it = g_planets.find(c->planet);
        auto st = g_stored.find(c->planet);
        std::vector<uint8_t> data;
        if (it == g_planets.end() || it->second.host != c->slot || st == g_stored.end() || !read_world(st->second, data)) {
            Writer w = control(REFUSED);
            w.str8("The galaxy server has no stored world of " + c->planet + " for you.");
            send(*c, w);
            return;
        }
        XferOut& x = g_downloads[c->slot];
        x.start(g_next_xfer++, c->planet, std::move(data));
        x.begin_ms = now_ms();
        send(*c, x.begin());
        say("planet '%s': sending its world to %u '%s' (%u KB)", c->planet.c_str(), c->slot, c->name.c_str(),
            st->second.size >> 10);
    } else if (type == XACK) {
        const uint32_t id = r.u32(), idx = r.u32();
        auto it = g_downloads.find(c->slot);
        if (r.ok() && it != g_downloads.end() && it->second.id == id)
            it->second.on_ack(idx);
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
        if (!in.begin(r))
            return;
        if (r.left() >= 13) { // the menu's facts of the planet (5X B)
            Meta m;
            m.tech = r.u8();
            for (auto& v : m.species)
                v = r.u32();
            g_upload_meta[c->slot] = m;
        }
        auto cur = g_uploads.find(c->slot);
        if (cur != g_uploads.end() && cur->second.id == in.id)
            return; // (its begin again)
        auto it = g_planets.find(c->planet);
        if (in.planet != c->planet || it == g_planets.end() || it->second.host != c->slot) {
            Writer w = control(XDONE);
            w.u32(in.id);
            w.u8(0);
            w.str8("only the planet's host stores its world");
            send(*c, w);
            return;
        }
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
        const bool ok = valid && store_world(in.planet, in, g_upload_meta[c->slot], why);
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
    say("galaxy server '%s' on UDP port %u (protocol %u), worlds in '%s'", g_name.c_str(), g_sock.local_port(), kProto,
        g_worlds_dir.c_str());
    load_stored();
    std::vector<uint8_t> buf(65536);
    uint32_t last_status = now_ms(), last_sweep = now_ms();
    uint64_t status_bytes = 0;
    for (;;) {
        Endpoint from;
        int n;
        bool any = false;
        while ((n = g_sock.recv_from(from, buf.data(), buf.size())) != 0) {
            if (n < 0)
                continue; // (an error or a dropped datagram: go on)
            any = true;
            if (buf[0] == kRelay)
                on_relay(from, buf.data(), (size_t)n);
            else if (buf[0] == kControl && n >= 2) {
                Reader r(buf.data() + 1, (size_t)n - 1);
                on_control(from, r);
            }
        }
        const uint32_t now = now_ms();
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
                g_clients.erase(s);
            }
        }
        if (now - last_status > 30000) {
            const double kbs = (g_relay_bytes - status_bytes) / 1024.0 / ((now - last_status) / 1000.0);
            say("status: %zu player(s), %zu planet(s), relay %.1f KB/s (%llu packets, %llu dropped in all)",
                g_clients.size(), g_planets.size(), kbs, (unsigned long long)g_relay_packets,
                (unsigned long long)g_relay_dropped);
            last_status = now;
            status_bytes = g_relay_bytes;
        }
        if (!any)
            g_sock.wait(5); // (wakes as soon as a datagram arrives: a sleep waited ~16 ms on Windows)
    }
}
