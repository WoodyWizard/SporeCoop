// Host-side unit tests of the fix switch registry (mods/coop/fixes.cpp, BUG-0116, BUG-0214): make test.
// `fixes_tests <sources...>` also scans the mod's sources (rules at scan() below), so a new fix
// cannot miss `coop.fix`; `fixes_tests --names <sources...>` prints the registered names.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fixes.h"

static int g_fail;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

static bool g_a = true, g_b = true, g_c = false, g_u = true;
COOP_FIX("t.a", "BUG-0001", g_a, "first test switch");
COOP_FIX("t.b", "BUG-0002", g_b, "second test switch");
COOP_FIX_UNSAFE("t.session_end", "BUG-0003", g_u, "an unsafe test switch");

static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

static void test_registry()
{
    CHECK(fix::all().size() == 3);
    CHECK(fix::find("t.a") && fix::find("t.a")->on == &g_a && !strcmp(fix::find("t.a")->ticket, "BUG-0001"));
    CHECK(!fix::find("t.nope"));
    // a duplicate name or a null bool is refused, the first registration stays
    CHECK(!fix::reg("t.a", "BUG-0003", &g_c, "dup"));
    CHECK(!fix::reg("t.c", "BUG-0003", nullptr, "null"));
    CHECK(!fix::reg("t.c", "BUG-0003", &g_a, "the same bool under a second name")); // BUG-0214 R2
    CHECK(fix::duplicates() == 3 && fix::all().size() == 3 && fix::find("t.a")->on == &g_a);
    CHECK(has(fix::list(), "3 refused"));
    CHECK(fix::find("t.session_end")->unsafe_off && !fix::find("t.a")->unsafe_off);
    CHECK(has(fix::list(), "t.session_end on BUG-0003 unsafe  an unsafe test switch\n"));
    // set: the module's own bool changes (the old commands see it)
    CHECK(fix::set("t.a", false) && !g_a && g_b);
    CHECK(!fix::set("t.nope", false));
    CHECK(has(fix::list(), "3 fixes (1 off)") && has(fix::list(), "t.a off BUG-0001  first test switch\n"));
    CHECK(fix::set_all(true) == 3 && g_a && g_b && g_u);
    size_t skipped = 9;
    CHECK(fix::set_all(false, false, &skipped) == 2 && !g_a && !g_b && g_u && skipped == 1); // R4
    CHECK(fix::set_all(false, true, &skipped) == 3 && !g_u && skipped == 0);
    CHECK(fix::set("t.session_end", true) && g_u);
    fix::set_all(true);
    // R4: a single unsafe switch still goes off by name (its own twin test needs it)
    CHECK(fix::set("t.session_end", false) && !g_u);
    fix::set_all(true);
}

static void test_command()
{
    bool ok;
    CHECK(has(fix::command({}, &ok), "3 fixes (0 off)") && ok);
    CHECK(has(fix::command({"list"}, &ok), "t.b on BUG-0002") && ok);
    CHECK(has(fix::command({"t.b", "off"}, &ok), "t.b off") && ok && !g_b);
    CHECK(has(fix::command({"t.b", "1"}, &ok), "t.b on") && ok && g_b);
    CHECK(has(fix::command({"t.b", "0"}, &ok), "t.b off") && ok && !g_b);
    CHECK(has(fix::command({"t.b"}, &ok), "t.b off") && ok && !g_b); // shows, does not change
    CHECK(has(fix::command({"all", "on"}, &ok), "all 3 fixes on") && ok && g_a && g_b);
    CHECK(has(fix::command({"all", "off"}, &ok), "all 2 fixes off, 1 unsafe skipped") && ok && !g_a && !g_b && g_u);
    CHECK(has(fix::command({"all", "off", "--unsafe"}, &ok), "all 3 fixes off, 0 unsafe skipped") && ok && !g_u);
    CHECK(has(fix::command({"all", "on"}, &ok), "on") && g_a && g_b && g_u);
    CHECK(has(fix::command({"all", "on", "x"}, &ok), "usage") && !ok);
    CHECK(has(fix::command({"nope", "off"}, &ok), "unknown fix 'nope' (coop.fix list)") && !ok);
    // the names of the issues written before the registry get a hint (BUG-0214)
    CHECK(has(fix::command({"session_end", "off"}, &ok), "did you mean t.session_end") && !ok && g_u);
    CHECK(has(fix::command({"end", "off"}, &ok), "did you mean t.session_end") && !ok);
    CHECK(!has(fix::command({"nd", "off"}, &ok), "did you mean") && !ok);
    CHECK(has(fix::command({"t.a", "maybe"}, &ok), "usage") && !ok && g_a);
    CHECK(has(fix::command({"all"}, &ok), "usage") && !ok && g_a);
    CHECK(has(fix::command({"t.a", "off", "x"}, &ok), "usage") && !ok && g_a);
}

