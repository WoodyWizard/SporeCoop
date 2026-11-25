// Shared Adventure Editor runtime module (WP-AE of 0.8.0, BUG-0863).
// All addresses and layouts verified on GOG 2024 SporeApp.exe (dc04aee5); see docs/re/adventure.md#adventure-shared-editor.
#include "adv_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <unordered_map>

#include "adv_editor_logic.h"
#include "call_guard.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/protocol.h"
#include "sporekit_game.hpp"

namespace adv_editor {

// ---------------- COOP_FIX switch ----------------
static bool g_shared_editor_fix = true;
COOP_FIX("adventure.shared_editor", "BUG-0863", g_shared_editor_fix,
         "adventure: shared Adventure Editor (host-authoritative placements, transforms, acts, captain, leases, per-player Undo/Redo)");

// ---------------- Verified GOG 2024 Layout Constants ----------------
constexpr uintptr_t kResCaptainOff = 0x130;
constexpr uintptr_t kResActsBeginOff = 0x23C;
constexpr uintptr_t kResActsEndOff = 0x240;
constexpr uintptr_t kActStride = 0x534;
constexpr uintptr_t kResClassesBeginOff = 0x2BF4;
constexpr uintptr_t kResClassesEndOff = 0x2BF8;
constexpr uintptr_t kClassEntryStride = 0x27E8;
constexpr uintptr_t kClassEntryBodyOff = 0x08;
constexpr uintptr_t kResMarkerPoolBeginOff = 0x2C10;
constexpr uintptr_t kResMarkerPoolEndOff = 0x2C14;
constexpr uintptr_t kMarkerSlotStride = 0x238;
constexpr uintptr_t kMarkerSlotBodyOff = 0x04;
constexpr uint32_t kMarkerSlotFreeBit = 0x80000000u;

constexpr uintptr_t kMarkerClassOff = 0x00;
constexpr uintptr_t kMarkerPosOff = 0x04;
constexpr uintptr_t kMarkerRotOff = 0x10;
constexpr uintptr_t kMarkerScaleOff = 0x20;
constexpr uintptr_t kMarkerSlotIdxOff = 0x228;

constexpr double kHelloPeriodSec = 1.0;
constexpr double kLeaseRenewPeriodSec = 2.0;
constexpr double kTransformFlushIdleSec = 0.18;
constexpr double kNoticeThrottleSec = 1.5;

// ---------------- Trampolines & Function Addresses ----------------
typedef void*(SK_THISCALL* CreateMarker_t)(void* data, int class_id, const float* pos, const float* rot, float scale);
typedef void*(SK_THISCALL* CloneMarker_t)(void* data, void* src_marker);
typedef void(SK_THISCALL* DeleteMarker_t)(void* data, void* marker);
typedef void(SK_THISCALL* SyncMarkerTransform_t)(void* data, void* marker, bool snap);
typedef int(SK_THISCALL* AddAct_t)(void* data, int insert_index);
typedef void(SK_THISCALL* DeleteAct_t)(void* data, int act_index);
typedef void(SK_THISCALL* SetCaptainAsset_t)(void* data, const uint32_t* key);
typedef void(SK_THISCALL* CommitHistory_t)(void* data);
typedef void(SK_THISCALL* EditHistoryOp_t)(void* hist);
typedef bool(SK_THISCALL* EditHistoryCan_t)(void* hist);
typedef void(SK_THISCALL* Save_t)(void* data, bool save_as);

static CreateMarker_t o_CreateMarker;
static CloneMarker_t o_CloneMarker;
static DeleteMarker_t o_DeleteMarker;
static SyncMarkerTransform_t o_SyncMarkerTransform;
static AddAct_t o_AddAct;
static DeleteAct_t o_DeleteAct;
static SetCaptainAsset_t o_SetCaptainAsset;
static CommitHistory_t o_CommitHistory;
static EditHistoryOp_t o_Undo;
static EditHistoryOp_t o_Redo;
static EditHistoryCan_t o_CanUndo;
static EditHistoryCan_t o_CanRedo;
static Save_t o_Save;

static uintptr_t a_ScenarioModePtr;
static uintptr_t a_SwitchToEditMode;
static uintptr_t a_ScenarioRequestReset;
static uintptr_t a_StateManagerGet;
static uintptr_t a_GetEditAct;
static uintptr_t a_SetEditAct;
static uintptr_t a_GetNumActs;
static uintptr_t a_GetClass;
static uintptr_t a_CreateClass;
static uintptr_t a_CanSave;
static uintptr_t a_EditHistorySingleton;

// Echo & re-entrancy guards
static bool g_applying = false;
CALL_GUARD("adv_editor.applying", g_applying);
static int g_in_create_depth = 0;
CALL_GUARD_DEPTH("adv_editor.create", g_in_create_depth);

// ---------------- Module State ----------------
static const SkHost* H;
static SkMod* M;
static Ctx C;

static double g_now = 0.0;
static uint32_t g_session_nonce = 0;
static uint32_t g_client_op_seq = 0;
static void* g_last_data = nullptr;
static double g_last_hello_sent = -1e9;
static double g_last_notice_at = -1e9;

static HostEditor g_host;
static GuestEditor g_guest;

// Local native binding between authoritative marker uid and native pool placementIndex (+0x228)
static std::unordered_map<uint32_t, uint32_t> g_uid_to_slot;
static std::unordered_map<uint32_t, uint32_t> g_slot_to_uid;

// Pending local UI creates on guest (client_op_id -> local native placementIndex) awaiting host K_OP_APPLIED/K_OP_REJECT
static std::unordered_map<uint32_t, uint32_t> g_pending_local_creates;

// Local transform drag tracking (batched into one gesture on CommitHistory or idle)
struct PendingTransform {
    uint32_t uid = 0;
    float pos[3] = {};
    float rot[4] = {0.f, 0.f, 0.f, 1.f};
    float scale = 1.f;
    double last_touch = 0.0;
    double last_preview_sent = -1e9;
    double last_lease_renew = -1e9;
};
static std::unordered_map<uint32_t, PendingTransform> g_dirty_xforms;

// Deferred outside-frame native work queue
enum class NativeJobKind : uint8_t {
    ReconcileSnapshot = 1,
    ApplyOp = 2,
    ApplyPreview = 3,
    DeleteSlot = 4,
};
struct NativeJob {
    NativeJobKind kind = NativeJobKind::ApplyOp;
    wire::adv_editor::OpApplied applied;
    wire::adv_editor::DragPreview preview;
    uint32_t slot_idx = 0;
};
static std::deque<NativeJob> g_native_jobs;
static bool g_native_drain_queued = false;

// Diagnostics counters
static uint32_t g_ops_tx = 0, g_ops_rx = 0, g_snaps_tx = 0, g_snaps_rx = 0;
static uint32_t g_previews_tx = 0, g_previews_rx = 0, g_rejects_rx = 0, g_leases_tx = 0;

// ---------------- Helpers ----------------
static bool connected() { return C.sess && C.sess->connected(); }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client; }
static uint8_t my_id() { return C.sess ? C.sess->my_id() : 0; }

static bool peer_reads_editor(uint8_t id)
{
    if (!C.sess)
        return false;
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess->peer_families(id, has, f) && peer_supports(f, has);
}

static bool host_reads_editor()
{
    if (!C.sess)
        return false;
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess->host_families(has, f) && peer_supports(f, has);
}

static void* scenario_mode()
{
    if (!a_ScenarioModePtr || !skg::readable((void*)a_ScenarioModePtr, 4))
        return nullptr;
    return *(void**)a_ScenarioModePtr;
}

static void* scenario_data() { return modes::adventure_data(); }

static void* scenario_resource(void* data)
{
    if (!data || !skg::readable(data, 0x14))
        return nullptr;
    void* res = skg::field<void*>(data, 0x10);
    return (res && skg::readable(res, 0x2C2C)) ? res : nullptr;
}

static bool in_edit_mode()
{
    if (game::mode_id() != modes::kScenario || modes::adventure_sub_mode() != 1)
        return false;
    void* data = scenario_data();
    return scenario_resource(data) != nullptr;
}

static wire::Key3 current_draft_key()
{
    wire::Key3 k;
    modes::adventure_mission_key(k.v);
    return k;
}

static uint32_t next_nonzero(uint32_t& c)
{
    uint32_t t = net::now_ms();
    c = (c + 1u) ^ (t * 2654435761u);
    if (c == 0)
        c = 1u;
    return c;
}

static void throttled_feed(const std::string& msg)
{
    if (!C.feed || g_now - g_last_notice_at < kNoticeThrottleSec)
        return;
    g_last_notice_at = g_now;
    C.feed(msg);
}

static std::string player_label(uint8_t id)
{
    if (C.sess) {
        for (const auto& p : C.sess->players())
            if (p.id == id && !p.name.empty())
                return p.name;
    }
    char buf[24];
    snprintf(buf, sizeof buf, "Player %u", id);
    return buf;
}

static const char* reject_name(uint8_t r)
{
    using namespace wire::adv_editor;
    switch (r) {
    case REJECT_LEASE_HELD: return "locked by another player";
    case REJECT_NOT_FOUND: return "marker no longer exists";
    case REJECT_LIMIT: return "limit reached";
    case REJECT_INVALID: return "invalid edit";
    case REJECT_NOTHING_TO_UNDO: return "nothing to undo";
    case REJECT_NOTHING_TO_REDO: return "nothing to redo";
    default: return "rejected";
    }
}

