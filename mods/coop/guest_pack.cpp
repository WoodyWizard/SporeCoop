#include "guest_pack.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "drama.h"
#include "modes.h"
#include "net/interp.h"
#include "sporekit_game.hpp"

namespace guest_pack {

static const SkHost* H;
static SkMod* M;
static Ctx C;

static bool g_fix = true;
static bool g_herd_fix = true; // tests: `coop.gpack herd 0` keeps the member in its own (nest) herd
COOP_FIX("creature.guest_pack", "BUG-9129", g_fix,
         "creature: a guest's Summon Flock and recruits join a pack the host runs for that guest (follows and fights for it)");

static bool g_out_of_host_pack = true;
COOP_FIX("creature.guest_flock_out_of_pack", "BUG-9233", g_out_of_host_pack,
         "creature: a guest's flock never fills the host's pack (the host can recruit while it is out)");
static uint32_t g_reposse;
static bool g_flock_leader_fix = true;
COOP_FIX("creature.guest_flock_leader", "BUG-9229", g_flock_leader_fix,
         "creature: a guest's Summon Flock event runs with the guest's creature as its leader (it follows the guest)");
// BUG-9247 (TITAN QA 2026-11-16): the game reuses a creature object for another creature without a new serial (the
// pool, BUG-0044, docs/re/creature.md#creature-pool-ids): a summoned member's object came back as a hostile species and
// the pack kept it, teleported it beside the partner and it attacked the partner. On: a member whose object is another
// species now, or sits in the pool (no herd, at the planet's centre), leaves the pack. Off: 63-dev.
static bool g_reuse_fix = true;
COOP_FIX("creature.pack_member_reuse", "BUG-9247", g_reuse_fix,
         "creature: a partner's pack member whose object the game pooled or reused for another creature leaves the pack");
static uint32_t g_reused, g_pooled;
struct Member {
    uint8_t owner = 0;
    uint32_t serial = 0;
    uint32_t species = 0;
    void* profile = nullptr;
    bool summoned = false;
    void* target = nullptr;     // what it was told to attack
    double target_until = 0;
    double next_move = 0;
};
static std::unordered_map<void*, Member> g_members;
static uint32_t g_summons, g_summoned, g_recruits, g_assists, g_defends, g_leashed, g_teleports, g_dropped;
static uintptr_t a_DramaGet, a_StartKind, a_SetHerd;
static uint32_t g_herd_moves;

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }

static bool alive(void* c, const Member& m)
{
    if (!c || !game::object_alive(c) || game::object_serial(c) != m.serial || game::is_dead(c) || game::health(c) <= 0)
        return false;
    if (!g_reuse_fix)
        return true;
    // (the species profile, not the key: a summoned creature's key reads 0 a few seconds after the summon, runtime
    // 2026-11-16, while its profile stays)
    if (game::species_profile(c) != m.profile) {
        g_reused++;
        H->log(M, SK_INFO, "guest_pack: member %p of player %u is another creature now (species %08X -> %08X, profile %p -> %p): out of the pack (BUG-9247)",
               c, m.owner, m.species, game::species_instance(c), m.profile, game::species_profile(c));
        return false;
    }
    net::Vec3 p = game::position(c);
    if (!game::herd_of(c) && p.x * p.x + p.y * p.y + p.z * p.z < 1.f) {
        g_pooled++;
        H->log(M, SK_INFO, "guest_pack: member %p of player %u went to the game's pool: out of the pack (BUG-9247)", c, m.owner);
        return false;
    }
    return true;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_DramaGet = H->sym("SporeCoop::DramaManager_Get");
    a_StartKind = H->sym("SporeCoop::DramaManager_StartKind");
    a_SetHerd = H->sym("SporeCoop::Creature_SetHerd");
}

bool enabled() { return g_fix; }

