// Cell: what the cells do to each other (the guest's acts on the host's pool, the game's damage, poison, electric,
// bite, chunk, kill, break, destroy and eat hooks, the food that comes in, the own cell's finite probes, the
// collision pairs); moved out of cellsync.cpp (C8 of 0.7.15, BUG-1183).
#include "cellsync_internal.h"
#include "own_progress.h"
#include "net/compat.h"

namespace cellsync {

// ---------------- guest -> host: what our avatar did to a cell of the host's pool ----------------
void send_act(uint32_t hidx, uint8_t kind, uint8_t arg, int food, bool plant)
{
    net::Writer w;
    w.u32(hidx);
    w.u8(kind);
    w.u8(arg);
    w.u32((uint32_t)food);
    w.u8(plant ? 1 : 0);
    uint32_t meal = g_meals.sent(food);
    w.u32(meal); // the meal number the host echoes in its total (BUG-0051)
    C.sess->send_reliable_to(0, net::MSG_CELL_EAT, w);
    static const char* kinds[] = {"gone", "kill", "break", "shrink"};
    trace::ev("eat", kind == ACT_GONE ? "sent gone" : kind == ACT_SHRINK ? "sent shrink" : "sent kill", "sent %s c%u food=%d %s meal=%u", kinds[kind % 4], hidx,
              food, plant ? "plant" : "meat", meal);
}

void flush_food()
{
    if (!g_pend_food)
        return;
    if (is_guest()) {
        net::Writer w;
        w.u32((uint32_t)g_pend_food);
        w.u8(g_pend_plant ? 1 : 0);
        uint32_t meal = g_meals.sent(g_pend_food);
        w.u32(meal);
        C.sess->send_reliable_to(0, net::MSG_CELL_FOOD, w);
        trace::ev("eat", "sent food", "sent food +%d %s meal=%u (no host cell with it)", g_pend_food,
                  g_pend_plant ? "plant" : "meat", meal);
    }
    g_pend_food = 0;
}

// the host index of a mirror (guest), 0 when the cell is not one
static uint32_t mirror_key(uint8_t* c)
{
    if (!c || !is_guest())
        return 0;
    auto it = g_host_of.find(F<uint32_t>(c, 0));
    return it != g_host_of.end() ? it->second : 0;
}

// ---------------- hooks ----------------
NoAttack_t& o_NoAttack_ptr();
DamageCell_t o_DamageCell;
void hurt_avatar(int dmg, int type);

// BUG-1008: a guest's hit that kills a host NPC (a mouth's last chunk, type 6, or a hit the game's path did not take)
// kills it in the name of the guest's puppet (KillCell's killer: its species alert 0xE71F00 turns on the puppet, not
// on the host's cell), and a hit on a dead cell is refused (a second KillCell). Off: the host's avatar is the killer.
bool g_fix_chunk_killer = true;
COOP_FIX("cell.chunk_killer", "BUG-1008", g_fix_chunk_killer, "cell: a guest's lethal hit (a mouth chunk) on a host NPC kills it in the name of the guest's puppet, and a dead cell refuses the hit");

// guest: our cell bit the host's cell <hidx> (its mirror here): the host applies it (MSG_CELL_HIT)
void send_hit(uint32_t hidx, int dmg, int type)
{
    net::Writer w;
    w.u32(hidx);
    w.u8((uint8_t)std::max(0, std::min(dmg, 255)));
    w.u8((uint8_t)type);
    C.sess->send_reliable_to(0, net::MSG_CELL_HIT, w);
    g_n.hits_sent++;
    trace::ev("bite", "sent", "sent c%u dmg=%d type=%d", hidx, dmg, type);
}

// BUG-0716 (class BUG-0428): a guest's poison part hurt nothing. Its clouds (0xE76660, made by the poison step
// 0xE76880 for the avatar) are cells its own game simulates: the guest's visibility step 0xE78C00 destroyed each
// in the frame it was made (a poison cell outside the active area: a guest has no active tiles of its own; lab
// 20261108-042625: 101 of 101), and kept alive they met no mirror (lab 20261108-043126). On the host the guest's
// cell is a puppet without AI, so its game ran no poison step for it. Now the host runs the game's own poison
// step (`SporeCoop::Cell_PoisonStep`, ESI = the cell) for every partner's puppet once per cell step, with the
// puppet as the game's avatar for the call (as the NoAttack hook lends it): the clouds come from the puppet's
// parts (the partner's creation), carry +0x110 (the player's), hurt the host's creatures through the game and are
// mirrored to the guests like any cell. The other attackers of a mirror on a guest stay dropped: another
// mirror (the host simulates the pair) and a partner's puppet (its owner's game sends its hits); `coop.pool`
// counts them (`foreign_hits=<mirror>/<puppet>/<other>`). Old (`coop.fix cell.puppet_poison off`): no poison step.
static bool g_fix_puppet_poison = true;
COOP_FIX("cell.puppet_poison", "BUG-0716", g_fix_puppet_poison, "cell: a guest's poison part makes its clouds in the host's game, where they hurt the host's creatures (its own game destroyed them at once)");
uintptr_t a_PoisonStep;
static void poison_step(uint8_t* cell, float dt) // custom convention: ESI = cell, stack float dt, caller pops
{
    uint32_t bits;
    memcpy(&bits, &dt, 4);
    uintptr_t fn = a_PoisonStep;
    __asm__ volatile("pushl %%esi\n\t"
                     "movl %%edi, %%esi\n\t"
                     "pushl %%eax\n\t"
                     "call *%%ebx\n\t"
                     "addl $4, %%esp\n\t"
                     "popl %%esi\n\t"
                     : "+a"(bits)
                     : "b"(fn), "D"(cell)
                     : "ecx", "edx", "memory", "cc");
}
// BUG-0799: a knocked-out cell (held at hp 1, invulnerable, see go_down) still made poison clouds: the game's poison
// step 0xE68630 asks only for a live cell (hp 1 is alive), so the host's own knocked-out avatar made its clouds, and
// the host ran puppet_poison for a knocked-out guest's puppet (its hp is pinned, it looks alive); the clouds hurt and
// killed NPCs for the 10 s and the kills counted as that player's. Now a knocked-out cell makes no clouds: the host's
// own (h_Cloud refuses its avatar's while g_downed) and a knocked-out partner's (no poison step for its puppet).
// Off: 0.7.12 (clouds while knocked out).
static bool g_fix_ko_no_poison = true;
COOP_FIX("cell.ko_no_poison", "BUG-0799", g_fix_ko_no_poison, "cell: a knocked-out cell makes no poison clouds (they hurt and killed creatures for its owner while it was down)");
bool guest_knocked_out(uint32_t from);
void puppet_poison(float dt)
{
    uint8_t* g = G();
    if (!g_fix_puppet_poison || !a_PoisonStep || !is_host() || !in_cell() || !g || !(dt > 0.f) || dt > 1.f)
        return;
    for (int id = 1; id < 4; id++) {
        uint8_t* pc = puppet_cell(id);
        if (!pc || F<uint8_t>(pc, kDead) || F<uint8_t>(pc, kDying) || !F<uint32_t>(pc, kModelKey))
            continue;
        if (g_fix_ko_no_poison && guest_knocked_out((uint32_t)id)) {
            g_n.ko_clouds_blocked++; // BUG-0799: a knocked-out partner's cell makes no clouds
            continue;
        }
        uint32_t& av = F<uint32_t>(g, 0x411C);
        uint32_t keep = av;
        size_t mark = game::swap_open(game::swap_undo_u32, &av, keep, "cell avatar index (puppet poison)"); // BUG-0110
        av = F<uint32_t>(pc, 0);
        poison_step(pc, dt);
        av = keep;
        game::swap_close(mark);
        g_n.puppet_poison_steps++;
    }
}
// BUG-0958: a partner's poison clouds hurt and knocked out another player's cell while friendly fire was off (lab
// 20261112-070110, -062128): the host's game makes every player's clouds (its own avatar's, and a guest's through
// puppet_poison, BUG-0716) and they are mirrored to the guests like any cell; a guest's own game then ran the cloud
// mirror's contact with its own cell (DamageCell, victim = the avatar), and the host's game the contact of a guest's
// cloud with the host's avatar. Now, with friendly fire off and outside Rivals, DamageCell of a player's own cell by a
// partner's poison cloud is refused: on a guest a mirror the host marks as a player's product (state flag 4, +0x110),
// on the host a cloud its game made for a partner's puppet (noted in h_Cloud). The own clouds and the NPCs' are as
// before. Friendly fire on or Rivals: as 0.7.12. `coop.fix cell.partner_poison off`: as 0.7.12.
bool g_fix_partner_poison = true;
COOP_FIX("cell.partner_poison", "BUG-0958", g_fix_partner_poison, "cell: with friendly fire off a partner's poison clouds do not hurt a player's cell");
std::unordered_map<uint32_t, uint8_t> g_partner_clouds; // host: cells its game made as clouds of a partner's puppet -> its id
// BUG-1513 (CELL-13 of the external audit of 0.7.15.5): the host makes a guest's clouds (puppet_poison, +0x110) and
// mirrors them to every guest, the owner too, as "a player's product" (flag 4); the owner's DamageCell spared its cell
// from such a mirror only while players do not hurt each other, so with friendly fire on (or Rivals) a guest's OWN
// clouds (their mirrors: its local cloud is culled at once) hurt and knocked out its own cell. On: the host notes each
// cloud's player and marks the clouds of the guest it sends to (state flag 8, an older guest ignores the bit); the guest
// never takes damage from its own. Off (`coop.fix cell.own_cloud off`): 0.7.16.1.
bool g_fix_own_cloud = true;
COOP_FIX("cell.own_cloud", "BUG-1513", g_fix_own_cloud,
         "cell: a player's own poison clouds never hurt its own cell (with friendly fire on they did, on a guest)");
unsigned g_own_cloud_refused; // tests (BUG-1513): own clouds' hits refused on this guest
char g_own_hit_last[160] = "-";                // tests: the last hit our cell took (attacker kind, origin, type)
uint32_t g_own_hits_n;                         // tests (BUG-1243): DamageCell calls on our own cell by another cell
bool players_hurt_each_other()
{
    return settings::on(settings::kFriendlyFire) || spaceext::rivals() || pfind::g_test_rivals;
}
const char* kind_of(uint8_t* c);
static bool own_product(uint8_t* att) { return att && att != avatar() && F<uint8_t>(att, 0x110) && !modes::cell_is_puppet(att); }
static void note_foreign_hit(uint8_t* att)
{
    if (!att || att == avatar())
        return;
    if (g_host_of.count(F<uint32_t>(att, 0)))
        g_n.foreign_mirror++;
    else if (modes::cell_is_puppet(att))
        g_n.foreign_puppet++;
    else
        g_n.foreign_other++;
}

// tests (BUG-0716): the poison clouds our avatar's poison part makes (0xE76660, cdecl void(cell* maker, int size, ...))
Cloud_t o_Cloud;
// host (BUG-0799): a partner's puppet whose player is knocked out
static bool ko_puppet(uint8_t* c)
{
    if (!is_host() || !modes::cell_is_puppet(c))
        return false;
    for (int id = 1; id < 8; id++)
        if (puppet_cell(id) == c)
            return guest_knocked_out((uint32_t)id);
    return false;
}
bool g_test_no_own_cloud = false; // tests (BUG-0892): our own avatar makes no cloud (a partner's puppet does)
// BUG-1508 (CELL-6 of the external audit of 0.7.15.5): puppet_poison lends a partner's puppet the avatar index for its
// poison step, so inside that step avatar() is the puppet: the knocked-out check `g_downed && maker == avatar()` refused
// every live partner's cloud for the 10 s of the HOST's knockout (counted as ko_clouds_blocked), and clouds_made counted
// the partners' clouds as ours. On: a puppet maker is never "our avatar" there (a knocked-out partner's puppet stays
// refused by ko_puppet). Off (`coop.fix cell.ko_partner_clouds off`): 0.7.16.1.
static bool g_fix_ko_partner_clouds = true;
COOP_FIX("cell.ko_partner_clouds", "BUG-1508", g_fix_ko_partner_clouds,
         "cell: the partners' poison clouds go on while the host's own cell is knocked out (they were all refused)");
unsigned g_partner_clouds_while_down; // tests: partners' clouds made in the host's game while the host was knocked out
static bool our_avatar_cloud(uint8_t* maker)
{
    return maker && maker == avatar() && !(g_fix_ko_partner_clouds && modes::cell_is_puppet(maker));
}
void SK_CDECL h_Cloud(uint8_t* maker, int size, void* at)
{
    if (g_test_no_own_cloud && in_cell() && maker && maker == avatar() && !modes::cell_is_puppet(maker)) {
        // (puppet_poison lends a partner's puppet the avatar index for its step: that cloud is the partner's)
        g_n.own_clouds_held++;
        return;
    }
    if (g_fix_ko_no_poison && in_cell() && maker && ((g_downed && our_avatar_cloud(maker)) || ko_puppet(maker))) {
        // BUG-0799: a knocked-out cell makes no cloud: our own, and a knocked-out partner's puppet (puppet_poison, and
        // the game's own poison step, which also runs for a puppet drawn here with a poison part: lab 20261112-063438)
        g_n.ko_clouds_blocked++;
        return;
    }
    uint32_t live0 = G() ? F<uint32_t>(G(), 0x1C + 0xC) : 0;
    // BUG-0958 (host): the cells this cloud of a partner's puppet adds are that partner's
    bool partner = g_fix_partner_poison && is_host() && in_cell() && maker && modes::cell_is_puppet(maker);
    std::unordered_set<uint32_t> before;
    if (partner)
        for_each_cell([&](uint8_t*, uint32_t idx) { before.insert(idx); });
    o_Cloud(maker, size, at);
    if (partner) {
        if (g_partner_clouds.size() > 1024) // (stale entries: their slots hold other cells now)
            for (auto it = g_partner_clouds.begin(); it != g_partner_clouds.end();) {
                uint8_t* c = modes::cell_at(it->first);
                it = c && F<uint8_t>(c, 0x110) ? std::next(it) : g_partner_clouds.erase(it);
            }
        uint8_t owner = 0; // BUG-1513: whose puppet made it
        for (int id = 1; id < 8 && !owner; id++)
            if (puppet_cell(id) == maker)
                owner = (uint8_t)id;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (!before.count(idx) && c != maker)
                g_partner_clouds[idx] = owner;
        });
    }
    if (in_cell() && maker && modes::cell_is_puppet(maker)) {
        g_n.puppet_clouds++; // host (BUG-0716): a partner's cloud (puppet_poison, or the game's own poison step)
        if (g_downed)
            g_partner_clouds_while_down++; // BUG-1508
    }
    if (in_cell() && our_avatar_cloud(maker)) {
        g_n.clouds_made++;
        g_n.cloud_last = (G() ? F<uint32_t>(G(), 0x1C + 0xC) : 0) - live0; // cells added by it
    }
}

