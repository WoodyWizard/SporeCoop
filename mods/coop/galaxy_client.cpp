#include "galaxy_client.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <deque>
#include <vector>

#include "net/galaxy.h"
#include "net/transport.h"
#include "world.h"
#include "game.h"
#include "modes.h"

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
using PlanetRow = PlanetInfo;
static std::vector<PlanetRow> g_planets;
static uint32_t g_planets_ver;
static double g_next_list;

// Decision 5Q A: the planet's world on the galaxy server. A host with a stored world downloads it (FETCH), loads it
// and hosts it then; a host uploads its world every g_upload_every seconds and when it leaves.
enum HostStep { kNoHost, kFetching, kLoading, kHosting };
static HostStep g_hstep;
static double g_hstep_at;
static bool g_left_stage;     // kLoading: the old stage went away (the load started)
static double g_ready_since;  // kLoading: playing since
static uint32_t g_world_bytes; // ROLE: the planet's stored world
static net::galaxy::XferIn g_down;
static net::galaxy::XferOut g_up;
enum UpStep { kUpIdle, kUpSaving, kUpSending };
static UpStep g_upstep;
static double g_up_at, g_next_upload, g_upload_every = 300;
static bool g_leave_after, g_disconnect_after;
static uint32_t g_uploads_done, g_downloads_done;
static std::string g_last_upload;
// 5V A: the planet's host changes (HOSTMOVE): this guest's session ended, it waits on the planet for the new host
static bool g_waiting_host;
static double g_waiting_since;
static std::string g_pending_join; // join this address once the old session is closed
static uint32_t g_handovers;
static std::deque<std::pair<uint16_t, std::vector<uint8_t>>> g_queue; // relay packets for the session
static uint64_t g_rx, g_tx, g_rx_bytes, g_tx_bytes, g_queue_dropped;

