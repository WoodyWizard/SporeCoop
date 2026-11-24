#include "guest_pack.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "drama.h"
#include "driver.h"
#include "modes.h"
#include "net/interp.h"
#include "net/protocol.h"
#include "npc.h"
#include "creations.h"
#include "home_nests.h"
#include "identity.h"
#include "player_rules.h"
#include "savestrip.h"
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
static bool g_attack_cmd = true;
COOP_FIX("creature.guest_pack_attack", "BUG-9248", g_attack_cmd,
         "creature: a partner's pack gets the game's own attack command for what the partner fights (it goes and fights)");
static uintptr_t a_CmdStim, a_CanDefend;
static uint32_t g_commands, g_declined;
struct Member {
    uint8_t owner = 0;
    uint32_t serial = 0;
    uint32_t species = 0;
    void* profile = nullptr;
    bool summoned = false;
    void* target = nullptr;     // what it was told to attack
    double target_until = 0;
    double next_move = 0;
    double next_follow = 0; // decision 5CD
    net::NpcState drv{};    // decision 5EA: its latest state from its partner's game (which runs it)
    double drv_at = 0;      // when that came (0: never)
    drive::State dst;       // the host's walking of it on that path (the proxies' driving, npc.cpp)
    bool drv_walk = false;
    net::Vec3 drv_seen{};   // where it was drawn the frame before (a jump of more than 3 m is counted)
};
static std::unordered_map<void*, Member> g_members;
static uint32_t g_summons, g_summoned, g_recruits, g_assists, g_defends, g_leashed, g_teleports, g_dropped;
static uintptr_t a_DramaGet, a_StartKind, a_SetHerd;
static uint32_t g_herd_moves;

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static uintptr_t a_PossePtr, a_PosseCtor, a_PosseInit, a_PosseRemove, a_AddMember, a_PosseGet, a_PlayerCmd, a_PostStim; // decision 5CD
static uint32_t g_follow_posts;
static bool g_follow_test; // tests: `coop.gpack follow 1`
static size_t ai_enter(void* c);     // decision 5CD (below)
static void before_destroy(void* c);
static void save_start();
static void save_end();
static void send_lists(double now, bool force);
static void party_leave(size_t tok);
static bool driven(void* c);                     // decision 5EA (below)
static void drive(void* c, Member& m, double now);
static void own_tick_guest(double now);

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
    a_CmdStim = H->sym("SporeCoop::CreatureCommandStimulus"); // BUG-9248
    a_CanDefend = H->sym("SporeCoop::PosseMemberCanDefend");
    // decision 5CD
    a_PossePtr = H->sym("SporeCoop::cPosseSimulator_ptr");
    a_PosseCtor = H->sym("SporeCoop::cPosseSimulator_ctor");
    a_PosseInit = H->sym("SporeCoop::cPosseSimulator_Init");
    a_PosseRemove = H->sym("SporeCoop::cPosseSimulator_RemoveMember");
    a_AddMember = H->sym("SporeCoop::cPosseSimulator_AddMember");
    a_PosseGet = H->sym("SporeCoop::cPosseSimulator_Get");
    a_PlayerCmd = H->sym("SporeCoop::Creature_PlayerCommand");
    a_PostStim = H->sym("SporeCoop::BT_PostStimulus");
    if (!a_PossePtr || !a_PosseCtor || !a_PosseInit || !a_PosseRemove || !a_AddMember || !a_PosseGet)
        H->log(M, SK_WARN, "guest_pack: a pack symbol is missing: partners' packs stay the mod's (decision 5CD off)");
    game::set_party_context(ai_enter, party_leave);
    game::set_ai_skip_pack(driven); // decision 5EA
    game::set_before_destroy(before_destroy);
    savestrip::add_save_hooks(save_start, save_end);
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
    if (g_herd_fix && !party_mode()) // (5CD: a member keeps its own herd, as in the host's pack)
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

// ---------------------------------------------------------------- decision 5CD: a pack per player
// Owner 2026-11-16: "Every player will have their own creature and it will feel like single player vanilla ... everyone
// is a host of their parties." The game has one pack: the pack manager ([cPosseSimulator_ptr]), its member list
// (cGameNounManager +0x5C / +0x60 / +0x64) and its leader (the avatar, +0x54) (docs/re/creature.md#creature-pack-map).
// Only a creature in that list obeys the pack's attack order (BUG-9248). Each partner gets a pack manager of its own (the
// game's ctor and init) and a member list of its own; while the game's pack code runs for a partner (its members' AI
// ticks, its recruit, its Summon Flock's start / update / end, the attack order) those three are the partner's, so the
// game runs a single-player pack for it: its size limit, its summons, following and fighting. The host's own pack never
// holds a partner's member. `coop.fix creature.party_packs off`: 63-dev's guest pack (members kept out of every pack,
// leashed by the mod, the flock event ended at the hand-over).
static bool g_party = true;
COOP_FIX("creature.party_packs", "BUG-9248", g_party,
         "creature: each partner has a pack of its own that the game runs as in single player (follows, fights, takes its summons and recruits)");
// phase 4: the partner's family (its own nest herd) is the game's avatar herd inside its party, and its members think
// inside that party (their parent is the partner, as the host's family's is the host). `coop.fix creature.party_family off`.
static bool g_family_fix = true;
COOP_FIX("creature.party_family", "BUG-9248", g_family_fix,
         "creature: a partner's own family (its nest) treats the partner as its parent, as the host's does the host");
static std::unordered_map<void*, uint8_t> g_family_of; // host: a partner's nest herd -> that partner (1 s old at most)
static double g_next_family;
static uint32_t g_family_enters;
struct Party {
    alignas(16) uint8_t posse[0x60] = {}; // the partner's cPosseSimulator (0x58 bytes, the game's ctor)
    bool made = false;
    void* list[3] = {nullptr, nullptr, nullptr}; // its member list (begin, end, capacity; the game's allocations)
};
static std::unordered_map<uint8_t, std::unique_ptr<Party>> g_parties;
struct Frame {
    uint8_t player;
    size_t mark;
    void* posse;
    void* list[3];
    void* avatar;
    void* puppet;
    void* lent;
    void* herd = nullptr; // the avatar herd before (phase 4)
    uint8_t* strategy = nullptr; // the creature mode strategy whose pack panel (+0x68) is hidden meanwhile
    void* panel = nullptr;
    bool family = false;
};
static std::vector<Frame> g_frames;
static uint32_t g_enters, g_parties_made, g_frames_lost, g_party_removed, g_party_freed;

static Party* party_of(uint8_t p, bool make)
{
    auto it = g_parties.find(p);
    if (it != g_parties.end())
        return it->second.get();
    if (!make)
        return nullptr;
    auto& slot = g_parties[p];
    slot = std::make_unique<Party>();
    return slot.get();
}

static void* host_posse_obj() { return a_PosseGet ? skg::cdecl_call<void*>(a_PosseGet) : nullptr; }

