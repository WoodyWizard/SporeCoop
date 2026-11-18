#include <cstdio>
#include <string>
#include "chief_arrival.h"
int main(int argc, char** argv)
{
    bool fix = !(argc > 1 && std::string(argv[1]) == "--nofix");
    int failures = 0;
    chief_arrival::Movement chief;
    auto eligible = [&](net::Vec3 p, float speed) { return !fix || chief.stationary(p,speed); };
    auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; printf("FAIL %s\n", name); } };
    check(eligible({10,20,30},0), "stacked newcomer can be placed");
    check(eligible({10.2f,20,30},0.1f), "small idle motion tolerated");
    check(!eligible({10.2f,20,30},6), "walking chief exempt");
    check(!eligible({10.2f,20,30},0), "stopped chief remains exempt");
    chief = {};
    check(eligible({10,20,30},0), "new arrival resets history");
    check(!eligible({12,20,30},0), "walk between samples detected");
    check(!eligible({10,20,30},0), "returning to origin remains exempt");
    printf("chief arrival: %d failures\n", failures);
    return failures ? 1 : 0;
}