static void on_control(net::Reader& r);
static void host_now();
static void downloaded();
static void upload_done(bool ok, const std::string& why);
static std::string leave_now();

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
            p.stored = r.u32();
            p.tech = r.u8();
            for (auto& v : p.species)
                v = r.u32();
            if (r.ok())
                v.push_back(p);
        }
        if (!r.ok())
            return;
        bool same = v.size() == g_planets.size();
        for (size_t i = 0; same && i < v.size(); i++)
            same = v[i].name == g_planets[i].name && v[i].players == g_planets[i].players && v[i].host == g_planets[i].host &&
                   v[i].stored == g_planets[i].stored && v[i].tech == g_planets[i].tech &&
                   !memcmp(v[i].species, g_planets[i].species, sizeof v[i].species);
        g_planets = v;
        if (same)
            return; // (the menu's poll: said once)
        g_planets_ver++;
        std::string line = v.empty() ? "No planet has players yet: enter one to host it." : "Planets:";
        for (auto& p : v)
            line += " " + p.name + " (" + std::to_string(p.players) + (p.host ? "" : ", no host") +
                    (p.stored ? ", saved" : "") + ")";
        say(line);
    } else if (type == ROLE) {
        std::string planet = r.str8();
        const uint8_t role = r.u8();
        const uint16_t host = r.u16();
        const uint32_t stored = r.u32();
        if (!r.ok())
            return;
        const bool handover = g_planet == planet && (g_waiting_host || g_role == kGuestRole);
        g_planet = planet;
        g_role = role;
        g_host_slot = host;
        g_world_bytes = stored;
        g_waiting_host = false;
        use_relay(true);
        std::string err;
        if (handover && C.sess && C.sess->role() != net::Role::None) // (a guest of the old host: that session ends)
            C.sess->leave("the planet's host changes");
        if (handover) {
            g_handovers++;
            if (role == kGuestRole) { // joined from tick once the old session is closed
                g_pending_join = slot_endpoint(host).str();
                say("Joining " + planet + "'s new host...");
                return;
            }
            say("You host " + planet + " now: its host left.");
            if (role == kHostRole && !stored) { // (no stored world: host the one it plays, once its old session closed)
                g_hstep = kLoading;
                g_hstep_at = H->time_seconds();
                g_left_stage = true;
                g_ready_since = 0;
                return;
            }
        }
        if (role == kHostRole) {
            if (stored) { // its world first: download, load, then host
                g_hstep = kFetching;
                g_hstep_at = H->time_seconds();
                g_down = {};
                send_control(control(FETCH));
                say("Downloading " + planet + " from the galaxy (" + std::to_string(stored >> 10) + " KB)...");
            } else
                host_now();
        } else {
            if (C.join && C.join(slot_endpoint(host).str(), err))
                say("Joining " + planet + "...");
            else
                say("Could not join " + planet + ": " + err);
        }
    } else if (type == HOSTMOVE) { // 5V A: the host left; the new one loads the world, then the server sends ROLE
        std::string planet = r.str8();
        const uint16_t next = r.u16();
        if (!r.ok() || planet != g_planet)
            return;
        g_waiting_host = true;
        g_waiting_since = H->time_seconds();
        g_host_slot = next;
        if (C.sess && C.sess->role() != net::Role::None)
            C.sess->leave("the planet's host changes");
        say("The host of " + planet + " left: another player takes it over, wait a moment...");
    } else if (type == XBEGIN) { // the planet's stored world (FETCH)
        net::galaxy::XferIn in;
        if (!in.begin(r) || g_hstep != kFetching || (g_down.on && g_down.id == in.id))
            return;
        g_down = std::move(in);
    } else if (type == XCHUNK) {
        const uint32_t id = r.u32(), idx = r.u32();
        if (!r.ok() || !g_down.on || g_down.id != id)
            return;
        const bool was = g_down.complete();
        if (!g_down.chunk(idx, r))
            return;
        net::Writer a = control(XACK);
        a.u32(id);
        a.u32(idx);
        send_control(a);
        if (!was && g_down.complete())
            downloaded();
    } else if (type == XACK) {
        const uint32_t id = r.u32(), idx = r.u32();
        if (r.ok() && g_up.on && g_up.id == id)
            g_up.on_ack(idx);
    } else if (type == XDONE) {
        const uint32_t id = r.u32();
        const uint8_t ok = r.u8();
        std::string why = r.str8();
        if (!r.ok() || !g_up.on || g_up.id != id)
            return;
        upload_done(ok != 0, why);
    } else if (type == REFUSED) {
        g_last_refusal = r.str8();
        if (r.ok())
            say("Galaxy server: " + g_last_refusal);
        if (r.ok() && g_hstep == kFetching) { // (no stored world after all)
            g_hstep = kNoHost;
            leave_now();
        }
    } else if (type == HOSTGONE) {
        std::string planet = r.str8();
        if (!r.ok())
            return;
        say("The host of " + planet + " left the galaxy.");
        if (g_role == kGuestRole && g_relay_on && C.sess && C.sess->role() != net::Role::None)
            C.sess->leave("the planet's host left"); // (tick takes it off the planet once the session is closed)
    }
}

static void reset_planet_steps()
{
    g_waiting_host = false;
    g_pending_join.clear();
    g_hstep = kNoHost;
    g_down = {};
    g_up = {};
    g_upstep = kUpIdle;
    g_leave_after = false;
}

static void host_now()
{
    std::string err;
    if (C.host && C.host(err)) {
        g_hstep = kHosting;
        send_control(control(READY));
        g_next_upload = H->time_seconds() + (g_world_bytes ? g_upload_every : 60); // (a new planet: stored soon)
        say("You host " + g_planet + " for the galaxy.");
    } else {
        g_hstep = kNoHost;
        say("Could not host " + g_planet + ": " + err);
    }
}

static void downloaded()
{
    net::Writer w = control(XDONE);
    w.u32(g_down.id);
    std::string err;
    bool ok = g_down.valid();
    if (!ok)
        err = "the world arrived damaged";
    else
        ok = world::galaxy_install(g_down.data, "Galaxy " + g_galaxy, err);
    w.u8(ok ? 1 : 0);
    w.str8(err);
    send_control(w);
    g_down.data.clear();
    g_down.data.shrink_to_fit();
    if (!ok) {
        say("Could not load " + g_planet + " from the galaxy: " + err);
        g_hstep = kNoHost;
        leave_now();
        return;
    }
    g_downloads_done++;
    g_hstep = kLoading;
    g_hstep_at = H->time_seconds();
    g_left_stage = false;
    g_ready_since = 0;
    say("Loading " + g_planet + "...");
}

