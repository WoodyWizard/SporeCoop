// Native unit tests for WP-AE Shared Adventure Editor (adv_editor_logic.h, wire/adv_editor.h, BUG-0863).
// Compiled with ASan + UBSan by `make unit-cpp`.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include "adv_editor_logic.h"

static int g_checks = 0, g_fails = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_fails++;                                                                                                 \
        }                                                                                                              \
    } while (0)

using namespace adv_editor;
using namespace wire::adv_editor;

static wire::Key3 mk_key(uint32_t a, uint32_t b = 0x2B978C46u, uint32_t c = 0x40626200u) { return {{a, b, c}}; }

static MarkerRec mk_marker(uint32_t uid, int32_t cid, float x, float y, float z, float scale = 1.f)
{
    MarkerRec m;
    m.uid = uid;
    m.class_id = cid;
    m.class_key = mk_key(0x1000u + (uint32_t)cid);
    m.act = 0;
    m.pos[0] = x;
    m.pos[1] = y;
    m.pos[2] = z;
    m.rot[0] = 0.f;
    m.rot[1] = 0.f;
    m.rot[2] = 0.f;
    m.rot[3] = 1.f;
    m.scale = scale;
    return m;
}

static OpData mk_create(int32_t cid, float x, float y, float z, float scale = 1.f)
{
    OpData o;
    o.kind = OP_CREATE_MARKER;
    o.uid = 0;
    o.class_id = cid;
    o.key = mk_key(0x2000u + (uint32_t)cid);
    o.act = 0;
    o.pos[0] = x;
    o.pos[1] = y;
    o.pos[2] = z;
    o.rot[3] = 1.f;
    o.scale = scale;
    return o;
}

static OpData mk_xform(uint32_t uid, float x, float y, float z, float scale = 1.f)
{
    OpData o;
    o.kind = OP_TRANSFORM_MARKER;
    o.uid = uid;
    o.pos[0] = x;
    o.pos[1] = y;
    o.pos[2] = z;
    o.rot[3] = 1.f;
    o.scale = scale;
    return o;
}

static void test_family_gating()
{
    std::vector<net::FamilyRev> old_peer = {{(uint8_t)net::Family::creature, 1, 4}};
    std::vector<net::FamilyRev> new_peer = {{(uint8_t)net::Family::creature, 1, 5}};
    CHECK(!peer_supports(old_peer, true));
    CHECK(!peer_supports(new_peer, false));
    CHECK(peer_supports(new_peer, true));
}

static void test_leases_conflict_and_expiry()
{
    LeaseTable lt;
    uint8_t holder = kNoOwner;

    // Actor 1 acquires uid 10 at t=100
    CHECK(lt.acquire_or_renew(10, 1, 100.0, kLeaseTtlSec, &holder));
    CHECK(holder == 1);
    CHECK(lt.holder_of(10, 102.0) == 1);
    CHECK(lt.can_edit(1, 10, 102.0));
    CHECK(!lt.can_edit(2, 10, 102.0));

    // Actor 2 is denied while actor 1's lease is active
    CHECK(!lt.acquire_or_renew(10, 2, 102.0, kLeaseTtlSec, &holder));
    CHECK(holder == 1);

    // Actor 1 renews at t=104 -> expires at 109
    CHECK(lt.acquire_or_renew(10, 1, 104.0, kLeaseTtlSec, &holder));
    CHECK(lt.holder_of(10, 108.5) == 1);

    // After 5 s of inactivity (t=109.1), lease expires automatically
    auto exp = lt.expire(109.1);
    CHECK(exp.size() == 1 && exp[0].uid == 10 && exp[0].owner == 1);
    CHECK(lt.holder_of(10, 109.1) == kNoOwner);
    CHECK(lt.can_edit(2, 10, 109.1));

    // Actor 2 acquires 10 and 11, then disconnects -> release_player frees both
    CHECK(lt.acquire_or_renew(10, 2, 110.0));
    CHECK(lt.acquire_or_renew(11, 2, 110.0));
    auto freed = lt.release_player(2);
    CHECK(freed.size() == 2);
    CHECK(lt.holder_of(10, 110.5) == kNoOwner && lt.holder_of(11, 110.5) == kNoOwner);
}

