// Decoder fuzzer for everything that parses bytes from another player or from the internet
// (make test, built with -fsanitize=address,undefined; docs/04-netcode.md#netcode-fuzzing).
//
// For every codec: write -> read round trip (re-encoding gives the same bytes, nothing left over),
// every strict prefix is rejected, and mutations of valid encodings (bit flips, byte changes,
// insertions, deletions, truncations, random tails) never read out of bounds; what a decoder
// accepts encodes and decodes again to the same thing. Then random datagrams into
// Connection::on_packet and into host and guest Sessions over a fake transport (Session::set_transport).
// Not covered: the on_message handlers of the game modules (tribe, civ, cell, space, ...) read their
// payloads next to game calls and do not build without the game; their shared checks are validate.h.
//
// Deterministic (fixed seeds, about 3 s). `fuzz_decoders --iters N` changes the mutation count per codec,
// `--seed S` the seed. Built with -DFUZZ_LIBFUZZER -fsanitize=fuzzer it is a libFuzzer target instead.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../updater/pack.h"
#include "net/connection.h"
#include "net/dbpf.h"
#include "net/interp.h"
#include "net/lobby.h"
#include "net/protocol.h"
#include "net/session.h"
#include "net/utf8.h"
#include "net/validate.h"

using namespace net;

static int g_fail = 0;
#define CHECK(c, ...)                                                    \
    do {                                                                 \
        if (!(c)) {                                                      \
            if (g_fail++ < 30) {                                         \
                printf("  FAIL %s:%d: %s ", __FILE__, __LINE__, #c);     \
                printf(__VA_ARGS__);                                     \
                printf("\n");                                            \
            }                                                            \
        }                                                                \
    } while (0)

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint64_t next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    uint32_t below(uint32_t n) { return n ? (uint32_t)(next() % n) : 0; }
    uint8_t u8() { return (uint8_t)next(); }
    uint16_t u16() { return (uint16_t)next(); }
    uint32_t u32() { return (uint32_t)next(); }
    float f32()
    {
        // mostly plain numbers, sometimes any bit pattern (NaN, inf, denormals)
        if (below(8) == 0) {
            uint32_t b = u32();
            float f;
            memcpy(&f, &b, 4);
            return f;
        }
        return (float)((int)below(20001) - 10000) / 7.f;
    }
    std::string str(size_t max)
    {
        size_t n = below(4) == 0 ? max : below((uint32_t)max + 1);
        std::string s(n, ' ');
        for (auto& c : s)
            c = (char)u8();
        return s;
    }
};

// the bytes in a heap block of exactly their size: ASan reports any read past the end
struct Exact {
    std::unique_ptr<uint8_t[]> p;
    size_t n;
    explicit Exact(const std::vector<uint8_t>& v) : p(new uint8_t[v.empty() ? 1 : v.size()]), n(v.size())
    {
        if (n)
            memcpy(p.get(), v.data(), n);
    }
    // an empty input as a pointer to a 0-byte allocation, not a 1-byte one
    const uint8_t* data() const { return p.get(); }
};

static std::vector<uint8_t> mutate(Rng& R, std::vector<uint8_t> v)
{
    int ops = 1 + (int)R.below(R.below(4) == 0 ? 8 : 2);
    for (int k = 0; k < ops; k++) {
        switch (R.below(8)) {
        case 0: case 1: // bit flip
            if (!v.empty())
                v[R.below((uint32_t)v.size())] ^= (uint8_t)(1u << R.below(8));
            break;
        case 2: // a byte to an interesting value
            if (!v.empty()) {
                static const uint8_t vals[] = {0, 1, 0x7F, 0x80, 0xFE, 0xFF};
                v[R.below((uint32_t)v.size())] = vals[R.below(6)];
            }
            break;
        case 3: // random byte
            if (!v.empty())
                v[R.below((uint32_t)v.size())] = R.u8();
            break;
        case 4: // truncate
            v.resize(R.below((uint32_t)v.size() + 1));
            break;
        case 5: // insert bytes
        {
            size_t at = R.below((uint32_t)v.size() + 1), n = 1 + R.below(8);
            for (size_t i = 0; i < n; i++)
                v.insert(v.begin() + (long)at, R.u8());
            break;
        }
        case 6: // delete bytes
            if (!v.empty()) {
                size_t at = R.below((uint32_t)v.size()), n = 1 + R.below(8);
                v.erase(v.begin() + (long)at, v.begin() + (long)std::min(v.size(), at + n));
            }
            break;
        case 7: // a 16/32-bit field to 0xFFFF... (lengths and counts)
            if (v.size() >= 4) {
                size_t at = R.below((uint32_t)v.size() - 3);
                uint32_t x = R.below(2) ? 0xFFFFFFFFu : R.u32();
                memcpy(&v[at], &x, R.below(2) ? 2 : 4);
            }
            break;
        }
    }
    return v;
}

// ---------------------------------------------------------------- protocol structs
template <class T>
static std::vector<uint8_t> enc(const T& x)
{
    Writer w;
    x.write(w);
    return w.data;
}

struct CodecStats {
    int roundtrips = 0, prefixes = 0, mutations = 0, accepted = 0;
};