static void* party_posse(Party& pt)
{
    void* host = g_frames.empty() ? host_posse_obj() : g_frames.front().posse;
    if (!host || !a_PossePtr || !a_PosseCtor || !a_PosseInit)
        return nullptr;
    if (!pt.made) {
        void** gp = (void**)a_PossePtr;
        void* was = *gp;
        skg::thiscall<void*>(a_PosseCtor, (void*)pt.posse);
        *gp = was; // (the ctor makes itself the world's pack manager)
        skg::thiscall<void>(a_PosseInit, (void*)pt.posse);
        pt.made = true;
        g_parties_made++;
        H->log(M, SK_INFO, "guest_pack: a pack manager of its own made at %p (decision 5CD)", (void*)pt.posse);
    }
    skg::field<int32_t>(pt.posse, 0x18) = skg::field<int32_t>(host, 0x18); // its size limit (the host's brain level for now)
    return pt.posse;
}

static void** list_slot(void* nm, int i) { return &skg::field<void*>(nm, 0x5C + 4 * i); }

// the game's pack is the player's own until party_leave(token); 0: not entered (no puppet, the switch off, inside it)
static size_t party_enter(uint8_t p)
{
    void* nm = game::noun_manager();
    void* pup = C.puppet_of ? C.puppet_of(p) : nullptr;
    if (!g_party || !g_fix || !nm || !pup || !a_PossePtr || (!g_frames.empty() && g_frames.back().player == p))
        return 0;
    Party* pt = party_of(p, true);
    void* posse = party_posse(*pt);
    if (!posse)
        return 0;
    Frame f{p, game::swap_depth(), *(void**)a_PossePtr, {*list_slot(nm, 0), *list_slot(nm, 1), *list_slot(nm, 2)},
            skg::field<void*>(nm, game::kNounAvatarOff), pup, nullptr};
    game::swap_open(game::swap_undo_u32, (void*)a_PossePtr, (uintptr_t)f.posse, "party pack manager");
    for (int i = 0; i < 3; i++)
        game::swap_open(game::swap_undo_u32, list_slot(nm, i), (uintptr_t)f.list[i], "party pack list");
    game::swap_open(game::swap_undo_u32, &skg::field<void*>(nm, game::kNounAvatarOff), (uintptr_t)f.avatar, "party leader");
    // a puppet of the partner's own creature has no archetype tuning (+0xE84): it borrows the avatar's, as in the AI swap
    void*& tun = skg::field<void*>(pup, 0xE84);
    if (!tun && f.avatar && f.avatar != pup) {
        f.lent = skg::field<void*>(f.avatar, 0xE84);
        game::swap_open(game::swap_undo_u32, &tun, 0, "party tuning");
        tun = f.lent;
    }
    // (the avatar mark +0xB58 & 0x200 is not lent: tried 2026-11-17 for following, no effect, see party_tick)
    // the host's own Pack panel is not the partner's: AddMember / RemoveMember add or remove a portrait there (the creature
    // mode strategy +0x68, skipped when null; runtime 2026-11-17: the host's panel showed 6, its 3 and the partners' 3)
    static uintptr_t strat_get = H->sym("App::cCreatureModeStrategy::Get");
    if (uint8_t* st = strat_get ? skg::cdecl_call<uint8_t*>(strat_get) : nullptr; st && skg::readable(st, 0x6C)) {
        f.strategy = st;
        f.panel = skg::field<void*>(st, 0x68);
        game::swap_open(game::swap_undo_u32, st + 0x68, (uintptr_t)f.panel, "party: the host's pack panel");
        skg::field<void*>(st, 0x68) = nullptr;
    }
    // phase 4: the leader's family is the partner's own nest herd (the game's avatar herd, cGameNounManager +0x58)
    f.herd = skg::field<void*>(nm, game::kNounAvatarHerdOff);
    if (void* fam = g_family_fix ? home_nests::family_herd(p) : nullptr) {
        game::swap_open(game::swap_undo_u32, &skg::field<void*>(nm, game::kNounAvatarHerdOff), (uintptr_t)f.herd, "party family");
        skg::field<void*>(nm, game::kNounAvatarHerdOff) = fam;
        f.family = true;
    }
    *(void**)a_PossePtr = posse;
    for (int i = 0; i < 3; i++)
        *list_slot(nm, i) = pt->list[i];
    skg::field<void*>(nm, game::kNounAvatarOff) = pup;
    g_frames.push_back(f);
    g_enters++;
    return g_frames.size();
}

static void party_leave(size_t tok)
{
    if (!tok || tok != g_frames.size())
        return; // (not entered, or a fault left frames: the frame tick drops them)
    Frame f = g_frames.back();
    g_frames.pop_back();
    void* nm = game::noun_manager();
    if (Party* pt = party_of(f.player, false); pt && nm)
        for (int i = 0; i < 3; i++)
            pt->list[i] = *list_slot(nm, i); // what the game made of it (it may have grown the vector)
    *(void**)a_PossePtr = f.posse;
    if (nm) {
        for (int i = 0; i < 3; i++)
            *list_slot(nm, i) = f.list[i];
        skg::field<void*>(nm, game::kNounAvatarOff) = f.avatar;
        if (f.family)
            skg::field<void*>(nm, game::kNounAvatarHerdOff) = f.herd;
    }
    if (f.strategy)
        skg::field<void*>(f.strategy, 0x68) = f.panel;
    if (f.lent && skg::field<void*>(f.puppet, 0xE84) == f.lent)
        skg::field<void*>(f.puppet, 0xE84) = nullptr;
    game::swap_close(f.mark, 5 + (f.lent ? 1 : 0) + (f.family ? 1 : 0) + (f.strategy ? 1 : 0));
}

static std::vector<void*> party_list(uint8_t p)
{
    std::vector<void*> v;
    Party* pt = party_of(p, false);
    if (!pt)
        return v;
    // (inside the party's context the live list is the noun manager's)
    void* nm = game::noun_manager();
    void** b = (void**)pt->list[0];
    void** e = (void**)pt->list[1];
    if (!g_frames.empty() && g_frames.back().player == p && nm) {
        b = (void**)*list_slot(nm, 0);
        e = (void**)*list_slot(nm, 1);
    }
    if (b && e > b && e - b <= 64 && skg::readable(b, (e - b) * sizeof(void*)))
        v.assign(b, e);
    return v;
}

// the party's list loses an entry whose object is gone (freed or another noun now): not through the game (it would read it)
static void party_drop_freed(uint8_t p)
{
    Party* pt = party_of(p, false);
    void** b = pt ? (void**)pt->list[0] : nullptr;
    void** e = pt ? (void**)pt->list[1] : nullptr;
    if (!b || e <= b || e - b > 64 || !skg::readable(b, (e - b) * sizeof(void*)))
        return;
    void** w = b;
    for (void** it = b; it < e; ++it)
        if (*it && game::listed(*it))
            *w++ = *it;
        else
            g_party_freed++;
    pt->list[1] = w;
}