static void test_host_ops_and_per_player_undo_redo()
{
    HostEditor host;
    host.start_session(mk_key(0xABCDEF01u), 0x12345678u, 1, 0, mk_key(0x1111u), {});
    CHECK(host.active() && host.state.rev == 0 && host.state.markers.empty());

    OpApplied a;
    OpReject r;

    // 1. Host (actor 0) creates marker 1 at (10, 20, 30)
    CHECK(host.apply_op(0, 1, mk_create(0, 10.f, 20.f, 30.f, 1.5f), 1.0, a, r));
    uint32_t u1 = a.op.uid;
    CHECK(u1 == 1 && a.rev == 1 && host.undo_depth(0) == 1);
    CHECK(host.state.markers.size() == 1);
    CHECK(std::fabs(host.state.markers[u1].pos[0] - 10.f) < 1e-4f);

    // 2. Guest (actor 1) acquires lease on u1 and moves it to (40, 50, 60)
    CHECK(host.leases.acquire_or_renew(u1, 1, 2.0));
    // Host is blocked from moving u1 while guest holds the lease!
    CHECK(!host.apply_op(0, 2, mk_xform(u1, 99.f, 99.f, 99.f), 2.5, a, r));
    CHECK(r.reason == REJECT_LEASE_HELD && r.holder_id == 1);

    // Guest moves u1 -> succeeds
    CHECK(host.apply_op(1, 10, mk_xform(u1, 40.f, 50.f, 60.f, 2.0f), 2.5, a, r));
    CHECK(a.rev == 2 && a.actor_id == 1 && host.undo_depth(1) == 1);
    CHECK(std::fabs(host.state.markers[u1].pos[0] - 40.f) < 1e-4f);
    CHECK(std::fabs(host.state.markers[u1].scale - 2.0f) < 1e-4f);

    // 3. Host creates marker 2 at (1, 2, 3) concurrently
    CHECK(host.apply_op(0, 3, mk_create(1, 1.f, 2.f, 3.f, 1.0f), 3.0, a, r));
    uint32_t u2 = a.op.uid;
    CHECK(u2 == 2 && a.rev == 3 && host.undo_depth(0) == 2);

    // 4. Guest runs Undo -> ONLY u1 reverts to (10, 20, 30, scale 1.5); u2 is UNTOUCHED!
    OpData undo_op;
    undo_op.kind = OP_UNDO;
    CHECK(host.apply_op(1, 11, undo_op, 3.5, a, r));
    CHECK(a.rev == 4 && (a.flags & kAppliedFlagFromUndo) && a.op.kind == OP_TRANSFORM_MARKER);
    CHECK(host.undo_depth(1) == 0 && host.redo_depth(1) == 1);
    CHECK(std::fabs(host.state.markers[u1].pos[0] - 10.f) < 1e-4f);
    CHECK(std::fabs(host.state.markers[u1].scale - 1.5f) < 1e-4f);
    CHECK(host.state.markers.count(u2) == 1);

    // Second Undo by guest fails cleanly with REJECT_NOTHING_TO_UNDO
    CHECK(!host.apply_op(1, 12, undo_op, 3.6, a, r));
    CHECK(r.reason == REJECT_NOTHING_TO_UNDO);

    // 5. Guest Redoes -> u1 goes back to (40, 50, 60, scale 2.0)
    OpData redo_op;
    redo_op.kind = OP_REDO;
    CHECK(host.apply_op(1, 13, redo_op, 4.0, a, r));
    CHECK(a.rev == 5 && (a.flags & kAppliedFlagFromRedo) && host.undo_depth(1) == 1 && host.redo_depth(1) == 0);
    CHECK(std::fabs(host.state.markers[u1].pos[0] - 40.f) < 1e-4f);

    // 6. Lease expires after 5 s (t=10.0), host deletes u1, then host Undoes delete -> u1 restored!
    OpData del_op;
    del_op.kind = OP_DELETE_MARKER;
    del_op.uid = u1;
    CHECK(host.apply_op(0, 4, del_op, 10.0, a, r));
    CHECK(a.rev == 6 && host.state.markers.count(u1) == 0);

    CHECK(host.apply_op(0, 5, undo_op, 10.5, a, r));
    CHECK(a.rev == 7 && a.op.kind == OP_CREATE_MARKER && a.op.uid == u1);
    CHECK(host.state.markers.count(u1) == 1);
    CHECK(std::fabs(host.state.markers[u1].pos[0] - 40.f) < 1e-4f);

    // 7. Acts and captain asset + Undo
    OpData add_act;
    add_act.kind = OP_ADD_ACT;
    add_act.act = 1;
    CHECK(host.apply_op(0, 6, add_act, 11.0, a, r));
    CHECK(host.state.num_acts == 2);
    CHECK(host.apply_op(0, 7, undo_op, 11.5, a, r));
    CHECK(host.state.num_acts == 1);

    OpData set_cap;
    set_cap.kind = OP_SET_CAPTAIN;
    set_cap.key = mk_key(0x9999u);
    CHECK(host.apply_op(1, 14, set_cap, 12.0, a, r));
    CHECK(host.state.captain_key == mk_key(0x9999u));
    CHECK(host.apply_op(1, 15, undo_op, 12.5, a, r));
    CHECK(host.state.captain_key == mk_key(0x1111u));
}