template <class T>
static void fuzz_struct(const char* name, Rng& R, int iters, const std::function<T(Rng&)>& gen,
                        const std::function<size_t(const T&)>& base_len = nullptr)
{
    CodecStats st;
    std::vector<std::vector<uint8_t>> corpus;
    for (int i = 0; i < 64; i++) {
        T x = gen(R);
        auto b = enc(x);
        Exact e(b);
        Reader r(e.data(), e.n);
        T y;
        bool ok = y.read(r);
        CHECK(ok, "%s: a valid encoding (%zu bytes) is rejected", name, b.size());
        CHECK(r.left() == 0, "%s: %zu bytes left after reading", name, r.left());
        CHECK(enc(y) == b, "%s: re-encoding differs", name);
        st.roundtrips++;
        // every strict prefix must be rejected
        for (size_t k = 0; k < b.size(); k++) {
            std::vector<uint8_t> pre(b.begin(), b.begin() + (long)k);
            Exact ep(pre);
            Reader rp(ep.data(), ep.n);
            T z;
            // an appended extension (HELLO / WELCOME caps, BUG-0219): a prefix that still holds the
            // base fields is an older peer's message and must be read
            if (base_len && k >= base_len(x))
                CHECK(z.read(rp), "%s: base prefix %zu/%zu rejected", name, k, b.size());
            else
                CHECK(!z.read(rp), "%s: prefix %zu/%zu accepted", name, k, b.size());
            st.prefixes++;
        }
        corpus.push_back(b);
    }
    for (int i = 0; i < iters; i++) {
        auto m = mutate(R, corpus[R.below((uint32_t)corpus.size())]);
        Exact e(m);
        Reader r(e.data(), e.n);
        T y;
        st.mutations++;
        if (!y.read(r))
            continue;
        st.accepted++;
        // accepted: it is a value of the type, so it encodes and decodes to itself
        auto b = enc(y);
        Exact e2(b);
        Reader r2(e2.data(), e2.n);
        T z;
        CHECK(z.read(r2) && r2.left() == 0 && enc(z) == b, "%s: accepted mutation does not round trip", name);
    }
    printf("  %-14s %3d round trips, %5d prefixes, %6d mutations (%d accepted)\n", name, st.roundtrips, st.prefixes,
           st.mutations, st.accepted);
}

static Vec3 v3(Rng& R) { return {R.f32(), R.f32(), R.f32()}; }
static Quat q4(Rng& R) { return {R.f32(), R.f32(), R.f32(), R.f32()}; }

// MSG_NPC_STATE: u32 time_ms, u8 count, count x NpcState (npc.cpp writes it, its guest reads it so)
struct NpcStateBatch {
    uint32_t time_ms = 0;
    std::vector<NpcState> v;
    void write(Writer& w) const
    {
        w.u32(time_ms);
        w.u8((uint8_t)v.size());
        for (auto& s : v)
            s.write(w);
    }
    bool read(Reader& r)
    {
        time_ms = r.u32();
        uint8_t n = r.u8();
        v.clear();
        for (int i = 0; i < n && r.ok(); i++) {
            NpcState s;
            if (!s.read(r))
                break;
            v.push_back(s);
        }
        return r.ok();
    }
};

static NpcState npc_state(Rng& R)
{
    NpcState s;
    s.net_id = R.u16(); s.pos = v3(R); s.rot = q4(R); s.vel = v3(R); s.health = R.f32(); s.flags = R.u8();
    return s;
}

// BUG-0219: the HELLO / WELCOME extension, explicit cases (0.7.2 shapes, short token, unknown bits,
// trailing garbage); Welcome::world stays at the same offset in all of them
static void hello_welcome_ext()
{
    auto rd = [](const std::vector<uint8_t>& b, auto& x) {
        Reader r(b.data(), b.size());
        return x.read(r);
    };
    Writer base;
    base.u16(kProtoVersion); base.u32(0x922af7c7); base.str8("Bob");
    Hello h;
    CHECK(rd(base.data, h) && h.caps == 0 && !h.has_token && h.name == "Bob", "Hello 0.7.2");
    Writer t = base;
    t.u32(CAP_SPLIT | CAP_TOKEN); t.u32(7); // a token cut short
    CHECK(rd(t.data, h) && h.caps == (CAP_SPLIT | CAP_TOKEN) && !h.has_token, "Hello short token");
    Writer u = base;
    uint64_t tok = 0x1122334455667788ull;
    u.u32(CAP_SPLIT | CAP_TOKEN | 0x80000000u); u.raw(&tok, 8); u.u32(0xDEADBEEF); u.u8(1); // unknown bit, garbage
    CHECK(rd(u.data, h) && h.has_token && h.token == tok && (h.caps & CAP_SPLIT), "Hello unknown bits");
    Writer wb;
    wb.u8(2); wb.u32(0xABCD1234); wb.u8(1); wb.u8(0); wb.str8("Host");
    Welcome w;
    CHECK(rd(wb.data, w) && w.caps == 0 && w.world == 0xABCD1234 && w.players.size() == 1, "Welcome 0.7.2");
    Writer wx = wb;
    wx.u32(CAP_SPLIT | 0x40u); wx.u8(9); wx.u8(9);
    CHECK(rd(wx.data, w) && (w.caps & CAP_SPLIT) && w.world == 0xABCD1234 && w.your_id == 2, "Welcome ext");
    Welcome mine;
    mine.world = 0xABCD1234;
    Writer wm;
    mine.write(wm);
    CHECK(wm.data.size() >= 5 && memcmp(wm.data.data() + 1, wb.data.data() + 1, 4) == 0, "Welcome::world offset");
}

