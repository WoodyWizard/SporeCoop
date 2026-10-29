// Rate limit of repeated log events (trace.h): the first `burst` events of a key in each window are
// written in full, the rest only counted; take_summary() hands out the counts of the window that
// ended. Pure logic, no game or Windows code (tests/text_tests.cpp).
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace trace {

class RateLimit {
public:
    explicit RateLimit(double window = 10.0) : window_(window) {}

    // true: write this event in full; false: counted for the summary
    bool allow(const std::string& key, double now, int burst)
    {
        roll(now);
        Count& c = counts_[key];
        c.seen++;
        if (c.seen <= burst)
            return true;
        c.dropped++;
        return false;
    }

    // the keys whose events were dropped in the window that ended at `now` (or earlier):
    // (key, dropped, seen); empty while the window runs
    std::vector<std::pair<std::string, std::pair<int, int>>> take_summary(double now)
    {
        std::vector<std::pair<std::string, std::pair<int, int>>> out;
        if (start_ < 0 || now - start_ < window_)
            return out;
        for (auto& [k, c] : counts_)
            if (c.dropped > 0)
                out.push_back({k, {c.dropped, c.seen}});
        counts_.clear();
        start_ = now;
        return out;
    }

    double window() const { return window_; }

private:
    struct Count {
        int seen = 0, dropped = 0;
    };
    void roll(double now)
    {
        if (start_ < 0)
            start_ = now;
    }
    double window_;
    double start_ = -1;
    std::map<std::string, Count> counts_;
};

} // namespace trace
