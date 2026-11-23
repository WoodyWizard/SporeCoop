// Cell: the guest's side (the host's state as mirrors, the heals of the own cell and of a mirror, the join after a
// load, guest_tick; the guest's own pool while the host is away, BUG-0205); moved out of cellsync.cpp (C8 of
// 0.7.15, BUG-1183).
#include "cellsync_internal.h"

namespace cellsync {

// ---------------- guest ----------------
static void* ref_for(uint32_t inst)
{
    return inst ? skg::cdecl_call<void*>(a_RefCreate, inst, (void*)a_Serializer) : nullptr;
}

void destroy_local(uint32_t idx)
{
    callguard::Hold our_destroy(g_our_destroy);
    modes::cell_destroy(idx);
}

// BUG-1248: the game's "Goldie's demise" tutorial (0xE79720, the first part; docs/re/cell.md#cell-goldie-cutscene) runs
// in each game on its own: it makes two cells next to the own avatar (Goldie and the cell that hunts it, both +0x354),
// aims the cell camera at Goldie (cCellUI +0xE0), sets G+0x518C = 1 and an event of type 0x24 (G+0x5194, +0x28 Goldie,
// +0x2C the hunter; 0xE5F160 keeps the camera on G+0x516C, the visible part-token cell, every frame) and plays the
// cinematic goldies_demise. On a guest both cells are local cells the host does not have: the stray purge below
// removed them with the next state of the host (0.1 s), so the guest's cutscene showed its letterbox over empty water
// (the camera on a cell that was gone; near the host it jumped to the mirror of the host's own token ~2 s later).
// Now they are kept while that tutorial runs (G+0x518C set), as the guest's own mate is: the guest sees its tutorial
// as a single player does. After it (0xE794F0 clears G+0x518C) they go as before.
// Old (`coop.fix cell.goldie_keep off`): purged.
bool g_fix_goldie_keep = true;
COOP_FIX("cell.goldie_keep", "BUG-1248", g_fix_goldie_keep, "cell: a guest's own first-part tutorial keeps its two cells (its cutscene showed empty water: they were removed as cells the host does not have)");
// the event of the running tutorial (type 0x24, the one G+0x5198 waits on; G+0x5194 names it too until another event
// takes that slot) in the event pool G+0x54; null: none
static uint8_t* tutorial_event(char* why = nullptr, size_t n = 0)
{
    uint8_t* g = G();
    if (!g || !F<uint8_t>(g, 0x518C)) {
        if (why)
            snprintf(why, n, "off");
        return nullptr;
    }
    uint8_t* pool = g + 0x54;
    uint8_t* data = F<uint8_t*>(pool, 0);
    uint32_t high = F<uint32_t>(pool, 0x10), size = F<uint32_t>(pool, 0x14);
    for (uint32_t at : {0x5198u, 0x5194u}) {
        uint32_t ei = F<uint32_t>(g, at);
        if (!ei || !data || size < 0x30 || (ei & 0xFFFF) >= high)
            continue;
        uint8_t* e = data + (size_t)(ei & 0xFFFF) * size;
        if (skg::readable(e, 0x30) && F<uint32_t>(e, 0) == ei && F<uint32_t>(e, 0x24) == 0x24)
            return e;
        if (why)
            snprintf(why, n, "G+%X=%08X size=%X high=%u e0=%08X type=%X", at, ei, size, high,
                     skg::readable(e, 0x30) ? F<uint32_t>(e, 0) : 0u, skg::readable(e, 0x30) ? F<uint32_t>(e, 0x24) : 0u);
    }
    return nullptr;
}
// the tutorial's two cells while it runs: its event's (+0x28 Goldie, +0x2C the hunter), else the ones seen at its
// start (the camera's cell and the cell hunting it, remembered); false: no tutorial
static uint32_t g_tut_goldie, g_tut_hunter;
bool tutorial_cells(uint32_t* goldie, uint32_t* hunter, char* why, size_t n)
{
    uint8_t* g = G();
    if (!g || !F<uint8_t>(g, 0x518C)) {
        g_tut_goldie = g_tut_hunter = 0;
        if (why)
            snprintf(why, n, "off");
        return false;
    }
    if (uint8_t* e = tutorial_event(why, n)) {
        g_tut_goldie = F<uint32_t>(e, 0x28);
        g_tut_hunter = F<uint32_t>(e, 0x2C);
    } else if (!g_tut_goldie) { // 0xE79720 aims the camera at Goldie and makes the hunter chase it (+0x22C, +0x230 = 1000)
        uint8_t* ui = a_CellUI ? *(uint8_t**)a_CellUI : nullptr;
        uint32_t f = ui && skg::readable(ui + 0xE0, 4) ? F<uint32_t>(ui, 0xE0) : 0;
        if (f && modes::cell_at(f) && !g_host_of.count(f)) {
            g_tut_goldie = f;
            for_each_cell([&](uint8_t* c, uint32_t idx) {
                if (idx != f && F<uint32_t>(c, kChase) == f && F<uint8_t>(c, kKeepAlive) && !g_host_of.count(idx))
                    g_tut_hunter = idx;
            });
        }
    }
    *goldie = g_tut_goldie;
    *hunter = g_tut_hunter;
    return g_tut_goldie != 0;
}
bool tutorial_cell(uint8_t* c, uint32_t idx)
{
    uint32_t goldie = 0, hunter = 0;
    return c && idx && tutorial_cells(&goldie, &hunter, nullptr, 0) && (idx == goldie || idx == hunter);
}

void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), n = r.u8();
    if (!r.ok())
        return;
    switch (g_rounds.on_chunk(cycle, chunk, g_fix_purge)) {
    case cell_logic::Rounds::OLD_CHUNK: // overtaken by a newer round: it would start that one again
        g_n.late_chunks++;
        return;
    case cell_logic::Rounds::NEW_ROUND:
        g_cycle_seen.clear();
        break;
    default:
        break;
    }
    double now = H->time_seconds();
    // BUG-0511: the host's world scale after the n cells (63 bytes each), when this host sends it
    constexpr size_t kRec = 4 + 4 + 12 + 1 + 4 + 12 + 8 + 16 + 1 + 1;
    float host_scale = 0.f, own_scale = world_scale();
    if (r.left() >= n * kRec + 4) {
        memcpy(&host_scale, r.cur() + n * kRec, 4);
        if (!std::isfinite(host_scale) || host_scale <= 1e-6f || host_scale > 1e9f)
            host_scale = 0.f;
    }
    float k = host_scale > 0 && own_scale > 0 ? host_scale / own_scale : 1.f;
    if (k != 1.f)
        g_n.scale_conv++;
    for (int i = 0; i < n && r.ok(); i++) {
        uint32_t hidx = r.u32(), inst = r.u32(), key[3];
        key[0] = r.u32(); key[1] = r.u32(); key[2] = r.u32();
        int level = r.u8();
        float size = r.f32();
        net::Vec3 p, v;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        v.x = r.f32(); v.y = r.f32(); v.z = 0;
        net::Quat q;
        q.x = r.f32(); q.y = r.f32(); q.z = r.f32(); q.w = r.f32();
        int hp = r.u8();
        uint8_t flags = r.u8();
        if (!r.ok())
            return;
        if (g_fix_finite && (!pose_finite(p, v.x, v.y, size) || !std::isfinite(q.x) || !std::isfinite(q.y) ||
                             !std::isfinite(q.z) || !std::isfinite(q.w))) {
            if (!g_n.nonfinite_dropped++)
                H->log(M, SK_WARN, "coop: cell: host cell %08X arrived with a pose that is not finite: not mirrored (BUG-0292)", hidx);
            continue; // not in this round: its mirror goes with the round's purge
        }
        if (g_fix_eat_once && g_eaten.has(hidx, now)) {
            g_n.eaten_skipped++; // BUG-0636: we ate it, the host's state has not dropped it yet
            continue;
        }
        g_cycle_seen.insert(hidx);
        auto& m = g_mirrors[hidx];
        m.pos = p; m.vel = v; m.rot = q; m.size = size; m.hp = hp; m.seen = now;
        m.product = (flags & 4) != 0; // BUG-0958
        m.own_product = (flags & 8) != 0; // BUG-1513
        m.scale = host_scale; // the pose stays in the host's scale; drawn in ours
        size *= k;
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c && cells_live() >= kMirrorCap) {
            g_n.mirror_capped++; // BUG-0295: the pool is full enough (an older host sends more)
            continue;
        }
        if (!c) { // dead ones too: the host shows a creature it killed until it is gone
            uint32_t idx = modes::cell_create({p.x * k, p.y * k, p.z * k}, ref_for(inst), level, key, &q);
            c = modes::cell_at(idx);
            if (c) {
                if (m.local && g_fix_hostof) // the old local cell is gone (BUG-0088)
                    g_host_of.erase(m.local);
                m.local = idx;
                g_host_of[idx] = hidx;
                g_n.created++;
            }
        }
        if (c) {
            F<uint8_t>(c, kAiLock) = 1;    // passive: no behaviour, the host decides
            F<uint8_t>(c, kKeepAlive) = 1; // we own the lifetime, not the tile streaming
            F<float>(c, kSize) = size;
            F<float>(c, kTargetSize) = size;
            F<int>(c, kHp) = hp;
            if ((flags & 3) && !F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead)) {
                // the host's creature died: the same death here (effect, dying / dead animation,
                // locks, event 6 -> dead). Its loot is the host's (SpawnLoot is off on the guest).
                {
                    callguard::Hold replay(g_replay);
                    o_KillCell(c, 0, nullptr, false, 6, 0.f);
                }
                g_n.death_replays++;
            }
        }
    }
    g_n.cells_peak = std::max(g_n.cells_peak, cells_live());
    if (chunk + 1 < chunks)
        return;
    // The host no longer has these: remove the mirrors. Only after a round of which every chunk
    // arrived (the state is unreliable): a lost or late chunk deleted its 14 cells here, and they
    // came back 0.1 s later as new cells (BUG-0032). Mirrors not listed for a second go anyway.
    bool complete = g_rounds.complete(chunks, g_fix_purge);
    if (!complete)
        g_n.rounds_partial++;
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (complete ? !g_cycle_seen.count(it->first) : now - it->second.seen > 1.0) {
            if (it->second.local) {
                g_host_of.erase(it->second.local);
                destroy_local(it->second.local);
                g_n.destroyed++;
            }
            it = g_mirrors.erase(it);
        } else
            ++it;
    }
    // stray local gameplay cells (spawned before the sync started, loot, ...) do not exist on the host.
    // Not the guest's own mate (G+0x51D4, spawned by its mating call 0xE760F0): it was removed here
    // within a second, so a guest could never reach it and open its own editor.
    std::vector<uint32_t> stray;
    uint32_t mate = mate_idx();
    g_n.products_live = 0;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (!g_host_of.count(idx) && !modes::cell_is_puppet(c) && F<uint8_t>(c, 0x110))
            g_n.products_live++; // tests (BUG-0716): our poison clouds and the like
        if (g_fix_goldie_keep && tutorial_cell(c, idx)) {
            g_n.goldie_kept++; // BUG-1248: our own tutorial's two cells (the cinematic looks at them)
            return;
        }
        if (!g_host_of.count(idx) && !modes::cell_is_puppet(c) && idx != mate)
            stray.push_back(idx);
    });
    for (uint32_t idx : stray) {
        destroy_local(idx);
        g_n.purged++;
    }
}