static uint32_t ms(double t) { return (uint32_t)(int64_t)(t * 1000); }

// the upload's XBEGIN with the menu's facts of the planet (5X B): its tech level and the host's creature
static uint8_t g_up_tech;
static uint32_t g_up_species[3];
static net::Writer up_begin()
{
    net::Writer w = g_up.begin();
    w.u8(g_up_tech);
    for (uint32_t v : g_up_species)
        w.u32(v);
    return w;
}
static void up_facts()
{
    const uint32_t m = game::mode_id();
    g_up_tech = m == modes::kCreature ? 1 : m == modes::kTribe ? 2 : m == modes::kCiv ? 4 : m == modes::kSpace ? 5 : 0;
    g_up_species[0] = g_up_species[1] = g_up_species[2] = 0;
    if (void* av = game::avatar()) {
        const game::Key k = game::species_key(av);
        g_up_species[0] = k.instance, g_up_species[1] = k.type, g_up_species[2] = k.group;
    }
}

static void start_upload()
{
    if (g_upstep != kUpIdle || g_hstep != kHosting)
        return;
    std::string err;
    if (!world::save_game(err)) { // (an editor, a cinematic, Space on a planet: again soon)
        g_next_upload = H->time_seconds() + 15;
        H->log(M, SK_INFO, "galaxy: no save for the upload now: %s", err.c_str());
        if (g_leave_after)
            say("Waiting to save " + g_planet + ": " + err);
        return;
    }
    g_upstep = kUpSaving;
    g_up_at = H->time_seconds();
}

static void upload_done(bool ok, const std::string& why)
{
    const uint32_t kb = (uint32_t)(g_up.data.size() >> 10), resent = g_up.resent;
    g_up = {};
    g_upstep = kUpIdle;
    g_next_upload = H->time_seconds() + (ok ? g_upload_every : 30);
    char b[160];
    snprintf(b, sizeof b, "%s %u KB in %.1f s, %u chunks resent", ok ? "stored" : "not stored", kb,
             H->time_seconds() - g_up_at, resent);
    g_last_upload = b;
    H->log(M, ok ? SK_INFO : SK_WARN, "galaxy: upload of %s: %s%s%s", g_planet.c_str(), b, why.empty() ? "" : ": ", why.c_str());
    if (ok)
        g_uploads_done++;
    if (!ok || g_leave_after)
        say(ok ? "Saved " + g_planet + " to the galaxy." : "Could not save " + g_planet + " to the galaxy: " + why);
    if (g_leave_after) {
        g_leave_after = false;
        leave_now();
        if (g_disconnect_after) {
            g_disconnect_after = false;
            disconnect();
        }
    }
}

static void upload_tick(double now)
{
    if (g_hstep != kHosting || g_role != kHostRole)
        return;
    if (g_upstep == kUpIdle && (now >= g_next_upload || g_leave_after) && C.sess && C.sess->role() == net::Role::Host)
        start_upload();
    if (g_upstep == kUpSaving && now - g_up_at > 4 && !world::save_busy()) {
        std::vector<uint8_t> blob;
        std::string planet, err;
        if (!world::galaxy_pack(blob, planet, err)) {
            g_upstep = kUpIdle;
            upload_done(false, err);
            return;
        }
        g_up.start(ms(now) ^ ((uint32_t)g_slot << 20), g_planet, std::move(blob));
        g_up.begin_ms = ms(now);
        up_facts();
        send_control(up_begin());
        g_upstep = kUpSending;
    }
    if (g_upstep == kUpSending) {
        if (!g_up.acked && ms(now) - g_up.begin_ms > 500) {
            g_up.begin_ms = ms(now);
            send_control(up_begin());
        }
        g_up.pump(ms(now), [](const net::Writer& w) { send_control(w); });
        if (now - g_up_at > 180)
            upload_done(false, "the upload timed out");
    }
}