// The source scan (BUG-0116, extended by BUG-0214). Rules over the given files:
//  - every COOP_FIX / COOP_FIX_UNSAFE names a unique `<area>.<switch>` and BUG-NNNN tickets, and
//    every bool is registered under one name only;
//  - every FIX_SWITCH(x) and every member of `struct Fixes` (net/fixes.h) is registered, or listed in
//    fix_table.cpp's kUnitOnly[] (R6);
//  - a fix bool of the usual names (`g_fix_x`, `g_x_fix`, `x_fix`) defined `= true`, and any bool
//    defined `= true` / `{true}` at file scope whose line or the comment line above names a BUG-NNNN
//    or says "tests:" / "old behaviour" / "before the fix" (at struct scope: the last three only), is
//    registered or carries
//    `// not a lab switch: <reason>`.
struct Scan {
    int regs = 0;
    size_t defs = 0;
    std::vector<std::string> problems;
    std::vector<std::pair<std::string, bool>> names; // registered name, unsafe
};

static std::string last_part(const std::string& v)
{
    size_t p = v.find_last_of(":.");
    return p == std::string::npos ? v : v.substr(p + 1);
}

static Scan scan(const std::vector<std::pair<std::string, std::string>>& files)
{
    Scan r;
    std::regex reg_re(R"re(COOP_FIX(_UNSAFE)?\(\s*"([^"]*)",\s*"([^"]*)",\s*([A-Za-z0-9_:.]+))re");
    std::regex name_re(R"([a-z0-9_]+\.[a-z0-9_]+)"), ticket_re(R"(BUG-\d{4}( BUG-\d{4})*)");
    std::regex switch_re(R"(\bFIX_SWITCH\(\s*([A-Za-z_]\w*)\s*\))");
    std::regex unit_re(R"(&\s*([A-Za-z0-9_:]+)\s*,)");
    std::regex decl_re(R"(^\s{0,4}(?:static\s+|inline\s+)*bool\s)");
    std::regex true_re(R"(\b([A-Za-z_]\w*)\s*(?:=\s*true\b|\{\s*true\s*\}))");
    std::regex usual_re(R"(^(g_fix_[a-z_]+|[a-z_]+_fix|g_fix)$)");
    std::regex marker_re(R"(BUG-\d{4}|\btests:|\(tests\)|old behaviou?r|before the fix)");
    std::regex member_marker_re(R"(\btests:|\(tests\)|old behaviou?r|before the fix)"); // a struct's data fields cite tickets too
    std::set<std::string> names, regvars, regkeys, unit;
    struct Def {
        std::string var, where;
        bool strict; // a FIX_SWITCH or a Fixes member: must be registered whatever its name
    };
    std::vector<Def> defs;
    auto bad = [&](const std::string& s) { r.problems.push_back(s); };
    for (auto& [file, src] : files) {
        for (std::sregex_iterator it(src.begin(), src.end(), reg_re), end; it != end; ++it) {
            std::string name = (*it)[2], ticket = (*it)[3], var = (*it)[4];
            r.regs++;
            r.names.push_back({name, (*it)[1].matched});
            if (!std::regex_match(name, name_re) || !std::regex_match(ticket, ticket_re) || !names.insert(name).second)
                bad(file + ": COOP_FIX(\"" + name + "\", \"" + ticket + "\"): name <area>.<switch>, unique; ticket BUG-NNNN");
            // one name per bool: an unqualified bool is per file (static), a qualified one global
            std::string id = var.find("::") == std::string::npos && var.find('.') == std::string::npos ? file + ":" + var : var;
            if (!regvars.insert(id).second)
                bad(file + ": bool " + var + " registered under two names (" + name + ")");
            regkeys.insert(last_part(var));
            regkeys.insert(var);
        }
        if (file.find("fix_table.cpp") != std::string::npos) {
            size_t a = src.find("kUnitOnly[]");
            size_t b = a == std::string::npos ? a : src.find("};", a);
            if (a != std::string::npos && b != std::string::npos) {
                std::string body = src.substr(a, b - a);
                for (std::sregex_iterator it(body.begin(), body.end(), unit_re), end; it != end; ++it)
                    unit.insert(last_part((*it)[1]));
            }
        }
        std::istringstream ls(src);
        std::string line, prev;
        int n = 0;
        bool in_fixes = false;
        while (std::getline(ls, line)) {
            n++;
            std::string where = file + ":" + std::to_string(n);
            std::string code = line.substr(0, line.find("//"));
            if (code.find("#define FIX_SWITCH") == std::string::npos)
                for (std::sregex_iterator it(code.begin(), code.end(), switch_re), end; it != end; ++it)
                    defs.push_back({(*it)[1], where, true});
            if (std::regex_search(line, std::regex(R"(^\s*struct\s+Fixes\b)")))
                in_fixes = true;
            else if (in_fixes && line.rfind("};", 0) == 0)
                in_fixes = false;
            if (std::regex_search(line, decl_re) && line.find("COOP_FIX") == std::string::npos &&
                line.find("not a lab switch:") == std::string::npos) {
                const std::regex& m = line[0] == ' ' ? member_marker_re : marker_re;
                bool marked = std::regex_search(line, m) ||
                              (prev.find("//") != std::string::npos && std::regex_search(prev, m));
                for (std::sregex_iterator it(line.begin(), line.end(), true_re), end; it != end; ++it) {
                    std::string v = (*it)[1];
                    if (in_fixes)
                        defs.push_back({v, where, true});
                    else if (marked || std::regex_match(v, usual_re))
                        defs.push_back({v, where, false});
                }
            }
            prev = line;
        }
    }
    for (auto& d : defs) {
        bool reg = regkeys.count(d.var) > 0, u = unit.count(d.var) > 0;
        if (reg && u)
            bad(d.where + ": " + d.var + " is both registered and in kUnitOnly[]");
        else if (!reg && !u)
            bad(d.where + ": bool " + d.var + (d.strict ? " is a FIX_SWITCH / net::Fixes member" : " looks like a fix switch") +
                " but is not registered (COOP_FIX, fix_table.cpp, kUnitOnly[] or `// not a lab switch: <reason>`)");
    }
    r.defs = defs.size();
    return r;
}

