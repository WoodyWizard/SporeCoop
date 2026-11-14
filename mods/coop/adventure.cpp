#include "adventure.h"
#include "adventure_sync.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include "driver.h"
#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/interp.h"
#include "sporekit_game.hpp"

namespace adventure {
namespace {
using namespace adventure_sync;
const SkHost* H;
SkMod* M;
net::Session* session;
using Credit = void(SK_THISCALL*)(void*, void*, void*, void*);
Credit original;
void* current_play;
std::array<uint32_t,3> mission{};
uint32_t nonce, host_run, applied;
uint32_t serial = GetTickCount() ^ (GetCurrentProcessId() << 16);
std::array<uint32_t,net::kMaxPlayers> visitors{};
std::vector<Goal> local;
std::vector<Packet> incoming, journal;
bool scheduled, replaying;
double hello_at;
int observed_act=-1, observed_completed=-1;

// ---- BUG-9089 (44): combat adventures. Every objective type is shared through the host journal
// (a credit for a defeated object counts once, whichever game saw it die); a scenario object (a
// building or creature placed by the adventure) that dies in one game dies in every game; the host
// decides the mission's end (a guest's game never fails on its own; the host's success / failure
// reaches every guest). `coop.fix adventure.combat off`: move-to and talk only, as in 43.
bool g_fix_combat = true;
COOP_FIX("adventure.combat", "BUG-9089", g_fix_combat,
         "adventure: every objective is shared, an adventure object that dies in one game dies in all, the host decides the mission's end");
constexpr uint32_t kBuildingNoun = 0x70703B3u, kCreatureNoun = 0x18EB45Eu, kSpatialType = 0x1186577u;
constexpr uintptr_t kBuildingCombatant = 0x120, kCombatantHp = 0x38;
using SetStateFn = void(SK_THISCALL*)(void*, int);
SetStateFn o_SetState;
bool host_ending;                 // guest: applying the host's outcome now
int host_outcome;                 // guest: the host's ended state (5 / 6), 0 none
unsigned blocked_failures, mirrored_kills, deaths_out, deaths_in, credits_held, outcomes_out;
struct Known { uint32_t id; bool alive; float hp, top; };
std::map<std::pair<void*,uint32_t>,Known> known;      // (object, serial) -> its id, alive last poll
std::unordered_map<void*,uint32_t> spatial_ids;      // spatial subobject -> id (credits name it)
std::map<uint32_t,uint32_t> deaths_local, deaths_wanted, deaths_told;
std::set<std::tuple<uint32_t,uint32_t,uint32_t>> applied_objects; // (act, slot, object) applied here
std::set<std::pair<void*,uint32_t>> killed_here; // objects this game destroyed for another game's death (once each)
double poll_at;

bool context(uint8_t*& play, std::array<uint32_t,3>& key)
{
    play = modes::adventure_play();
    if (!play || !game::avatar()) return false;
    auto address = H->sym("App::cScenarioMode::ptr");
    if (!address || !skg::readable((void*)address,4)) return false;
    auto mode = *(uint8_t**)address;
    if (!mode || !skg::readable(mode,0x7C)) return false;
    auto data = *(uint8_t**)(mode+0x74);
    if (!data || !skg::readable(data,0x14)) return false;
    auto resource = *(uint8_t**)(data+0x10);
    if (!resource || !skg::readable(resource,0x14)) return false;
    memcpy(key.data(),resource+8,12);
    return key[0] || key[1] || key[2];
}

uint8_t* goal_at(uint8_t* play, uint32_t slot)
{
    uintptr_t first=*(uintptr_t*)(play+0x64), end=*(uintptr_t*)(play+0x68);
    if (!first || end<first || (end-first)%0x1AC || (end-first)/0x1AC>8 ||
        slot >= (end-first)/0x1AC || !skg::readable((void*)first,end-first)) return nullptr;
    return (uint8_t*)(first+slot*0x1AC);
}

Goal describe(uint8_t* play, uint8_t* goal, uint32_t slot)
{
    return {*(uint32_t*)(play+0xB8),slot,*(uint32_t*)(goal+0x24),
            *(uint32_t*)(goal+0x28),*(uint32_t*)(goal+0x2C),*(uint32_t*)(goal+0x30)};
}
bool same_goal(const Goal& a, const Goal& b)
{
    return a.act==b.act && a.slot==b.slot && a.kind==b.kind && a.target==b.target && a.secondary==b.secondary &&
           a.required==b.required;
}

bool live(uint8_t*& play)
{
    std::array<uint32_t,3> key;
    return context(play,key) && play==current_play && key==mission && nonce;
}

Packet packet(Kind kind, uint32_t visitor)
{
    Packet p; p.kind=kind; p.mission=mission; p.run=host_run; p.visitor=visitor;
    return p;
}

void send(uint8_t to, const Packet& p)
{
    net::Writer w; p.write(w); session->send_reliable_to(to,net::MSG_ADVENTURE,w);
}

void to_guests(Packet p)
{
    for (auto player:session->players()) {
        if (!player.id || player.id>=visitors.size() || !visitors[player.id]) continue;
        p.visitor=visitors[player.id]; send(player.id,p);
    }
}

// ---- scenario objects (BUG-9089) ----
// The id of an adventure object, the same in every game: its class (+0x1C) and its scenario marker
// (+0x18: type, spawn position and orientation, docs/re/space.md#space-duo-adventure-combat). 0 = not one.
uint32_t object_id(void* o)
{
    if (!o || !skg::readable(o,0x24)) return 0;
    auto marker=*(uint8_t**)((uint8_t*)o+0x18);
    if (!marker || !skg::readable(marker,0x24)) return 0;
    uint32_t h=0x811C9DC5u;
    auto mix=[&](const uint8_t* p,size_t n){ for (size_t i=0;i<n;++i){ h^=p[i]; h*=0x01000193u; } };
    mix(marker,0x24);
    mix((uint8_t*)o+0x1C,4);
    return h?h:1;
}
float object_hp(void* o, uint32_t noun)
{
    if (noun==kBuildingNoun) {
        auto c=(uint8_t*)o+kBuildingCombatant;
        return skg::readable(c,kCombatantHp+4) ? *(float*)(c+kCombatantHp) : 0.f;
    }
    return game::health(o);
}
void kill(void* o, uint32_t noun)
{
    float hp=object_hp(o,noun);
    if (!(hp>0)) return;
    if (noun==kBuildingNoun) {
        auto c=(uint8_t*)o+kBuildingCombatant;
        auto vt=skg::readable(c,4) ? *(uintptr_t**)c : nullptr;
        if (!vt || !skg::readable(vt,0x1C)) return;
        static const float dir[3]={0.f,0.f,1.f}; // TakeDamage needs a direction (CLAUDE.md 2)
        // cBuilding::TakeDamage (combatant vt+0x18): thiscall(combatant, dmg, attacker political id, type, dir*, attacker) ret 0x14
        skg::thiscall<int>((uintptr_t)vt[6],c,hp+100000.f,0xFFFFFFFFu,0,(const void*)dir,(void*)nullptr);
    } else {
        game::apply_damage(o,hp+100000.f,0,nullptr);
    }
    ++mirrored_kills;
}

bool creatures_by_key(); // BUG-9114 (below)

void poll(double now)
{
    if (now<poll_at) return;
    poll_at=now+0.25;
    struct Q { std::map<std::pair<void*,uint32_t>,Known> seen; std::vector<std::pair<void*,uint32_t>> alive; void* av; } q;
    q.av=game::avatar();
    spatial_ids.clear();
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto q=(Q*)u;
        if ((noun!=kBuildingNoun && noun!=kCreatureNoun) || o==q->av || game::is_puppet(o)) return true;
        uint32_t id=object_id(o);
        if (!id) return true;
        float hp=object_hp(o,noun);
        bool alive=game::object_alive(o) && hp>0;
        q->seen[{o,game::object_serial(o)}]={id,alive,hp,hp};
        if (alive) q->alive.push_back({o,noun});
        if (void* sp=game::cast(o,kSpatialType)) spatial_ids[sp]=id;
        return true;
    },&q);
    // a death: alive before and now dead; or gone after it had been hurt (a destroyed building can leave the
    // list at once). An object that leaves unhurt was removed by the adventure (an act change, a load): no death.
    std::map<uint32_t,uint32_t> fresh;
    for (auto& [key,k]:known) {
        auto it=q.seen.find(key);
        if (it!=q.seen.end()) it->second.top=std::max(it->second.top,k.top);
        if (!k.alive) continue;
        if (it!=q.seen.end() ? !it->second.alive : (k.top>0 && k.hp<k.top*0.95f)) ++fresh[k.id];
    }
    known=std::move(q.seen);
    for (auto& [id,n]:fresh) deaths_local[id]+=n;
    bool host=session->role()==net::Role::Host;
    for (auto& [id,n]:deaths_local) {
        if (deaths_told[id]>=n) continue;
        deaths_told[id]=n;
        auto p=packet(Death,nonce); p.object=id; p.deaths=std::min<uint32_t>(n,64);
        if (host) to_guests(p); else send(0,p);
        ++deaths_out;
    }
    // what another game saw die: the same objects die here (any alive one of that id). BUG-9114: while the
    // host simulates the creatures, a creature's death is the host's and reaches a guest by its key
    // (guest_drive): "any one of that id" killed a living lookalike (soldiers of one marker share an id)
    const bool by_key=creatures_by_key();
    for (auto& [id,want]:deaths_wanted) {
        uint32_t have=deaths_local[id];
        for (auto& [o,noun]:q.alive) {
            if (have>=want) break;
            if (by_key && noun==kCreatureNoun) continue;
            if (object_id(o)!=id || !killed_here.insert({o,game::object_serial(o)}).second) continue;
            kill(o,noun);
            ++have; // counted as a death by the next poll
        }
    }
}

