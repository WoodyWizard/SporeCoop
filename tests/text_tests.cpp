// Host-side unit tests of the text handling: names, net strings, notification lines, overlay widths
// (BUG-0129, BUG-0159, BUG-0164). `text_tests --old` switches the fixes off: every check of a fixed
// bug must then fail.
#include <cstdio>
#include <cstring>
#include <string>

#include "net/buffer.h"
#include "net/utf8.h"
#include "notify_text.h"
#include "trace_rate.h"

static int g_fail;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

static std::string rep(const char* s, int n)
{
    std::string r;
    for (int i = 0; i < n; i++)
        r += s;
    return r;
}

// BUG-0164: a str8 / str16 text longer than its limit is cut between two characters
static void test_str_cut()
{
    printf("net strings: never cut inside a UTF-8 character (BUG-0164)\n");
    std::string name = rep("\xD0\x96", 130); // 130 x "Ж": 260 bytes
    net::Writer w;
    w.str8(name);
    w.str16(rep("\xE5\xA4\xAA", 1500)); // 4500 bytes of "太"
    net::Reader r(w.data.data(), w.data.size());
    std::string a = r.str8(), b = r.str16();
    CHECK(r.ok());
    CHECK(utf8::valid(a));
    CHECK(utf8::valid(b));
    CHECK(a.size() == 254 && b.size() == 3999);
}

// BUG-0164: names are capped like the co-op window does it (24 characters)
static void test_names()
{
    printf("names: 24 characters, no control characters (BUG-0164)\n");
    CHECK(utf8::player_name(rep("\xD0\x96", 130)) == rep("\xD0\x96", 24));
    CHECK(utf8::player_name("Bob\n\x01") == "Bob");
    CHECK(utf8::player_name("\xE5\xA4\xAA\xE9\x83\x8E") == "\xE5\xA4\xAA\xE9\x83\x8E");
}

// BUG-0159: the overlay centres by letter cells, not bytes
static void test_width()
{
    printf("overlay width: letter cells, not bytes (BUG-0159)\n");
    int (*width)(const std::string&) = utf8::g_cut_fix ? utf8::overlay_width : [](const std::string& s) {
        return (int)s.size() * 9; // the code before
    };
    CHECK(width("Ivan") == width("\xD0\x98\xD0\xB2\xD0\xB0\xD0\xBD")); // "Иван"
    CHECK(width("\xE2\x96\xBA Bob") == 5 * 9);                          // "► Bob"
    CHECK(width("\xE5\xA4\xAA\xE9\x83\x8E") == 4 * 9);                  // "太郎": two wide characters
}

// BUG-0129: notification lines by characters, valid UTF-8; chat never guessed from its words
static void test_notify_lines()
{
    printf("notification lines: by characters, never inside one (BUG-0129)\n");
    std::string ru = "<Ivan> " + rep("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 ", 8) + "\xD0\xB4\xD0\xB0"; // 65 characters
    size_t shown = 0;
    bool ok = true;
    for (auto& l : notify_text::lines_of(ru)) {
        ok &= utf8::valid(l);
        shown += utf8::count(l);
    }
    CHECK(ok);
    CHECK(shown * 10 >= utf8::count(ru) * 9); // at least 90 % shown (all 65 fit two lines of 46)
    std::string cjk = "<Li> " + rep("\xE5\xA4\xAA", 30);
    ok = true;
    for (auto& l : notify_text::lines_of(cjk))
        ok &= utf8::valid(l);
    CHECK(ok);
    // system lines with a colon are not chat; chat has its kind given by the caller
    CHECK(std::string(notify_text::kind_of("Offer sent: Bob: 5 spice")) != "chat");
    // the co-op window's chat: every part of a long line is kept
    std::string all;
    for (auto& l : notify_text::wrap(ru, 20))
        all += l + " ";
    CHECK(utf8::count(all) >= utf8::count(ru));
}

// the support log's rate limit (trace.h): a burst in full, the rest counted, summed up per window
static void test_trace_rate()
{
    printf("trace rate limit\n");
    trace::RateLimit r(10.0);
    int written = 0;
    for (int i = 0; i < 50; i++)
        written += r.allow("eat: own", 1.0 + i * 0.1, 5);
    written += r.allow("eat: sent", 2.0, 5);
    CHECK(written == 6);
    CHECK(r.take_summary(5.0).empty()); // the window still runs
    auto s = r.take_summary(11.5);
    CHECK(s.size() == 1 && s[0].first == "eat: own" && s[0].second.first == 45 && s[0].second.second == 50);
    CHECK(r.allow("eat: own", 12.0, 5)); // a new window
    CHECK(r.take_summary(30.0).empty()); // nothing dropped in it
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--old")) {
        utf8::g_cut_fix = false;
        notify_text::g_fix = false;
        printf("text fixes OFF\n");
    }
    test_str_cut();
    test_names();
    test_width();
    test_notify_lines();
    test_trace_rate();
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
