#include "find.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "coop_ui.h"
#include "fixes.h"
#include "net/protocol.h"
#include "net/utf8.h"
#include "settings.h"
#include "space_ext.h"
#include "sporekit_game.hpp"

namespace pfind {

static const SkHost* H;
static SkMod* M;
static Ctx C;

bool g_arrows_fix = true;
COOP_FIX("find.arrows", "BUG-0764", g_arrows_fix, "all stages: an arrow at the screen edge to every partner off screen (F2)");
bool g_map_fix = true;
COOP_FIX("find.map", "BUG-0764", g_map_fix, "Creature, Tribe: every partner on the game's minimap in its colour (F2)");
bool g_colors_fix = true;
COOP_FIX("find.colors", "BUG-0764", g_colors_fix, "all stages: the host gives every player one colour, the same on every game (F2)");

static findx::Colors g_tab;     // in force here (host: its own; guest: the host's)
static uint32_t g_changes = 1;
static int g_want = -1;         // the colour this player picked (coop.ini [coop] color), -1 none
static uint32_t g_msgs_in, g_msgs_out, g_refused;

#define SAY(...) H->out_printf(out, __VA_ARGS__)

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_client() { return C.sess && C.sess->role() == net::Role::Client; }
static uint8_t my_id() { return C.sess ? C.sess->my_id() : 0; }

static bool kit_tris() { return H && H->size >= offsetof(SkHost, overlay_tris) + sizeof(void*) && H->overlay_tris; }

// ---------------- colours ----------------
static void changed() { g_changes++; }

static void broadcast()
{
    if (!is_host())
        return;
    net::Writer w;
    findx::write_table(w, g_tab);
    C.sess->broadcast_reliable(net::MSG_COLORS, w);
    g_msgs_out++;
}

// find.colors off (the twin of the tests): each game numbers the players itself, its own colour first
static int local_index(uint8_t id)
{
    if (!C.sess)
        return id % findx::kColors;
    int n = 0;
    if (id == my_id())
        return 0;
    for (auto& p : C.sess->players())
        if (p.id != my_id()) {
            n++;
            if (p.id == id)
                return n % findx::kColors;
        }
    return id % findx::kColors;
}

int color_index(uint8_t id)
{
    if (!g_colors_fix)
        return local_index(id);
    int c = g_tab.of(id);
    if (c < 0 && id == my_id() && (!C.sess || C.sess->role() == net::Role::None))
        return g_want >= 0 ? g_want : 0;
    return c;
}

uint32_t color_of(uint8_t id)
{
    int c = color_index(id);
    return findx::kPalette[c >= 0 ? c : id % findx::kColors].argb;
}

uint32_t my_color() { return color_of(my_id()); }
const findx::Colors& colors() { return g_tab; }
uint32_t colors_changes() { return g_changes; }

static void remember(int idx)
{
    g_want = idx;
    if (H && H->config_set_str)
        H->config_set_str(M, "coop", "color", findx::kPalette[idx].key);
}

bool request_color(int idx, std::string* why)
{
    findx::Refused r = findx::can_take(g_tab, my_id(), idx);
    if (r == findx::Refused::Range) {
        if (why)
            *why = "not a colour (" + std::to_string(findx::kColors) + " colours: 0.." + std::to_string(findx::kColors - 1) + ")";
        return false;
    }
    if (r == findx::Refused::Taken && (is_host() || is_client())) {
        if (why)
            *why = std::string("another player has ") + findx::kPalette[idx].en;
        return false;
    }
    remember(idx);
    if (is_client()) {
        net::Writer w;
        findx::write_request(w, idx);
        C.sess->send_reliable_to(0, net::MSG_COLORS, w);
        g_msgs_out++;
        if (why)
            *why = "asked the host";
        return true;
    }
    g_tab.c[my_id()] = (int8_t)idx;
    changed();
    broadcast();
    return true;
}

void on_session_start()
{
    g_tab = findx::Colors();
    if (is_host()) {
        uint8_t me = my_id();
        if (g_want >= 0)
            g_tab.c[me] = (int8_t)g_want;
        else
            findx::auto_pick(g_tab, me);
    }
    changed();
}

void on_player_join(uint8_t id)
{
    if (!is_host())
        return;
    if (g_tab.of(my_id()) < 0)
        on_session_start();
    if (id != my_id())
        findx::auto_pick(g_tab, id);
    changed();
    broadcast();
}

void on_player_leave(uint8_t id)
{
    if (!is_host() || id == my_id())
        return;
    g_tab.c[id] = -1;
    changed();
    broadcast();
}

void on_connected()
{
    if (!is_client() || g_want < 0)
        return;
    net::Writer w;
    findx::write_request(w, g_want);
    C.sess->send_reliable_to(0, net::MSG_COLORS, w);
    g_msgs_out++;
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_COLORS)
        return;
    uint8_t kind = r.u8();
    if (!r.ok())
        return;
    g_msgs_in++;
    if (kind == 0 && is_client() && from == 0) {
        findx::Colors t;
        if (findx::read_table(r, t) && !(t == g_tab)) {
            g_tab = t;
            changed();
        }
        return;
    }
    if (kind == 1 && is_host()) {
        int idx = r.u8();
        if (!r.ok())
            return;
        if (findx::can_take(g_tab, from, idx) == findx::Refused::None) {
            g_tab.c[from] = (int8_t)idx;
            changed();
        } else {
            g_refused++;
        }
        broadcast(); // the guest learns the answer from the table either way
    }
}

