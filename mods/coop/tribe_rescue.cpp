// BUG-9278 (Lineages port, from PR #196's tribe.cpp: the founder rescue of its BUG-9118 / BUG-9135 and the party wipe of
// its BUG-9155): a player's downed last villager, its revive and the party wipe. docs/re/tribe.md#tribe-founder-rescue.
//
// Host: a lethal blow on the last living member of a player's village (the host's, seat 0, or a partner's own village
// of tribe_villages.cpp) while another player of the party is here with a village of their own does not kill it: it goes
// down where it stands (1 health, no behaviour, no orders, no blows taken or dealt) for 60 s (tribe.down_60). A living
// villager of another player's village that stays within 15 m of it for 1.5 s revives it (40 % health). Nobody came, or
// its player held G for a second (a guest's game sends CMD_GIVE_UP): the whole party pays (tribe.party_wipe): the newest
// win of the campaign is undone (the stage's goal history record, a step of the goal bar, the top piece of every player
// village's totem, an ally becomes a rival again), or with no win yet every player village loses 10 % of its food; the
// villager gets up at its village with 40 % health. When a win was lost the host then saves, sends every guest the
// world and reloads it (tribe.wipe_reload) so every game redraws the totem and the goal history (Lineages reloaded after
// every wipe; a food loss shows through wsync's food rows). The guests see the countdown over the downed villager from
// MSG_TRIBE_ANIM kind K_RESCUE (an older guest drops the kind and sees the notices only).
// Not ported from Lineages: the hut's own "village down" window, the capture of an unrevived village, the party wipe of
// the population event with every player village empty (its BUG-9147) and the red lost step on the goal bar.
#include "tribe_internal.h"
#include "tribe_rescue_logic.h"

#include <array>

#include "give_up.h"
#include "party.h"

