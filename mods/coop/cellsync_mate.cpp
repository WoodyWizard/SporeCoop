// Cell: the mate a guest's mating call brings, in the host's game (BUG-1246).
//
// The players' video: the guest called a mate ("do you see the male? on the left"), the host swam right next to it and
// saw nobody. The mating call (0xE760F0, docs/re/cell.md section 12) makes the mate in the caller's own pool
// (G+0x51D4); the Cell pool goes only host -> guests (MSG_CELL_STATE), so a guest's mate existed in the guest's game
// alone. On (`cell.partner_mate`): a guest sends its mate's pose 5 times a second (MSG_CELL_MATE kind 0, and kind 1 when
// it is gone); the host keeps a passive copy of it in its pool (no behaviour, invulnerable, its lifetime ours, never the
// host's own mate) at that pose, and sends it to the other guests as one of its cells, never back to its owner. The
// meeting and the editor stay the owner's (its own game). Off: the mate is the caller's game's only (0.7.15).
#include "cellsync_internal.h"

namespace cellsync {

static bool g_fix_partner_mate = true;
COOP_FIX("cell.partner_mate", "BUG-1246", g_fix_partner_mate,
         "cell: the mate a guest's mating call brings is in the host's game (and the other guests') too");

namespace {
constexpr double kMateSend = 0.2;  // guest: pose sends per second: 5
constexpr double kMateStale = 2.0; // host: a copy without a pose for this long goes

double g_mate_sent_at = -100;
bool g_mate_was = false;           // guest: the last send had a live mate
unsigned g_mate_sent, g_mate_gone_sent;

struct MateCopy {
    uint32_t local = 0; // the copy's pool index here (0: none yet)
    net::Vec3 pos{}, vel{};
    net::Quat rot{};
    float size = 0, scale = 0;
    uint32_t inst = 0, key[3] = {};
    int level = 0;
    double seen = 0;
};
std::unordered_map<int, MateCopy> g_copies; // host: player -> the copy of its mate
unsigned g_copies_made, g_copies_gone, g_poses_in;

void* ref_for_inst(uint32_t inst)
{
    return inst && a_RefCreate ? skg::cdecl_call<void*>(a_RefCreate, inst, (void*)a_Serializer) : nullptr;
}

void drop_copy(MateCopy& m, const char* why, int id)
{
    if (m.local && in_cell() && modes::cell_at(m.local))
        destroy_local(m.local);
    if (m.local) {
        g_copies_gone++;
        H->log(M, SK_INFO, "cell: player %d's mate copy %08X removed: %s (BUG-1246)", id, m.local, why);
    }
    m.local = 0;
}
} // namespace

unsigned g_mate_elsewhere_refused; // host (BUG-1507): poses of a guest that plays another world
unsigned g_mate_elsewhere_held;    // guest (BUG-1507): poses not sent while we play another world

// host: the pool index of the copy of player `id`'s mate (0: none); is `idx` a copy of anyone's mate
uint32_t mate_copy_of(int id)
{
    auto it = g_copies.find(id);
    return it != g_copies.end() ? it->second.local : 0;
}
bool is_mate_copy(uint32_t idx)
{
    for (auto& [id, m] : g_copies)
        if (m.local && m.local == idx)
            return true;
    return false;
}

// CELL-3 (BUG-1432): the copy was protected by +0x111 alone, which the game's lethal bite (Bite -> BreakOrSwallow),
// a mouth's chunks (EatChunk, KillIfDead) and a swallow do not ask (docs/re/cell.md#cell-guest-hit-game): the host's
// own cell, its NPCs and the other guests (MSG_CELL_HIT / MSG_CELL_EAT) killed it, every kill dropped the game's meat and
// mate_in made it again 0.2 s later (free meat again and again), and the guest's mate died on the host only. On: the
// copy is a picture like a partner's puppet: no collision pair, no damage, no death, no swallow, a guest's hit or kill
// on it refused. Off: 0.7.16.
static bool g_fix_mate_copy_safe = true;
COOP_FIX("cell.mate_copy_safe", "BUG-1432", g_fix_mate_copy_safe,
         "cell: the copy of a partner's mate cannot be bitten, eaten or killed in the host's game (it gave free meat again and again)");
unsigned g_mate_copy_refused; // tests: attacks on a copy refused
bool mate_copy_cell(const uint8_t* c)
{
    if (!g_fix_mate_copy_safe || !c || g_copies.empty() || !is_host() || !in_cell())
        return false;
    return is_mate_copy(F<uint32_t>((uint8_t*)c, 0));
}
bool mate_copy_refuse(const uint8_t* c)
{
    if (!mate_copy_cell(c))
        return false;
    g_mate_copy_refused++;
    return true;
}

// host: a guest's MSG_CELL_MATE (in Cell with an avatar)
void mate_in(int from, net::Reader& r)
{
    uint8_t kind = r.u8();
    if (!r.ok() || !is_host())
        return;
    if (kind == 1 || !g_fix_partner_mate) {
        if (auto it = g_copies.find(from); it != g_copies.end()) {
            drop_copy(it->second, kind == 1 ? "gone in its game" : "cell.partner_mate off", from);
            g_copies.erase(it);
        }
        return;
    }
    if (kind != 0)
        return;
    if (g_fix_elsewhere_spend && own_world::peer_elsewhere((uint8_t)from)) { // BUG-1507: its mate is in its own world
        g_mate_elsewhere_refused++;
        if (auto it = g_copies.find(from); it != g_copies.end()) {
            drop_copy(it->second, "its player plays another world", from);
            g_copies.erase(it);
        }
        return;
    }
    MateCopy n;
    n.inst = r.u32();
    n.key[0] = r.u32(); n.key[1] = r.u32(); n.key[2] = r.u32();
    n.level = r.u8();
    n.size = r.f32();
    n.pos.x = r.f32(); n.pos.y = r.f32(); n.pos.z = r.f32();
    n.vel.x = r.f32(); n.vel.y = r.f32();
    n.rot.x = r.f32(); n.rot.y = r.f32(); n.rot.z = r.f32(); n.rot.w = r.f32();
    n.scale = r.f32();
    if (!r.ok() || !pose_finite(n.pos, n.vel.x, n.vel.y, n.size) || !std::isfinite(n.rot.w) || !std::isfinite(n.scale))
        return;
    g_poses_in++;
    MateCopy& m = g_copies[from];
    uint32_t keep = m.local;
    m = n;
    m.local = keep;
    m.seen = H->time_seconds();
    if (m.local && !modes::cell_at(m.local))
        m.local = 0; // a level rebuild freed it: made again
    if (!m.local) {
        float own = world_scale(), k = m.scale > 0 && own > 0 ? m.scale / own : 1.f;
        uint32_t idx = modes::cell_create({m.pos.x * k, m.pos.y * k, m.pos.z * k}, ref_for_inst(m.inst), m.level, m.key, &m.rot);
        if (uint8_t* c = modes::cell_at(idx)) {
            m.local = idx;
            F<uint8_t>(c, kAiLock) = 1;    // passive: the owner's game moves it
            F<uint8_t>(c, kKeepAlive) = 1; // its lifetime is ours, not the tile streaming's
            F<uint8_t>(c, kInvuln) = 1;    // nobody here eats a partner's mate
            g_copies_made++;
            H->log(M, SK_INFO, "cell: player %d's mate is in our pool now as %08X (model %08X, BUG-1246)", from, idx, m.key[0]);
        }
    }
}

// both, every frame in Cell with an avatar and a session
void mate_tick(double now)
{
    if (is_guest()) {
        uint8_t* m = g_fix_partner_mate ? modes::cell_at(mate_idx()) : nullptr;
        bool live = m && !F<uint8_t>(m, kDying) && !F<uint8_t>(m, kDead);
        if (live && not_host_world()) { // BUG-1507: our mate is in our own world, not in the host's
            live = false;
            g_mate_elsewhere_held++;
        }
        if (live && now - g_mate_sent_at >= kMateSend) {
            g_mate_sent_at = now;
            void* ref = F<void*>(m, kRes);
            auto* key = (uint32_t*)(m + kModelKey);
            net::Vec3 p = pos_of(m);
            auto* v = (float*)(m + kVel);
            net::Quat q;
            modes::mat_to_quat((float*)(m + kRot), q);
            net::Writer w;
            w.u8(0);
            w.u32(ref ? *(uint32_t*)ref : 0);
            w.u32(key[0]); w.u32(key[1]); w.u32(key[2]);
            w.u8((uint8_t)F<int>(m, kLevel));
            w.f32(F<float>(m, kSize));
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(v[0]); w.f32(v[1]);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.f32(world_scale());
            C.sess->send_unreliable_to(0, net::MSG_CELL_MATE, w);
            g_mate_sent++;
            g_mate_was = true;
        } else if (!live && g_mate_was) {
            g_mate_was = false;
            net::Writer w;
            w.u8(1);
            C.sess->send_reliable_to(0, net::MSG_CELL_MATE, w);
            g_mate_gone_sent++;
        }
        return;
    }
    if (!is_host())
        return;
    for (auto it = g_copies.begin(); it != g_copies.end();) {
        MateCopy& m = it->second;
        if (now - m.seen > kMateStale) {
            drop_copy(m, "no pose for 2 s", it->first);
            it = g_copies.erase(it);
            continue;
        }
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c) {
            m.local = 0;
            ++it;
            continue;
        }
        double age = std::min(now - m.seen, 0.5);
        float own = m.scale > 0 ? world_scale() : 0.f, k = own > 0 ? m.scale / own : 1.f;
        net::Vec3 want{(m.pos.x + m.vel.x * (float)age) * k, (m.pos.y + m.vel.y * (float)age) * k, m.pos.z * k};
        net::Vec3 cur = pos_of(c);
        if (F<uint32_t>(c, 0x248) && F<int>(c, 0x250) > 1) { // a soft body: moved with its particles (as a mirror)
            modes::cell_move(c, want, true);
            auto* acc = (float*)(c + 0x1C4);
            acc[0] = acc[1] = acc[2] = 0;
        } else {
            if (net::dist(cur, want) > 3.f)
                set_pos(c, want);
            auto* v = (float*)(c + kVel);
            v[0] = m.vel.x * k + (want.x - cur.x) * 2.f;
            v[1] = m.vel.y * k + (want.y - cur.y) * 2.f;
        }
        F<float>(c, kSize) = m.size * k;
        F<float>(c, kTargetSize) = m.size * k;
        modes::quat_to_mat(m.rot, (float*)(c + kRot));
        F<uint16_t>(c, kFlags) |= 2;
        F<uint8_t>(c, kAiLock) = 1;
        F<uint8_t>(c, kKeepAlive) = 1;
        F<uint8_t>(c, kInvuln) = 1;
        ++it;
    }
}

