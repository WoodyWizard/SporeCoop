// Cell: the guards in the game's cell physics (the contact solver, BUG-0307; the soft-body particle pool and the
// cells' graphics pool, BUG-0338 / BUG-0355; the soft-body step of a mirror; the broadphase sweep, BUG-0337 /
// BUG-0718); moved out of cellsync.cpp (C8 of 0.7.15, BUG-1183).
#include "cellsync_internal.h"

namespace cellsync {

// ---- the contact solver (BUG-0307, docs/re/cell.md#cell-contact-nan) ----
// The collision pairs (HandleCollisionPair 0xE7F140 -> executor 0xE7E7F0 -> AddContact 0xE66F30) leave
// contacts in a pool; after the pair loop Collisions 0xE7F260 runs ContactSolvePass 0xE654D0: 10 rounds of
// ContactSolve 0xE58C90 over every contact of this frame, each one a sequential impulse along the normal
// (+0x18) with the effective mass +0x30 = 1 / (inverse mass A + B of the +0x36C roots), the target +0x38 and
// the accumulated impulse +0x28, applied by ApplyImpulse 0xE560D0 to the root's velocity +0x90 and to every
// soft-body particle's velocity: the only game code between the pairs and our guest tick that writes those
// two fields and not the positions (the own cell's mask 0x104 of BUG-0307).
ContactPass_t o_ContactPass;
ContactSolve_t o_ContactSolve;
uintptr_t a_ContactPass, a_ContactSolve;
// Two collision spheres closer than 0.004 (squared distance under 1.5e-5) leave the contact normal unset in
// the game's sphere tests: 0xE73BB0 (no soft body on either cell) passes the first cell's centre as the
// "normal" (|n| = its distance from the origin, ~100 m), 0xE6AD80 / 0xE73620 / 0xE738F0 whatever their stack
// slot held. The solver assumes |n| = 1: every round changes the closing speed by |n|^2 times what it meant,
// the 10 rounds overshoot by ~|n|^20, and the cells fly off at hundreds of m/s or their velocities overflow to
// inf / NaN (runtime: 2 cells on one centre 884 m/s, 4 cells inf, lab 20261106-111709). The fix: a contact's
// normal is made a unit vector before it is solved (the unset one keeps its direction, a zero or non-finite
// one becomes +x), and a contact whose impulse is still not finite is not applied.
static void contact_unit(Contact* k)
{
    float* n = k->normal;
    float n2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
    if (std::isfinite(n2) && std::fabs(n2 - 1.f) < 0.02f)
        return;
    g_n.contacts_unit++;
    if (std::isfinite(n2) && n2 > 1e-12f) {
        float r = 1.f / std::sqrt(n2);
        n[0] *= r, n[1] *= r, n[2] *= r;
    } else
        n[0] = 1.f, n[1] = n[2] = 0.f;
}

static uint8_t* contact_root(uint8_t* c) // 0xE58C50: the head of the +0x36C chain
{
    for (int i = 0; c && i < 64; i++) {
        uint8_t* p = modes::cell_at(F<uint32_t>(c, 0x36C));
        if (!p || p == c)
            return c;
        c = p;
    }
    return c;
}
// what ContactSolve would apply (its own arithmetic, in floats); false when any input or the impulse is not finite
static bool contact_finite(const Contact* k, uint8_t* ra, uint8_t* rb, float* out)
{
    const float* va = (const float*)(ra + kVel);
    const float* vb = (const float*)(rb + kVel);
    float rel = (vb[2] - va[2]) * k->normal[2] + (vb[1] - va[1]) * k->normal[1] + (vb[0] - va[0]) * k->normal[0];
    float x = (k->target - rel) * k->mass + k->accum;
    float acc = 0.f <= x ? x : 0.f;
    float imp = acc - k->accum;
    *out = imp;
    return fin(k->normal, 3) && std::isfinite(k->accum) && std::isfinite(k->mass) && std::isfinite(k->target) && fin(va, 3) &&
           fin(vb, 3) && std::isfinite(imp) && std::isfinite(F<float>(ra, 0xBC)) && std::isfinite(F<float>(rb, 0xBC));
}
static void contact_log(const char* what, const Contact* k, uint8_t* a, uint8_t* b, uint8_t* ra, uint8_t* rb, float imp)
{
    if (++g_n.contact_logs > 30)
        return;
    auto one = [](uint8_t* c, char* o, size_t n) {
        snprintf(o, n, "c%u %s size=%.3f mass=%g inv=%g vel=(%g %g) pos=(%.2f %.2f) parts=%d gfx=%d chain=%u nf=0x%X%s%s",
                 F<uint32_t>(c, 0), kind_of(c), F<float>(c, kSize), F<float>(c, 0xB8), F<float>(c, 0xBC), F<float>(c, kVel),
                 F<float>(c, kVel + 4), F<float>(c, 0x4C), F<float>(c, 0x50), F<int>(c, 0x250), F<uint32_t>(c, 0x248) ? 1 : 0,
                 F<uint32_t>(c, 0x36C), cell_nonfinite(c), c == avatar() ? " OWN" : "", modes::cell_is_puppet(c) ? " puppet" : "");
    };
    char sa[300], sb[300], sra[300] = "", srb[300] = "";
    one(a, sa, sizeof sa);
    one(b, sb, sizeof sb);
    if (ra != a)
        one(ra, sra, sizeof sra);
    if (rb != b)
        one(rb, srb, sizeof srb);
    H->log(M, SK_WARN,
           "coop: cell: contact not finite (BUG-0307) %s: normal=(%g %g %g) depth=%g accum=%g mass=%g mass2=%g target=%g "
           "impulse=%g frame=%u | A %s%s%s | B %s%s%s",
           what, k->normal[0], k->normal[1], k->normal[2], k->depth, k->accum, k->mass, k->mass2, k->target, imp, k->frame, sa,
           *sra ? " root " : "", sra, sb, *srb ? " root " : "", srb);
}
int SK_CDECL h_ContactSolve(Contact* k)
{
    uint8_t* a = k && in_cell() ? modes::cell_at(k->a) : nullptr;
    uint8_t* b = a ? modes::cell_at(k->b) : nullptr;
    if (!a || !b)
        return o_ContactSolve(k);
    g_n.contact_solves++;
    uint8_t *ra = contact_root(a), *rb = contact_root(b), *av = avatar();
    float imp = 0;
    contact_unit(k);
    bool st2 = g_stack2_frames > 0 && (F<uint32_t>(a, 0) == g_stack2_a || F<uint32_t>(b, 0) == g_stack2_a);
    if ((st2 || (g_stack_frames > 0 && (F<uint32_t>(a, 0) == g_stack_idx || F<uint32_t>(b, 0) == g_stack_idx))) && g_n.stack_logs++ < 12) {
        contact_finite(k, ra, rb, &imp);
        net::Vec3 fa = first_sphere(a), fb = first_sphere(b);
        char w[96];
        snprintf(w, sizeof w, "stacked (RE, first spheres %.5f apart, pairs %u)", std::hypot(fa.x - fb.x, fa.y - fb.y, fa.z - fb.z), g_n.stack_pairs);
        contact_log(w, k, a, b, ra, rb, imp);
    }
    if (contact_finite(k, ra, rb, &imp))
        return o_ContactSolve(k);
    bool own = av && (ra == av || rb == av);
    g_n.contacts_bad++;
    g_n.contacts_bad_own += own;
    contact_log("not applied", k, a, b, ra, rb, imp);
    if (!std::isfinite(k->accum))
        k->accum = 0.f; // the game's clamp keeps a contact's accumulated impulse; a NaN one stays out of the next round
    return 1;
}
void SK_CDECL h_ContactPass()
{
    own_probe("before the contact solver");
    o_ContactPass();
    own_probe("contact solver");
}
// SoftBodyStep 0xE5A430 (cdecl void(cell*, resData*, float dt), docs/re/cell.md 10): a soft body
// takes its rotation +0x5C from its particles (shape matching) every cell step, after the AI. A
// mirror's particles are only moved, never turned, by guest_tick: their cloud kept whatever
// orientation the guest's collisions and the shape matching gave it, so the rotation the guest wrote
// was replaced before every drawing and collision step and the host's NPC spun on the guest (and its
// mouth and body faced elsewhere than on the host). For a mirror the
// particle cloud is turned about its centre onto the host's facing right after the fit, and the
// host's matrix is written: what is drawn and what bites or is bitten here faces as on the host.
// ---- soft-body particles (BUG-0338, docs/re/cell.md#cell-particle-pool) ----
uintptr_t a_PartAlloc, a_PartFree;
int(SK_CDECL* o_PartAlloc)(uint8_t*, const float*, const float*, float, int);
void(SK_CDECL* o_PartFree)(uint8_t*, int);
uint32_t g_part_spare;
uintptr_t g_part_site[1500]; // RE (BUG-0355): the return address of each slot's last allocation
int particles_used()
{
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    return parts ? F<int>(parts, 0x1BD54) : -1;
}
// the game's own particle state: cells whose list holds -1 among real particles, and whether the
// free-list head points into the pool (it must while some slot below the high water is free)
void particle_state(int* minus1_cells, int* head_bad)
{
    *minus1_cells = 0;
    *head_bad = 0;
    uint8_t* g = G();
    uint8_t* parts = g ? F<uint8_t*>(g, 0x4108) : nullptr;
    uint8_t* data = g ? F<uint8_t*>(g, 0x1C) : nullptr;
    if (!parts || !data)
        return;
    int used = F<int>(parts, 0x1BD54), high = F<int>(parts, 0x1BD58), head = F<int>(parts, 0x1BD5C);
    *head_bad = used < high && (head < 0 || head >= kParticles);
    uint32_t n = F<uint32_t>(g, 0x1C + 0x10);
    for (uint32_t slot = 0; slot < n && slot < 4096; slot++) {
        uint8_t* c = data + (size_t)slot * 0x398;
        if ((F<uint32_t>(c, 0) & 0xFFFF) != slot || !F<uint32_t>(c, 0x248))
            continue;
        int k = F<int>(c, 0x250);
        if (k > 1 && k <= 64)
            for (int i = 0; i < k; i++)
                if (F<int>(c, 0x254 + 4 * i) < 0) {
                    (*minus1_cells)++;
                    break;
                }
    }
}
// ---- the cells' graphics pool (BUG-0355, docs/re/cell.md#cell-gfx-pool) ----
// A foreground cell gets a soft body when it comes into the active area (SoftBodyLod 0xE78C00 ->
// CreateSoftBody 0xE6D8F0): its particles first, then a graphics object from the cell graphics pool
// (cCellGFX +0x168). That pool has a fixed capacity; full, it hands out 0, the cell keeps +0x248 = 0
// with its particles in the list, and the next frame's LOD pass asks again: 4 to 64 particles lost
// per such cell per frame (DestroyCell / FreeSoftBody free only with +0x248 set), the graphics
// object Get(0) set up again each time. A 4-player guest's crowd of mirrors on screen filled the
// particle pool in seconds that way, then every cell shared BUG-0338's spare slot (it went NaN and
// spread), and the frame took 300 ms for good. The fix: no CreateSoftBody while the graphics pool
// is full; the cell stays as it was (no soft body, not drawn: what the game meant), the LOD asks
// again next frame and gets one when there is room.
uintptr_t a_CreateSoft, a_GfxPtr;
void(SK_CDECL* o_CreateSoft)(uint32_t, int);
uint32_t* gfx_pool() // cObjectPool_ fields as uint32: [3] live, [6] capacity
{
    uint8_t* gfx = a_GfxPtr ? *(uint8_t**)a_GfxPtr : nullptr;
    return gfx ? (uint32_t*)(gfx + 0x168) : nullptr;
}
void SK_CDECL h_CreateSoft(uint32_t idx, int arg)
{
    uint32_t* gp = gfx_pool();
    if (gp && gp[3] > g_n.gfx_peak)
        g_n.gfx_peak = gp[3];
    if (gp && gp[6] && gp[3] >= gp[6]) {
        if (!g_n.soft_refused++)
            H->log(M, SK_WARN, "cell: the cell graphics pool is full (%u of %u): no soft body for new cells on screen until there is room (BUG-0355)",
                   gp[3], gp[6]);
        // a new cell has count 0 (the pool's memset) until its first CreateSoftBody: give it the
        // game's own "no particles" form (count 1, -1), as CreateSoftBody does for tier 0
        if (uint8_t* c = modes::cell_at(idx))
            if (F<int>(c, 0x250) < 1) {
                F<int>(c, 0x250) = 1;
                F<int>(c, 0x254) = -1;
                g_n.soft_count0++;
            }
        return;
    }
    g_n.soft_made++;
    o_CreateSoft(idx, arg);
}

// ---- room in the particle pool (BUG-0355, docs/re/cell.md#cell-gfx-pool) ----
// BUG-0338's spare slot is one particle for every cell that asks a full pool: a 4-player guest with
// 900 mirrors filled the pool in ordinary play, hundreds of cells then held slot 1499 (up to 64
// times each, the own cell 56 of its 64 when it grew in the end-of-Cell cinematic), every soft-body
// step pulled it towards its own cell, it went NaN within seconds and the NaN spread through every
// cell holding it (their centres are the mean of their particles; guest_tick moves a mirror's
// particles by the NaN difference). The fix: a full pool frees the soft bodies of the cells
// farthest from our own (not our cell, not a partner's puppet, not the cell asking), down to
// kRoomTo used, and hands out a real slot; such a cell keeps its tier and becomes the game's own
// "no particles" form (+0x250 = 1, +0x254 = -1, as a model type 0 / 5 cell has), moved without a
// soft body. A cell left holding the spare (no cell to take from) is freed the same way once a
// frame, and the spare is put back to a finite place.
constexpr int kRoomTo = 1300; // used particles after a full pool made room (our cell at tier 3 takes 64)
// A mirror whose soft body the full pool freed was moved like a mirror without one (tier 0): steered by
// its velocity towards the host's pose (set_pos only when over 3 m off). A mirror made from a full
// pool lands on top of its neighbours and the cells around ours; steered, they shoved our own cell
// 8-18 m in a few seconds (cell_particles: 2 of 5 runs, BUG-0355 follow-up). Placed as a soft one
// is (the host's pose, at rest), they do not (own cell at most 1.1 m per round in 6 runs).
// the cell's soft body to the game's "no particles" form; returns the particles given back
static int strip_soft_body(uint8_t* parts, uint8_t* c)
{
    int n = F<int>(c, 0x250);
    if (!F<uint32_t>(c, 0x248) || n < 2 || n > 64)
        return 0;
    int freed = 0;
    for (int i = 0; i < n; i++) {
        int k = F<int>(c, 0x254 + 4 * i);
        if (k >= 0 && k < kSpare) {
            o_PartFree(parts, k);
            freed++;
        }
        F<int>(c, 0x254 + 4 * i) = -1;
    }
    F<int>(c, 0x250) = 1;
    g_n.part_stripped++;
    return freed;
}
// a full pool: free the soft bodies of the farthest cells (not ours, no puppet, not `asking`)
void make_particle_room(uint8_t* parts, int asking)
{
    uint8_t* av = avatar();
    net::Vec3 me = av ? pos_of(av) : net::Vec3{0, 0, 0};
    std::vector<std::pair<float, uint8_t*>> takers;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        int n = F<int>(c, 0x250);
        if (!F<uint32_t>(c, 0x248) || n < 2 || n > 64 || (idx & 0xFFFF) == ((uint32_t)asking & 0xFFFF) ||
            modes::cell_is_puppet(c))
            return;
        net::Vec3 p = pos_of(c);
        float d = std::hypot(p.x - me.x, p.y - me.y);
        takers.push_back({std::isfinite(d) ? d : 1e30f, c}); // a NaN cell first
    });
    std::sort(takers.begin(), takers.end(), [](auto& x, auto& y) { return x.first > y.first; });
    g_n.part_room_runs++;
    for (auto& [d, c] : takers) {
        if (F<int>(parts, 0x1BD54) <= kRoomTo)
            break;
        strip_soft_body(parts, c);
    }
}
// once a frame: no cell but ours (or a puppet) keeps the spare, the spare stays finite
void spare_tick()
{
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (!parts || !g_part_spare)
        return;
    uint8_t* av = avatar();
    for_each_cell([&](uint8_t* c, uint32_t) {
        int n = F<int>(c, 0x250);
        if (!F<uint32_t>(c, 0x248) || n < 2 || n > 64 || modes::cell_is_puppet(c))
            return;
        for (int i = 0; i < n; i++)
            if (F<int>(c, 0x254 + 4 * i) == kSpare) {
                strip_soft_body(parts, c);
                g_n.part_spare_freed++;
                return;
            }
    });
    auto* q = (float*)(parts + 4 + 12 * kSpare);
    auto* v = (float*)(parts + 0x4654 + 12 * kSpare);
    if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2]) || !std::isfinite(v[0]) || !std::isfinite(v[1])) {
        net::Vec3 me = av ? pos_of(av) : net::Vec3{0, 0, 0};
        q[0] = std::isfinite(me.x) ? me.x : 0.f, q[1] = std::isfinite(me.y) ? me.y : 0.f, q[2] = 0.f;
        v[0] = v[1] = v[2] = 0.f;
    }
}
int SK_CDECL h_PartAlloc(uint8_t* parts, const float* pos, const float* vel, float w, int cell)
{
    int used = parts ? F<int>(parts, 0x1BD54) : 0;
    if (used > (int)g_n.particles_peak)
        g_n.particles_peak = (uint32_t)used;
    // The game's own last slot is 1499 too: with no free slot below the high water it hands out
    // index = the new count (slots 1..1499; 1499 at 1498 used), so BUG-0338's spare could be a
    // real cell's particle (and its free was dropped: one slot lost). The pool counts as full at
    // 1498 used and slot 1499 is the spare alone (BUG-0355).
    int full_at = kParticles - 2;
    if (parts && used >= full_at && o_PartFree) {
        make_particle_room(parts, cell);
        used = F<int>(parts, 0x1BD54);
    }
    if (!parts || used < full_at) {
        int k = o_PartAlloc(parts, pos, vel, w, cell);
        if (k >= 0 && k < kParticles) // RE (BUG-0355): who took each slot (coop.pool orphans)
            g_part_site[k] = (uintptr_t)__builtin_return_address(0);
        return k;
    }
    for (int j = 0; j < 3; j++) { // the spare stands where the asking cell is, at rest
        ((float*)(parts + 4 + 12 * kSpare))[j] = pos ? pos[j] : 0.f;
        ((float*)(parts + 0x4654 + 12 * kSpare))[j] = vel ? vel[j] : 0.f;
    }
    if (!g_part_spare++)
        H->log(M, SK_WARN, "cell: the soft-body particle pool is full (%d of %d): the spare slot instead of -1 (BUG-0338)", used, kParticles);
    return kSpare;
}
void SK_CDECL h_PartFree(uint8_t* parts, int i)
{
    if (i < 0 || i >= kSpare) {
        g_n.part_bad_frees++;
        return;
    }
    o_PartFree(parts, i);
}

