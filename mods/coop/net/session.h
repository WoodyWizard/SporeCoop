// Coop session: host (server for up to 3 guests) or client. Game-agnostic.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "connection.h"
#include "protocol.h"
#include "transport.h"

namespace net {

enum class Role { None, Host, Client };

// BUG-0204: how Session starts the thread that looks a host name up. The default is a detached
// std::thread; coop.dll sets one that keeps the DLL loaded until the thread is done (hot reload).
using SpawnFn = bool (*)(void (*fn)(void*), void* arg);
extern SpawnFn g_spawn_thread;

struct Peer {
    uint8_t id = 0xFF; // player id (host side: guest id; client side: 0 = host)
    std::string name;
    bool accepted = false;
    bool bye = false; // said goodbye: dropped in update()
    uint32_t kick_at = 0; // host (BUG-9251): removed by the host, dropped at this time (ms) once the refusal is out
    uint32_t caps = 0;      // its HELLO / WELCOME caps (BUG-0219); 0 = an older SporeCoop
    std::string version;    // CAP_VERSION text ("" = unknown: an older SporeCoop)
    bool has_token = false; // host: its HELLO carried a rejoin token (BUG-0235)
    uint64_t token = 0;
    uint64_t resume_world = 0;
    std::string hello_name; // host: the name of its HELLO, cleaned, before " 2" / "guest<id>" (BUG-1001)
    Connection conn;
};

class Session {
public:
    struct Callbacks {
        std::function<void(const std::string&)> log;
        std::function<void()> connected;                       // client: welcome received
        std::function<void(const std::string&)> disconnected;  // client: lost / rejected / host left
        std::function<void(const PlayerInfo&)> player_join;
        std::function<void(uint8_t id, uint8_t reason)> player_leave;
        std::function<void(const PlayerState&)> player_state;
        std::function<void(uint8_t id, const std::string& text)> chat;
        std::function<void(uint8_t type, uint8_t from, Reader& payload)> custom; // other msgs
        // a player (host: a guest; guest: the host, id 0) runs an older SporeCoop without the HELLO
        // extension: long chat lines reach them cut, messages above one packet not at all (BUG-0219)
        std::function<void(uint8_t id, const std::string& name)> old_peer;
        // decision 6H A (host, through a galaxy): one unreliable message the galaxy server copies to these players
        // (false: not possible now; the session sends one to each). Unset: one to each, as before.
        std::function<bool(const std::vector<uint8_t>& ids, uint8_t type, const Writer& w)> fan_out;
    } cb;

    uint32_t build = 0;      // exe build id; mismatch = reject
    uint8_t seat_limit = kMaxPlayers; // host admission and discovery; includes the solo host
    bool personal_launch = false; // the game handles retained Civilization checkpoints
    std::string campaign_profile; // empty for stock-protocol clients and test peers
    std::string version;     // ours for HELLO / WELCOME (CAP_VERSION): "coop X, SporeKit Y, <game build> <exe hash>"
    // BUG-0141: names are unique in a session (the host appends " 2", " 3" to a taken name; the
    // guest takes its name from the roster). Space tables keyed by name mixed up two players with
    // the same (default) name. false = the old behaviour (tests: coop.corefix unique_names 0)
    bool unique_names = true;
    // a new player with the name of an entry silent for 2 s replaces it (a rejoin after a crash)
    // instead of getting "name 2". false = rename (tests: coop.fix core.rejoin_name off, BUG-0209)
    bool rejoin_by_name = true;
    bool paced_world_transfer = false; // host: opt-in for constrained UDP links
    // BUG-0235: the rejoin token this game sends in its HELLO (0 = none). The same game after a crash
    // sends the same token (coop.dll keeps it in a file until a clean leave); the host replaces a
    // silent entry only on an equal token (net::g_fix.rejoin_token), never by the name alone
    uint64_t rejoin_token = 0;
    uint64_t resume_world = 0;
    bool send_caps = true; // not a lab switch: test seam, false = HELLO / WELCOME without the extension (0.7.2 shape)
    // host: its world id (save CRC in Cell / Creature / Tribe / Civ, else 0), sent in WELCOME; coop.cpp
    // sets it every frame (host_world.h). Guest: welcome_world() is what the host sent (0 = unknown
    // or an older host), until leave(). The field stays where it is in Welcome (BUG-0231)
    uint32_t world = 0;
    uint32_t welcome_world() const { return welcome_world_; }
    uint32_t timeout_ms = 30000; // the game can block for a long time while saving/loading
    std::string lobby_info;  // host: shown to players looking for games on the network
    int lobby_scaling = -1;
    // F3 (BUG-0768), host: the players the session takes, the host counted (all.max_players, 2..kMaxPlayers), and
    // whether a new player is refused now (all.late_join off while the host plays its world). A player who was in
    // this session already (its name, or its rejoin token) may always come back. coop.cpp sets both every frame
    int max_players = kMaxPlayers;
    std::function<bool()> join_closed;
    // BUG-0776, host: the broadcast addresses of this machine's adapters (network byte order, coop.cpp from
    // adapters::list()): a discovery query from one of their subnets is answered too (a virtual LAN outside the
    // private ranges)
    std::vector<uint32_t> lan_broadcasts;
    static constexpr uint32_t kDiscoverPerSecond = 20; // BUG-0776: discovery answers a second, all senders together
    static constexpr size_t kMaxPeers = 2 * kMaxPlayers; // host: accepted and half-open peers (rejoins overlap)
    static constexpr int kHalfOpenPerIp = 4;           // BUG-0778: half-open peers of one address (a NAT: several games)
    // BUG-1001: a player without a rejoin token comes back after the start by its name only within this time of its drop
    static constexpr uint32_t kRejoinNameMs = 10 * 60 * 1000;
    // BUG-1007: what Transport::recv_from returns for one datagram it had to drop (too big for the buffer): more may follow
    static constexpr int kRecvSkipped = -2;

