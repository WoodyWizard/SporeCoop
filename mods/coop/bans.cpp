#include "bans.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <sstream>
#include <vector>

#include "creations.h"
#include "fixes.h"
#include "identity.h"
#include "net/utf8.h"

namespace bans {

static const SkHost* H;
static SkMod* M;
static Ctx C;

// `coop.fix core.bans off`: banned players are let in again (the list is kept); kick still works
static bool g_fix = true;
COOP_FIX("core.bans", "BUG-9251", g_fix, "core: players the host banned are removed as soon as they join (decision 5I)");

struct Entry {
    uint64_t id = 0;
    std::string name, when;
};
static std::vector<Entry> g_list;
static bool g_loaded;
static double g_next_check;
static std::map<uint8_t, double> g_removed_at; // host: player id -> when a ban removed it (no second kick meanwhile)
static uint32_t g_kicks, g_bans, g_refused;

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }

static std::wstring file()
{
    std::wstring dir = creations::coop_dir();
    return dir.empty() ? std::wstring() : dir + L"\\bans.txt";
}

// bans.txt: one ban per line, "<identity, 16 hex digits>\t<name when banned>\t<date>"; lines starting with # are notes
static void load()
{
    if (g_loaded)
        return;
    std::wstring f = file();
    if (f.empty())
        return; // the profile folder is not known yet: later
    g_loaded = true;
    g_list.clear();
    FILE* in = _wfopen(f.c_str(), L"rb");
    if (!in)
        return; // (no bans yet)
    char buf[512];
    while (fgets(buf, sizeof buf, in)) {
        std::string line = buf;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ls(line);
        std::string hex, name, when;
        std::getline(ls, hex, '\t');
        std::getline(ls, name, '\t');
        std::getline(ls, when);
        uint64_t id = strtoull(hex.c_str(), nullptr, 16);
        if (id)
            g_list.push_back({id, name, when});
    }
    fclose(in);
    if (H)
        H->log(M, SK_INFO, "bans: %zu ban(s) in bans.txt", g_list.size());
}