// BUG-0715 (class BUG-0428): the electric attack 0xE71520 (cdecl void(cell* attacker, int, u32* first target),
// from 0xE71980, the avatar's and every NPC's electric part, docs/re/cell.md#cell-electric-attack) chains to up
// to 32 cells near the attacker's electric parts and writes each one's hp itself (`hp -= GetDamageAmount`,
// no DamageCell, no message), then event 2 runs KillIfDead. On a guest the targets are mirrors: their hp came
// back with the host's next state and the host's creatures were never hurt by a guest's electric part (nor
// killed: the local KillIfDead was the mirror's). Now on a guest an attack of our avatar keeps the mirrors' hp
// (no local death) and sends each mirror's loss to the host (MSG_CELL_HIT, type 1: DamageCell plays the same
// animation 0xE); the host applies it through the game (BUG-0714). Old (`coop.fix cell.electric off`): local only.
static bool g_fix_electric = true;
COOP_FIX("cell.electric", "BUG-0715", g_fix_electric, "cell: a guest's electric part hurts the host's creatures (its chain attack is sent to the host, the mirrors keep the host's health)");
Zap_t o_Zap;
uintptr_t a_Zap;
// BUG-1243 (tests): zaps the game ran for a partner's puppet here (none seen: the electric step does not run for a
// puppet cell, lab 20261118-102744; docs/re/cell.md#cell-partner-attack-anim)
void SK_CDECL h_Zap(uint8_t* att, uint32_t a2, uint32_t* first)
{
    if (in_cell() && att && modes::cell_is_puppet(att))
        g_n.puppet_zaps++;
    if (in_cell() && att && att == avatar())
        g_n.zaps++; // our avatar's electric attacks, with the fix or without
    if (!g_fix_electric || !in_cell() || !is_guest() || !att || att != avatar() || g_downed) {
        if (in_cell() && g_downed && att && att == avatar()) {
            g_n.ko_bites_blocked++; // BUG-0509: a knocked-out cell zaps nobody
            return;
        }
        o_Zap(att, a2, first);
        return;
    }
    // the mirrors near us and their hp before the attack (it reaches up to 32 cells in a chain)
    std::vector<std::pair<uint8_t*, int>> around;
    net::Vec3 me = pos_of(att);
    for (auto& [local, hidx] : g_host_of) {
        uint8_t* c = modes::cell_at(local);
        if (c && net::dist(pos_of(c), me) < 60.f)
            around.push_back({c, F<int>(c, kHp)});
    }
    o_Zap(att, a2, first);
    for (auto& [c, hp] : around) {
        int lost = hp - F<int>(c, kHp);
        if (lost <= 0)
            continue;
        F<int>(c, kHp) = hp; // the host decides (its state brings the result)
        auto it = g_host_of.find(F<uint32_t>(c, 0));
        if (it != g_host_of.end()) {
            send_hit(it->second, lost, 1);
            g_n.zap_hits++;
        }
    }
}