static void fuzz_protocol(uint64_t seed, int iters)
{
    printf("protocol structs (%d mutations each)\n", iters);
    Rng R(seed);
    fuzz_struct<DiscoverReply>("DiscoverReply", R, iters, [](Rng& R) {
        DiscoverReply d; d.proto = R.u16(); d.port = R.u16(); d.name = R.str(255); d.players = R.u8();
        d.max_players = R.u8(); d.info = R.str(255); return d;
    });
    fuzz_struct<Hello>("Hello", R, iters, [](Rng& R) {
        Hello h; h.proto = R.u16(); h.build = R.u32(); h.name = R.str(255);
        h.ext = R.below(4) != 0; // without: a 0.7.2 HELLO
        h.caps = R.below(2) ? kMyCaps : R.u32();
        h.has_token = R.below(2); h.token = ((uint64_t)R.u32() << 32) | R.u32();
        return h;
    }, [](const Hello& h) { return (size_t)(2 + 4 + 1 + std::min<size_t>(h.name.size(), 255)); });
    fuzz_struct<Welcome>("Welcome", R, iters, [](Rng& R) {
        Welcome w; w.your_id = R.u8(); w.world = R.u32();
        for (int n = (int)R.below(R.below(8) ? 5 : 256), i = 0; i < n; i++) w.players.push_back({R.u8(), R.str(40)});
        w.ext = R.below(4) != 0;
        w.caps = R.below(2) ? kMyCaps : R.u32();
        return w;
    }, [](const Welcome& w) {
        size_t n = 1 + 4 + 1;
        for (size_t i = 0; i < w.players.size() && i < 255; i++)
            n += 1 + 1 + std::min<size_t>(w.players[i].name.size(), 255);
        return n;
    });
    hello_welcome_ext();
    fuzz_struct<Reject>("Reject", R, iters, [](Rng& R) { Reject x; x.reason = R.u8(); x.text = R.str(255); return x; });
    // the version (CAP_VERSION, 0.7.4) is appended: a prefix with id and name is a 0.7.3 PLAYER_JOIN
    fuzz_struct<PlayerJoin>(
        "PlayerJoin", R, iters,
        [](Rng& R) { PlayerJoin x; x.p = {R.u8(), R.str(255), R.below(2) ? R.str(60) : std::string()}; return x; },
        [](const PlayerJoin& x) { return (size_t)2 + x.p.name.size(); });
    fuzz_struct<PlayerLeave>("PlayerLeave", R, iters, [](Rng& R) { PlayerLeave x; x.id = R.u8(); x.reason = R.u8(); return x; });
    fuzz_struct<PlayerState>("PlayerState", R, iters, [](Rng& R) {
        PlayerState s; s.id = R.u8(); s.time_ms = R.u32(); s.pos = v3(R); s.rot = q4(R); s.vel = v3(R);
        s.flags = R.u8(); s.anim = R.u8(); s.health = R.f32(); s.max_health = R.f32(); s.species = R.u32();
        s.mode = R.u32(); s.world = R.u32(); return s;
    });
    fuzz_struct<Chat>("Chat", R, iters, [](Rng& R) { Chat c; c.id = R.u8(); c.text = R.str(4000); return c; });
    fuzz_struct<NpcSpawn>("NpcSpawn", R, iters, [](Rng& R) {
        NpcSpawn s; s.net_id = R.u16(); s.key[0] = R.u32(); s.key[1] = R.u32(); s.key[2] = R.u32(); s.scale = R.f32();
        s.flags = R.u8(); s.pos = v3(R); s.rot = q4(R); s.health = R.f32(); s.max_health = R.f32(); return s;
    });
    fuzz_struct<NpcState>("NpcState", R, iters, npc_state);
    fuzz_struct<NpcStateBatch>("NpcStateBatch", R, iters, [](Rng& R) {
        NpcStateBatch b; b.time_ms = R.u32();
        for (int n = (int)R.below(26), i = 0; i < n; i++) b.v.push_back(npc_state(R));
        return b;
    });
    fuzz_struct<DamageReq>("DamageReq", R, iters, [](Rng& R) {
        DamageReq d; d.net_id = R.u16(); d.damage = R.f32(); d.type = (int32_t)R.u32(); return d;
    });
    fuzz_struct<Damage>("Damage", R, iters, [](Rng& R) {
        Damage d; d.player = R.u8(); d.damage = R.f32(); d.type = (int32_t)R.u32(); d.attacker = R.u16(); return d;
    });
    fuzz_struct<AnimEvent>("AnimEvent", R, iters, [](Rng& R) {
        AnimEvent a; a.id = R.u8(); a.anim = R.u32(); a.block = (int32_t)R.u32(); a.flags = R.u8(); return a;
    });
    fuzz_struct<WorldOffer>("WorldOffer", R, iters, [](Rng& R) {
        WorldOffer o; o.world_id = R.u32(); o.total_size = R.u32(); o.file_count = R.u16(); o.planet = R.str(255); return o;
    });
    fuzz_struct<WorldFile>("WorldFile", R, iters, [](Rng& R) {
        WorldFile f; f.index = R.u16(); f.size = R.u32(); f.path = R.str(255); return f;
    });
    fuzz_struct<WorldChunk>("WorldChunk", R, iters, [](Rng& R) {
        WorldChunk c; c.index = R.u16(); c.offset = R.u32(); c.data.resize(R.below(1100));
        for (auto& b : c.data) b = R.u8();
        return c;
    });
}