// ---- BUG-9114: the host's game simulates the scenario's creatures ----
// Before, every game ran its own copy of each enemy, ally and character: deaths, credits and the end
// were shared (BUG-9089) but movement was not, and each game's enemies only saw their own captain (the
// partner was a name tag). With partner bodies (adventure.captain_bodies) the host's creatures pick the
// nearest captain, the host's or a partner's body (npc.cpp ai_target, as in the Creature stage), and
// their hits on a body reach that partner (MSG_DAMAGE). A guest's scenario creatures have no AI of their
// own: they follow the host's states (positions, facing, health), and the guest captain's hits on them
// go to the host, which applies them. `coop.fix adventure.enemies off`: each game simulates its own (44).
// Off by default in 44 (the creatures never struck a partner's body): on since BUG-9200 (bodies have the
// captain's side) and BUG-9201 (a guest's leftovers are removed, a creature the host's game killed dies
// here). Positions, health, deaths by key and corpses: docs/re/space.md#space-adventure-host-creatures.
bool g_fix_enemies = true;
COOP_FIX("adventure.enemies", "BUG-9114", g_fix_enemies,
         "adventure: the host's game moves the scenario's creatures for everyone, they attack partners' bodies, guests' hits go to the host");
void* (*body_of)(uint8_t player);
constexpr double kStreamEvery = 0.1, kFullEvery = 1.0, kActorTimeout = 5.0;
struct HostActor { void* obj; uint32_t id; net::Vec3 pos; float health; };
std::unordered_map<uint32_t,HostActor> host_actors; // host: key (object serial) -> its creature
double stream_at, full_at;
unsigned batches_out, actors_out, hits_in, hits_applied, hits_stale;
struct GuestActor {
    void* obj = nullptr; uint32_t serial = 0, id = 0;
    net::SnapshotBuffer buf; drive::State drv;
    double rx = 0; bool dead = false, corpse = false;
};
std::unordered_map<uint32_t,GuestActor> guest_actors; // guest: host key -> this game's creature
std::unordered_map<void*,uint32_t> guest_key_of;     // guest: creature -> host key
bool guest_driving;                                   // guest: scenario creatures follow the host now
unsigned batches_in, unpaired_now, hits_out, damage_dropped;
double batch_first, batch_last;                       // guest: when the host's states started / last came
// BUG-9201: a guest's scenario creature that no host creature drives (the guest's own waves spawned from
// markers, a copy whose host creature died while it was unpaired) stood there without AI, often at 0 hp,
// and took the guest captain's hits for nothing. Unpaired for kExtraGrace while the host's states arrive,
// it is removed here. Off (`coop.fix adventure.guest_extras off`): it stays, as in 44.
bool g_fix_extras = true;
COOP_FIX("adventure.guest_extras", "BUG-9201", g_fix_extras,
         "adventure: a guest's creature that no host creature drives is removed; one whose host creature died dies");
