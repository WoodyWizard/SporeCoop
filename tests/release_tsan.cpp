// BUG-0077: the setup's release state is written by the check thread and the install job and read
// by the window. Built with ThreadSanitizer (make test-tsan): two writers and a reader on
// upd::ReleaseBox. `release_tsan --old` uses the box without its lock (the old plain global):
// ThreadSanitizer then reports a data race and the run fails.
// BUG-0244: ReleaseBox::merge never replaces a good release with a failed one (a deterministic check
// first; `--old-merge` = the setup's old get-then-set: that check fails), and one thread merges
// alternating failed and good results while the others write and read.
#include <cstdio>
#include <string>
#include <thread>

#include "../updater/pack.h"

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--old") {
        upd::g_release_lock_fix = false;
        printf("release lock OFF\n");
    }
    if (argc > 1 && std::string(argv[1]) == "--old-merge") {
        upd::g_release_merge_fix = false;
        printf("release merge OFF\n");
    }
    bool merge_ok = true;
    {
        upd::ReleaseBox m;
        upd::ReleaseInfo good, bad;
        good.ok = good.done = true;
        good.rel.version = "0.8.0";
        bad.done = true;
        bad.err = "GitHub not reachable";
        m.set(good);
        // the install job stored a good release, the check thread's own request failed: kept
        bool stored = m.merge(bad);
        merge_ok &= !stored && m.get().ok && m.get().rel.version == "0.8.0";
        m.set(bad);
        merge_ok &= m.merge(good) && m.get().ok; // a good one over a failed one: stored
        // the window: the install job stores a good release while the check thread merges its failure
        upd::ReleaseBox w;
        w.set(bad);
        upd::ReleaseBox::test_between = [](upd::ReleaseBox& b) {
            upd::ReleaseInfo g;
            g.ok = g.done = true;
            g.rel.version = "0.8.1";
            b.set(g);
        };
        w.merge(bad);
        upd::ReleaseBox::test_between = nullptr;
        merge_ok &= w.get().ok && w.get().rel.version == "0.8.1";
        printf("merge: %s\n", merge_ok ? "a good release is never replaced by a failed one" : "FAILED");
    }
    upd::ReleaseBox box;
    auto writer = [&](const char* v) {
        for (int i = 0; i < 20000; i++) {
            upd::ReleaseInfo ri;
            ri.ok = ri.done = true;
            ri.rel.version = v;
            ri.rel.pack_url = std::string("https://github.com/WoodyWizard/SporeCoop/releases/download/v") + v +
                              "/SporeCoop-" + v + ".pack";
            box.set(ri);
        }
    };
    size_t seen = 0;
    std::thread a(writer, "0.8.0.1234567890123456"), b(writer, "0.9.0.6543210987654321");
    std::thread mg([&] {
        for (int i = 0; i < 20000; i++) {
            upd::ReleaseInfo ri;
            ri.done = true;
            ri.ok = i % 2;
            ri.rel.version = ri.ok ? "0.9.0.6543210987654321" : "";
            box.merge(ri);
        }
    });
    std::thread r([&] {
        for (int i = 0; i < 20000; i++)
            seen += box.get().rel.version.size();
    });
    a.join();
    b.join();
    mg.join();
    r.join();
    upd::ReleaseInfo last = box.get();
    bool ok = last.ok && (last.rel.version == "0.8.0.1234567890123456" || last.rel.version == "0.9.0.6543210987654321");
    ok &= merge_ok;
    printf("%s (read %zu chars)\n", ok ? "ALL OK" : "FAILED", seen);
    return ok ? 0 : 1;
}
