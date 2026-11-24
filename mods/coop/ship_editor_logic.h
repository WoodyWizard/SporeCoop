// Portable state machine for a guest's spaceship editor at the Civ -> Space vote (BUG-1531).
// Included by civ_arrival.cpp and by native unit tests (tests/civ_tests.cpp).
//
// At the end of Civ the host's game submits EditorRequest (0x96B24187, tag 0x0116D51D) through the
// co-op vote. When the vote passes, MSG_VOTE(on=0, editor=0x96B24187) reaches every guest. Because
// a guest of the shared nation receives city captures via cCity::ChangeOwner (not PlanetWon), its
// game never opens the spaceship editor itself. With civ.guest_ship_editor on, a connected guest in
// the host's Civ world opens its own spaceship editor (0x96B24187, tag kOwnShipTag = 0x0C0B1531 so
// cGameModeCiv does not launch a local single-player Space stage, with the red cross enabled so the
// guest may decline) once Civ has settled (0.5 s). Any Space world share from the host arriving
// while the editor is open is already deferred by world.cpp until 2 s after the guest leaves it.
#pragma once

#include <cstdint>

namespace ship_editor {

constexpr uint32_t kShipEditor = 0x96B24187u; // spaceship editor (SporeApp.exe 0x00CF5A7F)
constexpr uint32_t kNativeTag = 0x0116D51Du;  // host's native Civ -> Space tag (0x00CF5A86)
constexpr uint32_t kOwnShipTag = 0x0C0B1531u; // guest's own request tag (unused in SporeApp.exe)
constexpr uint32_t kUfoDesign = 0x98E03C0Du;  // planner UFO design slot (civ_editors.cpp)

constexpr double kSettleS = 0.5;            // brief settle in Civ before opening after the vote
constexpr double kPendingLimit = 90.0;      // max unpaused wait in Civ before dropping
constexpr double kPendingHardLimit = 300.0; // hard cap including pauses
constexpr double kOpenLimit = 30.0;         // max wait for game mode to switch to Editor after Submit

enum class Phase : int { Idle = 0, Pending, Opening, Open, Done };

inline const char* phase_name(Phase p)
{
    static const char* n[] = {"idle", "pending", "opening", "open", "done"};
    int i = (int)p;
    return i >= 0 && i <= (int)Phase::Done ? n[i] : "?";
}

enum class StepAction : int { Wait = 0, Open, Drop, Returned };

struct Result {
    bool seen = false;
    bool cancelled = false;
    uint32_t type_id = 0;
    uint32_t key[3] = {0, 0, 0};
};

struct State {
    Phase phase = Phase::Idle;
    double arm_at = 0;       // unpaused clock from vote end (paused dt shifts this forward)
    double arm_real = 0;     // wall time at vote end (hard cap)
    double settle_since = 0; // start of uninterrupted quiet Civ window
    double opened_at = 0;    // wall time when Submit succeeded
    bool editor_seen = false;
    Result res{};
    const char* why = "";

    unsigned votes = 0;
    unsigned opened = 0;
    unsigned open_failed = 0;
    unsigned results = 0;
    unsigned cancels = 0;
    unsigned dropped = 0;
    unsigned waits = 0;

    // Called when MSG_VOTE(on=0) ends a passed vote on a client.
    bool on_vote_end(uint32_t editor, uint32_t mode, uint32_t kCiv, bool is_guest, bool elsewhere, bool fix_on, double now)
    {
        if (!fix_on || !is_guest || elsewhere || editor != kShipEditor || mode != kCiv)
            return false;
        if (phase == Phase::Pending || phase == Phase::Opening || phase == Phase::Open)
            return false;
        phase = Phase::Pending;
        arm_at = arm_real = settle_since = now;
        opened_at = 0;
        editor_seen = false;
        res = Result{};
        why = "";
        votes++;
        return true;
    }

