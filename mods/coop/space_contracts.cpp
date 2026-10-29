// Co-op contracts (docs/09-coop-design.md, Space 0.7 feature 7; the game side in docs/re/space.md 10):
// jobs made for two or more players, with a reward for each of them. The host offers a contract every
// few minutes (and on demand: `coop.space contracts offer <armada|survey>`); every player sees it on the
// F9 Space page and as a notice, accepts it there, and the host runs it and pays when it is done.
//  * armada: a pirate armada heading for a colony of the host. Its waves are the game's own pirate
//    raids (raid starter 0x0100E620), started only while the host and another member of the crew are on
//    that planet together, with tougher ships; the raid ships are the host's NPC ships, mirrored to the
//    partners by squadron.cpp, so everybody fights the same ships. Destroy every wave in time; if the
//    pirates win a wave (the raid plunders the colony), the contract fails.
//  * survey: visit N stars near the host's star; the whole crew has to be at each of them together
//    for a few seconds.
// Failure: the time is up, nobody took the offer, or the crew got smaller than needed (a partner left
// the session). A contract never blocks anything: it only adds ships, rows and notices.
// Rewards: money and a tool. Money is one shared budget by default (shared.cpp: the host adds it
// once); in rivals mode every member of the crew gets it. The tool goes to every member of the crew.
//
// MSG_SPACE kinds (space_ext.h): 20 state (host -> all, on a change and every 5 s, so a rejoining
// player sees the current contract), 21 accept (a player -> host), 22 result (host -> all, once).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>

#include "game.h"
#include "net/utf8.h"
#include "space_ext.h"
#include "space_logic.h"
#include "sporekit_game.hpp"
#include "fixes.h"

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;
using spaceext::tr;