// BUG-0299: our own cell back at its last finite position, its soft-body particles on a small ring
// around it and at rest (their old positions are NaN too)
static net::Vec3 g_own_good;
bool g_own_good_set;
// BUG-0307: the heal of BUG-0299 fixed the position and the particles' positions and velocities only.
// A NaN left in the rotation / shape matrices, the size or the particles' goal offsets (the rest shape
// of the shape matching) made the next soft-body step NaN again: the own cell went NaN 26 times in
// 6 minutes in lab 20261030-104149. Now every non-finite field is repaired, and any non-finite field (not only
// the position) starts the heal.
static float g_own_good_size = 0.5f, g_own_good_tsize = 0.5f, g_own_good_mass = 1.f, g_own_good_imass = 1.f;
// A soft body's particles on a ring of 0.4 x its size about `p` (at rest), its centre there; the goal offsets of the
// shape matching too when they are not finite. The own cell's heal (BUG-0299 / BUG-0307) and a mirror's (BUG-1006).
static void soft_ring(uint8_t* a, const net::Vec3& p)
{
    float r = std::isfinite(F<float>(a, kSize)) ? std::clamp(F<float>(a, kSize) * 0.4f, 0.05f, 5.f) : 0.3f;
    int n = F<int>(a, 0x250);
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (F<uint32_t>(a, 0x248) && parts && n > 1 && n <= 64)
        for (int i = 0; i < n; i++) {
            uint32_t k = F<uint32_t>(a, 0x254 + 4 * i);
            if (k >= 1500)
                continue;
            auto* q = (float*)(parts + 4 + 12 * (size_t)k);
            auto* v = (float*)(parts + 0x4654 + 12 * (size_t)k);
            float ang = 6.2831853f * (float)i / (float)n;
            q[0] = p.x + r * std::cos(ang);
            q[1] = p.y + r * std::sin(ang);
            q[2] = p.z;
            v[0] = v[1] = v[2] = 0;
            auto* goal = (float*)(parts + 0x11944 + 12 * (size_t)k); // the rest offset of the shape matching
            if (!fin(goal, 3)) {
                goal[0] = r * std::cos(ang);
                goal[1] = r * std::sin(ang);
                goal[2] = 0;
            }
        }
    auto* pos = (float*)(a + 0x4C);
    auto* prev = (float*)(a + 0x84);
    pos[0] = prev[0] = p.x;
    pos[1] = prev[1] = p.y;
    pos[2] = prev[2] = p.z;
    F<uint16_t>(a, 0x48) |= 4;
    F<uint16_t>(a, 0x4A)++;
}

