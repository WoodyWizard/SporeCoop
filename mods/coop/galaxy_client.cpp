#include "galaxy_client.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

#include "net/galaxy.h"
#include "net/transport.h"

namespace galaxy {

using namespace net::galaxy;

static const SkHost* H;
static SkMod* M;
static Ctx C;

static net::UdpSocket g_sock;
static net::Endpoint g_server;
static bool g_welcomed;
static uint16_t g_slot;
static std::string g_galaxy, g_planet, g_last_refusal;
static uint8_t g_role;      // kHostRole / kGuestRole on a planet, 0 none
static uint16_t g_host_slot;
static bool g_relay_on;     // the session uses the relay transport now
static bool g_session_seen; // the planet's session was up once (its end then means: left the planet)
static double g_next_ping, g_last_rx, g_hello_at;
struct PlanetRow {
    std::string name;
    uint16_t players, host;
};
static std::vector<PlanetRow> g_planets;
static std::deque<std::pair<uint16_t, std::vector<uint8_t>>> g_queue; // relay packets for the session
static uint64_t g_rx, g_tx, g_rx_bytes, g_tx_bytes, g_queue_dropped;

static void on_control(net::Reader& r);

// everything the galaxy server sent: control messages handled, relay packets queued for the session
static void pump()
{
    if (!g_sock.is_open())
        return;
    uint8_t buf[65536];
    net::Endpoint from;
    int n;
    while ((n = g_sock.recv_from(from, buf, sizeof buf)) != 0) {
        if (n < 0 || from != g_server)
            continue;
        g_last_rx = H->time_seconds();
        if (buf[0] == kRelay && n >= 3) {
            uint16_t slot;
            memcpy(&slot, buf + 1, 2);
            if (g_queue.size() >= 4096) {
                g_queue.pop_front();
                g_queue_dropped++;
            }
            g_queue.emplace_back(slot, std::vector<uint8_t>(buf + 3, buf + n));
            g_rx++;
            g_rx_bytes += (uint64_t)n;
        } else if (buf[0] == kControl && n >= 2) {
            net::Reader r(buf + 1, (size_t)n - 1);
            on_control(r);
        }
    }
}

// The planet session's transport: a slot on the galaxy server is the session's address (net/galaxy.h)
class RelayTransport : public net::Transport {
public:
    bool open(uint16_t, std::string* err) override
    {
        if (!g_sock.is_open() && err)
            *err = "not connected to a galaxy server";
        return g_sock.is_open();
    }
    void close() override {} // (the galaxy connection stays)
    bool send_to(const net::Endpoint& to, const void* data, size_t n) override
    {
        const uint16_t slot = endpoint_slot(to);
        if (!slot || !g_sock.is_open() || n > 65000)
            return false;
        std::vector<uint8_t> f(n + 3);
        f[0] = kRelay;
        memcpy(f.data() + 1, &slot, 2);
        memcpy(f.data() + 3, data, n);
        g_tx++;
        g_tx_bytes += f.size();
        return g_sock.send_to(g_server, f.data(), f.size());
    }
    int recv_from(net::Endpoint& from, void* buf, size_t cap) override
    {
        if (g_queue.empty())
            pump();
        if (g_queue.empty())
            return 0;
        auto& [slot, data] = g_queue.front();
        if (data.size() > cap) {
            g_queue.pop_front();
            return -2; // (net::Session::kRecvSkipped: one datagram dropped, more may be pending)
        }
        from = slot_endpoint(slot);
        memcpy(buf, data.data(), data.size());
        const int n = (int)data.size();
        g_queue.pop_front();
        return n;
    }
    uint16_t local_port() const override { return g_sock.local_port(); }
};
static RelayTransport g_relay;

static void send_control(const net::Writer& w)
{
    if (g_sock.is_open())
        g_sock.send_to(g_server, w.data.data(), w.data.size());
}

static void say(const std::string& s)
{
    H->log(M, SK_INFO, "galaxy: %s", s.c_str());
    if (C.feed)
        C.feed(s);
}

static void use_relay(bool on)
{
    if (!C.sess || g_relay_on == on)
        return;
    C.sess->set_transport(on ? &g_relay : nullptr);
    g_relay_on = on;
    g_session_seen = false;
    if (!on)
        g_queue.clear();
}

static void on_control(net::Reader& r)
{
    const uint8_t type = r.u8();
    if (type == WELCOME) {
        g_slot = r.u16();
        g_galaxy = r.str8();
        if (!r.ok())
            return;
        g_welcomed = true;
        say("Connected to the galaxy '" + g_galaxy + "'.");
    } else if (type == PLANETS) {
        const uint8_t n = r.u8();
        std::vector<PlanetRow> v;
        for (uint8_t i = 0; i < n && r.ok(); i++) {
            PlanetRow p;
            p.name = r.str8();
            p.players = r.u16();
            p.host = r.u16();
            if (r.ok())
                v.push_back(p);
        }
        if (!r.ok())
            return;
        g_planets = v;
        std::string line = v.empty() ? "No planet has players yet: enter one to host it." : "Planets:";
        for (auto& p : v)
            line += " " + p.name + " (" + std::to_string(p.players) + (p.host ? ")" : ", no host)");
        say(line);
    } else if (type == ROLE) {
        std::string planet = r.str8();
        const uint8_t role = r.u8();
        const uint16_t host = r.u16();
        if (!r.ok())
            return;
        g_planet = planet;
        g_role = role;
        g_host_slot = host;
        use_relay(true);
        std::string err;
        if (role == kHostRole) {
            if (C.host && C.host(err))
                say("You host " + planet + " for the galaxy.");
            else
                say("Could not host " + planet + ": " + err);
        } else {
            if (C.join && C.join(slot_endpoint(host).str(), err))
                say("Joining " + planet + "...");
            else
                say("Could not join " + planet + ": " + err);
        }
    } else if (type == REFUSED) {
        g_last_refusal = r.str8();
        if (r.ok())
            say("Galaxy server: " + g_last_refusal);
    } else if (type == HOSTGONE) {
        std::string planet = r.str8();
        if (!r.ok())
            return;
        say("The host of " + planet + " left the galaxy.");
        if (g_role == kGuestRole && g_relay_on && C.sess && C.sess->role() != net::Role::None)
            C.sess->leave("the planet's host left"); // (tick takes it off the planet once the session is closed)
    }
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
}

std::string connect(const std::string& address)
{
    net::Endpoint ep;
    std::string err;
    if (!net::Endpoint::parse(address, kDefaultPort, ep, err))
        return "Galaxy address: " + err;
    if (C.sess && C.sess->role() != net::Role::None && !g_relay_on)
        return "Leave the current session first.";
    if (!g_sock.is_open() && !g_sock.open(0, &err))
        return "Could not open a UDP socket: " + err;
    g_server = ep;
    g_welcomed = false;
    net::Writer w = control(HELLO);
    w.u32(kProto);
    w.u64(C.identity ? C.identity() : 0);
    w.str8(C.name ? C.name() : "Player");
    w.str8(C.version);
    send_control(w);
    g_hello_at = g_last_rx = H->time_seconds();
    g_next_ping = g_hello_at + 2;
    return "Connecting to the galaxy server at " + ep.masked() + "...";
}

std::string disconnect()
{
    if (!g_sock.is_open())
        return "Not connected to a galaxy server.";
    leave();
    g_sock.close();
    g_welcomed = false;
    g_planets.clear();
    return "Disconnected from the galaxy server.";
}

std::string list()
{
    if (!g_welcomed)
        return "Not connected to a galaxy server.";
    send_control(control(LIST));
    return "Asking the galaxy server for its planets...";
}

std::string enter(const std::string& planet)
{
    if (!g_welcomed)
        return "Not connected to a galaxy server.";
    if (planet.empty())
        return "Which planet?";
    if (C.sess && C.sess->role() != net::Role::None && !g_relay_on)
        return "Leave the current session first.";
    net::Writer w = control(ENTER);
    w.str8(planet);
    send_control(w);
    return "Entering " + planet + "...";
}

std::string leave()
{
    if (g_planet.empty())
        return "Not on a planet of the galaxy.";
    send_control(control(LEAVE));
    std::string p = g_planet;
    g_planet.clear();
    g_role = 0;
    if (C.sess && C.sess->role() != net::Role::None && g_relay_on) {
        // (its goodbye goes out through the relay: the relay stays until the session is closed, tick switches it off)
        C.sess->leave("left the planet");
        g_session_seen = true;
    } else
        use_relay(false);
    return "Left " + p + ".";
}

void tick(double now)
{
    if (!g_sock.is_open())
        return;
    if (!g_relay_on || !C.sess || C.sess->role() == net::Role::None)
        pump(); // (while a planet session runs, its transport reads)
    if (now >= g_next_ping) {
        g_next_ping = now + 2;
        send_control(control(PING));
    }
    if (g_welcomed && now - g_last_rx > 20) {
        say("The galaxy server stopped answering.");
        g_welcomed = false;
    } else if (!g_welcomed && now - g_hello_at > 10 && g_hello_at > 0) {
        say("No answer from the galaxy server.");
        g_hello_at = 0;
    }
    // the planet's session ended (left, removed, its host gone): off the planet, back to the plain network
    if (g_relay_on && C.sess) {
        if (C.sess->role() != net::Role::None)
            g_session_seen = true;
        else if (g_session_seen) {
            send_control(control(LEAVE));
            g_planet.clear();
            g_role = 0;
            use_relay(false);
        }
    }
}

std::string status()
{
    char b[400];
    snprintf(b, sizeof b,
             "galaxy: %s server=%s galaxy='%s' slot=%u planet='%s' role=%s host_slot=%u relay=%d planets=%zu "
             "relay rx=%llu (%llu KB) tx=%llu (%llu KB) queue=%zu dropped=%llu",
             g_welcomed ? "connected" : g_sock.is_open() ? "connecting" : "off", g_server.masked().c_str(), g_galaxy.c_str(),
             g_slot, g_planet.c_str(), g_role == kHostRole ? "host" : g_role == kGuestRole ? "guest" : "-", g_host_slot,
             (int)g_relay_on, g_planets.size(), (unsigned long long)g_rx, (unsigned long long)(g_rx_bytes >> 10),
             (unsigned long long)g_tx, (unsigned long long)(g_tx_bytes >> 10), g_queue.size(),
             (unsigned long long)g_queue_dropped);
    return b;
}

} // namespace galaxy
