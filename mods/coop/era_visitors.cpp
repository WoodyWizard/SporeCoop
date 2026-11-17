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
#include "savestrip.h"
#include "game.h"
#include "net/protocol.h"
#include "sporekit_game.hpp"

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
    pr.centre = add(ap, fwd, 25);
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

std::string status()
{
    std::string out = "era visitors (spike): " + std::to_string(g_props.size()) + " prop(s), frames " + std::to_string(g_frames) + "\n";
    for (auto& pr : g_props) {
        net::Vec3 v{};
        const bool alive = pr.u && game::object_alive(pr.u) && game::object_serial(pr.u) == pr.serial;
        if (alive)
            game::object_position(pr.u, v);
        char b[200];
        snprintf(b, sizeof b, "  %s %p %s model %08X/%08X/%08X %s pos=(%.1f %.1f %.1f) ground=%.1f\n", pr.prop ? "prop" : "ufo", pr.u, alive ? "alive" : "gone",
                 pr.key[0], pr.key[1], pr.key[2], pr.drive ? "driving" : "standing", v.x, v.y, v.z,
                 alive ? game::ground_radius(v) : 0.f);
        out += b;
    }
    return out;
}

// our props are not the player's world (CLAUDE.md 2): a save never holds them
static bool savestrip_filter(void* obj, uint32_t noun)
{
    if (noun != kOrnamentNoun)
        return false;
    for (auto& pr : g_props)
        if (pr.prop && pr.u == obj)
            return true;
    return false;
}

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
}

void tick(double now)
{
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
