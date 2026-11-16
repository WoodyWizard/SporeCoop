// Cell: the host's side (host_tick and the state it sends, MSG_CELL_STATE); moved out of cellsync.cpp (C8 of
// 0.7.15, BUG-1183).
#include "cellsync_internal.h"

namespace cellsync {

// ---------------- host ----------------
// BUG-0927: which cells a guest gets (the ones within 120 m of its own cell, its nearest 900) was decided with the
// view centres of the last tile pass (g_views, filled in the PopulateTiles hook). The game runs no tile pass while
// its simulation stands (a caption that waits for its check mark, a cinematic, the pause menu): in the 0.7.12 final
// check's campaign the host stood in a caption for 110 s, a guest swam 100 m on and still got the cells around the
// place it had been at (850 mirrors, none on its screen: 12 % of the host's 159 cells around it). The states are
// sent in every tick, so the centres are taken in every tick (the same registry, views::partners).
// Old (`coop.fix cell.send_centres off`): the tile pass's centres.
bool g_fix_send_centres = true;
COOP_FIX("cell.send_centres", "BUG-0927", g_fix_send_centres, "cell: a guest gets the host's cells around where it is now, also while the host's game stands in a caption, a cinematic or the pause menu (it got the cells around where it was when the host stopped)");
float g_send_centre_lag, g_send_centre_lag_max;
struct SendCell {
    uint8_t* c;
    uint32_t idx;
};
static void send_state(int dest, const std::vector<SendCell>& cells);

void heal_own(uint8_t* a, const net::Vec3& me, bool bad);
void host_tick(double now)
{
    own_probe("host tick");
    if (uint8_t* a = avatar()) // BUG-0299 on the host too
        heal_own(a, pos_of(a), !views::finite_pos(pos_of(a)));
    uint8_t* s = S();
    if (s && now - g_last_food > 0.5) {
        g_last_food = now;
        int32_t food = F<int32_t>(s, 0x1C), plant = F<int32_t>(s, 0x20);
        bool changed = food != g_food_sent || plant != g_plant_sent;
        // Each guest gets the total with the last of its meals in it (BUG-0051), when it changed, and
        // again every 2 s: a guest drops it while it loads or is in an editor, and a newcomer had none
        // until the total changed (BUG-0128). Old (`fix resend 0`): on a change of the total only.
        for (auto& pl : C.sess->players()) {
            if (pl.id == 0)
                continue;
            FoodTx& tx = g_food_tx[pl.id];
            uint32_t ack = g_food_ack.count(pl.id) ? g_food_ack[pl.id] : 0;
            bool due = g_fix_resend ? food != tx.food || plant != tx.plant || ack != tx.ack || now - tx.at > 2.0
                                    : changed || ack != tx.ack;
            if (!due)
                continue;
            tx = {food, plant, ack, now};
            net::Writer w;
            w.u32((uint32_t)food);
            w.u32((uint32_t)plant);
            w.u32(ack); // older guests read the two totals only
            w.u8((uint8_t)(C.pace_announce ? C.pace_announce() : 1)); // BUG-0510: the party's pace divisor
            C.sess->send_reliable_to(pl.id, net::MSG_CELL_FOOD, w);
        }
        g_food_sent = food;
        g_plant_sent = plant;
    }
    if (now - g_last_send < 0.1)
        return;
    g_last_send = now;
    uint8_t* av = avatar();
    if (!av)
        return;
    net::Vec3 me = pos_of(av);
    using E = SendCell;
    // BUG-0174: each guest gets the cells within 120 m of its own cell (the host populates around it
    // too, h_PopTiles); a guest without a centre yet: around the host.
    std::vector<E> all;
    // BUG-0927: the centres of this tick, not the tile pass's (see g_fix_send_centres)
    views::Set fresh;
    if (g_fix_send_centres) {
        fresh.set_host({me.x, me.y, 0.f});
        views::Centre pc[views::kMaxCentres];
        size_t n = views::partners(modes::kCell, H->time_seconds(), pc, views::kMaxCentres - 1);
        for (size_t i = 0; i < n; i++)
            fresh.add(pc[i].player, {pc[i].pos.x, pc[i].pos.y, 0.f});
        if (g_views.size() > 1 && fresh.size() > 1) { // diagnostics: how far the tile pass's centres are behind
            float worst = 0;
            for (auto& c : fresh.list())
                if (const views::Centre* o = c.player ? g_views.find(c.player) : nullptr)
                    worst = std::fmax(worst, std::hypot(o->pos.x - c.pos.x, o->pos.y - c.pos.y));
            g_send_centre_lag = worst;
            g_send_centre_lag_max = std::fmax(g_send_centre_lag_max, worst);
        }
    }
    const views::Set& vs = g_fix_send_centres ? fresh : g_views;
    bool per_guest = vs.size() > 1;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || !F<void*>(c, kRes))
            return;
        net::Vec3 p = pos_of(c);
        if (g_fix_finite && !pose_finite(p, F<float>(c, kVel), F<float>(c, kVel + 4), F<float>(c, kSize))) {
            if (!g_n.nonfinite_sent_skipped++)
                H->log(M, SK_WARN, "coop: cell: host cell %08X has a pose that is not finite: not sent (BUG-0292)", idx);
            return;
        }
        if (per_guest ? !vs.near_any(p.x, p.y, 120.f) && net::dist(p, me) > 120.f : net::dist(p, me) > 120.f)
            return;
        all.push_back({c, idx});
    });
    g_cycle++;
    for (auto& pl : C.sess->players()) {
        if (pl.id == 0)
            continue;
        if (g_fix_send_here && !own_world::peer_here((uint8_t)pl.id)) {
            g_n.send_held++; // BUG-0809
            continue;
        }
        const views::Centre* vc = per_guest ? vs.find((uint8_t)pl.id) : nullptr;
        std::vector<E> mine;
        const std::vector<E>* list = &all;
        if (vc) {
            for (auto& e : all) {
                net::Vec3 p = pos_of(e.c);
                if (std::hypot(p.x - vc->pos.x, p.y - vc->pos.y) <= 120.f)
                    mine.push_back(e);
            }
            list = &mine;
        }
        // BUG-0295: at most kSendCap cells per guest, the nearest to it (its pool and the game's
        // broadphase behind it hold ~2048 cells; 4 players together had ~2300 within 120 m)
        if (list->size() > kSendCap) {
            float cx = vc ? vc->pos.x : me.x, cy = vc ? vc->pos.y : me.y;
            std::vector<std::pair<float, size_t>> d;
            d.reserve(list->size());
            for (size_t i = 0; i < list->size(); i++) {
                net::Vec3 p = pos_of((*list)[i].c);
                d.push_back({(p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy), i});
            }
            std::nth_element(d.begin(), d.begin() + kSendCap, d.end());
            std::vector<E> closest;
            closest.reserve(kSendCap);
            for (size_t i = 0; i < kSendCap; i++)
                closest.push_back((*list)[d[i].second]);
            g_n.send_capped++;
            send_state(pl.id, closest);
            continue;
        }
        send_state(pl.id, *list);
    }
}