size_t party_enter_puppet(void* puppet)
{
    if (!g_party || !puppet || !C.sess)
        return 0;
    for (uint8_t id = 1; id < (uint8_t)net::kMaxPlayers; id++)
        if (C.puppet_of && C.puppet_of(id) == puppet)
            return party_enter(id);
    return 0;
}
void party_leave_token(size_t tok) { party_leave(tok); }
bool party_mode() { return g_party && g_fix; }


static uint32_t g_cmds_got, g_cmds_given;
// ---------------------------------------------------------------- decision 5CD phase 2: the partner's own Pack panel
// The host sends each partner its pack's NPC ids (kPackListKind, on a change and every 2 s); the partner's game puts
// those proxies into its own pack list with the game's recruit (AddMember: the panel, a name, the first-pack hint), so the
// partner sees its pack as in single player. The proxies keep no AI of their own there (the host's game moves them).
// A summoned one is marked summoned (+0xE68) first: the game does not count it against the limit of 3, as for the
// host's own flock. Before a save the guest's pack list is empty (its members are proxies, never saved), after it back.
static std::vector<void*> g_mirror; // guest: proxies this game holds in its own pack list
static std::unordered_map<uint8_t, std::vector<std::pair<uint16_t, bool>>> g_sent; // host: last list per partner
static double g_next_send;
static uint32_t g_lists_sent, g_lists_got, g_mirror_added, g_mirror_removed, g_mirror_refused;

static void send_lists(double now, bool force)
{
    if (!C.sess || (!force && now < g_next_send))
        return;
    g_next_send = now + 2;
    for (auto& [p, pt] : g_parties) {
        std::vector<std::pair<uint16_t, bool>> v;
        for (void* c : party_list(p)) {
            uint16_t id = npc::id_of(c);
            void* h = game::herd_of(c);
            if (id != 0xFFFF)
                v.push_back({id, h && skg::readable(h, 0x194) && skg::field<uint8_t>(h, 0x192)});
        }
        net::Writer w;
        w.u8(kPackListKind);
        w.u8((uint8_t)std::min<size_t>(v.size(), 32));
        for (size_t i = 0; i < v.size() && i < 32; i++) {
            w.u16(v[i].first);
            w.u8(v[i].second ? 1 : 0);
        }
        C.sess->send_reliable_to(p, net::MSG_PLAYER_RULE, w);
        g_sent[p] = v;
        g_lists_sent++;
    }
}

void for_each_member(uint8_t me, void (*fn)(void* c, uint8_t owner, void* user), void* user)
{
    if (!C.sess || !C.sess->connected())
        return;
    std::vector<void*> seen;
    auto give = [&](void* c, uint8_t owner) {
        if (!c || std::find(seen.begin(), seen.end(), c) != seen.end())
            return;
        seen.push_back(c);
        fn(c, owner, user);
    };
    if (!is_host()) {
        for (void* c : std::vector<void*>(g_mirror))
            give(c, me);
        return;
    }
    for (auto& [c, m] : g_members)
        give(c, m.owner);
    for (auto& [p, pt] : g_parties)
        for (void* c : party_list(p))
            give(c, p);
    for (void* c : host_posse())
        give(c, me);
}

static void mirror_out(void* posse, void* c)
{
    if (a_PosseRemove && posse && game::listed(c))
        skg::thiscall<void>(a_PosseRemove, posse, c, 0u, 0u);
    if (game::listed(c))
        skg::field<uint32_t>(c, game::kFlagsOff) &= ~0x100u;
    g_mirror.erase(std::remove(g_mirror.begin(), g_mirror.end(), c), g_mirror.end());
    g_mirror_removed++;
}

void on_pack_list(const std::vector<std::pair<uint16_t, bool>>& ids)
{
    g_lists_got++;
    void* posse = party_mode() ? host_posse_obj() : nullptr;
    if (!posse)
        return;
    std::vector<std::pair<void*, bool>> want;
    for (auto& [id, s] : ids)
        if (void* p = npc::proxy(id))
            want.push_back({p, s});
    for (void* c : std::vector<void*>(g_mirror)) {
        bool keep = false;
        for (auto& w : want)
            keep |= w.first == c;
        if (!keep || !game::listed(c))
            mirror_out(posse, c);
    }
    for (auto& [c, s] : want) {
        if (std::find(g_mirror.begin(), g_mirror.end(), c) != g_mirror.end())
            continue;
        if (s)
            skg::field<uint8_t>(c, 0xE68) = 1;
        if (player_rules::local_pack_add(posse, c)) {
            g_mirror.push_back(c);
            g_mirror_added++;
        } else
            g_mirror_refused++;
    }
}

// a creature the mod removes leaves a pack list first (the guest's proxies; a host member of a partner's pack)
static void before_destroy(void* c)
{
    if (std::find(g_mirror.begin(), g_mirror.end(), c) != g_mirror.end())
        mirror_out(host_posse_obj(), c);
}

// ---------------------------------------------------------------- decision 5CD phase 3: each partner's pack in the save
// The host's save keeps the host's pack (the game's own list). A partner's recruits are noted per planet in worlds.ini
// (section "pack <identity key>": "species x y z;" per recruit, at the save's moment) and their pack bit is off while the
// save is written (a creature saved with the bit and in no list would come back as a pack member of nobody). When that
// partner is in this world again (the same planet, a load, a rejoin) the creatures standing at those spots with that
// species join its pack again (the game's recruit, inside its party). A summoned flock is not kept (it leaves at its
// summon's end in single player too).
static std::vector<void*> g_unmarked;                        // host: members whose bit is off while a save is written
static std::unordered_map<uint8_t, uint32_t> g_restored;     // host: party -> the world its pack was restored for
static uint32_t g_notes_written, g_pack_restored;
static double g_next_orphans;
static void clear_orphans(double now);

static std::string pack_section(uint8_t p) { return "pack " + identity::key(p); }

static bool summoned_member(void* c)
{
    void* h = game::herd_of(c);
    return (h && skg::readable(h, 0x194) && skg::field<uint8_t>(h, 0x192)) || skg::field<uint8_t>(c, 0xE68);
}

// one partner's pack into this planet's note (at a save: those here; at a leave: before its pack is let go)
static void note_pack(uint8_t p)
{
    const uint32_t w = C.world ? C.world() : 0;
    if (!w || !party_of(p, false))
        return;
    std::string v;
    char b[80];
    for (void* c : party_list(p))
        if (game::listed(c, 0x18EB45Eu) && !game::is_dead(c) && !summoned_member(c)) {
            net::Vec3 q = game::position(c);
            std::snprintf(b, sizeof b, "%08X %.1f %.1f %.1f;", game::species_instance(c), q.x, q.y, q.z);
            v += b;
        }
    creations::world_note_set(pack_section(p).c_str(), w, v);
    g_notes_written++;
}

static void note_packs()
{
    for (auto& [p, pt] : g_parties)
        if (C.puppet_of && C.puppet_of(p)) // (a partner who left: its note was written when it left)
            note_pack(p);
}