namespace {

enum Kind : uint8_t { K_STATE = 20, K_ACCEPT = 21, K_RESULT = 22 };
enum Type : uint8_t { T_NONE = 0, T_ARMADA = 1, T_SURVEY = 2 };
enum State : uint8_t { S_NONE = 0, S_OFFERED = 1, S_ACTIVE = 2, S_DONE = 3, S_FAILED = 4 };
enum Reason : uint8_t { R_NONE = 0, R_TIMEOUT = 1, R_EXPIRED = 2, R_LEFT = 3, R_CANCELLED = 4, R_PLUNDERED = 5 };

const SkHost* H;
SkMod* M;
uintptr_t a_SetState, a_PlayerUFOGet, a_GetAllUFOs, a_StartRaid, a_StarMgr, a_PlayerEmpire, a_AddMoney,
    a_SpaceGame, a_AddItemByKey, a_Context;

template <typename T>
T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

// cGameDataUFO (docs/re/space.md 0)
constexpr uintptr_t kMaxHp = 0x534, kState = 0x53C, kHp = 0x540, kMission = 0x800;
constexpr uint32_t kRaidMission = 0xA4B0A2DF; // cMission +0x78 of a pirate raid (docs/re/space.md 6, 7.4)
constexpr float kTough = 2.0f;        // armada ships: HP x2 (the colony's turrets must not win alone)
constexpr double kHold = 5.0;         // survey: the crew together at a star this long
constexpr double kOfferTime = 180.0;  // an offer waits this long for its crew
constexpr double kShowEnd = 60.0;     // the result stays on the page this long
constexpr uint8_t kNeeded = 2;

struct Target {
    uint32_t star = 0;
    uint8_t done = 0;
    uint32_t planet = 0; // armada: the PlanetID of the colony
};

struct Contract {
    uint32_t id = 0;
    uint8_t type = T_NONE, state = S_NONE, reason = R_NONE, needed = kNeeded;
    uint32_t money = 0, tool = 0;
    double deadline = 0; // local clock; offered: the offer's end, active: the time limit, done: page end
    std::vector<uint8_t> crew;
    std::vector<Target> targets; // armada: one star
    uint8_t wave = 0, waves = 0, left = 0, up = 0; // armada: current wave, its ships alive now (left = up)
    bool has(uint8_t id) const { return std::find(crew.begin(), crew.end(), id) != crew.end(); }
};

Contract g;                       // the current contract (the host's, a copy on guests)
// host, armada: the wave running now (a raid of the game) and the ships of it seen so far
struct Seen {
    float hp = 0, max = 1;
    int state = 0;
};
std::map<void*, Seen> g_seen;     // alive armada ships of the wave (last HP / state seen)
std::vector<void*> g_alive;       // the same pointers, in the order coop.space contracts ships prints
bool g_wave_on, g_wave_ships;     // a raid was started / its ships appeared
double g_wave_t, g_last_alive;    // when the raid was started / the last time a ship of it was alive
uint32_t g_wave_place;
int g_wave_result;                // the raid's end seen by spacecoop's SetState hook: 5 defended, 6 lost
// BUG-0046: the wave's own raid mission. A host who left the planet and came back started a second
// raid while the first still ran; the ships of both were taken for the wave (HP raised for each),
// and the end of either decided it. Now the wave keeps its mission: ships and the end state count
// only for it, and a return resumes it while it runs. coop.space contracts wavefix 0: the old way.
void* g_wave_mission;             // the cMission of the wave (pointer, checked in the mission list)
std::set<uint64_t> g_toughened;   // ships of the wave mission whose HP was raised (once each): spacelogic::tough_key
bool g_wave_fix = true;
COOP_FIX("contracts.wave", "BUG-0046", g_wave_fix, "space: an armada wave keeps its own mission");
// BUG-0227: the resume of BUG-0046 had three gaps. tough: the "raised once" set held pointers, so
// a new ship at a freed one's address kept its normal HP (now keyed by object serial). resume: a
// resumed wave kept "its ships appeared" and was won without a fight 30 s later (now it waits for
// ships again and, when none come, ends its old raid before a new one). away: a raid result while
// the host was away was dropped (now a loss fails the contract, a win counts).
bool g_wave_tough_fix = true, g_wave_resume_fix = true, g_wave_away_fix = true;
COOP_FIX("contracts.wave_tough", "BUG-0227", g_wave_tough_fix, "space: armada ships get their HP raised once per object, not per address");
COOP_FIX("contracts.wave_resume", "BUG-0227", g_wave_resume_fix, "space: a resumed armada wave waits for its ships again");
COOP_FIX("contracts.wave_away", "BUG-0227", g_wave_away_fix, "space: an armada raid that ends while the host is away counts");
bool g_wave_resumed;              // the running wave was resumed (BUG-0227)
int g_wave_away_result;           // the wave's raid ended while the host was away: 5 or 6 (BUG-0227)
bool g_tough_reuse, g_hide_ships; // tests: the address-reuse simulation / the resumed raid brings no ships
uint32_t n_aborted, n_away_results;
uint32_t n_resumed;               // returns that resumed the running raid
uint32_t g_stable_place;          // host: the place the crew check saw last, and since when
double g_stable_since, g_next_wave, g_last_tick, g_last_publish, g_next_offer;
std::vector<double> g_hold;       // host, survey: seconds the crew is together at each target
double g_period = 600;            // auto-offer period (s), 0 = off (coop.space contracts period)
double g_first_delay = 240;       // the first offer after the players meet in space
double g_limit_override;          // tests: the time limit of the next contracts
uint32_t g_rewarded;              // the last contract id this machine was paid for
uint32_t n_offered, n_done, n_failed, n_rewards, n_tools, n_raids, n_ships, n_gone;
int g_money_got;
std::mt19937 g_rng{0x5EED};

bool connected() { return spaceext::connected(); }
bool host() { return spaceext::is_host(); }
const spacecoop::Ctx& C() { return spaceext::ctx(); }
double now() { return H->time_seconds(); }

uint32_t star_of(uint32_t place)
{
    if (place & 0x80000000u)
        return place & 0x7FFFFFFFu;
    if (place & 0x40000000u)
        return place & 0xFFFFFFu; // PlanetID: the star in the low 24 bits
    return 0;
}

uint32_t my_place() { return C().my_place ? C().my_place() : 0; }

void* my_ufo()
{
    void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    return mgr ? F<void*>(mgr, 0x40) : nullptr;
}

// (ids come from the network: the safe lookup of spacecoop.cpp, GetStarRecord faults on a wrong id)
void* star_record(uint32_t id)
{
    void* r = spacecoop::star_record(id);
    return r && !IsBadReadPtr(r, 0xB0) ? r : nullptr;
}

// cStarRecord +0x60 mName (eastl::string16: begin, end), docs/re/space.md 10.1
std::string utf8(const uint16_t* b, const uint16_t* e);
std::string star_name(uint32_t id)
{
    std::string s;
    if (void* r = star_record(id))
        s = utf8(F<const uint16_t*>(r, 0x60), F<const uint16_t*>(r, 0x64));
    if (s.empty()) {
        char b[24];
        snprintf(b, sizeof b, "%s %06X", tr("star", "звезда"), id);
        s = b;
    }
    return s;
}

std::string utf8(const uint16_t* b, const uint16_t* e)
{
    std::string s;
    if (!b || e <= b || e - b > 64 || IsBadReadPtr(b, (e - b) * 2))
        return s;
    for (const uint16_t* p = b; p < e; p++) {
        uint32_t c = *p;
        if (c < 0x80)
            s += (char)c;
        else if (c < 0x800) {
            s += (char)(0xC0 | c >> 6);
            s += (char)(0x80 | (c & 0x3F));
        } else {
            s += (char)(0xE0 | c >> 12);
            s += (char)(0x80 | (c >> 6 & 0x3F));
            s += (char)(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// cStarRecord +0x84: vector<cPlanetRecord*>; cPlanetRecord +0x184 PlanetID, +0x194 tech level (5 = a
// colony / the homeworld), +0x18 mName (eastl::string16) (docs/re/space.md 10.1)
void* planet_record(uint32_t id)
{
    void* sr = star_record(id & 0xFFFFFF);
    if (!sr)
        return nullptr;
    void** b = F<void**>(sr, 0x84);
    void** e = F<void**>(sr, 0x88);
    if (!b || e < b || e - b > 64 || IsBadReadPtr(b, (e - b) * 4))
        return nullptr;
    for (void** it = b; it < e; ++it)
        if (*it && !IsBadReadPtr(*it, 0x198) && F<uint32_t>(*it, 0x184) == id)
            return *it;
    return nullptr;
}

std::string planet_name(uint32_t id)
{
    void* r = planet_record(id);
    std::string s = r ? utf8(F<const uint16_t*>(r, 0x18), F<const uint16_t*>(r, 0x1C)) : "";
    return s.empty() ? star_name(id & 0xFFFFFF) : s;
}

std::vector<void*> all_ufos()
{
    std::vector<void*> r;
    void* nm = game::noun_manager();
    void* v = nm && a_GetAllUFOs ? thiscall<void*>(a_GetAllUFOs, nm) : nullptr;
    if (!v)
        return r;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    for (void** it = b; it && it < e && r.size() < 512; ++it)
        if (*it)
            r.push_back(*it);
    return r;
}

// ---- rewards: money (cEmpire +0xD0) and tools (docs/re/space.md 7.7) ----
void* empire() { return a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr; }
int money()
{
    void* e = empire();
    return e ? F<int>(e, 0xD0) : -1;
}
void add_money(int n)
{
    void* e = empire();
    if (e && a_AddMoney && n)
        thiscall<void>(a_AddMoney, e, n);
}

void* inventory()
{
    void* sg = a_SpaceGame ? *(void**)a_SpaceGame : nullptr;
    return sg ? F<void*>(sg, 0x40) : nullptr;
}

// a tool key {FNV-1 of the tool file name, type, group}: type and group copied from a tool in the
// inventory, as `coop.space tool add` does (docs/re/space.md 9.3)
bool tool_key(uint32_t hash, uint32_t key[3])
{
    void* inv = inventory();
    if (!inv)
        return false;
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        uint8_t* it = vcall<uint8_t*>(inv, 0x60 / 4, i);
        if (!it || F<int>(it, 0x14) != 2)
            continue;
        const uint32_t* k = vcall<const uint32_t*>(it, 0x18 / 4);
        if (!k)
            continue;
        memcpy(key, k, 12);
        key[0] = hash;
        return true;
    }
    return false;
}

bool has_tool(uint32_t hash)
{
    uint32_t k[3];
    void* inv = inventory();
    return inv && tool_key(hash, k) && vcall<bool>(inv, 0x74 / 4, (const uint32_t*)k);
}

bool give_tool(uint32_t hash)
{
    uint32_t k[3];
    void* inv = inventory();
    if (!inv || !a_AddItemByKey || !tool_key(hash, k) || vcall<bool>(inv, 0x74 / 4, (const uint32_t*)k))
        return false;
    thiscall<void>(a_AddItemByKey, inv, (const uint32_t*)k);
    return vcall<bool>(inv, 0x74 / 4, (const uint32_t*)k);
}

struct ToolInfo {
    uint32_t hash;
    const char* en;
    const char* ru;
};
// tool file names in spacetools~ (0x30608F0B), hashes checked against the game's packages
const ToolInfo kArmadaTools[] = {{0x6DB0189A, "Laser 2", "Лазер 2"},
                                 {0x7B76E38C, "Pulse 2", "Импульс 2"},
                                 {0xFC22027B, "Proton missile 2", "Протонная ракета 2"},
                                 {0x6DB0189B, "Laser 3", "Лазер 3"},
                                 {0x7B76E38D, "Pulse 3", "Импульс 3"},
                                 {0xFC22027A, "Proton missile 3", "Протонная ракета 3"}};
const ToolInfo kSurveyTools[] = {{0x4542380A, "Shield", "Щит"},
                                 {0x44808DE7, "Cloak", "Маскировка"},
                                 {0x931426A0, "Atmosphere generator", "Генератор атмосферы"}};

std::string tool_name(uint32_t hash)
{
    for (auto& t : kArmadaTools)
        if (t.hash == hash)
            return tr(t.en, t.ru);
    for (auto& t : kSurveyTools)
        if (t.hash == hash)
            return tr(t.en, t.ru);
    char b[16];
    snprintf(b, sizeof b, "%08X", hash);
    return b;
}

template <size_t N>
uint32_t first_missing(const ToolInfo (&list)[N])
{
    for (auto& t : list)
        if (!has_tool(t.hash))
            return t.hash;
    return 0;
}

// ---- texts ----
std::vector<std::string> g_said; // the last notices of this feature (tests: coop.space contracts notices)
void say(const std::string& t)
{
    g_said.push_back(t);
    if (g_said.size() > 12)
        g_said.erase(g_said.begin());
    spaceext::notice(t);
}

std::string mmss(double secs)
{
    int s = std::max(0, (int)secs);
    char b[16];
    snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

std::string names(const std::vector<uint8_t>& ids)
{
    std::string s;
    for (uint8_t id : ids)
        s += (s.empty() ? "" : ", ") + spaceext::name_of(id);
    return s.empty() ? "-" : s;
}

std::string title(const Contract& c)
{
    if (c.type == T_ARMADA)
        return c.targets.empty() ? std::string("?")
                                 : tr("pirate armada heading for ", "пиратская армада летит к ") +
                                       planet_name(c.targets[0].planet) + " (" + star_name(c.targets[0].star) + ")";
    char b[96];
    snprintf(b, sizeof b, tr("joint survey of %d stars", "совместная разведка %d звёзд"), (int)c.targets.size());
    return b;
}

std::string reward_text(const Contract& c)
{
    std::string s = "$" + std::to_string(c.money);
    if (c.tool)
        s += " + " + tool_name(c.tool);
    return s;
}

std::string reason_text(uint8_t r)
{
    switch (r) {
    case R_TIMEOUT: return tr("the time is up", "время вышло");
    case R_EXPIRED: return tr("nobody took it", "никто не взялся");
    case R_LEFT: return tr("a partner left", "напарник ушёл");
    case R_CANCELLED: return tr("cancelled", "отменён");
    case R_PLUNDERED: return tr("the pirates won a wave", "пираты выиграли волну");
    }
    return "?";
}

const char* type_name(uint8_t t) { return t == T_ARMADA ? "armada" : t == T_SURVEY ? "survey" : "none"; }
const char* state_name(uint8_t s)
{
    static const char* n[] = {"none", "offered", "active", "done", "failed"};
    return s <= S_FAILED ? n[s] : "?";
}

// ---- the crew's whereabouts ----
// the StarID of a player (0: the galaxy view, not in space or unknown)
uint32_t star_of_player(uint8_t id)
{
    if (id == spaceext::my_id())
        return spaceext::in_space() ? star_of(my_place()) : 0;
    if (C().partners)
        for (auto& p : C().partners())
            if (p.id == id)
                return star_of(p.place);
    return 0;
}

uint32_t place_of_player(uint8_t id)
{
    if (id == spaceext::my_id())
        return spaceext::in_space() ? my_place() : 0;
    if (C().partners)
        for (auto& p : C().partners())
            if (p.id == id)
                return p.place;
    return 0;
}

// ---- state messages ----
void write_state(net::Writer& w, const Contract& c, double t)
{
    w.u32(c.id);
    w.u8(c.type);
    w.u8(c.state);
    w.u8(c.reason);
    w.u8(c.needed);
    w.u32(c.money);
    w.u32(c.tool);
    w.u32((uint32_t)std::max(0, (int)std::ceil(c.deadline - t)));
    w.u8((uint8_t)c.crew.size());
    for (uint8_t id : c.crew)
        w.u8(id);
    w.u8((uint8_t)c.targets.size());
    for (auto& tg : c.targets) {
        w.u32(tg.star);
        w.u8(tg.done);
        w.u32(tg.planet);
    }
    w.u8(c.wave);
    w.u8(c.waves);
    w.u8(c.left);
    w.u8(c.up);
}

bool read_state(net::Reader& r, Contract& c, double t)
{
    c = Contract{};
    c.id = r.u32();
    c.type = r.u8();
    c.state = r.u8();
    c.reason = r.u8();
    c.needed = r.u8();
    c.money = r.u32();
    c.tool = r.u32();
    c.deadline = t + r.u32();
    uint8_t n = r.u8();
    for (int i = 0; i < n && r.ok(); i++)
        c.crew.push_back(r.u8());
    n = r.u8();
    if (n > 16)
        return false;
    for (int i = 0; i < n && r.ok(); i++) {
        Target tg;
        tg.star = r.u32();
        tg.done = r.u8();
        tg.planet = r.u32();
        c.targets.push_back(tg);
    }
    c.wave = r.u8();
    c.waves = r.u8();
    c.left = r.u8();
    c.up = r.u8();
    // an armada needs its target planet (title, rows, the raid all read targets[0])
    if (c.type > T_SURVEY || (c.type == T_ARMADA && c.targets.empty()))
        return false;
    return r.ok();
}

// the notices of a change (every machine words its own, from the old and the new state)
void announce(const Contract& o, const Contract& c)
{
    if (c.id != o.id && c.state == S_OFFERED) {
        say(tr("New co-op contract: ", "Новый совместный контракт: ") + title(c) + ", " +
                         reward_text(c) + tr(". Accept it on the F9 Space page", ". Принять: F9, страница Космос"));
        return;
    }
    if (c.id != o.id || c.state >= S_DONE)
        return; // the result has its own message
    for (uint8_t id : c.crew)
        if (!o.has(id))
            say(spaceext::name_of(id) + tr(" accepted the contract", " принял(а) контракт"));
    if (c.state == S_ACTIVE && o.state == S_OFFERED)
        say(tr("Contract started: ", "Контракт начался: ") + title(c) + ", " + mmss(c.deadline - now()) +
                         tr(" to do it", " на выполнение"));
    if (c.type == T_ARMADA && c.up && !o.up) {
        char b[160];
        snprintf(b, sizeof b, tr("The armada attacks: wave %d/%d, %d ships", "Армада атакует: волна %d/%d, кораблей: %d"),
                 c.wave, c.waves, c.up);
        say(b);
    }
    if (c.type == T_SURVEY)
        for (size_t i = 0; i < c.targets.size() && i < o.targets.size(); i++)
            if (c.targets[i].done && !o.targets[i].done) {
                int d = 0;
                for (auto& t : c.targets)
                    d += t.done;
                char b[160];
                snprintf(b, sizeof b, tr("%s surveyed together (%d/%d)", "%s разведана вместе (%d/%d)"),
                         star_name(c.targets[i].star).c_str(), d, (int)c.targets.size());
                say(b);
            }
}

// host: the new state goes to everybody (and to this machine's notices)
Contract g_sent;
void publish()
{
    double t = now();
    announce(g_sent, g);
    g_sent = g;
    g_last_publish = t;
    net::Writer w;
    write_state(w, g, t);
    spaceext::send(K_STATE, w);
}

// every machine: the reward of a finished contract, once
void on_result(uint32_t id, bool ok, uint8_t reason, uint32_t money_, uint32_t tool, const std::vector<uint8_t>& crew,
               uint8_t type)
{
    bool mine = std::find(crew.begin(), crew.end(), spaceext::my_id()) != crew.end();
    if (!ok) {
        say(tr("Contract failed: ", "Контракт провален: ") + reason_text(reason));
        return;
    }
    (void)type;
    if (g_rewarded == id)
        return;
    g_rewarded = id;
    // money: one shared budget (the host adds it once, shared.cpp gives it to everybody), in rivals
    // mode every member of the crew gets it
    bool pay = spaceext::rivals() ? mine : host();
    if (pay) {
        add_money((int)money_);
        g_money_got += (int)money_;
    }
    bool got_tool = mine && tool && give_tool(tool);
    if (got_tool)
        n_tools++;
    n_rewards++;
    std::string s = tr("Contract done! Reward: $", "Контракт выполнен! Награда: $") + std::to_string(money_);
    if (!spaceext::rivals())
        s += tr(" to the shared budget", " в общий бюджет");
    if (got_tool)
        s += " + " + tool_name(tool);
    else if (mine && tool)
        s += tr(" (the tool is already in your cargo)", " (инструмент у вас уже есть)");
    if (!mine)
        s = names(crew) + tr(" completed a contract: $", " выполнили контракт: $") + std::to_string(money_);
    say(s);
}

void finish(bool ok, uint8_t reason)
{
    g.state = ok ? S_DONE : S_FAILED;
    g.reason = reason;
    g.up = 0;
    g.deadline = now() + kShowEnd;
    g_wave_on = false; // (a raid still running goes on as an ordinary raid of the game)
    g_seen.clear();
    g_wave_mission = nullptr;
    g_toughened.clear();
    g_wave_resumed = false;
    g_wave_away_result = 0;
    g_alive.clear();
    ok ? n_done++ : n_failed++;
    g_next_offer = now() + g_period;
    publish();
    net::Writer w;
    w.u32(g.id);
    w.u8(ok);
    w.u8(reason);
    w.u32(g.money);
    w.u32(g.tool);
    w.u8(g.type);
    w.u8((uint8_t)g.crew.size());
    for (uint8_t id : g.crew)
        w.u8(id);
    spaceext::send(K_RESULT, w);
    on_result(g.id, ok, reason, g.money, g.tool, g.crew, g.type);
}

// ---- host: offers ----
std::vector<uint32_t> near_stars(uint32_t from, size_t n)
{
    std::vector<std::pair<float, uint32_t>> v;
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    void* fr = star_record(from);
    if (!sm || !fr || IsBadReadPtr(sm, 0xD0))
        return {};
    auto& q0 = F<float[3]>(fr, 0x3C);
    // cStarManager +0xC8: 20-byte sectors, first dword = cStarRecord* array (docs/re/space.md 9.1)
    uint8_t* sb = F<uint8_t*>(sm, 0xC8);
    uint8_t* se = F<uint8_t*>(sm, 0xCC);
    if (!sb || se <= sb || (se - sb) / 20 > 4096 || IsBadReadPtr(sb, se - sb))
        return {};
    for (uint8_t* sec = sb; sec + 20 <= se; sec += 20) {
        void** b = F<void**>(sec, 0);
        void** e = F<void**>(sec, 4);
        if (!b || e <= b || e - b > 4096 || IsBadReadPtr(b, (e - b) * 4))
            continue;
        for (void** it = b; it < e; ++it) {
            void* r = *it;
            if (!r || IsBadReadPtr(r, 0xB0) || r == fr)
                continue;
            int type = F<int>(r, 0x48); // StarType: 4.. are real stars (1 core, 2 black hole, 3 disk)
            if (type < 4 || type > 16 || F<uint8_t>(r, 0xAC) == 0) // + at least one planet
                continue;
            auto& q = F<float[3]>(r, 0x3C);
            float dx = q[0] - q0[0], dy = q[1] - q0[1], dz = q[2] - q0[2];
            v.push_back({dx * dx + dy * dy + dz * dz, F<uint32_t>(r, 0x70)});
        }
    }
    std::sort(v.begin(), v.end());
    if (v.size() > n * 3)
        v.resize(n * 3);
    std::shuffle(v.begin(), v.end(), g_rng);
    std::vector<uint32_t> out;
    for (size_t i = 0; i < v.size() && out.size() < n; i++)
        out.push_back(v[i].second);
    return out;
}

// the armada's target: a colony of the host (a raid needs a planet of the raided empire, docs/re/space.md
// 7.4); "here": the planet the host is on or a colony of the host's star; else one of the host's stars
// (cEmpire +0x88: vector<cStarRecord*>, GetStars 0xC30A10 / 0xC30A30). The guests' colonies (feature 1's
// roster) are not in the host's save: no raid there (docs/known-issues.md).
bool colony(void* rec) { return rec && F<int>(rec, 0x194) == 5; }

uint32_t colony_of_star(uint32_t star)
{
    void* sr = star_record(star);
    if (!sr)
        return 0;
    void** b = F<void**>(sr, 0x84);
    void** e = F<void**>(sr, 0x88);
    if (!b || e < b || e - b > 64 || IsBadReadPtr(b, (e - b) * 4))
        return 0;
    for (void** it = b; it < e; ++it)
        if (*it && !IsBadReadPtr(*it, 0x198) && colony(*it))
            return F<uint32_t>(*it, 0x184);
    return 0;
}

uint32_t armada_planet(const std::string& arg)
{
    if (!arg.empty() && arg != "here") {
        uint32_t id = (uint32_t)strtoul(arg.c_str(), nullptr, 0);
        return colony(planet_record(id)) ? id : 0;
    }
    uint32_t place = my_place();
    if ((place & 0x40000000u) && colony(planet_record(place & 0x3FFFFFFFu)))
        return place & 0x3FFFFFFFu;
    if (uint32_t c = colony_of_star(star_of(place)))
        return c;
    if (arg == "here")
        return 0;
    std::vector<uint32_t> c;
    if (void* e = empire()) {
        void** b = F<void**>(e, 0x88);
        void** en = F<void**>(e, 0x8C);
        if (b && en > b && en - b < 512 && !IsBadReadPtr(b, (en - b) * 4))
            for (void** it = b; it < en; ++it)
                if (*it && !IsBadReadPtr(*it, 0x74))
                    if (uint32_t p = colony_of_star(F<uint32_t>(*it, 0x70)))
                        c.push_back(p);
    }
    return c.empty() ? 0 : c[std::uniform_int_distribution<size_t>(0, c.size() - 1)(g_rng)];
}

std::string offer(uint8_t type, const std::string& arg)
{
    if (!host())
        return "only the host offers contracts";
    if (!spaceext::in_space())
        return "not in space";
    if (g.state == S_OFFERED || g.state == S_ACTIVE)
        return "a contract is running (coop.space contracts cancel)";
    Contract c;
    c.id = (g_rng() & 0x7FFFFFFF) | 1;
    c.type = type;
    c.state = S_OFFERED;
    c.deadline = now() + kOfferTime;
    if (type == T_ARMADA) {
        uint32_t pl = armada_planet(arg);
        if (!pl)
            return "no colony of the host for the armada";
        c.targets.push_back({pl & 0xFFFFFF, 0, pl});
        c.money = 25000;
        c.tool = first_missing(kArmadaTools);
        c.waves = 2;
    } else {
        uint32_t from = star_of(my_place());
        size_t n = arg.empty() ? 3 : (size_t)std::clamp(atoi(arg.c_str()), 1, 6);
        for (uint32_t s : near_stars(from, n))
            c.targets.push_back({s, 0});
        if (c.targets.empty())
            return "no stars near the host (in the galaxy view?)";
        c.money = 15000;
        c.tool = first_missing(kSurveyTools);
    }
    if (!c.tool)
        c.money *= 2; // every tool of the list is in the cargo already
    g_wave_on = false;
    g_seen.clear();
    g_wave_mission = nullptr;
    g_toughened.clear();
    g_wave_resumed = false;
    g_wave_away_result = 0;
    g_alive.clear();
    g = c;
    g_hold.assign(g.targets.size(), 0);
    n_offered++;
    publish();
    return std::string("offered ") + type_name(type) + " id " + std::to_string(g.id);
}

double limit_of(uint8_t type)
{
    if (g_limit_override > 0)
        return g_limit_override;
    return type == T_ARMADA ? 600 : 900;
}

void accept(uint8_t who)
{
    if (!host() || (g.state != S_OFFERED && g.state != S_ACTIVE) || g.has(who))
        return;
    g.crew.push_back(who);
    if (g.state == S_OFFERED && g.crew.size() >= g.needed) {
        g.state = S_ACTIVE;
        g.deadline = now() + limit_of(g.type);
        if (g.type == T_ARMADA)
            g.wave = 1;
    }
    publish();
}

// the button / `coop.space contracts accept`
std::string accept_mine()
{
    if (g.state != S_OFFERED && g.state != S_ACTIVE)
        return "no contract to accept";
    if (g.has(spaceext::my_id()))
        return "already in the crew";
    if (host()) {
        accept(spaceext::my_id());
        return "accepted";
    }
    net::Writer w;
    w.u32(g.id);
    spaceext::send(K_ACCEPT, w);
    return "accept sent";
}

std::string jump_to(uint32_t star)
{
    char b[24];
    snprintf(b, sizeof b, "0x%X", star);
    std::string r = spacecoop::debug({"jumpto", b}); // the native travel of spacecoop.cpp (docs/re/space.md 7.3)
    if (r != "jumping")
        say(tr("Can't jump: ", "Прыжок невозможен: ") + r);
    return r;
}

// ---- host: the armada ----
int crew_with_me()
{
    uint32_t here = my_place();
    int n = 0;
    for (uint8_t id : g.crew)
        if (place_of_player(id) == here)
            n++;
    return n;
}

// a ship of the armada's raid: its owner mission (ufo +0x800, SetOwnerMission 0xC37810) is a pirate raid
// on the contract's planet (cMission +0x78 id, +0x1E8 target cPlanet, its record +0x13C, docs/re/space.md 6)
bool armada_ship(void* u)
{
    if (g_hide_ships && g_wave_resumed)
        return false; // tests: the resumed raid's ships never come
    void* m = F<void*>(u, kMission);
    if (g_wave_fix && g_wave_mission && m != g_wave_mission)
        return false;
    if (!m || IsBadReadPtr(m, 0x1F0) || F<uint32_t>(m, 0x78) != kRaidMission)
        return false;
    void* planet = F<void*>(m, 0x1E8);
    void* rec = planet && !IsBadReadPtr(planet, 0x140) ? F<void*>(planet, 0x13C) : nullptr;
    return rec && !IsBadReadPtr(rec, 0x188) && !g.targets.empty() && F<uint32_t>(rec, 0x184) == g.targets[0].planet;
}

// the game's raid starter (the `raid -start` cheat's routine 0x0100E620, the same address in both
// builds): thiscall, `this` unused, (int unused, cPlanetRecord* colony, int bombers), ret 0xC. It creates
// mission 0xA4B0A2DF (CreateMission 0xFEC420), SetTargetPlanet, SetNumBombers, Accept; the raid's ships
// (bombers type 4, 4 raiders type 2 each) come while the raided empire's player is on the planet.
// raid missions on the contract's planet in state 2 (accepted) or 3 (active) (docs/re/space.md 6)
static std::set<void*> raids_on_target()
{
    std::set<void*> r;
    if (g.targets.empty() || !spaceext::in_space())
        return r;
    for (void* m : spaceext::game_missions()) {
        if (!m || IsBadReadPtr(m, 0x1F0) || F<uint32_t>(m, 0x78) != kRaidMission)
            continue;
        int st = F<int>(m, 0x84);
        void* planet = F<void*>(m, 0x1E8);
        void* rec = planet && !IsBadReadPtr(planet, 0x140) ? F<void*>(planet, 0x13C) : nullptr;
        if ((st == 2 || st == 3) && rec && !IsBadReadPtr(rec, 0x188) && F<uint32_t>(rec, 0x184) == g.targets[0].planet)
            r.insert(m);
    }
    return r;
}

bool start_raid(int bombers)
{
    void* rec = g.targets.empty() ? nullptr : planet_record(g.targets[0].planet);
    if (!rec || !a_StartRaid)
        return false;
    std::set<void*> before = g_wave_fix ? raids_on_target() : std::set<void*>{};
    thiscall<bool>(a_StartRaid, (void*)nullptr, 0, rec, bombers);
    n_raids++;
    g_wave_mission = nullptr;
    g_toughened.clear();
    g_wave_resumed = false;
    g_wave_away_result = 0;
    if (g_wave_fix)
        for (void* m : raids_on_target())
            if (!before.count(m))
                g_wave_mission = m; // the raid StartRaid made (CreateMission + Accept, synchronous)
    return true;
}

void armada_tick(double t)
{
    if (g.targets.empty())
        return;
    uint32_t place = spaceext::in_space() ? my_place() : 0;
    uint32_t target = g.targets[0].planet | 0x40000000u;
    bool changed = false;
    if (g_wave_on) {
        if (place != g_wave_place) {
            // the host left the planet: the game runs the raid on without anybody there (off-screen
            // simulation): this wave comes again when the crew is together on the planet
            g_wave_on = false; // (the wave's mission stays: a return resumes it, BUG-0046)
            g_seen.clear();
            g_alive.clear();
            g.up = 0;
            g_next_wave = t + 5;
            changed = true;
        } else {
            std::vector<void*> alive;
            auto list = all_ufos();
            for (void* u : list) {
                if (!game::object_alive(u) || !armada_ship(u))
                    continue;
                bool live = F<int>(u, kState) != 2 && F<float>(u, kHp) > 0;
                auto it = g_seen.find(u);
                if (it == g_seen.end() && live) { // a new ship of the wave: tougher (once, BUG-0046)
                    if (g_tough_reuse && g_wave_resumed) // tests: as if an earlier ship at this address was raised
                        g_toughened.insert(spacelogic::tough_key((uintptr_t)u, 0, false));
                    uint64_t key = spacelogic::tough_key((uintptr_t)u, game::object_serial(u), g_wave_tough_fix);
                    if (!g_wave_fix || g_toughened.insert(key).second) {
                        F<float>(u, kMaxHp) *= kTough;
                        F<float>(u, kHp) = F<float>(u, kMaxHp);
                    }
                    it = g_seen.emplace(u, Seen{}).first;
                    n_ships++;
                    g_wave_ships = true;
                }
                if (it == g_seen.end())
                    continue;
                it->second = {F<float>(u, kHp), F<float>(u, kMaxHp), F<int>(u, kState)};
                if (live)
                    alive.push_back(u);
            }
            // ships of the wave gone: destroyed or flown off (a ship killed by one big hit leaves the UFO
            // list before a state 2 is ever seen, so the outcome is the raid's own: g_wave_result)
            for (auto it = g_seen.begin(); it != g_seen.end();) {
                if (std::find(alive.begin(), alive.end(), it->first) != alive.end()) {
                    ++it;
                    continue;
                }
                n_gone++;
                it = g_seen.erase(it);
                changed = true;
            }
            if (!alive.empty())
                g_last_alive = t;
            g_alive = alive;
            if (g.up != alive.size() || g.left != alive.size()) {
                g.up = g.left = (uint8_t)std::min<size_t>(alive.size(), 255);
                changed = true;
            }
            if (!g_wave_ships && t - g_wave_t > 30) {
                H->log(M, SK_WARN, "contracts: the raid of wave %d brought no ships in 30 s: again", g.wave);
                g_wave_on = false; // started again below
                if (g_wave_resume_fix && g_wave_resumed) {
                    // BUG-0227: end the old raid first (found in the live list this tick), or the
                    // next ready tick would resume it again, or start a second raid next to it
                    if (g_wave_mission && raids_on_target().count(g_wave_mission) && a_SetState) {
                        thiscall<void>(a_SetState, g_wave_mission, 7); // aborted
                        n_aborted++;
                    }
                    g_wave_mission = nullptr;
                    g_wave_resumed = false;
                }
            } else if (g_wave_result == 5 || g_wave_result == 6 || (g_wave_ships && alive.empty() && t - g_last_alive > 30)) {
                // (no end state 30 s after the last ship: taken as won, the ships are gone)
                bool won = g_wave_result != 6;
                H->log(M, SK_INFO, "contracts: wave %d over: raid state %d", g.wave, g_wave_result);
                g_wave_on = false;
                if (!won) {
                    finish(false, R_PLUNDERED);
                    return;
                }
                if (g.wave >= g.waves) {
                    finish(true, R_NONE);
                    return;
                }
                g.wave++;
                g_next_wave = t + 10;
                changed = true;
            }
        }
    }
    // a wave comes when the host and another member of the crew are on the colony together
    int ctx = a_Context ? cdecl_call<int>(a_Context) : -1;
    void* me = my_ufo();
    bool ready = !g_wave_on && place == target && ctx == 0 && me && F<int>(me, kState) != 2 &&
                 crew_with_me() >= g.needed && t >= g_next_wave;
    if (place != g_stable_place) {
        g_stable_place = place;
        g_stable_since = t;
    }
    // BUG-0227: the wave's raid ended while the host was away
    if (!g_wave_on && g_wave_away_result) {
        int r = g_wave_away_result;
        g_wave_away_result = 0;
        g_wave_mission = nullptr;
        auto act = spacelogic::wave_on_return(r, false);
        H->log(M, SK_INFO, "contracts: wave %d: its raid ended with %d while the host was away", g.wave, r);
        if (act == spacelogic::WaveReturn::Fail) {
            finish(false, R_PLUNDERED);
            return;
        }
        if (act == spacelogic::WaveReturn::Won) {
            if (g.wave >= g.waves) {
                finish(true, R_NONE);
                return;
            }
            g.wave++;
            g_next_wave = t + 10;
            publish();
            return;
        }
    }
    // BUG-0046: back on the planet while the wave's raid still runs: the wave goes on with it
    bool resume = false;
    if (ready && g_wave_fix && g_wave_mission && t - g_stable_since >= 3) {
        if (raids_on_target().count(g_wave_mission)) {
            resume = true;
            n_resumed++;
            H->log(M, SK_INFO, "contracts: wave %d: its raid still runs, resumed", g.wave);
        } else
            g_wave_mission = nullptr;
    }
    if (resume) {
        g_wave_on = true;
        g_wave_t = g_last_alive = t;
        g_wave_place = place;
        g_wave_result = 0;
        g_seen.clear();
        if (g_wave_resume_fix) { // BUG-0227: its ships have to come again
            g_wave_ships = false;
            g_wave_resumed = true;
        }
        changed = true;
    } else if (ready && t - g_stable_since >= 3 && start_raid((int)g.crew.size() + g.wave - 1)) {
        g_wave_on = true;
        g_wave_ships = false;
        g_wave_t = g_last_alive = t;
        g_wave_place = place;
        g_wave_result = 0;
        g_seen.clear();
        changed = true;
    }
    if (changed)
        publish();
}

// ---- host: the survey ----
void survey_tick(double dt)
{
    bool changed = false;
    if (g_hold.size() != g.targets.size())
        g_hold.assign(g.targets.size(), 0);
    for (size_t i = 0; i < g.targets.size(); i++) {
        auto& tg = g.targets[i];
        if (tg.done)
            continue;
        bool all = !g.crew.empty();
        for (uint8_t id : g.crew)
            if (star_of_player(id) != tg.star)
                all = false;
        g_hold[i] = all ? g_hold[i] + dt : 0;
        if (g_hold[i] >= kHold) {
            tg.done = 1;
            changed = true;
        }
    }
    bool done = true;
    for (auto& tg : g.targets)
        done = done && tg.done;
    if (done) {
        finish(true, R_NONE);
        return;
    }
    if (changed)
        publish();
}

void host_tick(double t)
{
    double dt = std::min(1.0, t - g_last_tick);
    g_last_tick = t;
    // the crew: players still in the session
    if (g.state == S_OFFERED || g.state == S_ACTIVE) {
        auto pl = C().sess->players();
        size_t before = g.crew.size();
        g.crew.erase(std::remove_if(g.crew.begin(), g.crew.end(),
                                    [&](uint8_t id) {
                                        return std::none_of(pl.begin(), pl.end(), [&](auto& p) { return p.id == id; });
                                    }),
                     g.crew.end());
        if (g.crew.size() != before) {
            if (g.state == S_ACTIVE && g.crew.size() < g.needed) {
                finish(false, R_LEFT);
                return;
            }
            publish();
        }
    }
    if (g.state == S_OFFERED && t > g.deadline) {
        finish(false, R_EXPIRED);
        return;
    }
    if (g.state == S_ACTIVE && t > g.deadline) {
        finish(false, R_TIMEOUT);
        return;
    }
    if (g.state == S_ACTIVE && g.type == T_ARMADA)
        armada_tick(t);
    else if (g.state == S_ACTIVE && g.type == T_SURVEY)
        survey_tick(dt);
    if ((g.state == S_DONE || g.state == S_FAILED) && t > g.deadline) {
        g = Contract{};
        publish();
    }
    // a new offer every few minutes while the players are in space together
    bool together = spaceext::in_space() && C().partners && !C().partners().empty();
    if (!together || g_period <= 0)
        g_next_offer = 0;
    else if (!g_next_offer)
        g_next_offer = t + std::min(g_first_delay, g_period);
    else if (g.state == S_NONE && t > g_next_offer) {
        std::string r = offer(std::uniform_int_distribution<int>(0, 1)(g_rng) ? T_ARMADA : T_SURVEY, "");
        if (r.rfind("offered", 0) != 0) // no colony for an armada: a survey
            r = offer(T_SURVEY, "");
        H->log(M, SK_INFO, "contracts: auto offer: %s", r.c_str());
        g_next_offer = t + g_period;
    }
    if (g.state != S_NONE && t - g_last_publish > 5)
        publish(); // rejoining players get the contract within 5 s
}

// ---- registry callbacks ----
bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    auto S = [](const char* n) { return H->sym(n); };
    a_PlayerUFOGet = S("Simulator::cSimulatorPlayerUFO::Get");
    a_GetAllUFOs = S("Simulator::GetAllUFOs");
    a_StartRaid = S("SporeCoop::Space_StartRaid");
    a_SetState = S("Simulator::cMission::SetState"); // BUG-0227: abort a resumed raid without ships
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_PlayerEmpire = S("Simulator::GetPlayerEmpire");
    a_AddMoney = S("Simulator::cEmpire::AddMoney");
    a_SpaceGame = S("Simulator::cSimulatorSpaceGame::ptr");
    a_AddItemByKey = S("Simulator::cPlayerInventory::AddItemByKey");
    a_Context = S("SporeCoop::Space_GetCurrentContext");
    g_rng.seed((uint32_t)(h->time_seconds() * 1000) ^ 0xC0B0u);
    spacecoop::set_raid_listener([](void* mission, uint32_t place, int state) {
        if (g_hide_ships && g_wave_resumed)
            return; // tests: a raid whose ships never came cannot end defended or plundered either
        if (host() && g.state == S_ACTIVE && g.type == T_ARMADA && g_wave_on && !g.targets.empty() &&
            place == (g.targets[0].planet | 0x40000000u) && state != 3 &&
            (!g_wave_fix || !g_wave_mission || mission == g_wave_mission)) // (BUG-0046: only the wave's raid)
            g_wave_result = state;
        else if (g_wave_away_fix && g_wave_fix && host() && g.state == S_ACTIVE && g.type == T_ARMADA && !g_wave_on &&
                 g_wave_mission && mission == g_wave_mission && (state == 5 || state == 6)) {
            g_wave_away_result = state; // BUG-0227: the host is away from the colony
            n_away_results++;
        }
    });
    return a_PlayerUFOGet && a_GetAllUFOs && a_StartRaid && a_StarMgr && a_PlayerEmpire &&
           a_AddMoney && a_SpaceGame && a_AddItemByKey && a_Context;
}

void tick(double t)
{
    if (!connected()) {
        if (g.state != S_NONE)
            g = Contract{};
        return;
    }
    if (host()) {
        static double last;
        if (t - last < 0.25)
            return;
        last = t;
        host_tick(t);
    } else if ((g.state == S_DONE || g.state == S_FAILED) && t > g.deadline + 10)
        g = Contract{}; // the host's "none" got lost
}

void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    double t = now();
    if (kind == K_STATE && !host()) {
        Contract c;
        if (!read_state(r, c, t))
            return;
        announce(g, c);
        g = c;
    } else if (kind == K_ACCEPT && host()) {
        uint32_t id = r.u32();
        if (r.ok() && id == g.id)
            accept(origin);
    } else if (kind == K_RESULT && !host()) {
        uint32_t id = r.u32();
        uint8_t ok = r.u8(), reason = r.u8();
        uint32_t mo = r.u32(), tool = r.u32();
        uint8_t type = r.u8(), n = r.u8();
        std::vector<uint8_t> crew;
        for (int i = 0; i < n && r.ok(); i++)
            crew.push_back(r.u8());
        if (r.ok())
            on_result(id, ok, reason, mo, tool, crew, type);
    }
}

void on_session_end()
{
    g = Contract{};
    g_sent = Contract{};
    g_wave_on = false;
    g_seen.clear();
    g_wave_mission = nullptr;
    g_toughened.clear();
    g_wave_resumed = false;
    g_wave_away_result = 0;
    g_alive.clear();
    g_next_offer = 0;
}

std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> v;
    if (!spaceext::in_space() || !connected() || g.state == S_NONE)
        return v;
    double t = now();
    uint8_t me = spaceext::my_id();
    bool in = g.has(me);
    uint32_t here = star_of(my_place());
    auto jump_row = [&](spaceext::Row& r, uint32_t star) {
        if (star && star != here) {
            r.button = tr("Jump there", "Прыгнуть");
            r.on_click = [star] { jump_to(star); };
        }
    };
    auto accept_row = [&](spaceext::Row& r) {
        if (!in && (g.state == S_OFFERED || g.state == S_ACTIVE)) {
            r.button = tr("Accept", "Принять");
            r.on_click = [] { accept_mine(); };
        }
    };
    char b[200];
    if (g.state == S_DONE || g.state == S_FAILED) {
        spaceext::Row r;
        r.text = g.state == S_DONE ? tr("Contract done: ", "Контракт выполнен: ") + reward_text(g)
                                   : tr("Contract failed: ", "Контракт провален: ") + reason_text(g.reason);
        r.color = g.state == S_DONE ? 0xFF60FF60 : 0xFFFF6040;
        v.push_back(std::move(r));
        return v;
    }
    if (g.state == S_OFFERED) {
        spaceext::Row r1, r2;
        snprintf(b, sizeof b, tr(": %d ships needed", ": нужно кораблей: %d"), (int)g.needed);
        r1.text = tr("Contract: ", "Контракт: ") + title(g) + b;
        r1.color = 0xFFFFD040;
        accept_row(r1);
        if (in) {
            snprintf(b, sizeof b, tr(" (accepted, %d more)", " (принят, ещё %d)"), (int)(g.needed - g.crew.size()));
            r1.text += b;
        }
        r2.text = tr("Reward each: ", "Награда каждому: ") + reward_text(g) + ", " + mmss(g.deadline - t) +
                  tr(" to accept", " на ответ");
        if (g.type == T_ARMADA && !g.targets.empty())
            jump_row(r2, g.targets[0].star);
        v.push_back(std::move(r1));
        v.push_back(std::move(r2));
        return v;
    }
    // active
    spaceext::Row r1;
    r1.color = 0xFFFFD040;
    if (g.type == T_ARMADA && !g.targets.empty()) {
        uint32_t s = g.targets[0].star;
        if (g.up)
            snprintf(b, sizeof b, tr(": wave %d/%d, %d ships, ", ": волна %d/%d, кораблей %d, "), g.wave, g.waves, g.left);
        else
            snprintf(b, sizeof b, tr(": wave %d/%d when %d crew are there, ", ": волна %d/%d, когда там %d из экипажа, "),
                     g.wave, g.waves, (int)g.needed);
        r1.text = tr("Armada at ", "Армада у ") + planet_name(g.targets[0].planet) + b + mmss(g.deadline - t);
        jump_row(r1, s);
        // in the colony's system view: land on the active planet (the travel picks the own colony first)
        int ctx = a_Context ? cdecl_call<int>(a_Context) : -1;
        if (in && here == s && ctx == 1) {
            r1.button = tr("Land", "Сесть");
            r1.on_click = [] {
                std::string e = spacecoop::debug({"land"});
                if (e != "landing")
                    say(tr("Can't land: ", "Посадка невозможна: ") + e);
            };
        }
        v.push_back(std::move(r1));
    } else {
        int d = 0;
        for (auto& tg : g.targets)
            d += tg.done;
        snprintf(b, sizeof b, tr("Survey: %d/%d stars, ", "Разведка: %d/%d звёзд, "), d, (int)g.targets.size());
        r1.text = b + mmss(g.deadline - t);
        v.push_back(std::move(r1));
        for (auto& tg : g.targets) {
            spaceext::Row r;
            if (tg.done) {
                r.text = "  " + star_name(tg.star) + tr(": surveyed", ": разведана");
                r.color = 0xFF60FF60;
            } else {
                int n = 0;
                for (uint8_t id : g.crew)
                    n += star_of_player(id) == tg.star;
                snprintf(b, sizeof b, tr(": crew here %d/%d", ": экипаж здесь %d/%d"), n, (int)g.crew.size());
                r.text = "  " + star_name(tg.star) + b;
                jump_row(r, tg.star);
            }
            v.push_back(std::move(r));
        }
    }
    spaceext::Row r2;
    r2.text = tr("Crew: ", "Экипаж: ") + names(g.crew) + tr(", reward ", ", награда ") + reward_text(g);
    accept_row(r2);
    v.push_back(std::move(r2));
    return v;
}

void draw_overlay()
{
    if (g.state != S_ACTIVE || !g.has(spaceext::my_id()) || !spaceext::in_space())
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    char b[200];
    double left = g.deadline - now();
    if (g.type == T_ARMADA)
        snprintf(b, sizeof b, tr("CONTRACT  armada wave %d/%d  ships %d  %s", "КОНТРАКТ  армада, волна %d/%d  кораблей %d  %s"),
                 g.wave, g.waves, g.left, mmss(left).c_str());
    else {
        int d = 0;
        for (auto& tg : g.targets)
            d += tg.done;
        snprintf(b, sizeof b, tr("CONTRACT  survey %d/%d stars  %s", "КОНТРАКТ  разведка %d/%d звёзд  %s"), d,
                 (int)g.targets.size(), mmss(left).c_str());
    }
    H->overlay_text(sw / 2 - utf8::overlay_width(b) / 2, 70, // cells, not bytes (BUG-0159)
                    left < 60 ? 0xFFFF8040 : 0xFFFFD040, b);
}

std::string status()
{
    char b[640];
    std::string crew;
    for (uint8_t id : g.crew)
        crew += (crew.empty() ? "" : ",") + std::to_string(id);
    int d = 0;
    for (auto& tg : g.targets)
        d += tg.done;
    snprintf(b, sizeof b,
             "contracts: id=%u type=%s state=%s reason=%u crew=%s needed=%u secs=%d star=%u wave=%u/%u ships_left=%u "
             "ships_up=%u targets=%d/%d money=%u tool=%08X offered=%u done=%u failed=%u rewards=%u tools_got=%u "
             "money_got=%d planet=%08X raids=%u ships=%u gone=%u period=%.0f resumed=%u wave_raids=%d\n",
             g.id, type_name(g.type), state_name(g.state), g.reason, crew.empty() ? "-" : crew.c_str(), g.needed,
             g.state ? (int)std::ceil(g.deadline - now()) : 0, g.targets.empty() ? 0u : g.targets[0].star, g.wave,
             g.waves, g.left, g.up, d, (int)g.targets.size(), g.money, g.tool, n_offered, n_done, n_failed, n_rewards,
             n_tools, g_money_got, g.targets.empty() ? 0u : g.targets[0].planet, n_raids, n_ships, n_gone,
             g_period, n_resumed, (int)raids_on_target().size());
    return b;
}

// coop.space contracts [offer <armada|survey> [here|<star>|<n>] | accept | cancel | period <s> |
//                       limit <s> | ships | targets | money | tool <hash>]
std::string debug(const std::vector<std::string>& a)
{
    if (a.empty())
        return status();
    char b[200];
    if (a[0] == "offer" && a.size() > 1)
        return offer(a[1] == "armada" ? T_ARMADA : T_SURVEY, a.size() > 2 ? a[2] : "") + "\n";
    if (a[0] == "accept")
        return accept_mine() + "\n";
    if (a[0] == "cancel") {
        if (!host() || (g.state != S_OFFERED && g.state != S_ACTIVE))
            return "nothing to cancel (host only)\n";
        finish(false, R_CANCELLED);
        return "cancelled\n";
    }
    if (a[0] == "period" && a.size() > 1) {
        g_period = atof(a[1].c_str());
        g_next_offer = 0;
        snprintf(b, sizeof b, "auto offers every %.0f s (0 = off)\n", g_period);
        return b;
    }
    if (a[0] == "limit" && a.size() > 1) {
        g_limit_override = atof(a[1].c_str());
        snprintf(b, sizeof b, "time limit of new contracts %.0f s (0 = the default)\n", g_limit_override);
        return b;
    }
    if ((a[0] == "tough_reuse" || a[0] == "hideships") && a.size() > 1) { // tests (BUG-0227)
        (a[0] == "tough_reuse" ? g_tough_reuse : g_hide_ships) = a[1] != "0";
        return a[0] + " " + a[1] + "\n";
    }
    if (a[0] == "wave") { // tests (BUG-0227): the wave's state
        snprintf(b, sizeof b, "wave %d/%d on=%d ships=%d resumed=%d away_result=%d mission=%p aborted=%u away_results=%u resumed_n=%u\n",
                 g.wave, g.waves, (int)g_wave_on, (int)g_wave_ships, (int)g_wave_resumed, g_wave_away_result,
                 g_wave_mission, n_aborted, n_away_results, n_resumed);
        return b;
    }
    if (a[0] == "wavefix" && a.size() > 1) { // BUG-0046: 0 = a wave is any raid on the planet (tests)
        g_wave_fix = a[1] != "0";
        return std::string("wavefix ") + (g_wave_fix ? "1" : "0") + "\n";
    }
    if (a[0] == "raids") { // tests (BUG-0046): the raid missions on the contract's planet (state 2/3)
        std::string o;
        char b[96];
        for (void* m : raids_on_target()) {
            snprintf(b, sizeof b, "raid %p state %d%s\n", m, F<int>(m, 0x84), m == g_wave_mission ? " (wave)" : "");
            o += b;
        }
        return o.empty() ? "no raids\n" : o;
    }
    if (a[0] == "ships") { // host: the armada ships alive now (the pointers coop.fleet list prints)
        std::string s;
        for (void* u : g_alive) {
            snprintf(b, sizeof b, "armada %p\n", u);
            s += b;
        }
        return s.empty() ? "no armada ships\n" : s;
    }
    if (a[0] == "targets") {
        std::string s;
        for (auto& tg : g.targets) {
            snprintf(b, sizeof b, "target %u 0x%X %s done=%d\n", tg.star, tg.star, star_name(tg.star).c_str(), tg.done);
            s += b;
        }
        return s.empty() ? "no targets\n" : s;
    }
    if (a[0] == "notices") {
        std::string s;
        for (auto& t : g_said)
            s += "notice " + t + "\n";
        return s.empty() ? "no notices\n" : s;
    }
    if (a[0] == "money") {
        snprintf(b, sizeof b, "money %d\n", money());
        return b;
    }
    if (a[0] == "tool" && a.size() > 1) {
        uint32_t h = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        snprintf(b, sizeof b, "tool %08X %s: %s\n", h, tool_name(h).c_str(), has_tool(h) ? "have" : "no");
        return b;
    }
    if (a[0] == "star" && a.size() > 1) {
        uint32_t s = (uint32_t)strtoul(a[1].c_str(), nullptr, 0);
        snprintf(b, sizeof b, "star %u: %s\n", s, star_name(s).c_str());
        return b;
    }
    return "usage: coop.space contracts [offer <armada|survey> [here|<star>|<n>] | accept | cancel | period <s> | "
           "limit <s> | ships | targets | money | tool <hex> | star <id>]\n";
}

bool reg = spaceext::add({.name = "contracts",
                          .kind_lo = 20,
                          .kind_hi = 29,
                          .init = init,
                          .tick = tick,
                          .on_message = on_message,
                          .on_session_end = on_session_end,
                          .draw_overlay = draw_overlay,
                          .rows = rows,
                          .status = status,
                          .debug = debug});

} // namespace
