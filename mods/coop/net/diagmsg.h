// Support diagnostics over the network (MSG_DIAG, docs/SUPPORT.md#support-reading): the session
// clock, bug marks and the sync self-check. Portable (tests/net_tests.cpp).
//
// MSG_DIAG payload: u8 kind, then
//   kClockReq  guest -> host (unreliable): u32 seq, f64 guest's local time
//   kClockAns  host -> guest (unreliable): u32 seq, f64 the echoed time, f64 the host's session time
//   kMark      any -> host -> all (reliable): u8 origin player, f64 session time, str8 name, str8 text
//   kDigestReq host -> guest (reliable): u32 seq
//   kDigest    guest -> host (reliable): the guest's Digest of the request
//   kResult    host -> guest (reliable): u32 seq, u8 state (Result), u16 objects compared, str16 text
// An older SporeCoop drops MSG_DIAG (an unknown type): a guest of an older host gets no clock answer
// and no check, an older guest never answers a digest request (the host stops asking after 3).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "buffer.h"

namespace net::diag {

enum Kind : uint8_t {
    kClockReq = 1,
    kClockAns = 2,
    kMark = 3,
    kDigestReq = 4,
    kDigest = 5,
    kResult = 6,
};

constexpr size_t kMarkTextMax = 200; // bytes of a mark's text (cut between characters)
constexpr size_t kDigestMax = 4000;  // entries of a digest (a Cell pool has ~200)

// ---------------- the session clock ----------------
struct ClockReq {
    uint32_t seq = 0;
    double t_local = 0;
    void write(Writer& w) const { w.u8(kClockReq); w.u32(seq); w.f64(t_local); }
    bool read(Reader& r) { seq = r.u32(); t_local = r.f64(); return r.ok(); }
};

struct ClockAns {
    uint32_t seq = 0;
    double t_echo = 0, t_session = 0;
    void write(Writer& w) const { w.u8(kClockAns); w.u32(seq); w.f64(t_echo); w.f64(t_session); }
    bool read(Reader& r) { seq = r.u32(); t_echo = r.f64(); t_session = r.f64(); return r.ok(); }
};

// The guest's estimate of the host's session clock: the sample with the shortest round trip of the
// last kKeep (its error is at most half its round trip; a long one was delayed on one way).
class ClockSync {
public:
    static constexpr size_t kKeep = 12;
    // sent at t_send, answered at t_recv (local seconds), the host's session time in the answer
    void add(double t_send, double t_recv, double host_t)
    {
        if (t_recv < t_send || t_recv - t_send > 5.0)
            return; // a stale answer
        s_.push_back({t_recv - t_send, host_t - (t_send + t_recv) / 2});
        if (s_.size() > kKeep)
            s_.pop_front();
    }
    bool valid() const { return !s_.empty(); }
    const auto& best() const
    {
        return *std::min_element(s_.begin(), s_.end(), [](const S& a, const S& b) { return a.rtt < b.rtt; });
    }
    double offset() const { return valid() ? best().off : 0; } // session = local + offset
    double rtt() const { return valid() ? best().rtt : -1; }
    size_t samples() const { return s_.size(); }
    void clear() { s_.clear(); }

private:
    struct S {
        double rtt, off;
    };
    std::deque<S> s_;
};

// ---------------- bug marks ----------------
struct Mark {
    uint8_t origin = 0;
    double t_session = -1; // -1: the marker had no session clock
    std::string name, text;
    void write(Writer& w) const
    {
        w.u8(kMark);
        w.u8(origin);
        w.f64(t_session);
        w.str8(name);
        std::string t = text.size() > kMarkTextMax ? text.substr(0, utf8::cut_bytes(text, kMarkTextMax)) : text;
        w.str8(t);
    }
    bool read(Reader& r)
    {
        origin = r.u8();
        t_session = r.f64();
        name = r.str8();
        text = r.str8();
        return r.ok();
    }
};

// ---------------- the sync self-check ----------------
// an object of the shared world: kind letter in the top byte (p player, n NPC creature, c NPC cell,
// t citizen, v vehicle, s ship), its number below (player id, host index / id)
struct Entry {
    uint32_t key = 0;
    float x = 0, y = 0, z = 0;
};
inline uint32_t make_key(char kind, uint32_t n) { return ((uint32_t)(uint8_t)kind << 24) | (n & 0xFFFFFF); }
inline char key_kind(uint32_t k) { return (char)(k >> 24); }
inline uint32_t key_num(uint32_t k) { return k & 0xFFFFFF; }
// "c123" -> make_key('c', 123); 0 for anything else
inline uint32_t parse_key(const std::string& s)
{
    if (s.size() < 2 || s[0] < 'a' || s[0] > 'z')
        return 0;
    uint32_t n = 0;
    for (size_t i = 1; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        n = n * 10 + (uint32_t)(s[i] - '0');
    }
    return make_key(s[0], n);
}
inline std::string key_text(uint32_t k) { return std::string(1, key_kind(k)) + std::to_string(key_num(k)); }

enum Total : uint8_t { kTotalDna = 1, kTotalCellFood = 2 };
inline const char* total_name(uint8_t id) { return id == kTotalDna ? "dna" : id == kTotalCellFood ? "cell food" : "total"; }

struct Digest {
    uint32_t seq = 0;
    uint8_t self = 0;       // the sender's player id
    uint32_t mode = 0, world = 0;
    uint8_t vote = 0;       // an evolution vote is on
    uint32_t vote_editor = 0;
    std::vector<std::pair<uint8_t, float>> totals;
    std::vector<Entry> entries; // players and objects
    void write(Writer& w) const
    {
        w.u8(kDigest);
        w.u32(seq);
        w.u8(self);
        w.u32(mode);
        w.u32(world);
        w.u8(vote);
        w.u32(vote_editor);
        w.u8((uint8_t)std::min<size_t>(totals.size(), 255));
        for (size_t i = 0; i < totals.size() && i < 255; i++) {
            w.u8(totals[i].first);
            w.f32(totals[i].second);
        }
        size_t n = std::min(entries.size(), kDigestMax);
        w.u16((uint16_t)n);
        for (size_t i = 0; i < n; i++) {
            w.u32(entries[i].key);
            w.f32(entries[i].x); w.f32(entries[i].y); w.f32(entries[i].z);
        }
    }
    bool read(Reader& r)
    {
        seq = r.u32();
        self = r.u8();
        mode = r.u32();
        world = r.u32();
        vote = r.u8();
        vote_editor = r.u32();
        int nt = r.u8();
        totals.clear();
        for (int i = 0; i < nt && r.ok(); i++) {
            uint8_t id = r.u8();
            float v = r.f32();
            totals.push_back({id, v});
        }
        size_t n = r.u16();
        if (n > kDigestMax || r.left() < n * 16)
            return false;
        entries.resize(n);
        for (auto& e : entries) {
            e.key = r.u32();
            e.x = r.f32(); e.y = r.f32(); e.z = r.f32();
        }
        return r.ok() && std::all_of(entries.begin(), entries.end(), [](const Entry& e) {
                   return std::isfinite(e.x) && std::isfinite(e.y) && std::isfinite(e.z);
               });
    }
};

// what counts as "the same": position per kind (world units; the guest shows host objects ~0.15 s
// late and the two digests are taken half a round trip apart), totals, the NPC range
struct Limits {
    float player = 10, npc = 8, cell = 4, citizen = 8, vehicle = 25, ship = 150, other = 20;
    float total = 0.5f;   // dna / food: whole points
    float npc_near = 90;  // a host NPC this close to the guest must be on the guest (relevance 110 m)
    bool npc_missing = true; // false: RTS stages (NPCs are sent around the tribe, not the guest)
    float pos(char kind) const
    {
        switch (kind) {
        case 'p': return player;
        case 'n': return npc;
        case 'c': return cell;
        case 't': return citizen;
        case 'v': return vehicle;
        case 's': return ship;
        default: return other;
        }
    }
};

// one difference; `id` is stable between two checks ("missing:c12"), so a recheck can tell a
// lasting difference from one that only happened to be there at the moment of a check
struct Issue {
    std::string id, cat, key; // cat: place, vote, total, missing, extra, pos
    float value = 0;          // pos: the distance
    std::string text;
};

struct Compared {
    bool other_place = false; // host and guest play different stages / worlds: nothing compared
    int objects = 0;          // objects compared by position
    std::vector<Issue> issues;
};

inline float dist(const Entry& a, const Entry& b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// host: its own digest against a guest's
inline Compared compare(const Digest& host, const Digest& guest, const Limits& lim = {})
{
    Compared c;
    char b[160];
    if (host.mode != guest.mode || host.world != guest.world) {
        c.other_place = true;
        snprintf(b, sizeof b, "other place: host %08X/%08X, guest %08X/%08X", host.mode, host.world, guest.mode, guest.world);
        c.issues.push_back({"place", "place", "", 0, b});
        return c;
    }
    if (host.vote != guest.vote)
        c.issues.push_back({"vote", "vote", "", 0, std::string("vote: host ") + (host.vote ? "on" : "off") + ", guest " + (guest.vote ? "on" : "off")});
    else if (host.vote && host.vote_editor != guest.vote_editor) {
        snprintf(b, sizeof b, "vote editor: host %08X, guest %08X", host.vote_editor, guest.vote_editor);
        c.issues.push_back({"vote_editor", "vote", "", 0, b});
    }
    for (auto& [id, hv] : host.totals)
        for (auto& [gid, gv] : guest.totals)
            if (gid == id && std::fabs(hv - gv) > lim.total) {
                snprintf(b, sizeof b, "%s: host %.1f, guest %.1f", total_name(id), hv, gv);
                c.issues.push_back({"total:" + std::to_string(id), "total", total_name(id), std::fabs(hv - gv), b});
            }
    std::map<uint32_t, const Entry*> h, g;
    for (auto& e : host.entries)
        h[e.key] = &e;
    for (auto& e : guest.entries)
        g[e.key] = &e;
    const Entry* me = nullptr; // the guest's own creature (its own game)
    if (auto it = g.find(make_key('p', guest.self)); it != g.end())
        me = it->second;
    for (auto& [k, he] : h) {
        auto it = g.find(k);
        char kind = key_kind(k);
        if (it == g.end()) {
            if (kind == 'n' && (!lim.npc_missing || !me || dist(*he, *me) > lim.npc_near))
                continue; // out of the guest's range: never sent to it
            c.issues.push_back({"missing:" + key_text(k), "missing", key_text(k), 0, ""});
            continue;
        }
        c.objects++;
        float d = dist(*he, *it->second);
        if (d > lim.pos(kind))
            c.issues.push_back({"pos:" + key_text(k), "pos", key_text(k), d, ""});
    }
    for (auto& [k, ge] : g)
        if (!h.count(k))
            c.issues.push_back({"extra:" + key_text(k), "extra", key_text(k), 0, ""});
    return c;
}

// the differences of `second` that were already there at `first` (a lasting difference)
inline std::vector<Issue> persistent(const std::vector<Issue>& first, const std::vector<Issue>& second)
{
    std::set<std::string> ids;
    for (auto& i : first)
        ids.insert(i.id);
    std::vector<Issue> out;
    for (auto& i : second)
        if (ids.count(i.id))
            out.push_back(i);
    return out;
}

// one line: "c: 3 missing on the guest (c12 c40 c41), n: 1 extra on the guest (n7), p1 23.5 apart, dna: host 120.0, guest 95.0"
inline std::string describe(const std::vector<Issue>& v, size_t max_keys = 6)
{
    std::string s;
    auto add = [&](const std::string& t) { s += (s.empty() ? "" : ", ") + t; };
    for (auto& i : v)
        if (!i.text.empty())
            add(i.text);
    // grouped by category and kind
    for (const char* cat : {"missing", "extra", "pos"}) {
        std::map<char, std::vector<const Issue*>> by;
        for (auto& i : v)
            if (i.cat == cat)
                by[i.key.empty() ? '?' : i.key[0]].push_back(&i);
        for (auto& [kind, list] : by) {
            std::string t = std::string(1, kind) + ": " + std::to_string(list.size()) + " " +
                            (std::string(cat) == "missing" ? "missing on the guest" : std::string(cat) == "extra" ? "extra on the guest" : "apart") + " (";
            if (std::string(cat) == "pos")
                std::sort(list.begin(), list.end(), [](const Issue* a, const Issue* b) { return a->value > b->value; });
            for (size_t k = 0; k < list.size() && k < max_keys; k++) {
                t += (k ? " " : "") + list[k]->key;
                if (std::string(cat) == "pos") {
                    char b[24];
                    snprintf(b, sizeof b, " %.1f", list[k]->value);
                    t += b;
                }
            }
            if (list.size() > max_keys)
                t += " +" + std::to_string(list.size() - max_keys);
            add(t + ")");
        }
    }
    return s;
}

// "p 2, n 34, c 0" of a digest
inline std::string counts(const Digest& d)
{
    std::map<char, int> n;
    for (auto& e : d.entries)
        n[key_kind(e.key)]++;
    std::string s;
    for (auto& [k, c] : n)
        s += (s.empty() ? "" : " ") + std::string(1, k) + std::to_string(c);
    return s.empty() ? "nothing" : s;
}

enum Result : uint8_t { kResOk = 0, kResDiffers = 1, kResMismatch = 2, kResOtherPlace = 3 };

struct ResultMsg {
    uint32_t seq = 0;
    uint8_t state = kResOk;
    uint16_t objects = 0;
    std::string text;
    void write(Writer& w) const { w.u8(kResult); w.u32(seq); w.u8(state); w.u16(objects); w.str16(text); }
    bool read(Reader& r) { seq = r.u32(); state = r.u8(); objects = r.u16(); text = r.str16(); return r.ok(); }
};

} // namespace net::diag
