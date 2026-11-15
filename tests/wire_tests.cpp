// Golden-byte tests of the game modules' message payloads (mods/coop/wire/*.h, C6 of docs/plan-0.7.15.md;
// docs/04-netcode.md#netcode-wire-structs). make test; built with AddressSanitizer.
//
// Per message kind:
//   * golden(): a fixed value written by the struct is exactly the bytes in the test (hex), the bytes the feature's
//     hand-written writer produced before the struct existed (its Writer calls are copied here verbatim as `legacy`),
//     and they read back to the same value with nothing left over;
//   * the C6 behaviour changes (a malformed payload the old hand parse used and the struct refuses) as rejects().
// A changed byte here is a wire change: 0.7.14 and 0.7.15 must still play together.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "wire/common.h"

static int g_fail = 0;
static int g_golden = 0;
#define CHECK(c, ...)                                                    \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s ", __FILE__, __LINE__, #c);         \
            printf(__VA_ARGS__);                                         \
            printf("\n");                                                \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static std::string hex(const std::vector<uint8_t>& v)
{
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) {
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}

template <class T>
static std::vector<uint8_t> enc(const T& x)
{
    net::Writer w;
    x.write(w);
    return w.data;
}

// `want`: the bytes as hex (spaces allowed between fields); `legacy`: the old hand-written writer's output for the
// same value (nullptr: the test only pins the hex)
template <class T>
static void golden(const char* name, const T& x, const char* want, const net::Writer* legacy)
{
    std::string w;
    for (const char* p = want; *p; p++)
        if (*p != ' ')
            w += *p;
    auto b = enc(x);
    CHECK(hex(b) == w, "%s: bytes %s, golden %s", name, hex(b).c_str(), w.c_str());
    if (legacy)
        CHECK(legacy->data == b, "%s: the old writer gave %s, the struct %s", name, hex(legacy->data).c_str(), hex(b).c_str());
    net::Reader r(b.data(), b.size());
    T y;
    CHECK(y.read(r), "%s: the golden bytes are rejected", name);
    CHECK(r.left() == 0, "%s: %zu bytes left", name, r.left());
    CHECK(enc(y) == b, "%s: read back and written again differs", name);
    g_golden++;
}

// a payload the struct refuses (a C6 behaviour change or an old check moved into read)
template <class T>
static void rejects(const char* name, const std::vector<uint8_t>& b)
{
    net::Reader r(b.data(), b.size());
    T y;
    CHECK(!y.read(r), "%s: accepted %s", name, hex(b).c_str());
}

// a payload the struct still takes (an older peer's shorter message, trailing bytes of a newer one)
template <class T>
static T accepts(const char* name, const std::vector<uint8_t>& b)
{
    net::Reader r(b.data(), b.size());
    T y;
    CHECK(y.read(r), "%s: rejected %s", name, hex(b).c_str());
    return y;
}

static std::vector<uint8_t> cut(std::vector<uint8_t> v, size_t n)
{
    v.resize(v.size() > n ? v.size() - n : 0);
    return v;
}

// ---------------------------------------------------------------- per feature
// (one function per wire header, in kind order)

int main()
{
    printf("wire golden bytes\n");
    printf(g_fail ? "wire_tests: FAILED (%d)\n" : "wire_tests: %d message kinds, ALL OK\n", g_fail ? g_fail : g_golden);
    return g_fail ? 1 : 0;
}
