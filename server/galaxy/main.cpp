// The galaxy server (decision 5O A, docs/12-server-and-players.md#server-front-door): the one address every player
// connects to. Runs no Spore: it keeps the players (a slot each), the planets (who hosts each one now, who is on it)
// and relays the planet sessions' packets between a planet's host and its players (net/galaxy.h).
//
// usage: galaxy_server [--local] [port] [galaxy name]     (port 47900 by default; --local: 127.0.0.1 only)
//
// First milestone: planets are made when a player enters one and forgotten when the last one leaves; the host is the
// first player on a planet. Not yet: storing worlds, handing a planet's host over, identities and bans across planets.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
    std::set<uint16_t> members; // the host too
};

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
    if (c.planet.empty())
        return;
    auto it = g_planets.find(c.planet);
    if (it != g_planets.end()) {
        Planet& p = it->second;
        p.members.erase(c.slot);
        if (p.host == c.slot) {
            p.host = 0;
            for (uint16_t m : p.members) { // their session's host is gone (the hand-over is a later step)
                Writer w = control(HOSTGONE);
                w.str8(c.planet);
                send(g_clients[m], w);
            }
            say("planet '%s': its host %u '%s' left (%s), %zu player(s) without a host", c.planet.c_str(), c.slot,
                c.name.c_str(), why, p.members.size());
        } else
            say("planet '%s': %u '%s' left (%s)", c.planet.c_str(), c.slot, c.name.c_str(), why);
        if (p.members.empty()) {
            say("planet '%s' is empty: forgotten", c.planet.c_str());
            g_planets.erase(it);
        }
    }
    c.planet.clear();
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
    } else if (type == LIST) {
        Writer w = control(PLANETS);
        const size_t n = g_planets.size() < 50 ? g_planets.size() : 50;
        w.u8((uint8_t)n);
        size_t i = 0;
        for (auto& [name, p] : g_planets) {
            if (i++ == n)
                break;
            w.str8(name);
            w.u16((uint16_t)p.members.size());
            w.u16(p.host);
        }
        send(*c, w);
    } else if (type == ENTER) {
        std::string name = r.str8();
        if (!r.ok() || name.empty())
            return;
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
            say("planet '%s': %u '%s' hosts it", name.c_str(), c->slot, c->name.c_str());
        } else {
            w.u8(kGuestRole);
            w.u16(p.host);
            say("planet '%s': %u '%s' joins host %u '%s'", name.c_str(), c->slot, c->name.c_str(), p.host,
                g_clients[p.host].name.c_str());
        }
        send(*c, w);
    } else if (type == LEAVE) {
        leave_planet(*c, "left");
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
    say("galaxy server '%s' on UDP port %u (protocol %u)", g_name.c_str(), g_sock.local_port(), kProto);
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
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
