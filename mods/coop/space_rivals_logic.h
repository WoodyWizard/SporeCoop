// Portable Space Rivals logic (WP-R6 of 0.8.0, BUG-1550, docs/11-rivals.md sections 11.3.5, 11.4, 11.7, 11.9).
// No Windows or SporeKit dependencies so native Linux ASan+UBSan unit tests cover every decision.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "rivals_logic.h"

namespace spacerivals {

// Chronicle Galaxy Core finale bonus (matches rivals::kPtsGalaxyCore in rivals_logic.h).
constexpr int32_t kPtsCoreFinale = ::rivals::kPtsGalaxyCore;

// Spore Space tool instance IDs (group 0x30608F0B, verified against Spore_Game.package and GOG 2024 SporeApp.exe).
constexpr uint32_t kToolPlanetBuster = 0x05475298u;    // Planet Buster (0x0BA5C1BF!0xDCF0CDB9)
constexpr uint32_t kToolFanaticalFrenzy = 0x1CC65FA3u; // Fanatical Frenzy (0x0BA5C1BF!0x06089937)
constexpr uint32_t kToolGravitationWave = 0xA43B6F38u; // Gravitation Wave (0x0BA5C1BF!0x06036020)

constexpr uint16_t kMinEmigCities = 1;
constexpr uint16_t kMaxEmigCities = 10;
constexpr uint16_t kDefaultEmigCities = 3;

constexpr int32_t kInfMissionGain = 15;
constexpr int32_t kInfTradeGain = 10;
constexpr int32_t kInfGiftGain = 10;
constexpr int32_t kInfAllyTrickle = 2;
constexpr double kInfAllyTrickleSecs = 60.0;
constexpr int32_t kSpaceMoneyPerInfluenceUnit = 200; // 50 base -> 10,000, 100 -> 20,000, 150 -> 30,000 Sporebucks
constexpr int32_t kSpaceDraftPayoutScale = 100;      // Civ->Space draft pick: 100 -> 10,000, 150 -> 15,000, 200 -> 20,000
constexpr float kSpaceNpcAllyRel = 70.0f;
constexpr float kSpaceNpcHostileRel = -25.0f;
constexpr float kSpaceNpcMaxRel = 95.0f;
constexpr size_t kMaxSpaceNpcUiEntries = 16;

// MSG_RIVALS_FACTION (Stage::Space) subkind payload ops.
enum EmigKind : uint8_t {
    EMIG_ASSIGN = 1, // host -> all: guest homeworld emigration assignment
    EMIG_ACK = 2,    // guest -> host: guest applied its emigrated homeworld
    EMIG_REQ = 3,    // guest -> host: guest requests its emigration assignment
};

enum NpcRelOp : uint8_t {
    NPCREL_TURN_AGAINST = 1, // host -> target guest: apply hostile shift from rival influence
    NPCREL_SMOOTH_OVER = 2,  // host -> actor guest: apply friendly shift from own influence
    NPCREL_EARN_INF = 3,     // guest -> host: report earned influence from mission/trade/gift
};

enum CustomOp : uint8_t {
    OP_CHARGE_MONEY = 1,   // host -> guest: charge own Space sporebucks for influence purchase
    OP_FINALE_SUMMARY = 2, // host -> all: Chronicle Galaxy Core finale summary + winner
    OP_HOMES_SYNC = 3,     // host -> all: sync all players' protected homeworlds (R7)
    OP_CORE_ENTERED = 4,   // guest -> host: report entering the galactic core
    OP_GRANT_MONEY = 5,    // host -> guest: grant Space sporebucks (Civ->Space draft payout)
};

// ---------------- R5: Emigration planet similarity & selection ----------------
struct HomeRef {
    uint32_t star_id = 0;
    uint32_t planet_id = 0;
    int32_t type = 5; // cPlanetRecord +0x28: 2=T0, 3=T1, 4=T2, 5=T3
    bool is_moon = false;
    float atmo = 0.5f;
    float temp = 0.5f;
    float water = 0.5f;
    uint16_t cities = kDefaultEmigCities;
};

struct CandidatePlanet {
    uint32_t star_id = 0;
    uint32_t planet_id = 0;
    int32_t star_type = 3;   // cStarRecord +0x48: 1=GalacticCore, 2..6=normal stars
    int32_t planet_type = 2; // cPlanetRecord +0x28: 0=Asteroid, 1=GasGiant, 2..6=T0..T3
    int32_t tech_level = 0;  // cPlanetRecord +0x194: >=4 is civ/space empire
    bool is_moon = false;    // cPlanetRecord +0x2C bit 2 (0x4)
    bool star_owned = false; // cStarRecord +0x54 != 0xFFFFFFFF
    bool star_is_home = false;
    float dist_from_host = 0.0f;
    float atmo = 0.0f;
    float temp = 0.0f;
    float water = 0.0f;
    std::string star_name;
    std::string planet_name;
};

inline int planet_tscore(int32_t planet_type)
{
    if (planet_type <= 2)
        return 0;
    if (planet_type == 3)
        return 1;
    if (planet_type == 4)
        return 2;
    return 3;
}

inline uint16_t clamp_emig_cities(int n)
{
    if (n < (int)kMinEmigCities)
        return kMinEmigCities;
    if (n > (int)kMaxEmigCities)
        return kMaxEmigCities;
    return (uint16_t)n;
}

inline float preferred_emigration_distance(::rivals::BaseDistance d)
{
    switch (d) {
    case ::rivals::BaseDistance::Near: return 12.0f;
    case ::rivals::BaseDistance::Far: return 55.0f;
    default: return 28.0f;
    }
}

inline bool is_suitable_emigration_planet(const CandidatePlanet& c, uint32_t host_home_star,
                                          const std::set<uint32_t>& reserved_stars)
{
    if (c.star_id == 0 || c.planet_id == 0 || c.star_id == host_home_star)
        return false;
    if (reserved_stars.count(c.star_id))
        return false;
    if (c.star_owned || c.star_is_home)
        return false;
    if (c.star_type < 2 || c.star_type > 6) // reject black hole (0), galactic core (1), proto-disks
        return false;
    if (c.planet_type < 2) // reject asteroid belts (0) and gas giants (1)
        return false;
    if (c.tech_level >= 4) // reject existing civilizations, empires, and scripted colony ruins
        return false;
    if (!std::isfinite(c.dist_from_host) || c.dist_from_host <= 0.01f)
        return false;
    return true;
}

// Lower score = better match for a guest's emigrated homeworld.
inline float score_emigration_planet(const CandidatePlanet& c, const HomeRef& ref, ::rivals::BaseDistance dist_pref)
{
    float score = 0.0f;
    // Match planet size class (full planet vs moon).
    if (c.is_moon != ref.is_moon)
        score += 400.0f;
    // Match terraforming tier closely, preferring higher T-scores when tied.
    int ct = planet_tscore(c.planet_type);
    int rt = planet_tscore(ref.type);
    score += std::fabs((float)(ct - rt)) * 60.0f;
    score -= (float)ct * 10.0f;
    // Climate similarity (atmosphere, temperature, water).
    float da = std::isfinite(c.atmo) ? std::fabs(c.atmo - ref.atmo) : 1.0f;
    float dt = std::isfinite(c.temp) ? std::fabs(c.temp - ref.temp) : 1.0f;
    float dw = std::isfinite(c.water) ? std::fabs(c.water - ref.water) : 1.0f;
    score += (da + dt + 0.5f * dw) * 30.0f;
    // Distance relative to the lobby's BaseDistance setting (Near / Middle / Far).
    float target_d = preferred_emigration_distance(dist_pref);
    score += std::fabs(c.dist_from_host - target_d) * 1.5f;
    return score;
}

// Returns the index of the best suitable candidate planet, or -1 if none qualifies.
inline int pick_emigration_planet(const std::vector<CandidatePlanet>& cands, const HomeRef& ref,
                                  ::rivals::BaseDistance dist_pref, const std::set<uint32_t>& reserved_stars)
{
    int best = -1;
    float best_score = 1e30f;
    for (size_t i = 0; i < cands.size(); i++) {
        const auto& c = cands[i];
        if (!is_suitable_emigration_planet(c, ref.star_id, reserved_stars))
            continue;
        float sc = score_emigration_planet(c, ref, dist_pref);
        if (best < 0 || sc < best_score - 1e-4f ||
            (std::fabs(sc - best_score) <= 1e-4f && c.planet_id < cands[(size_t)best].planet_id)) {
            best = (int)i;
            best_score = sc;
        }
    }
    return best;
}

inline std::string format_emigration_notice(const std::string& old_planet, const std::string& new_planet, bool ru)
{
    std::string old_n = old_planet.empty() ? (ru ? "старую планету" : "your old homeworld") : old_planet;
    std::string new_n = new_planet.empty() ? (ru ? "новую планету" : "a new homeworld") : new_planet;
    if (ru)
        return "Ваш народ покинул " + old_n + " ради " + new_n;
    return "Your people left " + old_n + " for " + new_n;
}

// ---------------- R5: Persistent Emigration Assignment Table ----------------
struct EmigrationRecord {
    uint8_t player_id = 0;
    uint32_t orig_home_star = 0;
    uint32_t orig_home_planet = 0;
    uint32_t new_home_star = 0;
    uint32_t new_home_planet = 0;
    uint16_t target_cities = kDefaultEmigCities;
    std::string old_planet_name;
    std::string new_planet_name;
    bool applied = false;

