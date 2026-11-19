// BUG-1305 (WP-K of 0.7.16; found by the Lineages fork, PR #196, its BUG-9175): a guest's own mating at the nest never
// finished. The guest's mate (the drama manager's pick near the nest, +0xB58 & 0x10) is one of the host's copies (a
// proxy, npc.cpp): its AI does not run in the guest's game, so it never answers the pairing (its node stays unset,
// mate node FFFFFFFF) and the guest's creature stood in its mating action 02D852F0 for good: no egg scene, no editor
// (lab lane 6, 20261119-111735 guest_mating: 90 s in 02D852F0).
//
// On (`coop.fix creature.guest_mating`): on a guest, when its creature is in the mating action with a host copy as the
// mate and has reached it (within kNear m, or kStall s after the click), the mod ends the pairing as the game's own
// egg scene ends: the creature game's message 0x4D9686F (CreatureGame_HandleMessage 0xD42E50 -> 0xD3C650(1): the drama
// events end, the clock at creature game +0x28; CreatureGame_Update then builds the mating editor's own request
// 0xD3C650(0), tag 0x12191CA), which the Submit hook takes as this guest's own evolution (vote.cpp: its own editor, the
// Evolve now route). Once per mating action. Off: the creature waits in its mating action (0.7.15).
// docs/re/creature.md#creature-guest-mating.
#include "coop_state.h"
#include "drama.h"

namespace coop_core {

static bool g_guest_mating_fix = true;
COOP_FIX("creature.guest_mating", "BUG-1305", g_guest_mating_fix,
         "creature: a guest's own mating at the nest (its mate is one of the host's creatures) ends in its evolution editor instead of waiting for ever");

constexpr uint32_t kMating = 0x02D852F0u, kMatingAtNest = 0xD23FE7CAu; // the avatar's behaviour node (brain +0x1D8)
constexpr uint32_t kMatingDone = 0x4D9686Fu; // the creature game's "mating done" message (docs/re/creature.md#creature-mating-call)
constexpr float kNear = 4.f;      // m: the creature reached its mate
constexpr double kNearFor = 1.0;  // s there (it turns to the mate)
constexpr double kStall = 12.0;   // s in the action at most (the host's own pairing took 12 s to its egg scene)
constexpr double kRequestWait = 8.0; // s for the game's request after the message, else once more

static double g_since = -1, g_near_since = -1, g_sent_at = -1;
static int g_sent; // messages sent in this mating action
static uint32_t g_ended, g_ended_far; // tests (coop.cocoon)

static uint32_t avatar_node(void* av)
{
    if (!av || !skg::readable((uint8_t*)av + 0xB4C, 4))
        return 0;
    uint8_t* br = *(uint8_t**)((uint8_t*)av + 0xB4C);
    return br && skg::readable(br + 0x1D8, 4) ? *(uint32_t*)(br + 0x1D8) : 0;
}

static void send_done(void*)
{
    if (!g_creature_game || !o_CreatureGameMsg || game::mode_id() != modes::kCreature)
        return;
    bool r = o_CreatureGameMsg(g_creature_game, kMatingDone, nullptr);
    LOG("guest mating: the creature game's mating-done message 0x%08X -> %d (BUG-1305)", kMatingDone, (int)r);
}

void guest_mating_tick(double now)
{
    void* av = g_mode == modes::kCreature ? game::avatar() : nullptr;
    const uint32_t node = av ? avatar_node(av) : 0;
    const bool mating = node == kMating || node == kMatingAtNest;
    if (!g_guest_mating_fix || !mating || g_sess.role() != net::Role::Client || !g_sess.connected()) {
        g_since = g_near_since = g_sent_at = -1;
        g_sent = 0;
        return;
    }
    if (g_since < 0)
        g_since = now;
    void* mate = drama::mate();
    if (!mate || !npc::is_proxy(mate)) // (a mate of this game's own runs its own AI: the game's pairing)
        return;
    const float d = net::dist(game::position(av), game::position(mate));
    if (d <= kNear) {
        if (g_near_since < 0)
            g_near_since = now;
    } else
        g_near_since = -1;
    const bool met = g_near_since >= 0 && now - g_near_since >= kNearFor;
    if (g_sent >= 2 || (g_sent == 1 && now - g_sent_at < kRequestWait) || (!met && now - g_since < kStall))
        return;
    if (g_sent == 0) {
        g_ended++;
        if (!met)
            g_ended_far++;
        LOG("guest mating: the mate is one of the host's creatures (its AI runs on the host): the pairing ends here after "
            "%.1f s, %.1f m from it (BUG-1305)", now - g_since, d);
    }
    g_sent++;
    g_sent_at = now;
    H->run_outside_frame(send_done, nullptr); // (the game's own message handler, outside Present)
}

std::string guest_mating_status()
{
    char b[160];
    snprintf(b, sizeof b, "guest mating: fix=%d ended=%u ended_far=%u in_action=%.1f s sent=%d\n", (int)g_guest_mating_fix,
             g_ended, g_ended_far, g_since >= 0 ? H->time_seconds() - g_since : 0.0, g_sent);
    return b;
}

} // namespace coop_core
