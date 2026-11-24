// Decision 8Q A: the galaxy traffic's encryption (net/galaxy_crypto.h). Monocypher is built here, in this one unit, so
// every build that has the net code (the game, the galaxy server, the unit tests) has it without a C step.
#include "galaxy_crypto.h"

#include <cstdio>
#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "../../../third_party/monocypher/monocypher.c"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#define GALAXY_HAVE_GETRANDOM 1
#endif
#endif

namespace net::galaxy::crypto {

bool random_bytes(void* out, size_t n)
{
#ifdef _WIN32
    // RtlGenRandom (advapi32 SystemFunction036): the system's CSPRNG, no extra import library
    using GenFn = BOOLEAN(WINAPI*)(PVOID, ULONG);
    static GenFn gen = [] {
        HMODULE m = LoadLibraryA("advapi32.dll");
        return m ? (GenFn)(void*)GetProcAddress(m, "SystemFunction036") : nullptr;
    }();
    return gen && n <= 0xFFFFFFFFu && gen(out, (ULONG)n);
#else
    uint8_t* p = (uint8_t*)out;
    size_t got = 0;
#ifdef GALAXY_HAVE_GETRANDOM
    while (got < n) {
        const ssize_t r = getrandom(p + got, n - got, 0);
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    if (got == n)
        return true;
#endif
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return false;
    got = 0;
    while (got < n) {
        const ssize_t r = ::read(fd, p + got, n - got);
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    ::close(fd);
    return got == n;
#endif
}

bool KeyPair::make()
{
    if (!random_bytes(secret, sizeof secret))
        return false;
    from_secret();
    return true;
}

void KeyPair::from_secret() { crypto_x25519_public_key(pub, secret); }

void KeyPair::wipe()
{
    crypto_wipe(secret, sizeof secret);
    crypto_wipe(pub, sizeof pub);
}

void Channel::wipe()
{
    crypto_wipe(tx, sizeof tx);
    crypto_wipe(rx, sizeof rx);
    on = false;
    tx_counter = rx_top = rx_bits = 0;
}

static void nonce_of(uint64_t counter, uint8_t nonce[24])
{
    memset(nonce, 0, 24);
    for (int i = 0; i < 8; i++)
        nonce[i] = (uint8_t)(counter >> (8 * i));
}

bool Channel::seal(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
    if (!on)
        return false;
    const uint64_t counter = ++tx_counter; // (from 1: 0 never goes out)
    out.resize(kOverhead + n);
    out[0] = kSealed;
    for (int i = 0; i < 8; i++)
        out[1 + i] = (uint8_t)(counter >> (8 * i));
    uint8_t nonce[24];
    nonce_of(counter, nonce);
    crypto_aead_lock(out.data() + kOverhead, out.data() + kHeader, tx, nonce, out.data(), kHeader, in, n);
    sealed++;
    return true;
}

bool Channel::open(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
    if (!on || n < kOverhead + 1 || in[0] != kSealed) {
        refused++;
        return false;
    }
    uint64_t counter = 0;
    for (int i = 0; i < 8; i++)
        counter |= (uint64_t)in[1 + i] << (8 * i);
    if (!counter) {
        refused++;
        return false;
    }
    if (counter <= rx_top) { // the window: the last 64 counters, each once
        const uint64_t back = rx_top - counter;
        if (back >= 64 || (rx_bits >> back) & 1) {
            replays++;
            return false;
        }
    }
    uint8_t nonce[24];
    nonce_of(counter, nonce);
    out.resize(n - kOverhead);
    if (crypto_aead_unlock(out.data(), in + kHeader, rx, nonce, in, kHeader, in + kOverhead, n - kOverhead) != 0) {
        out.clear();
        refused++;
        return false;
    }
    if (counter > rx_top) {
        const uint64_t ahead = counter - rx_top;
        rx_bits = ahead >= 64 ? 0 : rx_bits << ahead;
        rx_bits |= 1;
        rx_top = counter;
    } else
        rx_bits |= (uint64_t)1 << (rx_top - counter);
    opened++;
    return true;
}

static bool all_zero(const uint8_t* p, size_t n)
{
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++)
        acc |= p[i];
    return acc == 0;
}

// both sides: the two directions' keys from the public keys and the two shared secrets
static void derive(const uint8_t game_pub[32], const uint8_t server_static[32], const uint8_t server_eph[32],
                   const uint8_t s1[32], const uint8_t s2[32], uint8_t game_to_server[32], uint8_t server_to_game[32])
{
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 64);
    crypto_blake2b_update(&ctx, (const uint8_t*)kKeyxContext, strlen(kKeyxContext));
    crypto_blake2b_update(&ctx, game_pub, 32);
    crypto_blake2b_update(&ctx, server_static, 32);
    crypto_blake2b_update(&ctx, server_eph, 32);
    crypto_blake2b_update(&ctx, s1, 32);
    crypto_blake2b_update(&ctx, s2, 32);
    uint8_t k[64];
    crypto_blake2b_final(&ctx, k);
    memcpy(game_to_server, k, 32);
    memcpy(server_to_game, k + 32, 32);
    crypto_wipe(k, sizeof k);
}

std::vector<uint8_t> keyx_question(const KeyPair& eph)
{
    std::vector<uint8_t> q(kKeyxHelloBytes, 0);
    q[0] = kKeyx;
    q[1] = kKeyxAsk;
    q[2] = kKeyxVersion;
    memcpy(q.data() + 3, eph.pub, 32);
    return q;
}

bool keyx_game(const KeyPair& eph, const uint8_t* a, size_t n, uint8_t server_static[32], Channel& out)
{
    if (n < kKeyxAnswerBytes || a[0] != kKeyx || a[1] != kKeyxAnswer || a[2] != kKeyxVersion)
        return false;
    const uint8_t* st = a + 3;
    const uint8_t* ep = a + 35;
    uint8_t s1[32], s2[32];
    crypto_x25519(s1, eph.secret, st);
    crypto_x25519(s2, eph.secret, ep);
    const bool bad = all_zero(s1, 32) || all_zero(s2, 32); // (a low-order point: no secret)
    if (!bad) {
        out.wipe();
        derive(eph.pub, st, ep, s1, s2, out.tx, out.rx);
        out.on = true;
        memcpy(server_static, st, 32);
    }
    crypto_wipe(s1, 32);
    crypto_wipe(s2, 32);
    return !bad;
}

bool keyx_server(const KeyPair& server_static, const uint8_t* q, size_t n, std::vector<uint8_t>& answer, Channel& out)
{
    if (n < kKeyxHelloBytes || q[0] != kKeyx || q[1] != kKeyxAsk || q[2] != kKeyxVersion)
        return false;
    const uint8_t* game_pub = q + 3;
    KeyPair eph;
    if (!eph.make())
        return false;
    uint8_t s1[32], s2[32];
    crypto_x25519(s1, server_static.secret, game_pub);
    crypto_x25519(s2, eph.secret, game_pub);
    const bool bad = all_zero(s1, 32) || all_zero(s2, 32);
    if (!bad) {
        out.wipe();
        derive(game_pub, server_static.pub, eph.pub, s1, s2, out.rx, out.tx);
        out.on = true;
        answer.assign(kKeyxAnswerBytes, 0);
        answer[0] = kKeyx;
        answer[1] = kKeyxAnswer;
        answer[2] = kKeyxVersion;
        memcpy(answer.data() + 3, server_static.pub, 32);
        memcpy(answer.data() + 35, eph.pub, 32);
    }
    eph.wipe();
    crypto_wipe(s1, 32);
    crypto_wipe(s2, 32);
    return !bad;
}

std::string hex(const uint8_t* p, size_t n)
{
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

bool unhex(const std::string& s, uint8_t* out, size_t n)
{
    if (s.size() != 2 * n)
        return false;
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < n; i++) {
        const int h = v(s[2 * i]), l = v(s[2 * i + 1]);
        if (h < 0 || l < 0)
            return false;
        out[i] = (uint8_t)(h * 16 + l);
    }
    return true;
}

std::string fingerprint(const uint8_t pub[32])
{
    uint8_t h[8];
    crypto_blake2b(h, sizeof h, pub, 32);
    return hex(h, sizeof h);
}

} // namespace net::galaxy::crypto