void on_session_end()
{
    g_tab = findx::Colors();
    changed();
}

// ---------------- arrows ----------------
bool arrows_on() { return g_arrows_fix && settings::on(settings::kPartnerArrows); }
bool map_on() { return g_map_fix && settings::on(settings::kPartnerMap); }

struct Rec {
    uint8_t id;
    std::string name;
    bool on_screen, filled, walked;
    float x, y, ang, sx, sy, dist;
    float p[3];
};
static float g_me[3];
static bool g_has_me = false;
static std::vector<Rec> g_last; // the last overlay pass (coop.find)
static double g_last_t = 0;
static uint32_t g_drawn_total = 0;
constexpr float kSize = 22.f, kMargin = 46.f;

// projected point of w: filled = the projection gave a point (in front of the camera); on = inside its view
static bool project(const float* w, float* sc, bool& filled)
{
    sc[0] = sc[1] = -1e30f;
    float p[3] = {w[0], w[1], w[2]};
    bool on = H->world_to_screen(p, sc) != 0;
    filled = sc[0] > -1e29f && std::isfinite(sc[0]) && std::isfinite(sc[1]);
    return on;
}

int draw_arrows(const std::vector<Target>& targets, const float* me)
{
    g_last.clear();
    g_last_t = H->time_seconds();
    g_has_me = me != nullptr;
    if (me)
        g_me[0] = me[0], g_me[1] = me[1], g_me[2] = me[2];
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    if (sw <= 0 || sh <= 0)
        return 0;
    bool draw = arrows_on();
    int n = 0;
    for (const Target& t : targets) {
        Rec rc{};
        rc.id = t.id;
        rc.name = t.name;
        rc.p[0] = t.pos[0], rc.p[1] = t.pos[1], rc.p[2] = t.pos[2];
        float sc[2];
        bool filled = false;
        bool on = project(t.pos, sc, filled);
        rc.sx = sc[0], rc.sy = sc[1], rc.filled = filled;
        rc.dist = -1;
        if (me) {
            float dx = t.pos[0] - me[0], dy = t.pos[1] - me[1], dz = t.pos[2] - me[2];
            rc.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        if (!filled && me) {
            // behind the camera: the farthest point on the way from me to it that is in front gives the direction
            float lo = 0, hi = 1, best[2] = {0, 0};
            bool any = false;
            for (int it = 0; it < 14; it++) {
                float m = (lo + hi) / 2, q[3], qs[2];
                for (int k = 0; k < 3; k++)
                    q[k] = me[k] + (t.pos[k] - me[k]) * m;
                bool f = false;
                project(q, qs, f);
                if (f) {
                    lo = m;
                    best[0] = qs[0], best[1] = qs[1];
                    any = true;
                } else
                    hi = m;
            }
            if (any) {
                sc[0] = best[0], sc[1] = best[1];
                filled = true;
                on = false;
                rc.walked = true;
            }
        }
        findx::Arrow a;
        if (!findx::arrow_place(filled, on, sc[0], sc[1], sw, sh, kMargin, kMargin, a))
            continue;
        rc.on_screen = a.on_screen;
        rc.x = a.x, rc.y = a.y, rc.ang = a.ang;
        if (!a.on_screen && draw) {
            uint32_t col = color_of(t.id);
            std::string label = t.name + (t.what.empty() ? "" : " (" + t.what + ")");
            std::string d = findx::dist_text(rc.dist);
            if (!d.empty())
                label += "  " + d;
            if (kit_tris()) {
                float v[12];
                int k = findx::arrow_tris(a, kSize, 3.f, v);
                H->overlay_tris(v, k, 0xB0000000);
                k = findx::arrow_tris(a, kSize, 0.f, v);
                H->overlay_tris(v, k, col);
            } else { // an older SporeKit: the glyph arrow of the pings
                const char* g = std::fabs(std::cos(a.ang)) > 0.7071f ? (std::cos(a.ang) > 0 ? "►" : "◄")
                                                                     : (std::sin(a.ang) > 0 ? "▼" : "▲");
                H->overlay_text((int)a.x - 8, (int)a.y - 10, col, g);
            }
            int tw = utf8::overlay_width(label), lx, ly;
            findx::label_at(a, kSize, tw, sw, sh, lx, ly);
            H->overlay_text(lx, ly, col, label.c_str());
            n++;
            g_drawn_total++;
        }
        g_last.push_back(rc); // (also when not drawn: `coop.find` says drawn=0)
    }
    return n;
}


// ---------------- the minimap (docs/re/creature.md#creature-minimap, docs/re/tribe.md#tribe-minimap) ----------------
// The game's cMinimap (Creature: the global CreatureMinimap_Ptr; Tribe: cTribeUI_Get -> vt 0x6C -> +0x70) keeps
// blips by a u32 key; a blip is a UTFWin window tinted with the desc's colour. Ours: the key is the address of a
// static slot of the player (never a game key: those are game object pointers). MoveBlip each update, AddBlip when
// the map has none (the Tribe rebuild clears every blip), RemoveBlip when the partner is gone. UI: outside the frame.
struct BlipDesc {
    uint32_t argb;
    float pos[3];
    uint32_t flags;
    void* obj;
    uint32_t z[3];
};
static_assert(sizeof(BlipDesc) == 0x24, "BlipDesc");
static uint32_t g_keys[256];      // their addresses are the blip keys
static std::vector<Target> g_map_targets;
static uint32_t g_map_mode = 0;
static void* g_map = nullptr;     // the minimap our blips are on
static bool g_map_has[256];
static uint32_t g_map_flags = 0x2; // the icon (the tribe icon, the game tints it)
static double g_map_next = 0;
static bool g_map_queued = false;
static uint32_t g_map_adds, g_map_moves, g_map_removes;
static uintptr_t a_Add, a_Move, a_Find, a_Remove, a_CreaturePtr, a_TribeUI;

constexpr uint32_t kModeCreature = 0x1654C01, kModeTribe = 0x1654C02;

static uint32_t key_of(uint8_t id) { return (uint32_t)(uintptr_t)&g_keys[id]; }

static void* minimap_now(uint32_t mode)
{
    using skg::readable;
    if (mode == kModeCreature && a_CreaturePtr && readable((void*)a_CreaturePtr, 4)) {
        void* m = *(void**)a_CreaturePtr;
        return m && readable(m, 0x9A8) ? m : nullptr;
    }
    if (mode == kModeTribe && a_TribeUI && readable((void*)a_TribeUI, 4)) {
        void* ui = *(void**)a_TribeUI;
        if (!ui || !readable(ui, 0x250) || !readable(*(void**)ui, 0x70))
            return nullptr;
        void* g = skg::vcall<void*>(ui, 0x6C / 4);
        if (!g || !readable(g, 0x74))
            return nullptr;
        void* m = skg::field<void*>(g, 0x70);
        return m && readable(m, 0x9A8) ? m : nullptr;
    }
    return nullptr;
}

static const BlipDesc* find_blip(void* m, uint8_t id)
{
    return m && a_Find ? skg::thiscall<const BlipDesc*>(a_Find, m, key_of(id)) : nullptr;
}

static void map_task(void*)
{
    g_map_queued = false;
    if (!a_Add || !a_Move || !a_Find || !a_Remove)
        return;
    void* m = minimap_now(g_map_mode);
    if (m != g_map) { // a new map (a stage change, a UI reload): the old one took our blips with it
        g_map = m;
        memset(g_map_has, 0, sizeof g_map_has);
    }
    if (!m)
        return;
    bool want = map_on() && (is_host() || is_client());
    bool seen[256] = {};
    if (want)
        for (const Target& t : g_map_targets) {
            seen[t.id] = true;
            uint32_t key = key_of(t.id);
            float pos[3] = {t.pos[0], t.pos[1], t.pos[2]};
            const BlipDesc* d = find_blip(m, t.id);
            if (d && d->argb != color_of(t.id)) { // a new colour: a new blip (the tint is set when it is made)
                skg::thiscall<void>(a_Remove, m, key, 1, 0);
                g_map_removes++;
                d = nullptr;
            }
            if (d) {
                skg::thiscall<bool>(a_Move, m, key, (const float*)pos, false);
                g_map_moves++;
            } else {
                BlipDesc nd{};
                nd.argb = color_of(t.id);
                nd.pos[0] = pos[0], nd.pos[1] = pos[1], nd.pos[2] = pos[2];
                nd.flags = g_map_flags;
                skg::thiscall<bool>(a_Add, m, key, &nd, 0u, false);
                g_map_adds++;
            }
            g_map_has[t.id] = true;
        }
    for (int id = 0; id < 256; id++)
        if (g_map_has[id] && !seen[id]) {
            if (find_blip(m, (uint8_t)id)) {
                skg::thiscall<void>(a_Remove, m, key_of((uint8_t)id), 1, 0);
                g_map_removes++;
            }
            g_map_has[id] = false;
        }
}

void set_map_targets(const std::vector<Target>& targets, uint32_t mode)
{
    g_map_targets = targets;
    g_map_mode = mode;
}

void tick(double now)
{
    if (now < g_map_next || g_map_queued)
        return;
    g_map_next = now + 0.25;
    if (g_map_mode != kModeCreature && g_map_mode != kModeTribe && !g_map)
        return;
    g_map_queued = true;
    H->run_outside_frame(map_task, nullptr);
}

static void map_report(SkOut* out)
{
    void* m = g_map;
    SAY("map mode=%08X map=%d on=%d flags=%X adds=%u moves=%u removes=%u\n", g_map_mode, m ? 1 : 0, (int)map_on(), g_map_flags,
        g_map_adds, g_map_moves, g_map_removes);
    if (!m || m != minimap_now(g_map_mode))
        return;
    for (int id = 0; id < 256; id++) {
        const BlipDesc* d = find_blip(m, (uint8_t)id);
        if (d) // read back from the game's own map node
            SAY("blip id=%d key=%08X argb=%08X pos=%.2f,%.2f,%.2f flags=%X\n", id, key_of((uint8_t)id), d->argb, d->pos[0],
                d->pos[1], d->pos[2], d->flags);
    }
}

std::string status()
{
    char b[160];
    int arrows = 0;
    for (auto& r : g_last)
        arrows += !r.on_screen;
    snprintf(b, sizeof b, "find: arrows=%d on=%d map=%d colors_in=%u out=%u refused=%u", arrows, (int)arrows_on(), (int)map_on(),
             g_msgs_in, g_msgs_out, g_refused);
    return b;
}

int command(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string a1 = argc > 1 ? argv[1] : "";
    if (a1 == "color" && argc > 2 && (!strcmp(argv[2], "none") || !strcmp(argv[2], "auto"))) { // no pick: the host assigns
        g_want = -1;
        if (H->config_set_str)
            H->config_set_str(M, "coop", "color", "");
        SAY("ok (the host assigns the colour from the next session on)\n");
        return 0;
    }
    if (a1 == "color" && argc > 2) { // pick a colour (the lobby's API): a name or 0..7
        int idx = findx::color_find(argv[2]);
        std::string why;
        bool ok = request_color(idx, &why);
        SAY("%s%s%s\n", ok ? "ok" : "refused: ", ok && !why.empty() ? " " : "", why.c_str());
        return ok ? 0 : 1;
    }
    if (a1 == "colors") { // the table: id name colour argb
        if (C.sess)
            for (auto& p : C.sess->players()) {
                int c = color_index(p.id);
                SAY("color id=%u idx=%d key=%s argb=%08X name=%s%s\n", p.id, c, c >= 0 ? findx::kPalette[c].key : "-",
                    color_of(p.id), p.name.c_str(), p.id == my_id() ? " (me)" : "");
            }
        SAY("changes=%u want=%d fix=%d\n", g_changes, g_want, (int)g_colors_fix);
        return 0;
    }
    if (a1 == "map") { // the partners' blips on this game's minimap, read back from the game (runs outside the frame)
        if (argc > 2) // tests / looks: the blip icon flags (docs/re/creature.md#creature-minimap)
            g_map_flags = (uint32_t)strtoul(argv[2], nullptr, 16);
        map_report(out);
        return 0;
    }
    if (a1 == "rivals" && argc > 2) { // tests: this game's Rivals flag (Space sets it by the players' vote)
        spaceext::set_rivals(atoi(argv[2]) != 0);
        SAY("rivals=%d arrows_on=%d\n", (int)spaceext::rivals(), (int)arrows_on());
        return 0;
    }
    if (a1 == "project" && argc > 4) { // tests: where a world point is on this game's screen
        float w[3] = {(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])}, sc[2];
        bool filled = false;
        bool on = project(w, sc, filled);
        int sw = 0, sh = 0;
        H->screen_size(&sw, &sh);
        SAY("project on=%d filled=%d sx=%.1f sy=%.1f sw=%d sh=%d\n", (int)on, (int)filled, sc[0], sc[1], sw, sh);
        return 0;
    }
    // the arrows of the last overlay pass
    SAY("%s age=%.2f drawn_total=%u\n", status().c_str(), H->time_seconds() - g_last_t, g_drawn_total);
    if (g_has_me) // the own avatar (Tribe: the own chief), what a partner's game points at
        SAY("me pos=%.2f,%.2f,%.2f\n", g_me[0], g_me[1], g_me[2]);
    for (auto& r : g_last)
        SAY("%s id=%u name=%s x=%.1f y=%.1f ang=%.3f sx=%.1f sy=%.1f filled=%d walked=%d dist=%.1f drawn=%d argb=%08X pos=%.2f,%.2f,%.2f\n",
            r.on_screen ? "tag" : "arrow", r.id, r.name.c_str(), r.x, r.y, r.ang, r.sx, r.sy, (int)r.filled, (int)r.walked,
            r.dist, (int)(!r.on_screen && arrows_on()), color_of(r.id), r.p[0], r.p[1], r.p[2]);
    return 0;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    char b[32] = {};
    H->config_str(M, "coop", "color", "", b, sizeof b);
    g_want = b[0] ? findx::color_find(b) : -1;
    a_Add = H->sym("SporeCoop::Minimap_AddBlip");
    a_Move = H->sym("SporeCoop::Minimap_MoveBlip");
    a_Find = H->sym("SporeCoop::Minimap_FindBlip");
    a_Remove = H->sym("SporeCoop::Minimap_RemoveBlip");
    a_CreaturePtr = H->sym("SporeCoop::CreatureMinimap_Ptr");
    a_TribeUI = H->sym("SporeCoop::TribeUI_ptr");
}

} // namespace pfind
