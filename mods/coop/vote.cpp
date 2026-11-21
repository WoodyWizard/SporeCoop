// The evolution vote and the editors of the coop core (B2 of 0.7.15: moved out of coop.cpp, docs/audit-0.7.15/core.md
// CORE-2): the host's editor request waits for a vote (or opens at once with offers, BUG-0516), action votes, the
// Cell -> Creature switch vote (BUG-0315), the guest's own editor (build, open, submit, hold), the editor hooks
// (SelectUI, StageMsg, Submit, BlockingErrors, CreatureGame / Simulator messages, CellToCreature), what makes the game
// busy for a share (game_busy, save_blocker) and the editor-pending flag (BUG-0920). The MSG_VOTE handler is in the
// session callbacks.
#include "tribe_editors.h"
#include "coop_state.h"
#include "arrival_rule.h"
#include "designs.h"
#include "civ_editors.h"

namespace coop_core {

// Civilization and Space editors change designs (buildings, vehicles, ships, the anthem), not the
// species: the vote and the notices say so (until 2026-10-23 a house design "evolved the species")
bool design_stage(uint32_t m) { return m == modes::kCiv || m == modes::kSpace; }
void add_beacon(const net::Vec3& p, const std::string& text, double seconds, uint32_t argb);
bool start_action_vote(const std::string& text, const std::string& done, std::function<void()> action);

// Phase events of the host. The species is shared: when the host comes back from an editor
// (evolution inside a stage) or starts a new stage, every guest gets the new world at once.
uint32_t g_last_stage = 0;  // last stage the local player played
bool g_was_in_editor = false;

Vote g_vote;
// BUG-0043: a vote ends with its stage, its world and its session (its action or editor request is
// dropped, not run later on a freed civilization or in another save). `coop.fix core.vote_scope off`
// restores the old behaviour (tests).
bool g_vote_scope_fix = true;
COOP_FIX_UNSAFE("core.vote_scope", "BUG-0043", g_vote_scope_fix, "core: a vote ends with its stage, world and session");
// BUG-0234: a guest takes the editor id also from the message that ends a vote: a cancelled vote
// (drop_vote) ends with 0 and opens no own editor; a passed one (finish_vote) ends with its id.
// Off: the id only from "on" messages, every ended vote opened the guest's editor (0.7.3.0).
bool g_vote_drop_editor_fix = true;
COOP_FIX("core.vote_drop_editor", "BUG-0234", g_vote_drop_editor_fix, "core: a cancelled vote opens no guest editor");
// BUG-0234: a guest's own editor still due from a vote is forgotten when the session ends (it
// opened offline after a Leave or a lost host)
bool g_own_editor_end_fix = true;
COOP_FIX("core.own_editor_end", "BUG-0234", g_own_editor_end_fix, "core: a guest's pending own editor ends with the session");
unsigned g_vote_actions_run; // tests: action votes whose action ran
unsigned g_votes_dropped;    // tests: votes ended without running their result
bool g_vote_bypass = false;
CALL_GUARD("coop.vote_bypass", g_vote_bypass); // BUG-1047
bool g_vote_opened_ui = false; // the vote opened the co-op window: it closes again afterwards
double g_vote_last_send = 0;
Submit_t o_Submit;
void broadcast_vote();
void set_ready(bool on);

std::function<void()> g_vote_action; // a vote for something else than an editor (a superweapon ...)
std::string g_vote_done = "evolution starts";

// BUG-0315 (docs/09-coop-design.md 9.3): the host's click on History's check at the end of the Cell stage started the
// Creature stage at once (0xE62440, SporeCoop::CellStage_ToCreature) and the guests were pulled after it; only the first
// Creature editor waited for a vote. On: in a session the host's switch waits for a vote ("wants to move on to the
// Creature stage"); when it passes, the game's own switch runs; the first Creature editor's vote that follows counts
// every guest that has arrived in the host's Creature world as ready (they voted already). A guest's own click on the
// check in the host's Cell world does not take it to Creature alone: during the vote it is its "I'm ready". Off: as
// in 0.7.10. A session setting later (docs/plan-0.7.12.md F3).
bool g_switch_vote_fix = true;
COOP_FIX("core.switch_vote", "BUG-0315", g_switch_vote_fix, "core: the host's Cell -> Creature switch (History's check) waits for a vote of everybody");
double g_switch_passed_at = -1000; // host: the switch vote passed (the arrival vote follows within 300 s)
unsigned g_switch_votes, g_switch_held, g_switch_guest_held, g_switch_arrivals; // tests

// BUG-0516: the host's evolution in the Creature stage (the mating call's request, tag +0x90 0x64AA168) waited for
// every guest's ready (the vote, at most 60 s) although no world reload follows it since BUG-0508. On: the host's
// editor opens at once (no vote); every guest in the host's world gets an offer for 90 s: the co-op window opens with
// "Evolve now" (the ready button), which opens the guest's own editor when its stage has settled (own_editor_tick, as
// after a vote); a guest that does not take it plays on (the host in an editor counts as away, BUG-0205) and can evolve
// at its nest later. Stage starts (the first Creature editor, Creature -> Tribe), Cell and the design stages keep the
// vote. Off: the vote as in 0.7.10. A session setting later (docs/plan-0.7.12.md F3).
bool g_editor_now_fix = true;
// F5 (BUG-0769): the session setting creature.mode = host (everybody plays the host's creature) is in force here
bool host_creature_mode() { return settings::get(settings::kCreatureMode) == 1; }
// BUG-0920: an editor request the mod took and will never submit (evo_flag_tick clears the game's flag at once)
bool g_evo_flag_refused = false;
COOP_FIX("core.editor_now", "BUG-0516", g_editor_now_fix, "core: the host's Creature evolution editor opens at once; each guest gets an Evolve now offer instead of the ready vote");
// BUG-1085: core.editor_now keyed on the request's tag +0x90 == 0x64AA168, taken for the mating call's. It is the
// stage-end editor's (CreatureGame_HandleMessage 0xD42E50 on 0x4471C60 with +0xDE set, which only the Timeline's evolve
// button sets: Creature -> Tribe, docs/re/creature.md#creature-mating-call); the mating editor is 0xD3C650 with tag
// 0x12191CA (also the tag of the first Creature editor after Cell, 0xFD4902BD, which BUG-0315's arrival vote owns). So
// Creature -> Tribe went on without the vote (the guests got a pointless "Evolve now") and every mating evolution waited
// for it, the reverse of docs/09-coop-design.md 9.3. On: the mating request (0x12191CA, not the landfall editor, not
// right after the switch vote) opens at once with offers; the stage-end request (0x64AA168) is a stage start: the host's
// waits for the vote "wants to move on to the Tribe stage", a guest's in the host's Creature world is refused (its I'm
// ready during that vote) and never takes that guest to Tribe alone. Off: as 0.7.13 (keyed on 0x64AA168, a guest's
// stage-end request reaches its game).
static bool g_editor_tag_fix = true;
COOP_FIX("core.editor_now_tag", "BUG-1085", g_editor_tag_fix, "core: Creature: the mating evolution opens at once with offers, the stage-end editor (Creature -> Tribe) waits for the vote and a guest cannot start it alone");
unsigned g_stage_end_votes, g_stage_end_guest_refused; // tests (coop.vote)
// BUG-1245 (players' video: the host's mating call, then a black screen for 25+ s on which the mating scene and the egg
// laying came again and again, 6 eggs): the Cell game asks for its editor after the mating scene; while the vote holds
// that request the avatar and the mate stay within the meeting distance, so the cell step's meeting 0xE78D10 played the
// mating, the egg laying and the request again every ~10 s (each refused by h_Submit: one vote per editor), and each
// round faded the picture in and out. On: as soon as the vote holds the host's Cell request, the mate G+0x51D4 is no
// longer the mate (its index forgotten, the cell stays; given back when the vote is dropped and the stage goes on), so
// the scene plays once. Off: the meetings go on while the vote holds (0.7.15).
static bool g_vote_mate_fix = true;
COOP_FIX("cell.vote_mate", "BUG-1245", g_vote_mate_fix,
         "cell: while the host's evolution vote holds its editor the mate does not meet the host's cell again (the mating scene and the eggs repeated every 10 s)");
static uint32_t g_held_mate = 0;     // the mate taken out while the vote holds (0: none)
// BUG-1263 (the players' video: "a black screen for 25+ s" after the host's mating call in Cell): the host's Cell
// evolution (the request after the egg laying, 0xE72AC0) waited for the vote while the game had already faded its picture
// out, so the host saw one black picture until every guest pressed I'm ready (up to 60 s). The vote gave the guests
// nothing: it opens no editor for them in Cell (docs/re/cell.md#cell-editor-vote-timing), a guest evolves through its own
// mating call, and after the host's editor a guest that plays the host's cell takes the new one in place either way
// (BUG-0508). On, with the session setting core.editor_now (the host's editor opens at once): the host's Cell request
// goes to the game at once (only the save / new-body holds), the guests get a notice; the Cell -> Creature switch keeps
// its vote (BUG-0315). Off, or that setting off: the vote of 0.7.15 (cell.vote_mate keeps its round single).
static bool g_cell_now_fix = true;
COOP_FIX("cell.editor_now", "BUG-1263", g_cell_now_fix,
         "cell: the host's Cell evolution editor (its mating call) opens at once, the others get a notice (the vote kept the host's screen black up to 60 s)");
static unsigned g_cell_now = 0; // tests (coop.vote): the host's Cell editors opened without a vote
unsigned cell_editor_now_count() { return g_cell_now; }
// BUG-1265 (evo_screens_cell_one, creature.mode host: one creature for everyone, only the host evolves): a guest's own
// mating call in Cell is refused at h_Submit, but its mate G+0x51D4 stayed next to its cell, so the cell step's meeting
// 0xE78D10 played the mating scene, the egg laying with the game's fade and the refused request again every ~10 s: the
// guest's screen went black again and again (the loop of BUG-1245 on a refused request). Forgetting the mate only at the
// refusal ended the loop but left the guest's picture black (the fade of the egg laying is undone only by an editor's
// return or the next round, lab 20261118-142112: 36 s). On: in that mode a guest's mate is forgotten as soon as its
// mating call brings it (cellsync::forget_mate, every frame from evo_flag_tick, outside the game's own call chain), so
// no mating scene, egg or fade starts, and the guest reads why; a request that still comes is refused and its mate
// forgotten too. Off: the meetings go on (0.7.15).
static bool g_refused_mate_fix = true;
COOP_FIX("cell.refused_mate", "BUG-1265", g_refused_mate_fix,
         "cell: creature.mode host: a guest's mating call brings no mating round (its mate is forgotten): no mating scene, egg and black fade every 10 s");
static bool g_refused_mate_due = false;
static unsigned g_refused_mates = 0; // tests (coop.vote)
unsigned refused_mates_count() { return g_refused_mates; }
static unsigned g_vote_mates_held = 0; // tests (coop.vote)
unsigned vote_mates_held() { return g_vote_mates_held; }
// BUG-1245 (the same class: the host's game black behind a vote that holds its editor request): the first Creature
// editor after the switch vote (the landfall editor FD4902BD, BUG-0315's arrival vote) waited up to 60 s for every guest
// to arrive in the host's Creature world; a guest still receiving the world over a slow link kept the host's screen
// black (with the co-op window) for the whole 60 s (lab evo_screens_switch_slow, host 0-60 s black). Since BUG-1240
// (core.own_editor_arrival) a guest on its way keeps its own editor due and opens it after its arrival, so the host's
// editor need not wait for it. On: the arrival vote does not wait for a guest that is still on its way and has that
// fix (its creature family minor >= 1, net/messages.def); it ends as soon as the guests that arrived are ready. Off, or
// an older guest: waited for (0.7.15).
static bool g_arrival_nowait_fix = true;
COOP_FIX("core.arrival_nowait", "BUG-1245", g_arrival_nowait_fix,
         "core: the host's first Creature editor does not wait for a guest still on its way (it gets its own editor when it arrives); the host's screen stayed black meanwhile");
static unsigned g_arrival_not_waited = 0; // tests (coop.vote): arrival votes that did not wait for a guest on its way
static int g_arrival_onway_last = 0;
unsigned arrival_not_waited() { return g_arrival_not_waited; }
// BUG-1336: BUG-1245 waited for no guest on its way at all, and every guest is on its way when the host's landfall
// editor asks (the host's share of its Creature world starts only after the host's entry; since BUG-1269 the host also
// enters ~4 s after the vote): the first Creature editor's vote ended at once in every switch, each guest got its own
// arrival editor later and the party no longer evolved together (lab 20261119-214823: leave_avatar_cell,
// legless_guest, parts_transition, `1 player(s) on their way ... not waited for` 0.0 s after the vote's start; 0.7.15:
// `ready 2/2 after 9.5 s`). On: a guest on its way is waited for while its arrival is quick: its share not out yet
// or still being sent for at most arrival_rule::kSendWait s of the vote (the host's save and a transfer over a normal
// link take ~5 s), or out and acknowledged within arrival_rule::kLoadWait s (it loads the world, ~5 s); a guest in its
// own editor is not (BUG-1239), nor one whose share is still on a slow link after kSendWait (BUG-1245: the host's
// picture stays black behind the vote, evo_screens_switch_slow). Off: BUG-1245 as it was (no guest on its way waited for).
static bool g_arrival_wait_fix = true;
COOP_FIX("core.arrival_wait", "BUG-1336", g_arrival_wait_fix,
         "core: the host's first Creature editor waits for a guest that arrives quickly (the party evolves together); not for a slow link");
static unsigned g_arrival_waited = 0; // tests (coop.vote): arrival votes that waited for a guest on its way
static int g_arrival_waiting_last = 0;
unsigned arrival_waited() { return g_arrival_waited; }

// the guest takes its own landfall editor after a late arrival (BUG-1240): its build announces creature 1.1 or newer
static bool peer_takes_arrival_editor(uint8_t id)
{
    bool has = false;
    std::vector<net::FamilyRev> fam;
    if (!g_sess.peer_families(id, has, fam) || !has)
        return false;
    for (auto& f : fam)
        if (f.family == 3 && f.rev == 1 && f.minor >= 1) // creature (net/messages.def)
            return true;
    return false;
}
uint32_t g_offer_editor = 0;   // guest: the editor of the host's offer (0 = none)
double g_offer_until = 0;
bool g_offer_opened_ui = false;
unsigned g_offers_sent, g_offers_got, g_offers_taken, g_offers_expired; // tests
extern double g_own_editor_due_at, g_own_editor_due_first;
void broadcast_offer(uint32_t editor, const std::string& text);
void offer_end(const char* why);

// a vote whose result runs `action` (outside the frame) when everybody is ready or time is up
bool start_action_vote(const std::string& text, const std::string& done, std::function<void()> action)
{
    if (g_vote.active)
        return false;
    g_vote = Vote{true, nullptr, H->time_seconds(), text, 60, 0u, game::mode_id(), modes::current_world()};
    g_vote_action = std::move(action);
    g_vote_done = done;
    trace::line("vote", "start (action) '%s' players=%d", text.c_str(), (int)g_sess.players().size());
    for (auto& p : g_sess.players())
        g_ready[p.id] = p.id == 0;
    g_lobby_dirty = true;
    broadcast_vote();
    notice(text + ": press I'm ready (F9)", -1);
    g_vote_opened_ui = !coop_ui::is_open();
    coop_ui::open(true);
    return true;
}

void start_vote(void* req, const std::string& text)
{
    g_vote_action = nullptr;
    g_vote_done = design_stage(game::mode_id()) ? "the editor opens" : "evolution starts";
    if (req)
        skg::vcall<int>(req, 1); // AddRef: kept until the vote ends
    g_vote = Vote{true, req, H->time_seconds(), text, 60, req ? skg::field<uint32_t>(req, 0xC) : 0u,
                  game::mode_id(), modes::current_world()};
    // BUG-0315: the first Creature editor right after a passed switch vote: everybody voted for it already
    g_vote.arrival = g_switch_vote_fix && settings::on(settings::kSwitchVote) && req && g_vote.mode == modes::kCreature &&
                     H->time_seconds() - g_switch_passed_at < 300.0;
    if (g_vote.arrival)
        g_switch_passed_at = -1000;
    trace::line("vote", "start '%s' editor=%s %s players=%d%s", text.c_str(), trace::hex(g_vote.editor).c_str(),
                modes::mode_name(g_vote.mode), (int)g_sess.players().size(),
                g_vote.arrival ? " (after the switch vote: a guest that arrived is ready)" : "");
    for (auto& p : g_sess.players())
        g_ready[p.id] = p.id == 0;
    g_lobby_dirty = true;
    broadcast_vote();
    notice(text + ": press I'm ready (F9)", -1);
    g_vote_opened_ui = !coop_ui::is_open();
    coop_ui::open(true);
}

SelectUI_t o_SelectUI;
unsigned g_select_guards; // selections that needed the guard (tests)
void SK_CDECL h_SelectUI(void* sel, void* a2, void* a3, uint32_t a4, float a5)
{
    uint32_t* flags = nullptr;
    void* obj = sel && !IsBadReadPtr(sel, 8) ? *(void**)((uint8_t*)sel + 4) : nullptr;
    void* cr = obj ? skg::vcall<void*>(obj, 0xC / 4, 0xD0036E08u) : nullptr; // cCreatureAnimal
    if (cr && !IsBadReadPtr((uint8_t*)cr + 0xE84, 4) && *(void**)((uint8_t*)cr + 0xE84) == nullptr) {
        flags = (uint32_t*)((uint8_t*)cr + 0xB58);
        if (!(*flags & 1))
            flags = nullptr;
    }
    size_t mark = game::swap_depth();
    if (flags) {
        game::swap_open(game::swap_undo_or32, flags, 1u, "selection flag"); // BUG-0110
        *flags &= ~1u;
        g_select_guards++;
    }
    o_SelectUI(sel, a2, a3, a4, a5);
    if (flags) {
        *flags |= 1u;
        game::swap_close(mark);
    }
}

StageMsg_t o_StageMsg;
alignas(16) uint8_t g_blank_e84[0x1000];
unsigned g_stage_msg_guards; // handler calls that needed the blank object (tests)
unsigned g_stage_msg_other;  // ... for a creature of the message that was none of ours (tests)
bool SK_THISCALL h_StageMsg(void* self, uint32_t id, void* msg)
{
    void* patched[257];
    int n = 0;
    if (g_mode == modes::kCreature)
        for (void* c : game::puppets()) {
            void** e84 = (void**)((uint8_t*)c + 0xE84);
            if (n < 256 && !IsBadReadPtr(e84, 4) && *e84 == nullptr && game::noun_id(c) == 0x18EB45Eu) {
                *e84 = g_blank_e84;
                patched[n++] = c;
            }
        }
    // the two messages whose msg+4 is the creature read at 0xD48634 (cast with noun 0x18EB45E, 0xAC8010)
    if ((id == 0xD335362Cu || id == 0x60B4123u) && msg && !IsBadReadPtr(msg, 8)) {
        void* c = ((void**)msg)[1];
        void** e84 = c ? (void**)((uint8_t*)c + 0xE84) : nullptr;
        if (c && !IsBadReadPtr(c, 0xE88) && !IsBadReadPtr(*(void**)c, 0x24) && game::noun_id(c) == 0x18EB45Eu &&
            *e84 == nullptr) {
            *e84 = g_blank_e84;
            patched[n++] = c;
            g_stage_msg_other++;
        }
    }
    if (id == 0x60B4123u)
        social::g_stage_msg_win++; // BUG-0179 (tests)
    if (n)
        g_stage_msg_guards++;
    size_t mark = game::swap_depth(); // BUG-0110: a fault in the handler leaves no blank +0xE84 behind
    for (int i = 0; i < n; i++)
        game::swap_open(game::swap_undo_ptr_if, (uint8_t*)patched[i] + 0xE84, (uintptr_t)g_blank_e84, "blank +0xE84");
    size_t own = game::swap_depth() - mark;
    bool r = o_StageMsg(self, id, msg);
    game::swap_close(mark, own);
    for (int i = 0; i < n; i++) { // the handler may have destroyed nothing of ours: restore only our pointer
        void** e84 = (void**)((uint8_t*)patched[i] + 0xE84);
        if (!IsBadReadPtr(e84, 4) && *e84 == g_blank_e84)
            *e84 = nullptr;
    }
    return r;
}

// The own creature usually comes from the Creature Creator (My Creations) and may hold parts that
// are not in the stage editor's part list. The editor then marks that error (bit 5) as blocking
// when it loads the creature, and "done" only shows the error and stays in the editor (cancel
// works). In the own editor such parts are allowed: the bit is dropped from the blocking set
// (docs/re/creations.md 7).
bool g_own_editor = false; // the own editor was requested and the player has not left it yet
bool g_own_editor_charge_due = false; // BUG-0466: the own editor opened; its result charges once
// BUG-0811: a refused Submit cleared g_own_editor but left the price due (the next editor result without the evolution
// tag was charged as the own editor's), and the session end reset neither (the next editor of a later session waived the
// part-list error). The game's own Submit (0x5A92C0) always returns 1: only the mod's hook refuses (its test capture),
// so this is hardening. `coop.fix creations.own_editor_flags off`: as 0.7.11.
bool g_own_editor_flags_fix = true;
COOP_FIX("creations.own_editor_flags", "BUG-0811", g_own_editor_flags_fix, "creature: a refused own editor request or the session end leaves no own-editor price or part waiver for a later editor");
// BUG-1308 (lab 20261119-095414 tribe_own_outfitter, 20261119-104136 own_editor_drop): a guest's own Tribal Outfitter
// (BUG-1272) and Civ outfitter (BUG-1275) open for its own creature, whose Creature Creator parts are not in the
// outfitter's part list either: "done" stayed in the editor. They are own editors too: the part-list error is waived
// while one of ours is on its way or open. Off: only the own Creature editor (0.7.15).
static bool g_own_outfit_parts_fix = true;
COOP_FIX("editors.own_outfit_parts", "BUG-1308", g_own_outfit_parts_fix,
         "tribe, civ: a guest's own outfitter for its own creation closes with Done (its Creature Creator parts are allowed, as in its own Creature editor)");
BlockingErrors_t o_BlockingErrors;
unsigned g_part_list_waivers; // tests: how often the part-list error was waived
unsigned g_outfit_part_waivers; // BUG-1308: of them, in a guest's own outfitter
uint32_t* SK_THISCALL h_BlockingErrors(void* ed, uint32_t* out, void* res, void* model, int flag)
{
    uint32_t* r = o_BlockingErrors(ed, out, res, model, flag);
    bool outfit = !g_own_editor && g_own_outfit_parts_fix && (designs::own_outfitter_open() || civedit::own_outfit_open());
    if ((g_own_editor || outfit) && out && (out[0] & 0x20u)) {
        out[0] &= ~0x20u;
        g_part_list_waivers++;
        if (outfit && !g_outfit_part_waivers++)
            LOG("editors: our own outfitter's creation has parts outside its part list: allowed (BUG-1308)");
    }
    return r;
}

// BUG-0368: the creature stage's own evolution request (0xD42F2C) gives the editor the parts the
// species has collected (cPlayer +0x10E8, a cCollectableItems, at request +0x94); the palette shows a
// part locked only with that object (0x5C6820: null = every part available). The guest's own editor
// had none, so it offered every part. The guest's own request gets it too.
unsigned g_own_editor_items_set; // tests: own editor requests that got the collected parts

// the cPlayer's collected creature parts (cCollectableItems), or null
void* collected_parts()
{
    uintptr_t nouns = H->sym("Simulator::cGameNounManager::Get"), getp = H->sym("SporeCoop::cGameNounManager_GetPlayer");
    void* nm = nouns ? skg::cdecl_call<void*>(nouns) : nullptr;
    void* pl = nm && getp ? skg::thiscall<void*>(getp, nm) : nullptr;
    if (!pl || !skg::readable((char*)pl + 0x10E8, 4))
        return nullptr;
    return skg::field<void*>(pl, 0x10E8);
}

// BUG-0720 (class BUG-0431): every EditorRequest the mod builds comes from build_editor_request(), the
// fields the creature stage's own evolution request sets (CreatureGame_HandleMessage 0xD42E50, message
// 0x4471C60, docs/re/creations.md#creations-editor-request-builder): +0xC the editor, +0x10..+0x18 the
// species, +0x94 the collected parts (BUG-0368), the flags +0x34/+0x36/+0x37/+0x38/+0x39/+0x3B = 0 and
// +0x3C/+0x3D = 1 (the constructor 0x5A9140 leaves +0x39, +0x3B at 1 and +0x3C at 0), +0x70 the four
// consequence traits cPlayer +0x10FC..+0x1108, and +0x90 the evolution tag only for kEvolve. What the game
// does and the mod leaves out on purpose (the allow-list of the test, coop.editreq diff): +0x90 for the
// own editor (its price is charged by BUG-0466; with the tag the creature game applies an evolution of the
// world's species), the message 0x5D02A72 after Submit and the byte 0x1686AF0. Before (the switch off):
// open_own_editor and coop.vote editor set only +0xC, the key and +0x94.
bool g_editor_request_fix = true;
COOP_FIX("creations.editor_request", "BUG-0720", g_editor_request_fix, "creature: the editor requests the mod builds (a guest's own editor, the test commands) carry the fields of the game's own evolution request (flags, consequence traits)");
// BUG-0756: the guest's own editor keeps the constructor's +0x39 = 1, +0x3B = 1, +0x3C = 0 (the edit-a-creation editor of
// 0.7.10: its red cross and the Save New / Replace / Don't Save dialog); the game's evolution values (0, 0, 1) gave
// it the evolution editor's UI, with no way to leave without the edit. cEditor::OnEnter (0x58FB8D..0x58FBEF) passes
// +0x39 to 0x5DCDC0 (windows 0x682102A/B hidden when 1, 0x55FDCA8 / 0x47BC958 enabled) and +0x3B to 0x5DCC80
// (window 0x90439D7C, the red cross, visible); +0x3C goes to 0x5BF1E0 with the species key. The rest of BUG-0720
// (the other flags, the consequence traits, +0x94) stays the game's. `coop.fix creations.own_editor_cancel off`:
// the evolution values (no cross). docs/re/creations.md#creations-own-editor-cancel
static bool g_own_editor_cancel_fix = true;
COOP_FIX("creations.own_editor_cancel", "BUG-0756", g_own_editor_cancel_fix, "creature: a guest's own editor has its red cross and the save dialog (the request's +0x39 / +0x3B / +0x3C as the constructor's)");
unsigned g_own_keep_ctor = 0; // tests (coop.editreq keep): own-editor bytes left at the constructor's value (1 +0x39, 2 +0x3B, 4 +0x3C, 16 vec70)
// BUG-1255 (players' video: the guest's first own creature, the evolution editor of the host's world, asked on its check
// mark to replace the host's species): the own editor starts from the avatar's species; for a guest playing the
// world's species (no creation of its own yet: the first Creature editor after Cell, an offer after the host's
// evolution) that is the host's creation, and BUG-0756's edit-a-creation bytes (+0x39 = 1, +0x3C = 0) bring the game's
// "Save as a new creation or replace <the host's species>" dialog. On: such an own editor keeps only the red cross of
// the constructor (+0x3B = 1) and the evolution's +0x39 = 0 / +0x3C = 1 (no replace dialog: done saves a new creature);
// a guest playing its own creation keeps BUG-0756's dialog (replacing its own creation is its choice). Off: 0.7.15.
static bool g_own_no_replace_fix = true;
COOP_FIX("creations.own_editor_no_replace", "BUG-1255", g_own_no_replace_fix,
         "creature: a guest's first own creature (from the world's species) is saved as new without the dialog that offered to replace the host's species");
static bool g_own_from_world = false; // the own editor being built starts from the world's species (not ours)
unsigned g_own_no_replace = 0;        // tests (coop.vote)
unsigned own_no_replace_count() { return g_own_no_replace; }
static void* the_player()
{
    uintptr_t nouns = H->sym("Simulator::cGameNounManager::Get"), getp = H->sym("SporeCoop::cGameNounManager_GetPlayer");
    void* nm = nouns ? skg::cdecl_call<void*>(nouns) : nullptr;
    void* pl = nm && getp ? skg::thiscall<void*>(getp, nm) : nullptr;
    return pl && skg::readable((char*)pl + 0x10E8, 0x24) ? pl : nullptr;
}
// a new request (one reference held by the caller, as the game's new + ctor give it), or null
void* build_editor_request(EditKind kind, uint32_t editor_id, const game::Key& k)
{
    uintptr_t ctor = H->sym("EditorRequest_ctor"), alloc = H->sym("GeneralAllocator::new_");
    if (!ctor || !alloc)
        return nullptr;
    // the game's operator new (GeneralAllocator::new_: size, name, 0, 0, 0, 0), as at 0xD42F1E
    void* req = skg::cdecl_call<void*>(alloc, (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
    if (!req)
        return nullptr;
    skg::thiscall<void*>(ctor, req);
    skg::field<uint32_t>(req, 0xC) = editor_id ? editor_id : 0x465C50BAu; // CreatureEditorExtraLarge
    skg::field<uint32_t>(req, 0x10) = k.instance;
    skg::field<uint32_t>(req, 0x14) = k.type;
    skg::field<uint32_t>(req, 0x18) = k.group;
    bool creature = game::mode_id() == modes::kCreature;
    uintptr_t assign = H->sym("SporeCoop::IntrusivePtr_Assign"), push = H->sym("SporeCoop::VectorU32_PushBack");
    void* pl = the_player();
    if (creature && (kind == EditKind::Evolve || kind == EditKind::Own)) {
        // the native request's intrusive pointer assignment (AddRef the new, Release the old)
        if (void* items = collected_parts(); items && assign) {
            skg::thiscall<void*>(assign, (char*)req + 0x94, items);
            if (kind == EditKind::Own)
                g_own_editor_items_set++;
        }
    }
    if (kind == EditKind::Evolve)
        skg::field<uint32_t>(req, 0x90) = 0x64AA168u;
    if (creature && (g_editor_request_fix || kind == EditKind::Evolve)) {
        for (int o : {0x34, 0x36, 0x37, 0x38, 0x39, 0x3B})
            skg::field<uint8_t>(req, o) = 0;
        skg::field<uint8_t>(req, 0x3C) = 1;
        skg::field<uint8_t>(req, 0x3D) = 1;
        if (kind == EditKind::Own) {
            unsigned keep = g_own_keep_ctor | (g_own_editor_cancel_fix ? 7u : 0u);
            if (g_own_no_replace_fix && g_own_from_world && g_own_editor_cancel_fix && !g_own_keep_ctor) {
                keep = 2u; // BUG-1255: the cross only, no dialog offering to replace the world's (the host's) species
                g_own_no_replace++;
            }
            if (keep & 1) skg::field<uint8_t>(req, 0x39) = 1;
            if (keep & 2) skg::field<uint8_t>(req, 0x3B) = 1;
            if (keep & 4) skg::field<uint8_t>(req, 0x3C) = 0;
        }
        for (int o = 0x10FC; pl && push && o <= 0x1108 && !(kind == EditKind::Own && (g_own_keep_ctor & 16)); o += 4) {
            uint32_t v = skg::field<uint32_t>(pl, o);
            skg::thiscall<void*>(push, (char*)req + 0x70, &v);
        }
    }
    return req;
}

static bool guest_plays_own_creation();
// the creature editor for the own creature (EditorRequest + Submit, like ModAPI mods do)
bool open_own_editor(uint32_t editor_id)
{
    uintptr_t submit = H->sym("Editors::EditorRequest::Submit");
    void* av = game::avatar();
    if (!submit || !av)
        return false;
    // BUG-1255: a guest without a creation of its own in play edits the world's species (the host's)
    g_own_from_world = g_sess.role() == net::Role::Client && !guest_plays_own_creation();
    void* req = build_editor_request(EditKind::Own, editor_id, game::species_key(av));
    g_own_from_world = false;
    if (!req)
        return false;
    bool r{};
    {
        callguard::Hold vote_bypass(g_vote_bypass);
        g_own_editor = true; // the editor loads the creature after Submit, on a later frame
        g_own_editor_charge_due = true; // BUG-0466: its result charges the price
        r = skg::cdecl_call<bool>(submit, req);
    }
    if (!r) {
        g_own_editor = false;
        if (g_own_editor_flags_fix)
            g_own_editor_charge_due = false; // BUG-0811: no editor, no price
    }
    return r;
}

// the simulator's mode switcher has a switch pending (+0x28: a load, the quit to the menu, an editor)
bool mode_switch_pending()
{
    static uintptr_t mgr_ptr = H->sym("SporeCoop::SimulatorModeMgr_ptr");
    void* mgr = mgr_ptr ? *(void**)mgr_ptr : nullptr;
    return mgr && skg::field<uint8_t>(mgr, 0x28);
}

// The one busy gate (busy.h, BUG-0404): why the game is busy right now (busy::k* bits). Cheap,
// callable in the frame.
unsigned game_busy()
{
    uint32_t m = game::mode_id();
    return busy::bits(world::cinematic_playing(), coop_ui::input_lock_now(), world::time_paused(),
                      m == modes::kEditor || m == modes::kEdit,
                      m == modes::kLoad || mode_switch_pending() || world::load_queued());
}

// An editor request that waits until the host's save for a world share is written (a save made
// while the editor opens does not load again: world::save_game). One reference held.
void* g_submit_later = nullptr;
double g_submit_held_at = 0;
double g_editor_submitted_at = -100; // the last editor request that reached the game
bool g_submit_queued = false;        // released, the outside-frame task has not submitted it yet
unsigned g_submits_held; // tests: editor requests that waited for a save
unsigned g_submits_repeated; // tests: editor requests dropped as repeats of one that is opening

// a save for a world share is under way or due (a share that waits for its save: the editor lets
// it save first; a share that cannot save now holds the editor at most 15 s, submit_tick)
// (BUG-0849: not in the end of Tribe: no share save can be made there, the vehicle editor must not wait 15 s for one)
bool tribe_graduating();
bool share_save_due() { return (world::save_busy() || world::save_pending()) && !tribe_graduating(); }

// BUG-0560: the Cell stage's request (0xE84600) submits and then posts the editor's DNA budget (message 0x90A03FDF,
// +8 = DNAForFood(food) - spent); the editor's listener takes it only after it entered (cEditor::OnEnter registers
// it). A held request (the vote, a save, the avatar's new body) let the game post it at once with no editor
// listening: the editor opened with the config's start budget (100) and the game booked spent = budget - remaining
// on the way out, a negative spent that paid again on every visit. On: the budget is posted again after the held
// request's Submit, queued behind it as the game does.
void post_editor_budget(int budget)
{
    uintptr_t get = H->sym("App::IMessageManager::Get"), nw = H->sym("GeneralAllocator::new_");
    uintptr_t vt = H->sym("SporeCoop::EditorBudgetMsg_vtable");
    void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!mm || !nw || !vt)
        return;
    static const char kName[] = "SporeCoop";
    uint32_t* m = skg::cdecl_call<uint32_t*>(nw, 0x40u, kName, 0, 0, 0, 0);
    if (!m)
        return;
    memset(m, 0, 0x40);
    m[0] = (uint32_t)vt; // the game's refcounted message (0xE84600: base vtable, then this one; +4 refcount 0)
    m[0xC] = 0x90A03FDFu;
    m[2] = (uint32_t)budget;
    m[6] = 1;
    skg::vcall<void>(mm, 0x18 / 4, 0x90A03FDFu, (void*)m, (void*)nullptr, 0); // MessagePost (queued)
    LOG("cell: DNA budget %d posted again to the held editor (BUG-0560)", budget);
}

bool submit_now(void* req)
{
    int budget = game::mode_id() == modes::kCell ? cellsync::editor_budget() : -1;
    bool r{};
    {
        callguard::Hold vote_bypass(g_vote_bypass);
        r = o_Submit(req);
    }
    if (r)
        g_editor_submitted_at = H->time_seconds();
    if (r && budget >= 0)
        post_editor_budget(budget);
    return r;
}

// BUG-0404: an editor that opened while a cinematic of the stage played (the evolution's
// CRG_Fade_From_Black_Egg F4F96E43 right after the last editor, a caption, a brain upgrade) came up
// without its buttons, and the cinematic never ended in the editor mode: its input lock stayed on and
// the host could not leave the editor. The vote ends whenever the others are ready or its time is up,
// whatever the host's game plays. The held request now waits at the busy gate (busy::kEditorGate: a
// cinematic, the input lock, the game time paused) and for 3 s after the game came back from an
// editor (the evolution's cinematic starts then). A pause waits as long as it lasts; a cinematic at
// most 60 s, then it is ended natively (EndCinematic) right before the editor opens.
constexpr double kEditorCineWait = 60.0;

// BUG-0576: the game opens some editors from a paused game: at the end of Tribe the stage's listener 0xCD1D60 takes
// the end of TRG2CVG_PreEditor (and of TRG2CVG_ShowCityHall), pauses the game time (reason 0x4BF38A6), raises the
// input lock (cGameInputManager +0x110) and submits the City Hall editor 99E92F05; cGameModeManager's 0xB1DF60
// submits a request under the same pause and resumes after Submit. The pause and the lock are the request's own:
// the game lifts them in the editor. The busy gate held such a request until they ended, so the host's Tribe -> Civ
// change stood on a black screen for 60 s (input lock) or 300 s (pause only). On: the pause and the input lock the
// game had when it submitted the request do not hold it (the pause only while the game's editor pause reason
// 0x4BF38A6 is counted); a cinematic and any later pause or lock still do. Off: they held it (0.7.6).
bool g_editor_own_pause_fix = true;
COOP_FIX("core.editor_own_pause", "BUG-0576", g_editor_own_pause_fix,
         "core: a vote's editor the game requested from a paused game (end of Tribe) does not wait for that pause");
constexpr uint32_t kEditorPauseReason = 0x4BF38A6u;
unsigned g_req_own_busy = 0;     // busy::kPaused / kInputLock the game had when it submitted the held request
unsigned g_submits_own_pause;    // tests: held editor requests that went on under the game's own pause
bool g_own_told = false;         // the held request's own pause was logged
int editor_pause_count()
{
    uintptr_t get = H->sym("Simulator::cGameTimeManager::Get"), cnt = H->sym("Simulator::cGameTimeManager::GetPauseCount");
    void* tm = get ? skg::cdecl_call<void*>(get) : nullptr;
    return tm && cnt ? skg::thiscall<int>(cnt, tm, kEditorPauseReason) : -1;
}
// the busy bits that are the held request's own (masked out at the gate)
unsigned req_own_bits()
{
    if (!g_editor_own_pause_fix)
        return 0;
    unsigned own = g_req_own_busy & busy::kInputLock;
    if ((g_req_own_busy & busy::kPaused) && editor_pause_count() != 0) // -1: no symbol, trust the snapshot
        own |= busy::kPaused;
    return own;
}
double g_editor_left_at = -100;  // the game came back from an editor into a stage
busy::Wait g_submit_wait;        // the held request's wait at the busy gate
bool g_submit_busy_told = false; // the held request's wait was logged
bool g_submit_end_cine = false;  // the outside-frame submit ends the cinematic first
unsigned g_submits_after_cine;   // tests: editor requests that waited for a cinematic
unsigned g_submit_cines_ended;   // tests: cinematics ended because they outlasted the wait

void hold_submit(void* req)
{
    skg::vcall<int>(req, 1); // AddRef
    if (g_submit_later)
        skg::vcall<int>(g_submit_later, 2); // a newer request replaces it
    g_submit_later = req;
    g_submit_held_at = H->time_seconds();
    g_submit_wait = {};
    g_submit_busy_told = false;
    g_own_told = false;
    if (share_save_due()) {
        g_submits_held++;
        LOG("the editor waits until the world save is written");
    }
}

// the held request goes to the game once the save is written (at most 15 s later), outside the
// frame: editors create D3D resources
void submit_tick(double now)
{
    if (!g_submit_later || (share_save_due() && now - g_submit_held_at < 15.0))
        return;
    if (creations::avatar_swap_settling(now) && now - g_submit_held_at < 90.0)
        return;
    bool end_cine = false;
    if (stage_mode(game::mode_id())) { // BUG-0404: the busy gate
        unsigned all = game_busy() & busy::kEditorGate;
        unsigned b = all & ~req_own_bits();
        if (b != all && !g_own_told) {
            g_own_told = true;
            {
                LOG("the editor does not wait for the game's own %s (it requested the editor from it, BUG-0576; editor "
                    "pause count %d)", busy::names(all & ~b).c_str(), editor_pause_count());
            }
        }
        bool expired = false;
        bool settling = now - g_editor_left_at < 3.0; // (the evolution's cinematic is about to start)
        bool was = g_submit_busy_told;
        if (busy::hold(g_submit_wait, b, now, kEditorCineWait, busy::kPaused, 300.0, &expired) || settling) {
            if (!was) {
                g_submits_after_cine++;
                LOG("the editor waits: the game is busy (%s)", busy::names(b).c_str());
                if (b)
                    feed(b & busy::kPaused ? "your editor opens when the game goes on"
                                           : "your editor opens when the cinematic is over");
            }
            g_submit_busy_told = true;
            return;
        }
        end_cine = expired && (b & busy::kCinematic);
        g_submit_busy_told = false;
    }
    void* req = g_submit_later;
    if (req_own_bits() & (game_busy() & busy::kEditorGate))
        g_submits_own_pause++;
    g_submit_later = nullptr;
    g_submit_wait = {};
    g_submit_busy_told = false;
    if (!stage_mode(game::mode_id()) || mode_switch_pending()) { // the host left the stage meanwhile
        LOG("the held editor request is dropped: the game left the stage");
        skg::vcall<int>(req, 2);
        return;
    }
    if (now - g_submit_held_at > 0.05)
        LOG("the editor opens (held %.1f s: a world save or the avatar's new body)", now - g_submit_held_at);
    g_submit_queued = true; // no save from here until the game has the request (save_blocker)
    g_submit_end_cine = end_cine;
    H->run_outside_frame(
        [](void* r) {
            if (g_submit_end_cine && world::end_cinematic("before the held editor opens (BUG-0404)"))
                g_submit_cines_ended++;
            g_submit_end_cine = false;
            submit_now(r);
            g_submit_queued = false;
            skg::vcall<int>(r, 2); // Release
        },
        req);
}

// BUG-0849: the end of Tribe (the City Hall editor done, the vehicle editor to come, then Civ). The game takes
// the Tribe world down for Civ in that time (the player tribe has no food mat, every animal goes to (0 0 0),
// docs/re/tribe.md#tribe-civ-transition); a share save posted then (the return from the City Hall editor taken for
// an evolution, or a guest joining) was written while the village was dismantled: the fork's host faulted in the
// game's serializer 36 ms after the vehicle editor was asked for. No share and no share save from the City Hall
// editor's end to the Civ stage: the Civ stage is shared as a new stage. Off: as 0.7.12.
static bool g_graduation_fix = true;
COOP_FIX("tribe.graduation_save", "BUG-0849", g_graduation_fix, "tribe: no world share or share save between the City Hall editor and the Civilization stage");
static unsigned g_graduation_refusals; // tests: share saves refused in the end of Tribe
bool tribe_civ_editor(uint32_t id);
uint32_t g_last_editor_req;
bool tribe_graduating()
{
    return g_graduation_fix && game::mode_id() == modes::kTribe && (tribe::stage_end() || tribe_civ_editor(g_last_editor_req));
}

// why the game must not save now (world::save_game)
const char* save_blocker()
{
    uint32_t mode = game::mode_id();
    if (mode == modes::kEditor || mode == modes::kEdit)
        return "an editor is open (the world is sent when the host is back in the stage)";
    if (mode == modes::kLoad)
        return "the game is loading";
    // a save in the galaxy menu (a share that waited there after the host left its stage) wrote a
    // slot that faulted the host's own next load (0xBA7F87, cStarManager) and stayed in LoadGame
    if (!stage_mode(mode) && mode != modes::kScenario)
        return "the game is not in a stage";
    if (g_submit_queued || H->time_seconds() - g_editor_submitted_at < 5.0)
        return "an editor is opening";
    if (mode_switch_pending())
        return "the game is switching modes";
    if (tribe_graduating()) { // BUG-0849
        if (!g_graduation_refusals++)
            LOG("no world save in the end of Tribe (City Hall built, Civilization next): the world is sent in Civ");
        return "the Tribe stage is ending (Civilization comes next): the world is sent in the Civilization stage";
    }
    return nullptr;
}

SimMsg_t o_SimMsg;
unsigned g_quits_delayed; // tests: quit messages that waited for our save
double g_quit_delay_logged = 0;
int SK_THISCALL h_SimMsg(void* self, uint32_t id, void* data)
{
    if (id == 0x4715068u && world::save_busy()) {
        uintptr_t get = H->sym("App::IMessageManager::Get");
        if (void* mm = get ? skg::cdecl_call<void*>(get) : nullptr) {
            double now = H->time_seconds();
            if (now - g_quit_delay_logged > 5.0) {
                g_quit_delay_logged = now;
                g_quits_delayed++;
                LOG("leaving the stage waits until the world save is written");
            }
            skg::vcall<void>(mm, 0x18 / 4, 0x4715068u, (void*)nullptr, (void*)nullptr, 0); // MessagePost
            return 1;
        }
    }
    if (id == 0x4715068u) // BUG-0264: the game leaves the stage now (any source of the quit)
        game::stage_leaving("quit message handled");
    return o_SimMsg(self, id, data);
}

// BUG-0466 (observed, lab 20261103-141812 editor_sync_guest): the guest's own editor of a vote
// (open_own_editor: an EditorRequest without the evolution tag +0x90) showed the DNA budget and the
// price of the new parts (40 -> 15), but the game never charged it: the creature game's handler
// 0xD42E50 applies an editor's result (message 0x30C11C7, data +0xC the request's tag) only for the
// evolution tag 0x64AA168 (then the stage's return sets GetEvolutionPoints to the result's
// remaining DNA, +0x34, 0xD445AA). The guest kept its DNA and the host's total stayed the same:
// free parts. Now the result of the own editor (not cancelled, +0x44 == 0) charges the price
// through the game (the remaining DNA at +0x34), and the guest sends the spend to the host (BUG-0466).
void dna_spend_note(float d);
CreatureGameMsg_t o_CreatureGameMsg;
void* g_creature_game; // BUG-0720 tests: the creature game whose handler builds the evolution request
bool SK_THISCALL h_CreatureGameMsg(void* self, uint32_t id, void* data)
{
    g_creature_game = self;
    if (id == 0x30C11C7u && g_own_editor_charge_due && data && skg::readable(data, 0x48)) {
        uint32_t tag = skg::field<uint32_t>(data, 0xC);
        int left = skg::field<int32_t>(data, 0x34);
        uint8_t cancelled = skg::field<uint8_t>(data, 0x44);
        float now = game::dna();
        LOG("own editor result: tag %08X remaining DNA %d cancelled %u, DNA here %.0f", tag, left, (unsigned)cancelled, now);
        if (tag != 0x64AA168u && tag != 0x12191CAu) {
            g_own_editor_charge_due = false;
            if (!cancelled && left >= 0 && left < now) {
                game::dna_add_raw((float)left - now); // the editor's price, as the evolution's return sets it
                dna_spend_note((float)left - now); // a spend of ours: to the host (BUG-0466)
                feed("your new parts cost " + std::to_string((int)(now - left)) + " DNA");
            }
        }
    }
    return o_CreatureGameMsg(self, id, data);
}

// BUG-0588: the editor of the last request (EditorRequest +0xC): the end of Tribe's City Hall and vehicle editors
// (tribe_editors.h). BUG-1280 (found by the Lineages fork, PR #196, its BUG-9060): the third vehicle editor is 7D433FAD
// (a consequence archetype other than C2CA9495 / 60A78928); the list had a misread 7CB21BAD, so the BUG-0849 share-save
// guard did not know that editor. `coop.fix tribe.graduation_vehicle_id off`: the old list
static bool g_vehicle_id_fix = true;
COOP_FIX("tribe.graduation_vehicle_id", "BUG-1280", g_vehicle_id_fix, "tribe: the end of Tribe's third vehicle editor (7D433FAD) counts as an end-of-Tribe editor (no world share or share save in it)");
bool tribe_civ_editor(uint32_t id)
{
    return tribe_editors::end_editor(id, g_vehicle_id_fix);
}
// tests (BUG-1280, `coop.vote tribeend [<archetype hex>]`): the end-of-Tribe guard's inputs; with an archetype the
// player's consequence archetype (cPlayer +0x1104) is set first, so the game opens that archetype's vehicle editor
std::string tribe_end_line(bool set, uint32_t archetype)
{
    void* nm = game::noun_manager();
    void* pl = nm && skg::readable((char*)nm + 0x74, 4) ? skg::field<void*>(nm, 0x74) : nullptr;
    bool ok = pl && skg::readable((char*)pl + 0x1104, 4);
    if (set && ok)
        skg::field<uint32_t>(pl, 0x1104) = archetype;
    uint32_t a = ok ? skg::field<uint32_t>(pl, 0x1104) : 0;
    char b[256];
    snprintf(b, sizeof b,
             "tribe end: archetype %08X (vehicle editor %08X), last editor %08X end_editor=%d stage_end=%d graduating=%d "
             "fix=%d\n",
             a, tribe_editors::vehicle_editor(a), g_last_editor_req, (int)tribe_civ_editor(g_last_editor_req),
             game::mode_id() == modes::kTribe && tribe::stage_end() ? 1 : 0, (int)tribe_graduating(), (int)g_vehicle_id_fix);
    return b;
}
bool g_editreq_capture;
ReqImage g_editreq_game;
ReqImage req_image(void* req)
{
    ReqImage im;
    if (!req || !skg::readable(req, 0x9C))
        return im;
    im.have = true;
    memcpy(im.bytes, req, 0x9C);
    uint32_t* b = skg::field<uint32_t*>(req, 0x70);
    uint32_t* e = skg::field<uint32_t*>(req, 0x74);
    for (uint32_t* q = b; b && e && e - b <= 64 && q < e; q++)
        im.vec70.push_back(*q);
    return im;
}
// BUG-1222: a guest's own mating call while it plays its own creation (not the world's species) opens the own editor
// (`coop.fix creature.own_mating_editor off`: the game's evolution editor, 0.7.14)
static bool g_own_mating_fix = true;
COOP_FIX("creature.own_mating_editor", "BUG-1222", g_own_mating_fix,
         "creature: a guest playing its own creation evolves in its own editor (red cross, its parts allowed) after its mating call");
unsigned g_own_mating_rerouted; // tests
static bool guest_plays_own_creation()
{
    creations::Option m = creations::mine(creations::kCreature);
    if (!m.key.instance)
        return false;
    for (const creations::Option& o : creations::options(creations::kCreature))
        if (o.key.instance == m.key.instance)
            return true;
    return false;
}
bool SK_CDECL h_Submit(void* req)
{
    if (g_editreq_capture) { // tests: the game's own request, kept and not submitted
        g_editreq_game = req_image(req);
        g_editreq_capture = false;
        return false;
    }
    uint32_t mode = game::mode_id();
    if (req && skg::readable(req, 0x10))
        g_last_editor_req = skg::field<uint32_t>(req, 0xC);
    // The cell stage asks for its editor again while the mate is still next to the avatar
    // (0xE78D10 -> mating -> event 0x1E -> 0xE59800 -> Submit): one more request came 13 ms after
    // the vote had ended, before the held one reached the game, and started a second vote that
    // ran on while the host was in the editor (its result was dropped). One editor per vote. A
    // guest's own request reaches the game at once; its repeat only told everybody twice.
    if (!g_vote_bypass && req &&
        (g_submit_later || g_submit_queued || H->time_seconds() - g_editor_submitted_at < 5.0)) {
        g_submits_repeated++;
        return false;
    }
    if (!g_vote_bypass && req) // BUG-0576: what the game itself had paused or locked when it asked for the editor
        g_req_own_busy = game_busy() & (busy::kPaused | busy::kInputLock);
    if (req && mode == modes::kCell && g_sess.connected())
        cellsync::dna_note(g_vote_bypass ? "at the editor request (after the vote)" : "at the editor request");
    if (!g_vote_bypass && req && stage_mode(mode) && g_sess.connected() && g_sess.players().size() > 1) {
        uint32_t tag = skg::readable(req, 0x94) ? skg::field<uint32_t>(req, 0x90) : 0u;
        uint32_t editor = skg::field<uint32_t>(req, 0xC);
        // BUG-1085: which Creature request this is (the stage-end editor or an evolution inside the stage)
        bool stage_end = g_editor_tag_fix && mode == modes::kCreature && tag == kTagStageEnd;
        bool in_stage = !g_editor_tag_fix ? tag == kTagStageEnd
                                          : tag == kTagMating && editor != kEditorLandfall &&
                                                !(g_switch_vote_fix && settings::on(settings::kSwitchVote) &&
                                                  H->time_seconds() - g_switch_passed_at < 300.0);
        if (mode == modes::kCreature)
            LOG("editor request in Creature: editor %08X tag %08X (%s)", editor, tag,
                stage_end ? "the stage's end" : in_stage ? "an evolution in the stage" : "other");
        bool now_ok = g_editor_now_fix && settings::on(settings::kEditorNow) && g_sess.role() == net::Role::Host &&
                      !g_vote.active && mode == modes::kCreature && in_stage;
        // BUG-1263: the host's Cell evolution opens at once too (no offer: a guest evolves through its own mating call)
        bool cell_now = g_cell_now_fix && settings::on(settings::kEditorNow) && g_sess.role() == net::Role::Host &&
                        !g_vote.active && mode == modes::kCell;
        // F5 (BUG-0769): creature.mode host: one creature for everyone, only the host evolves it (no vote, no offer);
        // a guest's own editor request is refused (the game's nest / mating call of a guest)
        bool one = host_creature_mode() && (mode == modes::kCell || mode == modes::kCreature);
        if (one && g_sess.role() == net::Role::Host) {
            LOG("evolution: creature.mode host: the host's editor opens at once, every player gets the result");
            notice(g_name + " evolves the creature of everybody", -1);
        } else if (one) {
            LOG("evolution: creature.mode host: this guest's editor request is refused (the host evolves)");
            feed("the host evolves the creature for everybody (session setting: one creature for everyone)");
            g_evo_flag_refused = true; // BUG-0920: the game sets its "editor pending" flag after this call
            g_refused_mate_due = g_refused_mate_fix && mode == modes::kCell; // BUG-1265
            return false;
        } else if (cell_now) {
            g_cell_now++;
            LOG("evolution: the host's Cell editor opens at once, the others get a notice (cell.editor_now, BUG-1263)");
            notice(g_name + " evolves its cell", -1);
        } else if (now_ok) { // BUG-0516: the host's evolution goes on now, the guests get an offer
            uint32_t id = skg::field<uint32_t>(req, 0xC);
            LOG("evolution: the host's editor opens at once, the others get an offer (core.editor_now)");
            broadcast_offer(id, g_name + " evolves now: press Evolve now (F9) to evolve yours too");
            notice(g_name + " evolves now (the others may evolve too)", -1);
        } else if (g_sess.role() == net::Role::Host) {
            if (!g_vote.active) {
                if (stage_end) { // BUG-1085: a stage start keeps the vote
                    g_stage_end_votes++;
                    LOG("evolution: the end of the Creature stage waits for the vote (BUG-1085)");
                    start_vote(req, g_name + " wants to move on to the Tribe stage");
                } else if (designs::outfit_texts(mode, editor)) // BUG-1156: the outfitter is no evolution
                    start_vote(req, g_name + " wants to change the tribe's outfit");
                else
                    start_vote(req, g_name + (design_stage(mode) ? " wants to edit a design (" : " wants to evolve (") +
                                        modes::mode_name(mode) + ")");
            }
            return false; // runs when the vote is over
        } else if (stage_end && host_world_now() == host_world::HostWorld::Same) {
            // BUG-1085 (docs/09-coop-design.md 9.3): a guest cannot change the stage alone; its press is its I'm ready
            g_stage_end_guest_refused++;
            g_evo_flag_refused = true; // BUG-0920: the game sets its "editor pending" flag after this call
            if (g_vote.active) {
                LOG("evolution: this guest's stage-end request is its I'm ready (BUG-1085)");
                set_ready(true);
            } else {
                LOG("evolution: this guest's stage-end request is not taken: the host moves everybody on (BUG-1085)");
                feed("the host moves everybody on to the Tribe stage (after a vote)");
            }
            return false;
        } else if (designs::own_outfitter(req, mode, editor, host_world_now() != host_world::HostWorld::Same)) {
            // BUG-1272 (WP-T2): creature.mode own: the guest's own outfitter is for its own species line (designs.cpp)
            LOG("evolution: this guest's own Tribal Outfitter opens for its own species (BUG-1272)");
            notice(g_name + " changes the outfit of its tribe members", -1);
        } else if (std::string why = designs::refuse_own_editor(mode, editor, host_world_now() != host_world::HostWorld::Same,
                                                                 g_sess.name_of(0));
                   !why.empty()) {
            // BUG-1158: a guest's own outfitter / design editor in the host's world changed only its own game (the
            // shared tribe's outfit and civilization's designs are the host's, designs.h): refused, told why
            LOG("evolution: this guest's own editor %08X is refused: %s (BUG-1158)", editor, why.c_str());
            feed(why);
            return false;
        } else if (g_own_mating_fix && mode == modes::kCreature && in_stage && host_world_now() == host_world::HostWorld::Same &&
                   guest_plays_own_creation()) {
            // BUG-1222: a guest playing its own creation (the Creature Creator's parts) in the host's world got the game's
            // evolution editor from its own mating call: done is blocked by the part-list error (bit 5, only the own editor waives it)
            // and that editor has no red cross. It gets the own editor instead (open_own_editor, as after a vote:
            // the cross, the waiver, no evolution of the world's species), when the stage has settled
            offer_end("the own evolution");
            notice(g_name + " evolves its own creature", -1);
            set_ready(true);
            g_own_mating_rerouted++;
            g_evo_flag_refused = true; // BUG-0920: the game sets its "editor pending" flag after this call
            g_own_editor_due = editor;
            g_own_editor_due_at = g_own_editor_due_first = H->time_seconds();
            LOG("evolution: this guest plays its own creation: its mating call opens its own editor %08X (BUG-1222)", editor);
            return false;
        } else {
            offer_end("the own evolution"); // BUG-0516: this guest evolves on its own now
            notice(g_name + (design_stage(mode) ? " edits a design" : " evolves its own creature"), -1);
            set_ready(true);
        }
    }
    if (!g_vote_bypass && req && creations::avatar_swap_settling(H->time_seconds())) {
        hold_submit(req); // BUG-0318: after the new body is built (submit_tick)
        LOG("the editor waits until the avatar's new body is built");
        feed("your editor opens in a moment (your creature is still being built)");
        return false;
    }
    if (!g_vote_bypass && req && g_sess.role() == net::Role::Host && share_save_due()) {
        hold_submit(req); // the game's own request (alone in the session): after the save
        return false;
    }
    bool r = o_Submit(req);
    if (r)
        g_editor_submitted_at = H->time_seconds();
    return r;
}

CellToCreature_t o_CellToCreature;
CellStageEnd_t o_CellStageEnd;
bool g_switch_go = false; // the vote passed: the next call is the game's own switch
// BUG-1269 (key-frame review: during the switch vote the host's screen was plain white behind the co-op window while
// the guest still saw the Cell-end cinematic): History's check runs the Cell stage's end sequence
// (SporeCoop::CellStage_End 0xE72C80: the HUD hidden, the letterbox, the avatar's animation 0x2E, G+0x5158 = 3) and only
// after the animation the event 0x1F calls the switch (CellStage_ToCreature), where core.switch_vote held it: the host
// was left at the end of that sequence, a white picture, for the whole vote (up to 60 s). On: the vote holds History's
// check before the end sequence, the host plays on in its pool while the others vote; when the vote passes the game's
// own end sequence runs and its switch goes through. A guest's check in the host's world is its I'm ready there, its
// game starts no end sequence either (it stayed white until the host's world took it). Off: held at the switch
// (0.7.15).
static bool g_switch_world_fix = true;
COOP_FIX("core.switch_vote_world", "BUG-1269", g_switch_world_fix,
         "core: while the Cell -> Creature vote runs the host sees its world instead of a white screen (the vote holds History's check before the game's end sequence)");
static bool g_end_go = false;      // the vote passed: the next CellStage_End is the game's own
static unsigned g_switch_end_held; // tests (coop.vote): History's checks held before the end sequence
unsigned switch_end_held_count() { return g_switch_end_held; }

// The Cell -> Creature switch in a session (BUG-0315): true when this call is held (the host's: a vote starts; a
// guest's in the host's world: its I'm ready). `at_end`: called for the end sequence (BUG-1269), else for the switch;
// `from`: the game's return address (the log).
static bool switch_hold(bool at_end, void* from)
{
    bool multi = g_sess.connected() && g_sess.players().size() > 1;
    if (!g_switch_vote_fix || !settings::on(settings::kSwitchVote) || game::mode_id() != modes::kCell || !multi)
        return false;
    if (g_sess.role() == net::Role::Client) {
        if (host_world_now() != host_world::HostWorld::Same) // the guest's own world: its own switch
            return false;
        g_switch_guest_held++;
        if (g_vote.active) {
            LOG("the Creature stage: this guest's click on History's check is its I'm ready (BUG-0315)");
            set_ready(true);
        } else {
            LOG("the Creature stage: a guest's own switch is not taken, the host starts it for everybody (BUG-0315)");
            feed("the host moves everybody on to the Creature stage (after a vote)");
        }
        return true;
    }
    g_switch_held++;
    if (at_end)
        g_switch_end_held++;
    LOG("the host's %s to the Creature stage (from %p, cinematic %d)", at_end ? "History's check" : "switch",
        from, (int)world::cinematic_playing());
    if (g_vote.active) {
        LOG("the Creature stage waits: a vote runs ('%s')", g_vote.text.c_str());
        feed("the Creature stage: wait until the vote is over, then click History's check again");
        return true;
    }
    LOG("the Creature stage waits for a vote (BUG-0315, core.switch_vote%s)", at_end ? "; before the end sequence, BUG-1269" : "");
    g_switch_votes++;
    start_action_vote(g_name + " wants to move on to the Creature stage", "the Creature stage starts", [at_end] {
        if (game::mode_id() != modes::kCell) {
            LOG("the Creature stage: the vote passed out of the Cell stage, nothing to start");
            return;
        }
        g_switch_passed_at = H->time_seconds();
        g_switch_go = true;
        if (!at_end) {
            LOG("the Creature stage starts after the vote (BUG-0315)");
            h_CellToCreature();
            return;
        }
        LOG("the Creature stage starts after the vote: the game's end sequence first (BUG-1269)");
        g_end_go = true;
        h_CellStageEnd();
        // the end sequence posts the event 0x1F that calls the switch (it passes: g_switch_go). It does nothing for a
        // dead or locked avatar or a stage already ending (G+0x5158 == 3 already): then the switch at once, as before
        uint8_t* g = modes::cell_game();
        int state = g && skg::readable(g + 0x5158, 4) ? *(int*)(g + 0x5158) : -1;
        if (state != 3) {
            LOG("the Creature stage: the end sequence did not start (state %d): the switch at once", state);
            h_CellToCreature();
        }
    });
    return true;
}

void SK_CDECL h_CellToCreature()
{
    if (g_switch_go || !switch_hold(false, __builtin_return_address(0))) {
        g_switch_go = false;
        o_CellToCreature();
    }
}

// BUG-1269: History's check (the end sequence); see above
void SK_CDECL h_CellStageEnd()
{
    if (!g_switch_world_fix || g_end_go || g_switch_go || !switch_hold(true, __builtin_return_address(0))) {
        g_end_go = false;
        o_CellStageEnd();
    }
}

// BUG-0404 (the busy gate): an action vote's result (a superweapon ...) ran the moment the vote
// ended, also during a cinematic, a pause or a load. It now waits at the gate (busy::kActionGate):
// a pause as long as it lasts, the rest at most 60 s; it is dropped when the stage was left.
std::function<void()> g_vote_action_due;
busy::Wait g_vote_action_wait;
bool g_vote_action_told = false;
uint32_t g_vote_action_mode = 0;
unsigned g_vote_actions_waited; // tests

void vote_action_tick(double now)
{
    if (!g_vote_action_due)
        return;
    unsigned all = game_busy();
    if (game::mode_id() != g_vote_action_mode && !(all & (busy::kLoading | busy::kEditor))) {
        LOG("the vote's action is dropped: the stage was left");
        g_vote_action_due = nullptr;
        return;
    }
    unsigned b = all & busy::kActionGate;
    if (busy::hold(g_vote_action_wait, b, now, 60.0, busy::kPaused)) {
        if (!g_vote_action_told) {
            g_vote_action_told = true;
            g_vote_actions_waited++;
            LOG("the vote's action waits: the game is busy (%s)", busy::names(b).c_str());
        }
        return;
    }
    static std::function<void()> run;
    run = std::move(g_vote_action_due);
    g_vote_action_due = nullptr;
    g_vote_action_wait = {};
    g_vote_action_told = false;
    H->run_outside_frame([](void*) { g_vote_actions_run++; if (run) run(); }, nullptr);
}

void finish_vote(const char* why)
{
    if (!g_vote.active)
        return;
    void* req = g_vote.request;
    g_vote.active = false;
    g_held_mate = 0; // BUG-1245: the editor follows (its return rebuilds the pool)
    {
        int ready = 0, n = 0;
        for (auto& p : g_sess.players()) {
            n++;
            ready += g_ready.count(p.id) && g_ready[p.id];
        }
        trace::line("vote", "end: %s, ready %d/%d after %.1f s, editor=%s", why, ready, n, H->time_seconds() - g_vote.t0,
                    trace::hex(g_vote.editor).c_str());
    }
    broadcast_vote();
    if (g_vote_opened_ui)
        coop_ui::open(false);
    g_vote_opened_ui = false;
    notice(g_vote_done + " (" + why + ")", -1);
    if (g_vote_action) {
        // BUG-0404: vote_action_tick runs it at the busy gate
        g_vote_action_due = std::move(g_vote_action);
        g_vote_action = nullptr;
        g_vote_action_wait = {};
        g_vote_action_told = false;
        g_vote_action_mode = game::mode_id();
        vote_action_tick(H->time_seconds());
    }
    if (req) { // the editor opens from submit_tick (outside the frame) once no world save is written
        hold_submit(req);
        skg::vcall<int>(req, 2); // the vote's reference: hold_submit took its own
        submit_tick(H->time_seconds());
    }
}

void broadcast_vote()
{
    if (g_sess.role() != net::Role::Host)
        return;
    net::Writer w;
    w.u8(g_vote.active ? 1 : 0);
    w.u8((uint8_t)std::max(0, g_vote.left));
    w.str8(g_vote.text);
    w.u32(g_vote.editor);
    w.u8(0); // flags (BUG-0516)
    g_sess.broadcast_reliable(net::MSG_VOTE, w);
}

// BUG-0516: the host evolves now; every guest may open its own editor of the same id
void broadcast_offer(uint32_t editor, const std::string& text)
{
    net::Writer w;
    w.u8(0); // not a vote: an older guest takes it as the end of no vote
    w.u8((uint8_t)kOfferSeconds);
    w.str8(text);
    w.u32(editor);
    w.u8(kVoteOffer);
    g_sess.broadcast_reliable(net::MSG_VOTE, w);
    g_offers_sent++;
    trace::line("vote", "offer sent: '%s' editor=%s", text.c_str(), trace::hex(editor).c_str());
}

// guest: the offer ends (taken, expired, the stage or the session left)
void offer_end(const char* why)
{
    if (!g_offer_editor)
        return;
    trace::line("vote", "offer of editor %s ends: %s", trace::hex(g_offer_editor).c_str(), why);
    g_offer_editor = 0;
    if (g_offer_opened_ui && !g_vote.active)
        coop_ui::open(false);
    g_offer_opened_ui = false;
    g_lobby_dirty = true;
}

// guest: "Evolve now": the own editor opens when the stage has settled (own_editor_tick)
void offer_take()
{
    uint32_t id = g_offer_editor;
    if (!id)
        return;
    g_offers_taken++;
    offer_end("taken");
    g_own_editor_due = id;
    g_own_editor_due_at = g_own_editor_due_first = H->time_seconds();
    feed("evolution: your editor opens in a moment");
}

void offer_tick(double now)
{
    if (!g_offer_editor)
        return;
    if (!g_sess.connected() || g_mode != modes::kCreature) {
        offer_end(!g_sess.connected() ? "the session ended" : "the stage was left");
        return;
    }
    if (now > g_offer_until) {
        g_offers_expired++;
        offer_end("time is up");
        feed("evolution: you can evolve your creature at your nest later");
    }
}


// the vote ends without its result: the editor request is released, the action is not run
void drop_vote(const char* why)
{
    if (!g_vote.active)
        return;
    LOG("the vote '%s' is dropped: %s", g_vote.text.c_str(), why);
    g_votes_dropped++;
    if (g_held_mate && cellsync::restore_mate(g_held_mate)) // BUG-1245: the stage goes on, its mate with it
        LOG("cell: the mate %08X is the mate again (the vote was dropped)", g_held_mate);
    g_held_mate = 0;
    void* req = g_vote.request;
    g_vote = Vote{};
    g_vote_action = nullptr;
    // on purpose with editor = 0 (g_vote was reset): the guests open no own editor for a cancelled
    // vote (BUG-0234); finish_vote broadcasts with the id. Do not keep the id here.
    broadcast_vote();
    if (g_vote_opened_ui)
        coop_ui::open(false);
    g_vote_opened_ui = false;
    if (req)
        skg::vcall<int>(req, 2); // the vote's reference
}

// BUG-0801: a guest that plays another world of the host's stage ignores the host's vote (BUG-0702) and never sends
// MSG_READY: every vote of a 3-4 player session with one guest elsewhere ran into its 60 s timer. The vote counts
// only the players in the host's world now (own_world::peer_elsewhere: what that guest itself decides). Off: every
// player of the session (0.7.12).
static bool g_vote_present_fix = true;
COOP_FIX("core.vote_present", "BUG-0801", g_vote_present_fix, "core: a vote does not wait for a guest that plays another world");
static int g_vote_away_last = 0;
// BUG-1239 (players' video: while one player was in the evolution editor the other's screen stayed black): the host's
// evolution vote (the Cell mating call; the game has already faded its picture out and holds the editor request) waited
// for every guest's I'm ready, also for a guest that sat in its own editor at that moment (its own mating call opens it
// without a vote, docs/09-coop-design.md 9.4.1): that guest cannot answer, so the host stared at a black screen until
// the 60 s ran out. On: a guest whose game is in an editor is not waited for (it evolves already); the vote ends as
// soon as the others are ready. Off: waited for (0.7.15).
static bool g_vote_editor_fix = true;
COOP_FIX("core.vote_guest_editor", "BUG-1239", g_vote_editor_fix,
         "core: the host's evolution vote does not wait for a guest that is in its own editor (the host's screen stayed black meanwhile)");
// BUG-1369 (vote_net_4p, lab 20261120-133703): BUG-1239's rule was meant for the evolution vote (the game holds the
// host's editor request behind a faded picture; the guest in its own editor evolves already) but its check ran in every
// vote: an action vote (the Cell -> Creature switch, a superweapon, coop.vote action) passed without the ready of a guest
// that sat in its editor, although nothing on the host waits behind an action vote and the action changes that guest's
// game too. On: only a vote for an editor skips a guest in its own editor; an action vote waits for its ready (or its
// leave, or the 60 s) as before 0.7.15.1. Off: every vote skips it (0.7.15.1).
static bool g_vote_editor_evo_fix = true;
COOP_FIX("core.vote_editor_evo_only", "BUG-1369", g_vote_editor_evo_fix,
         "core: only the evolution vote skips a guest in its own editor; an action vote (stage switch, superweapon) waits for its ready");
static unsigned g_vote_in_editor = 0; // tests (coop.vote): votes that stopped waiting for a guest in its editor
static int g_vote_editor_last = 0;
static bool peer_in_editor(uint8_t id)
{
    auto it = g_remotes.find(id);
    if (it == g_remotes.end() || it->second.echo || it->second.buf.empty())
        return false;
    uint32_t m = it->second.buf.latest().mode;
    return m == modes::kEditor || m == modes::kEdit;
}
unsigned vote_in_editor_count() { return g_vote_in_editor; }
void vote_tick(double now)
{
    if (!g_vote.active || g_sess.role() != net::Role::Host)
        return;
    if (g_vote_scope_fix) {
        uint32_t w = modes::current_world();
        if (game::mode_id() != g_vote.mode || (g_vote.world && w && w != g_vote.world)) {
            notice(g_vote.text + ": cancelled (the stage was left)", -1);
            drop_vote("the stage was left");
            return;
        }
    }
    g_vote.left = (int)(60 - (now - g_vote.t0));
    if (g_vote_mate_fix && g_vote.request && g_vote.mode == modes::kCell)
        if (uint32_t m = cellsync::forget_mate()) {
            g_held_mate = m;
            g_vote_mates_held++;
            LOG("cell: the vote holds the editor: the mate %08X does not meet our cell again (BUG-1245)", m);
        }
    if (g_vote.arrival) // BUG-0315: a guest that plays the host's Creature world now voted for it in Cell
        for (auto& [id, r] : g_remotes) {
            if (r.echo || r.buf.empty() || (g_ready.count(id) && g_ready[id]))
                continue;
            const auto& ls = r.buf.latest();
            if (same_place(ls) && (ls.flags & net::PF_IN_WORLD)) {
                g_ready[id] = true;
                g_lobby_dirty = true;
                g_switch_arrivals++;
                trace::line("vote", "player %u arrived in the Creature stage: ready (BUG-0315)", (unsigned)id);
            }
        }
    bool all = true;
    int away = 0, editing = 0, onway = 0, arriving = 0;
    for (auto& p : g_sess.players()) {
        if (p.id != 0 && g_vote_present_fix && own_world::peer_elsewhere((uint8_t)p.id)) {
            away++; // BUG-0801: plays another world, ignores the vote (BUG-0702): not waited for
            continue;
        }
        if (p.id != 0 && g_vote.arrival && g_arrival_nowait_fix && !(g_ready.count(p.id) && g_ready[p.id]) &&
            peer_takes_arrival_editor((uint8_t)p.id)) {
            if (g_arrival_wait_fix) { // BUG-1336: a quick arrival is waited for
                auto rit = g_remotes.find(p.id);
                bool flight = (rit != g_remotes.end() && rit->second.needs_world) || world::share_in_flight((uint8_t)p.id);
                if (arrival_rule::waits_for(peer_in_editor((uint8_t)p.id), flight, now - g_vote.t0,
                                            world::since_share((uint8_t)p.id))) {
                    arriving++;
                    all = false;
                    continue;
                }
            }
            onway++; // BUG-1245: on its way, it opens its own editor when it arrives (BUG-1240): not waited for
            continue;
        }
        if (p.id != 0 && g_vote_editor_fix && !(g_vote_editor_evo_fix && g_vote_action) &&
            !(g_ready.count(p.id) && g_ready[p.id]) && peer_in_editor((uint8_t)p.id)) {
            editing++; // BUG-1239: in its own editor, it cannot answer: not waited for
            continue;
        }
        all &= g_ready.count(p.id) && g_ready[p.id];
    }
    if (editing != g_vote_editor_last) {
        g_vote_editor_last = editing;
        if (editing) {
            g_vote_in_editor++;
            trace::line("vote", "%d player(s) in their own editor: not waited for (BUG-1239)", editing);
        }
    }
    if (onway != g_arrival_onway_last) {
        g_arrival_onway_last = onway;
        if (onway) {
            g_arrival_not_waited++;
            trace::line("vote", "%d player(s) on their way to the Creature stage: not waited for, their own editor opens "
                                "when they arrive (BUG-1245)", onway);
        }
    }
    if (arriving != g_arrival_waiting_last) {
        if (arriving > g_arrival_waiting_last)
            g_arrival_waited++;
        g_arrival_waiting_last = arriving;
        trace::line("vote", "%d player(s) arriving in the Creature stage: waited for (BUG-1336)", arriving);
    }
    if (away != g_vote_away_last) {
        g_vote_away_last = away;
        trace::line("vote", "%d player(s) in another world: not waited for (BUG-0801)", away);
    }
    if (all)
        finish_vote("everybody is ready");
    else if (g_vote.left <= 0)
        finish_vote("time is up");
    else if (g_sess.players().size() <= 1)
        finish_vote("alone");
    else if (now - g_vote_last_send > 1.0) {
        g_vote_last_send = now;
        broadcast_vote();
    }
}

// The guest's own editor after the vote opens once its stage has settled. At the start of the
// Creature stage the world arrives a few seconds before the vote ends; an editor opened 4 s after
// that load came up without its interface (no parts, no buttons, cancel did nothing) and the guest
// was stuck in it (coop_editor_desync.py, 2026-10-24, also with 5 s of waiting). The planet showed
// 6-15 s after the avatar appeared (the screen stayed black before). It waits until the avatar has
// played for 15 s and no cinematic or caption is up, at most 90 s.
uint32_t g_own_editor_due = 0;
// BUG-1240 (players' video: at Cell -> Creature one player had the Creature editor, the other a black screen, and both
// played the same creature afterwards): the first Creature editor's vote (the landfall editor, BUG-0315's arrival vote)
// ends when every guest arrived in the host's Creature world, or after 60 s. A guest whose way there was slow (the
// host's Creature world still on its way over a slow link, its load, its galaxy menu) was not in Creature when the
// vote ended, so it got no own editor at all: it arrived while the host was in its editor, played the world's species
// (the host's creature) and then followed the host's evolved one (BUG-0508). On: the landfall editor's end keeps the
// guest's own editor due while it is on its way (Cell, the galaxy menu, the load); its 90 s start when it arrives in
// Creature (at most 300 s in all). Off: no own editor unless the guest is in Creature when the vote ends (0.7.15).
bool g_own_editor_arrival_fix = true;
COOP_FIX("core.own_editor_arrival", "BUG-1240", g_own_editor_arrival_fix,
         "core: a guest still on its way to the host's Creature world when the first Creature editor's vote ends gets its own editor when it arrives");
bool g_own_editor_arriving = false; // the due own editor waits for this guest's arrival in Creature
unsigned g_own_editor_arrivals = 0; // tests (coop.vote): own editors kept for an arrival
double g_own_editor_due_at = 0;
double g_own_editor_due_first = 0; // (BUG-0404: the 90 s may be pushed by pauses, not past 300 s)
bool g_own_editor_queued = false;
unsigned g_own_editor_waits; // tests: frames the own editor waited for the stage to settle
double g_own_editor_hold_until = 0; // tests (coop.vote hold <s>): the own editor waits until then
void* g_settle_avatar = nullptr;
double g_settle_since = 0;
bool pause_ui_up();

// BUG-0920: the game's "an editor of the stage is pending" flag left set with no editor coming. The creature
// stage's evolution request (its message handler 0xD42E50, the mating call) sets the byte 0x1686AF0 after
// EditorRequest::Submit whatever Submit answered, and only the next mode switch clears it (0xB5F19D). While it is
// set the frame function 0xB5EAE0 skips the stage's own drawing: the world and the whole game HUD are gone, the
// screen is black with only the mod's overlay, for good (60 s, input and F9 change nothing; the byte written back
// to 0 brings the picture back within a frame). A request the mod's Submit hook refuses and never submits later
// (creature.mode host: a guest's own evolution request, 0.7.12) left the guest like that; a held request that
// is dropped, or one the game's own Submit refuses, would too. On: when the flag is set in a stage and nothing
// will open an editor (no vote, no held or queued request, no own editor due, no mode switch pending, no request
// submitted in the last 15 s), it is cleared: at once after a refusal, else after 1 s of that state. Off: as before.
bool g_evo_flag_fix = true;
COOP_FIX("core.evo_flag_clear", "BUG-0920", g_evo_flag_fix,
         "core: an editor request that is refused or dropped does not leave the stage's screen black (the game's editor-pending flag is cleared)");
unsigned g_evo_flags_cleared = 0; // coop.status, tests
double g_evo_flag_idle_since = 0;
// C3 (BUG-1142): an editor is on its way on this game (a vote, a held or queued request, the own editor, a request
// submitted in the last 15 s; as evo_flag_tick reads it): the pause reason 0x4BF38A6 is then no Sporepedia
bool editor_coming(double now)
{
    return g_vote.active || g_submit_later || g_submit_queued || g_own_editor_due || g_own_editor_queued ||
           mode_switch_pending() || now - g_editor_submitted_at < 15.0;
}

int evo_flag_now()
{
    uintptr_t f = H->sym("SporeCoop::CreatureEvolutionPending_flag");
    return f ? *(uint8_t*)f : -1;
}
void evo_flag_tick(double now)
{
    // BUG-1265: creature.mode host: a guest's mate never meets its cell (no evolution of its own in that mode)
    bool one_guest = g_refused_mate_fix && g_sess.connected() && g_sess.role() == net::Role::Client &&
                     host_creature_mode() && game::mode_id() == modes::kCell;
    if (g_refused_mate_due || one_guest) {
        bool refused = g_refused_mate_due;
        g_refused_mate_due = false;
        if (uint32_t m = cellsync::forget_mate()) {
            g_refused_mates++;
            LOG("cell: the mate %08X does not meet our cell (creature.mode host: the host evolves, %s, BUG-1265)", m,
                refused ? "after a refused request" : "at the mating call");
            feed("the host evolves the cell for everybody (session setting: one creature for everyone)");
        }
    }
    bool refused = g_evo_flag_refused;
    g_evo_flag_refused = false;
    uintptr_t f = g_evo_flag_fix ? H->sym("SporeCoop::CreatureEvolutionPending_flag") : 0;
    if (!f || !*(uint8_t*)f || !stage_mode(game::mode_id()) || g_vote.active || g_submit_later || g_submit_queued ||
        g_own_editor_due || g_own_editor_queued || mode_switch_pending() || now - g_editor_submitted_at < 15.0) {
        g_evo_flag_idle_since = 0;
        return;
    }
    if (!refused) { // not the mod's own refusal: only after the state held for a second
        if (!g_evo_flag_idle_since)
            g_evo_flag_idle_since = now;
        if (now - g_evo_flag_idle_since < 1.0)
            return;
    }
    *(uint8_t*)f = 0;
    g_evo_flag_idle_since = 0;
    g_evo_flags_cleared++;
    LOG("evolution: the game's editor-pending flag was left set with no editor coming (%s): cleared, the stage is drawn again (BUG-0920, %u)",
        refused ? "the request was refused" : "nothing pending for 1 s", g_evo_flags_cleared);
}

// BUG-0404 (the busy gate): the guest's own editor waited for a cinematic but not for a pause: due
// while the guest's game time stood (the pause menu, the Sporepedia, My Collections) it was dropped
// for good ("your editor did not open (pause menu)") or opened under the Sporepedia. It now waits at
// the gate (busy::kEditorGate) while the 90 s limit counts only the time the game was not paused, and
// a pause menu found by the outside-frame check makes it wait again instead of dropping it.
double g_own_editor_last_tick = 0;

void own_editor_tick(double now)
{
    void* av = g_mode == modes::kCreature ? game::avatar() : nullptr;
    if (av != g_settle_avatar) {
        g_settle_avatar = av;
        g_settle_since = now;
    }
    double dt = g_own_editor_last_tick ? now - g_own_editor_last_tick : 0;
    g_own_editor_last_tick = now;
    if (!g_own_editor_due || g_own_editor_queued)
        return;
    if (host_creature_mode() && g_sess.role() == net::Role::Client) { // F5 (BUG-0769): the host evolves for everybody
        g_own_editor_due = 0;
        return;
    }
    if (g_own_editor_arriving) { // BUG-1240: on its way to the host's Creature world
        if (g_mode == modes::kCreature && av) {
            g_own_editor_arriving = false;
            g_own_editor_due_at = now; // the 90 s count from the arrival
            LOG("own editor: arrived in the Creature stage, the own editor opens when the stage has settled (BUG-1240)");
        } else if (now - g_own_editor_due_first > 300.0 || g_mode == modes::kTribe || g_mode == modes::kCiv ||
                   g_mode == modes::kSpace) {
            g_own_editor_arriving = false;
            g_own_editor_due = 0;
            LOG("own editor not opened: this guest did not arrive in the Creature stage (BUG-1240)");
            feed("evolution: your editor did not open; evolve at your nest later");
            return;
        } else {
            g_own_editor_waits++;
            return;
        }
    }
    unsigned b = game_busy() & busy::kEditorGate;
    if ((b & busy::kPaused) && dt > 0 && dt < 5.0)
        g_own_editor_due_at += dt; // a pause does not count against the 90 s (all in all 300 s)
    if (now - g_own_editor_due_at > 90.0 || now - g_own_editor_due_first > 300.0 ||
        (g_mode != modes::kCreature && stage_mode(g_mode))) {
        LOG("own editor not opened: the stage did not settle (or was left)");
        feed("evolution: your editor did not open; evolve at your nest later");
        g_own_editor_due = 0;
        return;
    }
    if (!av || !g_local || now - g_settle_since < 15.0 || now < g_own_editor_hold_until ||
        creations::avatar_swap_settling(now) || // BUG-0318
        world::cinematic_playing() || b || mode_switch_pending() || world::load_queued()) { // a received world loads first (BUG-0157)
        g_own_editor_waits++;
        return;
    }
    g_own_editor_queued = true;
    H->run_outside_frame( // editors create D3D resources: outside the frame
        [](void*) {
            g_own_editor_queued = false;
            uint32_t id = g_own_editor_due;
            if (!id)
                return;
            // the player may have left the stage since (pause menu "Exit to Main Menu")
            if (game::mode_id() != modes::kCreature || mode_switch_pending() || world::load_queued())
                return; // own_editor_tick decides again
            // an editor opened under the pause menu or its "save your game?" boxes: a click on
            // "Don't Save" right after left the guest in a black editor for good
            if (pause_ui_up())
                return; // BUG-0404: own_editor_tick waits for the pause (or a message box) to go
            g_own_editor_due = 0;
            bool ok = open_own_editor(id);
            LOG("own editor %s (%.1f s after the vote)", ok ? "opened" : "not opened",
                H->time_seconds() - g_own_editor_due_at);
        },
        nullptr);
}

} // namespace coop_core