constexpr double kExtraGrace = 5.0;
std::map<std::pair<void*,uint32_t>,double> extra_since; // guest: (creature, serial) -> unpaired since
unsigned extras_now, extras_removed;

bool enemies_on()
{
    return g_fix_enemies && g_fix_combat && session && session->connected() && nonce && current_play &&
           host_run && modes::adventure_body_ready();
}

bool creatures_by_key() { return enemies_on(); }

bool scenario_creature(void* o, void* av)
{
    return o && o!=av && !game::is_puppet(o) && object_id(o);
}

void host_stream(double now)
{
    if (now<stream_at) return;
    stream_at=now+kStreamEvery;
    const bool full=now>=full_at;
    if (full) full_at=now+kFullEvery;
    struct Q { std::vector<std::pair<void*,uint32_t>> list; void* av; } q{{},game::avatar()};
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto q=(Q*)u;
        if (noun==kCreatureNoun && scenario_creature(o,q->av)) q->list.push_back({o,object_id(o)});
        return true;
    },&q);
    std::unordered_map<uint32_t,HostActor> next;
    std::vector<ActorBatch> out;
    ActorBatch b; b.mission=mission; b.run=host_run; b.time_ms=GetTickCount();
    for (auto [o,id]:q.list) {
        uint32_t key=game::object_serial(o);
        if (!key || next.count(key)) continue;
        net::Vec3 p=game::position(o), v=game::velocity(o);
        net::Quat r=game::orientation(o);
        Actor a; a.key=key; a.id=id;
        a.pos[0]=p.x; a.pos[1]=p.y; a.pos[2]=p.z;
        a.rot[0]=r.x; a.rot[1]=r.y; a.rot[2]=r.z; a.rot[3]=r.w;
        a.vel[0]=v.x; a.vel[1]=v.y; a.vel[2]=v.z;
        a.health=game::health(o); a.max_health=game::max_health(o);
        a.flags=(game::is_dead(o) || !game::object_alive(o)) ? kActorDead : 0;
        auto prev=host_actors.find(key);
        // what did not move or change is sent once a second (a guest that joins late learns it too)
        bool changed=prev==host_actors.end() || net::dist(prev->second.pos,p)>0.05f ||
                     std::fabs(prev->second.health-a.health)>0.01f || drive::speed(v)>0.05f;
        next[key]={o,id,p,a.health};
        if ((!full && !changed) || !a.sane()) continue;
        b.actors.push_back(a);
        if (b.actors.size()==kMaxActorsPerBatch) { out.push_back(b); b.actors.clear(); }
    }
    if (!b.actors.empty()) out.push_back(b);
    host_actors=std::move(next);
    for (const auto& batch:out) {
        net::Writer w; batch.write(w);
        for (auto player:session->players())
            if (player.id && player.id<visitors.size() && visitors[player.id])
                session->send_unreliable_to(player.id,net::MSG_ADVENTURE_ACTORS,w);
        ++batches_out; actors_out+=unsigned(batch.actors.size());
    }
}