    bool host(uint16_t port, const std::string& name, std::string& err, std::string password = {});
    bool join(const std::string& addr, const std::string& name, std::string& err, std::string password = {});
    void leave(const char* why = "left");
    void update(); // call every frame: receive, timeouts, send

    void send_state(PlayerState st);              // own state
    // decision 6H A (guest): a message of the host copied by the galaxy server (MSG_PLAYER_STATE: as from the host)
    void host_fan_in(uint8_t type, Reader& r);
    uint64_t fan_states_sent() const { return fan_states_sent_; }
    uint64_t fan_states_single() const { return fan_states_single_; }
    void send_chat(const std::string& text);
    void broadcast_reliable(uint8_t type, const Writer& w, int except_id = -1);
    bool send_reliable_to(uint8_t id, uint8_t type, const Writer& w); // host: guest id, client: 0
    // host (BUG-9251, decision 5I): the guest is told `text` (MSG_REJECT: its game leaves with it) and dropped 1 s later
    bool kick(uint8_t id, const std::string& text);
    // player event (reliable): client -> host, host relays to the other guests
    void send_event(uint8_t type, const Writer& w);
    size_t pending_to(uint8_t id) const;
    bool send_unreliable_to(uint8_t id, uint8_t type, const Writer& w);
    // the connection to a player (host: a guest, guest: the host, id 0) for the support statistics
    // (diag_stats.cpp), nullptr when there is none; valid until the next update()
    Connection* conn_to(uint8_t id);

    Role role() const { return role_; }
    uint8_t my_id() const { return my_id_; }
    bool connected() const { return role_ == Role::Host || (role_ == Role::Client && welcomed_); }
    // BUG-9222: the names as players read them ("MSI-NoShare-" is "MSI"); name_raw_of / players_raw: as typed (the
    // tag marks a test player for the Lineages Cache; the wire always carries the raw name)
    std::string name_of(uint8_t id) const;
    std::string name_raw_of(uint8_t id) const;
    std::vector<PlayerInfo> players() const; // including self
    std::vector<PlayerInfo> players_raw() const;
    std::string status() const;
    uint16_t port() const { return tr_->local_port(); }
    // round trip time to a player in ms (host: any guest, guest: the host), -1 if unknown
    int rtt_of(uint8_t id) const;
    uint32_t peer_caps(uint8_t id) const;
    Endpoint peer_endpoint(uint8_t id) const; // an accepted peer's address ({} none)
    uint64_t peer_resume_world(uint8_t id) const;
    // BUG-0651: the q-quantile of the last round-trip samples to that peer (ms), -1 = none (yet)
    float rtt_quantile_of(uint8_t id, float q) const;
    // the link to a player (host: any guest, guest: the host) for the support trace: no address
    struct Link {
        int rtt_ms = -1;
        uint32_t sent = 0, resends = 0, silent_ms = 0, pending = 0;
    };
    bool link_of(uint8_t id, Link& out) const;
    std::string host_address() const; // guest: "ip:port" of the host
    const std::string& password() const { return password_; } // the session's password (BUG-9239: a restart rejoins)
    bool resolving() const { return lookup_ != nullptr; } // guest: the host name is being looked up
    struct Lookup; // a name lookup on a worker thread (BUG-0204)
    static constexpr uint32_t kLookupTimeoutMs = 20000;
    size_t peer_count() const { return peers_.size(); } // host: accepted and half-open peers