// BUG-1006: a mirror whose own soft body went NaN in our cell step (its particles, its centre, its fit) is rebuilt at
// the host's pose: guest_tick moved it by `want - pos` (NaN for every particle) and h_Soft turned it by an angle from
// its NaN matrix, so it stayed NaN for good (and a NaN mirror makes the own cell NaN through their contacts, BUG-0292).
// Off: no repair (the old path). The switch is defined at h_Soft.
static bool heal_mirror(uint8_t* c, const net::Vec3& want, float size)
{
    unsigned mask = cell_nonfinite(c);
    if (!g_fix_mirror_nan_heal || !mask || !views::finite_pos(want))
        return false;
    if (mask & NF_SIZE) {
        float sz = std::isfinite(size) && size > 0 ? size : 0.5f;
        F<float>(c, kSize) = sz;
        F<float>(c, kTargetSize) = sz;
    }
    if (mask & NF_MASS) {
        F<float>(c, 0xB8) = 1.f;
        F<float>(c, 0xBC) = 1.f;
    }
    static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (mask & NF_ROT)
        memcpy(c + kRot, id, sizeof id); // the host's matrix is written right after
    if (mask & NF_SHAPE)
        memcpy(c + 0x24, id, sizeof id);
    if (mask & NF_ACC) {
        auto* acc = (float*)(c + 0x1C4);
        acc[0] = acc[1] = acc[2] = 0;
    }
    soft_ring(c, want);
    stop_at(c, want);
    if (g_n.mirror_nan_heals++ < 20)
        H->log(M, SK_WARN, "coop: cell: mirror c%u had non-finite fields 0x%X: rebuilt at the host's pose (%.1f %.1f) (BUG-1006)",
               F<uint32_t>(c, 0), mask, want.x, want.y);
    return true;
}