// ---------------------------------------------------------------- other portable decoders
// MSG_LOBBY (lobby.h), MSG_CREATION pieces (validate.h chunk_add), the sender time after a snapshot
// chunk (interp.h snapshot_time) and the UTF-8 helpers every received name and chat text goes through
static void fuzz_misc(uint64_t seed, int iters)
{
    printf("lobby table, creation pieces, snapshot time, UTF-8 text\n");
    Rng R(seed);
    int lobbies = 0, completes = 0;
    for (int i = 0; i < iters; i++) {
        std::vector<LobbyEntry> pl;
        for (int n = (int)R.below(R.below(8) ? 5 : 256), k = 0; k < n; k++)
            pl.push_back({R.u8(), R.below(2) != 0, (int)R.below(70000) - 1, R.u8()});
        Writer w;
        write_lobby(w, pl, R.below(2) != 0);
        auto m = R.below(3) ? mutate(R, w.data) : w.data;
        for (int fix = 0; fix < 2; fix++) {
            Exact e(m);
            Reader r(e.data(), e.n);
            std::vector<LobbyEntry> out;
            bool roles = false;
            if (read_lobby(r, out, roles, fix != 0)) {
                lobbies++;
                CHECK(out.size() <= 255 && 4 * out.size() + 1 <= e.n, "lobby: %zu entries from %zu bytes", out.size(), e.n);
            }
        }
    }
    for (int i = 0; i < iters / 8; i++) { // one buffer in pieces: in order, shuffled, repeated, forged
        ChunkRx rx;
        const size_t chunk = 1 + R.below(R.below(2) ? 900 : 16);
        const uint32_t max_total = 1u << 16;
        uint32_t total = R.below(R.below(4) ? 5000 : max_total + 2), id = R.below(3);
        for (int k = 0; k < 40; k++) {
            uint32_t off = (uint32_t)(chunk * R.below(total / chunk + 2));
            size_t n = total > off ? std::min<size_t>(chunk, total - off) : 0;
            if (R.below(5) == 0)
                n = R.below((uint32_t)chunk * 2 + 1);
            if (R.below(8) == 0)
                off = R.u32();
            uint32_t t = R.below(10) ? total : R.u32();
            std::vector<uint8_t> piece(n);
            for (auto& b : piece)
                b = R.u8();
            Exact e(piece);
            Chunk c = chunk_add(rx, R.below(10) ? id : R.u32(), t, off, e.data(), e.n, chunk, max_total);
            if (rx.started) {
                CHECK(rx.data.size() == rx.total && rx.have <= rx.total && rx.total <= max_total,
                      "chunk_add: total %u, have %u, %zu bytes", rx.total, rx.have, rx.data.size());
            }
            if (c == Chunk::Complete) {
                completes++;
                CHECK(rx.have == rx.total, "chunk_add: complete with %u of %u", rx.have, rx.total);
            }
        }
    }
    for (int i = 0; i < iters; i++) {
        std::vector<uint8_t> b(R.below(64));
        for (auto& x : b)
            x = R.u8();
        Exact e(b);
        Reader r(e.data(), e.n);
        r.skip(R.below(8));
        double t = snapshot_time(r, R.below(70));
        CHECK(t == -1 || (t >= 0 && t <= 4294967.3), "snapshot_time %f", t);
    }
    for (int i = 0; i < iters; i++) {
        std::string s = R.str(R.below(4) ? 40 : 300);
        if (R.below(2)) // mostly multi-byte text
            for (auto& c : s)
                c = (char)(0x80 | (uint8_t)c) ;
        std::string name = utf8::player_name(s);
        CHECK(utf8::count(name) <= utf8::kNameMax && name.size() <= s.size(), "player_name: %zu code points",
              utf8::count(name));
        for (unsigned char c : name)
            CHECK(c >= 0x20 && c != 0x7F, "player_name kept a control byte %02X", c);
        size_t max = R.below(300);
        size_t cut = utf8::cut_bytes(s, max);
        CHECK(cut <= s.size() && (s.size() <= max ? cut == s.size() : cut <= max), "cut_bytes %zu of %zu", cut, s.size());
        if (utf8::valid(s))
            CHECK(utf8::valid(s.substr(0, cut)), "cut_bytes cut a valid text inside a character");
        size_t n = R.below(30);
        CHECK(utf8::count(utf8::head(s, n)) <= n, "head");
        CHECK(utf8::columns(s) >= 0 && (size_t)utf8::columns(s) <= 2 * s.size(), "columns");
        Writer w;
        w.str8(s);
        w.str16(s);
        Reader r(w.data.data(), w.size());
        std::string a = r.str8(), b = r.str16();
        CHECK(r.ok() && r.left() == 0 && a.size() <= 255 && b == s.substr(0, b.size()), "str8/str16 of %zu bytes", s.size());
    }
    printf("  %d lobby tables (%d accepted), %d piece buffers (%d complete), %d snapshot times, %d texts\n", iters,
           lobbies, iters / 8, completes, iters, iters);
}

