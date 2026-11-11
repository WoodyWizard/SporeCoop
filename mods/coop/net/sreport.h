// One report for the session (0.7.12 F4, BUG-0767, docs/REPORTING.md#reporting-session): the host asks every guest
// for its problem report, each guest answers "send" or "don't send", the reports come to the host over the session
// connection and the host writes one SporeCoop-session-report-<time>.zip. Portable (tests/validate_tests.cpp,
// tests/fuzz_decoders.cpp): the messages, the content check of a received report, the session zip writer and the
// host's bookkeeping of a round. The Windows side (files, the UI, the game's report) is mods/coop/sreport.cpp.
//
// MSG_REPORT payload: u8 kind, then
//   kAsk    host -> guest (reliable): u32 round, str8 the host's name
//   kAnswer guest -> host (reliable): u32 round, u8 answer (0 don't send, 1 sending), u32 size of the zip
//   kChunk  guest -> host (reliable): u32 round, u32 offset, u16 n, n bytes (in order, offset = what came before)
//   kDone   guest -> host (reliable): u32 round, u32 crc32 of the whole zip
//   kStop   host -> guest (reliable): u32 round, u8 why (Why): the host refused the report, stop sending
// An older SporeCoop drops MSG_REPORT (an unknown type): such a guest never answers and is listed "timed out" after
// kAnswerWait.
//
// Safety (docs/plan-0.7.12.md "F4: safety of the combined report"): a guest sends only after its click; at most
// kMaxReport bytes per player, the transfer is stopped at the size the guest announced (never above the cap); the
// host checks that the bytes are a zip whose entries have only the names a SporeCoop report has (check_zip), never
// unpacks it and never runs anything from it, and stores it as received; a refused report leaves no file.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "buffer.h"

