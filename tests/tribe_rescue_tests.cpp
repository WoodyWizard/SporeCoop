// BUG-9278: the pure rules of a player's downed last villager, its revive and the party wipe (mods/coop/
// tribe_rescue_logic.h). Built and run by `make test` (unit-cpp).
#include "tribe_rescue_logic.h"

#include <cmath>
#include <cstdio>

static int g_fail;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                   \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

using namespace tribe_rescue;

static DownIn base_in()
{
    DownIn in;
    in.fix = in.seat = in.player_here = in.partner_village = in.intact = true;
    in.damage = 30;
    in.health = 20;
    in.helper_alive = true;
    in.party_wipe = true;
    return in;
}

static void down_rules()
{
    CHECK(should_down(base_in()));
    CHECK(lethal(20, 20) && lethal(25, 20) && !lethal(19.9f, 20) && !lethal(5, 0) && !lethal(NAN, 10) && !lethal(-1, 10));
    auto off = [](void (*f)(DownIn&)) {
        DownIn in = base_in();
        f(in);
        return should_down(in);
    };
    CHECK(!off([](DownIn& i) { i.fix = false; }));             // tribe.founder_rescue off: the game's death
    CHECK(!off([](DownIn& i) { i.seat = false; }));            // an NPC village's member
    CHECK(!off([](DownIn& i) { i.player_here = false; }));     // an offline partner's village (the AI's)
    CHECK(!off([](DownIn& i) { i.partner_village = false; })); // nobody else has a village here: no partner to come
    CHECK(!off([](DownIn& i) { i.intact = false; }));          // its hut is gone or it was conquered
    CHECK(!off([](DownIn& i) { i.dead = true; }));
    CHECK(!off([](DownIn& i) { i.others_living = true; }));    // not the last one
    CHECK(!off([](DownIn& i) { i.damage = 5; }));              // not lethal
    // no helper alive: down only with the party wipe (a partner may still come; else the wipe, Lineages BUG-9147)
    CHECK(off([](DownIn& i) { i.helper_alive = false; }));
    CHECK(!off([](DownIn& i) {
        i.helper_alive = false;
        i.party_wipe = false;
    }));
    CHECK(down_seconds(true) == 60 && down_seconds(false) == 30);
}

static void windows()
{
    // a helper near for 1.5 s revives; leaving resets the hold
    Window w;
    w.member = 7;
    w.until = 60;
    CHECK(step(w, false, 10) == Step::Waiting);
    CHECK(step(w, true, 11) == Step::Waiting && w.near_since == 11);
    CHECK(step(w, true, 12) == Step::Waiting);
    CHECK(std::fabs(revive_progress(w, 12) - 2.f / 3) < 1e-4f);
    CHECK(step(w, false, 12.2) == Step::Waiting && w.near_since < 0); // walked off
    CHECK(step(w, true, 13) == Step::Waiting);
    CHECK(step(w, true, 14.5) == Step::Revived);
    close(w, 14.5);
    CHECK(!w.open() && w.grace_until == 14.5 + kGraceS && revive_progress(w, 15) == 0);
    // nobody: the window times out at its end
    Window t;
    t.member = 3;
    t.until = 30;
    CHECK(step(t, false, 29.9) == Step::Waiting);
    CHECK(step(t, false, 30) == Step::TimedOut);
    // a helper arriving 1 s before the end does not revive after it
    Window late;
    late.member = 3;
    late.until = 30;
    CHECK(step(late, true, 29) == Step::Waiting);
    CHECK(step(late, true, 30.6) == Step::TimedOut);
    // a give-up ends it now, even with a helper near
    Window g;
    g.member = 4;
    g.until = 50;
    CHECK(step(g, true, 10) == Step::Waiting);
    g.gave_up = true;
    CHECK(step(g, true, 12) == Step::TimedOut && g.until == 12);
    // a closed window does nothing
    Window none;
    CHECK(step(none, true, 5) == Step::Waiting && !none.open());
}

static void wipe_rules()
{
    CHECK(wipe_kind(0) == Wipe::LoseFood && wipe_kind(1) == Wipe::LoseWin && wipe_kind(5) == Wipe::LoseWin);
    CHECK(food_cost(100) == 10 && food_cost(15) == 2 && food_cost(3) == 1 && food_cost(0) == 0 && food_cost(-4) == 0 &&
          food_cost(NAN) == 0);
    CHECK(bar_after(3) == 2 && bar_after(0.5f) == 0 && bar_after(0) == 0);
    // a totem with as many pieces as wins: its top piece goes; another count (a village that joined later) keeps them
    CHECK(totem_pop_ok(8, 24, 2));
    CHECK(!totem_pop_ok(4, 12, 2) && !totem_pop_ok(8, 20, 2) && !totem_pop_ok(0, 0, 0) && !totem_pop_ok(6, 18, 2));
    CHECK(rival_delta(10) == kRivalRelation - 10 && rival_delta(kRivalRelation) == 0 && rival_delta(-9) == 0 &&
          rival_delta(NAN) == 0);
}

static void give_up_rules()
{
    CHECK(seat_action(0) == 1 && seat_action(2) == 3 && seat_action(-1) == 0);
    CHECK(give_up_ok(1, 0) && give_up_ok(1, 2) && !give_up_ok(1, 3) && !give_up_ok(-1, 0) && !give_up_ok(4, 0));
}

static void wire()
{
    // golden bytes: kind's payload {n, seat, player, member LE, tenths LE, revive}
    std::vector<Line> lines{{1, 2, 0x0105, 600, 40}, {0, 0, 9, 1, 0}};
    net::Writer w;
    write_lines(w, lines);
    const std::vector<uint8_t> want{2, 1, 2, 0x05, 0x01, 0x58, 0x02, 40, 0, 0, 9, 0, 1, 0, 0};
    CHECK(w.data == want);
    net::Reader r(w.data.data(), w.data.size());
    std::vector<Line> back;
    CHECK(read_lines(r, back) && back == lines);
    // the end of every window: n = 0
    net::Writer e;
    write_lines(e, {});
    CHECK(e.data.size() == 1 && e.data[0] == 0);
    net::Reader er(e.data.data(), e.data.size());
    CHECK(read_lines(er, back) && back.empty());
    // malformed: refused
    const std::vector<uint8_t> bad_seat{1, 4, 0, 1, 0, 10, 0, 0};
    net::Reader r1(bad_seat.data(), bad_seat.size());
    CHECK(!read_lines(r1, back));
    const std::vector<uint8_t> bad_tenths{1, 0, 0, 1, 0, 0x59, 0x02, 0}; // 601
    net::Reader r2(bad_tenths.data(), bad_tenths.size());
    CHECK(!read_lines(r2, back));
    const std::vector<uint8_t> short_line{1, 0, 0, 1, 0};
    net::Reader r3(short_line.data(), short_line.size());
    CHECK(!read_lines(r3, back));
    const std::vector<uint8_t> no_member{1, 0, 0, 0, 0, 10, 0, 0};
    net::Reader r4(no_member.data(), no_member.size());
    CHECK(!read_lines(r4, back));
    CHECK(tenths_left(30.04, 0) == 301 && tenths_left(0, 5) == 0 && tenths_left(1000, 0) == kMaxTenths);
}

int main()
{
    down_rules();
    windows();
    wipe_rules();
    give_up_rules();
    wire();
    if (g_fail) {
        std::printf("tribe_rescue_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("tribe_rescue_tests: ok\n");
    return 0;
}
