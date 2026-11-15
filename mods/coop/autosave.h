// The host's autosave (E2 of docs/plan-0.7.15.md, BUG-0864): while it hosts a session, the host's game saves itself
// every N minutes (the session setting all.autosave, default 10) and soon after a milestone (a new stage, a colony
// founded, a city captured, an epic killed), into its own save as the pause menu's Save does (world::save_game). Any
// save of the game counts (the player's own, a world share's): the timer starts again from it. A save the game refuses
// now (an editor, a load, a mode switch, Space on a planet, a cinematic, a share being written) is tried again every
// few seconds. docs/01-architecture.md#arch-autosave.
//
// Portable (no game): tests/store_tests.cpp runs the clock.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace autosave {

constexpr double kMilestoneDelay = 5.0; // s after a milestone before its save (the stage's own writes settle first)
constexpr double kRetry = 5.0;          // s between tries the game refused
constexpr double kConfirm = 30.0;       // s a posted save may take before it counts as lost
constexpr double kRearm = 15.0;         // s after a lost save before the next try

struct Clock {
    double interval = 600; // s between timed saves; <= 0: no autosave at all
    double started = -1;   // when the clock began (hosting this world); -1: not running
    double last_save = -1; // the last save of the game seen (any kind)
    bool pending = false;  // a milestone waits for a save
    std::string why;       // ... which one
    double pending_at = 0;
    double next_try = 0;
    double posted_at = -1; // our save went to the game, its write not seen yet
    uint32_t posted = 0, confirmed = 0, refused = 0, lost = 0, milestones = 0;
    std::string refusal;   // the game's last reason not to save

    bool running() const { return started >= 0 && interval > 0; }
    // the clock begins (the host's world is open in a session) / stops
    void start(double now)
    {
        started = now;
        pending = false;
        posted_at = -1;
        next_try = 0;
    }
    void stop() { started = -1, pending = false, posted_at = -1; }

    void milestone(const std::string& w, double now)
    {
        if (!running())
            return;
        pending = true;
        why = w;
        pending_at = now;
        next_try = std::max(next_try, now + kMilestoneDelay);
        milestones++;
    }
    // the game wrote a save (any: ours, the player's, a share's) at `at`
    void saved(double at)
    {
        last_save = std::max(last_save, at);
        if (posted_at >= 0 && at >= posted_at) {
            confirmed++;
            posted_at = -1;
        }
        if (pending && at >= pending_at)
            pending = false;
    }
    // why to save now ("" = not now): a milestone first, else the interval since the last save (or the start)
    std::string due(double now) const
    {
        if (!running() || posted_at >= 0 || now < next_try)
            return "";
        if (pending)
            return why;
        if (now - std::max(last_save, started) >= interval)
            return "timer";
        return "";
    }
    // the try: posted (the game takes it), or refused with the game's reason
    void tried(double now, bool ok, const std::string& reason = "")
    {
        if (ok) {
            posted++;
            posted_at = now;
            refusal.clear();
        } else {
            refused++;
            refusal = reason;
            next_try = now + kRetry;
        }
    }
    // every frame: a posted save whose write never came is lost after kConfirm
    void tick(double now)
    {
        if (posted_at >= 0 && now - posted_at > kConfirm) {
            lost++;
            posted_at = -1;
            next_try = now + kRearm;
        }
    }
    // seconds since the last save, -1: none seen
    double ago(double now) const { return last_save < 0 ? -1 : now - last_save; }
};

// "saved 3 min ago" / "saved just now" / "not saved yet" (the co-op window); ru: Russian
inline std::string ago_text(double s, bool ru)
{
    if (s < 0)
        return ru ? "ещё не сохранялась" : "not saved yet";
    if (s < 60)
        return ru ? "сохранена только что" : "saved just now";
    int m = (int)(s / 60);
    if (m < 120)
        return (ru ? "сохранена " : "saved ") + std::to_string(m) + (ru ? " мин назад" : " min ago");
    return (ru ? "сохранена " : "saved ") + std::to_string(m / 60) + (ru ? " ч назад" : " h ago");
}

} // namespace autosave