static void place_near(void* c, void* puppet, int slot)
{
    net::Vec3 p = game::position(puppet);
    float ang = slot * 2.0943951f + 0.6f, r = 3.5f;
    // a point beside the puppet on its tangent plane: offset along two axes perpendicular to "up"
    float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len < 1)
        return;
    net::Vec3 up{p.x / len, p.y / len, p.z / len};
    net::Vec3 a = std::fabs(up.x) < 0.9f ? net::Vec3{1, 0, 0} : net::Vec3{0, 1, 0};
    // e1 = normalize(a - up*(a.up)), e2 = up x e1
    float d = a.x * up.x + a.y * up.y + a.z * up.z;
    net::Vec3 e1{a.x - up.x * d, a.y - up.y * d, a.z - up.z * d};
    float l1 = std::sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
    e1 = {e1.x / l1, e1.y / l1, e1.z / l1};
    net::Vec3 e2{up.y * e1.z - up.z * e1.y, up.z * e1.x - up.x * e1.z, up.x * e1.y - up.y * e1.x};
    net::Vec3 at{p.x + (e1.x * std::cos(ang) + e2.x * std::sin(ang)) * r, p.y + (e1.y * std::cos(ang) + e2.y * std::sin(ang)) * r,
                 p.z + (e1.z * std::cos(ang) + e2.z * std::sin(ang)) * r};
    float gr = game::ground_radius(at);
    if (gr > 1) { // on the ground
        float al = std::sqrt(at.x * at.x + at.y * at.y + at.z * at.z);
        at = {at.x / al * (gr + 0.3f), at.y / al * (gr + 0.3f), at.z / al * (gr + 0.3f)};
    }
    game::teleport(c, at, game::orientation(c));
}

// its own herd pulled it back to its nest (it followed 8 m, then walked off to 50 m): it moves into the herd
// the mod keeps for the guest's puppet (modes.cpp creature_spawn: quiet, its home follows the puppet)
static void into_puppet_herd(void* c, uint8_t guest)
{
    void* puppet = C.puppet_of ? C.puppet_of(guest) : nullptr;
    void* herd = puppet ? game::herd_of(puppet) : nullptr;
    if (!a_SetHerd || !herd || herd == game::herd_of(c) || herd == game::avatar_herd())
        return;
    skg::thiscall<void>(a_SetHerd, c, herd);
    g_herd_moves++;
}

static void join(uint8_t guest, void* c, bool summoned)
{
    auto& m = g_members[c];
    m.owner = guest;
    m.serial = game::object_serial(c);
    m.species = game::species_instance(c);
    m.profile = game::species_profile(c);
    m.summoned = summoned;
    m.target = nullptr;
    m.next_move = 0;
    if (g_herd_fix)
        into_puppet_herd(c, guest);
}

// the host's pack list (cGameNounManager +0x5C begin / +0x60 end, docs/re/creature.md 11)
static std::vector<void*> host_posse()
{
    std::vector<void*> v;
    void* nm = game::noun_manager();
    if (!nm)
        return v;
    void** b = skg::field<void**>(nm, 0x5C);
    void** e = skg::field<void**>(nm, 0x60);
    if (!b || e < b || e - b > 64 || IsBadReadPtr(b, (e - b) * sizeof(void*)))
        return v;
    v.assign(b, e);
    return v;
}

// out of the host's pack: the bit and the list entry (the summon event put it there for the host's avatar)
static void leave_host_posse(void* c)
{
    skg::field<uint32_t>(c, game::kFlagsOff) &= ~0x100u;
    void* nm = game::noun_manager();
    if (!nm)
        return;
    void**& e = skg::field<void**>(nm, 0x60);
    void** b = skg::field<void**>(nm, 0x5C);
    if (!b || e < b)
        return;
    for (void** it = b; it < e; ++it)
        if (*it == c) {
            std::move(it + 1, e, it);
            --e;
            *e = nullptr;
            return;
        }
}