namespace net::sreport {

enum Kind : uint8_t { kAsk = 1, kAnswer = 2, kChunk = 3, kDone = 4, kStop = 5 };

constexpr uint32_t kMaxReport = 25u << 20; // per player (a report is 1-5 MB; SporeKit keeps it under ~24 MB)
constexpr size_t kChunkBytes = 1000;       // one packet each
constexpr double kAnswerWait = 120.0;      // s a guest has to answer before it is listed "no answer"
constexpr double kStallWait = 60.0;        // s without a piece of a report being sent: "timed out"
constexpr size_t kMaxEntries = 4096;       // entries of a received zip

enum class Why : uint8_t {
    None = 0,
    TooBig = 1,    // announced or sent more than kMaxReport (or more than it announced)
    Malformed = 2, // not a zip of a SporeCoop report (structure, an entry name, the checksum)
    Order = 3,     // pieces out of order, a piece or the end before the answer
};
inline const char* why_text(Why w)
{
    switch (w) {
    case Why::TooBig: return "too big";
    case Why::Malformed: return "not a SporeCoop report";
    case Why::Order: return "broken transfer";
    default: return "";
    }
}

// ---------------- messages ----------------
struct Ask {
    uint32_t round = 0;
    std::string host;
    void write(Writer& w) const { w.u8(kAsk); w.u32(round); w.str8(host); }
    bool read(Reader& r) { round = r.u32(); host = r.str8(); return r.ok(); }
};
struct Answer {
    uint32_t round = 0;
    uint8_t send = 0;
    uint32_t size = 0;
    void write(Writer& w) const { w.u8(kAnswer); w.u32(round); w.u8(send); w.u32(size); }
    bool read(Reader& r) { round = r.u32(); send = r.u8(); size = r.u32(); return r.ok() && send <= 1; }
};
struct Piece {
    uint32_t round = 0, offset = 0;
    const uint8_t* data = nullptr;
    uint16_t n = 0;
    std::vector<uint8_t> own; // read: the bytes
    void write(Writer& w) const { w.u8(kChunk); w.u32(round); w.u32(offset); w.u16(n); w.raw(data, n); }
    bool read(Reader& r)
    {
        round = r.u32();
        offset = r.u32();
        n = r.u16();
        if (!r.ok() || n == 0 || n > kChunkBytes || r.left() < n)
            return false;
        own.resize(n);
        r.raw(own.data(), n);
        data = own.data();
        return r.ok();
    }
};
struct Done {
    uint32_t round = 0, crc = 0;
    void write(Writer& w) const { w.u8(kDone); w.u32(round); w.u32(crc); }
    bool read(Reader& r) { round = r.u32(); crc = r.u32(); return r.ok(); }
};
struct Stop {
    uint32_t round = 0;
    Why why = Why::None;
    void write(Writer& w) const { w.u8(kStop); w.u32(round); w.u8((uint8_t)why); }
    bool read(Reader& r) { round = r.u32(); why = (Why)r.u8(); return r.ok(); }
};

// ---------------- crc32 (the zip's; header-only for the unit tests) ----------------
inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ---------------- the content check of a received report ----------------
// A name SporeKit's report archive has (sporekit/diag.cpp diag_write_bundle_with): README.txt, LEFT_OUT.txt, the
// report (report-*.txt or crash-*.txt) at the top, else one known folder and a plain file name with the folder's
// extensions. Printable ASCII only: no backslash, no drive letter or colon, no "..", no leading dot or space, no
// second folder level.
inline bool ends_with(const std::string& s, const char* e)
{
    size_t n = strlen(e);
    return s.size() >= n && s.compare(s.size() - n, n, e) == 0;
}
inline bool plain_file(const std::string& f)
{
    if (f.empty() || f.size() > 120 || f[0] == '.' || f[0] == ' ' || f.back() == ' ' || f.back() == '.')
        return false;
    if (f.find("..") != std::string::npos)
        return false;
    for (unsigned char ch : f)
        if (ch < 0x20 || ch > 0x7E || strchr("/\\:*?\"<>|", ch))
            return false;
    return true;
}
inline bool allowed_name(const std::string& name)
{
    size_t slash = name.find('/');
    if (slash == std::string::npos) {
        if (!plain_file(name))
            return false;
        if (name == "README.txt" || name == "LEFT_OUT.txt")
            return true;
        return (name.rfind("report-", 0) == 0 || name.rfind("crash-", 0) == 0) && ends_with(name, ".txt");
    }
    std::string dir = name.substr(0, slash), file = name.substr(slash + 1);
    if (!plain_file(file))
        return false;
    if (dir == "logs" || dir == "previous")
        return ends_with(file, ".log");
    if (dir == "crashes")
        return ends_with(file, ".txt") || ends_with(file, ".dmp");
    if (dir == "reports")
        return ends_with(file, ".txt");
    if (dir == "shots")
        return ends_with(file, ".jpg");
    if (dir == "settings")
        return ends_with(file, ".ini");
    return false;
}

inline uint32_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// d, n: the received bytes. true = a zip of a SporeCoop report; else *why says what (for the host's log, not for the
// guest: the guest only hears "refused"). Reads only inside [d, d + n); never unpacks.
inline bool check_zip(const uint8_t* d, size_t n, std::string* why, std::vector<std::string>* names = nullptr)
{
    auto no = [&](const std::string& s) {
        if (why)
            *why = s;
        return false;
    };
    if (n < 22 || n > kMaxReport)
        return no("size " + std::to_string(n));
    // the end of central directory record: the archive ends with it (the comment exactly up to the end)
    size_t eocd = SIZE_MAX;
    size_t lo = n >= 22 + 65535 ? n - 22 - 65535 : 0;
    for (size_t i = n - 22 + 1; i-- > lo;)
        if (rd32(d + i) == 0x06054B50 && i + 22 + rd16(d + i + 20) == n) {
            eocd = i;
            break;
        }
    if (eocd == SIZE_MAX)
        return no("no end of central directory");
    const uint8_t* e = d + eocd;
    uint32_t disk = rd16(e + 4), cd_disk = rd16(e + 6), here = rd16(e + 8), total = rd16(e + 10);
    uint32_t cd_size = rd32(e + 12), cd_off = rd32(e + 16);
    if (disk || cd_disk || here != total || total == 0 || total > kMaxEntries)
        return no("entries " + std::to_string(total));
    if ((uint64_t)cd_off + cd_size != eocd)
        return no("central directory not before its end record");
    std::set<std::string> seen;
    std::vector<std::pair<uint64_t, uint64_t>> spans; // [start, end) of every local header + data
    size_t p = cd_off;
    for (uint32_t k = 0; k < total; k++) {
        if (p + 46 > eocd || rd32(d + p) != 0x02014B50)
            return no("central entry " + std::to_string(k));
        const uint8_t* c = d + p;
        uint32_t flags = rd16(c + 8), method = rd16(c + 10), crc = rd32(c + 16), csize = rd32(c + 20),
                 usize = rd32(c + 24), nlen = rd16(c + 28), xlen = rd16(c + 30), clen = rd16(c + 32),
                 dstart = rd16(c + 34), loc = rd32(c + 42);
        if (flags & 0x0041) // encrypted / strong encryption
            return no("encrypted entry");
        if (method != 0 && method != 8)
            return no("method " + std::to_string(method));
        if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || loc == 0xFFFFFFFFu || dstart)
            return no("zip64 / multi-disk entry");
        if (nlen == 0 || p + 46 + nlen + xlen + clen > eocd)
            return no("central entry name");
        std::string name((const char*)c + 46, nlen);
        if (!allowed_name(name))
            return no("entry name not of a report");
        if (!seen.insert(name).second)
            return no("entry twice");
        if (method == 0 && csize != usize)
            return no("stored entry sizes");
        if ((uint64_t)loc + 30 > cd_off || rd32(d + loc) != 0x04034B50)
            return no("local header");
        const uint8_t* l = d + loc;
        uint32_t lnlen = rd16(l + 26), lxlen = rd16(l + 28);
        if (rd16(l + 6) & 0x0041 || lnlen != nlen || (uint64_t)loc + 30 + lnlen + lxlen > cd_off ||
            memcmp(l + 30, name.data(), nlen) != 0)
            return no("local header name");
        uint64_t data = (uint64_t)loc + 30 + lnlen + lxlen;
        if (data + csize > cd_off)
            return no("entry data past the central directory");
        if (method == 0 && crc32(d + data, csize) != crc)
            return no("entry checksum");
        spans.push_back({loc, data + csize});
        if (names)
            names->push_back(name);
        p += 46 + nlen + xlen + clen;
    }
    if (p != eocd)
        return no("central directory size");
    std::sort(spans.begin(), spans.end());
    for (size_t i = 1; i < spans.size(); i++)
        if (spans[i].first < spans[i - 1].second)
            return no("entries overlap");
    return true;
}

