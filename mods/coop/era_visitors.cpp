// Decision 5S A spike (docs/12-server-and-players.md#server-era-visitors), alpha only: can a Creature-stage game show a
// vehicle or a city building of another era? The Creature stage flies UFOs (its drama events; the mod mirrors them,
// BUG-0558), and the game's SpawnUFO takes any model key: a ship made with a vehicle's or a building's model, placed on
// the ground and moved every frame, is the cheapest look-alike to try.
//
//   coop.visit spawn <instance> <type> <group> [drive|stand] [lift] [axes] [prop|ufo]
//     (hex; drive: it circles 12 m around a point near the avatar)
//
// The carrier (decision 5T B): `prop` (the default) is a plain ornament (noun 0x18C88E4, the object the game uses for a
// city's decorations and the Tribe attack marker, docs/re/space.md#space-city-ornaments): it does not fly, so it stands
// where it is put. `ufo` is the first try: SpawnUFO with the model, which the UFO's own update lifts 6 m over the ground.
//   coop.visit clear | status
//
// Not saved: the save-strip filter leaves our props out of a save (not tested yet); UFO carriers are not covered (do not
// save while they exist).
#include "era_visitors.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "creations.h"
#include "creature_hp.h"
#include "fixes.h"
#include "galaxy_client.h"
#include "galaxy_crt.h"
#include "game.h"
#include "net/buffer.h"
#include "net/galaxy.h"
#include "net/interp.h"
#include "net/protocol.h"
#include "modes.h"
#include "net/utf8.h"
#include "notify.h"
#include "partner_marker.h"
#include "savestrip.h"
#include "spacecoop.h"
#include "civ.h"
#include "tribe.h"
#include "sporekit_game.hpp"

#include <cstring>
#include <map>
#include <tuple>