int summon(uint8_t guest)
{
    if (!g_fix || !is_host() || !game::in_creature_stage() || !a_DramaGet || !a_StartKind)
        return 0;
    void* puppet = C.puppet_of ? C.puppet_of(guest) : nullptr;
    void* mgr = skg::cdecl_call<void*>(a_DramaGet);
    if (!puppet || !mgr)
        return 0;
    g_summons++;
    auto before = host_posse();
    // BUG-9229 (owner play test 2026-11-15: "Summon Flock isn't following BigTexas when they call it"; "I'm unable to add
    // creatures to my Pack"): the event ran with the host's avatar as its leader: its update steers the members for its
    // 2-3 minutes and at its end tells them to follow the host. On: the event runs with the guest's puppet as its avatar
    // (drama::summon_for, the start and every update, BUG-0736). Off: the host's avatar (58-dev).
    // BUG-9233: the summon counts its flock in the posse simulator (+0x1C, the summoned members: 0 -> 4 at a guest's
    // flock, runtime 2026-11-15) and the pack takes no recruit while members + that count are at its size; the count drops
    // only when the flock leaves the game's own way, so a flock handed to a guest left the host's pack "full" for good.
    static uintptr_t posse_get = H->sym("SporeCoop::cPosseSimulator_Get");
    uint8_t* posse = posse_get ? skg::cdecl_call<uint8_t*>(posse_get) : nullptr;
    const bool have_count = posse && skg::readable(posse, 0x20);
    const int32_t count_before = have_count ? skg::field<int32_t>(posse, 0x1C) : -1;
    const bool for_puppet = g_flock_leader_fix && drama::summon_for(puppet);
    if (!g_flock_leader_fix)
        skg::thiscall<void>(a_StartKind, mgr, 8, 0); // the game's own summon (cSummonCreatureEvent): a flock in the host's pack
    int n = 0, slot = 0;
    for (void* c : host_posse()) {
        if (std::find(before.begin(), before.end(), c) != before.end() || !c || game::is_puppet(c))
            continue;
        leave_host_posse(c);
        join(guest, c, true);
        place_near(c, puppet, slot++);
        n++;
    }
    g_summoned += n;
    // BUG-9233: the game takes a summoned flock out of the host's pack only when its event ends (59-dev: the host could
    // not recruit for its 2-3 minutes): it ends now, the guest's pack keeps the members at the guest's side
    if (g_out_of_host_pack && for_puppet && n)
        drama::end_flock_event();
    if (have_count) {
        const int32_t count_after = skg::field<int32_t>(posse, 0x1C);
        const bool put_back = g_out_of_host_pack && n && count_after != count_before;
        if (put_back)
            skg::field<int32_t>(posse, 0x1C) = count_before;
        H->log(M, SK_INFO, "guest_pack: the host's pack: summoned count %d -> %d%s (BUG-9233)", count_before, count_after,
               put_back ? ", put back" : "");
    }
    H->log(M, n ? SK_INFO : SK_WARN, "guest_pack: %s summoned %d for player %u", n ? "the flock" : "nothing", n, guest);
    return n;
}

bool recruit(uint8_t guest, void* npc)
{
    if (!g_fix || !is_host() || !npc || game::is_puppet(npc) || npc == game::avatar() || game::is_dead(npc) ||
        game::pack_member(npc) || g_members.count(npc))
        return false;
    join(guest, npc, false);
    g_recruits++;
    H->log(M, SK_INFO, "guest_pack: player %u recruited %p", guest, npc);
    return true;
}

bool member(void* c) { return g_fix && c && g_members.count(c); }

int owner(void* c)
{
    auto it = c ? g_members.find(c) : g_members.end();
    return it == g_members.end() ? -1 : it->second.owner;
}

static void attack(void* c, Member& m, void* victim, double now)
{
    if (!victim || victim == c || game::is_dead(victim))
        return;
    skg::vcall<void>(c, 0x84 / 4, (uint8_t*)victim + game::kCombatOff, 1, 0); // SetTarget (as the charge test)
    m.target = victim;
    m.target_until = now + 15;
}

void assist(uint8_t guest, void* victim)
{
    if (!g_fix || !victim || member(victim) || game::is_puppet(victim))
        return;
    double now = H->time_seconds();
    for (auto& [c, m] : g_members)
        if (m.owner == guest && alive(c, m) && m.target != victim) {
            attack(c, m, victim, now);
            g_assists++;
        }
}

void defend(uint8_t guest, void* attacker)
{
    if (!g_fix || !attacker || member(attacker) || game::is_puppet(attacker) || attacker == game::avatar())
        return;
    double now = H->time_seconds();
    for (auto& [c, m] : g_members)
        if (m.owner == guest && alive(c, m) && m.target != attacker) {
            attack(c, m, attacker, now);
            g_defends++;
        }
}