// ---------------- the session zip (stored entries, written as they come) ----------------
class ZipOut {
public:
    std::function<bool(const uint8_t*, size_t)> out; // appends to the file; false = cannot
    uint16_t dtime = 0, ddate = 0x21;                // DOS time / date of every entry
    bool ok = true;

    bool add(const std::string& name, const uint8_t* data, size_t n)
    {
        if (!ok || n > 0xFFFFFFF0u || count_ >= 0xFFFF)
            return ok = false;
        uint32_t crc = crc32(data, n), size = (uint32_t)n;
        std::vector<uint8_t> h;
        put32(h, 0x04034B50);
        put16(h, 20);
        put16(h, 0x0800); // UTF-8 names
        put16(h, 0);      // stored
        put16(h, dtime);
        put16(h, ddate);
        put32(h, crc);
        put32(h, size);
        put32(h, size);
        put16(h, (uint32_t)name.size());
        put16(h, 0);
        h.insert(h.end(), name.begin(), name.end());
        put32(central_, 0x02014B50);
        put16(central_, 20);
        put16(central_, 20);
        put16(central_, 0x0800);
        put16(central_, 0);
        put16(central_, dtime);
        put16(central_, ddate);
        put32(central_, crc);
        put32(central_, size);
        put32(central_, size);
        put16(central_, (uint32_t)name.size());
        put16(central_, 0);
        put16(central_, 0);
        put16(central_, 0);
        put16(central_, 0);
        put32(central_, 0);
        put32(central_, offset_);
        central_.insert(central_.end(), name.begin(), name.end());
        ok = out && out(h.data(), h.size()) && (n == 0 || out(data, n));
        offset_ += (uint32_t)(h.size() + n);
        count_++;
        return ok;
    }
    bool finish()
    {
        if (!ok)
            return false;
        std::vector<uint8_t> e;
        put32(e, 0x06054B50);
        put16(e, 0);
        put16(e, 0);
        put16(e, count_);
        put16(e, count_);
        put32(e, (uint32_t)central_.size());
        put32(e, offset_);
        put16(e, 0);
        ok = out(central_.data(), central_.size()) && out(e.data(), e.size());
        return ok;
    }
    uint32_t bytes() const { return offset_; }
    unsigned entries() const { return count_; }

private:
    std::vector<uint8_t> central_;
    uint32_t offset_ = 0;
    unsigned count_ = 0;
    static void put16(std::vector<uint8_t>& o, uint32_t v)
    {
        o.push_back((uint8_t)v);
        o.push_back((uint8_t)(v >> 8));
    }
    static void put32(std::vector<uint8_t>& o, uint32_t v)
    {
        put16(o, v & 0xFFFF);
        put16(o, v >> 16);
    }
};