namespace era_visitors {

static const SkHost* H;
static SkMod* M;
static uintptr_t a_SpawnUFO, a_NounGet, a_CreateInstance, a_DestroyInstance, a_OrnSetModel;
constexpr uint32_t kOrnamentNoun = 0x018C88E4;
static void* noun_mgr() { return a_NounGet ? skg::cdecl_call<void*>(a_NounGet) : nullptr; }

struct Prop {
    void* u = nullptr;
    uint32_t serial = 0;
    uint32_t key[3]{};
    bool drive = false;
    bool prop = false; // an ornament of ours (AddRef held), else a SpawnUFO ship
    net::Vec3 centre{};
    float angle = 0;
    float lift = 0; // m over the ground
    net::Vec3 fwd{0, 0, 1};
    net::Quat avq{0, 0, 0, 1};
    int axes = 0;   // tests: 0 Y forward / Z up, 1 the avatar's own orientation (turned for the heading), 2 X forward / Z up
};
static std::vector<Prop> g_props;
static double g_last;
static uint32_t g_frames;
static net::Vec3 g_next_at{};
static bool g_next_at_set;

static net::Vec3 norm(net::Vec3 v)
{
    float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0 ? net::Vec3{v.x / l, v.y / l, v.z / l} : net::Vec3{0, 0, 1};
}
static net::Vec3 cross(net::Vec3 a, net::Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static net::Vec3 add(net::Vec3 a, net::Vec3 b, float k) { return {a.x + b.x * k, a.y + b.y * k, a.z + b.z * k}; }

// a rotation whose local Y looks along `fwd` and local Z along `up` (Spore's models: Z up)
static int g_axes;
static net::Quat basis_quat(net::Vec3 fwd, net::Vec3 up)
{
    if (g_axes == 2) { // X forward: the frame turned a quarter about Z
        net::Vec3 r0 = norm(cross(fwd, up));
        fwd = net::Vec3{-r0.x, -r0.y, -r0.z};
    }
    net::Vec3 r = norm(cross(fwd, up));
    fwd = norm(cross(up, r));
    // the matrix columns: X = r, Y = fwd, Z = up
    const float m00 = r.x, m01 = fwd.x, m02 = up.x, m10 = r.y, m11 = fwd.y, m12 = up.y, m20 = r.z, m21 = fwd.z, m22 = up.z;
    net::Quat q;
    const float t = m00 + m11 + m22;
    if (t > 0) {
        float s = std::sqrt(t + 1.f) * 2;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.f + m00 - m11 - m22) * 2;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        float s = std::sqrt(1.f + m11 - m00 - m22) * 2;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        float s = std::sqrt(1.f + m22 - m00 - m11) * 2;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return q;
}

static void place(void* u, const net::Vec3& p, const net::Quat& q)
{
    void* sp = game::cast(u, 0x1186577); // cSpatialObject
    if (!sp)
        return;
    struct { float x, y, z; } vp{p.x, p.y, p.z};
    struct { float x, y, z, w; } vq{q.x, q.y, q.z, q.w};
    if (game::noun_id(u) == kOrnamentNoun) { // a prop: SetPosition and SetOrientation, as worldobj.cpp moves the city
        skg::vcall<void>(sp, 0x38 / 4, &vp); // decorations (one vt 0x44 call moved it once, later ones were ignored)
        skg::vcall<void>(sp, 0x3C / 4, &vq);
        return; // (no flight of its own)
    }
    skg::vcall<void>(sp, 0x44 / 4, &vp, &vq); // SetPositionAndOrientation
    memcpy((uint8_t*)u + 0x718, &vp, 12);     // (the ship's own flight: next position and destination, as BUG-0558)
    memcpy((uint8_t*)u + 0x750, &vp, 12);
    // the ship flies to its destination at its zoom altitude (mZoomAltitude +0x768, docs/re/space.md): the point's own
    // distance from the planet's centre, or it hovers over the ground (6 m in the Creature stage)
    const float alt = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    memcpy((uint8_t*)u + 0x768, &alt, 4);
}

// a point on the ground and the pose of a prop there, facing along `fwd`
static void pose_at(const net::Vec3& dir_in, const net::Vec3& fwd_in, float lift, net::Vec3& p, net::Quat& q)
{
    const net::Vec3 up = norm(dir_in);
    const float g = game::ground_radius(net::Vec3{up.x * 600, up.y * 600, up.z * 600}); // (a point over the ground)
    p = {up.x * (g + lift), up.y * (g + lift), up.z * (g + lift)};
    q = basis_quat(fwd_in, up);
}

static void drive_prop(Prop& pr, float dt)
{
    g_axes = pr.axes;
    const net::Vec3 up = norm(pr.centre);
    const net::Vec3 east = norm(cross(up, net::Vec3{0, 0, 1}));
    const net::Vec3 north = cross(up, east);
    pr.angle += dt * 5.f / 12.f; // 5 m/s on a 12 m circle
    const float c = std::cos(pr.angle), s = std::sin(pr.angle);
    const net::Vec3 off = add(add({0, 0, 0}, east, 12 * c), north, 12 * s);
    const net::Vec3 tangent = add(add({0, 0, 0}, east, -s), north, c);
    net::Vec3 p;
    net::Quat q;
    pose_at(add(pr.centre, off, 1), tangent, pr.lift, p, q);
    place(pr.u, p, q);
}

static bool baked_or_ask(const uint32_t key[3]); // BUG-9290 (below)
std::string spawn(uint32_t inst, uint32_t type, uint32_t group, bool drive, float lift, int axes, bool prop)
{
    void* av = game::avatar();
    if (!av || !game::in_creature_stage())
        return "needs the Creature stage";
    if (prop ? !(a_NounGet && a_CreateInstance && a_DestroyInstance && a_OrnSetModel) : !a_SpawnUFO)
        return "missing a symbol for this carrier";
    creations::ModelKind kind = type == 0x24682294 ? creations::kMkVehicle
                                : type == 0x2399BE55 ? creations::kMkBuilding
                                : type == 0x476A98C7 ? creations::kMkUfo
                                                     : creations::kMkCreature;
    creations::Key k{inst, type, group};
    creations::Model m = creations::ensure_model(k, kind);
    uint32_t key[3] = {inst, type, group};
    if (m.key.instance)
        key[0] = m.key.instance, key[1] = m.key.type, key[2] = m.key.group;
    if (!baked_or_ask(key)) // BUG-9290
        return "its model is not baked here yet: asked the game to bake it, spawn again in a few seconds";
    void* u = nullptr;
    if (prop) {
        void* nm = noun_mgr();
        u = nm ? skg::thiscall<void*>(a_CreateInstance, nm, kOrnamentNoun) : nullptr;
        if (!u || game::noun_id(u) != kOrnamentNoun)
            return "CreateInstance made no ornament";
        skg::vcall<void>(u, 0); // AddRef: ours until clear()
        skg::thiscall<void>(a_OrnSetModel, u, key[0], key[1], key[2]);
        if (void* sp = game::cast(u, 0x1186577)) // the creation's model by its key, as cVehicle::Load (spatial vt 0x94 SetModelKey)
            skg::vcall<void>(sp, 0x94 / 4, (const uint32_t*)key);
    } else {
        uint32_t pol = 0;
        u = skg::cdecl_call<void*>(a_SpawnUFO, 7, &pol, (void*)key);
        if (!u)
            return "SpawnUFO made nothing";
    }
    Prop pr;
    pr.u = u;
    pr.prop = prop;
    pr.serial = game::object_serial(u);
    memcpy(pr.key, key, sizeof key);
    pr.drive = drive;
    pr.lift = lift;
    pr.axes = axes;
    // 25 m ahead of the avatar (its +Y), on the ground
    const net::Vec3 ap = game::position(av);
    const net::Quat aq = game::orientation(av);
    const net::Vec3 fwd = {2 * (aq.x * aq.y - aq.w * aq.z), 1 - 2 * (aq.x * aq.x + aq.z * aq.z), 2 * (aq.y * aq.z + aq.w * aq.x)};
    pr.centre = g_next_at_set ? g_next_at : add(ap, fwd, 25);
    g_next_at_set = false;
    pr.fwd = fwd;
    pr.avq = aq;
    g_axes = axes;
    net::Vec3 p;
    net::Quat q;
    pose_at(pr.centre, fwd, lift, p, q);
    if (axes == 1)
        q = aq;
    place(u, p, q);
    g_props.push_back(pr);
    char b[200];
    snprintf(b, sizeof b, "spawned %s %p model %08X/%08X/%08X (%s) at (%.1f %.1f %.1f)%s", prop ? "prop" : "ufo", u, key[0], key[1], key[2],
             m.status == creations::Model::kSame ? "its own model" : m.status == creations::Model::kSubstituted ? "a stand-in model" : "the key as given",
             p.x, p.y, p.z, drive ? ", driving" : "");
    H->log(M, SK_INFO, "era_visitors: %s", b);
    return b;
}

std::string spawn_at(const net::Vec3& at) // tests (BUG-9291): the next spawn stands there
{
    g_next_at = at;
    g_next_at_set = true;
    return "next spawn there";
}
std::string move_last(const net::Vec3& at)
{
    if (g_props.empty())
        return "no prop";
    g_props.back().centre = at;
    return "moved";
}


std::string clear()
{
    size_t n = 0;
    for (auto& pr : g_props) {
        const bool alive = pr.u && game::object_alive(pr.u) && game::object_serial(pr.u) == pr.serial;
        if (alive && pr.prop) {
            if (void* nm = noun_mgr())
                skg::thiscall<void>(a_DestroyInstance, nm, pr.u);
            skg::vcall<void>(pr.u, 1); // Release our reference
            n++;
        } else if (alive) {
            game::destroy_creature(pr.u); // (the deferred path: any cGameData)
            n++;
        }
    }
    g_props.clear();
    return "cleared " + std::to_string(n);
}

std::string stream_status();
std::string status()
{
    std::string out = stream_status() + galaxy_crt::status() + "era visitors (spike): " + std::to_string(g_props.size()) + " prop(s), frames " + std::to_string(g_frames) + "\n";
    for (auto& pr : g_props) {
        net::Vec3 v{};
        const bool alive = pr.u && game::object_alive(pr.u) && game::object_serial(pr.u) == pr.serial;
        if (alive)
            game::object_position(pr.u, v);
        char b[260];
        float w[3] = {v.x, v.y, v.z}, sc[2] = {NAN, NAN};
        if (alive)
            H->world_to_screen(w, sc); // (tests, BUG-9291: on the screen or not)
        void* av = game::avatar();
        snprintf(b, sizeof b, "  %s %p %s model %08X/%08X/%08X %s pos=(%.1f %.1f %.1f) ground=%.1f screen=(%.0f %.0f) from_avatar=%.1f\n",
                 pr.prop ? "prop" : "ufo", pr.u, alive ? "alive" : "gone", pr.key[0], pr.key[1], pr.key[2],
                 pr.drive ? "driving" : "standing", v.x, v.y, v.z, alive ? game::ground_radius(v) : 0.f, sc[0], sc[1],
                 av ? net::dist(game::position(av), v) : -1.f);
        out += b;
    }
    return out;
}

static void on_visitor(net::Reader& r); // BUG-9287 (below)
static void on_visit_objects(net::Reader& r); // BUG-9290 (below)
static bool our_object_prop(void* obj);       // BUG-9290 (below)

// our props are not the player's world (CLAUDE.md 2): a save never holds them
static bool ship_ours(void* obj); // BUG-9349 (below)
static bool savestrip_filter(void* obj, uint32_t noun)
{
    if (noun == 0x018EBADCu) // BUG-9349: our visitor ships
        return ship_ours(obj);
    if (noun != kOrnamentNoun)
        return false;
    for (auto& pr : g_props)
        if (pr.prop && pr.u == obj)
            return true;
    return our_object_prop(obj);
}

// BUG-9290: a model is drawn only once the game has baked it (its mesh in the graphics cache). A Creature profile never
// baked another era's buildings and vehicles, so their props drew nothing (the spike's profile had them baked). As the
// game does for a model it is about to use (0xC5B250, 0xC5B420): IBakeManager vt 0x1C(key, 0) "baked", else vt 0x18(key)
// "asked already" or vt 0x4C(key, &0) asks for the bake. true: baked (or no bake manager: as before)
static uint64_t g_bakes_asked, g_bakes_pending, g_bakes_skipped;
static bool g_bake_outside = true;
COOP_FIX("galaxy.bake_outside_frame", "BUG-9296", g_bake_outside,
         "galaxy: another era's model is asked to bake outside the frame (asked in it, a Tribe start crashed)");
// BUG-9341: a bake's picture done, the bake's render (0x410DC0) posts message 0x3FC3F13 to every initialized cEditor (its
// listener from cEditor::Initialize to Dispose). The editor's handler (cEditor::HandleMessage 0x591FA0, the case at
// +0x108) reads its save area's database (Resource::GetDatabaseByID([this + 0x29C])) and its model ([this + 0x88],
// GetCreationName) without a null check: on a galaxy planet a Creature host crashed ~6 s after a bake ask of another
// era's model, at +0x11E (no database: the death caption, and in plain play) and at +0x147 (no model: just out of the
// editor). The message has nothing to save for such an editor: it is left out. Off: the game's handler as it is
static bool g_snap_guard = true;
COOP_FIX("galaxy.editor_snapshot_guard", "BUG-9341", g_snap_guard,
         "galaxy: an editor without its save area or model skips a bake's snapshot message (it crashed)");
typedef char(__fastcall* EdMsgFn)(void* self, void* edx, uint32_t id, void* msg);
static EdMsgFn o_EdMsg;
static uintptr_t a_GetDb;
static uint64_t g_snaps_seen, g_snaps_skipped;
static char __fastcall h_EdMsg(void* self, void* edx, uint32_t id, void* msg)
{
    if (id == 0x3FC3F13 && self) {
        g_snaps_seen++;
        void* db = a_GetDb ? skg::cdecl_call<void*>(a_GetDb, *(uint32_t*)((uint8_t*)self + 0x29C)) : (void*)1;
        void* model = *(void**)((uint8_t*)self + 0x88);
        if (g_snap_guard && (!db || !model)) {
            g_snaps_skipped++;
            H->log(M, SK_INFO, "era visitors: an editor without its %s skipped a bake's snapshot message (BUG-9341)",
                   !db ? "save area" : "model");
            return 0;
        }
    }
    return o_EdMsg(self, edx, id, msg);
}

static bool baked_or_ask(const uint32_t key[3])
{
    static uintptr_t get = H->sym("Editors::IBakeManager::Get");
    void* bm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!bm)
        return true;
    int lods = 0;
    for (int l = 0; l < 4; l++)
        lods |= (skg::vcall<char>(bm, 0x1C / 4, key, l) != 0) << l;
    if (lods == 0xF)
        return true;
    // BUG-9296: a Tribe game crashed ~5 s after any bake ask (Editors::cEditor::HandleMessage, a null editor: the bake's
    // end reports to an editor the Tribe stage does not have); there the prop is made with what is baked, nothing asked
    if (g_bake_outside && !game::in_creature_stage()) {
        g_bakes_skipped++;
        return true;
    }
    static std::map<std::tuple<uint32_t, uint32_t, uint32_t>, double> asked; // (once every 5 s a key)
    double& next = asked[{key[0], key[1], key[2]}];
    const double now = H->time_seconds();
    if (now < next)
        return (lods & 1) != 0;
    next = now + 5.0;
    if (g_bake_outside && skg::vcall<char>(bm, 0x18 / 4, key) != 0) { // BUG-9296: asked and not done yet: never again
        g_bakes_pending++;                                             // (a second ask then crashed a Tribe game)
        return (lods & 1) != 0;
    }
    if (g_bake_outside) { // BUG-9296: the bake renders: asked outside the frame (in it, a Tribe start crashed in it)
        struct Ask {
            uint32_t key[3];
            int lods;
        };
        Ask* a = new Ask{{key[0], key[1], key[2]}, lods};
        H->run_outside_frame(
            [](void* p) {
                Ask* a = (Ask*)p;
                static uintptr_t get2 = H->sym("Editors::IBakeManager::Get");
                void* bm2 = get2 ? skg::cdecl_call<void*>(get2) : nullptr;
                if (bm2 && skg::vcall<char>(bm2, 0x18 / 4, a->key) == 0) // (not asked in the meantime)
                    for (int l = 0; l < 4; l++)
                        if (!(a->lods & (1 << l))) {
                            int lod = l;
                            skg::vcall<void>(bm2, 0x4C / 4, a->key, &lod);
                            g_bakes_asked++;
                        }
                delete a;
            },
            a);
    } else
        for (int l = 0; l < 4; l++)
            if (!(lods & (1 << l))) {
                int lod = l;
                skg::vcall<void>(bm, 0x4C / 4, key, &lod);
                g_bakes_asked++;
            }
    H->log(M, SK_INFO, "era visitors: %08X/%08X baked levels %X: asked the rest (BUG-9290)", key[0], key[1], lods);
    return (lods & 1) != 0; // (drawn close up already; the farther levels come)
}

// a plain ornament carrying a creation's model (decision 5T B), AddRef held: ours until prop_destroy. Null: not made.
// Put where it stands in the same frame: BUG-9291, an ornament draws only near the place it had in its first frames
// (moved more than its size later, it is no longer drawn), so a moving one is made again (objects_show)
static void* prop_make(const uint32_t key[3], const net::Vec3& at, const net::Quat& rot)
{
    void* nm = noun_mgr();
    if (!nm || !a_CreateInstance || !a_OrnSetModel)
        return nullptr;
    void* u = skg::thiscall<void*>(a_CreateInstance, nm, kOrnamentNoun);
    if (!u || game::noun_id(u) != kOrnamentNoun)
        return nullptr;
    skg::vcall<void>(u, 0); // AddRef
    place(u, at, rot);
    skg::thiscall<void>(a_OrnSetModel, u, key[0], key[1], key[2]);
    if (void* sp = game::cast(u, 0x1186577)) // the model by its key, as cVehicle::Load (spatial vt 0x94 SetModelKey)
        skg::vcall<void>(sp, 0x94 / 4, key);
    return u;
}
static void prop_destroy(void* u, uint32_t serial)
{
    if (!u || !game::object_alive(u) || game::object_serial(u) != serial)
        return; // (gone with its world)
    if (void* nm = noun_mgr())
        skg::thiscall<void>(a_DestroyInstance, nm, u);
    skg::vcall<void>(u, 1); // Release our reference
}

// BUG-9294 (decision 7I A): the player's own choice (coop.ini [galaxy] other_eras, chat "/eras on|off"), on by default:
// off, the server passes this player's visits to nobody and sends it nobody's (net/galaxy.h ERAS)
static bool g_eras = true;
static bool g_eras_told; // the server of this connection knows it
static uint64_t g_eras_sent;

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    a_SpawnUFO = H->sym("Simulator::SpawnUFO");
    a_NounGet = H->sym("Simulator::cGameNounManager::Get");
    a_CreateInstance = H->sym("Simulator::cGameNounManager::CreateInstance");
    a_DestroyInstance = H->sym("Simulator::cGameNounManager::DestroyInstance");
    a_OrnSetModel = H->sym("SporeCoop::cOrnament_SetModel");
    savestrip::add_filter(savestrip_filter);
    galaxy::on_message(net::galaxy::VISITOR, on_visitor); // BUG-9287
    galaxy::on_message(net::galaxy::VISITOBJ, on_visit_objects); // BUG-9290
    galaxy_crt::init(H, M);
    a_GetDb = H->sym("Resource::GetDatabaseByID"); // BUG-9341
    if (uintptr_t ed = H->sym("Editors::cEditor::HandleMessage"))
        if (a_GetDb && H->hook_create(M, (void*)ed, (void*)&h_EdMsg, (void**)&o_EdMsg) != 0)
            o_EdMsg = nullptr;
    H->log(M, SK_INFO, "era visitors: the editor's snapshot guard %s (BUG-9341)", o_EdMsg ? "on" : "off (no hook)");
    g_eras = H->config_int(M, "galaxy", "other_eras", 1) != 0; // BUG-9294
}

