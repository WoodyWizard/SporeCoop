#include "settings.h"

#include <windows.h>
#include <shlobj.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "net/protocol.h"

namespace settings {

static const SkHost* H;
static SkMod* M;
static Ctx C;

static Values g_defaults;     // a new world's values: the built-in defaults, coop.ini's keys over them
static Values g_cur;          // the host's / a player's alone values in force
static bool g_pending = false; // set while no world was open (the lobby): the next world takes them
static std::string g_open;    // the world whose values g_cur are ("" = none open)
static bool g_was_client = false;

static Values g_host;         // guest: the host's table
static bool g_host_sent[kCount];
static bool g_have_host = false, g_host_started = false;
static bool g_sent_started = false; // host: the started flag it sent last
static uint32_t g_msgs_in, g_msgs_out, g_loaded, g_saved;

#define SAY(...) H->out_printf(out, __VA_ARGS__)

static bool is_client() { return C.sess && C.sess->role() == net::Role::Client; }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }

static std::string world_key()
{
    if (!C.playing || !C.playing() || !C.world_name)
        return "";
    std::string n = C.world_name();
    if (n.empty() || n == "SaveGame")
        return "";
    for (auto& c : n)
        c = (char)tolower((unsigned char)c);
    return n;
}

static std::wstring file_of(const std::string& world)
{
    wchar_t p[MAX_PATH];
    if (world.empty() || FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) // profile-aware (hooked)
        return L"";
    std::wstring d = std::wstring(p) + L"\\Spore\\SporeCoop";
    CreateDirectoryW(d.c_str(), nullptr);
    d += L"\\settings";
    CreateDirectoryW(d.c_str(), nullptr);
    char n[16];
    snprintf(n, sizeof n, "%08X", net::crc32_update(0, (const uint8_t*)world.data(), world.size()));
    return d + L"\\" + std::wstring(n, n + strlen(n)) + L".txt";
}

static bool load(const std::string& world, Values& out)
{
    std::wstring f = file_of(world);
    FILE* fp = f.empty() ? nullptr : _wfopen(f.c_str(), L"rb");
    if (!fp)
        return false;
    std::string text;
    char b[512];
    size_t n;
    while ((n = fread(b, 1, sizeof b, fp)) > 0 && text.size() < 16384)
        text.append(b, n);
    fclose(fp);
    return parse_file(text, world, g_defaults, out);
}

static void save()
{
    if (g_open.empty())
        return;
    std::wstring f = file_of(g_open);
    FILE* fp = f.empty() ? nullptr : _wfopen(f.c_str(), L"wb");
    if (!fp) {
        H->log(M, SK_WARN, "settings: cannot write the file of world '%s'", g_open.c_str());
        return;
    }
    std::string s = format_file(g_open, g_cur);
    fwrite(s.data(), 1, s.size(), fp);
    fclose(fp);
    g_saved++;
}

static void broadcast()
{
    if (!is_host())
        return;
    net::Writer w;
    g_sent_started = started();
    write_msg(w, g_cur, g_sent_started);
    C.sess->broadcast_reliable(net::MSG_SETTINGS, w);
    g_msgs_out++;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    for (int i = 0; i < kCount; i++)
        if (kDefs[i].ini)
            g_defaults.v[i] = clamp_value(i, H->config_int(M, "coop", kDefs[i].ini, kDefs[i].def));
    g_cur = g_defaults;
    std::string d = diff_text(g_defaults, Values());
    H->log(M, SK_INFO, "settings: %d declared; coop.ini defaults %s", (int)kCount, d.empty() ? "as built in" : d.c_str());
}

void tick()
{
    if (is_client()) { // the host's world is the host's: nothing of ours opens or is stored
        g_was_client = true;
        g_open.clear();
        return;
    }
    if (g_was_client) {
        g_was_client = false;
        g_open.clear();
    }
    std::string k = world_key();
    if (k == g_open) {
        if (is_host() && started() != g_sent_started)
            broadcast();
        return;
    }
    if (k.empty()) { // the world closed (menu, load, editor): the values stay, the lobby may change them
        g_open.clear();
        if (is_host())
            broadcast();
        return;
    }
    Values stored, before = g_cur;
    bool has = load(k, stored);
    Opened o = on_world_open(g_pending, has, stored, g_defaults, g_cur);
    g_pending = false;
    g_open = k;
    if (o == Opened::Stored)
        g_loaded++;
    std::string d = diff_text(g_cur, Values());
    H->log(M, SK_INFO, "settings: world '%s' opened with %s values%s%s", k.c_str(),
           o == Opened::Pending ? "the lobby's" : o == Opened::Stored ? "its stored" : "a new world's (coop.ini)",
           d.empty() ? "" : ": ", d.c_str());
    save();
    if (is_host() || before != g_cur)
        broadcast();
}