static void test_snapshot_chunking_and_guest_reconciliation()
{
    HostEditor host;
    std::vector<MarkerRec> initial;
    for (uint32_t i = 1; i <= 40; i++)
        initial.push_back(mk_marker(i, (int32_t)(i % 5), (float)i, (float)(i * 2), 0.f));
    host.start_session(mk_key(0xCAFEBABEu), 0x88776655u, 3, 1, mk_key(0x5555u), initial);

    auto chunks = host.build_snapshot_chunks();
    CHECK(chunks.size() == 3); // 16 + 16 + 8 = 40
    CHECK(chunks[0].markers.size() == 16 && chunks[1].markers.size() == 16 && chunks[2].markers.size() == 8);

    // Deliver snapshot chunks out of order (2, 0, 1) to late-joining guest
    GuestEditor guest;
    guest.my_id = 1;
    CHECK(!guest.on_snapshot_chunk(chunks[2]));
    CHECK(!guest.synced);
    CHECK(!guest.on_snapshot_chunk(chunks[0]));
    CHECK(!guest.synced);
    CHECK(guest.on_snapshot_chunk(chunks[1]));
    CHECK(guest.synced && guest.state.markers.size() == 40 && guest.state.num_acts == 3);
    CHECK(guest.state.captain_key == mk_key(0x5555u));

    // Host commits rev 1 and rev 2; deliver rev 2 BEFORE rev 1 to guest
    OpApplied a1, a2;
    OpReject r;
    CHECK(host.apply_op(0, 1, mk_xform(5, 100.f, 200.f, 300.f), 1.0, a1, r));
    CHECK(host.apply_op(1, 2, mk_create(2, 7.f, 8.f, 9.f), 2.0, a2, r));
    CHECK(a1.rev == 1 && a2.rev == 2);

    std::vector<OpApplied> ready;
    guest.on_op_applied(a2, ready);
    CHECK(ready.empty() && guest.state.rev == 0); // buffered waiting for rev 1

    guest.on_op_applied(a1, ready);
    CHECK(ready.size() == 2 && ready[0].rev == 1 && ready[1].rev == 2);
    CHECK(guest.state.rev == 2);
    CHECK(guest.state.markers.size() == 41);
    CHECK(std::fabs(guest.state.markers[5].pos[0] - 100.f) < 1e-4f);
    CHECK(guest.my_undo_depth == 1);

    // Delta catch-up via ops_since
    std::vector<OpApplied> delta;
    CHECK(host.ops_since(0, delta) && delta.size() == 2);
    CHECK(host.ops_since(1, delta) && delta.size() == 1 && delta[0].rev == 2);
    CHECK(host.ops_since(2, delta) && delta.empty());
    CHECK(!host.ops_since(99, delta));
}