// BUG-0350: the guest's bite on a mirror skips the game's DamageCell (the host applies it), and with
// it the health ring DamageCell shows when the avatar bites (ShowHealthRollover 0xE62340, before the
// hp change, docs/re/cell.md#cell-guest-bite-ring): the guest never saw the hp of what it bit. The
// ring is shown here under DamageCell's own conditions (damage > 0, the victim damageable and not
// spared); the hp change and the hurt reaction stay the host's (they come with the state).
// tests: whether a cell is inside the window (world_to_screen)
int on_screen_dbg(uint8_t* c)
{
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    float w[3] = {F<float>(c, kPos), F<float>(c, kPos + 4), F<float>(c, kPos + 8)}, sc[2];
    return H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh;
}

// tests: the game's health ring of cell <idx>: the timer of its entry in the cell UI's rollover pool
// (*cCellUI::_ptr + 0x9C, entries: +4 the cell's pool index, +0x24 a timer the game sets to 0.5),
// -1 when the cell has none (docs/re/cell.md#cell-guest-bite-ring)
float rollover_of(uint32_t idx)
{
    uint8_t* ui = a_CellUI ? *(uint8_t**)a_CellUI : nullptr;
    if (!ui || !idx)
        return -1.f;
    uint8_t* pool = ui + 0x9C;
    uint8_t* data = F<uint8_t*>(pool, 0);
    uint32_t high = F<uint32_t>(pool, 0x10), size = F<uint32_t>(pool, 0x14);
    if (!data || size < 0x28 || high > 4096)
        return -1.f;
    for (uint32_t i = 0; i < high; i++) {
        uint8_t* e = data + (size_t)i * size;
        if ((F<uint32_t>(e, 0) & 0xFFFF) == i && F<uint32_t>(e, 4) == idx)
            return F<float>(e, 0x24);
    }
    return -1.f;
}

const char* g_bite_ring_skip = "-"; // tests: why the last guest bite showed no ring
static void bite_ring(uint8_t* victim, uint8_t* attacker, int dmg)
{
    const char* why = !a_Rollover ? "nosym" : dmg <= 0 ? "dmg" : !F<uint32_t>(victim, 0x248) ? "gfx"
                      : F<int>(victim, kHp) <= 0 ? "hp" : F<uint8_t>(victim, kDead) || F<uint8_t>(victim, kDying) ? "dead"
                      : F<uint8_t>(victim, 0x17C) ? "lock" : F<uint8_t>(victim, kInvuln) ? "invuln"
                      : o_NoAttack_ptr() && o_NoAttack_ptr()(victim, attacker) ? "spared" : nullptr;
    g_bite_ring_skip = why ? why : "-";
    if (why)
        return;
    skg::cdecl_call<void>(a_Rollover, victim, F<int>(victim, kHp));
    g_n.bite_rings++;
}

bool SK_CDECL h_DamageCell(uint8_t* victim, uint8_t* attacker, const float* dir, int type, int x, float dt)
{
    if (!in_cell())
        return o_DamageCell(victim, attacker, dir, type, x, dt);
    if (g_downed && attacker && attacker == avatar() && victim != attacker) {
        g_n.ko_bites_blocked++; // BUG-0509: a knocked-out cell's spike, poison or bite hurts nobody
        return false;
    }
    // Damage to a player's cell is decided on that player's machine: there the host's NPC is a
    // mirror that swims into the real cell, and the guest's own collision step bites it (hurt_local,
    // then its DamageCell / KillCell hooks: knockout, revive). The host's picture of the guest
    // (a puppet, no collision pairs, invulnerable) takes nothing, or the guest would be bitten twice
    // (docs/re/cell.md 9). The guest tells the host which NPC bit it (MSG_CELL_BITTEN) for the
    // NPC's own after-bite behaviour.
    if (is_host() && victim && C.puppet_owner && C.puppet_owner(victim) > 0) {
        g_n.puppet_hits_dropped++;
        return false;
    }
    if (mate_copy_refuse(victim)) // BUG-1432: a partner's mate is its owner's game's
        return false;
    if (victim && victim == avatar() && attacker && attacker != victim) {
        // BUG-0958: a partner's poison cloud (a mirror of the host's that is a player's product, or on the host a
        // cloud of a partner's puppet) never hurts our cell while players do not hurt each other
        auto am = is_guest() ? g_host_of.find(F<uint32_t>(attacker, 0)) : g_host_of.end();
        auto mm = am != g_host_of.end() ? g_mirrors.find(am->second) : g_mirrors.end();
        bool mirror_product = mm != g_mirrors.end() && mm->second.product;
        bool mirror_own = mm != g_mirrors.end() && mm->second.own_product; // BUG-1513
        bool partner_cloud = is_host() && g_partner_clouds.count(F<uint32_t>(attacker, 0)) && F<uint8_t>(attacker, 0x110);
        const char* k = kind_of(attacker);
        g_own_hits_n++;
        snprintf(g_own_hit_last, sizeof g_own_hit_last, "%s_c%u_type=%d_%s%s_hp=%d", k, F<uint32_t>(attacker, 0), type,
                 am != g_host_of.end() ? "mirror" : modes::cell_is_puppet(attacker) ? "puppet" : "local",
                 mirror_product || partner_cloud ? " partner_product" : F<uint8_t>(attacker, 0x110) ? " own_product" : "",
                 F<int>(victim, kHp));
        if (mirror_own && !strcmp(k, "poison") && g_fix_own_cloud) {
            g_own_cloud_refused++; // BUG-1513: our own cloud (the host's mirror of it) never hurts our cell
            return false;
        }
        if ((mirror_product || partner_cloud) && !strcmp(k, "poison")) {
            if (g_fix_partner_poison && !players_hurt_each_other()) {
                g_n.partner_poison_refused++;
                return false;
            }
            g_n.partner_poison_taken++;
        }
    }
    if (is_guest() && victim) {
        auto it = g_host_of.find(F<uint32_t>(victim, 0));
        if (it != g_host_of.end()) { // a mirror: the host applies the bite
            if (attacker == avatar()) {
                int dmg = a_GetDamage ? skg::cdecl_call<int>(a_GetDamage, attacker, victim) : 1;
                send_hit(it->second, dmg, type);
                bite_ring(victim, attacker, dmg);
            } else // tests: the other attackers of a mirror (another mirror, a puppet: their hits are the host's / their owner's)
                note_foreign_hit(attacker);
            return false;
        }
        if (victim == avatar() && attacker && g_host_of.count(F<uint32_t>(attacker, 0)))
            g_n.hurt_local++; // a mirror bit us here: the only way a host NPC hurts us (see above)
    }
    if (victim && victim == avatar()) { // damage our own cell takes (diagnostics: is the guest as exposed?)
        int before = F<int>(victim, kHp);
        bool r = o_DamageCell(victim, attacker, dir, type, x, dt);
        int lost = before - F<int>(victim, kHp);
        if (lost > 0 && !g_downed) {
            g_n.bites_taken++;
            g_n.damage_taken += lost;
        }
        if (lost > 0 && is_guest() && attacker) {
            // a mirror bit us: on the host that NPC only touched our puppet (no bite there), so it
            // would chase us on and on. The host runs its after-bite rest (0xE57930 -> 0xE578B0).
            auto it = g_host_of.find(F<uint32_t>(attacker, 0));
            if (it != g_host_of.end()) {
                net::Writer w;
                w.u32(it->second);
                C.sess->send_reliable_to(0, net::MSG_CELL_BITTEN, w);
                g_n.bitten_sent++;
            }
        }
        return r;
    }
    if (victim && attacker && attacker == avatar()) { // our own bites on the host's cells (diagnostics)
        int before = F<int>(victim, kHp);
        bool r = o_DamageCell(victim, attacker, dir, type, x, dt);
        g_n.own_bites++;
        if (F<int>(victim, kHp) < before || F<uint8_t>(victim, kDying))
            g_n.own_hits++;
        return r;
    }
    return o_DamageCell(victim, attacker, dir, type, x, dt);
}