static void eras_tell()
{
    net::Writer w;
    w.u8(g_eras ? 1 : 0);
    if (galaxy::send_message(net::galaxy::ERAS, w)) {
        g_eras_told = true;
        g_eras_sent++;
    }
}

std::string set_other_eras(bool on)
{
    g_eras = on;
    H->config_set_str(M, "galaxy", "other_eras", on ? "1" : "0");
    if (galaxy::welcomed())
        eras_tell();
    return on ? "Other eras shown: you and this planet's other stages see each other."
              : "Other eras hidden: you and this planet's other stages no longer see each other.";
}
bool other_eras() { return g_eras; }

bool chat_command(const std::string& t) // "/eras", "/eras on", "/eras off"
{
    if (t.size() < 5 || (t.compare(0, 5, "/eras") != 0 && t.compare(0, 5, "/ERAS") != 0) || (t.size() > 5 && t[5] != ' '))
        return false;
    std::string arg = t.size() > 6 ? t.substr(6) : "";
    while (!arg.empty() && arg.back() == ' ')
        arg.pop_back();
    for (auto& ch : arg)
        ch = (char)tolower((unsigned char)ch);
    std::string line;
    if (arg == "on" || arg == "off")
        line = set_other_eras(arg == "on");
    else
        line = std::string("Other eras are ") + (g_eras ? "shown" : "hidden") + ". Type /eras on or /eras off.";
    notify::show(line, "info");
    return true;
}

// ---- BUG-9287 (step 4 part 5, docs/12-server-and-players.md#server-era-visitors-built): visitors across eras ----
// Each player in a stage sends where its creature is (galaxy VISIT, twice a second); the galaxy server passes it to the
// players of the same planet in its other worlds (another era, a party's crossing). This game shows each such player
// look-only as a creature of its species with no AI (a puppet as a partner's: own_herd_for, puppet_register, kept out
// of saves), walking where the player walks. A creature drawn on a ground prop (the spike's carrier) shows nothing, so
// creatures go as creatures; another era's vehicles and buildings (props) come with a Civ era. Shown in the Creature
// and Tribe stages; a visitor whose species this game does not have is not shown (counted).
static bool g_stream = true;
COOP_FIX("galaxy.era_visitors", "BUG-9287", g_stream,
         "galaxy: players of the planet's other eras are shown as look-only creatures (and this player to them)");