template <class T> static bool roundtrip(const T& in, T& out)
{
    net::Writer w;
    in.write(w);
    net::Reader r(w.data.data(), w.size());
    return out.read(r);
}

static void test_wire_roundtrips_and_bounds()
{
    DraftHeader hdr{mk_key(0x11223344u), 0x55667788u};

    HelloSync hs{hdr, 42}, hs2;
    CHECK(roundtrip(hs, hs2) && hs2.last_rev == 42 && hs2.hdr.session_id == hdr.session_id);

    SnapshotChunk sc;
    sc.hdr = hdr;
    sc.rev = 7;
    sc.num_acts = 2;
    sc.current_act = 1;
    sc.captain_key = mk_key(0x99u);
    sc.chunk_idx = 0;
    sc.chunk_total = 1;
    sc.markers.push_back(mk_marker(1, 0, 1.f, 2.f, 3.f, 1.25f));
    SnapshotChunk sc2;
    CHECK(roundtrip(sc, sc2) && sc2.markers.size() == 1 && sc2.markers[0].uid == 1);

    OpReq req{hdr, 99, mk_create(3, 5.f, 6.f, 7.f, 2.f)}, req2;
    CHECK(roundtrip(req, req2) && req2.client_op_id == 99 && req2.op.kind == OP_CREATE_MARKER);

    OpApplied app{hdr, 12, 1, 99, kAppliedFlagFromUndo, 2, 1, mk_xform(1, 3.f, 4.f, 5.f)}, app2;
    CHECK(roundtrip(app, app2) && app2.rev == 12 && app2.flags == kAppliedFlagFromUndo);

    LeaseMsg lm{hdr, LEASE_GRANT, 7, 2, 5000}, lm2;
    CHECK(roundtrip(lm, lm2) && lm2.owner_id == 2 && lm2.ttl_ms == 5000);

    DragPreview dp;
    dp.hdr = hdr; dp.actor_id = 1; dp.uid = 7;
    dp.pos[0] = 1.f; dp.pos[1] = 2.f; dp.pos[2] = 3.f;
    dp.rot[3] = 1.f; dp.scale = 1.5f;
    DragPreview dp2;
    CHECK(roundtrip(dp, dp2) && dp2.uid == 7 && std::fabs(dp2.scale - 1.5f) < 1e-5f);

    OpReject rej{hdr, 55, REJECT_LEASE_HELD, 0, 7}, rej2;
    CHECK(roundtrip(rej, rej2) && rej2.reason == REJECT_LEASE_HELD);

    // Reject malformed payloads: NaN pos, raw OP_UNDO inside OpApplied, oversized scale, zero session
    DragPreview bad_nan = dp;
    bad_nan.pos[0] = std::numeric_limits<float>::quiet_NaN();
    DragPreview out_dp;
    CHECK(!roundtrip(bad_nan, out_dp));

    OpApplied bad_undo = app;
    bad_undo.op.kind = OP_UNDO;
    OpApplied out_app;
    CHECK(!roundtrip(bad_undo, out_app));

    DragPreview bad_scale = dp;
    bad_scale.scale = 99999.f;
    CHECK(!roundtrip(bad_scale, out_dp));

    LeaseMsg bad_ttl = lm;
    bad_ttl.ttl_ms = kMaxLeaseTtlMs + 1;
    LeaseMsg out_lm;
    CHECK(!roundtrip(bad_ttl, out_lm));

    // Truncated buffer must fail cleanly
    net::Writer w;
    sc.write(w);
    for (size_t cut = 0; cut < w.size(); cut++) {
        net::Reader r(w.data.data(), cut);
        SnapshotChunk tmp;
        CHECK(!tmp.read(r));
    }
}