// kLoading: the downloaded world loads; once the player plays in it, host it
static void loading_tick(double now)
{
    if (g_hstep != kLoading)
        return;
    const bool ready = C.ready && C.ready();
    if (!ready)
        g_left_stage = true, g_ready_since = 0;
    else if (g_left_stage && !g_ready_since)
        g_ready_since = now;
    if (g_ready_since && now - g_ready_since > 3)
        host_now();
    else if (now - g_hstep_at > 240) {
        say("Loading " + g_planet + " took too long: left it.");
        g_hstep = kNoHost;
        leave_now();
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
    g_sock.set_buffers(1 << 20); // (the relay carries the whole session: a world arrives in bursts)
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
    if (g_hstep == kHosting && g_role == kHostRole && !g_leave_after) { // its world first
        g_disconnect_after = true;
        return leave();
    }
    leave_now();
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
    if (g_hstep == kHosting && g_role == kHostRole && C.sess && C.sess->role() == net::Role::Host) {
        if (g_leave_after)
            return "Saving " + g_planet + " to the galaxy before leaving...";
        g_leave_after = true; // upload_tick saves and uploads it, upload_done leaves
        return "Saving " + g_planet + " to the galaxy before leaving...";
    }
    return leave_now();
}

std::string save()
{
    if (g_hstep != kHosting || g_role != kHostRole)
        return "Only the planet's host saves it to the galaxy.";
    if (g_upstep != kUpIdle)
        return "Saving " + g_planet + " to the galaxy already.";
    g_next_upload = 0;
    return "Saving " + g_planet + " to the galaxy...";
}

static std::string leave_now()
{
    if (g_planet.empty())
        return "Not on a planet of the galaxy.";
    reset_planet_steps();
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
    loading_tick(now);
    upload_tick(now);
    if (g_welcomed && game::mode_id() == modes::kMenu && now >= g_next_list) { // the galaxy menu's list (5X B)
        g_next_list = now + 10;
        send_control(control(LIST));
    }
    if (!g_pending_join.empty() && C.sess && C.sess->role() == net::Role::None) { // 5V A: the new host
        std::string err, addr = g_pending_join;
        g_pending_join.clear();
        if (C.join && C.join(addr, err))
            say("Joining " + g_planet + "...");
        else {
            say("Could not join " + g_planet + "'s new host: " + err);
            leave_now();
        }
    }
    if (g_waiting_host && now - g_waiting_since > 360) {
        say("The new host of " + g_planet + " did not come: left it.");
        leave_now();
    }
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
        else if (g_session_seen && g_hstep != kLoading && g_hstep != kFetching && !g_waiting_host &&
                 g_pending_join.empty()) {
            send_control(control(LEAVE));
            g_planet.clear();
            g_role = 0;
            reset_planet_steps();
            use_relay(false);
        }
    }
}

bool online() { return g_sock.is_open() && g_welcomed; }
uint32_t planets_version() { return g_planets_ver; }
std::string current_planet() { return g_planet; }
std::string galaxy_name() { return g_welcomed ? g_galaxy : ""; }
std::vector<PlanetInfo> planets_known()
{
    std::vector<PlanetInfo> v = g_planets;
    std::stable_sort(v.begin(), v.end(), [](const PlanetInfo& a, const PlanetInfo& b) { return a.players > b.players; });
    return v;
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
    std::string s = b;
    static const char* steps[] = {"-", "downloading", "loading", "hosting"};
    snprintf(b, sizeof b,
             "\nworld: %s%s stored=%u KB down=%u/%u chunks upload=%s (%u/%u chunks) next upload in %.0f s, uploads %u, "
             "downloads %u, hand-overs %u, last upload: %s",
             steps[g_hstep], g_waiting_host ? " (waiting for the new host)" : "", g_world_bytes >> 10, g_down.got, (unsigned)g_down.have.size(),
             g_upstep == kUpIdle ? "idle" : g_upstep == kUpSaving ? "saving" : "sending", g_up.acked,
             g_up.on ? g_up.chunks() : 0u, g_hstep == kHosting ? g_next_upload - H->time_seconds() : 0.0,
             g_uploads_done, g_downloads_done, g_handovers, g_last_upload.empty() ? "-" : g_last_upload.c_str());
    return s + b;
}

void set_upload_every(double s) { g_upload_every = s < 10 ? 10 : s; }

} // namespace galaxy