// guest: the host's states drive this game's copy of each creature (the partner puppets' driving)
void guest_drive(double now)
{
    struct Q { std::vector<std::pair<void*,uint32_t>> list; void* av; } q{{},game::avatar()};
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto q=(Q*)u;
        if (noun==kCreatureNoun && scenario_creature(o,q->av) && game::object_alive(o)) q->list.push_back({o,object_id(o)});
        return true;
    },&q);
    std::unordered_map<void*,uint32_t> present;
    for (auto [o,id]:q.list) present[o]=id;
    for (auto it=guest_actors.begin(); it!=guest_actors.end();) {
        auto& g=it->second;
        bool gone=g.obj && (!present.count(g.obj) || game::object_serial(g.obj)!=g.serial);
        if (gone) { guest_key_of.erase(g.obj); g.obj=nullptr; }
        if (now-g.rx>kActorTimeout) { if (g.obj) guest_key_of.erase(g.obj); it=guest_actors.erase(it); continue; }
        ++it;
    }
    unpaired_now=0;
    for (auto& [key,g]:guest_actors) {
        net::PlayerState s;
        if (g.buf.empty() || !g.buf.sample(now,s)) continue;
        if (!g.obj) {
            // the same scenario object here: the nearest one of its id not driven yet (ids repeat when
            // the adventure spawns several creatures from one marker)
            float best=12.f; void* pick=nullptr;
            for (auto [o,id]:q.list) {
                if (id!=g.id || guest_key_of.count(o)) continue;
                float d=net::dist(game::position(o),s.pos);
                if (d<best || !pick) { best=d; pick=o; }
            }
            if (!pick) { ++unpaired_now; continue; }
            g.obj=pick; g.serial=game::object_serial(pick); g.drv=drive::State{}; guest_key_of[pick]=key;
        }
        void* o=g.obj;
        if (game::is_dead(o)) { // a corpse lies where the host's lies (it died where this game had it)
            if (!g.corpse) {
                *(net::Vec3*)((uint8_t*)o+game::kVelOff)={0,0,0};
                game::teleport(o,s.pos,s.rot);
                g.corpse=true;
            }
            continue;
        }
        if (g.dead) { // the host's creature died: exactly this one dies here (its corpse is placed next frame)
            if (killed_here.insert({o,g.serial}).second) kill(o,kCreatureNoun); // health 0
            // BUG-9201: a creature dies in its AI (the behaviour tree's death node calls Die,
            // docs/re/creature.md 14.1), which a guest skips for these: it stood at 0 hp. The game's Die here.
            if (g_fix_extras && !game::is_dead(o)) game::play_death(o);
            continue;
        }
        g.drv.drawn=game::position(o);
        drive::path_update(g.drv,s.pos,now);
        net::Vec3 vel=s.vel;
        if (drive::speed(vel)<=0.3f) vel=drive::path_velocity(g.drv); // moves carried by an animation
        if (drive::speed(vel)>0.3f && net::dist(game::position(o),s.pos)<8.f) {
            drive::walk_place(o,s.pos,s.rot,vel,g.drv,now);
        } else {
            drive::walk_end(o,s.pos,g.drv,now);
            drive::step(o,s.pos,s.rot,vel,g.drv,now,0,true,4.f);
        }
        if (s.health>0) game::set_health(o,s.health); // the host's health (the guest's own hits went there)
    }
    // BUG-9201: what no host creature drives (the pairing above had its chance) goes after a grace time,
    // a corpse too (the host's corpse is gone: its key timed out)
    extras_now=0;
    const bool streaming=batch_last>0 && now-batch_last<1.5 && now-batch_first>3*kFullEvery;
    std::map<std::pair<void*,uint32_t>,double> since;
    for (auto [o,id]:q.list) {
        if (guest_key_of.count(o) || game::destroy_pending(o)) continue;
        ++extras_now;
        if (!streaming) continue;
        std::pair<void*,uint32_t> k{o,game::object_serial(o)};
        auto it=extra_since.find(k);
        const double t=it!=extra_since.end() ? it->second : now;
        if (g_fix_extras && now-t>=kExtraGrace) {
            game::destroy_creature(o);
            ++extras_removed;
            continue;
        }
        since[k]=t;
    }
    extra_since=std::move(since);
}