static void test_session_reset_and_guest_resync()
{
    wire::Key3 unsaved_dk = {{0, 0, 0}};
    wire::Key3 saved_dk = mk_key(0xF63B6851u, 0x030BDEE3u, 0x408A0000u);
    wire::Key3 save_as_dk = mk_key(0x99887766u, 0x030BDEE3u, 0x408A0000u);
    wire::Key3 other_dk = mk_key(0xDEADBEEFu, 0x030BDEE3u, 0x408A0000u);

    HostEditor host;
    GuestEditor guest;
    guest.my_id = 1;

    // 1. Both start on unsaved draft {0,0,0}, session S1
    host.start_session(unsaved_dk, 0x11111111u, 1, 0, mk_key(0x100u), {mk_marker(1, 0, 5.f, 6.f, 7.f)});
    for (const auto& c : host.build_snapshot_chunks())
        CHECK(guest.on_snapshot_chunk(c));
    CHECK(guest.synced && guest.state.session_id == 0x11111111u);

    // Guest makes an op and holds a lease on S1, plus buffers a future op rev=99
    OpApplied a1;
    OpReject r1;
    CHECK(host.apply_op(1, 1, mk_xform(1, 10.f, 11.f, 12.f), 1.0, a1, r1));
    std::vector<OpApplied> ready;
    guest.on_op_applied(a1, ready);
    CHECK(ready.size() == 1 && guest.my_undo_depth == 1 && guest.state.rev == 1);
    guest.on_lease(LeaseMsg{{unsaved_dk, 0x11111111u}, LEASE_GRANT, 1, 1, 5000}, 1.0);
    CHECK(guest.leases.holder_of(1, 2.0) == 1);

    // 2. Host saves unsaved draft -> draft_key changes to saved_dk and host mints S2
    host.start_session(saved_dk, 0x22222222u, 1, 0, mk_key(0x100u), {mk_marker(1, 0, 10.f, 11.f, 12.f)});
    DraftHeader save_rst{saved_dk, 0x22222222u};
    CHECK(guest.on_session_reset(save_rst, unsaved_dk));
    CHECK(!guest.synced && guest.state.session_id == 0 && guest.my_undo_depth == 0);
    CHECK(guest.leases.holder_of(1, 2.0) == kNoOwner);
    CHECK(guest.has_adopted_draft_key && guest.effective_draft_key(unsaved_dk) == saved_dk);

    // Guest resyncs from host's S2 snapshot even though guest's local draft_key is still {0,0,0}
    auto chunks2 = host.build_snapshot_chunks();
    CHECK(chunks2.size() == 1 && guest.accepts_draft_key(chunks2[0].hdr.draft_key, unsaved_dk));
    CHECK(guest.on_snapshot_chunk(chunks2[0]));
    guest.adopt_draft_key_if_needed(chunks2[0].hdr.draft_key, unsaved_dk);
    CHECK(guest.synced && guest.state.session_id == 0x22222222u && guest.state.draft_key == saved_dk);

    // Future ops on S2 apply cleanly instead of being dropped forever
    OpApplied a2;
    CHECK(host.apply_op(0, 2, mk_create(2, 20.f, 21.f, 22.f), 3.0, a2, r1));
    CHECK(!guest.session_mismatch(a2.hdr));
    guest.on_op_applied(a2, ready);
    CHECK(ready.size() == 1 && guest.state.markers.size() == 2 && guest.state.rev == 1);

    // 3. Host enters Play-test -> broadcasts reset with session_id == 0
    CHECK(guest.on_session_reset(DraftHeader{saved_dk, 0u}, unsaved_dk));
    CHECK(!guest.synced && guest.state.session_id == 0);
    CHECK(guest.effective_draft_key(unsaved_dk) == saved_dk);

    // Host returns from Play-test -> mints S3
    host.start_session(saved_dk, 0x33333333u, 1, 0, mk_key(0x100u), {mk_marker(1, 0, 10.f, 11.f, 12.f)});
    CHECK(guest.on_session_reset(DraftHeader{saved_dk, 0x33333333u}, unsaved_dk));
    for (const auto& c : host.build_snapshot_chunks())
        CHECK(guest.on_snapshot_chunk(c));
    CHECK(guest.synced && guest.state.session_id == 0x33333333u);

    // 4. Defensive session mismatch detection if a reset packet was missed (host jumped to S4)
    host.start_session(save_as_dk, 0x44444444u, 1, 0, mk_key(0x100u), {mk_marker(1, 0, 10.f, 11.f, 12.f)});
    OpApplied a4;
    CHECK(host.apply_op(0, 3, mk_xform(1, 50.f, 51.f, 52.f), 5.0, a4, r1));
    CHECK(guest.session_mismatch(a4.hdr));
    CHECK(guest.on_session_reset(a4.hdr, unsaved_dk));
    CHECK(!guest.synced && guest.effective_draft_key(unsaved_dk) == save_as_dk);

    // Direct snapshot transition to S5 without prior reset also clears old leases and undo depth
    for (const auto& c : host.build_snapshot_chunks())
        CHECK(guest.on_snapshot_chunk(c));
    guest.my_undo_depth = 3;
    guest.on_lease(LeaseMsg{{save_as_dk, 0x44444444u}, LEASE_GRANT, 1, 2, 5000}, 6.0);
    host.start_session(save_as_dk, 0x55555555u, 1, 0, mk_key(0x100u), {mk_marker(1, 0, 50.f, 51.f, 52.f)});
    for (const auto& c : host.build_snapshot_chunks())
        CHECK(guest.on_snapshot_chunk(c));
    CHECK(guest.synced && guest.state.session_id == 0x55555555u);
    CHECK(guest.my_undo_depth == 0 && guest.leases.holder_of(1, 6.5) == kNoOwner);

    // 5. Isolation: an unsynced guest on a DIFFERENT saved adventure rejects host's unrelated draft_key
    GuestEditor other_guest;
    other_guest.my_id = 2;
    CHECK(!other_guest.accepts_draft_key(saved_dk, other_dk));
    CHECK(!other_guest.on_session_reset(DraftHeader{saved_dk, 0x66666666u}, other_dk));
    CHECK(!other_guest.has_adopted_draft_key);
}

