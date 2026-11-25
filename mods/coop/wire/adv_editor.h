// Payloads of MSG_ADV_EDITOR 82 (creature 1.5, WP-AE of 0.8.0, BUG-0863): host-authoritative Shared Adventure Editor
// placements, transforms, acts, captain asset, per-marker renewable leases, live drag previews, per-player Undo/Redo,
// and chunked snapshot sync for late joiners. Body after MSG_ADV_EDITOR is u8 kind, then the kind's struct.
// Rules: wire/common.h.
#pragma once

#include "common.h"

namespace wire::adv_editor {

// creature family minor that introduces MSG_ADV_EDITOR (82)
constexpr uint16_t kCreatureMinor = 5;

constexpr uint16_t kMaxMarkers = 1024;
constexpr uint8_t kMaxMarkersPerChunk = 16; // 16 * 53 bytes + header < 1000 bytes (below kMaxPacket = 1200)
constexpr uint8_t kMaxChunks = 64;          // 64 * 16 = 1024 markers
constexpr uint8_t kMaxActs = 8;
constexpr uint32_t kMaxLeaseTtlMs = 30000;
constexpr float kMinScale = 0.01f;
constexpr float kMaxScale = 1000.0f;
constexpr float kMaxCoord = 100000.0f;

enum Kind : uint8_t {
    K_HELLO_SYNC = 0,   // guest -> host (R): announce editor entry + last applied rev
    K_SNAPSHOT = 1,     // host -> guest (R): chunked authoritative draft snapshot (acts, markers, captain, rev)
    K_OP_REQ = 2,       // guest -> host (R): request an edit operation (or Undo/Redo)
    K_OP_APPLIED = 3,   // host -> guests (R): authoritative committed op (concrete delta, rev, actor, undo/redo flags)
    K_LEASE = 4,        // any -> host / host -> guests (R): acquire / renew / release / grant / deny / expired lease
    K_DRAG_PREVIEW = 5, // any -> host / host -> guests (U, 10 Hz): unreliable live drag preview transform
    K_OP_REJECT = 6,    // host -> guest (R): op request rejected (lease conflict, limit, invalid, empty undo/redo)
};

enum OpKind : uint8_t {
    OP_CREATE_MARKER = 1,
    OP_DELETE_MARKER = 2,
    OP_TRANSFORM_MARKER = 3,
    OP_ADD_ACT = 4,
    OP_DELETE_ACT = 5,
    OP_SET_CAPTAIN = 6,
    OP_UNDO = 7,
    OP_REDO = 8,
};

enum LeaseAction : uint8_t {
    LEASE_ACQUIRE = 1,
    LEASE_RENEW = 2,
    LEASE_RELEASE = 3,
    LEASE_GRANT = 4,
    LEASE_DENY = 5,
    LEASE_EXPIRED = 6,
};

enum RejectReason : uint8_t {
    REJECT_NONE = 0,
    REJECT_LEASE_HELD = 1,
    REJECT_NOT_FOUND = 2,
    REJECT_LIMIT = 3,
    REJECT_INVALID = 4,
    REJECT_NOTHING_TO_UNDO = 5,
    REJECT_NOTHING_TO_REDO = 6,
};

constexpr uint8_t kAppliedFlagFromUndo = 1;
constexpr uint8_t kAppliedFlagFromRedo = 2;

inline bool finite4(const float* v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && std::isfinite(v[3]);
}

inline bool valid_pos(const float* p)
{
    return finite3(p) && std::fabs(p[0]) <= kMaxCoord && std::fabs(p[1]) <= kMaxCoord && std::fabs(p[2]) <= kMaxCoord;
}

inline bool valid_rot(const float* q)
{
    if (!finite4(q))
        return false;
    float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return std::isfinite(n2) && n2 >= 1e-6f && n2 <= 100.f;
}

inline bool valid_scale(float s)
{
    return std::isfinite(s) && ((s == 0.f) || (s >= kMinScale && s <= kMaxScale));
}

// Common draft header carried by every MSG_ADV_EDITOR payload
struct DraftHeader {
    Key3 draft_key;
    uint32_t session_id = 0;

    void write(net::Writer& w) const
    {
        put_key(w, draft_key);
        w.u32(session_id);
    }
    bool read(net::Reader& r)
    {
        draft_key = get_key(r);
        session_id = r.u32();
        return r.ok();
    }
};

struct MarkerRec {
    uint32_t uid = 0;
    int32_t class_id = -1;
    Key3 class_key;
    uint8_t act = 0;
    float pos[3] = {};
    float rot[4] = {0.f, 0.f, 0.f, 1.f};
    float scale = 1.f;

