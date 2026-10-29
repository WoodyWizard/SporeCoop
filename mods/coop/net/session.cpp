#include "session.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <thread>

#include "fixes.h"

namespace net {

static void logf(const Session::Callbacks& cb, const char* fmt, ...)
{
    if (!cb.log)
        return;
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    cb.log(b);
}

// a connection id that differs from the previous ones of this process (and of other processes)
static uint32_t new_conn_id(uint32_t now, uint32_t salt)
{
    static uint64_t counter = 0;
    uint64_t x = ((uint64_t)now << 32) ^ salt ^ (++counter * 0x9E3779B97F4A7C15ull) ^ (uint64_t)(uintptr_t)&counter;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (uint32_t)x ? (uint32_t)x : 1u;
}

bool Session::host(uint16_t port, const std::string& name, std::string& err)
{
    leave("restart");
    if (!tr_->open(port, &err))
        return false;
    role_ = Role::Host;
    my_id_ = 0;
    my_name_ = name;
    welcomed_ = true;
    logf(cb, "hosting on UDP port %u", tr_->local_port());
    return true;
}

struct Session::Lookup {
    std::string host;
    std::atomic<bool> done{false};
    bool ok = false;
    uint32_t ip = 0;
    std::string err;
};

static bool spawn_std_thread(void (*fn)(void*), void* arg)
{
    try {
        std::thread(fn, arg).detach();
        return true;
    } catch (...) {
        return false;
    }
}
SpawnFn g_spawn_thread = spawn_std_thread;

// the worker: owns one reference to the lookup; the session may have dropped its own already
static void lookup_main(void* arg)
{
    auto* ref = static_cast<std::shared_ptr<Session::Lookup>*>(arg);
    Session::Lookup& l = **ref;
    l.ok = Endpoint::resolve_name(l.host, l.ip, l.err);
    l.done.store(true, std::memory_order_release);
    delete ref;
}

bool Session::join(const std::string& addr, const std::string& name, std::string& err)
{
    leave("restart");
    Endpoint ep;
    std::string host_name;
    if (g_fix.async_resolve) {
        // BUG-0204: a numeric address is parsed here; a name is looked up on a worker thread and
        // the join goes on in update(), so a slow name server does not stop the game's frames
        if (!Endpoint::parse_numeric(addr, kDefaultPort, ep, err, &host_name) && host_name.empty())
            return false;
    } else if (!Endpoint::parse(addr, kDefaultPort, ep, err)) // the old way: blocks in the click
        return false;
    if (!tr_->open(0, &err))
        return false;
    role_ = Role::Client;
    my_name_ = name;
    welcomed_ = false;
    join_started_ = now();
    if (!host_name.empty()) {
        auto l = std::make_shared<Lookup>();
        l->host = host_name;
        auto* ref = new std::shared_ptr<Lookup>(l);
        if (!g_spawn_thread(lookup_main, ref)) {
            delete ref;
            leave("lookup thread");
            err = "cannot start the name lookup";
            return false;
        }
        lookup_ = l;
        lookup_port_ = ep.port;
        lookup_started_ = join_started_;
        logf(cb, "looking up a host name (%u characters) ...", (unsigned)host_name.size());
        return true;
    }
    start_hello(ep, join_started_);
    return true;
}

void Session::start_hello(const Endpoint& ep, uint32_t now)
{
    join_started_ = now;
    auto p = std::make_unique<Peer>();
    p->id = 0;
    p->conn.remote = ep;
    p->conn.conn_id = new_conn_id(join_started_, tr_->local_port());
    p->conn.created_ms = p->conn.last_recv_ms = join_started_;
    Hello h;
    h.build = build;
    h.name = my_name_;
    h.ext = send_caps;
    h.version = version;
    h.has_token = rejoin_token != 0;
    h.token = rejoin_token;
    Writer w;
    h.write(w);
    p->conn.send_reliable(MSG_HELLO, w);
    peers_.push_back(std::move(p));
    logf(cb, "joining %s ...", ep.masked().c_str());
}

bool Session::poll_lookup(uint32_t now)
{
    if (!lookup_->done.load(std::memory_order_acquire)) {
        if (now - lookup_started_ > kLookupTimeoutMs) {
            std::string host = lookup_->host;
            leave("lookup timeout");
            if (cb.disconnected)
                cb.disconnected("cannot resolve " + host + " (the name server does not answer)");
        }
        return false;
    }
    std::shared_ptr<Lookup> l = std::move(lookup_);
    if (!l->ok) {
        leave("lookup failed");
        if (cb.disconnected)
            cb.disconnected(l->err);
        return false;
    }
    Endpoint ep;
    ep.ip = l->ip;
    ep.port = lookup_port_;
    start_hello(ep, now);
    return true;
}

void Session::leave(const char* why)
{
    if (role_ == Role::None)
        return;
    if (delivering_) {
        // inside Connection::on_packet: freeing the peers now frees the Connection that is still
        // running (BUG-0029); update() leaves as soon as the packet is done
        if (!leave_pending_)
            pending_why_ = why ? why : "left";
        leave_pending_ = true;
        return;
    }
    leave_pending_ = false;
    lookup_.reset(); // a lookup still running ends on its own (the worker holds its own reference)
    uint32_t now = this->now();
    for (auto& p : peers_) {
        // unreliable, in three packets: a reliable BYE waited behind any earlier lost message (and
        // was sent once), so over a lossy link the other side often never saw it
        Writer w;
        w.u8(0);
        std::vector<uint8_t> pkt;
        for (int i = 0; i < 3; i++) {
            p->conn.send_unreliable(MSG_BYE, w, true);
            if (p->conn.build_packet(now + 1000, pkt))
                tr_->send_to(p->conn.remote, pkt.data(), pkt.size());
        }
    }
    peers_.clear();
    roster_.clear();
    retired_.clear();
    tr_->close();
    role_ = Role::None;
    welcomed_ = false;
    logf(cb, "session closed (%s)", why);
}

Peer* Session::find_peer(const Endpoint& e)
{
    for (auto& p : peers_)
        if (p->conn.remote == e)
            return p.get();
    return nullptr;
}

bool Session::retired(uint32_t conn) const
{
    return std::find(retired_.begin(), retired_.end(), conn) != retired_.end();
}

uint8_t Session::alloc_id() const
{
    for (uint8_t id = 1; id < kMaxPlayers; id++) {
        bool used = false;
        for (auto& p : peers_)
            if (p->accepted && p->id == id)
                used = true;
        if (!used)
            return id;
    }
    return 0xFF;
}

std::string Session::name_of(uint8_t id) const
{
    if (id == my_id_ && role_ != Role::None)
        return my_name_;
    if (role_ == Role::Host) {
        for (auto& p : peers_)
            if (p->accepted && p->id == id)
                return p->name;
    } else {
        for (auto& r : roster_)
            if (r.id == id)
                return r.name;
    }
    return "player" + std::to_string(id);
}

std::vector<PlayerInfo> Session::players() const
{
    std::vector<PlayerInfo> v;
    if (role_ == Role::Host) {
        v.push_back({0, my_name_, version});
        for (auto& p : peers_)
            if (p->accepted)
                v.push_back({p->id, p->name, p->version});
    } else if (role_ == Role::Client) {
        v = roster_;
        bool self = false;
        for (auto& r : v)
            if (r.id == my_id_)
                self = true;
        if (!self && welcomed_)
            v.push_back({my_id_, my_name_, version});
    }
    return v;
}

int Session::rtt_of(uint8_t id) const
{
    for (auto& p : peers_)
        if (p->id == id && (p->accepted || role_ == Role::Client))
            return (int)p->conn.rtt_ms;
    return -1;
}

bool Session::link_of(uint8_t id, Link& out) const
{
    for (auto& p : peers_)
        if (p->id == id && (p->accepted || role_ == Role::Client)) {
            out.rtt_ms = (int)p->conn.rtt_ms;
            out.sent = p->conn.stats.packets_sent;
            out.resends = p->conn.stats.resends;
            out.silent_ms = now() - p->conn.last_recv_ms;
            out.pending = (uint32_t)p->conn.pending_reliable();
            return true;
        }
    return false;
}

std::string Session::host_address() const
{
    return role_ == Role::Client && !peers_.empty() ? peers_[0]->conn.remote.str() : std::string();
}

std::string Session::status() const
{
    char b[256];
    std::string s;
    if (role_ == Role::None)
        return "offline\n";
    if (role_ == Role::Host)
        snprintf(b, sizeof b, "host port=%u players=%d/%d\n", tr_->local_port(),
                 (int)players().size(), kMaxPlayers);
    else
        snprintf(b, sizeof b, "client id=%u %s\n", my_id_, welcomed_ ? "connected" : "connecting");
    s += b;
    uint32_t now = this->now();
    for (auto& p : peers_) {
        snprintf(b, sizeof b, "  [%u] %-16s %s rtt=%.0fms last=%ums sent=%u recv=%u acked=%u resend=%u pending=%u\n",
                 p->id, p->name.c_str(), p->conn.remote.masked().c_str(), p->conn.rtt_ms,
                 now - p->conn.last_recv_ms, p->conn.stats.packets_sent, p->conn.stats.packets_recv,
                 p->conn.stats.acked, p->conn.stats.resends, (unsigned)p->conn.pending_reliable());
        s += b;
    }
    // BUG-0219: a line of its own after the session block, never inside a peer line
    for (auto& p : peers_)
        if ((p->accepted || role_ == Role::Client) && !p->conn.peer_split) {
            snprintf(b, sizeof b, "older peer %u: too_big_old=%u frag_lost=%u\n", p->id,
                     p->conn.stats.too_big_old, p->conn.stats.frag_lost);
            s += b;
        }
    return s;
}

void Session::broadcast_reliable(uint8_t type, const Writer& w, int except_id)
{
    for (auto& p : peers_)
        if (p->accepted && (int)p->id != except_id)
            p->conn.send_reliable(type, w);
}

bool Session::send_reliable_to(uint8_t id, uint8_t type, const Writer& w)
{
    for (auto& p : peers_)
        if ((p->accepted || role_ == Role::Client) && p->id == id) {
            p->conn.send_reliable(type, w);
            return true;
        }
    return false;
}

void Session::send_event(uint8_t type, const Writer& w)
{
    for (auto& p : peers_)
        if (p->accepted || role_ == Role::Client)
            p->conn.send_reliable(type, w);
}

bool Session::send_unreliable_to(uint8_t id, uint8_t type, const Writer& w)
{
    for (auto& p : peers_)
        if ((p->accepted || role_ == Role::Client) && p->id == id) {
            p->conn.send_unreliable(type, w);
            return true;
        }
    return false;
}

size_t Session::pending_to(uint8_t id) const
{
    for (auto& p : peers_)
        if (p->id == id)
            return p->conn.pending_reliable();
    return 0;
}

Connection* Session::conn_to(uint8_t id)
{
    for (auto& p : peers_)
        if (p->id == id && (p->accepted || role_ == Role::Client))
            return &p->conn;
    return nullptr;
}

void Session::send_state(PlayerState st)
{
    if (!connected())
        return;
    st.id = my_id_;
    st.time_ms = now();
    Writer w;
    st.write(w);
    for (auto& p : peers_)
        if (p->accepted || role_ == Role::Client)
            p->conn.send_unreliable(MSG_PLAYER_STATE, w, true);
}

// BUG-0219: a peer that cannot join pieces gets a chat line that fits one packet (cut between two
// characters) instead of pieces it would read as broken messages, or nothing
void Session::send_chat_to(Peer& p, const Chat& c)
{
    Writer w;
    c.write(w);
    if (w.size() > Connection::kMaxSingle && g_fix.big_reliable && g_fix.peer_caps && !p.conn.peer_split) {
        Chat cut = c;
        cut.text.resize(utf8::cut_bytes(c.text, Connection::kMaxSingle - 3)); // u8 id + u16 length
        Writer wc;
        cut.write(wc);
        p.conn.send_reliable(MSG_CHAT, wc);
        return;
    }
    p.conn.send_reliable(MSG_CHAT, w);
}

void Session::send_chat(const std::string& text)
{
    if (!connected())
        return;
    Chat c;
    c.id = my_id_;
    c.text = text;
    for (auto& p : peers_)
        if (p->accepted || role_ == Role::Client)
            send_chat_to(*p, c);
    if (cb.chat)
        cb.chat(my_id_, text);
}

void Session::drop_peer(size_t idx, uint8_t reason)
{
    auto& p = peers_[idx];
    bool was = p->accepted;
    uint8_t id = p->id;
    logf(cb, "player %u (%s) disconnected (reason %u)", id, p->name.c_str(), reason);
    retired_.push_back(p->conn.conn_id);
    if (retired_.size() > 64)
        retired_.erase(retired_.begin());
    peers_.erase(peers_.begin() + idx);
    if (was) {
        PlayerLeave pl{id, reason};
        Writer w;
        pl.write(w);
        broadcast_reliable(MSG_PLAYER_LEAVE, w);
        if (cb.player_leave)
            cb.player_leave(id, reason);
    }
}

void Session::handle_msg(Peer& p, uint8_t type, Reader& r)
{
    if (role_ == Role::Host) {
        if (type == MSG_HELLO) {
            if (p.accepted)
                return;
            Hello h;
            if (!h.read(r))
                return;
            Reject rej;
            // BUG-0235: the same game after a crash (its HELLO carries the token of the old entry,
            // which has been silent for 2 s) replaces that entry now, before the full check (a crash
            // rejoin into 4/4 was refused) and before WELCOME (its roster held its own old entry).
            // A busy player of the same name (loading, saving) keeps its entry: the token differs
            const bool by_token = unique_names && rejoin_by_name && g_fix.rejoin_token;
            if (by_token && h.has_token && h.proto == kProtoVersion && !(build && h.build && h.build != build))
                for (size_t k = 0; k < peers_.size(); k++) {
                    Peer* o = peers_[k].get();
                    if (o != &p && o->accepted && !o->bye && o->has_token && o->token == h.token &&
                        this->now() - o->conn.last_recv_ms > 2000) {
                        logf(cb, "player %u (%s) rejoins: its old entry is dropped", o->id, o->name.c_str());
                        drop_peer(k, 0); // PLAYER_LEAVE(old) goes out before PLAYER_JOIN(new)
                        break;
                    }
                }
            if (h.proto != kProtoVersion) {
                rej.reason = REJ_VERSION;
                rej.text = "protocol version mismatch";
            } else if (build && h.build && h.build != build) {
                rej.reason = REJ_BUILD;
                rej.text = "different SporeApp.exe build";
            } else if (alloc_id() == 0xFF) {
                rej.reason = REJ_FULL;
                rej.text = "session is full";
            }
            if (rej.reason) {
                Writer w;
                rej.write(w);
                p.conn.send_reliable(MSG_REJECT, w);
                logf(cb, "rejected %s: %s", p.conn.remote.masked().c_str(), rej.text.c_str());
                return;
            }
            p.id = alloc_id();
            p.name = h.name.empty() ? "guest" + std::to_string(p.id) : h.name;
            if (unique_names) {
                // a player who rejoins after its game died (no BYE) still has its old entry until
                // that times out: an entry of this name silent for 2 s is that player, it goes now
                // and the name stays (BUG-0209)
                if (rejoin_by_name && !by_token) // the 0.7.3.0 rule: name and 2 s of silence
                    for (auto& o : peers_)
                        if (o->accepted && o.get() != &p && !o->bye && o->name == p.name &&
                            this->now() - o->conn.last_recv_ms > 2000) {
                            o->bye = true;
                            logf(cb, "player %u (%s) rejoins: its old entry is dropped", o->id, o->name.c_str());
                        }
                auto taken = [&](const std::string& n) {
                    if (n == my_name_)
                        return true;
                    for (auto& o : peers_)
                        if (o->accepted && o.get() != &p && !o->bye && o->name == n)
                            return true;
                    return false;
                };
                std::string base = p.name;
                for (int k = 2; taken(p.name) && k < 300; k++)
                    p.name = base + " " + std::to_string(k);
            }
            p.accepted = true;
            p.caps = h.caps;
            p.has_token = h.has_token;
            p.token = h.token;
            p.conn.peer_split = (h.caps & CAP_SPLIT) != 0;
            p.version = h.version;
            Welcome wl;
            wl.your_id = p.id;
            wl.world = world;
            wl.players = players();
            wl.ext = send_caps;
            wl.host_version = version;
            Writer w;
            wl.write(w);
            p.conn.send_reliable(MSG_WELCOME, w);
            PlayerJoin pj;
            pj.p = {p.id, p.name, p.version};
            Writer wj;
            pj.write(wj);
            broadcast_reliable(MSG_PLAYER_JOIN, wj, p.id);
            logf(cb, "player %u '%s' joined from %s: %s", p.id, p.name.c_str(), p.conn.remote.masked().c_str(),
                 p.version.empty() ? "version unknown (pre-0.7.4)" : p.version.c_str());
            if (cb.player_join)
                cb.player_join(pj.p);
            if (!h.caps) {
                logf(cb, "player %u '%s' uses an older SporeCoop (no HELLO caps)", p.id, p.name.c_str());
                if (cb.old_peer)
                    cb.old_peer(p.id, p.name);
            }
            return;
        }
        if (!p.accepted)
            return;
        switch (type) {
        case MSG_PLAYER_STATE: {
            PlayerState st;
            if (!st.read(r))
                return;
            st.id = p.id; // never trust the id from the wire
            if (cb.player_state)
                cb.player_state(st);
            Writer w;
            st.write(w);
            for (auto& o : peers_)
                if (o->accepted && o->id != p.id)
                    o->conn.send_unreliable(MSG_PLAYER_STATE, w);
            break;
        }
        case MSG_CHAT: {
            Chat c;
            if (!c.read(r))
                return;
            c.id = p.id;
            if (cb.chat)
                cb.chat(c.id, c.text);
            for (auto& o : peers_)
                if (o->accepted && o->id != p.id)
                    send_chat_to(*o, c);
            break;
        }
        case MSG_PING: {
            Writer w;
            w.u32(r.u32());
            p.conn.send_unreliable(MSG_PONG, w);
            break;
        }
        case MSG_BYE:
            p.bye = true; // dropped in update()
            break;
        case MSG_ANIM: {
            AnimEvent ev;
            if (!ev.read(r))
                return;
            ev.id = p.id;
            Writer w;
            ev.write(w);
            broadcast_reliable(MSG_ANIM, w, p.id);
            Reader rr(w.data.data(), w.size());
            if (cb.custom)
                cb.custom(type, p.id, rr);
            break;
        }
        default:
            if (cb.custom)
                cb.custom(type, p.id, r);
        }
        return;
    }

    // ---- client ----
    switch (type) {
    case MSG_WELCOME: {
        Welcome wl;
        if (!wl.read(r))
            return;
        my_id_ = wl.your_id;
        welcomed_ = true;
        p.accepted = true;
        p.caps = wl.caps;
        p.conn.peer_split = (wl.caps & CAP_SPLIT) != 0;
        p.version = wl.host_version;
        roster_ = wl.players;
        for (auto& pl : roster_) {
            if (pl.id == 0)
                p.name = pl.name;
            if (pl.id == my_id_ && unique_names && !pl.name.empty())
                my_name_ = pl.name; // the host may have made it unique (BUG-0141)
        }
        logf(cb, "joined as player %u (host '%s', %d players)", my_id_, p.name.c_str(), (int)roster_.size());
        for (auto& pl : roster_)
            if (pl.id != my_id_)
                logf(cb, "player %u '%s': %s", pl.id, pl.name.c_str(),
                     pl.version.empty() ? "version unknown (pre-0.7.4)" : pl.version.c_str());
        if (cb.connected)
            cb.connected();
        for (auto& pl : roster_)
            if (pl.id != my_id_ && cb.player_join)
                cb.player_join(pl);
        if (!wl.caps) {
            logf(cb, "the host '%s' uses an older SporeCoop (no WELCOME caps)", p.name.c_str());
            if (cb.old_peer)
                cb.old_peer(0, p.name);
        }
        break;
    }
    case MSG_REJECT: {
        Reject rej;
        rej.read(r);
        std::string why = "rejected: " + rej.text;
        logf(cb, "%s", why.c_str());
        if (!leave_pending_)
            pending_disc_ = why;
        leave("rejected"); // deferred until on_packet returns (BUG-0029)
        break;
    }
    case MSG_PLAYER_JOIN: {
        PlayerJoin pj;
        if (!pj.read(r))
            return;
        roster_.push_back(pj.p);
        logf(cb, "player %u '%s' joined: %s", pj.p.id, pj.p.name.c_str(),
             pj.p.version.empty() ? "version unknown (pre-0.7.4 or an older host)" : pj.p.version.c_str());
        if (cb.player_join)
            cb.player_join(pj.p);
        break;
    }
    case MSG_PLAYER_LEAVE: {
        PlayerLeave pl;
        if (!pl.read(r))
            return;
        roster_.erase(std::remove_if(roster_.begin(), roster_.end(),
                                     [&](const PlayerInfo& x) { return x.id == pl.id; }),
                      roster_.end());
        if (cb.player_leave)
            cb.player_leave(pl.id, pl.reason);
        break;
    }
    case MSG_PLAYER_STATE: {
        PlayerState st;
        if (!st.read(r) || st.id == my_id_ || !cb.player_state)
            break;
        // BUG-0109: the host relays a leaving guest's last state unreliably, behind the frame's
        // snapshot chunks, so it can arrive after the reliable PLAYER_LEAVE: it made a ghost
        // partner that stayed for the session (and passed its buffer to the next player of the id)
        if (g_fix.state_roster && std::none_of(roster_.begin(), roster_.end(),
                                               [&](const PlayerInfo& x) { return x.id == st.id; }))
            break;
        cb.player_state(st);
        break;
    }
    case MSG_CHAT: {
        Chat c;
        if (c.read(r) && cb.chat)
            cb.chat(c.id, c.text);
        break;
    }
    case MSG_BYE:
        logf(cb, "host closed the session");
        if (!leave_pending_)
            pending_disc_ = "host closed the session";
        leave("host left"); // deferred until on_packet returns (BUG-0029)
        break;
    default:
        if (cb.custom)
            cb.custom(type, 0, r);
    }
}

void Session::handle_packet(Peer& p, const uint8_t* data, size_t n, uint32_t now)
{
    delivering_ = true;
    p.conn.on_packet(data, n, now, [&](uint8_t type, Reader& r) {
        if (role_ != Role::None && !leave_pending_)
            handle_msg(p, type, r);
    });
    delivering_ = false;
    if (leave_pending_) {
        // the connection is done with the packet: now it may go
        std::string why = pending_why_, disc = pending_disc_;
        pending_why_.clear();
        pending_disc_.clear();
        leave(why.c_str());
        if (!disc.empty() && cb.disconnected)
            cb.disconnected(disc);
    }
}

void Session::flush(uint32_t now)
{
    std::vector<uint8_t> pkt;
    for (auto& p : peers_) {
        // the reliable bulk (world transfer) is paced by the ack window: all of it per flush on a short
        // round trip (LAN), a third on the internet; unreliable messages are stale by the next frame,
        // so they all go out (up to 64 more packets): a cell pool snapshot is one ~900-byte chunk per
        // 14 cells and used to push the player state queued after it past the limit
        int burst = p->conn.rtt_ms < 40.f ? (int)kAckWindow : (int)kAckWindow / 3;
        for (int i = 0; (i < burst || (i < burst + 64 && p->conn.unreliable_queued() > 0)) && p->conn.build_packet(now, pkt); i++)
            tr_->send_to(p->conn.remote, pkt.data(), pkt.size());
        p->conn.drop_unreliable();
    }
}

void Session::update()
{
    if (role_ == Role::None)
        return;
    uint32_t now = this->now();
    if (lookup_ && !poll_lookup(now))
        return;
    uint8_t buf[2048];
    for (int i = 0; i < 256; i++) {
        Endpoint from;
        int n = tr_->recv_from(from, buf, sizeof buf);
        if (n <= 0)
            break;
        if (n >= 6 && role_ == Role::Host && *(const uint32_t*)buf == kDiscoverQuery) {
            DiscoverReply rep;
            rep.port = tr_->local_port();
            rep.name = my_name_;
            rep.players = (uint8_t)players().size();
            rep.max_players = kMaxPlayers;
            rep.info = lobby_info;
            Writer w;
            rep.write(w);
            tr_->send_to(from, w.data.data(), w.size());
            continue;
        }
        if (!Connection::looks_valid(buf, n))
            continue;
        Peer* p = find_peer(from);
        uint32_t conn = Connection::peek_conn(buf, n);
        if (role_ == Role::Host && (!p || p->conn.conn_id != conn)) {
            if (retired(conn))
                continue; // a late packet of a connection that is gone
            if (p) {
                // the same address and port with a new connection id: the guest left and joined
                // again (its goodbye got lost) through a NAT / proxy that kept the mapping; the old
                // peer goes (otherwise it swallowed the new HELLO and never timed out)
                for (size_t k = 0; k < peers_.size(); k++)
                    if (peers_[k].get() == p) {
                        logf(cb, "a new connection from %s replaces player %u", from.masked().c_str(), p->id);
                        drop_peer(k, 0);
                        break;
                    }
                p = nullptr;
            }
        }
        if (!p) {
            if (role_ != Role::Host || peers_.size() >= 16)
                continue;
            auto np = std::make_unique<Peer>();
            np->conn.remote = from;
            np->conn.conn_id = conn;
            np->conn.created_ms = now;
            peers_.push_back(std::move(np));
            p = peers_.back().get();
        }
        handle_packet(*p, buf, n, now);
        if (role_ == Role::None)
            return; // left during handling
    }

    // timeouts
    if (role_ == Role::Client) {
        if (!welcomed_ && now - join_started_ > 10000) {
            leave("join timeout");
            if (cb.disconnected)
                cb.disconnected("no answer from host (timeout)");
            return;
        }
        if (welcomed_ && !peers_.empty() && now - peers_[0]->conn.last_recv_ms > timeout_ms) {
            leave("timeout");
            if (cb.disconnected)
                cb.disconnected("connection to host lost (timeout)");
            return;
        }
    } else {
        for (size_t i = 0; i < peers_.size();) {
            auto& p = peers_[i];
            bool dead = p->bye || (p->accepted ? now - p->conn.last_recv_ms > timeout_ms
                                               : now - p->conn.created_ms > 10000);
            if (dead)
                drop_peer(i, p->bye ? 0 : 1);
            else
                i++;
        }
    }
    flush(now);
}

} // namespace net
