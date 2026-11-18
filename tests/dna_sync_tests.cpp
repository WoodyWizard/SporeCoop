#include <cstdio>
#include <string>
#include "dna_sync.h"
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
int main(int argc, char** argv)
{
    bool fix = !(argc > 1 && std::string(argv[1]) == "--nofix");
    float budget = 100, earned = 300;
    auto receive = [&](float b, float e) {
        dna_sync::apply(b,e,budget,earned,fix,
            [&](float d) { budget += d; earned += d; },
            [&](float total) { budget = total; });
    };
    receive(60,300); // another editor spent 40 DNA
    CHECK(budget == 60 && earned == 300);
    receive(50,310); // +10 reward and -20 spend in one host frame
    CHECK(budget == 50 && earned == 310);
    receive(45,305); // 10% unrevived death: shared budget AND progress loss
    CHECK(budget == 45 && earned == 305);
    receive(45,305); // periodic resend / duplicate
    CHECK(budget == 45 && earned == 305);
    receive(45,320); // earned progress changed but budget did not
    CHECK(budget == 45 && earned == 320);
    receive(0,320); // spending the final DNA does not remove progress
    CHECK(budget == 0 && earned == 320);
    float prior = earned;
    receive(NAN,900); receive(-1,900); receive(INFINITY,900);
    CHECK(budget == 0 && earned == prior);
    // Catch up from another save / an editor to the current host snapshot.
    budget = 900; earned = 100;
    receive(45,305);
    CHECK(budget == 45 && earned == 305);
    net::Writer w; dna_sync::Progress{7,305}.write(w);
    net::Reader r(w.data.data(),w.data.size());
    auto p = dna_sync::Progress::read(r);
    CHECK(p.valid(7) && p.earned == 305 && !p.valid(8) && !p.valid(0));
    for (size_t n=0; n<w.data.size(); ++n) {
        net::Reader truncated(w.data.data(),n);
        CHECK(!dna_sync::Progress::read(truncated).valid(7));
    }
    net::Writer invalid; dna_sync::Progress{7,NAN}.write(invalid);
    CHECK(invalid.data.empty());
    w.data[0] ^= 1;
    net::Reader wrong_tag(w.data.data(),w.data.size());
    CHECK(!dna_sync::Progress::read(wrong_tag).valid(7));
    // Legacy host fallback is intentionally unchanged, rather than silently
    // treating an unknown negative delta as spending and losing death penalties.
    budget = 100; earned = 300;
    receive(90,NAN);
    CHECK(budget == 90 && earned == 290);
    printf("DNA sync: %d failures\n",failures);
    return failures ? 1 : 0;
}