struct Visitor {
    uint64_t owner = 0; // its player's identity (BUG-9290: whose creation its species key is)
    std::string name;
    uint8_t era = 0;
    uint32_t key[3]{};
    net::Vec3 pos{};
    double seen = 0;
    void* c = nullptr;
    uint32_t serial = 0;
    net::Vec3 walk_to{};
    bool walking = false;
    bool waiting = false, missing = false; // its creature asked from the store / not buildable here
    void* ship = nullptr; // BUG-9349: a Space era player's ship (a UFO of ours)
    uint32_t ship_serial = 0;
    double ship_wait = 0; // when its model was first asked from the store
    net::Vec3 ship_at{};  // where it shows here (lower than its player's zoom altitude)
};
static bool g_tags = true;
COOP_FIX("galaxy.visitor_tags", "BUG-9289", g_tags, "galaxy: a visitor of another era wears a name tag (its name, its era)");
static std::map<uint16_t, Visitor> g_visitors; // by the sender's galaxy slot
static double g_next_visit;
static uint64_t g_visits_sent, g_visits_got, g_shown, g_no_species, g_gone;
constexpr double kVisitorTimeout = 6.0;
constexpr float kTeleportBeyond = 30.f;

static bool visitor_alive(const Visitor& v)
{
    return v.c && game::creature_listed(v.c) && game::object_serial(v.c) == v.serial && game::object_alive(v.c);
}
static void ship_remove(Visitor& v); // BUG-9349 (below)
static void visitor_remove(Visitor& v)
{
    ship_remove(v);
    if (visitor_alive(v)) {
        creature_hp::forget_puppet(v.c);
        game::destroy_creature(v.c); // (the deferred path; it leaves the puppet list)
    } else if (v.c)
        game::puppet_unregister(v.c);
    v.c = nullptr;
    v.walking = false;
}
static bool shows_visitors(uint8_t era) { return era == net::galaxy::kEraCreature || era == net::galaxy::kEraTribe; }

// BUG-9349: a Space era player whose ship is at the planet is a ship over the planet in its other eras: a UFO of ours
// (SpawnUFO with the ship's model as installed from the creation store, the default UFO when it never comes) moved to
// where its player's ship is (on the sphere of its zoom altitude, as a partner's ship). Creature, Tribe and Civ games
// show it (they fly UFOs of their own); kept out of saves (savestrip_filter). Off: no ships sent or shown
static bool g_ships = true;
COOP_FIX("galaxy.space_ships", "BUG-9349", g_ships, "galaxy: a Space era player's ship at the planet shows as a UFO in its other eras");
constexpr uint32_t kUfoNoun = 0x018EBADCu;
constexpr float kShipOverGround = 60.f; // m: a visiting ship's height over the ground at most
static uint64_t g_ships_sent, g_ships_shown, g_ships_default;
static bool shows_ships(uint8_t era) { return shows_visitors(era) || era == net::galaxy::kEraCiv; }
static bool ship_alive(const Visitor& v)
{
    return v.ship && game::object_alive(v.ship) && game::object_serial(v.ship) == v.ship_serial;
}
static void ship_remove(Visitor& v)
{
    if (ship_alive(v)) {
        if (void* nm = noun_mgr())
            skg::thiscall<void>(a_DestroyInstance, nm, v.ship);
    }
    v.ship = nullptr;
}
static void ship_show(Visitor& v)
{
    if (!ship_alive(v)) {
        v.ship = nullptr;
        if (!a_SpawnUFO || !a_DestroyInstance)
            return;
        uint32_t lk[3] = {0, 0, 0};
        if (!galaxy_crt::local_key(v.owner, v.key, lk)) { // asked from the store: the default UFO after 15 s
            const double now = H->time_seconds();
            if (!v.ship_wait)
                v.ship_wait = now;
            if (now - v.ship_wait < 15)
                return;
            lk[0] = lk[1] = lk[2] = 0;
            g_ships_default++;
        }
        uint32_t pol = 0xFFFFFFFFu; // (no empire)
        void* u = skg::cdecl_call<void*>(a_SpawnUFO, 7, &pol, (void*)lk);
        if (!u)
            return;
        v.ship = u;
        v.ship_serial = game::object_serial(u);
        g_ships_shown++;
        H->log(M, SK_INFO, "era visitors: %s's ship (Space) shown, model %08X/%08X/%08X (BUG-9349)", v.name.c_str(), lk[0],
               lk[1], lk[2]);
    }
    const net::Vec3 up = norm(v.pos);
    g_axes = 0;
    // a Space ship flies at its player's zoom altitude (775 m from the centre seen, 2026-11-23: out of a Civ camera's
    // view): here it flies over the same point at most kShipOverGround above the ground, as the stages' own UFOs do
    const float r = std::sqrt(v.pos.x * v.pos.x + v.pos.y * v.pos.y + v.pos.z * v.pos.z);
    const float g = game::ground_radius(net::Vec3{up.x * 600, up.y * 600, up.z * 600});
    const float at = std::isfinite(g) && g > 10 ? std::min(r, g + kShipOverGround) : r;
    v.ship_at = net::Vec3{up.x * at, up.y * at, up.z * at};
    place(v.ship, v.ship_at, basis_quat(net::Vec3{0, 0, 1}, up));
}

