// WP-D1 (BUG-1473): the host's party difficulty; see party_difficulty.h, docs/03-gameplay.md#gameplay-party-difficulty
#include "party_difficulty.h"

#include <cstdio>
#include <cstring>

#include "fixes.h"
#include "settings.h"

namespace party {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

// `coop.fix core.party_tuning off`: the twin of coop_party_difficulty.py (the hits are the game's own at any setting)
static bool g_fix = true;
COOP_FIX("core.party_tuning", "BUG-1473", g_fix,
         "all stages: all.party_difficulty scales the hits between players and NPCs on the host (3-4 player difficulty)");

static int g_logged = 100;
static uint32_t g_to_npc = 0, g_to_player = 0;     // hits scaled (diagnostics)
static double g_dealt_npc = 0, g_game_npc = 0;     // the damage dealt to NPCs, and what the game's would have been
static double g_dealt_player = 0, g_game_player = 0;

int percent_now()
{
    if (!g_fix || !S || S->role() != net::Role::Host || !S->connected())
        return 100;
    int pct = percent(settings::get(settings::kPartyDifficulty), (int)S->players().size());
    if (pct != g_logged && H) {
        H->log(M, SK_INFO, "party difficulty: %d %% now (%s, %d players)", pct,
               settings::value_text(settings::kPartyDifficulty, settings::get(settings::kPartyDifficulty)).c_str(),
               (int)S->players().size());
        g_logged = pct;
    }
    return pct;
}

float hit(float damage, Side victim, Side attacker)
{
    int pct = percent_now();
    float d = scale(damage, victim, attacker, pct);
    if (d != damage) {
        if (victim == Side::Npc) {
            g_to_npc++;
            g_dealt_npc += d;
            g_game_npc += damage;
        } else {
            g_to_player++;
            g_dealt_player += d;
            g_game_player += damage;
        }
    }
    return d;
}

float player_hits_npc(float damage) { return hit(damage, Side::Npc, Side::Player); }
float npc_hits_player(float damage) { return hit(damage, Side::Player, Side::Npc); }

std::string status()
{
    char b[320];
    int choice = settings::get(settings::kPartyDifficulty);
    snprintf(b, sizeof b,
             "party difficulty: percent=%d setting=%s players=%d switch=%s\n"
             "  hits on npcs scaled=%u dealt=%.1f game=%.1f; hits on players scaled=%u dealt=%.1f game=%.1f\n",
             percent_now(), settings::value_text(settings::kPartyDifficulty, choice).c_str(),
             S ? (int)S->players().size() : 0, g_fix ? "on" : "off", g_to_npc, g_dealt_npc, g_game_npc, g_to_player,
             g_dealt_player, g_game_player);
    return b;
}

static int command(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "reset"))
        g_to_npc = g_to_player = 0, g_dealt_npc = g_game_npc = g_dealt_player = g_game_player = 0;
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    H->command_add(M, "coop.party", "coop.party [reset] - the host's party difficulty (all.party_difficulty, BUG-1473): "
                   "the percentage in force and the hits it scaled", command, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace party
