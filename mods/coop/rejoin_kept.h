// The rejoin tokens of clean leaves, kept on disk across a restart of the game (BUG-1135), see rejoin.h.
//
// A clean leave keeps its token for the same host address and the same name (BUG-1001). In memory only, a restart of
// the game sent a fresh token, and the host (which keys the kept Space state by the token, BUG-1107) gave none of the
// player's colonies, hold, badges or budget back. The file keeps those tokens next to the slot files, one line per
// (host, name) pair, under a salted 64-bit hash of the pair: the file holds no readable host address or name. An entry
// is used only for a join to the same host address under the same name (never sent to another host or under another
// name: "no stable id follows a player from host to host" stays true), expires after `ttl` (a day; 30 days with
// net.rejoin_month, BUG-1150: the per-player store on the host's disk, player_store.h, finds a player by this token, and
// the next evening of a weekly game is more than a day later) and the file keeps at most kMax entries (the oldest go
// first).
//
// Portable (no Windows): tests/space_tests.cpp runs it natively.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rejoin {

class KeptFile {
public:
    static constexpr int64_t kTtl = 24 * 3600;           // s: an entry older than a day is gone (net.rejoin_month off)
    static constexpr int64_t kTtlMonth = 30 * 24 * 3600; // s: ... older than 30 days (BUG-1150, net.rejoin_month)
    static constexpr size_t kMax = 16;                   // entries
    int64_t ttl = kTtl; // the caller's choice of the two

    struct Entry {
        uint64_t key = 0, token = 0;
        int64_t at = 0; // unix time of the leave that kept it
    };

    // the file's text; anything that is not this format (another version, a damaged file) is an empty store
    bool parse(const std::string& text)
    {
        salt_ = 0;
        e_.clear();
        size_t pos = 0;
        bool first = true;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            if (line.empty())
                continue;
            unsigned long long a = 0, b = 0;
            long long t = 0;
            if (first) {
                if (sscanf(line.c_str(), "sporecoop-rejoin 1 %16llx", &a) != 1 || !a) {
                    e_.clear();
                    return false;
                }
                salt_ = a;
                first = false;
                continue;
            }
            if (sscanf(line.c_str(), "%16llx %16llx %lld", &a, &b, &t) == 3 && a && b && e_.size() < kMax)
                e_.push_back({a, b, t});
        }
        return salt_ != 0;
    }

    std::string text() const
    {
        if (!salt_)
            return "";
        char b[80];
        snprintf(b, sizeof b, "sporecoop-rejoin 1 %016llx\n", (unsigned long long)salt_);
        std::string o = b;
        for (auto& x : e_) {
            snprintf(b, sizeof b, "%016llx %016llx %lld\n", (unsigned long long)x.key, (unsigned long long)x.token,
                     (long long)x.at);
            o += b;
        }
        return o;
    }

    // a new file takes a random salt (the caller's random source; 0 is not a salt)
    void ensure_salt(uint64_t random)
    {
        if (!salt_)
            salt_ = random ? random : 0x5EC0C0DE5EC0C0DEull;
    }
    uint64_t salt() const { return salt_; }

    // the key of a (host address, name) pair in this file: FNV-1a over the salt, the address, a 0 byte, the name, then
    // a 64-bit finaliser. 0 without a salt
    uint64_t key(const std::string& host, const std::string& name) const
    {
        if (!salt_)
            return 0;
        uint64_t h = 0xCBF29CE484222325ull;
        auto add = [&h](uint8_t c) {
            h ^= c;
            h *= 0x100000001B3ull;
        };
        for (int i = 0; i < 8; i++)
            add((uint8_t)(salt_ >> (8 * i)));
        for (char c : host)
            add((uint8_t)c);
        add(0);
        for (char c : name)
            add((uint8_t)c);
        h ^= h >> 33;
        h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
        h *= 0xC4CEB9FE1A85EC53ull;
        h ^= h >> 33;
        return h ? h : 1;
    }

    // drop the entries older than ttl (and any from the future: a clock set back). true: something went
    bool expire(int64_t now)
    {
        size_t n = e_.size();
        int64_t t = ttl;
        e_.erase(std::remove_if(e_.begin(), e_.end(), [now, t](const Entry& x) { return x.at > now || now - x.at > t; }),
                 e_.end());
        return e_.size() != n;
    }

    // the token kept for this pair, 0 = none (expired entries do not count)
    uint64_t find(const std::string& host, const std::string& name, int64_t now) const
    {
        uint64_t k = key(host, name);
        for (auto& x : e_)
            if (k && x.key == k && x.at <= now && now - x.at <= ttl)
                return x.token;
        return 0;
    }

    // keep the token of a clean leave for this pair (replaces the pair's old one; the oldest entry goes when full)
    void put(const std::string& host, const std::string& name, uint64_t token, int64_t now)
    {
        uint64_t k = key(host, name);
        if (!k || !token)
            return;
        e_.erase(std::remove_if(e_.begin(), e_.end(), [k](const Entry& x) { return x.key == k; }), e_.end());
        expire(now);
        while (e_.size() >= kMax)
            e_.erase(std::min_element(e_.begin(), e_.end(), [](const Entry& a, const Entry& b) { return a.at < b.at; }));
        e_.push_back({k, token, now});
    }

    // forget the pair (a join that was refused as another game's would not take it again)
    void erase(const std::string& host, const std::string& name)
    {
        uint64_t k = key(host, name);
        e_.erase(std::remove_if(e_.begin(), e_.end(), [k](const Entry& x) { return x.key == k; }), e_.end());
    }

    size_t size() const { return e_.size(); }

private:
    uint64_t salt_ = 0;
    std::vector<Entry> e_;
};

// NET-2 (BUG-1498): the slot file (rejoin-<instance>-<slot>.txt) holds the token of the current join and, since 0.7.17,
// for whom: a hash of the host address and the name (`<token> <pair>`, both 16 hex digits). The slot outlives a crash
// and a join that failed at once (an address that does not parse): the next join sent that token although it went to
// another host (or under another name), the kept token of a clean leave lost against it and the host's per-player
// store did not know the player. A slot of another pair is ignored now (net.rejoin_slot_pair); a slot written by an
// older version (the token alone) still counts for any join, as before.
struct SlotLine {
    uint64_t token = 0, pair = 0; // pair 0: an older version's slot (no pair)

    // the pair's hash: KeptFile::key under a fixed salt (the slot is emptied by every clean end; no plain address)
    static uint64_t pair_of(const std::string& host, const std::string& name)
    {
        KeptFile k;
        k.ensure_salt(0x51071A1BC0DE5107ull);
        return k.key(host, name);
    }
    static std::string text(uint64_t token, uint64_t pair)
    {
        if (!token)
            return "";
        char b[48];
        snprintf(b, sizeof b, "%016llx %016llx\n", (unsigned long long)token, (unsigned long long)pair);
        return b;
    }
    // the slot's bytes; false: no token (empty, damaged)
    bool parse(const std::string& t)
    {
        token = pair = 0;
        unsigned long long a = 0, b = 0;
        if (t.size() < 16 || sscanf(t.c_str(), "%16llx", &a) != 1 || !a)
            return false;
        token = a;
        if (t.size() >= 33 && t[16] == ' ' && sscanf(t.c_str() + 17, "%16llx", &b) == 1)
            pair = b;
        return true;
    }
    // may a join of this pair send the slot's token? (pair_check off: always, 0.7.16.1)
    bool usable_for(uint64_t want, bool pair_check) const { return token && (!pair_check || !pair || pair == want); }
};

} // namespace rejoin