// A carnivore's mouth does not bite through DamageCell: the event dispatcher 0xE7E130 runs
// EatChunkA 0xE7DD90 (event 0x13) / EatChunkB 0xE7DBD0 (0x14), cdecl void(u32 eaterIdx,
// u32 victimIdx, u32 ev+0x30, u32 ev+0x3C, float dt): the eater gets the food (OnEat), the victim
// loses GetDamageAmount(eater, victim) hp and KillIfDead may kill it (docs/re/cell.md 10). On the
// guest the victim is a mirror: its hp came back with the next state of the host, the host never
// heard of the bite (the guest could not hurt the host's creatures with a mouth), and a lethal
// chunk killed the mirror only here. The guest now sends the bite to the host as MSG_CELL_HIT
// (type 6, as the host's own chunk bites) and keeps the mirror alive until the host decides.
Chunk_t o_ChunkA, o_ChunkB;

static void chunk(Chunk_t orig, uint32_t eater, uint32_t victim, uint32_t a, uint32_t b, float dt)
{
    uint8_t* e = in_cell() ? modes::cell_at(eater) : nullptr;
    uint8_t* v = e ? modes::cell_at(victim) : nullptr;
    if (!v) {
        orig(eater, victim, a, b, dt);
        return;
    }
    if (g_downed && e == avatar()) { // BUG-0364: a knocked-out mouth does not bite
        g_n.ko_eats_blocked++;
        return;
    }
    if (mate_copy_refuse(v)) // BUG-1432: no chunk off a partner's mate (it fed the eater)
        return;
    if (e == avatar() && !is_guest())
        g_n.own_chunks++;
    auto it = is_guest() ? g_host_of.find(victim) : g_host_of.end();
    if (g_mirror_fixes && it != g_host_of.end() && e == avatar()) {
        int dmg = a_GetDamage ? skg::cdecl_call<int>(a_GetDamage, e, v) : 1;
        send_hit(it->second, dmg, 6);
        g_n.chunks_sent++;
        // the bite shows here (food, health ring, push), but the mirror does not die here
        int hp = F<int>(v, kHp);
        if (hp - dmg < 1)
            F<int>(v, kHp) = dmg + 1;
        orig(eater, victim, a, b, dt);
        F<int>(v, kHp) = std::max(1, hp - dmg);
        return;
    }
    int before = victim == avatar_idx() ? F<int>(v, kHp) : 0;
    orig(eater, victim, a, b, dt);
    if (victim == avatar_idx() && F<int>(v, kHp) < before && !g_downed) {
        g_n.bites_taken++;
        g_n.damage_taken += before - F<int>(v, kHp);
        if (is_guest()) { // a mirror bit a chunk off us: the host's NPC rests after it (as for a bite)
            auto at = g_host_of.find(eater);
            if (at != g_host_of.end()) {
                g_n.chunks_taken++;
                net::Writer w;
                w.u32(at->second);
                C.sess->send_reliable_to(0, net::MSG_CELL_BITTEN, w);
                g_n.bitten_sent++;
            }
        }
    }
}
void SK_CDECL h_ChunkA(uint32_t e, uint32_t v, uint32_t a, uint32_t b, float dt) { chunk(o_ChunkA, e, v, a, b, dt); }
void SK_CDECL h_ChunkB(uint32_t e, uint32_t v, uint32_t a, uint32_t b, float dt) { chunk(o_ChunkB, e, v, a, b, dt); }

KillCell_t o_KillCell;
std::unordered_map<uintptr_t, uint32_t> g_destroy_callers; // diagnostics: who destroyed a mirror
bool SK_CDECL h_KillCell(uint8_t* c, uint32_t killer, void* res, bool by_player, int type, float dt)
{
    if (uint32_t hidx = !g_replay && g_mirror_fixes && in_cell() ? mirror_key(c) : 0) {
        // Our avatar killed a cell of the host's pool (a lethal bite, the chunks a mouth tears off
        // 0xE7DD90 / 0xE7DBD0 -> KillIfDead, ...). Killed here, its death and its meat (KillCell's loot)
        // stayed on this machine: the loot was purged as cells the host does not have, the host's
        // creature lived on. The host kills it; its death and loot come back with the state.
        g_n.mirror_kills++;
        g_destroy_callers[((uintptr_t)__builtin_return_address(0) - H->rebase(0)) | 0x20000000u]++;
        if (!F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead)) {
            if (kill_send_first(hidx, H->time_seconds())) {
                send_act(hidx, ACT_KILL, (uint8_t)type, 0, false);
                g_n.kills_sent++;
            } else
                g_n.kills_dup_sent++; // BUG-0632: killed already, the host decides
        }
        return false;
    }
    if (mate_copy_refuse(c)) // BUG-1432: a partner's mate does not die here (its loot was free meat)
        return false;
    if (in_cell() && go_down(c))
        return false;
    if (g_in_guest_attack) // BUG-0714: a guest's attack replayed through the game is a player's kill
        by_player = true;
    bool npc = in_cell() && c && c != avatar() && !modes::cell_is_puppet(c) && !F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead);
    bool r = o_KillCell(c, killer, res, by_player, type, dt);
    if (npc && F<uint8_t>(c, kDying))
        g_n.npc_deaths++;
    return r;
}

Break_t o_Break;
void SK_CDECL h_Break(uint8_t* victim, uint8_t* attacker, float f)
{
    if (g_downed && in_cell() && attacker && attacker == avatar() && victim != attacker) {
        g_n.ko_bites_blocked++; // BUG-0509: nor kills
        return;
    }
    if (uint32_t hidx = g_mirror_fixes && in_cell() ? mirror_key(victim) : 0) {
        // Bite 0xE7B0A0 breaks the victim instead of damaging it when the bite is lethal: effect, then
        // (event 5 0xE7ADD0) its loot and DestroyCell(idx, 1). On a mirror all of it happened here only
        // (the meat stayed on the guest and was purged as stray, the host never saw the kill): the
        // host breaks its own cell (ACT_BREAK), with the same death and loot, mirrored back.
        g_n.mirror_kills++;
        g_destroy_callers[((uintptr_t)__builtin_return_address(0) - H->rebase(0)) | 0x10000000u]++;
        int st = F<int>(victim, 0x18C);
        bool lethal = attacker && attacker == avatar() && st != 7 && st != 8 && st != 9 && !F<uint8_t>(victim, kDead);
        if (lethal && !kill_send_first(hidx, H->time_seconds())) {
            g_n.kills_dup_sent++; // BUG-0632: we broke it already, the host's death comes with its state
        } else if (lethal) {
            effect(victim, "cell_death_break");
            send_act(hidx, ACT_BREAK, 0, 0, false);
            g_n.kills_sent++;
            g_n.breaks_sent++;
        }
        return;
    }
    if (mate_copy_refuse(victim)) // BUG-1432: the lethal bite's BreakOrSwallow (Bite asks no +0x111 there)
        return;
    if (in_cell() && go_down(victim))
        return;
    if (in_cell() && victim && victim != avatar() && !modes::cell_is_puppet(victim))
        g_n.npc_deaths++;
    o_Break(victim, attacker, f);
}

// BUG-0635: RemoveCellsOfOldScale 0xE7CD10 (a level change) on the host destroys every cell outside the HOST's visible box (0xE5CDE0: the sphere
// pos +0x4C, 1.7 x size +0x58 against Simulator::Cell::ViewBoxes, the background ones by the frustum)
// and repopulates the tiles: the cells a partner had on its screen vanished there and new ones came
// at other places (a churn after every level-up, a single player keeps what is in its view). With
// cell.levelup_keep the host's wipe keeps a cell inside any partner's visible box as it keeps the
// ones in its own (the box +-10 x +-7.5 around the partner's cell, with a margin for its camera).
// Off: 0.7.9 (only the host's view is kept).
static bool g_fix_levelup_keep = true;
COOP_FIX("cell.levelup_keep", "BUG-0635", g_fix_levelup_keep, "cell: a level change on the host keeps the cells on a partner's screen, as a single player keeps the cells in its view (they vanished on the guest and new ones came)");
bool g_in_wipe; // host: inside RemoveCellsOfOldScale
std::unordered_map<uint32_t, AreaMark> g_area_mark;
float g_centre_err_max, g_centre_err_moving; // tests (BUG-1064): the farthest a partner's view centre was from its cell (settled / scale moving)
AreaCull g_area_cull[4];
uint32_t g_sim_frame, g_boxes_frame, g_area_skips0; // h_WorldScale counts SimUpdates; pop_tiles notes its box build
void area_cull_note(uint8_t area, uint8_t* c);
uintptr_t a_CullRet; // DestroyCell's return address in the cell update after ShouldCullCell (0xE7C3E6)
// host: is the cell's sphere inside a partner's visible box (as 0xE5CDE0 tests the host's own box)?
static bool in_partner_view(uint8_t* c)
{
    float r = 1.7f * F<float>(c, kSize);
    if (!std::isfinite(r))
        return false;
    for (int id = 1; id < 4; id++) {
        uint8_t* pc = puppet_cell(id);
        if (!pc)
            continue;
        float dx = std::fabs(F<float>(c, kPos) - F<float>(pc, kPos)), dy = std::fabs(F<float>(c, kPos + 4) - F<float>(pc, kPos + 4));
        if (dx <= 15.f + r && dy <= 11.f + r)
            return true;
    }
    return false;
}
Destroy_t o_Destroy;
std::map<uintptr_t, uint32_t> g_dbg_destroy_by; // RE (BUG-0355): DestroyCell calls by return address
// Guest: the cells our avatar touched lately (HandleCollisionPair with the avatar), local index -> time.
// Only these can have been eaten or swallowed by us.
std::unordered_map<uint32_t, double> g_touched;
// BUG-0827: SwallowWhole 0xE7D880 of a cell with a model (every creature) plays "cell_death_eaten" and destroys the
// cell only at the end of that animation (event 7), long after the touch that started it: h_Destroy took the
// destroy of a mirror our avatar swallowed for the game's own (not touched within 1 s), sent no ACT_GONE and the
// host's cell lived on (eaten again) while the meal still came. Now the swallow marks the mirror (local index ->
// time) and its destroy in the next 10 s counts as eaten. Off: 0.7.12.
static bool g_fix_swallow_gone = true;
COOP_FIX("cell.swallow_gone", "BUG-0827", g_fix_swallow_gone, "cell: a host cell a guest swallows whole is gone in the host's game too (it lived on and could be eaten again)");
std::unordered_map<uint32_t, double> g_swallowed;
const char* kind_of(uint8_t* c);
std::unordered_map<int, void*> g_puppet_seen;
std::unordered_map<uint32_t, SentCell> g_sent_at;
double g_sent_pruned;
// BUG-0480: host cells destroyed less than 2 s after their first send, by the caller of DestroyCell
std::map<uintptr_t, uint32_t> g_quick_destroy;