// tests (BUG-0604, BUG-0307): the own cell's velocity and its particles' velocities NaN (mask 0x104, the
// guest's level-up in lab 20261105-142923), written after its soft-body step for <n> frames
int g_nan_vel_frames;
static void nan_velocity(uint8_t* c)
{
    float nan = std::nanf("");
    auto* v = (float*)(c + kVel);
    v[0] = v[1] = nan;
    int n = F<int>(c, 0x250);
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (F<uint32_t>(c, 0x248) && parts && n > 1 && n <= 64)
        for (int i = 0; i < n; i++) {
            uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
            if (k < 1500)
                for (int j = 0; j < 2; j++)
                    ((float*)(parts + 0x4654 + 12 * (size_t)k))[j] = nan;
        }
}
// BUG-1006 (heal_mirror below): a guest's mirror whose soft body went NaN
bool g_fix_mirror_nan_heal = true;
COOP_FIX("cell.mirror_nan_heal", "BUG-1006", g_fix_mirror_nan_heal, "cell: a guest's mirror whose soft body went NaN is rebuilt at the host's pose, and the mirror turn skips a NaN angle");
Soft_t o_Soft;
void SK_CDECL h_Soft(uint8_t* c, void* res, float dt)
{
    bool own = c && c == avatar();
    if (own)
        own_probe("before the soft body");
    o_Soft(c, res, dt);
    if (own && g_nan_vel_frames > 0) { // tests (BUG-0604): the level-up's NaN velocity inside the cell step
        g_nan_vel_frames--;
        nan_velocity(c);
    }
    if (own)
        own_probe("soft body");
    if (!c || !g_mirror_fixes || !is_guest() || !in_cell() || g_mirrors.empty())
        return;
    auto hit = g_host_of.find(F<uint32_t>(c, 0));
    if (hit == g_host_of.end())
        return;
    auto mit = g_mirrors.find(hit->second);
    int n = F<int>(c, 0x250);
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (mit == g_mirrors.end() || !parts || !F<uint32_t>(c, 0x248) || n < 2 || n > 64)
        return;
    float want[9];
    modes::quat_to_mat(mit->second.rot, want);
    auto* m = (float*)(c + kRot);
    // row-vector convention (goal = rest * M + centre): the image of the x axis is row 0
    float d = std::atan2(want[1], want[0]) - std::atan2(m[1], m[0]);
    float cs = std::cos(d), sn = std::sin(d);
    if (g_fix_mirror_nan_heal && !(std::isfinite(cs) && std::isfinite(sn))) { // BUG-1006: a NaN matrix (guest_tick rebuilds it)
        g_n.mirror_nan_turns++;
        return;
    }
    float cx = 0, cy = 0;
    int used = 0;
    for (int i = 0; i < n; i++) {
        uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
        if (k >= 1500)
            continue;
        auto* q = (float*)(parts + 4 + 12 * (size_t)k);
        cx += q[0];
        cy += q[1];
        used++;
    }
    if (!used)
        return;
    cx /= used;
    cy /= used;
    auto turn = [&](float* v, float ox, float oy) {
        float x = v[0] - ox, y = v[1] - oy;
        v[0] = ox + x * cs - y * sn;
        v[1] = oy + x * sn + y * cs;
    };
    for (int i = 0; i < n; i++) {
        uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
        if (k >= 1500)
            continue;
        turn((float*)(parts + 4 + 12 * (size_t)k), cx, cy);       // position, about the centre
        turn((float*)(parts + 0x4654 + 12 * (size_t)k), 0, 0);    // velocity
        turn((float*)(parts + 0x11944 + 12 * (size_t)k), 0, 0);   // goal offset of the shape matching
    }
    memcpy(m, want, sizeof want);
    memcpy(c + 0x24, want, sizeof want); // the shape matrix the fit wrote with it (blend 0: the same)
    F<uint16_t>(c, kFlags) |= 2;
    g_n.soft_turns++;
    mit->second.soft_at = H->time_seconds();
    mit->second.soft_d = d;
}

