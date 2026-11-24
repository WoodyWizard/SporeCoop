// Decision 8Q A: the galaxy traffic's encryption (mods/coop/net/galaxy_crypto.h). Native, no game.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "net/galaxy_crypto.h"

using namespace net::galaxy::crypto;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

// a game and a server that went through the exchange
static bool pair(Channel& game, Channel& server, KeyPair& st, uint8_t seen_static[32])
{
    KeyPair eph;
    if (!eph.make())
        return false;
    std::vector<uint8_t> q = keyx_question(eph), a;
    if (q.size() != kKeyxHelloBytes || !keyx_server(st, q.data(), q.size(), a, server))
        return false;
    if (a.size() > q.size()) // (never bigger than the question)
        return false;
    return keyx_game(eph, a.data(), a.size(), seen_static, game);
}

static void test_exchange_and_seal()
{
    KeyPair st;
    CHECK(st.make());
    Channel g, s;
    uint8_t seen[32];
    CHECK(pair(g, s, st, seen));
    CHECK(!memcmp(seen, st.pub, 32));
    CHECK(!memcmp(g.tx, s.rx, 32) && !memcmp(g.rx, s.tx, 32) && memcmp(g.tx, g.rx, 32));
    const char* msg = "G\x01hello galaxy";
    std::vector<uint8_t> sealed, opened;
    CHECK(g.seal((const uint8_t*)msg, strlen(msg), sealed));
    CHECK(sealed.size() == strlen(msg) + kOverhead && sealed[0] == kSealed);
    const char* word = "hello";
    CHECK(std::search(sealed.begin(), sealed.end(), word, word + 5) == sealed.end()); // (the clear text is not in it)
    CHECK(s.open(sealed.data(), sealed.size(), opened));
    CHECK(opened.size() == strlen(msg) && !memcmp(opened.data(), msg, opened.size()));
    // the other way
    CHECK(s.seal((const uint8_t*)msg, strlen(msg), sealed));
    CHECK(g.open(sealed.data(), sealed.size(), opened));
    // a replay
    CHECK(!g.open(sealed.data(), sealed.size(), opened) && g.replays == 1);
    // a changed byte anywhere: refused
    for (size_t i = 1; i < sealed.size(); i += 7) {
        std::vector<uint8_t> t = sealed;
        t[i] ^= 0x40;
        Channel g2 = g;
        g2.rx_top = 0, g2.rx_bits = 0; // (not the replay window: the MAC)
        CHECK(!g2.open(t.data(), t.size(), opened));
    }
    // the sender's own key does not open its datagram (one key per direction)
    std::vector<uint8_t> mine;
    CHECK(g.seal((const uint8_t*)msg, strlen(msg), mine));
    Channel echo = g; // (its own datagram reflected back: the other direction's key does not open it)
    CHECK(!echo.open(mine.data(), mine.size(), opened));
}

static void test_window()
{
    KeyPair st;
    CHECK(st.make());
    Channel g, s;
    uint8_t seen[32];
    CHECK(pair(g, s, st, seen));
    std::vector<std::vector<uint8_t>> out(100);
    for (auto& d : out)
        CHECK(g.seal((const uint8_t*)"G\x08", 2, d));
    std::vector<uint8_t> p;
    CHECK(s.open(out[50].data(), out[50].size(), p)); // out of order: 51st first
    CHECK(s.open(out[10].data(), out[10].size(), p)); // 40 back: in the window
    CHECK(!s.open(out[10].data(), out[10].size(), p)); // twice: no
    CHECK(s.open(out[99].data(), out[99].size(), p));
    CHECK(!s.open(out[20].data(), out[20].size(), p)); // 79 back: out of the window
    CHECK(s.open(out[98].data(), out[98].size(), p));
}

static void test_other_server_and_off()
{
    KeyPair st, other;
    CHECK(st.make() && other.make());
    Channel g, s;
    uint8_t seen[32];
    CHECK(pair(g, s, other, seen));
    CHECK(memcmp(seen, st.pub, 32)); // (the game sees another server's key: the pin catches it)
    Channel off;
    std::vector<uint8_t> d;
    CHECK(!off.seal((const uint8_t*)"G", 1, d));
    // a short or foreign question or answer is no exchange
    KeyPair eph;
    CHECK(eph.make());
    std::vector<uint8_t> q = keyx_question(eph), a;
    Channel x;
    CHECK(!keyx_server(st, q.data(), q.size() - 1, a, x));
    q[2] = 99;
    CHECK(!keyx_server(st, q.data(), q.size(), a, x));
    // a low-order public key (all zero) gives no secret: refused
    std::vector<uint8_t> z(kKeyxHelloBytes, 0);
    z[0] = kKeyx, z[1] = kKeyxAsk, z[2] = kKeyxVersion;
    CHECK(!keyx_server(st, z.data(), z.size(), a, x));
    uint8_t h[32];
    CHECK(unhex(hex(st.pub, 32), h, 32) && !memcmp(h, st.pub, 32) && fingerprint(st.pub).size() == 16);
}

int main()
{
    test_exchange_and_seal();
    test_window();
    test_other_server_and_off();
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