// ---------------- Native Inspection & Mutation Helpers ----------------
static int native_num_acts(void* data)
{
    if (!data)
        return 1;
    if (a_GetNumActs)
        return std::clamp(skg::thiscall<int>(a_GetNumActs, data), 1, (int)wire::adv_editor::kMaxActs);
    void* res = scenario_resource(data);
    if (!res)
        return 1;
    uint8_t* b = skg::field<uint8_t*>(res, kResActsBeginOff);
    uint8_t* e = skg::field<uint8_t*>(res, kResActsEndOff);
    if (!b || !e || e < b)
        return 1;
    return std::clamp((int)((e - b) / kActStride), 1, (int)wire::adv_editor::kMaxActs);
}

static int native_current_act()
{
    if (!a_GetEditAct)
        return 0;
    return std::max(0, skg::cdecl_call<int>(a_GetEditAct));
}

static wire::Key3 native_captain_key(void* data)
{
    wire::Key3 k;
    void* res = scenario_resource(data);
    if (!res)
        return k;
    k.v[0] = skg::field<uint32_t>(res, kResCaptainOff + 0);
    k.v[1] = skg::field<uint32_t>(res, kResCaptainOff + 4);
    k.v[2] = skg::field<uint32_t>(res, kResCaptainOff + 8);
    return k;
}

static wire::Key3 native_class_key(void* data, int32_t class_id)
{
    wire::Key3 k;
    if (!data || class_id < 0 || !a_GetClass)
        return k;
    void* cls = skg::thiscall<void*>(a_GetClass, data, (int)class_id);
    if (!cls || !skg::readable(cls, 12))
        return k;
    k.v[0] = skg::field<uint32_t>(cls, 0);
    k.v[1] = skg::field<uint32_t>(cls, 4);
    k.v[2] = skg::field<uint32_t>(cls, 8);
    return k;
}

static int32_t native_first_class_id(void* data)
{
    void* res = scenario_resource(data);
    if (!res)
        return -1;
    uint8_t* b = skg::field<uint8_t*>(res, kResClassesBeginOff);
    uint8_t* e = skg::field<uint8_t*>(res, kResClassesEndOff);
    if (!b || !e || e < b + kClassEntryStride || !skg::readable(b, 4))
        return -1;
    return *(int32_t*)b;
}

static int32_t resolve_or_create_class(void* data, int32_t preferred_id, const wire::Key3& key)
{
    if (!data)
        return -1;
    bool has_key = (key.v[0] != 0 || key.v[1] != 0 || key.v[2] != 0);
    if (preferred_id >= 0 && a_GetClass) {
        void* cls = skg::thiscall<void*>(a_GetClass, data, (int)preferred_id);
        if (cls && skg::readable(cls, 12)) {
            wire::Key3 existing = native_class_key(data, preferred_id);
            if (!has_key || existing == key)
                return preferred_id;
        }
    }
    if (has_key) {
        if (void* res = scenario_resource(data)) {
            uint8_t* b = skg::field<uint8_t*>(res, kResClassesBeginOff);
            uint8_t* e = skg::field<uint8_t*>(res, kResClassesEndOff);
            if (b && e && e > b) {
                size_t count = std::min<size_t>((size_t)(e - b) / kClassEntryStride, wire::adv_editor::kMaxMarkers);
                for (size_t i = 0; i < count; i++) {
                    uint8_t* ent = b + i * kClassEntryStride;
                    if (!skg::readable(ent, kClassEntryBodyOff + 12))
                        break;
                    wire::Key3 ek;
                    ek.v[0] = *(uint32_t*)(ent + kClassEntryBodyOff + 0);
                    ek.v[1] = *(uint32_t*)(ent + kClassEntryBodyOff + 4);
                    ek.v[2] = *(uint32_t*)(ent + kClassEntryBodyOff + 8);
                    if (ek == key)
                        return *(int32_t*)ent;
                }
            }
        }
        if (a_CreateClass) {
            int created = skg::thiscall<int>(a_CreateClass, data, key.v);
            if (created >= 0)
                return created;
        }
    }
    if (preferred_id >= 0 && a_GetClass && skg::thiscall<void*>(a_GetClass, data, (int)preferred_id))
        return preferred_id;
    return native_first_class_id(data);
}

struct NativeMarkerInfo {
    void* marker = nullptr;
    uint32_t slot_idx = 0;
    int32_t class_id = -1;
    wire::Key3 class_key;
    float pos[3] = {};
    float rot[4] = {0.f, 0.f, 0.f, 1.f};
    float scale = 1.f;
};

static bool read_marker_info(void* data, void* marker, NativeMarkerInfo& out)
{
    if (!marker || !skg::readable(marker, kMarkerSlotIdxOff + 4))
        return false;
    int32_t cid = skg::field<int32_t>(marker, kMarkerClassOff);
    if (cid < 0)
        return false; // skip avatar (-2) or invalid
    out.marker = marker;
    out.class_id = cid;
    out.slot_idx = skg::field<uint32_t>(marker, kMarkerSlotIdxOff);
    out.class_key = native_class_key(data, cid);
    for (int i = 0; i < 3; i++)
        out.pos[i] = skg::field<float>(marker, kMarkerPosOff + i * 4);
    for (int i = 0; i < 4; i++)
        out.rot[i] = skg::field<float>(marker, kMarkerRotOff + i * 4);
    out.scale = skg::field<float>(marker, kMarkerScaleOff);
    if (!wire::adv_editor::valid_rot(out.rot)) {
        out.rot[0] = out.rot[1] = out.rot[2] = 0.f;
        out.rot[3] = 1.f;
    }
    if (!wire::adv_editor::valid_scale(out.scale) || out.scale == 0.f)
        out.scale = 1.f;
    return wire::adv_editor::valid_pos(out.pos);
}

static std::vector<NativeMarkerInfo> scan_native_markers(void* data)
{
    std::vector<NativeMarkerInfo> out;
    void* res = scenario_resource(data);
    if (!res)
        return out;
    uint8_t* b = skg::field<uint8_t*>(res, kResMarkerPoolBeginOff);
    uint8_t* e = skg::field<uint8_t*>(res, kResMarkerPoolEndOff);
    if (!b || !e || e <= b)
        return out;
    size_t nslots = std::min<size_t>((size_t)(e - b) / kMarkerSlotStride, wire::adv_editor::kMaxMarkers);
    for (size_t i = 0; i < nslots; i++) {
        uint8_t* slot = b + i * kMarkerSlotStride;
        if (!skg::readable(slot, kMarkerSlotStride))
            break;
        uint32_t hdr = *(uint32_t*)slot;
        if ((hdr & kMarkerSlotFreeBit) != 0)
            continue;
        NativeMarkerInfo info;
        if (read_marker_info(data, slot + kMarkerSlotBodyOff, info))
            out.push_back(info);
    }
    return out;
}

static void* find_native_marker_by_slot(void* data, uint32_t slot_idx)
{
    void* res = scenario_resource(data);
    if (!res || slot_idx >= wire::adv_editor::kMaxMarkers)
        return nullptr;
    uint8_t* b = skg::field<uint8_t*>(res, kResMarkerPoolBeginOff);
    uint8_t* e = skg::field<uint8_t*>(res, kResMarkerPoolEndOff);
    if (!b || !e || e <= b)
        return nullptr;
    uint8_t* slot = b + (size_t)slot_idx * kMarkerSlotStride;
    if (slot < b || slot + kMarkerSlotStride > e || !skg::readable(slot, kMarkerSlotStride))
        return nullptr;
    if ((*(uint32_t*)slot & kMarkerSlotFreeBit) != 0)
        return nullptr;
    return slot + kMarkerSlotBodyOff;
}

static void bind_uid_slot(uint32_t uid, uint32_t slot_idx)
{
    if (auto it = g_uid_to_slot.find(uid); it != g_uid_to_slot.end())
        g_slot_to_uid.erase(it->second);
    if (auto it = g_slot_to_uid.find(slot_idx); it != g_slot_to_uid.end())
        g_uid_to_slot.erase(it->second);
    g_uid_to_slot[uid] = slot_idx;
    g_slot_to_uid[slot_idx] = uid;
}

static void unbind_uid(uint32_t uid)
{
    if (auto it = g_uid_to_slot.find(uid); it != g_uid_to_slot.end()) {
        g_slot_to_uid.erase(it->second);
        g_uid_to_slot.erase(it);
    }
}

static void write_marker_transform_native(void* data, void* marker, const float pos[3], const float rot[4], float scale,
                                          bool snap)
{
    if (!data || !marker || !skg::readable(marker, kMarkerScaleOff + 4) || !o_SyncMarkerTransform)
        return;
    for (int i = 0; i < 3; i++)
        skg::field<float>(marker, kMarkerPosOff + i * 4) = pos[i];
    for (int i = 0; i < 4; i++)
        skg::field<float>(marker, kMarkerRotOff + i * 4) = rot[i];
    if (scale > 0.f)
        skg::field<float>(marker, kMarkerScaleOff) = scale;
    o_SyncMarkerTransform(data, marker, snap);
}

// ---------------- Wire Senders ----------------
static void send_to_peer(uint8_t peer, wire::adv_editor::Kind kind, const auto& payload, bool reliable = true)
{
    if (!C.sess)
        return;
    net::Writer w;
    w.u8((uint8_t)kind);
    payload.write(w);
    if (reliable)
        C.sess->send_reliable_to(peer, net::MSG_ADV_EDITOR, w);
    else
        C.sess->send_unreliable_to(peer, net::MSG_ADV_EDITOR, w);
}

static void broadcast_editor(wire::adv_editor::Kind kind, const auto& payload, bool reliable = true,
                             uint8_t except = 0xFF)
{
    if (!is_host() || !C.sess)
        return;
    for (const auto& p : C.sess->players()) {
        if (p.id == 0 || p.id == except || !peer_reads_editor(p.id))
            continue;
        send_to_peer(p.id, kind, payload, reliable);
    }
}

