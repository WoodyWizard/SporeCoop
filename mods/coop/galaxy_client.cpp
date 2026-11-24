#include "galaxy_client.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <algorithm>
#include <deque>
#include <vector>

#include "galaxy_crt.h"
#include "net/galaxy_crypto.h"
#include "net/galaxy.h"
#include "net/protocol.h"
#include "net/transport.h"
#include "world.h"
#include "fixes.h"
#include "mouse.h"
#include "sporekit_game.hpp"
#include <windows.h>
#include "game.h"
#include "modes.h"
#include "parties_net.h"

namespace galaxy {

using namespace net::galaxy;

static const SkHost* H;
static SkMod* M;
static Ctx C;

static net::UdpSocket g_sock;
static net::Endpoint g_server;
static bool g_welcomed;
static uint16_t g_slot;
static_assert(kMaxPlanetPlayers == net::kMaxPlayers, "a planet's players are one session's");
static uint8_t g_planet_players = kPlanetPlayers; // 6E B: what the server's WELCOME says a planet takes
static uint32_t g_refresh_secs;                   // BUG-9285: the server's nest refresh (0 off)
static uint32_t g_rotate_secs;                    // BUG-9286: the server's nest rotation (0 off)
static int64_t g_clock_offset;                    // BUG-9285: the server's clock minus ours (its PONGs)
static bool g_clock_known;
static uint64_t g_fan_tx, g_fan_tx_bytes, g_fan_rx, g_fan_rx_bytes, g_fan_refused; // 6F B
static double g_reconnect_at = -100; // 6H A
// 6I A: the host's last upload the server stored (its crc, world, the save's newest write, when it was packed); a guest's
// stored world to load before it joins (crc, 0 none)
static uint32_t g_up_crc, g_up_world, g_last_crc, g_last_world;
static uint64_t g_up_write, g_last_write;
static double g_up_packed, g_last_packed = -1000;
static uint32_t g_join_crc;
static bool g_fix_stored_join = true; // ([galaxy] stored_join=0: a joiner gets the world from the host as before)
static uint32_t g_stored_joins, g_stored_accepted, g_stored_refused;
constexpr double kStoredJoinMaxAge = 150; // s: an older stored world is not offered as the joiner's world
static uint32_t g_reconnects;
static void send_hello();
static uint64_t g_cookie; // 6S: the server's cookie for this address (CHALLENGE)
static uint32_t g_challenges = 0, g_key_refusals = 0;
uint32_t key_refusals() { return g_key_refusals; }
static void reset_planet_steps();
static std::string g_galaxy, g_planet, g_last_refusal;
static uint8_t g_role;      // kHostRole / kGuestRole on a planet, 0 none
static uint16_t g_host_slot;
static bool g_relay_on;     // the session uses the relay transport now
static bool g_session_seen; // the planet's session was up once (its end then means: left the planet)
static double g_next_ping, g_last_rx, g_hello_at;
using PlanetRow = PlanetInfo;
static std::vector<PlanetRow> g_planets;
// BUG-9339: the menu's poll put the whole planet list on the screen in one line ("Planets: Abber|Civ (0, no host, saved)
// ...", cut off at the screen's edge with a handful of planets): the screen gets "<galaxy>: N planets, M players online",
// the log and `coop.galaxy list` the whole list. Off: the whole list on the screen
static bool g_short_list = true;
COOP_FIX("galaxy.short_planet_list", "BUG-9339", g_short_list, "galaxy: the planet list on the screen is one short line (the whole list in the log)");
static bool g_list_asked; // coop.galaxy list asked: its answer is the whole list
static uint32_t g_planets_ver;
static std::vector<PlanetRow> g_list_pages; // S10: the pages of the list being received
static unsigned g_list_paged;
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
// BUG-9278 (step 4 part 3): a party's crossing to the next stage
static bool g_cross_fix = true;
COOP_FIX("galaxy.party_stage_end", "BUG-9278", g_cross_fix,
         "galaxy: a party that ends its stage moves on into the planet's next era with its leader (the others keep theirs)");
static double g_moveon_at = -100, g_moved_at = -100;
static bool g_arrive_sent = false;
static double g_arrive_ready_since = 0;
static std::string g_pending_enter; // kMovedJoin: entered once the crossing's session is closed
static uint32_t g_moveons_sent = 0, g_crossings = 0, g_arrivals = 0;
static std::string g_pending_join; // join this address once the old session is closed
// BUG-9336 (5R A): this player starts a stage its planet has no era of (the galaxy menu's stage choice): STARTERA; in
// its start crossing (the era below's world, hosted by it) the game plays that stage's end by itself (C.start_stage),
// then it ARRIVEs as any crossing
static bool g_start_fix = true;
COOP_FIX("galaxy.stage_start", "BUG-9336", g_start_fix,
         "galaxy: a player can start a stage its planet has no era of yet (from the stage below's world)");
static uint8_t g_start_era;        // the stage this game starts (0 none)
static double g_start_sent = -100; // STARTERA went out
static double g_start_since = -1;  // its start crossing hosted and played since
static bool g_start_asked;         // the stage's end was asked there
static uint32_t g_starts_sent, g_start_ends;
static uint32_t g_handovers;
// decision 6B C: the warm hand-over. g_planet_world: the world this game plays on the planet (a session up, in a stage);
// g_token: the last hand-over's token; g_token_world: the world it is valid for (this game kept it loaded)
static uint32_t g_planet_world;
static std::string g_planet_file; // its save file ("Name.spo") while this game plays the planet (galaxy_pack in a menu)
// BUG-9268: the game's own reload of the hosted planet (through the galaxy menu) is not leaving it
static bool g_reload_stays = true, g_reload_said = false;
// BUG-9272: the server forgot this game (a server restart, a 20 s freeze while the game saved or loaded): once welcomed
// again it enters the planet it was on; a host still playing (or loading) that planet hosts it again with no reload
static bool g_resume_fix = true;
COOP_FIX("galaxy.resume", "BUG-9272", g_resume_fix,
         "galaxy: a game the server forgot (its restart, a long freeze) goes back on its planet once connected again");
static struct {
    std::string planet;
    uint32_t world = 0;
    double at = 0, enter_at = 0; // (a former guest enters 4 s after its welcome: its old host takes the planet first)
    bool host = false;
} g_resume;
static uint32_t g_resumes = 0, g_resumed_hosts = 0;
static bool g_resume_hold = false; // the old planet session's end (it was left when the server forgot this game) is no leave
static uint32_t g_reloads_kept = 0;
COOP_FIX("galaxy.reload_stays", "BUG-9268", g_reload_stays,
         "galaxy: a host whose game reloads the planet it hosts (through the galaxy menu) stays its host, not handing it over");
static uint64_t g_token;
static uint32_t g_token_world;
static uint32_t g_warm_hosts, g_warm_joins, g_warm_accepted;
// decision 6A A: every way out saves first; saves are Spore's silent save (world::silent_save: no pause, no box)
static bool g_up_nosave;          // the stage was left for the menu: Spore saved on the way, pack that save
static bool g_close_after;        // Alt+F4 / the X: close the window once the planet is saved and left
static double g_close_asked;
static double g_last_hurt = -100, g_quiet_wait_since = -1;
static float g_last_hp = -1;
static uint32_t g_quiet_waits, g_menu_handovers, g_close_holds, g_silent_saves, g_idle_saves;
static double g_hosted_stored_at = -1; // hosting a stored planet since: the first creature is asked for
static std::deque<std::pair<uint16_t, std::vector<uint8_t>>> g_queue; // relay packets for the session
static uint64_t g_rx, g_tx, g_rx_bytes, g_tx_bytes, g_queue_dropped;

static void on_control(net::Reader& r);
static void host_now();
static bool fetch_fallback(const std::string& why);
static double g_fetch_sent;   // the FETCH last went out (again every 4 s until its XBEGIN: it is one UDP datagram)
static uint32_t g_fetch_got;  // the chunks at the last look, and when they last grew
static double g_fetch_progress;
static uint32_t g_fetch_again, g_fetch_fallbacks;
static void downloaded();
static void upload_done(bool ok, const std::string& why);
static std::string leave_now();

// decision 8Q A (net/galaxy_crypto.h): the galaxy traffic is encrypted. connect() exchanges keys with the server first
// (its answer carries the server's static key, kept per server in coop.ini [galaxy_keys]: another key later is refused),
// then everything goes sealed. A server that does not answer in 6 s (an older one) is played in clear, unless this game
// knew its key (then nothing goes in clear). Switch `coop.fix galaxy.encrypt off`: in clear, as before
namespace gc = net::galaxy::crypto;
static bool g_encrypt = true;
COOP_FIX("galaxy.encrypt", "8Q A", g_encrypt, "galaxy: the galaxy traffic is encrypted (a key exchange before HELLO)");
enum { kKxOff, kKxAsking, kKxOn, kKxClear };
static int g_kx = kKxOff;
static gc::KeyPair g_kx_eph;
static gc::Channel g_chan;
static double g_kx_started, g_kx_sent;
static bool g_kx_pinned, g_kx_new, g_kx_told;
static std::string g_kx_fp; // the server key's fingerprint
static unsigned g_kx_exchanges, g_kx_mismatch, g_kx_fallbacks, g_kx_clear_dropped, g_kx_bad;
static std::string pin_name() { return g_server.str(); }
static std::string pinned_key()
{
    char b[80] = {};
    H->config_str(M, "galaxy_keys", pin_name().c_str(), "", b, sizeof b);
    return b;
}
// every datagram to the galaxy server goes here: sealed once the keys are exchanged, nothing while they are
static bool raw_send(const uint8_t* d, size_t n)
{
    if (!g_sock.is_open())
        return false;
    if (g_kx == kKxOn) {
        std::vector<uint8_t> sealed;
        return g_chan.seal(d, n, sealed) && g_sock.send_to(g_server, sealed.data(), sealed.size());
    }
    if (g_kx == kKxAsking)
        return false;
    return g_sock.send_to(g_server, d, n);
}
static void hello_now();
static void say(const std::string& s);
static void keyx_start()
{
    g_chan.wipe();
    g_kx_eph.wipe();
    g_kx_told = false;
    if (!g_encrypt || !g_kx_eph.make()) { // (off, or no random generator: in clear)
        g_kx = kKxClear;
        hello_now();
        return;
    }
    g_kx = kKxAsking;
    g_kx_started = g_kx_sent = g_hello_at = g_last_rx = H->time_seconds();
    g_kx_pinned = !pinned_key().empty();
    const std::vector<uint8_t> q = gc::keyx_question(g_kx_eph);
    g_sock.send_to(g_server, q.data(), q.size());
}
static void keyx_answer(const uint8_t* d, size_t n)
{
    if (g_kx != kKxAsking)
        return;
    uint8_t st[32];
    gc::Channel ch;
    if (!gc::keyx_game(g_kx_eph, d, n, st, ch)) {
        g_kx_bad++;
        return;
    }
    const std::string have = pinned_key(), got = gc::hex(st, 32);
    g_kx_fp = gc::fingerprint(st);
    if (!have.empty() && have != got) {
        g_kx_mismatch++;
        g_kx = kKxOff;
        ch.wipe();
        H->log(M, SK_WARN, "galaxy: the server %s answered with key %s, not the one this game knew: refused (8Q A)",
               g_server.masked().c_str(), g_kx_fp.c_str());
        say("This galaxy server answers with another key than before, so it was not joined (someone may stand between "
            "you and it). If its owner set it up again: coop.galaxy forgetkey, then connect.");
        g_sock.close();
        g_welcomed = false;
        return;
    }
    g_kx_new = have.empty();
    if (g_kx_new)
        H->config_set_str(M, "galaxy_keys", pin_name().c_str(), got.c_str());
    g_chan = ch;
    ch.wipe();
    g_kx_eph.wipe();
    g_kx = kKxOn;
    g_kx_exchanges++;
    H->log(M, SK_INFO, "galaxy: encrypted with %s, its key %s%s (8Q A)", g_server.masked().c_str(), g_kx_fp.c_str(),
           g_kx_new ? " (new: kept)" : "");
    hello_now();
}
static void keyx_tick(double now)
{
    if (g_kx != kKxAsking)
        return;
    if (now - g_kx_started > 6) {
        if (g_kx_pinned) { // (it answered encrypted before: never in clear)
            if (!g_kx_told) {
                g_kx_told = true;
                say("The galaxy server does not answer the encrypted connect (it did before): still trying.");
            }
        } else {
            g_kx = kKxClear;
            g_kx_fallbacks++;
            H->log(M, SK_WARN, "galaxy: no answer to the key exchange from %s in 6 s: in clear (an older server, 8Q A)",
                   g_server.masked().c_str());
            hello_now();
            return;
        }
    }
    if (now - g_kx_sent >= 1.0) {
        g_kx_sent = now;
        const std::vector<uint8_t> q = gc::keyx_question(g_kx_eph);
        g_sock.send_to(g_server, q.data(), q.size());
    }
}
static std::string encryption_word()
{
    switch (g_kx) {
    case kKxOn: return "encrypted (the server's key " + g_kx_fp + (g_kx_new ? ", new" : "") + ")";
    case kKxAsking: return "exchanging keys";
    case kKxClear: return g_encrypt ? "in clear (an older server)" : "in clear (galaxy.encrypt off)";
    default: return "-";
    }
}
bool chat(uint8_t channel, const std::string& text) // 8O
{
    if (!g_welcomed || text.empty())
        return false;
    net::Writer w = control(GCHAT);
    w.u8(channel);
    w.str16(text.substr(0, kChatMaxBytes));
    return raw_send(w.data.data(), w.data.size());
}
std::string forget_key()
{
    if (!g_server.port)
        return "Connect to a galaxy server first (its key is kept by its address).";
    if (pinned_key().empty())
        return "No key kept for " + g_server.masked() + ".";
    H->config_set_str(M, "galaxy_keys", pin_name().c_str(), "");
    return "The key kept for " + g_server.masked() + " is forgotten: the next connect keeps the key it answers with.";
}

static void datagram(const uint8_t* buf, int n);
// everything the galaxy server sent: control messages handled, relay packets queued for the session
static void pump()
{
    if (!g_sock.is_open())
        return;
    uint8_t raw[65536];
    net::Endpoint from;
    int got;
    std::vector<uint8_t> plain;
    // (the socket can close inside the loop: a refused server key. A closed socket's recv_from fails at once, every
    // time: the loop spun forever, seen on the MSI 2026-11-23)
    while (g_sock.is_open() && (got = g_sock.recv_from(from, raw, sizeof raw)) != 0) {
        if (got < 0 || from != g_server)
            continue;
        if (raw[0] == gc::kKeyx) { // 8Q A: the server's answer (in clear)
            if (got == 2 && raw[1] == gc::kKeyxNone && g_kx == kKxOn) { // it has no keys for this game (a restart): as
                net::Writer w = control(REFUSED);                         // its "not connected" (a new exchange)
                w.str8(kNotConnected);
                datagram(w.data.data(), (int)w.data.size());
                continue;
            }
            keyx_answer(raw, (size_t)got);
            continue;
        }
        if (raw[0] == gc::kSealed) {
            if (g_kx != kKxOn || !g_chan.open(raw, (size_t)got, plain) || plain.empty())
                continue;
            datagram(plain.data(), (int)plain.size());
            continue;
        }
        // in clear while encrypted: only the server's "not connected" (it restarted and lost the keys) gets through
        if (g_kx == kKxOn && !(raw[0] == kControl && got >= 2 && raw[1] == REFUSED)) {
            g_kx_clear_dropped++;
            continue;
        }
        if (g_kx == kKxAsking)
            continue;
        datagram(raw, got);
    }
}
static void datagram(const uint8_t* buf, int n)
{
    {
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
        } else if (buf[0] == kFan && n >= 4) { // 6F B: a copy of the host's message, from the galaxy server
            uint16_t slot;
            memcpy(&slot, buf + 1, 2);
            g_fan_rx++;
            g_fan_rx_bytes += (uint64_t)n;
            // only from this planet's host, while this game plays the planet as a guest through the relay
            if (slot != g_host_slot || g_role != kGuestRole || !g_relay_on || !C.sess ||
                C.sess->role() != net::Role::Client || !C.sess->connected() || !C.fan_in) {
                g_fan_refused++;
                return;
            }
            net::Reader r(buf + 4, (size_t)n - 4);
            C.fan_in(buf[3], r);
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
        return raw_send(f.data(), f.size());
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
    raw_send(w.data.data(), w.data.size()); // (8Q A: sealed once the keys are exchanged)
}
static std::map<uint8_t, std::function<void(net::Reader&)>> g_handlers; // BUG-9287: on_message

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
    if (type == CHALLENGE) { // 6S: the server asks this address to prove itself: HELLO again with its cookie
        const uint64_t cookie = r.u64();
        static double last;
        const double now = H->time_seconds();
        if (!r.ok() || g_welcomed || now - last < 0.5)
            return;
        last = now;
        g_cookie = cookie;
        g_challenges++;
        send_hello();
        return;
    }
    if (type == WELCOME) {
        g_slot = r.u16();
        g_galaxy = r.str8();
        if (!r.ok())
            return;
        const uint8_t cap = r.left() >= 1 ? r.u8() : 4; // (a server before 6E B: 4)
        g_planet_players = cap < 2 ? 2 : cap > net::kMaxPlayers ? (uint8_t)net::kMaxPlayers : cap;
        g_refresh_secs = r.ok() && r.left() >= 4 ? r.u32() : 0; // BUG-9285 (an older server: no refresh)
        g_rotate_secs = r.ok() && r.left() >= 4 ? r.u32() : 0;  // BUG-9286 (an older server: no rotation)
        g_welcomed = true;
        say("Connected to the galaxy '" + g_galaxy + "'.");
        if (!g_resume.planet.empty() && H->time_seconds() - g_resume.at < 120) // BUG-9272: tick enters it
            g_resume.enter_at = H->time_seconds() + (g_resume.host ? 0.0 : 4.0);
        else
            g_resume = {};
    } else if (g_handlers.count(type)) { // BUG-9287: VISITOR (era_visitors.cpp), CRTHAVE / CRTPART (galaxy_crt.cpp)
        g_handlers[type](r);
    } else if (type == PARTYVIEW || type == PARTYNOTE || type == PLAYERS || type == WHISPER) { // 6K, 6L A
        if (C.party_in)
            C.party_in(type, r);
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
        if (r.left() >= 4) { // S10: one page of the list; the next is asked until it is whole (an older server: one)
            const uint16_t total = r.u16(), first = r.u16();
            if (r.left() >= 8 * v.size()) // 5Z B: each planet's creature's owner (an older server: none)
                for (auto& p : v)
                    p.owner = r.u64();
            if (first == 0)
                g_list_pages.clear();
            if (first != g_list_pages.size())
                return; // (a page of an older request)
            g_list_pages.insert(g_list_pages.end(), v.begin(), v.end());
            if (g_list_pages.size() < total && !v.empty()) {
                net::Writer w = control(LIST);
                w.u16((uint16_t)g_list_pages.size());
                send_control(w);
                return;
            }
            if (first)
                g_list_paged++;
            v = std::move(g_list_pages);
            g_list_pages.clear();
        }
        bool same = v.size() == g_planets.size();
        for (size_t i = 0; same && i < v.size(); i++)
            same = v[i].name == g_planets[i].name && v[i].players == g_planets[i].players && v[i].host == g_planets[i].host &&
                   v[i].stored == g_planets[i].stored && v[i].tech == g_planets[i].tech &&
                   !memcmp(v[i].species, g_planets[i].species, sizeof v[i].species) && v[i].owner == g_planets[i].owner;
        g_planets = v;
        const bool asked = g_list_asked;
        g_list_asked = false;
        if (same && !asked)
            return; // (the menu's poll: said once)
        if (!same)
            g_planets_ver++;
        std::string line = v.empty() ? "No planet has players yet: enter one to host it." : "Planets:";
        for (auto& p : v)
            line += " " + p.name + " (" + std::to_string(p.players) + (p.host ? "" : ", no host") +
                    (p.stored ? ", saved" : "") + ")";
        if (asked || !g_short_list || v.empty()) {
            say(line);
        } else { // BUG-9339: the menu's poll: the whole list in the log, a short line on the screen
            H->log(M, SK_INFO, "galaxy: %s", line.c_str());
            std::set<std::string> planets;
            unsigned players = 0;
            for (auto& p : v) {
                planets.insert(net::galaxy::crossing_planet(p.name));
                players += p.players;
            }
            char b[160];
            snprintf(b, sizeof b, "%s: %u planet%s, %u player%s online", g_galaxy.empty() ? "Galaxy" : g_galaxy.c_str(),
                     (unsigned)planets.size(), planets.size() == 1 ? "" : "s", players, players == 1 ? "" : "s");
            if (C.feed)
                C.feed(b);
        }
    } else if (type == ROLE) {
        std::string planet = r.str8();
        const uint8_t role = r.u8();
        const uint16_t host = r.u16();
        const uint32_t stored = r.u32();
        const uint64_t token = r.u64();
        if (!r.ok())
            return;
        const uint32_t stored_crc = r.left() >= 4 ? r.u32() : 0; // (6I A; an older server: none)
        const uint32_t stored_age = r.left() >= 4 ? r.u32() : 0xFFFFFFFFu;
        const bool handover = g_planet == planet && (g_waiting_host || g_role == kGuestRole);
        const bool resume = !g_resume.planet.empty() && g_resume.planet == planet; // BUG-9272
        const uint32_t resume_world = resume ? g_resume.world : 0;
        if (resume)
            g_resume = {};
        g_planet = planet;
        g_role = role;
        g_host_slot = host;
        g_world_bytes = stored;
        g_waiting_host = false;
        use_relay(true);
        std::string err;
        // 6B C: still in the planet's world (playing it): keep it (no reload, the token for the others' rejoin)
        const bool warm = handover && token && g_planet_world && modes::current_world() == g_planet_world &&
                          C.ready && C.ready();
        g_token = warm ? token : 0;
        g_token_world = warm ? g_planet_world : 0;
        if (warm && role == kHostRole && C.promote) // 6B C: the old host's creatures shown here go on as this game's
            C.promote();                            //   (taken before the session end drops them)
        if (handover && C.sess && C.sess->role() != net::Role::None) // (a guest of the old host: that session ends)
            C.sess->leave("the planet's host changes");
        if (handover) {
            g_handovers++;
            if (role == kGuestRole) { // joined from tick once the old session is closed
                g_pending_join = slot_endpoint(host).str();
                say("Joining " + era_display(planet) + "'s new host...");
                return;
            }
            say("You host " + era_display(planet) + " now: its host left.");
            if (role == kHostRole && (!stored || warm)) { // (host the world it plays once its old session closed)
                if (warm)
                    g_warm_hosts++;
                g_hstep = kLoading;
                g_hstep_at = H->time_seconds();
                g_left_stage = true;
                g_ready_since = 0;
                return;
            }
        }
        if (resume) {
            // BUG-9272: still in the planet's world, or loading it (the freeze happened in its load): host that one
            std::string lf = world::menu_load_planet(), want = era_planet(planet) + ".spo";
            for (auto& ch : lf) ch = (char)tolower((unsigned char)ch);
            for (auto& ch : want) ch = (char)tolower((unsigned char)ch);
            const bool playing = resume_world && modes::current_world() == resume_world && C.ready && C.ready();
            if (role == kHostRole && (playing || (!lf.empty() && lf == want) || !stored)) {
                g_resumed_hosts++;
                g_hstep = kLoading;
                g_hstep_at = H->time_seconds();
                g_left_stage = true;
                g_ready_since = 0;
                say("You host " + era_display(planet) + " again.");
                return;
            }
            if (role == kGuestRole && playing) { // (its world: the host's share or its stored copy as any join)
                g_pending_join = slot_endpoint(host).str();
                say("Joining " + era_display(planet) + " again...");
                return;
            }
        }
        if (role == kHostRole) {
            if (stored) { // its world first: download, load, then host
                g_hstep = kFetching;
                g_hstep_at = g_fetch_sent = g_fetch_progress = H->time_seconds();
                g_fetch_got = 0;
                g_down = {};
                send_control(control(FETCH));
                say("Downloading " + era_display(planet) + " from the galaxy (" + std::to_string(stored >> 10) + " KB)...");
            } else
                host_now();
        } else if (g_fix_stored_join && stored && stored_crc && stored_age < kStoredJoinMaxAge - 30) {
            // 6I A: a recent stored world: the planet from the galaxy, then its host (which takes it if nothing was saved
            // since; else it sends its world as before)
            g_join_crc = stored_crc;
            g_hstep = kFetching;
            g_hstep_at = g_fetch_sent = g_fetch_progress = H->time_seconds();
            g_fetch_got = 0;
            g_down = {};
            g_stored_joins++;
            send_control(control(FETCH));
            say("Downloading " + era_display(planet) + " from the galaxy (" + std::to_string(stored >> 10) + " KB)...");
        } else {
            // joined from tick (2026-11-18 review: this runs inside Session::update while a planet session is up, and
            // a join from there read as a join timeout at once)
            g_pending_join = slot_endpoint(host).str();
        }
    } else if (type == MOVED) { // BUG-9278: the party moves on to the next stage
        const std::string planet = r.str8();
        const uint8_t kind = r.u8();
        const uint16_t host = r.u16();
        const uint64_t token = r.u64();
        if (!r.ok() || planet.empty())
            return;
        const double now = H->time_seconds();
        if (kind == kMovedSplit) { // as the warm hand-over: the world this game plays stays
            const bool keep = token && g_planet_world && modes::current_world() == g_planet_world;
            g_planet = planet;
            g_host_slot = host;
            g_token = keep ? token : 0;
            g_token_world = keep ? g_planet_world : 0;
            g_moved_at = now;
            g_arrive_sent = false;
            g_arrive_ready_since = 0;
            g_crossings++;
            reset_planet_steps();
            if (host == g_slot) {
                g_role = kHostRole;
                if (C.sess && C.sess->role() == net::Role::Client && C.promote)
                    C.promote(); // (a guest's copies of the planet's creatures go on as this game's)
                if (C.sess && C.sess->role() != net::Role::None)
                    C.sess->leave("the party moves on to the next stage");
                g_hstep = kLoading;
                g_hstep_at = now;
                g_left_stage = true;
                g_ready_since = 0;
                say("Your party moves on to the next stage: you lead it there.");
            } else {
                g_role = kGuestRole;
                g_waiting_host = true;
                g_waiting_since = now;
                if (C.sess && C.sess->role() != net::Role::None)
                    C.sess->leave("the party moves on to the next stage");
                say("Your party moves on to the next stage with its leader...");
            }
        } else if (kind == kMovedRenamed) { // the crossing is the planet's next era now: same session
            g_planet = planet;
            g_arrivals++;
            if (g_role == kHostRole && g_hstep == kHosting)
                g_next_upload = now + 30; // (its first save there soon)
            say("Your party arrived: " + era_display(planet) + (g_role == kHostRole ? ", you host it." : "."));
        } else if (kind == kMovedJoin) { // the next era has players: join them (one load)
            reset_planet_steps();
            g_planet.clear();
            g_role = 0;
            g_arrivals++;
            g_pending_enter = planet;
            if (C.sess && C.sess->role() != net::Role::None)
                C.sess->leave("the party joins the players of the next stage");
            say("Your party arrived: joining the players of " + era_display(planet) + "...");
        }
    } else if (type == PONG) {
        if (r.left() >= 8) { // BUG-9285: the server's clock
            const int64_t srv = (int64_t)r.u64();
            if (r.ok() && srv > 0)
                g_clock_offset = srv - (int64_t)std::time(nullptr), g_clock_known = true;
        }
        // the server answers again after "stopped answering" and still knows this game (no REFUSED then): welcomed again
        if (!g_welcomed && H->time_seconds() - g_hello_at > 5)
            send_hello();
    } else if (type == HOSTMOVE) { // 5V A: the host left; the new one loads the world, then the server sends ROLE
        std::string planet = r.str8();
        const uint16_t next = r.u16();
        if (!r.ok() || planet != g_planet)
            return;
        const uint64_t token = r.u64();
        g_waiting_host = true;
        g_waiting_since = H->time_seconds();
        g_host_slot = next;
        // 6B C: still in the planet's world: the rejoin carries the token (the new host sends no world then)
        const bool keep = token && g_planet_world && modes::current_world() == g_planet_world;
        g_token = keep ? token : 0;
        g_token_world = keep ? g_planet_world : 0;
        if (C.sess && C.sess->role() != net::Role::None)
            C.sess->leave("the planet's host changes");
        say("The host of " + era_display(planet) + " left: another player takes it over, wait a moment...");
    } else if (type == XBEGIN) { // the planet's stored world (FETCH)
        net::galaxy::XferIn in;
        if (!in.header(r) || g_hstep != kFetching || (g_down.on && g_down.id == in.id))
            return; // (nothing allocated for a world nobody asked for)
        if (!in.size) { // (no chunk would ever come: 2026-11-18 review)
            fetch_fallback("the galaxy's copy is empty");
            return;
        }
        in.alloc();
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
        // 6H A: the server forgot this game (it timed it out: a game at 1 FPS, a server restart): connect again, at
        // most every 5 s; the planet is left (the server dropped it from the planet, the host times its session out)
        if (r.ok() && g_last_refusal == kIdentityTaken) // 6S: say it once, no reconnect loop
            g_key_refusals++;
        if (r.ok() && g_last_refusal == kNotConnected) {
            const double now = H->time_seconds();
            if (now - g_reconnect_at < 5.0)
                return;
            g_reconnect_at = now;
            g_reconnects++;
            say("The galaxy server lost this game: connecting again.");
            if (g_resume_fix && !g_planet.empty()) {
                g_resume = {g_planet, g_planet_world, now, 0, g_role == kHostRole};
                g_resume_hold = C.sess && C.sess->role() != net::Role::None;
            }
            if (!g_planet.empty()) {
                if (g_relay_on && C.sess && C.sess->role() != net::Role::None)
                    C.sess->leave("the galaxy server lost this game");
                reset_planet_steps();
                g_planet.clear();
                g_role = 0;
            }
            g_welcomed = false;
            if (g_kx == kKxOn) // 8Q A: it lost this game's keys too (a restart): a new exchange
                g_kx = kKxOff;
            send_hello();
            return;
        }
        if (r.ok())
            say("Galaxy server: " + g_last_refusal);
        if (r.ok() && g_hstep == kFetching) // (no stored world after all: a guest joins its host the old way)
            fetch_fallback("the galaxy has no copy for this game");
    } else if (type == HOSTGONE) {
        std::string planet = r.str8();
        if (!r.ok())
            return;
        say("The host of " + era_display(planet) + " left the galaxy.");
        if (g_role == kGuestRole && g_relay_on && C.sess && C.sess->role() != net::Role::None)
            C.sess->leave("the planet's host left"); // (tick takes it off the planet once the session is closed)
    }
}

// BUG-9354: the crossing's ARRIVE was sent once per game session: the flag was reset only by MOVED (a party moving on),
// so a second stage start (a start crossing comes by ROLE) never arrived as the planet's next era. Reset with the
// planet's other steps (leaving, a lost server, MOVED). Off: as before
static bool g_arrive_reset = true;
COOP_FIX("galaxy.arrive_reset", "BUG-9354", g_arrive_reset, "galaxy: each stage start's crossing arrives as the planet's next era");

static void reset_planet_steps()
{
    if (g_arrive_reset) {
        g_arrive_sent = false;
        g_arrive_ready_since = 0;
    }
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
        say("You host " + era_display(g_planet) + " for the galaxy.");
        g_hosted_stored_at = g_world_bytes ? H->time_seconds() : -1; // (hosted_stored_tick asks)
    } else {
        g_hstep = kNoHost;
        say("Could not host " + era_display(g_planet) + ": " + err);
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
        say("Could not load " + era_display(g_planet) + " from the galaxy: " + err);
        fetch_fallback(err);
        return;
    }
    g_downloads_done++;
    g_hstep = kLoading;
    g_hstep_at = H->time_seconds();
    g_left_stage = false;
    g_ready_since = 0;
    say("Loading " + era_display(g_planet) + "...");
}