static uint32_t g_fake_idx, g_fake_inst; // tests (BUG-0455, coop.cell spawn ... <model>)
void set_fake_model(uint32_t idx, uint32_t inst)
{
    g_fake_idx = idx;
    g_fake_inst = inst;
}


static void send_state(int dest, const std::vector<SendCell>& cells)
{
    double now_s = H->time_seconds();
    if (now_s - g_sent_pruned > 5.0) {
        g_sent_pruned = now_s;
        for (auto it = g_sent_at.begin(); it != g_sent_at.end();)
            it = now_s - it->second.at > 20.0 ? g_sent_at.erase(it) : std::next(it);
    }
    for (auto& e : cells) {
        auto f = g_sent_at.find(e.idx);
        if (f == g_sent_at.end())
            g_sent_at[e.idx] = {now_s, now_s, pos_of(e.c), F<int>(e.c, kLevel), 1};
        else {
            f->second.at = now_s;
            f->second.pos = pos_of(e.c);
            f->second.sends++;
        }
    }
    size_t per = 14, chunks = std::max<size_t>(1, (cells.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        size_t from = k * per, to = std::min(cells.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            uint8_t* c = cells[i].c;
            void* ref = F<void*>(c, kRes);
            uint32_t inst = ref ? *(uint32_t*)ref : 0;
            auto* key = (uint32_t*)(c + kModelKey);
            net::Vec3 p = pos_of(c);
            auto* v = (float*)(c + kVel);
            net::Quat q;
            modes::mat_to_quat((float*)(c + kRot), q);
            w.u32(cells[i].idx);
            w.u32(inst);
            w.u32(g_fake_inst && cells[i].idx == g_fake_idx ? g_fake_inst : key[0]); w.u32(key[1]); w.u32(key[2]);
            w.u8((uint8_t)F<int>(c, kLevel));
            w.f32(F<float>(c, kSize));
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(v[0]); w.f32(v[1]);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.u8(cell_logic::hp_byte(F<int>(c, kHp))); // a negative hp is 0, not 255 (BUG-0160)
            // flag 4 (BUG-0958): a player's product (+0x110, the clouds of a player's poison part; older guests ignore it)
            w.u8((uint8_t)((F<uint8_t>(c, kDying) ? 1 : 0) | (F<uint8_t>(c, kDead) ? 2 : 0) | (F<uint8_t>(c, 0x110) ? 4 : 0)));
        }
        w.f32(world_scale()); // BUG-0511: the scale these poses are in (a guest without the field stops before it)
        C.sess->send_unreliable_to(dest, net::MSG_CELL_STATE, w);
    }
}

} // namespace cellsync
