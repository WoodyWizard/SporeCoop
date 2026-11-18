// Decision 6J: parties of 4 (mods/coop/parties.h).
#include <cstdio>

#include "parties.h"

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

using namespace parties;

int main()
{
    const uint32_t kCreature = 0x1654C01, kTribe = 0x1654C02;
    // a plain co-op session: the joiners are in the host's party ("the person you joined"), 4 at most
    {
        Book b;
        b.add(0);
        for (uint8_t p = 1; p <= 4; p++)
            b.add(p, 0);
        CHECK(b.find(1) == b.find(0) && b.find(3) == b.find(0) && b.find(0)->leader == 0);
        CHECK(b.find(4) != b.find(0) && b.find(4)->members.size() == 1 && b.find(4)->leader == 4); // fifth: alone
        CHECK(b.all().size() == 2);
    }
    // a galaxy planet: everyone alone; an invite, accepted: the inviter leads ("the person who invited")
    {
        Book b;
        for (uint8_t p = 0; p < 6; p++)
            b.add(p);
        CHECK(b.all().size() == 6);
        CHECK(b.invite(2, 5, kCreature, kCreature, 0) == Result::Ok && b.pending_from(5) == 2);
        CHECK(b.answer(5, 2, true, 1) == Result::Ok);
        CHECK(b.find(2) == b.find(5) && b.find(2)->leader == 2 && b.find(2)->members.front() == 2);
        CHECK(b.all().size() == 5);
        CHECK(b.answer(5, 2, true, 2) == Result::NoInvite); // answered already
        // the invited player's old party: it leaves it (a two-player party: the other one stays alone, leading)
        CHECK(b.invite(0, 1, 0, 0, 3) == Result::Ok && b.answer(1, 0, true, 3) == Result::Ok);
        CHECK(b.invite(2, 1, kCreature, kCreature, 4) == Result::Ok && b.answer(1, 2, true, 4) == Result::Ok);
        CHECK(b.find(0)->members.size() == 1 && b.find(0)->leader == 0 && b.find(1) == b.find(2));
        // a member who is not the leader invites: the party keeps its leader
        CHECK(b.invite(5, 3, kCreature, kCreature, 5) == Result::Ok && b.answer(3, 5, true, 5) == Result::Ok);
        CHECK(b.find(3) == b.find(2) && b.find(3)->leader == 2 && b.find(2)->members.size() == 4);
        // full: refused on the invite and on a late answer
        CHECK(b.invite(2, 4, kCreature, kCreature, 6) == Result::Full);
        // declined, lapsed, cross stage, self, same party, unknown
        Book c;
        for (uint8_t p = 0; p < 4; p++)
            c.add(p);
        CHECK(c.invite(0, 1, kCreature, kCreature, 0) == Result::Ok && c.answer(1, 0, false, 1) == Result::Declined);
        CHECK(c.find(1)->members.size() == 1);
        CHECK(c.invite(0, 1, kCreature, kCreature, 0) == Result::Ok && c.answer(1, 0, true, kInviteSeconds + 1) == Result::NoInvite);
        CHECK(c.invite(0, 2, kCreature, kTribe, 0) == Result::CrossStage && c.pending_from(2) == -1);
        CHECK(c.invite(0, 0, kCreature, kCreature, 0) == Result::Self);
        CHECK(c.invite(0, 9, kCreature, kCreature, 0) == Result::Unknown);
        CHECK(c.invite(0, 3, kCreature, kCreature, 0) == Result::Ok && c.answer(3, 0, true, 1) == Result::Ok);
        CHECK(c.invite(3, 0, kCreature, kCreature, 2) == Result::SameParty);
        // a party filled while an invite waited: the late yes is refused
        Book d;
        for (uint8_t p = 0; p < 6; p++)
            d.add(p);
        CHECK(d.invite(0, 5, 0, 0, 0) == Result::Ok);
        for (uint8_t p = 1; p <= 3; p++)
            CHECK(d.invite(0, p, 0, 0, 1) == Result::Ok && d.answer(p, 0, true, 1) == Result::Ok);
        CHECK(d.answer(5, 0, true, 2) == Result::Full && d.find(5)->members.size() == 1);
    }
    // the lead: handed on by the leader only, to a member; the leader leaves: the longest standing member leads
    {
        Book b;
        b.add(0);
        b.add(1, 0);
        b.add(2, 0);
        b.add(3, 0);
        CHECK(b.set_leader(1, 2) == Result::NotLeader);
        CHECK(b.set_leader(0, 2) == Result::Ok && b.find(0)->leader == 2);
        b.add(7);
        CHECK(b.set_leader(2, 7) == Result::NotMember);
        b.remove(2); // the leader left the session
        CHECK(b.find(0)->leader == 0 && b.find(0)->members.size() == 3);
        b.remove(0);
        CHECK(b.find(1)->leader == 1); // (the longest standing of 1 and 3)
        CHECK(b.leave(1) == Result::Ok && b.find(1)->members.size() == 1 && b.find(3)->leader == 3);
        b.remove(3);
        CHECK(!b.find(3));
        // an invite from a player who leaves goes with it
        CHECK(b.invite(1, 7, 0, 0, 0) == Result::Ok);
        b.remove(1);
        CHECK(b.pending_from(7) == -1);
    }
    // 6K: kick (the leader only, a member only), a rejoin after a drop, the galaxy server's 16-bit slots
    {
        BasicBook<uint16_t> b;
        b.add(300);
        b.add(301, 300);
        b.add(302, 300);
        CHECK(b.kick(301, 302) == Result::NotLeader && b.kick(300, 300) == Result::Self);
        b.add(900);
        CHECK(b.kick(300, 900) == Result::NotMember);
        CHECK(b.kick(300, 302) == Result::Ok && b.find(302)->members.size() == 1 && b.find(300)->members.size() == 2);
        const uint16_t pid = b.find(300)->id;
        b.remove(301);
        CHECK(b.rejoin(301, pid) && b.find(301) == b.find(300) && b.find(300)->leader == 300);
        CHECK(!b.rejoin(301, pid)); // (in a party already)
        b.remove(300);
        b.remove(301); // the party is gone
        CHECK(!b.rejoin(301, pid) && !b.find(301));
    }
    if (g_fail) {
        printf("party_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("party_tests: all passed\n");
    return 0;
}
