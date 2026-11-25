// Pure portable logic for the Shared Adventure Editor (WP-AE of 0.8.0, BUG-0863):
//   - Host-authoritative DraftState + monotonic revision counter (`rev`)
//   - Per-marker renewable leases with automatic 5 s inactivity expiry
//   - Per-player Undo/Redo gesture stacks (forward + inverse deltas so each player undoes only their own gestures)
//   - Chunked snapshot generation and late-join / delta reconciliation on the guest
// No Windows or SporeKit dependencies: tested natively in tests/adv_editor_tests.cpp under ASan + UBSan.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include "net/compat.h"
#include "wire/adv_editor.h"

namespace adv_editor {

constexpr double kLeaseTtlSec = 5.0;               // s: automatic lease expiry after inactivity
constexpr double kDragLeaseReleaseSettleSec = 0.25; // s: release drag lease shortly after final flushed transform
constexpr double kDragPreviewPeriodSec = 0.1;    // s: 10 Hz unreliable live drag preview rate
constexpr size_t kMaxUndoPerPlayer = 128;        // gestures per player
constexpr size_t kMaxOpHistory = 512;            // recent applied ops retained for delta catch-up
constexpr uint8_t kNoOwner = 0xFF;

inline bool peer_supports(const std::vector<net::FamilyRev>& fams, bool has)
{
    return net::compat::has_minor(fams, has, (uint8_t)net::Family::creature, 1, wire::adv_editor::kCreatureMinor);
}

struct DraftState {
    wire::Key3 draft_key;
    uint32_t session_id = 0;
    uint32_t rev = 0;
    uint8_t num_acts = 1;
    uint8_t current_act = 0;
    wire::Key3 captain_key;
    std::map<uint32_t, wire::adv_editor::MarkerRec> markers;

    void reset()
    {
        draft_key = {};
        session_id = 0;
        rev = 0;
        num_acts = 1;
        current_act = 0;
        captain_key = {};
        markers.clear();
    }