static void test_drag_lease_release_on_commit_and_settle()
{
    DragLeaseTracker dlt;
    HostEditor host;
    host.start_session(mk_key(0x10u), 0x100u, 1, 0, mk_key(0x1u), {mk_marker(10, 0, 1.f, 2.f, 3.f)});

    // 1. Interactive drag acquires lease on uid 10; CommitHistory releases it immediately (no 5 s lockup!)
    CHECK(host.leases.acquire_or_renew(10, 1, 10.0));
    dlt.on_drag_acquire(10, 10.0);
    dlt.on_drag_touch(10, 10.1);
    CHECK(dlt.take_settled(10.5).empty()); // still dirty -> do not release mid-drag

    // CommitHistory flushes transform and calls take_all()
    dlt.on_transform_flushed(10, 10.2);
    auto rel = dlt.take_all();
    CHECK(rel.size() == 1 && rel[0] == 10 && dlt.empty());
    CHECK(host.leases.release(10, 1));
    // Another peer (actor 2) can edit immediately at t=10.21 instead of waiting until t=15.0
    CHECK(host.leases.can_edit(2, 10, 10.21));
    CHECK(host.leases.acquire_or_renew(10, 2, 10.21));

    // Remaining TTL helper for LEASE_DENY reports accurate remaining ms
    uint32_t rem = host.leases.remaining_ttl_ms(10, 12.21);
    CHECK(rem >= 2900 && rem <= 3100);
    CHECK(host.leases.remaining_ttl_ms(10, 20.0) == 0);

    // 2. Idle flush without CommitHistory: releases after kDragLeaseReleaseSettleSec (0.25 s) quiet period
    dlt.on_drag_acquire(20, 20.0);
    dlt.on_transform_flushed(20, 20.20);
    CHECK(dlt.take_settled(20.40).empty()); // only 0.20 s since flush
    // Resuming drag before settle keeps lease active
    dlt.on_drag_touch(20, 20.40);
    CHECK(dlt.take_settled(21.0).empty());
    // Final flush -> quiet >= 0.25 s -> released!
    dlt.on_transform_flushed(20, 21.10);
    auto settled = dlt.take_settled(21.36);
    CHECK(settled.size() == 1 && settled[0] == 20 && dlt.empty());

    // 3. forget() drops denied/deleted marker without scheduling a release
    dlt.on_drag_acquire(30, 30.0);
    dlt.forget(30);
    CHECK(dlt.empty() && dlt.take_all().empty());
}