static void send_snapshot_to_peer(uint8_t peer)
{
    auto chunks = g_host.build_snapshot_chunks();
    for (const auto& c : chunks)
        send_to_peer(peer, wire::adv_editor::K_SNAPSHOT, c, true);
    if (!chunks.empty())
        g_snaps_tx++;
    // Also send active leases so the joining peer sees current locks
    for (const auto& [uid, le] : g_host.leases.leases) {
        if (le.expires_at <= g_now)
            continue;
        wire::adv_editor::LeaseMsg lm;
        lm.hdr = {g_host.state.draft_key, g_host.state.session_id};
        lm.action = wire::adv_editor::LEASE_GRANT;
        lm.uid = uid;
        lm.owner_id = le.owner;
        lm.ttl_ms = (uint32_t)std::clamp((le.expires_at - g_now) * 1000.0, 250.0,
                                         (double)wire::adv_editor::kMaxLeaseTtlMs);
        send_to_peer(peer, wire::adv_editor::K_LEASE, lm, true);
    }
}

// ---------------- Outside-Frame Native Apply Queue ----------------
static void apply_op_native_now(void* data, const wire::adv_editor::OpApplied& a)
{
    using namespace wire::adv_editor;
    const OpData& op = a.op;
    switch (op.kind) {
    case OP_CREATE_MARKER: {
        // Check if this is our own guest UI create returning from host
        if (is_guest() && a.actor_id == my_id() && a.client_op_id != 0) {
            auto pit = g_pending_local_creates.find(a.client_op_id);
            if (pit != g_pending_local_creates.end()) {
                uint32_t local_slot = pit->second;
                g_pending_local_creates.erase(pit);
                if (void* m = find_native_marker_by_slot(data, local_slot)) {
                    bind_uid_slot(op.uid, local_slot);
                    write_marker_transform_native(data, m, op.pos, op.rot, op.scale, true);
                    return;
                }
            }
        }
        // Already bound? Just update transform
        if (auto it = g_uid_to_slot.find(op.uid); it != g_uid_to_slot.end()) {
            if (void* m = find_native_marker_by_slot(data, it->second)) {
                write_marker_transform_native(data, m, op.pos, op.rot, op.scale, true);
                return;
            }
            unbind_uid(op.uid);
        }
        if (!o_CreateMarker)
            return;
        int32_t cid = resolve_or_create_class(data, op.class_id, op.key);
        if (cid < 0)
            return;
        void* m = o_CreateMarker(data, (int)cid, op.pos, op.rot, op.scale);
        if (m && skg::readable(m, kMarkerSlotIdxOff + 4)) {
            uint32_t slot = skg::field<uint32_t>(m, kMarkerSlotIdxOff);
            bind_uid_slot(op.uid, slot);
            write_marker_transform_native(data, m, op.pos, op.rot, op.scale, true);
        }
        break;
    }
    case OP_DELETE_MARKER: {
        auto it = g_uid_to_slot.find(op.uid);
        if (it == g_uid_to_slot.end())
            return;
        void* m = find_native_marker_by_slot(data, it->second);
        unbind_uid(op.uid);
        if (m && o_DeleteMarker)
            o_DeleteMarker(data, m);
        break;
    }
    case OP_TRANSFORM_MARKER: {
        auto it = g_uid_to_slot.find(op.uid);
        if (it == g_uid_to_slot.end())
            return;
        if (void* m = find_native_marker_by_slot(data, it->second))
            write_marker_transform_native(data, m, op.pos, op.rot, op.scale, true);
        break;
    }
    case OP_ADD_ACT: {
        if (!o_AddAct)
            return;
        int cur = native_num_acts(data);
        if (cur < (int)kMaxActs) {
            int idx = std::clamp((int)op.act, 1, cur);
            o_AddAct(data, idx);
        }
        break;
    }
    case OP_DELETE_ACT: {
        if (!o_DeleteAct)
            return;
        int cur = native_num_acts(data);
        if (cur > 1 && op.act < cur) {
            if (a_SetEditAct && native_current_act() >= cur - 1)
                skg::cdecl_call<void>(a_SetEditAct, cur - 2);
            o_DeleteAct(data, (int)op.act);
        }
        break;
    }
    case OP_SET_CAPTAIN: {
        if (!o_SetCaptainAsset)
            return;
        if (op.key.v[0] || op.key.v[1] || op.key.v[2])
            o_SetCaptainAsset(data, op.key.v);
        break;
    }
    default:
        break;
    }
}

static void reconcile_snapshot_native_now(void* data)
{
    if (!g_guest.synced)
        return;
    const DraftState& st = g_guest.state;

    // 1. Reconcile act count
    int have_acts = native_num_acts(data);
    while (have_acts < (int)st.num_acts && o_AddAct) {
        if (o_AddAct(data, have_acts) < 0)
            break;
        have_acts++;
    }
    while (have_acts > (int)st.num_acts && have_acts > 1 && o_DeleteAct) {
        if (a_SetEditAct && native_current_act() >= have_acts - 1)
            skg::cdecl_call<void>(a_SetEditAct, have_acts - 2);
        o_DeleteAct(data, have_acts - 1);
        have_acts--;
    }

    // 2. Reconcile captain asset
    if (o_SetCaptainAsset && (st.captain_key.v[0] || st.captain_key.v[1] || st.captain_key.v[2])) {
        if (native_captain_key(data) != st.captain_key)
            o_SetCaptainAsset(data, st.captain_key.v);
    }

    // 3. Reconcile markers: keep matching existing markers, create missing ones, delete extras
    auto local = scan_native_markers(data);
    std::unordered_map<uint32_t, NativeMarkerInfo> local_by_slot;
    for (const auto& info : local)
        local_by_slot[info.slot_idx] = info;

    std::unordered_map<uint32_t, uint32_t> new_uid_to_slot;
    std::unordered_map<uint32_t, uint32_t> new_slot_to_uid;

    // Pass A: keep already-bound slots whose class still matches
    for (const auto& [uid, m] : st.markers) {
        auto it = g_uid_to_slot.find(uid);
        if (it == g_uid_to_slot.end())
            continue;
        auto lit = local_by_slot.find(it->second);
        if (lit == local_by_slot.end() || new_slot_to_uid.count(it->second))
            continue;
        bool same_cls = (lit->second.class_id == m.class_id) ||
                        ((m.class_key.v[0] || m.class_key.v[1] || m.class_key.v[2]) &&
                         lit->second.class_key == m.class_key);
        if (!same_cls)
            continue;
        new_uid_to_slot[uid] = it->second;
        new_slot_to_uid[it->second] = uid;
        write_marker_transform_native(data, lit->second.marker, m.pos, m.rot, m.scale, true);
    }

    // Pass B: match remaining snapshot markers to unbound local markers by class + near position (< 0.15 m)
    for (const auto& [uid, m] : st.markers) {
        if (new_uid_to_slot.count(uid))
            continue;
        uint32_t best_slot = 0xFFFFFFFFu;
        float best_d2 = 0.15f * 0.15f;
        for (const auto& [slot, info] : local_by_slot) {
            if (new_slot_to_uid.count(slot))
                continue;
            bool same_cls = (info.class_id == m.class_id) ||
                            ((m.class_key.v[0] || m.class_key.v[1] || m.class_key.v[2]) &&
                             info.class_key == m.class_key);
            if (!same_cls)
                continue;
            float dx = info.pos[0] - m.pos[0], dy = info.pos[1] - m.pos[1], dz = info.pos[2] - m.pos[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_slot = slot;
            }
        }
        if (best_slot != 0xFFFFFFFFu) {
            new_uid_to_slot[uid] = best_slot;
            new_slot_to_uid[best_slot] = uid;
            write_marker_transform_native(data, local_by_slot[best_slot].marker, m.pos, m.rot, m.scale, true);
        }
    }

    // Pass C: delete unbound local markers not in host snapshot
    for (const auto& [slot, info] : local_by_slot) {
        if (new_slot_to_uid.count(slot))
            continue;
        if (o_DeleteMarker && info.marker)
            o_DeleteMarker(data, info.marker);
    }

    // Pass D: create missing markers from host snapshot
    for (const auto& [uid, m] : st.markers) {
        if (new_uid_to_slot.count(uid) || !o_CreateMarker)
            continue;
        int32_t cid = resolve_or_create_class(data, m.class_id, m.class_key);
        if (cid < 0)
            continue;
        void* created = o_CreateMarker(data, (int)cid, m.pos, m.rot, m.scale);
        if (created && skg::readable(created, kMarkerSlotIdxOff + 4)) {
            uint32_t slot = skg::field<uint32_t>(created, kMarkerSlotIdxOff);
            new_uid_to_slot[uid] = slot;
            new_slot_to_uid[slot] = uid;
            write_marker_transform_native(data, created, m.pos, m.rot, m.scale, true);
        }
    }

    g_uid_to_slot = std::move(new_uid_to_slot);
    g_slot_to_uid = std::move(new_slot_to_uid);
}

static void drain_native_jobs_task(void*)
{
    g_native_drain_queued = false;
    if (!in_edit_mode()) {
        g_native_jobs.clear();
        return;
    }
    void* data = scenario_data();
    if (!data)
        return;
    g_applying = true;
    while (!g_native_jobs.empty()) {
        NativeJob job = std::move(g_native_jobs.front());
        g_native_jobs.pop_front();
        switch (job.kind) {
        case NativeJobKind::ReconcileSnapshot:
            reconcile_snapshot_native_now(data);
            break;
        case NativeJobKind::ApplyOp:
            apply_op_native_now(data, job.applied);
            break;
        case NativeJobKind::ApplyPreview: {
            auto it = g_uid_to_slot.find(job.preview.uid);
            if (it != g_uid_to_slot.end()) {
                if (void* m = find_native_marker_by_slot(data, it->second))
                    write_marker_transform_native(data, m, job.preview.pos, job.preview.rot, job.preview.scale, false);
            }
            break;
        }
        case NativeJobKind::DeleteSlot: {
            if (void* m = find_native_marker_by_slot(data, job.slot_idx))
                if (o_DeleteMarker)
                    o_DeleteMarker(data, m);
            break;
        }
        }
    }
    g_applying = false;
}