// ---------------------------------------------------------------- dbpf / RefPack
// a valid RefPack stream of random data: literal runs and back references; returns the plain bytes
static std::vector<uint8_t> refpack_encode_random(Rng& R, std::vector<uint8_t>& plain)
{
    plain.clear();
    std::vector<uint8_t> cmd;
    size_t target = 1 + R.below(R.below(4) ? 600 : 20000);
    auto put_lit = [&](size_t k) {
        for (size_t i = 0; i < k; i++) {
            uint8_t b = R.below(3) ? (uint8_t)('a' + R.below(4)) : R.u8();
            cmd.push_back(b);
            plain.push_back(b);
        }
    };
    auto put_copy = [&](size_t off, size_t len) {
        size_t from = plain.size() - off;
        for (size_t i = 0; i < len; i++)
            plain.push_back(plain[from + i]);
    };
    while (plain.size() < target) {
        uint32_t kind = plain.empty() ? 3 : R.below(4);
        if (kind == 0 && plain.size() + 3 >= 1) { // 2-byte code: off <= 1024, len 3..10, lit 0..3
            size_t lit = R.below(4), len = 3 + R.below(8);
            size_t off = 1 + R.below((uint32_t)std::min<size_t>(1024, plain.size() + lit));
            cmd.push_back((uint8_t)(((off - 1) >> 3 & 0x60) | ((len - 3) << 2) | lit));
            cmd.push_back((uint8_t)((off - 1) & 0xFF));
            put_lit(lit);
            put_copy(off, len);
        } else if (kind == 1) { // 3-byte code: off <= 16384, len 4..67, lit 0..3
            size_t lit = R.below(4), len = 4 + R.below(64);
            size_t off = 1 + R.below((uint32_t)std::min<size_t>(16384, plain.size() + lit));
            cmd.push_back((uint8_t)(0x80 | (len - 4)));
            cmd.push_back((uint8_t)((lit << 6) | ((off - 1) >> 8)));
            cmd.push_back((uint8_t)((off - 1) & 0xFF));
            put_lit(lit);
            put_copy(off, len);
        } else if (kind == 2) { // 4-byte code: off <= 131072, len 5..1028, lit 0..3
            size_t lit = R.below(4), len = 5 + R.below(1024);
            size_t off = 1 + R.below((uint32_t)std::min<size_t>(131072, plain.size() + lit));
            size_t o = off - 1, l = len - 5;
            cmd.push_back((uint8_t)(0xC0 | ((o >> 16 & 1) << 4) | ((l >> 8 & 3) << 2) | lit));
            cmd.push_back((uint8_t)(o >> 8 & 0xFF));
            cmd.push_back((uint8_t)(o & 0xFF));
            cmd.push_back((uint8_t)(l & 0xFF));
            put_lit(lit);
            put_copy(off, len);
        } else { // literal run: 4..112 bytes, a multiple of 4
            size_t k = 4 * (1 + R.below(28));
            cmd.push_back((uint8_t)(0xE0 | (k / 4 - 1)));
            put_lit(k);
        }
    }
    size_t tail = R.below(4); // stop code with 0..3 literals
    cmd.push_back((uint8_t)(0xFC | tail));
    put_lit(tail);
    std::vector<uint8_t> out;
    uint32_t size = (uint32_t)plain.size();
    if (size > 0xFFFFFF || R.below(4) == 0) {
        out = {0x90, 0xFB, (uint8_t)(size >> 24), (uint8_t)(size >> 16), (uint8_t)(size >> 8), (uint8_t)size};
    } else {
        out = {0x10, 0xFB, (uint8_t)(size >> 16), (uint8_t)(size >> 8), (uint8_t)size};
    }
    out.insert(out.end(), cmd.begin(), cmd.end());
    return out;
}

static void fuzz_refpack(uint64_t seed, int iters)
{
    printf("RefPack decoder\n");
    Rng R(seed);
    std::vector<std::vector<uint8_t>> corpus;
    int accepted = 0;
    for (int i = 0; i < 64; i++) {
        std::vector<uint8_t> plain, out;
        auto c = refpack_encode_random(R, plain);
        Exact e(c);
        CHECK(dbpf::refpack_decode(e.data(), e.n, out) && out == plain, "refpack round trip %d (%zu -> %zu)", i,
              c.size(), plain.size());
        // prefixes (sampled) are rejected; the decoder stops at the declared size, so a stream that
        // lacks only its bare stop code (0xFC, no literals) is complete and accepted
        for (size_t k = 0; k < c.size(); k += 1 + c.size() / 64) {
            if (k + 1 == c.size() && c.back() == 0xFC)
                continue;
            std::vector<uint8_t> pre(c.begin(), c.begin() + (long)k);
            Exact ep(pre);
            CHECK(!dbpf::refpack_decode(ep.data(), ep.n, out), "refpack prefix %zu/%zu accepted", k, c.size());
        }
        corpus.push_back(c);
    }
    for (int i = 0; i < iters; i++) {
        auto m = mutate(R, corpus[R.below((uint32_t)corpus.size())]);
        Exact e(m);
        std::vector<uint8_t> out;
        if (dbpf::refpack_decode(e.data(), e.n, out))
            accepted++;
        CHECK(out.size() <= (64u << 20), "refpack output too big");
    }
    printf("  %d round trips, %d mutations (%d accepted)\n", (int)corpus.size(), iters, accepted);
}

static std::vector<uint8_t> random_package(Rng& R)
{
    std::vector<dbpf::Record> recs;
    for (int n = 1 + (int)R.below(6), i = 0; i < n; i++) {
        dbpf::Record rec;
        rec.key = {R.u32(), R.below(2) ? 0x2B978C46u : R.u32(), R.below(2) ? 0x40626200u : R.u32()};
        rec.data.resize(R.below(300));
        for (auto& b : rec.data)
            b = R.u8();
        if (R.below(3) == 0 && rec.data.size() >= 12) { // a key for find_keys
            uint32_t t = 0x2B978C46u, g = 0x40626200u, inst = R.u32();
            size_t at = R.below((uint32_t)rec.data.size() - 11);
            memcpy(&rec.data[at], &t, 4);
            memcpy(&rec.data[at + 4], &inst, 4);
            memcpy(&rec.data[at + 8], &g, 4);
        }
        if (R.below(3) == 0) { // pollen_metadata names
            static const uint8_t mark[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE};
            rec.data.insert(rec.data.end(), mark, mark + 8);
            for (int s = 0; s < 2; s++) {
                uint32_t len = R.below(20);
                for (int b = 3; b >= 0; b--)
                    rec.data.push_back((uint8_t)(len >> (8 * b)));
                for (uint32_t k = 0; k < 2 * len; k++)
                    rec.data.push_back(R.u8());
            }
        }
        recs.push_back(rec);
    }
    return dbpf::write(recs);
}