struct RestoreQ {
    uint32_t species;
    net::Vec3 at;
    void* found = nullptr;
    float best = 10.f; // the nearest of that species within 10 m (it wandered between the load and the partner's return)
};
static void restore_pack(uint8_t p)
{
    const uint32_t w = C.world ? C.world() : 0;
    if (!w || g_restored[p] == w)
        return;
    g_restored[p] = w;
    const std::string note = creations::world_note(pack_section(p).c_str(), w);
    if (!note.empty()) { // (a creature with a stale pack bit, in no pack, is free to come back)
        g_next_orphans = 0;
        clear_orphans(H->time_seconds());
    }
    int n = 0;
    for (size_t i = 0; i < note.size();) {
        size_t j = note.find(';', i);
        if (j == std::string::npos)
            break;
        RestoreQ q{};
        if (std::sscanf(note.substr(i, j - i).c_str(), "%x %f %f %f", &q.species, &q.at.x, &q.at.y, &q.at.z) == 4) {
            game::for_each_object(
                [](void* o, uint32_t noun, void* u) {
                    auto& q = *(RestoreQ*)u;
                    if (noun != 0x18EB45Eu || !game::object_alive(o) || game::is_dead(o) || game::is_puppet(o) ||
                        game::pack_member(o) || o == game::avatar() || game::species_instance(o) != q.species)
                        return true;
                    const float d = net::dist(game::position(o), q.at);
                    if (d < q.best) {
                        q.best = d;
                        q.found = o;
                    }
                    return true;
                },
                &q);
            if (q.found && recruit(p, q.found))
                n++;
        }
        i = j + 1;
    }
    if (n)
        g_pack_restored += n;
    if (!note.empty())
        H->log(M, SK_INFO, "guest_pack: player %u's pack from this planet's note: %d of its recruits back (decision 5CD)", p, n);
}

// guest saves: the own pack list holds only proxies (never saved): empty while the save is written
static void* g_saved_list[3];
static bool g_list_hidden;
static void save_start()
{
    void* nm = game::noun_manager();
    if (!nm || !C.sess)
        return;
    if (C.sess->role() == net::Role::Host && party_mode() && !g_parties.empty()) {
        note_packs();
        for (auto& [p, pt] : g_parties)
            for (void* c : party_list(p))
                if (game::listed(c) && (skg::field<uint32_t>(c, game::kFlagsOff) & 0x100u)) {
                    skg::field<uint32_t>(c, game::kFlagsOff) &= ~0x100u;
                    g_unmarked.push_back(c);
                }
        return;
    }
    if (g_mirror.empty() || C.sess->role() != net::Role::Client)
        return;
    for (int i = 0; i < 3; i++) {
        g_saved_list[i] = *list_slot(nm, i);
        *list_slot(nm, i) = nullptr;
    }
    g_list_hidden = true;
}
static void save_end()
{
    for (void* c : g_unmarked)
        if (game::listed(c))
            skg::field<uint32_t>(c, game::kFlagsOff) |= 0x100u;
    g_unmarked.clear();
    void* nm = game::noun_manager();
    if (!g_list_hidden || !nm)
        return;
    for (int i = 0; i < 3; i++)
        *list_slot(nm, i) = g_saved_list[i];
    g_list_hidden = false;
}

// tests (decision 5CD): who asks for the pack manager (return addresses of cPosseSimulator_Get), `coop.gpack callers`
using PosseGet_t = void*(__cdecl*)();
static PosseGet_t o_PosseGetHook;
static std::unordered_map<uintptr_t, uint32_t> g_posse_callers;
static bool g_count_callers;
static void* __cdecl h_PosseGet()
{
    if (g_count_callers && g_posse_callers.size() < 256)
        g_posse_callers[(uintptr_t)__builtin_return_address(0)]++;
    return o_PosseGetHook();
}
void pack_command(uint8_t guest, uint32_t lo, uint32_t hi, float value, uint32_t c, uint32_t d, void* target)
{
    g_cmds_got++;
    void* pup = C.puppet_of ? C.puppet_of(guest) : nullptr;
    if (!party_mode() || !is_host() || !pup || !a_PlayerCmd || party_list(guest).empty())
        return;
    const size_t tok = party_enter(guest);
    if (!tok)
        return;
    // the relay's attack case (0x10) sends the pack at the leader's own target (Steam 0xC42E40 reads it from the avatar,
    // not from the command): the puppet takes the target the partner's click gave its creature in its own game
    if ((lo & 0x10u) && target && !game::is_dead(target))
        skg::vcall<void>(pup, 0x84 / 4, (uint8_t*)target + game::kCombatOff, 1, 0);
    uint32_t vbits;
    std::memcpy(&vbits, &value, 4);
    // Creature_PlayerCommand(avatar = the puppet here, lo, hi, value, c, d, target, keep): the stimulus on the puppet,
    // then cPosseSimulator_Relay to its pack (the puppet has no AI of its own: the stimulus is only read by the relay)
    skg::thiscall<void*>(a_PlayerCmd, pup, lo, hi, vbits, c, d, target, 0u);
    party_leave(tok);
    g_cmds_given++;
    // the relay's attack case did not reach the members in the QA runs (2026-11-17: the order arrived, the target set on
    // the puppet, no hit): the pack's own attack command per member as well (runtime: recruits killed the target with it)
    if ((lo & 0x10u) && target && !game::is_dead(target))
        assist(guest, target);
}

