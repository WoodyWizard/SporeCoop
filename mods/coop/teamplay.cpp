#include "teamplay.h"

#include <cmath>
#include <cstdio>
#include <map>

#include "fixes.h"
#include "game.h"
#include "hunt_hits.h"
#include "net/interp.h"
#include "revive.h"
#include "settings.h"
#include "trace.h"

namespace teamplay {

static const SkHost* H;
static Ctx C;
static bool g_on = true;
static double g_last_rescue = -1000, g_last_regen = 0;
static uint32_t g_joint_hits = 0, g_rescues = 0;
static uint32_t g_hits_host = 0, g_hits_partners = 0; // diagnostics: hits counted by on_player_hit (play_creature)

static HitTable g_hits(kHuntWindow); // target -> recent hits by players (BUG-0099)
COOP_FIX("team.hunt_hits", "BUG-0099", g_hits.fix, "creature: the joint-hunt hit table forgets old hits of every target");

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    C = std::move(ctx);
    // the session settings creature.pack_rescue / pack_heal / joint_hunt decide each bonus (coop.ini teamplay is
    // their default); g_on is the console's switch for all of them (`coop.team off`)
}

bool enabled() { return g_on; }

void forget_target(void* target)
{
    if (g_hits.fix)
        g_hits.forget(target);
}

// BUG-0330: the rescue cooldown (kRescueCooldown) outlived the session and the world: a player that
// left and joined again (or loaded another world) within a minute of a rescue had none in the new
// session (the warm `team` runs failed "rescue counted (1 -> 1)", the guest died of the lethal hit).
// `coop.fix team.rescue_reset off`: the cooldown runs on (0.7.3.x).
static bool g_rescue_reset = true;
COOP_FIX("team.rescue_reset", "BUG-0330", g_rescue_reset, "creature: a new session or world starts with the pack's rescue ready");
void reset()
{
    if (g_hits.fix)
        g_hits.clear();
    if (g_rescue_reset)
        g_last_rescue = -1000;
}

size_t hit_targets() { return g_hits.size(); }
void set_enabled(bool on) { g_on = on; }

float on_player_hit(void* target, uint8_t player, float damage)
{
    if (!g_on || !target || !settings::on(settings::kJointHunt))
        return damage;
    bool joint = g_hits.hit(target, player, H->time_seconds());
    (player ? g_hits_partners : g_hits_host)++;
    // BUG-0618: every hit the joint table counted (support logs: "why no pack bonus"; the play bots' hunt timeline)
    trace::line("team", "hit %p by player %u dmg %.1f hp %.1f%s", target, (unsigned)player, damage, game::health(target),
                joint ? " joint" : "");
    if (!joint)
        return damage;
    g_joint_hits++;
    return damage * kHuntBonus;
}

static std::string nearest_partner(void* avatar, float max_dist, float* out_dist = nullptr)
{
    if (!C.partners || !avatar)
        return {};
    net::Vec3 me = game::position(avatar);
    std::string best;
    float bd = max_dist;
    for (auto& [name, p] : C.partners()) {
        float d = net::dist(me, p);
        if (d <= bd) {
            bd = d;
            best = name;
        }
    }
    if (out_dist)
        *out_dist = bd;
    return best;
}

// the pack rescue first, the revive after it (WP-R1, BUG-1472; owner question 4 of docs/plan-0.7.17.md, provisional:
// the order of revive_logic.h catch_hit)
float on_own_damage(void* avatar, float damage)
{
    if (!avatar || damage <= 0)
        return damage;
    if (revive::guard(avatar, damage)) // down, or just revived: no hit lands (creature.revive_hold)
        return damage;
    if (pack_rescue(avatar, damage))
        return damage;
    revive::down_now(avatar, damage); // a lethal hit the pack did not take, a partner in the world: down (creature.revive)
    return damage;
}

// BUG-0099: a lethal hit next to a partner leaves 40 % health, once a minute; true: it saved us (damage changed)
bool pack_rescue(void* avatar, float& damage)
{
    if (!g_on || !avatar || damage <= 0 || !settings::on(settings::kPackRescue))
        return false;
    float hp = game::health(avatar);
    if (hp <= 0 || hp - damage > 0)
        return false;
    double now = H->time_seconds();
    if (now - g_last_rescue < kRescueCooldown)
        return false;
    std::string partner = nearest_partner(avatar, kRescueDist);
    if (partner.empty())
        return false;
    g_last_rescue = now;
    g_rescues++;
    // the hit lands, but the pack pulls us up: 40 % health afterwards
    float mx = game::max_health(avatar), keep = 0.4f * mx;
    float before = std::fmin(mx, keep + damage);
    game::set_health(avatar, before);
    if (C.notice)
        C.notice(partner + " saved " + C.my_name() + " from death");
    damage = before - keep;
    return true;
}

void tick(void* avatar, double now)
{
    if (!g_on || !avatar || now - g_last_regen < 1.0 || !settings::on(settings::kPackHeal) || revive::down())
        return; // (a downed creature stays at 1 health: revive.cpp holds it)
    g_last_regen = now;
    float hp = game::health(avatar), mx = game::max_health(avatar);
    if (hp <= 0 || hp >= mx || nearest_partner(avatar, kRegenDist).empty())
        return;
    game::set_health(avatar, std::fmin(mx, hp + kRegenPerSec * mx));
}

std::string status()
{
    if (!g_on)
        return "";
    void* av = game::avatar();
    float d = 0;
    std::string p = av ? nearest_partner(av, kRescueDist, &d) : std::string();
    double now = H->time_seconds();
    char b[200];
    int cd = (int)(kRescueCooldown - (now - g_last_rescue));
    snprintf(b, sizeof b, "Pack: %s  |  rescue %s  |  joint hits %u, rescues %u  |  hits host %u, partners %u",
             p.empty() ? "alone" : (p + (d <= kRegenDist ? " close (regen)" : " near")).c_str(),
             cd > 0 ? (std::to_string(cd) + "s").c_str() : "ready", g_joint_hits, g_rescues, g_hits_host, g_hits_partners);
    return b;
}

} // namespace teamplay