static void queue_native_job(NativeJob job)
{
    g_native_jobs.push_back(std::move(job));
    if (!g_native_drain_queued && H) {
        g_native_drain_queued = true;
        H->run_outside_frame(drain_native_jobs_task, nullptr);
    }
}

// ---------------- Session Lifecycle ----------------
static void reset_state(const char* why)
{
    if (g_host.active() || g_guest.synced || !g_uid_to_slot.empty())
        H->log(M, SK_INFO, "adv_editor: reset (%s)", why);
    g_host.reset();
    g_guest.reset();
    g_uid_to_slot.clear();
    g_slot_to_uid.clear();
    g_pending_local_creates.clear();
    g_dirty_xforms.clear();
    g_native_jobs.clear();
    g_last_data = nullptr;
}

static void host_ensure_session(void* data)
{
    wire::Key3 dk = current_draft_key();
    if (g_host.active() && g_last_data == data && g_host.state.draft_key == dk)
        return;
    g_uid_to_slot.clear();
    g_slot_to_uid.clear();
    g_dirty_xforms.clear();

    auto native_markers = scan_native_markers(data);
    std::vector<wire::adv_editor::MarkerRec> recs;
    recs.reserve(native_markers.size());
    uint32_t next_id = 1;
    for (const auto& info : native_markers) {
        wire::adv_editor::MarkerRec r;
        r.uid = next_id++;
        r.class_id = info.class_id;
        r.class_key = info.class_key;
        r.act = (uint8_t)std::clamp(native_current_act(), 0, (int)wire::adv_editor::kMaxActs - 1);
        for (int i = 0; i < 3; i++)
            r.pos[i] = info.pos[i];
        for (int i = 0; i < 4; i++)
            r.rot[i] = info.rot[i];
        r.scale = info.scale;
        recs.push_back(r);
        bind_uid_slot(r.uid, info.slot_idx);
    }
    uint32_t sid = next_nonzero(g_session_nonce);
    g_host.start_session(dk, sid, (uint8_t)native_num_acts(data), (uint8_t)native_current_act(),
                         native_captain_key(data), recs);
    g_last_data = data;
    H->log(M, SK_INFO, "adv_editor: host session %08X started (draft=%08X markers=%zu acts=%u)",
           sid, dk.v[0], recs.size(), g_host.state.num_acts);
}

// ---------------- Local Op Submission (Host direct / Guest K_OP_REQ) ----------------
static bool host_commit_and_broadcast(uint8_t actor, uint32_t client_op_id, const wire::adv_editor::OpData& req,
                                      bool apply_native_on_host, wire::adv_editor::OpApplied* out_applied = nullptr,
                                      wire::adv_editor::OpReject* out_reject = nullptr)
{
    wire::adv_editor::OpApplied applied;
    wire::adv_editor::OpReject reject;
    if (!g_host.apply_op(actor, client_op_id, req, g_now, applied, reject)) {
        if (out_reject)
            *out_reject = reject;
        if (actor != 0)
            send_to_peer(actor, wire::adv_editor::K_OP_REJECT, reject, true);
        else
            throttled_feed(std::string("Adventure Editor: ") + reject_name(reject.reason));
        return false;
    }
    if (apply_native_on_host) {
        NativeJob job;
        job.kind = NativeJobKind::ApplyOp;
        job.applied = applied;
        queue_native_job(std::move(job));
    }
    broadcast_editor(wire::adv_editor::K_OP_APPLIED, applied, true);
    g_ops_tx++;
    if (out_applied)
        *out_applied = applied;
    return true;
}

static uint32_t guest_send_op_req(const wire::adv_editor::OpData& op)
{
    if (!g_guest.synced)
        return 0;
    uint32_t cid = ++g_client_op_seq;
    if (cid == 0)
        cid = ++g_client_op_seq;
    wire::adv_editor::OpReq req;
    req.hdr = {g_guest.state.draft_key, g_guest.state.session_id};
    req.client_op_id = cid;
    req.op = op;
    send_to_peer(0, wire::adv_editor::K_OP_REQ, req, true);
    g_ops_tx++;
    return cid;
}

static void request_lease_local(uint32_t uid, uint8_t action)
{
    using namespace wire::adv_editor;
    if (uid == 0 || !connected())
        return;
    if (is_host() && g_host.active()) {
        uint8_t holder = kNoOwner;
        if (action == LEASE_RELEASE) {
            if (g_host.leases.release(uid, 0)) {
                LeaseMsg lm{{g_host.state.draft_key, g_host.state.session_id}, LEASE_RELEASE, uid, 0, 0};
                broadcast_editor(K_LEASE, lm, true);
                g_leases_tx++;
            }
        } else {
            if (g_host.leases.acquire_or_renew(uid, 0, g_now, kLeaseTtlSec, &holder)) {
                LeaseMsg lm{{g_host.state.draft_key, g_host.state.session_id}, LEASE_GRANT, uid, 0,
                            (uint32_t)(kLeaseTtlSec * 1000.0)};
                broadcast_editor(K_LEASE, lm, true);
                g_leases_tx++;
            }
        }
    } else if (is_guest() && g_guest.synced) {
        LeaseMsg lm{{g_guest.state.draft_key, g_guest.state.session_id}, action, uid, my_id(),
                    action == LEASE_RELEASE ? 0u : (uint32_t)(kLeaseTtlSec * 1000.0)};
        send_to_peer(0, K_LEASE, lm, true);
        g_leases_tx++;
    }
}

static void flush_dirty_transforms(bool force_all)
{
    for (auto it = g_dirty_xforms.begin(); it != g_dirty_xforms.end();) {
        const PendingTransform& pt = it->second;
        if (!force_all && (g_now - pt.last_touch < kTransformFlushIdleSec)) {
            ++it;
            continue;
        }
        wire::adv_editor::OpData op;
        op.kind = wire::adv_editor::OP_TRANSFORM_MARKER;
        op.uid = pt.uid;
        for (int i = 0; i < 3; i++)
            op.pos[i] = pt.pos[i];
        for (int i = 0; i < 4; i++)
            op.rot[i] = pt.rot[i];
        op.scale = pt.scale;
        if (is_host() && g_host.active())
            host_commit_and_broadcast(0, 0, op, false);
        else if (is_guest() && g_guest.synced)
            guest_send_op_req(op);
        it = g_dirty_xforms.erase(it);
    }
}

// ---------------- Native Game Hooks ----------------
static bool hooks_active()
{
    return !g_applying && g_shared_editor_fix && connected() && in_edit_mode() &&
           (is_host() ? g_host.active() : g_guest.synced);
}

static void* SK_THISCALL h_CreateMarker(void* data, int class_id, const float* pos, const float* rot, float scale)
{
    if (!hooks_active())
        return o_CreateMarker(data, class_id, pos, rot, scale);

    g_in_create_depth++;
    void* created = o_CreateMarker(data, class_id, pos, rot, scale);
    g_in_create_depth--;
    if (!created)
        return nullptr;

    NativeMarkerInfo info;
    if (!read_marker_info(data, created, info))
        return created;

    wire::adv_editor::OpData op;
    op.kind = wire::adv_editor::OP_CREATE_MARKER;
    op.uid = 0;
    op.class_id = info.class_id;
    op.key = info.class_key;
    op.act = (uint8_t)std::clamp(native_current_act(), 0, (int)wire::adv_editor::kMaxActs - 1);
    for (int i = 0; i < 3; i++)
        op.pos[i] = info.pos[i];
    for (int i = 0; i < 4; i++)
        op.rot[i] = info.rot[i];
    op.scale = info.scale;

    if (is_host()) {
        wire::adv_editor::OpApplied applied;
        if (host_commit_and_broadcast(0, 0, op, false, &applied))
            bind_uid_slot(applied.op.uid, info.slot_idx);
    } else if (is_guest()) {
        uint32_t cid = guest_send_op_req(op);
        if (cid != 0)
            g_pending_local_creates[cid] = info.slot_idx;
    }
    return created;
}

static void* SK_THISCALL h_CloneMarker(void* data, void* src_marker)
{
    if (!hooks_active())
        return o_CloneMarker(data, src_marker);

    g_in_create_depth++;
    void* created = o_CloneMarker(data, src_marker);
    g_in_create_depth--;
    if (!created)
        return nullptr;

    NativeMarkerInfo info;
    if (!read_marker_info(data, created, info))
        return created;

    wire::adv_editor::OpData op;
    op.kind = wire::adv_editor::OP_CREATE_MARKER;
    op.uid = 0;
    op.class_id = info.class_id;
    op.key = info.class_key;
    op.act = (uint8_t)std::clamp(native_current_act(), 0, (int)wire::adv_editor::kMaxActs - 1);
    for (int i = 0; i < 3; i++)
        op.pos[i] = info.pos[i];
    for (int i = 0; i < 4; i++)
        op.rot[i] = info.rot[i];
    op.scale = info.scale;

    if (is_host()) {
        wire::adv_editor::OpApplied applied;
        if (host_commit_and_broadcast(0, 0, op, false, &applied))
            bind_uid_slot(applied.op.uid, info.slot_idx);
    } else if (is_guest()) {
        uint32_t cid = guest_send_op_req(op);
        if (cid != 0)
            g_pending_local_creates[cid] = info.slot_idx;
    }
    return created;
}

