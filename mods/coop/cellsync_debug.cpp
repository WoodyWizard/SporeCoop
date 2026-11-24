// coop.pool: the Cell module's console command (tests and diagnostics) and its reports; moved out of cellsync.cpp
// (C8 of 0.7.15, BUG-1183).
#include "cellsync_internal.h"

namespace cellsync {

// ---- debug (tests) ----
// what a player sees: the kind of a cell from its resource (docs/re/cell.md 1): cellType +0xB4,
// ai.type (first int of the AI block by difficulty)
const char* kind_of(uint8_t* c)
{
    int type, ai;
    if (!res_types(c, &type, &ai))
        return "?";
    switch (type) {
    case 1: return "rock";
    case 3: return "plant";
    case 4: return "bubble";
    case 5: return "poison";
    case 6: return "part";
    case 7: return "nanite";
    case 2: return ai != 0 ? "creature" : "meat";
    default: return "other";
    }
}

// on-screen gameplay cells: key kind size_ratio hp distance screen_x screen_y scale_diff (the nearest first);
// scale_diff = GetScaleDifference(avatar, cell) (2 = the same level; the game's mouth eats meat at 0 / 1 and plants at 1,
// docs/re/cell.md#cell-eat-gate, BUG-0293)
std::string see(int max)
{
    uint8_t* av = avatar();
    net::Vec3 me = pos_of(av);
    float my = std::max(F<float>(av, kSize), 0.01f);
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    struct Seen {
        uint32_t key;
        const char* kind;
        float ratio, d;
        int hp, x, y, sd;
    };
    std::vector<Seen> v;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
            return;
        uint32_t key = idx;
        if (is_guest()) {
            auto it = g_host_of.find(idx);
            if (it == g_host_of.end())
                return;
            key = it->second;
        }
        net::Vec3 p = pos_of(c);
        float w[3] = {p.x, p.y, p.z}, sc[2];
        if (!H->world_to_screen(w, sc) || sc[0] < 0 || sc[1] < 0 || sc[0] >= sw || sc[1] >= sh)
            return;
        int sd = a_ScaleDiff ? skg::cdecl_call<int>(a_ScaleDiff, av, c) : -1;
        v.push_back({key, kind_of(c), F<float>(c, kSize) / my, net::dist(p, me), F<int>(c, kHp), (int)sc[0], (int)sc[1], sd});
    });
    std::sort(v.begin(), v.end(), [](const Seen& a, const Seen& b) { return a.d < b.d; });
    std::string out;
    char b[128];
    float w[3] = {me.x, me.y, me.z}, sc[2] = {-1, -1};
    H->world_to_screen(w, sc);
    snprintf(b, sizeof b, "me %d %d hp=%d screen=%dx%d\n", (int)sc[0], (int)sc[1], F<int>(av, kHp), sw, sh);
    out += b;
    for (size_t i = 0; i < v.size() && (int)i < max; i++) {
        snprintf(b, sizeof b, "see c%u %s %.2f %d %.1f %d %d %d\n", v[i].key, v[i].kind, v[i].ratio, v[i].hp, v[i].d, v[i].x,
                 v[i].y, v[i].sd);
        out += b;
    }
    return out;
}

// the NPCs within r of a player: what the game's own target tests say about each player
// (docs/re/cell.md 9): prey (chase) / threat (flee) and the current chase target
// mode: 0 the sums only, 1 a row per NPC (40 at most), 2 only the hunters (NPCs that take a player for prey, 80 at
// most: play_cell's hunt trial, BUG-0910)
std::string prey_report(float r, int mode)
{
    bool rows = mode != 0;
    uint8_t* av = avatar();
    if (!a_TargetParams || !a_IsPrey || !a_IsThreat || !a_GetData)
        return "symbols missing";
    struct Pl {
        std::string name;
        uint8_t* c;
        // NPCs within r / in their prey range and IsPrey / in their threat range and IsThreat / chasing it
        int around = 0, prey = 0, threat = 0, chasing = 0;
        int hunted = 0; // in its prey range, prey and its chase target now
    };
    std::vector<Pl> players{{"me", av}};
    if (C.puppets)
        for (auto& p : C.puppets())
            if (p.cell)
                players.push_back({p.name, (uint8_t*)p.cell});
    std::string out;
    char b[256];
    for (auto& pl : players) {
        uint8_t* pc = pl.c;
        // scale difference me -> player (2 = same level)
        int sd = a_ScaleDiff ? skg::cdecl_call<int>(a_ScaleDiff, av, pc) : -1;
        snprintf(b, sizeof b, "player %s idx=%u level=%d scale_diff=%d lock189=%d dead=%d invuln=%d size=%.2f pos=(%.1f %.1f)\n",
                 pl.name.c_str(), F<uint32_t>(pc, 0), F<int>(pc, kLevel), sd, F<uint8_t>(pc, 0x189), F<uint8_t>(pc, kDead),
                 F<uint8_t>(pc, kInvuln), F<float>(pc, kSize), pos_of(pc).x, pos_of(pc).y);
        if (rows)
            out += b;
    }
    int n = 0, compared = 0, differ = 0; // NPCs whose answers for a partner differ from those for us
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || strcmp(kind_of(c), "creature") != 0)
            return;
        float best = 1e9f;
        for (auto& pl : players)
            best = std::min(best, net::dist(pos_of(c), pos_of(pl.c)));
        if (best > r)
            return;
        void* holder = nullptr;
        uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, F<void*>(c, kRes), &holder);
        if (holder)
            --F<int>(holder, 0xC);
        if (!res)
            return;
        alignas(4) uint8_t params[32] = {};
        skg::cdecl_call<void>(a_TargetParams, c, res, params);
        snprintf(b, sizeof b, "npc %u level=%d chase=%u flee=%u ranges=%.1f/%.1f", idx, F<int>(c, kLevel),
                 F<uint32_t>(c, kChase), F<uint32_t>(c, kFlee), F<float>(params, 0), F<float>(params, 4));
        std::string row = b;
        int mine = -1;
        bool hunter = false;
        for (auto& pl : players) {
            bool prey = skg::cdecl_call<bool>(a_IsPrey, c, pl.c, params);
            bool threat = skg::cdecl_call<bool>(a_IsThreat, c, pl.c, params);
            int ans = prey * 2 + threat;
            if (mine < 0)
                mine = ans;
            else {
                compared++;
                differ += ans != mine;
            }
            float d = net::dist(pos_of(c), pos_of(pl.c));
            pl.around += d <= r;
            pl.prey += prey && d <= F<float>(params, 0);
            pl.threat += threat && d <= F<float>(params, 4);
            pl.chasing += F<uint32_t>(c, kChase) == F<uint32_t>(pl.c, 0);
            pl.hunted += prey && d <= F<float>(params, 0) && F<uint32_t>(c, kChase) == F<uint32_t>(pl.c, 0);
            hunter |= prey;
            snprintf(b, sizeof b, " | %s d=%.1f prey=%d threat=%d", pl.name.c_str(), d, (int)prey, (int)threat);
            row += b;
        }
        if (mode == 2 ? hunter && n++ < 80 : rows && n++ < 40)
            out += row + "\n";
    });
    snprintf(b, sizeof b, "prey_rules compared=%d differ=%d\n", compared, differ);
    out += b;
    for (size_t i = 0; i < players.size(); i++) {
        snprintf(b, sizeof b, "prey_sum %s around=%d prey=%d threat=%d chasing=%d hunted=%d\n", i ? "partner" : "me",
                 players[i].around, players[i].prey, players[i].threat, players[i].chasing, players[i].hunted);
        out += b;
    }
    return out;
}

// The cell broadphase (SweepPrune, G+0x4104, docs/re/cell.md#cell-sap-active-list): replays the
// axis-0 sweep of SweepPrune::UpdatePairs over the sorted endpoints and reports the most objects
// open at once (the game's stack list holds 1024) and the objects still open at the end (a max
// endpoint before its min), with what those cells are.
std::string sap_report()
{
    uint8_t* g = G();
    uint8_t* sap = g ? F<uint8_t*>(g, 0x4104) : nullptr;
    if (!sap)
        return "sap: none";
    int cap = F<int>(sap, 4), n = F<int>(sap, 0x20), pend = F<int>(sap, 0x2C);
    auto* ep = F<uint8_t*>(sap, 0x24);
    uint8_t* objs = F<uint8_t*>(sap, 0x14);
    if (!ep || !objs || n < 0 || n > 2 * cap || cap <= 0 || cap > 0x8000)
        return "sap: bad header";
    std::vector<int> active;
    std::vector<uint8_t> open(cap, 0);
    // entries per id: an object has one min and one max per axis; more min entries with one id
    // are endpoints left behind (a RemoveObject that marked other entries)
    std::vector<uint16_t> mins(cap, 0), maxs(cap, 0);
    for (int i = 0; i < n; i++) {
        int16_t type = F<int16_t>(ep, 8 * i), id = F<int16_t>(ep, 8 * i + 2);
        if (id >= 0 && id < cap && type != -2)
            (type == 0 ? mins : maxs)[id]++;
    }
    int dup_min = 0, lone_min = 0, lone_max = 0;
    for (int i = 0; i < cap; i++) {
        dup_min += mins[i] > 1;
        lone_min += mins[i] > maxs[i];
        lone_max += maxs[i] > mins[i];
    }
    int live_objs = F<int>(sap, 0x10);
    int peak = 0, nan_v = 0, removed = 0, unsorted = 0, max_first = 0;
    float prev = -INFINITY;
    for (int i = 0; i < n; i++) {
        int16_t type = F<int16_t>(ep, 8 * i), id = F<int16_t>(ep, 8 * i + 2);
        float v = F<float>(ep, 8 * i + 4);
        if (type == -2) {
            removed++;
            continue;
        }
        if (v != v)
            nan_v++;
        else {
            if (v < prev)
                unsorted++;
            prev = v;
        }
        if (id < 0 || id >= cap)
            continue;
        if (type == 0) {
            active.push_back(id);
            open[id]++;
            peak = std::max(peak, (int)active.size());
        } else {
            auto it = std::find(active.begin(), active.end(), (int)id);
            if (it == active.end())
                max_first++;
            else {
                *it = active.back();
                active.pop_back();
                open[id]--;
            }
        }
    }
    // what the objects left open are: cell +0x364 holds the cell's broadphase object
    std::unordered_set<uint32_t> mirror_local;
    for (auto& [h, m] : g_mirrors)
        mirror_local.insert(m.local);
    int k_mirror = 0, k_puppet = 0, k_avatar = 0, k_bg = 0, k_other = 0, k_none = 0, inverted = 0;
    float rmin = INFINITY, rmax = -INFINITY;
    std::unordered_map<int, uint8_t*> by_obj;
    uint8_t* data = F<uint8_t*>(g, 0x1C);
    uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
    if (data && size == 0x398)
        for (uint32_t slot = 0; slot < used && slot < 4096; slot++) {
            uint8_t* c = data + (size_t)slot * size;
            if ((F<uint32_t>(c, 0) & 0xFFFF) == slot && F<int>(c, 0x364) >= 0 && F<int>(c, 0x364) < cap)
                by_obj[F<int>(c, 0x364)] = c;
        }
    char sample[200] = "";
    for (int id : active) {
        auto it = by_obj.find(id);
        uint8_t* c = it == by_obj.end() ? nullptr : it->second;
        if (!c) {
            k_none++;
            continue;
        }
        // its endpoint values (object record: +8 min index per axis, +0xC max index per axis)
        uint8_t* o = objs + 16 * (size_t)id;
        int mi = F<int16_t>(o, 8) & 0x7FFF, ma = F<int16_t>(o, 0xC) & 0x7FFF;
        if (mi < n && ma < n && F<float>(ep, 8 * mi + 4) > F<float>(ep, 8 * ma + 4))
            inverted++;
        float r = F<float>(c, kSize);
        rmin = std::min(rmin, r), rmax = std::max(rmax, r);
        uint32_t idx = F<uint32_t>(c, 0);
        if (idx == avatar_idx())
            k_avatar++;
        else if (modes::cell_is_puppet(c))
            k_puppet++;
        else if (mirror_local.count(idx))
            k_mirror++;
        else if (F<void*>(c, kQuery) != fg_query())
            k_bg++;
        else
            k_other++;
        if (!sample[0])
            snprintf(sample, sizeof sample, " first=c%u pos=(%.1f %.1f) size=%.3f min=%.2f max=%.2f", idx, pos_of(c).x,
                     pos_of(c).y, r, mi < n ? F<float>(ep, 8 * mi + 4) : 0.f, ma < n ? F<float>(ep, 8 * ma + 4) : 0.f);
    }
    int m1 = 0, hb = 0;
    particle_state(&m1, &hb);
    char b[1000];
    snprintf(b, sizeof b,
             "sap: merges_ours=%u merge_peak=%u merge_clipped=%u merge_fix=%d particles=%d peak=%u spare=%u bad_frees=%u minus1_cells=%d free_head_bad=%d endpoints=%d objects_cap=%d pending=%d held=%u removed=%d nan=%d unsorted=%d peak_open=%d open_at_end=%d "
             "max_first=%d live_objects=%d dup_min=%d lone_min=%d lone_max=%d sweeps=%u over_1024=%u sweep_peak=%u sweep_left=%u pairs=%u/%u pairs_full=%u fix=%d | open: mirror=%d other=%d bg=%d puppet=%d avatar=%d no_cell=%d inverted=%d size=%.3f..%.3f%s",
             g_n.merge_ours, g_n.merge_peak, g_n.merge_clipped, (int)g_fix_sap_merge, particles_used(), g_n.particles_peak, g_part_spare, g_n.part_bad_frees, m1, hb, n, cap, pend, modes::cell_sap_held(), removed, nan_v, unsorted, peak, (int)active.size(), max_first, live_objs, dup_min, lone_min, lone_max, g_n.sap_sweeps, g_n.sap_over, g_n.sap_peak, g_n.sap_left,
             g_n.sap_pairs, g_n.sap_pair_cap, g_n.sap_pairs_full, 1, k_mirror, k_other, k_bg, k_puppet,
             k_avatar, k_none, inverted, rmin, rmax, sample);
    return b;
}