static uint32_t ms(double t) { return (uint32_t)(int64_t)(t * 1000); }

// the upload's XBEGIN with the menu's facts of the planet (5X B): its tech level and the host's creature
static uint8_t g_up_tech, g_up_era;
static uint32_t g_up_species[3];
static net::Writer up_begin()
{
    net::Writer w = g_up.begin();
    w.u8(g_up_tech);
    for (uint32_t v : g_up_species)
        w.u32(v);
    w.u8(g_up_era); // (protocol 7: the server keeps an era's worlds of its stage only)
    return w;
}
// step 4 part 1: the era of a game mode (0: not a stage: a menu, an editor, a load)
uint8_t era_of_mode(uint32_t m)
{
    return m == modes::kCell ? kEraCell : m == modes::kCreature ? kEraCreature : m == modes::kTribe ? kEraTribe
         : m == modes::kCiv ? kEraCiv : m == modes::kSpace ? kEraSpace : kEraNone;
}
static uint8_t g_last_era; // the stage this game last played (an upload packed in the menu takes it)
static void up_facts()
{
    const uint32_t m = game::mode_id();
    g_up_era = era_of_mode(m) ? era_of_mode(m) : g_last_era;
    g_up_tech = g_up_era == kEraCreature ? 1 : g_up_era == kEraTribe ? 2 : g_up_era == kEraCiv ? 4 : g_up_era == kEraSpace ? 5 : 0;
    g_up_species[0] = g_up_species[1] = g_up_species[2] = 0;
    if (void* av = game::avatar()) {
        const game::Key k = game::species_key(av);
        g_up_species[0] = k.instance, g_up_species[1] = k.type, g_up_species[2] = k.group;
        if (k.instance) // BUG-9355: the galaxy menu of a game without this creature asks the store for it (galaxy_menu.cpp)
            galaxy_crt::put(k.instance, k.type, k.group);
    }
}