void heal_own(uint8_t* a, const net::Vec3& me, bool bad)
{
    unsigned mask = cell_nonfinite(a);
    if (!bad && !mask) {
        g_own_good = me;
        g_own_good_set = true;
        g_own_good_size = F<float>(a, kSize);
        g_own_good_tsize = F<float>(a, kTargetSize);
        g_own_good_mass = F<float>(a, 0xB8);
        g_own_good_imass = F<float>(a, 0xBC);
        return;
    }
    if (!g_own_good_set)
        return;
    const net::Vec3 p = !bad ? me : g_own_good;
    if (mask & NF_SIZE) {
        F<float>(a, kSize) = g_own_good_size;
        F<float>(a, kTargetSize) = g_own_good_tsize;
    }
    if (mask & NF_MASS) {
        F<float>(a, 0xB8) = g_own_good_mass;
        F<float>(a, 0xBC) = g_own_good_imass;
    }
    if (mask & NF_ROT) {
        static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        memcpy(a + kRot, id, sizeof id);
        F<uint16_t>(a, 0x48) |= 2;
    }
    if (mask & NF_SHAPE) {
        static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        memcpy(a + 0x24, id, sizeof id);
    }
    soft_ring(a, p);
    stop_at(a, p);
    if (!g_n.nan_heals++ || (mask && g_n.nan_heals <= 20))
        H->log(M, SK_WARN, "coop: cell: our own cell went back to its last finite position (%.1f %.1f), non-finite fields 0x%X (BUG-0299, BUG-0307)",
               p.x, p.y, mask);
}