// the Cell pool is gone (the stage left, a load): the copies' indices with it
void mate_forget()
{
    g_copies.clear();
    g_mate_was = false;
}
// a player left / the session ended: its copy goes
void mate_player_left(int id)
{
    if (auto it = g_copies.find(id); it != g_copies.end()) {
        drop_copy(it->second, "the player left", id);
        g_copies.erase(it);
    }
}
void mate_session_end()
{
    for (auto& [id, m] : g_copies)
        drop_copy(m, "the session ended", id);
    g_copies.clear();
    g_mate_was = false;
}

std::string mate_status()
{
    std::string o;
    char b[200];
    snprintf(b, sizeof b, " mate_sent=%u mate_gone_sent=%u copies=%u made=%u gone=%u poses_in=%u copy_refused=%u", g_mate_sent,
             g_mate_gone_sent, (unsigned)g_copies.size(), g_copies_made, g_copies_gone, g_poses_in, g_mate_copy_refused);
    o = b;
    for (auto& [id, m] : g_copies) {
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        net::Vec3 p = c ? pos_of(c) : net::Vec3{};
        snprintf(b, sizeof b, " copy(player=%d idx=%08X alive=%d gfx=%d at=(%.1f %.1f) model=%08X hp=%d dying=%d)", id, m.local,
                 c ? 1 : 0, c && F<uint32_t>(c, 0x248) ? 1 : 0, p.x, p.y, c ? F<uint32_t>(c, kModelKey) : 0u,
                 c ? F<int>(c, kHp) : 0, c && (F<uint8_t>(c, kDying) || F<uint8_t>(c, kDead)) ? 1 : 0);
        o += b;
    }
    return o;
}

} // namespace cellsync