// "Host-" / "Guest-" + the player's name as a plain file name (letters, digits, '-', '_'; others '_'), unique in
// `taken` ("-2", "-3" appended); a name with nothing left is "player<id>"
inline std::string entry_name(const char* prefix, const std::string& player, uint8_t id, std::set<std::string>& taken)
{
    std::string s;
    for (unsigned char ch : player) {
        if (s.size() >= 32)
            break;
        if (isalnum(ch) || ch == '-' || ch == '_')
            s += (char)ch;
        else if (ch < 0x80 || (ch & 0xC0) == 0xC0) // one '_' per character (not per UTF-8 byte)
            s += (s.empty() || s.back() == '_') ? "" : "_";
    }
    while (!s.empty() && s.back() == '_')
        s.pop_back();
    if (s.empty())
        s = "player" + std::to_string(id);
    std::string base = std::string(prefix) + s, n = base + ".zip";
    for (int k = 2; taken.count(n); k++)
        n = base + "-" + std::to_string(k) + ".zip";
    taken.insert(n);
    return n;
}

// ---------------- the host's round ----------------
enum class State : uint8_t {
    Asked,     // waiting for the guest's answer
    Receiving, // the guest said "send": pieces coming
    Sent,      // complete and checked: in the session zip
    NotSent,   // the guest clicked "don't send"
    TimedOut,  // no answer in kAnswerWait, or no piece for kStallWait
    Refused,   // too big or malformed (why): nothing of it is kept
    Left,      // the guest left the session before its report was complete
};
inline const char* state_text(State s)
{
    switch (s) {
    case State::Asked: return "asked";
    case State::Receiving: return "sending";
    case State::Sent: return "sent";
    case State::NotSent: return "not sent";
    case State::TimedOut: return "timed out";
    case State::Refused: return "refused";
    case State::Left: return "left";
    }
    return "?";
}
inline bool final_state(State s) { return s != State::Asked && s != State::Receiving; }

struct Guest {
    uint8_t id = 0;
    std::string name;
    State st = State::Asked;
    Why why = Why::None;
    std::string detail; // the check's reason (host log only)
    uint32_t size = 0;  // announced
    std::vector<uint8_t> data;
    double last = 0; // the last message of this guest (s)
};

// One round of asking. The host's game feeds it the messages and the clock; what it returns says whether to send
// the guest a kStop. A Sent guest's data is complete and checked (take it with take_sent()).
struct Round {
    uint32_t id = 0;
    double t0 = 0;
    std::vector<Guest> guests;
    uint32_t cap = kMaxReport; // tests may lower it (never raise it)
    double stall_wait = kStallWait; // tests may lower it (coop.sreport stallwait)