bool ai_skip(void* c)
{
    return guest_driving && scenario_creature(c,game::avatar());
}

// Keep values only: an act transition can rebuild both goal and actor lists.
void SK_THISCALL credit(void* self, void* raw_goal, void* spatial, void* second)
{
    auto play=(uint8_t*)self;
    if (!replaying && session->connected() && self==current_play && nonce &&
        *(int*)(play+0x90)==3) {
        for (uint32_t slot=0; slot<8; ++slot) {
            auto goal=goal_at(play,slot);
            if (!goal) break;
            if (goal!=raw_goal) continue;
            Goal description=describe(play,goal,slot);
            description.count=uint32_t(std::max(0,*(int*)(goal+4)));
            if (g_fix_combat && spatial) {
                auto it=spatial_ids.find(spatial);
                if (it!=spatial_ids.end()) description.object=it->second;
            }
            // A defeat (kind 3) of an adventure object is credited natively, at once, in every game: the
            // object dies in every game (deaths are mirrored), so each game counts it once by itself.
            // Held back, the game saw the target gone and the count short and failed the mission
            // ("You can no longer complete this goal: Destroy Big Evil Keep (0 of 5 destroyed)").
            if (g_fix_combat && description.kind==3 && description.object) break;
            const bool old_rule=!g_fix_combat && (description.kind==1 || description.kind==2) && description.required==1;
            if (!goal[0] && description.supported() && (g_fix_combat || old_rule) &&
                (g_fix_combat || description.count==0)) {
                if (std::none_of(local.begin(),local.end(),[&](const Goal& g){return g.same_credit(description);}) &&
                    local.size()<32)
                    local.push_back(description);
                ++credits_held;
                return; // Host commits before any captain advances this goal.
            }
        }
    }
    original(self,raw_goal,spatial,second);
}

// 0: wait for native transition, 1: applied/already present, -1: incompatible.
int apply(const Goal& description)
{
    uint8_t* play;
    if (!live(play)) return -1;
    uint32_t act=*(uint32_t*)(play+0xB8);
    if (description.act<act) return 1;
    if (description.act>act || *(int*)(play+0x90)!=3) return 0;
    auto goal=goal_at(play,description.slot);
    if (!goal || !same_goal(describe(play,goal,description.slot),description)) return -1;
    if (goal[0]) return 1;
    int count=*(int*)(goal+4);
    if (description.object) {
        if (applied_objects.count({description.act,description.slot,description.object})) return 1;
    } else {
        if (count>int(description.count)) return 1;
        if (count<int(description.count)) return 0;
    }
    replaying=true;
    original(play,goal,nullptr,nullptr);
    replaying=false;
    if (description.object) applied_objects.insert({description.act,description.slot,description.object});
    // Credit may replace the vector. Reacquire before inspecting it.
    if (!live(play)) return -1;
    if (*(uint32_t*)(play+0xB8)>description.act) return 1;
    goal=goal_at(play,description.slot);
    return goal && same_goal(describe(play,goal,description.slot),description) &&
           (goal[0] || *(int*)(goal+4)>count) ? 1 : -1;
}

// true: done with this credit (committed, a duplicate or stale); false: keep it until this game can take it
bool commit(const Goal& goal)
{
    if (std::any_of(journal.begin(),journal.end(),[&](const Packet& p){return p.goal.same_credit(goal);}) ||
        journal.size()>=kMaxCredits) return true;
    uint8_t* play;
    if (!live(play)) return false;
    uint32_t act=*(uint32_t*)(play+0xB8);
    if (goal.act<act) return true;
    if (goal.act>act || *(int*)(play+0x90)!=3) return false; // BUG-9089: not playing yet (an intro): later
    auto native=goal_at(play,goal.slot);
    if (!native || !same_goal(describe(play,native,goal.slot),goal) || native[0]) return true;
    Goal entry=goal;
    if (!entry.object) entry.count=uint32_t(std::max(0,*(int*)(native+4))); // the host's own count orders it
    int result=apply(entry);
    if (result==0) return false;
    if (result!=1) return true;
    Packet p=packet(Commit,nonce); p.goal=entry; p.sequence=uint32_t(journal.size()+1);
    journal.push_back(p);
    H->log(M,SK_INFO,"adventure: committed %u act=%u slot=%u type=%u object=%08X count=%u",p.sequence,entry.act,
           entry.slot,entry.kind,entry.object,entry.count);
    to_guests(p);
    return true;
}

// BUG-9089: the mission's end is the host's. A guest's game does not fail on its own (its captain
// down, a timer of its own simulation); the host's end reaches every guest.
void SK_THISCALL set_state(void* play, int state)
{
    if (g_fix_combat && session && session->connected() && play && play==current_play && nonce &&
        (state==5 || state==6)) {
        if (session->role()==net::Role::Host) {
            o_SetState(play,state);
            auto p=packet(Outcome,nonce); p.state=uint32_t(state); to_guests(p);
            ++outcomes_out;
            H->log(M,SK_INFO,"adventure: the mission ended here (state %d): told the guests",state);
            return;
        }
        if (!host_ending && state==6) {
            ++blocked_failures;
            H->log(M,SK_INFO,"adventure: a failure of this game alone was held (the host decides the mission's end)");
            return;
        }
    }
    o_SetState(play,state);
}