// BUG-0352: after a world load the guest's cell starts at the pool's start, and only an own-cell
// rebuild (creations.cpp apply_own_cell -> join_partner) moved it to the host: a load without one
// (every load after the first of a session, BUG-0349) left it up to 130 m away, outside the host's
// tiles. Every new own avatar (a load, an editor, a rebuild) now goes next to the host once the host's
// cell is known here (its state says the same place), at most 30 s after the avatar appeared.
uint32_t g_join_avatar;    // the avatar index this was decided for
double g_join_since = -1;  // when that avatar appeared (-1: decided)
double g_join_check = -1;  // when to check that the move held
static net::Vec3 g_join_to;       // where it was put
int g_join_tries;
static void join_after_load(double now)
{
    uint32_t idx = avatar_idx();
    if (idx != g_join_avatar) {
        g_join_avatar = idx;
        g_join_since = now;
        g_join_check = -1;
        g_join_tries = 0;
    }
    uint8_t* a = avatar();
    if (!a)
        return;
    if (g_join_check >= 0 && now >= g_join_check) { // the game's hatch can put the cell back at the start
        g_join_check = -1;
        if (net::dist(pos_of(a), g_join_to) > 15.f && g_join_tries < 3)
            g_join_since = now - 2.0; // again
    }
    if (g_join_since < 0 || now - g_join_since < 2.0)
        return;
    if (now - g_join_since > 30.0) {
        g_join_since = -1; // the host never showed up here: leave it
        return;
    }
    net::Vec3 at;
    float d = -1;
    // not during the hatch (the game's input lock after a load or a rebuild), not knocked out, and
    // only once the host's state is here
    if (F<uint8_t>(a, kDying) || g_downed || coop_ui::input_lock_now() > 0 || nearest_partner(1e9f, &at, &d).empty())
        return;
    if (!spawn_spot(&at, &d, false, 2.5f, 1.f)) // BUG-0792: d is the host's distance with the fix
        return;
    g_join_since = -1;
    if (d < 15.f && !(g_fix_spawn_slots && crowded(a, 2.f)))
        return;
    g_join_to = at;
    stop_at(a, g_join_to);
    g_join_tries++;
    g_join_check = now + 3.0;
    g_n.joins_load++;
    H->log(M, SK_INFO, "cell: our new cell joined the host (%.0f m away after the load, try %d)", d, g_join_tries);
}

void guest_tick(double now)
{
    join_after_load(now);
    flush_food();
    own_probe("guest tick");
    shared_parts_scan(now);
    // BUG-0292 diagnostics: the moment our own cell's position stops being finite, and what was near
    static bool own_bad;
    if (uint8_t* a = avatar()) {
        net::Vec3 me = pos_of(a);
        bool bad = !views::finite_pos(me);
        if (bad && !own_bad) {
            size_t odd = 0;
            for (auto& [hidx, m] : g_mirrors)
                odd += !pose_finite(m.pos, m.vel.x, m.vel.y, m.size);
            H->log(M, SK_WARN, "coop: cell: our own cell's position is not finite (BUG-0292): %u mirrors, %u of them with a pose that is not finite, %u dropped so far",
                   (unsigned)g_mirrors.size(), (unsigned)odd, g_n.nonfinite_dropped);
        }
        own_bad = bad;
        heal_own(a, me, bad);
    }
    for (auto& [hidx, m] : g_mirrors) {
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c)
            continue;
        double age = std::min(now - m.seen, 0.5);
        net::Vec3 want{m.pos.x + m.vel.x * (float)age, m.pos.y + m.vel.y * (float)age, m.pos.z};
        // BUG-0511: in our world scale now (it moves for ~3 s after a level change, at another moment
        // than the host's)
        float own = m.scale > 0 ? world_scale() : 0.f, k = own > 0 ? m.scale / own : 1.f;
        want.x *= k;
        want.y *= k;
        want.z *= k;
        net::Vec3 cur = pos_of(c);
        int n = F<int>(c, 0x250);
        if (F<uint32_t>(c, 0x248) && n > 1) {
            // a soft body (docs/re/cell.md 6): +0x4C and +0x90 are the mean of its particles, writes
            // to them alone are undone, and a centre written away from its particles blew the body
            // up (mirrors flew off to 1e10 m on the guest). Move the particles with it and keep them
            // still: the pose is the host's, extrapolated along its velocity.
            heal_mirror(c, want, m.size * k);
            modes::cell_move(c, want, true);
            auto* acc = (float*)(c + 0x1C4);
            acc[0] = acc[1] = acc[2] = 0;
            g_n.soft_moves++;
        } else if (F<uint32_t>(c, 0x248) && n == 1 && F<int>(c, 0x24C) >= 1) {
            // a mirror whose soft body we freed (BUG-0355): placed as a soft one is (the
            // host's pose, at rest), not steered by velocity into the cells around it
            set_pos(c, want);
            auto* v = (float*)(c + kVel);
            v[0] = v[1] = 0.f;
            g_n.bare_moves++;
        } else {
            if (net::dist(cur, want) > 3.f)
                set_pos(c, want);
            auto* v = (float*)(c + kVel);
            v[0] = m.vel.x * k + (want.x - cur.x) * 2.f;
            v[1] = m.vel.y * k + (want.y - cur.y) * 2.f;
        }
        modes::quat_to_mat(m.rot, (float*)(c + kRot));
        F<uint16_t>(c, kFlags) |= 2;
        F<uint8_t>(c, kAiLock) = 1;
    }
    own_probe("mirror moves");
}
// BUG-0205: the guest's own pool while the host is away (see g_fix_away)
void solo_reset()
{
    if (g_solo_food[0] || g_solo_food[1]) {
        g_n.solo_dropped++;
        trace::ev("eat", "solo dropped", "food kept while the host was away dropped (meat %d plant %d): the stage or "
                  "the session ended first", g_solo_food[0], g_solo_food[1]);
    }
    g_solo = g_solo_owed = false;
    g_solo_food[0] = g_solo_food[1] = 0;
    solo_refill_stop(); // BUG-1408
}