// ---- the cell broadphase's sweep (BUG-0337, docs/re/cell.md#cell-sap-active-list) ----
// SweepPrune::UpdatePairs 0xBBB450 sweeps axis 0 with the open objects in an int[1024] on its
// stack and no bound check: the 1025th open object overwrites its return address (and the next
// ones the caller's frame, SimUpdate's), so `ret` jumps to a cell's broadphase id (0x196, 0x4D6...)
// every frame. The fix runs the same sweep with an open list on the heap: the same merges (the
// game's MergeAxis), the same pair test (the game's), the same pairs in the same order.
uintptr_t a_UpdatePairs, a_MergeAxis, a_Overlap;
void(__cdecl* o_UpdatePairs)(uint8_t*);
std::string sap_report(); // sweeps with over 1024 open objects, the most open at once, sweeps of ours

// BUG-0718 (class BUG-0432): SweepPrune::MergeAxis 0xBBAF10 merges the pending endpoints into the sorted
// array in place and parks the entries it displaces in a queue of 4096 endpoints on its stack, with no
// bound check (docs/re/cell.md#cell-broadphase-cap, #cell-sap-merge). Only entries of the sorted array are
// ever parked, so an array of at most 4096 entries cannot overrun it; above (more than ~2048 live cells)
// it can, and then faults at 0xBBB07B with the stack and the heap corrupted. The soft budgets
// (BUG-0295) keep the mod's own spawns below that; the game's loot and spawns are not bounded. This is
// the hard guard at the consumer: a sorted array over 4096 entries is merged here with the queue on the
// heap, with the game's result: removed entries (type -2) dropped, the added ones sorted by value and
// merged (an equal value after the old entry), each moved endpoint's index written back to its object
// record (+8 / +0xA min, +0xC / +0xE max, per axis), the new count returned. At most 2 x the object
// capacity entries are written (the array's size). Old (`coop.fix cell.sap_merge off`): the game's
// MergeAxis at every size.
bool g_fix_sap_merge = true;
COOP_FIX_UNSAFE("cell.sap_merge", "BUG-0718", g_fix_sap_merge, "cell: a broadphase axis with over 4096 endpoints (over ~2048 live cells) is merged with its queue on the heap, not in the game's 4096-entry stack buffer (it overran and faulted at 0xBBB07B)");
struct SapEp {
    int16_t type, obj;
    float v;
};
static_assert(sizeof(SapEp) == 8, "SAP endpoint");
static int merge_axis(uint8_t* s, SapEp* sorted, int count, SapEp* added, int nadd, int axis)
{
    static std::vector<SapEp> old, add; // reused: no allocation per frame
    old.clear();
    add.clear();
    for (int i = 0; i < count; i++)
        if (sorted[i].type != -2)
            old.push_back(sorted[i]);
    for (int i = 0; i < nadd; i++)
        if (added[i].type != -2)
            add.push_back(added[i]);
    std::stable_sort(add.begin(), add.end(), [](const SapEp& a, const SapEp& b) { return a.v < b.v; });
    uint8_t* objs = F<uint8_t*>(s, 0x14);
    int cap = 2 * F<int>(s, 4), objcap = F<int>(s, 4), k = 0;
    size_t i = 0, j = 0;
    while ((i < old.size() || j < add.size()) && k < cap) {
        const SapEp& e = j < add.size() && (i >= old.size() || add[j].v < old[i].v) ? add[j++] : old[i++];
        sorted[k] = e;
        if (e.obj >= 0 && e.obj < objcap)
            F<int16_t>(objs + 16 * (size_t)e.obj, (e.type == 0 ? 8 : 0xC) + 2 * axis) = (int16_t)k;
        k++;
    }
    g_n.merge_clipped += (uint32_t)(old.size() - i + add.size() - j);
    g_n.merge_ours++;
    g_n.merge_peak = std::max(g_n.merge_peak, (uint32_t)count);
    return k;
}
constexpr int kMergeQueue = 4096; // the game's stack queue: 0x8000 bytes of 8-byte endpoints