std::string debug(const std::vector<std::string>& a)
{
    if (!a.empty() && a[0] == "fix") { // any stage (resend also covers the DNA total) // fix [<name>|all 0|1]: the wave 2 fixes, off = the old behaviour
        struct Sw {
            const char* name;
            bool* on;
        } sw[] = {{"purge", &g_fix_purge},     {"hostof", &g_fix_hostof},   {"foodseq", &g_fix_foodseq},
                  {"resend", &g_fix_resend},   {"kostale", &g_fix_kostale}, {"koreset", &g_fix_koreset},
                  {"bodylevel", &g_fix_bodylevel}, {"cellat", &modes::cell_at_fix},
                  {"guestbite", &g_fix_guestbite}};
        if (a.size() >= 3) {
            if (a[2] != "0" && a[2] != "1")
                return "usage: coop.pool fix [<name>|all 0|1]";
            bool found = false;
            for (auto& x : sw)
                if (a[1] == "all" || a[1] == x.name)
                    *x.on = a[2] == "1", found = true;
            if (!found)
                return "no such fix: " + a[1];
        }
        std::string o = "fixes:";
        for (auto& x : sw)
            o += std::string(" ") + x.name + "=" + (*x.on ? "1" : "0");
        return o;
    }
    uint8_t* av = in_cell() ? avatar() : nullptr; // G() outlives the stage, its pool does not
    if (!av)
        return "not in the cell stage";
    if (a.size() >= 2 && a[0] == "zap") { // guest: the game removes <n> mirrors' cells (not our removal path)
        int n = atoi(a[1].c_str()), done = 0;
        for (auto& [h, m] : g_mirrors)
            if (done < n && m.local && modes::cell_at(m.local)) {
                destroy_local(m.local); // the mirror and g_host_of keep it: the next state recreates it
                done++;
            }
        return "zapped " + std::to_string(done);
    }
    if (!a.empty() && a[0] == "mirrormap") { // guest: host index -> local pool index of every live mirror
        std::string o;
        for (auto& [h, m] : g_mirrors)
            if (m.local && modes::cell_at(m.local))
                o += "map " + std::to_string(h) + " " + std::to_string(m.local) + "\n";
        return o.empty() ? "no live mirrors\n" : o;
    }
    if (!a.empty() && a[0] == "small") { // our cell as after a small respawn (body 0.5)
        set_body(av, kSmall);
        return "small";
    }
    if (a.size() >= 2 && a[0] == "foodlocal") { // our total only, as a total the host sent and we missed
        {
            callguard::Hold food_guard(g_food_guard);
            o_AddFood(atoi(a[1].c_str()), false);
        }
        return "local food changed";
    }
    if (a.size() >= 2 && a[0] == "leavefix") {
        if (a[1] != "0" && a[1] != "1")
            return "usage: coop.pool leavefix 0|1";
        g_leave_fix = a[1] == "1";
        return g_leave_fix ? "leave fix on" : "leave fix off";
    }
    if (!a.empty() && a[0] == "own") {
        char b[100];
        snprintf(b, sizeof b, "avatar=%u mate=%u mate_alive=%d", avatar_idx(), mate_idx(),
                 modes::cell_at(mate_idx()) ? 1 : 0);
        return b;
    }
    if (!a.empty() && a[0] == "why") { // which game code destroyed mirrors on this guest
        std::string o;
        char b[64];
        for (auto& [ra, n] : g_destroy_callers) {
            if (ra & 0x80000000u)
                snprintf(b, sizeof b, "partner cell destroyed from 0x%08X x%u\n", (unsigned)(ra & 0x7FFFFFFF), n);
            else if (ra & 0x20000000u)
                snprintf(b, sizeof b, "mirror killed (KillCell) from 0x%08X x%u\n", (unsigned)(ra & 0x0FFFFFFF), n);
            else if (ra & 0x10000000u)
                snprintf(b, sizeof b, "mirror broken (BreakOrSwallow) from 0x%08X x%u\n", (unsigned)(ra & 0x0FFFFFFF), n);
            else if (ra & 0x40000000u)
                snprintf(b, sizeof b, "reported (touched) from 0x%08X x%u\n", (unsigned)(ra & 0x3FFFFFFF), n);
            else
                snprintf(b, sizeof b, "destroy from 0x%08X x%u\n", (unsigned)(ra - H->rebase(0)), n);
            o += b;
        }
        return o.empty() ? "none" : o;
    }
    if (!a.empty() && a[0] == "deaths") { // tests (BUG-0221): the game's death count S+0x70 (docs/re/cell.md)
        uint8_t* s0 = S();
        return "deaths=" + std::to_string(s0 ? F<int32_t>(s0, 0x70) : -1);
    }
    if (!a.empty() && a[0] == "kin") // tests (BUG-1257): the kin of every player's species [within r m of a player]
        return kin_report(a.size() > 1 ? (float)atof(a[1].c_str()) : 1e9f);
    if (!a.empty() && a[0] == "species") { // tests (BUG-0637): the creature models of the pool (foreground, alive): model count levels
        std::map<uint32_t, std::pair<int, std::set<int>>> m;
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (c == av || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) || strcmp(kind_of(c), "creature") ||
                F<void*>(c, kQuery) != fg_query())
                return;
            auto& e = m[F<uint32_t>(c, kModelKey)];
            e.first++;
            e.second.insert(F<int>(c, kLevel));
        });
        std::string out;
        char b[96];
        snprintf(b, sizeof b, "own model=%08X level=%d food_level=%d\n", av ? F<uint32_t>(av, kModelKey) : 0, av ? F<int>(av, kLevel) : -1,
                 g_tile_level);
        out = b;
        for (auto& [k, e] : m) {
            std::string lv;
            for (int l : e.second)
                lv += (lv.empty() ? "" : ",") + std::to_string(l);
            snprintf(b, sizeof b, "model %08X n=%d levels=%s\n", k, e.first, lv.c_str());
            out += b;
        }
        return out;
    }
    if (!a.empty() && a[0] == "npcs") { // tests (BUG-0205): the creatures within <r> m: local index, mirror, AI lock, pos
        uint8_t* av = avatar();
        if (!av)
            return "no cell";
        float r = a.size() > 1 ? (float)atof(a[1].c_str()) : 80.f;
        net::Vec3 me = pos_of(av);
        std::string out;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (c == av || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) ||
                strcmp(kind_of(c), "creature") || net::dist(pos_of(c), me) > r)
                return;
            char b[128];
            snprintf(b, sizeof b, "npc %08X mirror=%d ailock=%d pos=(%.2f %.2f)\n", idx, g_host_of.count(idx) ? 1 : 0,
                     F<uint8_t>(c, kAiLock), pos_of(c).x, pos_of(c).y);
            out += b;
        });
        return out.empty() ? "no npcs\n" : out;
    }
    if (!a.empty() && a[0] == "census") { // tests (BUG-0307 hunt): every used slot of the cell pool by kind
        uint8_t* g = G();
        if (!g)
            return "no cell game\n";
        uint8_t* data = F<uint8_t*>(g, 0x1C);
        uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
        if (!data || size != 0x398)
            return "no pool\n";
        void* fq = fg_query();
        uint32_t av = avatar_idx(), live = 0, fg = 0, other_q = 0, mirrors = 0, puppets = 0, strays = 0, keep = 0,
                 dying = 0, dead = 0, stray_keep = 0, stray_dying = 0, own = 0;
        std::map<int, uint32_t> stray_levels, bg_levels;
        uint32_t bg = 0, bg_far = 0, bg_keep = 0, bg_host = 0;
        uint8_t* avc = avatar();
        net::Vec3 me = avc ? pos_of(avc) : net::Vec3{};
        for (uint32_t slot = 0; slot < used && slot < 4096; slot++) {
            uint8_t* c = data + (size_t)slot * size;
            uint32_t idx = F<uint32_t>(c, 0);
            if ((idx & 0xFFFF) != slot)
                continue;
            live++;
            if (idx == av) {
                own++;
                continue;
            }
            if (F<void*>(c, kQuery) != fq) {
                other_q++;
                bool is_bg = F<void*>(c, kQuery) == F<void*>(g, 0x4100);
                bg += is_bg;
                if (is_bg) {
                    bg_levels[F<int>(c, kLevel)]++;
                    net::Vec3 q = pos_of(c);
                    float d = std::hypot(q.x - me.x, q.y - me.y);
                    bg_far += d > 150.f;
                    bg_keep += F<uint8_t>(c, kKeepAlive) != 0;
                    bg_host += g_host_of.count(idx) ? 1 : 0;
                }
                continue;
            }
            fg++;
            bool k = F<uint8_t>(c, kKeepAlive), dy = F<uint8_t>(c, kDying), de = F<uint8_t>(c, kDead);
            keep += k; dying += dy; dead += de;
            if (g_host_of.count(idx))
                mirrors++;
            else if (modes::cell_is_puppet(c))
                puppets++;
            else {
                strays++;
                stray_keep += k;
                stray_dying += dy || de;
                stray_levels[F<int>(c, kLevel)]++;
            }
        }
        char b[900];
        int n = snprintf(b, sizeof b, "census: header=%u used=%u live=%u own=%u fg=%u other_query=%u mirrors=%u (map %u) puppets=%u "
                         "strays=%u (keepalive %u, dying/dead %u) keepalive=%u dying=%u dead=%u stray_levels=",
                         cells_live(), used, live, own, fg, other_q, mirrors, (unsigned)g_mirrors.size(), puppets, strays,
                         stray_keep, stray_dying, keep, dying, dead);
        for (auto& [lv, k] : stray_levels)
            n += snprintf(b + n, sizeof b - std::min<size_t>(n, sizeof b), "%d:%u ", lv, k);
        n += snprintf(b + n, sizeof b - std::min<size_t>(n, sizeof b), " background=%u (over 150 m %u, keepalive %u, mirrors %u) bg_culled=%u leave_writes=%u leave_dropped=%u bg_levels=",
                      bg, bg_far, bg_keep, bg_host, g_n.bg_culled, g_n.leave_writes, g_n.leave_dropped);
        for (auto& [lv, k] : bg_levels)
            n += snprintf(b + n, sizeof b - std::min<size_t>(n, sizeof b), "%d:%u ", lv, k);
        n = std::min<int>(n, (int)sizeof b - 1);
        return std::string(b) + "\n";
    }
    if (!a.empty() && a[0] == "mirrors") { // live local pool indices, for checking cleanup after leave
        std::vector<uint32_t> indices;
        for (auto& [host, mirror] : g_mirrors)
            if (mirror.local && modes::cell_at(mirror.local))
                indices.push_back(mirror.local);
        std::sort(indices.begin(), indices.end());
        std::string out;
        for (uint32_t idx : indices)
            out += "mirror_local " + std::to_string(idx) + "\n";
        return out.empty() ? "no live mirrors\n" : out;
    }
    if (a.size() >= 2 && a[0] == "cell") { // a cell by its sync key (host index): local state
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        double seen_age = -1;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
            if (it != g_mirrors.end())
                seen_age = H->time_seconds() - it->second.seen;
            if (!c) { // BUG-0480: a proxy row of the census is a local cell mapped to this host index
                for (auto& [l, h] : g_host_of)
                    if (h == key && (c = modes::cell_at(l)) != nullptr)
                        break;
                if (c) {
                    char b[160];
                    snprintf(b, sizeof b, "cell %s: local %08X maps to it, mirror %s (seen %.1f s ago) fg=%d\n", a[1].c_str(), F<uint32_t>(c, 0),
                             it != g_mirrors.end() ? "listed" : "not listed", seen_age, F<void*>(c, kQuery) == fg_query() ? 1 : 0);
                    return b;
                }
            }
        } else
            c = modes::cell_at(key);
        if (!c && !is_guest()) {
            auto st = g_sent_at.find(key);
            if (st != g_sent_at.end()) { // BUG-0480: sent, not here now
                char b[200];
                uint8_t* slot = nullptr;
                uint8_t* g = G();
                if (g && F<uint8_t*>(g, 0x1C) && (key & 0xFFFF) < F<uint32_t>(g, 0x1C + 0x10))
                    slot = F<uint8_t*>(g, 0x1C) + (size_t)(key & 0xFFFF) * 0x398;
                snprintf(b, sizeof b, "no such cell (last sent %.2f s ago at (%.1f %.1f) level %d, first %.2f s ago, %u sends; its slot holds %08X now)\n",
                         H->time_seconds() - st->second.at, st->second.pos.x, st->second.pos.y, st->second.level,
                         H->time_seconds() - st->second.first, st->second.sends, slot ? F<uint32_t>(slot, 0) : 0u);
                std::string out(b);
                char q[64];
                snprintf(q, sizeof q, "  host cells destroyed < 2 s after their first send: %u;", g_n.quick_destroys);
                out.pop_back();
                out += q;
                for (auto& [ra, n] : g_quick_destroy) {
                    snprintf(q, sizeof q, " ra 0x%08X x%u", (unsigned)ra, n);
                    out += q;
                }
                return out + "\n";
            }
        }
        if (!c)
            return "no such cell";
        std::string pre;
        if (is_guest() || F<void*>(c, kQuery) != fg_query()) {
            char b[128];
            snprintf(b, sizeof b, "cell %s: fg=%d seen=%.1f s ago puppet=%d dying=%d dead=%d\n", a[1].c_str(), F<void*>(c, kQuery) == fg_query() ? 1 : 0,
                     seen_age, modes::cell_is_puppet(c) ? 1 : 0, F<uint8_t>(c, kDying), F<uint8_t>(c, kDead));
            if (F<void*>(c, kQuery) != fg_query())
                return b;
            pre = b;
        }
        char b[256];
        auto* v = (float*)(c + kVel);
        snprintf(b, sizeof b, "cell %s %s pos=(%.2f %.2f) vel=(%.2f %.2f) size=%.2f gfx=%u particles=%d hp=%d level=%d "
                 "ailock=%d keepalive=%d ring=%.2f anim=%X idx=%08X onscreen=%d\n", a[1].c_str(), kind_of(c), pos_of(c).x, pos_of(c).y, v[0], v[1],
                 F<float>(c, kSize), F<uint32_t>(c, 0x248), F<int>(c, 0x250), F<int>(c, kHp), F<int>(c, kLevel),
                 F<uint8_t>(c, kAiLock), F<uint8_t>(c, kKeepAlive), rollover_of(F<uint32_t>(c, 0)), F<uint32_t>(c, 0x1B0), F<uint32_t>(c, 0), on_screen_dbg(c));
        return pre + b;
    }
    if (!a.empty() && a[0] == "anims") { // tests (BUG-1243): the animation of the own cell and of the partners' puppets
        uint8_t* av = avatar();
        char b[420];
        const float* m = av ? (const float*)(av + kRot) : nullptr;
        snprintf(b, sizeof b, "anim avatar %08X anim=%X head=%.3f angv=%.3f pos=(%.2f %.2f) hp=%d state=%X hits=%u last_hit=%s own_eats=%u\n",
                 av ? F<uint32_t>(av, 0) : 0u, av ? F<uint32_t>(av, 0x1B0) : 0u, m ? std::atan2(m[1], m[0]) : 0.f,
                 av ? F<float>(av, 0x1D0) : 0.f, av ? pos_of(av).x : 0.f, av ? pos_of(av).y : 0.f, av ? F<int>(av, kHp) : 0,
                 av ? F<uint32_t>(av, 0x18C) : 0u, g_own_hits_n, g_own_hit_last, g_n.own_eats); // own_eats: BUG-1400
        char z[128];
        snprintf(z, sizeof z, "zaps=%u puppet_zaps=%u\n", g_n.zaps, g_n.puppet_zaps);
        return std::string(b) + z + modes::cell_puppet_anims();
    }
    if (!a.empty() && a[0] == "diet") { // what each cell's mouth eats (the model data CanEat 0xE68470 reads)
        std::string out;
        auto line = [&](const char* who, uint8_t* c) {
            char b[160];
            auto* k = (uint32_t*)(c + kModelKey);
            uint8_t* md = a_ModelData && k[0] ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
            snprintf(b, sizeof b, "diet %s model=%08X carnivore=%d herbivore=%d\n", who, k[0], md ? md[0x1250] : -1,
                     md ? md[0x1251] : -1);
            out += b;
        };
        if (uint8_t* av = avatar())
            line("me", av);
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (modes::cell_is_puppet(c))
                line("partner", c);
        });
        return out.empty() ? std::string("diet: no avatar\n") : out;
    }
    if (!a.empty() && a[0] == "see")
        return see(a.size() > 1 ? atoi(a[1].c_str()) : 60);
    if (!a.empty() && a[0] == "pace") // BUG-1309 (host): each guest's paced Cell state
        return pace_status();
    // BUG-1330: "nearkeys", not "near": BUG-1246's `near <x> <y> <r>` below must stay reachable (evo_screens_cell_mate)
    if (!a.empty() && a[0] == "nearkeys") { // tests (BUG-1309): nearkeys <r> [x y]: this game's own live cells within r
        // of (x y) (default: our own cell), keyed by the host's index (a guest: its mirrors; cells of its own game
        // that mirror none are not listed), dying / dead ones and players' cells left out
        float r = a.size() > 1 ? (float)atof(a[1].c_str()) : 30.f;
        net::Vec3 at = av ? pos_of(av) : net::Vec3{};
        if (a.size() > 3) {
            at.x = (float)atof(a[2].c_str());
            at.y = (float)atof(a[3].c_str());
        }
        std::string keys;
        int n = 0;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) || !F<void*>(c, kRes))
                return;
            uint32_t key = idx;
            if (is_guest()) {
                auto it = g_host_of.find(idx);
                if (it == g_host_of.end())
                    return;
                key = it->second;
            }
            net::Vec3 p = pos_of(c);
            if (std::hypot(p.x - at.x, p.y - at.y) > r)
                return;
            keys += (n++ ? " " : "") + std::to_string(key);
        });
        char b[120];
        snprintf(b, sizeof b, "nearkeys n=%d at=(%.1f %.1f) r=%.1f\n", n, at.x, at.y, r);
        return std::string(b) + "keys " + keys + "\n";
    }
    if (!a.empty() && a[0] == "prey")
        return prey_report(a.size() > 1 ? (float)atof(a[1].c_str()) : 40.f,
                           a.size() < 3 ? 1 : a[2] == "sum" ? 0 : a[2] == "hunters" ? 2 : 1);
    if (!a.empty() && a[0] == "bite") { // our avatar bites the nearest mirror (through DamageCell)
        uint8_t* best = nullptr;
        float bd = 1e9f;
        for (auto& [h, m] : g_mirrors) {
            uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
            if (!c)
                continue;
            float d = net::dist(pos_of(c), pos_of(av));
            if (d < bd) {
                bd = d;
                best = c;
            }
        }
        if (!best)
            return "no mirror";
        float dir[3] = {1, 0, 0};
        h_DamageCell(best, av, dir, 4, 0, 0.f);
        return "bit mirror " + std::to_string(F<uint32_t>(best, 0)) + " at " + std::to_string(bd);
    }
    if (a.size() >= 2 && a[0] == "alive") { // tests (BUG-0635): which of the host indices k1,k2,... are alive cells here (mirrors on a guest)
        std::string out = "alive";
        const char* p = a[1].c_str();
        while (*p) {
            while (*p == ',' || *p == 'c')
                p++;
            char* e = nullptr;
            uint32_t key = (uint32_t)strtoul(p, &e, 10);
            if (e == p)
                break;
            p = e;
            uint8_t* c = nullptr;
            if (is_guest()) {
                auto it = g_mirrors.find(key);
                c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
            } else
                c = modes::cell_at(key);
            if (c && !F<uint8_t>(c, kDead) && !F<uint8_t>(c, kDying))
                out += " c" + std::to_string(key);
        }
        return out + "\n";
    }
    if (a.size() >= 2 && a[0] == "gap" && a[1] == "near") { // tests (BUG-0634): the nearest creature of 0.4-2.5 x our size
        float best = 1e9f, my = std::max(F<float>(av, kSize), 0.01f);
        uint32_t bkey = 0;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (c == av || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) || strcmp(kind_of(c), "creature") ||
                F<void*>(c, kQuery) != fg_query())
                return;
            float r = F<float>(c, kSize) / my, d = net::dist(pos_of(c), pos_of(av));
            auto it = g_host_of.find(idx);
            if (r < 0.4f || r > 2.5f || d >= best || (is_guest() && it == g_host_of.end()))
                return;
            best = d;
            bkey = is_guest() ? it->second : idx;
        });
        if (!bkey)
            return "no creature";
        char b[64];
        snprintf(b, sizeof b, "near c%u d=%.1f\n", bkey, best);
        return b;
    }
    if (a.size() >= 2 && a[0] == "gap") { // tests (BUG-0634): our cell and the cell <key>: centre distance, sizes, own pairs so far
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        } else
            c = modes::cell_at(key);
        if (!c)
            return "no such cell";
        char b[200];
        snprintf(b, sizeof b, "gap %s d=%.3f own=%.3f other=%.3f own_pairs=%u particles=%d/%d ailock=%d dead=%d\n", a[1].c_str(),
                 net::dist(pos_of(c), pos_of(av)), F<float>(av, kSize), F<float>(c, kSize), g_n.own_pairs, F<int>(av, 0x250), F<int>(c, 0x250),
                 F<uint8_t>(c, kAiLock), F<uint8_t>(c, kDead) | F<uint8_t>(c, kDying));
        return b;
    }
    if (!a.empty() && a[0] == "food_near") { // tests (BUG-0636): the nearest plant / meat cell of the host's pool within <max> m
        float best = a.size() > 1 ? (float)atof(a[1].c_str()) : 30.f;
        uint8_t* bc = nullptr;
        uint32_t bkey = 0;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            const char* k = kind_of(c);
            if (modes::cell_is_puppet(c) || c == av || (strcmp(k, "meat") && strcmp(k, "plant")) || F<uint8_t>(c, kDead))
                return;
            auto it = g_host_of.find(idx);
            if (is_guest() && it == g_host_of.end())
                return;
            float d = net::dist(pos_of(c), pos_of(av));
            if (d < best) {
                best = d;
                bc = c;
                bkey = is_guest() ? it->second : idx;
            }
        });
        if (!bc)
            return "no food";
        char b[96];
        snprintf(b, sizeof b, "food c%u %s dist=%.1f\n", bkey, kind_of(bc), best);
        return b;
    }
    if (!a.empty() && a[0] == "meat") { // every meat cell (on screen or not): key pos level size, sync key on a guest ('l' = local only)
        std::string o;
        char b[128];
        int n = 0;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (modes::cell_is_puppet(c) || strcmp(kind_of(c), "meat") != 0)
                return;
            auto it = g_host_of.find(idx);
            bool mirror = it != g_host_of.end();
            snprintf(b, sizeof b, "meat %s%u pos=(%.1f %.1f) level=%d size=%.2f dist=%.1f dead=%d\n", is_guest() && !mirror ? "l" : "c",
                     is_guest() && mirror ? it->second : idx, pos_of(c).x, pos_of(c).y, F<int>(c, kLevel), F<float>(c, kSize),
                     net::dist(pos_of(c), pos_of(av)), F<uint8_t>(c, kDead));
            if (n++ < 80)
                o += b;
        });
        return o + "meat_count=" + std::to_string(n) + "\n";
    }
    if (!a.empty() && a[0] == "loot") { // host: the cells the last loot drops created (alive ones): key kind age
        std::string o;
        char b[128];
        double now = H->time_seconds();
        for (auto& [idx, t] : g_loot_recent)
            if (uint8_t* c = modes::cell_at(idx)) {
                snprintf(b, sizeof b, "loot c%u %s age=%.1f pos=(%.1f %.1f)\n", idx, kind_of(c), now - t, pos_of(c).x, pos_of(c).y);
                o += b;
            }
        return o.empty() ? "no loot" : o;
    }
    if (a.size() >= 2 && a[0] == "swimto") { // swim to a cell by its sync key (like a click on it)
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        } else
            c = modes::cell_at(key);
        if (!c && !is_guest()) {
            auto st = g_sent_at.find(key);
            if (st != g_sent_at.end()) { // BUG-0480: sent, not here now
                char b[260];
                uint8_t* slot = nullptr;
                uint8_t* g = G();
                if (g && F<uint8_t*>(g, 0x1C) && (key & 0xFFFF) < F<uint32_t>(g, 0x1C + 0x10))
                    slot = F<uint8_t*>(g, 0x1C) + (size_t)(key & 0xFFFF) * 0x398;
                snprintf(b, sizeof b, "no such cell (last sent %.2f s ago at (%.1f %.1f) level %d, first %.2f s ago, %u sends; its slot holds %08X now)\n",
                         H->time_seconds() - st->second.at, st->second.pos.x, st->second.pos.y, st->second.level,
                         H->time_seconds() - st->second.first, st->second.sends, slot ? F<uint32_t>(slot, 0) : 0u);
                std::string out(b);
                char q[64];
                snprintf(q, sizeof q, "  host cells destroyed < 2 s after their first send: %u;", g_n.quick_destroys);
                out.pop_back();
                out += q;
                for (auto& [ra, n] : g_quick_destroy) {
                    snprintf(q, sizeof q, " ra 0x%08X x%u", (unsigned)ra, n);
                    out += q;
                }
                return out + "\n";
            }
        }
        if (!c)
            return "no such cell";
        net::Vec3 p = pos_of(c);
        if (a.size() >= 3 && a[2] == "tp") { // right next to it [tp <m>: that far from its centre, and stay there]
            net::Vec3 me = pos_of(av);
            float dx = me.x - p.x, dy = me.y - p.y, l = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
            float r = a.size() >= 4 ? (float)atof(a[3].c_str()) : F<float>(av, kSize) * 0.8f + F<float>(c, kSize);
            stop_at(av, {p.x + dx / l * r, p.y + dy / l * r, 0.f});
            if (a.size() >= 4)
                return "moved near " + a[1];
        }
        auto* t = (float*)(av + kTarget);
        t[0] = p.x; t[1] = p.y; t[2] = 0;
        F<uint8_t>(av, 0x04) = 0;
        return "swimming to " + a[1];
    }
    if (a.size() >= 2 && a[0] == "eat") { // our avatar eats a cell by its sync key: the game's eat event 0x12 (EatFood)
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        } else
            c = modes::cell_at(key);
        if (!c || !a_EatFood)
            return "no such cell";
        g_touched[F<uint32_t>(c, 0)] = H->time_seconds(); // as the mouth contact would (HandleCollisionPair)
        uint32_t before = g_n.own_food;
        skg::cdecl_call<void>(a_EatFood, avatar_idx(), F<uint32_t>(c, 0), F<void*>(c, kRes), 1.f / 30.f);
        return "ate " + a[1] + " points=" + std::to_string(g_n.own_food - before);
    }
    if (a.size() >= 2 && a[0] == "swallow") { // BUG-0746: our avatar's mouth touches a cell by its sync key and the
        // game's executor 0xE7E7F0 runs interaction kind 10 (meat swallowed whole, as a carnivore mouth at d 0/1) or
        // `swallow <key> 9` (a plant swallowed whole); the meal comes ~1 s later (event type 1 -> OnEat)
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        } else
            c = modes::cell_at(key);
        if (!c || !a_ExecInter)
            return "no such cell";
        g_touched[F<uint32_t>(c, 0)] = H->time_seconds();
        uint32_t in[24] = {};
        in[0] = avatar_idx();
        in[1] = F<uint32_t>(c, 0);
        in[2] = a.size() > 2 ? (uint32_t)atoi(a[2].c_str()) : 10;
        net::Vec3 p = pos_of(c), me = pos_of(av);
        float dx = p.x - me.x, dy = p.y - me.y, l = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
        float* f = (float*)in;
        f[6] = p.x; f[7] = p.y; f[8] = 0; // the contact point
        f[9] = dx / l; f[10] = dy / l; f[11] = 0; // the normal
        uint32_t before = g_n.ko_swallows_blocked;
        skg::cdecl_call<void>(a_ExecInter, in, 1.f / 30.f);
        return "swallow " + a[1] + " kind " + std::to_string(in[2]) + (g_n.ko_swallows_blocked != before ? " blocked" : " run");
    }
    if (!a.empty() && a[0] == "kill") {
        // one bite of our avatar (DamageCell, hooked: a guest's goes to the host) on the NPC creature
        // with sync key a[1], or on the nearest one. A bitten cell is not hurt again for a moment
        // (it took 2 calls a second apart): call it until it says "dying".
        std::vector<std::pair<float, uint8_t*>> v;
        // "kill <key> hit": DamageCell only (spikes, poison, electric: a lethal hit goes to KillCell)
        uint32_t want = a.size() > 1 && a[1] != "-" ? (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10) : 0;
        // "kill <key|-> anyhit [kind]": DamageCell on a cell of any kind (or the nearest of that kind):
        // the host's own bite there, a MSG_CELL_HIT from a guest (BUG-0240)
        bool anyhit = a.size() > 2 && a[2] == "anyhit";
        std::string kind = anyhit && a.size() > 3 ? a[3] : "creature";
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (modes::cell_is_puppet(c) || c == av || (!(anyhit && want) && kind != kind_of(c)))
                return;
            auto it = g_host_of.find(idx);
            if (is_guest() && it == g_host_of.end())
                return;
            uint32_t key = is_guest() ? it->second : idx;
            if (want ? key != want : (F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying)))
                return;
            v.push_back({net::dist(pos_of(c), pos_of(av)), c});
        });
        if (v.empty())
            return want ? "gone" : "no creature";
        std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first < y.first; });
        uint8_t* c = v[0].second;
        uint32_t key = is_guest() ? g_host_of[F<uint32_t>(c, 0)] : F<uint32_t>(c, 0);
        net::Vec3 cp = pos_of(c), ap = pos_of(av);
        float dx = cp.x - ap.x, dy = cp.y - ap.y, l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
        float dir[3] = {dx / l, dy / l, 0}, at[3] = {cp.x, cp.y, cp.z};
        if (!F<uint8_t>(c, kDead) && !F<uint8_t>(c, kDying)) {
            if (a_Bite && !(a.size() > 2 && (a[2] == "hit" || anyhit))) // the game's bite (event 10): DamageCell, or BreakOrSwallow when lethal
                skg::cdecl_call<bool>(a_Bite, avatar_idx(), F<uint32_t>(c, 0), at, dir, 0, 1.f / 30.f);
            else
                h_DamageCell(c, av, dir, 4, 0, 0.f);
        }
        char b[240];
        snprintf(b, sizeof b, "bit c%u %s at %.1f level=%d hp=%d pos=(%.1f %.1f) ring=%.2f rings=%u skip=%s lock=%d invuln=%d noattack=%d%s", key, kind_of(c), v[0].first, F<int>(c, kLevel), F<int>(c, kHp),
                 pos_of(c).x, pos_of(c).y, rollover_of(F<uint32_t>(c, 0)), g_n.bite_rings, g_bite_ring_skip, F<uint8_t>(c, 0x17C), F<uint8_t>(c, kInvuln),
                 o_NoAttack_ptr() ? (int)o_NoAttack_ptr()(c, av) : -1, F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) ? " dying" : "");
        return b;
    }
    if (!a.empty() && a[0] == "realbite") { // realbite [max distance | c<key>]: our cell bites the nearest
        // creature (or that one) through the game's Bite 0xE7B0A0, what a bite event runs: DamageCell,
        // or BreakOrSwallow when the bite is lethal
        uint8_t* best = nullptr;
        float bd = a.size() > 1 && a[1][0] != 'c' ? (float)atof(a[1].c_str()) : 30.f;
        if (a.size() > 1 && a[1][0] == 'c') {
            uint32_t key = (uint32_t)strtoul(a[1].c_str() + 1, nullptr, 10);
            auto mi = g_mirrors.find(key);
            best = is_guest() ? (mi != g_mirrors.end() && mi->second.local ? modes::cell_at(mi->second.local) : nullptr)
                              : modes::cell_at(key);
            bd = best ? net::dist(pos_of(best), pos_of(av)) : 0.f;
        } else {
            for_each_cell([&](uint8_t* c, uint32_t idx) {
                if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) || strcmp(kind_of(c), "creature"))
                    return;
                if (is_guest() && !g_host_of.count(idx))
                    return;
                float d = net::dist(pos_of(c), pos_of(av));
                if (d < bd) {
                    bd = d;
                    best = c;
                }
            });
        }
        if (!best || !o_Bite)
            return "no creature";
        net::Vec3 p = pos_of(best), me = pos_of(av);
        float at[3] = {p.x, p.y, p.z}, dir[3] = {p.x - me.x, p.y - me.y, 0};
        float n = std::max(0.01f, std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]));
        dir[0] /= n;
        dir[1] /= n;
        uint32_t idx = F<uint32_t>(best, 0);
        auto it = g_host_of.find(idx);
        char b[160];
        snprintf(b, sizeof b, "bit c%u hp=%d at %.1f: %d", it != g_host_of.end() ? it->second : idx, F<int>(best, kHp), bd,
                 (int)h_Bite(avatar_idx(), idx, at, dir, 0, 0.033f));
        return b;
    }
    if (a.size() >= 2 && a[0] == "mirrorfix") { // 0: the guest as in 0.7.2 (see g_mirror_fixes)
        g_mirror_fixes = atoi(a[1].c_str()) != 0;
        return g_mirror_fixes ? "mirror fixes on" : "mirror fixes off (as in 0.7.2)";
    }
    if (a.size() >= 2 && a[0] == "food") { // the game's AddFoodPoints (hooked: shared), exactly N (test setups)
        int n = atoi(a[1].c_str());
        food_in_step([n] { // BUG-0633: in the cell step, as a meal
            dna_pace::g_bypass = true;
            h_AddFood(n, false);
            dna_pace::g_bypass = false;
        });
        return "food added";
    }
    if (a.size() >= 2 && a[0] == "foodhold") { // tests (BUG-1431): the food queued for the cell step waits secs (at most
        // 2), as while the game's cell step does not run (a short pause); the totals go out as usual meanwhile
        double secs = std::clamp(atof(a[1].c_str()), 0.0, 2.0);
        g_test_food_hold_until = H->time_seconds() + secs;
        char b[80];
        snprintf(b, sizeof b, "food queue held %.2f s (%u queued now)", secs, (unsigned)g_food_q.size());
        return b;
    }
    if (a.size() >= 2 && a[0] == "meal") { // an own meal as AvatarEat makes it: the party's pace applies (BUG-0510)
        h_AddFood(atoi(a[1].c_str()), a.size() >= 3 && a[2] == "plant");
        return "meal eaten";
    }
    if (!a.empty() && a[0] == "tokens") { // tests (BUG-0316): the part tokens in the pool; `tokens to <id> [c<key>]`: the nearest one (or that one) 4 m from that player's cell
        std::vector<std::pair<float, uint8_t*>> v;
        net::Vec3 me = pos_of(av);
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (!modes::cell_is_puppet(c) && c != av && !F<uint8_t>(c, kDead) && !F<uint8_t>(c, kDying) && !strcmp(kind_of(c), "part"))
                v.push_back({net::dist(pos_of(c), me), c});
        });
        std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first < y.first; });
        std::string o;
        char b[160];
        if (a.size() >= 4 && a[1] == "to") { // `tokens to <id> c<key>`: that token
            uint32_t want = (uint32_t)strtoul(a[3].c_str() + (a[3][0] == 'c' ? 1 : 0), nullptr, 10);
            for (size_t i = 0; i < v.size(); i++)
                if (F<uint32_t>(v[i].second, 0) == want)
                    std::swap(v[0], v[i]);
            if (v.empty() || F<uint32_t>(v[0].second, 0) != want)
                return "no such token\n";
        }
        if (a.size() >= 3 && a[1] == "to" && !v.empty()) {
            uint8_t* pc = atoi(a[2].c_str()) == (C.sess ? C.sess->my_id() : 0) ? av : puppet_cell(atoi(a[2].c_str()));
            if (!pc)
                return "no such player cell\n";
            net::Vec3 q = pos_of(pc);
            set_pos(v[0].second, {q.x + 4.f, q.y, q.z});
            snprintf(b, sizeof b, "moved c%u to (%.1f %.1f)\n", F<uint32_t>(v[0].second, 0), q.x + 4.f, q.y);
            o = b;
        }
        for (size_t i = 0; i < v.size() && i < 20; i++) {
            auto it = g_host_of.find(F<uint32_t>(v[i].second, 0));
            uint32_t key = is_guest() && it != g_host_of.end() ? it->second : F<uint32_t>(v[i].second, 0);
            snprintf(b, sizeof b, "token c%u pos=(%.1f %.1f) dist=%.1f size=%.2f level=%d lock17b=%d\n", key, pos_of(v[i].second).x, pos_of(v[i].second).y,
                     v[i].first, F<float>(v[i].second, kSize), F<int>(v[i].second, kLevel), F<uint8_t>(v[i].second, 0x17B));
            o += b;
        }
        // 0xE7B630 collects a token only while our cell's +0x112 / +0x113 / +0x178 / +0x17F and the token's +0x17B are 0
        char lk[40];
        for (int i = 0; i < 30; i++) // +0x16C..+0x189: the cell's lock bytes
            lk[i] = F<uint8_t>(av, 0x16C + i) ? '1' : '0';
        lk[30] = 0;
        uint8_t* g = G();
        char ev[96] = "none"; // the event G+0x5194 names, in the event pool G+0x54: type +0x24, timers +0x1C / +0x20
        if (g) {
            uint32_t ei = F<uint32_t>(g, 0x5194);
            uint8_t* pool = g + 0x54;
            uint8_t* data = F<uint8_t*>(pool, 0);
            uint32_t high = F<uint32_t>(pool, 0x10), size = F<uint32_t>(pool, 0x14);
            if (data && size >= 0x28 && (ei & 0xFFFF) < high) {
                uint8_t* e = data + (size_t)(ei & 0xFFFF) * size;
                if (F<uint32_t>(e, 0) == ei)
                    snprintf(ev, sizeof ev, "type=%X t=%.2f/%.2f", F<uint32_t>(e, 0x24), F<float>(e, 0x1C), F<float>(e, 0x20));
                else
                    snprintf(ev, sizeof ev, "gone");
            }
        }
        snprintf(b, sizeof b, "tokens=%u own_level=%d own_locks=%d%d%d%d anim=%d locks16c=%s hatch=%d hatch_ev=%08X(%s) avatar=%08X g5158=%d\n", (unsigned)v.size(), F<int>(av, kLevel), F<uint8_t>(av, 0x112),
                 F<uint8_t>(av, 0x113), F<uint8_t>(av, 0x178), F<uint8_t>(av, 0x17F), F<int>(av, 0x18C), lk, g ? F<uint8_t>(g, 0x51DB) : -1,
                 g ? F<uint32_t>(g, 0x5194) : 0u, ev, avatar_idx(), g ? F<int>(g, 0x5158) : -1);
        return o + b;
    }
    if (!a.empty() && a[0] == "spend") { // tests (BUG-1249): the guest's own editor spends, the DNA this game shows
        if (a.size() >= 3 && a[1] == "fake") // tests (BUG-1507): as if our own editor had just spent <n>
            spend_fake(atoi(a[2].c_str()));
        if (a.size() >= 3 && a[1] == "set") { // tests (BUG-1538): set local S+0x2C directly (simulates an editor spend)
            if (uint8_t* s = S())
                F<int32_t>(s, 0x2C) = atoi(a[2].c_str());
        }
        uint8_t* s = S();
        char b[320];
        snprintf(b, sizeof b, "%s spent=%d dna=%d food=%d", spend_status().c_str(), s ? F<int32_t>(s, 0x2C) : -1,
                 editor_budget(), s ? F<int32_t>(s, 0x1C) : -1);
        return b;
    }
    if (!a.empty() && a[0] == "cloud") { // tests (BUG-1247): how the NPC cells on screen are drawn: the angle of their soft
        // body's first particle about the particles' mean (the picture turns with it); on a guest for its mirrors (key = the
        // host's index) with the host's heading, on the host for its own NPC cells
        std::string out;
        int left = a.size() > 1 ? atoi(a[1].c_str()) : 40;
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        double now = H->time_seconds();
        auto line = [&](uint32_t key, uint8_t* c, const Mirror* m) {
            int n = c ? F<int>(c, 0x250) : 0;
            if (!c || !parts || left <= 0 || !F<uint32_t>(c, 0x248) || n > 64 || !on_screen_dbg(c))
                return;
            bool bare = n < 2; // no soft body: what is drawn is the heading after the step (step_head; the host: +0x5C)
            float cx = 0, cy = 0, fx = 0, fy = 0;
            int used = bare ? 1 : 0;
            for (int i = 0; i < n && !bare; i++) {
                uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
                if (k >= 1500)
                    continue;
                auto* q = (float*)(parts + 4 + 12 * (size_t)k);
                if (!used)
                    fx = q[0], fy = q[1];
                cx += q[0], cy += q[1];
                used++;
            }
            if (!used)
                return;
            cx /= used, cy /= used;
            const float* r = (const float*)(c + kRot);
            float drawn = bare ? (m ? m->step_head : std::atan2(r[1], r[0])) : std::atan2(fy - cy, fx - cx);
            float host = std::atan2(r[1], r[0]);
            if (m) {
                float want[9];
                modes::quat_to_mat(m->rot, want);
                host = std::atan2(want[1], want[0]);
            }
            char b[280];
            auto* v = (const float*)(c + kVel);
            snprintf(b, sizeof b, "cloud c%u host=%.3f rot=%.3f cloud=%.3f soft_age=%.2f soft_d=%.3f d=%.1f level=%d fg=%d anim=%X state=%X "
                     "speed=%.2f ailock=%d bare=%d\n", key, host, std::atan2(r[1], r[0]), drawn,
                     m && m->soft_at > 0 ? now - m->soft_at : -1.0, m ? m->soft_d : 0.f, av ? net::dist(pos_of(c), pos_of(av)) : -1.f,
                     F<int>(c, kLevel), F<void*>(c, kQuery) == fg_query() ? 1 : 0, F<uint32_t>(c, 0x1B0), F<uint32_t>(c, 0x18C),
                     std::hypot(v[0], v[1]), F<uint8_t>(c, kAiLock), bare ? 1 : 0);
            out += b;
            left--;
        };
        if (is_guest())
            for (auto& [h, m] : g_mirrors)
                line(h, m.local ? modes::cell_at(m.local) : nullptr, &m);
        else
            for_each_cell([&](uint8_t* c, uint32_t idx) {
                if (c != av && !modes::cell_is_puppet(c))
                    line(idx, c, nullptr);
            });
        return out.empty() ? std::string("cloud: none\n") : out;
    }
    if (!a.empty() && a[0] == "goldie") { // tests (BUG-1248): the Goldie tutorial's cells and what the camera follows
        uint8_t* g = G();
        uint8_t* ui = a_CellUI ? *(uint8_t**)a_CellUI : nullptr;
        uint32_t gi = g ? F<uint32_t>(g, 0x516C) : 0, fi = ui && skg::readable(ui + 0xE0, 4) ? F<uint32_t>(ui, 0xE0) : 0;
        uint8_t* gc = gi ? modes::cell_at(gi) : nullptr;
        uint8_t* fc = fi ? modes::cell_at(fi) : nullptr;
        uint32_t ev_goldie = 0, ev_hunter = 0;
        char why[120] = "-";
        bool ev = tutorial_cells(&ev_goldie, &ev_hunter, why, sizeof why);
        uint8_t* ec = ev && ev_goldie ? modes::cell_at(ev_goldie) : nullptr;
        int eaters = ev && ev_hunter && modes::cell_at(ev_hunter) ? 1 : 0;
        if (ev && ev_goldie) { // the tutorial's own Goldie (G+0x516C is whichever part-token cell the visibility step saw first)
            gi = ev_goldie;
            gc = ec;
        }
        char b[480];
        snprintf(b, sizeof b, "goldie running=%d idx=%08X alive=%d onscreen=%d dist=%.1f eaters=%d follow=%08X follow_alive=%d "
                 "follow_onscreen=%d done=%d kept=%u fix=%d tut=%08X/%08X mirror=%d why=%s\n", g ? F<uint8_t>(g, 0x518C) : -1, gi, gc ? 1 : 0, gc ? on_screen_dbg(gc) : 0,
                 gc && av ? net::dist(pos_of(gc), pos_of(av)) : -1.f, eaters, fi, fc ? 1 : 0, fc ? on_screen_dbg(fc) : 0,
                 S() ? F<uint8_t>(S(), 0x68) : -1, g_n.goldie_kept, (int)g_fix_goldie_keep, ev_goldie, ev_hunter,
                 gi && g_host_of.count(gi) ? 1 : 0, why);
        return b;
    }
    if (!a.empty() && a[0] == "mate") { // tests: the mating-call button (S+0x69), the mate, where it is
        uint8_t* s = S();
        if (s && a.size() >= 2 && a[1] == "button")
            F<uint8_t>(s, 0x69) = 1; // as if a part had been collected (0xE5D7B0 sets it)
        if (s && a.size() >= 2 && a[1] == "nobutton") // tests (BUG-0316): the button hidden, as before the first part
            F<uint8_t>(s, 0x69) = 0;
        // tests (BUG-0316): S+0x68 = the "Goldie's demise" tutorial was played. AddFoodPoints 0xE7CE10 starts it once
        // (0xE79720: two cells, the cinematic goldies_demise, event 0x24 after 15 s) when the food before the add is
        // 120-150, at most 4 parts are unlocked and the difficulty is not hard; in the lab it leaves both avatars'
        // locks +0x16C / +0x178 / +0x17B / +0x17C set, and 0xE7B630 then collects no part token
        if (s && a.size() >= 2 && a[1] == "nogoldie")
            F<uint8_t>(s, 0x68) = 1;
        uint8_t* m = modes::cell_at(mate_idx());
        if (m && a.size() >= 2 && a[1] == "tp") // tests (BUG-0237): our cell right onto the mate (it touches it)
            stop_at(av, pos_of(m));
        float sc[2] = {-1, -1};
        if (m) {
            net::Vec3 p = pos_of(m);
            float w[3] = {p.x, p.y, p.z};
            H->world_to_screen(w, sc);
        }
        // tests (BUG-1186): where the mate is from the host's cell (a guest: the host's puppet), and who removed it
        uint8_t* ref = is_guest() ? puppet_cell(0) : av;
        net::Vec3 rel = m && ref ? net::Vec3{pos_of(m).x - pos_of(ref).x, pos_of(m).y - pos_of(ref).y, 0.f} : net::Vec3{0, 0, 0};
        char b[400];
        snprintf(b, sizeof b, "goldie_done=%d mate_button=%d mate=%08X alive=%d dist=%.1f at %d %d from_host=(%.1f %.1f) gone_by=0x%08X %.1f s ago matings=%u egg_lays=%u pos=(%.1f %.1f) model=%08X",
                 s ? F<uint8_t>(s, 0x68) : -1, s ? F<uint8_t>(s, 0x69) : -1, mate_idx(), m ? 1 : 0, m ? net::dist(pos_of(m), pos_of(av)) : -1.f,
                 (int)sc[0], (int)sc[1], rel.x, rel.y, g_mate_gone_by, g_mate_gone_at < 0 ? -1.0 : H->time_seconds() - g_mate_gone_at,
                 g_matings, g_egg_lays, m ? pos_of(m).x : 0.f, m ? pos_of(m).y : 0.f, m ? F<uint32_t>(m, kModelKey) : 0u);
        return b + mate_status();
    }
    if (a.size() >= 4 && a[0] == "near") { // tests (BUG-1246): the cells of this game within <r> m of <x y>
        net::Vec3 p{(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), 0.f};
        float rad = (float)atof(a[3].c_str());
        std::string o;
        char b[160];
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            net::Vec3 q = pos_of(c);
            float d = std::hypot(q.x - p.x, q.y - p.y);
            if (d > rad || o.size() > 3000)
                return;
            snprintf(b, sizeof b, "cell %08X model=%08X gfx=%d d=%.1f puppet=%d avatar=%d hp=%d\n", idx, F<uint32_t>(c, kModelKey),
                     F<uint32_t>(c, 0x248) ? 1 : 0, d, modes::cell_is_puppet(c) ? 1 : 0, c == av ? 1 : 0, F<int>(c, kHp));
            o += b;
        });
        return o.empty() ? "none" : o;
    }
    if (a.size() >= 4 && a[0] == "npcflag") { // host, tests: npcflag c<key> spare|invuln 0|1, hp <n> (BUG-0078)
        // spare: the NPC's species "won't attack the player" (resource data +0x300, all of its kind);
        // invuln: the NPC's own +0x111. Both make DamageCell skip a bite of the player on it.
        uint8_t* c = modes::cell_at((uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10));
        if (!c || !is_host() || modes::cell_is_puppet(c) || c == av)
            return "no such NPC (host only)";
        uint8_t v = a[3] == "1";
        if (a[2] == "hp") { // hp <n>: room for a few bites
            F<int>(c, kHp) = atoi(a[3].c_str());
            v = 1;
        } else if (a[2] == "invuln") {
            F<uint8_t>(c, kInvuln) = v;
        } else if (a[2] == "alert" && a_GetData && F<void*>(c, kRes)) { // BUG-1008: the species' death alert radius
            // (KillCell's alert 0xE71F00 runs only when the AI block's +0x2C is not 0: the fixture's level-1 kinds have 0)
            void* holder = nullptr;
            uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, F<void*>(c, kRes), &holder);
            if (holder)
                --F<int>(holder, 0xC);
            if (!res)
                return "no resource data";
            float r = (float)atof(a[3].c_str());
            F<float>(res, 0xE0 + 0x2C) = r;
            if (F<int>(res, 0x248) != -1)
                F<float>(res, 0x248 + 0x2C) = r;
            if (F<int>(res, 0x194) != -1)
                F<float>(res, 0x194 + 0x2C) = r;
            v = r != 0.f;
        } else if (a[2] == "spare" && a_GetData && F<void*>(c, kRes)) {
            void* holder = nullptr;
            uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, F<void*>(c, kRes), &holder);
            if (holder)
                --F<int>(holder, 0xC);
            if (!res)
                return "no resource data";
            F<uint8_t>(res, 0x300) = v;
        } else
            return "usage: coop.pool npcflag c<key> spare|invuln 0|1 | hp <n> | alert <radius>";
        char b[96];
        snprintf(b, sizeof b, "npcflag %s %s=%d hp=%d", a[1].c_str(), a[2].c_str(), v, F<int>(c, kHp));
        return b;
    }
    if (a.size() >= 2 && a[0] == "away") { // host, tests: Cell messages handled as in an editor (BUG-0239)
        g_test_away = a[1] == "1";
        return g_test_away ? "away 1" : "away 0";
    }
    if (a.size() >= 2 && a[0] == "statehold") { // guest, tests (BUG-0480): drop the host's MSG_CELL_STATE for <s> seconds
        double s = std::clamp(atof(a[1].c_str()), 0.0, 30.0);
        g_statehold_until = s > 0 ? H->time_seconds() + s : 0;
        char b[96];
        snprintf(b, sizeof b, "statehold %.1f s (dropped so far %u)", s, g_n.statehold_dropped);
        return b;
    }
    if (a.size() >= 3 && a[0] == "cull") { // host, tests (BUG-0480): DestroyCell on <n> NPC cells within <r> m of the nearest partner
        if (!is_host() || !avatar())
            return "host only";
        float r = (float)atof(a[1].c_str());
        int n = atoi(a[2].c_str());
        net::Vec3 at = pos_of(avatar());
        nearest_partner(1e9f, &at);
        std::vector<std::pair<float, uint32_t>> v;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (modes::cell_is_puppet(c) || c == avatar() || F<uint8_t>(c, kDying) || F<uint8_t>(c, kDead))
                return;
            float d = net::dist(pos_of(c), at);
            if (d <= r)
                v.push_back({d, idx});
        });
        std::sort(v.begin(), v.end());
        std::string o;
        int k = 0;
        for (auto& [d, idx] : v) {
            if (k >= n)
                break;
            o += " c" + std::to_string(idx);
            modes::cell_destroy(idx);
            k++;
        }
        return "culled=" + std::to_string(k) + o;
    }
    if (a.size() >= 2 && a[0] == "kodrop") { // tests: ignore MSG_CELL_KO for <s> seconds (a late refresh, BUG-0238)
        double s = std::clamp(atof(a[1].c_str()), 0.0, 30.0);
        g_kodrop_until = s > 0 ? H->time_seconds() + s : 0;
        return "kodrop " + std::to_string(s).substr(0, 4);
    }
    if (a.size() >= 2 && a[0] == "invulnfor") { // tests: invulnerable for <s> seconds, as after a respawn (BUG-0237)
        F<uint8_t>(av, kInvuln) = 1;
        g_invuln_until = H->time_seconds() + std::clamp(atof(a[1].c_str()), 0.0, 120.0);
        return "invulnerable until " + std::to_string(g_invuln_until);
    }
    if (a.size() >= 2 && a[0] == "invuln") { // tests: the avatar's invulnerability flag +0x111
        F<uint8_t>(av, kInvuln) = (uint8_t)atoi(a[1].c_str());
        return "invuln set";
    }
    if (a.size() >= 3 && a[0] == "cellhp" && is_host()) { // host, tests (BUG-1432): cellhp c<idx> <hp>: a cell's hp
        uint8_t* c = modes::cell_at((uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10));
        if (!c || c == av || modes::cell_is_puppet(c))
            return "no such cell";
        F<int>(c, kHp) = atoi(a[2].c_str());
        return "hp " + std::to_string(F<int>(c, kHp));
    }
    if (a.size() >= 3 && a[0] == "celllevel") { // tests (BUG-1514): celllevel c<idx> <lv>: a cell's level
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        } else
            c = modes::cell_at(key);
        if (!c || c == av || modes::cell_is_puppet(c))
            return "no such cell";
        F<int>(c, kLevel) = std::clamp(atoi(a[2].c_str()), 0, 16);
        return "level " + std::to_string(F<int>(c, kLevel));
    }
    if (a.size() >= 2 && a[0] == "hp") {
        F<int>(av, kHp) = atoi(a[1].c_str());
        return "hp set";
    }
    if (a.size() >= 3 && a[0] == "goto") { // swim to <dx dy> from where we are (like a mouse click)
        net::Vec3 p = pos_of(av);
        auto* t = (float*)(av + kTarget);
        t[0] = p.x + (float)atof(a[1].c_str());
        t[1] = p.y + (float)atof(a[2].c_str());
        t[2] = p.z;
        F<uint8_t>(av, 0x04) = 0;
        return "swimming";
    }
    if (!a.empty() && a[0] == "scalepos") { // tests (BUG-0519): world scale, own cell and every partner's puppet, one frame
        std::string o;
        char b[96];
        net::Vec3 me = pos_of(av);
        snprintf(b, sizeof b, "scale=%.6g me=(%.2f %.2f)", world_scale(), me.x, me.y);
        o = b;
        for (int id = 0; id < 8; id++)
            if (uint8_t* pc = puppet_cell(id)) {
                net::Vec3 q = pos_of(pc);
                snprintf(b, sizeof b, " p%d=(%.2f %.2f)", id, q.x, q.y);
                o += b;
            }
        return o;
    }
    if (a.size() >= 2 && a[0] == "stack2") { // tests (BUG-0307): two cells without a soft body onto one centre for <frames> steps
        if (a[1] == "?") {
            char b[200];
            snprintf(b, sizeof b, "stack2 frames=%d pairs=%u nf=0x%X vmax=%g", g_stack2_frames, g_n.stack2_pairs, g_stack2_nf, g_stack2_vmax);
            return b;
        }
        net::Vec3 me = pos_of(av);
        std::vector<std::pair<float, uint8_t*>> v;
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (c == av || modes::cell_is_puppet(c) || F<uint8_t>(c, kDying) || F<uint8_t>(c, kDead) || F<uint32_t>(c, 0x248) ||
                cell_nonfinite(c))
                return;
            net::Vec3 q = pos_of(c);
            float d = std::hypot(q.x - me.x, q.y - me.y);
            if (d > 30.f && std::hypot(q.x, q.y) > 50.f)
                v.push_back({d, c});
        });
        size_t want = a.size() >= 3 ? (size_t)std::max(2, std::min(8, atoi(a[2].c_str()))) : 2;
        if (v.size() < want)
            return "no cells";
        std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first < y.first; });
        g_stack2.clear();
        for (size_t i = 0; i < want; i++)
            g_stack2.push_back(F<uint32_t>(v[i].second, 0));
        g_stack2_a = g_stack2[0];
        g_stack2_nf = 0;
        g_stack2_vmax = 0;
        g_n.stack2_pairs = 0;
        g_stack2_frames = atoi(a[1].c_str());
        net::Vec3 q = pos_of(v[0].second);
        char b[200];
        snprintf(b, sizeof b, "stack2 %zu cells (c%u %s first) at (%.1f %.1f), %.0f m from ours, for %d steps", want, g_stack2_a,
                 kind_of(v[0].second), q.x, q.y, v[0].first, g_stack2_frames);
        return b;
    }
    if (a.size() >= 2 && a[0] == "stack") { // tests (BUG-0307): the nearest cell onto our own cell's centre for <frames> steps
        net::Vec3 me = pos_of(av);
        uint8_t* best = nullptr;
        float bd = 1e30f;
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (c == av || modes::cell_is_puppet(c) || F<uint8_t>(c, kDying))
                return;
            net::Vec3 q = pos_of(c);
            float d = std::hypot(q.x - me.x, q.y - me.y);
            if (std::isfinite(d) && d < bd)
                bd = d, best = c;
        });
        if (!best)
            return "no cell";
        g_stack_idx = F<uint32_t>(best, 0);
        g_stack_frames = atoi(a[1].c_str());
        char b[200];
        snprintf(b, sizeof b, "stack c%u %s %.1f m away onto (%.2f %.2f) for %d steps (pairs before: %u)", g_stack_idx, kind_of(best), bd, me.x,
                 me.y, g_stack_frames, g_n.stack_pairs);
        return b;
    }
    if (!a.empty() && a[0] == "camsnap") { // tests (BUG-1064): our camera onto our own cell (a still cell's camera stays where a rescale left it)
        if (!av)
            return "no cell\n";
        camera_snap(pos_of(av));
        return "camera on our cell\n";
    }
    if (!a.empty() && a[0] == "cam") { // tests (BUG-0343): the cell camera (cCellUI +0x48 drawn, +0x54 target, +0x60 lag)
        uint8_t* ui = a_CellUI ? *(uint8_t**)a_CellUI : nullptr;
        if (!ui || !skg::readable(ui + 0x48, 0xB0))
            return "no camera";
        auto* d = (float*)(ui + 0x48);
        auto* t = (float*)(ui + 0x54);
        auto* l = (float*)(ui + 0x60);
        net::Vec3 me = pos_of(av);
        char b[360];
        snprintf(b, sizeof b, "cam=(%.2f %.2f) target=(%.2f %.2f) lag=(%.2f %.2f) avatar=(%.2f %.2f) off=%.2f follow=%u bc=%.2f snaps=%u scale=%.3f heals=%u origin_holds=%u",
                 d[0], d[1], t[0], t[1], l[0], l[1], me.x, me.y, std::hypot(d[0] - me.x, d[1] - me.y),
                 F<uint32_t>(ui, 0xE0), F<float>(ui, 0xBC), g_n.camera_snaps, G() ? F<float>(G(), 0x514C) : 0.f, g_n.camera_heals, g_n.rescale_origin_holds);
        return b;
    }
    if (a.size() >= 2 && a[0] == "noowncloud") { // tests (BUG-0892): 1 = our own avatar makes no poison cloud
        g_test_no_own_cloud = atoi(a[1].c_str()) != 0;
        return "noowncloud " + std::to_string((int)g_test_no_own_cloud) + " held=" + std::to_string(g_n.own_clouds_held);
    }
    if (a.size() >= 3 && a[0] == "place" && !is_guest()) { // tests (BUG-0892): put the cell c<key> onto the cell c<to>
        // (a host creature onto a poison cloud: the game's own contact pair decides what it costs it)
        uint32_t k = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint32_t t = (uint32_t)strtoul(a[2].c_str() + (a[2][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = modes::cell_at(k);
        uint8_t* to = modes::cell_at(t);
        if (!c || !to)
            return "no such cell";
        net::Vec3 p = pos_of(to);
        stop_at(c, p);
        char b[120];
        snprintf(b, sizeof b, "placed c%u onto c%u at (%.2f %.2f)", k, t, p.x, p.y);
        return b;
    }
    if (a.size() >= 3 && a[0] == "tp") { // move our cell to <dx dy> from the nearest partner [or player <id>]
        net::Vec3 at;
        if (a.size() >= 4) { // tests with 3-4 players (BUG-0152): next to that player's cell as we draw it
            uint8_t* pc = puppet_cell(atoi(a[3].c_str()));
            if (!pc)
                return "no such player";
            at = pos_of(pc);
        } else if (nearest_partner(1e9f, &at).empty())
            return "no partner";
        stop_at(av, {at.x + (float)atof(a[1].c_str()), at.y + (float)atof(a[2].c_str()), 0.f});
        return "moved";
    }
    if (!a.empty() && a[0] == "staletarget") { // tests (BUG-0306): AI cells follow a group leader that holds a dead chase target
        // The fixture's groups (+0x38C, pool G+0xA8, leader at group+8) are clusters without AI: their
        // leader never runs the behaviours, so it never drops a target (a leader that skips its check,
        // as in the lab run). <n> AI cells join such a group; at their next pick (PickTargets 0xE70CF0)
        // they copy the leader's dead chase target.
        uint8_t* g = G();
        if (!g)
            return "no cell game\n";
        uint8_t* gdata = F<uint8_t*>(g, 0xA8);
        uint32_t ghigh = F<uint32_t>(g, 0xA8 + 0x10), gsize = F<uint32_t>(g, 0xA8 + 0x14);
        int max = a.size() >= 2 ? atoi(a[1].c_str()) : 8, followers = 0;
        if (!gdata || !gsize || gsize > 0x1000)
            return "no group pool\n";
        uint32_t gi = 0;
        uint8_t* lc = nullptr;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            uint32_t g1 = F<uint32_t>(c, 0x38C), gs = g1 & 0xFFFF;
            if (lc || !g1 || gs > ghigh || modes::cell_is_puppet(c))
                return;
            uint8_t* grp = gdata + (size_t)gs * gsize;
            uint32_t li = F<uint32_t>(grp, 8);
            uint8_t* l = F<uint32_t>(grp, 0) == g1 ? modes::cell_at(li) : nullptr;
            if (l && li != idx && ai_type(l) <= 0) {
                gi = g1;
                lc = l;
            }
        });
        if (!lc)
            return "staletarget: 0 followers (no group with a leader without AI)\n";
        uint32_t dead = F<uint32_t>(lc, 0) ^ 0x10000; // the leader's slot with another generation: a dead cell
        F<uint32_t>(lc, kChase) = dead;
        F<float>(lc, 0x230) = 60.f; // chase timer
        std::map<std::string, int> types;
        for_each_cell([&](uint8_t* c, uint32_t) {
            int at = ai_type(c);
            if (followers >= max || at <= 0 || c == lc || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
                return;
            F<uint32_t>(c, 0x38C) = gi;
            F<float>(c, 0x224) = F<float>(c, 0x228) = F<float>(c, 0x21C) = 0.f; // free to chase at its next pick
            F<float>(c, 0x238) = 0.f;
            char k[16];
            snprintf(k, sizeof k, "%X", at);
            types[k]++;
            followers++;
        });
        std::string ts;
        for (auto& [t, n] : types)
            ts += " " + t + ":" + std::to_string(n);
        char b[160];
        snprintf(b, sizeof b, "staletarget: %d followers of leader c%u chasing dead c%u, stale_targets=%u, follower ai types", followers,
                 F<uint32_t>(lc, 0), dead, g_n.stale_targets);
        return b + ts + "\n";
    }
    if (!a.empty() && a[0] == "parts") { // RE (BUG-0355): who holds the soft-body particles
        // cells by tier and distance from our cell, mirrors apart, holders of the spare slot, how far
        // the spare and the cells' particles are from their cells
        net::Vec3 me = pos_of(av);
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        int tiers[4] = {}, held = 0, mir_held = 0, spare_cells = 0, spare_refs = 0, own_spare = 0, near_held = 0, far_held = 0;
        float far_part = 0;
        int nan_cells = 0, nan_parts = 0;
        if (parts)
            for (int k = 0; k < kParticles; k++)
                nan_parts += !std::isfinite(((float*)(parts + 4 + 12 * k))[0]) || !std::isfinite(((float*)(parts + 4 + 12 * k))[1]);
        int t0_big = 0, t0_stamp = 0, t0_nogfx = 0;
        uint32_t stamp = G() ? F<uint32_t>(G(), 0x4100) : 0;
        int near_bare = 0; // cells within 10 m with a graphics object and a tier but no particles (the "no particles" form)
        auto scan = [&](uint8_t* c, bool mirror) {
            int n = F<int>(c, 0x250), t = F<int>(c, 0x24C);
            if (c != av && F<uint32_t>(c, 0x248) && n == 1 && t >= 1) {
                net::Vec3 q = pos_of(c);
                near_bare += std::hypot(q.x - me.x, q.y - me.y) < 10.f;
            }
            if (t == 0) {
                t0_big += F<float>(c, kSize) >= 0.3f;
                t0_stamp += F<uint32_t>(c, 0x35C) == stamp;
                t0_nogfx += !F<uint32_t>(c, 0x248);
            }
            nan_cells += !std::isfinite(F<float>(c, 0x4C)) || !std::isfinite(F<float>(c, 0x50));
            if (t >= 0 && t < 4)
                tiers[t]++;
            if (!F<uint32_t>(c, 0x248) || n < 2 || n > 64)
                return;
            net::Vec3 p = pos_of(c);
            float d = std::hypot(p.x - me.x, p.y - me.y);
            held += n;
            if (mirror)
                mir_held += n;
            (d < 60.f ? near_held : far_held) += n;
            int s = 0;
            for (int i = 0; i < n; i++) {
                int k = F<int>(c, 0x254 + 4 * i);
                s += k == kSpare;
                if (parts && k >= 0 && k < kParticles) {
                    auto* q = (float*)(parts + 4 + 12 * k);
                    far_part = std::max(far_part, std::hypot(q[0] - p.x, q[1] - p.y));
                }
            }
            spare_refs += s;
            spare_cells += s > 0;
            if (c == av)
                own_spare = s;
        };
        for_each_cell([&](uint8_t* c, uint32_t idx) { scan(c, g_host_of.count(idx) > 0); });
        scan(av, false);
        float* sp = parts ? (float*)(parts + 4 + 12 * kSpare) : nullptr;
        static uintptr_t a_box = H->sym("Simulator::Cell::ActiveBox");
        auto* box = (const float*)a_box;
        char bx[200] = "";
        if (box)
            snprintf(bx, sizeof bx, " active_box=%.0fx%.0f m", box[3] - box[0], box[4] - box[1]);
        if (uint32_t* gp = gfx_pool())
            snprintf(bx + strlen(bx), sizeof bx - strlen(bx), " gfx=%u/%u peak=%u soft_made=%u soft_refused=%u count0=%u", gp[3], gp[6], g_n.gfx_peak,
                     g_n.soft_made, g_n.soft_refused, g_n.soft_count0);
        char b[800];
        snprintf(b, sizeof b,
                 "parts: used=%d held=%d (mirrors %d, within 60 m %d, beyond %d) tiers=%d/%d/%d/%d spare_cells=%d spare_refs=%d own_spare=%d "
                 "own_n=%d spare_at=(%.0f %.0f) farthest_particle=%.0f m nan_cells=%d nan_particles=%d room_runs=%u stripped=%u spare_freed=%u spare=%u tier0: size>=0.3 %d, +0x35C==G+0x4100 %d, no +0x248 %d near_bare=%d bare_moves=%u%s\n",
                 particles_used(), held, mir_held, near_held, far_held, tiers[0], tiers[1], tiers[2], tiers[3], spare_cells, spare_refs,
                 own_spare, F<int>(av, 0x250), sp ? sp[0] : 0.f, sp ? sp[1] : 0.f, far_part, nan_cells, nan_parts, g_n.part_room_runs, g_n.part_stripped,
                 g_n.part_spare_freed, g_part_spare, t0_big, t0_stamp, t0_nogfx, near_bare, g_n.bare_moves, bx);
        return b;
    }
    if (!a.empty() && a[0] == "orphans") { // RE (BUG-0355): used particles no cell's list holds
        uint8_t* g = G();
        uint8_t* parts = g ? F<uint8_t*>(g, 0x4108) : nullptr;
        uint8_t* data = g ? F<uint8_t*>(g, 0x1C) : nullptr;
        if (!parts || !data)
            return "orphans: no pool\n";
        std::vector<int> holders(kParticles, 0);
        uint32_t nslots = F<uint32_t>(g, 0x1C + 0x10);
        int big_lists = 0, nogfx_lists = 0;
        for (uint32_t slot = 0; slot < nslots && slot < 4096; slot++) {
            uint8_t* c = data + (size_t)slot * 0x398;
            if ((F<uint32_t>(c, 0) & 0xFFFF) != slot)
                continue;
            int k = F<int>(c, 0x250);
            if (k > 64) {
                big_lists++;
                continue;
            }
            if (k > 1 && !F<uint32_t>(c, 0x248))
                nogfx_lists++;
            if (k > 1)
                for (int i = 0; i < k; i++) {
                    int j = F<int>(c, 0x254 + 4 * i);
                    if (j >= 0 && j < kParticles)
                        holders[j]++;
                }
        }
        int high = F<int>(parts, 0x1BD58), used = F<int>(parts, 0x1BD54), owned = 0, orphans = 0, dead_owner = 0, own_av = 0, multi = 0;
        // the free list: slots on it are free
        std::vector<char> on_free(kParticles + 1, 0);
        int head = F<int>(parts, 0x1BD5C), free_n = 0;
        for (int guard = 0; head >= 0 && head < kParticles && !on_free[head] && guard < kParticles; guard++) {
            on_free[head] = 1;
            free_n++;
            head = F<int>(parts, 0x1A5E4 + 4 * head);
        }
        uint32_t avi = avatar_idx();
        std::map<uintptr_t, int> sites;
        for (int i = 0; i <= high && i < kParticles; i++) {
            if (on_free[i])
                continue;
            uint32_t ow = F<uint32_t>(parts, 0x18E74 + 4 * i);
            owned += ow != 0;
            multi += holders[i] > 1;
            if (holders[i] == 0 && ow) {
                orphans++;
                own_av += ow == avi;
                dead_owner += !modes::cell_at(ow);
                sites[g_part_site[i] ? g_part_site[i] - H->image_base() + 0x400000 : 0]++;
            }
        }
        std::string st;
        for (auto& [a_, n_] : sites)
            st += " " + std::to_string(n_) + "@" + [&] { char h[16]; snprintf(h, sizeof h, "%08X", (unsigned)a_); return std::string(h); }();
        char b[300];
        snprintf(b, sizeof b, "orphans: used=%d high=%d on_free_list=%d owned=%d orphans=%d (owner gone %d, owner our cell %d) held_twice=%d lists_over_64=%d lists_without_gfx=%d\n",
                 used, high, free_n, owned, orphans, dead_owner, own_av, multi, big_lists, nogfx_lists);
        std::string first = b;
        snprintf(b, sizeof b, "  destroys of cells with particles: %u, particles %u, given back %u, without +0x248 %u, still alive after %u\n",
                 g_n.dbg_destroys, g_n.dbg_expect, g_n.dbg_freed, g_n.dbg_nogfx, g_n.dbg_alive);
        std::string by;
        for (auto& [ra, n_] : g_dbg_destroy_by) {
            char h[96], mod[80];
            coop_module_of(ra, mod, sizeof mod);
            snprintf(h, sizeof h, " %u@%s", n_, mod);
            by += h;
        }
        return first + std::string(b) + "  orphans allocated from:" + st + "\n  DestroyCell from:" + by + "\n";
    }
    if (a.size() >= 2 && a[0] == "particles") { // tests (BUG-0338): fill the soft-body particle pool up to <n> used (owner: our cell)
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        int want = std::min(atoi(a[1].c_str()), kParticles - 1), made = 0;
        net::Vec3 me = pos_of(av);
        float p[3] = {me.x, me.y, me.z}, v[3] = {0, 0, 0};
        while (parts && o_PartAlloc && F<int>(parts, 0x1BD54) < want && made < kParticles) {
            if (o_PartAlloc(parts, p, v, 1.f, (int)avatar_idx()) < 0)
                break;
            made++;
        }
        char b[96];
        snprintf(b, sizeof b, "particles: %d made, %d of %d used\n", made, particles_used(), kParticles);
        return b;
    }
    if (!a.empty() && a[0] == "collide") { // tests (BUG-0342): Collisions 0xE7F260's tests for our cell's pairs
        uint8_t* av = avatar();
        uint8_t* g = G();
        uint8_t* sap = g ? F<uint8_t*>(g, 0x4104) : nullptr;
        if (!av || !sap)
            return "collide: no avatar / sap\n";
        // the bounding sphere 0xE6D790 builds for the narrow test
        auto sphere = [&](uint8_t* c, float* ctr) -> float {
            float sc = F<float>(c, 0x58);
            auto* k = (uint32_t*)(c + kModelKey);
            if (!k[0]) {
                ctr[0] = F<float>(c, 0x4C), ctr[1] = F<float>(c, 0x50), ctr[2] = F<float>(c, 0x54);
                return sc;
            }
            uint8_t* md = a_ModelData ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
            if (!md)
                return -1.f;
            float v[3] = {F<float>(md, 0x1238), F<float>(md, 0x123C), F<float>(md, 0x1240)};
            if (F<uint8_t>(c, 0x48) & 2) {
                float* m = (float*)(c + 0x5C);
                float x = v[0], y = v[1], z = v[2];
                v[0] = m[3] * y + m[6] * z + x * m[0];
                v[1] = m[1] * x + m[4] * y + m[7] * z;
                v[2] = m[2] * x + m[5] * y + m[8] * z;
            }
            for (int i = 0; i < 3; i++)
                ctr[i] = F<float>(c, 0x4C + 4 * i) + v[i] * sc;
            return F<float>(md, 0x1244) * sc;
        };
        uint32_t me = F<uint32_t>(av, 0);
        int np = F<int>(sap, 8);
        int* pairs = F<int*>(sap, 0xC);
        std::string out;
        char b[300];
        int n = 0;
        for (int i = 0; pairs && i < np && i < 8192; i++) {
            uint32_t x = (uint32_t)pairs[2 * i], y = (uint32_t)pairs[2 * i + 1];
            if (x != me && y != me)
                continue;
            uint32_t other = x == me ? y : x;
            uint8_t* ca = modes::cell_at(me);
            uint8_t* cb = modes::cell_at(other);
            float pa[3] = {}, pb[3] = {};
            float ra = ca ? sphere(ca, pa) : -1.f, rb = cb ? sphere(cb, pb) : -1.f;
            float d2 = (pa[0] - pb[0]) * (pa[0] - pb[0]) + (pa[1] - pb[1]) * (pa[1] - pb[1]) + (pa[2] - pb[2]) * (pa[2] - pb[2]);
            snprintf(b, sizeof b, "pair me=%08X(%s puppet=%d) other=%08X(%s %s) ra=%.3f rb=%.3f d=%.3f narrow=%d me_obj=%p av=%p\n", me,
                     ca ? "found" : "GONE", ca ? (int)modes::cell_is_puppet(ca) : -1, other, cb ? "found" : "GONE", cb ? kind_of(cb) : "-", ra, rb, std::sqrt(d2),
                     ca && cb && d2 < (ra + rb) * (ra + rb), (void*)ca, (void*)av);
            out += b;
            n++;
        }
        snprintf(b, sizeof b, "collide: %d pairs with our cell %08X of %d, own_pairs=%u\n", n, me, np, g_n.own_pairs);
        return out + b;
    }
    if (!a.empty() && a[0] == "sapme") { // tests (BUG-0342): our own cell's broadphase object [or c<key>'s]
        uint8_t* av = avatar();
        if (a.size() > 1) {
            uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
            auto it = g_mirrors.find(key);
            av = is_guest() ? (it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr)
                            : modes::cell_at(key);
        }
        uint8_t* g = G();
        uint8_t* sap = g ? F<uint8_t*>(g, 0x4104) : nullptr;
        if (!av || !sap)
            return "sapme: no avatar / sap";
        int id = F<int>(av, 0x364), cap = F<int>(sap, 4);
        char b[400];
        if (id < 0 || id >= cap)
            return "sapme: avatar " + std::to_string(F<uint32_t>(av, 0)) + " sap id " + std::to_string(id);
        uint8_t* o = F<uint8_t*>(sap, 0x14) + 16 * id;
        uint16_t e[4] = {F<uint16_t>(o, 8), F<uint16_t>(o, 10), F<uint16_t>(o, 12), F<uint16_t>(o, 14)};
        auto val = [&](int axis, uint16_t ei) -> float {
            uint8_t* ep = F<uint8_t*>(sap, (ei & 0x8000 ? 0x30 : 0x24) + 4 * axis);
            return F<float>(ep, 8 * (ei & 0x7FFF) + 4);
        };
        auto* k = (uint32_t*)(av + kModelKey);
        uint8_t* md = a_ModelData && k[0] ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
        int np = F<int>(sap, 8), mine = 0;
        int* pairs = F<int*>(sap, 0xC);
        for (int i = 0; pairs && i < np && i < cap; i++)
            mine += pairs[2 * i] == F<int>(o, 0) || pairs[2 * i + 1] == F<int>(o, 0);
        auto who = [&](int axis, uint16_t ei) -> int {
            uint8_t* ep = F<uint8_t*>(sap, (ei & 0x8000 ? 0x30 : 0x24) + 4 * axis);
            return F<int16_t>(ep, 8 * (ei & 0x7FFF) + 2);
        };
        snprintf(b, sizeof b, "sapme: avatar %u idx=%08X sapid=%d obj=(%08X %08X) e=%04X/%04X %04X/%04X x=%.2f..%.2f y=%.2f..%.2f "
                 "owners=%d/%d/%d/%d pos=(%.2f %.2f) size=%.2f grp=%08X live=%d pairs_with_me=%d/%d md_parts=%d md_radius=%.3f md_centre=(%.2f %.2f) model=%08X",
                 F<uint32_t>(av, 0), F<uint32_t>(av, 0), id, F<uint32_t>(o, 0), F<uint32_t>(o, 4), e[0], e[1], e[2], e[3],
                 val(0, e[0]), val(0, e[2]), val(1, e[1]), val(1, e[3]), who(0, e[0]), who(0, e[2]), who(1, e[1]), who(1, e[3]),
                 pos_of(av).x, pos_of(av).y, F<float>(av, kSize), F<uint32_t>(av, 0x1B0), F<int>(sap, 0x10), mine, np, md ? F<int>(md, 0) : -1, md ? F<float>(md, 0x1244) : -1.f, md ? F<float>(md, 0x1238) : 0.f,
                 md ? F<float>(md, 0x123C) : 0.f, k[0]);
        std::string out = b;
        float* m = (float*)(av + 0x5C);
        char c2[300];
        snprintf(c2, sizeof c2, " flags48=%04X scale58=%.3f m=(%.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f) p4c=(%.2f %.2f %.2f)",
                 F<uint16_t>(av, 0x48), F<float>(av, 0x58), m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8],
                 F<float>(av, 0x4C), F<float>(av, 0x50), F<float>(av, 0x54));
        return out + c2;
    }
    if (!a.empty() && a[0] == "sap") // the cell broadphase's axis-0 sweep, as UpdatePairs would run it (BUG-0337)
        return sap_report() + "\n";
    if (a.size() >= 3 && a[0] == "crowd") { // tests (BUG-0355): <n> of our cells on a disc of <r> m around us (a crowd on screen)
        net::Vec3 me = pos_of(av);
        int want = atoi(a[1].c_str()), moved = 0;
        float r = (float)atof(a[2].c_str());
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (moved >= want || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
                return;
            // a sunflower spiral: even density, no two cells on one spot, none on our own (from 3 m)
            float t = (float)(moved + 1) / (float)std::max(want, 1), ang = 2.39996323f * (float)moved;
            float d = 3.f + (r - 3.f) * std::sqrt(t);
            set_pos(c, {me.x + d * std::cos(ang), me.y + d * std::sin(ang), me.z});
            moved++;
        });
        char b[96];
        snprintf(b, sizeof b, "crowd: %d cells moved\n", moved);
        H->log(M, SK_INFO, "cell: %s", b);
        return b;
    }
    if (a.size() >= 2 && a[0] == "column") { // tests (BUG-0337): <n> of our cells on one vertical line next to us (all open at once in the sweep)
        net::Vec3 me = pos_of(av);
        int want = atoi(a[1].c_str()), moved = 0;
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (moved >= want || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
                return;
            // 40 m tall, 8 m to our right: inside our active area (cells taken out of it are culled)
            set_pos(c, {me.x + 8.f, me.y - 20.f + 40.f * (float)moved / (float)std::max(want, 1), me.z});
            moved++;
        });
        char b[96];
        snprintf(b, sizeof b, "column: %d cells moved\n", moved);
        H->log(M, SK_INFO, "cell: %s", b);
        return b;
    }
    if (!a.empty() && a[0] == "nanmirror" && is_guest()) { // tests (BUG-1006): `nanmirror [c<key>]`: a soft mirror's particles NaN
        uint32_t want = a.size() > 1 ? (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10) : 0;
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        uint8_t* best = nullptr;
        uint32_t key = 0;
        float bd = 1e30f;
        for (auto& [h, m] : g_mirrors) {
            uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
            int n = c ? F<int>(c, 0x250) : 0;
            if (!c || !F<uint32_t>(c, 0x248) || n < 2 || n > 64 || (want && h != want) || cell_nonfinite(c) ||
                F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) || strcmp(kind_of(c), "creature"))
                continue;
            float d = net::dist(pos_of(c), pos_of(av));
            if (d < bd) {
                bd = d;
                best = c;
                key = h;
            }
        }
        if (!best || !parts)
            return "nanmirror: no soft mirror\n";
        float nan = std::nanf("");
        int n = F<int>(best, 0x250);
        for (int i = 0; i < n; i++) {
            uint32_t k = F<uint32_t>(best, 0x254 + 4 * i);
            if (k < 1500)
                for (int j = 0; j < 3; j++)
                    ((float*)(parts + 4 + 12 * (size_t)k))[j] = nan;
        }
        char b[96];
        snprintf(b, sizeof b, "nanmirror c%u: %d particles NaN at %.1f m\n", key, n, bd);
        return b;
    }
    if (a.size() >= 2 && a[0] == "mirrornf" && is_guest()) { // tests (BUG-1006): a mirror's game state: non-finite fields, centre, particle mean
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        auto it = g_mirrors.find(key);
        uint8_t* c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        if (!c)
            return "mirrornf: gone\n";
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        int n = F<int>(c, 0x250), used = 0;
        double mx = 0, my = 0;
        if (parts && F<uint32_t>(c, 0x248) && n > 1 && n <= 64)
            for (int i = 0; i < n; i++) {
                uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
                if (k >= 1500)
                    continue;
                mx += ((float*)(parts + 4 + 12 * (size_t)k))[0];
                my += ((float*)(parts + 4 + 12 * (size_t)k))[1];
                used++;
            }
        if (used) {
            mx /= used;
            my /= used;
        }
        char b[240];
        snprintf(b, sizeof b, "mirrornf c%u nf=0x%X pos=(%.2f %.2f) pmean=(%.2f %.2f) parts=%d heals=%u turns_skipped=%u\n", key,
                 cell_nonfinite(c), pos_of(c).x, pos_of(c).y, mx, my, used, g_n.mirror_nan_heals, g_n.mirror_nan_turns);
        return b;
    }
    if (!a.empty() && a[0] == "rebuild" && is_host() && G()) { // tests (BUG-1000): the game rebuilds the level next frame
        // (G+0x51D8, the respawn's request: RebuildLevel 0xE7FD00 clears the tile pool, docs/re/cell.md)
        F<uint8_t>(G(), 0x51D8) = 1;
        return "rebuild requested\n";
    }
    if (a.size() >= 2 && a[0] == "viewedge" && is_host()) { // tests (BUG-0999): the host's view box at the 1e7 edge for <n> tile passes
        g_test_view_edge = std::clamp(atoi(a[1].c_str()), 0, 600);
        return "view box at the 1e7 edge for " + std::to_string(g_test_view_edge) + " tile passes\n";
    }
    if (!a.empty() && a[0] == "areas" && is_host()) { // tests (BUG-1064): the host's own area and each partner's
        float* vb = view_boxes();
        if (a.size() > 1 && a[1] == "mark") {
            g_area_mark.clear();
            g_centre_err_max = g_centre_err_moving = 0;
            memset(g_area_cull, 0, sizeof g_area_cull);
            g_area_skips0 = g_n.tilecap_skips;
            if (!vb)
                return "areas: no view boxes\n";
            float hx = (vb[3] - vb[0]) * 0.5f, hy = (vb[4] - vb[1]) * 0.5f;
            std::vector<std::pair<int, net::Vec3>> ctr{{0, {(vb[0] + vb[3]) * 0.5f, (vb[1] + vb[4]) * 0.5f, 0.f}}};
            for (int id = 1; id < 4; id++)
                if (uint8_t* pc = puppet_cell(id))
                    ctr.push_back({id, pos_of(pc)});
            uint32_t n[4] = {};
            for_each_cell([&](uint8_t* c, uint32_t idx) {
                if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
                    return;
                net::Vec3 p = pos_of(c);
                float r = 1.7f * F<float>(c, kSize);
                if (!std::isfinite(r))
                    return;
                for (auto& [id, q] : ctr)
                    if (std::fabs(p.x - q.x) <= hx + r && std::fabs(p.y - q.y) <= hy + r) {
                        uint8_t ring = (uint8_t)std::min(6.f, 6.f * std::max(std::fabs(p.x - q.x) / hx, std::fabs(p.y - q.y) / hy));
                        int lv = F<int32_t>(c, kLevel) - cell_logic::level_of_food(S() ? F<int32_t>(S(), 0x1C) : 0);
                        g_area_mark[idx] = {(uint8_t)id, (uint8_t)(modes::cell_at(F<uint32_t>(c, 0x36C)) != nullptr), ring,
                                            (int8_t)std::clamp(lv, -9, 9), 0};
                        n[id]++;
                        break;
                    }
            });
            char b[160];
            snprintf(b, sizeof b, "areas marked host=%u p1=%u p2=%u p3=%u box=%.1fx%.1f\n", n[0], n[1], n[2], n[3], hx, hy);
            return b;
        }
        // per area: marked, gone, gone by RemoveCellsOfOldScale / by another DestroyCell caller / without DestroyCell
        std::map<int, std::array<uint32_t, 4>> st;
        std::map<int, std::map<uint32_t, uint32_t>> by;
        for (auto& [idx, m] : g_area_mark) {
            auto& v = st[m.area];
            v[0]++;
            uint8_t* c = modes::cell_at(idx);
            if (c && !F<uint8_t>(c, kDead) && !F<uint8_t>(c, kDying))
                continue;
            v[1]++;
            if (m.by)
                by[m.area][m.by]++;
            else
                v[3]++;
        }
        std::string out;
        { // tests (BUG-1064): per area, culled (0xE7C3E6) / marked by the cell's ring at the mark (sixths of the box half),
          // by its level against the food level (-2 .. +3), and of the cells with a live parent cell
            uint32_t rm[4][7] = {}, rc[4][7] = {}, lm[4][6] = {}, lc[4][6] = {}, pm[4] = {}, pcl[4] = {};
            uint32_t cull = a_CullRet ? (uint32_t)(a_CullRet - H->rebase(0)) : 0;
            for (auto& [idx, m] : g_area_mark)
                if (m.area < 4) {
                    bool culled = cull && m.by == cull;
                    rm[m.area][std::min<int>(m.ring, 6)]++, rc[m.area][std::min<int>(m.ring, 6)] += culled;
                    int l = std::clamp(m.lvl + 2, 0, 5);
                    lm[m.area][l]++, lc[m.area][l] += culled;
                    pm[m.area] += m.parent, pcl[m.area] += m.parent && culled;
                }
            for (int k = 0; k < 4; k++) {
                if (!(rm[k][0] + rm[k][1] + rm[k][2] + rm[k][3] + rm[k][4] + rm[k][5] + rm[k][6]))
                    continue;
                std::string ln = std::string("rings ") + (k ? "p" : "host") + std::to_string(k);
                for (int r = 0; r < 7; r++)
                    ln += " " + std::to_string(rc[k][r]) + "/" + std::to_string(rm[k][r]);
                ln += " levels";
                for (int l = 0; l < 6; l++)
                    ln += " " + std::to_string(lc[k][l]) + "/" + std::to_string(lm[k][l]);
                ln += " parent " + std::to_string(pcl[k]) + "/" + std::to_string(pm[k]) + "\n";
                out += ln;
            }
        }
        for (auto& [area, v] : st) {
            char b[96];
            snprintf(b, sizeof b, "area %s%d marked=%u gone=%u nodestroy=%u by", area ? "p" : "host", area, v[0], v[1], v[3]);
            out += b;
            for (auto& [ra, k] : by[area]) {
                snprintf(b, sizeof b, " %08X:%u", ra, k);
                out += b;
            }
            out += "\n";
        }
        { // where the host keeps the world round each partner, and where the partner's cell is
            char b[200];
            snprintf(b, sizeof b, "centre_err settled %.1f moving %.1f\n", g_centre_err_max, g_centre_err_moving);
            out += b;
            // the cull of marked cells: the age of the boxes it read, the area's box off its cell, the culled tile's
            // level against the food level, and whether the host's box moved onto the area's cell kept it
            for (int k = 0; k < 4; k++) {
                auto& c = g_area_cull[k];
                if (!c.n)
                    continue;
                snprintf(b, sizeof b, "cull %s%d n=%u stale=%u maxage=%u maxoff=%.1f maxrel=%.2f fresh_ok=%u box_ok=%u union_ok=%u layer1=%u lvl=%u/%u/%u/%u/%u/%u/%u/%u\n",
                         k ? "p" : "host", k, c.n, c.stale, c.maxage, c.maxoff, c.maxrel, c.fresh_ok, c.box_ok, c.union_ok, c.layer1, c.lvl[0], c.lvl[1], c.lvl[2], c.lvl[3],
                         c.lvl[4], c.lvl[5], c.lvl[6], c.lvl[7]);
                out += b;
            }
            snprintf(b, sizeof b, "passes skipped by the tile cap %u\n", g_n.tilecap_skips - g_area_skips0);
            out += b;
            for (auto& v : g_views.list()) {
                uint8_t* pc = v.player ? puppet_cell(v.player) : avatar();
                auto pt = g_ptiles.find(v.player);
                int n = pt != g_ptiles.end() && pt->second.list ? (int)((pt->second.list->end - pt->second.list->begin) / 2) : -1;
                snprintf(b, sizeof b, "centre %d (%.1f %.1f) cell (%.1f %.1f) tiles %d scale %.4f target %.4f\n", v.player, v.pos.x, v.pos.y,
                         pc ? pos_of(pc).x : 0.f, pc ? pos_of(pc).y : 0.f, n, world_scale(), F<float>(G(), 0x5150));
                out += b;
            }
        }
        return out.empty() ? std::string("areas: none marked\n") : out;
    }
    if (!a.empty() && a[0] == "tilestat") { // tests (BUG-0999, BUG-1000): the host's tile bookkeeping counters
        char b[320];
        snprintf(b, sizeof b, "tilestat views_empty_skips=%u flushes=%u handed=%u handed_flushed=%u resets=%u between_warns=%u "
                 "check_warns=%u passes=%u lists=%u views=%u unshared=%u scale_flushes=%u\n", g_n.views_empty_skips, g_n.tiles_flush, g_n.tiles_handed, g_n.handed_flushed,
                 g_n.tile_resets, g_n.tile_check_warns_between, g_n.tile_checks_bad, g_n.tile_passes, (unsigned)g_ptiles.size(), (unsigned)g_views.size(),
                 g_n.tiles_unshared, g_n.tiles_scale_flush);
        return std::string(b) + tiles_shadow_status(); // BUG-1510
    }
    if (!a.empty() && a[0] == "lootbig") { // tests (BUG-1004): `lootbig <n>`: the next foreground loot drop makes <n> cells; `lootbig`: the counters
        if (a.size() > 1)
            g_loot_big_n = (uint32_t)std::clamp(atoi(a[1].c_str()), 0, 1000);
        char b[160];
        snprintf(b, sizeof b, "lootbig armed=%u calls=%u cells=%u listed_last=%d listed_big=%d pool=%u tables=", g_loot_big_n, g_n.loot_calls,
                 g_n.loot_cells, g_loot_listed_last, g_loot_listed_big, pool_live());
        return b + g_loot_seen + "\n";
    }
    if (a.size() >= 4 && a[0] == "sendhit" && is_guest()) { // tests (BUG-1008): `sendhit c<key> <dmg> <type>`: a MSG_CELL_HIT as it is
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        send_hit(key, atoi(a[2].c_str()), atoi(a[3].c_str()));
        return "hit sent\n";
    }
    if (a.size() >= 3 && a[0] == "herd" && is_host()) { // tests (BUG-1008): `herd c<key> <n>`: <n> cells of its species within 4 m of it
        uint8_t* v = modes::cell_at((uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10));
        if (!v || v == av || modes::cell_is_puppet(v))
            return "herd: no such NPC\n";
        void* kind = F<void*>(v, kRes); // KillCell's alert takes the cells of the same resource
        int want = atoi(a[2].c_str()), moved = 0;
        net::Vec3 at = pos_of(v);
        std::string idx;
        for_each_cell([&](uint8_t* c, uint32_t i) {
            if (moved >= want || c == v || c == av || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) ||
                F<void*>(c, kRes) != kind || F<void*>(c, kQuery) != fg_query())
                return;
            float ang = 6.2831853f * (float)moved / (float)std::max(want, 1);
            set_pos(c, {at.x + 3.f * std::cos(ang), at.y + 3.f * std::sin(ang), at.z});
            idx += " " + std::to_string(i);
            moved++;
        });
        return "herd: " + std::to_string(moved) + " moved:" + idx + "\n";
    }
    if (a.size() >= 2 && a[0] == "targets") { // tests (BUG-1008): `targets <idx,...>`: each cell's flee (+0x220) and chase (+0x22C) target
        std::string out;
        size_t p0 = 0;
        const std::string& l = a[1];
        while (p0 <= l.size()) {
            size_t p1 = l.find(',', p0);
            if (p1 == std::string::npos)
                p1 = l.size();
            uint32_t i = (uint32_t)strtoul(l.substr(p0, p1 - p0).c_str(), nullptr, 10);
            if (uint8_t* c = i ? modes::cell_at(i) : nullptr) {
                char b[128];
                snprintf(b, sizeof b, "target %u flee=%u flee_t=%.2f chase=%u\n", i, F<uint32_t>(c, 0x220), F<float>(c, 0x21C),
                         F<uint32_t>(c, 0x22C));
                out += b;
            }
            p0 = p1 + 1;
        }
        char b[96];
        snprintf(b, sizeof b, "avatar=%u chunk_kills=%u puppets=", avatar_idx(), g_n.chunk_kills);
        out += b;
        if (C.puppets)
            for (auto& pp : C.puppets())
                if (pp.cell)
                    out += std::to_string(pp.id) + ":" + std::to_string(F<uint32_t>(pp.cell, 0)) + " ";
        return out + "\n";
    }
    if (!a.empty() && a[0] == "nanself") { // tests (BUG-0299): our own cell and its particles NaN, as seen in a 4-player run
        uint8_t* av = avatar();
        if (!av)
            return "no avatar\n";
        if (a.size() >= 2 && a[1] == "vel") { // BUG-0604: velocity NaN inside the next <n> cell steps (default 17)
            g_nan_vel_frames = a.size() >= 3 ? std::clamp(atoi(a[2].c_str()), 1, 600) : 17;
            return "own cell velocity NaN for " + std::to_string(g_nan_vel_frames) + " steps\n";
        }
        float nan = std::nanf("");
        int n = F<int>(av, 0x250);
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        if (F<uint32_t>(av, 0x248) && parts && n > 1 && n <= 64)
            for (int i = 0; i < n; i++) {
                uint32_t k = F<uint32_t>(av, 0x254 + 4 * i);
                if (k < 1500)
                    for (int j = 0; j < 3; j++)
                        ((float*)(parts + 4 + 12 * (size_t)k))[j] = nan;
            }
        for (int j = 0; j < 2; j++) {
            ((float*)(av + 0x4C))[j] = nan;
            ((float*)(av + 0x84))[j] = nan;
        }
        if (a.size() >= 2 && a[1] == "all") { // BUG-0307: also the fields the first heal left alone
            for (int j = 0; j < 9; j++)
                ((float*)(av + kRot))[j] = ((float*)(av + 0x24))[j] = nan;
            if (F<uint32_t>(av, 0x248) && parts && n > 1 && n <= 64)
                for (int i = 0; i < n; i++) {
                    uint32_t k = F<uint32_t>(av, 0x254 + 4 * i);
                    if (k < 1500)
                        for (int j = 0; j < 3; j++)
                            ((float*)(parts + 0x11944 + 12 * (size_t)k))[j] = nan;
                }
            return "own cell NaN (all fields)\n";
        }
        return "own cell NaN\n";
    }
    if (!a.empty() && a[0] == "selfstate") { // tests (BUG-0823): the own cell's address, opacity and whether a puppet sits there
        uint8_t* c = avatar();
        char b[200];
        bool as_puppet = false;
        if (C.puppets)
            for (auto& p : C.puppets())
                as_puppet |= c && p.cell == c;
        snprintf(b, sizeof b, "self cell=%p opacity=%.2f hp=%d table_names_it=%d refused=%u handed_own=%u", (void*)c,
                 c ? F<float>(c, kOpacity) : -1.f, c ? F<int>(c, kHp) : -1, (int)as_puppet, g_n.puppet_cell_refused,
                 g_n.puppet_cell_own);
        return b;
    }
    if (a.size() >= 2 && a[0] == "selfhit") { // tests: our own cell takes a bite of <dmg> (a lethal one: KillCell)
        hurt_avatar(atoi(a[1].c_str()), 4);
        uint8_t* c = avatar();
        char b[96];
        snprintf(b, sizeof b, "selfhit hp=%d dying=%d dead=%d downed=%d", c ? F<int>(c, kHp) : -1, c ? F<uint8_t>(c, kDying) : -1,
                 c ? F<uint8_t>(c, kDead) : -1, (int)g_downed);
        return b;
    }
    if (!a.empty() && a[0] == "budget") { // tests (BUG-0718): the host's population caps
        kPopCap = a.size() > 2 ? (uint32_t)atoi(a[1].c_str()) : 1100;
        kPopCapScreen = a.size() > 2 ? (uint32_t)atoi(a[2].c_str()) : 1300;
        char b[96];
        snprintf(b, sizeof b, "budget: populate below %u, a partner's screen below %u\n", kPopCap, kPopCapScreen);
        return b;
    }
    if (a.size() >= 2 && a[0] == "borrow") { // tests (BUG-0715 / 0716): `borrow electric|poison|off [graft]`: our avatar uses the
        // model of a cell here whose parts have an electric part (part type 6) / a poison part (model +0x1270, +0x1274),
        // so the game's own electric attack 0xE71520 / poison 0xE76880 runs for it; off: our own model again
        static uint32_t saved[3];
        static bool have;
        static uint8_t backup[0x48 + 0x20];
        static uint8_t* grafted;
        auto* k = (uint32_t*)(av + kModelKey);
        uint8_t* who = av; // the cell the status is about
        if (a.size() > 3 && a[3][0] == 'p') { // `... graft p<id>`: the model of that partner's puppet here (host)
            uint8_t* pc = puppet_cell(atoi(a[3].c_str() + 1));
            if (!pc)
                return "no such puppet\n";
            k = (uint32_t*)(pc + kModelKey);
            who = pc;
        }
        if (a[1] == "status") { // what the poison step 0xE68630 asks of our avatar, and what it left
            uint8_t* md = a_ModelData && k[0] ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
            std::string by;
            for (auto& [ra, n] : g_product_destroys) {
                char q[48];
                snprintf(q, sizeof q, " 0x%08X x%u", (unsigned)(ra + 0x400000), n);
                by += q;
            }
            char b[240];
            static uintptr_t can = H->sym("SporeCoop::Cell_CanPoison");
            int ok = can ? (int)skg::cdecl_call<uint8_t>(can, who) : -1;
            snprintf(b, sizeof b, "borrow status: can=%d level=%d gfx=%08X model=%08X dead=%d state=%X poisoning=%d accum=%.2f md=%p poison=%.2f/%d part0=%d electric_period=%.2f clouds_made=%u cloud_cells=%d kept=%u puppet_clouds=%u puppet_poison_steps=%u\n",
                     ok, F<int>(who, kLevel), F<uint32_t>(who, 0x248), k[0], F<uint8_t>(who, 0x112), F<uint32_t>(who, 0x18C), F<uint8_t>(who, 0x1AC), F<float>(who, 0x374), md,
                     md ? F<float>(md, 0x1270) : 0.f, md ? F<int>(md, 0x1274) : 0, md ? F<int>(md, 0x1278) : -1, md ? F<float>(md, 0x1260) : 0.f, g_n.clouds_made, (int)g_n.cloud_last, g_n.products_kept, g_n.puppet_clouds, g_n.puppet_poison_steps);
            return std::string(b) + "  destroyed by:" + (by.empty() ? " -" : by) + "\n";
        }
        if (a[1] == "off") {
            if (have)
                memcpy(k, saved, sizeof saved);
            have = false;
            if (grafted && skg::readable(grafted, 0x1280)) {
                memcpy(grafted + 8, backup, 0x48);
                memcpy(grafted + 0x125C, backup + 0x48, 0x20);
            }
            grafted = nullptr;
            return "own model\n";
        }
        bool electric = a[1] == "electric";
        // BUG-1243 (tests): `borrow jaw|spike [graft]`: a carnivore mouth (part type 2) / a weapon (part type 7, the
        // interaction kind 8 of 0xE6A3F0, docs/re/cell.md 11), used by the game at a contact
        int want = electric ? 6 : a[1] == "jaw" ? 2 : a[1] == "spike" ? 7 : -1;
        bool puppet_target = a.size() > 3 && a[3][0] == 'p';
        uint32_t found[3] = {};
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (puppet_target)
                return; // a puppet's model is only grafted
            auto* ck = (uint32_t*)(c + kModelKey);
            if (found[0] || c == av || !ck[0] || !a_ModelData)
                return;
            uint8_t* md = skg::cdecl_call<uint8_t*>(a_ModelData, ck[0], ck[1], ck[2]);
            if (!md || !skg::readable(md, 0x1280))
                return;
            bool ok = false;
            if (want >= 0) {
                int n = F<int>(md, 0);
                for (int i = 0; i < n && i < 64 && !ok; i++)
                    ok = F<int>(md, 8 + 0x48 * i) == want;
                ok = ok && (!electric || F<float>(md, 0x1260) != -1.f);
            } else
                ok = F<float>(md, 0x1270) != 0.f && F<int>(md, 0x1274) > 0;
            if (ok)
                memcpy(found, ck, sizeof found);
        });
        if (!found[0] && a.size() > 2 && a[2] == "graft" && a_ModelData && k[0]) {
            // no cell of that kind in this pool (the fixture's level-1 creatures have none): our own model data
            // gets the part, as a creation with it has (its first part record becomes an electric part, type 6,
            // with the electric timings; or the poison values), so the game's own step runs for our avatar
            uint8_t* md = skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]);
            if (!md || !skg::readable(md, 0x1280) || F<int>(md, 0) < 1)
                return "none here (no model data to graft)\n";
            if (!grafted) {
                memcpy(backup, md + 8, 0x48);
                memcpy(backup + 0x48, md + 0x125C, 0x20);
                grafted = md;
            }
            if (want >= 0 && !electric) {
                F<int>(md, 8) = want; // the first part becomes that part
            } else if (electric) {
                F<int>(md, 8) = 6;
                F<float>(md, 0x125C) = 1.0f;  // the cooldown after a near target
                F<float>(md, 0x1260) = 1.5f;  // the period
                F<float>(md, 0x1264) = 0.5f;  // the charge
                F<float>(md, 0x1268) = 6.0f;  // the range
                F<int>(md, 0x126C) = 4;       // targets of the chain
            } else {
                F<float>(md, 0x1270) = 1.0f;
                F<int>(md, 0x1274) = 1;
                F<int>(md, 0x1278) = 0; // the cloud at part 0
            }
            char b[160];
            snprintf(b, sizeof b, "borrowed model %08X %s (grafted into our own model data, part 0 type %d)\n", k[0], a[1].c_str(), F<int>(md, 8));
            return b;
        }
        if (!found[0])
            return "none here\n";
        if (!have)
            memcpy(saved, k, sizeof saved);
        have = true;
        memcpy(k, found, sizeof found);
        uint8_t* md = skg::cdecl_call<uint8_t*>(a_ModelData, found[0], found[1], found[2]);
        char b[200];
        snprintf(b, sizeof b, "borrowed model %08X %s: period=%.2f charge=%.2f range=%.2f targets=%d poison=%.2f/%d\n", found[0], a[1].c_str(),
                 F<float>(md, 0x1260), F<float>(md, 0x1264), F<float>(md, 0x1268), F<int>(md, 0x126C), F<float>(md, 0x1270), F<int>(md, 0x1274));
        return b;
    }
    if (a.size() >= 3 && a[0] == "sendact" && is_guest()) { // tests (BUG-0714): `sendact c<key> break|kill`: a lethal bite / kill
        // of that host creature sent as it is, whatever our own state (an old guest's, or one in flight at a knockout)
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        send_act(key, a[2] == "kill" ? ACT_KILL : ACT_BREAK, 0, 0, false);
        return "act sent";
    }
    if (a.size() >= 3 && a[0] == "hurt") { // host: send a bite to player <id>
        net::Writer w;
        w.u32(0xFFFFFFFFu);
        w.u8((uint8_t)atoi(a[2].c_str()));
        w.u8(4);
        C.sess->send_reliable_to((uint8_t)atoi(a[1].c_str()), net::MSG_CELL_HIT, w);
        return "bite sent";
    }
    char b[640];
    int local = 0, chase_me = 0, chase_partner = 0;
    std::unordered_set<uint32_t> partner_idx;
    if (C.puppets)
        for (auto& p : C.puppets())
            if (p.cell)
                partner_idx.insert(F<uint32_t>(p.cell, 0));
    for_each_cell([&](uint8_t* c, uint32_t) {
        local++;
        uint32_t t = F<uint32_t>(c, 0x22C); // chase target (docs/re/cell.md 1)
        if (t == avatar_idx())
            chase_me++;
        else if (partner_idx.count(t))
            chase_partner++;
    });
    float pd = 0;
    nearest_partner(1e9f, nullptr, &pd);
    std::string ko;
    if (C.puppets) // what we draw of each partner (a knocked out one: translucent)
        for (auto& p : C.puppets())
            if (p.cell)
                ko += "puppet " + std::to_string(p.id) + " op=" + std::to_string(F<float>(p.cell, kOpacity)).substr(0, 4) + "\n";
    for (auto& [id, k] : g_ko) {
        uint8_t* pc = puppet_cell(id);
        char kb[96];
        snprintf(kb, sizeof kb, "ko[%d]=%d left=%.1f hold=%.2f op=%.2f fx=%u\n", id, k.state, k.left, k.hold,
                 pc ? F<float>(pc, kOpacity) : -1.f, k.fx);
        ko += kb;
    }
    snprintf(b, sizeof b,
             "avatar hp=%d invuln=%d own_eats=%u own_points=%u bites_taken=%u damage_taken=%u pos=(%.1f %.1f) size=%.2f target=%.2f body=%.2f downed=%d dying=%d dead=%d ko_respawns=%u ko_deaths=%u partner_dist=%.1f "
             "revive_dist=%.1f local_gameplay_cells=%d own_pairs=%u ko_eats_blocked=%u ko_bites_blocked=%u ko_hits_refused=%u ko_swallows_blocked=%u\n",
             F<int>(av, kHp), F<uint8_t>(av, kInvuln), g_n.own_eats, g_n.own_food, g_n.bites_taken, g_n.damage_taken, pos_of(av).x, pos_of(av).y, F<float>(av, kSize), F<float>(av, kTargetSize), g_body,
             g_downed ? 1 : 0, (int)F<uint8_t>(av, kDying), (int)F<uint8_t>(av, kDead), g_n.respawns, g_n.ko_deaths, pd, revive_dist(av), local, g_n.own_pairs, g_n.ko_eats_blocked, g_n.ko_bites_blocked, g_n.ko_hits_refused, g_n.ko_swallows_blocked);
    // cine=1: a caption waits for the player's OK (cinematic manager +0x2C, docs/re/space.md 7.2);
    // parts: the part states S+0x34 (13 ints, 0 locked; until BUG-0467 this read 13 bytes)
    void* cm = a_Cine ? skg::cdecl_call<void*>(a_Cine) : nullptr;
    std::string parts;
    if (uint8_t* s = S())
        for (int i = 0; i < 13; i++)
            parts += F<int>(s, 0x34 + 4 * i) ? '1' : '0';
    char ob[224];
    snprintf(ob, sizeof ob, "avatar_opacity=%.2f cine=%d parts=%s chasing_me=%d chasing_partner=%d\n"
             "world_scale=%.4f target=%.4f scale_converted=%u\n",
             F<float>(av, kOpacity), cm && F<uint32_t>(cm, 0x2C) ? 1 : 0, parts.c_str(), chase_me, chase_partner,
             world_scale(), G() ? F<float>(G(), 0x5150) : 0.f, g_n.scale_conv);
    return b + std::string(ob) + ko + status() + away_status(); // the away line last: tests read the first food=
}

} // namespace cellsync