std::map<uintptr_t, uint32_t> g_product_destroys; // tests (BUG-0716): who destroys our poison clouds, by return address
uintptr_t a_PoisonCullRet; // the visibility step's DestroyCell of a poison cell outside the active area (0xE78C37)
uint32_t g_mate_gone_by;
double g_mate_gone_at = -1;
void SK_CDECL h_Destroy(uint32_t idx, bool loot, float f, int x)
{
    if (in_cell() && idx && idx == mate_idx() && modes::cell_at(idx)) { // tests (BUG-1186): who removes our mate
        g_mate_gone_by = (uint32_t)((uintptr_t)__builtin_return_address(0) - H->rebase(0));
        g_mate_gone_at = H->time_seconds();
    }
    if (in_cell() && is_guest()) {
        uint8_t* pc = modes::cell_at(idx);
        uintptr_t ra = (uintptr_t)__builtin_return_address(0);
        if (pc && pc != avatar() && F<uint8_t>(pc, 0x110) && !g_host_of.count(idx) && !modes::cell_is_puppet(pc)) {
            // tests (BUG-0716): a guest's game has no active tiles of its own, so the visibility step 0xE78C00
            // destroys every poison cloud of its avatar in the frame it is made ("outside the active area",
            // lab 20261108-042625: 101 of 101 at 0xE78C37); kept, they still met no mirror (lab 20261108-043126).
            // The host makes the guest's clouds instead (puppet_poison)
            g_n.products_kept += a_PoisonCullRet && ra == a_PoisonCullRet;
            if (g_product_destroys.size() < 32)
                g_product_destroys[ra - H->rebase(0)]++;
        }
    }
    if (is_host() && in_cell()) {
        auto st = g_sent_at.find(idx);
        if (st != g_sent_at.end() && H->time_seconds() - st->second.first < 2.0) {
            g_n.quick_destroys++;
            g_quick_destroy[(uintptr_t)__builtin_return_address(0) - H->rebase(0)]++;
        }
    }
    if (in_cell()) {
        // A partner's cell is ours to remove (modes::cell_despawn forgets it before it destroys it).
        // The game removed it on a level change (RemoveCellsOfOldScale), when an NPC broke or swallowed
        // it, ...: the partner blinked out and in again with an "appeared in the world" message each
        // time (13 times in 10 minutes). Its owner decides what happens to it.
        uint8_t* c = modes::cell_at(idx);
        if (c && modes::cell_is_puppet(c)) {
            uintptr_t ra = (uintptr_t)__builtin_return_address(0) - H->rebase(0);
            g_destroy_callers[ra | 0x80000000u]++;
            g_n.puppet_saves++;
            F<uint8_t>(c, kDead) = 0;
            F<uint8_t>(c, kDying) = 0;
            return;
        }
    }
    if (g_in_wipe && is_host()) { // BUG-0635: the host's level change and a partner's screen
        uint8_t* c = modes::cell_at(idx);
        if (c && c != avatar() && !modes::cell_is_puppet(c)) {
            bool in_view = in_partner_view(c);
            if (in_view && g_fix_levelup_keep) {
                g_n.wipe_kept++;
                return;
            }
            g_n.wipe_destroyed++;
            g_n.wipe_near_lost += in_view;
        }
    }
    if (is_guest() && !g_our_destroy && in_cell()) {
        auto it = g_host_of.find(idx);
        if (it != g_host_of.end()) {
            uintptr_t ra = (uintptr_t)__builtin_return_address(0);
            auto t = g_touched.find(idx);
            bool touched = t != g_touched.end() && H->time_seconds() - t->second <= 1.0;
            // BUG-0827: a mirror our avatar swallowed whole is destroyed at the end of its "eaten" animation (event 7)
            auto sw = g_swallowed.find(idx);
            bool swallowed = g_fix_swallow_gone && sw != g_swallowed.end() && H->time_seconds() - sw->second <= 10.0;
            if (sw != g_swallowed.end())
                g_swallowed.erase(sw);
            touched = touched || swallowed;
            g_destroy_callers[touched ? ((ra - H->rebase(0)) | 0x40000000u) : ra]++;
            if (!touched) {
                // Not our doing: the game removes it for its own reasons (a poison cloud outside our
                // active area 0xE78C00, the cells of the old scale on a level change 0xE7CD10, the area
                // cleared around a hatching cell 0xE78B20, size 0). It is the host's cell: the host
                // decides, the mirror stays until the host's state drops it. Reported, these made
                // the host destroy cells nobody had eaten (docs/stability/cell.md).
                g_n.kept++;
                return;
            }
            // we ate it (or swallowed it): the host removes it the same way (with its loot, if the
            // game asked for loot) and adds what we got for it, once (not if someone ate it first)
            send_act(it->second, ACT_GONE, loot ? 1 : 0, g_pend_food, g_pend_plant);
            if (++g_gone_sends[it->second] > 1)
                g_n.gone_again++; // BUG-0636: the same host cell eaten again (a mirror made again)
            if (g_gone_sends.size() > 512)
                g_gone_sends.clear();
            g_eaten.first(it->second, H->time_seconds(), g_fix_eat_once);
            g_pend_food = 0;
            g_n.eats_sent++;
            g_mirrors.erase(it->second);
            g_host_of.erase(it);
            if (t != g_touched.end())
                g_touched.erase(t);
            loot = false;
        }
    }
    // RE (BUG-0355): does the game's DestroyCell give the cell's soft-body particles back?
    uint8_t* dc = modes::cell_at(idx);
    uint8_t* dparts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    int dn = dc ? F<int>(dc, 0x250) : 0, before = dparts ? F<int>(dparts, 0x1BD54) : 0;
    bool dgfx = dc && F<uint32_t>(dc, 0x248);
    if (dc)
        g_dbg_destroy_by[(uintptr_t)__builtin_return_address(0)]++;
    if (!g_area_mark.empty()) {
        auto am = g_area_mark.find(idx);
        if (am != g_area_mark.end() && !am->second.by) {
            am->second.by = (uint32_t)((uintptr_t)__builtin_return_address(0) - H->rebase(0));
            if (dc && a_CullRet && (uintptr_t)__builtin_return_address(0) == a_CullRet)
                area_cull_note(am->second.area, dc);
        }
    }
    o_Destroy(idx, loot, f, x);
    if (dc && dparts && dn > 1) {
        g_n.dbg_destroys++;
        g_n.dbg_expect += (uint32_t)dn;
        g_n.dbg_freed += (uint32_t)std::max(0, before - F<int>(dparts, 0x1BD54));
        g_n.dbg_nogfx += !dgfx;
        g_n.dbg_alive += modes::cell_at(idx) != nullptr;
    }
}

