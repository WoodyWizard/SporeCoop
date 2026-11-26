// Host-side unit tests of the text handling: names, net strings, notification lines, overlay widths
// (BUG-0129, BUG-0159, BUG-0164). `text_tests --old` switches the fixes off: every check of a fixed
// bug must then fail.
#include <cstdio>
#include <cstring>
#include <string>

#include "i18n.h"
#include "net/buffer.h"
#include "net/utf8.h"
#include "notify_text.h"
#include "settings_core.h"
#include "tooltip_wrap.h"
#include "trace_rate.h"
#include "typed_text.h"

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
// BUG-1219: the Creature status block's "Pack: ..." line (99 letters, 891 px) went past the screen's right edge
static void test_wrap_fields()
{
    const std::string pack = "Pack: Alice close (regen)  |  rescue ready  |  joint hits 0, rescues 0  |  hits host 0, partners 0";
    auto l = utf8::wrap_fields(pack, "  |  ", 468);
    CHECK(l.size() == 2);
    for (auto& x : l)
        CHECK(utf8::overlay_width(x) <= 468);
    CHECK(l.size() == 2 && l[0] == "Pack: Alice close (regen)  |  rescue ready" &&
          l[1] == "joint hits 0, rescues 0  |  hits host 0, partners 0");
    CHECK(utf8::wrap_fields(pack, "  |  ", 2000).size() == 1 && utf8::wrap_fields(pack, "  |  ", 2000)[0] == pack);
    CHECK(utf8::wrap_fields("a long field", "  |  ", 9).size() == 1); // a field is never cut
    CHECK(utf8::wrap_fields("", "  |  ", 100).size() == 1);
    CHECK(utf8::wrap_fields("a  |  b", "  |  ", 9).size() == 2);
}

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

// BUG-1027: the co-op line typed by the mod under the game's input lock: the limit in UTF-16 units, no DEL, whole pairs
static void test_locked_typing()
{
    printf("locked typing: characters against the limit, no DEL, emoji whole (BUG-1027)\n");
    std::u16string q;
    for (int i = 0; i < 30; i++)
        q += u'\u0416'; // 30 x "Ж"
    q += u'\u007F';     // DEL
    q += u"\U0001F600"; // an emoji: a surrogate pair
    bool enter = false;
    unsigned typed = 0;
    std::string t = typed_text::apply("", q, 24, enter, typed); // the name line (24)
    CHECK(utf8::count(t) == 24 && t == rep("\xD0\x96", 24));
    t = typed_text::apply("", q, 120, enter, typed); // the chat line (120)
    CHECK(t == rep("\xD0\x96", 30) + "\xF0\x9F\x98\x80"); // all 30, no DEL, the emoji as one character
    CHECK(utf8::valid(t) && t.find("\xEF\xBF\xBD") == std::string::npos);
    std::u16string bs = u"\b";
    t = typed_text::apply(t, bs, 120, enter, typed); // Backspace takes the whole emoji
    CHECK(t == rep("\xD0\x96", 30));
    std::u16string cut = u"ab\U0001F600"; // a pair that does not fit (2 units left of 3) is not split
    t = typed_text::apply("", cut, 3, enter, typed);
    CHECK(t == "ab");
    std::u16string go = u"x\ry";
    enter = false;
    t = typed_text::apply("", go, 120, enter, typed);
    CHECK(enter && t == "x");
}

// BUG-1584: UTF-8 multi-line tooltip wrapper & 5-language settings tooltip completeness
static void test_tooltip_wrap()
{
    printf("settings tooltips: multi-line UTF-8 wrap & 5-language completeness (BUG-1584)\n");
    // Explicit newline header + ASCII word wrap
    bool trunc = false;
    auto lines = tooltip_wrap::wrap("Friendly fire (live)\n"
                                    "on: players can hurt each other in Together stages; off: no friendly fire",
                                    260.f, 6, &trunc);
    CHECK(!trunc && lines.size() >= 3 && lines[0] == "Friendly fire (live)");
    for (const auto& ln : lines) {
        CHECK(utf8::valid(ln));
        CHECK(tooltip_wrap::measure(ln) <= 260.f);
    }

    // Russian Cyrillic UTF-8 wrapping (never splits a multi-byte sequence)
    auto ru = tooltip_wrap::wrap("Общая дипломатия (до старта)\n"
                                 "Вкл: войны, союзы и репутация с ИИ-империями синхронизируются на всех игроков.",
                                 320.f, 6, &trunc);
    CHECK(!trunc && ru.size() >= 3 && ru[0] == "Общая дипломатия (до старта)");
    for (const auto& ln : ru) {
        CHECK(utf8::valid(ln));
        CHECK(tooltip_wrap::measure(ln) <= 320.f);
    }

    // CJK wrapping without spaces
    std::string cjk_text = "独立资金 (开局前)\n"
                           "开: 每位玩家拥有独立孢币账户、独立支付开销并收取各自城市收入，适合对手模式。关: 共享国库。";
    auto zh = tooltip_wrap::wrap(cjk_text, 240.f, 6, &trunc);
    CHECK(!trunc && zh.size() >= 3);
    for (const auto& ln : zh) {
        CHECK(utf8::valid(ln));
        CHECK(tooltip_wrap::measure(ln) <= 240.f);
    }

    // Truncation adds "..." within max_px
    auto cut = tooltip_wrap::wrap("Title\nWord1 Word2 Word3 Word4 Word5 Word6 Word7 Word8 Word9 Word10", 140.f, 2, &trunc);
    CHECK(trunc && cut.size() == 2 && cut[1].size() >= 3 && cut[1].substr(cut[1].size() - 3) == "...");
    CHECK(utf8::valid(cut[1]) && tooltip_wrap::measure(cut[1]) <= 140.f);

    // Single-line flattening for coop.ui state
    CHECK(tooltip_wrap::single_line("Title (live)\nLine 1\r\nLine 2") == "Title (live) | Line 1 | Line 2");

    // Every session setting in all 5 languages has a non-empty detailed description that fits 7 lines at 650 px without truncation
    const i18n::Lang langs[5] = {i18n::Lang::En, i18n::Lang::Ru, i18n::Lang::Hu, i18n::Lang::Es, i18n::Lang::Zh};
    for (int id = 0; id < settings::kCount; id++) {
        const auto& d = settings::kDefs[id];
        for (i18n::Lang l : langs) {
            std::string title = i18n::setting_label(d.key, d.label, l);
            std::string help = i18n::setting_help(d.key, d.help, l);
            CHECK(!title.empty() && utf8::valid(title));
            CHECK(!help.empty() && utf8::valid(help));
            if (l != i18n::Lang::En)
                CHECK(help != d.help); // every setting must actually be translated in RU/HU/ES/ZH
            bool t = false;
            auto w = tooltip_wrap::wrap(title + " (scope)\n" + help, 590.f, 8, &t);
            if (t || w.size() < 2)
                printf("  setting %s lang=%s lines=%zu trunc=%d\n", d.key, i18n::lang_code(l), w.size(), (int)t);
            CHECK(!t && w.size() >= 2 && w.size() <= 8);
            for (const auto& ln : w)
                CHECK(utf8::valid(ln));
        }
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--old")) {
        utf8::g_cut_fix = false;
        notify_text::g_fix = false;
        typed_text::g_utf16_fix = false;
        printf("text fixes OFF\n");
    }
    test_str_cut();
    test_names();
    test_width();
    test_notify_lines();
    test_wrap_fields();
    test_trace_rate();
    test_locked_typing();
    test_tooltip_wrap();
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
