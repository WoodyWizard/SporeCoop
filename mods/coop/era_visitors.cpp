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
#include "partner_marker.h"
#include "savestrip.h"
#include "civ.h"
#include "tribe.h"
#include "sporekit_game.hpp"

#include <cstring>
#include <map>

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

std::string stream_status();
std::string status()
{
    std::string out = stream_status() + galaxy_crt::status() + "era visitors (spike): " + std::to_string(g_props.size()) + " prop(s), frames " + std::to_string(g_frames) + "\n";
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

static void on_visitor(net::Reader& r); // BUG-9287 (below)
static void on_visit_objects(net::Reader& r); // BUG-9290 (below)
static bool our_object_prop(void* obj);       // BUG-9290 (below)

// our props are not the player's world (CLAUDE.md 2): a save never holds them
static bool savestrip_filter(void* obj, uint32_t noun)
{
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
static uint64_t g_bakes_asked;
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
    for (int l = 0; l < 4; l++)
        if (!(lods & (1 << l))) {
            int lod = l;
            skg::vcall<void>(bm, 0x4C / 4, key, &lod);
            g_bakes_asked++;
        }
    H->log(M, SK_INFO, "era visitors: %08X/%08X baked levels %X: asked the rest (BUG-9290)", key[0], key[1], lods);
    return (lods & 1) != 0; // (drawn close up already; the farther levels come)
}

// a plain ornament carrying a creation's model (decision 5T B), AddRef held: ours until prop_destroy. Null: not made
static void* prop_make(const uint32_t key[3])
{
    void* nm = noun_mgr();
    if (!nm || !a_CreateInstance || !a_OrnSetModel)
        return nullptr;
    void* u = skg::thiscall<void*>(a_CreateInstance, nm, kOrnamentNoun);
    if (!u || game::noun_id(u) != kOrnamentNoun)
        return nullptr;
    skg::vcall<void>(u, 0); // AddRef
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
static void visitor_remove(Visitor& v)
{
    if (visitor_alive(v)) {
        creature_hp::forget_puppet(v.c);
        game::destroy_creature(v.c); // (the deferred path; it leaves the puppet list)
    } else if (v.c)
        game::puppet_unregister(v.c);
    v.c = nullptr;
    v.walking = false;
}
static bool shows_visitors(uint8_t era) { return era == net::galaxy::kEraCreature || era == net::galaxy::kEraTribe; }

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
    if (v.c && (v.key[0] != key[0] || v.key[2] != key[2])) // another creature now (an editor, a new stage)
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
static void stream_tick(double now)
{
    const std::string planet = galaxy::current_planet();
    const uint8_t era = net::galaxy::era_of(planet);
    // this player's creature: the avatar (Creature), the tribe's chieftain (Tribe: the game has no avatar there)
    void* av = game::avatar() ? game::avatar() : era == net::galaxy::kEraTribe ? tribe::player_chief() : nullptr;
    if (!g_stream || planet.empty() || !galaxy::welcomed()) {
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
        if (av && shows_visitors(era))
            visitor_show(v, av);
        else if (v.c)
            visitor_remove(v);
        ++it;
    }
}

// BUG-9289: a name tag over each visitor near this player (its name, its era), in grey: a visitor, not a partner
void overlay(int sw, int sh)
{
    if (!g_stream || !g_tags || g_visitors.empty() || sw <= 0 || sh <= 0)
        return;
    const std::string planet = galaxy::current_planet();
    void* me = game::avatar() ? game::avatar()
                              : net::galaxy::era_of(planet) == net::galaxy::kEraTribe ? tribe::player_chief() : nullptr;
    if (!me)
        return;
    const net::Vec3 mine = game::position(me);
    for (auto& [s, v] : g_visitors) {
        if (!visitor_alive(v))
            continue;
        const net::Vec3 p = game::position(v.c);
        const float dist = net::dist(p, mine);
        if (!std::isfinite(dist) || dist > 120.f)
            continue;
        const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        const float up = len > 1 ? modes::creature_tag_height(v.c) / len : 0; // over its head (as a partner's tag)
        float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2] = {NAN, NAN};
        H->world_to_screen(w, sc);
        if (!std::isfinite(sc[0]) || !std::isfinite(sc[1]) || sc[0] < 40 || sc[0] > sw - 40 || sc[1] < 40 || sc[1] > sh - 40)
            continue;
        const std::string name = v.name.substr(0, utf8::cut_bytes(v.name, 32));
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
};
static std::map<std::pair<uint16_t, uint32_t>, ObjProp> g_objs; // (sender slot, its object's id)
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
            o.u = prop_make(lk);
            if (!o.u)
                continue;
            o.serial = game::object_serial(o.u);
            o.waiting = false;
            g_props_made++;
        }
        const float t = (float)std::min(age, 1.0);
        const net::Vec3 at{o.pos.x + o.vel.x * t, o.pos.y + o.vel.y * t, o.pos.z + o.vel.z * t};
        const net::Vec3 up = norm(at);
        const float g = game::ground_radius(net::Vec3{up.x * 600, up.y * 600, up.z * 600});
        place(o.u, {up.x * g, up.y * g, up.z * g}, o.rot);
    }
}

std::string stream_status()
{
    char b[300];
    snprintf(b, sizeof b,
             "era visitors (BUG-9287): on=%d planet='%s' sent=%llu got=%llu visitors=%u shown=%llu no_species=%llu gone=%llu\n",
             (int)g_stream, galaxy::current_planet().c_str(), (unsigned long long)g_visits_sent,
             (unsigned long long)g_visits_got, (unsigned)g_visitors.size(), (unsigned long long)g_shown,
             (unsigned long long)g_no_species, (unsigned long long)g_gone);
    std::string out = b;
    {
        size_t live = 0, waiting = 0;
        for (auto& [k, o] : g_objs)
            live += o.u != nullptr, waiting += o.waiting;
        snprintf(b, sizeof b, "  objects (BUG-9290): sent %llu in %llu messages; got %llu, now %u (props %u, waiting %u), made %llu, gone %llu, bakes asked %llu\n",
                 (unsigned long long)g_objs_sent, (unsigned long long)g_obj_msgs, (unsigned long long)g_objs_got,
                 (unsigned)g_objs.size(), (unsigned)live, (unsigned)waiting, (unsigned long long)g_props_made,
                 (unsigned long long)g_props_gone, (unsigned long long)g_bakes_asked);
        out += b;
        int listed = 0;
        for (auto& [k, o] : g_objs) {
            if (listed++ >= 12)
                break;
            net::Vec3 at{};
            const bool alive = o.u && game::object_alive(o.u) && game::object_position(o.u, at);
            snprintf(b, sizeof b, "    %s %08X/%08X sent (%.0f %.0f %.0f) prop %s at (%.0f %.0f %.0f)\n",
                     o.kind == net::galaxy::kVisitVehicle ? "vehicle" : "building", o.key[0], o.key[1], o.pos.x, o.pos.y,
                     o.pos.z, alive ? "alive" : "none", at.x, at.y, at.z);
            out += b;
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