    bool valid() const { return new_home_star != 0 && new_home_planet != 0 && new_home_planet != orig_home_planet; }
};

struct EmigrationStore {
    std::map<std::string, EmigrationRecord> by_key;

    static std::string sanitize(const std::string& s, size_t max_len = 64)
    {
        std::string out;
        out.reserve(std::min(s.size(), max_len));
        for (char c : s) {
            if (out.size() >= max_len)
                break;
            if (c == '\n' || c == '\r' || c == '\t' || c == '|')
                out.push_back(' ');
            else
                out.push_back(c);
        }
        return out;
    }

    std::vector<uint8_t> encode() const
    {
        std::string out;
        char buf[160];
        for (const auto& [k, r] : by_key) {
            if (k.empty() || !r.valid())
                continue;
            snprintf(buf, sizeof buf, "emig %s %u %u %u %u %u|", sanitize(k, 40).c_str(), (unsigned)r.orig_home_star,
                     (unsigned)r.orig_home_planet, (unsigned)r.new_home_star, (unsigned)r.new_home_planet,
                     (unsigned)r.target_cities);
            out += buf;
            out += sanitize(r.old_planet_name, 48);
            out += "|";
            out += sanitize(r.new_planet_name, 48);
            out += "\n";
        }
        return std::vector<uint8_t>(out.begin(), out.end());
    }