// BUG-9348 (above start_upload)
static bool g_space_hint = true;
COOP_FIX("galaxy.space_save_hint", "BUG-9348", g_space_hint, "galaxy: a Space era host at its planet is told the world is saved once the ship is in the solar system");
static std::string g_space_hint_for;
static unsigned g_space_hints;
// BUG-9359: a planet stored while its host was on the death screen loads into that screen for the next host ("You were
// killed in battle!" on arrival: Spore keeps the death in the save; full health on arrival did not stop it). The
// timed save waited for a quiet moment, but only 150 s, and leaving (Leave, the menu, Alt+F4) saved at once. On: no
// galaxy save while the host's creature is dead (the death screen up): the timed one waits for the respawn, leaving
// keeps the galaxy's last save (the player is told). Off: as before.
static bool g_no_dead_save = true;
COOP_FIX("galaxy.no_dead_save", "BUG-9359", g_no_dead_save, "galaxy: a planet is never stored while its host's creature is dead");
static bool g_host_dead;           // the host's creature at the last look in the Creature stage (kept for the menu's save)
static unsigned g_dead_saves_held; // timed saves put off, leaves without a save
static void host_dead_tick()
{
    if (game::mode_id() == modes::kMenu)
        return; // (Spore's save on the way to the menu was made with the last state seen)
    void* av = game::mode_id() == modes::kCreature ? game::avatar() : nullptr;
    g_host_dead = av && (game::is_dead(av) || game::health(av) <= 0);
}