namespace tribe {

using tribe_rescue::Window;
using tribe_rescue::kMaxSeats;

static bool g_fix = true;
COOP_FIX("tribe.founder_rescue", "BUG-9278", g_fix,
         "tribe: a player's last villager goes down instead of dying while a partner is here; a partner's villager revives it");
static bool g_wipe = true;
COOP_FIX("tribe.party_wipe", "BUG-9278", g_wipe,
         "tribe: a downed last villager nobody revived costs the party its newest win (or 10% food); off: it gets up at home for 10% of its village's food");
static bool g_minute = true;
COOP_FIX("tribe.down_60", "BUG-9278", g_minute, "tribe: a downed last villager waits a minute for a partner (off: 30 s)");
static bool g_wipe_reload = true;
COOP_FIX("tribe.wipe_reload", "BUG-9278", g_wipe_reload,
         "tribe: after a party wipe that took a win the host saves, sends the world and reloads it (every game redraws the totem)");

// ---- host ----
static std::array<Window, kMaxSeats> g_win;
static std::array<net::Vec3, kMaxSeats> g_anchor{};
static bool g_any_open;        // a window is open (the cheap gate of rescue_downed: every behaviour tick asks)
static double g_test_give_up;  // coop.tribe rescue giveup (the own downed villager, host or guest): asked until then
static double g_lines_next;
static bool g_lines_open;      // the guests were told about an open window (an empty list follows its end)
static double g_refresh_until, g_refresh_next;
static uint32_t g_downs, g_revives, g_timeouts, g_gave_up, g_wipes, g_setbacks, g_blows_held, g_lines_sent, g_refreshes;
static std::string g_last = "-";
// ---- guest ----
struct Remote {
    tribe_rescue::Line l;
    double until = 0;
};
static std::vector<Remote> g_remote;
static double g_give_up_sent;
static uint32_t g_lines_rx, g_lines_bad, g_give_ups_sent;

static void note(const std::string& text, bool notice)
{
    g_last = text;
    H->log(M, SK_INFO, "tribe rescue: %s", text.c_str());
    if (notice && C.notice)
        C.notice(text);
}

static uint8_t seat_player(int seat) { return seat == 0 ? C.sess->my_id() : party::player_of_seat(seat); }
static bool seat_here(int seat) { return seat == 0 || seat_player(seat) < 0xFE; }
static std::string seat_name(int seat)
{
    const uint8_t p = seat_player(seat);
    return p < 0xFE ? C.sess->name_of(p) : "Seat " + std::to_string(seat);
}

bool rescue_downed(void* c)
{
    if (!g_any_open || !c)
        return false;
    HEntry* e = hm_of(c);
    if (!e)
        return false;
    for (const auto& w : g_win)
        if (w.open() && w.member == e->id)
            return true;
    return false;
}

static bool alive_member(void* c) { return game::health(c) > 0 && !F<uint8_t>(c, 0xB5E) && !rescue_downed(c); }

// Lineages intact_village: not conquered, a hut with health that is this village's
static bool intact(void* t)
{
    void* hut = t && skg::readable((uint8_t*)t + 0x368, 4) ? F<void*>(t, 0x368) : nullptr;
    return t && !F<uint8_t>(t, 0x556) && hut && game::listed(hut) && skg::readable((uint8_t*)hut + 0x230, 4) &&
           F<float>(hut, 0x158) > 0 && F<void*>(hut, 0x230) == t && F<uint32_t>(hut, 0x30) == F<uint32_t>(t, 0x30);
}

// another present player's village has a living member (`partner`: such a village exists at all)
static bool helper_alive(int seat, bool* partner)
{
    bool any = false, alive = false;
    for (int s = 0; s < (int)kMaxSeats; ++s) {
        void* v = s != seat && seat_here(s) ? seat_village(s) : nullptr;
        if (!v)
            continue;
        any = true;
        for (void* c : members(v))
            alive |= alive_member(c);
    }
    if (partner)
        *partner = any;
    return alive;
}

static double down_time() { return tribe_rescue::down_seconds(g_minute); }

bool rescue_protect(void* victim, float damage, uint32_t attacker)
{
    if (!g_fix || !is_host() || !active() || !victim)
        return false;
    HEntry* e = hm_of(victim);
    if (!e)
        return false;
    const double now = H->time_seconds();
    for (const auto& w : g_win)
        if (w.member == e->id && (w.open() || now < w.grace_until)) { // down now, or just up: the blow is not dealt
            g_blows_held++;
            return true;
        }
    void* t = F<void*>(victim, kOwnerTribeOff);
    const int seat = founder_seat(t);
    tribe_rescue::DownIn in;
    in.fix = g_fix;
    in.seat = seat >= 0 && seat < (int)kMaxSeats;
    if (!in.seat)
        return false;
    in.player_here = seat_here(seat);
    in.damage = damage;
    in.health = game::health(victim);
    in.dead = F<uint8_t>(victim, 0xB5E) != 0;
    if (!tribe_rescue::lethal(in.damage, in.health) || in.dead) // (the common case: no walk below)
        return false;
    in.intact = intact(t);
    for (void* c : members(t))
        in.others_living |= c != victim && alive_member(c);
    in.helper_alive = helper_alive(seat, &in.partner_village);
    in.party_wipe = g_wipe;
    if (!tribe_rescue::should_down(in))
        return false;
    Window& w = g_win[seat];
    w = {};
    w.member = e->id;
    w.faction = F<uint32_t>(t, 0x30);
    w.until = now + down_time();
    g_anchor[seat] = game::position(victim);
    g_any_open = true;
    g_lines_next = 0; // the guests hear it at once
    g_downs++;
    game::set_health(victim, 1);
    game::stop(victim);
    char b[300];
    snprintf(b, sizeof b, "%s's last villager is down: bring a partner's villager to it within %d s, or %s (hold G to give up).",
             seat_name(seat).c_str(), (int)down_time(),
             g_wipe ? "the party loses its newest win" : "it gets up at home for 10% of its food");
    note(b, true);
    H->log(M, SK_INFO, "tribe rescue: seat %d member #%u down (blow %.1f on %.1f health, attacker %08X, helper alive %d)", seat,
           e->id, damage, in.health, attacker, (int)in.helper_alive);
    return true;
}

// ---- the party wipe ----
struct GoalRecord {
    uintptr_t vt;
    int kind; // 1 conquered, 2 allied
    void* tribe;
    uintptr_t zero;
};
static_assert(sizeof(GoalRecord) == 16, "tTribeGoalData");

static bool goal_history(void*& ui, GoalRecord*& b, GoalRecord*& e)
{
    static uintptr_t get = H->sym("SporeCoop::cTribeUI_Get");
    ui = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!ui || !skg::readable((uint8_t*)ui + 0x40, 12))
        return false;
    b = F<GoalRecord*>(ui, 0x40);
    e = F<GoalRecord*>(ui, 0x44);
    if (b == e)
        return true;
    return b && e > b && e - b <= 64 && skg::readable(b, (e - b) * sizeof(GoalRecord));
}