    bool decode(const std::vector<uint8_t>& blob)
    {
        by_key.clear();
        std::string text(blob.begin(), blob.end());
        size_t pos = 0;
        bool any = false;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = nl == std::string::npos ? text.substr(pos) : text.substr(pos, nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            if (line.rfind("emig ", 0) != 0)
                continue;
            size_t p1 = line.find('|');
            if (p1 == std::string::npos)
                continue;
            size_t p2 = line.find('|', p1 + 1);
            if (p2 == std::string::npos)
                continue;
            std::string head = line.substr(0, p1);
            char key[64] = {};
            unsigned os = 0, op = 0, ns = 0, np = 0, tc = kDefaultEmigCities;
            if (sscanf(head.c_str(), "emig %63s %u %u %u %u %u", key, &os, &op, &ns, &np, &tc) == 6 && ns != 0 &&
                np != 0 && np != op) {
                EmigrationRecord r;
                r.orig_home_star = os;
                r.orig_home_planet = op;
                r.new_home_star = ns;
                r.new_home_planet = np;
                r.target_cities = clamp_emig_cities((int)tc);
                r.old_planet_name = line.substr(p1 + 1, p2 - p1 - 1);
                r.new_planet_name = line.substr(p2 + 1);
                by_key[key] = std::move(r);
                any = true;
            }
        }
        return any;
    }

    void adopt_key(const std::string& from, const std::string& to)
    {
        if (from.empty() || to.empty() || from == to)
            return;
        if (auto it = by_key.find(from); it != by_key.end()) {
            by_key[to] = std::move(it->second);
            by_key.erase(it);
        }
    }
};

// ---------------- R5: Guest Emigration Liftoff State Machine (BUG-1555) ----------------
// Distinguishes home creation, relinquishing the host's original homeworld, and traveling/teleporting
// away from the host's star so natural Civ -> Space entry (starting landed on host's planet with ctx==0)
// never strands the guest at the host's star after liftoff.
enum class EmigStep : uint8_t {
    None = 0,           // done or waiting for liftoff
    CreateHome = 1,     // claim/populate new homeworld + SetHomePlanet
    RelinquishHost = 2, // remove host homeworld from guest empire & SpacePlayerData
    TeleportHome = 3,   // jump away from host star to new home star (teleport_in_galaxy or travel_to_star)
};

struct GuestEmigMachine {
    uint32_t applied_epoch = 0;
    uint32_t applied_planet = 0;
    bool home_created = false;
    bool host_relinquished = false;
    bool teleported = false;

    void reset()
    {
        applied_epoch = 0;
        applied_planet = 0;
        home_created = false;
        host_relinquished = false;
        teleported = false;
    }

    bool needs_reapply(uint32_t epoch, uint32_t new_home_planet, uint32_t cur_empire_home_planet) const
    {
        if (!new_home_planet)
            return false;
        if (!home_created || applied_epoch != epoch || applied_planet != new_home_planet)
            return true;
        if (cur_empire_home_planet && cur_empire_home_planet != 0xFFFFFFFFu && cur_empire_home_planet != new_home_planet)
            return true;
        return false;
    }

