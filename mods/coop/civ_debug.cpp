// coop.civ: the Civ module's console command (tests and diagnostics); moved out of civ.cpp (C8 of 0.7.15,
// BUG-1164).
#include "civ_internal.h"

namespace civ {

std::string debug(const std::vector<std::string>& a)
{
    if (!a.empty() && a[0] == "owndesign") // BUG-1274: owndesign [set <hex id> <hex inst> | did <city> <loco> | hpref ...]
        return own_designs_debug(std::vector<std::string>(a.begin() + 1, a.end()));
    if (!a.empty() && (a[0] == "influence" || a[0] == "canbuy" || ((a[0] == "convert" || a[0] == "unroad") && is_host()))) // BUG-1284..1287 (tests)
        return influence_debug(a);
    if (!a.empty() && a[0] == "shotprobe") // BUG-1322 (tests): the shot observer with a target that is no object
        return shot_probe();
    if (!a.empty() && a[0] == "shotobjs") // BUG-1322: shooters / targets of the host's shots that are no listed object
        return shot_listed_status();
    if (!a.empty() && a[0] == "citizens") // citizens: the owners' species lines and the citizens per city and species (BUG-1273)
        return species_debug();
    if (a.size() >= 2 && a[0] == "vehhold" && is_guest()) { // vehhold 1|0: tests (BUG-0951), the host's vehicle states
        g_veh_hold = atoi(a[1].c_str()) != 0;               // are not taken (as while the host is in an editor)
        return std::string("vehicle states ") + (g_veh_hold ? "held" : "taken") + "\n";
    }
    if (a.size() >= 1 && a[0] == "reusehunt" && is_guest()) { // reusehunt [npc|<mirror id> [frames]]: BUG-0951 (tests)
        if (a.size() < 2)
            return veh_hunt_status();
        // npc: the first live mirror of another civilization than this guest's (the new vehicle is made for the guest's
        // own civilization: it is never bound to that host vehicle by its kind)
        GvRef it = a[1] == "npc" || a[1] == "any" ? GvRef{} : gv_find((uint16_t)atoi(a[1].c_str()));
        void* pc = player_civ();
        // (the new vehicle is made 40 m away: farther than the 25 m a mirror binds a local vehicle of its kind by)
        if (a[1] == "npc" || a[1] == "any")
            for (int pass = a[1] == "npc" ? 0 : 1; pass < 2 && !it; pass++)
                gv_each([&](const GvRef& x) {
                    if (!it && gv_ok(x) && !F<uint8_t>(x.v, kVDead) && H->time_seconds() - x.g->seen < 1.0 &&
                        (pass || !pc || pol_of(x.v) != pol_of(pc)) && !kill_waiting(x.v))
                        it = x;
                });
        if (!it || !gv_ok(it)) {
            char b[120];
            snprintf(b, sizeof b, "no live mirror (%u mirrors, %u vehicles in the game)\n", (unsigned)gv_count(),
                     (unsigned)all_vehicles().size());
            return b;
        }
        void* v = it.v;
        g_vh = VehHunt{};
        g_vh.target = (uint8_t*)v;
        g_vh.id = it.id;
        g_vh.purpose = F<int>(v, kVPurpose);
        g_vh.loco = F<int>(v, kVLoco);
        g_vh.at = veh_pos(v);
        g_vh.at.x += 40.f;
        g_vh.left = a.size() > 2 ? atoi(a[2].c_str()) : 150;
        kill_local(v); // the game's removal: the mirror still names it
        return "mirror's vehicle removed by the game; " + veh_hunt_status();
    }
    if (a.size() >= 3 && a[0] == "fix") { // fix cities|aispawn|caption|away 0|1: the code before the fix (tests)
        bool on = atoi(a[2].c_str()) != 0;
        if (a[1] == "cities")
            g_fix_cities = on;
        else if (a[1] == "aispawn")
            g_fix_aispawn = on;
        else if (a[1] == "caption")
            g_fix_caption = on;
        else if (a[1] == "away")
            g_fix_away = on;
        else
            return "usage: coop.civ fix cities|aispawn|caption|away 0|1";
        return "fix " + a[1] + " " + (on ? "on" : "off");
    }
    if (!a.empty() && a[0] == "spawns") { // spawns: the game's own vehicle spawns on this guest by caller
        std::string out;
        char b[64];
        for (auto& [ra, n] : g_local_spawns) {
            snprintf(b, sizeof b, "spawn from 0x%08X x%u\n", (unsigned)ra, n);
            out += b;
        }
        snprintf(b, sizeof b, "ai spawns blocked %u\n", g_n.ai_spawns_blocked);
        return out + b;
    }
    if (!a.empty() && a[0] == "tables") { // sizes of the pointer tables (tests: BUG-0040, BUG-0059)
        char b[420];
        snprintf(b, sizeof b,
                 "tables: hv=%u boost=%u alarm_at=%u siege=%u joint=%u gifted=%u bfix=%u host_cities=%u city_owner=%u "
                 "gv=%u world_resets=%u next_id=%u world_changes=%u "
                 "mirror_designs=%u veh_fallback=%u build_skipped=%u bound_redesigned=%u last_order=%d/%u/%u/%08X\n",
                 (unsigned)hv_count(), (unsigned)g_boost.size(), (unsigned)g_alarm_at.size(), (unsigned)g_siege.size(),
                 (unsigned)g_joint_told.size(), (unsigned)g_city_gifted.size(), (unsigned)g_bfix_seen.size(),
                 (unsigned)g_host_cities.size(), (unsigned)g_city_owner.size(), (unsigned)gv_count(), g_n.world_resets,
                 (unsigned)g_next_id, g_n.world_checks_fired, g_n.veh_design_checked,
                 g_n.veh_design_fallback, g_n.build_skipped, g_n.bound_redesigned, g_last_order.type, g_last_order.kind,
                 g_last_order.id, g_last_order.noun);
        char c[200]; // BUG-0775 / 0875 / 0877 / 0878
        snprintf(c, sizeof c,
                 "captures: captured=%u credit_cmd=%u credit_hit=%u credit_siege=%u credit_nearest=%u buys_sent=%u "
                 "buys_done=%u refused=%u away_sent=%u\n",
                 (unsigned)g_city_captured.size(), g_n.credit_by[0], g_n.credit_by[1], g_n.credit_by[2], g_n.credit_by[3],
                 g_n.city_buys_sent, g_n.city_buys_done, g_n.city_buys_refused, g_n.away_captures_sent);
        char k[200]; // BUG-0950
        size_t kn = 0;
        if (auto it = g_kept_cities.find(g_kept_key); it != g_kept_cities.end())
            kn = it->second.size();
        snprintf(k, sizeof k, "kept: fix=%d worlds=%u this_world=%u saved=%u restored=%u key=%s\n", (int)g_fix_owners_kept,
                 (unsigned)g_kept_cities.size(), (unsigned)kn, g_n.kept_saved, g_n.kept_restored, g_kept_key.c_str());
        char sp[200]; // BUG-0982
        snprintf(sp, sizeof sp, "specialty: fix=%d asked=%u shown=%u answered=%u applied=%u rejected=%u queued=%u open=%d "
                 "unpaused=%u\n", (int)g_fix_specialty_buyer, g_n.spec_asked, g_n.spec_shown, g_n.spec_answered,
                 g_n.spec_applied, g_n.spec_rejected, (unsigned)g_spec_queue.size(), g_spec_open, g_n.spec_unpaused);
        char cl[240]; // E1 (BUG-1153)
        snprintf(cl, sizeof cl, "claims: fix=%d serial=%u cities=%u vehicles=%u matched=%d/%d from_disk=%u/%u held=%u back=%u\n",
                 (int)g_store_fix, g_claims_serial, (unsigned)g_city_claim.size(), (unsigned)g_veh_claim.size(),
                 (int)g_cities_matched, (int)g_vehs_matched, g_n.claim_cities, g_n.claim_vehs, g_n.claims_held, g_n.claims_back);
        std::string reg = "registry: " + g_hvr.status() + " | " + g_gvr.status() + " | rx: generation=" +
                          std::to_string(g_rx.generation) + " complete=" + std::to_string(g_rx.complete) + " cut=" +
                          std::to_string(g_rx.cut) + " abandoned=" + std::to_string(g_rx.abandoned) + " bad=" +
                          std::to_string(g_rx.bad) +
                          "\n"; // C1 (BUG-1140)
        return std::string(b) + k + sp + cl + c + reg + took_status();
    }
    if (!a.empty() && a[0] == "fakeveh") { // tests (BUG-0469): fakeveh <hex design instance|off> [<hex type> <hex group>]
        // every mirror vehicle is built with that design key (the guest's own civ design otherwise)
        g_veh_force = a.size() > 1 && a[1] != "off";
        if (g_veh_force) {
            g_veh_force_key[0] = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
            g_veh_force_key[1] = a.size() > 2 ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : 0x24682294; // vcl
            g_veh_force_key[2] = a.size() > 3 ? (uint32_t)strtoul(a[3].c_str(), nullptr, 16) : 0x40646200;
        }
        return std::string("mirror vehicles: the design key ") + (g_veh_force ? "is forced for the tests" : "is the civ's own");
    }
    if (a.size() >= 5 && a[0] == "setdesign") { // setdesign <hex pol> <purpose> <loco> <hex inst>: tests (BUG-0647)
        // the civ's design key of that purpose and locomotion names this vehicle design (a player's own creation)
        void* civ = thiscall<void*>(a_CivByPol, nm(), (uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        uint32_t* k = civ ? thiscall<uint32_t*>(a_DesignKey, civ,
                                                skg::cdecl_call<int>(a_DesignId, atoi(a[2].c_str()), atoi(a[3].c_str())))
                          : nullptr;
        if (!k)
            return "no design slot";
        k[0] = (uint32_t)strtoul(a[4].c_str(), nullptr, 16);
        k[1] = 0x24682294;
        k[2] = 0x40646200;
        return "design set";
    }
    if (!a.empty() && a[0] == "bmodels") // tests (BUG-0670): every city's buildings and their model keys
        return host_buildings_list();
    if (a.size() >= 4 && a[0] == "bdesign") { // bdesign <hex pol> <house|factory|fun|hall> <hex inst>: tests (BUG-0670)
        void* civ = thiscall<void*>(a_CivByPol, nm(), (uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        uint32_t did = a[2] == "factory" ? 0x47C10953 : a[2] == "fun" ? 0x72C49181 : a[2] == "hall" ? 0x99E92F05 : 0x4E3F7777;
        uint32_t* k = civ ? thiscall<uint32_t*>(a_DesignKey, civ, (int)did) : nullptr;
        if (!k)
            return "no design slot";
        k[0] = (uint32_t)strtoul(a[3].c_str(), nullptr, 16);
        k[1] = kBuildingModelType;
        k[2] = 0x40636200;
        return "design set";
    }
    if (a.size() >= 4 && a[0] == "vdesign") { // vdesign <hex pol> <purpose> <loco>: that civ's design key (BUG-0647)
        void* civ = thiscall<void*>(a_CivByPol, nm(), (uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        if (!civ)
            return "no civilization of that political id";
        uint32_t* k = thiscall<uint32_t*>(a_DesignKey, civ,
                                          skg::cdecl_call<int>(a_DesignId, atoi(a[2].c_str()), atoi(a[3].c_str())));
        if (!k)
            return "no design";
        char b[160];
        snprintf(b, sizeof b, "design %08X/%08X/%08X known=%d\n", k[0], k[1], k[2],
                 (int)creations::key_known({k[0], k[1], k[2]}));
        return b;
    }
    if (!a.empty() && a[0] == "content") { // tests (BUG-0469): the model checks of the mirrors
        return creations::content_status();
    }
    if (a.size() >= 2 && a[0] == "design") { // design <city> [house|factory|fun]: the civ's building design key
        // and the model check of it (tests, BUG-0469: the guest checks a design it has)
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uint32_t noun = a.size() > 2 && a[2] == "factory" ? 0x18EA2CC : a.size() > 2 && a[2] == "fun" ? 0x1A56ABA
                                                                                                      : 0x18EB106;
        uint32_t type = type_for_noun(noun), did = building_design_id(type);
        void* civ = city ? F<void*>(city, kCityCiv) : nullptr;
        uint32_t* k = civ && did ? thiscall<uint32_t*>(a_DesignKey, civ, (int)did) : nullptr;
        char b[220];
        snprintf(b, sizeof b, "design noun=%08X type=%08X id=%08X city=%p civ=%p key=%08X/%08X/%08X\n", noun, type, did,
                 city, civ, k ? k[0] : 0, k ? k[1] : 0, k ? k[2] : 0);
        if (!k || !k[0])
            return std::string(b) + "no design here: the game generates one\n";
        creations::Model m = creations::ensure_model({k[0], k[1], k[2]}, creations::kMkBuilding, false);
        return std::string(b) + "model " +
               (m.key.instance ? "ok" : "not in this game") + ", " + creations::content_status();
    }
    if (!a.empty() && a[0] == "nulllocal") { // tests (BUG-0223): the stage's local entity is null for N frames
        int n = a.size() > 1 ? atoi(a[1].c_str()) : 1;
        modes::test_null_local(n < 1 ? 1 : n > 600 ? 600 : n);
        return "the local entity is null for " + std::to_string(n < 1 ? 1 : n > 600 ? 600 : n) + " frames\n";
    }
    // tests of the co-op mechanics through the game's own functions (host)
    // siege <city> <player> [hits [pc]]: capture damage by that player (host; a guest while the host is away, its own
    // game); `pc`: the attacker is the player civilization, so the game's own CheckMilitaryCapture takes the city for
    // it at 100 (BUG-0775 / BUG-0878 tests)
    if (a.size() >= 3 && a[0] == "siege" && (is_host() || local_play())) {
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uintptr_t ctxf = H->sym("Simulator::GetActivePlanetRecord");
        if (!city)
            return "no such city";
        void* pc = player_civ();
        int hits = a.size() > 3 ? std::clamp(atoi(a[3].c_str()), 1, 200) : 1;
        uint32_t att = a.size() > 4 && a[4] == "pc" && pc ? pol_of(pc) : 0x0BADF00Du;
        float before = F<float>(city, 0x748);
        uint32_t pol0 = pol_of(city);
        for (int i = 0; i < hits && pol_of(city) == pol0; i++) {
            g_hit_by = (uint8_t)atoi(a[2].c_str());
            h_AddCapture(skg::cdecl_call<void*>(ctxf), city, (uint8_t*)city + 0x2F0, pol_of(city), 10.f, att, false);
            g_hit_by = 0xFF;
        }
        char b[128];
        snprintf(b, sizeof b, "capture %.2f -> %.2f pol %08X -> %08X", before, F<float>(city, 0x748), pol0, pol_of(city));
        return b;
    }
    // bhp <city> <hp>: every building of the city but its hall down to that health at most (host; tests, BUG-0775:
    // a real siege that ends in minutes)
    if (a.size() >= 3 && a[0] == "bhp" && is_host()) {
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        float hp = std::max(1.f, (float)atof(a[2].c_str()));
        int n = 0;
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings))
            if (bl && bl != F<void*>(city, 0x320) /* the hall */ && F<int>(bl, kBuildingState) != 2 && F<float>(bl, kBuildingState + 4) > hp) {
                F<float>(bl, kBuildingState + 4) = hp;
                n++;
            }
        return std::to_string(n) + " buildings at " + std::to_string((int)hp) + " hp";
    }
    // vhp <id> <hp>: that vehicle's health (host; tests, BUG-0775: the besiegers live through the siege)
    if (a.size() >= 3 && a[0] == "vhp" && is_host()) {
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v || !exists(v))
            return "no such vehicle";
        F<float>(v, kVHp) = std::max(1.f, (float)atof(a[2].c_str()));
        return "hp " + std::to_string((int)F<float>(v, kVHp));
    }
    // vput <id> <x> <y> <z>: that vehicle put there (spatial SetPosition vt 0x38; host; tests, BUG-0775: a host vehicle
    // standing nearer the city than the besiegers)
    // vput <id> <x> <y> <z>: tests, the vehicle's SetPosition (host: its own; a guest while the host is away: its
    // mirror, which its own game drives then: BUG-1056)
    if (a.size() >= 5 && a[0] == "vput" && (is_host() || local_play())) {
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v || !exists(v))
            return "no such vehicle";
        float p[3] = {(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
        if (!is_host()) // (a guest's mirror: its locomotion would pull it back to where it drove, as guest_tick does)
            vcall<void>((uint8_t*)v + kVSpatial, 0xEC / 4); // StopMovement
        vcall<void>((uint8_t*)v + kVSpatial, 0x38 / 4, (const float*)p);
        net::Vec3 q = veh_pos(v);
        char b[96];
        snprintf(b, sizeof b, "put at (%.1f %.1f %.1f)", q.x, q.y, q.z);
        return b;
    }
    // buycity <city> [price]: the game's purchase of a city for the player civilization, as FinishBuyCity makes it
    // (cCity buy 0xBE99A0; 0 = the game's GetBuyPrice) (tests, BUG-0877)
    if (a.size() >= 2 && a[0] == "buycity") {
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        void* pc = player_civ();
        uintptr_t fb = H->sym("SporeCoop::cCity_Buy");
        if (!city || !pc || !fb)
            return "no such city / civ / symbol";
        float money = thiscall<float>(a_GetMoney, pc);
        uint32_t pol0 = pol_of(city);
        thiscall<void>(fb, city, pol_of(pc), a.size() > 2 ? (float)atof(a[2].c_str()) : 0.f); // through our hook
        char b[160];
        snprintf(b, sizeof b, "buycity %zu pol %08X -> %08X money %.0f -> %.0f sent=%u", ci, pol0, pol_of(city), money,
                 thiscall<float>(a_GetMoney, pc), g_n.city_buys_sent);
        return b;
    }
    if (a.size() >= 2 && a[0] == "alarm" && is_host()) { // alarm <city>: as if an enemy attacked it
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        g_alarm_at.erase(city);
        if (a.size() >= 3 && a[2] == "expire") { // BUG-0771: every boost has just run out, boost_tick not run yet
            double now = H->time_seconds();
            for (auto& [v, bo] : g_boost)
                bo.until = now - 0.001;
        }
        h_OnAttacked(city, 0, 0x0BADF00D);
        return "alarm raised";
    }
    if (a.size() >= 2 && a[0] == "hvdrop" && is_host()) { // hvdrop <net id>: out of the host's table, it lives (BUG-0773)
        uint16_t id = (uint16_t)atoi(a[1].c_str());
        if (!hv_by_id(id))
            return "no such vehicle";
        hv_drop(id);
        return "dropped from the table";
    }
    if (a.size() >= 4 && a[0] == "guestcmd" && is_host()) { // guestcmd <from> <vehicle id> v<vehicle id>|<city> [type]:
        // tests (BUG-1053), a guest's order (CMD_ORDER on a vehicle or a host city) handled here now, as on_message does;
        // then the ordered vehicle's current order in the game (mOrders +0xB68: target, type)
        uint16_t vid = (uint16_t)atoi(a[2].c_str());
        Target t;
        void* tp = nullptr;
        if (a[3][0] == 'v') {
            t.kind = 1;
            t.id = (uint16_t)atoi(a[3].c_str() + 1);
            tp = hv_by_id(t.id).v;
        } else {
            t.kind = 2;
            t.id = (uint16_t)atoi(a[3].c_str());
            tp = t.id < g_host_cities.size() ? g_host_cities[t.id] : nullptr;
        }
        net::Writer w;
        w.u8(CMD_ORDER);
        w.u16(vid);
        w.u32((uint32_t)(a.size() >= 5 ? atoi(a[4].c_str()) : t.kind == 1 ? 1 : 2));
        w.u32(1);
        write_target(w, t);
        uint32_t d0 = g_n.cmds_done, r0 = g_n.cmds_rejected, g0 = g_n.cmd_gone;
        net::Reader r(w.data.data(), w.data.size());
        host_command((uint8_t)atoi(a[1].c_str()), r);
        void* v = hv_by_id(vid).v;
        void* ot = nullptr;
        int otype = -1;
        if (v && game::listed(v, kVehicleNoun)) {
            uint8_t* ob = F<uint8_t*>(v, kVOrders);
            uint8_t* oe = F<uint8_t*>(v, kVOrders + 4);
            if (ob && oe > ob) {
                ot = *(void**)ob;
                otype = *(int*)(ob + 0x10);
            }
        }
        char b[240];
        snprintf(b, sizeof b, "guestcmd: target %p in_table=%d listed=%d ran=%u refused=%u gone_now=%u | vehicle #%u %p order target=%p type=%d",
                 tp, (int)(tp != nullptr), (int)(tp && game::listed(tp)), g_n.cmds_done - d0, g_n.cmds_rejected - r0, g_n.cmd_gone - g0,
                 (unsigned)vid, v, ot, otype);
        return b;
    }
    if (!a.empty() && a[0] == "world_change") { // the tables' reset of a world change, the world staying (BUG-0773)
        on_world_change();
        return "world change run";
    }
    if (a.size() >= 2 && a[0] == "capture" && is_host()) { // capture <city>: the player civ takes it (tests, BUG-0187)
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        void* pc = player_civ();
        if (!city || !pc)
            return "no such city / civ";
        int r = h_Capture(city, pol_of(pc), a.size() > 2 ? atoi(a[2].c_str()) : 0, 0, 0, 0);
        return "capture -> " + std::to_string(r);
    }
    if (!a.empty() && a[0] == "movie") { // the host's cinematic / caption (BUG-0187)
        char b[96];
        snprintf(b, sizeof b, "movie playing=%d closed=%u fix=%d wait=%.0f armed=%d arms=%u", (int)movie_playing(), g_n.captions_closed,
                 (int)g_fix_caption, g_caption_wait, (int)(g_caption_armed > 0), g_n.caption_arms);
        return b;
    }
    if (!a.empty() && a[0] == "sw") { // the superweapon votes (host, BUG-0117)
        // civ= the player civ (tests read the game's own fields: +0x98 money, +0x484 unlocked[12], BUG-0222)
        char b[192];
        snprintf(b, sizeof b, "sw last_request=%d fired=%d refused=%u orphans_given=%u civ=%p", g_sw_last_req,
                 g_sw_fired, g_n.sw_refused, g_n.orphans_given, g_ok && game::mode_id() == modes::kCiv && nm() ? player_civ() : nullptr);
        return b;
    }
    if (a.size() >= 2 && a[0] == "super") { // super <type> [x y z]: fire a superweapon (through the vote)
        // BUG-0222: always a target, as the game's own callers pass one: FireSuperweapon reads it without
        // a null check (GOG 2024 0xBF8BB0: compares it with the zero vector 0x168C514 = untargeted)
        void* pc = player_civ();
        float t[3] = {0, 0, 0};
        if (a.size() >= 5)
            for (int i = 0; i < 3; i++)
                t[i] = (float)atof(a[2 + i].c_str());
        return pc ? std::string("fired -> ") + (h_FireSW(pc, atoi(a[1].c_str()), t) ? "1" : "0") : "no civ";
    }
    if (!a.empty() && a[0] == "boosts") { // the boosted vehicles (tests read the game's +0x218 speed, BUG-0222)
        std::string out;
        char b[96];
        double now = H->time_seconds();
        for (auto& [v, bo] : g_boost) {
            snprintf(b, sizeof b, "boost %p orig=%.3f left=%.1f owner=%d\n", v, bo.orig, bo.until - now,
                     hv_find(v) ? (int)hv_find(v).h->owner : -1);
            out += b;
        }
        snprintf(b, sizeof b, "boosts=%zu restored=%u\n", g_boost.size(), g_n.boost_restored);
        return (out.empty() ? std::string("no boosts\n") : out) + b;
    }
    if (a.size() >= 3 && a[0] == "stale_hv" && is_host() && game::mode_id() == modes::kCiv) {
        // stale_hv <vehicle id> <owner> (tests, BUG-0222 / BUG-0059): the table entry of that vehicle as if
        // its address had held a player's vehicle of the old world: a player owner and another nation's pol
        HvRef x = hv_by_id((uint16_t)atoi(a[1].c_str()));
        if (!x)
            return "no such vehicle";
        HV& h = *x.h;
        h.owner = (uint8_t)atoi(a[2].c_str());
        h.pol ^= 0x5A5A5A5Au;
        return "stale entry: owner=" + std::to_string(h.owner);
    }
    if (a.size() >= 3 && a[0] == "select") {
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v)
            return "no such vehicle";
        thiscall<int>(a_SetSelected, (uint8_t*)v + kVSpatial, atoi(a[2].c_str()));
        return "selected=" + std::to_string(F<uint8_t>(v, kVSelected));
    }
    if (!a.empty() && a[0] == "pushprobe" && is_guest()) // tests (BUG-0186): who moves a mirror
        return push_probe();
    if (a.size() >= 3 && a[0] == "attack" && (is_host() || is_guest())) { // on a guest: the game's call of the guest (BUG-0648)
        // attack <id> <host city index | v<vehicle id>> [type]: the game's own attack order (tests, BUG-0477):
        // type 2 the military attack of a city (default for a city), 1 a unit (default for a vehicle)
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        void* t = nullptr;
        int type = 2;
        if (a[2][0] == 'v') {
            t = local_vehicle((uint16_t)atoi(a[2].c_str() + 1));
            type = 1;
        } else if (a[2][0] == 'c') { // c<x>,<y>,<z>: this game's wild creature (noun 0x18EB45E) within 10 m of there (BUG-0970)
            net::Vec3 p{};
            if (sscanf(a[2].c_str() + 1, "%f,%f,%f", &p.x, &p.y, &p.z) == 3)
                t = game::find_object(kCreatureNoun, p, 10.f);
            type = 1;
        } else {
            int ci = atoi(a[2].c_str());
            t = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        }
        if (a.size() >= 4)
            type = atoi(a[3].c_str());
        if (!v || !t)
            return "no such vehicle or target";
        thiscall<int>(a_SetOrder, v, t, type, 1);
        thiscall<void>(a_OrderStamp, v, 0);
        return "attack ordered";
    }
    if (!a.empty() && (a[0] == "tribes" || a[0] == "razetribe")) {
        // tribes: the old Tribe stage tribes in this game; razetribe <i>: the host's game destroys tribe i of that
        // list (DestroyInstance, the last step of its vehicles' raid 0xDCAAB0; tests BUG-0539)
        auto ts = tribes_here();
        if (a[0] == "razetribe") {
            size_t i = a.size() >= 2 ? (size_t)atoi(a[1].c_str()) : 0;
            if (!is_host() || i >= ts.size() || !a_DestroyInst)
                return "no such tribe (host only)";
            thiscall<void>(a_DestroyInst, nm(), ts[i].first);
            return "tribe destroyed";
        }
        std::string out;
        for (auto& [t, p] : ts) {
            char line[96];
            snprintf(line, sizeof line, "tribe %p pos=(%.1f %.1f %.1f)\n", t, p.x, p.y, p.z);
            out += line;
        }
        return out + "tribes " + std::to_string(ts.size());
    }
    if (a.size() >= 2 && (a[0] == "turrets" || a[0] == "addturret" || a[0] == "budget")) {
        // turrets <host city index>: the city's turrets; addturret <ci>: one more in the next free slot (the game's
        // City_AddTurret, tests BUG-0484); budget <ci> <value>: the city's civilization's growth budget (+0x4AC,
        // what the city growth 0xBE5890 buys turrets and buildings with; tests BUG-0484)
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        if (a[0] == "addturret") {
            if (!a_AddTurret)
                return "no turret hook";
            return o_AddTurret(city) ? "turret added" : "no free turret slot";
        }
        if (a[0] == "budget" && a.size() >= 3 && a[2] == "attacked") {
            // budget <ci> attacked: the city counts as attacked now (city +0x98 / +0x9C, the time 0xC00600 compares
            // with the game time: under attack in the last 20 s, the growth buys turrets first; tests BUG-0484)
            uintptr_t tg = H->sym("Simulator::cGameTimeManager::Get"), tf = H->sym("cGameTimeManager_TimeAtStartOfFrame");
            void* mgr = tg ? skg::cdecl_call<void*>(tg) : nullptr;
            if (!mgr || !tf)
                return "no game time";
            uint64_t t = thiscall<uint64_t>(tf, mgr);
            F<uint32_t>(city, 0x98) = (uint32_t)t;
            F<uint32_t>(city, 0x9C) = (uint32_t)(t >> 32);
            return "attacked at " + std::to_string(t);
        }
        if (a[0] == "budget") {
            void* civ = F<void*>(city, kCityCiv);
            if (!civ)
                return "no civilization";
            if (a.size() >= 3)
                F<float>(civ, 0x4AC) = (float)atof(a[2].c_str());
            return "budget " + std::to_string(F<float>(civ, 0x4AC));
        }
        std::string out;
        for (auto& [t, p] : turrets_of(city)) {
            char line[96];
            snprintf(line, sizeof line, "turret %p pos=(%.1f %.1f %.1f)\n", t, p.x, p.y, p.z);
            out += line;
        }
        return out + "turrets " + std::to_string(turrets_of(city).size());
    }
    if (a.size() >= 4 && a[0] == "spawnveh" && is_host()) {
        // spawnveh <host city index> <purpose 0 military | 1 religious | 2 economic> <loco>: a vehicle of the
        // player's civilization at that city, free (tests, BUG-0530: religious shots); the civ's own design for
        // that purpose and locomotion, else the game's generated one (key 0, as cVehicle::Load makes it).
        // `force` (5th word): past the civ's purpose allowance (VehicleSlotsLeft 0), as hpref spawns its reference; the
        // civ_npc fixture's one city has no slot left of any purpose (BUG-1284, BUG-1286 tests). 36 vehicles stay the cap
        int ci = atoi(a[1].c_str()), purpose = atoi(a[2].c_str()), loco = atoi(a[3].c_str());
        bool force = a.size() >= 5 && a[4] == "force";
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        void* civ = player_civ();
        if (!city || !civ)
            return "no such city";
        int left = thiscall<int>(a_SlotsLeft, civ, purpose);
        if (left <= 0 && !force)
            return "no vehicle slot left";
        if (left <= 0 && vec_of((uint8_t*)civ + 0xB0).size() >= 36)
            return "no vehicle slot left (36 vehicles)";
        uint32_t* key = thiscall<uint32_t*>(a_DesignKey, civ, skg::cdecl_call<int>(a_DesignId, purpose, loco));
        uint32_t k[3] = {0, 0, 0};
        if (key)
            k[0] = key[0], k[1] = key[1], k[2] = key[2];
        void* v = thiscall<void*>(a_SpawnVehicle, city, purpose, loco, k[0], k[1], k[2], 0); // the city's spawn point
        if (!v)
            return "the game made no vehicle";
        thiscall<int>(a_PushEvent, v, 10);
        char b[96];
        snprintf(b, sizeof b, "spawned %p purpose=%d loco=%d design=%08X", v, F<int>(v, kVPurpose), F<int>(v, kVLoco), k[0]);
        return b;
    }
    if (a.size() >= 6 && a[0] == "creatureshift") {
        // creatureshift <x> <y> <z> <r> <d>: every wild creature of this game within r m of the place is set d m
        // aside along the ground (tests, BUG-0970: this game's herd is no longer where the partner's is; wild
        // creatures are not synced in Civ)
        struct Q {
            net::Vec3 c;
            float r2;
            std::vector<void*> found;
        } q{{(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())},
            (float)(atof(a[4].c_str()) * atof(a[4].c_str())), {}};
        float d = (float)atof(a[5].c_str());
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto* q = (Q*)u;
                net::Vec3 p;
                if (n == kCreatureNoun && game::object_alive(o) && game::object_position(o, p)) {
                    float dx = p.x - q->c.x, dy = p.y - q->c.y, dz = p.z - q->c.z;
                    if (dx * dx + dy * dy + dz * dz < q->r2)
                        q->found.push_back(o);
                }
                return true;
            },
            &q);
        // a direction along the ground: up x (0 0 1) (or x (1 0 0) near a pole), keeping the distance to the centre
        float len = std::sqrt(q.c.x * q.c.x + q.c.y * q.c.y + q.c.z * q.c.z);
        if (len < 1.f)
            return "no place";
        net::Vec3 up{q.c.x / len, q.c.y / len, q.c.z / len};
        net::Vec3 t{up.y, -up.x, 0.f};
        if (std::fabs(up.z) > 0.9f)
            t = {0.f, up.z, -up.y};
        float tl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        for (void* o : q.found) {
            net::Vec3 p;
            game::object_position(o, p);
            net::Vec3 n{p.x + t.x / tl * d, p.y + t.y / tl * d, p.z + t.z / tl * d};
            float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            n = {n.x / nl * pl, n.y / nl * pl, n.z / nl * pl};
            game::set_object_position(o, n);
        }
        return "shifted " + std::to_string(q.found.size()) + " creatures";
    }
    if (a.size() >= 5 && a[0] == "move") { // move <id> <x> <y> <z>
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v)
            return "no such vehicle";
        thiscall<int>(a_SetMoveOrder, v, (float)atof(a[2].c_str()), (float)atof(a[3].c_str()),
                      (float)atof(a[4].c_str()), 1);
        return "move ordered";
    }
    if (a.size() >= 3 && a[0] == "buy") { // buy <host city index> <loco>: like the city menu button
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        uint8_t o = owner_of_city(city);
        if (o != C.sess->my_id() && o != kShared)
            return "this city belongs to player " + std::to_string(o);
        if (is_guest()) {
            request_buy(city, atoi(a[2].c_str()));
            return "purchase requested";
        }
        return host_buy(city, atoi(a[2].c_str())) ? "bought" : "purchase failed";
    }
    if (a.size() >= 3 && a[0] == "build") { // build <host city index> <house|factory|fun>
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        uint32_t noun = a[2] == "factory" ? 0x18EA2CC : a[2] == "fun" ? 0x1A56ABA : 0x18EB106;
        net::Vec3 p = city_pos(city);
        if (is_guest()) {
            float paid = 0;
            auto send = [&] {
                net::Writer w;
                w.u8(CMD_BUILD);
                w.u8((uint8_t)ci);
                w.u32(noun);
                w.f32(p.x); w.f32(p.y); w.f32(p.z);
                w.f32(paid); // BUG-0807
                send_cmd(w);
            };
            if (!g_fix_build_money) // BUG-0613 off: sent before the guest knows it can pay
                send();
            void* pc = player_civ();
            float m0 = pc ? thiscall<float>(a_GetMoney, pc) : 0.f;
            void* b{};
            {
                callguard::Hold applying(g_applying); // the guest's own copy, as its planner would place it
                b = build_in_city(city, noun, p, true);
            }
            paid = b && pc ? std::max(0.f, m0 - thiscall<float>(a_GetMoney, pc)) : 0.f;
            if (g_fix_build_money && b)
                send(); // only what the guest could build and pay for
            return b ? "built locally, sent to the host" : g_build_fail;
        }
        return build_in_city(city, noun, p, a.size() < 4 || a[3] != "nopay") ? "built" : g_build_fail; // nopay: tests
    }
    if (!a.empty() && a[0] == "roads") { // roads [a b [type]]: every city's trade routes; host (tests): AddTradeRoute a b
        std::string out;
        char b[160];
        if (a.size() >= 3 && is_host() && a_AddTradeRoute) {
            size_t i = (size_t)atoi(a[1].c_str()), j = (size_t)atoi(a[2].c_str());
            if (i >= g_host_cities.size() || j >= g_host_cities.size() || i == j)
                return "no such cities";
            thiscall<void>(a_AddTradeRoute, g_host_cities[i], g_host_cities[j], a.size() > 3 ? atoi(a[3].c_str()) : 0);
            snprintf(b, sizeof b, "route %zu-%zu added: %s\n", i, j, has_route(g_host_cities[i], g_host_cities[j]) ? "yes" : "no");
            out += b;
        }
        for (size_t i = 0; i < g_host_cities.size(); i++) {
            void* c = g_host_cities[i];
            if (!c || !city_listed(c))
                continue;
            for (const Route& r : routes_of(c)) {
                auto it = std::find(g_host_cities.begin(), g_host_cities.end(), r.partner);
                snprintf(b, sizeof b, "route %zu %d type=%d obj=%p noun=%08X\n", i,
                         it == g_host_cities.end() ? -1 : (int)(it - g_host_cities.begin()), r.type, r.obj,
                         r.obj ? game::noun_id(r.obj) : 0);
                out += b;
            }
        }
        snprintf(b, sizeof b, "roads sent=%u in=%u added=%u failed=%u\n", g_n.roads_sent, g_n.roads_in, g_n.roads_added,
                 g_n.roads_failed);
        return out + b;
    }
    if (a.size() >= 1 && a[0] == "nouns") { // nouns [hex noun [TYPE]]: objects per noun, or that noun's objects on screen
        struct Q {
            uint32_t noun = 0, type = 0;
            std::unordered_map<uint32_t, int> count;
            std::string out;
            const SkHost* h;
        } q;
        q.h = H;
        if (a.size() >= 2)
            q.noun = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        if (a.size() >= 3)
            q.type = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto* q = (Q*)u;
                if (!q->noun) {
                    q->count[n]++;
                    return true;
                }
                net::Vec3 p;
                if (n != q->noun || !game::object_alive(o) || !game::object_position(o, p) || q->out.size() > 6000)
                    return true;
                float w[3] = {p.x, p.y, p.z}, sc[2] = {-1, -1};
                if (!q->h->world_to_screen(w, sc))
                    sc[0] = sc[1] = -1;
                char line[128];
                snprintf(line, sizeof line, "obj %p %.0f %.0f %.0f %d %d pol=%08X%s%s\n", o, p.x, p.y, p.z, (int)sc[0],
                         (int)sc[1], pol_of(o), q->type && !game::cast(o, q->type) ? " (not that type)" : "",
                         n == 0x18C6DE8 && F<uint8_t>(o, kVDead) ? " dead" : "");
                q->out += line;
                return true;
            },
            &q);
        if (q.noun)
            return q.out;
        std::string out;
        for (auto& [n, c] : q.count) {
            char line[48];
            snprintf(line, sizeof line, "noun %08X %d\n", n, c);
            out += line;
        }
        return out;
    }
    if (a.size() >= 2 && a[0] == "ruinlocal" && is_guest()) {
        // ruinlocal <host city index>: this game alone ruins one building of that city (tests, BUG-0324)
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        static const float zero[3] = {0, 0, 0};
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings))
            if (type_for_noun(game::noun_id(bl)) && F<int>(bl, kBuildingState) != 2) {
                {
                    callguard::Hold applying(g_applying);
                    thiscall<int>(a_CombatantDamage, (uint8_t*)bl + 0x120, F<float>(bl, kBuildingState + 4) + 1.f, 0xFFFFFFFFu,
                                  0, (const float*)zero, (void*)nullptr);
                }
                char b[96];
                snprintf(b, sizeof b, "building %p ruined here: state %d", bl, F<int>(bl, kBuildingState));
                return b;
            }
        return "no building to ruin";
    }
    if (a.size() >= 2 && a[0] == "buildings") { // buildings <host city index | s<i>: Space, all cities>: object, noun, state, hp, position
        int ci = atoi(a[1].c_str() + (a[1][0] == 's'));
        std::vector<void*> sc = a[1][0] == 's' ? all_cities() : std::vector<void*>();
        void* city = a[1][0] == 's' ? (ci >= 0 && ci < (int)sc.size() ? sc[ci] : nullptr)
                                    : ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        std::string out;
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings)) {
            net::Vec3 p;
            game::object_position(bl, p);
            char line[128];
            snprintf(line, sizeof line, "building %p noun=%08X state=%d hp=%.0f pos=(%.0f %.0f %.0f)\n", bl,
                     game::noun_id(bl), F<int>(bl, kBuildingState), F<float>(bl, kBuildingState + 4), p.x, p.y, p.z);
            out += line;
        }
        return out;
    }
    if (a.size() >= 2 && a[0] == "chunk") { // chunk <bytes>: smaller snapshot chunks (tests: many chunks)
        g_chunk_budget = std::clamp<size_t>((size_t)atoi(a[1].c_str()), 200, net::kMaxPacket - 48);
        g_n.max_chunk = 0;
        return "chunk budget " + std::to_string(g_chunk_budget);
    }
    if (a.size() >= 2 && a[0] == "savekill" && is_guest()) { // savekill on|off (tests, BUG-9301): the next save's end
        took_debug_kill(a[1] == "on");                         // first removes a mirror it took out (as a host snapshot)
        return took_status();
    }
    if (!a.empty() && a[0] == "civlists") {
        // civlists (tests, BUG-0470): every civilization's vehicle vector (+0xB0) and selectable vector
        // (+0xD4): entries and nulls (a stripped vehicle read back as null)
        std::string out;
        for (void* civ : vec_of(thiscall<void*>(a_GetAllCivs, nm()))) {
            int n[2] = {0, 0}, z[2] = {0, 0};
            const uintptr_t offs[2] = {0xB0, 0xD4};
            for (int k = 0; k < 2; k++) {
                void** b = F<void**>(civ, offs[k]);
                void** e = F<void**>(civ, offs[k] + 4);
                for (void** it = b; it && it < e && n[k] < 512; ++it) {
                    n[k]++;
                    z[k] += *it == nullptr;
                }
            }
            // BUG-9301: entries that are no vehicle of the game's list now (pointers only: a removed, freed one)
            int un = 0;
            g_exist_frame = ~0ull;
            for (int k = 0; k < 2; k++) {
                void** b = F<void**>(civ, offs[k]);
                void** e = F<void**>(civ, offs[k] + 4);
                int m = 0;
                for (void** it = b; it && it < e && m < 512; ++it, ++m)
                    if (*it && !exists(k == 0 ? *it : (uint8_t*)*it - kVSpatial))
                        un++;
            }
            char line[160];
            snprintf(line, sizeof line, "civ %p pol=%08X vehicles=%d nulls=%d selectable=%d nulls=%d unlisted=%d\n", civ,
                     pol_of(civ), n[0], z[0], n[1], z[1], un);
            out += line;
        }
        return out.empty() ? "no civilizations\n" : out;
    }
    if (a.size() >= 2 && a[0] == "discover") {
        // discover <hex pol> (tests, BUG-0492): the game meets that civilization outside the frame (0xC746F0
        // on the planet context, what a first click on one of its cities does: the discovery card);
        // the result is in the `planner:` status line (met=)
        static uint32_t pol;
        pol = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        if (!H->sym("SporeCoop::CivDiscovery_Meet") || !H->sym("SporeCoop::TimeOfDay_GetObject"))
            return "no symbol";
        g_met = -1;
        H->run_outside_frame(
            [](void*) {
                void* ctx = skg::cdecl_call<void*>(H->sym("SporeCoop::TimeOfDay_GetObject"));
                g_met = ctx ? (int)thiscall<char>(H->sym("SporeCoop::CivDiscovery_Meet"), ctx, &pol) : -2;
            },
            nullptr);
        char b[96];
        snprintf(b, sizeof b, "meeting %08X (civ=%p)", pol, thiscall<void*>(a_CivByPol, nm(), pol));
        return b;
    }
    if (a.size() >= 2 && a[0] == "remove" && is_host()) { // remove <id>: the game removes it (disband path, tests)
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v || !exists(v) || !a_VehicleRemove)
            return "no such vehicle";
        thiscall<void>(a_VehicleRemove, v, 0);
        g_exist_frame = ~0ull;
        return "removed";
    }
    if (a.size() >= 2 && a[0] == "kill" && is_host()) { // kill <id>: the vehicle dies (Die with effects, tests)
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v || !exists(v))
            return "no such vehicle";
        thiscall<void>(a_Die, v, 1, 0xFFFFFFFFu);
        return "died";
    }
    if (!a.empty() && a[0] == "planner_probe") {
        if (a.size() > 1)
            g_planner_probe = a[1] == "on" || a[1] == "1";
        char line[200];
        snprintf(line, sizeof line, "planner_probe %s frames=%u editor_seen=%u stale_reads=%u stale_cities=%u refused=%u fix=%d\n",
                 g_planner_probe ? "on" : "off", g_n.probe_frames, g_n.probe_seen, g_n.probe_stale_reads, g_n.probe_stale_cities,
                 g_n.planner_stale_refused, g_fix_planner_stale ? 1 : 0);
        return line;
    }
    if (!a.empty() && a[0] == "planner") {
        // planner [pick <obj>]: the open city planner (BUG-0491/0493 tests): its city, the objects it moves,
        // the buildings in the world outside every city list (orphans); pick: the planner's own pick-up
        // of that object (what a click on it does)
        std::string out;
        char line[160];
        void* pc = planner_city();
        snprintf(line, sizeof line, "planner editor=%p city=%p host_index=%d\n", g_editor, pc, pc ? host_index_of_city(pc) : -1);
        out += line;
        std::vector<void*> moved;
        planner_moved(moved);
        for (void* sp : moved) {
            void* v = sp ? (uint8_t*)sp - kVSpatial : nullptr;
            bool veh = v && exists(v);
            snprintf(line, sizeof line, "moved %p %s%s\n", sp, veh ? "vehicle" : "object",
                     veh && gv_bound(v) ? " (mirror)" : "");
            out += line;
        }
        if (pc)
            for (void* bl : vec_of((uint8_t*)pc + kCityBuildings)) {
                snprintf(line, sizeof line, "city building %p noun=%08X pick_ok=%d\n", bl, game::noun_id(bl),
                         (int)planner_pick_ok(g_editor, (uint8_t*)bl + 0x34));
                out += line;
            }
        std::unordered_set<void*> in_city;
        for (void* c : all_cities())
            for (void* bl : vec_of((uint8_t*)c + kCityBuildings))
                in_city.insert(bl);
        struct O {
            std::unordered_set<void*>* in;
            std::string* out;
        } o{&in_city, &out};
        game::for_each_object(
            [](void* x, uint32_t n, void* u) {
                auto* o = (O*)u;
                net::Vec3 p;
                if (type_for_noun(n) && game::object_alive(x) && !o->in->count(x) && game::object_position(x, p) &&
                    o->out->size() < 4000) {
                    char l[96];
                    snprintf(l, sizeof l, "orphan %p noun=%08X pos=(%.1f %.1f %.1f)\n", x, n, p.x, p.y, p.z);
                    *o->out += l;
                }
                return true;
            },
            &o);
        if (a.size() >= 3 && a[1] == "pick") {
            void* x = (void*)(uintptr_t)strtoul(a[2].c_str(), nullptr, 16);
            if (!pc || !x || !a_EditorPickUp)
                return out + "no planner open / no object\n";
            int r = thiscall<int>(a_EditorPickUp, g_editor, (uint8_t*)x + 0x34);
            snprintf(line, sizeof line, "pick -> %d held=%p\n", r, F<void*>(g_editor, 0xC0));
            out += line;
        }
        return out;
    }
    if (!a.empty() && a[0] == "scities") { // tests (BUG-0751): every city of this game (Space: s<i>), position, buildings
        std::string out;
        auto sc = all_cities();
        for (size_t i = 0; i < sc.size(); i++) {
            net::Vec3 p = city_pos(sc[i]);
            char line[128];
            snprintf(line, sizeof line, "s%u pos=(%.0f %.0f %.0f) buildings=%u\n", (unsigned)i, p.x, p.y, p.z,
                     (unsigned)vec_of((uint8_t*)sc[i] + kCityBuildings).size());
            out += line;
        }
        return out.empty() ? "no cities\n" : out;
    }
    if (a.size() >= 3 && a[0] == "unbuild" && a[1][0] == 's') {
        // unbuild s<i> <k> (tests, BUG-0751): the game's DestroyBuilding of building k of city s<i> (a free plot)
        auto sc = all_cities();
        int ci = atoi(a[1].c_str() + 1);
        void* city = ci >= 0 && ci < (int)sc.size() ? sc[ci] : nullptr;
        if (!city || !a_DestroyBuilding)
            return "no such city";
        auto bl = vec_of((uint8_t*)city + kCityBuildings);
        size_t k = (size_t)atoi(a[2].c_str());
        if (k >= bl.size())
            return "no such building";
        uint32_t noun = game::noun_id(bl[k]);
        thiscall<void>(a_DestroyBuilding, city, bl[k]);
        char l[96];
        snprintf(l, sizeof l, "destroyed building %u (%08X) of s%d", (unsigned)k, noun, ci);
        return l;
    }
    if (a.size() >= 3 && a[0] == "sbuild" && a[1][0] == 's') {
        // sbuild s<i> <house|factory|fun> (tests, BUG-0901): the mod's build_in_city into city s<i> (not paid), the
        // path of every Space colony building the mod makes (BUG-0751 / 0766 / 0884)
        auto sc = all_cities();
        int ci = atoi(a[1].c_str() + 1);
        void* city = ci >= 0 && ci < (int)sc.size() ? sc[ci] : nullptr;
        if (!city)
            return "no such city";
        uint32_t noun = a[2] == "factory" ? 0x18EA2CC : a[2] == "fun" ? 0x1A56ABA : 0x18EB106;
        void* b{};
        {
            callguard::Hold applying(g_applying);
            b = build_in_city(city, noun, city_pos(city), false);
            if (b)
                space_write_city(city); // BUG-0901: as space_build_here
        }
        char l[96];
        snprintf(l, sizeof l, "%s %08X in s%d", b ? "built" : g_build_fail, noun, ci);
        return l;
    }
    if (a.size() >= 3 && a[0] == "unslot") {
        // unslot <host city index> <k> (tests, BUG-0491): the state an orphan has: building k of the city
        // leaves its layout slot (cLayoutSlot::RemoveObject) and stays in the world
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        auto bl = vec_of((uint8_t*)city + kCityBuildings);
        size_t k = (size_t)atoi(a[2].c_str());
        uint8_t* b = F<uint8_t*>(city, kCitySlots);
        uint8_t* e = F<uint8_t*>(city, kCitySlots + 4);
        if (k >= bl.size())
            return "no such building";
        for (uint8_t* s = b; s && s + 0x18 <= e; s += 0x18)
            if (F<void*>(s, 4) == bl[k]) {
                F<uint8_t>(s, 0) = 0; // the slot's flag and occupant as RemoveObject leaves them
                F<int>(s, 8) = 0;
                F<void*>(s, 4) = nullptr; // the city's list keeps its reference
                char l[96];
                snprintf(l, sizeof l, "building %p out of its slot", bl[k]);
                return l;
            }
        return "that building is in no slot";
    }
    if (a.size() >= 3 && a[0] == "vscale") { // vscale <net id> <scale> (tests, BUG-0549): this game's vehicle of that id
        uint16_t id = (uint16_t)atoi(a[1].c_str());
        void* v = nullptr;
        if (is_host())
            v = hv_by_id(id).v;
        else
            v = veh_mirror(id);
        if (!v)
            return "no such vehicle";
        game::set_object_scale(v, (float)atof(a[2].c_str()));
        char l[64];
        snprintf(l, sizeof l, "vehicle %u scale %.2f", (unsigned)id, game::object_scale(v));
        return l;
    }
    if (a.size() >= 6 && a[0] == "bmove") { // bmove <city> <k> <x> <y> <z> (tests, BUG-0602): building k of the city to a spot, its slot kept
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        auto bl = vec_of((uint8_t*)city + kCityBuildings);
        size_t k = (size_t)atoi(a[2].c_str());
        if (k >= bl.size())
            return "no such building";
        float q[3] = {(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
        vcall<void>((uint8_t*)bl[k] + 0x34, 0x38 / 4, (const float*)q);
        net::Vec3 bp{};
        game::object_position(bl[k], bp);
        char l[128];
        snprintf(l, sizeof l, "building %08X at (%.1f %.1f %.1f)", game::noun_id(bl[k]), bp.x, bp.y, bp.z);
        return l;
    }
    if (a.size() >= 2 && a[0] == "blist") { // blist <host city index>: the game's buildings of the city (BUG-0602)
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        std::string out;
        uint8_t* sb = F<uint8_t*>(city, kCitySlots);
        uint8_t* se = F<uint8_t*>(city, kCitySlots + 4);
        for (void* b : vec_of((uint8_t*)city + kCityBuildings)) {
            net::Vec3 p{};
            game::object_position(b, p);
            int slot = -1;
            for (int k = 0; sb && sb + (k + 1) * 0x18 <= se && k < 64; k++)
                if (F<void*>(sb + k * 0x18, 4) == b)
                    slot = k;
            char line[160];
            snprintf(line, sizeof line, "b %08X pos=(%.1f %.1f %.1f) state=%d slot=%d\n", game::noun_id(b), p.x, p.y, p.z,
                     F<int>(b, kBuildingState), slot);
            out += line;
        }
        return out;
    }
    if (a.size() >= 2 && a[0] == "slots") { // slots <host city index | s<i>: Space>: building plots, where they are on screen
        int ci = atoi(a[1].c_str() + (a[1][0] == 's'));
        std::vector<void*> sc = a[1][0] == 's' ? all_cities() : std::vector<void*>();
        void* city = a[1][0] == 's' ? (ci >= 0 && ci < (int)sc.size() ? sc[ci] : nullptr)
                                    : ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        std::string out;
        uint8_t* b = F<uint8_t*>(city, kCitySlots);
        uint8_t* e = F<uint8_t*>(city, kCitySlots + 4);
        for (int k = 0; b && b + (k + 1) * 0x18 <= e && k < 32; k++) {
            uint8_t* s = b + k * 0x18;
            float* p = (float*)(s + 0xC);
            float sc[2] = {-1, -1};
            if (!H->world_to_screen(p, sc))
                sc[0] = sc[1] = -1;
            char line[128];
            snprintf(line, sizeof line, "slot %d %s %.0f %.0f %.0f %d %d\n", k, *(void**)(s + 4) ? "busy" : "free", p[0],
                     p[1], p[2], (int)sc[0], (int)sc[1]);
            out += line;
        }
        return out;
    }
    return "usage: coop.civ select <id> <0|1> | move <id> <x> <y> <z> | buy <city> <0 land|1 water|2 air> | "
           "build <city> <house|factory|fun> | slots <city>";
}

} // namespace civ