static void* cplayer()
{
    void* nm = game::noun_manager();
    void* p = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? F<void*>(nm, 0x74) : nullptr;
    return p && skg::readable((uint8_t*)p + 0x10F0, 8) ? p : nullptr;
}

static std::string wins_line()
{
    void* ui;
    GoalRecord *b, *e;
    if (!goal_history(ui, b, e))
        return "goal history: unreadable";
    std::string out = "goal history: " + std::to_string(e - b) + " win(s)";
    for (GoalRecord* r = b; r < e; ++r) {
        char line[96];
        snprintf(line, sizeof line, " [%s %08X]", r->kind == 1 ? "conquered" : r->kind == 2 ? "allied" : "kind?",
                 r->tribe && game::listed(r->tribe, kTribeNoun) ? F<uint32_t>(r->tribe, 0x30) : 0u);
        out += line;
    }
    if (void* p = cplayer()) {
        char line[64];
        snprintf(line, sizeof line, "; goal bar %.1f / %.1f", F<float>(p, 0x10F0), F<float>(p, 0x10F4));
        out += line;
    }
    for (int s = 0; s < (int)kMaxSeats; ++s) {
        void* v = seat_village(s);
        void* totem = v ? F<void*>(v, 0x19D0) : nullptr;
        if (totem && game::listed(totem) && skg::readable((uint8_t*)totem + 0x11C, 0x1C)) {
            char line[64];
            snprintf(line, sizeof line, "; seat %d totem %u piece(s)", s, (unsigned)((F<uintptr_t>(totem, 0x120) - F<uintptr_t>(totem, 0x11C)) / 4));
            out += line;
        }
    }
    return out;
}

// the top piece off every player village's totem that has `wins` pieces (village +0x19D0 the totem: its piece kinds
// +0x11C / +0x120, u32 each, and colours +0x130 / +0x134, 12 bytes each; the game redraws it on the reload)
static int drop_totem_piece(size_t wins)
{
    int n = 0;
    for (int s = 0; s < (int)kMaxSeats; ++s) {
        void* v = seat_village(s);
        void* totem = v ? F<void*>(v, 0x19D0) : nullptr;
        if (!totem || !game::listed(totem) || !skg::readable((uint8_t*)totem + 0x11C, 0x1C))
            continue;
        const uintptr_t kinds = F<uintptr_t>(totem, 0x11C), kinds_end = F<uintptr_t>(totem, 0x120);
        const uintptr_t colours = F<uintptr_t>(totem, 0x130), colours_end = F<uintptr_t>(totem, 0x134);
        if (kinds_end < kinds || colours_end < colours ||
            !tribe_rescue::totem_pop_ok(kinds_end - kinds, colours_end - colours, wins))
            continue;
        F<uintptr_t>(totem, 0x120) = kinds_end - 4;
        F<uintptr_t>(totem, 0x134) = colours_end - 12;
        n++;
    }
    return n;
}

