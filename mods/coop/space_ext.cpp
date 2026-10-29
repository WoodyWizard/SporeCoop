#include "space_ext.h"
#include <algorithm>
#include <cstdlib>
#include <map>

#include "coop_ui.h"
#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"

namespace spaceext {

static const SkHost* H;
static SkMod* M;
static const spacecoop::Ctx* C;
static std::vector<bool> g_on; // per feature: init succeeded

std::vector<Feature>& all()
{
    static std::vector<Feature> v; // a function-local static: safe from any static initializer
    return v;
}

bool add(Feature f)
{
    all().push_back(std::move(f));
    return true;
}

const SkHost* host() { return H; }
SkMod* mod() { return M; }
const spacecoop::Ctx& ctx() { return *C; }
bool connected() { return C && C->sess && C->sess->connected(); }
bool is_host() { return connected() && C->sess->role() == net::Role::Host; }
uint8_t my_id() { return C && C->sess ? C->sess->my_id() : 0; }
std::string name_of(uint8_t id) { return C && C->sess ? C->sess->name_of(id) : "?"; }
static bool g_away;
bool away() { return g_away; }
void set_away(bool on) { g_away = on; }
bool in_space() { return !g_away && game::mode_id() == modes::kSpace; }

static uint32_t g_epoch;
static bool g_space_open; // the Space world is alive (kSpace, or an editor / adventure entered from it)
static void* g_epoch_emp;
static uintptr_t a_GetPlayerEmpire;
uint32_t world_epoch() { return g_epoch; }
static void epoch_tick()
{
    uint32_t m = game::mode_id();
    if (m == modes::kSpace) {
        void* e = a_GetPlayerEmpire ? skg::cdecl_call<void*>(a_GetPlayerEmpire) : nullptr;
        bool emp_changed = e && g_epoch_emp && e != g_epoch_emp;
        if (!g_space_open || emp_changed) {
            g_epoch++;
            if (H)
                H->log(M, SK_INFO, "space: world epoch %u (%s)", g_epoch,
                       emp_changed ? "the player's empire changed" : "Space entered");
        }
        g_space_open = true;
        if (e)
            g_epoch_emp = e;
    } else if (!(g_space_open && (m == modes::kEdit || m == modes::kEditor || m == modes::kScenario))) {
        g_space_open = false;
        g_epoch_emp = nullptr;
    }
}
bool russian() { return coop_ui::russian(); }
static uint32_t g_saves;
static std::vector<void (*)()> g_save_fns;
void on_save(void (*fn)())
{
    if (std::find(g_save_fns.begin(), g_save_fns.end(), fn) == g_save_fns.end())
        g_save_fns.push_back(fn);
}
void note_save()
{
    g_saves++;
    for (auto fn : g_save_fns)
        fn();
}
uint32_t saves() { return g_saves; }

static bool g_rivals;
static std::map<uint16_t, Relation> g_rel; // (low id << 8 | high id) -> relation

bool rivals() { return g_rivals; }
Relation relation(uint8_t a, uint8_t b)
{
    if (!g_rivals || a == b)
        return Relation::Ally;
    auto it = g_rel.find(a < b ? (uint16_t)(a << 8 | b) : (uint16_t)(b << 8 | a));
    return it == g_rel.end() ? Relation::Neutral : it->second;
}
void set_rivals(bool on) { g_rivals = on; }
void set_relation(uint8_t a, uint8_t b, Relation r) { g_rel[a < b ? (uint16_t)(a << 8 | b) : (uint16_t)(b << 8 | a)] = r; }
void clear_relations() { g_rel.clear(); }

void notice(const std::string& text)
{
    if (C && C->notice)
        C->notice(text);
}

void send(uint8_t kind, const net::Writer& body)
{
    if (!connected())
        return;
    net::Writer w;
    w.u8(kind);
    w.u8(C->sess->my_id());
    w.raw(body.data.data(), body.data.size());
    if (C->sess->role() == net::Role::Host)
        C->sess->broadcast_reliable(net::MSG_SPACE, w);
    else
        C->sess->send_reliable_to(0, net::MSG_SPACE, w);
}

void init_all(const SkHost* h, SkMod* m, const spacecoop::Ctx* c)
{
    H = h;
    M = m;
    C = c;
    a_GetPlayerEmpire = h->sym("Simulator::GetPlayerEmpire");
    auto& v = all();
    g_on.assign(v.size(), false);
    for (size_t i = 0; i < v.size(); i++) {
        g_on[i] = !v[i].init || v[i].init(h, m);
        H->log(M, g_on[i] ? SK_INFO : SK_WARN, "space feature %s: %s", v[i].name.c_str(), g_on[i] ? "on" : "off");
    }
}

void tick_all(double now)
{
    epoch_tick();
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (g_on[i] && v[i].tick)
            v[i].tick(now);
}

bool dispatch(uint8_t kind, uint8_t origin, net::Reader& r)
{
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (v[i].kind_lo && kind >= v[i].kind_lo && kind <= v[i].kind_hi) {
            if (g_on[i] && v[i].on_message)
                v[i].on_message(kind, origin, r);
            return true;
        }
    return false;
}

void session_end_all()
{
    g_away = false;
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (g_on[i] && v[i].on_session_end)
            v[i].on_session_end();
}

void overlay_all()
{
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (g_on[i] && v[i].draw_overlay)
            v[i].draw_overlay();
}

std::vector<Row> rows_all()
{
    std::vector<Row> out;
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (g_on[i] && v[i].rows)
            for (auto& r : v[i].rows())
                out.push_back(std::move(r));
    return out;
}

std::string status_all()
{
    std::string s;
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++) {
        if (!g_on[i]) {
            s += v[i].name + ": off\n";
            continue;
        }
        std::string l = v[i].status ? v[i].status() : v[i].name + ": on";
        if (!l.empty() && l.back() != '\n')
            l += '\n';
        s += l;
    }
    return s;
}

bool debug(const std::vector<std::string>& args, std::string& out)
{
    if (args.empty())
        return false;
    if (args[0] == "features") {
        out = status_all();
        return true;
    }
    if (args[0] == "rows") { // tests: what the Space page shows (n: text | button)
        auto rows = rows_all();
        out.clear();
        for (size_t i = 0; i < rows.size(); i++)
            out += std::to_string(i) + ": " + rows[i].text + (rows[i].button.empty() ? "" : " | " + rows[i].button) + "\n";
        if (out.empty())
            out = "no rows\n";
        return true;
    }
    if (args[0] == "press" && args.size() > 1) { // tests: press the button of row n (outside the frame)
        auto rows = rows_all();
        size_t n = (size_t)atoi(args[1].c_str());
        if (n >= rows.size() || !rows[n].on_click) {
            out = "no button in row " + args[1] + "\n";
            return true;
        }
        rows[n].on_click();
        out = "pressed '" + rows[n].button + "'\n";
        return true;
    }
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (v[i].name == args[0]) {
            if (!g_on[i])
                out = v[i].name + ": off (see sporekit.log: a symbol or a hook is missing)\n";
            else if (v[i].debug)
                out = v[i].debug(std::vector<std::string>(args.begin() + 1, args.end()));
            else
                out = v[i].status ? v[i].status() : v[i].name + ": no commands\n";
            return true;
        }
    return false;
}

} // namespace spaceext