// mutations that aim at the header fields the readers trust: index offset, size and count (DBPF
// 0x24 count, 0x28/0x40 offset, 0x2C size; DBBF 0x28 size, 0x38 offset)
static void mutate_header(Rng& R, std::vector<uint8_t>& f)
{
    if (f.size() < 96)
        return;
    static const size_t fields[] = {0x24, 0x28, 0x2C, 0x30, 0x38, 0x3C, 0x40};
    size_t at = fields[R.below(7)];
    uint32_t vals[] = {0, 1, 0x7FFFFFFF, 0xFFFFFFFF, 0xFFFFFFF0, (uint32_t)f.size(), (uint32_t)f.size() - 1, R.u32()};
    uint32_t v = vals[R.below(8)];
    memcpy(&f[at], &v, 4);
    if (R.below(4) == 0)
        memcpy(f.data(), "DBBF", 4);
    if (R.below(8) == 0) { // a DBBF 64-bit index offset near 2^64 (BUG-0208: offset + size wrapped)
        memcpy(f.data(), "DBBF", 4);
        uint64_t o = ~0ull - R.below(200);
        memcpy(&f[0x38], &o, 8);
    }
}

static void fuzz_dbpf(uint64_t seed, int iters)
{
    printf("DBPF packages (load, read, find_keys, metadata_names)\n");
    Rng R(seed);
    int loaded = 0, records = 0;
    for (int i = 0; i < iters; i++) {
        auto f = random_package(R);
        if (i % 4 != 0) // every 4th package stays valid: its records read back
            f = mutate(R, f);
        if (R.below(2))
            mutate_header(R, f);
        Exact e(f);
        std::vector<uint8_t> copy(e.data(), e.data() + e.n);
        dbpf::Package pkg;
        if (pkg.load(copy)) {
            loaded++;
            for (auto& en : pkg.entries()) {
                std::vector<uint8_t> out;
                pkg.read(en, out);
                records++;
            }
        }
        dbpf::IndexInfo info;
        if (dbpf::index_info(e.data(), e.n, info) && info.offset <= e.n && info.size <= e.n - info.offset) {
            std::vector<dbpf::Entry> entries;
            dbpf::parse_index(e.data() + info.offset, info.size, info, entries);
        }
        std::vector<uint32_t> inst;
        dbpf::find_keys(copy, 0x2B978C46u, 0x40626200u, inst);
        std::string a, n;
        dbpf::metadata_names(copy, a, n);
    }
    printf("  %d packages (%d loaded, %d records read)\n", iters, loaded, records);
}

// ---------------------------------------------------------------- update pack, release JSON
static void fuzz_pack(uint64_t seed, int iters)
{
    printf("update pack and release JSON\n");
    Rng R(seed);
    std::vector<std::vector<uint8_t>> corpus;
    for (int i = 0; i < 8; i++) {
        upd::Pack p;
        p.version = "0." + std::to_string(R.below(20)) + ".1";
        for (int n = 1 + (int)R.below(4), k = 0; k < n; k++) {
            upd::PackFile f;
            f.path = k == 0 ? "SporebinEP1/mods/coop.dll" : "SporebinEP1/mods/x" + std::to_string(k) + ".ini";
            f.flags = R.below(2);
            f.data.resize(R.below(200));
            for (auto& b : f.data)
                b = R.u8();
            p.files.push_back(f);
        }
        auto b = upd::write_pack(p);
        upd::Pack q;
        std::string err;
        CHECK(upd::parse_pack(b, q, err) && q.files.size() == p.files.size(), "pack round trip: %s", err.c_str());
        corpus.push_back(b);
    }
    int ok = 0;
    for (int i = 0; i < iters; i++) {
        auto m = mutate(R, corpus[R.below((uint32_t)corpus.size())]);
        upd::Pack q;
        std::string err;
        ok += upd::parse_pack(m, q, err);
    }
    std::string json = R"({"tag_name":"v0.7.3","html_url":"https://example.invalid/r","assets":[)"
                       R"({"name":"SporeCoop-0.7.3.pack","browser_download_url":"https://example.invalid/p"},)"
                       R"({"name":"SHA256SUMS.txt","browser_download_url":"https://example.invalid/s"}]})";
    for (int i = 0; i < iters; i++) {
        auto m = mutate(R, std::vector<uint8_t>(json.begin(), json.end()));
        upd::Release rel;
        upd::parse_release(std::string(m.begin(), m.end()), rel);
        upd::sums_lookup(std::string(m.begin(), m.end()), "SporeCoop-0.7.3.pack");
        upd::compare_versions(std::string(m.begin(), m.begin() + (long)std::min<size_t>(m.size(), 12)), "0.7.3");
    }
    printf("  %d packs (%d accepted), %d JSON answers\n", iters, ok, iters);
}

