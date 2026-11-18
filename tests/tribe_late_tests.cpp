// BUG-9135 roadmap phase 4 (joining late, Tribe) and BUG-9138: the pure rules of mods/coop/tribe_late.h.
#include <cstdio>
#include <string>
#include <vector>

#include "tribe_late.h"

using namespace tribe_late;

static int g_failures;
static void check(bool ok, const char* name)
{
    if (!ok) {
        ++g_failures;
        printf("FAIL %s\n", name);
    }
}

static bool no_em_dash(const std::string& s) { return s.find("\xE2\x80\x94") == std::string::npos; }

int main()
{
    // ---- why a partner waits ----
    PartnerState ready{false, true, true, false, false};
    PartnerState host_creation{false, true, true, true, false};
    PartnerState founder_species{false, true, true, false, true};
    PartnerState settling{false, true, false, true, true};
    PartnerState none{};
    PartnerState none_settled{false, false, true, false, false};
    PartnerState pending{true, false, false, true, true};
    check(founding_wait(ready, true, true) == Wait::Ready, "ready");
    check(founding_wait(none, true, true) == Wait::NoCreatureYet, "no creature yet, young world");
    check(founding_wait(none_settled, true, true) == Wait::NoCreature, "no creature once the world settled");
    check(founding_wait(none_settled, false, false) == Wait::NoCreature, "no creature: never ready, switches off too");
    check(founding_wait(settling, true, true) == Wait::Settling, "settling comes first");
    check(founding_wait(host_creation, true, true) == Wait::HostCreation, "host creation");
    check(founding_wait(founder_species, true, true) == Wait::FounderSpecies, "founder species");
    check(founding_wait(pending, true, true) == Wait::Ready, "a made village finishes saving");
    // the switches' off sides: as before
    check(founding_wait(host_creation, false, true) == Wait::Ready, "tribe.founder_settle off: no host-creation check");
    check(founding_wait(founder_species, true, false) == Wait::Ready, "tribe.founding_skip off: counted ready (BUG-9138)");
    // BUG-9150 (tribe.stand_in): the three waits for a creature of their own found now with a stand-in
    check(founding_wait(none_settled, true, true, true) == Wait::StandIn, "stand-in: no creature once settled");
    check(founding_wait(host_creation, true, true, true) == Wait::StandIn, "stand-in: one of the host's creations");
    check(founding_wait(founder_species, true, true, true) == Wait::StandIn, "stand-in: a founder's species");
    check(founding_wait(none, true, true, true) == Wait::NoCreatureYet, "stand-in: a young world still waits");
    check(founding_wait(settling, true, true, true) == Wait::Settling, "stand-in: a creature that may still change waits");
    check(founding_wait(ready, true, true, true) == Wait::Ready, "stand-in: an own creature is founded with it");
    check(founds_now(Wait::StandIn) && founds_now(Wait::Ready) && !founds_now(Wait::NoCreature), "founds_now");
    {
        std::vector<Wait> waits{founding_wait(none_settled, true, true, true), founding_wait(ready, true, true, true)};
        int why = 0;
        check(pick_partner(waits, &why) == 0 && why == -1, "stand-in: the first partner is founded at once");
    }
    check(told(Wait::NoCreature) && told(Wait::HostCreation) && told(Wait::FounderSpecies) && !told(Wait::Settling) &&
              !told(Wait::NoCreatureYet) && !told(Wait::Ready),
          "told waits");

    // ---- BUG-9138: a partner on a founder's species never holds up the partners after them ----
    {
        std::vector<Wait> fixed{founding_wait(founder_species, true, true), founding_wait(ready, true, true)};
        int why = -2;
        check(pick_partner(fixed, &why) == 1 && why == 0, "fixed: the second partner is founded");
        std::vector<Wait> old{founding_wait(founder_species, true, false), founding_wait(ready, true, false)};
        check(pick_partner(old) == 0, "switch off: the blocked partner is picked first (the bug)");
        std::vector<Wait> waiting{Wait::Settling, Wait::HostCreation, Wait::FounderSpecies};
        check(pick_partner(waiting, &why) == -1 && why == 1, "nobody ready: the first told reason");
        std::vector<Wait> quiet{Wait::Settling, Wait::NoCreatureYet};
        check(pick_partner(quiet, &why) == -1 && why == -1, "nothing worth telling");
        check(pick_partner({}, &why) == -1 && why == -1, "no partners");
    }

    // ---- the words everybody reads ----
    {
        const std::string t = wait_text(Wait::HostCreation, "Kit");
        check(t.rfind("Kit plays one of the host's creatures", 0) == 0 && t.find("Co-op window") != std::string::npos,
              "notice names the player and the window");
        check(wait_text(Wait::FounderSpecies, "Kit") == t, "same words for a founder's species");
        check(wait_text(Wait::Settling, "Kit").empty() && wait_text(Wait::Ready, "Kit").empty() &&
                  wait_text(Wait::NoCreatureYet, "Kit").empty(),
              "no notice otherwise");
        check(no_em_dash(t) && t.find("your creature") == std::string::npos, "plain words for both sides, no em dash");
        const std::string n = wait_text(Wait::NoCreature, "Kit");
        check(n.rfind("Kit has no creature of their own here", 0) == 0 && no_em_dash(n) &&
                  n.find("your creature") == std::string::npos,
              "notice for a partner without any creature");
    }

    {
        const std::string h = host_status(Wait::HostCreation, "Kit");
        check(h == "Kit has no village yet: they play one of your creatures." && no_em_dash(h), "host status line");
        check(host_status(Wait::FounderSpecies, "Kit") == h, "host status line, founder species");
        check(host_status(Wait::NoCreature, "Kit") == "Kit has no village yet: they have no creature of their own.",
              "host status line, no creature");
        check(host_status(Wait::Settling, "Kit").empty(), "no status line for a short wait");
    }

    // ---- the partner's Co-op window ----
    check(prompt(true, false, false, kPromptSettle) == Prompt::ChooseCreature, "late joiner without a creature of their own");
    check(prompt(true, true, false, 100) == Prompt::None, "has a village (an older seat founded with the host's creature)");
    check(prompt(true, false, true, 100) == Prompt::VillageComing, "plays their own creature: the selector stays, the founding follows");
    // BUG-9150: a village that wears its stand-in keeps the prompt; one with the player's own creature does not
    check(prompt(true, true, false, 100, true) == Prompt::StandInChoose, "stand-in village: choose or make a creature");
    check(prompt(true, true, true, 100, true) == Prompt::StandInComing, "stand-in village, own creature chosen: it comes");
    check(prompt(true, true, false, 100, false) == Prompt::None, "own village: no prompt");
    check(prompt(true, true, false, 1, true) == Prompt::None, "stand-in village, world not settled: no prompt yet");
    check(prompt(true, true, true, 100) == Prompt::None, "founded: the window as before");
    check(prompt(true, false, false, kPromptSettle - 1) == Prompt::None, "the world has not settled");
    check(prompt(false, false, false, 100) == Prompt::None, "not a guest in Tribe");

    // ---- catch-up (decision 5) ----
    {
        // the host's village at the start: the fresh village (3 adults, level 0) needs nothing
        Village start{0, 3, 0, 0};
        Village fresh{0, 3, 0, 0};
        check(!catch_up_plan(start, fresh).any(), "host at the start: no catch-up");
        // a grown village: level 2, 7 adults, 2 babies, racks 1 (torches), 5 (maracas) and the fire pit bit 10
        Village host{2, 7, 2, (uint16_t)((1u << 1) | (1u << 5) | (1u << 10))};
        CatchUp c = catch_up_plan(host, fresh);
        check(c.level == 2, "hut level");
        check(c.adults == 4 && c.babies == 2, "members by age: 4 adults, 2 babies");
        check(c.tools == ((1u << 1) | (1u << 5)), "racks only, not the fire pit");
        check(kinds(c.tools) == std::vector<int>({1, 5}), "rack kinds ascending");
        // the fresh village already has a rack and more adults than the host
        Village rich{2, 9, 0, (uint16_t)(1u << 5)};
        c = catch_up_plan(host, rich);
        check(c.level == -1 && c.adults == 0 && c.babies == 0 && c.tools == (1u << 1), "never removes, keeps what it has");
        // more babies than adults wanted
        Village nursery{1, 2, 3, 0};
        c = catch_up_plan(nursery, fresh);
        check(c.adults == 0 && c.babies == 2 && c.level == 1, "host 2 adults + 3 babies: two babies");
        // a host level outside the native range is not copied
        Village odd{7, 3, 0, 0};
        check(catch_up_plan(odd, fresh).level == -1, "level out of range kept");
        // the member count is bounded
        Village crowd{0, 200, 0, 0};
        c = catch_up_plan(crowd, fresh);
        check(c.adults + c.babies == kMaxCatchUpMembers, "member bound");
        const std::string line = describe(catch_up_plan(host, fresh), 2, 4, 1, (uint16_t)(1u << 1));
        check(line == "hut level 2, members +5 of 6 (4 adults, 1 babies), racks 1 5(not placed)", "log line");
        check(no_em_dash(line), "log line without em dash");
    }

    // BUG-9159 (decision 2M): making a creature in the editor holds the stand-in; their saved creature founds the village
    {
        PartnerState e;
        e.settled = true;
        e.editing = true;
        check(founding_wait(e, true, true, true) == Wait::Editing, "2M: founding_wait(e, true, true, true) == Wait::Editing");
        check(!founds_now(Wait::Editing) && told(Wait::Editing), "2M: !founds_now(Wait::Editing) && told(Wait::Editing)");
        check(!wait_text(Wait::Editing, "Ann").empty() && !host_status(Wait::Editing, "Ann").empty(), "2M: !wait_text(Wait::Editing, 'Ann').empty() && !host_status(Wai");
        e.has_creature = true; // saved: their own creature, settling then founded as usual
        check(founding_wait(e, true, true, true) == Wait::Ready, "2M: founding_wait(e, true, true, true) == Wait::Ready");
        e.has_creature = false;
        e.editing = false; // left without saving: the stand-in
        check(founding_wait(e, true, true, true) == Wait::StandIn, "2M: founding_wait(e, true, true, true) == Wait::StandIn");
        e.pending = true; e.editing = true; // a village already made finishes first
        check(founding_wait(e, true, true, true) == Wait::Ready, "2M: founding_wait(e, true, true, true) == Wait::Ready");
        // the host asks instead of a stand-in, waits kEditorAnswer for the answer, then the stand-in
        check(editor_ask(Wait::StandIn, true, 0, 100) == Ask::AskNow, "2M: ask first");
        check(editor_ask(Wait::StandIn, true, 100, 110) == Ask::Waiting, "2M: wait for the answer");
        check(editor_ask(Wait::StandIn, true, 100, 100 + kEditorAnswer + 1) == Ask::None, "2M: no answer: the stand-in");
        check(editor_ask(Wait::Ready, true, 0, 100) == Ask::None, "2M: a partner with a creature is founded");
        check(editor_ask(Wait::StandIn, false, 0, 100) == Ask::None, "2M: off: the stand-in at once");
        // the guest opens it when asked, once per world, not with a creature of their own
        check(editor_first(true, true, false, false, 0), "2M: asked: open");
        check(!editor_first(true, false, false, false, 0), "2M: not asked: never");
        check(!editor_first(true, true, true, false, 0), "2M: plays their own creature");
        check(!editor_first(true, true, false, true, 0), "2M: already opened here");
        check(!editor_first(true, true, false, false, kEditorTries), "2M: tried enough");
        check(!editor_first(false, true, false, false, 0), "2M: off");
        check(loading_progress(0) == 0.f && loading_progress(-1) == 0.f, "2M: loading_progress(0) == 0.f && loading_progress(-1) == 0.f");
        check(loading_progress(kFoundingSeconds * 10) <= 0.95f, "2M: loading_progress(kFoundingSeconds * 10) <= 0.95f");
        check(loading_step(0.1f) == 0 && loading_step(0.3f) == 1 && loading_step(0.6f) == 2 && loading_step(0.9f) == 3, "2M: loading_step(0.1f) == 0 && loading_step(0.3f) == 1 && loadin");
    }

    printf("tribe late: %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