static bool g_ctx_test = true; // not a lab switch: tests: `coop.gpack ctx 0`: members update outside their party
static size_t ai_enter(void* c)
{
    if (!g_party || !g_ctx_test || (g_members.empty() && g_family_of.empty()))
        return 0;
    auto it = g_members.find(c);
    if (it != g_members.end())
        return party_enter(it->second.owner);
    if (!g_family_of.empty())
        if (auto f = g_family_of.find(game::herd_of(c)); f != g_family_of.end()) {
            g_family_enters++;
            return party_enter(f->second);
        }
    return 0;
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
    if (party_mode()) {
        // 5CD: the game's own Summon Flock inside the partner's pack: the start adds the flock to it (and counts it),
        // its update steers it, its end takes it out again (drama.cpp runs both in the party), as for the host's own
        const size_t tok = party_enter(guest);
        if (!tok)
            return 0;
        auto before = party_list(guest);
        bool ok = drama::summon_for(puppet, guest);
        if (!ok) // BUG-9362: the world's summon is another party's: an event of this party's own
            ok = drama::summon_extra(puppet, guest);
        auto after = party_list(guest);
        party_leave(tok);
        int n = 0;
        for (void* c : after)
            if (c && std::find(before.begin(), before.end(), c) == before.end()) {
                join(guest, c, true);
                n++;
            }
        g_summoned += n;
        H->log(M, n ? SK_INFO : SK_WARN, "guest_pack: %s: %d in player %u's own pack (event %s, decision 5CD)",
               n ? "the flock summoned" : "nothing summoned", n, guest, ok ? "started" : "not started");
        return n;
    }
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
    const bool for_puppet = g_flock_leader_fix && drama::summon_for(puppet, guest);
    // BUG-9362: the world's summon is another party's: an event of this party's own (its flock lands in the host's pack
    // as the world event's does, and goes to the partner below)
    const bool extra = g_flock_leader_fix && !for_puppet && drama::summon_extra(puppet, guest);
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
    if (g_out_of_host_pack && extra && n)
        drama::end_extra_flock(guest); // (BUG-9362: as the world event's above)
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
    if (party_mode()) {
        // 5CD: the game's own recruit into the partner's pack (its limit of 3, its name, the member's own herd kept)
        const size_t tok = party_enter(guest);
        if (!tok || !a_AddMember)
            return false;
        const bool ok = skg::thiscall<bool>(a_AddMember, *(void**)a_PossePtr, npc);
        party_leave(tok);
        if (!ok) {
            H->log(M, SK_INFO, "guest_pack: player %u's pack refused %p (full, decision 5CD)", guest, npc);
            return false;
        }
    }
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
    // BUG-9248 (owner 2026-11-16: "Does it actually attack"): SetTarget alone was kept (+0xE7C) but a summoned member
    // stayed at the partner's side. On: the posse's own attack command, as cPosseSimulator 0xD52E40 gives the host's
    // pack (CreatureCommandStimulus 0x10 with the target as its sender, BUG-0735). Off: SetTarget only (63-dev).
    // (the relay's checks first, case 0x10 at Steam 0xC42FC0: the member qualifies, PosseMemberCanDefend 0xD7D0E0)
    if (g_attack_cmd && a_CmdStim && a_CanDefend && skg::readable(skg::field<uint8_t*>(c, 0xB4C), 0x200)) {
        if (!skg::cdecl_call<bool>(a_CanDefend, c)) {
            g_declined++;
            return;
        }
        skg::thiscall<void*>(a_CmdStim, c, 0x10u, 0u, 0x20030030u, 0x40040000u, 0x7F7FFFFFu, 0x100000u, 0u, 0u, 0u,
                             victim, (uint32_t)0);
        g_commands++;
    } else
        skg::vcall<void>(c, 0x84 / 4, (uint8_t*)victim + game::kCombatOff, 1, 0); // SetTarget (as the charge test)
    m.target = victim;
    m.target_until = now + 15;
}

void assist(uint8_t guest, void* victim)
{
    if (!g_fix || !victim || member(victim) || game::is_puppet(victim))
        return;
    double now = H->time_seconds();
    const size_t tok = party_enter(guest); // (5CD: the pack's checks see the partner as the leader)
    for (auto& [c, m] : g_members)
        if (m.owner == guest && alive(c, m) && m.target != victim) {
            attack(c, m, victim, now);
            g_assists++;
        }
    party_leave(tok);
}

void defend(uint8_t guest, void* attacker)
{
    if (!g_fix || !attacker || member(attacker) || game::is_puppet(attacker) || attacker == game::avatar())
        return;
    double now = H->time_seconds();
    const size_t tok = party_enter(guest);
    for (auto& [c, m] : g_members)
        if (m.owner == guest && alive(c, m) && m.target != attacker) {
            attack(c, m, attacker, now);
            g_defends++;
        }
    party_leave(tok);
}

// BUG-9249 (TITAN QA 2026-11-16, Samuth.spo): the posse's summoned count (+0x1C; the pack takes no recruit while its
// members + that count reach its size +0x18) read 3 of 3 with no summon running and nobody in the pack: the host's
// recruit was refused ("cannot join"); the count came with the save (3 at the start of every session there), left by
// flocks of older builds (BUG-9233). The game counts a summon's flock up at its start and down at its end, so with no
// summon running the count is 0: a count left over then is cleared. `coop.fix creature.posse_count_heal off`: kept.
static bool g_count_heal = true;
COOP_FIX("creature.posse_count_heal", "BUG-9249", g_count_heal,
         "creature: a summoned-creature count left in the pack with no summon running is cleared (the pack takes recruits again)");
static uint32_t g_count_healed;
static double g_next_count_check;
static void heal_posse_count(double now)
{
    // (5CD: on every game, the guests too: a partner's own pack panel counts its summoned mirrors there; QA 2026-11-17 saw
    // -5 on a guest after hot reloads, and its pack refused every member)
    if (!g_count_heal || now < g_next_count_check || !game::in_creature_stage() || drama::summon_running() || !g_frames.empty())
        return;
    g_next_count_check = now + 2;
    static uintptr_t posse_get = H->sym("SporeCoop::cPosseSimulator_Get");
    uint8_t* posse = posse_get ? skg::cdecl_call<uint8_t*>(posse_get) : nullptr;
    if (!posse || !skg::readable(posse, 0x20))
        return;
    int32_t summoned = 0; // the summoned creatures the list really holds (a flock in it counts, as the game counts it)
    for (void* c : host_posse())
        if (c && game::listed(c) && summoned_member(c))
            summoned++;
    if (skg::field<int32_t>(posse, 0x1C) == summoned)
        return;
    H->log(M, SK_INFO, "guest_pack: the pack's summoned count %d with no summon running: set to %d (pack size %d, %zu member(s), BUG-9249)",
           skg::field<int32_t>(posse, 0x1C), summoned, skg::field<int32_t>(posse, 0x18), host_posse().size());
    skg::field<int32_t>(posse, 0x1C) = summoned;
    g_count_healed++;
}