void work(void*)
{
    scheduled=false;
    if (!session->connected()) { reset(); return; }
    uint8_t* play;
    std::array<uint32_t,3> key;
    if (!context(play,key)) { reset(); return; }
    int act=*(int*)(play+0xB8), completed=*(int*)(play+0x28);
    if (play!=current_play || mission!=key || !nonce ||
        act<observed_act || completed<observed_completed) {
        reset(); current_play=play; mission=key;
        if (++serial==0) ++serial;
        nonce=serial;
        if (session->role()==net::Role::Host) host_run=nonce;
    }
    observed_act=act; observed_completed=completed;
    bool host=session->role()==net::Role::Host;
    if (g_fix_combat && (host || host_run) && *(int*)(play+0x90)==3) poll(H->time_seconds()); // only while it plays
    const bool shared_creatures=enemies_on() && *(int*)(play+0x90)==3; // BUG-9114
    guest_driving=!host && shared_creatures;
    if (shared_creatures) {
        if (host) host_stream(H->time_seconds());
        else guest_drive(H->time_seconds());
    }
    if (!host && H->time_seconds()>=hello_at) {
        auto p=packet(Hello,nonce); p.sequence=applied;
        send(0,p); hello_at=H->time_seconds()+1;
    }
    if (host) {
        auto pending=std::move(local); local.clear();
        for (const auto& goal:pending)
            if (!commit(goal) && local.size()<32) local.push_back(goal); // BUG-9089: kept, not lost
    } else if (host_run) {
        for (const auto& goal:local) {
            auto p=packet(Propose,nonce); p.goal=goal; send(0,p);
        }
        local.clear();
        std::sort(incoming.begin(),incoming.end(),[](const Packet& a,const Packet& b){return a.sequence<b.sequence;});
        while (!incoming.empty()) {
            auto p=incoming.front();
            if (p.sequence<=applied) { incoming.erase(incoming.begin()); continue; }
            if (p.sequence!=applied+1) break;
            int result=apply(p.goal);
            if (result!=1) break;
            applied=p.sequence; incoming.erase(incoming.begin());
        }
        // the host's end, once the credits before it are in
        if (host_outcome && o_SetState && live(play) && *(int*)(play+0x90)==3 && incoming.empty()) {
            host_ending=true;
            o_SetState(play,host_outcome);
            host_ending=false;
            H->log(M,SK_INFO,"adventure: the host's mission ended (state %d): this game too",host_outcome);
        }
    }
}
}

void init(const SkHost* host, SkMod* mod, net::Session* s)
{
    H=host; M=mod; session=s;
    auto address=H->sym("SporeCoop::ScenarioCreditGoal");
    if (!address || H->hook_create(M,(void*)address,(void*)credit,(void**)&original)!=0)
        H->log(M,SK_ERROR,"adventure: shared objective hook unavailable");
    auto set=H->sym("Simulator::cScenarioPlayMode::SetState");
    if (!set || H->hook_create(M,(void*)set,(void*)set_state,(void**)&o_SetState)!=0)
        H->log(M,SK_ERROR,"adventure: mission end hook unavailable");
    game::set_ai_skip(ai_skip); // BUG-9114
}

void reset()
{
    current_play=nullptr; mission={}; nonce=host_run=applied=0; visitors={};
    local.clear(); incoming.clear(); journal.clear(); hello_at=0;
    observed_act=observed_completed=-1;
    known.clear(); spatial_ids.clear(); killed_here.clear(); deaths_local.clear(); deaths_wanted.clear(); deaths_told.clear();
    applied_objects.clear(); host_outcome=0; host_ending=false; poll_at=0;
    host_actors.clear(); guest_actors.clear(); guest_key_of.clear(); guest_driving=false; stream_at=full_at=0;
    unpaired_now=0; extra_since.clear(); extras_now=0; batch_first=batch_last=0;
}

void set_body_lookup(void* (*fn)(uint8_t player)) { body_of=fn; }

bool partners_targetable()
{
    uint8_t* play;
    return session && session->role()==net::Role::Host && enemies_on() && live(play) && *(int*)(play+0x90)==3;
}

// guest (BUG-9114): a hit on a scenario creature the host simulates. The captain's own hits go to the
// host (its health comes back with the states); others (this game's leftovers) are dropped.
bool guest_damage(void* c, float damage, int type, void* attacker)
{
    void* av=game::avatar();
    if (!guest_driving || !scenario_creature(c,av)) return false;
    auto it=guest_key_of.find(c);
    if (attacker && attacker==av && it!=guest_key_of.end() && damage>0 && damage<1e6f) {
        auto p=packet(Hit,nonce); p.object=object_id(c); p.key=it->second; p.damage=damage;
        p.damage_type=uint32_t(type)<256 ? uint32_t(type) : 0;
        send(0,p);
        ++hits_out;
    } else
        ++damage_dropped;
    return true;
}