    // test seam (tests/fuzz_decoders.cpp): another network and clock; nullptr = UDP and now_ms()
    void set_transport(Transport* t) { tr_ = t ? t : &udp_; }
    uint32_t (*clock)() = nullptr;

private:
    uint64_t fan_states_sent_ = 0, fan_states_single_ = 0; // 6H A: states sent once for several / to one
    // 6H A: send `w` to these accepted peers: the CAP_FAN_IN ones through cb.fan_out when two or more, the rest one each
    void send_unreliable_fanned(const std::vector<Peer*>& to, uint8_t type, const Writer& w, bool latest);
    void client_player_state(Reader& r);
    std::string password_;
    void handle_packet(Peer& p, const uint8_t* data, size_t n, uint32_t now);
    void handle_msg(Peer& p, uint8_t type, Reader& r);
    void flush(uint32_t now);
    Peer* find_peer(const Endpoint& e);
    void drop_peer(size_t idx, uint8_t reason);
    void send_chat_to(Peer& p, const Chat& c);
    uint8_t alloc_id() const;
    std::vector<std::string> seen_names_; // host: every name admitted since host() (the late-join rule before BUG-1001)
    // BUG-1001, host: the rejoin tokens admitted since host(), and when a name (Peer::hello_name) was last dropped
    std::vector<uint64_t> seen_tokens_;
    std::vector<std::pair<std::string, uint32_t>> dropped_names_;
    bool rejoin_known(const Peer& p, const Hello& h, bool by_token) const; // BUG-1001: may come back after the start
    bool retired(uint32_t conn) const;
    void start_hello(const Endpoint& ep, uint32_t now);
    void restart_leave();                  // BUG-0861: host() / join(): the old session ends now, also in a delivery
    void end_session(const char* why);     // leave() without the deferral
    static bool carries_hello(const uint8_t* data, size_t n, Hello* out = nullptr); // BUG-0778 (out: BUG-1003)
    void make_room(const Endpoint& from);  // BUG-0778
    bool discovery_answer(const Endpoint& from, uint32_t now); // BUG-0776
    bool poll_lookup(uint32_t now); // false = the session ended or still waits

    std::shared_ptr<Lookup> lookup_;
    uint16_t lookup_port_ = 0;
    uint32_t lookup_started_ = 0;

    Role role_ = Role::None;
    uint32_t now() const { return clock ? clock() : now_ms(); }
    UdpTransport udp_;
    Transport* tr_ = &udp_;
    std::string my_name_;
    uint32_t lan_id_ = 0; // BUG-0210: DiscoverReply::host_id of the game hosted now
    uint8_t my_id_ = 0;
    bool welcomed_ = false;
    uint32_t welcome_world_ = 0; // guest: Welcome::world of the host
    uint32_t join_started_ = 0;
    uint32_t last_poll_ms_ = 0; // BUG-0651: the previous update's socket read
    std::vector<std::unique_ptr<Peer>> peers_;
    std::vector<PlayerInfo> roster_; // client: known players (incl. host)
    std::vector<uint32_t> retired_;  // host: conn ids of dropped peers (their late packets are ignored)
    // BUG-0029: a leave() asked for while a Connection is parsing a packet (REJECT, BYE, a callback)
    // waits until on_packet has returned: leave() frees the peers and with them that Connection
    bool delivering_ = false;
    bool leave_pending_ = false;
    std::string pending_why_;  // leave() reason
    std::string pending_disc_; // cb.disconnected text after the deferred leave ("" = none)
    // BUG-0861: changes with every host(), join() and end of a session; a delivery or an update that sees it change
    // stops working for the old session. zombies_: the old session's peers while their packet is still parsed
    uint32_t gen_ = 0;
    std::vector<std::unique_ptr<Peer>> zombies_;
    uint32_t disc_window_ms_ = 0, disc_in_window_ = 0, disc_answered_ = 0, disc_refused_ = 0; // BUG-0776
    uint32_t halfopen_refused_ = 0, halfopen_evicted_ = 0;                                      // BUG-0778
    uint32_t replace_refused_ = 0;                                                              // BUG-1003
    uint32_t recv_skipped_ = 0;                                                                 // BUG-1007
};

} // namespace net