static void start_upload()
{
    if (g_upstep != kUpIdle || g_hstep != kHosting || is_crossing(g_planet)) // (BUG-9278: a crossing is not stored)
        return;
    if (g_no_dead_save && g_host_dead) { // BUG-9359
        g_dead_saves_held++;
        if (g_leave_after) {
            H->log(M, SK_INFO, "galaxy: leaving with the creature dead: no save (BUG-9359)");
            upload_done(false, "your creature is dead; the galaxy keeps the last save from before");
        } else
            g_next_upload = H->time_seconds() + 15;
        return;
    }
    std::string err;
    if (g_up_nosave) { // (Spore saved when it left the stage for the menu)
        g_upstep = kUpSaving;
        g_up_at = H->time_seconds();
        return;
    }
    if (!world::silent_save(err)) { // (an editor, a cinematic, Space on a planet: again soon)
        g_next_upload = H->time_seconds() + 15;
        H->log(M, SK_INFO, "galaxy: no save for the upload now: %s", err.c_str());
        // BUG-9348: Spore saves Space only in a solar system or the galaxy: a planet's Space era waits for the ship to
        // leave the planet (the upload retries); the player is told once per planet. Off: no word
        if (g_space_hint && game::mode_id() == modes::kSpace && g_space_hint_for != g_planet && !g_leave_after) {
            g_space_hint_for = g_planet;
            g_space_hints++;
            say(era_display(g_planet) + " is saved to the galaxy once your ship is out in the solar system.");
        }
        if (g_leave_after)
            say("Waiting to save " + era_display(g_planet) + ": " + err);
        return;
    }
    g_upstep = kUpSaving;
    g_up_at = H->time_seconds();
    g_silent_saves++;
}