static void solo_start()
{
    if (g_fix_session_reset)
        g_food_q.clear(); // BUG-1002: the host's totals queued before it went away
    // the host's pool is not simulated now: its mirrors become cells of our game, with their AI.
    // Dying and dead ones keep the locks of their death.
    const uint32_t released0 = g_n.solo_released;
    for (auto& [h, m] : g_mirrors) {
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c || F<uint8_t>(c, kDying) || F<uint8_t>(c, kDead))
            continue;
        F<uint8_t>(c, kAiLock) = 0;
        F<uint8_t>(c, kKeepAlive) = 0; // the tile streaming owns them again
        g_n.solo_released++;
    }
    g_mirrors.clear();
    g_host_of.clear();
    g_touched.clear();
    g_swallowed.clear();
    g_cycle_seen.clear();
    g_rounds.reset();
    if (g_pend_food) { // not sent yet: kept with the rest
        g_solo_food[g_pend_plant ? 1 : 0] += g_pend_food;
        g_pend_food = 0;
    }
    g_solo = true;
    g_solo_owed = false;
    g_n.solo_times++;
    // BUG-1408: none of the host's cells here: the tiles made while we mirrored it get their own cells
    solo_refill_start(g_n.solo_released - released0, own_world::elsewhere());
    trace::line("cell", "host away: the guest plays its own pool (%u cells released)", g_n.solo_released);
}

// the host is back and its pool runs again (its first state or total arrived): what we ate meanwhile
// goes to it as meals; the totals it sends back count them (BUG-0051)
void solo_send()
{
    g_solo_owed = false;
    for (int k = 0; k < 2; k++)
        while (g_solo_food[k] > 0) {
            int n = std::min(g_solo_food[k], 999); // the host refuses one meal of 1000 or more
            g_solo_food[k] -= n;
            net::Writer w;
            w.u32((uint32_t)n);
            w.u8((uint8_t)k);
            w.u32(g_meals.sent(n));
            C.sess->send_reliable_to(0, net::MSG_CELL_FOOD, w);
            g_n.solo_sent += n;
            trace::ev("eat", "solo sent", "host back: food +%d %s eaten while it was away sent", n, k ? "plant" : "meat");
        }
    g_solo_food[0] = g_solo_food[1] = 0;
}

void solo_tick()
{
    bool want = is_guest() && in_cell() &&
                ((C.host_away && C.host_away()) || (g_fix_solo_elsewhere && (own_world::elsewhere() || own_world::host_left())));
    if (want == g_solo)
        return;
    if (want) {
        solo_start();
        return;
    }
    if (is_guest() && in_cell()) { // the host is back: its pool replaces ours at its first state
        g_solo = false;
        solo_refill_stop(); // BUG-1408
        g_solo_owed = g_solo_food[0] || g_solo_food[1];
        trace::line("cell", "host back: the host's pool replaces the guest's at its first state");
    } else if (is_guest() && solo_keep_in_editor())
        return; // BUG-1433: our own editor: the food eaten alone is kept, decided again back in Cell
    else
        solo_reset();
}

} // namespace cellsync