    void mark_home_created(uint32_t epoch, uint32_t new_home_planet)
    {
        applied_epoch = epoch;
        applied_planet = new_home_planet;
        home_created = true;
        host_relinquished = false;
        teleported = false;
    }

    // Returns true when tick() should schedule apply_guest_emigration_task().
    bool needs_task(uint32_t epoch, const EmigrationRecord& rec, uint32_t cur_empire_home_planet, int ctx,
                    uint32_t active_planet, uint32_t active_star) const
    {
        if (!rec.valid())
            return false;
        if (needs_reapply(epoch, rec.new_home_planet, cur_empire_home_planet))
            return true;
        if (!host_relinquished) {
            bool on_host_surface = (ctx == 0 && active_planet == rec.orig_home_planet);
            return !on_host_surface;
        }
        if (!teleported) {
            if (active_star == rec.orig_home_star)
                return ctx != 0; // wait until liftoff before jumping to new homeworld
            return active_star != 0;
        }
        return false;
    }

    // Returns the next pending phase to execute inside apply_guest_emigration_task().
    EmigStep next_step(uint32_t epoch, const EmigrationRecord& rec, uint32_t cur_empire_home_planet, int ctx,
                       uint32_t active_planet, uint32_t active_star) const
    {
        if (!rec.valid())
            return EmigStep::None;
        if (needs_reapply(epoch, rec.new_home_planet, cur_empire_home_planet))
            return EmigStep::CreateHome;
        if (!host_relinquished) {
            if (ctx == 0 && active_planet == rec.orig_home_planet)
                return EmigStep::None; // deferred while landed on host's planet
            return EmigStep::RelinquishHost;
        }
        if (!teleported && active_star == rec.orig_home_star && ctx != 0)
            return EmigStep::TeleportHome;
        return EmigStep::None;
    }
};

// ---------------- R5: Clean Relinquish & Null-Empire Invariants (BUG-1567) ----------------
// When a Space Rivals guest relinquishes the host's original home star (mEmpireID = 0xFFFFFFFF),
// leaving star/planet tech level at 5 violates Spore's invariant (tech 5 => non-null GetEmpire):
// Galaxy-view hover/click faults at 0x0106CA9F / 0x00FDC5FD ([NULL+0x84]), and EnterSolarSystem
// calls GetEmpireForStar -> AddStarOwnership -> UpdateAndGetColor with mHomeStarID == 0xFFFFFFFF,
// crashing at 0x00BB0F22 (FindStars reading NULL+0x3C).
constexpr uint32_t kRelinquishedTechLevel = 1u;      // TechLevel::Creature (non-empire)
constexpr uint32_t kEmpirePaletteBase = 0x53DBCF3u;  // Spore's 12 empire palette IDs
constexpr uint32_t kEmpirePaletteCount = 12u;

struct RelinquishedStarState {
    uint32_t empire_id = 0xFFFFFFFFu;
    uint32_t star_tech = 0;
    uint32_t star_flags = 0;
    std::vector<uint32_t> planet_techs;
};

inline bool is_orphaned_tech5_star(uint32_t empire_id, uint32_t star_tech)
{
    return empire_id == 0xFFFFFFFFu && star_tech == 5u;
}

inline void apply_clean_relinquish(RelinquishedStarState& st, bool clean_fix)
{
    st.empire_id = 0xFFFFFFFFu;
    st.star_flags &= ~0x7u;
    if (!clean_fix)
        return;
    for (uint32_t& pt : st.planet_techs)
        if (pt == 5u)
            pt = kRelinquishedTechLevel;
    if (st.star_tech == 5u)
        st.star_tech = kRelinquishedTechLevel;
}

inline uint32_t pick_color_fallback_home_star(uint32_t cur_home_star_id, bool cur_home_valid,
                                              const std::vector<uint32_t>& owned_star_ids)
{
    if (cur_home_valid && cur_home_star_id != 0 && cur_home_star_id != 0xFFFFFFFFu)
        return cur_home_star_id;
    for (uint32_t sid : owned_star_ids)
        if (sid != 0 && sid != 0xFFFFFFFFu)
            return sid;
    return 0xFFFFFFFFu;
}

inline uint32_t fallback_empire_palette_id(uint32_t pol_id)
{
    return kEmpirePaletteBase + (pol_id % kEmpirePaletteCount);
}

// ---------------- R7: Homeworld Inviolability Table ----------------
struct ProtectedHome {
    uint8_t player_id = 0;
    uint32_t star_id = 0;
    uint32_t planet_id = 0;
};

struct HomeProtectionTable {
    std::map<uint8_t, ProtectedHome> homes; // player_id -> homeworld

    void clear() { homes.clear(); }