// the scan itself on made-up sources: a regex change must not silently drop a rule
static void test_scan_self()
{
    auto probs = [](std::vector<std::pair<std::string, std::string>> f) { return scan(f).problems.size(); };
    CHECK(probs({{"a.cpp", "static bool g_x_fix = true;\nCOOP_FIX(\"a.x\", \"BUG-0001\", g_x_fix, \"x\");\n"}}) == 0);
    CHECK(probs({{"a.cpp", "static bool g_x_fix = true;\n"}}) == 1);                       // unregistered usual name
    CHECK(probs({{"a.cpp", "static bool g_keep = true; // BUG-0084: tests\n"}}) == 1);      // marker on the line
    CHECK(probs({{"a.cpp", "// tests: off = old\nstatic bool g_keep = true;\n"}}) == 1);    // marker above
    CHECK(probs({{"a.cpp", "static bool g_keep = true; // BUG-0084 not a lab switch: setup\n"}}) == 0);
    CHECK(probs({{"a.cpp", "    bool legacy{true}; // tests: the old pruning\n"}}) == 1);   // brace init, struct scope
    CHECK(probs({{"a.cpp", "    bool playing = true; // a state field (BUG-0131)\n"}}) == 0); // a member citing a ticket
    CHECK(probs({{"n.cpp", "FIX_SWITCH(g_any);\n"}}) == 1);                                  // FIX_SWITCH unregistered
    CHECK(probs({{"n.cpp", "FIX_SWITCH(g_any);\n"}, {"fix_table.cpp", "COOP_FIX(\"net.any\", \"BUG-0001\", ns::g_any, \"x\");\n"}}) == 0);
    CHECK(probs({{"n.cpp", "FIX_SWITCH(g_any);\n"}, {"fix_table.cpp", "const bool* const kUnitOnly[] = {\n    &ns::g_any, // x\n};\n"}}) == 0);
    CHECK(probs({{"f.h", "struct Fixes {\n    bool new_one = true;\n};\n"}}) == 1);            // Fixes member, any name
    CHECK(probs({{"a.cpp", "static bool g_y_fix = true;\nCOOP_FIX(\"a.y\", \"BUG-0001\", g_y_fix, \"y\");\n"
                           "COOP_FIX(\"a.z\", \"BUG-0002\", g_y_fix, \"z\");\n"}}) == 1);       // one bool, two names
    CHECK(probs({{"a.cpp", "COOP_FIX(\"Bad\", \"BUG-1\", g_q, \"q\");\n"}}) == 1);             // name and ticket form
    Scan u = scan({{"a.cpp", "COOP_FIX_UNSAFE(\"a.u\", \"BUG-0001\", g_u_fix, \"u\");\nstatic bool g_u_fix = true;\n"}});
    CHECK(u.problems.empty() && u.names.size() == 1 && u.names[0].second);
}

static std::vector<std::pair<std::string, std::string>> read_files(int argc, char** argv, int from)
{
    std::vector<std::pair<std::string, std::string>> files;
    for (int i = from; i < argc; i++) {
        std::ifstream f(argv[i]);
        std::stringstream ss;
        ss << f.rdbuf();
        files.push_back({argv[i], ss.str()});
    }
    return files;
}

// the sources of the mod: registrations well formed and unique, no fix bool left out
static void test_sources(int argc, char** argv)
{
    if (argc < 2)
        return;
    Scan r = scan(read_files(argc, argv, 1));
    for (auto& p : r.problems)
        printf("  FAIL %s\n", p.c_str());
    g_fail += (int)r.problems.size();
    printf("  sources: %d registrations, %zu fix bools defined\n", r.regs, r.defs);
    CHECK(r.regs >= 50);
}

int main(int argc, char** argv)
{
    // --names <sources...>: the registered names, one per line, "unsafe" after the unsafe ones
    // (tools/tests/coop_fix_registry.py counts the `coop.fix all off` answer with it)
    if (argc >= 2 && !strcmp(argv[1], "--names")) {
        Scan r = scan(read_files(argc, argv, 2));
        for (auto& [n, u] : r.names)
            printf("%s%s\n", n.c_str(), u ? " unsafe" : "");
        return r.problems.empty() ? 0 : 1;
    }
    test_registry();
    test_scan_self();
    test_command();
    test_sources(argc, argv);
    printf("fixes tests: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
