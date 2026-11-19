// WP-X1 of docs/plan-0.7.16.md (BUG-1276): a guest's own City Hall and vehicle editors at its arrival in the
// Civilization stage (the Tribe -> Civ pattern of BUG-1240's Cell -> Creature arrival, docs/re/civ.md#civ-own-arrival).
//
// At the end of Tribe the host's game asks for the City Hall editor (TRG2CVG_PreEditor's end, the Tribe listener
// 0xCD1D60: EditorRequest editor 0x99E92F05, tag 0x116DD1B) and then for the vehicle editor (TRG2CVG_ShowCityHall's
// end: F670AA43 / 9AD7D4AA / 7D433FAD by the consequence archetype cPlayer +0x1104); each request waits for the
// host's vote (vote.cpp). A guest's game never runs that end itself: it plays the host's Tribe until the host's Civ
// world comes, and arrives in Civ with the culture set the host's editors made (BUG-1158 refused a guest's own design
// editors in the host's world; WP-C3 lets them open for its species line, civ_editors.cpp).
//
// On (`core.own_civ_arrival`, own mode: creature.mode own, the species line, WP-C3's civ.guest_editors): a guest that
// saw the host's vote for one of those editors end while it was in the host's Tribe keeps "its own Civ editors due";
// once it plays the host's Civ world and its game has settled (20 s, no cinematic, pause, load, vote or pause menu), the
// same two editors open on its game, one after the other, built as the Tribe listener builds them (plus the editor's
// red cross: the guest may decline). Nothing on the host waits for them (no vote, no hold: the host plays its Civ stage
// meanwhile, BUG-1263's lesson). Their results never reach the game's civilization: an editor's result only becomes a
// culture entry through the planner's apply 0xD095A0 (docs/re/civ.md#civ-own-editors), which WP-C3 skips for a guest;
// this file takes the result (the editor's message 0x30C11C7, seen in cGameModeCiv's listener hooked by
// civ_editors.cpp; else the planner's copy of it, planner +0x4C / +0x50) and hands it to civedit::take_editor ->
// species::set_design. Every game then shows the guest's cities and vehicles in those designs (civ_own_designs.cpp).
// Off, host mode, the host's game, an older host: as 0.7.15 (no own editor at the arrival).
//
// BUG-1290 (WP-S1b, `core.own_civ_late`): a guest that missed the end of Tribe (it joined the host's Civ world, came
// back after the change, restarted its game) never saw that vote, so the editors were never due. On: at its first
// arrival in the host's Civ world of a session, a guest in own mode that plays an own creature there and has no own
// City Hall in its species line gets the same two editors after the same settle, once per world: the City Hall
// editor's opening is noted in its SporeCoop\worlds.ini ([civ_arrival] <world crc>), a later join of that world does
// not offer them again (the red cross counts as offered). A guest that plays the host's species here gets none (it
// picks its creature in the co-op window first, BUG-1289; the editors come at its next arrival). Off: only the vote
// path above.
#include "coop_state.h"
#include "civ_editors.h"
#include "designs.h"
#include "species_line.h"