// `redraw`: a win was lost (the totem and the goal history of the guests' games come back with the world refresh; a
// food loss shows through wsync's food rows without one)
static std::string party_wipe(int seat, bool& redraw)
{
    void* ui;
    GoalRecord *b, *e;
    std::string lost;
    redraw = false;
    if (goal_history(ui, b, e) && tribe_rescue::wipe_kind(e - b) == tribe_rescue::Wipe::LoseWin) {
        redraw = true;
        const GoalRecord last = e[-1];
        const size_t wins = e - b;
        F<GoalRecord*>(ui, 0x44) = e - 1; // the newest win leaves the history
        if (void* p = cplayer())
            F<float>(p, 0x10F0) = tribe_rescue::bar_after(F<float>(p, 0x10F0));
        const int totems = drop_totem_piece(wins);
        void* village = last.tribe && game::listed(last.tribe, kTribeNoun) ? last.tribe : nullptr;
        char id[16] = "?";
        if (village) {
            const uint32_t npc = F<uint32_t>(village, 0x30);
            snprintf(id, sizeof id, "%08X", npc);
            static uintptr_t get = H->sym("Simulator::cRelationshipManager::Get"),
                             calc = H->sym("Simulator::cRelationshipManager::CalculateRelationship"),
                             apply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
            void* mgr = get ? skg::cdecl_call<void*>(get) : nullptr;
            if (last.kind == 2 && mgr && calc && apply)
                for (int s = 0; s < (int)kMaxSeats; ++s)
                    if (void* v = seat_village(s))
                        for (int dir = 0; dir < 2; ++dir) {
                            const uint32_t f = F<uint32_t>(v, 0x30), x = dir ? f : npc, y = dir ? npc : f;
                            const float d = tribe_rescue::rival_delta(thiscall<float>(calc, mgr, x, y, 0));
                            if (d < 0)
                                (void)thiscall<float>(apply, mgr, x, y, 0x0530CF07u, d);
                        }
        }
        lost = last.kind == 2 ? std::string("its newest ally (") + id + ") is a rival again, a step of the goal and a totem piece are lost"
                              : std::string("its newest conquest (") + id + ") no longer counts: a step of the goal and a totem piece are lost";
        H->log(M, SK_INFO, "tribe rescue: wipe: win %zu of %zu removed (kind %d, village %s), %d totem(s) shortened", wins, wins,
               last.kind, id, totems);
    } else {
        static uintptr_t add = H->sym("SporeCoop::cTribe_AddFood");
        int villages = 0;
        for (int s = 0; s < (int)kMaxSeats; ++s) {
            void* v = seat_here(s) ? seat_village(s) : nullptr;
            const double cost = v ? tribe_rescue::food_cost(village_food_of(v)) : 0;
            if (!v || !add || cost <= 0)
                continue;
            thiscall<void>(add, v, -(int)cost);
            villages++;
        }
        lost = "no win to lose yet: every village lost 10% of its food (" + std::to_string(villages) + " villages)";
    }
    g_wipes++;
    return "Party wipe: " + seat_name(seat) + "'s last villager was not revived: " + lost + ".";
}

// the world refresh after a wipe: saved, sent to every guest and reloaded by the host when it went out (the totem and
// the goal history are redrawn from the save on every game); a save busy now: tried again each second for a minute
static void wipe_refresh(double now)
{
    if (!g_wipe_reload)
        return;
    std::string err;
    bool sent = false;
    for (const auto& p : C.sess->players()) {
        if (p.id == C.sess->my_id())
            continue;
        if (!world::share_with(p.id, err, true)) { // (one save for every share waiting: BUG-0112)
            if (!g_refresh_until) {
                g_refresh_until = now + 60;
                H->log(M, SK_INFO, "tribe rescue: the wipe's world refresh waits: %s", err.c_str());
            }
            g_refresh_next = now + 1;
            return;
        }
        sent = true;
    }
    g_refresh_until = 0;
    if (sent) {
        world::host_reload_after_share(); // (the host loads the same save once it has gone out, as after a stage change)
        g_refreshes++;
    }
}