    bool active() const { return session_id != 0; }
};

// Applies a validated concrete delta op (never OP_UNDO/OP_REDO) to `st`. Returns false if invalid.
inline bool apply_concrete_op(DraftState& st, const wire::adv_editor::OpData& op)
{
    using namespace wire::adv_editor;
    switch (op.kind) {
    case OP_CREATE_MARKER: {
        if (op.uid == 0 || st.markers.size() >= kMaxMarkers)
            return false;
        MarkerRec m;
        m.uid = op.uid;
        m.class_id = op.class_id;
        m.class_key = op.key;
        m.act = op.act < st.num_acts ? op.act : 0;
        for (int i = 0; i < 3; i++)
            m.pos[i] = op.pos[i];
        for (int i = 0; i < 4; i++)
            m.rot[i] = op.rot[i];
        m.scale = op.scale;
        st.markers[m.uid] = m;
        return true;
    }
    case OP_DELETE_MARKER: {
        auto it = st.markers.find(op.uid);
        if (it == st.markers.end())
            return false;
        st.markers.erase(it);
        return true;
    }
    case OP_TRANSFORM_MARKER: {
        auto it = st.markers.find(op.uid);
        if (it == st.markers.end())
            return false;
        for (int i = 0; i < 3; i++)
            it->second.pos[i] = op.pos[i];
        for (int i = 0; i < 4; i++)
            it->second.rot[i] = op.rot[i];
        it->second.scale = op.scale;
        return true;
    }
    case OP_ADD_ACT: {
        if (st.num_acts >= kMaxActs || op.act < 1 || op.act > st.num_acts)
            return false;
        st.num_acts++;
        return true;
    }
    case OP_DELETE_ACT: {
        if (st.num_acts <= 1 || op.act >= st.num_acts)
            return false;
        st.num_acts--;
        if (st.current_act >= st.num_acts)
            st.current_act = (uint8_t)(st.num_acts - 1);
        for (auto& [uid, m] : st.markers)
            if (m.act >= st.num_acts)
                m.act = (uint8_t)(st.num_acts - 1);
        return true;
    }
    case OP_SET_CAPTAIN: {
        st.captain_key = op.key;
        return true;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------- Leases

struct LeaseEntry {
    uint8_t owner = kNoOwner;
    double expires_at = 0.0;
};

struct ExpiredLease {
    uint32_t uid = 0;
    uint8_t owner = kNoOwner;
};

struct LeaseTable {
    std::map<uint32_t, LeaseEntry> leases;

    void clear() { leases.clear(); }

    uint8_t holder_of(uint32_t uid, double now) const
    {
        auto it = leases.find(uid);
        if (it == leases.end() || it->second.expires_at <= now)
            return kNoOwner;
        return it->second.owner;
    }

    bool can_edit(uint8_t actor, uint32_t uid, double now) const
    {
        uint8_t h = holder_of(uid, now);
        return h == kNoOwner || h == actor;
    }

    uint32_t remaining_ttl_ms(uint32_t uid, double now) const
    {
        auto it = leases.find(uid);
        if (it == leases.end() || it->second.expires_at <= now)
            return 0;
        double rem = (it->second.expires_at - now) * 1000.0;
        return (uint32_t)std::clamp(rem, 250.0, (double)wire::adv_editor::kMaxLeaseTtlMs);
    }

    // Attempts to acquire or renew lease on `uid` for `actor`. On conflict, writes current holder to `out_holder`.
    bool acquire_or_renew(uint32_t uid, uint8_t actor, double now, double ttl_sec = kLeaseTtlSec,
                          uint8_t* out_holder = nullptr)
    {
        if (uid == 0 || actor == kNoOwner)
            return false;
        double clamped_ttl = std::clamp(ttl_sec, 0.25, (double)wire::adv_editor::kMaxLeaseTtlMs / 1000.0);
        auto it = leases.find(uid);
        if (it != leases.end() && it->second.expires_at > now && it->second.owner != actor) {
            if (out_holder)
                *out_holder = it->second.owner;
            return false;
        }
        leases[uid] = {actor, now + clamped_ttl};
        if (out_holder)
            *out_holder = actor;
        return true;
    }

    bool release(uint32_t uid, uint8_t actor)
    {
        auto it = leases.find(uid);
        if (it == leases.end() || it->second.owner != actor)
            return false;
        leases.erase(it);
        return true;
    }

    void force_grant(uint32_t uid, uint8_t owner, double now, double ttl_sec = kLeaseTtlSec)
    {
        if (uid == 0 || owner == kNoOwner)
            return;
        leases[uid] = {owner, now + ttl_sec};
    }

    void force_clear(uint32_t uid) { leases.erase(uid); }

    std::vector<ExpiredLease> release_player(uint8_t actor)
    {
        std::vector<ExpiredLease> out;
        for (auto it = leases.begin(); it != leases.end();) {
            if (it->second.owner == actor) {
                out.push_back({it->first, actor});
                it = leases.erase(it);
            } else {
                ++it;
            }
        }
        return out;
    }

    std::vector<ExpiredLease> expire(double now)
    {
        std::vector<ExpiredLease> out;
        for (auto it = leases.begin(); it != leases.end();) {
            if (it->second.expires_at <= now) {
                out.push_back({it->first, it->second.owner});
                it = leases.erase(it);
            } else {
                ++it;
            }
        }
        return out;
    }
};

struct DragLeaseState {
    bool dirty = true;
    double last_touch = 0.0;
    double flushed_at = 0.0;
};

// Tracks markers leased by local interactive drags so leases are released immediately on CommitHistory
// or shortly after the final flushed transform goes quiet (instead of waiting for the 5 s TTL).
struct DragLeaseTracker {
    std::map<uint32_t, DragLeaseState> active;

    void clear() { active.clear(); }
    bool empty() const { return active.empty(); }
    bool contains(uint32_t uid) const { return active.count(uid) != 0; }

    void on_drag_acquire(uint32_t uid, double now)
    {
        if (uid == 0)
            return;
        auto& st = active[uid];
        st.dirty = true;
        st.last_touch = now;
    }

    void on_drag_touch(uint32_t uid, double now)
    {
        auto it = active.find(uid);
        if (it == active.end())
            return;
        it->second.dirty = true;
        it->second.last_touch = now;
    }

    void on_transform_flushed(uint32_t uid, double now)
    {
        auto it = active.find(uid);
        if (it == active.end())
            return;
        it->second.dirty = false;
        it->second.flushed_at = now;
    }

    void forget(uint32_t uid) { active.erase(uid); }

    // Releases all tracked drag leases immediately (e.g. on CommitHistory after flushing dirty transforms).
    std::vector<uint32_t> take_all()
    {
        std::vector<uint32_t> out;
        out.reserve(active.size());
        for (const auto& [uid, st] : active)
            out.push_back(uid);
        active.clear();
        return out;
    }

    // Releases drag leases whose final transform has been flushed and stayed quiet for `settle_sec`.
    std::vector<uint32_t> take_settled(double now, double settle_sec = kDragLeaseReleaseSettleSec)
    {
        std::vector<uint32_t> out;
        for (auto it = active.begin(); it != active.end();) {
            if (!it->second.dirty && (now - it->second.flushed_at >= settle_sec)) {
                out.push_back(it->first);
                it = active.erase(it);
            } else {
                ++it;
            }
        }
        return out;
    }
};

enum class UndoRoute : uint8_t {
    None = 0,
    Native = 1,
    Synced = 2,
};

// Routes Ctrl+Z / Ctrl+Y between the shared Adventure Editor op log and Spore's native ScenarioEditHistory
// so native-only edits (terrain, atmosphere, goals, cast/marker properties) remain undoable with Ctrl+Z.
struct UndoRouter {
    static constexpr size_t kMaxEntries = 256;

    std::vector<UndoRoute> undo_stack;
    std::vector<UndoRoute> redo_stack;
    bool pending_synced_gesture = false;

    void clear()
    {
        undo_stack.clear();
        redo_stack.clear();
        pending_synced_gesture = false;
    }

    // Called when any synced edit hook fires (Create/Clone/Delete/Transform marker, Add/Delete Act, SetCaptain).
    void mark_synced_gesture() { pending_synced_gesture = true; }

    // Called from h_CommitHistory after flushing dirty transforms.
    // Records whether this history commit belonged to a synced op or a native-only edit.
    UndoRoute on_commit_history()
    {
        UndoRoute kind = pending_synced_gesture ? UndoRoute::Synced : UndoRoute::Native;
        pending_synced_gesture = false;
        push_undo(kind);
        return kind;
    }

    // Called when a synced op is submitted outside CommitHistory (e.g. console command)
    // so top-of-stack reflects the synced op without misclassifying a subsequent native CommitHistory.
    void on_synced_op_committed_directly()
    {
        if (pending_synced_gesture)
            return;
        push_undo(UndoRoute::Synced);
    }

    // Decides which backend should handle CanUndo / Undo without mutating the stacks.
    UndoRoute peek_undo(uint8_t shared_undo_depth, bool native_can_undo) const
    {
        if (pending_synced_gesture)
            return shared_undo_depth > 0 ? UndoRoute::Synced : (native_can_undo ? UndoRoute::Native : UndoRoute::None);
        for (auto it = undo_stack.rbegin(); it != undo_stack.rend(); ++it) {
            if (*it == UndoRoute::Synced) {
                if (shared_undo_depth > 0)
                    return UndoRoute::Synced;
                continue; // synced op already consumed (e.g. console undo or rejected); fall through
            }
            if (*it == UndoRoute::Native)
                return native_can_undo ? UndoRoute::Native : UndoRoute::None;
        }
        if (shared_undo_depth > 0)
            return UndoRoute::Synced;
        return native_can_undo ? UndoRoute::Native : UndoRoute::None;
    }

    // Decides which backend should handle CanRedo / Redo without mutating the stacks.
    UndoRoute peek_redo(uint8_t shared_redo_depth, bool native_can_redo) const
    {
        for (auto it = redo_stack.rbegin(); it != redo_stack.rend(); ++it) {
            if (*it == UndoRoute::Synced) {
                if (shared_redo_depth > 0)
                    return UndoRoute::Synced;
                continue;
            }
            if (*it == UndoRoute::Native)
                return native_can_redo ? UndoRoute::Native : UndoRoute::None;
        }
        if (shared_redo_depth > 0)
            return UndoRoute::Synced;
        return native_can_redo ? UndoRoute::Native : UndoRoute::None;
    }

    // Commits an Undo transition on the router stack after deciding the route.
    void consume_undo(UndoRoute route)
    {
        if (route == UndoRoute::None)
            return;
        if (route == UndoRoute::Synced && pending_synced_gesture) {
            pending_synced_gesture = false;
            push_redo(UndoRoute::Synced);
            return;
        }
        while (!undo_stack.empty() && undo_stack.back() != route)
            undo_stack.pop_back();
        if (!undo_stack.empty())
            undo_stack.pop_back();
        push_redo(route);
    }

    // Commits a Redo transition on the router stack after deciding the route.
    void consume_redo(UndoRoute route)
    {
        if (route == UndoRoute::None)
            return;
        while (!redo_stack.empty() && redo_stack.back() != route)
            redo_stack.pop_back();
        if (!redo_stack.empty())
            redo_stack.pop_back();
        if (undo_stack.size() >= kMaxEntries)
            undo_stack.erase(undo_stack.begin());
        undo_stack.push_back(route);
    }

    // Reverts a consumed Synced Undo/Redo if the host rejected the guest's OP_UNDO / OP_REDO.
    void rollback_undo()
    {
        if (!redo_stack.empty() && redo_stack.back() == UndoRoute::Synced) {
            redo_stack.pop_back();
            if (undo_stack.size() >= kMaxEntries)
                undo_stack.erase(undo_stack.begin());
            undo_stack.push_back(UndoRoute::Synced);
        }
    }

    void rollback_redo()
    {
        if (!undo_stack.empty() && undo_stack.back() == UndoRoute::Synced) {
            undo_stack.pop_back();
            push_redo(UndoRoute::Synced);
        }
    }

  private:
    void push_undo(UndoRoute r)
    {
        if (undo_stack.size() >= kMaxEntries)
            undo_stack.erase(undo_stack.begin());
        undo_stack.push_back(r);
        redo_stack.clear();
    }

    void push_redo(UndoRoute r)
    {
        if (redo_stack.size() >= kMaxEntries)
            redo_stack.erase(redo_stack.begin());
        redo_stack.push_back(r);
    }
};

// ---------------------------------------------------------------- Per-Player Undo/Redo & Host Authority

struct GestureEntry {
    wire::adv_editor::OpData forward;
    wire::adv_editor::OpData inverse;
};

struct PlayerHistory {
    std::vector<GestureEntry> undo;
    std::vector<GestureEntry> redo;

    void push_gesture(const GestureEntry& g)
    {
        if (undo.size() >= kMaxUndoPerPlayer)
            undo.erase(undo.begin());
        undo.push_back(g);
        redo.clear();
    }
};

struct HostEditor {
    DraftState state;
    LeaseTable leases;
    std::unordered_map<uint8_t, PlayerHistory> history;
    std::vector<wire::adv_editor::OpApplied> op_log;
    uint32_t next_uid = 1;

    void reset()
    {
        state.reset();
        leases.clear();
        history.clear();
        op_log.clear();
        next_uid = 1;
    }

    void start_session(const wire::Key3& draft_key, uint32_t session_id, uint8_t num_acts, uint8_t current_act,
                       const wire::Key3& captain_key, const std::vector<wire::adv_editor::MarkerRec>& initial_markers)
    {
        reset();
        state.draft_key = draft_key;
        state.session_id = session_id ? session_id : 1u;
        state.rev = 0;
        state.num_acts = std::clamp<uint8_t>(num_acts, 1, wire::adv_editor::kMaxActs);
        state.current_act = current_act < state.num_acts ? current_act : 0;
        state.captain_key = captain_key;
        uint32_t max_u = 0;
        for (const auto& m : initial_markers) {
            if (m.uid == 0 || state.markers.size() >= wire::adv_editor::kMaxMarkers)
                continue;
            state.markers[m.uid] = m;
            if (m.uid > max_u)
                max_u = m.uid;
        }
        next_uid = max_u + 1;
    }

    bool active() const { return state.active(); }

    uint8_t undo_depth(uint8_t actor) const
    {
        auto it = history.find(actor);
        if (it == history.end())
            return 0;
        return (uint8_t)std::min<size_t>(it->second.undo.size(), 255);
    }

    uint8_t redo_depth(uint8_t actor) const
    {
        auto it = history.find(actor);
        if (it == history.end())
            return 0;
        return (uint8_t)std::min<size_t>(it->second.redo.size(), 255);
    }

    // Builds chunked K_SNAPSHOT messages covering the entire authoritative DraftState.
    std::vector<wire::adv_editor::SnapshotChunk> build_snapshot_chunks() const
    {
        using namespace wire::adv_editor;
        std::vector<SnapshotChunk> chunks;
        if (!active())
            return chunks;
        std::vector<MarkerRec> all;
        all.reserve(state.markers.size());
        for (const auto& [uid, m] : state.markers)
            all.push_back(m);
        uint8_t total = all.empty()
                            ? 1
                            : (uint8_t)((all.size() + kMaxMarkersPerChunk - 1) / kMaxMarkersPerChunk);
        if (total > kMaxChunks)
            total = kMaxChunks;
        for (uint8_t ci = 0; ci < total; ci++) {
            SnapshotChunk c;
            c.hdr = {state.draft_key, state.session_id};
            c.rev = state.rev;
            c.num_acts = state.num_acts;
            c.current_act = state.current_act;
            c.captain_key = state.captain_key;
            c.chunk_idx = ci;
            c.chunk_total = total;
            size_t start = (size_t)ci * kMaxMarkersPerChunk;
            size_t end = std::min(all.size(), start + kMaxMarkersPerChunk);
            for (size_t i = start; i < end; i++)
                c.markers.push_back(all[i]);
            chunks.push_back(std::move(c));
        }
        return chunks;
    }

    // If `last_rev` is reachable in `op_log`, returns the exact delta ops after `last_rev`; else returns false (caller
    // should send a full chunked snapshot).
    bool ops_since(uint32_t last_rev, std::vector<wire::adv_editor::OpApplied>& out) const
    {
        out.clear();
        if (!active())
            return false;
        if (last_rev == state.rev)
            return true;
        if (last_rev > state.rev || op_log.empty())
            return false;
        if (op_log.front().rev > last_rev + 1)
            return false;
        for (const auto& a : op_log)
            if (a.rev > last_rev)
                out.push_back(a);
        return true;
    }

    // Validates and applies `req` on behalf of `actor`. Handles normal ops and per-player OP_UNDO/OP_REDO.
    bool apply_op(uint8_t actor, uint32_t client_op_id, const wire::adv_editor::OpData& req, double now,
                  wire::adv_editor::OpApplied& out_applied, wire::adv_editor::OpReject& out_reject)
    {
        using namespace wire::adv_editor;
        out_applied = {};
        out_reject = {{state.draft_key, state.session_id}, client_op_id, REJECT_NONE, kNoOwner, req.uid};
        if (!active()) {
            out_reject.reason = REJECT_INVALID;
            return false;
        }
        leases.expire(now);

        if (req.kind == OP_UNDO || req.kind == OP_REDO)
            return apply_undo_redo(actor, client_op_id, req.kind == OP_UNDO, now, out_applied, out_reject);

        OpData concrete = req;
        OpData inverse = {};
        switch (req.kind) {
        case OP_CREATE_MARKER: {
            if (state.markers.size() >= kMaxMarkers) {
                out_reject.reason = REJECT_LIMIT;
                return false;
            }
            if (concrete.uid == 0 || state.markers.count(concrete.uid))
                concrete.uid = next_uid++;
            else if (concrete.uid >= next_uid)
                next_uid = concrete.uid + 1;
            if (concrete.act >= state.num_acts)
                concrete.act = 0;
            inverse.kind = OP_DELETE_MARKER;
            inverse.uid = concrete.uid;
            break;
        }
        case OP_DELETE_MARKER: {
            auto it = state.markers.find(req.uid);
            if (it == state.markers.end()) {
                out_reject.reason = REJECT_NOT_FOUND;
                return false;
            }
            if (!leases.can_edit(actor, req.uid, now)) {
                out_reject.reason = REJECT_LEASE_HELD;
                out_reject.holder_id = leases.holder_of(req.uid, now);
                return false;
            }
            const MarkerRec& m = it->second;
            inverse.kind = OP_CREATE_MARKER;
            inverse.uid = m.uid;
            inverse.class_id = m.class_id;
            inverse.key = m.class_key;
            inverse.act = m.act;
            for (int i = 0; i < 3; i++)
                inverse.pos[i] = m.pos[i];
            for (int i = 0; i < 4; i++)
                inverse.rot[i] = m.rot[i];
            inverse.scale = m.scale;
            leases.force_clear(req.uid);
            break;
        }
        case OP_TRANSFORM_MARKER: {
            auto it = state.markers.find(req.uid);
            if (it == state.markers.end()) {
                out_reject.reason = REJECT_NOT_FOUND;
                return false;
            }
            if (!leases.can_edit(actor, req.uid, now)) {
                out_reject.reason = REJECT_LEASE_HELD;
                out_reject.holder_id = leases.holder_of(req.uid, now);
                return false;
            }
            const MarkerRec& m = it->second;
            inverse.kind = OP_TRANSFORM_MARKER;
            inverse.uid = m.uid;
            for (int i = 0; i < 3; i++)
                inverse.pos[i] = m.pos[i];
            for (int i = 0; i < 4; i++)
                inverse.rot[i] = m.rot[i];
            inverse.scale = m.scale;
            break;
        }
        case OP_ADD_ACT: {
            if (state.num_acts >= kMaxActs) {
                out_reject.reason = REJECT_LIMIT;
                return false;
            }
            if (concrete.act == 0 || concrete.act > state.num_acts)
                concrete.act = state.num_acts;
            inverse.kind = OP_DELETE_ACT;
            inverse.act = concrete.act;
            break;
        }
        case OP_DELETE_ACT: {
            if (state.num_acts <= 1 || req.act >= state.num_acts) {
                out_reject.reason = REJECT_INVALID;
                return false;
            }
            inverse.kind = OP_ADD_ACT;
            inverse.act = req.act == 0 ? 1 : req.act;
            break;
        }
        case OP_SET_CAPTAIN: {
            inverse.kind = OP_SET_CAPTAIN;
            inverse.key = state.captain_key;
            break;
        }
        default:
            out_reject.reason = REJECT_INVALID;
            return false;
        }

        if (!apply_concrete_op(state, concrete)) {
            out_reject.reason = REJECT_INVALID;
            return false;
        }
        history[actor].push_gesture({concrete, inverse});
        record_applied(actor, client_op_id, 0, concrete, out_applied);
        return true;
    }

  private:
    bool apply_undo_redo(uint8_t actor, uint32_t client_op_id, bool is_undo, double now,
                         wire::adv_editor::OpApplied& out_applied, wire::adv_editor::OpReject& out_reject)
    {
        using namespace wire::adv_editor;
        auto& ph = history[actor];
        auto& src = is_undo ? ph.undo : ph.redo;
        auto& dst = is_undo ? ph.redo : ph.undo;
        if (src.empty()) {
            out_reject.reason = is_undo ? REJECT_NOTHING_TO_UNDO : REJECT_NOTHING_TO_REDO;
            return false;
        }
        GestureEntry g = src.back();
        const OpData& target = is_undo ? g.inverse : g.forward;
        out_reject.uid = target.uid;

        // Validate target against current state and leases before popping
        if (target.kind == OP_TRANSFORM_MARKER || target.kind == OP_DELETE_MARKER) {
            if (!state.markers.count(target.uid)) {
                out_reject.reason = REJECT_NOT_FOUND;
                return false;
            }
            if (!leases.can_edit(actor, target.uid, now)) {
                out_reject.reason = REJECT_LEASE_HELD;
                out_reject.holder_id = leases.holder_of(target.uid, now);
                return false;
            }
            if (target.kind == OP_DELETE_MARKER)
                leases.force_clear(target.uid);
        } else if (target.kind == OP_CREATE_MARKER) {
            if (state.markers.size() >= kMaxMarkers) {
                out_reject.reason = REJECT_LIMIT;
                return false;
            }
            if (state.markers.count(target.uid)) {
                out_reject.reason = REJECT_INVALID;
                return false;
            }
        }
        if (!apply_concrete_op(state, target)) {
            out_reject.reason = REJECT_INVALID;
            return false;
        }
        src.pop_back();
        if (dst.size() >= kMaxUndoPerPlayer)
            dst.erase(dst.begin());
        dst.push_back(g);
        record_applied(actor, client_op_id, is_undo ? kAppliedFlagFromUndo : kAppliedFlagFromRedo, target, out_applied);
        return true;
    }

    void record_applied(uint8_t actor, uint32_t client_op_id, uint8_t flags, const wire::adv_editor::OpData& op,
                        wire::adv_editor::OpApplied& out)
    {
        state.rev++;
        out.hdr = {state.draft_key, state.session_id};
        out.rev = state.rev;
        out.actor_id = actor;
        out.client_op_id = client_op_id;
        out.flags = flags;
        out.actor_undo_depth = undo_depth(actor);
        out.actor_redo_depth = redo_depth(actor);
        out.op = op;
        if (op_log.size() >= kMaxOpHistory)
            op_log.erase(op_log.begin());
        op_log.push_back(out);
    }
};

// ---------------------------------------------------------------- Guest Reconciliation

struct GuestEditor {
    DraftState state;
    LeaseTable leases;
    bool synced = false;
    uint8_t my_id = 0;
    uint8_t my_undo_depth = 0;
    uint8_t my_redo_depth = 0;
    std::map<uint32_t, wire::adv_editor::OpApplied> pending_ops;

    // Draft key adopted from host when host saves an unsaved draft or Save-As's a shared draft
    wire::Key3 adopted_draft_key;
    bool has_adopted_draft_key = false;

    // Chunked snapshot assembly buffer
    uint32_t snap_session_id = 0;
    uint32_t snap_rev = 0;
    uint8_t snap_num_acts = 1;
    uint8_t snap_cur_act = 0;
    wire::Key3 snap_draft_key;
    wire::Key3 snap_captain_key;
    uint8_t snap_total = 0;
    std::map<uint8_t, std::vector<wire::adv_editor::MarkerRec>> snap_chunks;

    static bool is_empty_key(const wire::Key3& k) { return k.v[0] == 0 && k.v[1] == 0 && k.v[2] == 0; }

    wire::Key3 effective_draft_key(const wire::Key3& local_dk) const
    {
        return has_adopted_draft_key ? adopted_draft_key : local_dk;
    }

    // Whether the guest should accept a host message carrying `host_dk` given the guest's local `local_dk`.
    bool accepts_draft_key(const wire::Key3& host_dk, const wire::Key3& local_dk) const
    {
        if (host_dk == effective_draft_key(local_dk))
            return true;
        return is_empty_key(local_dk) || synced || has_adopted_draft_key;
    }

    void adopt_draft_key_if_needed(const wire::Key3& host_dk, const wire::Key3& local_dk)
    {
        if (!is_empty_key(host_dk) && host_dk != local_dk) {
            adopted_draft_key = host_dk;
            has_adopted_draft_key = true;
        } else if (host_dk == local_dk) {
            adopted_draft_key = {};
            has_adopted_draft_key = false;
        }
    }

    bool session_mismatch(const wire::adv_editor::DraftHeader& hdr) const
    {
        return synced && (hdr.session_id != state.session_id || hdr.draft_key != state.draft_key);
    }

    const wire::adv_editor::MarkerRec* find_marker(uint32_t uid) const
    {
        auto it = state.markers.find(uid);
        return it == state.markers.end() ? nullptr : &it->second;
    }

    void reset()
    {
        state.reset();
        leases.clear();
        synced = false;
        my_undo_depth = 0;
        my_redo_depth = 0;
        pending_ops.clear();
        adopted_draft_key = {};
        has_adopted_draft_key = false;
        clear_snap();
    }

    // Handles a host session reset signal (session_id == 0 when host left edit mode, or a new session_id
    // after Save / Play-test re-entry). Clears synced state and adopts the new draft_key if applicable.
    // Returns true if accepted (caller should clear native bindings and re-send K_HELLO_SYNC when session_id != 0).
    bool on_session_reset(const wire::adv_editor::DraftHeader& hdr, const wire::Key3& local_dk)
    {
        if (!accepts_draft_key(hdr.draft_key, local_dk))
            return false;
        bool had_adopted = has_adopted_draft_key;
        wire::Key3 prev_adopted = adopted_draft_key;
        reset();
        if (!is_empty_key(hdr.draft_key) && hdr.draft_key != local_dk) {
            adopted_draft_key = hdr.draft_key;
            has_adopted_draft_key = true;
        } else if (hdr.session_id == 0 && had_adopted && is_empty_key(hdr.draft_key)) {
            adopted_draft_key = prev_adopted;
            has_adopted_draft_key = true;
        }
        return true;
    }

    void clear_snap()
    {
        snap_session_id = 0;
        snap_rev = 0;
        snap_num_acts = 1;
        snap_cur_act = 0;
        snap_draft_key = {};
        snap_captain_key = {};
        snap_total = 0;
        snap_chunks.clear();
    }

    // Feeds one K_SNAPSHOT chunk. Returns true when all chunks for this snapshot have arrived and `state` is updated.
    bool on_snapshot_chunk(const wire::adv_editor::SnapshotChunk& c)
    {
        if (c.hdr.session_id == 0 || c.chunk_total == 0 || c.chunk_idx >= c.chunk_total)
            return false;
        if (snap_session_id != c.hdr.session_id || snap_rev != c.rev || snap_total != c.chunk_total) {
            clear_snap();
            snap_session_id = c.hdr.session_id;
            snap_rev = c.rev;
            snap_num_acts = c.num_acts;
            snap_cur_act = c.current_act;
            snap_draft_key = c.hdr.draft_key;
            snap_captain_key = c.captain_key;
            snap_total = c.chunk_total;
        }
        snap_chunks[c.chunk_idx] = c.markers;
        if (snap_chunks.size() < snap_total)
            return false;

        // Snapshot complete! If switching sessions, discard old leases, undo depths, and pending ops.
        bool session_changed = (state.session_id != snap_session_id);
        if (session_changed) {
            leases.clear();
            my_undo_depth = 0;
            my_redo_depth = 0;
            pending_ops.clear();
        }
        state.draft_key = snap_draft_key;
        state.session_id = snap_session_id;
        state.rev = snap_rev;
        state.num_acts = snap_num_acts;
        state.current_act = snap_cur_act;
        state.captain_key = snap_captain_key;
        state.markers.clear();
        for (const auto& [idx, vec] : snap_chunks)
            for (const auto& m : vec)
                state.markers[m.uid] = m;
        clear_snap();
        synced = true;
        // Drop any pending ops already covered by the snapshot or from a different session
        for (auto it = pending_ops.begin(); it != pending_ops.end();) {
            if (it->first <= state.rev || it->second.hdr.session_id != state.session_id)
                it = pending_ops.erase(it);
            else
                ++it;
        }
        return true;
    }

    // Buffers an incoming K_OP_APPLIED and drains all contiguous ready ops (`rev == state.rev + 1`) into `out_ready`.
    void on_op_applied(const wire::adv_editor::OpApplied& a, std::vector<wire::adv_editor::OpApplied>& out_ready)
    {
        out_ready.clear();
        if (!synced || a.hdr.session_id != state.session_id)
            return;
        if (a.rev <= state.rev)
            return;
        pending_ops[a.rev] = a;
        while (true) {
            auto it = pending_ops.find(state.rev + 1);
            if (it == pending_ops.end())
                break;
            wire::adv_editor::OpApplied cur = std::move(it->second);
            pending_ops.erase(it);
            apply_concrete_op(state, cur.op);
            state.rev = cur.rev;
            if (cur.actor_id == my_id) {
                my_undo_depth = cur.actor_undo_depth;
                my_redo_depth = cur.actor_redo_depth;
            }
            if (cur.op.kind == wire::adv_editor::OP_DELETE_MARKER)
                leases.force_clear(cur.op.uid);
            out_ready.push_back(std::move(cur));
        }
    }

    void on_lease(const wire::adv_editor::LeaseMsg& l, double now)
    {
        using namespace wire::adv_editor;
        if (!synced || l.hdr.session_id != state.session_id)
            return;
        if (l.action == LEASE_GRANT) {
            leases.force_grant(l.uid, l.owner_id, now, (double)l.ttl_ms / 1000.0);
        } else if (l.action == LEASE_DENY) {
            if (l.owner_id != kNoOwner && l.owner_id != my_id) {
                double ttl = l.ttl_ms > 0 ? (double)l.ttl_ms / 1000.0 : kLeaseTtlSec;
                leases.force_grant(l.uid, l.owner_id, now, ttl);
            }
        } else if (l.action == LEASE_RELEASE || l.action == LEASE_EXPIRED) {
            leases.force_clear(l.uid);
        }
    }

    // Handles K_OP_REJECT on the guest: records conflicting lease holder (if REJECT_LEASE_HELD) and writes
    // the authoritative MarkerRec to `out_snap_back` when `rej.uid` exists so the caller snaps it back.
    bool on_op_reject(const wire::adv_editor::OpReject& rej, double now,
                      wire::adv_editor::MarkerRec* out_snap_back = nullptr)
    {
        using namespace wire::adv_editor;
        if (!synced || rej.hdr.session_id != state.session_id)
            return false;
        if (rej.reason == REJECT_LEASE_HELD && rej.uid != 0 && rej.holder_id != kNoOwner && rej.holder_id != my_id)
            leases.force_grant(rej.uid, rej.holder_id, now, kLeaseTtlSec);
        if (rej.uid != 0) {
            if (const MarkerRec* m = find_marker(rej.uid)) {
                if (out_snap_back)
                    *out_snap_back = *m;
                return true;
            }
        }
        return false;
    }
};

} // namespace adv_editor