// no hit taken in the last 4 s, no cinematic, the creature alive: a save's hitch is least felt then, and a planet is
// never stored with its host dead (the next host loaded into the death screen, owner test 2026-11-17)
static bool quiet(double now)
{
    void* av = game::avatar();
    return now - g_last_hurt > 4 && !world::cinematic_playing() && !(av && game::health(av) <= 0);
}

static void hosted_stored_tick(double now)
{
    if (g_hosted_stored_at < 0 || g_hstep != kHosting)
        return;
    if (now - g_hosted_stored_at > 600 || !C.hosted_stored || C.hosted_stored()) // (a death screen may wait minutes for its OK)
        g_hosted_stored_at = -1;
}

// BUG-9336: the start crossing hosted and played, nothing else waiting (the first creature's editor first): the game's
// own stage end, once. Done when the planet is an era again (arrived) or the start came to nothing (refused)
static void start_tick(double now)
{
    if (!g_start_era)
        return;
    if ((!g_planet.empty() && !is_crossing(g_planet)) || (g_planet.empty() && now - g_start_sent > 30)) {
        g_start_era = 0;
        return;
    }
    if (g_start_asked || !is_crossing(g_planet) || g_role != kHostRole || g_hstep != kHosting || g_hosted_stored_at >= 0 ||
        era_of_mode(game::mode_id()) != era_of(g_planet) || !C.ready || !C.ready()) {
        if (!g_start_asked)
            g_start_since = -1;
        return;
    }
    if (g_start_since < 0)
        g_start_since = now;
    else if (now - g_start_since > 5 && C.start_stage && C.start_stage()) { // (the world settled first)
        g_start_asked = true;
        g_start_ends++;
        H->log(M, SK_INFO, "galaxy: start crossing %s: the stage's end asked (BUG-9336)", g_planet.c_str());
    }
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
    if (ok) {
        g_uploads_done++;
        g_last_crc = g_up_crc, g_last_world = g_up_world, g_last_write = g_up_write, g_last_packed = g_up_packed;
    }
    if (!ok || g_leave_after)
        say(ok ? "Saved " + era_display(g_planet) + " to the galaxy." : "Could not save " + era_display(g_planet) + " to the galaxy: " + why);
    g_up_nosave = false;
    if (g_leave_after) {
        g_leave_after = false;
        leave_now();
        if (g_disconnect_after) {
            g_disconnect_after = false;
            disconnect();
        }
        if (g_close_after) { // Alt+F4 / the X: the planet is saved and left, the window closes now
            g_close_after = false;
            if (HWND w = (HWND)H->game_window())
                PostMessageW(w, WM_CLOSE, 0, 0);
        }
    }
}