static void test_lease_deny_and_op_reject_snapback()
{
    wire::Key3 dk = mk_key(0x42u);
    uint32_t sid = 0x77777777u;
    HostEditor host;
    host.start_session(dk, sid, 1, 0, mk_key(0x1u), {mk_marker(7, 0, 10.f, 20.f, 30.f, 1.5f)});

    GuestEditor guest;
    guest.my_id = 1;
    for (const auto& c : host.build_snapshot_chunks())
        CHECK(guest.on_snapshot_chunk(c));

    // 1. Host denies guest's lease request because host (owner 0) holds marker 7 with 3.2 s left
    LeaseMsg deny{{dk, sid}, LEASE_DENY, 7, 0, 3200};
    guest.on_lease(deny, 10.0);
    CHECK(guest.leases.holder_of(7, 10.5) == 0);
    CHECK(!guest.leases.can_edit(1, 7, 10.5));
    // Expires after 3.2 s (at t=13.3)
    CHECK(guest.leases.holder_of(7, 13.3) == kNoOwner);
    // Authoritative marker is available to snap back the guest's diverged local transform
    const MarkerRec* auth = guest.find_marker(7);
    CHECK(auth != nullptr && std::fabs(auth->pos[0] - 10.f) < 1e-4f && std::fabs(auth->pos[1] - 20.f) < 1e-4f &&
          std::fabs(auth->pos[2] - 30.f) < 1e-4f && std::fabs(auth->scale - 1.5f) < 1e-4f);

    // 2. LEASE_DENY with ttl_ms == 0 (0.8.0 host compat) falls back to kLeaseTtlSec (5.0 s)
    LeaseMsg deny_zero_ttl{{dk, sid}, LEASE_DENY, 7, 2, 0};
    guest.on_lease(deny_zero_ttl, 20.0);
    CHECK(guest.leases.holder_of(7, 24.5) == 2);
    CHECK(guest.leases.holder_of(7, 25.1) == kNoOwner);

    // 3. K_OP_REJECT for OP_TRANSFORM_MARKER with REJECT_LEASE_HELD records holder AND yields authoritative snap-back
    OpReject rej_lease{{dk, sid}, 55, REJECT_LEASE_HELD, 0, 7};
    MarkerRec snap;
    CHECK(guest.on_op_reject(rej_lease, 30.0, &snap));
    CHECK(guest.leases.holder_of(7, 31.0) == 0);
    CHECK(!guest.leases.can_edit(1, 7, 31.0));
    CHECK(snap.uid == 7 && std::fabs(snap.pos[0] - 10.f) < 1e-4f && std::fabs(snap.scale - 1.5f) < 1e-4f);

    // 4. K_OP_REJECT with REJECT_INVALID still snaps back authoritative transform without fabricating a lease
    guest.leases.force_clear(7);
    OpReject rej_inv{{dk, sid}, 56, REJECT_INVALID, kNoOwner, 7};
    MarkerRec snap2;
    CHECK(guest.on_op_reject(rej_inv, 31.0, &snap2));
    CHECK(guest.leases.holder_of(7, 31.0) == kNoOwner);
    CHECK(snap2.uid == 7 && std::fabs(snap2.pos[2] - 30.f) < 1e-4f);

    // 5. K_OP_REJECT for unknown uid returns false (nothing to snap back)
    OpReject rej_missing{{dk, sid}, 57, REJECT_NOT_FOUND, kNoOwner, 999};
    CHECK(!guest.on_op_reject(rej_missing, 31.0, &snap2));
}

