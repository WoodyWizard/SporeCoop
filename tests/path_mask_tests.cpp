// Host-side unit tests of the private-path masks of logs and reports (sporekit/path_mask.h, BUG-1124).
// `path_mask_tests --old` sets the masks up as SporeKit did before the fix (USERPROFILE read through the ANSI
// API: a Cyrillic name in the cp1251 code page, compared with the UTF-8 log text): the checks of a non-ASCII
// name must then fail.
#include <cstdio>
#include <cstring>
#include <string>

#include "../sporekit/path_mask.h"

static int g_fail;
static bool g_old;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

using sk::pathmask::Masks;

static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// "Иван" in UTF-8 and in the Windows-1251 code page (what GetEnvironmentVariableA returned on a Russian Windows)
static const char* kIvanUtf8 = "\xD0\x98\xD0\xB2\xD0\xB0\xD0\xBD";
static const char* kIvanCp1251 = "\xC8\xE2\xE0\xED";
static const char* kIVANUtf8 = "\xD0\x98\xD0\x92\xD0\x90\xD0\x9D"; // "ИВАН"

static Masks profile_masks()
{
    Masks m;
    m.add("C:\\Program Files (x86)\\GOG Galaxy\\Games\\Spore", "<game>");
    if (g_old)
        m.add(std::string("C:\\Users\\") + kIvanCp1251, "%USERPROFILE%");
    else {
        m.add(std::string("C:\\Users\\") + kIvanUtf8, "%USERPROFILE%");
        m.add(std::string("C:\\Users\\") + kIvanCp1251, "%USERPROFILE%"); // the ANSI form as well
    }
    return m;
}

static void test_cyrillic_profile()
{
    printf("path masks: a Cyrillic Windows user name (BUG-1124)\n");
    Masks m = profile_masks();
    std::string line = std::string("save folder C:\\Users\\") + kIvanUtf8 + "\\AppData\\Roaming\\Spore\\Games";
    std::string out = m.apply(line);
    CHECK(!has(out, kIvanUtf8));
    CHECK(out == "save folder %USERPROFILE%\\AppData\\Roaming\\Spore\\Games");
    // another case and forward slashes (a path as some code printed it)
    out = m.apply(std::string("c:/users/") + kIVANUtf8 + "/Documents/x.txt");
    CHECK(!has(out, kIVANUtf8));
    CHECK(out == "%USERPROFILE%/Documents/x.txt");
    // a line some code wrote with an A function: the ANSI bytes
    out = m.apply(std::string("loaded C:\\Users\\") + kIvanCp1251 + "\\a.dll");
    CHECK(out == "loaded %USERPROFILE%\\a.dll");
    // several in one line, the game folder (longer) inside nothing else
    out = m.apply(std::string("C:\\Users\\") + kIvanUtf8 + " and C:\\Program Files (x86)\\GOG Galaxy\\Games\\Spore\\Data");
    CHECK(out == "%USERPROFILE% and <game>\\Data");
}

static void test_ascii_unchanged()
{
    printf("path masks: ASCII paths as before (case, slashes, longest first, short ones ignored)\n");
    Masks m;
    m.add("C:\\users\\steamuser\\", "%USERPROFILE%");
    m.add("C:\\users\\steamuser\\Games\\Spore", "<game>");
    m.add("Z:\\", "<root>"); // too short: no private path
    CHECK(m.size() == 2);
    CHECK(m.apply("C:\\USERS\\SteamUser\\Games\\Spore\\SporebinEP1\\SporeApp.exe") == "<game>\\SporebinEP1\\SporeApp.exe");
    CHECK(m.apply("c:/users/steamuser/x") == "%USERPROFILE%/x");
    CHECK(m.apply("Z:\\tmp\\x") == "Z:\\tmp\\x");
    CHECK(m.apply("abc") == "abc");
    std::string same = "nothing private here, 0x0040 at 12:00";
    CHECK(m.apply(same) == same);
    m.add("c:\\USERS\\steamuser", "%USERPROFILE%"); // the same folder again
    CHECK(m.size() == 2);
}

static void test_other_letters()
{
    printf("path masks: Latin-1, Latin Extended-A, Greek user names without case\n");
    Masks m;
    m.add("C:\\Users\\J\xC3\xBCrgen \xC5\x81ukasz \xCE\xB1\xCE\xBB", "%USERPROFILE%"); // "Jürgen Łukasz αλ"
    CHECK(m.apply("C:\\USERS\\J\xC3\x9CRGEN \xC5\x82UKASZ \xCE\x91\xCE\x9B\\x") == "%USERPROFILE%\\x"); // "JÜRGEN łUKASZ ΑΛ"
    // a different name that only shares the first letters is not masked
    CHECK(m.apply("C:\\Users\\J\xC3\xBC") == "C:\\Users\\J\xC3\xBC");
}

static void test_invalid_utf8()
{
    printf("path masks: bytes that are no UTF-8 never match a letter, and a mask never starts inside one\n");
    Masks m;
    m.add("C:\\Users\\\xD0\x98", "%USERPROFILE%");
    std::string t = "C:\\Users\\\xC8 x"; // cp1251 "И" is not UTF-8 "И"
    CHECK(m.apply(t) == t);
    Masks n;
    n.add("\x98\\abc", "<x>"); // a continuation byte: no mask starts in the middle of "И"
    std::string u = "\xD0\x98\\abc";
    CHECK(n.apply(u) == u);
    std::string cut = "C:\\Users\\\xD0"; // a cut sequence at the end
    CHECK(m.apply(cut) == cut);
}

static void test_covered()
{
    printf("path masks: Documents inside the profile is covered by it, outside it is not\n");
    Masks m = profile_masks();
    CHECK(m.covered(std::string("C:\\Users\\") + kIvanUtf8 + "\\Documents") != g_old);
    CHECK(!m.covered("D:\\Docs"));
    CHECK(!m.covered(std::string("C:\\Users\\") + kIvanUtf8 + "X\\Documents"));
}

int main(int argc, char** argv)
{
    g_old = argc > 1 && !strcmp(argv[1], "--old");
    test_cyrillic_profile();
    test_ascii_unchanged();
    test_other_letters();
    test_invalid_utf8();
    test_covered();
    if (g_fail)
        printf("path_mask_tests: %d FAILED%s\n", g_fail, g_old ? " (--old)" : "");
    else
        printf("path_mask_tests: all passed%s\n", g_old ? " (--old)" : "");
    return g_fail ? 1 : 0;
}