// BUG-0364: the knockout holds our cell in place at hp 1 (downed_tick), but the game's contact
// events still ran: food floating into it was eaten (event 0x12, EatFood 0xE7DF40 -> OnEat ->
// AvatarEat -> AddFoodPoints), so the cell came back from its respawn bigger than "small again".
// A knocked-out cell eats nothing: the food stays where it is for whoever comes.
// BUG-1514 (CELL-14 of the external audit of 0.7.15.5): EatFood shrinks the plant one level in place (TargetSize times
// TileSizes[L - 1] / TileSizes[L], +0x358 = L - 1, 0xE7DFDB) unless the level below is out of the view's range (then it
// destroys it, which a guest sends as ACT_GONE). A guest's bite on a plant mirror shrank only its own mirror and sent
// nothing but the points: the host's plant stayed whole (the partners ate the same food again) and the guest's mirror
// kept its lower level. On: the guest sends ACT_SHRINK with the new level (to a host of cell 1.2 or newer) and its
// points with it; the host shrinks its plant the game's way. Off (`coop.fix cell.plant_shrink off`): 0.7.16.1.
bool g_fix_plant_shrink = true;
COOP_FIX("cell.plant_shrink", "BUG-1514", g_fix_plant_shrink,
         "cell: a plant a guest bites shrinks in the host's game too (it stayed whole there: the same food eaten twice)");
unsigned g_shrinks_sent, g_shrinks_old_host, g_shrinks_applied, g_shrinks_refused; // tests (BUG-1514)
static bool host_reads_shrink()
{
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess && C.sess->host_families(has, f) &&
           net::compat::has_minor(f, has, (uint8_t)net::Family::cell, 1, kCellMinorShrink);
}
bool shrink_plant(uint8_t* c, int new_level)
{
    const float* sizes = a_TileSizes ? (const float*)a_TileSizes : nullptr;
    float k = 1.f;
    if (!c || !cell_logic::plant_shrink(F<int>(c, kLevel), new_level, sizes, k)) {
        g_shrinks_refused++;
        return false;
    }
    F<float>(c, kTargetSize) *= k;
    F<int>(c, kLevel) = new_level;
    if (F<float>(c, 0x1F0) > 0.f)
        F<float>(c, 0x1F0) = 0.f;
    g_shrinks_applied++;
    return true;
}
EatFood_t o_EatFood;
void SK_CDECL h_EatFood(uint32_t eater, uint32_t food, void* ref, float dt)
{
    if (g_downed && in_cell() && eater == avatar_idx() && avatar()) {
        g_n.ko_eats_blocked++;
        return;
    }
    // BUG-1514: our bite on a plant mirror (a host's plant)
    uint32_t hidx = 0;
    int level = 0;
    if (g_fix_plant_shrink && is_guest() && in_cell() && eater == avatar_idx())
        if (auto it = g_host_of.find(food); it != g_host_of.end())
            if (uint8_t* c = modes::cell_at(food)) {
                hidx = it->second;
                level = F<int>(c, kLevel);
            }
    o_EatFood(eater, food, ref, dt);
    if (!hidx)
        return;
    uint8_t* c = modes::cell_at(food);
    auto it = g_host_of.find(food);
    if (!c || it == g_host_of.end() || it->second != hidx || F<int>(c, kLevel) != level - 1)
        return; // eaten whole (DestroyCell: ACT_GONE went), or not shrunk
    if (!host_reads_shrink()) {
        g_shrinks_old_host++; // (an older host would take the act for a kill: its plant stays whole, as before)
        return;
    }
    send_act(hidx, ACT_SHRINK, (uint8_t)(level - 1), g_pend_food, g_pend_plant);
    g_pend_food = 0;
    g_shrinks_sent++;
}

// BUG-0746: Simulator::Cell::ExecuteInteraction 0xE7E7F0, cdecl void(interaction*, float dt): +0 eater idx (A),
// +4 the other (B), +8 kind. Kinds 9 / 10 of our knocked-out avatar as the eater become kind 1 (contact push only).
ExecInter_t o_ExecInter;
uintptr_t a_ExecInter;
void SK_CDECL h_ExecInter(uint32_t* in, float dt)
{
    if (g_fix_ko_no_swallow && g_downed && in && (in[2] == 9 || in[2] == 10) && in_cell() && avatar() &&
        in[0] == avatar_idx()) {
        g_n.ko_swallows_blocked++;
        uint32_t k = in[2];
        in[2] = 1;
        o_ExecInter(in, dt);
        in[2] = k;
        return;
    }
    o_ExecInter(in, dt);
}

// BUG-0827: Simulator::Cell::SwallowWhole 0xE7D880, cdecl void(cell* eater, cell* food, int foodType), plain ret
Swallow_t o_Swallow;
void SK_CDECL h_Swallow(uint8_t* eater, uint8_t* food, int type)
{
    if (mate_copy_refuse(food)) // BUG-1432: nobody swallows a partner's mate whole
        return;
    if (food && eater && in_cell() && is_guest() && eater == avatar() && g_host_of.count(F<uint32_t>(food, 0))) {
        g_swallowed[F<uint32_t>(food, 0)] = H->time_seconds();
        g_n.swallows_marked++;
        if (g_swallowed.size() > 256)
            g_swallowed.clear();
    }
    o_Swallow(eater, food, type);
}

AddFood_t o_AddFood;
// BUG-0633 / BUG-0316: AddFoodPoints called outside the cell step (a partner's meal or the host's total from the
// network, a console command; all on the main thread between steps) that crosses a world scale line (food 200, ...)
// leaves the avatar's lock bytes +0x16C / +0x178 / +0x17B / +0x17C set for good with the animation +0x18C back at
// 0 (the level-up animation 0x1D, 0xE72B40 -> 0xE599B0, never ends there); 0xE7B630 then collects no part token
// for the rest of the session, on both games after one shared level-up. The same level-up by a meal inside the
// step (AvatarEat) clears them within ~1.5 s. With cell.food_in_step the food changes from outside the game's
// own step are queued and applied at the start of the next cell step (h_WorldScale, first in SimUpdate 0xE7F3A0),
// where a meal would be; a change that waits longer than 0.5 s (no step: paused, away) is applied as before.
// Off: 0.7.9 (applied at once).
static bool g_fix_food_in_step = true;
COOP_FIX("cell.food_in_step", "BUG-0633 BUG-0316", g_fix_food_in_step, "cell: food from a partner or the host's total is added inside the cell step, so a level-up over a world scale line does not leave the avatar's locks set (no part token could be collected)");
std::vector<std::pair<double, std::function<void()>>> g_food_q;
bool in_cell();
void food_in_step(std::function<void()> f)
{
    if (!g_fix_food_in_step || !in_cell()) {
        f();
        return;
    }
    g_food_q.push_back({H->time_seconds(), std::move(f)});
}
double g_test_food_hold_until; // tests (BUG-1431): no drain until then, as while the host's cell step does not run
void food_drain(bool late_only)
{
    if (g_food_q.empty())
        return;
    double now = H->time_seconds();
    if (now < g_test_food_hold_until)
        return;
    if (late_only && now - g_food_q.front().first < 0.5)
        return;
    auto q = std::move(g_food_q);
    g_food_q.clear();
    for (auto& e : q)
        e.second();
}
void SK_CDECL h_AddFood(int amount, bool plant)
{
    // BUG-1469 (own progress, cell.level_together): an own meal over the next level line while a partner is still below
    // it waits (banked) until that partner gets there (the one pool is populated at the host's level); the network's
    // totals, the released bank and the test setups come with the food guard or the bypass
    if (!g_food_guard && amount > 0 && in_cell() && !dna_pace::g_bypass) {
        if (uint8_t* s = S()) {
            amount = own_progress::clip_meal(amount, F<int32_t>(s, 0x1C));
            if (amount <= 0)
                return;
        }
    }
    // BUG-0510: an own meal (AvatarEat 0xE7D070 -> AddFoodPoints) enters the shared food at the
    // party's pace (amount / N, the remainder kept; coop.cpp dna_pace_meal). The guest's game applies
    // and sends its share, the host adds a guest's meal whole: scaled once, where it was eaten
    if (!g_food_guard && amount > 0 && in_cell() && !dna_pace::g_bypass && C.pace_meal) {
        amount = C.pace_meal(amount);
        if (amount <= 0)
            return; // its share is still below one point (kept for the next meal)
    }
    uint8_t* a0 = in_cell() ? avatar() : nullptr;
    float before = a0 ? F<float>(a0, kTargetSize) : 0.f;
    o_AddFood(amount, plant);
    if (!g_food_guard && amount > 0 && in_cell()) { // our own avatar ate (AvatarEat 0xE7D070), not the network
        g_n.own_eats++;
        g_n.own_food += amount;
        uint8_t* s0 = S();
        trace::ev("eat", "own", "own cell ate +%d %s (total %d)", amount, plant ? "plant" : "meat",
                  s0 ? F<int32_t>(s0, 0x1C) : -1);
    }
    uint8_t* a = in_cell() ? avatar() : nullptr;
    if (a && a == a0 && g_body < 1.f) {
        // we ate: the small body grows back (50 points = full size). A meal over a level line made
        // the game resize the avatar for the new level inside AddFoodPoints (SetCellSize): that size
        // is the full one, not the small one (BUG-0041: it was divided by g_body again, up to 2x).
        float after = F<float>(a, kTargetSize);
        cell_logic::Body b = cell_logic::regrow(before, after, g_body, amount, g_fix_bodylevel);
        if (b.target != after && after > 0) {
            float k = b.target / after;
            F<float>(a, kTargetSize) = b.target;
            F<float>(a, 0xB8) *= k * k; // mass ~ size^2, as set_body
            F<float>(a, 0xBC) /= k * k;
        }
        g_body = b.factor;
        g_body_size = b.target;
    }
    if (g_solo && is_guest() && !g_food_guard && amount != 0 && in_cell()) {
        g_solo_food[plant ? 1 : 0] += amount; // BUG-0205: kept until the host is back
        return;
    }
    if (is_guest() && !g_food_guard && amount != 0 && in_cell()) {
        // AvatarEat runs before the food is removed (EatFood 0xE7DF40: OnEat, then DestroyCell):
        // h_Destroy takes it with the eaten cell, the tick sends what is left
        if (g_pend_food && g_pend_plant != plant)
            flush_food();
        g_pend_food += amount;
        g_pend_plant = plant;
    }
}

