#include "connection.h"

#include <algorithm>
#include <cstring>

#include "fixes.h"
#include "protocol.h"

namespace net {

// header: magic u32, proto u16, conn u32, seq u16, ack u16, ackbits 2 x u64 (128 packets), count u8
static constexpr size_t kHeader = 4 + 2 + 4 + 2 + 2 + 16 + 1;

bool Connection::looks_valid(const uint8_t* data, size_t n)
{
    if (n < kHeader)
        return false;
    Reader r(data, n);
    return r.u32() == kMagic && r.u16() == kProtoVersion;
}

uint32_t Connection::peek_conn(const uint8_t* data, size_t n)
{
    uint32_t c = 0;
    if (n >= 10)
        memcpy(&c, data + 6, 4);
    return c;
}

// payload of a reliable message that fits one packet (header + type, flags, len, rel_id)
static constexpr size_t kRelPiece = kMaxPacket - kHeader - 6;
static_assert(kRelPiece == Connection::kMaxSingle, "Connection::kMaxSingle is one reliable piece");

void Connection::send_reliable(uint8_t type, const Writer& w)
{
    if (g_fix.big_reliable && w.data.size() > kRelPiece) {
        // BUG-0034: a message that fits no packet was never sent and blocked every reliable message
        // after it (and the due check sent a burst of empty packets every frame): split it into
        // pieces of one packet, flag 2 on all but the last; the receiver joins them in order
        if (w.data.size() > kMaxReliable) {
            stats.too_big++;
            return;
        }
        if (g_fix.peer_caps && !peer_split) {
            // BUG-0219: an older peer would read each piece as a whole message; queuing it whole
            // brings back the BUG-0034 stall: not sent (callers cut what can be cut, e.g. chat)
            stats.too_big_old++;
            return;
        }
        stats.split++;
        for (size_t off = 0; off < w.data.size(); off += kRelPiece) {
            size_t n = std::min(kRelPiece, w.data.size() - off);
            OutMsg m;
            m.type = type;
            m.rel_id = next_rel_id_++;
            m.payload.assign(w.data.begin() + off, w.data.begin() + off + n);
            m.more = off + n < w.data.size();
            rel_out_.push_back(std::move(m));
        }
        return;
    }
    OutMsg m;
    m.type = type;
    m.rel_id = next_rel_id_++;
    m.payload = w.data;
    rel_out_.push_back(std::move(m));
}

void Connection::deliver_reliable(uint8_t type, uint8_t flags, const uint8_t* p, size_t len, const Deliver& deliver)
{
    bool more = (flags & 2) != 0;
    if (!more && !frag_on_) {
        Reader pr(p, len);
        deliver(type, pr);
        return;
    }
    if (!frag_on_ || frag_type_ != type) { // the first piece (a different type: the old one was cut)
        frag_.clear();
        frag_on_ = true;
        // BUG-0219: a peer that never announced splitting sends no pieces: drop, do not join
        frag_drop_ = g_fix.peer_caps && !peer_split;
        frag_type_ = type;
    }
    if (!frag_drop_ && frag_.size() + len > kMaxReliable) {
        frag_drop_ = true; // never collect more than the sender may send
        frag_.clear();
        frag_.shrink_to_fit();
    }
    if (!frag_drop_)
        frag_.insert(frag_.end(), p, p + len);
    if (more)
        return;
    frag_on_ = false;
    if (frag_drop_) {
        stats.frag_lost++;
        return;
    }
    stats.joined++;
    std::vector<uint8_t> whole = std::move(frag_);
    frag_.clear();
    Reader pr(whole.data(), whole.size());
    deliver(type, pr);
}

void Connection::send_unreliable(uint8_t type, const Writer& w, bool first)
{
    if (first)
        unrel_out_.emplace(unrel_out_.begin(), type, w.data);
    else
        unrel_out_.emplace_back(type, w.data);
}

bool Connection::build_packet(uint32_t now, std::vector<uint8_t>& out)
{
    uint32_t rto = (uint32_t)std::max(100.f, rtt_ms * 1.5f + 20.f);
    uint16_t oldest = rel_out_.empty() ? 0 : rel_out_.front().rel_id; // rel_out_ is in rel_id order
    auto due = [&](const OutMsg& m) {
        return (m.sends == 0 || now - m.last_sent >= rto) && (uint16_t)(m.rel_id - oldest) < kRelWindow;
    };
    bool any_rel_due = false;
    for (auto& m : rel_out_) {
        if ((uint16_t)(m.rel_id - oldest) >= kRelWindow)
            break;
        if (due(m)) {
            any_rel_due = true;
            break;
        }
    }
    uint32_t since = now - last_send_ms_;
    bool keepalive = since >= 250 || (ack_pending_ && since >= 30);
    if (unrel_out_.empty() && !any_rel_due && !keepalive)
        return false;

    Writer w;
    w.u32(kMagic);
    w.u16(kProtoVersion);
    w.u32(conn_id);
    if (!seq_started_) {
        seq_started_ = true;
        if (g_fix.first_ack) // read at the first packet, so a test can switch it per connection
            local_seq_ = 1;
    }
    uint16_t seq = local_seq_++;
    w.u16(seq);
    w.u16(remote_seq_);
    AckBits bits = have_remote_ ? recv_bits_ : AckBits{};
    w.raw(&bits.lo, 8);
    w.raw(&bits.hi, 8);
    size_t count_pos = w.size();
    w.u8(0);
    uint8_t count = 0;
    SentPkt sp;
    sp.seq = seq;
    sp.time = now;

    // unreliable first (latest state matters most); what does not fit goes into the next packet of
    // this flush (snapshots come in several chunks: dropping the rest froze those objects)
    size_t used = 0;
    for (auto& [type, payload] : unrel_out_) {
        if (count == 255)
            break;
        if (w.size() + 4 + payload.size() > kMaxPacket) {
            if (count == 0 && used == 0)
                used++; // can never fit any packet: drop it
            break;
        }
        w.u8(type);
        w.u8(0);
        w.u16((uint16_t)payload.size());
        w.raw(payload.data(), payload.size());
        count++;
        used++;
    }
    unrel_out_.erase(unrel_out_.begin(), unrel_out_.begin() + used);
    for (auto& m : rel_out_) {
        if ((uint16_t)(m.rel_id - oldest) >= kRelWindow)
            break;
        if (!due(m))
            continue;
        if (w.size() + 6 + m.payload.size() > kMaxPacket || count == 255)
            break;
        w.u8(m.type);
        w.u8(m.more ? 3 : 1);
        w.u16((uint16_t)m.payload.size());
        w.u16(m.rel_id);
        w.raw(m.payload.data(), m.payload.size());
        if (m.sends > 0)
            stats.resends++;
        m.sends++;
        m.last_sent = now;
        sp.rel_ids.push_back(m.rel_id);
        count++;
    }
    w.data[count_pos] = count;
    sent_.push_back(std::move(sp));
    while (sent_.size() > 1024)
        sent_.pop_front();
    stats.bytes_sent += w.data.size();
    out = std::move(w.data);
    last_send_ms_ = now;
    ack_pending_ = false;
    stats.packets_sent++;
    return true;
}

void Connection::ack_packet(SentPkt& p, uint32_t now)
{
    if (p.acked)
        return;
    p.acked = true;
    stats.acked++;
    float sample = (float)(now - p.time);
    rtt_ms = rtt_ms * 0.9f + sample * 0.1f;
    if (!stats.rtt_n || sample < stats.rtt_min)
        stats.rtt_min = sample;
    if (sample > stats.rtt_max)
        stats.rtt_max = sample;
    stats.rtt_sum += sample;
    stats.rtt_n++;
    for (uint16_t id : p.rel_ids) {
        auto it = std::find_if(rel_out_.begin(), rel_out_.end(),
                               [id](const OutMsg& m) { return m.rel_id == id; });
        if (it != rel_out_.end())
            rel_out_.erase(it);
    }
}

bool Connection::on_packet(const uint8_t* data, size_t n, uint32_t now, const Deliver& deliver)
{
    if (!looks_valid(data, n))
        return false;
    Reader r(data, n);
    r.u32();
    r.u16();
    if (r.u32() != conn_id)
        return false; // a late packet of an earlier connection from the same address
    uint16_t seq = r.u16();
    uint16_t ack = r.u16();
    AckBits ackbits;
    if (r.left() >= 16) {
        memcpy(&ackbits.lo, r.cur(), 8);
        memcpy(&ackbits.hi, r.cur() + 8, 8);
    }
    r.skip(16);
    uint8_t count = r.u8();
    if (!r.ok())
        return false;

    stats.packets_recv++;
    stats.bytes_recv += n;
    last_recv_ms = now;
    ack_pending_ = true;

    // receive window
    if (!have_remote_) {
        have_remote_ = true;
        remote_seq_ = seq;
        recv_bits_.clear();
    } else if (seq_greater(seq, remote_seq_)) {
        uint16_t shift = (uint16_t)(seq - remote_seq_);
        recv_bits_.shift(shift);
        if (shift <= kAckWindow)
            recv_bits_.set(shift - 1u); // the previous latest
        remote_seq_ = seq;
    } else {
        uint16_t diff = (uint16_t)(remote_seq_ - seq);
        if (diff >= 1 && diff <= kAckWindow)
            recv_bits_.set(diff - 1u);
    }

    // acks for our packets
    for (auto& p : sent_) {
        if (p.acked)
            continue;
        if (p.seq == ack) {
            ack_packet(p, now);
            continue;
        }
        uint16_t diff = (uint16_t)(ack - p.seq);
        if (diff >= 1 && diff <= kAckWindow && ackbits.test(diff - 1u))
            ack_packet(p, now);
        else if (!p.lost && diff >= kLostAfter && diff < 32768) { // counted once (support statistics)
            p.lost = true;
            stats.lost++;
        }
    }

    for (int i = 0; i < count; i++) {
        uint8_t type = r.u8();
        uint8_t flags = r.u8();
        uint16_t len = r.u16();
        uint16_t rel_id = 0;
        if (flags & 1)
            rel_id = r.u16();
        if (!r.ok() || r.left() < len)
            return false;
        const uint8_t* p = r.cur();
        r.skip(len);
        if (!(flags & 1)) {
            Reader pr(p, len);
            deliver(type, pr);
            continue;
        }
        if (rel_id == next_rel_expected_) {
            next_rel_expected_++;
            deliver_reliable(type, flags, p, len, deliver);
            for (auto it = rel_in_.find(next_rel_expected_); it != rel_in_.end();
                 it = rel_in_.find(next_rel_expected_)) {
                InMsg m = std::move(it->second);
                rel_in_.erase(it);
                next_rel_expected_++;
                deliver_reliable(m.type, m.flags, m.payload.data(), m.payload.size(), deliver);
            }
        } else if (seq_greater(rel_id, next_rel_expected_)) {
            // the sender keeps within kRelWindow of the oldest message we lack (see build_packet)
            if ((uint16_t)(rel_id - next_rel_expected_) < kRelWindow && !rel_in_.count(rel_id))
                rel_in_[rel_id] = {type, flags, std::vector<uint8_t>(p, p + len)};
        }
        // else: duplicate of an already delivered message
    }
    return true;
}

} // namespace net