static void on_visitor(net::Reader& r)
{
    const uint16_t slot = r.u16();
    const uint64_t owner = r.u64();
    std::string name = r.str8();
    const uint8_t era = r.u8();
    uint32_t key[3];
    for (auto& k : key)
        k = r.u32();
    net::Vec3 p;
    p.x = r.f32(), p.y = r.f32(), p.z = r.f32();
    if (!r.ok() || !g_stream || !slot || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        return;
    Visitor& v = g_visitors[slot];
    if ((v.c || v.ship) && (v.key[0] != key[0] || v.key[2] != key[2] || v.era != era)) // another creature now (an editor, a new stage)
        visitor_remove(v);
    v.owner = owner;
    v.name = std::move(name);
    v.era = era;
    memcpy(v.key, key, sizeof key);
    v.pos = p;
    v.seen = H->time_seconds();
    g_visits_got++;
}

static void visitor_show(Visitor& v, void* av)
{
    if (!visitor_alive(v)) {
        if (v.c)
            visitor_remove(v);
        // its player's creature as installed here from the galaxy's creation store (a key of ours: another creature of
        // this game may have its key, BUG-9290); not here yet: asked, shown later
        uint32_t lk[3];
        if (!galaxy_crt::local_key(v.owner, v.key, lk)) {
            v.waiting = true;
            return;
        }
        void* species = game::species_by_key({lk[0], lk[1], lk[2]});
        if (!species) {
            if (!v.waiting || !v.missing)
                g_no_species++;
            v.missing = true;
            return;
        }
        void* herd = game::own_herd_for(species);
        if (herd) {
            game::herd_home(herd, v.pos);
            game::herd_quiet(herd);
        } else
            herd = game::create_herd(v.pos, species);
        void* c = herd ? game::spawn_creature(v.pos, species, herd) : nullptr;
        if (!c)
            return;
        game::puppet_register(c);
        game::teleport(c, v.pos, av ? game::orientation(av) : net::Quat{0, 0, 0, 1});
        v.c = c;
        v.serial = game::object_serial(c);
        v.walking = false;
        g_shown++;
        H->log(M, SK_INFO, "era visitors: %s (%s) shown as %08X (BUG-9287)", v.name.c_str(), net::galaxy::era_name(v.era),
               v.key[0]);
        return;
    }
    const net::Vec3 at = game::position(v.c);
    const float d = net::dist(at, v.pos);
    // (it trails its player by 5-6 m on average while walking, at 2 VISITs a second and 10 FPS; aiming 0.5 s ahead on
    // the path did not change that (5.2 m against 5.7 m over 4 walks each, BUG-9289) and was taken out)
    const net::Vec3& target = v.pos;
    if (d > kTeleportBeyond) {
        game::teleport(v.c, v.pos, game::orientation(v.c));
        v.walking = false;
    } else if (d > 1.5f && (!v.walking || net::dist(v.walk_to, target) > 2.f)) {
        game::move_to(v.c, target, 1.f); // the game walks it there (its own walk animation)
        v.walk_to = target;
        v.walking = true;
    } else if (d <= 1.5f)
        v.walking = false;
}

static void objects_send(double now); // BUG-9290 (below)
static void objects_show(double now);
static void objs_clear();
static void solid_tick(uint8_t era); // BUG-9293 (below)
static void stream_tick(double now)
{
    const std::string planet = galaxy::current_planet();
    const uint8_t era = net::galaxy::era_of(planet);
    // this player's creature: the avatar (Creature), the tribe's chieftain (Tribe: the game has no avatar there)
    void* av = game::avatar() ? game::avatar() : era == net::galaxy::kEraTribe ? tribe::player_chief() : nullptr;
    if (!galaxy::welcomed())
        g_eras_told = false;
    else if (!g_eras_told)
        eras_tell(); // BUG-9294: on each connection
    if (!g_stream || !g_eras || planet.empty() || !galaxy::welcomed()) {
        for (auto& [s, v] : g_visitors)
            visitor_remove(v);
        g_visitors.clear();
        objs_clear();
        return;
    }
    if (era == net::galaxy::kEraCiv && galaxy::hosting()) // BUG-9290: the Civ era's vehicles and buildings
        objects_send(now);
    if (shows_visitors(era) && av)
        objects_show(now);
    else
        objs_clear();
    float ship_p[3];
    uint32_t ship_k[3];
    if (!av && g_ships && era == net::galaxy::kEraSpace && now >= g_next_visit &&
        spacecoop::ship_on_planet(ship_p, ship_k)) { // BUG-9349: this player's ship at the planet
        g_next_visit = now + net::galaxy::kVisitEveryMs / 1000.0;
        net::Writer w;
        w.u8(era);
        for (uint32_t k : ship_k)
            w.u32(k);
        w.f32(ship_p[0]), w.f32(ship_p[1]), w.f32(ship_p[2]);
        if (galaxy::send_message(net::galaxy::VISIT, w)) {
            g_ships_sent++;
            galaxy_crt::put(ship_k[0], ship_k[1], ship_k[2]);
        }
    }
    if (av && now >= g_next_visit) { // where this player is, for the planet's other worlds
        g_next_visit = now + net::galaxy::kVisitEveryMs / 1000.0;
        const game::Key k = game::species_key(av);
        const uint32_t key[3] = {k.instance, k.type, k.group};
        const net::Vec3 p = game::position(av);
        net::Writer w;
        w.u8(era);
        for (uint32_t v : key)
            w.u32(v);
        w.f32(p.x), w.f32(p.y), w.f32(p.z);
        if (k.instance && galaxy::send_message(net::galaxy::VISIT, w)) {
            g_visits_sent++;
            galaxy_crt::put(k.instance, k.type, k.group); // the store keeps it for the other eras' games
        }
    }
    for (auto it = g_visitors.begin(); it != g_visitors.end();) {
        Visitor& v = it->second;
        if (now - v.seen > kVisitorTimeout) { // it left (another planet, a menu) or its game stopped
            visitor_remove(v);
            g_gone++;
            it = g_visitors.erase(it);
            continue;
        }
        if (v.era == net::galaxy::kEraSpace) { // BUG-9349: a ship
            if (g_ships && shows_ships(era))
                ship_show(v);
            else
                ship_remove(v);
        } else if (av && shows_visitors(era))
            visitor_show(v, av);
        else if (v.c)
            visitor_remove(v);
        ++it;
    }
    if (shows_visitors(era))
        solid_tick(era);
}

static bool ship_ours(void* obj)
{
    for (auto& [s, v] : g_visitors)
        if (v.ship == obj)
            return true;
    return false;
}

// BUG-9289: a name tag over each visitor near this player (its name, its era), in grey: a visitor, not a partner
void overlay(int sw, int sh)
{
    if (!g_stream || !g_tags || g_visitors.empty() || sw <= 0 || sh <= 0)
        return;
    const std::string planet = galaxy::current_planet();
    void* me = game::avatar() ? game::avatar()
                              : net::galaxy::era_of(planet) == net::galaxy::kEraTribe ? tribe::player_chief() : nullptr;
    const net::Vec3 mine = me ? game::position(me) : net::Vec3{};
    for (auto& [s, v] : g_visitors) {
        // BUG-9349: a ship's tag over it wherever it shows (a Civ game has no avatar to measure from), 10 m above
        const bool ship = g_ships && ship_alive(v);
        if (!ship && (!me || !visitor_alive(v)))
            continue;
        const net::Vec3 p = ship ? v.ship_at : game::position(v.c);
        const float dist = ship ? 0.f : net::dist(p, mine);
        if (!std::isfinite(dist) || dist > 120.f)
            continue;
        const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        const float up = len > 1 ? (ship ? 10.f : modes::creature_tag_height(v.c)) / len : 0; // over its head (as a partner's tag)
        float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2] = {NAN, NAN};
        H->world_to_screen(w, sc);
        if (!std::isfinite(sc[0]) || !std::isfinite(sc[1]) || sc[0] < 40 || sc[0] > sw - 40 || sc[1] < 40 || sc[1] > sh - 40)
            continue;
        const std::string shown = utf8::shown_name(v.name); // (as the Social Menu: no -NoShare- test tag)
        const std::string name = shown.substr(0, utf8::cut_bytes(shown, 32));
        const std::string era = std::string(net::galaxy::era_name(v.era)) + " era";
        const float bw = (float)std::max(utf8::overlay_text_px(name), utf8::overlay_text_px(era)) + 20, bh = 38;
        const float x1 = sc[0] - bw * .5f, y1 = sc[1] - bh;
        partner_marker::plate(H, x1, y1, x1 + bw, y1 + bh, 17, 0x8C282828, 0xC8A0A0A0);
        const int nx = (int)(sc[0] - utf8::overlay_text_px(name) / 2), ex = (int)(sc[0] - utf8::overlay_text_px(era) / 2);
        H->overlay_text(nx + 1, (int)y1 + 5, 0xFF000000, name.c_str());
        H->overlay_text(nx, (int)y1 + 4, 0xFFE6E6E6, name.c_str());
        H->overlay_text(ex, (int)y1 + 19, 0xFFB4BEC8, era.c_str());
    }
}

// ---- BUG-9290: a Civ era's vehicles and buildings in the other eras (look-only props, decision 5T B) ----
// The Civ era's host sends its cities' buildings and its vehicles near the other eras' players it hears of (VISITOR:
// within kObjRadius), the vehicles twice a second, the buildings every 4 s, and puts their designs in the galaxy's
// creation store. A Creature or Tribe game stands each on its ground as a prop with that model (installed from the
// store under a key of its own; the game's own designs as they are), a vehicle moved on along its velocity between
// the updates. Props are kept out of saves and go when their object is no longer sent.
struct ObjProp {
    uint64_t owner = 0;
    uint32_t key[3]{};
    uint8_t kind = 0;
    net::Vec3 pos{}, vel{};
    net::Quat rot{0, 0, 0, 1};
    double seen = 0;
    void* u = nullptr;
    uint32_t serial = 0;
    bool waiting = false;
    net::Vec3 made_at{}; // BUG-9291: drawn only near here
};
static std::map<std::pair<uint16_t, uint32_t>, ObjProp> g_objs;
static uint64_t g_props_remade;
static bool g_remake = true;
COOP_FIX("galaxy.prop_remake", "BUG-9291", g_remake,
         "galaxy: another era's moving vehicle is made again where it drives (a prop draws only near where it was made)");
constexpr float kRemakeBeyond = 6.f; // m from where a prop was made (a truck's box is ~8 m across) // (sender slot, its object's id)
struct Sent {
    net::Vec3 pos{};
    double at = 0;
};
static std::map<uint32_t, Sent> g_obj_last; // host: an object's last sent place (its velocity)
static double g_next_vehicles, g_next_buildings;
static uint64_t g_objs_sent, g_obj_msgs, g_objs_got, g_props_made, g_props_gone;
constexpr float kObjRadius = 250.f;
constexpr size_t kMaxObjProps = 150;
constexpr double kVehicleTimeout = 3.0, kBuildingTimeout = 12.0;

static bool our_object_prop(void* obj)
{
    for (auto& [k, o] : g_objs)
        if (o.u == obj)
            return true;
    return false;
}

static void objs_clear()
{
    for (auto& [k, o] : g_objs)
        prop_destroy(o.u, o.serial);
    g_objs.clear();
}