static void SK_THISCALL h_DeleteMarker(void* data, void* marker)
{
    if (!hooks_active()) {
        o_DeleteMarker(data, marker);
        return;
    }
    NativeMarkerInfo info;
    if (!read_marker_info(data, marker, info)) {
        o_DeleteMarker(data, marker);
        return;
    }
    auto it = g_slot_to_uid.find(info.slot_idx);
    if (it == g_slot_to_uid.end()) {
        o_DeleteMarker(data, marker);
        return;
    }
    uint32_t uid = it->second;
    const LeaseTable& lt = is_host() ? g_host.leases : g_guest.leases;
    if (!lt.can_edit(my_id(), uid, g_now)) {
        throttled_feed("Adventure Editor: marker is locked by " + player_label(lt.holder_of(uid, g_now)));
        return;
    }
    g_dirty_xforms.erase(uid);

    wire::adv_editor::OpData op;
    op.kind = wire::adv_editor::OP_DELETE_MARKER;
    op.uid = uid;
    if (is_host()) {
        if (host_commit_and_broadcast(0, 0, op, false)) {
            unbind_uid(uid);
            g_applying = true;
            o_DeleteMarker(data, marker);
            g_applying = false;
        }
    } else if (is_guest()) {
        // Wait for host K_OP_APPLIED to delete locally (prevents loss if host rejects lease race)
        guest_send_op_req(op);
    }
}

static void SK_THISCALL h_SyncMarkerTransform(void* data, void* marker, bool snap)
{
    if (!hooks_active() || g_in_create_depth > 0) {
        o_SyncMarkerTransform(data, marker, snap);
        return;
    }
    NativeMarkerInfo info;
    if (!read_marker_info(data, marker, info)) {
        o_SyncMarkerTransform(data, marker, snap);
        return;
    }
    auto it = g_slot_to_uid.find(info.slot_idx);
    if (it == g_slot_to_uid.end()) {
        o_SyncMarkerTransform(data, marker, snap);
        return;
    }
    uint32_t uid = it->second;
    const DraftState& st = is_host() ? g_host.state : g_guest.state;
    const LeaseTable& lt = is_host() ? g_host.leases : g_guest.leases;

    if (!lt.can_edit(my_id(), uid, g_now)) {
        // Conflicting drag against another player's active lease: snap back to authoritative state
        if (auto mit = st.markers.find(uid); mit != st.markers.end()) {
            g_applying = true;
            write_marker_transform_native(data, marker, mit->second.pos, mit->second.rot, mit->second.scale, true);
            g_applying = false;
        }
        throttled_feed("Adventure Editor: marker is locked by " + player_label(lt.holder_of(uid, g_now)));
        return;
    }

    o_SyncMarkerTransform(data, marker, snap);

    // Check if transform actually differs from authoritative state
    if (auto mit = st.markers.find(uid); mit != st.markers.end()) {
        const auto& m = mit->second;
        bool same = std::fabs(m.pos[0] - info.pos[0]) < 1e-4f && std::fabs(m.pos[1] - info.pos[1]) < 1e-4f &&
                    std::fabs(m.pos[2] - info.pos[2]) < 1e-4f && std::fabs(m.rot[0] - info.rot[0]) < 1e-4f &&
                    std::fabs(m.rot[1] - info.rot[1]) < 1e-4f && std::fabs(m.rot[2] - info.rot[2]) < 1e-4f &&
                    std::fabs(m.rot[3] - info.rot[3]) < 1e-4f && std::fabs(m.scale - info.scale) < 1e-4f;
        if (same && !g_dirty_xforms.count(uid))
            return;
    }

    auto& pt = g_dirty_xforms[uid];
    pt.uid = uid;
    for (int i = 0; i < 3; i++)
        pt.pos[i] = info.pos[i];
    for (int i = 0; i < 4; i++)
        pt.rot[i] = info.rot[i];
    pt.scale = info.scale;
    pt.last_touch = g_now;

    if (g_now - pt.last_lease_renew >= kLeaseRenewPeriodSec) {
        pt.last_lease_renew = g_now;
        request_lease_local(uid, wire::adv_editor::LEASE_ACQUIRE);
    }
    if (g_now - pt.last_preview_sent >= kDragPreviewPeriodSec) {
        pt.last_preview_sent = g_now;
        wire::adv_editor::DragPreview dp;
        dp.hdr = {st.draft_key, st.session_id};
        dp.actor_id = my_id();
        dp.uid = uid;
        for (int i = 0; i < 3; i++)
            dp.pos[i] = info.pos[i];
        for (int i = 0; i < 4; i++)
            dp.rot[i] = info.rot[i];
        dp.scale = info.scale;
        if (is_host())
            broadcast_editor(wire::adv_editor::K_DRAG_PREVIEW, dp, false);
        else
            send_to_peer(0, wire::adv_editor::K_DRAG_PREVIEW, dp, false);
        g_previews_tx++;
    }
}

static int SK_THISCALL h_AddAct(void* data, int insert_index)
{
    if (!hooks_active())
        return o_AddAct(data, insert_index);

    wire::adv_editor::OpData op;
    op.kind = wire::adv_editor::OP_ADD_ACT;
    op.act = (uint8_t)std::clamp(insert_index, 1, (int)wire::adv_editor::kMaxActs - 1);
    if (is_host()) {
        int res = o_AddAct(data, insert_index);
        if (res >= 0) {
            op.act = (uint8_t)res;
            host_commit_and_broadcast(0, 0, op, false);
        }
        return res;
    }
    guest_send_op_req(op);
    return -1; // applied authoritatively when K_OP_APPLIED arrives
}

static void SK_THISCALL h_DeleteAct(void* data, int act_index)
{
    if (!hooks_active()) {
        o_DeleteAct(data, act_index);
        return;
    }
    wire::adv_editor::OpData op;
    op.kind = wire::adv_editor::OP_DELETE_ACT;
    op.act = (uint8_t)std::clamp(act_index, 0, (int)wire::adv_editor::kMaxActs - 1);
    if (is_host()) {
        o_DeleteAct(data, act_index);
        host_commit_and_broadcast(0, 0, op, false);
    } else {
        guest_send_op_req(op);
    }
}

static void SK_THISCALL h_SetCaptainAsset(void* data, const uint32_t* key)
{
    if (!hooks_active() || !key) {
        o_SetCaptainAsset(data, key);
        return;
    }
    wire::adv_editor::OpData op;
    op.kind = wire::adv_editor::OP_SET_CAPTAIN;
    op.key.v[0] = key[0];
    op.key.v[1] = key[1];
    op.key.v[2] = key[2];
    if (is_host()) {
        o_SetCaptainAsset(data, key);
        host_commit_and_broadcast(0, 0, op, false);
    } else {
        guest_send_op_req(op);
    }
}

static void SK_THISCALL h_CommitHistory(void* data)
{
    o_CommitHistory(data);
    if (hooks_active() && !g_dirty_xforms.empty())
        flush_dirty_transforms(true);
}

static bool SK_THISCALL h_CanUndo(void* hist)
{
    if (!hooks_active())
        return o_CanUndo(hist);
    return is_host() ? (g_host.undo_depth(0) > 0) : (g_guest.my_undo_depth > 0);
}

static bool SK_THISCALL h_CanRedo(void* hist)
{
    if (!hooks_active())
        return o_CanRedo(hist);
    return is_host() ? (g_host.redo_depth(0) > 0) : (g_guest.my_redo_depth > 0);
}

static void SK_THISCALL h_Undo(void* hist)
{
    if (!hooks_active()) {
        o_Undo(hist);
        return;
    }
    flush_dirty_transforms(true);
    wire::adv_editor::OpData op;
    op.kind = wire::adv_editor::OP_UNDO;
    if (is_host())
        host_commit_and_broadcast(0, 0, op, true);
    else
        guest_send_op_req(op);
}

static void SK_THISCALL h_Redo(void* hist)
{
    if (!hooks_active()) {
        o_Redo(hist);
        return;
    }
    flush_dirty_transforms(true);
    wire::adv_editor::OpData op;
    op.kind = wire::adv_editor::OP_REDO;
    if (is_host())
        host_commit_and_broadcast(0, 0, op, true);
    else
        guest_send_op_req(op);
}

static void SK_THISCALL h_Save(void* data, bool save_as)
{
    o_Save(data, save_as);
    if (hooks_active()) {
        uint32_t rev = is_host() ? g_host.state.rev : g_guest.state.rev;
        H->log(M, SK_INFO, "adv_editor: local save triggered (save_as=%d rev=%u)", (int)save_as, rev);
        if (C.feed) {
            char buf[96];
            snprintf(buf, sizeof buf, "Adventure Editor: saved draft at rev %u", rev);
            C.feed(buf);
        }
    }
}

