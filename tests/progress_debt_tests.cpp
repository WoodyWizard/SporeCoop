// BUG-9119: the progress debt's pure rules (mods/coop/progress_debt_logic.h)
#include <cstdio>
#include "progress_debt_logic.h"
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
using namespace progress_debt;
int main()
{
    // a death: 10% of the stage's progress so far, whole points, at least 1 (never DNA)
    CHECK(progress_cost(321) == 32);
    CHECK(progress_cost(204) == 20);
    CHECK(progress_cost(5) == 1);
    CHECK(progress_cost(0.5f) == 0);
    CHECK(progress_cost(0) == 0);
    CHECK(progress_cost(-5) == 0);
    // the ledger: gains repay first, nothing earned is taken away
    Ledger l;
    CHECK(l.place(1, 7));
    l.add(3);
    CHECK(l.absorb(2) == 0 && l.owed == 1);        // all of it repays
    CHECK(l.absorb(5) == 4 && l.owed == 0);        // the rest counts again
    CHECK(l.absorb(5) == 5);                        // no debt: whole
    l.add(4);
    CHECK(!l.place(1, 7) && l.owed == 4);           // the same stage and world (an editor between) keeps it
    CHECK(l.place(1, 8) && l.owed == 0);            // another world clears it
    l.add(-2);
    CHECK(l.owed == 0);
    CHECK(l.absorb(-1) == -1);                      // a spend is not a repayment
    // Tribe's bar (1280x960, 2026-11-12): sections 233-422, 423-804, 808-1179 hold 1, 2 and 2 of 5 steps
    const float sx1[3] = {233, 423, 808}, sx2[3] = {422, 804, 1179};
    float x1 = 0, x2 = 0;
    CHECK(step_slot(sx1, sx2, 3, 0, 5, x1, x2) && x1 == 233 && x2 == 422);
    CHECK(step_slot(sx1, sx2, 3, 1, 5, x1, x2) && x1 == 423 && x2 > 613 && x2 < 614);
    CHECK(step_slot(sx1, sx2, 3, 2, 5, x1, x2) && x1 > 613 && x1 < 614 && x2 == 804);
    CHECK(step_slot(sx1, sx2, 3, 4, 5, x1, x2) && x2 == 1179);
    CHECK(!step_slot(sx1, sx2, 3, 5, 5, x1, x2));  // past the last step
    CHECK(!step_slot(sx1, sx2, 3, -1, 5, x1, x2));
    const float gone1[3] = {233, 0, 808}, gone2[3] = {422, 0, 1179}; // a missing section is skipped
    CHECK(step_slot(gone1, gone2, 3, 1, 5, x1, x2) && (x2 <= 422 || x1 >= 808)); // never in the gap
    if (!failures) printf("progress_debt_tests: all passed\n");
    return failures ? 1 : 0;
}