    void write(net::Writer& w) const
    {
        w.u32(uid);
        w.u32((uint32_t)class_id);
        put_key(w, class_key);
        w.u8(act);
        for (float f : pos)
            w.f32(f);
        for (float f : rot)
            w.f32(f);
        w.f32(scale);
    }
    bool read(net::Reader& r)
    {
        uid = r.u32();
        class_id = (int32_t)r.u32();
        class_key = get_key(r);
        act = r.u8();
        for (float& f : pos)
            f = r.f32();
        for (float& f : rot)
            f = r.f32();
        scale = r.f32();
        return r.ok() && uid != 0 && class_id >= -1 && act < kMaxActs && valid_pos(pos) && valid_rot(rot) &&
               valid_scale(scale);
    }
};

struct OpData {
    uint8_t kind = 0; // OpKind
    uint32_t uid = 0;
    int32_t class_id = -1;
    Key3 key; // class_key for OP_CREATE_MARKER, captain_key for OP_SET_CAPTAIN
    uint8_t act = 0;
    float pos[3] = {};
    float rot[4] = {0.f, 0.f, 0.f, 1.f};
    float scale = 1.f;

    void write(net::Writer& w) const
    {
        w.u8(kind);
        switch (kind) {
        case OP_CREATE_MARKER:
            w.u32(uid);
            w.u32((uint32_t)class_id);
            put_key(w, key);
            w.u8(act);
            for (float f : pos)
                w.f32(f);
            for (float f : rot)
                w.f32(f);
            w.f32(scale);
            break;
        case OP_DELETE_MARKER:
            w.u32(uid);
            break;
        case OP_TRANSFORM_MARKER:
            w.u32(uid);
            for (float f : pos)
                w.f32(f);
            for (float f : rot)
                w.f32(f);
            w.f32(scale);
            break;
        case OP_ADD_ACT:
        case OP_DELETE_ACT:
            w.u8(act);
            break;
        case OP_SET_CAPTAIN:
            put_key(w, key);
            break;
        case OP_UNDO:
        case OP_REDO:
            break;
        default:
            break;
        }
    }

    bool read(net::Reader& r)
    {
        *this = {};
        kind = r.u8();
        if (!r.ok())
            return false;
        switch (kind) {
        case OP_CREATE_MARKER:
            uid = r.u32();
            class_id = (int32_t)r.u32();
            key = get_key(r);
            act = r.u8();
            for (float& f : pos)
                f = r.f32();
            for (float& f : rot)
                f = r.f32();
            scale = r.f32();
            return r.ok() && class_id >= -1 && act < kMaxActs && valid_pos(pos) && valid_rot(rot) &&
                   valid_scale(scale);
        case OP_DELETE_MARKER:
            uid = r.u32();
            return r.ok() && uid != 0;
        case OP_TRANSFORM_MARKER:
            uid = r.u32();
            for (float& f : pos)
                f = r.f32();
            for (float& f : rot)
                f = r.f32();
            scale = r.f32();
            return r.ok() && uid != 0 && valid_pos(pos) && valid_rot(rot) && valid_scale(scale);
        case OP_ADD_ACT:
        case OP_DELETE_ACT:
            act = r.u8();
            return r.ok() && act < kMaxActs;
        case OP_SET_CAPTAIN:
            key = get_key(r);
            return r.ok();
        case OP_UNDO:
        case OP_REDO:
            return r.ok();
        default:
            return false;
        }
    }
};

// K_HELLO_SYNC (guest -> host)
struct HelloSync {
    DraftHeader hdr;
    uint32_t last_rev = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(last_rev);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        last_rev = r.u32();
        return r.ok();
    }
};

// K_SNAPSHOT (host -> guest): chunked authoritative draft state
struct SnapshotChunk {
    DraftHeader hdr;
    uint32_t rev = 0;
    uint8_t num_acts = 1;
    uint8_t current_act = 0;
    Key3 captain_key;
    uint8_t chunk_idx = 0;
    uint8_t chunk_total = 1;
    std::vector<MarkerRec> markers;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(rev);
        w.u8(num_acts);
        w.u8(current_act);
        put_key(w, captain_key);
        w.u8(chunk_idx);
        w.u8(chunk_total);
        uint8_t n = (uint8_t)(markers.size() < kMaxMarkersPerChunk ? markers.size() : kMaxMarkersPerChunk);
        w.u8(n);
        for (uint8_t i = 0; i < n; i++)
            markers[i].write(w);
    }
    bool read(net::Reader& r)
    {
        markers.clear();
        if (!hdr.read(r))
            return false;
        rev = r.u32();
        num_acts = r.u8();
        current_act = r.u8();
        captain_key = get_key(r);
        chunk_idx = r.u8();
        chunk_total = r.u8();
        uint8_t n = r.u8();
        if (!r.ok() || hdr.session_id == 0 || num_acts < 1 || num_acts > kMaxActs || current_act >= num_acts ||
            chunk_total < 1 || chunk_total > kMaxChunks || chunk_idx >= chunk_total || n > kMaxMarkersPerChunk)
            return false;
        markers.reserve(n);
        for (uint8_t i = 0; i < n; i++) {
            MarkerRec m;
            if (!m.read(r)) {
                markers.clear();
                return false;
            }
            markers.push_back(m);
        }
        return true;
    }
};