    void set(uint8_t player_id, uint32_t star_id, uint32_t planet_id)
    {
        if (!::rivals::valid_player(player_id) || star_id == 0 || planet_id == 0)
            return;
        homes[player_id] = {player_id, star_id, planet_id};
    }

    void erase(uint8_t player_id) { homes.erase(player_id); }

    bool is_protected_planet(uint32_t planet_id, uint8_t* owner_out = nullptr) const
    {
        if (planet_id == 0)
            return false;
        for (const auto& [pid, h] : homes) {
            if (h.planet_id == planet_id) {
                if (owner_out)
                    *owner_out = pid;
                return true;
            }
        }
        return false;
    }

    bool is_protected_star(uint32_t star_id, uint8_t* owner_out = nullptr) const
    {
        if (star_id == 0)
            return false;
        for (const auto& [pid, h] : homes) {
            if (h.star_id == star_id) {
                if (owner_out)
                    *owner_out = pid;
                return true;
            }
        }
        return false;
    }

    static bool is_banned_homeworld_tool(uint32_t tool_inst)
    {
        return tool_inst == kToolPlanetBuster || tool_inst == kToolFanaticalFrenzy ||
               tool_inst == kToolGravitationWave;
    }

    bool should_block_tool(uint32_t tool_inst, uint32_t active_planet, uint8_t* owner_out = nullptr) const
    {
        if (!is_banned_homeworld_tool(tool_inst))
            return false;
        return is_protected_planet(active_planet, owner_out);
    }