void on_player_join(uint8_t id)
{
    if (!is_host() || id == C.sess->my_id())
        return;
    net::Writer w;
    write_msg(w, g_cur, started());
    C.sess->send_reliable_to(id, net::MSG_SETTINGS, w);
    g_msgs_out++;
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_SETTINGS || from != 0 || !is_client())
        return;
    Values got = g_cur;
    bool sent[kCount], st = false;
    if (!read_msg(r, got, sent, st)) {
        H->log(M, SK_WARN, "settings: a malformed table from the host dropped");
        return;
    }
    g_msgs_in++;
    bool first = !g_have_host;
    for (int i = 0; i < kCount; i++) { // (every message is the host's whole table)
        bool was = !first && g_host_sent[i];
        int old = was ? g_host.v[i] : g_cur.v[i];
        if (sent[i] && got.v[i] != old) {
            H->log(M, SK_INFO, "settings: the host's %s = %d (was %d%s)", kDefs[i].key, got.v[i], old, was ? "" : ", own");
            if (!first && C.feed)
                C.feed(std::string("The host set ") + kDefs[i].label + ": " + value_text(i, got.v[i]));
        }
        g_host_sent[i] = sent[i];
    }
    g_host = got;
    g_have_host = true;
    g_host_started = st;
}

void on_disconnect()
{
    if (g_have_host)
        H->log(M, SK_INFO, "settings: the host's table dropped (own values again)");
    g_have_host = false;
    g_host_started = false;
    for (auto& s : g_host_sent)
        s = false;
}

static int raw(Id id)
{
    if (is_client() && g_have_host && g_host_sent[id])
        return g_host.v[id];
    return g_cur.v[id];
}

int get(Id id)
{
    if (id < 0 || id >= kCount)
        return 0;
    int v = raw(id);
    if (kDefs[id].off_in_rivals && C.rivals && C.rivals())
        return 0;
    return v;
}

bool started()
{
    if (is_client())
        return g_host_started;
    return !world_key().empty();
}

bool from_host() { return is_client() && g_have_host; }
const Values& defaults() { return g_defaults; }
std::string diff() { return diff_text(is_client() && g_have_host ? g_host : g_cur, Values()); }

bool set(Id id, int value, bool force, std::string* why)
{
    if (id < 0 || id >= kCount)
        return false;
    Refusal rf = can_set(id, !is_client(), started());
    if (rf == Refusal::Guest || (rf != Refusal::None && !force)) {
        if (why)
            *why = refusal_text(rf);
        return false;
    }
    value = clamp_value(id, value);
    int old = g_cur.v[id];
    g_cur.v[id] = value;
    if (g_open.empty())
        g_pending = true; // the next world takes it
    else
        save();
    H->log(M, SK_INFO, "settings: %s = %d (was %d)%s%s", kDefs[id].key, value, old, force ? " (forced)" : "",
           g_open.empty() ? ", for the next world" : "");
    broadcast();
    return true;
}

static void list(SkOut* out)
{
    const char* who = is_host() ? "host" : is_client() ? (g_have_host ? "guest (the host's table)" : "guest (own values: no table from the host)") : "alone";
    SAY("settings: %s, world '%s', started=%d pending=%d in=%u out=%u loaded=%u saved=%u\n", who,
        g_open.empty() ? "-" : g_open.c_str(), (int)started(), (int)g_pending, g_msgs_in, g_msgs_out, g_loaded, g_saved);
    for (int i = 0; i < kCount; i++) {
        const Def& d = kDefs[i];
        int v = raw((Id)i), e = get((Id)i);
        SAY("%s=%d %s [%s, default %s%s] %s: %s%s - %s\n", d.key, v, value_text(i, v).c_str(),
            d.scope == Scope::Live ? "live" : "session", value_text(i, g_defaults.v[i]).c_str(),
            v != g_defaults.v[i] ? ", changed" : "", d.group, d.label, e != v ? " (off: Rivals mode)" : "", d.help);
    }
}

int command(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string sub = argc > 1 ? argv[1] : "";
    if (sub.empty() || sub == "list") {
        list(out);
        return 0;
    }
    if (sub == "get" && argc > 2) {
        int id = find(argv[2]);
        if (id < 0) {
            SAY("no setting '%s'", argv[2]);
            return 1;
        }
        SAY("%s=%d", argv[2], get((Id)id));
        return 0;
    }
    if ((sub == "set" || sub == "force") && argc > 3) {
        int id = find(argv[2]), v = 0;
        if (id < 0) {
            SAY("no setting '%s' (coop.settings lists them)", argv[2]);
            return 1;
        }
        if (!parse_value(id, argv[3], v)) {
            SAY("%s: '%s' is not a value (on, off, default or %d..%d)", argv[2], argv[3], kDefs[id].min, kDefs[id].max);
            return 1;
        }
        std::string why;
        if (!set((Id)id, v, sub == "force", &why)) {
            SAY("refused: %s: %s", kDefs[id].key, why.c_str());
            return 1;
        }
        SAY("%s=%d %s%s", kDefs[id].key, v, value_text(id, v).c_str(), g_open.empty() ? " (for the next world)" : "");
        return 0;
    }
    if (sub == "stored") { // tests: the stored file of the world open now
        Values v;
        if (g_open.empty() || !load(g_open, v)) {
            SAY("stored: none%s", g_open.empty() ? " (no world open)" : "");
            return 1;
        }
        SAY("stored '%s': %s", g_open.c_str(), diff_text(v, Values()).c_str());
        return 0;
    }
    SAY("coop.settings [list] | get <key> | set <key> on|off|default|<n> | force <key> <value> (tests: ignores the "
        "scope) | stored");
    return 1;
}

} // namespace settings