namespace coop_core {

bool pause_ui_up();
bool mode_switch_pending();
extern bool g_vote_bypass;

static bool g_fix = true;
COOP_FIX("core.own_civ_arrival", "BUG-1276", g_fix,
         "core: in own mode a guest's own City Hall and vehicle editors open after its arrival in the host's Civ world (the host does not wait); their designs go to its species line");

constexpr uint32_t kCityHall = 0x99E92F05u;
constexpr uint32_t kTribeTag = 0x0116DD1Bu;    // the Tribe listener's tag for both editors (0xCD1D60)
constexpr double kArriveLimit = 900.0;           // the host's two editors and the Civ world's transfer
constexpr double kSettle = 20.0, kSettleLimit = 240.0, kOpenLimit = 30.0, kNextSettle = 5.0, kNextLimit = 120.0;

// the vehicle editors of the end of Tribe (0xCD2308: cPlayer +0x1104 C2CA9495 -> F670AA43, 60A78928 -> 9AD7D4AA, else
// 9AD7D4AA + E26B6B03 = 7D433FAD; each is its own asset type, Editors::cEditor::GetEditorForAssetType)
static bool tribe_end_editor(uint32_t id) { return id == kCityHall || id == 0xF670AA43u || id == 0x9AD7D4AAu || id == 0x7D433FADu; }

enum Phase : int { kNone, kDue, kSettling, kOpening, kOpen, kNext, kDone };
static const char* phase_name(int p)
{
    static const char* n[] = {"none", "due", "settling", "opening", "open", "next", "done"};
    return p >= 0 && p <= kDone ? n[p] : "?";
}

struct Result {
    bool seen = false, cancelled = false, taken = false;
    uint32_t did = 0, type_id = 0, key[3] = {0, 0, 0};
    const char* from = "";
};
static int g_phase = kNone;
static int g_step = 0;            // 0 the City Hall editor, 1 the vehicle editor
static uint32_t g_editor[2] = {kCityHall, 0};
static double g_due_at = 0, g_arrived_at = 0, g_since = 0, g_opened_at = 0, g_back_at = 0, g_last_tick = 0;
static bool g_editor_seen = false; // the game showed the editor after our Submit
static bool g_queued = false;
static Result g_res[2];
static unsigned g_dues, g_arrivals, g_opened[2], g_open_failed, g_results, g_taken, g_cancels, g_dropped, g_waits;
static std::string g_why = "";     // the last reason it ended / was dropped

// BUG-1290: the late path (a guest that missed the end of Tribe)
static bool g_late_fix = true;
COOP_FIX("core.own_civ_late", "BUG-1290", g_late_fix,
         "core: a guest that missed the end of Tribe (joined in Civ, came back after the change) gets its own City Hall and vehicle editors once per world after it arrived in the host's Civ world");
constexpr const char* kNote = "civ_arrival"; // worlds.ini section: the world's own Civ editors were offered
static bool g_late = false;                  // this round came from the late path
static bool g_late_checked = false;          // this session's first arrival in the host's Civ world was looked at
static bool g_was_here = false;
static uint32_t g_late_world = 0;
static std::string g_late_why = "";
static unsigned g_late_dues, g_late_skips, g_late_notes;

static void set_phase(int p, double now)
{
    g_phase = p;
    g_since = now;
}
static void drop(const std::string& why, const char* tell)
{
    LOG("civ arrival: own Civ editors not opened: %s (BUG-1276)", why.c_str());
    g_why = why;
    g_dropped++;
    g_phase = kNone;
    if (tell)
        feed(tell);
}

// the host takes our species line (its build has creations 1.1 or newer: a 0.7.15.2 host keeps the old look).
// BUG-1306: the host's WELCOME families (host_families); peer_families answers only on the host, so allowed() was
// false on every guest and no own City Hall editor ever opened (lab 20261119-095414 civ_own_arrival). Off
// (core.own_civ_families): the host-only query (rel/0.7.16 before the fix)
static bool g_families_fix = true;
COOP_FIX("core.own_civ_families", "BUG-1306", g_families_fix,
         "civ: a guest knows that the host keeps species lines, so its own City Hall and vehicle editors open at its Civ arrival");
static bool host_has_line()
{
    bool has = false;
    std::vector<net::FamilyRev> fam;
    if (!(g_families_fix ? g_sess.host_families(has, fam) : g_sess.peer_families(0, has, fam)) || !has)
        return false;
    for (auto& f : fam)
        if (f.family == 10 && f.rev == 1 && f.minor >= 1) // creations (net/messages.def)
            return true;
    return false;
}
static bool allowed()
{
    return g_fix && g_sess.connected() && g_sess.role() == net::Role::Client && !host_creature_mode() &&
           species::own_mode() && civedit::allows(0) && host_has_line();
}

// BUG-1323 (lab 20261119-113002 civ_own_arrival, civ_late_arrival, line_stages_editors; probe on lane 2 with the
// pause trace): back in Civ from our own editor, cGameModeCiv's update (0xCFE2C0) pauses 0x4BF38A7 while its return
// state machine (CivGame_EditorReturn 0xCFBD70, +0xB4) runs, and that machine resumes it in its state 7 only when the
// mode before was not the editor: after an editor the planner it re-opens (+0xF8, 0xCF20B0) carries the pause and its
// close lifts it. Our editors come from no planner (+0xF8 = -1): the guest's game stayed paused (busy=paused, no HUD),
// its vehicle editor never opened. On: when the machine has ended (+0xB4 = 10) and 0x4BF38A7 is counted more than
// when our editor was requested, it is resumed once. Off: the game stays paused (rel/0.7.16's first run).
static bool g_unpause_fix = true;
COOP_FIX("core.own_civ_unpause", "BUG-1323", g_unpause_fix,
         "civ: after a guest's own City Hall or vehicle editor at its Civ arrival its game goes on (it stayed paused, the vehicle editor never came)");
constexpr uint32_t kModalPause = 0x4BF38A7u; // a UI modal / the Civ return's own pause (docs/re/civ.md#civ-own-arrival)
static int g_modal_at_open = 0;              // its count when our editor was requested
static double g_unpause_due_at = -1;         // our editor returned to Civ at
static int g_civ_state = -1;                 // cGameModeCiv +0xB4 at the last look (status)
static unsigned g_unpaused, g_unpause_skipped;

static int modal_pauses()
{
    for (auto& [r, c] : world::pause_reasons())
        if (r == kModalPause)
            return c;
    return 0;
}

static void unpause_tick(double now)
{
    if (g_unpause_due_at < 0)
        return;
    if (game::mode_id() != modes::kCiv || now - g_unpause_due_at > 20.0) {
        g_unpause_skipped++;
        LOG("civ arrival: the Civ return's pause was not looked at (mode %08X, %.0f s, state %d, BUG-1323)",
            game::mode_id(), now - g_unpause_due_at, g_civ_state);
        g_unpause_due_at = -1;
        return;
    }
    static uintptr_t civ_get = H->sym("SporeCoop::GameCiv_Get");
    void* gc = civ_get ? skg::cdecl_call<void*>(civ_get) : nullptr;
    if (!gc || !skg::readable((char*)gc + 0xB4, 4))
        return;
    g_civ_state = skg::field<int>(gc, 0xB4);
    if (g_civ_state != 10)
        return; // (its return still runs)
    g_unpause_due_at = -1;
    int n = modal_pauses();
    if (n <= g_modal_at_open)
        return;
    static uintptr_t tm_get = H->sym("Simulator::cGameTimeManager::Get"),
                     resume = H->sym("Simulator::cGameTimeManager::Resume");
    void* tm = tm_get ? skg::cdecl_call<void*>(tm_get) : nullptr;
    if (!tm || !resume)
        return;
    skg::thiscall<int>(resume, tm, kModalPause);
    g_unpaused++;
    LOG("civ arrival: back from our own editor the Civ return left the game paused (0x4BF38A7 %d, %d before the editor): "
        "resumed (BUG-1323)", n, g_modal_at_open);
}

// session_glue (the host's vote ended on this guest): `editor` the editor of that vote, `mode` this game's stage
void civ_arrival_vote(uint32_t editor, uint32_t mode)
{
    if (!tribe_end_editor(editor) || mode != modes::kTribe || guest_elsewhere() || !allowed())
        return;
    if (g_phase != kNone && g_phase != kDone)
        return; // (the vehicle editor's vote after the City Hall's: one due is enough)
    double now = H->time_seconds();
    g_phase = kDue;
    g_late = false;
    g_due_at = g_since = now;
    g_step = 0;
    g_res[0] = g_res[1] = Result{};
    g_why = "";
    g_dues++;
    LOG("civ arrival: the host's end-of-Tribe editor %08X: this guest's own City Hall and vehicle editors open after "
        "its arrival in Civ (BUG-1276)", editor);
    feed("Civilization: your own City Hall and vehicle editors open when you have arrived in the Civilization stage");
}

// the vehicle editor the game's end of Tribe would open here (the consequence archetype, cPlayer +0x1104)
static uint32_t vehicle_editor()
{
    void* nm = game::noun_manager();
    void* pl = nm && skg::readable((char*)nm + 0x74, 4) ? skg::field<void*>(nm, 0x74) : nullptr;
    uint32_t a = pl && skg::readable((char*)pl + 0x1104, 4) ? skg::field<uint32_t>(pl, 0x1104) : 0;
    return a == 0xC2CA9495u ? 0xF670AA43u : a == 0x60A78928u ? 0x9AD7D4AAu : 0x7D433FADu;
}

static void* planner()
{
    static uintptr_t a = H->sym("SporeCoop::Planner_Instance");
    void* p = a ? *(void**)a : nullptr;
    return p && skg::readable(p, 0x210) ? p : nullptr;
}

// the editor request of the end of Tribe (0xCD1D60), in Civ: our own and not the vote's
static bool open_editor(uint32_t editor)
{
    uintptr_t ctor = H->sym("EditorRequest_ctor"), alloc = H->sym("GeneralAllocator::new_"),
              submit = H->sym("Editors::EditorRequest::Submit");
    if (!ctor || !alloc || !submit)
        return false;
    void* req = skg::cdecl_call<void*>(alloc, (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
    if (!req)
        return false;
    skg::thiscall<void*>(ctor, req); // (+0x1C the current game mode, the reference count 0)
    skg::vcall<int>(req, 1);         // AddRef (the game's intrusive pointer, 0x61DF80)
    skg::field<uint32_t>(req, 0xC) = editor;
    skg::field<uint32_t>(req, 0x90) = kTribeTag;
    skg::field<uint8_t>(req, 0x34) = 0;
    skg::field<uint8_t>(req, 0x36) = 1;
    skg::field<uint8_t>(req, 0x37) = 1;
    skg::field<uint8_t>(req, 0x38) = 1;
    skg::field<uint8_t>(req, 0x39) = 0;
    skg::field<uint8_t>(req, 0x3B) = 1; // the red cross (the Tribe's has none: the host must build its City Hall);
                                        // +0x39 0 / +0x3C 1: no "replace" dialog (BUG-1255)
    skg::field<uint8_t>(req, 0x3C) = 1;
    skg::field<uint8_t>(req, 0x3D) = 1;
    skg::field<uint8_t>(req, 0x64) = 1;
    bool r{};
    {
        callguard::Hold vote_bypass(g_vote_bypass); // (our own request: no vote, no BUG-1158 refusal)
        r = skg::cdecl_call<bool>(submit, req);
    }
    skg::vcall<int>(req, 2); // Release (Submit keeps its own reference)
    return r;
}

static void open_task(void*)
{
    g_queued = false;
    if (g_phase != kOpening)
        return;
    double now = H->time_seconds();
    // the player may have left the stage, a world may load, the pause menu may be up: wait again
    if (game::mode_id() != modes::kCiv || mode_switch_pending() || world::load_queued() || pause_ui_up() || g_vote.active) {
        set_phase(g_step ? kNext : kSettling, now);
        g_since = now - (g_step ? kNextSettle : kSettle) + 3.0; // (3 s more)
        return;
    }
    uint32_t ed = g_editor[g_step];
    g_modal_at_open = modal_pauses(); // BUG-1323
    bool ok = open_editor(ed);
    LOG("civ arrival: own %s editor %08X %s (%.0f s after the arrival, BUG-1276)", g_step ? "vehicle" : "City Hall", ed,
        ok ? "requested" : "refused by the game", now - g_arrived_at);
    if (!ok) {
        g_open_failed++;
        drop("the game refused the editor request", "Civilization: your own editor did not open; make your designs in "
                                                      "your city's planner");
        return;
    }
    g_opened[g_step]++;
    civedit::own_opened(); // BUG-1298: its return stays ours if the session ends while it is open
    g_opened_at = now;
    if (g_late_fix && !g_step) { // BUG-1290: offered in this world (the red cross too): not again at a later join
        uint32_t w = g_late ? g_late_world : modes::current_world();
        creations::world_note_set(kNote, w, 1);
        g_late_notes++;
        LOG("civ arrival: own Civ editors offered in world %08X: noted (BUG-1290)", w);
    }
    g_editor_seen = false;
    set_phase(kOpen, now);
    if (!g_step) {
        notice(g_name + " makes its own City Hall and vehicle", -1);
        feed("your own City Hall: your cities show it on every game (the shared capital keeps the host's); the red "
             "cross keeps the civilization's");
    } else
        feed("your own vehicle: the vehicles you buy show it on every game");
}

static void take(int step, const char* from)
{
    Result& r = g_res[step];
    if (r.cancelled || !r.key[0] || r.taken)
        return;
    r.taken = civedit::take_editor(g_editor[step], r.key);
    r.from = from;
    if (r.taken)
        g_taken++;
    LOG("civ arrival: our own %s design %08X/%08X/%08X (design %08X, from %s) %s (BUG-1276)", step ? "vehicle" : "City Hall",
        r.key[0], r.key[1], r.key[2], g_editor[step], from, r.taken ? "goes to our species line" : "was not taken");
}

// civ_editors.cpp (cGameModeCiv's listener, every message 0x30C11C7): the result of our own editor
void civ_arrival_result(const void* data)
{
    void* d = const_cast<void*>(data);
    if (g_phase != kOpen || !d || !skg::readable(d, 0x48) || skg::field<uint32_t>(d, 0xC) != kTribeTag)
        return;
    Result& r = g_res[g_step];
    r.seen = true;
    r.type_id = skg::field<uint32_t>(d, 0x14);
    for (int i = 0; i < 3; i++)
        r.key[i] = skg::field<uint32_t>(d, 0x18 + 4 * i);
    r.cancelled = skg::field<uint8_t>(d, 0x44) != 0;
    g_results++;
    LOG("civ arrival: our editor's result: model type %08X key %08X/%08X/%08X cancelled %d (BUG-1276)", r.type_id, r.key[0],
        r.key[1], r.key[2], (int)r.cancelled);
    if (r.cancelled)
        g_cancels++;
    take(g_step, "the editor's message");
}

// back from the editor: the planner's copy of the result (its listener 0xD130D0 copies every 0x30C11C7 into +0x4C..),
// when cGameModeCiv's listener did not show it to us; then that copy is emptied as the planner's own apply leaves it
// (0xD0E170: +0x4C = -1), so a later planner never applies our design to the civilization (WP-C3's h_Apply would skip
// it in own mode; in host mode it would not)
static void back(int step)
{
    Result& r = g_res[step];
    void* p = planner();
    uint32_t pd = p ? skg::field<uint32_t>(p, 0x4C) : 0;
    const uint32_t* pk = p ? &skg::field<uint32_t>(p, 0x50) : nullptr;
    if (!r.seen && p && pd == g_editor[step] && pk[0]) {
        r.seen = true;
        r.type_id = pd;
        memcpy(r.key, pk, 12);
        r.cancelled = skg::field<uint8_t>(p, 0x5C) != 0;
        g_results++;
        if (r.cancelled)
            g_cancels++;
        take(step, "the planner's copy");
    }
    if (p && pd == g_editor[step] && r.seen && pk[0] == r.key[0]) {
        skg::field<uint32_t>(p, 0x4C) = 0xFFFFFFFFu;
        LOG("civ arrival: the planner's copy of our result for %08X emptied (it is not the civilization's, BUG-1276)", pd);
    }
}

// BUG-1290: this session's first arrival in the host's Civ world: due when the guest missed the end of Tribe
static void late_check(double now, uint32_t m)
{
    bool here = m == modes::kCiv && g_sess.connected() && g_sess.role() == net::Role::Client && designs::civ_player() &&
                !guest_elsewhere() && !world::load_queued() && !mode_switch_pending();
    static double here_since = 0;
    if (here && !g_was_here)
        here_since = now;
    g_was_here = here;
    if (!here || g_late_checked)
        return;
    if (g_late_fix && !allowed() && now - here_since < 30.0)
        return; // (the host's families or the session setting may come a moment after the world)
    g_late_checked = true;
    uint32_t w = modes::current_world();
    const char* why = !g_late_fix                                ? "core.own_civ_late is off"
                      : !allowed()                               ? "not in own mode (or the host keeps no species line)"
                      : g_phase != kNone                         ? "the end of Tribe made them due already"
                      : !w                                       ? "no world name"
                      : creations::world_note(kNote, w)          ? "offered in this world already"
                      : !creations::plays_own(creations::kCreature) ? "this guest plays the host's species here"
                      : species::own_design(kCityHall).instance  ? "its species line has its own City Hall"
                      : !species::design_ids(g_sess.my_id()).empty() ? "it made own Civ designs already (its planner)"
                                                                 : nullptr;
    if (why) {
        g_late_why = why;
        g_late_skips++;
        LOG("civ arrival: no own Civ editors at this arrival in world %08X: %s (BUG-1290)", w, why);
        return;
    }
    g_late = true;
    g_late_world = w;
    g_late_why = "due";
    g_late_dues++;
    g_dues++;
    g_arrivals++;
    g_step = 0;
    g_res[0] = g_res[1] = Result{};
    g_why = "";
    g_due_at = g_arrived_at = now;
    set_phase(kSettling, now);
    LOG("civ arrival: this guest missed the end of Tribe of world %08X: its own City Hall and vehicle editors open when "
        "the stage has settled (BUG-1290)", w);
    feed("Civilization: your own City Hall and vehicle editors open in a moment (you came after the end of Tribe)");
}

void civ_arrival_tick(double now)
{
    double dt = g_last_tick ? now - g_last_tick : 0;
    g_last_tick = now;
    late_check(now, game::mode_id());
    unpause_tick(now); // BUG-1323 (also after the last editor: phase done)
    if (g_phase == kNone || g_phase == kDone || g_queued)
        return;
    // BUG-1298 (designs.own_editor_offline): an open editor of ours is followed to its end without a session (its
    // result goes to our line at the next session; the next editor does not open then)
    bool offline_open = g_phase == kOpen && !g_sess.connected() && designs::g_own_offline_fix && g_fix;
    if (!allowed() && !offline_open) {
        drop(!g_sess.connected() ? "the session ended" : "not in own mode any more", nullptr);
        return;
    }
    uint32_t m = game::mode_id();
    bool editor = m == modes::kEditor || m == modes::kEdit;
    if (g_phase == kDue) { // on its way: the host's editors, the Civ world's transfer, its load
        if (m == modes::kCiv && designs::civ_player() && !guest_elsewhere()) {
            g_arrivals++;
            g_arrived_at = now;
            set_phase(kSettling, now);
            LOG("civ arrival: arrived in the host's Civ world %.0f s after the host's vote: our own editors open when "
                "the stage has settled (BUG-1276)", now - g_due_at);
        } else if (now - g_due_at > kArriveLimit || m == modes::kCell || m == modes::kCreature || m == modes::kSpace) {
            drop("this guest did not arrive in the host's Civ world", "Civilization: your own editors did not open; make "
                                                                         "your designs in your city's planner");
        } else
            g_waits++;
        return;
    }
    if (g_phase == kSettling || g_phase == kNext) {
        bool next = g_phase == kNext;
        double limit = next ? kNextLimit : kSettleLimit, settle = next ? kNextSettle : kSettle;
        if (m != modes::kCiv || guest_elsewhere()) {
            if (!editor && !(m == modes::kLoad || mode_switch_pending() || world::load_queued()))
                drop("this guest left the host's Civ world", nullptr);
            g_since = now; // (a load or another editor of the player's: the settle counts again)
            return;
        }
        unsigned b = game_busy() & busy::kEditorGate;
        if ((b & busy::kPaused) && dt > 0 && dt < 5.0)
            (next ? g_back_at : g_arrived_at) += dt; // a pause does not count against the limit
        if (now - (next ? g_back_at : g_arrived_at) > limit) {
            drop("the Civ stage did not settle", "Civilization: your own editor did not open; make your designs in your "
                                                  "city's planner");
            return;
        }
        if (b || g_vote.active || mode_switch_pending() || world::load_queued() || world::cinematic_playing()) {
            g_since = now;
            g_waits++;
            return;
        }
        if (now - g_since < settle) {
            g_waits++;
            return;
        }
        if (!next && g_late && species::own_design(kCityHall).instance) {
            g_late_why = "its species line has its own City Hall (the host's store)";
            drop(g_late_why, nullptr); // (a stored row came after the arrival: nothing to offer)
            return;
        }
        if (next)
            g_step = 1;
        g_editor[1] = vehicle_editor();
        set_phase(kOpening, now);
        g_queued = true;
        H->run_outside_frame(open_task, nullptr); // editors create D3D resources: outside the frame
        return;
    }
    if (g_phase == kOpen) {
        if (editor) {
            g_editor_seen = true;
            return;
        }
        if (!g_editor_seen) {
            if (now - g_opened_at > kOpenLimit)
                drop("the editor did not show", "Civilization: your own editor did not open; make your designs in "
                                                "your city's planner");
            return;
        }
        if (m != modes::kCiv)
            return; // (the way back)
        if (g_unpause_fix)
            g_unpause_due_at = now; // BUG-1323
        back(g_step);
        g_back_at = now;
        Result& r = g_res[g_step];
        if (g_step == 0 && !r.cancelled && !offline_open) {
            set_phase(kNext, now);
            return;
        }
        g_phase = kDone;
        g_why = r.cancelled ? (g_step ? "the vehicle editor was cancelled" : "the City Hall editor was cancelled") : "done";
        LOG("civ arrival: our own Civ editors are done (%s; City Hall %08X, vehicle %08X, BUG-1276)", g_why.c_str(),
            g_res[0].key[0], g_res[1].key[0]);
        if (r.cancelled && !g_step)
            feed("you kept the civilization's City Hall and vehicle; your city's planner makes your own later");
    }
}

std::string civ_arrival_status()
{
    double now = H->time_seconds();
    char b[640];
    snprintf(b, sizeof b,
             "civarrival: fix=%d allowed=%d phase=%s step=%d due=%.0f arrived=%.0f editors=%08X/%08X dues=%u "
             "arrivals=%u opened=%u/%u open_failed=%u results=%u taken=%u cancels=%u dropped=%u waits=%u why='%s'\n",
             (int)g_fix, (int)allowed(), phase_name(g_phase), g_step, g_phase != kNone ? now - g_due_at : -1.0,
             g_arrived_at ? now - g_arrived_at : -1.0, g_editor[0], g_editor[1], g_dues, g_arrivals, g_opened[0],
             g_opened[1], g_open_failed, g_results, g_taken, g_cancels, g_dropped, g_waits, g_why.c_str());
    std::string s = b;
    uint32_t w = modes::current_world();
    snprintf(b, sizeof b, "  late: fix=%d this=%d checked=%d world=%08X noted=%u dues=%u skips=%u notes=%u why='%s' (BUG-1290)\n",
             (int)g_late_fix, (int)g_late, (int)g_late_checked, w, creations::world_note(kNote, w), g_late_dues,
             g_late_skips, g_late_notes, g_late_why.c_str());
    s += b;
    snprintf(b, sizeof b, "  unpause: fix=%d due=%d civ_state=%d modal_at_open=%d modal_now=%d resumed=%u skipped=%u (BUG-1323)\n",
             (int)g_unpause_fix, (int)(g_unpause_due_at >= 0), g_civ_state, g_modal_at_open, modal_pauses(), g_unpaused,
             g_unpause_skipped);
    s += b;
    for (int i = 0; i < 2; i++) {
        const Result& r = g_res[i];
        snprintf(b, sizeof b, "  result %s editor=%08X seen=%d type=%08X key=%08X/%08X/%08X cancelled=%d taken=%d from='%s'\n",
                 i ? "vehicle" : "cityhall", g_editor[i], (int)r.seen, r.type_id, r.key[0], r.key[1], r.key[2],
                 (int)r.cancelled, (int)r.taken, r.from);
        s += b;
    }
    return s;
}

// coop.civarrival [status | due]: `due` (tests) makes the own editors due as if the host's end-of-Tribe vote had just
// ended on this guest in Tribe (the arrival path; nothing else of the Tribe -> Civ change runs)
static int c_civarrival(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string a = argc > 1 ? argv[1] : "status";
    if (a == "forget") { // tests (BUG-1290): this world's note gone (the next session's arrival offers them again)
        creations::world_note_set(kNote, modes::current_world(), 0);
        H->out_printf(out, "forgotten\n");
    }
    if (a == "due") {
        uint32_t m = game::mode_id();
        civ_arrival_vote(kCityHall, modes::kTribe);
        H->out_printf(out, "%s (this game in %s)\n", g_phase == kDue ? "due" : "not due (see why / allowed)",
                      modes::mode_name(m));
    }
    H->out_printf(out, "%s", civ_arrival_status().c_str());
    return 0;
}

static void reset()
{
    g_late = g_late_checked = g_was_here = false; // BUG-1290: the next session's first arrival is looked at
    g_late_world = 0;
    if (g_phase == kOpen && designs::g_own_offline_fix && g_fix)
        return; // BUG-1298: our open editor is followed to its end (civ_arrival_tick)
    g_phase = kNone;
    g_step = 0;
    g_queued = false;
}
static size_t held() { return g_phase != kNone && g_phase != kDone ? 1 : 0; }
SESSION_STATE("core.own_civ_arrival", &g_fix, reset, held);

void civ_arrival_init()
{
    H->command_add(M, "coop.civarrival",
                   "coop.civarrival [status | due | forget] - a guest's own City Hall and vehicle editors at its arrival in "
                   "Civ (WP-X1, BUG-1276; a guest that missed the end of Tribe: BUG-1290)",
                   c_civarrival, nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace coop_core