void actors_message(uint8_t from, net::Reader& r)
{
    ActorBatch b;
    if (!original || !session || session->role()==net::Role::Host || from!=0 || !b.read(r) ||
        b.mission!=mission || !host_run || b.run!=host_run || !g_fix_enemies) return;
    const double now=H->time_seconds();
    ++batches_in;
    if (!batch_last || now-batch_last>3.0) batch_first=now; // (again) after a gap: the grace starts over
    batch_last=now;
    for (const auto& a:b.actors) {
        auto it=guest_actors.find(a.key);
        if (it==guest_actors.end()) {
            if (guest_actors.size()>=1024) continue;
            it=guest_actors.emplace(a.key,GuestActor{}).first;
        }
        auto& g=it->second;
        if (g.id && g.id!=a.id) { if (g.obj) guest_key_of.erase(g.obj); g=GuestActor{}; } // a reused host object
        g.id=a.id; g.rx=now; g.dead=(a.flags&kActorDead)!=0;
        net::PlayerState s;
        s.time_ms=b.time_ms;
        s.pos={a.pos[0],a.pos[1],a.pos[2]};
        s.rot={a.rot[0],a.rot[1],a.rot[2],a.rot[3]};
        s.vel={a.vel[0],a.vel[1],a.vel[2]};
        s.health=a.health; s.max_health=a.max_health;
        g.buf.push(s,now);
    }
}

std::string actors(float radius, int around_player)
{
    struct Q { std::string out; net::Vec3 me; float r; void* av; unsigned n; } q{{},{},radius,game::avatar(),0};
    if (q.av) q.me=game::position(q.av);
    if (around_player>=0 && body_of) { // around a partner's body instead
        void* b=body_of(uint8_t(around_player));
        if (!b) return "no body of that player here\n";
        q.me=game::position(b);
        char t[80];
        snprintf(t,sizeof t,"body of %d at=%p avatar at=%p\n",around_player,b,q.av);
        q.out=t;
    }
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto q=(Q*)u;
        if (noun!=kCreatureNoun || !scenario_creature(o,q->av) || !game::object_alive(o)) return true;
        net::Vec3 p=game::position(o);
        if (net::dist(p,q->me)>q->r || q->n>=200) return true;
        // key: the host's object (the host: its own serial; a guest: the host object it follows, 0 none)
        uint32_t key=0;
        if (session && session->role()==net::Role::Host) key=game::object_serial(o);
        else if (auto it=guest_key_of.find(o); it!=guest_key_of.end()) key=it->second;
        char b[200];
        snprintf(b,sizeof b,"actor %08X class=%d pos=(%.2f %.2f %.2f) hp=%.1f dead=%d key=%08X posse=%d d=%.0f at=%p\n",object_id(o),
                 *(int*)((uint8_t*)o+0x1C),p.x,p.y,p.z,game::health(o),(int)game::is_dead(o),key,
                 (int)game::pack_member(o),net::dist(p,q->me),o);
        q->out+=b; ++q->n;
        return true;
    },&q);
    return q.out;
}

// tests: this game's captain hits the nearest live creature of a scenario class (a player's hit: through
// the damage filter, a guest's goes to the host)
std::string debug_hit(uint32_t cls, float damage)
{
    struct Q { uint32_t cls; void* best; float d; net::Vec3 me; void* av; } q{cls,nullptr,1e9f,{},game::avatar()};
    if (!q.av) return "no captain";
    q.me=game::position(q.av);
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto q=(Q*)u;
        if (noun!=kCreatureNoun || !scenario_creature(o,q->av) || *(uint32_t*)((uint8_t*)o+0x1C)!=q->cls ||
            !game::object_alive(o) || game::is_dead(o) || !(game::health(o)>0)) return true;
        if (guest_driving && !guest_key_of.count(o)) return true; // a guest: one the host drives
        float d=net::dist(game::position(o),q->me);
        if (d<q->d) { q->d=d; q->best=o; }
        return true;
    },&q);
    if (!q.best) return "no live creature of that class";
    float before=game::health(q.best);
    game::hit(q.best,damage,0,q.av);
    uint32_t key=0;
    if (auto it=guest_key_of.find(q.best); it!=guest_key_of.end()) key=it->second;
    char b[160]; snprintf(b,sizeof b,"hit %08X key=%08X %.0f m away: hp %.1f -> %.1f here",object_id(q.best),key,q.d,before,
                          game::health(q.best));
    return b;
}

net::AdventureGate outgoing() { return {}; } // Legacy tail remains wire-compatible.

void tick()
{
    if (!original || scheduled) return;
    scheduled=true;
    H->run_outside_frame(work,nullptr);
}