// ---------------- Network Message Dispatch ----------------
void on_message(uint8_t from, net::Reader& r)
{
    using namespace wire::adv_editor;
    if (!g_shared_editor_fix || !connected() || !in_edit_mode())
        return;
    uint8_t kind = r.u8();
    if (!r.ok())
        return;

    void* data = scenario_data();
    if (!data)
        return;
    wire::Key3 dk = current_draft_key();

    switch (kind) {
    case K_HELLO_SYNC: {
        HelloSync msg;
        if (!is_host() || from == 0 || !msg.read(r) || msg.hdr.draft_key != dk)
            return;
        host_ensure_session(data);
        if (!g_host.active())
            return;
        std::vector<OpApplied> delta;
        if (msg.hdr.session_id == g_host.state.session_id && msg.last_rev > 0 &&
            g_host.ops_since(msg.last_rev, delta)) {
            for (const auto& a : delta)
                send_to_peer(from, K_OP_APPLIED, a, true);
        } else {
            send_snapshot_to_peer(from);
        }
        break;
    }
    case K_SNAPSHOT: {
        SnapshotChunk chunk;
        if (!is_guest() || from != 0 || !chunk.read(r) || chunk.hdr.draft_key != dk)
            return;
        g_guest.my_id = my_id();
        if (g_guest.on_snapshot_chunk(chunk)) {
            g_snaps_rx++;
            H->log(M, SK_INFO, "adv_editor: guest received full snapshot (session=%08X rev=%u markers=%zu acts=%u)",
                   g_guest.state.session_id, g_guest.state.rev, g_guest.state.markers.size(), g_guest.state.num_acts);
            NativeJob job;
            job.kind = NativeJobKind::ReconcileSnapshot;
            queue_native_job(std::move(job));
        }
        break;
    }
    case K_OP_REQ: {
        OpReq req;
        if (!is_host() || from == 0 || !req.read(r) || !g_host.active())
            return;
        if (req.hdr.draft_key != g_host.state.draft_key || req.hdr.session_id != g_host.state.session_id)
            return;
        g_ops_rx++;
        host_commit_and_broadcast(from, req.client_op_id, req.op, true);
        break;
    }
    case K_OP_APPLIED: {
        OpApplied applied;
        if (!is_guest() || from != 0 || !applied.read(r) || !g_guest.synced)
            return;
        if (applied.hdr.draft_key != g_guest.state.draft_key || applied.hdr.session_id != g_guest.state.session_id)
            return;
        g_ops_rx++;
        std::vector<OpApplied> ready;
        g_guest.on_op_applied(applied, ready);
        for (auto& a : ready) {
            NativeJob job;
            job.kind = NativeJobKind::ApplyOp;
            job.applied = std::move(a);
            queue_native_job(std::move(job));
        }
        break;
    }
    case K_LEASE: {
        LeaseMsg lm;
        if (!lm.read(r))
            return;
        if (is_host() && from != 0 && g_host.active() && lm.hdr.session_id == g_host.state.session_id) {
            if (lm.action == LEASE_ACQUIRE || lm.action == LEASE_RENEW) {
                uint8_t holder = kNoOwner;
                double ttl = lm.ttl_ms > 0 ? (double)lm.ttl_ms / 1000.0 : kLeaseTtlSec;
                if (g_host.leases.acquire_or_renew(lm.uid, from, g_now, ttl, &holder)) {
                    LeaseMsg resp{lm.hdr, LEASE_GRANT, lm.uid, from, (uint32_t)(std::min(ttl, kLeaseTtlSec) * 1000.0)};
                    broadcast_editor(K_LEASE, resp, true);
                } else {
                    LeaseMsg resp{lm.hdr, LEASE_DENY, lm.uid, holder, 0};
                    send_to_peer(from, K_LEASE, resp, true);
                }
            } else if (lm.action == LEASE_RELEASE) {
                if (g_host.leases.release(lm.uid, from)) {
                    LeaseMsg resp{lm.hdr, LEASE_RELEASE, lm.uid, from, 0};
                    broadcast_editor(K_LEASE, resp, true);
                }
            }
        } else if (is_guest() && from == 0 && g_guest.synced && lm.hdr.session_id == g_guest.state.session_id) {
            g_guest.on_lease(lm, g_now);
            if (lm.action == LEASE_DENY)
                throttled_feed("Adventure Editor: marker locked by " + player_label(lm.owner_id));
        }
        break;
    }
    case K_DRAG_PREVIEW: {
        DragPreview dp;
        if (!dp.read(r))
            return;
        if (is_host() && from != 0 && g_host.active() && dp.hdr.session_id == g_host.state.session_id) {
            if (!g_host.leases.can_edit(from, dp.uid, g_now))
                return;
            dp.actor_id = from;
            g_previews_rx++;
            broadcast_editor(K_DRAG_PREVIEW, dp, false, from);
            NativeJob job;
            job.kind = NativeJobKind::ApplyPreview;
            job.preview = dp;
            queue_native_job(std::move(job));
        } else if (is_guest() && from == 0 && g_guest.synced && dp.hdr.session_id == g_guest.state.session_id) {
            if (dp.actor_id == my_id() || g_guest.leases.holder_of(dp.uid, g_now) == my_id())
                return;
            g_previews_rx++;
            NativeJob job;
            job.kind = NativeJobKind::ApplyPreview;
            job.preview = dp;
            queue_native_job(std::move(job));
        }
        break;
    }
    case K_OP_REJECT: {
        OpReject rej;
        if (!is_guest() || from != 0 || !rej.read(r) || !g_guest.synced)
            return;
        g_rejects_rx++;
        // Clean up any unapproved local UI create
        if (rej.client_op_id != 0) {
            auto pit = g_pending_local_creates.find(rej.client_op_id);
            if (pit != g_pending_local_creates.end()) {
                NativeJob job;
                job.kind = NativeJobKind::DeleteSlot;
                job.slot_idx = pit->second;
                g_pending_local_creates.erase(pit);
                queue_native_job(std::move(job));
            }
        }
        std::string msg = std::string("Adventure Editor: ") + reject_name(rej.reason);
        if (rej.reason == REJECT_LEASE_HELD && rej.holder_id != kNoOwner)
            msg += " (" + player_label(rej.holder_id) + ")";
        throttled_feed(msg);
        break;
    }
    default:
        break;
    }
}

static void on_session_end() { reset_state("session end"); }
LIFECYCLE_HOOK("adv_editor.session", lifecycle::kSessionEnd, -54, [](const lifecycle::Ctx&) { on_session_end(); }, false);

static void on_player_leave(uint8_t id)
{
    if (!is_host() || !g_host.active())
        return;
    auto freed = g_host.leases.release_player(id);
    for (const auto& e : freed) {
        wire::adv_editor::LeaseMsg lm{{g_host.state.draft_key, g_host.state.session_id},
                                      wire::adv_editor::LEASE_EXPIRED, e.uid, e.owner, 0};
        broadcast_editor(wire::adv_editor::K_LEASE, lm, true);
    }
}
LIFECYCLE_HOOK("adv_editor.leave", lifecycle::kPlayerLeave, 0,
               [](const lifecycle::Ctx& c) { on_player_leave(c.player); }, false);

// ---------------- Frame Tick & Init ----------------
void tick(double now)
{
    g_now = now;
    if (!g_shared_editor_fix || !connected() || !in_edit_mode()) {
        if (g_host.active() || g_guest.synced || !g_uid_to_slot.empty())
            reset_state(!connected() ? "disconnected" : (!g_shared_editor_fix ? "fix off" : "left edit mode"));
        return;
    }

    void* data = scenario_data();
    if (!data)
        return;

    if (is_host()) {
        host_ensure_session(data);
        // Expire stale leases (> 5 s inactivity) and notify peers
        auto expired = g_host.leases.expire(now);
        for (const auto& e : expired) {
            wire::adv_editor::LeaseMsg lm{{g_host.state.draft_key, g_host.state.session_id},
                                          wire::adv_editor::LEASE_EXPIRED, e.uid, e.owner, 0};
            broadcast_editor(wire::adv_editor::K_LEASE, lm, true);
        }
        flush_dirty_transforms(false);
    } else if (is_guest()) {
        if (!host_reads_editor())
            return;
        wire::Key3 dk = current_draft_key();
        if (g_last_data != data || (g_guest.synced && g_guest.state.draft_key != dk)) {
            g_guest.reset();
            g_uid_to_slot.clear();
            g_slot_to_uid.clear();
            g_pending_local_creates.clear();
            g_dirty_xforms.clear();
            g_last_data = data;
            g_last_hello_sent = -1e9;
        }
        g_guest.my_id = my_id();
        g_guest.leases.expire(now);
        if (!g_guest.synced && now - g_last_hello_sent >= kHelloPeriodSec) {
            g_last_hello_sent = now;
            wire::adv_editor::HelloSync hs;
            hs.hdr = {dk, g_guest.state.session_id};
            hs.last_rev = g_guest.state.rev;
            send_to_peer(0, wire::adv_editor::K_HELLO_SYNC, hs, true);
        }
        if (g_guest.synced)
            flush_dirty_transforms(false);
    }
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);

    a_ScenarioModePtr = H->sym("App::cScenarioMode::ptr");
    a_SwitchToEditMode = H->sym("SporeCoop::ScenarioMode_SwitchToEditMode");
    a_ScenarioRequestReset = H->sym("SporeCoop::ScenarioRequest_Reset");
    a_StateManagerGet = H->sym("App::IStateManager::Get");
    a_GetEditAct = H->sym("SporeCoop::ScenarioEdit_GetAct");
    a_SetEditAct = H->sym("SporeCoop::ScenarioEdit_SetAct");
    a_GetNumActs = H->sym("SporeCoop::ScenarioData_GetNumActs");
    a_GetClass = H->sym("SporeCoop::ScenarioData_GetClass");
    a_CreateClass = H->sym("SporeCoop::ScenarioData_CreateClass");
    a_CanSave = H->sym("SporeCoop::ScenarioData_CanSave");
    a_EditHistorySingleton = H->sym("SporeCoop::ScenarioEditHistory_Singleton");

    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_CreateMarker"))
        H->hook_create(M, (void*)a, (void*)h_CreateMarker, (void**)&o_CreateMarker);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_CloneMarker"))
        H->hook_create(M, (void*)a, (void*)h_CloneMarker, (void**)&o_CloneMarker);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_DeleteMarker"))
        H->hook_create(M, (void*)a, (void*)h_DeleteMarker, (void**)&o_DeleteMarker);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_SyncMarkerTransform"))
        H->hook_create(M, (void*)a, (void*)h_SyncMarkerTransform, (void**)&o_SyncMarkerTransform);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_AddAct"))
        H->hook_create(M, (void*)a, (void*)h_AddAct, (void**)&o_AddAct);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_DeleteAct"))
        H->hook_create(M, (void*)a, (void*)h_DeleteAct, (void**)&o_DeleteAct);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_SetCaptainAsset"))
        H->hook_create(M, (void*)a, (void*)h_SetCaptainAsset, (void**)&o_SetCaptainAsset);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_CommitHistory"))
        H->hook_create(M, (void*)a, (void*)h_CommitHistory, (void**)&o_CommitHistory);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioEditHistory_CanUndo"))
        H->hook_create(M, (void*)a, (void*)h_CanUndo, (void**)&o_CanUndo);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioEditHistory_CanRedo"))
        H->hook_create(M, (void*)a, (void*)h_CanRedo, (void**)&o_CanRedo);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioEditHistory_Undo"))
        H->hook_create(M, (void*)a, (void*)h_Undo, (void**)&o_Undo);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioEditHistory_Redo"))
        H->hook_create(M, (void*)a, (void*)h_Redo, (void**)&o_Redo);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioData_Save"))
        H->hook_create(M, (void*)a, (void*)h_Save, (void**)&o_Save);
}