static void upload_tick(double now)
{
    if (g_close_after && now - g_close_asked > 30) { // (the save did not come: close anyway)
        g_close_after = false;
        if (HWND w = (HWND)H->game_window())
            PostMessageW(w, WM_CLOSE, 0, 0);
    }
    if (g_hstep != kHosting || g_role != kHostRole)
        return;
    host_dead_tick(); // BUG-9359
    // owner 2026-11-17: leaving to the main menu while hosting leaves the planet: Spore saved on the way, that save goes
    // to the galaxy and the planet is handed over
    // BUG-9268: not the game's own reload of this planet (a Tribe rescue, a new founder seat: world::load_slot goes
    // through the menu): the host comes back to the same planet and keeps it
    const bool own_reload = g_reload_stays && !g_planet_file.empty() && world::menu_load_planet() == g_planet_file;
    if (own_reload && game::mode_id() == modes::kMenu && !g_reload_said) {
        g_reload_said = true;
        g_reloads_kept++;
        H->log(M, SK_INFO, "galaxy: the game reloads %s through the menu: still its host (BUG-9268)", g_planet_file.c_str());
    } else if (!own_reload)
        g_reload_said = false;
    if (game::mode_id() == modes::kMenu && !own_reload && !g_leave_after && C.sess && C.sess->role() == net::Role::Host) {
        g_leave_after = true;
        g_up_nosave = true;
        g_menu_handovers++;
        say("You left " + era_display(g_planet) + " for the menu: saving it to the galaxy and handing it over.");
    }
    // decision 6A A: every 2 minutes while others are on the planet (a crashed host loses less), else every 5
    const bool others = C.sess && C.sess->players().size() > 1;
    const double every = others ? std::min(g_upload_every, 120.0) : g_upload_every;
    if (g_upstep == kUpIdle && !g_leave_after && g_next_upload > now + every)
        g_next_upload = now + every;
    if (void* av = game::avatar()) { // (a hit taken: the hp went down)
        const float hp = game::health(av);
        if (g_last_hp >= 0 && hp < g_last_hp - 0.01f)
            g_last_hurt = now;
        g_last_hp = hp;
    }
    if (g_upstep == kUpIdle && (now >= g_next_upload || g_leave_after) && C.sess && C.sess->role() == net::Role::Host) {
        // owner 2026-11-17 ("seamless"): the write itself is the only cost left: it waits for a moment the player does
        // not feel it (the game paused, or 8 s without input) for 90 s, then for a quiet one (no hit, no cinematic)
        if (!g_leave_after) {
            const uint64_t last = mouse::last_input_ms();
            const bool idle = world::time_paused() || (last && GetTickCount64() - last > 8000);
            if (g_quiet_wait_since < 0) {
                g_quiet_wait_since = now;
                g_quiet_waits++;
            }
            const double waited = now - g_quiet_wait_since;
            if (!(idle && quiet(now)) && !(waited > 90 && quiet(now)) && (waited < 150 || (g_no_dead_save && g_host_dead)))
                return; // (BUG-9359: no limit while the host is dead)
            if (idle)
                g_idle_saves++;
        }
        g_quiet_wait_since = -1;
        start_upload();
    }
    if (g_upstep == kUpSaving && now - g_up_at > 4 && !world::save_busy()) {
        std::vector<uint8_t> blob;
        std::string planet, err;
        if (!world::galaxy_pack(blob, planet, err, g_planet_file)) {
            g_upstep = kUpIdle;
            upload_done(false, err);
            return;
        }
        g_up_crc = net::galaxy::crc32(0, blob.data(), blob.size()); // 6I A: which save the server will hold
        g_up_world = modes::current_world();
        g_up_write = world::current_save_write();
        g_up_packed = now;
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
// The stored world does not come (2026-11-18 review: a lost FETCH left the player on "Downloading..." for good): a
// guest joins the planet's host as before 6I A (the host sends its world); a host-to-be has no other world: it leaves.
static bool fetch_fallback(const std::string& why)
{
    if (g_hstep != kFetching)
        return false;
    g_hstep = kNoHost;
    g_down = {};
    g_fetch_fallbacks++;
    if (g_role == kGuestRole && g_host_slot) {
        g_join_crc = 0;
        g_pending_join = slot_endpoint(g_host_slot).str(); // (joined from tick)
        say("The galaxy's copy of " + era_display(g_planet) + " did not come (" + why + "): joining its host instead...");
        return true;
    }
    say("Could not get " + era_display(g_planet) + " from the galaxy (" + why + "): left it.");
    leave_now();
    return true;
}

static void fetch_tick(double now)
{
    if (g_hstep != kFetching)
        return;
    if (g_down.on && g_down.got != g_fetch_got) {
        g_fetch_got = g_down.got;
        g_fetch_progress = now;
    }
    if (!g_down.on && now - g_fetch_sent > 4) { // (the server takes one FETCH per 3 s)
        g_fetch_sent = now;
        g_fetch_again++;
        send_control(control(FETCH));
    }
    if (now - g_fetch_progress > 30)
        fetch_fallback(g_down.on ? "the download stopped" : "no answer");
}

static void loading_tick(double now)
{
    if (g_hstep != kLoading)
        return;
    const bool ready = C.ready && C.ready();
    if (!ready)
        g_left_stage = true, g_ready_since = 0;
    else if (g_left_stage && !g_ready_since)
        g_ready_since = now;
    if (g_ready_since && now - g_ready_since > 3 && g_role == kGuestRole) { // 6I A: in the stored world: join its host
        g_hstep = kNoHost;
        g_token_world = modes::current_world();
        g_token = net::galaxy::stored_receipt(g_join_crc, g_token_world);
        g_join_crc = 0;
        std::string err;
        if (C.join && C.join(slot_endpoint(g_host_slot).str(), err))
            say("Joining " + era_display(g_planet) + "...");
        else
            say("Could not join " + era_display(g_planet) + ": " + err);
    } else if (g_ready_since && now - g_ready_since > 3)
        host_now();
    else if (now - g_hstep_at > 240) {
        say("Loading " + era_display(g_planet) + " took too long: left it.");
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

// HELLO goes after the key exchange (8Q A): while it runs, the exchange's end sends it
static void send_hello()
{
    if (g_kx == kKxAsking)
        return;
    if (g_kx == kKxOff) {
        keyx_start();
        return;
    }
    hello_now();
}
static void hello_now()
{
    net::Writer w = control(HELLO);
    w.u32(kProto);
    w.u64(C.identity ? C.identity() : 0);
    w.str8(C.name ? C.name() : "Player");
    w.str8(C.version);
    w.u64(C.key ? C.key() : 0); // 6S (BUG-9273)
    w.u64(g_cookie);
    send_control(w);
    g_hello_at = g_last_rx = H->time_seconds();
    g_next_ping = g_hello_at + 2;
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
    g_kx = kKxOff; // 8Q A: a new connection exchanges keys first
    send_hello();
    return "Connecting to the galaxy server at " + ep.masked() + "...";
}

static bool g_direct_after_fix = true;
COOP_FIX("galaxy.direct_after_disconnect", "BUG-9369", g_direct_after_fix,
         "galaxy: disconnecting from a galaxy server gives the session its plain network back (a direct game could not be hosted or joined after it)");
static unsigned g_direct_restored;
std::string disconnect()
{
    if (!g_sock.is_open())
        return "Not connected to a galaxy server.";
    if (g_hstep == kHosting && g_role == kHostRole && !g_leave_after) { // its world first
        g_disconnect_after = true;
        return leave();
    }
    leave_now();
    // BUG-9369: leave_now leaves the planet session's relay transport on "until the session is closed, tick switches it
    // off", but tick does nothing once the socket is closed: the session kept the relay for good and every later direct
    // game of this run failed (`coop.host`: "host failed: not connected to a galaxy server"; a join never connected).
    // On: the session ends here and goes back to the plain network. Off: as before (a restart was needed).
    if (g_direct_after_fix && C.sess && g_relay_on) {
        if (C.sess->role() != net::Role::None)
            C.sess->leave("left the galaxy");
        use_relay(false);
        g_direct_restored++;
    }
    g_sock.close();
    g_welcomed = false;
    g_planets.clear();
    g_chan.wipe(); // 8Q A
    g_kx = kKxOff;
    return "Disconnected from the galaxy server.";
}

std::string list()
{
    if (!g_welcomed)
        return "Not connected to a galaxy server.";
    g_list_asked = true; // BUG-9339: asked by name: the whole list
    send_control(control(LIST));
    return "Asking the galaxy server for its planets...";
}

// step 4 part 1: a plain planet name to its era's key: the era this game plays (in a stage); in the menu the planet's
// only era on the server; "" and why when it cannot be told
static std::string era_for(const std::string& planet, std::string& why)
{
    if (era_of(planet))
        return planet;
    if (const uint8_t e = era_of_mode(game::mode_id()))
        return era_key(planet, e);
    std::string found, all;
    int n = 0;
    for (const auto& p : g_planets)
        if (era_of(p.name) && era_planet(p.name) == planet) {
            found = p.name;
            all += (n++ ? ", " : "") + std::string(era_name(era_of(p.name)));
        }
    if (n == 1)
        return found;
    why = n ? planet + " has " + all + ": enter " + planet + "|<stage> (for example " + era_key(planet, era_of(found)) + ")."
            : "Load a world of " + planet + " first (its stage is the era it hosts), or enter " + planet + "|<stage>.";
    return "";
}

static std::string enter_with(const std::string& planet_in, uint8_t flags);
std::string enter(const std::string& planet_in) { return enter_with(planet_in, 0); }

// decision 8N A (owner 2026-11-23: "8NA - Once they exit to Space, they can see players. Space age players can still
// visit the planet, they just can't start there."): a party's private planet. The Social Menu's "Private planet: New"
// waits for the next new game this player starts on an empty star (a world it was not playing at the press); that
// world enters the galaxy as a new planet with kEnterPrivate: the server lets its party (and friends the leader takes
// into the party) see and enter it, others only its Space era. Off: no row, no private planets
static bool g_private_fix = true;
COOP_FIX("galaxy.private_planets", "8N A", g_private_fix, "galaxy: a party's private planet (Social Menu: Private planet, New, then a new game)");
static bool g_private_pending;
static std::string g_private_old_world; // the world played at the press (not the new game)
static std::string g_private_seen;
static double g_private_seen_at;
static unsigned g_private_made;
bool private_on() { return g_private_fix && g_welcomed; }
bool private_pending() { return g_private_pending; }
std::string private_request(bool on)
{
    if (!private_on())
        return "Private planets need a galaxy server.";
    g_private_pending = on;
    if (!on)
        return "No private planet then.";
    g_private_old_world = modes::current_world_name();
    g_private_seen.clear();
    return "Start a new game on an empty star: it becomes your party's private planet. Only your party (and friends you "
           "invite to it) can play there; others can visit its Space stage.";
}
// BUG-9364: a player connected to a galaxy who started a new game on an empty star played it off the galaxy (only the
// private row above or the console put a new world on the server): nobody saw or could join the new planet, and the
// owner's idea of the menu (2026-11-17, "'New game' makes a new planet on the server") was never built. On: a world
// this player starts while connected (not the one it played at the connect, on a planet or in a session before)
// enters the galaxy as a new planet once it settled for 5 s, as the private one does; a name the galaxy already has
// stays off the galaxy (entering it would join that planet and drop the new world; the player is told). One try per
// world. Off: only the private row and the console.
static bool g_new_planet_fix = true;
COOP_FIX("galaxy.new_game_planet", "BUG-9364", g_new_planet_fix,
         "galaxy: a new game started while connected becomes a planet of the galaxy");
static std::string g_online_old_world; // the world of the connect, of a planet or session, or tried: never entered by itself
static unsigned g_new_planets_made, g_new_planets_taken;
std::string new_planet_status()
{
    return "new game planets (BUG-9364, " + std::string(g_new_planet_fix ? "on" : "off") + "): made " +
           std::to_string(g_new_planets_made) + ", name taken " + std::to_string(g_new_planets_taken) + ", old world '" +
           g_online_old_world + "'";
}
static void private_tick(double now)
{
    const std::string w = modes::current_world_name();
    if (!g_welcomed || !g_planet.empty() || (C.sess && C.sess->role() != net::Role::None)) {
        g_online_old_world = w; // BUG-9364: not a new game started while connected and free
        return;
    }
    const bool priv = g_private_pending;
    if (!priv && !g_new_planet_fix)
        return;
    const uint8_t era = era_of_mode(game::mode_id());
    if (!era || w.size() < 5 || w.compare(w.size() - 4, 4, ".spo") || w == (priv ? g_private_old_world : g_online_old_world)) {
        g_private_seen.clear();
        return;
    }
    if (w != g_private_seen) { // (the new game settled for 5 s: its name is its own)
        g_private_seen = w;
        g_private_seen_at = now;
        return;
    }
    if (now - g_private_seen_at < 5)
        return;
    const std::string name = w.substr(0, w.size() - 4);
    const std::string key = era_key(name, era);
    g_online_old_world = w; // (one try per world)
    if (priv) {
        g_private_pending = false;
        g_private_made++;
        H->log(M, SK_INFO, "galaxy: '%s' becomes this party's private planet (8N A)", key.c_str());
        say(enter_with(key, kEnterPrivate) + " (your party's private planet)");
        return;
    }
    for (const auto& p : g_planets)
        if (era_of(p.name) && era_planet(p.name) == name) {
            g_new_planets_taken++;
            H->log(M, SK_WARN, "galaxy: the new game '%s' stays off the galaxy: it has a planet of that name (BUG-9364)", name.c_str());
            say("This galaxy already has a planet named " + name + ": your new game stays off the galaxy. Start one with another name to share it.");
            return;
        }
    g_new_planets_made++;
    H->log(M, SK_INFO, "galaxy: the new game '%s' becomes a planet of the galaxy (BUG-9364)", key.c_str());
    say(enter_with(key, 0) + " (your new planet joins the galaxy: others can play here)");
}
std::string enter_private(const std::string& planet) { return enter_with(planet, kEnterPrivate); } // tests

static std::string enter_with(const std::string& planet_in, uint8_t flags)
{
    if (!g_welcomed)
        return "Not connected to a galaxy server.";
    if (planet_in.empty())
        return "Which planet?";
    std::string why;
    const std::string planet = era_for(planet_in, why);
    if (planet.empty())
        return why;
    if (C.sess && C.sess->role() != net::Role::None && !g_relay_on)
        return "Leave the current session first.";
    // 2026-11-18 review: entering another planet while this game hosts one dropped that planet's last save, and the
    // "leave after the save" then threw the player off the new planet: the hosted planet is saved and left first
    if (!g_planet.empty() && planet != g_planet) {
        if (g_hstep == kHosting && g_role == kHostRole && C.sess && C.sess->role() == net::Role::Host) {
            const std::string old = g_planet;
            leave();
            return "Saving " + era_display(old) + " to the galaxy first: enter " + era_display(planet) + " again in a moment.";
        }
        leave_now(); // (a guest: off the old planet, its session closed, before the new one's ROLE)
    }
    net::Writer w = control(ENTER);
    w.str8(planet);
    if (flags)
        w.u8(flags); // 8N A
    send_control(w);
    return "Entering " + era_display(planet) + "...";
}

bool stage_start_on() { return g_start_fix; }
std::string start_era(const std::string& from, uint8_t era)
{
    if (!g_start_fix)
        return "Starting a new stage is off (coop.fix galaxy.stage_start).";
    if (!g_welcomed)
        return "Not connected to a galaxy server.";
    if (!era_of(from) || is_crossing(from) || era != era_of(from) + 1)
        return "That stage cannot be started there.";
    if (C.sess && C.sess->role() != net::Role::None && !g_relay_on)
        return "Leave the current session first.";
    if (!g_planet.empty()) { // (as enter: a hosted planet is saved and left first)
        if (g_hstep == kHosting && g_role == kHostRole && C.sess && C.sess->role() == net::Role::Host) {
            const std::string old = g_planet;
            leave();
            return "Saving " + era_display(old) + " to the galaxy first: try again in a moment.";
        }
        leave_now();
    }
    net::Writer w = control(STARTERA);
    w.str8(from);
    w.u8(era);
    send_control(w);
    g_start_era = era;
    g_start_sent = H->time_seconds();
    g_start_since = -1;
    g_start_asked = false;
    g_starts_sent++;
    return std::string("Starting the ") + era_name(era) + " stage on " + era_planet(from) + "...";
}

std::string leave()
{
    if (g_planet.empty())
        return "Not on a planet of the galaxy.";
    if (is_crossing(g_planet)) // (BUG-9278: nothing to save)
        return leave_now();
    if (g_hstep == kHosting && g_role == kHostRole && C.sess && C.sess->role() == net::Role::Host) {
        if (g_leave_after)
            return "Saving " + era_display(g_planet) + " to the galaxy before leaving...";
        g_leave_after = true; // upload_tick saves and uploads it, upload_done leaves
        return "Saving " + era_display(g_planet) + " to the galaxy before leaving...";
    }
    return leave_now();
}

std::string save()
{
    if (g_hstep != kHosting || g_role != kHostRole)
        return "Only the planet's host saves it to the galaxy.";
    if (g_upstep != kUpIdle)
        return "Saving " + era_display(g_planet) + " to the galaxy already.";
    g_next_upload = 0;
    return "Saving " + era_display(g_planet) + " to the galaxy...";
}

static std::string leave_now()
{
    if (g_planet.empty())
        return "Not on a planet of the galaxy.";
    reset_planet_steps();
    // 6E B (test 2026-11-18): the goodbye first, through the relay, then LEAVE. LEAVE first ended the relay at the
    // server, the goodbye was dropped and the host kept the place 30 s (a full planet refused the next player)
    const bool bye = C.sess && C.sess->role() != net::Role::None && g_relay_on;
    if (bye)
        C.sess->leave("left the planet"); // (the relay stays until the session is closed, tick switches it off)
    send_control(control(LEAVE));
    std::string p = g_planet;
    g_planet.clear();
    g_role = 0;
    if (bye)
        g_session_seen = true;
    else
        use_relay(false);
    return "Left " + p + ".";
}

void tick(double now)
{
    if (!g_sock.is_open())
        return;
    if (!g_relay_on || !C.sess || C.sess->role() == net::Role::None)
        pump(); // (while a planet session runs, its transport reads)
    keyx_tick(now); // 8Q A
    fetch_tick(now);
    loading_tick(now);
    upload_tick(now);
    hosted_stored_tick(now);
    start_tick(now);
    private_tick(now); // 8N A
    if (g_welcomed && game::mode_id() == modes::kMenu && now >= g_next_list) { // the galaxy menu's list (5X B)
        g_next_list = now + 10;
        send_control(control(LIST));
    }
    if (!g_pending_join.empty() && C.sess && C.sess->role() == net::Role::None) { // 5V A: the new host
        std::string err, addr = g_pending_join;
        g_pending_join.clear();
        if (C.join && C.join(addr, err))
            say("Joining " + era_display(g_planet) + "...");
        else {
            say("Could not join " + era_display(g_planet) + ": " + err);
            leave_now();
        }
    }
    if (g_waiting_host && now - g_waiting_since > 360) {
        say("The new host of " + era_display(g_planet) + " did not come: left it.");
        leave_now();
    }
    if (now >= g_next_ping) {
        g_next_ping = now + 2;
        net::Writer ping = control(PING); // with this game's planet state (a lost LEAVE or READY: net/galaxy.h)
        ping.str8(g_planet);
        ping.u8(g_planet.empty() ? kPingNone : g_hstep == kHosting && g_role == kHostRole ? kPingHosting : kPingOnPlanet);
        send_control(ping);
    }
    if (g_welcomed && now - g_last_rx > 20) {
        say("The galaxy server stopped answering.");
        g_welcomed = false;
    } else if (!g_welcomed && now - g_hello_at > 10 && g_hello_at > 0) {
        say("No answer from the galaxy server.");
        g_hello_at = 0;
    }
    if (!g_planet.empty() && C.sess && C.sess->connected() && C.ready && C.ready())
        g_planet_world = modes::current_world(); // (6B C: the world this game plays on the planet)
    if (const uint8_t e = era_of_mode(game::mode_id()))
        g_last_era = e; // (step 4: the era of an upload packed in the menu)
    if (!g_planet.empty() && g_planet_world && modes::current_world() == g_planet_world) {
        const std::string f = modes::current_world_name();
        if (f.size() > 4 && f.compare(f.size() - 4, 4, ".spo") == 0)
            g_planet_file = f;
    }
    if (g_planet.empty()) {
        g_planet_world = 0;
        g_planet_file.clear();
    }
    // BUG-9278: the crossing's host plays the next stage (its party came along): it arrives
    if (is_crossing(g_planet) && g_role == kHostRole && g_hstep == kHosting && !g_arrive_sent) {
        const uint8_t e = era_of_mode(game::mode_id());
        if (e && e != era_of(g_planet) && C.ready && C.ready()) {
            if (!g_arrive_ready_since)
                g_arrive_ready_since = now;
            else if (now - g_arrive_ready_since > 10) { // (its party's games load the next stage too)
                net::Writer w = control(ARRIVE);
                w.u8(e);
                send_control(w);
                g_arrive_sent = true;
                H->log(M, SK_INFO, "galaxy: the party's crossing arrives in %s (BUG-9278)", era_name(e));
            }
        } else
            g_arrive_ready_since = 0;
    }
    if (!g_pending_enter.empty() && g_welcomed && !g_session_seen && (!C.sess || C.sess->role() == net::Role::None)) {
        const std::string to = g_pending_enter;
        g_pending_enter.clear();
        say(enter(to));
    }
    if (!g_resume.planet.empty() && g_resume.enter_at > 0 && now >= g_resume.enter_at && g_welcomed) { // BUG-9272
        g_resume.enter_at = 0;
        net::Writer w = control(ENTER);
        w.str8(g_resume.planet);
        send_control(w);
        g_resumes++;
        say("Going back on " + era_display(g_resume.planet) + "...");
    }
    // the planet's session ended (left, removed, its host gone): off the planet, back to the plain network
    if (g_relay_on && C.sess) {
        if (C.sess->role() != net::Role::None)
            g_session_seen = true;
        else if (g_resume_hold) { // BUG-9272: the session left when the server forgot this game ended: not a leave
            g_resume_hold = false;
            g_session_seen = false;
        } else if (g_session_seen && g_hstep != kLoading && g_hstep != kFetching && !g_waiting_host &&
                 g_pending_join.empty() && g_pending_enter.empty()) {
            send_control(control(LEAVE));
            g_planet.clear();
            g_role = 0;
            reset_planet_steps();
            use_relay(false);
            g_session_seen = false; // (BUG-9278: done; a later planet's session starts unseen)
        } else if (!g_pending_enter.empty() && g_session_seen) {
            g_session_seen = false; // BUG-9278 (kMovedJoin): the crossing's session ended: no LEAVE, ENTER comes next
            use_relay(false);
        }
    }
}

bool online() { return g_sock.is_open() && g_welcomed; }

StageEnd stage_end_request(std::string& why)
{
    if (!g_cross_fix || !online() || g_planet.empty() || !era_of(g_planet) || is_crossing(g_planet))
        return kStageEndPlain;
    if (!parties::i_lead()) {
        why = "Only your party's leader moves your party on to the next stage.";
        return kStageEndRefuse;
    }
    const double now = H->time_seconds();
    if (now - g_moveon_at > 5) {
        send_control(control(MOVEON));
        g_moveon_at = now;
        g_moveons_sent++;
    }
    why = "Your party moves on to the next stage: its own way there first...";
    return kStageEndHold;
}
bool in_crossing() { return is_crossing(g_planet); }
bool crossing_ready()
{
    if (!is_crossing(g_planet) || g_role != kHostRole || g_hstep != kHosting || !C.sess)
        return false;
    const size_t want = std::max<size_t>(1, parties::members().size());
    return C.sess->players().size() >= want || H->time_seconds() - g_moved_at > 60;
}
uint32_t planets_version() { return g_planets_ver; }
std::string current_planet() { return g_planet; }
unsigned planet_players() { return g_planet_players; }
bool welcomed() { return g_sock.is_open() && g_welcomed; }
int64_t server_time() { return g_clock_known ? (int64_t)std::time(nullptr) + g_clock_offset : 0; }
uint32_t refresh_seconds() { return g_welcomed ? g_refresh_secs : 0; }
uint32_t rotate_seconds() { return g_welcomed ? g_rotate_secs : 0; }
uint16_t my_slot() { return g_slot; }
bool party_send(uint8_t kind, uint64_t other, uint8_t flag, uint32_t stage)
{
    if (!welcomed())
        return false;
    net::Writer w = control(PARTY);
    w.u8(kind);
    w.u64(other);
    w.u8(flag);
    w.u32(stage);
    send_control(w);
    return true;
}
void on_message(uint8_t type, std::function<void(net::Reader&)> fn) { g_handlers[type] = std::move(fn); }
bool send_message(uint8_t type, const net::Writer& body)
{
    if (!welcomed())
        return false;
    net::Writer w = control((Type)type);
    w.data.insert(w.data.end(), body.data.begin(), body.data.end());
    send_control(w);
    return true;
}
bool whisper(uint64_t to, const std::string& text)
{
    if (!welcomed() || !to || text.empty())
        return false;
    net::Writer w = control(WHISPER);
    w.u64(to);
    w.str16(text);
    send_control(w);
    return true;
}
void ask_players(uint16_t from)
{
    if (!welcomed())
        return;
    net::Writer w = control(WHO);
    if (from)
        w.u16(from); // S10: the next page
    send_control(w);
}
void upload_soon(double secs)
{
    if (hosting())
        g_next_upload = std::min(g_next_upload, H->time_seconds() + secs);
}
uint16_t slot_of(const net::Endpoint& peer) { return g_relay_on ? endpoint_slot(peer) : 0; }
bool fan_out(const std::vector<uint16_t>& slots, uint8_t type, const net::Writer& body)
{
    if (!g_relay_on || !g_sock.is_open() || g_role != kHostRole || slots.empty() || slots.size() > kMaxFanSlots)
        return false;
    std::vector<uint8_t> f;
    f.reserve(3 + 2 * slots.size() + body.data.size());
    f.push_back(kFan);
    f.push_back((uint8_t)slots.size());
    for (uint16_t s : slots) {
        if (!s)
            return false;
        f.push_back((uint8_t)(s & 0xFF));
        f.push_back((uint8_t)(s >> 8));
    }
    f.push_back(type);
    f.insert(f.end(), body.data.begin(), body.data.end());
    if (f.size() > 1400 - gc::kOverhead)
        return false;
    if (!raw_send(f.data(), f.size()))
        return false;
    g_fan_tx++;
    g_fan_tx_bytes += f.size();
    return true;
}
bool hosting() { return g_hstep == kHosting && g_role == kHostRole && C.sess && C.sess->role() == net::Role::Host; }
bool relayed() { return g_relay_on; }

uint64_t warm_join_token()
{
    if (!g_token || !g_token_world || modes::current_world() != g_token_world || !g_relay_on)
        return 0;
    g_warm_joins++;
    return g_token;
}

bool accept_warm(uint64_t receipt)
{
    const bool ok = receipt && receipt == g_token && g_token_world && modes::current_world() == g_token_world &&
                    hosting();
    if (ok) {
        g_warm_accepted++;
        return true;
    }
    // 6I A: a guest that loaded the stored world: it is this host's last upload, nothing saved since, not too old
    if (!receipt || !hosting() || !g_last_crc)
        return false;
    if (receipt != net::galaxy::stored_receipt(g_last_crc, g_last_world))
        return false;
    const double now = H->time_seconds();
    const bool same = g_last_world == modes::current_world() && world::current_save_write() == g_last_write &&
                      now - g_last_packed < kStoredJoinMaxAge;
    if (same) {
        g_stored_accepted++;
        return true;
    }
    g_stored_refused++; // it gets the world from the host as before; the galaxy's copy is brought up to date soon
    g_next_upload = std::min(g_next_upload, now + 15);
    return false;
}

bool close_request()
{
    if (!hosting() || g_close_after)
        return g_close_after; // (a second Alt+F4 while saving is held too)
    g_close_after = true;
    g_close_asked = H->time_seconds();
    g_close_holds++;
    g_leave_after = true;
    say("Saving " + era_display(g_planet) + " to the galaxy before closing...");
    return true;
}
std::string galaxy_name() { return g_welcomed ? g_galaxy : ""; }
std::vector<PlanetInfo> planets_known()
{
    std::vector<PlanetInfo> v = g_planets;
    std::stable_sort(v.begin(), v.end(), [](const PlanetInfo& a, const PlanetInfo& b) { return a.players > b.players; });
    return v;
}

std::string status()
{
    char b[1200];
    snprintf(b, sizeof b,
             "galaxy: %s server=%s galaxy='%s' slot=%u planet='%s' role=%s host_slot=%u relay=%d planets=%zu (lists of "
             "several pages %u) relay rx=%llu (%llu KB) tx=%llu (%llu KB) queue=%zu dropped=%llu",
             g_welcomed ? "connected" : g_sock.is_open() ? "connecting" : "off", g_server.masked().c_str(), g_galaxy.c_str(),
             g_slot, g_planet.c_str(), g_role == kHostRole ? "host" : g_role == kGuestRole ? "guest" : "-", g_host_slot,
             (int)g_relay_on, g_planets.size(), g_list_paged, (unsigned long long)g_rx, (unsigned long long)(g_rx_bytes >> 10),
             (unsigned long long)g_tx, (unsigned long long)(g_tx_bytes >> 10), g_queue.size(),
             (unsigned long long)g_queue_dropped);
    std::string s = b;
    snprintf(b, sizeof b, " copies (6F B): sent %llu (%llu KB), got %llu (%llu KB), refused %llu; reconnects %u; "
             "stored joins (6I A): loaded %u, accepted %u, refused %u",
             (unsigned long long)g_fan_tx, (unsigned long long)(g_fan_tx_bytes >> 10), (unsigned long long)g_fan_rx,
             (unsigned long long)(g_fan_rx_bytes >> 10), (unsigned long long)g_fan_refused, g_reconnects,
             g_stored_joins, g_stored_accepted, g_stored_refused);
    s += b;
    snprintf(b, sizeof b, "\nencryption (8Q A): %s; exchanges %u, refused keys %u, in clear after no answer %u, sealed sent "
             "%llu opened %llu refused %llu replays %llu, clear dropped %u", encryption_word().c_str(), g_kx_exchanges,
             g_kx_mismatch, g_kx_fallbacks, (unsigned long long)g_chan.sealed, (unsigned long long)g_chan.opened,
             (unsigned long long)g_chan.refused, (unsigned long long)g_chan.replays, g_kx_clear_dropped);
    s += b;
    static const char* steps[] = {"-", "downloading", "loading", "hosting"};
    snprintf(b, sizeof b,
             "\nworld: %s%s stored=%u KB down=%u/%u chunks upload=%s (%u/%u chunks) next upload in %.0f s, uploads %u, "
             "downloads %u, hand-overs %u (warm: hosted %u, rejoined %u, accepted %u), silent saves %u (idle %u), quiet waits %u, menu hand-overs %u (own reloads kept %u), resumes %u (hosts %u), close holds %u, last upload: %s"
             "\nstage starts (BUG-9336, %s): sent %u, ends asked %u, now %s%s",
             steps[g_hstep], g_waiting_host ? " (waiting for the new host)" : "", g_world_bytes >> 10, g_down.got, (unsigned)g_down.have.size(),
             g_upstep == kUpIdle ? "idle" : g_upstep == kUpSaving ? "saving" : "sending", g_up.acked,
             g_up.on ? g_up.chunks() : 0u, g_hstep == kHosting ? g_next_upload - H->time_seconds() : 0.0,
             g_uploads_done, g_downloads_done, g_handovers, g_warm_hosts, g_warm_joins, g_warm_accepted, g_silent_saves, g_idle_saves, g_quiet_waits, g_menu_handovers, g_reloads_kept, g_resumes, g_resumed_hosts, g_close_holds,
             g_last_upload.empty() ? "-" : g_last_upload.c_str(), g_start_fix ? "on" : "off", g_starts_sent, g_start_ends,
             g_start_era ? era_name(g_start_era) : "-", g_start_era ? (g_start_asked ? " (its end asked)" : " (on its way)") : "");
    return s + b + "\n" + new_planet_status();
}

void set_upload_every(double s) { g_upload_every = s < 10 ? 10 : s; }

} // namespace galaxy