// tests: destroy the nearest object of a scenario class in this game only (a player's kill)
std::string debug_kill(uint32_t cls)
{
    struct Q { uint32_t cls; void* best; uint32_t noun; float d; net::Vec3 me; } q{cls,nullptr,0,1e9f,{}};
    if (void* av=game::avatar()) q.me=game::position(av);
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        auto q=(Q*)u;
        if ((noun!=kBuildingNoun && noun!=kCreatureNoun) || !skg::readable(o,0x20) || *(uint32_t*)((uint8_t*)o+0x1C)!=q->cls ||
            !game::object_alive(o) || !(object_hp(o,noun)>0)) return true;
        net::Vec3 p;
        if (!game::object_position(o,p)) return true;
        float d=net::dist(p,q->me);
        if (d<q->d) { q->d=d; q->best=o; q->noun=noun; }
        return true;
    },&q);
    if (!q.best) return "no live object of that class";
    kill(q.best,q.noun);
    char b[96]; snprintf(b,sizeof b,"destroyed %p (id %08X) %.0f m away",q.best,object_id(q.best),q.d);
    return b;
}

std::string status()
{
    char b[768];
    unsigned paired=0;
    for (auto& [k,g]:guest_actors) paired+=g.obj!=nullptr;
    snprintf(b,sizeof b,"adventure: run=%08X journal=%u applied=%u held=%u objects=%u deaths_out=%u deaths_in=%u "
             "mirrored_kills=%u failures_held=%u outcomes_out=%u host_outcome=%d\n"
             "creatures: shared=%d host_actors=%u batches_out=%u actors_out=%u hits_in=%u hits_applied=%u hits_stale=%u "
             "guest_driving=%d guest_actors=%u paired=%u unpaired=%u batches_in=%u hits_out=%u damage_dropped=%u "
             "extras=%u extras_removed=%u",
             host_run,(unsigned)journal.size(),applied,credits_held,(unsigned)known.size(),deaths_out,deaths_in,
             mirrored_kills,blocked_failures,outcomes_out,host_outcome,
             (int)enemies_on(),(unsigned)host_actors.size(),batches_out,actors_out,hits_in,hits_applied,hits_stale,
             (int)guest_driving,(unsigned)guest_actors.size(),paired,unpaired_now,batches_in,hits_out,damage_dropped,
             extras_now,extras_removed);
    return b;
}

void message(uint8_t from, net::Reader& r)
{
    Packet p;
    if (!original || !session->connected() || !p.read(r) || p.mission!=mission || !nonce ||
        from>=net::kMaxPlayers || from==session->my_id()) return;
    bool host=session->role()==net::Role::Host;
    if (host && p.kind==Hello) {
        const bool fresh=visitors[from]!=p.visitor; // a guest new to this run (Hello repeats every second)
        visitors[from]=p.visitor;
        auto reply=packet(Welcome,p.visitor); send(from,reply);
        uint32_t acknowledged=p.run==host_run ? p.sequence : 0;
        for (auto credit:journal) if (credit.sequence>acknowledged) {
            credit.visitor=p.visitor; send(from,credit);
        }
        if (fresh) for (auto& [id,n]:deaths_local) { // a guest that comes (back) gets every death so far
            auto d=packet(Death,p.visitor); d.object=id; d.deaths=std::min<uint32_t>(n,64); send(from,d);
        }
    } else if (host && p.kind==Propose && p.run==host_run && visitors[from]==p.visitor) {
        if (local.size()<32 && std::none_of(local.begin(),local.end(),[&](const Goal& g){return g.same_credit(p.goal);}))
            local.push_back(p.goal);
    } else if (host && p.kind==Death && p.run==host_run && visitors[from]==p.visitor && g_fix_combat) {
        auto& w=deaths_wanted[p.object]; w=std::max(w,p.deaths); ++deaths_in;
    } else if (host && p.kind==Hit && p.run==host_run && visitors[from]==p.visitor && g_fix_enemies) {
        // BUG-9114: a guest's captain hit this creature in the guest's game: it is hit here, by its body
        ++hits_in;
        auto it=host_actors.find(p.key);
        void* o=it!=host_actors.end() && it->second.id==p.object ? it->second.obj : nullptr;
        if (!o || !game::creature_listed(o) || game::object_serial(o)!=p.key || !game::object_alive(o) || game::is_dead(o)) {
            ++hits_stale;
            return;
        }
        game::apply_damage(o,p.damage,int(p.damage_type),body_of ? body_of(from) : nullptr);
        ++hits_applied;
    } else if (!host && !from && p.visitor==nonce) {
        if (p.kind==Welcome) {
            if (host_run && host_run!=p.run) { applied=0; incoming.clear(); }
            host_run=p.run;
        } else if (p.kind==Commit && p.run==host_run && p.sequence>applied && incoming.size()<kMaxCredits &&
            std::none_of(incoming.begin(),incoming.end(),[&](const Packet& q){return q.sequence==p.sequence;})) {
            incoming.push_back(p);
        } else if (p.kind==Death && p.run==host_run && g_fix_combat) {
            auto& w=deaths_wanted[p.object]; w=std::max(w,p.deaths); ++deaths_in;
        } else if (p.kind==Outcome && p.run==host_run && g_fix_combat) {
            host_outcome=int(p.state);
        }
    }
}
}