    Guest* find(uint8_t gid)
    {
        for (auto& g : guests)
            if (g.id == gid)
                return &g;
        return nullptr;
    }
    bool finished() const
    {
        for (auto& g : guests)
            if (!final_state(g.st))
                return false;
        return true;
    }
    // the guest refused: Why to send it in a kStop (None: nothing to send)
    Why refuse(Guest& g, Why w, const std::string& detail)
    {
        g.st = State::Refused;
        g.why = w;
        g.detail = detail;
        std::vector<uint8_t>().swap(g.data); // nothing of it is kept
        return w;
    }
    Why on_answer(uint8_t gid, const Answer& a, double now)
    {
        Guest* g = find(gid);
        if (!g || a.round != id || g->st != State::Asked)
            return Why::None; // a late or foreign answer: ignored
        g->last = now;
        if (!a.send) {
            g->st = State::NotSent;
            return Why::None;
        }
        if (a.size > cap)
            return refuse(*g, Why::TooBig, "announced " + std::to_string(a.size) + " bytes");
        if (a.size < 22)
            return refuse(*g, Why::Malformed, "announced " + std::to_string(a.size) + " bytes");
        g->st = State::Receiving;
        g->size = a.size;
        g->data.reserve(a.size);
        return Why::None;
    }
    Why on_piece(uint8_t gid, const Piece& p, double now)
    {
        Guest* g = find(gid);
        if (!g || p.round != id)
            return Why::None;
        if (g->st != State::Receiving)
            return g->st == State::Asked ? refuse(*g, Why::Order, "a piece before the answer") : Why::None;
        g->last = now;
        if (p.offset != g->data.size())
            return refuse(*g, Why::Order, "piece at " + std::to_string(p.offset) + ", have " + std::to_string(g->data.size()));
        if ((uint64_t)p.offset + p.n > g->size || (uint64_t)p.offset + p.n > cap) // stopped at the size, never above the cap
            return refuse(*g, Why::TooBig, "more than the " + std::to_string(g->size) + " bytes announced");
        g->data.insert(g->data.end(), p.data, p.data + p.n);
        return Why::None;
    }
    Why on_done(uint8_t gid, const Done& d, double now)
    {
        Guest* g = find(gid);
        if (!g || d.round != id)
            return Why::None;
        if (g->st != State::Receiving)
            return g->st == State::Asked ? refuse(*g, Why::Order, "the end before the answer") : Why::None;
        g->last = now;
        if (g->data.size() != g->size)
            return refuse(*g, Why::Order, "ended at " + std::to_string(g->data.size()) + " of " + std::to_string(g->size));
        if (crc32(g->data.data(), g->data.size()) != d.crc)
            return refuse(*g, Why::Malformed, "checksum of the transfer");
        std::string why;
        if (!check_zip(g->data.data(), g->data.size(), &why))
            return refuse(*g, Why::Malformed, why);
        g->st = State::Sent;
        return Why::None;
    }
    void on_leave(uint8_t gid)
    {
        Guest* g = find(gid);
        if (g && !final_state(g->st)) {
            g->st = State::Left;
            std::vector<uint8_t>().swap(g->data);
        }
    }
    // the guests that time out now (no kStop for them: an answer that comes later is ignored)
    void tick(double now)
    {
        for (auto& g : guests) {
            if ((g.st == State::Asked && now - t0 > kAnswerWait) || (g.st == State::Receiving && now - g.last > stall_wait)) {
                g.st = State::TimedOut;
                std::vector<uint8_t>().swap(g.data);
            }
        }
    }
    // one line per guest: "Bob: sent (1234 KB)", "Carol: not sent", "Dave: refused (too big)"
    std::string line_of(const Guest& g) const
    {
        std::string s = g.name + ": " + state_text(g.st);
        if (g.st == State::Refused && g.why != Why::None)
            s += std::string(" (") + why_text(g.why) + ")";
        if (g.st == State::Sent)
            s += " (" + std::to_string((g.data.size() + 1023) / 1024) + " KB)";
        if (g.st == State::Receiving && g.size)
            s += " " + std::to_string((unsigned)(g.data.size() * 100ull / g.size)) + "%";
        return s;
    }
    int count(State s) const
    {
        int n = 0;
        for (auto& g : guests)
            n += g.st == s;
        return n;
    }
};

} // namespace net::sreport