// ---------------------------------------------------------------- Connection::on_packet
static void fuzz_connection(uint64_t seed, int iters)
{
    printf("random datagrams into Connection::on_packet\n");
    Rng R(seed);
    // valid packets of a real sender, then mutated
    Connection tx;
    tx.conn_id = 0x1234567;
    std::vector<std::vector<uint8_t>> corpus;
    std::vector<uint8_t> pkt;
    for (int i = 0; i < 200; i++) {
        for (int n = (int)R.below(6), k = 0; k < n; k++) {
            Writer w;
            for (int b = (int)R.below(R.below(8) ? 40 : 1100); b > 0; b--)
                w.u8(R.u8());
            if (R.below(2))
                tx.send_reliable((uint8_t)R.below(60), w);
            else
                tx.send_unreliable((uint8_t)R.below(60), w);
        }
        if (tx.build_packet(1000 + i * 300, pkt))
            corpus.push_back(pkt);
    }
    int accepted = 0, delivered = 0;
    Connection rx;
    rx.conn_id = tx.conn_id;
    for (int i = 0; i < iters; i++) {
        std::vector<uint8_t> m;
        if (R.below(10) == 0) { // garbage after a valid header prefix
            m.assign(corpus[0].begin(), corpus[0].begin() + 10);
            for (int b = (int)R.below(1300); b > 0; b--)
                m.push_back(R.u8());
        } else
            m = mutate(R, corpus[R.below((uint32_t)corpus.size())]);
        Exact e(m);
        bool ok = rx.on_packet(e.data(), e.n, 5000 + i, [&](uint8_t, Reader& r) {
            // a handler reads the whole payload (ASan checks the bytes are there: inside the datagram,
            // or a copy of an out-of-order or joined reliable message), never more than a message holds
            CHECK(r.left() <= kMaxReliable, "payload of %zu bytes", r.left());
            std::vector<uint8_t> sink(r.left());
            r.raw(sink.data(), sink.size());
            delivered++;
        });
        accepted += ok;
        if (R.below(16) == 0)
            rx.build_packet(5000 + i, pkt);
        if (R.below(500) == 0) { // a fresh receiver now and then (state builds up otherwise)
            rx = Connection();
            rx.conn_id = tx.conn_id;
        }
    }
    printf("  %d datagrams (%d accepted, %d messages delivered)\n", iters, accepted, delivered);
}

// ---------------------------------------------------------------- Sessions over a fake transport
static uint32_t g_clock = 1000;
static uint32_t fake_clock() { return g_clock; }

struct FakeTransport : Transport {
    std::deque<std::pair<Endpoint, std::vector<uint8_t>>> in;
    std::vector<std::vector<uint8_t>> out;
    uint16_t port = 0;
    bool open(uint16_t p, std::string*) override { port = p ? p : 50000; return true; }
    void close() override {}
    bool send_to(const Endpoint&, const void* d, size_t n) override
    {
        out.emplace_back((const uint8_t*)d, (const uint8_t*)d + n);
        return true;
    }
    int recv_from(Endpoint& from, void* buf, size_t cap) override
    {
        if (in.empty())
            return 0;
        auto [ep, d] = std::move(in.front());
        in.pop_front();
        size_t n = std::min(cap, d.size());
        if (n)
            memcpy(buf, d.data(), n);
        from = ep;
        return (int)n;
    }
    uint16_t local_port() const override { return port; }
};

// a datagram of `conn` carrying the given messages (type, reliable, rel_id, payload)
struct Msg {
    uint8_t type;
    bool rel;
    uint16_t rel_id;
    std::vector<uint8_t> payload;
};
static std::vector<uint8_t> datagram(uint32_t conn, uint16_t seq, const std::vector<Msg>& msgs)
{
    Writer w;
    w.u32(kMagic); w.u16(kProtoVersion); w.u32(conn); w.u16(seq); w.u16(0);
    uint64_t z = 0;
    w.raw(&z, 8); w.raw(&z, 8);
    w.u8((uint8_t)msgs.size());
    for (auto& m : msgs) {
        w.u8(m.type); w.u8(m.rel); w.u16((uint16_t)m.payload.size());
        if (m.rel) w.u16(m.rel_id);
        w.raw(m.payload.data(), m.payload.size());
    }
    return w.data;
}

// random messages of the session's own types (and others) with random payloads: what the session
// layer parses itself (HELLO, WELCOME, REJECT, JOIN, LEAVE, STATE, CHAT, PING, BYE, ANIM)
static Msg random_msg(Rng& R, uint16_t& rel)
{
    static const uint8_t types[] = {MSG_HELLO, MSG_WELCOME, MSG_REJECT, MSG_PLAYER_JOIN, MSG_PLAYER_LEAVE,
                                    MSG_PLAYER_STATE, MSG_CHAT, MSG_PING, MSG_PONG, MSG_ANIM, 99};
    Msg m;
    m.type = types[R.below(sizeof types)];
    m.rel = R.below(4) != 0;
    m.rel_id = R.below(8) ? rel++ : R.u16();
    Writer w;
    switch (m.type) {
    case MSG_HELLO: { Hello h; h.name = R.str(20); h.proto = R.below(4) ? kProtoVersion : R.u16(); h.write(w); break; }
    case MSG_WELCOME: {
        Welcome wl; wl.your_id = R.u8();
        for (int n = (int)R.below(6), i = 0; i < n; i++) wl.players.push_back({R.u8(), R.str(10)});
        wl.write(w); break;
    }
    case MSG_PLAYER_JOIN: { PlayerJoin j; j.p = {R.u8(), R.str(10)}; j.write(w); break; }
    case MSG_PLAYER_LEAVE: { PlayerLeave l; l.id = R.u8(); l.write(w); break; }
    default: for (int n = (int)R.below(60); n > 0; n--) w.u8(R.u8());
    }
    m.payload = w.data;
    if (R.below(3) == 0)
        m.payload = mutate(R, m.payload);
    return m;
}

