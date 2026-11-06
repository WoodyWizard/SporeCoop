#include "adventure.h"

#include <windows.h>
#include "game.h"
#include "modes.h"
#include "sporekit.h"
#include "sporekit_game.hpp"

extern const SkHost* g_host_for_modes;

namespace adventure {
namespace {
net::AdventureGate offer;
uint32_t partner_visit, current_world;
uint32_t serial = GetTickCount() ^ (GetCurrentProcessId() << 16);
int goals_before;
void* current_play;
uint32_t queued;

// One reachable object, required once, with no parallel goals. Leave every other
// objective to the scenario. Spore's goal-credit routine updates the native summary.
uint8_t* single_gate(uint8_t* play)
{
    uintptr_t first = *(uintptr_t*)(play + 0x64), end = *(uintptr_t*)(play + 0x68);
    if (!first || end <= first || end - first != 0x1AC || !skg::readable((void*)first, 0x1AC))
        return nullptr;
    auto g = (uint8_t*)first;
    return !g[0] && *(int*)(g + 4) == 0 && *(int*)(g + 0x24) == 1 &&
           *(int*)(g + 0x28) >= 0 && *(int*)(g + 0x30) == 1 ? g : nullptr;
}

void give_credit(void* arg)
{
    uint32_t visit = (uint32_t)(uintptr_t)arg;
    if (visit != queued || visit != offer.visit)
        return;
    queued = 0;
    auto play = modes::adventure_play();
    if (!play || play != current_play || !game::avatar() ||
        modes::current_place(modes::for_mode(modes::kScenario)) != current_world ||
        *(int*)(play + 0x90) != 3 || *(uint32_t*)(play + 0xB8) != offer.act)
        return;
    uint8_t* goal = single_gate(play);
    auto credit = g_host_for_modes->sym("SporeCoop::ScenarioCreditGoal");
    if (!credit || !goal || *(uint32_t*)(goal + 0x28) != offer.target)
        return;
    offer.visit = partner_visit = 0; // the native credit cannot be sent back as a new arrival
    skg::thiscall<void>(credit, play, goal, nullptr, nullptr);
}
}

void reset()
{
    offer = {};
    partner_visit = current_world = queued = 0;
    current_play = nullptr;
}

net::AdventureGate outgoing() { return offer; }

void tick(uint32_t world, const net::PlayerState* peer)
{
    auto play = modes::adventure_play();
    int act, state, completed;
    if (!world || !play || !modes::adventure_progress(act, state, completed) || state != 3 || act < 0) {
        reset();
        return;
    }
    if (current_world != world || current_play != play) {
        reset();
        current_world = world;
        current_play = play;
    }
    if (offer.visit && offer.act != (uint32_t)act) {
        if (peer && partner_visit && peer->gate.visit == partner_visit &&
            (uint32_t)act == offer.act + 1 && completed == goals_before + 1)
            offer.credit = partner_visit;
        offer.visit = partner_visit = queued = 0;
    }
    if (!peer) {
        reset();
        return;
    }
    auto goal = single_gate(play);
    if (!goal)
        return; // a completed goal can stay in the old act for a frame
    uint32_t target = *(uint32_t*)(goal + 0x28);
    if (!offer.visit || offer.target != target) {
        if (++serial == 0) ++serial;
        offer.visit = serial;
        offer.act = (uint32_t)act;
        offer.target = target;
        goals_before = completed;
        partner_visit = queued = 0;
    }
    if (peer->gate.visit && peer->gate.act == offer.act && peer->gate.target == target)
        partner_visit = peer->gate.visit;
    if (partner_visit && peer->gate.credit == offer.visit && !queued) {
        queued = offer.visit;
        g_host_for_modes->run_outside_frame(give_credit, (void*)(uintptr_t)queued);
    }
}
}