// Guest: interactions between two cells of the host's pool (mirror eats mirror food, mirror
// swallows mirror, the host's cell bumps into a mirror) are the host's business: it simulates them
// and the result arrives with the next state. Run here they made the guest report "eaten" to the
// host for cells nobody on the guest had touched, and the host destroyed them (docs/stability/cell.md).
const char* kind_of(uint8_t* c);
// BUG-0307 diagnostics: where in the frame our own cell's state first stops being finite. The probes
// run at the start of the cell step (WorldScaleStep), around the own cell's collision pairs, after its
// soft-body step and after our guest tick; the first probe that sees a non-finite field logs the
// field mask, the probe and the last probe that saw it finite, with what was near.
bool fin(const float* v, int n)
{
    for (int i = 0; i < n; i++)
        if (!std::isfinite(v[i]))
            return false;
    return true;
}
unsigned cell_nonfinite(uint8_t* c)
{
    unsigned m = 0;
    if (!c)
        return 0;
    m |= fin((float*)(c + 0x4C), 3) ? 0 : NF_POS;
    m |= fin((float*)(c + 0x84), 3) ? 0 : NF_PREV;
    m |= fin((float*)(c + 0x90), 3) ? 0 : NF_VEL;
    m |= fin((float*)(c + 0x1C4), 3) ? 0 : NF_ACC;
    m |= fin((float*)(c + 0x5C), 9) ? 0 : NF_ROT;
    m |= fin((float*)(c + 0x24), 9) ? 0 : NF_SHAPE;
    m |= fin((float*)(c + 0x58), 1) && fin((float*)(c + 0xB4), 1) ? 0 : NF_SIZE;
    m |= fin((float*)(c + 0xB8), 2) ? 0 : NF_MASS; // mass, inverse mass
    int n = F<int>(c, 0x250);
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (F<uint32_t>(c, 0x248) && parts && n > 1 && n <= 64)
        for (int i = 0; i < n; i++) {
            uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
            if (k >= 1500)
                continue;
            m |= fin((float*)(parts + 4 + 12 * (size_t)k), 3) ? 0 : NF_PPOS;
            m |= fin((float*)(parts + 0x4654 + 12 * (size_t)k), 3) ? 0 : NF_PVEL;
            m |= fin((float*)(parts + 0x11944 + 12 * (size_t)k), 3) ? 0 : NF_PGOAL;
        }
    return m;
}
static const char* g_own_ok_at = "-";
static unsigned g_own_nf;          // the mask at the last probe
uint32_t g_shared_parts;    // own particle indices also held by another cell (last scan)
static double g_shared_scan_at;
float g_last_dt;
void own_probe(const char* where, uint8_t* other)
{
    uint8_t* av = in_cell() ? avatar() : nullptr;
    if (!av)
        return;
    unsigned m = cell_nonfinite(av);
    if (!m) {
        g_own_nf = 0;
        g_own_ok_at = where;
        return;
    }
    if (g_own_nf) {
        g_own_nf = m;
        return;
    }
    g_own_nf = m;
    if (++g_n.own_nf_events > 20)
        return;
    // what was near: cells within 3 sizes, non-finite cells there, own particles shared with others
    auto* p = (float*)(av + 0x84); // the previous position (the current one may be NaN)
    float size = F<float>(av, kSize), r = std::isfinite(size) ? std::max(3.f * size, 2.f) : 2.f;
    int near_n = 0, near_bad = 0, zero_size = 0, shared = 0;
    std::unordered_set<uint32_t> own;
    int n = F<int>(av, 0x250);
    if (F<uint32_t>(av, 0x248) && n > 1 && n <= 64)
        for (int i = 0; i < n; i++)
            own.insert(F<uint32_t>(av, 0x254 + 4 * i));
    for_each_cell([&](uint8_t* c, uint32_t) {
        auto* q = (float*)(c + 0x4C);
        int cn = F<int>(c, 0x250);
        if (!own.empty() && F<uint32_t>(c, 0x248) && cn > 1 && cn <= 64)
            for (int i = 0; i < cn; i++)
                shared += own.count(F<uint32_t>(c, 0x254 + 4 * i)) ? 1 : 0;
        if (!std::isfinite(q[0]) || !std::isfinite(q[1])) {
            near_bad++;
            return;
        }
        if (fin(p, 2) && std::hypot(q[0] - p[0], q[1] - p[1]) < r) {
            near_n++;
            zero_size += F<float>(c, kSize) <= 1e-4f;
        }
    });
    char ob[160] = "";
    if (other)
        snprintf(ob, sizeof ob, " other=c%u %s pos=(%.2f %.2f) size=%.3f nf=0x%X dist=%.4f", F<uint32_t>(other, 0), kind_of(other),
                 F<float>(other, 0x4C), F<float>(other, 0x50), F<float>(other, kSize), cell_nonfinite(other),
                 std::hypot(F<float>(other, 0x4C) - p[0], F<float>(other, 0x50) - p[1]));
    H->log(M, SK_WARN,
           "coop: cell: own cell not finite (BUG-0307) at %s, finite at %s: mask=0x%X prev=(%.2f %.2f) size=%.3f dt=%.4f "
           "particles=%d near=%d (size 0: %d) non-finite cells=%d shared particles=%d mirrors=%u%s",
           where, g_own_ok_at, m, p[0], p[1], size, g_last_dt, n, near_n, zero_size, near_bad, shared,
           (unsigned)g_mirrors.size(), ob);
}
// own particle indices that another live cell holds too (once a second on the guest; logged when > 0)
void shared_parts_scan(double now)
{
    if (now - g_shared_scan_at < 1.0)
        return;
    g_shared_scan_at = now;
    uint8_t* av = avatar();
    int n = av ? F<int>(av, 0x250) : 0;
    if (!av || !F<uint32_t>(av, 0x248) || n < 2 || n > 64)
        return;
    std::unordered_set<uint32_t> own;
    for (int i = 0; i < n; i++)
        own.insert(F<uint32_t>(av, 0x254 + 4 * i));
    uint32_t shared = 0, holder = 0;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        int cn = F<int>(c, 0x250);
        if (!F<uint32_t>(c, 0x248) || cn < 2 || cn > 64)
            return;
        for (int i = 0; i < cn; i++)
            if (own.count(F<uint32_t>(c, 0x254 + 4 * i))) {
                shared++;
                holder = idx;
            }
    });
    if (shared && !g_shared_parts)
        H->log(M, SK_WARN, "coop: cell: %u of our own %d soft-body particles are held by another cell too (c%u) (BUG-0307)", shared, n,
               holder);
    g_shared_parts = shared;
}

uint32_t g_stack_idx; // tests: `coop.pool stack`
uint32_t g_stack2_a, g_stack2_nf;
std::vector<uint32_t> g_stack2; // the cells (the first is g_stack2_a)
float g_stack2_vmax;
int g_stack2_frames;
int g_stack_frames;
void stack_pair(uint8_t* av, uint8_t* other);
net::Vec3 first_sphere(uint8_t* c);
Pair_t o_Pair;
const char* kind_of(uint8_t* c);
// what our cell does to creatures (tests: how often a player gets to bite): collision pairs of our
// cell with a creature, interaction events of kind 10 our cell started (Bite 0xE7B0A0)
Bite_t o_Bite;
bool SK_CDECL h_Bite(uint32_t att, uint32_t vic, void* p, void* d, int x, float dt)
{
    if (in_cell() && att == avatar_idx()) {
        if (g_downed) { // BUG-0509: a knocked-out mouth does not bite
            g_n.ko_bites_blocked++;
            return false;
        }
        g_n.bite_events++;
    }
    return o_Bite(att, vic, p, d, x, dt);
}