// K_OP_REQ (guest -> host)
struct OpReq {
    DraftHeader hdr;
    uint32_t client_op_id = 0;
    OpData op;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(client_op_id);
        op.write(w);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        client_op_id = r.u32();
        return op.read(r) && hdr.session_id != 0;
    }
};

// K_OP_APPLIED (host -> guests): concrete delta op committed at revision `rev`
struct OpApplied {
    DraftHeader hdr;
    uint32_t rev = 0;
    uint8_t actor_id = 0;
    uint32_t client_op_id = 0;
    uint8_t flags = 0;
    uint8_t actor_undo_depth = 0;
    uint8_t actor_redo_depth = 0;
    OpData op;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(rev);
        w.u8(actor_id);
        w.u32(client_op_id);
        w.u8(flags);
        w.u8(actor_undo_depth);
        w.u8(actor_redo_depth);
        op.write(w);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        rev = r.u32();
        actor_id = r.u8();
        client_op_id = r.u32();
        flags = r.u8();
        actor_undo_depth = r.u8();
        actor_redo_depth = r.u8();
        if (!op.read(r))
            return false;
        if (hdr.session_id == 0 || rev == 0)
            return false;
        if ((flags & ~(kAppliedFlagFromUndo | kAppliedFlagFromRedo)) != 0)
            return false;
        // Applied ops are always concrete deltas (never raw OP_UNDO/OP_REDO)
        if (op.kind == OP_UNDO || op.kind == OP_REDO)
            return false;
        if (op.kind == OP_CREATE_MARKER && op.uid == 0)
            return false;
        return true;
    }
};

// K_LEASE (any -> host / host -> guests)
struct LeaseMsg {
    DraftHeader hdr;
    uint8_t action = 0; // LeaseAction
    uint32_t uid = 0;
    uint8_t owner_id = 0;
    uint32_t ttl_ms = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u8(action);
        w.u32(uid);
        w.u8(owner_id);
        w.u32(ttl_ms);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        action = r.u8();
        uid = r.u32();
        owner_id = r.u8();
        ttl_ms = r.u32();
        return r.ok() && hdr.session_id != 0 && action >= LEASE_ACQUIRE && action <= LEASE_EXPIRED && uid != 0 &&
               ttl_ms <= kMaxLeaseTtlMs;
    }
};

// K_DRAG_PREVIEW (any -> host / host -> guests, unreliable 10 Hz)
struct DragPreview {
    DraftHeader hdr;
    uint8_t actor_id = 0;
    uint32_t uid = 0;
    float pos[3] = {};
    float rot[4] = {0.f, 0.f, 0.f, 1.f};
    float scale = 1.f;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u8(actor_id);
        w.u32(uid);
        for (float f : pos)
            w.f32(f);
        for (float f : rot)
            w.f32(f);
        w.f32(scale);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        actor_id = r.u8();
        uid = r.u32();
        for (float& f : pos)
            f = r.f32();
        for (float& f : rot)
            f = r.f32();
        scale = r.f32();
        return r.ok() && hdr.session_id != 0 && uid != 0 && valid_pos(pos) && valid_rot(rot) && valid_scale(scale);
    }
};

// K_OP_REJECT (host -> guest)
struct OpReject {
    DraftHeader hdr;
    uint32_t client_op_id = 0;
    uint8_t reason = REJECT_NONE; // RejectReason
    uint8_t holder_id = 0;
    uint32_t uid = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(client_op_id);
        w.u8(reason);
        w.u8(holder_id);
        w.u32(uid);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        client_op_id = r.u32();
        reason = r.u8();
        holder_id = r.u8();
        uid = r.u32();
        return r.ok() && hdr.session_id != 0 && reason >= REJECT_LEASE_HELD && reason <= REJECT_NOTHING_TO_REDO;
    }
};

} // namespace wire::adv_editor