void __cdecl h_UpdatePairs(uint8_t* s)
{
    if (!s || !a_MergeAxis || !a_Overlap)
        return o_UpdatePairs(s);
    int n = 0;
    for (int axis = 0; axis < 2; axis++) {
        if (g_fix_sap_merge && F<int>(s, 0x20) > kMergeQueue)
            n = merge_axis(s, F<SapEp*>(s, 0x24 + 4 * axis), F<int>(s, 0x20), F<SapEp*>(s, 0x30 + 4 * axis), F<int>(s, 0x2C), axis);
        else
            n = skg::cdecl_call<int>(a_MergeAxis, s, F<void*>(s, 0x24 + 4 * axis), F<int>(s, 0x20), F<void*>(s, 0x30 + 4 * axis),
                                     F<int>(s, 0x2C), axis);
    }
    F<int>(s, 0x20) = n;
    F<int>(s, 0x2C) = 0;
    F<int>(s, 8) = 0;
    g_n.sap_sweeps++;
    static std::vector<int> open; // reused: no allocation per frame
    open.clear();
    uint32_t peak = 0;
    uint8_t* ep = F<uint8_t*>(s, 0x24);
    uint8_t* objs = F<uint8_t*>(s, 0x14);
    for (int i = 0; i < n; i++) {
        int id = F<int16_t>(ep, 8 * i + 2);
        uint8_t* o = objs + 16 * id;
        if (F<int16_t>(ep, 8 * i) == 0) {
            for (int other : open) {
                uint8_t* p = objs + 16 * other;
                if (skg::cdecl_call<uint8_t>(a_Overlap, s, 0, o, p) && F<int>(s, 8) != F<int>(s, 0)) {
                    int a = F<int>(o, 0), b = F<int>(p, 0);
                    int* pair = (int*)(F<uint8_t*>(s, 0xC) + 8 * (size_t)F<int>(s, 8));
                    pair[0] = std::min(a, b);
                    pair[1] = std::max(a, b);
                    F<int>(s, 8)++;
                }
            }
            open.push_back(id);
            peak = std::max(peak, (uint32_t)open.size());
        } else {
            auto it = std::find(open.begin(), open.end(), id);
            if (it != open.end()) {
                *it = open.back();
                open.pop_back();
            }
        }
    }
    g_n.sap_pairs = (uint32_t)F<int>(s, 8);
    g_n.sap_pair_cap = (uint32_t)F<int>(s, 0);
    g_n.sap_pairs_full += g_n.sap_pairs >= g_n.sap_pair_cap;
    g_n.sap_peak = std::max(g_n.sap_peak, peak);
    g_n.sap_left = std::max(g_n.sap_left, (uint32_t)open.size());
    if (peak > 1024 && !g_n.sap_over++) // the first one per session (the count is in `coop.pool sap`)
        H->log(M, SK_WARN, "cell: the broadphase sweep held %u open objects (the game's list holds 1024, BUG-0337)", peak);
    // objects still open after the sweep have their max endpoint before their min: a broken order
    static double s_told;
    if (open.size() > 64 && H->time_seconds() - s_told > 30) {
        s_told = H->time_seconds();
        H->log(M, SK_WARN, "cell: %u broadphase objects left open by the sweep (BUG-0337): %s", (unsigned)open.size(),
               sap_report().c_str());
    }
}

} // namespace cellsync