// BUG-0714 (class BUG-0428): the host applied a guest's bites by hand: a copy of DamageCell's gate and
// `hp -= dmg` (MSG_CELL_HIT), `hp = 0` and BreakOrSwallow (ACT_BREAK). What the game does around a bite was
// lost: the bitten creature's hurt animation (PlayAnimation 0x10 and its event 2, whose end runs the
// after-hurt reaction), Bite's hurt marks (+0x178 on both cells), the victim's and the attacker's AI
// reactions (0xE57910 / 0xE576F0), and Bite's own rule for a lethal bite (BreakOrSwallow only when the
// damage reaches the victim's hp in the host's game, docs/re/cell.md#cell-guest-hit-game). Now the host
// replays the guest's attack through the game's own function with the guest's puppet as the attacker:
// a hit (MSG_CELL_HIT: a bite's non-lethal DamageCell, spikes, electric, poison) through DamageCell 0xE7A7C0,
// the lethal bite (ACT_BREAK) through Bite 0xE7B0A0; the damage is the guest's (GetDamageAmount answers it for that pair, the
// puppet's own table value is not the guest's) and a kill counts as the player's (KillCell's byPlayer).
// A mouth's chunk (type 6) stays as it was: EatChunk itself writes the hp (no gate, no animation).
// A knocked-out guest's attack is refused for every message (KO_OUT; before, only MSG_CELL_HIT).
// Old (`coop.fix cell.guest_hit_game off`): the hand-made path.
bool g_fix_guest_hit_game = true;
COOP_FIX("cell.guest_hit_game", "BUG-0714", g_fix_guest_hit_game, "cell: the host applies a guest's bite or hit through the game's Bite / DamageCell with the guest's cell as the attacker (hurt animation, the game's lethal-bite rule), not a hand-made hp change");
static struct {
    uint8_t* att = nullptr;
    uint8_t* vic = nullptr;
    int dmg = 0;
} g_forced;                         // the damage GetDamageAmount answers for the guest's attack being replayed
uint32_t g_guest_attack_types[8]; // tests
GetDamage_t o_GetDamage;
int SK_CDECL h_GetDamage(uint8_t* att, uint8_t* vic)
{
    if (g_forced.att && att == g_forced.att && vic == g_forced.vic)
        return g_forced.dmg;
    return o_GetDamage(att, vic);
}
// host: the guest's cell pc attacked our cell c with dmg of damage type t (lethal: the guest's game broke
// it). False when the game's functions are missing (the caller then takes the old path).
bool guest_attack(uint32_t from, uint8_t* c, uint8_t* pc, int dmg, int t, bool lethal)
{
    if (!g_fix_guest_hit_game || !c || !pc || !o_DamageCell || !o_Bite || !o_GetDamage)
        return false;
    net::Vec3 cp = pos_of(c), pp = pos_of(pc);
    float dx = cp.x - pp.x, dy = cp.y - pp.y, l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
    float dir[3] = {dx / l, dy / l, 0}, at[3] = {cp.x, cp.y, cp.z};
    int hp0 = F<int>(c, kHp);
    g_forced.att = pc;
    g_forced.vic = c;
    g_forced.dmg = lethal ? std::max(dmg, hp0) : dmg;
    {
        callguard::Hold in_guest_attack(g_in_guest_attack);
        // A MSG_CELL_HIT is what the guest's DamageCell decided (a bite's non-lethal part, spikes, a test's hit on any
        // kind): DamageCell here, whose gate spares what IsDamageable refuses (BUG-0240). The lethal bite (ACT_BREAK)
        // is Bite's other branch: Bite here, with the damage raised to the victim's hp.
        if (lethal) // Bite negates its direction for DamageCell (it is the bite's, from the victim)
            o_Bite(F<uint32_t>(pc, 0), F<uint32_t>(c, 0), at, dir, 0, 1.f / 30.f);
        else {
            float back[3] = {-dir[0], -dir[1], 0};
            o_DamageCell(c, pc, back, t, 0, 1.f / 30.f);
        }
    }
    g_forced.att = g_forced.vic = nullptr;
    g_n.guest_attacks++;
    g_guest_attack_types[(lethal ? 4 : t) & 7]++;
    bool died = F<uint8_t>(c, kDying) || F<uint8_t>(c, kDead) || F<int>(c, kHp) <= 0;
    bool hurt = died || F<int>(c, kHp) < hp0;
    g_n.guest_attacks_spared += !hurt;
    g_n.guest_attack_kills += died;
    trace::ev("bite", hurt ? "applied" : "spared", "from %u c%u (%s) dmg=%d type=%d%s through the game: hp %d -> %d%s anim=%X", from,
              F<uint32_t>(c, 0), kind_of(c), dmg, t, lethal ? " lethal" : "", hp0, F<int>(c, kHp), died ? " killed" : "",
              F<uint32_t>(c, 0x1B0));
    return true;
}
// host: a knocked-out guest bites nothing (BUG-0509): an old guest, or an attack in flight
bool guest_knocked_out(uint32_t from)
{
    auto ko = g_ko.find(from);
    return ko != g_ko.end() && ko->second.state == KO_OUT;
}
void SK_CDECL h_Pair(uint8_t* a, uint8_t* b, float dt)
{
    if (in_cell() && a && b && (a == avatar() || b == avatar()) && !strcmp(kind_of(a == avatar() ? b : a), "creature"))
        g_n.creature_pairs++;
    if (in_cell() && (modes::cell_is_puppet(a) || modes::cell_is_puppet(b) || mate_copy_cell(a) || mate_copy_cell(b))) {
        // a partner's cell is a picture of a cell simulated on its owner's machine: here nothing eats
        // it, swallows it or is eaten by it (the owner's own game does that, with our cells mirrored)
        g_n.pairs_skipped++;
        return;
    }
    if (is_guest() && in_cell() && !g_solo) { // BUG-0205: our own pool while the host is away: every pair
        uint8_t* av = avatar();
        if (a != av && b != av) {
            g_n.pairs_skipped++;
            return;
        }
        uint8_t* other = a == av ? b : a;
        if (other) {
            double now = H->time_seconds();
            g_touched[F<uint32_t>(other, 0)] = now;
            if (g_touched.size() > 256) // forget old touches
                for (auto it = g_touched.begin(); it != g_touched.end();)
                    it = now - it->second > 1.0 ? g_touched.erase(it) : std::next(it);
        }
    }
    uint8_t* av = avatar();
    bool mine = av && (a == av || b == av);
    if (mine)
        g_n.own_pairs++;
    if (g_stack2_frames > 0 && (F<uint32_t>(a, 0) == g_stack2_a || F<uint32_t>(b, 0) == g_stack2_a))
        g_n.stack2_pairs++;
    if (mine)
        stack_pair(av, a == av ? b : a);
    if (mine)
        own_probe("before a collision", a == av ? b : a);
    o_Pair(a, b, dt);
    if (mine)
        own_probe("collision", a == av ? b : a);
}

// ---- the Cell audit fixes of 0.7.17 (BUG-1507..1514): their switches and counters, one line of `coop.pool` ----
unsigned g_partner_scaled;
extern unsigned g_breaking_refused, g_shadow_counted, g_elsewhere_spend_held, g_elsewhere_spend_refused, g_owed_dropped,
    g_mate_elsewhere_refused, g_mate_elsewhere_held;
extern bool g_fix_breaking_dying, g_fix_shadow_empty;
std::string audit_status()
{
    char b[520];
    snprintf(b, sizeof b,
             "cell_audit: elsewhere_spend=%d held=%u refused=%u owed_dropped=%u mate_held=%u mate_refused=%u "
             "ko_partner_clouds=%d partner_clouds_down=%u breaking=%d breaking_refused=%u shadow_empty=%d shadow_counted=%u "
             "partner_scaled=%u own_cloud=%d own_cloud_refused=%u plant_shrink=%d shrinks_sent=%u shrinks_old_host=%u "
             "shrinks_applied=%u shrinks_refused=%u\n",
             (int)g_fix_elsewhere_spend, g_elsewhere_spend_held, g_elsewhere_spend_refused, g_owed_dropped, g_mate_elsewhere_held,
             g_mate_elsewhere_refused, (int)g_fix_ko_partner_clouds, g_partner_clouds_while_down, (int)g_fix_breaking_dying,
             g_breaking_refused, (int)g_fix_shadow_empty, g_shadow_counted, g_partner_scaled, (int)g_fix_own_cloud,
             g_own_cloud_refused, (int)g_fix_plant_shrink, g_shrinks_sent, g_shrinks_old_host, g_shrinks_applied, g_shrinks_refused);
    return b;
}

} // namespace cellsync
