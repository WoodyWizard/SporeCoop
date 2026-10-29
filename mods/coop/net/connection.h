// One logical connection over UDP: seq/ack/ackbits, reliable-ordered + unreliable messages.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

#include "buffer.h"
#include "udp.h"

namespace net {

inline bool seq_greater(uint16_t a, uint16_t b)
{
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}

// which of the 128 packets before the latest one arrived: bit k = sequence latest - (k + 1)
struct AckBits {
    uint64_t lo = 0, hi = 0;
    void clear() { lo = hi = 0; }
    void shift(unsigned s) // a newer packet arrived s sequence numbers later
    {
        if (s >= 128) {
            lo = hi = 0;
        } else if (s >= 64) {
            hi = s == 64 ? lo : lo << (s - 64);
            lo = 0;
        } else if (s > 0) {
            hi = (hi << s) | (lo >> (64 - s));
            lo <<= s;
        }
    }
    void set(unsigned k) { (k < 64 ? lo : hi) |= 1ull << (k & 63); }
    bool test(unsigned k) const { return k < 128 && (((k < 64 ? lo : hi) >> (k & 63)) & 1); }
};
constexpr unsigned kAckWindow = 128;
// reliable messages the receiver buffers ahead of a missing one; the sender never sends a message
// this far past the oldest unacknowledged one, so the buffer can never overflow (it used to drop the
// message of a packet it still acknowledged: lost for good, the world transfer stalled forever)
constexpr unsigned kRelWindow = 2048;

struct ConnStats {
    uint32_t packets_sent = 0, packets_recv = 0, resends = 0, acked = 0;
    uint32_t split = 0;     // reliable messages sent in pieces (bigger than one packet)
    uint32_t too_big = 0;   // reliable messages refused: above kMaxReliable
    uint32_t joined = 0;    // reliable messages received in pieces and joined
    uint32_t frag_lost = 0; // received pieces thrown away (a message above kMaxReliable, or a peer that never split)
    uint32_t too_big_old = 0; // reliable messages not sent: above one packet for a peer that cannot join pieces
};
// largest reliable message (BUG-0034): bigger ones are split into pieces of one packet each and
// joined by the receiver; this caps what a receiver collects for one message
constexpr size_t kMaxReliable = 1u << 20;

class Connection {
public:
    using Deliver = std::function<void(uint8_t type, Reader& payload)>;

    Endpoint remote;
    // connection id in every packet header (protocol 6): the guest picks a new one for every join,
    // so a rejoin from the same address and port (a NAT that keeps the mapping, a proxy) is a new
    // connection and late packets of the old one are told apart instead of corrupting the new one
    uint32_t conn_id = 0;
    float rtt_ms = 100.f;
    uint32_t last_recv_ms = 0;
    uint32_t created_ms = 0;
    ConnStats stats;
    // BUG-0219: the peer joins split reliable messages (its HELLO / WELCOME caps bit CAP_SPLIT). A
    // 0.7.2 peer read every piece as a whole message (long chat lost, the species table emptied).
    // false (the safe side) until the handshake says otherwise
    bool peer_split = false;
    // the largest reliable message that goes in one packet (no split needed)
    // (kMaxPacket 1200 - the 31-byte header - type, flags, len, rel_id; checked in connection.cpp)
    static constexpr size_t kMaxSingle = 1200 - 31 - 6;

    void send_reliable(uint8_t type, const Writer& w);
    // first: ahead of what is queued (the player state must not wait behind a big snapshot)
    void send_unreliable(uint8_t type, const Writer& w, bool first = false);
    size_t pending_reliable() const { return rel_out_.size(); }

    // Builds the next datagram if anything is due (messages, acks, keepalive).
    bool build_packet(uint32_t now, std::vector<uint8_t>& out);
    // Parses a datagram, updates acks, delivers messages in order. false = invalid packet.
    bool on_packet(const uint8_t* data, size_t n, uint32_t now, const Deliver& deliver);

    static bool looks_valid(const uint8_t* data, size_t n);
    static uint32_t peek_conn(const uint8_t* data, size_t n); // conn id of a valid packet

private:
    struct OutMsg {
        uint8_t type;
        uint16_t rel_id;
        std::vector<uint8_t> payload;
        bool more = false; // a piece of a split message, more pieces follow (wire flag 2)
        uint32_t last_sent = 0;
        int sends = 0;
    };
    struct SentPkt {
        uint16_t seq;
        uint32_t time;
        std::vector<uint16_t> rel_ids;
        bool acked = false;
    };

    void ack_packet(SentPkt& p, uint32_t now);

    // BUG-0030: the first packet is seq 1. A peer that has received nothing writes ack 0 (with no ack
    // bits); with a first packet of seq 0 that "acknowledged" it, and a lost first packet of the host
    // (the WELCOME) was never resent: the join failed after 10 s. No wire change.
    uint16_t local_seq_ = 0;
    bool seq_started_ = false;
    uint16_t remote_seq_ = 0;
    AckBits recv_bits_;
    bool have_remote_ = false;
    bool ack_pending_ = false;
    uint32_t last_send_ms_ = 0;

    std::deque<SentPkt> sent_;
    uint16_t next_rel_id_ = 0;
    std::deque<OutMsg> rel_out_;
    uint16_t next_rel_expected_ = 0;
    struct InMsg {
        uint8_t type, flags;
        std::vector<uint8_t> payload;
    };
    std::map<uint16_t, InMsg> rel_in_;
    // pieces of a split reliable message received so far (reliable delivery is in order)
    std::vector<uint8_t> frag_;
    uint8_t frag_type_ = 0;
    bool frag_on_ = false, frag_drop_ = false;
    void deliver_reliable(uint8_t type, uint8_t flags, const uint8_t* p, size_t len, const Deliver& deliver);
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> unrel_out_;

public:
    // unreliable messages not sent by the end of a flush are stale by the next one
    void drop_unreliable() { unrel_out_.clear(); }
    size_t unreliable_queued() const { return unrel_out_.size(); }

private:
};

} // namespace net