static void on_visit_objects(net::Reader& r)
{
    const uint16_t slot = r.u16();
    const uint64_t owner = r.u64();
    const uint8_t n = r.u8();
    const double now = H->time_seconds();
    for (uint8_t i = 0; i < n && r.ok(); i++) {
        const uint32_t id = r.u32();
        uint32_t key[3];
        for (auto& k : key)
            k = r.u32();
        net::Vec3 p, v;
        net::Quat q;
        p.x = r.f32(), p.y = r.f32(), p.z = r.f32();
        q.x = r.f32(), q.y = r.f32(), q.z = r.f32(), q.w = r.f32();
        v.x = r.f32(), v.y = r.f32(), v.z = r.f32();
        const uint8_t kind = r.u8();
        if (!r.ok() || !g_stream || !std::isfinite(p.x + p.y + p.z + q.x + q.y + q.z + q.w + v.x + v.y + v.z))
            return;
        ObjProp& o = g_objs[{slot, id}];
        if (o.u && memcmp(o.key, key, sizeof key)) { // another model at that id: made again
            prop_destroy(o.u, o.serial);
            o.u = nullptr;
        }
        o.owner = owner;
        memcpy(o.key, key, sizeof key);
        o.kind = kind, o.pos = p, o.vel = v, o.rot = q, o.seen = now;
        g_objs_got++;
    }
}

// host of a Civ era: its objects near the other eras' players, to them
static void objects_send(double now)
{
    const bool vehicles = now >= g_next_vehicles, buildings = now >= g_next_buildings;
    if (!vehicles && !buildings)
        return;
    if (vehicles)
        g_next_vehicles = now + net::galaxy::kVisitEveryMs / 1000.0;
    if (buildings)
        g_next_buildings = now + 4.0;
    struct U {
        bool vehicles, buildings;
        double now;
        std::vector<net::Vec3> close_to;
        net::Writer w;
        uint8_t n = 0;
        size_t sent = 0;
    } u{vehicles, buildings, now, {}};
    for (auto& [s, v] : g_visitors)
        u.close_to.push_back(v.pos);
    if (u.close_to.empty())
        return;
    auto flush = [](U& u) {
        if (!u.n)
            return;
        net::Writer m;
        m.u8(u.n);
        m.data.insert(m.data.end(), u.w.data.begin(), u.w.data.end());
        if (galaxy::send_message(net::galaxy::VISITOBJ, m))
            g_obj_msgs++;
        u.w = net::Writer{};
        u.n = 0;
    };
    civ::for_each_visit_object(
        [](void* o, const uint32_t key[3], bool vehicle, void* up) {
            auto& u = *(U*)up;
            if ((vehicle && !u.vehicles) || (!vehicle && !u.buildings) || u.sent >= 2 * 24)
                return;
            net::Vec3 p;
            if (!game::object_position(o, p))
                return;
            bool close = false;
            for (auto& v : u.close_to)
                close = close || net::dist(v, p) <= kObjRadius;
            if (!close)
                return;
            void* sp = game::cast(o, 0x1186577);
            const float* q = sp ? (const float*)((uint8_t*)sp + 0x10) : nullptr; // cSpatialObject::mOrientation
            const uint32_t id = game::object_serial(o);
            net::Vec3 vel{};
            Sent& last = g_obj_last[id];
            const double dt = u.now - last.at;
            if (vehicle && last.at > 0 && dt > 0.1 && dt < 2.0)
                vel = {(float)((p.x - last.pos.x) / dt), (float)((p.y - last.pos.y) / dt), (float)((p.z - last.pos.z) / dt)};
            last = {p, u.now};
            u.w.u32(id);
            for (int i = 0; i < 3; i++)
                u.w.u32(key[i]);
            u.w.f32(p.x), u.w.f32(p.y), u.w.f32(p.z);
            u.w.f32(q ? q[0] : 0.f), u.w.f32(q ? q[1] : 0.f), u.w.f32(q ? q[2] : 0.f), u.w.f32(q ? q[3] : 1.f);
            u.w.f32(vel.x), u.w.f32(vel.y), u.w.f32(vel.z);
            u.w.u8(vehicle ? net::galaxy::kVisitVehicle : net::galaxy::kVisitBuilding);
            galaxy_crt::put(key[0], key[1], key[2]); // its design, for games that do not have it
            u.n++;
            u.sent++;
            g_objs_sent++;
            if (u.n >= 24) {
                net::Writer m;
                m.u8(u.n);
                m.data.insert(m.data.end(), u.w.data.begin(), u.w.data.end());
                if (galaxy::send_message(net::galaxy::VISITOBJ, m))
                    g_obj_msgs++;
                u.w = net::Writer{};
                u.n = 0;
            }
        },
        &u);
    flush(u);
}

// a Creature or Tribe game: each object as a prop on this game's ground
static void objects_show(double now)
{
    for (auto it = g_objs.begin(); it != g_objs.end();) {
        ObjProp& o = it->second;
        const double age = now - o.seen;
        if (age > (o.kind == net::galaxy::kVisitVehicle ? kVehicleTimeout : kBuildingTimeout)) {
            prop_destroy(o.u, o.serial);
            g_props_gone++;
            it = g_objs.erase(it);
            continue;
        }
        ++it;
        if (o.u && (!game::object_alive(o.u) || game::object_serial(o.u) != o.serial))
            o.u = nullptr; // (gone with a load)
        const float t = (float)std::min(age, 1.0);
        const net::Vec3 at{o.pos.x + o.vel.x * t, o.pos.y + o.vel.y * t, o.pos.z + o.vel.z * t};
        const net::Vec3 up = norm(at);
        const float g = game::ground_radius(net::Vec3{up.x * 600, up.y * 600, up.z * 600});
        const net::Vec3 ground{up.x * g, up.y * g, up.z * g};
        if (g_remake && o.u && net::dist(ground, o.made_at) > kRemakeBeyond) { // BUG-9291: made again where it is now
            prop_destroy(o.u, o.serial);
            o.u = nullptr;
            g_props_remade++;
        }
        if (!o.u) {
            uint32_t lk[3];
            size_t live = 0;
            for (auto& [k, x] : g_objs)
                live += x.u != nullptr;
            if (live >= kMaxObjProps || !galaxy_crt::local_key(o.owner, o.key, lk)) {
                o.waiting = true;
                continue;
            }
            creations::Model m = creations::ensure_model({lk[0], lk[1], lk[2]},
                                                         o.kind == net::galaxy::kVisitVehicle ? creations::kMkVehicle
                                                                                              : creations::kMkBuilding);
            if (m.key.instance)
                lk[0] = m.key.instance, lk[1] = m.key.type, lk[2] = m.key.group;
            if (!baked_or_ask(lk)) { // (made once its model is baked)
                o.waiting = true;
                continue;
            }
            o.u = prop_make(lk, ground, o.rot);
            o.made_at = ground;
            if (!o.u)
                continue;
            o.serial = game::object_serial(o.u);
            o.waiting = false;
            g_props_made++;
        }
        place(o.u, ground, o.rot);
    }
}

// ---- BUG-9293 (decision 7H A): another era's buildings, vehicles and visitors are solid for this game's creatures ----
// The props have no physics body of their own (look-only ornaments), so once a frame this game's own creatures (the
// avatar in Creature; the player tribe's members in Tribe, on the game that simulates them) are pushed out of each
// prop's box, turned with the prop, on the ground's tangent plane. A visitor's creature is a creature of this game
// (Havok's own collision blocks it), but it walks 5-6 m behind its player: its block reaches on to where its player last
// said it was (decision 7H, the owner's "increase the size of the hitbox ... to adjust for latency"), at most kStretchMax.
static bool g_solid = true;
COOP_FIX("galaxy.visitor_solid", "BUG-9293", g_solid,
         "galaxy: this game's own creatures cannot walk through another era's buildings, vehicles and visitors");
static uint64_t g_solid_pushes, g_solid_stretch_pushes;
static float g_stretch_closest = 1e9f, g_stretch_r; // tests: the nearest a stretch came to this game's creature
static float g_solid_last_push;
// (the stretch reaches as far as a visitor can trail before it is put where its player is: a running Tribe chief trailed
// 10-27 m at 10 FPS, so 10 m missed it)
constexpr float kStretchMax = kTeleportBeyond, kSolidNear = 60.f;
// a creature this far below a prop's base is still blocked: a slope, or another world's ground lower than the sender's
// (the prop then stands on its own pillars, images/stage4_civ_visitors.png)
constexpr float kSolidBelow = 15.f;