static void send_lines(double now, bool any)
{
    std::vector<tribe_rescue::Line> lines;
    for (int s = 0; s < (int)kMaxSeats; ++s) {
        const Window& w = g_win[s];
        if (!w.open())
            continue;
        tribe_rescue::Line l;
        l.seat = (uint8_t)s;
        l.player = seat_player(s) < 0xFE ? seat_player(s) : 0xFE;
        l.member = w.member;
        l.tenths = tribe_rescue::tenths_left(w.until, now);
        l.revive = (uint8_t)std::lround(tribe_rescue::revive_progress(w, now) * 100);
        lines.push_back(l);
    }
    net::Writer wr;
    wr.u8(wire::tribe_anim::K_RESCUE);
    tribe_rescue::write_lines(wr, lines);
    for (const auto& p : C.sess->players())
        if (p.id != C.sess->my_id())
            anim_send(p.id, wr, true);
    g_lines_sent++;
    g_lines_open = any;
}

void rescue_host_tick(double now)
{
    if (g_refresh_until && now >= g_refresh_next) {
        if (now > g_refresh_until) {
            g_refresh_until = 0;
            note("The party wipe could not refresh the world: the totem is redrawn at the next load.", true);
        } else
            wipe_refresh(now);
    }
    bool any = false;
    for (int seat = 0; seat < (int)kMaxSeats; ++seat) {
        Window& w = g_win[seat];
        if (!w.open())
            continue;
        void* t = seat_village(seat);
        void* body = hm_obj(w.member);
        bool listed = false;
        if (t && body)
            for (void* c : members(t))
                listed |= c == body;
        if (!t || F<uint32_t>(t, 0x30) != w.faction || !listed || F<uint8_t>(body, 0xB5E)) {
            note(seat_name(seat) + "'s downed villager or its village is gone: the window is closed", false);
            w = {};
            continue;
        }
        if (!intact(t)) { // its hut fell meanwhile: the villager falls the game's way (no window, no grace)
            w = {};
            static const float zero[3] = {0, 0, 0};
            vcall<int>((uint8_t*)body + 0x5A8, 0x18 / 4, 1e6f, 0xFFFFFFFFu, 0, zero, (void*)nullptr);
            note(seat_name(seat) + "'s village lost its hut: the downed villager falls", false);
            continue;
        }
        if (!g_fix) { // tribe.founder_rescue switched off meanwhile: the villager is the game's again (1 health)
            w = {};
            note(seat_name(seat) + "'s downed villager is left to the game (tribe.founder_rescue off)", false);
            continue;
        }
        any = true;
        if (seat == 0 && !w.gave_up && (give_up::held(H, now) || now < g_test_give_up)) {
            w.gave_up = true; // the host's own villager (a guest's: CMD_GIVE_UP)
            g_test_give_up = 0;
        }
        // held where it fell, at 1 health
        game::set_health(body, 1);
        game::stop(body);
        if (net::dist(game::position(body), g_anchor[seat]) > 0.4f)
            game::teleport(body, g_anchor[seat], game::orientation(body));
        int helper_seat = -1;
        for (int s = 0; s < (int)kMaxSeats && helper_seat < 0; ++s)
            if (void* v = s != seat && seat_here(s) ? seat_village(s) : nullptr)
                for (void* c : members(v))
                    if (alive_member(c) && net::dist(game::position(c), g_anchor[seat]) <= tribe_rescue::kReachM) {
                        helper_seat = s;
                        break;
                    }
        const bool gave_up = w.gave_up;
        const tribe_rescue::Step st = tribe_rescue::step(w, helper_seat >= 0, now);
        if (st == tribe_rescue::Step::Waiting)
            continue;
        bool wiped = false;
        if (st == tribe_rescue::Step::Revived) {
            g_revives++;
            note(seat_name(helper_seat) + " revived " + seat_name(seat) + "'s last villager.", true);
        } else {
            (gave_up ? g_gave_up : g_timeouts)++;
            if (g_wipe) {
                note((gave_up ? seat_name(seat) + " gave up. " : std::string()) + party_wipe(seat, wiped), true);
            } else {
                static uintptr_t add = H->sym("SporeCoop::cTribe_AddFood");
                const double cost = tribe_rescue::food_cost(village_food_of(t));
                if (add && cost > 0)
                    thiscall<void>(add, t, -(int)cost);
                g_setbacks++;
                note(seat_name(seat) + "'s last villager got up at its village: food -" + std::to_string((int)cost) + ".", true);
            }
            game::teleport(body, tribe_pos(t), game::orientation(body));
        }
        game::set_health(body, std::max(1.f, game::max_health(body) * tribe_rescue::kUpHealth));
        tribe_rescue::close(w, now);
        if (wiped)
            wipe_refresh(now);
    }
    g_any_open = any;
    if (any ? now >= g_lines_next : g_lines_open) {
        g_lines_next = now + 1;
        send_lines(now, any);
    }
}