    bool should_block_capture(uint32_t target_star, uint32_t target_planet, bool emigrating_guard,
                              uint8_t* owner_out = nullptr) const
    {
        if (emigrating_guard)
            return false;
        if (target_planet && is_protected_planet(target_planet, owner_out))
            return true;
        if (target_star && is_protected_star(target_star, owner_out))
            return true;
        return false;
    }
};

inline std::string format_protection_notice(uint32_t tool_inst, bool is_capture, bool ru)
{
    if (tool_inst == kToolPlanetBuster) {
        return ru ? "Нельзя применить Планетодробитель против родной планеты игрока!"
                  : "Cannot use Planet Buster on a player's homeworld!";
    }
    if (is_capture || tool_inst == kToolFanaticalFrenzy) {
        return ru ? "Родную планету игрока нельзя захватить!"
                  : "A player's homeworld cannot be captured!";
    }
    return ru ? "Родная планета игрока защищена от уничтожения!"
              : "A player's homeworld cannot be destroyed!";
}

// ---------------- BUG-1582: R7 HP Floor, Cinematic Guards, Surface Orientation & Civ Cleanup ----------------
constexpr float kR7CityHpFloorMin = 100.0f;
constexpr float kR7CityHpFloorFrac = 0.20f;
constexpr uint32_t kToolPlaceColony = 0x733B280Cu; // placecolony (Colony Incredi-Pak)

// Safe HP floor restored on a protected homeworld city's hall/buildings when h_DestroyCity blocks destruction,
// so Spore stops re-invoking DestroyCity every simulation tick (fixes 67x infinite R7 spam).
inline float r7_city_hp_floor(float max_hp)
{
    if (!std::isfinite(max_hp) || max_hp <= 0.0f)
        return kR7CityHpFloorMin;
    float frac = max_hp * kR7CityHpFloorFrac;
    float base = std::min(max_hp, kR7CityHpFloorMin);
    return std::max(base, frac);
}

// Decide whether h_CaptureSystem / h_TransferStar should block a star transfer on a protected homeworld.
// Allows idempotent ownership refreshes (cur_owner_pol == new_pol) and Space intro cinematic setup.
inline bool r7_should_block_star_transfer(bool protected_star, bool emigrating_guard, bool cinematic_playing,
                                          uint32_t cur_owner_pol, uint32_t new_pol)
{
    if (!protected_star || emigrating_guard || cinematic_playing)
        return false;
    if (new_pol != 0xFFFFFFFFu && cur_owner_pol == new_pol)
        return false;
    return true;
}

// Decide whether h_SetCaptureProgress should block city capture progress on a protected homeworld.
// Ignores intro cinematic transitions and self-pol updates.
inline bool r7_should_block_city_capture(bool protected_planet, bool cinematic_playing, uint32_t city_pol,
                                         uint32_t attacker_pol, float pct)
{
    if (!protected_planet || cinematic_playing || pct <= 0.0f || attacker_pol == 0xFFFFFFFFu)
        return false;
    if (city_pol != 0 && city_pol != 0xFFFFFFFFu && attacker_pol == city_pol)
        return false;
    return true;
}

// Suppress R7 protection notices during Space intro cinematics or self-inflicted/non-rival events.
inline bool r7_should_emit_notice(bool cinematic_playing, bool has_rival_attacker = true)
{
    return !cinematic_playing && has_rival_attacker;
}

// Build a unit quaternion (x, y, z, w) that aligns a city's local +Z axis to the outward radial surface normal
// at planet position (x, y, z). Prevents cities from lying tilted on their sides at non-polar latitudes.
inline void city_surface_quat(float x, float y, float z, float q_out[4])
{
    float len2 = x * x + y * y + z * z;
    if (!std::isfinite(len2) || len2 < 1e-6f) {
        q_out[0] = 0.0f;
        q_out[1] = 0.0f;
        q_out[2] = 0.0f;
        q_out[3] = 1.0f;
        return;
    }
    float inv = 1.0f / std::sqrt(len2);
    float ux = x * inv, uy = y * inv, uz = z * inv;
    if (uz < -0.9999f) {
        // 180 deg rotation around +X aligns +Z to -Z.
        q_out[0] = 1.0f;
        q_out[1] = 0.0f;
        q_out[2] = 0.0f;
        q_out[3] = 0.0f;
        return;
    }
    float s = std::sqrt((1.0f + uz) * 2.0f);
    float qx = -uy / s;
    float qy = ux / s;
    float qz = 0.0f;
    float qw = 0.5f * s;
    float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (qlen > 1e-6f) {
        qx /= qlen;
        qy /= qlen;
        qz /= qlen;
        qw /= qlen;
    }
    q_out[0] = qx;
    q_out[1] = qy;
    q_out[2] = qz;
    q_out[3] = qw;
}

// Returns true when quaternion (qx, qy, qz, qw) is valid and its rotated +Z axis points radially outward at (x, y, z).
inline bool city_quat_upright(float x, float y, float z, float qx, float qy, float qz, float qw)
{
    float len2 = x * x + y * y + z * z;
    float qn2 = qx * qx + qy * qy + qz * qz + qw * qw;
    if (!std::isfinite(len2) || len2 < 1e-6f || !std::isfinite(qn2) || qn2 < 0.01f)
        return false;
    float inv_q = 1.0f / std::sqrt(qn2);
    qx *= inv_q;
    qy *= inv_q;
    qz *= inv_q;
    qw *= inv_q;
    // Rotated +Z column of unit quaternion.
    float rx = 2.0f * (qx * qz + qw * qy);
    float ry = 2.0f * (qy * qz - qw * qx);
    float rz = 1.0f - 2.0f * (qx * qx + qy * qy);
    float inv_p = 1.0f / std::sqrt(len2);
    float dot = rx * (x * inv_p) + ry * (y * inv_p) + rz * (z * inv_p);
    return std::isfinite(dot) && dot >= 0.70f;
}

// Civ -> Space homeworld cleanup predicates: identify leftover foreign Civ-stage vehicles and rival cities.
inline bool should_cleanup_civ_vehicle(uint32_t veh_pol, uint32_t my_pol)
{
    if (my_pol == 0 || my_pol == 0xFFFFFFFFu)
        return false;
    return veh_pol != my_pol;
}

inline bool should_unify_civ_city(uint32_t city_pol, uint32_t my_pol)
{
    if (my_pol == 0 || my_pol == 0xFFFFFFFFu)
        return false;
    if (city_pol == 0 || city_pol == 0xFFFFFFFFu)
        return false;
    return city_pol != my_pol;
}

// Space Rivals emigrated guest starter unlock plan: unlocks Space tutorial travel gates (Story201 / Flight School)
// so an emigrated guest is never trapped in its new home star system or locked out of Colony Packs.
struct EmigUnlockPlan {
    bool set_first_comm = false;
    bool set_flight_school = false;
    bool set_mission201 = false;
    bool grant_interplanetary = false;
    bool grant_interstellar = false;
    bool grant_scan = false;
    bool grant_laser = false;
    bool grant_abduct = false;
    bool grant_drop_cargo = false;
    bool grant_health = false;
    bool grant_energy = false;
    bool grant_place_colony = false;

