// E2 (BUG-0864): the host's autosave. autosave.h (the clock, unit tested), autosave_glue.h.
#include "autosave.h"
#include "autosave_glue.h"

#include <cstdio>

#include "coop_ui.h"
#include "i18n.h"
#include "fixes.h"
#include "game.h"
#include "settings.h"
#include "stage_epoch.h"
#include "store.h"
#include "world.h"

namespace autosave {

static const SkHost* H;
static SkMod* M;
static net::Session* S;
static std::function<void(const std::string&)> g_feed;

// `coop.fix world.autosave off`: no autosave (0.7.14: only the players' own saves and the world shares' saves)
static bool g_fix = true;
COOP_FIX("world.autosave", "BUG-0864", g_fix,
         "world: the host's game saves itself every N minutes (all.autosave) and after milestones while it hosts");

static Clock g_clock;
static std::string g_world;     // the store world the clock runs for
static int g_stage = -1;        // the stage index last seen in it (a later stage: a milestone)
static uint32_t g_saves_seen;   // store::saves() last read
static double g_test_interval;  // tests (coop.autosave interval <s>): instead of the setting's minutes; 0 = the setting
static bool g_task;             // a try runs outside the frame
static std::string g_last_why;
static uint32_t g_by_why[3];    // timer, milestone, (unused)

static bool hosting() { return S && S->role() == net::Role::Host && S->connected(); }

static double interval()
{
    if (g_test_interval > 0)
        return g_test_interval;
    return 60.0 * settings::autosave_minutes(settings::get(settings::kAutosave));
}

static void try_task(void*)
{
    g_task = false;
    double now = H->time_seconds();
    std::string why = g_clock.due(now);
    if (why.empty() || !hosting())
        return;
    std::string err;
    bool ok = world::save_game(err);
    g_clock.tried(now, ok, err);
    if (ok) {
        g_last_why = why;
        g_by_why[why == "timer" ? 0 : 1]++;
        H->log(M, SK_INFO, "autosave: the host's game saves (%s)", why.c_str());
    } else if (g_clock.refused % 12 == 1)
        H->log(M, SK_INFO, "autosave: not now (%s): %s", why.c_str(), err.c_str());
}

void tick()
{
    if (!H)
        return;
    double now = H->time_seconds();
    if (store::saves() != g_saves_seen) { // any save of the game (ours, the player's, a share's)
        g_saves_seen = store::saves();
        g_clock.saved(now - std::max(0.0, store::since_save()));
    }
    g_clock.interval = g_fix ? interval() : 0;
    std::string w = store::world_name();
    bool on = g_fix && hosting() && !w.empty() && g_clock.interval > 0;
    if (!on) {
        if (g_clock.started >= 0)
            g_clock.stop();
        g_world.clear();
        g_stage = -1;
        return;
    }
    if (g_clock.started < 0 || w != g_world) { // hosting this world from now on
        g_world = w;
        g_clock.start(now);
        g_stage = -1;
    }
    int st = stage_epoch::index_of(game::mode_id());
    if (st >= 0) {
        if (g_stage >= 0 && st > g_stage)
            g_clock.milestone("a new stage", now);
        g_stage = st;
    }
    g_clock.tick(now);
    if (g_task || g_clock.due(now).empty())
        return;
    // the game's own moments that a save must not break; save_game refuses the others (editors, loads, a planet)
    const char* wait = world::cinematic_playing() ? "a cinematic plays"
                       : world::save_busy()       ? "a world share's save is being written"
                       : world::saved_box_up()    ? "the last save's box is up"
                                                  : nullptr;
    if (wait) {
        g_clock.tried(now, false, wait);
        return;
    }
    g_task = true;
    H->run_outside_frame(try_task, nullptr);
}

void milestone(const char* why)
{
    if (H && g_fix && hosting())
        g_clock.milestone(why, H->time_seconds());
}

std::string saved_line()
{
    if (!H || !S || S->role() != net::Role::Host)
        return "";
    std::string t = ago_text(store::since_save(), coop_ui::russian());
    if (!g_clock.running())
        return t;
    return t + i18n::tr(" (autosave on)", " (автосохранение)");
}

static int command(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string sub = argc > 1 ? argv[1] : "";
    double now = H->time_seconds();
    if (sub == "interval" && argc > 2) { // tests: seconds between timed saves (0: the setting's minutes again)
        g_test_interval = atof(argv[2]);
        H->out_printf(out, "autosave interval %.0f s\n", interval());
        return 0;
    }
    if (sub == "milestone") { // tests: as if a milestone happened now
        milestone(argc > 2 ? argv[2] : "a test milestone");
        H->out_printf(out, "milestone noted (pending=%d)\n", (int)g_clock.pending);
        return 0;
    }
    H->out_printf(out,
                  "autosave: fix=%d running=%d interval=%.0f world=%s stage=%d posted=%u confirmed=%u refused=%u lost=%u "
                  "milestones=%u timer_saves=%u milestone_saves=%u pending=%d due=%s ago=%.0f last=%s refusal=%s\n",
                  (int)g_fix, (int)g_clock.running(), g_clock.interval, g_world.empty() ? "-" : g_world.c_str(), g_stage,
                  g_clock.posted, g_clock.confirmed, g_clock.refused, g_clock.lost, g_clock.milestones, g_by_why[0],
                  g_by_why[1], (int)g_clock.pending, g_clock.due(now).empty() ? "-" : g_clock.due(now).c_str(),
                  store::since_save(), g_last_why.empty() ? "-" : g_last_why.c_str(),
                  g_clock.refusal.empty() ? "-" : g_clock.refusal.c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, net::Session* sess, std::function<void(const std::string&)> feed)
{
    H = host;
    M = mod;
    S = sess;
    g_feed = std::move(feed);
    H->command_add(M, "coop.autosave",
                   "coop.autosave [interval <s> | milestone [<why>]] - the host's autosave (E2, BUG-0864): status, test seams",
                   command, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace autosave