static net::Vec3 sub(net::Vec3 a, net::Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static float len(net::Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
static net::Vec3 qrot(const net::Quat& q, net::Vec3 v) // v turned by q
{
    const net::Vec3 u{q.x, q.y, q.z};
    net::Vec3 t = cross(u, v);
    t = {2 * t.x, 2 * t.y, 2 * t.z};
    const net::Vec3 c = cross(u, t);
    return {v.x + q.w * t.x + c.x, v.y + q.w * t.y + c.y, v.z + q.w * t.z + c.z};
}
// a prop's box in its own frame (min x y z, max x y z: spatial vt 0x68, as the game sizes a held item)
static bool prop_box(void* u, net::Vec3& at, net::Quat& q, float b[6])
{
    void* sp = game::cast(u, 0x1186577);
    if (!sp)
        return false;
    const float* bb = skg::vcall<const float*>(sp, 0x68 / 4);
    if (!bb || !skg::readable(bb, 24))
        return false;
    for (int i = 0; i < 6; i++)
        if (!std::isfinite(bb[i]))
            return false;
    memcpy(b, bb, 24);
    const float* f = (const float*)((uint8_t*)sp + 4);
    at = {f[0], f[1], f[2]};
    q = {f[3], f[4], f[5], f[6]}; // +0x10 the orientation
    return std::isfinite(at.x + at.y + at.z + q.x + q.y + q.z + q.w);
}
// keeps p at the radius (height) it had: a push moves along the ground, never up or down
static net::Vec3 same_height(net::Vec3 p, float r)
{
    const float l = len(p);
    return l > 1 ? net::Vec3{p.x * r / l, p.y * r / l, p.z * r / l} : p;
}

struct Box {
    net::Vec3 at;
    net::Quat q;
    float b[6];
};
static std::vector<Box> g_boxes; // this frame's props (read once a frame, not once per creature)

static void push_out(void* c)
{
    net::Vec3 m = game::position(c);
    const float mr = len(m);
    if (!std::isfinite(mr) || mr < 1)
        return;
    const float cr = std::min(std::max(game::bounding_radius(c) * 0.5f, 0.3f), 3.f);
    bool moved = false;
    for (const Box& x : g_boxes) { // the buildings and vehicles: out of the box, by its nearest side
        const net::Vec3& at = x.at;
        const net::Quat& q = x.q;
        const float* b = x.b;
        if (net::dist(at, m) > kSolidNear)
            continue;
        const net::Quat qi{-q.x, -q.y, -q.z, q.w};
        net::Vec3 l = qrot(qi, sub(m, at));
        if (l.z < b[2] - kSolidBelow || l.z > b[5]) // (under its ground by more: a cave; over its top: free)
            continue;
        const float d[4] = {l.x - (b[0] - cr), (b[3] + cr) - l.x, l.y - (b[1] - cr), (b[4] + cr) - l.y};
        if (d[0] <= 0 || d[1] <= 0 || d[2] <= 0 || d[3] <= 0)
            continue;
        int i = 0;
        for (int j = 1; j < 4; j++)
            if (d[j] < d[i])
                i = j;
        if (i == 0)
            l.x = b[0] - cr;
        else if (i == 1)
            l.x = b[3] + cr;
        else if (i == 2)
            l.y = b[1] - cr;
        else
            l.y = b[4] + cr;
        m = same_height(add(at, qrot(q, l), 1.f), mr);
        g_solid_last_push = d[i];
        g_solid_pushes++;
        moved = true;
    }
    for (auto& [s, v] : g_visitors) { // a visitor's stretch: from its creature to where its player is
        if (!visitor_alive(v))
            continue;
        const net::Vec3 a = game::position(v.c);
        net::Vec3 ab = sub(v.pos, a);
        const float l = len(ab);
        if (l < 1.f || net::dist(a, m) > kSolidNear)
            continue;
        const float lc = std::min(l, kStretchMax);
        ab = {ab.x / l, ab.y / l, ab.z / l};
        const net::Vec3 am = sub(m, a);
        const float t = std::min(std::max(am.x * ab.x + am.y * ab.y + am.z * ab.z, 0.f), lc);
        if (t <= 0.f) // (by its creature itself: Havok's)
            continue;
        const net::Vec3 near_pt = add(a, ab, t);
        const float r = std::min(std::max(game::bounding_radius(v.c) * 0.5f, 0.3f), 3.f) + cr;
        net::Vec3 away = sub(m, near_pt);
        const float up = (away.x * m.x + away.y * m.y + away.z * m.z) / mr; // (on the ground's plane)
        away = add(away, m, -up / mr);
        const float dl = len(away);
        if (dl < g_stretch_closest)
            g_stretch_closest = dl, g_stretch_r = r;
        if (dl >= r || dl < 1e-3f)
            continue;
        m = same_height(add(near_pt, away, r / dl), mr);
        g_solid_last_push = r - dl;
        g_solid_stretch_pushes++;
        moved = true;
    }
    if (moved)
        game::place(c, m, game::orientation(c)); // (SetPosition alone: the locomotion put it back next frame)
}

// tests (BUG-9293, `coop.visit onstretch`): once a visitor trails its player by 4 m or more, this game's avatar is put
// on the middle of that gap (the stretch); the status then says how far it was moved off that spot
static bool g_onstretch;
static net::Vec3 g_onstretch_at{};
static double g_onstretch_when = -1;
static float g_onstretch_lag;
std::string onstretch()
{
    g_onstretch = true;
    g_onstretch_when = -1;
    return "armed: the avatar goes onto the next visitor's stretch of 4 m or more";
}
static void onstretch_tick()
{
    void* av = game::avatar();
    if (!g_onstretch || !av)
        return;
    for (auto& [s, v] : g_visitors) {
        if (!visitor_alive(v))
            continue;
        const net::Vec3 a = game::position(v.c);
        const float l = net::dist(a, v.pos);
        if (l < 4.f || l > kStretchMax)
            continue;
        const net::Vec3 mid{(a.x + v.pos.x) / 2, (a.y + v.pos.y) / 2, (a.z + v.pos.z) / 2};
        const net::Vec3 up = norm(mid);
        const float g = game::ground_radius(net::Vec3{up.x * 600, up.y * 600, up.z * 600});
        g_onstretch_at = {up.x * g, up.y * g, up.z * g};
        game::place(av, g_onstretch_at, game::orientation(av));
        g_onstretch_when = H->time_seconds();
        g_onstretch_lag = l;
        g_onstretch = false;
        return;
    }
}

static void solid_tick(uint8_t era)
{
    if (era == net::galaxy::kEraCreature)
        onstretch_tick(); // (tests)
    if (!g_solid || (g_objs.empty() && g_visitors.empty()))
        return;
    g_boxes.clear();
    for (auto& [k, o] : g_objs) {
        Box x;
        if (o.u && game::object_alive(o.u) && game::object_serial(o.u) == o.serial && prop_box(o.u, x.at, x.q, x.b))
            g_boxes.push_back(x);
    }
    if (era == net::galaxy::kEraCreature) {
        if (void* av = game::avatar())
            push_out(av);
    } else if (era == net::galaxy::kEraTribe && galaxy::hosting()) { // (members are the simulating game's)
        tribe::for_each_owned([](void* c, uint8_t, void*) { push_out(c); }, nullptr);
    }
}

std::string go(const net::Vec3& to, int mode)
{
    const uint8_t era = net::galaxy::era_of(galaxy::current_planet());
    void* me = game::avatar() ? game::avatar() : era == net::galaxy::kEraTribe ? tribe::player_chief() : nullptr;
    if (!me)
        return "no creature";
    const net::Vec3 at = game::position(me);
    if (to.x == 0 && to.y == 0 && to.z == 0) {
        char b[100];
        snprintf(b, sizeof b, "at (%.1f %.1f %.1f)", at.x, at.y, at.z);
        return b;
    }
    g_stretch_closest = 1e9f; // (a new test run)
    if (mode == 2) { // tp: put there (on the ground)
        const net::Vec3 up = norm(to);
        const float g = game::ground_radius(net::Vec3{up.x * 600, up.y * 600, up.z * 600});
        game::teleport(me, net::Vec3{up.x * g, up.y * g, up.z * g}, game::orientation(me));
    } else
        game::move_to(me, to, 0.5f);
    if (mode == 1)
        game::set_speed(me, 2.f);
    char b[140];
    snprintf(b, sizeof b, "from (%.1f %.1f %.1f) to (%.1f %.1f %.1f), %.0f m", at.x, at.y, at.z, to.x, to.y, to.z, net::dist(at, to));
    return b;
}

std::string walk_into(int n)
{
    void* av = game::avatar();
    int i = 0;
    for (auto& [k, o] : g_objs)
        if (i++ == n) {
            net::Vec3 at;
            if (!av || !o.u || !game::object_position(o.u, at))
                return "no avatar or no prop";
            game::move_to(av, at, 0.5f);
            char b[120];
            snprintf(b, sizeof b, "walking to object %d at (%.0f %.0f %.0f), %.0f m", n, at.x, at.y, at.z,
                     net::dist(at, game::position(av)));
            return b;
        }
    return "no such object";
}

std::string stream_status()
{
    char b[360];
    snprintf(b, sizeof b,
             "era visitors (BUG-9287): on=%d other_eras=%d (told %llu) planet='%s' sent=%llu got=%llu visitors=%u shown=%llu no_species=%llu gone=%llu\n",
             (int)g_stream, (int)g_eras, (unsigned long long)g_eras_sent, galaxy::current_planet().c_str(),
             (unsigned long long)g_visits_sent,
             (unsigned long long)g_visits_got, (unsigned)g_visitors.size(), (unsigned long long)g_shown,
             (unsigned long long)g_no_species, (unsigned long long)g_gone);
    std::string out = b;
    { // BUG-9349
        unsigned live = 0;
        for (auto& [k, v] : g_visitors)
            live += ship_alive(v);
        snprintf(b, sizeof b, "space ships (BUG-9349): on=%d sent=%llu shown=%llu default_model=%llu live=%u\n", (int)g_ships,
                 (unsigned long long)g_ships_sent, (unsigned long long)g_ships_shown, (unsigned long long)g_ships_default, live);
        out += b;
        for (auto& [k, v] : g_visitors) // (tests: where each ship is and where it shows on the screen)
            if (ship_alive(v)) {
                float w[3] = {v.ship_at.x, v.ship_at.y, v.ship_at.z}, sc[2] = {NAN, NAN};
                H->world_to_screen(w, sc);
                snprintf(b, sizeof b, "  ship of %s: sent (%.0f %.0f %.0f) r=%.0f, shown at r=%.0f, screen (%.0f %.0f)\n",
                         v.name.c_str(), v.pos.x, v.pos.y, v.pos.z,
                         std::sqrt(v.pos.x * v.pos.x + v.pos.y * v.pos.y + v.pos.z * v.pos.z),
                         std::sqrt(v.ship_at.x * v.ship_at.x + v.ship_at.y * v.ship_at.y + v.ship_at.z * v.ship_at.z), sc[0], sc[1]);
                out += b;
                const float* up = (const float*)((const uint8_t*)v.ship + 0x38); // (cGameDataUFO position, spacecoop kPos)
                const uint32_t* mk = (const uint32_t*)((const uint8_t*)v.ship + 0x780);
                snprintf(b, sizeof b, "    the UFO is at (%.0f %.0f %.0f) r=%.0f, model %08X/%08X/%08X, ship_at (%.0f %.0f %.0f)\n", up[0], up[1], up[2],
                         std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]), mk[0], mk[1], mk[2], v.ship_at.x, v.ship_at.y,
                         v.ship_at.z);
                out += b;
            }
    }
    {
        size_t live = 0, waiting = 0;
        for (auto& [k, o] : g_objs)
            live += o.u != nullptr, waiting += o.waiting;
        snprintf(b, sizeof b, "  objects (BUG-9290): sent %llu in %llu messages; got %llu, now %u (props %u, waiting %u), made %llu (again %llu), gone %llu, bakes asked %llu (pending %llu, not asked in this stage %llu); editor snapshots %llu, skipped %llu\n",
                 (unsigned long long)g_objs_sent, (unsigned long long)g_obj_msgs, (unsigned long long)g_objs_got,
                 (unsigned)g_objs.size(), (unsigned)live, (unsigned)waiting, (unsigned long long)g_props_made,
                 (unsigned long long)g_props_remade, (unsigned long long)g_props_gone, (unsigned long long)g_bakes_asked,
                 (unsigned long long)g_bakes_pending, (unsigned long long)g_bakes_skipped, (unsigned long long)g_snaps_seen,
                 (unsigned long long)g_snaps_skipped);
        out += b;
        snprintf(b, sizeof b, "  solid (BUG-9293): on=%d pushes %llu, by a stretch %llu, last %.2f m; nearest stretch %.2f m (its reach %.2f)\n",
                 (int)g_solid, (unsigned long long)g_solid_pushes, (unsigned long long)g_solid_stretch_pushes, g_solid_last_push,
                 g_stretch_closest, g_stretch_r);
        out += b;
        if (g_onstretch_when >= 0 && game::avatar()) {
            snprintf(b, sizeof b, "  onstretch: put on a %.1f m stretch %.1f s ago; now %.2f m from that spot\n", g_onstretch_lag,
                     H->time_seconds() - g_onstretch_when, net::dist(game::position(game::avatar()), g_onstretch_at));
        } else
            snprintf(b, sizeof b, "  onstretch: %s\n", g_onstretch ? "armed" : "-");
        out += b;
        int listed = 0;
        for (auto& [k, o] : g_objs) {
            if (listed++ >= 12)
                break;
            net::Vec3 at{};
            const bool alive = o.u && game::object_alive(o.u) && game::object_position(o.u, at);
            float w[3] = {at.x, at.y, at.z}, sc[2] = {NAN, NAN};
            if (alive)
                H->world_to_screen(w, sc);
            snprintf(b, sizeof b, "    %s %08X/%08X sent (%.0f %.0f %.0f) prop %s at (%.0f %.0f %.0f) screen (%.0f %.0f)\n",
                     o.kind == net::galaxy::kVisitVehicle ? "vehicle" : "building", o.key[0], o.key[1], o.pos.x, o.pos.y,
                     o.pos.z, alive ? "alive" : "none", at.x, at.y, at.z, sc[0], sc[1]);
            out += b;
            net::Vec3 pa;
            net::Quat pq;
            float bx[6];
            if (alive && prop_box(o.u, pa, pq, bx)) { // BUG-9293: its box, and the avatar in the prop's frame
                net::Vec3 l{};
                if (void* av = game::avatar())
                    l = qrot({-pq.x, -pq.y, -pq.z, pq.w}, sub(game::position(av), pa));
                snprintf(b, sizeof b, "      box (%.1f %.1f %.1f)..(%.1f %.1f %.1f) avatar in its frame (%.1f %.1f %.1f)\n",
                         bx[0], bx[1], bx[2], bx[3], bx[4], bx[5], l.x, l.y, l.z);
                out += b;
            }
        }
    }
    for (auto& [s, v] : g_visitors) {
        net::Vec3 at{};
        const bool alive = visitor_alive(v);
        if (alive)
            at = game::position(v.c);
        snprintf(b, sizeof b, "  slot %u '%s' (%s) species %08X at (%.0f %.0f %.0f) %s, %.1f m from it\n", s, v.name.c_str(),
                 net::galaxy::era_name(v.era), v.key[0], v.pos.x, v.pos.y, v.pos.z, alive ? "shown" : "not shown",
                 alive ? net::dist(at, v.pos) : 0.f);
        out += b;
    }
    return out;
}

void tick(double now)
{
    stream_tick(now);
    galaxy_crt::tick(now);
    const float dt = g_last > 0 ? (float)std::min(now - g_last, 0.25) : 0.f;
    g_last = now;
    if (g_props.empty())
        return;
    if (!game::in_creature_stage()) { // (left the stage: the objects go with the world)
        g_props.clear();
        return;
    }
    g_frames++;
    for (auto& pr : g_props) {
        if (!pr.u || !game::object_alive(pr.u) || game::object_serial(pr.u) != pr.serial)
            continue;
        g_axes = pr.axes;
        if (pr.drive)
            drive_prop(pr, dt);
        else { // held in place (the ship's own flight would move it)
            net::Vec3 p;
            net::Quat q;
            pose_at(pr.centre, pr.fwd, pr.lift, p, q);
            if (pr.axes == 1)
                q = pr.avq;
            place(pr.u, p, q);
        }
    }
}

} // namespace era_visitors