void tick(double now)
{
    if (g_members.empty())
        return;
    if (!g_fix || !is_host() || !game::in_creature_stage()) {
        g_members.clear();
        return;
    }
    int slot = 0;
    for (auto it = g_members.begin(); it != g_members.end();) {
        void* c = it->first;
        Member& m = it->second;
        if (!alive(c, m)) {
            it = g_members.erase(it);
            g_dropped++;
            continue;
        }
        void* puppet = C.puppet_of ? C.puppet_of(m.owner) : nullptr;
        if (!puppet || now < m.next_move) {
            ++it;
            slot++;
            continue;
        }
        m.next_move = now + 0.5;
        // BUG-9233 (owner play test 2026-11-15: "I'm unable to add creatures to my Pack"): the summon event put a guest's
        // flock back into the host's pack while it ran (pack bit 0x100, the noun manager's list), so the host's pack was
        // full: its own recruits failed (MSI: 3 recruits join with no flock, none while a guest's flock is out). On: a
        // member back in the host's pack leaves it again. Off: taken out once, at the summon (59-dev).
        if (g_out_of_host_pack && (skg::field<uint32_t>(c, game::kFlagsOff) & 0x100u)) {
            leave_host_posse(c);
            g_reposse++;
        }
        if (m.target && (now > m.target_until || game::is_dead(m.target) || !game::object_alive(m.target)))
            m.target = nullptr;
        float d = net::dist(game::position(c), game::position(puppet));
        if (d > 60) { // lost (another place, a teleport): back beside it
            place_near(c, puppet, slot);
            g_teleports++;
        } else if (m.target && d < 30) {
            // fighting near the guest: leave it to the game's attack
        } else if (d > 6) {
            game::move_to(c, game::position(puppet), 3.f);
            g_leashed++;
        }
        ++it;
        slot++;
    }
}

void on_player_leave(uint8_t id)
{
    for (auto it = g_members.begin(); it != g_members.end();)
        it = it->second.owner == id ? g_members.erase(it) : std::next(it);
}

void reset() { g_members.clear(); }
void set_herd_fix(bool on) { g_herd_fix = on; }

std::string status()
{
    char b[320];
    std::snprintf(b, sizeof b,
                  "guest_pack: fix=%d members=%zu summons=%u summoned=%u recruits=%u assists=%u defends=%u leashed=%u "
                  "teleports=%u dropped=%u herd_moves=%u herd_fix=%d reuse_fix=%d reused=%u pooled=%u\n",
                  (int)g_fix, g_members.size(), g_summons, g_summoned, g_recruits, g_assists, g_defends, g_leashed,
                  g_teleports, g_dropped, g_herd_moves, (int)g_herd_fix, (int)g_reuse_fix, g_reused, g_pooled);
    std::string out = b;
    // tests (BUG-9233): the posse simulator's 0x58 bytes and the host's pack list length
    static uintptr_t posse_get = H->sym("SporeCoop::cPosseSimulator_Get");
    if (uint8_t* ps = posse_get ? skg::cdecl_call<uint8_t*>(posse_get) : nullptr; ps && skg::readable(ps, 0x58)) {
        out += "  posse:";
        for (int i = 0; i < 0x58; i += 4) {
            std::snprintf(b, sizeof b, " %08X", *(uint32_t*)(ps + i));
            out += b;
        }
        std::snprintf(b, sizeof b, " | host pack list %zu\n", host_posse().size());
        out += b;
    }
    for (auto& [c, m] : g_members) {
        void* p = C.puppet_of ? C.puppet_of(m.owner) : nullptr;
        std::snprintf(b, sizeof b, "  %p owner=%u %s species=%08X/%08X b135=%u b67=%u hp=%.1f dist=%.1f target=%p herd=%p puppet=%p puppet_herd=%p avatar_herd=%p\n", c,
                      m.owner, m.summoned ? "summoned" : "recruit", m.species, game::species_instance(c),
                      skg::field<uint8_t>(c, 0x135), skg::field<uint8_t>(c, 0xB67), game::health(c),
                      p ? net::dist(game::position(c), game::position(p)) : -1.f, m.target, game::herd_of(c), p,
                      p ? game::herd_of(p) : nullptr, game::avatar_herd());
        out += b;
    }
    return out;
}

} // namespace guest_pack