bool rescue_give_up(uint8_t from, uint32_t action)
{
    const int seat = party::seat_of_player(from);
    if (!tribe_rescue::give_up_ok(seat, action) || !g_win[seat].open())
        return false;
    g_win[seat].gave_up = true;
    return true;
}

// ---- guest ----
void on_rescue_lines(net::Reader& r)
{
    std::vector<tribe_rescue::Line> lines;
    if (!tribe_rescue::read_lines(r, lines)) {
        g_lines_bad++;
        return;
    }
    g_lines_rx++;
    const double now = H->time_seconds();
    const uint8_t me = C.sess->my_id();
    bool was_down = false;
    for (const auto& x : g_remote)
        was_down |= x.l.player == me && x.until > now;
    g_remote.clear();
    bool down = false;
    for (const auto& l : lines) {
        g_remote.push_back({l, now + l.tenths / 10.0});
        down |= l.player == me;
    }
    if (down && !was_down && C.feed)
        C.feed("Your last villager is down: a partner's villager next to it revives it. Hold G to give up.");
}

void rescue_guest_tick(double now)
{
    const uint8_t me = C.sess->my_id();
    int own_seat = -1;
    for (const auto& x : g_remote)
        if (x.l.player == me && x.until > now)
            own_seat = x.l.seat;
    if (own_seat < 0)
        return; // (a test's give-up asked just before the host's line came waits for it: g_test_give_up's 5 s)
    if ((give_up::held(H, now) || now < g_test_give_up) && now - g_give_up_sent > 2) {
        g_test_give_up = 0;
        g_give_up_sent = now;
        net::Writer w;
        w.u8(CMD_GIVE_UP);
        w.u8(0);
        w.u32(tribe_rescue::seat_action(own_seat));
        write_target(w, Target{});
        w.f32(0);
        w.f32(0);
        w.f32(0);
        C.sess->send_reliable_to(0, net::MSG_TRIBE_CMD, w);
        g_give_ups_sent++;
        H->log(M, SK_INFO, "tribe rescue: this player gave up (seat %d)", own_seat);
    }
}

// ---- both ----
float revive_seconds(void* c)
{
    if (!c || !active())
        return 0;
    const double now = H->time_seconds();
    const uint16_t id = member_id(c);
    if (!id)
        return 0;
    if (is_host()) {
        for (const auto& w : g_win)
            if (w.open() && w.member == id)
                return (float)std::max(0.1, w.until - now);
        return 0;
    }
    for (const auto& x : g_remote)
        if (x.l.member == id && x.until > now)
            return (float)(x.until - now);
    return 0;
}