// 5CD: each party's list loses objects that are gone and dead members (through the game's own removal, in the party);
// the mod's bookkeeping follows the lists (a summon's end takes its flock out again, as for the host's own); a member
// far from its partner (a respawn at a nest, a teleport) is put beside it as before
static void party_tick(double now)
{
    for (auto& [p, pt] : g_parties) {
        party_drop_freed(p);
        std::vector<void*> dead;
        for (void* c : party_list(p))
            if (game::is_dead(c))
                dead.push_back(c);
        if (!dead.empty() && a_PosseRemove)
            if (const size_t tok = party_enter(p)) {
                for (void* c : dead) {
                    skg::thiscall<void>(a_PosseRemove, *(void**)a_PossePtr, c, 0u, 0u);
                    g_party_removed++;
                }
                party_leave(tok);
            }
    }
    std::unordered_map<uint8_t, std::vector<void*>> lists;
    int slot = 0;
    for (auto it = g_members.begin(); it != g_members.end();) {
        void* c = it->first;
        Member& m = it->second;
        auto li = lists.find(m.owner);
        if (li == lists.end())
            li = lists.emplace(m.owner, party_list(m.owner)).first;
        if (!alive(c, m) || std::find(li->second.begin(), li->second.end(), c) == li->second.end()) {
            it = g_members.erase(it);
            g_dropped++;
            continue;
        }
        if (driven(c)) { // decision 5EA: its partner's game runs it
            drive(c, m, now);
            ++it;
            slot++;
            continue;
        }
        void* puppet = C.puppet_of ? C.puppet_of(m.owner) : nullptr;
        // Following: in the host's game a pack member follows the creature it senses with the avatar mark (Steam 0xAF9570
        // from cCreatureAnimal::Update posts stimulus 0x4000000 from it, value 5.0; runtime 2026-11-17: the host's recruit
        // carried it from the host's creature). A partner's member never took it from the partner's puppet, not with the
        // mark lent inside the party nor with the stimulus posted by hand (the member walked off instead): the choice
        // there is not understood yet. So the member is walked back to its partner as before (the game's MoveTo), and
        // left alone while it fights near it. (`coop.gpack follow 1`, tests: the stimulus by hand at 4 Hz.)
        if (puppet && a_PostStim && g_follow_test && now >= m.next_follow) {
            m.next_follow = now + 0.25;
            uint8_t* brain = skg::field<uint8_t*>(c, 0xB4C);
            if (brain && skg::readable(brain, 0x200)) {
                const float five = 5.f;
                uint32_t v;
                std::memcpy(&v, &five, 4);
                skg::thiscall<void>(a_PostStim, brain + 8, 0x4000000u, 0u, v, puppet);
                g_follow_posts++;
            }
        }
        if (puppet && now >= m.next_move) {
            m.next_move = now + 0.5;
            if (m.target && (now > m.target_until || game::is_dead(m.target) || !game::object_alive(m.target)))
                m.target = nullptr;
            const float d = net::dist(game::position(c), game::position(puppet));
            if (d > 80) {
                place_near(c, puppet, slot);
                g_teleports++;
            } else if (m.target && d < 40) {
                // fighting near the partner: the game's attack
            } else if (d > 6 && !g_follow_test) {
                game::move_to(c, game::position(puppet), 3.f);
                g_leashed++;
            }
        }
        ++it;
        slot++;
    }
    send_lists(now, false); // phase 2
}


static void mirror_tick()
{
    // guest: a proxy freed without our removal (a load) leaves the own pack list by hand (the game would read it)
    void* nm = game::noun_manager();
    if (g_mirror.empty() || !nm)
        return;
    void** b = (void**)*list_slot(nm, 0);
    void** e = (void**)*list_slot(nm, 1);
    for (void* c : std::vector<void*>(g_mirror)) {
        if (game::listed(c))
            continue;
        g_mirror.erase(std::remove(g_mirror.begin(), g_mirror.end(), c), g_mirror.end());
        if (b && e > b && e - b <= 64) {
            void** w = std::remove(b, e, c);
            *list_slot(nm, 1) = w;
            e = w;
        }
        g_mirror_removed++;
    }
}

// 5CD: a creature with the pack bit in no pack list (the host's or a partner's) belongs to nobody: a pack lost by a fault,
// a hot reload of the mod (tests) or a save made by an older build. The bit goes, so it can be recruited again and does
// not take part in pack behaviour for nobody. (every 5 s, host)
static uint32_t g_orphans_cleared;
struct OrphanQ {
    std::vector<void*> listed;
    uint32_t cleared = 0;
};
static void clear_orphans(double now)
{
    if (now < g_next_orphans || !party_mode() || !is_host() || !game::in_creature_stage() || !g_frames.empty())
        return;
    g_next_orphans = now + 5;
    OrphanQ q;
    q.listed = host_posse();
    for (auto& [p, pt] : g_parties)
        for (void* c : party_list(p))
            q.listed.push_back(c);
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto& q = *(OrphanQ*)u;
            if (noun != 0x18EB45Eu || !game::object_alive(o) || game::is_puppet(o) || o == game::avatar() ||
                !(skg::field<uint32_t>(o, game::kFlagsOff) & 0x100u) ||
                std::find(q.listed.begin(), q.listed.end(), o) != q.listed.end() || npc::is_proxy(o))
                return true;
            skg::field<uint32_t>(o, game::kFlagsOff) &= ~0x100u;
            q.cleared++;
            return true;
        },
        &q);
    if (q.cleared) {
        g_orphans_cleared += q.cleared;
        H->log(M, SK_INFO, "guest_pack: %u creature(s) with the pack bit in no pack: the bit cleared (decision 5CD)", q.cleared);
    }
}