static void fuzz_session(uint64_t seed, int iters)
{
    printf("random datagrams into a host and a guest Session\n");
    Rng R(seed);
    int joins = 0, leaves = 0, peak = 0;
    for (int role = 0; role < 2; role++) {
        FakeTransport tr;
        Session s;
        s.set_transport(&tr);
        s.clock = fake_clock;
        s.timeout_ms = 3000;
        std::string err;
        s.cb.player_join = [&](const PlayerInfo&) { joins++; };
        s.cb.player_leave = [&](uint8_t, uint8_t) { leaves++; };
        std::vector<uint32_t> conns = {1, 2, 3, 4};
        std::vector<uint16_t> rel(64, 0), seq(64, 0);
        auto start = [&] {
            if (role == 0)
                CHECK(s.host(kDefaultPort, "host", err), "host: %s", err.c_str());
            else
                CHECK(s.join("127.0.0.1", "guest", err), "join: %s", err.c_str());
        };
        start();
        uint32_t guest_conn = 0;
        for (int i = 0; i < iters; i++) {
            g_clock += R.below(50);
            if (s.role() == Role::None) { // a guest left (BYE, REJECT, timeout): again
                start();
                tr.out.clear();
            }
            if (role == 1 && tr.out.size() && Connection::looks_valid(tr.out[0].data(), tr.out[0].size()))
                guest_conn = Connection::peek_conn(tr.out[0].data(), tr.out[0].size());
            int k = (int)R.below((uint32_t)conns.size());
            uint32_t conn = role == 1 ? (R.below(8) ? guest_conn : R.u32()) : (R.below(16) ? conns[k] : R.u32());
            Endpoint from{0x0100007F, (uint16_t)(role == 1 ? kDefaultPort : 40000 + k)};
            if (R.below(10) == 0)
                from.port = (uint16_t)(40000 + R.below(20));
            std::vector<Msg> msgs;
            for (int n = (int)R.below(4); n > 0; n--)
                msgs.push_back(random_msg(R, rel[k + 4 * role]));
            auto d = datagram(conn, seq[k]++, msgs);
            if (R.below(4) == 0)
                d = mutate(R, d);
            if (R.below(50) == 0) { // a LAN discovery query
                d.assign(6, 0);
                uint32_t q = kDiscoverQuery;
                memcpy(d.data(), &q, 4);
            }
            tr.in.emplace_back(from, d);
            s.update();
            peak = std::max(peak, (int)s.peer_count());
            CHECK(s.peer_count() <= 17, "%zu peers", s.peer_count());
            CHECK(s.players().size() <= 64, "%zu players", s.players().size());
            tr.out.clear();
        }
        s.leave("end");
    }
    printf("  %d datagrams each, joins=%d leaves=%d, peak peers=%d\n", iters, joins, leaves, peak);
}

#ifdef FUZZ_LIBFUZZER
// make fuzz: one target for all decoders, the first byte picks the decoder
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;
    std::vector<uint8_t> v(data + 1, data + size);
    Reader r(v.data(), v.size());
    switch (data[0] % 12) {
    case 0: { Hello x; x.read(r); break; }
    case 1: { Welcome x; x.read(r); break; }
    case 2: { PlayerState x; x.read(r); break; }
    case 3: { NpcSpawn x; x.read(r); break; }
    case 4: { NpcStateBatch x; x.read(r); break; }
    case 5: { WorldChunk x; x.read(r); break; }
    case 6: { DiscoverReply x; x.read(r); break; }
    case 7: { std::vector<uint8_t> out; dbpf::refpack_decode(v.data(), v.size(), out); break; }
    case 8: { dbpf::Package p; if (p.load(v)) for (auto& e : p.entries()) { std::vector<uint8_t> o; p.read(e, o); } break; }
    case 9: { std::vector<uint32_t> i; dbpf::find_keys(v, 0x2B978C46u, 0x40626200u, i); std::string a, b; dbpf::metadata_names(v, a, b); break; }
    case 10: { upd::Pack p; std::string e; upd::parse_pack(v, p, e); break; }
    case 11: {
        static Connection rx;
        rx.conn_id = v.size() >= 10 ? Connection::peek_conn(v.data(), v.size()) : 0;
        rx.on_packet(v.data(), v.size(), 1000, [](uint8_t, Reader& pr) { std::vector<uint8_t> s(pr.left()); pr.raw(s.data(), s.size()); });
        break;
    }
    }
    return 0;
}
#else
int main(int argc, char** argv)
{
    uint64_t seed = 1;
    int iters = 6000; // per codec: ~100k struct mutations in all
    if (argc == 2 && !strcmp(argv[1], "--nofix")) { // the code before the bugs this test found
        dbpf::g_bounds_fix = false;
        printf("fixes OFF\n");
    }
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--seed"))
            seed = strtoull(argv[i + 1], nullptr, 0);
        else if (!strcmp(argv[i], "--iters"))
            iters = atoi(argv[i + 1]);
    }
    fuzz_protocol(seed, iters);
    fuzz_misc(seed + 6, iters);
    fuzz_refpack(seed + 1, iters);
    fuzz_dbpf(seed + 2, iters);
    fuzz_pack(seed + 3, iters / 2);
    fuzz_connection(seed + 4, iters * 5);
    fuzz_session(seed + 5, iters * 2);
    printf(g_fail ? "fuzz_decoders: FAILED (%d)\n" : "fuzz_decoders: ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
#endif