static void test_undo_router_mixed_synced_and_native()
{
    UndoRouter ur;

    // Empty state: falls through to native when shared depth is 0
    CHECK(ur.peek_undo(0, false) == UndoRoute::None);
    CHECK(ur.peek_undo(0, true) == UndoRoute::Native);
    CHECK(ur.peek_redo(0, true) == UndoRoute::Native);

    // Mixed edit sequence: Synced(marker) -> Native(terrain) -> Synced(drag) -> Native(goal)
    ur.mark_synced_gesture();
    CHECK(ur.on_commit_history() == UndoRoute::Synced);
    CHECK(ur.on_commit_history() == UndoRoute::Native);
    ur.mark_synced_gesture();
    CHECK(ur.on_commit_history() == UndoRoute::Synced);
    CHECK(ur.on_commit_history() == UndoRoute::Native);

    uint8_t shared_ud = 2, shared_rd = 0;

    // Undo #1: top is Native (goal) -> routes to Native
    CHECK(ur.peek_undo(shared_ud, true) == UndoRoute::Native);
    ur.consume_undo(UndoRoute::Native);

    // Undo #2: top is Synced (drag) -> routes to Synced
    CHECK(ur.peek_undo(shared_ud, true) == UndoRoute::Synced);
    ur.consume_undo(UndoRoute::Synced);
    shared_ud--;
    shared_rd++;

    // Undo #3: top is Native (terrain) -> routes to Native
    CHECK(ur.peek_undo(shared_ud, true) == UndoRoute::Native);
    ur.consume_undo(UndoRoute::Native);

    // Undo #4: top is Synced (marker) -> routes to Synced
    CHECK(ur.peek_undo(shared_ud, true) == UndoRoute::Synced);
    ur.consume_undo(UndoRoute::Synced);
    shared_ud--;
    shared_rd++;

    // Undo #5: both exhausted -> None
    CHECK(ur.peek_undo(shared_ud, false) == UndoRoute::None);

    // Redo sequence unwinds in exact reverse: Synced -> Native -> Synced -> Native
    CHECK(ur.peek_redo(shared_rd, true) == UndoRoute::Synced);
    ur.consume_redo(UndoRoute::Synced);
    shared_ud++;
    shared_rd--;

    CHECK(ur.peek_redo(shared_rd, true) == UndoRoute::Native);
    ur.consume_redo(UndoRoute::Native);

    CHECK(ur.peek_redo(shared_rd, true) == UndoRoute::Synced);
    ur.consume_redo(UndoRoute::Synced);
    shared_ud++;
    shared_rd--;

    CHECK(ur.peek_redo(shared_rd, true) == UndoRoute::Native);
    ur.consume_redo(UndoRoute::Native);
    CHECK(ur.peek_redo(shared_rd, false) == UndoRoute::None);

    // Rollback on rejected guest OP_UNDO / OP_REDO
    ur.mark_synced_gesture();
    ur.on_commit_history();
    shared_ud++;
    CHECK(ur.peek_undo(shared_ud, true) == UndoRoute::Synced);
    ur.consume_undo(UndoRoute::Synced);
    ur.rollback_undo();
    CHECK(ur.peek_undo(shared_ud, true) == UndoRoute::Synced);

    // Console synced op does NOT poison a following native CommitHistory
    ur.clear();
    ur.on_synced_op_committed_directly();
    CHECK(ur.on_commit_history() == UndoRoute::Native);
    CHECK(ur.peek_undo(1, true) == UndoRoute::Native);

    // Stale Synced entry (e.g. shared op already undone via console or rejected) falls through to Native underneath
    ur.clear();
    CHECK(ur.on_commit_history() == UndoRoute::Native);
    ur.mark_synced_gesture();
    CHECK(ur.on_commit_history() == UndoRoute::Synced);
    CHECK(ur.peek_undo(0, true) == UndoRoute::Native);
    ur.consume_undo(UndoRoute::Native);
    CHECK(ur.undo_stack.empty() && ur.redo_stack.size() == 1 && ur.redo_stack.back() == UndoRoute::Native);
}

int main()
{
    test_family_gating();
    test_leases_conflict_and_expiry();
    test_host_ops_and_per_player_undo_redo();
    test_snapshot_chunking_and_guest_reconciliation();
    test_wire_roundtrips_and_bounds();
    test_session_reset_and_guest_resync();
    test_drag_lease_release_on_commit_and_settle();
    test_lease_deny_and_op_reject_snapback();
    test_undo_router_mixed_synced_and_native();
    printf("adv_editor_tests: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