static bool save()
{
    std::wstring f = file();
    if (f.empty())
        return false;
    std::wstring tmp = f + L".tmp";
    FILE* out = _wfopen(tmp.c_str(), L"wb");
    if (!out)
        return false;
    fputs("# SporeCoop Lineages: players this host banned (identity, name when banned, date). /unban <name> in the "
          "chat lets one in again.\n", out);
    bool ok = true;
    for (auto& e : g_list)
        ok &= fprintf(out, "%016llX\t%s\t%s\n", (unsigned long long)e.id, e.name.c_str(), e.when.c_str()) > 0;
    ok &= fclose(out) == 0;
    if (!ok)
        return false;
    return MoveFileExW(tmp.c_str(), f.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool banned(uint64_t id)
{
    load();
    return id && std::any_of(g_list.begin(), g_list.end(), [&](const Entry& e) { return e.id == id; });
}

static std::string lower(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string shown(const std::string& name) { return utf8::shown_name(name); }

// a player in the session by id or by name (the shown name, any case; a unique start of a name is enough); 0xFF none
static uint8_t find_player(const std::string& who, std::string& err)
{
    if (!C.sess) {
        err = "not in a session";
        return 0xFF;
    }
    const auto& ps = C.sess->players();
    char* end = nullptr;
    long n = strtol(who.c_str(), &end, 10);
    if (!who.empty() && end && !*end) {
        for (auto& p : ps)
            if (p.id == n)
                return p.id;
        err = "no player " + who;
        return 0xFF;
    }
    const std::string w = lower(who);
    std::vector<uint8_t> starts;
    for (auto& p : ps) {
        if (lower(p.name) == w || lower(shown(p.name)) == w)
            return p.id;
        if (!w.empty() && lower(shown(p.name)).rfind(w, 0) == 0)
            starts.push_back(p.id);
    }
    if (starts.size() == 1)
        return starts[0];
    err = starts.empty() ? "no player called " + who : "more than one player starts with " + who;
    return 0xFF;
}

static std::string today()
{
    time_t t = time(nullptr);
    tm lt{};
    localtime_s(&lt, &t);
    char b[16];
    strftime(b, sizeof b, "%Y-%m-%d", &lt);
    return b;
}

std::string kick(const std::string& who, bool ban)
{
    if (!is_host())
        return "Only the host can remove players.";
    std::string err;
    const uint8_t id = find_player(who, err);
    if (id == 0xFF)
        return err + ".";
    if (id == C.sess->my_id())
        return "You can't remove yourself (Close session ends the game for everyone).";
    const std::string name = shown(C.sess->name_of(id));
    if (ban) {
        const uint64_t ident = identity::of(id);
        if (!ident)
            return name + "'s game has not sent its identity yet: try again in a few seconds.";
        load();
        if (!banned(ident)) {
            g_list.push_back({ident, name, today()});
            if (!save())
                H->log(M, SK_WARN, "bans: bans.txt could not be written");
        }
        g_bans++;
    }
    const bool ok = C.sess->kick(id, ban ? "The host banned you from their games." : "The host removed you from the game.");
    if (!ok)
        return "Could not remove " + name + ".";
    g_kicks++;
    H->log(M, SK_INFO, "bans: player %u '%s' %s by the host", id, name.c_str(), ban ? "banned" : "kicked");
    const std::string line = name + (ban ? " was banned by the host." : " was removed by the host.");
    if (C.notice) {
        C.notice(line); // (every player's feed, the host's too)
        return "";      // (no second line for the chat command)
    }
    return line;
}

std::string unban(const std::string& who)
{
    load();
    if (g_list.empty())
        return "Nobody is banned.";
    const std::string w = lower(who);
    char* end = nullptr;
    unsigned long n = strtoul(who.c_str(), &end, 10);
    const bool number = !who.empty() && end && !*end && n >= 1 && n <= g_list.size();
    const uint64_t hex = strtoull(who.c_str(), nullptr, 16);
    for (size_t i = 0; i < g_list.size(); i++)
        if ((number && i + 1 == n) || lower(g_list[i].name) == w || (who.size() == 16 && hex == g_list[i].id)) {
            std::string name = g_list[i].name;
            g_list.erase(g_list.begin() + i);
            save();
            H->log(M, SK_INFO, "bans: '%s' unbanned", name.c_str());
            return name + " is no longer banned.";
        }
    return "No ban for " + who + " (/bans lists them).";
}

std::string list()
{
    load();
    if (g_list.empty())
        return "Nobody is banned.";
    std::string out = "Banned:";
    for (size_t i = 0; i < g_list.size(); i++)
        out += " " + std::to_string(i + 1) + ". " + g_list[i].name + " (" + g_list[i].when + ")";
    return out;
}

std::string status()
{
    return "bans: fix=" + std::to_string((int)g_fix) + " kicks=" + std::to_string(g_kicks) + " bans=" + std::to_string(g_bans) +
           " removed_at_join=" + std::to_string(g_refused);
}

bool chat_command(const std::string& t)
{
    if (t.empty() || t[0] != '/')
        return false;
    std::string cmd = t, arg;
    size_t sp = t.find(' ');
    if (sp != std::string::npos) {
        cmd = t.substr(0, sp);
        arg = t.substr(sp + 1);
        while (!arg.empty() && arg.back() == ' ')
            arg.pop_back();
    }
    cmd = lower(cmd);
    std::string line;
    if (cmd == "/kick" || cmd == "/ban")
        line = arg.empty() ? "Type " + cmd + " and the player's name." : kick(arg, cmd == "/ban");
    else if (cmd == "/unban")
        line = arg.empty() ? "Type /unban and the name (/bans lists them)." : unban(arg);
    else if (cmd == "/bans")
        line = list();
    else
        return false;
    if (C.feed && !line.empty())
        C.feed(line);
    return true;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
}

void tick(double now)
{
    if (now < g_next_check || !is_host())
        return;
    g_next_check = now + 1.0;
    if (!g_fix)
        return;
    for (auto& p : C.sess->players()) {
        if (p.id == C.sess->my_id())
            continue;
        const uint64_t ident = identity::of(p.id);
        auto it = g_removed_at.find(p.id);
        if (!ident || !banned(ident) || (it != g_removed_at.end() && now - it->second < 5))
            continue;
        g_removed_at[p.id] = now;
        if (!C.sess->kick(p.id, "The host banned you from their games."))
            continue; // (already on its way out: the /ban that just named it)
        g_refused++;
        const std::string name = shown(p.name);
        H->log(M, SK_INFO, "bans: player %u '%s' is banned here: removed", p.id, name.c_str());
        if (C.feed)
            C.feed(name + " is banned here and was removed.");
    }
}

} // namespace bans