bool rescue_pending()
{
    if (!active())
        return false;
    if (is_host())
        return g_any_open;
    const double now = H->time_seconds();
    for (const auto& x : g_remote)
        if (x.until > now)
            return true;
    return false;
}

void for_each_downed(void (*fn)(void* c, uint8_t player, float seconds, void* user), void* user)
{
    if (!active())
        return;
    const double now = H->time_seconds();
    if (is_host()) {
        for (int s = 0; s < (int)kMaxSeats; ++s)
            if (g_win[s].open())
                if (void* c = hm_obj(g_win[s].member))
                    fn(c, seat_player(s), (float)std::max(0.0, g_win[s].until - now), user);
        return;
    }
    for (const auto& x : g_remote)
        if (x.until > now)
            if (void* c = mirror_of(x.l.member))
                fn(c, x.l.player, (float)(x.until - now), user);
}

void rescue_forget()
{
    g_win = {};
    g_any_open = false;
    g_lines_open = false;
    g_test_give_up = 0;
    g_refresh_until = 0;
    g_remote.clear();
}
LIFECYCLE_HOOK("tribe.rescue", lifecycle::kSessionEnd | lifecycle::kWorldChange, -47, [](const lifecycle::Ctx&) { rescue_forget(); },
               false);

std::string rescue_debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[1] == "giveup") { // the own downed villager gives up (as holding G)
        g_test_give_up = H->time_seconds() + 5;
        return "give up asked\n";
    }
    if (a.size() >= 3 && a[1] == "wipe" && is_host() && active()) { // tests: the party wipe of seat N now, then the refresh
        bool redraw = false;
        const std::string text = party_wipe(atoi(a[2].c_str()), redraw);
        note(text, true);
        if (redraw)
            wipe_refresh(H->time_seconds());
        return text + "\n" + wins_line() + "\n";
    }
    std::string out;
    char b[400];
    const double now = H->time_seconds();
    snprintf(b, sizeof b,
             "tribe rescue: fix=%d wipe=%d down=%.0fs reload=%d open=%d downs=%u revives=%u timeouts=%u gave_up=%u wipes=%u "
             "setbacks=%u blows_held=%u lines_sent=%u refreshes=%u refresh_wait=%d lines_rx=%u lines_bad=%u give_ups_sent=%u "
             "last: %s\n",
             (int)g_fix, (int)g_wipe, down_time(), (int)g_wipe_reload, (int)g_any_open, g_downs, g_revives, g_timeouts, g_gave_up,
             g_wipes, g_setbacks, g_blows_held, g_lines_sent, g_refreshes, (int)(g_refresh_until > 0), g_lines_rx, g_lines_bad,
             g_give_ups_sent, g_last.c_str());
    out += b;
    if (is_host()) {
        for (int s = 0; s < (int)kMaxSeats; ++s) {
            const Window& w = g_win[s];
            if (!w.open())
                continue;
            void* c = hm_obj(w.member);
            snprintf(b, sizeof b, "  seat %d: member #%u hp=%.1f left=%.1fs revive=%.0f%% gave_up=%d at (%.1f %.1f %.1f)\n", s,
                     w.member, c ? game::health(c) : -1.f, w.until - now, tribe_rescue::revive_progress(w, now) * 100,
                     (int)w.gave_up, g_anchor[s].x, g_anchor[s].y, g_anchor[s].z);
            out += b;
        }
        if (active())
            out += "  " + wins_line() + "\n";
    } else
        for (const auto& x : g_remote) {
            snprintf(b, sizeof b, "  seat %u player %u member #%u left=%.1fs revive=%u%%\n", x.l.seat, x.l.player, x.l.member,
                     x.until - now, x.l.revive);
            out += b;
        }
    return out;
}

} // namespace tribe