    bool any_flag() const { return set_first_comm || set_flight_school || set_mission201; }
    bool any_tool() const
    {
        return grant_interplanetary || grant_interstellar || grant_scan || grant_laser || grant_abduct ||
               grant_drop_cargo || grant_health || grant_energy || grant_place_colony;
    }
    bool any() const { return any_flag() || any_tool(); }
};

inline EmigUnlockPlan emigration_unlock_plan(bool fix, bool active, bool is_host, bool home_created, bool hold_grants,
                                             bool has_mm, bool has_inv, bool first_comm, bool flight_school,
                                             bool mission201, bool has_interplanetary, bool has_interstellar,
                                             bool has_scan, bool has_laser, bool has_abduct, bool has_drop_cargo,
                                             bool has_health, bool has_energy, bool has_place_colony,
                                             bool colony_pack_already_granted)
{
    EmigUnlockPlan p;
    if (!fix || !active || is_host || !home_created)
        return p;
    if (has_mm) {
        p.set_first_comm = !first_comm;
        p.set_flight_school = !flight_school;
        p.set_mission201 = !mission201;
    }
    if (has_inv && !hold_grants) {
        p.grant_interplanetary = !has_interplanetary;
        p.grant_interstellar = !has_interstellar;
        p.grant_scan = !has_scan;
        p.grant_laser = !has_laser;
        p.grant_abduct = !has_abduct;
        p.grant_drop_cargo = !has_drop_cargo;
        p.grant_health = !has_health;
        p.grant_energy = !has_energy;
        p.grant_place_colony = !has_place_colony && !colony_pack_already_granted;
    }
    return p;
}

// ---------------- Space NPC Empire Influence Helpers ----------------
inline int32_t influence_gain_for_event(uint32_t ev_id, std::string* reason_out = nullptr)
{
    auto set_r = [&](const char* r) {
        if (reason_out)
            *reason_out = r;
    };
    switch (ev_id) {
    case 0x0526E519u: // mission completed
        set_r("mission");
        return kInfMissionGain;
    case 0x0526E527u:
    case 0x0667AF08u:
    case 0x0526E4EEu: // trade
        set_r("trade");
        return kInfTradeGain;
    case 0x0526E51Fu:
    case 0x0526E4F2u: // gift
        set_r("gift");
        return kInfGiftGain;
    default:
        return 0;
    }
}

// Rotating Spore diplomacy event IDs so repeated TurnAgainst / SmoothOver actions stack cleanly.
inline uint32_t hostile_influence_event(uint32_t seq)
{
    static constexpr uint32_t kHostile[4] = {0x0526E4FEu, 0x0526E50Eu, 0x05F62736u, 0x0594AFFFu};
    return kHostile[seq % 4];
}

inline uint32_t friendly_influence_event(uint32_t seq)
{
    static constexpr uint32_t kFriendly[4] = {0x0526E51Fu, 0x0526E4E5u, 0x0526E4F2u, 0x05B6CF09u};
    return kFriendly[seq % 4];
}

// Space-scaled influence purchase cost and Civ->Space draft payout (BUG-1555).
inline int32_t influence_buy_space_cost(int32_t base_price)
{
    if (base_price <= 0)
        return 0;
    int64_t scaled = (int64_t)base_price * kSpaceMoneyPerInfluenceUnit;
    if (scaled > 99999999LL)
        scaled = 99999999LL;
    return (int32_t)scaled;
}

inline int32_t space_draft_payout_money(int32_t raw_payout)
{
    if (raw_payout <= 0)
        return 0;
    int64_t scaled = (int64_t)raw_payout * kSpaceDraftPayoutScale;
    if (scaled > 99999999LL)
        scaled = 99999999LL;
    return (int32_t)scaled;
}

// ---------------- F9 Influence UI: discovered Space NPC empires (BUG-1555) ----------------
struct SpaceNpcSnapshot {
    uint32_t pol_id = 0;
    std::string name;
    float rel = 0.0f;
    bool flag_ally = false;
    bool flag_war = false;
};

struct SpaceNpcUiItem {
    uint32_t npc_id = 0;
    std::string label;
    bool allied = false;
    bool hostile = false;
    bool can_turn = false;
    bool can_protect = false;
    bool can_smooth = false;
};

inline std::string format_space_npc_label(const SpaceNpcSnapshot& s, int ordinal)
{
    std::string nm = s.name.empty() ? ("Empire #" + std::to_string(ordinal)) : s.name;
    float r = std::isfinite(s.rel) ? s.rel : 0.0f;
    bool ally = s.flag_ally || r >= kSpaceNpcAllyRel;
    bool war = s.flag_war || (!ally && r <= kSpaceNpcHostileRel);
    char tail[64];
    if (ally)
        snprintf(tail, sizeof tail, " (Ally, %+.0f)", r);
    else if (war)
        snprintf(tail, sizeof tail, " (War, %+.0f)", r);
    else
        snprintf(tail, sizeof tail, " (%+.0f)", r);
    return nm + tail;
}

inline std::vector<SpaceNpcUiItem> build_space_npc_ui_list(std::vector<SpaceNpcSnapshot> snaps, bool has_rival)
{
    auto rank_of = [](const SpaceNpcSnapshot& s) -> int {
        float r = std::isfinite(s.rel) ? s.rel : 0.0f;
        if (s.flag_ally || r >= kSpaceNpcAllyRel)
            return 0; // allied first
        if (s.flag_war || r <= kSpaceNpcHostileRel)
            return 2; // hostile last
        return 1;     // neutral middle
    };
    std::sort(snaps.begin(), snaps.end(), [&](const SpaceNpcSnapshot& a, const SpaceNpcSnapshot& b) {
        int ra = rank_of(a), rb = rank_of(b);
        if (ra != rb)
            return ra < rb;
        if (a.name != b.name)
            return a.name < b.name;
        return a.pol_id < b.pol_id;
    });
    std::vector<SpaceNpcUiItem> out;
    int ord = 1;
    for (const auto& s : snaps) {
        if (!s.pol_id)
            continue;
        if (out.size() >= kMaxSpaceNpcUiEntries)
            break;
        float r = std::isfinite(s.rel) ? s.rel : 0.0f;
        SpaceNpcUiItem item;
        item.npc_id = s.pol_id;
        item.label = format_space_npc_label(s, ord++);
        item.allied = s.flag_ally || r >= kSpaceNpcAllyRel;
        item.hostile = s.flag_war || (!item.allied && r <= kSpaceNpcHostileRel);
        item.can_turn = has_rival;
        item.can_protect = item.allied;
        item.can_smooth = r < kSpaceNpcMaxRel;
        out.push_back(std::move(item));
    }
    return out;
}

// ---------------- Space Race Progress & Chronicle Galaxy Core Finale ----------------
struct SpaceRaceStats {
    int32_t money = 0;
    uint16_t colonies = 0;
    uint16_t stars = 0;
    uint16_t allied_npcs = 0;
    float dist_to_core = 0.0f;
};

inline ::rivals::PlayerProgress build_player_progress(uint8_t player_id, const SpaceRaceStats& s)
{
    ::rivals::PlayerProgress p;
    p.player_id = player_id;
    p.present = true;
    p.primary = (float)s.colonies;
    p.secondary = (float)s.money;
    p.stat_a = s.stars;
    p.stat_b = s.allied_npcs;
    p.extra = std::isfinite(s.dist_to_core) && s.dist_to_core >= 0.0f ? s.dist_to_core : 0.0f;
    return p;
}

struct FinaleState {
    bool awarded = false;
    uint8_t first_player = 255;
    uint8_t winner = 255;
    int32_t bonus = kPtsCoreFinale;
    int32_t totals[::rivals::kMaxPlayers] = {};