    // Advance while Pending. Caller submits editor outside the frame when StepAction::Open is returned.
    StepAction step_pending(bool connected, bool is_guest, bool fix_on, bool in_civ, bool in_editor, bool loading,
                            bool elsewhere, bool busy, bool paused, bool vote_active, double now, double dt)
    {
        if (phase != Phase::Pending)
            return StepAction::Wait;
        if (!fix_on || !connected || !is_guest) {
            phase = Phase::Idle;
            why = !connected ? "the session ended" : !fix_on ? "civ.guest_ship_editor off" : "not a guest";
            dropped++;
            return StepAction::Drop;
        }
        if (in_editor) {
            phase = Phase::Done;
            why = "editor already open";
            return StepAction::Drop;
        }
        if (elsewhere || (!in_civ && !loading)) {
            phase = Phase::Idle;
            why = "left the host's Civ world";
            dropped++;
            return StepAction::Drop;
        }
        if (paused && dt > 0 && dt < 5.0)
            arm_at += dt;
        if (now - arm_at > kPendingLimit || now - arm_real > kPendingHardLimit) {
            phase = Phase::Idle;
            why = "the Civ stage did not settle";
            dropped++;
            return StepAction::Drop;
        }
        if (!in_civ || loading || busy || paused || vote_active) {
            settle_since = now;
            waits++;
            return StepAction::Wait;
        }
        if (now - settle_since < kSettleS) {
            waits++;
            return StepAction::Wait;
        }
        phase = Phase::Opening;
        return StepAction::Open;
    }

    // Called inside the outside-frame open task if conditions became unsafe before Submit.
    void defer_opening(double now)
    {
        if (phase != Phase::Opening)
            return;
        phase = Phase::Pending;
        settle_since = now;
    }

    void on_submit_result(bool ok, double now)
    {
        if (phase != Phase::Opening)
            return;
        if (!ok) {
            open_failed++;
            dropped++;
            phase = Phase::Idle;
            why = "the game refused the spaceship editor request";
            return;
        }
        opened++;
        opened_at = now;
        editor_seen = false;
        phase = Phase::Open;
    }

    // Advance while Open. Returns StepAction::Returned once back in Civ after seeing the editor,
    // or StepAction::Drop if the editor timed out or transitioned directly out of Civ.
    StepAction step_open(bool in_civ, bool in_editor, bool loading, double now)
    {
        if (phase != Phase::Open)
            return StepAction::Wait;
        if (in_editor) {
            editor_seen = true;
            return StepAction::Wait;
        }
        if (!editor_seen) {
            if (now - opened_at > kOpenLimit) {
                dropped++;
                phase = Phase::Idle;
                why = "the spaceship editor did not show";
                return StepAction::Drop;
            }
            return StepAction::Wait;
        }
        if (loading)
            return StepAction::Wait;
        if (!in_civ) {
            phase = Phase::Done;
            why = "left Civ";
            return StepAction::Drop;
        }
        phase = Phase::Done;
        why = res.cancelled ? "cancelled" : "done";
        return StepAction::Returned;
    }

    // Called from cGameModeCiv's 0x30C11C7 listener on every editor result.
    bool on_result(uint32_t tag, uint32_t type_id, const uint32_t key[3], bool cancelled)
    {
        if (phase != Phase::Open || tag != kOwnShipTag)
            return false;
        res.seen = true;
        res.cancelled = cancelled;
        res.type_id = type_id;
        for (int i = 0; i < 3; i++)
            res.key[i] = key ? key[i] : 0;
        results++;
        if (cancelled)
            cancels++;
        return true;
    }

    // Session ended (BUG-0234): cancel a Pending open; keep an already Open editor tracked so return unpauses cleanly.
    void on_session_end()
    {
        if (phase == Phase::Open)
            return;
        if (phase == Phase::Pending || phase == Phase::Opening) {
            why = "the session ended";
            dropped++;
        }
        phase = Phase::Idle;
    }

    bool held() const { return phase == Phase::Pending || phase == Phase::Opening || phase == Phase::Open; }
};

} // namespace ship_editor