void tick(double now)
{
    heal_posse_count(now);
    clear_orphans(now);
    if (C.sess && C.sess->role() == net::Role::Client) {
        mirror_tick();
        own_tick_guest(now);
    }
    if (!g_frames.empty()) { // a fault the game caught left a party open: the ledger gave the host's pack back already
        g_frames_lost += (uint32_t)g_frames.size();
        H->log(M, SK_WARN, "guest_pack: %zu party context(s) left open by a fault: dropped (decision 5CD)", g_frames.size());
        g_frames.clear();
    }
    if (party_mode() && is_host() && game::in_creature_stage() && C.puppet_of) // phase 3: a pack noted on this planet
        for (uint8_t id = 1; id < (uint8_t)net::kMaxPlayers; id++)
            if (void* pup = C.puppet_of(id); pup && !game::is_dead(pup))
                restore_pack(id);
    if (now >= g_next_family) { // phase 4: which nest herd is which partner's family (once a second)
        g_next_family = now + 1;
        g_family_of.clear();
        if (g_family_fix && party_mode() && is_host() && game::in_creature_stage() && C.puppet_of)
            for (uint8_t id = 1; id < (uint8_t)net::kMaxPlayers; id++)
                if (void* h = C.puppet_of(id) ? home_nests::family_herd(id) : nullptr; h && h != game::avatar_herd())
                    g_family_of[h] = id;
    }
    if (party_mode() && is_host() && game::in_creature_stage() && !g_parties.empty()) {
        party_tick(now);
        return;
    }
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

// 5CD: a partner who leaves takes nobody along: its pack's members are let go the game's way (in its party, while its
// puppet is still there), as members leave a pack in single player
static void disband(uint8_t id)
{
    Party* pt = party_of(id, false);
    if (!pt)
        return;
    party_drop_freed(id);
    auto l = party_list(id);
    if (!l.empty() && a_PosseRemove)
        if (const size_t tok = party_enter(id)) {
            for (void* c : l) {
                skg::thiscall<void>(a_PosseRemove, *(void**)a_PossePtr, c, 0u, 0u);
                g_party_removed++;
            }
            party_leave(tok);
        }
    for (void* c : party_list(id)) // (no puppet any more: out by hand, the bit cleared)
        if (c && game::listed(c))
            skg::field<uint32_t>(c, game::kFlagsOff) &= ~0x100u;
    pt->list[1] = pt->list[0];
    H->log(M, SK_INFO, "guest_pack: player %u's pack let go (%zu member(s), decision 5CD)", id, l.size());
}

void on_player_leave(uint8_t id)
{
    if (is_host() && party_mode())
        note_pack(id); // (phase 3: where its pack stood when it left)
    disband(id);
    g_restored.erase(id); // (5CD phase 3: a rejoin brings its noted pack back)
    for (auto it = g_members.begin(); it != g_members.end();)
        it = it->second.owner == id ? g_members.erase(it) : std::next(it);
}

void reset()
{
    g_members.clear();
    g_frames.clear();
    g_restored.clear(); // phase 3: a load (the same planet again) brings the noted packs back once more
    // guest: after a load the old list and its proxies are gone; after an editor round trip the proxies still listed in the
    // own pack list stay this game's mirror (else nothing would take them out before they are removed)
    g_mirror.clear();
    if (void* nm = game::noun_manager(); nm && C.sess && C.sess->role() == net::Role::Client) {
        void** b = (void**)*list_slot(nm, 0);
        void** e = (void**)*list_slot(nm, 1);
        for (void** it = b; b && e > b && e - b <= 64 && it < e; ++it)
            if (*it && game::listed(*it) && npc::is_proxy(*it))
                g_mirror.push_back(*it);
    }
    // 5CD: the local entity changed: a load (every creature freed: the lists empty out) or an editor round trip (the
    // world stays: a member still listed with the pack bit stays in its partner's pack; an object at a freed address
    // is a new creature without the bit and leaves the list)
    for (auto& [p, pt] : g_parties) {
        party_drop_freed(p);
        void** b = (void**)pt->list[0];
        void** e = (void**)pt->list[1];
        void** w = b;
        int summoned = 0;
        for (void** it = b; b && it < e; ++it)
            if (game::listed(*it, 0x18EB45Eu) && (skg::field<uint32_t>(*it, game::kFlagsOff) & 0x100u) && !game::is_dead(*it)) {
                *w++ = *it;
                void* h = game::herd_of(*it);
                const bool s = h && skg::readable(h, 0x194) && skg::field<uint8_t>(h, 0x192);
                summoned += s;
                join(p, *it, s);
            }
        if (b)
            pt->list[1] = w;
        if (pt->made)
            skg::field<int32_t>(pt->posse, 0x1C) = summoned;
    }
}
void set_herd_fix(bool on) { g_herd_fix = on; }

std::string status()
{
    char b[320];
    std::snprintf(b, sizeof b,
                  "guest_pack: fix=%d members=%zu summons=%u summoned=%u recruits=%u assists=%u defends=%u leashed=%u "
                  "teleports=%u dropped=%u herd_moves=%u herd_fix=%d reuse_fix=%d reused=%u pooled=%u attack_cmd=%d commands=%u declined=%u\n",
                  (int)g_fix, g_members.size(), g_summons, g_summoned, g_recruits, g_assists, g_defends, g_leashed,
                  g_teleports, g_dropped, g_herd_moves, (int)g_herd_fix, (int)g_reuse_fix, g_reused, g_pooled, (int)g_attack_cmd, g_commands, g_declined);
    std::string out = b;
    std::snprintf(b, sizeof b, "parties: mode=%d made=%u enters=%u lost=%u removed=%u freed=%u open=%zu commands=%u/%u follow_posts=%u family=%zu/%u\n",
                  (int)party_mode(), g_parties_made, g_enters, g_frames_lost, g_party_removed, g_party_freed, g_frames.size(),
                  g_cmds_given, g_cmds_got, g_follow_posts, g_family_of.size(), g_family_enters);
    out += b;
    std::snprintf(b, sizeof b, "own pack panel (guest): mirror=%zu added=%u removed=%u refused=%u lists=%u got / %u sent, notes=%u restored=%u orphans=%u\n",
                  g_mirror.size(), g_mirror_added, g_mirror_removed, g_mirror_refused, g_lists_got, g_lists_sent, g_notes_written,
                  g_pack_restored, g_orphans_cleared);
    out += b;
    out += own_status();
    for (auto& [p, pt] : g_parties) {
        auto l = party_list(p);
        std::snprintf(b, sizeof b, "  party %u: pack manager %p size %d summoned %d members %zu\n", p, (void*)pt->posse,
                      pt->made ? skg::field<int32_t>(pt->posse, 0x18) : -1, pt->made ? skg::field<int32_t>(pt->posse, 0x1C) : -1,
                      l.size());
        out += b;
        for (void* c : l) {
            std::snprintf(b, sizeof b, "    %p flags=%08X hp=%.1f species=%08X herd=%p\n", c,
                          game::listed(c) ? skg::field<uint32_t>(c, game::kFlagsOff) : 0u, game::listed(c) ? game::health(c) : -1.f,
                          game::listed(c) ? game::species_instance(c) : 0u, game::listed(c) ? game::herd_of(c) : nullptr);
            out += b;
        }
    }
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

std::string callers(bool start)
{
    if (start) {
        if (!o_PosseGetHook && a_PosseGet)
            H->hook_create(M, (void*)a_PosseGet, (void*)h_PosseGet, (void**)&o_PosseGetHook);
        g_posse_callers.clear();
        g_count_callers = true;
        return "counting\n";
    }
    g_count_callers = false;
    std::vector<std::pair<uint32_t, uintptr_t>> v;
    for (auto& [a, n] : g_posse_callers)
        v.push_back({n, a});
    std::sort(v.rbegin(), v.rend());
    std::string out;
    char b[64];
    for (auto& [n, a] : v) {
        std::snprintf(b, sizeof b, "  %08X x%u\n", (unsigned)a, n);
        out += b;
    }
    return out;
}

void set_follow_test(bool on) { g_follow_test = on; }

void set_ctx_test(bool on) { g_ctx_test = on; }

void host_command(void* target)
{
    void* av = game::avatar();
    if (!av || !target || !a_PlayerCmd || game::is_dead(target))
        return;
    skg::vcall<void>(av, 0x84 / 4, (uint8_t*)target + game::kCombatOff, 1, 0);
    skg::thiscall<void*>(a_PlayerCmd, av, 0x10u, 0u, 0x7F7FFFFFu, 0u, 0u, target, 0u);
}

// ---------------------------------------------------------------- decision 5EA: each partner's own game runs its pack
// The owner (2026-11-17, "is it possible to have each player run their own side of things instead of it being all on the
// host?"): a partner's pack thinks in the partner's own game, where that player's creature is the game's own avatar and
// the pack its own pack (phase 2 put the members, proxies of the host's creatures, into that game's pack list). There the
// game's single-player code follows, defends and fights by the player's own orders (Creature_PlayerCommand, no relay to
// the host). The partner's game sends where each member is (10 Hz); the host runs no AI for them and walks them there
// (the host and the other partners see them as before). Their bites on the host's creatures go to the host like the
// player's own (npc::damage_filter). The host keeps the members' health and deaths (every hit on them lands there).
// A member without a state for kDriveStale s (the partner left, its game paused, the switch off there) thinks in the
// host's game again (5CD). Both games need the switch: `coop.fix creature.pack_own_game off` = 5CD (the host runs it).
static bool g_own_game = true;
COOP_FIX("creature.pack_own_game", "BUG-9248", g_own_game,
         "creature: a partner's own game runs its pack (follows, defends, fights by that player's orders); the host shows it");
static constexpr double kDriveStale = 1.5;
static double g_own_next_lend, g_own_next_send;
static uint32_t g_own_lends, g_own_sent, g_own_got, g_own_bad, g_own_drives, g_own_jumps;
struct OwnAnim {
    uint16_t id;
    uint32_t anim;
    uint8_t how, a;
    int32_t b;
};
static std::vector<OwnAnim> g_own_anims; // guest: started since the last send
static uint32_t g_own_anims_sent, g_own_anims_played, g_own_anims_failed;

void own_anim(void* c, uint32_t anim, int how, int a, int b)
{
    uint16_t id = npc::id_of(c);
    if (id != 0xFFFF && g_own_anims.size() < 32)
        g_own_anims.push_back({id, anim, (uint8_t)how, (uint8_t)(a & 0xFF), (int32_t)b});
}

static void send_own_anims()
{
    if (g_own_anims.empty())
        return;
    net::Writer w;
    w.u8(kPackAnimKind);
    w.u8((uint8_t)g_own_anims.size());
    for (auto& e : g_own_anims) {
        w.u16(e.id);
        w.u32(e.anim);
        w.u8(e.how);
        w.u8(e.a);
        w.u32((uint32_t)e.b);
    }
    C.sess->send_reliable_to(0, net::MSG_PLAYER_RULE, w);
    g_own_anims_sent += (uint32_t)g_own_anims.size();
    g_own_anims.clear();
}

void on_pack_anims(uint8_t from, net::Reader& r)
{
    uint8_t n = r.u8();
    for (uint8_t i = 0; i < n && i < 32 && r.ok(); i++) {
        uint16_t id = r.u16();
        uint32_t anim = r.u32();
        uint8_t how = r.u8(), a = r.u8();
        int32_t b = (int32_t)r.u32();
        if (!r.ok() || !own_game() || !is_host())
            return;
        void* c = npc::host_npc(id);
        auto it = c ? g_members.find(c) : g_members.end();
        if (it == g_members.end() || it->second.owner != from || game::is_dead(c))
            continue;
        if (game::play_behaviour_anim(c, anim, how, a, b))
            g_own_anims_played++;
        else
            g_own_anims_failed++;
    }
}

bool own_game() { return g_own_game && party_mode(); }

bool own_member(void* c)
{
    return own_game() && c && std::find(g_mirror.begin(), g_mirror.end(), c) != g_mirror.end() && npc::lent_for(c) > 0;
}

static void own_tick_guest(double now)
{
    if (!own_game() || g_mirror.empty() || !C.sess || !C.sess->connected() || !game::in_creature_stage()) {
        g_own_anims.clear();
        return;
    }
    send_own_anims(); // (as they start: a bite shows on the host's screen when it lands)
    if (now >= g_own_next_lend) {
        g_own_next_lend = now + 0.5;
        for (void* c : g_mirror)
            if (game::listed(c) && !game::is_dead(c) && npc::lend_local_ai(c, 2.0))
                g_own_lends++;
    }
    if (now < g_own_next_send)
        return;
    g_own_next_send = now + 0.1;
    std::vector<net::NpcState> v;
    for (void* c : g_mirror) {
        uint16_t id = game::listed(c) && npc::lent_for(c) > 0 ? npc::id_of(c) : 0xFFFF;
        if (id == 0xFFFF)
            continue;
        net::NpcState st;
        st.net_id = id;
        st.pos = game::position(c);
        st.rot = game::orientation(c);
        st.vel = game::velocity(c);
        v.push_back(st);
        if (v.size() == 16)
            break;
    }
    if (v.empty())
        return;
    net::Writer w;
    w.u8(kPackStateKind);
    w.u8((uint8_t)v.size());
    for (auto& st : v)
        st.write(w);
    C.sess->send_unreliable_to(0, net::MSG_PLAYER_RULE, w);
    g_own_sent++;
}

void on_pack_states(uint8_t from, const std::vector<net::NpcState>& v)
{
    if (!own_game() || !is_host())
        return;
    const double now = H->time_seconds();
    for (auto& st : v) {
        void* c = npc::host_npc(st.net_id);
        auto it = c ? g_members.find(c) : g_members.end();
        if (it == g_members.end() || it->second.owner != from || !std::isfinite(st.pos.x) || !std::isfinite(st.pos.y) ||
            !std::isfinite(st.pos.z) || !std::isfinite(st.vel.x) || !std::isfinite(st.vel.y) || !std::isfinite(st.vel.z)) {
            g_own_bad++;
            continue;
        }
        it->second.drv = st;
        it->second.drv_at = now;
        g_own_got++;
    }
}

static bool driven(void* c)
{
    if (!g_own_game || g_members.empty())
        return false;
    auto it = g_members.find(c);
    return it != g_members.end() && it->second.drv_at > 0 && H->time_seconds() - it->second.drv_at < kDriveStale;
}

static void drive(void* c, Member& m, double now)
{
    // the proxies' driving (npc.cpp guest_tick): a running member is walked by the game and placed on the partner's
    // path (walk_place), a standing one is stepped to its spot (step); a new state is extrapolated by its age
    const net::NpcState& st = m.drv;
    const float age = (float)std::min(now - m.drv_at, 0.5);
    const net::Vec3 at = game::position(c);
    if (m.drv_seen.x != 0 || m.drv_seen.y != 0 || m.drv_seen.z != 0)
        if (net::dist(at, m.drv_seen) > 3.f)
            g_own_jumps++;
    if (drive::speed(st.vel) > 0.3f && net::dist(at, st.pos) < 8.f) {
        if (!m.drv_walk)
            m.dst.kinematic = false; // a new walk: its first goal at once
        drive::WalkPlace o;
        o.ground = true;
        o.max_dt = 0.4;
        o.snap = 2.5f;
        o.keep_lost = true;
        o.catch_prop = true;
        const net::Vec3 aim{st.pos.x + st.vel.x * age, st.pos.y + st.vel.y * age, st.pos.z + st.vel.z * age};
        drive::walk_place(c, aim, st.rot, st.vel, m.dst, now, o);
        m.drv_walk = true;
    } else {
        if (m.drv_walk) {
            m.drv_walk = false;
            if (m.dst.kinematic)
                drive::walk_end(c, st.pos, m.dst, now);
        }
        drive::step(c, st.pos, st.rot, st.vel, m.dst, now, age, true, 8.f, true, true, false, true);
    }
    g_own_drives++;
    m.drv_seen = game::position(c);
}

std::string own_status()
{
    char b[256];
    std::snprintf(b, sizeof b, "own game (5EA): on=%d lends=%u sent=%u anims=%u (guest) got=%u bad=%u drives=%u jumps=%u driven=%d anims=%u/%u failed (host)\n",
                  (int)own_game(), g_own_lends, g_own_sent, g_own_anims_sent, g_own_got, g_own_bad, g_own_drives, g_own_jumps,
                  (int)std::count_if(g_members.begin(), g_members.end(), [](auto& e) { return driven(e.first); }),
                  g_own_anims_played, g_own_anims_failed);
    return b;
}

} // namespace guest_pack