// ---------------- Console Command (`coop.adveditor`, SK_CMD_OUTSIDE_FRAME) ----------------
std::string command(const std::vector<std::string>& args)
{
    int sub = modes::adventure_sub_mode();
    void* data = scenario_data();
    bool active = in_edit_mode();
    const DraftState* st = is_host() ? (g_host.active() ? &g_host.state : nullptr)
                                     : (g_guest.synced ? &g_guest.state : nullptr);
    const LeaseTable* lt = is_host() ? &g_host.leases : &g_guest.leases;

    auto status_line = [&]() -> std::string {
        char buf[448];
        size_t native_cnt = active ? scan_native_markers(data).size() : 0;
        int acts = active ? native_num_acts(data) : 0;
        int can_save_reason = 0;
        bool can_save = (active && data && a_CanSave) ? skg::thiscall<bool>(a_CanSave, data, &can_save_reason) : false;
        uint8_t ud = is_host() ? g_host.undo_depth(0) : g_guest.my_undo_depth;
        uint8_t rd = is_host() ? g_host.redo_depth(0) : g_guest.my_redo_depth;
        snprintf(buf, sizeof buf,
                 "adveditor fix=%d mode=%s sub=%d(%s) role=%s synced=%d session=%08X rev=%u "
                 "native_markers=%zu draft_markers=%zu acts=%d act=%d can_save=%d(%d) leases=%zu undo=%u redo=%u "
                 "tx=%u rx=%u snaps=%u/%u prev=%u/%u ltx=%u rejects=%u\n",
                 (int)g_shared_editor_fix, modes::mode_name(game::mode_id()), sub,
                 sub == 1 ? "edit" : (sub == 2 ? "play" : "none"),
                 is_host() ? "host" : (is_guest() ? "guest" : "solo"), (int)(st != nullptr),
                 st ? st->session_id : 0u, st ? st->rev : 0u, native_cnt, st ? st->markers.size() : 0u, acts,
                 active ? native_current_act() : -1, (int)can_save, can_save_reason, lt ? lt->leases.size() : 0u, ud, rd,
                 g_ops_tx, g_ops_rx, g_snaps_tx, g_snaps_rx, g_previews_tx, g_previews_rx, g_leases_tx, g_rejects_rx);
        return buf;
    };

    if (args.empty() || args[0] == "status")
        return status_line();

    // `coop.adveditor enter [inst_hex] [group_hex]`:
    //   - If already in Scenario with Editor UI allocated ([sm+0x10] & [sm+0x14] non-null), switches preview -> Edit.
    //   - Otherwise (e.g. Galaxy Menu), launches native Adventure Editor via ScenarioRequest_Reset + IStateManager.
    if (args[0] == "enter") {
        if (game::mode_id() == modes::kScenario) {
            void* sm = scenario_mode();
            if (sm && skg::readable(sm, 0xD4) && skg::field<void*>(sm, 0x10) && skg::field<void*>(sm, 0x14) &&
                a_SwitchToEditMode) {
                if (modes::adventure_sub_mode() != 1)
                    skg::thiscall<void>(a_SwitchToEditMode, sm);
                return status_line();
            }
            return status_line() + "current Scenario was opened Play-only (no Editor UI); enter from Galaxy Menu\n";
        }
        if (!a_ScenarioRequestReset || !a_StateManagerGet)
            return "ScenarioRequest_Reset or IStateManager unavailable\n";
        uint32_t* req = skg::cdecl_call<uint32_t*>(a_ScenarioRequestReset);
        if (!req || !skg::readable(req, 0x30))
            return "ScenarioRequest_Reset returned null\n";
        if (args.size() >= 2 && args[1] != "new") {
            uint32_t inst = (uint32_t)strtoul(args[1].c_str(), nullptr, 16);
            uint32_t grp = args.size() >= 3 ? (uint32_t)strtoul(args[2].c_str(), nullptr, 16) : 0x408A0000u;
            req[0] = inst;
            req[1] = 0x030BDEE3u;
            req[2] = grp;
        }
        req[0x24 / 4] = 1u; // subMode = Edit
        req[0x28 / 4] = 0u; // entryKind = Edit/Create (allocates Editor UI controllers +0x10/+0x14)
        void* stm = skg::cdecl_call<void*>(a_StateManagerGet);
        if (!stm || !skg::readable(stm, 4))
            return "IStateManager null\n";
        uintptr_t vt = *(uintptr_t*)stm;
        if (!vt || !skg::readable((void*)(vt + 0x28), 4))
            return "IStateManager vtable unreadable\n";
        uintptr_t fn = *(uintptr_t*)(vt + 0x28);
        skg::thiscall<void>(fn, stm, 0x21851EBEu);
        return "launching Adventure Editor\n";
    }

    if (!active || !data)
        return status_line() + "not in Adventure Editor (submode must be 1)\n";

    if (args[0] == "snapshot") {
        if (!g_shared_editor_fix || !connected())
            return status_line() + "shared editor inactive\n";
        if (is_host()) {
            host_ensure_session(data);
            for (const auto& p : C.sess->players())
                if (p.id != 0 && peer_reads_editor(p.id))
                    send_snapshot_to_peer(p.id);
            return status_line() + "snapshot broadcast\n";
        }
        wire::adv_editor::HelloSync hs{{current_draft_key(), 0}, 0};
        send_to_peer(0, wire::adv_editor::K_HELLO_SYNC, hs, true);
        return status_line() + "snapshot requested\n";
    }

    if (args[0] == "markers") {
        std::string out = status_line();
        auto native = scan_native_markers(data);
        for (const auto& m : native) {
            uint32_t uid = 0;
            if (auto it = g_slot_to_uid.find(m.slot_idx); it != g_slot_to_uid.end())
                uid = it->second;
            uint8_t holder = (uid && lt) ? lt->holder_of(uid, g_now) : kNoOwner;
            char ln[220];
            snprintf(ln, sizeof ln,
                     "  uid=%u slot=%u class=%d key=%08X:%08X:%08X pos=(%.2f %.2f %.2f) scale=%.2f lease=%d\n",
                     uid, m.slot_idx, m.class_id, m.class_key.v[0], m.class_key.v[1], m.class_key.v[2],
                     m.pos[0], m.pos[1], m.pos[2], m.scale, holder == kNoOwner ? -1 : (int)holder);
            out += ln;
        }
        return out;
    }

    if (args[0] == "create" && args.size() >= 5) {
        int32_t cid = -1;
        wire::Key3 ck;
        if (args[1] == "auto") {
            cid = native_first_class_id(data);
            ck = native_class_key(data, cid);
        } else {
            uint32_t val = (uint32_t)strtoul(args[1].c_str(), nullptr, 16);
            if (val < 1024 && a_GetClass && skg::thiscall<void*>(a_GetClass, data, (int)val)) {
                cid = (int32_t)val;
                ck = native_class_key(data, cid);
            } else {
                ck.v[0] = val;
                cid = resolve_or_create_class(data, -1, ck);
                if (cid >= 0)
                    ck = native_class_key(data, cid);
            }
        }
        if (cid < 0)
            return status_line() + "no valid scenario class for " + args[1] + "\n";

        float pos[3] = {(float)atof(args[2].c_str()), (float)atof(args[3].c_str()), (float)atof(args[4].c_str())};
        float rot[4] = {0.f, 0.f, 0.f, 1.f};
        float scale = args.size() >= 6 ? (float)atof(args[5].c_str()) : 1.f;

        if (!hooks_active()) {
            // Solo or fix-off: create natively without replication
            void* m = o_CreateMarker ? o_CreateMarker(data, (int)cid, pos, rot, scale) : nullptr;
            if (!m)
                return status_line() + "native CreateMarker failed\n";
            char ln[128];
            snprintf(ln, sizeof ln, "created local slot=%u class=%d pos=(%.2f %.2f %.2f)\n",
                     skg::field<uint32_t>(m, kMarkerSlotIdxOff), cid, pos[0], pos[1], pos[2]);
            return status_line() + ln;
        }

        wire::adv_editor::OpData op;
        op.kind = wire::adv_editor::OP_CREATE_MARKER;
        op.uid = 0;
        op.class_id = cid;
        op.key = ck;
        op.act = (uint8_t)std::clamp(native_current_act(), 0, (int)wire::adv_editor::kMaxActs - 1);
        for (int i = 0; i < 3; i++)
            op.pos[i] = pos[i];
        for (int i = 0; i < 4; i++)
            op.rot[i] = rot[i];
        op.scale = scale;

        if (is_host()) {
            wire::adv_editor::OpApplied applied;
            if (!host_commit_and_broadcast(0, 0, op, false, &applied))
                return status_line() + "create rejected\n";
            g_applying = true;
            apply_op_native_now(data, applied);
            g_applying = false;
            char ln[128];
            snprintf(ln, sizeof ln, "created uid=%u rev=%u class=%d pos=(%.2f %.2f %.2f)\n",
                     applied.op.uid, applied.rev, cid, pos[0], pos[1], pos[2]);
            return status_line() + ln;
        }
        uint32_t req_id = guest_send_op_req(op);
        char ln[96];
        snprintf(ln, sizeof ln, "create requested client_op=%u\n", req_id);
        return status_line() + ln;
    }

    if (args[0] == "move" && args.size() >= 5) {
        uint32_t uid = (uint32_t)strtoul(args[1].c_str(), nullptr, 10);
        float pos[3] = {(float)atof(args[2].c_str()), (float)atof(args[3].c_str()), (float)atof(args[4].c_str())};
        float rot[4] = {0.f, 0.f, 0.f, 1.f};
        float scale = args.size() >= 6 ? (float)atof(args[5].c_str()) : 1.f;
        if (st) {
            if (auto mit = st->markers.find(uid); mit != st->markers.end()) {
                for (int i = 0; i < 4; i++)
                    rot[i] = mit->second.rot[i];
                if (args.size() < 6)
                    scale = mit->second.scale;
            }
        }
        if (!hooks_active()) {
            void* m = find_native_marker_by_slot(data, uid);
            if (!m)
                return status_line() + "local marker not found\n";
            write_marker_transform_native(data, m, pos, rot, scale, true);
            return status_line() + "moved local marker\n";
        }
        request_lease_local(uid, wire::adv_editor::LEASE_ACQUIRE);
        wire::adv_editor::OpData op;
        op.kind = wire::adv_editor::OP_TRANSFORM_MARKER;
        op.uid = uid;
        for (int i = 0; i < 3; i++)
            op.pos[i] = pos[i];
        for (int i = 0; i < 4; i++)
            op.rot[i] = rot[i];
        op.scale = scale;
        if (is_host()) {
            wire::adv_editor::OpApplied applied;
            wire::adv_editor::OpReject rej;
            if (!host_commit_and_broadcast(0, 0, op, false, &applied, &rej))
                return status_line() + std::string("move rejected: ") + reject_name(rej.reason) + "\n";
            g_applying = true;
            apply_op_native_now(data, applied);
            g_applying = false;
            char ln[128];
            snprintf(ln, sizeof ln, "moved uid=%u rev=%u pos=(%.2f %.2f %.2f)\n", uid, applied.rev, pos[0], pos[1], pos[2]);
            return status_line() + ln;
        }
        uint32_t req_id = guest_send_op_req(op);
        char ln[96];
        snprintf(ln, sizeof ln, "move requested uid=%u client_op=%u\n", uid, req_id);
        return status_line() + ln;
    }

    if (args[0] == "delete" && args.size() >= 2) {
        uint32_t uid = (uint32_t)strtoul(args[1].c_str(), nullptr, 10);
        if (!hooks_active()) {
            void* m = find_native_marker_by_slot(data, uid);
            if (!m || !o_DeleteMarker)
                return status_line() + "local marker not found\n";
            o_DeleteMarker(data, m);
            return status_line() + "deleted local marker\n";
        }
        wire::adv_editor::OpData op;
        op.kind = wire::adv_editor::OP_DELETE_MARKER;
        op.uid = uid;
        if (is_host()) {
            wire::adv_editor::OpApplied applied;
            wire::adv_editor::OpReject rej;
            if (!host_commit_and_broadcast(0, 0, op, false, &applied, &rej))
                return status_line() + std::string("delete rejected: ") + reject_name(rej.reason) + "\n";
            g_applying = true;
            apply_op_native_now(data, applied);
            g_applying = false;
            char ln[96];
            snprintf(ln, sizeof ln, "deleted uid=%u rev=%u\n", uid, applied.rev);
            return status_line() + ln;
        }
        uint32_t req_id = guest_send_op_req(op);
        char ln[96];
        snprintf(ln, sizeof ln, "delete requested uid=%u client_op=%u\n", uid, req_id);
        return status_line() + ln;
    }

    if (args[0] == "undo" || args[0] == "redo") {
        bool is_u = (args[0] == "undo");
        if (!hooks_active()) {
            void* hist = (void*)a_EditHistorySingleton;
            if (hist && (is_u ? o_Undo : o_Redo))
                (is_u ? o_Undo : o_Redo)(hist);
            return status_line() + (is_u ? "local undo\n" : "local redo\n");
        }
        wire::adv_editor::OpData op;
        op.kind = is_u ? wire::adv_editor::OP_UNDO : wire::adv_editor::OP_REDO;
        if (is_host()) {
            wire::adv_editor::OpApplied applied;
            wire::adv_editor::OpReject rej;
            if (!host_commit_and_broadcast(0, 0, op, false, &applied, &rej))
                return status_line() + std::string(args[0]) + " rejected: " + reject_name(rej.reason) + "\n";
            g_applying = true;
            apply_op_native_now(data, applied);
            g_applying = false;
            char ln[96];
            snprintf(ln, sizeof ln, "%s applied rev=%u op_kind=%u uid=%u\n", args[0].c_str(), applied.rev,
                     applied.op.kind, applied.op.uid);
            return status_line() + ln;
        }
        uint32_t req_id = guest_send_op_req(op);
        char ln[96];
        snprintf(ln, sizeof ln, "%s requested client_op=%u\n", args[0].c_str(), req_id);
        return status_line() + ln;
    }

    if (args[0] == "act" && args.size() >= 2) {
        if (args[1] == "add") {
            int idx = args.size() >= 3 ? atoi(args[2].c_str()) : native_num_acts(data);
            if (!hooks_active()) {
                int r = o_AddAct ? o_AddAct(data, std::clamp(idx, 1, native_num_acts(data))) : -1;
                char ln[64];
                snprintf(ln, sizeof ln, "local act add -> %d\n", r);
                return status_line() + ln;
            }
            wire::adv_editor::OpData op;
            op.kind = wire::adv_editor::OP_ADD_ACT;
            op.act = (uint8_t)std::clamp(idx, 1, (int)wire::adv_editor::kMaxActs - 1);
            if (is_host()) {
                wire::adv_editor::OpApplied applied;
                if (!host_commit_and_broadcast(0, 0, op, false, &applied))
                    return status_line() + "act add rejected\n";
                g_applying = true;
                apply_op_native_now(data, applied);
                g_applying = false;
                return status_line() + "act added\n";
            }
            guest_send_op_req(op);
            return status_line() + "act add requested\n";
        }
        if (args[1] == "delete" && args.size() >= 3) {
            int idx = atoi(args[2].c_str());
            if (!hooks_active()) {
                if (o_DeleteAct)
                    o_DeleteAct(data, idx);
                return status_line() + "local act deleted\n";
            }
            wire::adv_editor::OpData op;
            op.kind = wire::adv_editor::OP_DELETE_ACT;
            op.act = (uint8_t)std::clamp(idx, 0, (int)wire::adv_editor::kMaxActs - 1);
            if (is_host()) {
                wire::adv_editor::OpApplied applied;
                if (!host_commit_and_broadcast(0, 0, op, false, &applied))
                    return status_line() + "act delete rejected\n";
                g_applying = true;
                apply_op_native_now(data, applied);
                g_applying = false;
                return status_line() + "act deleted\n";
            }
            guest_send_op_req(op);
            return status_line() + "act delete requested\n";
        }
    }

    if (args[0] == "captain" && args.size() >= 2) {
        wire::Key3 k;
        k.v[0] = (uint32_t)strtoul(args[1].c_str(), nullptr, 16);
        k.v[1] = args.size() >= 3 ? (uint32_t)strtoul(args[2].c_str(), nullptr, 16) : 0x2B978C46u;
        k.v[2] = args.size() >= 4 ? (uint32_t)strtoul(args[3].c_str(), nullptr, 16) : 0x40626200u;
        if (!hooks_active()) {
            if (o_SetCaptainAsset)
                o_SetCaptainAsset(data, k.v);
            return status_line() + "local captain set\n";
        }
        wire::adv_editor::OpData op;
        op.kind = wire::adv_editor::OP_SET_CAPTAIN;
        op.key = k;
        if (is_host()) {
            wire::adv_editor::OpApplied applied;
            if (!host_commit_and_broadcast(0, 0, op, false, &applied))
                return status_line() + "captain set rejected\n";
            g_applying = true;
            apply_op_native_now(data, applied);
            g_applying = false;
            return status_line() + "captain updated\n";
        }
        guest_send_op_req(op);
        return status_line() + "captain update requested\n";
    }

    if (args[0] == "lease" && args.size() >= 2) {
        uint32_t uid = (uint32_t)strtoul(args[1].c_str(), nullptr, 10);
        bool rel = (args.size() >= 3 && args[2] == "release");
        request_lease_local(uid, rel ? wire::adv_editor::LEASE_RELEASE : wire::adv_editor::LEASE_ACQUIRE);
        char ln[96];
        snprintf(ln, sizeof ln, "lease %s uid=%u\n", rel ? "release" : "acquire", uid);
        return status_line() + ln;
    }

    return status_line() +
           "usage: coop.adveditor [status | enter | snapshot | markers | create <class|auto> <x> <y> <z> [scale] | "
           "move <uid> <x> <y> <z> [scale] | delete <uid> | undo | redo | act add [i] | act delete <i> | "
           "captain <inst_hex> [type_hex] [group_hex] | lease <uid> [release]]\n";
}

} // namespace adv_editor