    void reset()
    {
        awarded = false;
        first_player = 255;
        winner = 255;
        bonus = kPtsCoreFinale;
        for (int i = 0; i < ::rivals::kMaxPlayers; i++)
            totals[i] = 0;
    }

    // Returns true only for the first valid player to enter the galaxy core.
    bool claim_first(uint8_t player_id)
    {
        if (awarded || !::rivals::valid_player(player_id))
            return false;
        awarded = true;
        first_player = player_id;
        return true;
    }

    void snapshot_chronicle(const ::rivals::Chronicle& chr)
    {
        for (int i = 0; i < ::rivals::kMaxPlayers; i++)
            totals[i] = chr.totals[i];
        uint8_t ldr = chr.leader();
        winner = ::rivals::valid_player(ldr) ? ldr : first_player;
    }

    std::string format_summary(const std::string names[::rivals::kMaxPlayers],
                               const bool present[::rivals::kMaxPlayers], bool ru) const
    {
        if (!awarded)
            return "";
        std::string first_n = ::rivals::valid_player(first_player) && !names[first_player].empty()
                                  ? names[first_player]
                                  : ("P" + std::to_string(first_player));
        std::string win_n = ::rivals::valid_player(winner) && !names[winner].empty()
                                ? names[winner]
                                : ("P" + std::to_string(winner));
        char head[220];
        if (ru) {
            snprintf(head, sizeof head,
                     "Финал Хроники (ядро галактики): %s первым(ой) вошёл(ла) в ядро (+%d). Победитель: %s! Очки:",
                     first_n.c_str(), (int)bonus, win_n.c_str());
        } else {
            snprintf(head, sizeof head,
                     "Chronicle Finale (Galaxy Core): %s reached the core first (+%d). Winner: %s! Totals:",
                     first_n.c_str(), (int)bonus, win_n.c_str());
        }
        std::string out = head;
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            if (!present[i] && totals[i] == 0)
                continue;
            std::string nm = names[i].empty() ? ("P" + std::to_string(i)) : names[i];
            char item[96];
            snprintf(item, sizeof item, " %s=%d", nm.c_str(), (int)totals[i]);
            out += item;
        }
        out += ru ? ". Игра продолжается." : ". The game continues.";
        return out;
    }
};

} // namespace spacerivals
